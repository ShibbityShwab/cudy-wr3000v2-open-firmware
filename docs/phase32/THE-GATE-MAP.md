# THE GATE MAP: doorbell -> dispatch, one page (phase 32, 2026-10-04)

Merge of the four phase-32 lane reports, all four verified **PASS** in
`docs/phase32/VERIFICATION.md`. Everything here is already established in one of the four; this
page adds no new claim, it only joins them.

Lane reports merged:

| lane | report | verified |
| --- | --- | --- |
| firmware chain / status register | `docs/phase32/interrupt-status-and-doorbell-path.md` | PASS |
| host-window reach of the interrupt block | `docs/phase32/irq-block-mapping.md` | PASS |
| what calls the dispatcher, and when | `docs/phase32/service-thread-gate.md` | PASS |
| which endpoint's window writes the doorbell | `docs/phase32/doorbell-endpoint-path.md` | PASS |

Device image: `build/tmp/FIRMWARE.bin`, 928,920 B, md5 `0e530b976d5a20e87358671f1a577695`.
Host driver: `hi5622v100_plat.ko`, md5 `23660bc285393e678d5cade1c36c194b`. All firmware offsets are
file offsets (runtime = file offset + `0x40000`); all `.ko` offsets are symbol offsets.

---

## 1. The armed chain (device side, firmware)

Every hop below is a quoted instruction in `interrupt-status-and-doorbell-path.md` §3/§4 and
`service-thread-gate.md` §1-§3, and was re-disassembled in `VERIFICATION.md` §1/§3.

```
ctx armed (pcie_msg_init, file 0x9334)
  ctx              = runtime 0x0010C190          (stub pool word, file 0x29c)
  ctx+0x00/0x04    = out[1] 0x40039014 / out[0] 0x40039010   (0x9784)
  ctx+0x0c (ack)   = 0x400392F0                  (0x9770)
  ctx+0x10 (doorbell) = 0x400392D4               (0x9776, computed as ack-0x1c)
  ctx+0x20 (H2D handler table) = 0x00118D68      (static, file 0xCC1B0)

H2D handlers
  pcie_msg_init registers the H2D handler into obj+0xbc -> the same 0x10C1B0 slot
  (0x8187A / 0x9838); handler id 3, arg = 0x0010C0F4.
  fn-array (the interrupt fn array, `register` 0x874B0 / ISR 0x82EFC):
    id 0x4C -> 0x40295  (the stub at file 0x294)      <- THE H2D PATH
    id 0x4E -> 0x402A1  (stub 0x2A0 -> 0x86108, D2H kick)
    id 0x2D -> 0x462F9, id 0x2E -> 0x4624D  (per-channel demux, PARALLEL, never reach the dispatcher)

interrupt handlers
  handler array = *(0x17D430 + id*4) = *(0x17D398 + 0x98 + id*4)   (0x82F16 / 0x874E8)
  registration: register(id,prio,fn) 0x874B0; enable(id) 0x86FF4 -> 0x40161100 bitmap;
                enableA(id) 0x86DB8 -> 0x40161180 + mirror 0x40161280
  for id 0x4C: word 2, bit 12 -> CA 0x40161108 / 0x40161188 = 0x00001000
  pending/active-id register = CA 0x4016010C (read 0x82F04); EoI = CA 0x40160110 (0x82F52)

dispatcher
  IRQ stub (ARM, file 0x64) -blx 0x82EFC-> ISR reads 0x4016010C -> fn = fn_array[id]
  -> id 0x4C -> stub 0x294 (ldr r0,=0x0010C190 ; b.w 0x818AC) -> 0x818AC
  dispatcher: ack 0x400392F0=1 (0x818B8); read+clear out[0] 0x40039010 (0x818BC/0x818BE);
              re-arm doorbell 0x400392D4=8 (0x818C4); blx handler[lowest set bit] via ctx+0x20
```

Status of each hop: **armed** in both a vendor boot and a takeover - nothing on the firmware side
is missing (`service-thread-gate.md` §6). The dispatcher has exactly one caller in the whole image,
file `0x296`, and it is reached only indirectly through `fn_array[0x4C]`
(`service-thread-gate.md` §1.1).

## 2. The armed chain (host side, vendor driver)

`doorbell-endpoint-path.md` §A-§E, verified in `VERIFICATION.md` §4:

```
pcie_msg_send @0x160F4 / pcie_msg_send_irq @0x174A8   (hi5622v100_plat.ko)
  -> OR bit 0 into comm+0x34                          (0x161A4..0x161B0 / 0x17544..0x17550)
  -> comm+0x34 VA built by shuangta_pcie_msg_reg_map @0x1B1A0 for CA 0x400392D4
  -> oal_pcie_inbound_ca_to_va(table, CA), table = chip->dev[0]->[4]   (pcie_msg_init @0xB6E4)
  -> dev[] keyed on phy_devid = cfg[0xff8] & 0xf       (oal_pcie_get_phy_devid @0xBE5C)
  -> dev[0] = 0001:00:00.0 (EP1, RC1), region-3 base 0x583B8000
  -> vendor doorbell VA = 0x583B8000 + 0x392D4 = 0x583F12D4
```

The port (`opensource/lab/wifidrv1`) owns `0000:00:00.0` (EP0, RC0), region-3 base `0x403B8000`,
and rings the doorbell at `0x403F12D4`. Both windows decode the **same** device register
CA `0x400392D4` (sibling-EP alias), so the register-level effect is identical; only the root
complex carrying the host->device TLP differs.

## 3. The single open link

> **mailbox H2D event -> device interrupt line `0x4C` -> CA `0x4016010C`**

Everything upstream of it is armed and everything downstream of it is armed. The doorbell is
consumed by the mailbox (readback 0, phase 31) but no id `0x4C` ever appears at CA `0x4016010C`,
so `blx r5` (file `0x82F4A`) never executes and `0x818AC` is never entered - which is exactly the
record's "the dispatcher's ack signature is never observed" (`service-thread-gate.md` §6). The
assertion is device-internal; the firmware never writes `0x4016010C` (its only reference is the ISR
read).

What is **not** the open link, now that the lanes are merged:

- It is not the interrupt block's readability. CA `0x40161100`/`0x40161800` are outside every host
  window (region 3, `SHUANGTA_REGION_IO`, ends at device CA `0x4011FFFF`), and the phase-30/31
  `BAR0 0x519100` probe was an out-of-bounds read landing on `FIRMWARE.bin[0x40100..0x40120]` -
  retracted in `irq-block-mapping.md` §D/§E. The interrupt block is **unobservable from the host**
  ("reads zero" was an artifact: the address resolves to region-4 ACP SRAM at CA `0x02041100`).
- It is not a firmware-side gate. The ISR is a pure exception handler: no thread, no polling flag,
  no semaphore; the wait is the CPU's IRQ line (`service-thread-gate.md` §3.3/§6).
- It is not the id mapping. Phase 30's `0x2D`/`0x2E` are the per-channel receivers; the H2D
  dispatcher is id `0x4C`, a slot the phase-30 live dump never covered
  (`service-thread-gate.md` §7).

Two candidate failure sites remain, both **outside** `FIRMWARE.bin`:
**(a)** the mailbox's own interrupt generation, and **(b)** the per-root-complex TLP path the
doorbell takes (EP0/RC0 for the port vs EP1/RC1 for the vendor, `doorbell-endpoint-path.md` §C/§D).

---

## 4. Priority list: next experiments, by lane

Live = device experiment (router cycle); static = image/module/dump analysis only. Numbering is the
recommended order. Each entry names its lane report.

1. **[static] Nail the hardware line->id binding for the mailbox.** Resume `interrupt-status-and-doorbell-path.md` §6: find where the mailbox H2D event is routed to interrupt line `0x4C` (SoC/boot-side wiring not in the blob). Search the vendor `hi5622v100_*` modules and any boot dumps for the mailbox IRQ number; expected support: the ISR's `cpsie/cpsid` window and `wfi` at file `0x86BD0`. Cheapest next step, no router needed.
2. **[static] Decide whether id `0x4C` is ever asserted in a *vendor* boot.** `service-thread-gate.md` §6: the vendor path is the same code, but no artifact yet shows `fn_array[0x4C]` firing. Recover the live `[fwctx]`/ISR signature from a vendor-boot dump (`bothep/002`) and compare against a takeover boot. Static comparison; no new device run required if the dumps suffice.
3. **[live] Probe the mailbox's interrupt generation from inside the device, without an added window.** `service-thread-gate.md` §5 + `irq-block-mapping.md` §F: CA `0x4016010C` and the enables are not host-visible, so make the firmware observe them - e.g. instrument a `.ko` that reads `0x4016010C` after a doorbell is rung from the host and reports through an existing in-window CA. This directly tests site (a).
4. **[live] Test the endpoint asymmetry (site (b)).** `doorbell-endpoint-path.md` §D/E: repeat the phase-31 doorbell-write cycle from **EP1** (vendor's `dev[0]`, region-3 base `0x583B8000`, doorbell VA `0x583F12D4`, INTx 209) instead of EP0 (`0x403F12D4`, INTx 207), with the same `out[0]` bitmap and the same ack readback. If EP1's doorbell latches an interrupt and EP0's does not, the RC path is the gate.
5. **[live] Add a spare iATU viewport onto device CA `0x40160000`, then read the pending/enable words.** `irq-block-mapping.md` §F: the endpoint has 16 inbound viewports and the vendor programs 6; a spare one could target `0x40160000` so `0x4016010C`/`0x40161108` become host-visible. Must not disturb the six named windows or calibration. Highest cost, highest information - it converts the whole block from unobservable to measurable.
6. **[static] Re-check the ACK negative against the other two message words.** `interrupt-status-and-doorbell-path.md` §2.3: the still-standing evidence is `out[5] = 0x400392F0` staying 0 across doorbell values 1 and 8. Before spending a device run, confirm from the vendor modules that `out[5]` is the correct ack for the H2D path (`pcie_msg_send_irq` writes it) and that the port's omission of the `out[5]=8` arm (`doorbell-endpoint-path.md` §A.2) cannot itself explain a silent dispatcher. Cheap, and it could invalidate priority 4/5 before they are run.

---

## 5. One-line summary

Firmware ISR chain (IRQ stub -> `0x82EFC` -> `fn_array[0x4C]` -> stub `0x294` -> dispatcher
`0x818AC`) is fully armed; the host driver's doorbell write path is fully known (vendor EP1,
port EP0, same register CA `0x400392D4`); the interrupt block is not host-visible; and the one
open link is **mailbox H2D event -> interrupt line `0x4C` -> CA `0x4016010C`**, to be attacked by
priorities 1-6 above.

Sources: `docs/phase32/interrupt-status-and-doorbell-path.md`,
`docs/phase32/irq-block-mapping.md`, `docs/phase32/service-thread-gate.md`,
`docs/phase32/doorbell-endpoint-path.md`, `docs/phase32/VERIFICATION.md`.
