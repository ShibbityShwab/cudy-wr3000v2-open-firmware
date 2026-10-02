# sibling-ep: the message path belongs to `0001:00:00.0`, and taking it makes the interrupt fire (phase 21, 2026-10-02)

Task `st_01a0fb98`. Direct sequel to `docs/phase21/live-binding.md`, which pinned the last gate:
the vendor's message interrupt is delivered on the sibling endpoint `0001:00:00.0` (irq 209)
while every takeover so far claimed `0000:00:00.0` (irq 207, zero interrupts even on a working
vendor boot). This phase (A) works out the endpoint-to-radio mapping from `hi5622v100_plat.ko`
plus live vendor evidence, (B) redoes the full bring-up on the sibling endpoint in
`lab/sibep/sibep.c`, and (C) records whether the chip takes the frame.

Every claim is **[proven]** (a constant/relocation/control-flow in the instruction stream, a
captured live value, or a value the device printed) or **[inferred]**.

---

## Headline

**Part A: the message path for the radio we have been driving is carried by the sibling
endpoint `0001:00:00.0` (domain 1, irq 209).** The chip exposes two PCIe functions and two
register *instances* `0x800` apart. The vendor initialises and services only instance 0
(glue `0x40039000`, ETE interrupt block `0x40039508`, rings `0x4003a000`), and instance 0's
completion interrupt is the one delivered on irq 209. The block we have been programming is
exactly instance 0; what was missing was claiming the sibling so that its INTx reaches us.
Both functions alias the same on-chip register space, but each sits on its own root complex,
so the host-side window decode has to be redone on the sibling's RC. **[proven]**

**Part B: claiming the sibling works as predicted — the interrupt now fires.** The takeover
boot on `0001:00:00.0` claims it, decodes its iATU (host `0x58000000` / `0x59800000`), applies
the two live-captured binding writes, releases the chip and runs the service thread:
`request_irq(209)` rc=0, `/proc/interrupts` `209: 16864`, and the run summary
`irq=209 irq_taken=16864` where every earlier takeover had `irq_taken=0`. **[proven]**

**Part C: the chip still does not take the frame.** Even with the interrupt delivered and the
service thread running the id-6 wake and the SR pump, `out[0]` (the H2D mask, CA `0x40039010`)
is only ever written by our own doorbells and is never cleared by the device, there is no id-1
reply and no payload, and the device-side DR index does not leave its idle baseline. The
interrupt is no longer the blocker; the named next blocker is the device-side **H2D accept /
SR-consume gate** on instance 0 (the device reads/perpetuates the SR indices without consuming
the committed frame and never clears the mask). **[proven]**

---

## Part A — which endpoint carries the message path (edge-to-radio mapping)

### A.0 Method and sources

Static: `lab/ko_disasm.py build/register-dumps/teardown/hi5622v100_plat.ko <func>` (full
`.symtab`, md5 `23660bc285393e678d5cade1c36c194b`) — raw dumps in
`build/register-dumps/sibep/01{0,1,2,3}_disasm_*.txt`.
Live (normal vendor boot, read-only): the vendor's own boot dmesg, the two endpoints' PCI
config space (`/sys/bus/pci/devices/*`), and the register instances through endpoint 0's BAR0
region-3 viewport (`0x403b8000 -> CA 0x40000000`) — captured in
`build/register-dumps/sibep/000_partA_live_vendor.txt`.

### A.1 Two PCIe functions, selected by `cfg[0xff8] & 0xf`

`oal_pcie_probe` @`0x4d0` is the probe of the vendor's `pci_driver`. It accepts a function only
if the host-view bus id is 0 or 1 and the "chip id" is 0, then keys the per-function context on
the *phy device id*:

```
  0x000528: bl       -> pcie_get_bus_id_host_view [R_ARM_CALL]
  0x000530: cmp      r0, #1
  0x000538: (bhi)    -> error if bus id > 1
  0x00054c: bl       -> pcie_get_chip_id [R_ARM_CALL]
  0x000550: subs     r4, r0, #0
  0x000554: beq      #0x56c                 ; chip_id must be 0
  0x0005f8: bl       -> oal_pcie_get_phy_devid [R_ARM_CALL]
  0x000624: ldr      r2, [r6]               ; the context array
  0x00062c: ldr      r3, [r2, r0, lsl #2]   ; ctx[phy_devid]
  0x000650: str      r5, [r2, r0, lsl #2]   ; ctx[phy_devid] = new ctx
  0x000674: str      r3, [r6, #0x6c]        ; probe count++
```

`oal_pcie_get_phy_devid` @`0xbe5c` is a **config-space** read, not a table:

```
  0x00be60: movw     r1, #0xff8
  0x00be7c: bl       -> pci_read_config_dword [R_ARM_CALL]
  0x00be94: ldrb     r0, [sp, #8]
  0x00be9c: and      r0, r0, #0xf           ; phy_devid = cfg[0xff8] & 0xf
```

`pcie_get_bus_id_host_view` @`0xa030` returns `pdev->bus->number` (offset `+8` then `+0x74`),
and `pcie_get_chip_id` @`0xa008` tests bit `bus_id` of `[board_info+8]`. `pcie_bus_getnum`
@`0x10814` is a hard `mov r0, #2` — the platform expects **two** endpoints. **[proven]**

The live vendor boot prints the mapping directly:

```
[PCIEL]chip 0, bus 0, probe cnt 1, phy_devid:1.      <- 0000:00:00.0
[PCIEL]chip 0, bus 1, probe cnt 2, phy_devid:0.      <- 0001:00:00.0
[PCIEL]bus_id_hostview[1], phy_devid[0], is_pcie_cross[1]   (irq 209)
[PCIEL]bus_id_hostview[0], phy_devid[1], is_pcie_cross[1]   (irq 207)
```

and the config words are `cfg[0xff8] = 0x00011521` (ep0, `&0xf = 1`) and `0x00011520`
(ep1, `&0xf = 0`). So:

| endpoint | domain | host BAR0 | cfg[0xff8] | phy_devid | INTx |
|---|---|---|---|---|---|
| `0000:00:00.0` | 0 | `0x40000000` | `0x00011521` | 1 | irq 207 |
| `0001:00:00.0` | 1 | `0x58000000` | `0x00011520` | 0 | irq 209 |

Both are `59e7:0005` class `028000` and are bound to the same driver (`rox_pci0`). **[proven]**

### A.2 Both functions request their INTx; only the sibling's ever fires

`oal_pci_lres_init` @`0x7bb0` runs once per probed endpoint (from `pci_dev_res_init` @`0x821c`)
and calls `oal_pcie_probe_irq_init` @`0x698`, which (when the function is not already bound)
calls `do_request_irq(pdev, ctx)` @`0x1081c`. The vendor's own log shows both requests:

```
[PCIEL]raw irq: 209
[PCIEL]request pcie intx irq 209 succ
[PCIEL]raw irq: 207
[PCIEL]request pcie intx irq 207 succ
```

but on a fully working vendor boot only 209 is ever delivered:

```
207: 0      GIC-0  91 Level  hisi_pci_intx     <- 0000:00:00.0 (takes zero)
209: 20626  GIC-0  95 Level  hisi_pci_intx     <- 0001:00:00.0 (the live message ISR)
```

The vendor's service dispatcher `pcie_intr_handle` @`0x82e4` runs from the irq-209 stack and
reads the glue status as `*(*(ctx+4) + 0x2ec)`:

```
  0x0082f4: ldr      r3, [r6, #4]      ; ctx+4  -> base pointer
  0x008300: ldr      r3, [r3]
  0x008304: ldr      r4, [r3, #0x2ec]  ; glue status
  0x00830c: ands     r4, r4, #0x3d8    ; vendor dispatch mask
```

A kprobe at `pcie_intr_handle+0x20` on the live stack gives `base=0xc9ab9000`, i.e. device CA
`0x40039000` (from `live-binding.md` A.2), status `0x400392ec`. **[proven]**

### A.3 Two register instances `0x800` apart; only instance 0 is initialised

`shuangta_pcie_l1ss_set` @`0x1aeb8` touches the per-device register block of *each* probed
function, and the two addresses differ by exactly `0x800`:

```
  0x01aef8: ldr      r8, [r3, #0x10]          ; first pdev
  0x01af0c: movw     r1, #0x9220 ; movt r1, #0x4003    ; CA 0x40039220
  0x01af34: movw     r1, #0x92d0 ; movt r1, #0x4003    ; CA 0x400392d0
  ...
  0x01af9c: ldr      r3, [r3, #4]             ; second pdev
  0x01afa0: ldr      r5, [r3, #0x10]
  0x01afb4: movw     r1, #0x9a20 ; movt r1, #0x4003    ; CA 0x40039a20
  0x01afd8: movw     r1, #0x9ad0 ; movt r1, #0x4003    ; CA 0x40039ad0
```

Two per-function instances thus live in the one on-chip address space: instance 0 based at glue
`0x40039000` and instance 1 at `0x40039800` (`+0x800`). Reading them live (through endpoint 0's
BAR0, read-only) shows only instance 0 carries the vendor's init values:

| CA | instance | working vendor boot | meaning |
|---|---|---|---|
| `0x40039000` | 0 | `0x10b` | glue block |
| `0x400392e8` | 0 | `0x00000020` | chn_res, `pcie_ete_chn_res` **binding write #2 applied** |
| `0x40039508` | 0 | `0x3f201818` | ETE interrupt, `pcie_ete_intr_init` **binding write #1 applied** |
| `0x4003a000` | 0 | `0x0000010a` | ETE ring block |
| `0x4003a410` | 0 | `0x83a52000` (dynamic) | SR0 base (our takeover drives this instance) |
| `0x40039800` | 1 | `0x10c` | glue block, other function |
| `0x40039ae8` | 1 | `0x000003ff` | chn_res **not** masked (raw reset value) |
| `0x40039d08` | 1 | `0x3f3f1f1f` | ETE interrupt **not** written (raw reset value) |
| `0x4003a800` | 1 | `0x0` | ETE ring block, uninitialised |

This matches the static loops: `pcie_ete_intr_init` @`0x7528` takes the interrupt-block CA from
a per-device descriptor (not a literal) and applies the literal mask `0xffe0f8f8`:

```
  0x007578: ldr      r3, [ip]              ; ip = ete_ctx+0x84
  0x007588: ldr      r0, [r3, #4]          ; per-device ETE CA (instance select)
  0x00758c: bl       -> oal_pcie_inbound_ca_to_va [R_ARM_CALL]
  0x0075ac: movw     r2, #0xf8f8 ; movt r2, #0xffe0   ; 0xffe0f8f8
  0x0075b4: and      r2, r2, r1
  0x0075b8: str      r2, [r3]
```

and `pcie_ete_chn_res` @`0x7490` loops over the device array but **skips every device whose
`[dev_res+0x18] != 0`**, which is why the live capture saw the chn_res write exactly once:

```
  0x0074e8: ldr      r2, [r2, #4]
  0x0074ec: ldrb     r0, [r2, #0x18]
  0x0074f4: bne      #0x74bc              ; skip this device
  0x0074fc: ldr      r8, [r3, #0x2e8]     ; glue chn_res
  0x007504: and      r8, r8, sb           ; sb = 0xfffffc20
  0x007520: str      r8, [r2, #0x2e8]
```

So the vendor initialises **instance 0 only**, and instance 0 is the block we have been driving.
**[proven]**

### A.4 The two functions alias the same register space, but each has its own RC

Reading the *same* device CAs through `0000:00:00.0`'s BAR0 (`0x403b8000+…`) and through
`0001:00:00.0`'s BAR0 (`0x583b8000+…`) returns byte-identical values, including the dynamic SR0
base `0x83a52000`:

```
ep0 CA=0x400392e8 = 0x00000020      ep1 CA=0x400392e8 = 0x00000020
ep0 CA=0x40039508 = 0x3F201818      ep1 CA=0x40039508 = 0x3F201818
ep0 CA=0x4003a410 = 0x83A52000      ep1 CA=0x4003a410 = 0x83A52000
```

so the on-chip register space is shared (each function's BAR0 maps it; the earlier `barmap`
page maps differ only in dynamic pages). **[proven]**

What is **not** shared is the host decode: the two functions sit on two root complexes, visible
in the vendor dmesg as two host bridges and two inbound region sets:

```
[10.730827] PCIe:0 switch to RC mode
[10.749672] hi_pcie 10160000.pcie: PCI host bridge to bus 0000:00
[10.859600] PCIe:1 switch to RC mode
[10.876345] hi_pcie 10164000.pcie: PCI host bridge to bus 0001:00
...
(ep1) region idx0 paddr:0x58000000 ... idx3 paddr:0x583b8000 (IO) ...
(ep0) region idx0 paddr:0x40000000 ... idx3 paddr:0x403b8000 (IO) ...
```

Each RC has its own iATU (programmed through the function's BAR2: `0x59800000` for ep1,
`0x41800000` for ep0). **[proven]**

### A.5 Conclusion and what it implies

* **Which endpoint carries the message path for the radio we drive: the sibling
  `0001:00:00.0` (phy_devid 0, irq 209).** Instance 0 (the initialised ETE/glue block,
  `0x40039000`/`0x40039508`/`0x4003a000`) is the block both our takeover and the vendor's
  irq-209 handler use. **[proven]**
* **Does the ETE block we programmed belong to the sibling?** Yes — it is instance 0, the
  irq-209 message path. **[proven]**
* **Does the window decode have to be done on the sibling?** The device CA does not change
  (both functions alias one space), but the *host* decode does: claiming ep1 means mapping its
  BAR0 `0x58000000` and programming its own RC iATU via BAR2 `0x59800000`. So yes — do the
  claim/decode/rings on the sibling. **[proven]**
* There is no need to claim both: the two functions do not split the pieces we need; instance 0
  lives on ep1's side and ep1's INTx is the one the device asserts. **[proven]**
* The band label is **[inferred]**: `phy_devid 0` is the first enumerated device, and the host
  driver's `radio0/phy0` is `band=2g` (`radio1/phy1` is `5g`), so the sibling's message path is
  the 2.4 GHz one; `plat.ko` alone only proves `phy_devid 0 ↔ ep1`.

---

## Part B — `lab/sibep/sibep.c`, staged and booted on `0001:00:00.0`

### B.1 The module

`lab/sibep/sibep.c` is `lab/bind/bind.c` with only the endpoint selection changed: the
`domain` default is `1`, the `hostirq` default is `209`, and the new `omo-sibep` loader passes
`domain=1 irq=209 hostirq=209`. Everything else — claim
(`pci_enable_device` + `pci_request_mem_regions` on `pci_get_domain_bus_and_slot(1,0,0)`),
the six inbound + one outbound iATU viewports, the firmware load, the vendor message context,
the ETE SR/DR rings, the DR posts, the vendor id-1 SR frame, release `0x5a5a`, the service
thread with the in-thread SR producer pump, and the two binding writes — is carried verbatim.
`enable=0`, so CA `0x400392f0` (out[5], the hang family) is never written. The only
unproven-but-quoted write is `PCI_INTERRUPT_LINE` (the takeover boot leaves it `0xff`; the live
vendor value for this endpoint is 209). **[proven]**

Built by the module CI, run **`36981226194`**, commit `8464401`, artifact `sibep-ko`:
`sibep.ko` 54544 bytes, md5 **`a0131507d87d3e6e9abec25eb8ce512d`**,
`vermagic=5.10.201 SMP mod_unload ARMv7`. **[proven]**

### B.2 Staging, test boot, decisive log

Recovery was armed on the device before anything was staged (`/root/recover-sibep.sh`,
`start-stop-daemon -S -b -m … --watch`, pid 12397) and re-armed inside the takeover boot the
moment SSH answered (pid 6157). The loader (S99) claimed the sibling and ran the 25 s service
thread. Decisive lines from `build/register-dumps/sibep/061_testboot_full.txt`:

```
omo-sibep: endpoint 0001:00:00.0 id 59e7:0005
omo-sibep: pci_enable_device rc=0 command 0x0140 -> 0x0142
omo-sibep: pci_request_mem_regions rc=0 (MEM BARs claimed)
omo-sibep: BAR0 base=0x58000000 (config space), BAR2=0x59800000 (iatu_bar1)
omo-sibep:   iatu[0x708] <= 0x583b8000 readback=0x583b8000 match=YES  (r3 base_lo)
omo-sibep:   ETE intr 0x40039508    [0x000] <= 0x3f201818 readback=0x3f201818 match=YES
omo-sibep:   glue chn_res 0x400392e8 [0x2e8] <= 0x00000020 readback=0x00000020 match=YES
omo-sibep: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
omo-sibep: PCI_INTERRUPT_LINE <= 209 (unproven-but-quoted, live vendor value) readback=209
omo-sibep: request_irq(209, IRQF_SHARED, "omo-sibep") rc=0 - IRQ path live
omo-sibep: [send post0] pcie_msg_send(chip,3): out[0] CA=0x40039010 0x00000000 -> 0x00000008
omo-sibep: [glue svc +1050ms] CA 0x400392ec 0x00000008 -> 0x00000018 mask0x3d8=0x00000018
omo-sibep: [glue svc +1560ms] CA 0x400392ec 0x00000018 -> 0x00000008 mask0x3d8=0x00000008
omo-sibep: [glue svc +2850ms] CA 0x400392ec 0x00000008 -> 0x00000009 mask0x3d8=0x00000008
omo-sibep: [svc +950ms] MBOX out[1] pending CA=0x40039014 0x00000000 -> 0x00000040   (id 6)
omo-sibep: [svc +1490ms] out[1] CA=0x40039014 0x00000040 -> 0x00000004            (id 2)
omo-sibep: done (release=1 rings=1 sr_posted=1 acpoff=0 svc=1 iters=169 glue_clears=0
           pollms=100 polldur=20000 irq=209 irq_taken=16864 irq_handled=2 msgs=3
           services=5 sendflag=1 dr_events=4 sr_events=1 pumped=0)
```

and the interrupt table captured on the takeover boot
(`build/register-dumps/sibep/062_irq_and_params.txt`):

```
209:      16864          0     GIC-0  95 Level
```

The action name is empty because `omo_svc_init` frees its handler when the one-shot insmod
returns; the 16864 count is the number of deliveries while it was registered. **[proven]**

### B.3 What the sibling claim changed, and what it did not

Changed: the message interrupt is now delivered (`irq_taken=16864`, `irq_handled=2`) where every
prior takeover recorded `irq_taken=0`; the service thread sees the glue status go
`0x8 -> 0x18 -> 0x8 -> 0x9` and D2H mailbox bits `id 6` (`0x40`) and `id 2` (`0x04`), and
dispatches the id-6 wake into `pcie_wkup_thread` (send flag set). **[proven]**

Unchanged: `out[0]` CA `0x40039010` is written only by our own `pcie_msg_send` doorbells
(`0x00 -> 0x08 -> 0x20 -> 0x08 -> …`); there is **no** transition to `0`, no id-1 reply, and no
payload. The only `id=1` line in the log is our own posted host frame
(`SR ch0 node[0] … id=1`), not a device reply. The DR device index stays at its idle baseline
`0x10` (no completion). **[proven]**

A logging artefact: the `[postdr +4294706996ms]` timestamps are `jiffies_to_msecs(jiffies -
omo_t0)` taken before `omo_t0` is set; they are not a device event. **[proven]**

---

## Part C — did the chip take the frame? named next blocker

**No.** With the sibling endpoint claimed and the interrupt finally live, the chip still does
not take the host frame: `out[0]` (H2D mask) is never cleared by the device, no id-1 reply
arrives, no payload lands in a DR buffer, and the DR index does not advance. **[proven]**

The interrupt-delivery blocker is now **eliminated** — that was the whole point of this phase,
and it is confirmed working. The remaining blocker is the device-side **H2D accept / SR-consume
gate** on instance 0, and this run narrows it:

* The device's message engine is alive in the D2H direction (`out[1]` id 6 / id 2, dynamic glue
  status) and the id-6 wake reaches the host, so the accept failure is specific to H2D.
* The in-thread SR producer pump made **zero** refills this boot (`pumped=0`) where the ep0
  `bind` run made 64: the SR indices read back desynchronised (`SR+0x18` host commit low,
  device `SR+0x1c = 0x10`), so the pump computed no free slots and did not re-commit. The device
  is not consuming the committed H2D descriptors the way it did on ep0. **[proven]**
* The two device-side writes we do know from the vendor (out[5] `0x400392f0 <= 8`, which hangs
  the chip, and the per-channel ETE enable) are the remaining candidates, but `plat.ko` has not
  yet yielded a *quoted* H2D channel-enable/accept write we can apply safely.

**Named next blocker: the device-side H2D SR accept/consume gate on instance 0** — the next
experiment is to find the vendor's quoted H2D channel-enable/accept write (in the
`pcie_ete_init`/`pcie_ete_chn_res` neighbourhood or a per-channel `+0x00`/`+0x48` set) and to
reconcile the SR producer/consumer index bookkeeping so the committed frame is actually
consumed. `enable=1` (out[5] = 8) remains forbidden (it hangs the chip).

---

## Recovery and health

Recovery was cancelled with `/tmp/omo-sibep.done` and run by hand
(`build/register-dumps/sibep/070_recovery_run.txt`); the recovered boot verifies
(`build/register-dumps/sibep/080_final_health.txt`):

```
2 wiphys: phy0 phy1 ; radios vap0 (2g AP) / vap8 (5g AP) up
modules: hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3
md5 wifi e21629d226ec7de9a860a8955952d311 / plat 23660bc285393e678d5cade1c36c194b = baseline
calibration: get_2g_power_param -> [SUCC]17161605… ; get_5g_power_param -> [SUCC]00000000…
leftovers: no *.omo-off, no sibep.ko, no init.d/omo-sibep, no S99omo-sibep,
           no /root/recover-sibep.sh, no /tmp/sibep.ko, /tmp/omo-sibep*, watchdog gone
pstore: no new record (blk-0 10:41, blk-1/2 14:37 unchanged)
br-lan 192.168.10.1/24 UP
```

Boots this session: one takeover boot (`sibep`, sibling endpoint) plus one recovery boot. No
panic.

## Writes per takeover boot

Six inbound iATU viewports + one outbound viewport on the **sibling's** RC (host
`0x58000000`/`0x59800000`) + `PCI_COMMAND=7` + the 928,920-byte firmware (read back) + the
SR/DR program registers + the ETE interrupt-block RMW CA `0x40039508` + the glue
channel-resource RMW CA `0x400392e8` + the `0x5a5a` release + the host `out[1]`/`out[3]`/`out[4]`
service words + `out[0]`/`out[2]` for the id-3/id-5 doorbells + the single
unproven-but-quoted `PCI_INTERRUPT_LINE <= 209`. The RC misc window `0x10161000` was never
touched; CA `0x400392f0` (`out[5]`) was never written (`enable=0`).

## Artifacts

```
lab/sibep/sibep.c                        the module (bind.c, domain-1/irq-209 defaults)
lab/sibep/Makefile, omo-sibep            build + one-shot loader
lab/sibep/stage-sibep.sh                 staging (vendor modules -> .omo-off, S99 symlink)
lab/sibep/recover-sibep.sh               self-recovery + detached watchdog
build/tmp/phase21/sibep-ko/sibep.ko      built module (md5 a0131507…)
build/register-dumps/sibep/000_partA_live_vendor.txt   Part A live vendor evidence
build/register-dumps/sibep/010_disasm_probe_irq.txt    oal_pcie_probe / lres_init / irq
build/register-dumps/sibep/011_disasm_endpoint_select.txt  phy_devid / bus id / board
build/register-dumps/sibep/012_disasm_ete_glue.txt     ete_intr_init / chn_res / msg_init
build/register-dumps/sibep/013_disasm_l1ss_msgmap.txt  the two-instance l1ss + msg map
build/register-dumps/sibep/050_staging.txt             staging log
build/register-dumps/sibep/060_first_contact.txt       first contact + re-arm
build/register-dumps/sibep/061_testboot_full.txt       full takeover dmesg (1369 lines)
build/register-dumps/sibep/062_decisive.txt            decisive lines
build/register-dumps/sibep/062_irq_and_params.txt      /proc/interrupts + module params
build/register-dumps/sibep/070_recovery_run.txt        recovery run
build/register-dumps/sibep/080_final_health.txt        recovered health
```
