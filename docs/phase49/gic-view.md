# GIC VIEW: the DEVICE side reads the GIC, and nothing was pending at either sampled instant (phase 49 follow-up, 2026-10-04)

The post-plan follow-up lane the task-13 branch recommendation named. Task 13 proved the firmware registers
and enables source `0x4c` (BRANCH-1) while the HOST cannot see the GIC block at all (CA `0x40160000` reads
`0xffffffff` through an inbound spare viewport). This experiment flips the sensor: the patched firmware
reads the GIC itself and deposits what it found into WRAM cells the host CAN read. Evidence
`build/register-dumps/exp/20261004-174357/`.

## The instrument

`tools/patch_fw_scratch.py` variant **`gicview`** (+304/-24, uncommitted at record time): the proven scratch
variant plus six device-side GIC reads deposited into fresh WRAM cells C0..C5. Blob
`build/tmp/fw-patched/gicview.bin`, md5 `b6b7faa9b2682d959cce6d0fb2bae315`, size 928920 (= stock size, the
patch is size-preserving). One takeover cycle, `EXP RESULT: PASS`, 155 s, healthy recovery. Manifest
`build/tmp/fw-patched/gicview.bin.manifest.json`; hooks and run log in `build/tmp/wifidrv1-art/`.

The two trampoline pads (the same entries the scratch variant used: enable thunk at file `0x87024`,
bring-up end at file `0x6ed4`) each read their GIC words after the S1/S2 stores:

- enable-thunk pad: CA `0x40161108` -> C0, CA `0x40161208` -> C1, CA `0x40160118` -> C2.
- bring-up-end pad: CA `0x40161208` -> C3, CA `0x40161308` -> C4, CA `0x40160118` -> C5.

The original ISENABLER write at file `0x8702c` is byte-for-byte preserved (`manifest.preserved.original_gic_write`).
The pads save and restore r0..r5 and the condition flags, and the acknowledging GICC IAR CA `0x4016010c` is
NEVER read by this variant. `interp.txt`.

## The values (BAR0 and the ACP alias identical for all ten cells)

| cell | BAR0 addr | alias addr | value (both views) | source |
| --- | --- | --- | --- | --- |
| S1 | `0x401035A0` | `0x407BB5A0` | `0x00001000` | bitmap word, bit 12 -> id `0x4c` (BRANCH-1 re-verify) |
| S1+4 | `0x401035A4` | `0x407BB5A4` | `0x40161108` | enable destination (BRANCH-1 re-verify) |
| S2 | `0x40103EB4` | `0x407BBEB4` | `0x00000001` | bring-up CTLR value 1 (BRANCH-1 re-verify) |
| S2+4 | `0x40103EB8` | `0x407BBEB8` | `0x50AA7E49` | scratch marker, BY EQUALITY (BRANCH-1 re-verify) |
| C0 | `0x40103EDC` | `0x407BBEDC` | `0x00000001` | ISENABLER word 2 (CA `0x40161108`), PRE-write |
| C1 | `0x40103FB4` | `0x407BBFB4` | `0x00000000` | ISPENDR2 (CA `0x40161208`), enable point |
| C2 | `0x40103FE4` | `0x407BBFE4` | `0x000003FF` | GICC HPPIR (CA `0x40160118`), enable point |
| C3 | `0x40103FEC` | `0x407BBFEC` | `0x00000000` | ISPENDR2 (CA `0x40161208`), bring-up end |
| C4 | `0x40103FF4` | `0x407BBFF4` | `0x00000000` | ISACTIVER2 (CA `0x40161308`) |
| C5 | `0x40103FFC` | `0x407BBFFC` | `0x000003FF` | GICC HPPIR (CA `0x40160118`), bring-up end |

Window sanity `0x406B8000` = `0xE59FF018` (the task-9/task-11 sanity word: the window decoded). No read
returned `0xffffffff`, and every alias equals its BAR0 value, so nothing here is a window artifact. `[sig]`
9/9, the port's done marker landed, and the loaded blob was the staged `.omo-pat` (dmesg
`[   42.398058] omo-drv1: firmware file /lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat size=928920 bytes`).
`build/register-dumps/exp/20261004-174357/capture-cmd.txt` and `interp.txt`.

## The map correction (load-bearing)

The orchestrator brief's map put ISPENDR2/ISACTIVER2 at `0x40160208`/`0x40160308`, which assumes a GICD base
of `0x40160000`. Those addresses fall inside the GICC page (base `0x40160100`), in its reserved area, and a
read there returns a guaranteed zero. Left uncorrected they would have faked "nothing pending" with a value
that proves nothing. The words that actually decode as the GIC-400 siblings are `0x40161208`/`0x40161308`,
in the page anchored by the firmware's own writes:

- **GICD base `0x40161000`** anchored by the firmware's own CTLR literal (file `0x716c`, loaded into r7 and
  written at file `0x6ed4`; phase47 calls that store "distributor CTLR = 1"). This run's S2 = `0x00000001`
  re-confirms it ran.
- **GICD_ISENABLER base `0x40161100`** from the enable thunk's literal at file `0x87044`; its store reaches
  word 2 / bit 12 = CA `0x40161108` for id `0x4c` (phase32 "for id 0x4C: word 2, bit 12 -> CA 0x40161108").
- **ICENABLER `0x40161180`** and **ICPENDR `0x40161280`** from phase32's own clear pair (phase32/phase47
  anchors) -> the pending block sits at base+`0x200`, so ISPENDR2 = `0x40161208` and ISACTIVER2 =
  `0x40161308`.

The independent verifier graded this correction RIGHT and the accompanying bounds HONEST. `interp.txt`
records it as a defect of the draft map, not of the measurement.

## Why a zero C cell still means the store executed

C0..C2 sit in the same straight-line block as the S1/S1+4 stores, after them, with no branch between
(`... str r5,[r0,#4] ; movw r1,#0x1108 ; movt r1,#0x4016 ; ldr r1,[r1] ; ...`), and C3..C5 likewise follow
S2/S2+4. S1 = `0x00001000` and S2+4 = `0x50AA7E49` prove both blocks ran, so the C stores executed and hold
the GIC words read. A zero deposit is a real "the register reads clear", not a missing store.

## Matched branch: BRANCH-G

**The device CAN read the GIC, and at both sampled init instants nothing was pending or active for the
CPU.**

- C0 = `0x00000001` is a live ISENABLER word 2 read from CA `0x40161108`, bit 0 set. Bit 0 of word 2 is
  source id `0x40`, and the firmware's own per-id descriptor table (base file `0xC5F8C`, indexed id*3) gives
  id `0x40` the descriptor `40 05 01` at file `0xC604C`, the same table shape phase47 quotes for `0x4c`
  (`4c 05 01` at file `0xC6070`). So an earlier-enabled word-2 source left a real bit set in a register the
  host reads as `0xffffffff` (task 13) and the device reads as `0x1`.
- Nothing pending for the CPU: C2 = C5 = `0x000003FF` (HPPIR "spurious/none") and C1 = C3 = `0x00000000`
  (ISPENDR word 2), C4 = `0x00000000` (ISACTIVER word 2). C2/C5 = `0x3FF` is itself proof that HPPIR is a
  real register (an unimplemented word would read 0), so the two HPPIR reads are positive evidence, not a
  failed read.
- Both BRANCH-1 cells re-verify: S1/S1+4/S2/S2+4 carry the scratch-written values at both aliases.

## The bounds (declared, not hidden)

1. **C0 is a PRE-write read.** The thunk pad runs before the original store at file `0x8702c`, so the
   brief's exact "bit 12 set" observation is not what C0 shows; bit 12 is clear at the read point only
   because the store that sets it comes next. The substantive claim (the device reads a host-invisible
   word) rests on the nonzero, architecturally-correct mask `0x1`. A post-write probe after file `0x8702c`
   is the one-line follow-up if bit 12 must be seen read back.
2. **No doorbell was rung in this boot.** The port's params are the frozen set plus the fwpath override, and
   the samples are taken during firmware init. A clear pending word therefore does NOT falsify a late or
   latch-triggered assert: "no ring -> not pending" is the expected reading and says nothing about the wire.
   Row 2 (ISPENDR2 bit 12 set -> the wire fires) and row 5 (ISACTIVER2 bit 12 -> delivered) are **NOT
   TESTED**, not excluded.
3. **A zero cell cannot distinguish "clear" from "RAZ" by value alone.** The layout is anchored by the
   firmware's own write targets (enable base `0x40161100`; the ICPENDR0 write at `0x40161280` -> pending
   block `0x40161200`) and the read path is shown live by the positive controls C0 (`0x1`) and C2/C5
   (`0x3FF`). ISACTIVER2's address rests on the relative layout only, since the firmware never touches
   ISACTIVER.
4. The enable-thunk pad is gated on the word-2 bit-12 bitmap (`0x1000`); within the thunk's valid id range
   `0x10..0x5f` only id `0x4c` produces it, so the C0..C2 deposits come from the id-`0x4c` pass.

## Verification

An independent verifier CONFIRMED (high), including the map-correction grade RIGHT and the bounds HONEST.
Acceptance re-run: 54/54 checks `ALL_OK`
(`build/register-dumps/exp/20261004-174357/acceptance.txt`), covering both-views reads for all ten cells,
alias agreement, no `0xffffffff` anywhere, blob md5s re-measured on the device, `[sig]` 9/9, the BRANCH-1
re-verify, and the KNOBSET contract. The frozen probe and scratch blob md5s are unchanged (`2a9a9f1d...` /
`b08699bd...`), and the original ISENABLER write at file `0x8702c` is byte-identical. `[sig]` dmesg line
`[   53.505570] omo-drv1: [sig] 9/9 signature registers changed -> THE CHIP LEFT ROM STATE`; done marker
`[   58.858695] omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded`.

## Health and cleanup

`health.txt` (`build/register-dumps/exp/20261004-174357/health.txt`): `WIFI=1 PLAT=1 WIPHY=2 IFACE=6
CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. Post-cycle cleanup: the staged `.omo-pat` removed, stock
FIRMWARE.bin md5 re-verified `0e530b976d5a20e87358671f1a577695`, vendor modules loaded, 2 wiphys / 6
interfaces, calibration `[SUCC]` on both bands, no new pstore record and no new crash dump (`cleanup.txt`,
`pstore-check.txt`).

## Next branch: a post-SEND sampling iteration

Sample ISPENDR2 and HPPIR at the moment the FIRMWARE itself posts to the host (its `out[1]` writes), which
is exactly when the missing ctrl-rb -> GIC wire would have to assert. That iteration is dispatched
2026-10-04. It is the instrument this run deliberately did not build: same pads, moved to the post path and
paired with a ring-capable knob.

---

# ADDENDUM (2026-10-04): the post-SEND iteration LANDED - BRANCH-S, the send-site sample

The iteration the section above dispatched. Evidence `build/register-dumps/exp/20261004-180736/`
(`interp.txt`, `capture-cmd.txt`, `acceptance.txt`, `cleanup.txt`). Round 1 of the same instrument
(`build/register-dumps/exp/20261004-180114/`) hit an aliasing anomaly and is kept with an
`aliasing-note.txt`; round 2 is the corrected, sentinel-proven run. Everything in the gicview record above
stands unchanged.

## The instrument (third trampoline at the firmware's own post)

`tools/patch_fw_scratch.py` variant **`gicsend`** (uncommitted at record time) keeps the gicview pads
byte-identical and adds a THIRD trampoline at the firmware's own send site: file `0x86f5a`, the `bl
#0x108468` that replaces the four bytes `1a600122` (`str r2,[r3]` writing `out[1]` = CA `0x40039014`
<- 4, then `movs r2,#1`). The pad's file offset `0xc8468`, run length `0x9c`, continuation `0x86f5e`
(`gicsend.bin.manifest.json`). The pad lifts r0..r5 and the APSR, **stores the marker first**, then reads
CA `0x40161208` (ISPENDR2), `0x40160118` (HPPIR) and `0x40161308` (ISACTIVER2) into fresh cells, and
returns. Blob `build/tmp/fw-patched/gicsend.bin`, md5 `326619bed125e65930a0a62bc6abdd47`, size 928920
(= stock size; `size_unchanged: true`). One `exp.sh` cycle, `EXP RESULT: PASS`, `exp_rc=0`, 149 s, healthy
recovery (`run-gicsend.log`). Manifest `build/tmp/fw-patched/gicsend.bin.manifest.json`; capture hook
`build/tmp/wifidrv1-art/gicsend-capture.hook` (copy: `capture-cmd.hook.txt` beside the run).

The D cells live in two pages that the frozen barmap shows as all-zero at both views (`0x11e000` and
`0x11f000`, no live firmware word), chosen because round 1's cells sat inside the live `0x104000` page and
were not readable back unambiguously. Each page carries a presence marker `0x50AA7E49` written by the same
pad, a few instructions after the GIC reads.

## THE ALIASING FACTS (state them plainly)

At these upper addresses the BAR0-direct view reads `0x0` while the **ACP alias** (`BAR0 + 0x6b8000` +
addr) carries the pad's stores. The boundary is empirically pinned between `0x103ffc` (the C5 cell: BAR0
`0x000003FF` == alias `0x000003FF`, both views agree) and `0x104004` (round 1's D0: BAR0 `0x0` vs alias
`0x00000020`, they diverge). The sentinels `0x50AA7E49` are readable ONLY through the alias, which proves
the stores ran and that the alias is the faithful view there. **The disagreement mechanism itself is
UNEXPLAINED and accepted as an observed device fact.** Rule: the D cells are quoted from the ALIAS ONLY;
the BAR0-direct zeros of those addresses are not admissible evidence. Moving the cells from the live
`0x104000` page to all-zero pages did not remove the phenomenon, so it is a property of the address range,
not of the page contents.

## The values (ALIAS view; both pages agree)

| cell | BAR0 addr | alias addr | alias value | meaning at the post |
| --- | --- | --- | --- | --- |
| D0 | `0x4011E000` | `0x407D6000` | `0x00000020` | ISPENDR2 bit 12 (id `0x4c`) CLEAR; bit 5 SET (id `0x45`) |
| D1 | `0x4011E008` | `0x407D6008` | `0x000003FF` | GICC HPPIR: nothing pending for the CPU |
| D2 | `0x4011E010` | `0x407D6010` | `0x00000000` | ISACTIVER2 bit 12 clear |
| D3 | `0x4011E018` | `0x407D6018` | `0x50AA7E49` | presence marker, page 1 |
| D4 | `0x4011F000` | `0x407D7000` | `0x00000020` | ISPENDR2, page 2, the independent re-read (agrees) |
| D5 | `0x4011F008` | `0x407D7008` | `0x50AA7E49` | presence marker, page 2 |

Sanities: S1 = `0x00001000`, S1+4 = `0x40161108`, S2 = `0x00000001`, S2+4 = `0x50AA7E49`; C0 = `0x00000001`,
C2 = C5 = `0x000003FF`; WIN `0x406B8000` = `0xE59FF018`; `[sig]` 9/9. The dialogue witness: `out[1]`
`0x0 -> 0x4` (bit 2, id 2) at `t=500ms`, one transition. So the D sample is not a dead window: the same
read path returns a nonzero register (`D0 = 0x20`), a decoded architectural HPPIR (`0x3FF`), and the pad's
constant.

## Matched branch: BRANCH-S

**At the firmware's own post the GIC mechanism is ALIVE, while SOURCE `0x4c`'s LINE DOES NOT SHOW PENDING
at that sampled instant.** A different line, word 2 bit 5 = id `0x45`, IS pending in the very same register
read by the very same instruction, and HPPIR decodes as a real architectural value. Combined with L1
(registered) and BRANCH-1 (enabled), the break is specific to the `0x4c` signal's path, not the controller.

The claim is phrased exactly: **not pending at the post instant**. This is a one-shot sample of a latched
register taken a few instructions after the store; an assert delivered AND acked within that window would
be invisible. That window is roughly ten instructions, judged unlikely but stated. The word "never" is not
claimed.

## The bounds (declared, not hidden)

1. **One-shot timing.** The pad stores then reads a few instructions later, and because the site is the
   `out[1]` writer the cells hold the last post's sample. A clear bit 12 means "not pending at the sampled
   instant", not "never pending".
2. **The register layout.** ISPENDR2 and ISACTIVER2 rest on the GIC-400 relative layout anchored by the
   firmware's own ISENABLER2 literal `0x40161108`. ISACTIVER2 has no firmware-write anchor at all.
3. **The BAR0/alias boundary mechanism is unexplained**, as above; only the alias values are quoted.
4. **D2's zero is meaningful only via its same-page marker** (D3): a bare zero there cannot separate
   "clear" from "reserved/RAZ" by value, so the marker carries the page.
5. **The host-issued ring / id-6 `d2h_notify` hop remains untested.** This boot rings no doorbell, so the
   run says nothing about whether a host ring would raise `0x4c`.
6. **id `0x45` is recorded, not explained.** It is not in phase47's constant-id enable list; the two
   dynamic enable sites are the candidates, and this run does not distinguish them.

## Verification

An independent verifier CONFIRMED (high): the values were traced to the raw capture, the acceptance rerun
to 49/49 (`acceptance.txt`, `PASS tally: 49 passed, 0 failed`, `ALL_OK`), the pads were independently
disassembled (store-then-read order, the sentinel stores, and the original ISENABLER write byte-identical
at `0x8702c`), the frozen probe/scratch/gicview blob md5s are unchanged, and the live device is clean.

## Health and cleanup

`health.txt` (`build/register-dumps/exp/20261004-180736/health.txt`): `WIFI=1 PLAT=1 WIPHY=2 IFACE=6
CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. Post-cycle cleanup (`cleanup.txt`): the staged
`.omo-pat` removed, stock FIRMWARE.bin md5 re-verified `0e530b976d5a20e87358671f1a577695`, vendor modules
loaded, 2 wiphys / 6 interfaces, calibration `[SUCC]` on both bands, no `.omo-off` leftovers.

## Next branch

Follow the `0x4c` line's routing and mask inside the chip. Candidates: (a) compare against a VENDOR boot
with the same sentinel-instrumented blob, does `0x4c` show pending when the vendor's own stack runs; (b)
probe the two dynamic enable sites, or run an other-source id `0x45` probe; (c) the host-ring / id-6 hop.
