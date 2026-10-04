# THE LAST LINK - the ctrl-rb host2device interrupt: verdict, next action, decider, safety (phase 47, 2026-10-04)

Merge of the three phase-47 lane reports, each independently re-checked by `VERIFICATION.md`
(all three PASS, one non-load-bearing correction to lane 1). This file adds no new measurement and
writes nothing else; every claim below carries the lane that asserted it and the evidence it rests
on.

| item | value |
| --- | --- |
| image | `build/tmp/FIRMWARE.bin`, 928,920 B (`0xe2c98`), md5 `0e530b976d5a20e87358671f1a577695` (all four files agree) |
| convention | runtime address = file offset + `0x40000`; offsets below are **file** offsets unless marked CA (device register) |
| lanes | L1 = `fw-source-ids.md` (firmware id enumeration); L2 = `sibling-source-map.md` (hi1105 host-tree web read); L3 = `reconcile-source-ids.md` (reconciliation + next actions) |
| verifier | `VERIFICATION.md` - re-disassembled every quoted offset from the bytes on disk (capstone 5.0.7, Thumb) and re-fetched every sibling quote raw at the pinned commit |

---

## 1. Verdict

**The last link is a delivery failure between the ctrl-rb's latched event and the GIC source, NOT an
unregistered source.** The firmware's own init registers *and* enables the ctrl-rb host2device line
as controller source id **`0x4C`**, so "the id was never registered" is eliminated; what remains
unmeasured is the device-internal hop from the ctrl-rb's masked status to the controller input.

The chain, with where it stops being observable:

```
doorbell write 0x400392d4 bit 0            [host-visible, phase46 measured]
  -> ctrl-rb raw status 0x400392e4 bit 0   [latches 1, phase46 measured]
  -> mask 0x400392e8 bit 0 = 0 (open)      [vendor live value 0x20, bit 0 already 0]
  -> masked status 0x400392ec bit 0        [latches 1, phase46 measured]  <-- last measured point
  -> >>> device-internal wire into the GIC: SPI 0x4C <<<                  <-- THE MISSING HOP
  -> GIC distributor enable 0x40161108 bit 12 (firmware sets it, CA-unreadable)
  -> CPU interface IAR 0x4016010c -> ISR file 0x82efc -> fn_array[0x4c]
  -> thunk 0x294 -> H2D dispatcher 0x818ac (acks 0x400392f0=1, consumes out[0] 0x40039010, re-arms doorbell 8)
```

### Evidence, by lane

**L1 `fw-source-ids.md`** - the image contains **28 `register` call sites** (27 constant ids plus one
dynamic; the four in `pcie_msg_init` are only a subset), **29 `enable` call sites** (25 distinct ids
plus 2 dynamic) and 17 `disable+clear` sites. The ctrl-rb line is registered at file **`0x9800`**
(`0x97f6 movs r0,#0x4c`, `0x979e movs r1,#5`, fn pool `0x9858` = `0x00040295` -> the H2D dispatch
thunk) and enabled at file **`0x9816`** (`0x9814 movs r0,#0x4c` -> `0x86ff4` -> bit 12 of
**CA `0x40161108`**). The id also appears in the only source-list-shaped table in the image, the
per-id 3-byte descriptor at file `0xC5F8C` (`4c 05 01` at file `0xC6070`), read by `register()`
itself. Verifier: PASS - A1-A3 re-disassembled clean; its section 5 item 2's handler-array
pre-writes are wrong in detail (slot 1 is `0x40161c00` ICFGR, slot `0x1d` is `0x000c20a3`, not
TYPER), which the verifier corrects in place - the report's load-bearing facts (array zeroed at
`0x6e5c`, `0x1d` pre-written with a code pointer, `0x4c`/`0x4e` pair as claimed) all survive, and the
"oddity" it flagged does not exist.

**L2 `sibling-source-map.md`** - the hi1105 host tree at pin `297b33b0` is **host-side only**: it
contains no device firmware, no interrupt-controller driver, and no INTID anywhere (`0x4016xxxx` is
0 hits; `intr_id`/`src_id`/`hwirq`/... are 0 hits). What it does establish: in this family the
ctrl-rb H2D source is **bit 0 / index 0** of the 12-source host-intr group; its enable is
`PCIE_CTRL_RB_HOST_INTR_MASK` offset `0x2E8` **bit 0 = 0** (0 = unmask; polarity stated at
`pcie_chip_mp17c.c` L261, `/* mask:1 for mask, 0 for unmask */`); it is deliberately
"registered-but-ignored" by the host (`"host2device intr, ignore by host"`,
`int_event_ignore_mask = (1 << HOST2DEVICE_TX_INTR_MASK)`), its consumer stated in code to be the
device; and the resting mask value leaves bit 0 at 0 - exactly the `0x20` on the record's live dump.
Verifier: PASS - every quote re-fetched at the pin and matched; the report's scoping (it is an
inference, it never claims to derive `0x4C`) is correct.

**L3 `reconcile-source-ids.md`** - joins the two: the `0x4C` handler's own ack is the ctrl-rb's
`HOST_INTR_CLR` bit 0 (`0x818b2 movs r7,#1` / `0x818b8 str r7,[r2]` -> CA `0x400392f0`), so a handler
that ends the host2device_tx interrupt *is* that interrupt's handler; `0x4C`/`0x4E` are the H2D/D2H
pair manufactured by the same `pcie_msg_init` that computes the doorbell; no other registered
handler touches a ctrl-rb interrupt register (the `0x2d`/`0x2e` handlers service the ETE page at
`0x4003a86c`); and the same hardware running vendor firmware proves the doorbell -> `0x4C` wiring is
the real path. Verdict: registered and enabled; the remaining hop is device-internal. Verifier:
PASS - all instruction-level claims matched (its `0x4c`-as-id reading was itself re-confirmed by
`0x874b4 mov r4, r0`).

### The two caveats that bound the verdict (both lanes state them)

1. **The id -> hardware-line binding is not in the image.** SoC-internal; the binding is proven from
   handler semantics plus the vendor's own functioning boot, not from a document. (L1 §7.1, L3 §3.)
2. **`pcie_msg_init` (file `0x9334`) has no direct caller.** No branch of any encoding reaches it;
   its only reference is a function-pointer word at file `0xcf254` = `0x00049335`, in the message
   module descriptor at file `0xcf250`. If the takeover never reaches that indirect callback, the
   `enable(0x4C)` never lands and the observable is byte-for-byte what phase 46 measured - still a
   delivery/sequencing gap, not an unregistered source. (L1 §7.2, L3 §5.)

---

## 2. Concrete next action and its order in the cycle

Do them in this order. (1) is the only one that can settle this without the device.

**(1) FIRST - static/web follow-up, before any further device cycle:** fetch the sibling's
device-side IRQ registration and compare its id against `0x4C`. Target: `dpe/hal/host_hal_irq.c`
(15,859 B) and the `frw_ext_if.h` / `oal_ext_if.h` headers under
`5-Super-Rookie-5/hongmengkernel@297b33b07b56adb3fa53baf618fdbc16ad9fd447`
(`kernel/linux-5.10-lts/drivers/connectivity/hi11xx/hi1105/wifi/dpe/`). Grep for the registration of
the PCIe message / H2D source and compare its id to `0x4c` (and the pair `0x4e`, plus `0x2d`/`0x2e`).
**If it matches `0x4C`, the family wiring corroborates the verdict and (3) becomes the only
remaining check; if it differs, the verdict inverts to "unregistered source" and that id becomes the
target of a wrapper registration or firmware patch.** (L3 §6.1. Note: this must be fetched from the
tree, not searched for - a web search does not surface it.)

**(2) LABELLED GUESS, only if (1) names nothing better, and only in the experiment cycle AFTER the
verdict is falsified as "no delivery":** the one untried family enable inside the host-visible
window - the twin PCIe1 ctrl-rb mask, **named register CA `0x40039ae8`** (BAR0 `0x3f1ae8`), written
`old & ~0x9u` (`0x3ff -> 0x3f6`), in **minimal read-modify-write form only**. Same-run companion:
re-assert the EP0 mask **CA `0x400392e8` = `0x20`** at message-service time (after the firmware's
boot helpers `0x86f3e`-`0x86f50`, immediately before the first doorbell) - the sibling's arm
placement (`oal_firmware_msg_download_pre`), which the port's pre-release write does not follow.
Expected to read back `0x20` already; if it does not, that is itself the finding. Order: this is a
normal `tools/exp.sh` module experiment, armed watchdog first, after the calibration snapshot. (L3
§6.2.)

**(3) DECISIVE - device-side instrument, the only falsifier:** a JTAG/ROM-monitor trace across one
doorbell ring reading (a) the distributor enable word CA `0x40161108` (bit 12 must be set if
`pcie_msg_init` ran) and (b) the CPU-interface IAR CA `0x4016010c` (must show `0x4c` when the line
fires). Order: last - it is the only path that reaches past the host-invisible hop, and it is what
(1) is meant to make unnecessary. (L3 §6.3.)

---

## 3. The observable that decides

> **CORRECTION (2026-10-04): the ctrl-rb ack is not a deciding observable.** CA `0x400392f0` is the
> ctrl-rb `HOST_INTR_CLR` register, write-to-clear: the sibling map gives it as the bit-0 clear of
> the 12-source host-intr group, and the H2D dispatcher named in section 1 writes 1 to clear it
> (`THE-GATE-MAP.md` section 1, `0x818b2 movs r7,#1` / `0x818b8 str r7,[r2]`, phase 32). A W1C
> register reads 0 before AND after any dispatcher run, so "the ack never flips" decides nothing,
> and any readback-based "byte-identical" claim resting on it proves decode/storage only, never
> device effect. Keep the never-write-`0x400392f0` rule: a snapshot showing it latched is expected,
> not evidence. The valid observables are the raw status CA `0x400392e4` and masked status CA
> `0x400392ec` (phase 46 measured: the doorbell sets bit 0 of each), and the device-side signatures
> (pending id CA `0x4016010c`) as the real delivery witness. The table below already stands on
> 0x40161108 and 0x4016010c, and needs no change; only the ack line of section 1 is struck.

One measurement separates every alternative: **read CA `0x40161108` bit 12 and CA `0x4016010c` across
one doorbell ring** (device-side instrument, action 3).

| observation | decision |
| --- | --- |
| `0x40161108` bit 12 = **0** | `pcie_msg_init` never ran in the takeover boot - the init-not-executed case of caveat 2. Fix = trigger the firmware's message-service init in the takeover, or re-issue the enable from the host side via a firmware patch. |
| IAR `0x4016010c` = **`0x4c`** when the line fires | the verdict holds: source registered, enabled, delivered; the work is then in the handler path (dispatcher `0x818ac`), not the link. |
| IAR = an id **other than `0x4C`** | the id->line binding in this file is wrong and the source *is* unregistered under that id - register that id instead. |
| IAR = **`0x3ff`** (spurious, no line) across the ring | the ctrl-rb output never reaches the controller at all - a broken device-internal wire with no software gate left. |

The cheap leading indicator, available with no instrument, is the masked-status latch at CA
`0x400392ec` bit 0: it already latches 1 on a doorbell (phase 46, `exp/20261004-144441`). If a
future run shows it *not* latching, the failure moved upstream of this file entirely and no
`0x4C`-side action applies.

---

## 4. Safety constraints (each attributed to its lane)

- **Never read the RC misc window CA `0x10161000`** - a read-only `devmem` there panicked the box;
  measure only through the endpoints' BAR0/BAR2. (L1/L3 port rule, restated in the lane scope: both
  lanes are *static, read-only* - the image plus the record, no device access, no register writes,
  the report file the only thing written. L1 header, L3 header.)
- **The GIC/distributor is PCIe-unreachable from the host** - phase 34's spare iATU viewport at
  `0x40160000` returns the no-decode signature `0xffffffff`. Do not attempt to read or patch
  `0x40160100`/`0x40161000` from the host; the deciding reads must be device-side. (L3 §5; L2 §6
  ledger row 4 confirms neither vendor module references `0x4016xxxx`.)
- **Re-asserting the ctrl-rb mask cannot be the gate, and 6.2 is an explicitly labelled guess** -
  the EP0 mask already reads `0x20` (bit 0 open), and the family's resting value leaves it at 0, so
  expect a no-op; write only the named registers, minimal RMW, and treat a non-`0x20` read-back as
  the finding rather than proceeding. (L3 §6.2; L2 §3.3/§8: "re-asserting that mask cannot be the
  gate".)
- **There is no literal for `0x400392d4/e4/e8/ec/f0` in the image** (0 hits) - doorbell and ack are
  computed arithmetically at `0x976c`-`0x9776`. The firmware cannot re-arm the mask, so do not
  expect an in-image patch to open it; any enable change is either a firmware patch or a host-side
  write. (L3 §5; L1 §7.2.)
- **Do not "fix" this by touching the interrupt-controller init the image already performs** - the
  distributor bring-up (file `0x6e5c`-`0x6ed4`) and the per-id register/enable calls are correct and
  complete for `0x4C`; the open question is whether that init *ran*, which is a sequencing matter,
  not a code defect. (L1 §5/§7.2, L3 §4.)
- **Any experiment goes through the normal cycle, not this document**: `tools/wifi-cal-snapshot.sh`
  before any device work, detached run with the device-side watchdog armed first, evidence into
  `build/register-dumps/exp/<UTC>/`, never in the repo root, and `tools/restore-now.sh` after. This
  file itself performs no device access. (L1/L3 scope statements; project rule.)
