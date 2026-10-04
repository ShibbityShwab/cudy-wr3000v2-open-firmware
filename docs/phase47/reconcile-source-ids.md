# reconcile-source-ids: the ctrl-rb H2D line is registered AND enabled (id 0x4C) - the last link is delivery, not an unregistered source (phase 47, 2026-10-04)

Task `st_01a10765`. Static, read-only: `build/tmp/FIRMWARE.bin`, the phase31-46 record, and the
sibling lane reports. No device access, no register writes. This file is the only thing written.

Source note: the task named "the two phase-47 lane reports" to read; **no phase47 directory exists
in the tree** (checked on disk and in git at 14:55Z). The two lane findings reconciled here are the
actual lane outputs the parent produced: `phase45/hi1105-mailbox-irq.md` (L1, the mp17c ctrl-rb
register map) and `phase45/hi3881-mailbox-irq.md` (L2, the SDIO structural analog), folded together
with `phase45/reconcile-luofu.md` and the phase46 ctrl-rb measurement. If a phase47 lane report
lands later and contradicts anything below, that file wins.

| item | value |
| --- | --- |
| image | `build/tmp/FIRMWARE.bin`, 928,920 B (`0xe2c98`), md5 `0e530b976d5a20e87358671f1a577695` |
| convention | runtime address = file offset + `0x40000`; all offsets below are **file offsets** |
| disassembler | `pyenv/Scripts/python.exe`, capstone 5.0.7, `CS_ARCH_ARM, CS_MODE_THUMB`, `skipdata=True` |
| verification | every quoted offset was re-disassembled this task from the bytes on disk; ledger in section 7 |

---

## 1. Verdict

**The ctrl-rb host2device line is interrupt-controller source id `0x4C` - a source the firmware's
init both registers and enables. The last link is NOT an unregistered source; it is a delivery
failure between the ctrl-rb's latched status and the GIC's SPI 0x4C** (or, less likely given the
evidence in section 4, the enable never being applied because `pcie_msg_init` has not run in the
takeover boot - both are delivery-side explanations; neither is "the id was never registered").

In one chain: doorbell write (`0x400392d4` bit 0, phase46-measured) -> ctrl-rb raw status
(`0x400392e4` bit 0 latches 1) -> mask (`0x400392e8` bit 0 = 0, open) -> masked status
(`0x400392ec` bit 0 latches 1) -> **[the missing hop: device-internal wire into the GIC]** -> GIC
distributor SPI 0x4C (`0x40161000` file, enabled by the firmware itself) -> CPU interface
(`0x4016010c` IAR) -> IRQ stub (file `0x64`) -> ISR (file `0x82efc`) -> `fn_array[0x4c]` ->
thunk `0x294` -> H2D dispatcher `0x818ac` (which acks `0x400392f0=1`, consumes `out[0]`, re-arms
the doorbell with 8). Phase46 proved everything up to and including the masked-status latch.
Everything after it is device-internal, and the family register map (phase45 L1) has **no register
between the mask and the delivery** - the mask is the last gate and it is open.

## 2. The two findings being reconciled

**Finding A - the hardware lane (phase45 L1/L3 + phase46).** The doorbell sets the ctrl-rb raw
status bit 0 AND the masked status bit 0 (phase46 measurement, `exp/20261004-144441`: raw `0x08 ->
0x09`, masked `0x08 -> 0x09`). The mask register (`PCIE_CTRL_RB_HOST_INTR_MASK_OFF = 0x2E8`, bit 0
`host2device_tx_intr_mask`, 1 = masked, 0 = enabled, `pcie_chip_mp17c.c` L261) holds the vendor's
own live value `0x20` - bit 0 already 0. The port's existing `chn_res` write reproduces exactly
that value. So at the ctrl-rb, the interrupt **generates** and the family's only enable is **open**.

**Finding B - the firmware lane (phase32 + re-derived here).** The firmware services a GIC-shaped
controller at `0x4016xxxx` (ISR reads the pending id from `0x4016010c`, EoI to `0x40160110`). Its
init registers four interrupt ids in `pcie_msg_init` (file `0x9334`): `0x2d` and `0x2e` (channel
message demuxers that read the ETE-page status word `0x4003a86c+4`, phase32 section 4.4), `0x4c`
(fn `0x40295` -> the H2D dispatcher), and `0x4e` (fn `0x402a1` -> the D2H flush). Each of the four
is then enabled.

The question the task poses is exactly where these two findings meet: **is the ctrl-rb H2D line one
of those four ids (registered + enabled), or an id the firmware never registered?**

## 3. The source id is 0x4C - evidence chain

The id->line binding is not a documented SoC fact (no public register map names it), so it is
proven the way firmware bindings are proven: by what the handler does. Four independent pieces:

1. **The 0x4C handler's "ack" is the ctrl-rb H2D interrupt-clear.** The dispatcher writes `1` to
   `0x400392f0` (ctx+0xc): `movs r7, #1` at `0x818b2`, `str r7, [r2]` at `0x818b8`. In the sibling
   map (phase45 L1, `pcie_ctrl_rb_regs.h` L429-444) `0x2F0` is `PCIE_CTRL_RB_HOST_INTR_CLR` and the
   value 1 is exactly `host2device_tx_intr_clr` (bit 0) - the clear bit for the very event the
   doorbell sets. A handler that ends the host2device_tx interrupt *is* the host2device_tx
   interrupt handler. The same dispatcher consumes `out[0]` (`0x40039010`, the H2D pending word:
   `0x818bc`/`0x818be`) and re-arms the doorbell with 8 (`0x818c0`/`0x818c2`/`0x818c4`) - the full
   H2D service loop, and nothing else.
2. **0x4C/0x4E are the H2D/D2H pair.** `pcie_msg_init` registers both in the same breath
   (`0x9800`, `0x980c`) and it is the same function that *manufactures* the doorbell address
   (`0x976c`-`0x9776`: ack `0x400392f0`, doorbell = ack-0x1c = `0x400392d4`). The 0x4E handler is
   the mirror direction: `0x2a0` loads `0x10c0f4` and branches to the D2H flush `0x86108`, which
   waits for `out[1]` to read 0 and rings the D2H doorbell `0x40101434` (phase43 section 4.2). The
   two ids that bracket the ctrl-rb mailbox in both directions are 0x4C and 0x4E.
3. **No other registered handler touches any ctrl-rb interrupt register.** The 0x2D/0x2E handlers
   (file `0x62f8`/`0x624c`) read and clear a status word in the **ETE page** - `[0x10f1f0+0xc]` =
   `0x4003a86c` (pool `0x6388` = `0x0010f1f0`, data word `0xcf1fc` = `0x4003a86c`), bit 12 at
   `+4` - a different register file (the ETE/channel block at `0x4003a000+`), not the ctrl-rb. If
   the doorbell raised 0x2D/0x2E, the dispatcher's clear of `0x400392f0` would never happen; the
   firmware instead wires the ctrl-rb clear into 0x4C.
4. **The vendor's own boot is the operational proof of the wiring.** In vendor normal operation
   the host rings the doorbell bit 0 (`plat.ko` `pcie_msg_send_irq`, phase45 L3) and the device
   accepts H2D messages (phase22's canonical proof reads the dispatcher's signature: ack
   `0x400392f0`, clear `out[0]`, re-arm `0x400392d4`). Same hardware, same firmware image - so the
   doorbell->0x4C wiring is the real hardware path, not an aspirational registration.

## 4. Did the firmware's init enable it? Yes - instruction-level

`pcie_msg_init` programs id 0x4C completely, unconditionally, in one sequence:

```
0x009754  movs r0, #0x4c
0x009756  bl   #0x86db8        ; prologue: disable+clear 0x4c (ICENABLER/ICPENDR, 0x86db8..0x86e00)
0x0097f6  movs r0, #0x4c
0x0097fe  ldr  r2, [pc, #0x58] ; pool 0x9858 = 0x00040295  (the H2D thunk)
0x009800  bl   #0x874b0        ; register(0x4c, prio, 0x40295)
0x009814  movs r0, #0x4c
0x009816  bl   #0x86ff4        ; enable(0x4c)
```

What those three helpers write (all re-disassembled this task):

| fn | action for id 0x4C | evidence |
| --- | --- | --- |
| `register` `0x874b0` | stores fn `0x40295` into the ISR's dispatch array `[0x17d398 + 0x98 + id*4]` = `0x17d430 + 0x4c*4` | `0x874c4 add.w r3, r5, r4, lsl #2`; `0x874e8 str.w r8, [r3, #0x98]` |
| `register` -> `0x81a3c` | programs the distributor's priority byte lane: `[0x40161400 + (id&~3)]` byte `(id%4)` = `prio<<4` (`0x50` for prio 5) | pool `0x81a88` = `0x40161400`; `0x81a5a..0x81a72` |
| `register` -> `0x87500..0x87526` | programs the distributor's target byte lane: `[0x40161800 + (id&~3)]` byte `(id%4)` = table byte `[0x105f8c + id*3 + 2]` = `0x01` (CPU0) | pool `0x8753c` = `0x40161800`; table at file `0xc5f8c`, entry 0x4c = `4c 05 01` |
| `enable` `0x86ff4` | sets the distributor enable bitmap bit: `[0x40161100 + (id>>5)*4]` = `0x40161108` gets `1 << (0x4c & 0x1f)` = `1<<12` = `0x1000` | `0x8701a lsrs r2, r4, #5`; `0x8701c and r4, r4, #0x1f`; `0x87026 ldr r3, [pc, #0x1c]` (pool `0x87044` = `0x40161100`); `0x8702c str.w r4, [r3, r2, lsl #2]` |

Nothing later turns 0x4C off. The only disable-and-clear helper in the image (`0x86db8`) is called
with `0x22`/`0x23` by the ETE bring-up (per-CPU table at file `0xc5fdc`, indexed by `MPIDR&3` via
`0x82098`) and with `0x2f..0x32` by the channel bring-up (`0x9498a..0x9499c`); `pcie_msg_init`'s
own prologue disable (`0x9756`) is immediately followed by its register+enable. The distributor
init (`0x6e5c..0x6ed4`, loops at `0x7496`/`0x74a0`/`0x74aa`) leaves every SPI level-triggered
(ICFGR `0x40161c00` words = 0), priority `0xff`, target CPU0, all disabled - and the per-id
register/enable calls then lift 0x4C out of that default exactly as above.

Two honest caveats, so the verdict is not overstated:

- **"Exactly four registered" is true for the register fn's direct callers** (`0x96ca`, `0x96dc`,
  `0x9800`, `0x980c` - the only four `bl 0x874b0` in the image, scanned). A wrapper at `0x86da2`
  forwards to the register fn for the MAC interrupt ids (`0x2f`, `0x31..0x3d`, `0x52..0x55`, seen
  at `0x91efa..0x92242`), so the *full* registered set is larger. This does not touch the 0x4C
  analysis; it is recorded because "which ids exist" matters if a later experiment shows the
  doorbell raising some other id.
- **The id->line binding is the firmware's own architecture** (handler semantics + the vendor
  boot's functioning), not a SoC document. It is the strongest binding obtainable without the
  vendor interrupt-wiring source; section 6.1 names the exact follow-up that could still falsify
  it.

## 5. What the remaining link is (and what it is not)

Phase46 localized the gate to "downstream of the ctrl-rb". Combining that with this verdict, the
remaining link is precisely:

```
0x400392ec bit 0 (masked status, latched 1)  --->  GIC SPI 0x4C input  --->  distributor pending/CPU interface
        [phase46 measured]                            [device-internal, unmeasured, host-invisible]
```

Why it cannot be a host-side register: the family map has nothing between `HOST_INTR_STATUS`
(`0x2EC`) and delivery - the mask (`0x2E8`) is the only gate, and the vendor's own live value `0x20`
already has bit 0 open; the firmware image contains **no literal at all** for
`0x400392d4/e4/e8/ec/f0` (this task's scan; doorbell/ack are computed arithmetically at
`0x976c`-`0x9776`), so the firmware cannot re-arm the mask either. The GIC distributor itself is
PCIe-unreachable from the host (phase34: spare iATU viewport at `0x40160000` returns the no-decode
signature `0xffffffff`), and neither vendor module references `0x4016xxxx` (phase31 scans). Every
host-visible instrument has therefore been exhausted: the remaining hop is a device-internal wire
or a device-internal state the host cannot reach.

The one runtime question the static verdict cannot close: whether `pcie_msg_init` has actually
*run* in the takeover boot. It has no direct callers; its pointer (word `0x49335`) sits at file
`0xcf254` in a method table, and the call path through that table was not fully resolved this task.
If it has not run, the doorbell would raise 0x4C into a GIC whose enable bit is still 0 and whose
`fn_array[0x4c]` is still 0 - the ISR (which silently EoIs ids with no handler: `0x82f22/0x82f24 it hi; movs
r5, #0`, `0x82f48 cbz r5, #0x82f4c`, `0x82f52 str r7, [r3]`) would swallow it, and the observable would
be byte-for-byte what phase46 measured. That failure mode is *still* a delivery/sequencing gap in
the ctrl-rb->controller link (the source is registered and enabled in the image's init; the gate is
that init not having executed), **not** an unregistered source - but it is the one alternative
reading that explains the silence without a broken wire, and it is the cheapest one to falsify
(section 6.3).

## 6. Concrete next actions

### 6.1 [static/web follow-up - do this first] The sibling's device-side id table

The hi1105 sibling tree (`5-Super-Rookie-5/hongmengkernel`, pinned commit
`297b33b07b56adb3fa53baf618fdbc16ad9fd447`) carries device-side WLAN code at
`kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/wifi/dpe/`. Its
`hal/product/mp17c/pilot/host_irq_mp17c.c/.h` was fetched this task: it defines the MAC/PHY
*status-bit* unions but **not** the GIC irq numbers. The GIC id table lives in the firmware's
frw/oal layer - the follow-up is to fetch `dpe/hal/host_hal_irq.c` (15,859 B) and the `frw_ext_if.h`
/ `oal_ext_if.h` headers under the same tree and grep for the irq registration of the PCIe
message/H2D source, then compare its id against `0x4c` (and the pair `0x4e`, plus `0x2d`/`0x2e`).
If the sibling's device-side H2D id matches 0x4C, the family wiring corroborates this verdict; if
it differs, this verdict inverts to "unregistered source" and the differing id becomes the target
of a firmware patch or a wrapper registration. (Checked this task: a web search for the device-side
irq table does not surface it - it must be fetched from the tree, not searched for.)

### 6.2 [live, only if 6.1 names nothing better - labelled guess] The one untried family enable

Phase45's candidate 2 remains the only family enable register inside the host-visible window that
the port has never written: the **twin PCIe1 ctrl-rb's mask, CA `0x40039ae8`** (BAR0 `0x3f1ae8`),
value `old & ~0x9u` (`0x3ff -> 0x3f6`). Under the record's verified claim that both endpoint
windows decode the same doorbell, this is expected to be a no-op; it is offered only because it is
the single untried arm, and in minimal RMW form only. Same-run companion: re-assert the EP0 mask
`0x400392e8 = 0x20` **at message-service time** (after the firmware's boot helpers
`0x86f3e`-`0x86f50` run, immediately before the first doorbell) - the sibling's arm placement
(`oal_firmware_msg_download_pre`, phase45 L1 section 3.3), which the port's pre-release write does
not follow. Expected to read back `0x20` already; if it does not, that is itself the finding.

### 6.3 [device-side instrument - the only decisive falsifier]

A JTAG/ROM-monitor trace that, across one doorbell ring, reads (a) the distributor's enable word
`0x40161108` (bit 12 must be set if `pcie_msg_init` ran) and (b) the CPU interface's IAR
`0x4016010c` (must show `0x4c` when the line fires). This one measurement separates every
alternative: bit 12 = 0 -> `pcie_msg_init` never ran (the 6.3/5-caveat case; fix = trigger the
firmware's message-service init in the takeover, or re-issue the enable from the host side via a
firmware patch); IAR = some id other than 0x4C -> this report's verdict is wrong and the source is
unregistered under that id; IAR = `0x3ff` (spurious, no line) across the ring -> the ctrl-rb output
never reaches the GIC at all, i.e. a broken device-internal wire with no software gate left.

## 7. Verification ledger (this task)

Every quoted offset re-disassembled from the image on disk with `pyenv/Scripts/python.exe`
(capstone 5.0.7, Thumb, skipdata). Key rows; all match the claims above:

| offset | bytes | disassembly (claimed) |
| --- | --- | --- |
| `0x9756` | `7d f0 2f fb` | `bl #0x86db8` (disable+clear, r0=0x4c from `0x9754`) |
| `0x97f6` | `4c 20` | `movs r0, #0x4c` |
| `0x9800` | `7d f0 56 fe` | `bl #0x874b0` (register; pool `0x9858` = `0x00040295`) |
| `0x9816` | `7d f0 ed fb` | `bl #0x86ff4` (enable; r0=0x4c from `0x9814`) |
| `0x874e8` | `c3 f8 98 80` | `str.w r8, [r3, #0x98]` (fn into the dispatch array) |
| `0x81a5a`/`0x81a72` | `20 f0 03 05` / `28 51` | `bic r5, r0, #3` / `str r0, [r5, r4]` (IPRIORITYR lane; pool `0x81a88` = `0x40161400`) |
| `0x87500` | `9b 78` | `ldrb r3, [r3, #2]` (target byte; table file `0xc5f8c`, id 0x4c -> `01`) |
| `0x8701a..0x8702c` | `62 09` / `04 f0 1f 04` / `43 f8 22 40` | enable-bit computation and `str.w r4, [r3, r2, lsl #2]` to `0x40161100` (pool `0x87044`) |
| `0x818b2`/`0x818b8` | `01 27` / `17 60` | `movs r7, #1` / `str r7, [r2]` (ack `0x400392f0` = 1) |
| `0x818bc`/`0x818be` | `15 68` / `11 60` | `ldr r5, [r2]` / `str r1, [r2]` (consume `out[0]`) |
| `0x818c0`/`0x818c4` | `08 21` / `11 60` | `movs r1, #8` / `str r1, [r2]` (doorbell re-arm 8) |
| `0x82f22`/`0x82f24` | `88 bf`/`00 25` | `it hi`; `movs r5, #0` (out-of-range id -> no handler; the IT-context conditional form) |
| `0x82f48`/`0x82f52` | `05 b1` / `1f 60` | `cbz r5, #0x82f4c` / `str r7, [r3]` (skip + EoI regardless) |
| `0x96ca`/`0x96dc`/`0x9800`/`0x980c` | `7d f0 f1 fe` / `7d f0 e8 fe` / `7d f0 56 fe` / `7d f0 50 fe` | the four register calls (ids 0x2d/0x2e/0x4c/0x4e) - the complete caller list of `0x874b0` |
| `0x96ea`/`0x96f0`/`0x9816`/`0x981c` | `7d f0 83 fc` / `7d f0 80 fc` / `7d f0 ed fb` / `7d f0 ea fb` | the four enable calls in `pcie_msg_init` |
| `0x2a0`/`0x2a2` | `01 48` / `85 f0 31 bf` | `ldr r0, [pc, #4]` / `b.w #0x86108` (id 0x4e -> D2H flush; pool `0x2a8` = `0x10c0f4`) |
| `0x6e5c..0x6ed4` | - | distributor init: memset fn array, CTLR=0, per-line prio/target/disable loops, CTLR=1 |
| `0x86e32` | `ff f7 c1 ff` | `bl #0x86db8` (ETE bring-up disable; table `0xc5fdc` -> ids `0x22`/`0x23`) |

Literal scans (little-endian, whole image): `0x400392d4/e4/e8/ec/f0` **absent**;
`0x40039010` at `0x97e0`; `0x40039014` at `0x97dc`,`0x86fbc`; `0x40039000` at `0x86fb4`,`0xcf2ac`;
`0x4003a86c` at `0x0cf1fc`. Maximum quoted file offset `0xc5fdc+` (< `0xe2c98`).

Record files read: `phase46/intr-fires-at-ctrlrb.md`, `phase45/*` (all five),
`phase43/device-dr-side.md`, `phase32/interrupt-status-and-doorbell-path.md`,
`phase31/*`, `phase35/dispatcher-never-runs.md`, `phase22/h2d-accept.md`,
`build/register-dumps/exp/20261004-144441/*` (phase46 evidence), `build/register-dumps/reg_all.txt`.
