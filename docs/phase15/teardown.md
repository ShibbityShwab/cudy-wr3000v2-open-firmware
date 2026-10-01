# teardown: the second fault (plat dispatching a freed wifi callback), what the race reaches, and why the stack still will not unload (phase 15, 2026-10-01)

**Result. The second fault is bypassed, but the stack cannot be unloaded cleanly.** The race from
the brief (unload wifi and plat back-to-back in one shell command) does remove both modules:
`rmmod hi5622v100_wifi` and `rmmod hi5622v100_plat` both return `0`, both leave `lsmod`, and the
wifi stale-callback fault documented in `../unload-patch.md` does not recur. The box then faults
(attempt 1: pstore record; attempt 2: a reset with no pstore record) on **hi5622v100_plat's own
incomplete teardown**, ~1.8 s after `rmmod plat`, and the watchdog reboots it. `hi5622v100_plat`'s
`module_exit` never unregisters the PCI driver `rox_pci0`, never stops its `Host MSG RX` /
`pcie_thread` kthreads (`kthread_stop` is not linked anywhere in the module), and never cancels its
pending hrtimers - so after the unload the endpoints are still bound to `rox_pci0`, a plat hrtimer
node in `.bss` is still queued in the core timerqueue, and the PCIe ISR is still installed; the
first of those to fire walks freed module memory.

The deepest point reached is therefore: **both modules out of `lsmod` (rc=0) and the wifi-side
stale callback gone, but the endpoints are NOT freed** (`rox_pci0` survives, both endpoints stay
bound) and the box does not survive. Router ends healthy: watchdog reboot, patched module
re-loaded from the overlay, both radios and all AP VAPs back, endpoint driver re-bound,
`dmesg` clean for the new boot.

Two reboots were spent: attempt 1 produced the pstore record below; attempt 2 reproduced the
unload and the endpoint evidence. Recovery is automatic (watchdog), and the manual recovery is in
section 0.

---

## 0. Recovery - written down BEFORE the first reboot (required)

Boot-path module copies are what kmodloader resolves (it matches by basename in
`/lib/modules/$(uname -r)/`). The pristine originals always exist in the read-only lower:

    /rom/lib/modules/5.10.201/hi5622v100_wifi.ko   md5 4737fcb21a1a2262a96f84d780ad8b35
    /rom/lib/modules/5.10.201/hi5622v100_plat.ko   md5 23660bc285393e678d5cade1c36c194b

State at the start of this run (and at the end): wifi is the patch-#1 module from
`../unload-patch.md`, plat is stock.

    /lib/modules/5.10.201/hi5622v100_wifi.ko   md5 e21629d226ec7de9a860a8955952d311  (patched #1)
    /lib/modules/5.10.201/hi5622v100_plat.ko   md5 23660bc285393e678d5cade1c36c194b  (stock)

Recovery (safe; a no-op for any module not patched) - restore stock from the read-only lower and
reboot:

    cp /rom/lib/modules/5.10.201/hi5622v100_wifi.ko /lib/modules/5.10.201/hi5622v100_wifi.ko
    cp /rom/lib/modules/5.10.201/hi5622v100_plat.ko /lib/modules/5.10.201/hi5622v100_plat.ko
    sync; reboot

Do not `rm` the merged path: with overlayfs that creates a whiteout hiding the lower file and
leaves no module to load.

Deeper recovery if the box does not come back: the board is dual-slot; boot the other firmware
slot from U-Boot ("slot A"). The vendor stack re-probes both endpoints on any clean boot and both
radios return, so a watchdog/panic reboot is itself a recovery.

Originals are also kept on the PC:

    build/register-dumps/unload-patch/hi5622v100_wifi.ko.orig   md5 4737fcb2...
    build/register-dumps/teardown/hi5622v100_plat.ko.rom        md5 23660bc2...

---

## 1. The second fault, re-derived from the module files

Panic record from `../unload-patch.md` section 7 (pstore `dmesg-pstore_blk-1`):

    CPU: 1 PID: 1170 Comm: Host MSG RX
    PC is at 0xbfa5ff0c          LR is at hcc_queue_msg_process+0xdc/0x134 [hi5622v100_plat]
    r3 : bfa5ff0c
    [<bf919b60>] (hcc_queue_msg_process [hi5622v100_plat]) from [<bf91a28c>] (hcc_process_rx_thread+0x148/0x15c)

Module bases (confirmed again this run): `hi5622v100_plat .text = 0xbf907000`,
`hi5622v100_wifi .text = 0xbf969000`. So `hcc_queue_msg_process = 0xbf907000 + 0x12a84 =
0xbf919a84`, and the panic `LR` `0xbf919b60` is exactly `+0xdc`. `PC` `0xbfa5ff0c - 0xbf969000 =
0xf6f0c = hmac_pfm_detect_process_event+0x0` [hi5622v100_wifi] - a stale pointer into the freed
wifi image.

### 1.1 The exact dispatch (`hi5622v100_plat.ko`)

`hcc_queue_msg_process` (`vaddr 0x12a84`, size `0x134`, file offset `0x12abc`):

    +0x0d8  ebfffffe  bl hcc_msg_process   ; <-- sets LR = +0xdc (the panic LR)
    +0x0dc  e595303c  ldr r3, [r5, #0x3c]

`hcc_msg_process` (`vaddr 0x1204c`, size `0xb0`) is a **tail-call dispatcher**; its last
instruction is `bx r3` and it never touches LR, which is why the panic LR stayed `+0xdc`:

    +0x02c  ldr r1, [r3, #0x10]   ; r3 = group/queue;  r1 = handler table
    +0x030  cmp r1, #0
    +0x034  bxeq lr               ; NULL table -> return safely
    +0x038  ldrb r3, [r2]
    +0x058  add ip, r1, r3        ; entry = table + idx*12
    +0x068  ldr r3, [ip, #4]      ; entry->[4] = per-id handler array
    +0x06c  adds r3, r3, r2, lsl #4
    +0x074  ldr r3, [r3, #4]      ; r3 = HANDLER
    +0x078  cmp r3, #0
    +0x07c  bxeq lr
    +0x080  bx r3                 ; <-- tail call into the freed wifi image

### 1.2 Where the stale handler came from - and why nothing removes it

`hmac_pfm_detect_process_event` is an `R_ARM_ABS32` relocation at `hi5622v100_wifi.ko`
`.data + 0x6cc` - one entry of the wifi driver's event table (12-byte entries from `.data 0x584`
on). wifi's init hands that table to plat:

    hdpp_main_init+0x7c   bl hcc_msg_register_tab_chip
    hmac_main_init+0xb0   bl hcc_msg_register_tab_chip
    hmac_main_init+0xc0   bl hcc_msg_register_tab_chip
    hmac_main_init+0xd0   bl hcc_msg_register_tab_chip
    wal_main_init+0x34    bl hcc_msg_register_tab_chip

`hcc_msg_register_tab_chip/_core` -> `hcc_msg_register_tab_customise` (`plat vaddr 0x118c4`)
**copies the handler pointers out of wifi's `.data` into plat's own group structure**
(`hcc_get_group_res(group)+0x10`); the pointers therefore live in plat's memory, not wifi's, after
registration. Freeing wifi's image does not invalidate them - that is the bug.

**plat exports no unregister for this table.** Parsing `hi5622v100_plat.ko`'s `__ksymtab` /
`__ksymtab_gpl` (299 exports) gives `hcc_msg_register_tab`, `hcc_msg_register_tab_customise`,
`hcc_msg_register_tab_chip`, `hcc_msg_register_tab_core`, `hcc_queue_register_customer*`,
`hcc_timer_*`, `hcc_set_state`, `hcc_get_state`, ... and **no `hcc_msg_unregister_tab*` and no
`hcc_queue_unregister_customer*`**. wifi's `hmac_main_exit` (its module exit) calls
`hdpp_main_exit`, `hmac_pfm_exit`, `hcc_timer_unregister_set(2)`, `hmac_acs_exit`, ...,
`hcc_set_state(1)`, ... but nothing that could remove the copied handlers. The vendor teardown is
missing this step. (`hcc_set_state` is a subsystem-ready bitmask - values 1, 2, 4 are stored by
different init/exit paths - not a dispatch gate: `hcc_msg_process` and `hcc_queue_msg_process`
never read it.)

### 1.3 Approach 2 and approach 3 are not implementable as minimal patches

This is recorded so the attempt log is read correctly; both were worked through from the
disassembly before falling back to the race.

- **Approach 2 ("patch wifi's exit to deregister its plat callback")** needs a plat unregister to
  call. There is none (section 1.2). The only "remove" primitive is to re-run
  `hcc_msg_register_tab_customise` with a zeroed source table for the same indices, which is a
  multi-call patch (three `_chip` registrations across `hdpp_main_init`, `hmac_main_init`,
  `wal_main_init`), needs a zeroed table built in wifi's `.data`, and needs a *new* import
  relocation for a function wifi does not currently call from its exit. Not a minimal in-place
  edit.
- **Approach 3 ("guard/divert plat's call site")** cannot be made safe from the disassembly.
  The only thing that makes the handler call stop is a NULL table pointer (checked at
  `hcc_msg_process+0x30`) or a NULL handler (checked at `+0x78`) - neither address is reachable
  from wifi. A "is this pointer still in a live module" test needs `__module_text_address()`,
  which plat does not import and which cannot be added without rebuilding the module's
  `.rel.text`/`.symtab`. Guarding on "handler not inside plat `.text`" would break normal
  operation, because the whole point of the table is that wifi's handlers live outside plat.
  A hardcoded range test cannot tell a live module from a freed one (all modules are in the same
  0xbf000000-0xbfe00000 window). So no plat call-site patch is applied; doing so would be a
  break-normal-operation change I cannot argue.

No new patch is therefore applied in this document. The prerequisite is patch #1 from
`../unload-patch.md` (still staged): `hmac_softnp_user_clear_proc` file offset `0xf90cc`,
`e12fff33` -> `ea00000d`; staged module md5 `e21629d2...`.

---

## 2. Attempt 1 - the race (one reboot)

Command (traffic paths down, then both `rmmod`s in a single shell so there is no SSH round trip
between them), captured in `100_attempt1_race.txt`:

    wifi down ; /etc/init.d/softapd stop ; kill app_acs app_nlc ; /etc/init.d/wpad stop
    rmmod hi5622v100_wifi ; rmmod hi5622v100_plat

Observed:

    refcnts after teardown : hi5622v100_wifi 1   hi5622v100_plat 2
    rmmod hi5622v100_wifi  -> rc=0
    rmmod hi5622v100_plat  -> rc=0
    lsmod immediate        : no hi5622v100_wifi, no hi5622v100_plat  (only hi_pcie of the chain)
    uptime immediate       : 10:34:36 up 8 min
    (8 s later, the SSH re-check: "connect to host 192.168.10.1 port 22: Connection timed out")

The box came back on its own after ~40 s (watchdog). New pstore record `dmesg-pstore_blk-3`
(pulled to `pstore_attempt1_blk-3.txt`):

    [  515.537184] DFR: deinit ok.
    [  515.537210] [HIWIFI]hi_wifi_tx_hook para func is null
    [  515.542317] hmac_softnp_woe_hook_exit: succ          <-- wifi exit completes
    [  515.546574] swa_netdevice_event 215:event:6, name:Hisilicon0
    [  515.552790] [HSLINK]dev(Hisilicon0) is destroy
    [  517.321624] 8<--- cut here ---
    [  517.324679] Unable to handle kernel paging request at virtual address bf942cc8
    [  517.340809] Internal error: Oops: 7 [#1] SMP ARM
    ... Modules linked in: ... hi_pcie(O) ... [last unloaded: hi5622v100_plat]
    [  517.582263] CPU: 1 PID: 0 Comm: swapper/1
    [  517.592842] PC is at rb_erase+0xec/0x380
    [  517.654910] 8<--- cut here ---
    [  517.663291] Unable to handle kernel paging request at virtual address bf90f54c
    [  517.780002] [<c0330d94>] (rb_erase) from [<c0335b0c>] (timerqueue_del+0x30/0x88)
    [  517.787373] [<c0335b0c>] (timerqueue_del) from [<c009b964>] (__hrtimer_run_queues+0x158/0x358)
    [  517.795948] [<c009b964>] (__hrtimer_run_queues) from [<c009c2e8>] (hrtimer_interrupt+0x120/0x344)

**Reading of the record.** The wifi exit lines are identical to the clean attempt in
`../unload-patch.md` - the wifi stale-callback fault (`PC = hmac_pfm_detect_process_event`) did
**not** happen; the race took plat out before its `Host MSG RX` thread could dispatch to the freed
wifi table. What faults instead is plat's own leftover state, 1.8 s later (515.54 -> 517.32, the
same ~2 s cadence as the old fault):

- fault address `0xbf942cc8` - module offset `0x3bcc8`, i.e. **hi5622v100_plat `.bss + 0x3d48`**
  (section list from the live device: `.bss = 0xbf93ef80`). No symbol covers it: an anonymous
  `struct hrtimer` still linked in the core timerqueue after the module was freed, so
  `hrtimer_interrupt -> __hrtimer_run_queues -> timerqueue_del -> rb_erase` walks freed memory.
- second fault `0xbf90f54c` - module offset `0x854c` = **`oal_pcie_intx_isr`** [hi5622v100_plat],
  i.e. the PCIe ISR is still installed after the module that owns it is gone.

So the race gets past fault #2 and hits fault #3: plat's `module_exit` does not cancel its
timers, stop its threads, or unregister its PCI driver.

## 3. Attempt 2 - the race, plus an endpoint capture (one reboot)

Same sequence, but the endpoint/driver state was read in the same shell immediately after the two
`rmmod`s (`110_attempt2_race_endpoints.txt`):

    refcnts after teardown : hi5622v100_wifi 0   hi5622v100_plat 1
    rmmod hi5622v100_wifi  -> rc=0
    rmmod hi5622v100_plat  -> rc=0
    lsmod                  : (no hi5622v100 modules)
    endpoint drivers       : 0000:00:00.0 -> .../bus/pci/drivers/rox_pci0
                             0001:00:00.0 -> .../bus/pci/drivers/rox_pci0
    rox_pci0 dir           : /sys/bus/pci/drivers/rox_pci0   (still present)
    dmesg tail             : ... [HSLINK]dev(Hisilicon0) is destroy     (no oops yet)

Immediately afterwards the box dropped off the network and returned with `uptime` ~0
(`/proc/uptime` 64 s on the next check). **No new pstore record was written** (`/sys/fs/pstore`
still holds only the pre-run `blk-2` and attempt-1 `blk-3`), i.e. this reset was a hang/watchdog
reset rather than a captured oops.

**The important observation is the endpoints.** Even with both modules gone from `lsmod`,
`rox_pci0` and both endpoint bindings were still there. `hi5622v100_plat.ko`'s exit never
unregisters the driver:

- `cleanup_module` (`plat vaddr 0x1a828`, size 4) is only `b hcc_exit`.
- `hcc_exit` (`0x111d0`, size 0x1c) is only `bl hcc_timer_module_exit` + `strb state, 0`.
- The module's single `pci_unregister_driver` reference is a jump at `.text+0x82d8` (tail call
  after `r0 = <driver struct>` at `0x82d0`), which is **not reachable from `cleanup_module`**.
- There is no `kthread_stop` relocation anywhere in the module, although `hcc_process_rx_thread`
  and `pcie_process_thread` both loop on `kthread_should_stop`.
- `hcc_timer_module_exit` does not cancel hrtimers; it only re-initialises the `g_hrtimer_list`
  list heads in `.bss` (`str`/`str` around `0x13ed0`), which is exactly how attempt 1's timer node
  stayed queued in the core timerqueue.

So after `rmmod hi5622v100_plat` the endpoints are **not** free: `rox_pci0` is still registered
with the PCI core and both devices stay bound to it, its ISR is still installed, and its timer is
still queued - all pointing into freed module memory.

---

## 4. The unload sequence that reaches the deepest point

This is the sequence that actually removes the stack (deepest reachable state):

    # prerequisites: patch #1 staged at /lib/modules/5.10.201/hi5622v100_wifi.ko (md5 e21629d2...)
    wifi down
    /etc/init.d/softapd stop
    kill $(pgrep app_acs) ; kill $(pgrep app_nlc)
    /etc/init.d/wpad stop

    # the race: both in one shell command so there is no gap for the Host MSG RX thread
    rmmod hi5622v100_wifi ; rmmod hi5622v100_plat

Result at the moment both commands return: `hi5622v100_wifi` rc=0, `hi5622v100_plat` rc=0, both
gone from `lsmod`. The wifi stale-callback fault is bypassed. ~1.8 s later plat's orphaned hrtimer
/ ISR fault in the freed image, the watchdog reboots, and the vendor stack comes back.

`hi_pcie` (use count 0, not the endpoint driver) was deliberately left loaded, before and after.

## 5. Deepest point reached / why "fully unloaded" is out of reach

Reached:

- patch #1 still does its job: `rmmod hi5622v100_wifi` completes (rc=0), no `PC=0x0`.
- the race removes **both** `hi5622v100_wifi` and `hi5622v100_plat` from `lsmod` (rc=0 each) and
  suppresses the wifi stale-callback fault;
- the wifi module's own exit is clean (same lines as the earlier run, no oops at that point).

Blocked by hi5622v100_plat, i.e. the remaining faults are not cross-module callbacks any more:

- its `module_exit` (`cleanup_module -> hcc_exit`) does not unregister `rox_pci0`, so the
  endpoints are not released (observed in attempt 2: driver and both bindings survive the unload);
- it does not stop its kthreads (no `kthread_stop` link), so `Host MSG RX` / `pcie_thread` resume
  in freed memory;
- it does not cancel its hrtimers, so a node left in `.bss` stays in the core timerqueue
  (attempt 1: `rb_erase` in `hrtimer_interrupt` at `.bss+0x3d48`).

A clean full unload would require rewriting `hi5622v100_plat`'s teardown (PCI unregister + kthread
stop + hrtimer cancel, and the missing hcc message-table deregistration API), not a minimal
in-place patch. That is beyond the "module patches and their staging" scope of this run, and there
is no unregister entry point to hook. The deepest reachable point is therefore the race above,
which is what is documented, with the panic record in section 2.

## 6. Final health (acceptance)

`200_final_health.txt`, captured after attempt 2's watchdog reboot with the patched module
re-loaded from the overlay:

    staged modules        : wifi e21629d2... (patched #1) ; plat 23660bc2... (stock)
    uptime                : up 2 min
    modules               : hi5622v100_wifi refcnt=1 ; hi5622v100_plat refcnt=3 ; hi_pcie refcnt=0
    endpoints             : 0000:00:00.0, 0001:00:00.0 -> rox_pci0 (rox_pci0/module -> hi5622v100_plat)
    radios                : Wiphy phy0 (Band 1), Wiphy phy1 (Band 2)
    interfaces            : 6 (vap0/vap3/vap8/vap11 AP, vap1/vap9 managed)
    br-lan                : eth0, eth1, eth2, vap0, vap3, vap8, vap11
    calibration           : alg get_2g_power_param [SUCC] ; alg get_5g_power_param [SUCC]
    oops since this boot  : 0
    pstore                : blk-2 (pre-run) + blk-3 (attempt 1) only

**Acceptance reached: the router ends healthy with the patched module staged, both radios up, all
four AP VAPs in `br-lan`, and both calibration commands answering `[SUCC]`.** The vendor stack was unloaded twice (both modules out of `lsmod`); the second fault was
bypassed; the next fault is plat's own, and it is documented with its panic record.

## 7. Artifacts

| file | what it is |
| --- | --- |
| `000_baseline.txt` | pre-run device state (md5s, refcnts, endpoints, iw, section bases) |
| `plat_sections.txt` | live `/sys/module/hi5622v100_plat/sections/*` bases used to map fault addresses |
| `100_attempt1_race.txt` | attempt 1 command + output (both `rmmod`s rc=0, box gone 8 s later) |
| `pstore_attempt1_blk-3.txt` | attempt 1's pstore record (freed-plat hrtimer fault, `[last unloaded: hi5622v100_plat]`) |
| `110_attempt2_race_endpoints.txt` | attempt 2 command + output (both `rmmod`s rc=0, `rox_pci0` and both endpoint bindings still present) |
| `120_attempt2_postreboot.txt` | post-attempt-2 check (no new pstore record) |
| `200_final_health.txt` | final acceptance capture |
| `disasm_plat_call.py` | locates `hcc_queue_msg_process` and resolves the stale-callback call site |
| `dumpfn.py` | full function disassembly with correct per-section relocations |
| `callers.py` | caller finder used for the registration/exit/register-tab analysis |
| `find_sym.py`, `refs.py` | symbol/relocation helpers |
| `hi5622v100_plat.ko` | plat module pulled from the device (== `/rom` copy, md5 23660bc2...) |
| `hi5622v100_plat.ko.rom` | plat module from `/rom` (pristine) |

Related: `../unload-patch.md` (patch #1 and the first fault), `../bringup.md`,
`../takeover-prep.md`.
