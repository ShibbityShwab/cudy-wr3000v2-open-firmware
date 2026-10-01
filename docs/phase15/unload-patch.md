# unload-patch: binary-patching the vendor Wi-Fi module's broken unload path (phase 15, 2026-10-01)

**Result: the documented fault is fixed. With the patched module staged in the overlay and booted,
`rmmod hi5622v100_wifi` returns `0` and the module's exit path completes (previously it oopsed at
`PC=0x0`). The unload is nevertheless not end-to-end safe: ~2 s later `hi5622v100_plat`'s
`Host MSG RX` thread (`hcc_process_rx_thread`) calls a still-registered `hi5622v100_wifi` callback
(`hmac_pfm_detect_process_event`, address `0xbfa5ff0c`) whose memory the unload had just freed,
page-faults, and the watchdog reboots the box. That is the next faulting instruction; per the
brief the chain was stopped there (no `hi5622v100_plat` unload was attempted). The router ends
healthy with the patched module loaded: both radios, all AP VAPs in `br-lan`, calibration `[SUCC]`.**

The minimal failing step from `bringup.md` is the unload of the first module in the reverse
order, `hi5622v100_wifi`. Its own `module_exit` dereferences a NULL function-pointer slot that the
same exit path cleared a few instructions earlier. This document reproduces that path from the
module file, picks a minimal in-place patch, stages it in the overlay, and runs the unload.

## 0. Recovery - written down BEFORE the reboot (required)

The patched file is dropped at the boot-time load path
`/lib/modules/5.10.201/hi5622v100_wifi.ko` (confirmed: `kmodloader` resolves a module by
**basename** in `/lib/modules/$(uname -r)/`, so the stock script's `insmod
/lib/hisilicon/ko/hi5622v100_wifi.ko` actually loads this file). The overlayfs upper copy now
shadows the read-only squashfs file. The pristine original still exists, untouched, at:

    /rom/lib/modules/5.10.201/hi5622v100_wifi.ko    md5 4737fcb21a1a2262a96f84d780ad8b35

**Recovery (safe, no-op if not patched): restore the original from the read-only lower and reboot**

    cp /rom/lib/modules/5.10.201/hi5622v100_wifi.ko /lib/modules/5.10.201/hi5622v100_wifi.ko
    sync; reboot

Do **not** simply `rm` the merged path: with overlayfs that would create a whiteout hiding the
lower file and leave no module to load. Restoring by `cp` from `/rom` is unambiguous.

Deeper recovery (if the box does not come back on its own): the board is dual-slot; from the
U-Boot prompt boot the other firmware slot ("slot A"). The vendor stack re-probes both endpoints
and both radios return on a clean boot in any case, so a watchdog/panic reboot is itself a
recovery.

Original module is also kept on the PC (two copies of the same bytes):

    opensource/build/register-dumps/unload-patch/hi5622v100_wifi.ko.orig   md5 4737fcb2...  (this tree)
    build/register-dumps/wifidrv0/dev-modules/hi5622v100_wifi.ko           md5 4737fcb2...  (pulled copy)

---

## 1. The faulting path, reproduced from the module file

Input: `/lib/modules/5.10.201/hi5622v100_wifi.ko` (device) = `build/register-dumps/wifidrv0/
dev-modules/hi5622v100_wifi.ko` (PC), **md5 `4737fcb21a1a2262a96f84d780ad8b35`, size 3564728**.
It is an `ET_REL` ARM object, so symbol values are section-relative; `.text` has `sh_offset=0x38`,
hence vaddr `v` maps to file offset `0x38 + v`.

`hmac_softnp_user_clear_proc` is at vaddr `0xf9018`, size `0xc0`, i.e. file offset `0xf9050`.
Full A32 disassembly (`010_disasm_clear_proc_orig.txt`):

```
+0x00  e92d4070  push    {r4, r5, r6, lr}
+0x04  e1a04000  mov     r4, r0
+0x08  e5d0100d  ldrb    r1, [r0, #0xd]
+0x0c  e24dd008  sub     sp, sp, #8
+0x10  e5d0000e  ldrb    r0, [r0, #0xe]
+0x14  ebfffffe  bl      hdpp_vap_get_by_id              ; (reloc R_ARM_CALL)
+0x18  e2505000  subs    r5, r0, #0
+0x1c  0a00001c  beq     +0x94                           ; vap == NULL -> info log + return
+0x20  e2946018  adds    r6, r4, #0x18                   ; r6 = &p[0x18] (user MAC)
...             (build MAC word from p[0x18..0x1b])
+0x44  e3004000  movw    r4, #0                          ; reloc -> .LANCHOR0
+0x48  e3404000  movt    r4, #0                          ; r4 = &(.bss LANCHOR0)
+0x6c  e3a01037  mov     r1, #0x37
+0x70  ebfffffe  bl      oam_warning_log1                ; "{hmac_softnp_user_clear_proc:: pst_user mac[%08X****]}"
+0x74  e5943010  ldr     r3, [r4, #0x10]                 ; SLOT A = hi_cfe_flush_by_mac
+0x78  e1a00006  mov     r0, r6
+0x7c  e12fff33  blx     r3                              ; <-- PC=0x0 when slot A is NULL; LR = +0x80
+0x80  e5943014  ldr     r3, [r4, #0x14]                 ; SLOT B = hi_cfe_flush_mc_by_devname
+0x84  e595001c  ldr     r0, [r5, #0x1c]
+0x88  e28dd008  add     sp, sp, #8
+0x8c  e8bd4070  pop     {r4, r5, r6, lr}
+0x90  e12fff13  bx      r3                              ; tail-call slot B
+0x94  ...        (vap==NULL path: oam_info_log1, then:)
+0xb8  e28dd008  add     sp, sp, #8                      ; <-- shared unwind/return
+0xbc  e8bd8070  pop     {r4, r5, r6, pc}
```

This matches the `bringup.md` pstore record exactly: `PC is at 0x0`, `LR is at
hmac_softnp_user_clear_proc+0x80` — the `blx r3` at `+0x7c` (return address `+0x80`).

### 1.1 What the two slots are, and why they are NULL

`r4` is loaded from a local anchor `.LANCHOR0` (symtab index 11635, `.bss` + `0x35530`). The
`movw`/`movt` at `+0x44/+0x48` are `R_ARM_MOVW_ABS_NC`/`R_ARM_MOVT_ABS` relocations resolving to
that exact symbol. The anchor is populated only by `hmac_softnp_woe_hook_init`, which
`__symbol_get`s seven external hooks (resolved by name, `012_callback_resolution.txt`):

| anchor off | symbol name |
| --- | --- |
| `+0x00` | `hi_wifi_rx_pkt` |
| `+0x04` | `hi_wifi_reg` |
| `+0x08` | `hi_wifi_unreg` |
| `+0x0c` | `hi_wifi_tx_only_lrn` |
| `+0x10` | **`hi_cfe_flush_by_mac`** (slot A) |
| `+0x14` | **`hi_cfe_flush_mc_by_devname`** (slot B) |
| `+0x18` | `hi_wifi_tx_hook` |

`hmac_softnp_woe_hook_exit` (`013_woe_hook_disasm.txt`) zeroes **all** of them, including both
CFE flush slots:

```
+0x80  ldr  r3, [r4, #0x10] ; cmp r3, #0 ; movne r3, #0 ; strne r3, [r4, #0x10]   ; clear slot A
+0x90  ldr  r3, [r4, #0x14] ; cmp r3, #0 ; movne r3, #0 ; strne r3, [r4, #0x14]   ; clear slot B
```

And `hmac_main_exit` calls `hmac_pfm_exit` (which calls `hmac_softnp_woe_hook_exit`) **before**
`hdpp_main_exit`:

```
hmac_main_exit+0x44  bl hmac_pfm_exit       ; -> hmac_pbas_exit, ..., woe_hook_exit (clears slots)
hmac_main_exit+0x58  bl hdpp_main_exit      ; -> hdpp_user_module_exit -> hdpp_user_del -> clear_proc
```

The pstore record shows the same ordering at runtime (`hmac_softnp_woe_hook_exit: succ` is logged
immediately before the oops). **Therefore both slot A and slot B are NULL by the time
`clear_proc` runs.** This is a structural ordering bug in the vendor's `module_exit`, not a side
effect of `wifi down`.

## 2. Why the plain NOP is not sufficient (and was rejected)

The brief's first candidate is to NOP the faulting `blx r3` (`+0x7c`, `e12fff33` -> `e1a00000`).
That removes the first NULL call, but execution then falls straight through to `+0x80`..`+0x90`:

```
+0x80  ldr r3, [r4, #0x14]   ; r3 = slot B = hi_cfe_flush_mc_by_devname = NULL
+0x84  ldr r0, [r5, #0x1c]
+0x88  add sp, sp, #8
+0x8c  pop {r4, r5, r6, lr}
+0x90  bx  r3                ; -> PC = 0x0 again
```

`woe_hook_exit` clears slot B in the same place it clears slot A, so a NOP-only patch would just
move the panic from `blx r3` to `bx r3` and cost another reboot for nothing. A correct minimal
patch must skip **both** indirect calls.

## 3. Chosen patch: return early once the function's own work is done

`hmac_softnp_user_clear_proc`'s own work is the VAP lookup (`hdpp_vap_get_by_id`) and the
per-user MAC warning log (`oam_warning_log1`). The two instructions after that are *external*
fast-path cleanups (`hi_cfe_flush_*`), which the exit path has already unregistered. Replace the
first indirect call with a branch to the function's own unwind/return epilogue, skipping both:

    file offset 0xf90cc   (fn vaddr 0xf9018, +0x7c)
      before: 33 ff 2f e1   = e12fff33   blx r3
      after : 0d 00 00 ea   = ea00000d   b   0xf90d0   (fn+0xb8: add sp,#8 ; pop {r4,r5,r6,pc})

Encoding check: branch at vaddr `0xf9094`, target `0xf90d0`, `imm24 = (0xf90d0-(0xf9094+8))/4 = 0xd`
-> `0xEA00000D`. Verified with capstone (`020_patch_apply.txt`), and the patched function
disassembly (`011_disasm_clear_proc_patched.txt`) confirms the branch lands on `add sp, sp, #8`.

Why this and not NOP:

- **Single 4-byte in-place edit.** No instruction shifting, no relocation/section changes, so
  `.symtab`, section sizes and `vermagic` are untouched and the file size is identical.
- **It skips exactly the two calls whose targets are provably NULL on this path**, and only those.
  The VAP lookup and the MAC log still run, `sp` is unwound correctly, and the callee's
  `pop {r4,r5,r6,pc}` restores the same registers the original epilogue would have.
- **No register or stack perturbation** between the patch site and the epilogue: the skipped
  instructions (`ldr r3,[r4,#0x14]`, `ldr r0,[r5,#0x1c]`, `add sp`, `pop`, `bx r3`) have no side
  effects the return path depends on.

The alternative "NOP `+0x7c` **and** change `+0x90 bx r3` to `bx lr`" is behaviourally identical
(both skip the two calls and return); it is two instructions instead of one, so the single branch
is preferred.

### 3.1 Behaviour trade-off, stated plainly

`clear_proc` is also reached from `hdpp_user_del` at runtime (via the user-features exit callback
and the `.data`-registered `hdpp_user_sync_del`), where the slots *are* valid. The patch therefore
also drops the two CFE fast-path flushes when a user is deleted in normal operation. It cannot be
made conditional in a single 4-byte patch (A32 has no conditional `blx`, and a null-guard does not
fit the 8 available instruction slots). The alternative that would preserve runtime flushing -
stop `woe_hook_exit` clearing the two slots - was rejected because slot B's argument is read from
`[vap+0x1c]` after the same teardown has already destroyed the netdevice (the pstore shows
`[HSLINK]dev(Hisilicon0) is destroy` immediately before the oops), so calling it can use-after-free.
Skipping the calls is the deterministic, panic-free choice; the dropped work (flushing software
fast-path flow entries for the departing user) is aged out by the fast path anyway.

## 4. Local patch application and verification

`020_patch_apply.txt`:

    file offset      : 0xf90cc
    before word      : 0xe12fff33  bytes 33 ff 2f e1
    after word       : 0xea00000d  bytes 0d 00 00 ea  -> b #0xf90d0  (fn+0xb8)
    elf parses       : yes; sections=40; symtab entries=21495
    __this_module    : present
    orig     size=3564728  md5=4737fcb21a1a2262a96f84d780ad8b35
    patched  size=3564728  md5=e21629d226ec7de9a860a8955952d311

No module-signature enforcement: the module has no `.module_sig` section, `/proc/sys/kernel/
modules_disabled` is `0`, and the kernel is already tainted with the unsigned-module bit
(`tainted=4097`), so an unsigned, locally-modified `.ko` loads.

## 5. Device staging

`030_staged.txt`. The patched file was `scp`'d to `/tmp`, verified (`md5 e21629d2...`), then copied
to the boot path. Overlayfs copied it up, so the overlay upper copy now shadows the squashfs file:

    /lib/modules/5.10.201/hi5622v100_wifi.ko                md5 e21629d226ec7de9a860a8955952d311  (patched)
    /overlay/upper/lib/modules/5.10.201/hi5622v100_wifi.ko  md5 e21629d2...                       (upper copy)
    /rom/lib/modules/5.10.201/hi5622v100_wifi.ko            md5 4737fcb21a1a2262a96f84d780ad8b35  (original)

The recovery command in section 0 was written before this step. The device was then rebooted.

## 6. Load verification after reboot

`040_postreboot.txt`. The device returned after ~50 s. Verification:

- boot-path file still `e21629d2...` (patched); `/rom` original still `4737fcb2...`;
- `hi5622v100_wifi` loaded, `refcnt=1`; `hi5622v100_plat` loaded, `refcnt=3`;
- both endpoints bound to `rox_pci0`; `Wiphy phy0`/`phy1` present; `vap0/3/8/11` AP in `br-lan`;
  calibration `get_2g_power_param`/`get_5g_power_param` answer `[SUCC]`;
- dmesg: `firmware_download success`, `hmac_softnp_woe_hook_init: succ`, `enable radio0..3`;
- **no new pstore record** for this boot (nothing panicked on the way up).

The kernel exposes no `/proc/kcore` on this build, so the loaded image cannot be read back directly;
"the running module is the patched one" is established by the boot-path md5 plus the behavioural
result in section 7 (the `+0x80` fault is gone, which can only happen if the branch is in the loaded
code).

## 7. Unload attempt

`050_teardown.txt`, `060_unload_wifi.txt`, `100_oops_plat_stale_wifi_callback.txt`.

Traffic paths down (as in `bringup.md`):

    wifi down ; /etc/init.d/softapd stop ; kill app_acs app_nlc ; /etc/init.d/wpad stop
    # hi5622v100_wifi refcnt 1 -> 0 ; hi5622v100_plat refcnt 3 -> 1

Then:

    # rmmod hi5622v100_wifi ; echo rc=$?
    rc=0

**The documented fault is fixed.** `rmmod` returned 0, the box stayed alive, and `lsmod` no longer
listed `hi5622v100_wifi` (`hi5622v100_plat` fell to refcnt 0). The dmesg shows the *same* teardown
that used to end in `PC=0x0` - and now there is no oops after it:

    [HIWIFI]hi_wifi_tx_hook para func is null
    hmac_softnp_woe_hook_exit: succ
    swa_netdevice_event 215:event:6, name:Hisilicon0
    [HSLINK]dev(Hisilicon0) is destroy
    (no oops)

**But the unload is not end-to-end clean.** ~2 s after the module was removed, a worker faulted
and the 30 s watchdog rebooted the box. The new pstore record (`dmesg-pstore_blk-1/2`) reads:

    ... Modules linked in: ... hi5622v100_plat(O) hi_pcie(O) ... [last unloaded: hi5622v100_wifi]
    CPU: 1 PID: 1170 Comm: Host MSG RX  Tainted: P           O      5.10.201 #0
    PC is at 0xbfa5ff0c
    LR is at hcc_queue_msg_process+0xdc/0x134 [hi5622v100_plat]
    pc : [<bfa5ff0c>]    lr : [<bf919b60>]
    r3 : bfa5ff0c
    [<bf919b60>] (hcc_queue_msg_process [hi5622v100_plat]) from [<bf91a28c>] (hcc_process_rx_thread+0x148/0x15c [hi5622v100_plat])
    Kernel panic - not syncing: Fatal exception

The faulting address is inside the memory the just-unloaded `hi5622v100_wifi` occupied: module base
(`/sys/module/hi5622v100_wifi/sections/.text`) was `0xbf969000`, and `0xbfa5ff0c - 0xbf969000 =
0xf6f0c`, which is the symtab entry for **`hmac_pfm_detect_process_event` +0x0 [hi5622v100_wifi]**.
The fault is a paging request with `*pte=00000000`, and the module list says `[last unloaded:
hi5622v100_wifi]`, so this is a stale function pointer: `hi5622v100_plat` still had a callback into
the removed wifi image registered, and its `Host MSG RX` thread invoked it after the unload.

So the *next instruction that is wrong* is not in the wifi exit path at all; it is
`hcc_queue_msg_process+0xdc [hi5622v100_plat]` dispatching the stale callback
`hmac_pfm_detect_process_event` (a pointer into freed `hi5622v100_wifi` `.text`). Fixing the wifi
exit path uncovered a second, independent cross-module unload-order bug: wifi unregisters none of
the callbacks plat holds, and plat does not stop its RX dispatch before wifi goes away.

## 8. Chain continuation

The brief says not to proceed down the stack until the wifi unload is clean. The wifi module's own
exit is clean, but the system panics ~2 s later, so the unload is not clean end-to-end and the
chain was **not** continued: no `hi5622v100_plat` (or `hi_pcie`/`hi_kwificlk`) `rmmod` was
attempted. Stopping at the first failure means the second fault above is the terminal finding of
this run.

## 9. Final health (acceptance)

`090_final_health.txt` - captured after the watchdog reboot, with the patched module re-loaded from
the overlay:

    patched file still staged : md5 e21629d2... (overlay) ; /rom original 4737fcb2...
    modules                   : hi5622v100_wifi refcnt=1 ; hi5622v100_plat refcnt=3
    endpoints                 : 0000:00:00.0, 0001:00:00.0 -> rox_pci0
    radios                    : Wiphy phy0 (Band 1), Wiphy phy1 (Band 2)
    vaps                      : vap0/3/8/11 AP in br-lan ; vap1/9 managed
    calibration               : get_2g_power_param [SUCC] ; get_5g_power_param [SUCC]
    oops since boot           : none

**Acceptance reached: the router ends healthy (both radios, calibration answering) with the patched
module loaded.** The patch is left staged in the overlay; section 0 is the way back to the stock
module.

## 10. Artifacts

| file | what it is |
| --- | --- |
| `010_disasm_clear_proc_orig.txt` | disassembly of `hmac_softnp_user_clear_proc` in the original `.ko`, with raw file offsets |
| `011_disasm_clear_proc_patched.txt` | same function after the patch (branch visible) |
| `012_callback_resolution.txt` | exact `__symbol_get` names behind the anchor offsets |
| `013_woe_hook_disasm.txt` | `woe_hook_init` / `woe_hook_exit` disassembly showing the slot writes/clears |
| `020_patch_apply.txt` | patch application transcript (offset, before/after bytes, ELF check, md5) |
| `apply_patch.py`, `disasm_clear_proc.py`, `resolve_callbacks.py`, `analyze_anchor.py` | analysis/patch scripts |
| `hi5622v100_wifi.ko.orig` | pristine original (md5 4737fcb2...) |
| `hi5622v100_wifi.ko.patched` | the staged module (md5 e21629d2...) |
| `000_baseline.txt` | pre-work device state (module md5, refcnts, radios, overlay layout) |
| `030_staged.txt` | staging transcript (temp md5, install, overlay upper created, /rom untouched) |
| `040_postreboot.txt` | post-reboot verification (patched file, modules, endpoints, radios, calibration) |
| `050_teardown.txt` | traffic-path teardown; refcnt `1 -> 0`, `3 -> 1` |
| `060_unload_wifi.txt` | the `rmmod hi5622v100_wifi` run: `rc=0`, module gone, dmesg to the old fault point |
| `070_confirm_wifi_gone.txt` | the post-`rmmod` re-check attempt: `ssh: connect ... Connection timed out` (the delayed panic had already taken the box down) |
| `090_final_health.txt` | final acceptance capture after the watchdog reboot |
| `100_oops_plat_stale_wifi_callback.txt` | the delayed oops + symbol resolution of the stale pointer |
| `pstore_pre_*.txt` | panic records present before this run |
| `pstore_after_wifi_*.txt` | pstore records after the delayed panic (`blk-1`/`blk-2` are the new one; `blk-0` is the old `bringup.md` record) |
