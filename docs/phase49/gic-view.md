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

---

# ADDENDUM 2 (2026-10-04): the conditional-enable test (gicpost) - BRANCH-P

The conditional-enable test the two sections above earned the hard way. Evidence
`build/register-dumps/exp/20261004-190222/` (`interp.txt`, `capture-cmd.txt`, `acceptance.txt`, `health.txt`,
`cleanup.txt`, `pstore-check.txt`). One `exp.sh` cycle, `EXP RESULT: PASS`, 147 s, healthy recovery
(`run-gicpost.log`). Everything in the gicview and gicsend records above stands unchanged.

## The question

The enable write is not a plain store. It is the CONDITIONAL `strlo.w r4,[r3,r2,lsl #2]` at file `0x8702c`,
which executes iff LO holds: C = 0 and Z = 0. Every prior sample of this write (gicview C0, gicsend's
BRANCH-1 cells) was taken PRE-write, so none of them shows the store landing. If the condition failed in a
takeover boot, the enable would never happen at all, and that would be a root cause sitting upstream of the
whole delivery question: a line that was never enabled cannot be delivered. This run settles it by reading
back the enable register AFTER the store, no inference from the flag path required.

## The instrument (gate pad plus a post-store trampoline)

`tools/patch_fw_scratch.py` variant **`gicpost`** (uncommitted at record time) keeps the proven pads and makes
two changes:

- **the gate pad additionally deposits the APSR it captures.** The same `mrs` it already uses to test the
  condition feeds cell E3, and on its id-`0x4c` path it also deposits a REQUEST `0xe2e2e2e2` into E2 as the
  handshake's first half.
- **a post-store trampoline at file `0x87036`** (pad file offset `0xc8558`). The firmware's own `bl
  #0xbdf50` there is replaced by a trampoline whose pad holds NO `bl` and ends `b.w #0xbdf50` with `lr`
  untouched (the push excludes `lr`), so control resumes at `0x8703a` exactly as the original did. The pad
  matches and clears the `0xe2e2e2e2` request, re-arms the sentinel `0x50AA7E49` into E2, and reads CA
  `0x40161108` (POST-write ISENABLER2 word 2) into E0 and CA `0x40161208` (ISPENDR2 word 2) into E1.

Blob `build/tmp/fw-patched/gicpost.bin`, md5 `a4e69d74c172d856dd0e336a64482fbd`. The E cells sit at
`0x103fb8`/`0x103fe8`/`0x103ff0`/`0x103ff8`, inside the sub-boundary band where BAR0 and the ACP alias agree
(see the gicsend section's aliasing facts), and all four agree at both views. So this run quotes both views,
and the upper-address disagreement plays no part in it.

## The values

| cell | BAR0 addr | alias addr | value (both views) | meaning |
| --- | --- | --- | --- | --- |
| E0 | `0x40103FB8` | `0x407BBBB8` | `0x00001001` | POST-write ISENABLER2 word 2: bit 12 SET (the conditional store TOOK); bit 0 = the prior id-`0x40` enable |
| E1 | `0x40103FE8` | `0x407BBBE8` | `0x00000000` | ISPENDR2 word 2, post-store: the `0x4c` line not pending at that instant |
| E2 | `0x40103FF0` | `0x407BBBF0` | `0x50AA7E49` | handshake fired: request matched and cleared, sentinel re-armed |
| E3 | `0x40103FF8` | `0x407BBBF8` | `0x80000093` | APSR at the gate: N=1 Z=0 C=0 V=0 -> **LO HELD**; low bits are mode residue |

Sanities: S1 / S1+4 / S2 / S2+4 re-verify BRANCH-1 (`0x00001000` / `0x40161108` / `0x00000001` /
`0x50AA7E49`); C0 = `0x1` is the same register read PRE-write (bit 12 clear), so the 0 -> 1 on bit 12
happened in this pass, not before it; C2 / C5 = `0x3FF` (HPPIR still decodes as a real register); the D3 / D5
markers are present; `[sig]` 9/9.

## Matched branch: BRANCH-P

**The conditional store EXECUTED and TOOK. The silent-condition root cause is EXONERATED.**

E0 = `0x00001001` is the PK51/whatever readback of a latched register, the strongest evidence class in this
record: not a timing sample of a wire, but the register's own value read after the store. C0 = `0x1` (same
word, same run, PRE-write) makes the transition explicit: bit 12 was clear at the gate's read, and set at the
post-store read. E3 = `0x80000093` independently confirms the branch path: Z = 0, C = 0, so LO held and
`strlo` was architecturally bound to execute.

The chain now reads: **registered (L1) -> enabled-and-taken (E0) -> the line not pending at the enable instant
(E1) nor at the firmware's own post (gicsend's D0 = D4 = `0x00000020`, bit 12 clear) while word 2 carries
another line (id `0x45`) and HPPIR decodes `0x3FF`.** So the break is DOWNSTREAM of the enable register: the
delivery or mask path for the `0x4c` line. E0 is a latched register readback, not a sampled wire.

## The bounds (declared, not hidden)

1. **E1 is an instant, not a window.** It says the `0x4c` line was not pending at that instant. It does NOT
   say the line was never pending, and the word "never" is not claimed.
2. **The gate cannot see an enable that never reached the bitmap path.** The gate fires on the word-2 bit-12
   bitmap (`0x1000`); E0 proves the store landed, but a path that silently never reached this bitmap would
   simply produce no gate and no E cells. The positive result is safe (E0 exists, so the pass happened); a
   hypothetical absence would not have been decidable from the gate alone.
3. **The bitmap read is an id-`0x4c` pass.** Gate bitmap `0x1000` admits id `0x4c` (word 2) and `0x2c` (word
   1). A landed WORD-2 bit 12 is only produced by id `0x4c` (word 1's bit 12 is id `0x2c`), so E0's bit 12 is
   an id-`0x4c` witness.
4. **The register map rests on the GIC-400 relative layout.** Anchored by the firmware's own literals and
   write targets, as in the gicview map correction above. IAR CA `0x4016010c` is never read in this variant;
   CA `0x400392f0` is untouched; the RC misc window `0x10161000` is never read.
5. **The BAR0 / alias disagreement above `0x104000` is unexplained** (the gicsend aliasing facts). This run
   uses only sub-boundary cells where the two views agree, so it is not load-bearing here.
6. **No doorbell was rung.** The host-ring / id-6 hop stays untested, so this run says nothing about whether a
   ring would raise `0x4c` (the gicsend bound stands).
7. **No pre-cycle calibration snapshot.** The lane writes no calibration data: it is a firmware-init lane, not
   a calibration one, and the cleanup below shows the device healthy and calibration `[SUCC]` after.
8. **The run proves the WRITE landed, not the downstream cause.** It removes one candidate (the conditional
   never firing) and narrows the search; it does not find the delivery fault.

## Verification

An independent verifier CONFIRMED (high). The values were independently parsed from the raw capture; the
acceptance re-run passed 47/47 on every check; all five variant md5s were regenerated identical and the
`gicpost` blob is deterministic; capstone confirmed the site swap (stock `36f08bff` = `bl #0xbdf50` -> patched
`41f08ff8` = `bl #0xc8558`, with the pad's tail `b.w #0xbdf50`, no `bl` inside the pad, and `lr` preserved to
`0x8703a`); the handshake logic was proven by the sentinel (E2 = `0x50AA7E49` requires BOTH pads to run;
`0xe2e2e2e2` or `0x0` are the distinguishable alternatives if either half fails); the live device is clean
after the cycle. One nit was noted, and it did not affect this result: the acceptance script would label P1
even if LO were false, so the label alone is not the proof; the proof is the value pair (E3 = `0x80000093`
with a landed E0), and the nit was not triggered by the observed pair.

## Health and cleanup

The cycle ended healthy. `health.txt` (`build/register-dumps/exp/20261004-190222/health.txt`): `WIFI=1
PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. Post-cycle cleanup
(`cleanup.txt`): the staged `.omo-pat` removed (the stock `FIRMWARE.bin` is never written by this lane; the
patched bytes were staged under the distinct `.omo-pat` name via `EXP_EXTRA_STAGE`), stock FIRMWARE.bin md5
re-verified `0e530b976d5a20e87358671f1a577695`, vendor modules loaded, 2 wiphys / 6 interfaces, calibration
`[SUCC]` on both bands, no `.omo-off` leftovers. `pstore-check.txt`: 3 pstore records (the same 3 known ones,
no new record), and the 3 known wifi exception dumps (`excp_pktram_chip0.bin`, `excp_smac_dtcm_chip0.bin`,
`excp_wram_chip0.bin`), no new crash. The host copy of the blob remains at
`a4e69d74c172d856dd0e336a64482fbd` while the device carries none.

## Next branch

With the enable proven taken and the line silent downstream, the remaining question is WHERE. Probe the
distributor's delivery/target/priority words and the CPU-interface mask for the `0x4c` line, takeover
instrumented (safe), or run a vendor-stack boot with the same instrument (needs the lead's authorization, and
carries the task-12 abort risk).

---

# ADDENDUM 3 (2026-10-04): the delivery-configuration test (gicmask) - BRANCH-M

The WHERE probe the previous section named, run as a configuration sweep rather than another pending-word
sample. Evidence `build/register-dumps/exp/20261004-194824/` (`interp.txt`, `capture-cmd.txt`,
`acceptance.txt`, `artifact-check.txt`, `health.txt`, `cleanup.txt`, `pstore-check.txt`, `run-gicmask.log`,
`gm_check.py`, `KNOBSET.txt`). One `exp.sh` cycle, `EXP RESULT: PASS`, 150 s, healthy recovery. Everything
in the gicview, gicsend and gicpost records above stands unchanged.

## The question

After register (L1), enable (BRANCH-1) and the landed conditional store (BRANCH-P), the delivery path could
still be dead at a SETTING: the CPU interface disabled, no target CPU, or the glue masking the line. Any one
of those three is a root cause upstream of the wire, and each is a register the firmware itself writes. This
run samples the DELIVERY CONFIGURATION for source `0x4c` device-side, at the firmware's own post, and reads
all three candidate roots directly instead of inferring them.

## The instrument

`tools/patch_fw_scratch.py` variant **`gicmask`** (+608/-13, uncommitted at record time) keeps the proven
gate and post pads and makes the send-site pad additionally read and store ten words F0..F9. Blob
`build/tmp/fw-patched/gicmask.bin`, md5 `2d354bd297270d6a64056bf2fc663dc2`, size 928920 (= stock size;
`size_unchanged: true`). The F cells are the fourth-word group of two all-zero 4 KB pages above the
`0x104000` boundary (`all_zero_pages[2]`/`[3]`, manifest `gic_map.cells_note`), so they are quoted from the
ACP alias; `F8`/`F9` carry the sentinel `0x50AA7E49` and prove the pads ran. Manifest
`build/tmp/fw-patched/gicmask.bin.manifest.json`.

## The values (F cells above the `0x104000` boundary - ALIAS view; E/S/C agree at both views)

| cell | register | value | meaning |
| --- | --- | --- | --- |
| F0 | `0x40160100` GICC_CTLR | `0x00000001` | bit 0 EnableGrp0 SET, the CPU interface is enabled |
| F1 | `0x40160104` GICC_PMR | `0x000000F0` | passes `0x4c`'s priority `0x50` |
| F2 | `0x4016184C` ITARGETSR | `0x01010101` | byte 0 = `0x01`, targeted at CPU 0 |
| F3 | `0x4016144C` IPRIORITYR | `0xF050F050` | id `0x4c` -> `0x50` |
| F4 | `0x40161C10` ICFGR word | `0x55555555` | all fields `0b01` = EDGE |
| F5 | `0x400392E8` glue HOST_INTR_MASK | `0x00000020` | bit 0 = 0, the H2D line is UNMASKED/open |
| F6 | `0x400392E4` glue raw status | `0x0` | no event latched at the sampled post |
| F7 | `0x400392EC` glue post-mask | `0x0` | no event latched at the sampled post |

Sentinels: F8 = F9 = `0x50AA7E49` (both the page sentinel and the handshake), so the send pad ran after the
id-`0x4c` enable pass. E/S/C re-verify the earlier runs at both views (E0 = `0x1001`, E3 = `0x80000093`,
S1 = `0x1000`, S2 = `0x1`, C0 = `0x1`, C2 = C5 = `0x3FF`); no value anywhere returned `0xffffffff`; the
window sanity word `0xE59FF018` decoded.

## Matched branch: BRANCH-M

**All three candidate roots are EXCLUDED. The delivery configuration is fully ARMED; the missing piece is
the EVENT ASSERTION (ctrl-rb -> GIC).**

F0 = `0x1` rules out "the GICC is disabled". F2 byte 0 = `0x01` rules out "no target CPU". F5 bit 0 = `0`
rules out "the glue masks the line". F1/F3/F4 show the priority and trigger settings pass. So the line's path
is configured to deliver end to end, and what is missing sits on the assertion side, not downstream of it:
the mailbox-side generation of the H2D interrupt. F6/F7 clear is the EXPECTED reading of a boot with no ring
rung (the frozen params never write `0x400392d4`), so it cannot separate "never asserts" from "asserts but is
dropped" - the classification label BRANCH-M4 says exactly that, and the run's own decode prints the same
caveat.

## Two brief corrections (verified by disasm)

1. **`mask0x3d8` is a MASK CONSTANT, not an address.** The brief's "glue mask `0x3d8`" is the value the
   vendor's own `pcie_intr_handle` ANDs with the glue status word: `ldr r4,[r3,#0x2ec]` at `hi5622v100_plat.ko`
   file `0x833c`, then `ands r4,r4,#0x3d8` at `0x8344` (bits {3,4,6,7,8,9}). There is no register at ctrl-rb
   +`0x3d8`. The real mask register is CA `0x400392E8` (+0x2E8, 1 = masked per the sibling header and the
   vendor's own live value), which is F5.
2. **The F addresses rest on firmware literals.** GICC base `0x40160100` = firmware literal file `0x8309c`
   (also `0x83010` = `0x4016010c` IAR, `0x83020` = `0x40160110` EoI); GICD ITARGETSR base `0x40161800` = file
   `0x7174`; IPRIORITYR base `0x40161400` = file `0x7170`; ICFGR base `0x40161c00` = file `0x715c`; the glue
   triplet `0x40039000` = file `0x86fb4`. Note the verifier's nit: `0x83098` = `0x40160104` IS a literal too,
   so F1 is literal-anchored, not layout-derived; the instrument's prose contradicted its own table on this
   (bound 4 called F1 layout-derived), and the conservative reading is taken with no value changed.

## Own findings

1. **The BAR0 view LAGS the alias above the boundary.** F9's BAR0-direct view reads `0xE2E2E2E2` (the gate
   pad's EARLIER request to F9) while its alias reads `0x50AA7E49` (the send pad's LATER sentinel). So the
   boundary phenomenon has content: the BAR0 view is stale relative to the alias, not a different location.
   One sample, stated as an observation, and it changes nothing here (the F cells are quoted from the alias).
2. **F1 = `0xF0`, while the image's only GICC_PMR writer stores `0xff`** (`movs r2,#0xff` at file `0x8304e`,
   `str r2,[r3]` at `0x83054`, r3 = the literal at `0x83098` = `0x40160104`). Both `0xF0` and `0xff` pass
   `0x4c`'s priority `0x50`, so the discrepancy is immaterial to the branch and reported as its own finding.
3. **No `0xffffffff` anywhere**, so the read path is live at the sampled addresses.

## The bounds (declared, not hidden)

1. **Instant samples.** F0..F7 are one-shot reads a few instructions after the reproduced `out[1]` post; F6/F7
   are wire-status samples and share the "not pending at the sampled instant" bound.
2. **No doorbell was rung**, so F6/F7 clear is the expected reading and says nothing about a ring.
3. **The F cells are quoted from the ACP alias** (above `0x104000`); their BAR0-direct zeros are not
   admissible evidence.
4. **The F deposits are provable because of F8/F9**, not by their values alone.
5. **The id is the id-`0x4c` pass** by the handshake's construction plus E0's landed word-2 bit 12.

## Verification

An independent verifier CONFIRMED (high): the values were reproduced (44 view-reads parsed), the acceptance
re-run 67/67 `ALL_OK`, `gm_check` 55/55 `ALL_OK` (an independent re-derivation from the BYTES ON DISK: the
four sites re-disassemble to the manifest's pad offsets, the pads' `movw`/`movt` constants match the F read
map, the two sentinels and the two `0xe2e2e2e2` requests are located by value, every changed byte lies inside
the four sites or four pads). Six variants regenerate (probe `2a9a9f1d...`, scratch `b08699bd...`, gicview
`b6b7faa9...`, gicsend `326619be...`, gicpost `a4e69d74...` unchanged; gicmask deterministic). Capstone
confirmed the mask constant (`ands r4,r4,#0x3d8`) and the firmware literals. The live device is clean. Two
inherited conventions are flagged as RESIDUAL RISKS, not re-derived here: (i) the F5 mask POLARITY (1 =
masked) is inherited from a sibling header plus the vendor's live value, so an inversion would resurrect the
"the glue masks it" root; (ii) GICC_CTLR bit 0 is read as "enabled" without checking the interrupt's GROUP
(no IGROUPR check, so a Group-1 SPI with only EnableGrp0 set would keep a variant alive). Both are cheap to
test. One acceptance weakness is noted: the ladder prints F1..F4 without pinning them (only F0/F5/F6 gate the
branch), though the values are independently pinned in this record. The "no ring -> expected clear" reading is
correct and conceals nothing; the doorbell/ring test is the named next branch.

## Health and cleanup

`health.txt` (`build/register-dumps/exp/20261004-194824/health.txt`): `WIFI=1 PLAT=1 WIPHY=2 IFACE=6
CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. Post-cycle cleanup (`cleanup.txt`): the staged `.omo-pat`
removed (`rm_rc=0`; 928920 bytes present), firmware dir back to its 3 stock entries, stock FIRMWARE.bin md5
re-verified `0e530b976d5a20e87358671f1a577695`, `.omo-off` count 0, no loader/staged-module/watchdog
leftovers, vendor stack loaded with NO `wifidrv1`, 2 wiphys / 6 interfaces, calibration `[SUCC]` on both bands.
`pstore-check.txt`: still exactly the 3 pre-existing records and the 3 known wifi exception dumps, no new
crash.

## Next branch

A ring-instrumented run (doorbell plus event-assertion sampling at the ring instant) AND the two convention
checks: F5 polarity via a known-masked line, and IGROUPR for id `0x4c` plus EnableGrp1. All takeover-safe.

---

# ADDENDUM 4 (2026-10-05): the assertion trio (gicking) - BRANCH-GICK

The ring-instrumented run the previous section named, plus the two convention checks it left open. The
instrument rings the H2D doorbell itself, pays the fixture forward (held-mask vs open-mask in one boot,
three reads), and reads GICD_IGROUPR word 2. Evidence `build/register-dumps/exp/20261005-053936/`. One
`exp.sh` cycle, `EXP RESULT: PASS`, 150 s, healthy recovery. The gicview, gicsend, gicpost and gicmask
records above stand unchanged; nothing below contradicts a value they recorded.

## The instrument

`tools/patch_fw_scratch.py` variant **`gicking`** (+670/-20, uncommitted at record time) keeps the proven
four patches and adds a fifth send-pad block, plus the new G cells G0..G3. The pads carry the loads/stores
the manifest claims and nothing else. Blob `build/tmp/fw-patched/gicking.bin`, md5
`0f2e431f8e32c040b8ad7e7001f8ab57`, size 928920 (= stock size; `size_unchanged: true`). Manifest
`build/tmp/fw-patched/gicking.bin.manifest.json`; hooks and run log in `build/tmp/wifidrv1-art/`
(`gicking-capture.hook`, `run-gicking.sh`, `run-gicking.log`, `gk_verify.py`, `gk_facts.py`).

The send pad at file `0x86f5a` (runtime `0x108198`) is the new work. In order it: reproduces the `out[1]`
post, writes the glue MASK CA `0x400392e8` <= `0x21` (bit 0 MASKED plus the bit-5 field held), rings the
H2D doorbell CA `0x400392d4` <= `0x1` **exactly once**, then samples, then restores the mask to the
vendor-live `0x20` and reads G0/G1, with G2 (IGROUPR) and G3 (page sentinel) last. The other pads are
`thunk` @ `0x87024` (S1/S1+4/C0..C2/E2/E3), `bringup` @ `0x6ed4` (S2/S2+4/C3..C5), `post` @ `0x87036`
(E0/E1). The original ISENABLER store at file `0x8702c` is byte-for-byte preserved
(`manifest.preserved.original_gic_write`); the itet block `0x8702a..0x87035` and every continuation are
intact; the only deltas are the four `bl`s and the four pad bodies. The acknowledging GICC IAR CA
`0x4016010c` is NEVER read by this variant; CA `0x400392f0` is untouched; the RC misc window `0x10161000`
is never read.

## The values

The BAR0/alias agreement holds for the sub-boundary cells (S, C, E); the D/F/G cells sit above the
`0x104000` boundary and are quoted from the ACP alias.

| cell | register | value (alias view) | meaning |
| --- | --- | --- | --- |
| S1 | `0x401035A0` bitmap word | `0x00001000` | bit 12 -> id `0x4c` (BRANCH-1 re-verify) |
| S1+4 | `0x401035A4` dest | `0x40161108` | enable destination (BRANCH-1 re-verify) |
| S2 | `0x40103EB4` bring-up state | `0x00000001` | (BRANCH-1 re-verify) |
| S2+4 | `0x40103EB8` marker | `0x50AA7E49` | handshake (BRANCH-1 re-verify) |
| C0 | `0x40103EDC` ISENABLER2 word 2 PRE | `0x00000001` | id `0x4c` bit clear before the write |
| C1 | `0x40103FB4` ISPENDR2 word 2 PRE | `0x00000000` | (BRANCH-1 re-verify) |
| C2 | `0x40103FE4` GICC HPPIR PRE | `0x000003FF` | decode sanity |
| C3 | `0x40103FEC` ISPENDR2 word 2 bring-up end | `0x00000000` | (BRANCH-1 re-verify) |
| C4 | `0x40103FF4` ISACTIVER2 word 2 | `0x00000000` | (BRANCH-1 re-verify) |
| C5 | `0x40103FFC` GICC HPPIR bring-up end | `0x000003FF` | decode sanity |
| E0 | `0x40103FB8` ISENABLER2 word 2 POST | `0x00001001` | the enable store LANDED, bit 12 SET |
| E1 | `0x40103FE8` ISPENDR2 word 2 POST | `0x00000000` | the `0x4c` line not pending at the post |
| E2 | `0x40103FF0` handshake | `0x50AA7E49` | post pad consumed the request |
| E3 | `0x40103FF8` APSR | `0x80000093` | N=1 Z=0 C=0 V=0 -> LO held |
| F5 | `0x407D9008` glue HOST_INTR_MASK (post-ring, MASKED) | `0x00000021` | bit 0 = 1 MASKED, bit 5 held |
| F6 | `0x407D9010` glue HOST_INTR_RAW_STATUS | `0x00000001` | bit 0 = 1 (the ring latch) |
| F7 | `0x407D9018` glue HOST_INTR_STATUS (post-mask) | `0x00000000` | bit 0 = 0 while the mask holds it down |
| G0 | `0x407DA000` glue STATUS, mask OPEN | `0x00000001` | bit 0 = 1 |
| G1 | `0x407DA008` glue MASK readback | `0x00000020` | restored to the vendor-live `0x20` |
| G2 | `0x407DA010` GICD_IGROUPR word 2 | `0x00000000` | bit 12 = 0, id `0x4c` is Group 0 |
| G3 | `0x407DA018` G-page sentinel | `0x50AA7E49` | the G deposits are real |

D0/D4 (ISPENDR2 word 2, post-ring) = `0x00000020`, D1 (HPPIR) = `0x000003FF`, D2 (ISACTIVER2 word 2) =
`0x00000000`, D3/D5 (send-pad markers) = `0x50AA7E49`, the D0..D5 cells above the boundary (alias view).
F0 GICC_CTLR = `0x1`, F1 PMR = `0xF0`, F2 ITARGETSR = `0x01010101`, F3 IPRIORITYR = `0xF050F050`, F4 ICFGR =
`0x55555555` are the gicmask settings, unchanged and re-read here. Window sanity `0x406B8000` = `0xE59FF018`
(the task-9/task-11 sanity word: the window decoded). No read returned `0xffffffff`, and every sub-boundary
alias equals its BAR0 value, so nothing here is a window artifact. Sentinels F8 = F9 = D3 = D5 = S2+4 = G3
= `0x50AA7E49`, all present. `[sig]` 9/9, the done marker landed (`init done wiphy=omo-drv1 ifname=omowl1
hw=1 regs=decoded`), the `out[1]` witness is present (`out[1] bit 2 set (id 2)`, `poll done: 1 transitions
in 8000 ms`), and the loaded blob was the staged `.omo-pat` (dmesg
`[   40.475328] omo-drv1: firmware file /lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat size=928920 bytes`).
Staged md5 `0f2e431f8e32c040b8ad7e7001f8ab57` (== gicking), stock md5 `0e530b976d5a20e87358671f1a577695`
(UNCHANGED). `build/register-dumps/exp/20261005-053936/capture-cmd.txt`.

## The three verdicts

**1. RING (BRANCH-GICK-R): the ring LEAVES THE HOST and latches the glue raw status. SUPPORTED at the
ctrl-rb.** F6 bit 0 = 1, with F8/F9/G3 intact. The attribution is clean: the identical pad with the same
frozen params and NO ring (gicmask, `build/register-dumps/exp/20261004-194824/acceptance.txt`) read F6 =
`0x0`; the only actor added in gicking is the `0x400392d4 <= 0x1` store; and the firmware's own dispatcher
writes value 8 (bit 3) to that CA, which would set raw bit 3, while F6 has bit 3 clear. The raw bit 0 is new
and is caused by the ring. **The ring does NOT show up at the device GIC, and this boot cannot decide it.**
D0 and D4 (ISPENDR2 word 2, post-ring) read `0x00000020` with id `0x4c` bit 12 CLEAR, but the sample is
CONFOUNDED: the glue mask was deliberately held MASKED at that instant (F5 = `0x21`) and the post-mask
status stayed `0x0` (F7 = `0x0`), so a line gated by the glue mask is expected to leave ISPENDR clear. The
pad restores the mask OPEN only AFTER the D samples and then reads G0 without re-sampling ISPENDR2, so there
is no post-unmask GIC sample. The run supports neither "the ring reaches the GIC" nor "the ring fails to
reach the GIC"; only the ctrl-rb assertion is witnessed.

**2. GROUP (BRANCH-GICK-G): the group gate DIES. SUPPORTED.** G2 (`0x40161088`, GICD_IGROUPR word 2) bit 12
= 0, so id `0x4c` is Group 0. F0 `0x40160100` GICC_CTLR = `0x1` is exactly the configuration Group 0 needs
(bit 0 EnableGrp0 SET, bit 1 EnableGrp1 = 0), and E0 = `0x1001` re-proves the id-`0x4c` enable landed in the
same boot. This is addr.md row B: the group gate is NOT the blocker, and the residual root is the EVENT
ASSERTION (ctrl-rb -> GIC) side. This closes residual risk (ii) that ADDENDUM 3 named. Note the address is
LAYOUT-DERIVED (the image holds no IGROUPR literal), so the read is the check; the read path is proven live
(G1 = `0x20`, G3 page sentinel, WIN sanity, no `0xffffffff`), so `0x0` is a real architectural read, not a
dead window. One observation carried forward: GIC-400 IGROUPR resets "typically all-1s = Group 1" (addr.md
section 1d) while the device reads `0x00000000`; the read is the authority and it kills the group-gate root.

**3. POLARITY (BRANCH-GICK-P): "1 = masked" is CONFIRMED in-run. SUPPORTED.** The received convention was
inherited from a sibling header plus the vendor's live value, not re-derived (ADDENDUM 3 residual risk i).
This boot pays the fixture forward inside one boot with three reads of the same wire: F6 (raw, MASKED
instant) bit 0 = 1, F7 (post-mask status, same MASKED instant) bit 0 = 0, and G0 (status once the mask is
OPEN) bit 0 = 1. That triple separates the three models: the INVERTED model needs F7 bit 0 = 1 with the mask
at `0x21` and is EXCLUDED (F7 = 0); the INDEPENDENT-RAW-LATCH model needs F7 bit 0 = 1 as well and is
EXCLUDED (F7 = 0); the RECORD model (mask `0x2e8` bit 0 = 1 masks, `0x2ec = 0x2e4 & ~0x2e8`) predicts the
observed sequence exactly. The propagation-latency objection is excluded too: the raw bit was ALREADY 1 at
F6, read BEFORE F7, so F7 = 0 is not a not-yet-settled artifact, and F5 = `0x21` read at the same instant
proves the mask was really set when F7 read 0. So the polarity is settled from this run's own bytes and no
longer rests on the header.

## The bounds (declared, not hidden)

1. **Instant samples.** F5/F6/F7/G0/G1 and D0/D4 are one-shot latched reads a few instructions after the
   ring. "Bit clear at the sampled instant" is not "never set". The F5/G1 same-instant readbacks mitigate
   this for the glue; they do not for the GIC (bound 2).
2. **No post-unmask GIC sample (the top bound).** The mask is restored OPEN only after the D samples and
   the pad never re-reads ISPENDR2, so the ring -> GIC id `0x4c` step is UNRESOLVED in either direction.
   This is the single most important limit of this boot and the headline of the named next branch.
3. **The D/F/G cells above `0x104000` are quoted from the ACP alias**; their BAR0-direct reads are not
   admissible. Sub-boundary cells (S/C/E) are quoted at both views and must agree.
4. **The G2 address is layout-derived** (no IGROUPR literal in the image); the read is the check, and the
   live read path makes `0x0` a real value. The G deposits are provable because of G3, not by their values.
5. **The id is the id-`0x4c` pass** by construction: the gate bitmap `0x1000` admits id `0x4c` (word 2)
   only, E0's landed word-2 bit 12 is the witness, and E3 = `0x80000093` shows LO held.
6. **This run's own `interp.txt` / `acceptance.txt` / `KNOBSET.txt` / `cleanup.txt` / `pstore-check.txt`
   were NOT produced** (`pack-gicking-evidence.sh` did not run). Branch logic is checked against the raw
   capture and the manifest's own declared branch set, not a narrative.
7. **No calibration snapshot** (this is a firmware-init lane, not a calibration one; the cleanup below
   shows both bands at `[SUCC]` after). Device cycle serial/detached via `tools/exp.sh` with the watchdog
   armed. IAR `0x4016010c` never read, CA `0x400392f0` untouched, RC misc `0x10161000` never read.

## Verification

Two independent verifiers, both CONFIRMED, neither ran the cycle. The runtime verifier
(`build/register-dumps/diffs/20261005-053936-vrun/verdict.txt`, `parse_gicking.py`) re-parsed the raw
capture into its own equivalent acceptance (108 passed, 0 failed, `ALL_OK`), re-derived the polarity from
the F6/F7/G0 triple, and read G2 = `0x0` as addr.md row B; it declared three residuals: R1 (no `interp.txt`,
so it keyed on the manifest's branch set), R2 (the ring -> GIC step is undecided, the masked-ISPENDR
confound), and R3 (the staged `.omo-pat` was still present at probe time, and it removed it). The instrument
verifier (`build/register-dumps/diffs/20261005T054100Z-vtool/verdict.txt`, `vtool_dis.py`) regenerated the
blob six times, all md5 `0f2e431f...` and byte-identical to the staged blob, and capstone-audited it: the
selftest PASS, the seven frozen md5s unchanged, `gicking` deterministic, each of the four sites
re-disassembles to a single `bl` to the matching pad, the ring CA `0x400392d4 <= 0x1` is written exactly
once (not `0x8`), the mask `0x21 -> 0x20` and every CA->cell pair match the manifest, the original ISENABLER
store and continuations are byte-identical to the stock image, `changed bytes OUTSIDE 4 sites + 4 pads: 0`,
and no forbidden CA is written or read. The runtime verifier's independent acceptance and the instrument
verifier agree on the instrument and on every capture value.

## Health and cleanup

`health.txt` (`build/register-dumps/exp/20261005-053936/health.txt`): `WIFI=1 PLAT=1 WIPHY=2 IFACE=6
CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. The run's own pack-cleanup did not execute, so the
staged `.omo-pat` was still on the device at probe time; the runtime verifier removed it under the HARD
RULE. Its PROBE 1 confirmed the staged file md5 `0f2e431f8e32c040b8ad7e7001f8ab57` was the correct gicking
blob; the post-remove state: firmware dir back to its 3 stock entries
(`FIRMWARE.bin` / `cfg_device_hisi.ini` / `cfg_hi5622v100_hisi.ini`), `pat_after` = 0, stock FIRMWARE.bin
md5 re-verified `0e530b976d5a20e87358671f1a577695`. PROBE 2: no `wifidrv1`, 0 `*.omo-off`, 2 wiphys, 6
interfaces, 4 vendor `hi5622v100` modules, `get_2g_power_param` / `get_5g_power_param` both `[SUCC]`.
PROBE 3: 3 pstore records (the same 3 known ones, all dated Oct 2, no new crash) and the 3 known wifi
exception dumps. The router is healthy with no leftovers.

## Next branch

The named next branch is one follow-up cycle: a device-side ISPENDR2 word-2 read for source `0x4c` AFTER the
mask is restored open (or a compile-time open mask with no restore), which is the sample this boot is missing
and the one read that decides the ring -> GIC step. Pair it with an HPPIR decode and an ISACTIVER2 read at
the same post. All takeover-safe, one serial/detached `tools/exp.sh` cycle, verified against the same hook.

---

# ADDENDUM 5 (2026-10-05): the post-unmask ring test (gicunmask) - BRANCH A: THE WIRE IS ALIVE

The one follow-up cycle ADDENDUM 4's next branch named, and its runtime residual R2 retired (the masked
ISPENDR sample that left the ring -> GIC step undecided). Evidence
`build/register-dumps/exp/20261005-072450/` (`acceptance.txt`, `interp.txt`, `capture-cmd.txt`,
`artifact-check.txt`, `run-gicunmask.log`, `cleanup.txt`, `health.txt`). One `exp.sh` cycle, `EXP RESULT:
PASS`, acceptance 60/0, `gu_verify` 36/36, artifact-check clean. The gicview, gicsend, gicpost, gicmask and
gicking records above stand unchanged; nothing below contradicts a value they recorded.

## The question (the R2 follow-up)

Does a RINGED event, with the glue mask OPEN, reach the GIC? gicking's post-ring ISPENDR sample could not
say: its own R2 confound held the mask MASKED at the sample instant (F5 = `0x21`), so a line gated by the
glue mask was expected to leave ISPENDR clear, and there was no post-unmask read. This boot opens the mask
BEFORE it rings so the sample is not gated, and it is the read gicking was missing.

## The instrument

`tools/patch_fw_scratch.py` variant **`gicunmask`** (+639/-4, uncommitted at record time) keeps the proven
pads and re-sequences the send pad: entry read H5 (ISPENDR2 word 2), drive the glue mask OPEN (CA
`0x400392E8` <= `0x20`; `0x21` is NEVER written this run), then ring the doorbell ONCE (CA `0x400392D4` <=
`0x1`), then read H0 = ISPENDR2 word 2 (CA `0x40161208`), H1 = GICC HPPIR (`0x40160118`), H2 = ISACTIVER2
word 2 (`0x40161308`), H3 = glue raw (`0x400392E4`), H4 = glue status (`0x400392EC`), with the sentinels H6
and H7 (`0x50AA7E49`). Blob md5 `a68d5fed68b3015ab97c63ca7a394c28`, size 928920 (the patch is
size-preserving). The acknowledging GICC IAR CA `0x4016010c` is NEVER read by this variant; CA `0x400392f0`
is untouched; the RC misc window `0x10161000` is never read. Manifest and hooks in `build/tmp/wifidrv1-art/`
(`gu_verify.py`, `artifact-check.txt`); manifest `build/tmp/fw-patched/gicunmask.bin.manifest.json`.

## The values (the H cells above the boundary, quoted from the ACP alias)

| cell | register | value (alias view) | meaning |
| --- | --- | --- | --- |
| H0 | ISPENDR2 word 2 (CA `0x40161208`) | `0x00001020` | BIT 12 SET = id `0x4c` pending, plus bit 5 |
| H1 | GICC HPPIR (CA `0x40160118`) | `0x0000004C` | the CPU interface reports id `0x4c` as THE pending interrupt |
| H2 | ISACTIVER2 word 2 (CA `0x40161308`) | `0x00000000` | not yet active; IAR never read |
| H3 | glue raw (CA `0x400392E4`) | `0x00000001` | bit 0 latch |
| H4 | glue status (CA `0x400392EC`) | `0x00000001` | bit 0 set, the OPEN mask passes it |
| H5 | ISPENDR2 word 2, entry | `0x00000020` | bit 12 CLEAR at the next send-site visit |
| H6 | page sentinel | `0x50AA7E49` | the H deposits are real |
| H7 | page sentinel | `0x50AA7E49` | the H deposits are real |

Sanities: S1 = `0x1000`, S1+4 = `0x40161108`, S2 = `0x1`, S2+4 = marker; C0 = `0x1`, C1 = `0x0`, C2 =
`0x3FF`, C3 = `0x0`, C4 = `0x0`, C5 = `0x3FF`; E0 = `0x1001`, E1 = `0x0`, E2 = marker, E3 = `0x80000093`;
window `0x406B8000` = `0xE59FF018` (the task-9/task-11 sanity word: the window decoded); `[sig]` 9/9; the
staged blob md5 `a68d5fed...` and stock md5 `0e530b97...`. `build/register-dumps/exp/20261005-072450/`.

## Matched branch: BRANCH A - THE RINGED EVENT REACHES THE GIC

**Two ends agree, and they agree on the same source id.** The distributor pending bit is set (H0 ISPENDR2
word 2 bit 12 = 1, id `0x4c`) AND the CPU interface reports that id (H1 HPPIR = `0x0000004C`, not `0x3FF`,
not another id), while the glue itself latched and passed (H3 = H4 = `0x1`, with the open mask). A ringed
event with the mask open therefore reaches the GIC end to end: **the device-internal ctrl-rb -> GIC WIRE IS
ALIVE.**

This is the death of the phase-48 dead-link hypothesis. If the wire were dead, H0 bit 12 could not be set
and HPPIR could not name `0x4c`; both do. The earlier zeros were not a dead wire, they were a missing ring:
**the natural firmware posts never ring the doorbell, so a TRIGGER problem, not a dead wire, is what the
gicview / gicsend / gicpost / gicking zeros record.** The RING succeeded (the glue latched, H3 = 1) and the
DELIVERY succeeded (H0 bit 12 and H1 = `0x4c`).

The dead-link and delayed-delivery branches are excluded by **H0 bit 12 SET** - both require it CLEAR - so
neither survives this boot. H1 = `0x4c` is a unique architectural decode (HPPIR returns the highest-priority
pending id, and `0x3FF` is spurious/none), so the read is a real register answering a real request, not a
window artifact.

## The bounds (declared, not hidden)

1. **Instant samples.** H0..H4 are one-shot reads a few instructions after the ring; "set at the sampled
   instant" is the claim, nothing about how long it holds.
2. **The H cells are quoted from the ACP alias** (above the `0x104000` boundary). The alias model is
   INHERITED from gicsend / gicmask this boot, not re-demonstrated here by a BAR0-direct H read. Support
   for the inherited model: the sentinels survived, H1 decodes a valid unique id, and two CAs agree.
3. **H2 = `0x00000000` is pre-acknowledgement state.** The IAR is never read, so the interrupt is reported
   pending but not taken: H2 is the not-yet-active reading, as expected.
4. **H0 bit 5 (`0x20`) is a second pending line, reported not attributed.** It is present in the same word;
   this run does not name or explain it.
5. **H5's bit-12-clear is stated as RECORDED.** The "H5 entry was consumed / the send ran again" inference
   is weak: bit 5 could already have been set, so H5 = `0x20` does not by itself prove the entry clear came
   from a prior consume. It does not affect Branch A, whose evidence is H0/H1.
6. **The ring actor is DEVICE-side.** The firmware pads ring the doorbell; the vendor's NATURAL trigger is
   not reproduced, so this boot proves the wire when rung, not what rings it in normal operation. That is
   the open question below.

## Verification

An independent verifier CONFIRMED (high): the raw capture was re-read, the acceptance (60/0) and `gu_verify`
(36/36) were re-run, capstone showed the exact sequence (H5 entry -> mask OPEN `0x20` with NO `0x21`
anywhere -> ring CA `0x400392d4` <= `0x1` ONCE -> H0..H4 -> sentinels H6/H7), the forbidden CAs are absent
in all pads, the original ISENABLER store at file `0x8702c` is preserved, and the 513 changed bytes are
confined to the intended sites and pads. The live device is clean after the cycle (no `.omo-pat`, stock
FIRMWARE.bin md5 `0e530b976d5a20e87358671f1a577695`, 0/0/3 pstore, 6 interfaces, calibration `[SUCC]`). The
verifier noted two non-load-bearing items: the H-cell alias model is inherited (bound 2) and H5's bit-12
inference is weak (bound 5); neither touches Branch A.

## Health and cleanup

`health.txt` (`build/register-dumps/exp/20261005-072450/health.txt`): `WIFI=1 PLAT=1 WIPHY=2 IFACE=6
CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. Post-cycle cleanup (`cleanup.txt`): the staged
`.omo-pat` removed, stock FIRMWARE.bin md5 re-verified `0e530b976d5a20e87358671f1a577695`, vendor modules
loaded, 2 wiphys / 6 interfaces, calibration `[SUCC]` on both bands, no leftovers.

## Next question (one line)

What natural firmware action should ring the H2D doorbell (the trigger), and does the host ISR vision now
follow a firmware-side ring?

---

# ADDENDUM 6 (2026-10-05): the trigger test (trigring) - BRANCH T4-BIT12 (the ring -> GIC delta, isolated)

The cycle ADDENDUM 5's own next question named, run as the `trigring` variant. ADDENDUM 5 rang the doorbell
after opening the mask, but its only pinned GIC sample (H0) sat AFTER the ring while its pre-read (H5) came
from a DIFFERENT visit, so the mask change and the ring were conflated. This boot puts the pre and post
samples in the SAME straight-line visit with the glue mask already OPEN, so the single ring store is the
only actor between them. One `tools/exp.sh` cycle, detached, `EXP RESULT: PASS`, evidence
`build/register-dumps/exp/20261005-075136/` (07:51:35Z -> 07:54:07Z, `run-trigring.log`); blob
`build/tmp/fw-patched/trigring.bin` md5 `0769eed122c0506d3afca6b5a8bd8ae9`, an uncommitted working-tree
addition on top of HEAD `140caee`.

## The instrument

One new pad on the send site file `0x86f5a` (the site gicsend/gicunmask already patched), plus the three
inherited side-effect-free reads (thunk pad file `0xc8198`, runtime `0x108198`). Sequence: reproduce the
firmware's own `out[1]` post, open the glue mask (CA `0x400392E8` <= `0x20`; `0x21` NEVER written), take
T0..T3 PRE-ring, ring the H2D doorbell ONCE (CA `0x400392D4` <= `0x1`), take T4..T7 POST-ring, then return
to the natural D2H notify. The ring is exactly the research candidate in
`build/tmp/trigger-spec/trigger.md` section 3.

```
0xc8198  mrs ip, apsr
0xc819e  str r2, [r3]              ; reproduce *0x40039014 = 4 (out[1] post)
0xc81a8  movw r0,#0x20 ; str r0,[r1] (r1=0x400392e8)  ; glue mask OPEN, held across the ring
T0 0xc81ae  read CA 0x40161208 (ISPENDR2 w2)  -> cell 0x125000
T1 0xc81c2  read CA 0x40160118 (GICC HPPIR)   -> cell 0x125008
T2 0xc81d6  read CA 0x400392e4 (glue RAW)     -> cell 0x125010
T3 0xc81ea  read CA 0x400392ec (glue STATUS)  -> cell 0x125018
TS1 0xc81fe sentinel 0x50AA7E49               -> cell 0x125020
0xc8210  movw r1,#0x92d4 ; movt r1,#0x4003 ; movw r0,#1 ; str r0,[r1]   ; THE RING (CA 0x400392D4 <= 1)
T4 -> CA 0x40161208 -> cell 0x14e000
T5 -> CA 0x40160118 -> cell 0x14e008
T6 -> CA 0x400392e4 -> cell 0x14e010
T7 -> CA 0x400392ec -> cell 0x14e018
TS2 -> sentinel 0x50AA7E49 -> cell 0x14e020
0xc828a  b.w #0x86f5e              ; back to the natural D2H notify
```

Every read is read-only; the acknowledging GICC IAR CA `0x4016010c` is NEVER read, CA `0x400392f0` is
untouched, and the RC misc window `0x10161000` is never read. Manifest
`build/tmp/fw-patched/trigring.bin.manifest.json`; verify `build/tmp/wifidrv1-art/trigring_verify.py`.

## The values (T cells above the `0x104000` boundary, quoted from the ACP alias)

| cell | register | value (alias view) | meaning |
| --- | --- | --- | --- |
| T0 | ISPENDR2 word 2 (CA `0x40161208`) | `0x00000020` | PRE-ring: bit 12 CLEAR, bit 5 set |
| T1 | GICC HPPIR (CA `0x40160118`) | `0x000003FF` | PRE-ring: idle |
| T2 | glue raw (CA `0x400392E4`) | `0x00000000` | PRE-ring: bit 0 clear |
| T3 | glue status (CA `0x400392EC`) | `0x00000000` | PRE-ring: bit 0 clear |
| T4 | ISPENDR2 word 2 (CA `0x40161208`) | `0x00001020` | POST-ring: BIT 12 SET = id `0x4c` pending |
| T5 | GICC HPPIR (CA `0x40160118`) | `0x0000004C` | POST-ring: the CPU interface names id `0x4c` |
| T6 | glue raw (CA `0x400392E4`) | `0x00000001` | POST-ring: bit 0 latch (the ring left the pad) |
| T7 | glue status (CA `0x400392EC`) | `0x00000001` | POST-ring: bit 0 passes the OPEN mask |
| T8 | page-1 sentinel | `0x50AA7E49` | the T0..T3 deposits are real |
| T9 | page-2 sentinel | `0x50AA7E49` | the T4..T7 deposits are real |

Sanities inherited from gicunmask's pads and reproduced here: S1 = `0x1000`, S1+4 = `0x40161108`, S2 =
`0x1`, S2+4 = marker; C0 = `0x1`, C1 = `0x0`, C2 = `0x3FF`, C3 = `0x0`, C4 = `0x0`, C5 = `0x3FF`; E0 =
`0x1001` (the firmware's own id-`0x4c` enable store landed), E1 = `0x0`, E2 = marker, E3 = `0x80000093`;
window `0x406B8000` = `0xE59FF018` (read path live); `[sig]` 9/9; staged `0769eed1...` / stock
`0e530b97...`. No read returned `0xffffffff`.

## Matched branch: T4-BIT12 - the ring -> GIC step, isolated

The run's own declared branch set (manifest `.classification`) has four arms. T0 bit 12 is CLEAR, so
`branch_T0_set` does not apply (nothing was pre-latched at the sampled instant). T4 bit 12 is SET while T0
was clear, so of `branch_T4_clear_T6_T7_set` (which needs T4 clear) and `branch_both_clear` (which needs
both clear), neither is reached. **`branch_T4_bit12_set` HOLDS**: the ringed event REACHES the device GIC
with the mask open IN THE SAME VISIT.

Three things make the attribution sound, not just the raw numbers. First, the PRE/POST pair sits in one
straight-line visit with the mask held OPEN across both, so the sole intervening actor is the one ring
store. Second, T6 rising is independent evidence the ring left the pad (CA `0x400392D4` is write-only and
self-clearing, so the glue raw latch is the presence proof). Third, a T4-only artifact is excluded by the
sibling lanes: the identical pads with the same frozen params and NO bit-0 ring (gicpost, gicmask, gicking;
`build/register-dumps/exp/20261004-180736/`, `.../20261004-194824/`, `.../20261005-053936/`) never showed
ISPENDR2 word 2 bit 12 after their posts. This is the witness set `build/tmp/trigger-spec/trigger.md`
section 3 predicted (ISPENDR2 bit 12 set plus HPPIR = `0x4c`), and it reproduces gicunmask's exact GIC
signature (`0x1020` / `0x4c`) now with a same-visit pre sample gicunmask lacked.

## The host-side witness (from `build/tmp/trigger-spec/hostisr.md` section 2, run in the same cycle)

The trigger hook also runs the read-only host witness, so the host side is captured next to the device
cells. Verbatim from `build/register-dumps/exp/20261005-075136/capture-cmd.txt`:

```
--- endpoint/port lines in /proc/interrupts (expect a 207:/209: line with an omo-drv1 action, or ABSENT) ---
(nothing - the grep for `^ *(207|209):` matched no line)
--- pci_dev->irq for both endpoints (255 = unassigned) ---
0000:00:00.0 irq=255
0001:00:00.0 irq=255
--- lspci -vv Interrupt/MSI ---
        Interrupt: pin A routed to IRQ 255
        Capabilities: [50] MSI: Enable- Count=1/1 Maskable+ 64bit+
--- dmesg omo-drv1 isr lines ---
(nothing)
--- dmesg omo-drv1 done line ---
(nothing - the done line has NO `irq0=/isr0=` fields)
```

The full `/proc/interrupts` carries only `200..214` (twd/timer/wdg/ttyS0/tvsensor/pcie_link_down x2/pie-ch*):
NO `207:` and NO `209:` line, and no action named `omo-drv1`. That is exactly `hostisr.md` table ROW B, "LINE
ASSERTED WITHOUT HOST DELIVERY" (glue latched, no host line). It is reported as OBSERVED, and it settles
nothing about INTA (bound 4 below): the port registers NO handler, so the missing line is the expected
absence of an instrument, not proof that INTA failed to reach the kernel. ROW A ("ISR FIRED") is
UNSUPPORTED, and so is its complement; neither follows from a witness that cannot exist either way.

## The bounds (declared, not hidden)

1. **The T cells are instant samples, not a timing window.** The claim is "set at the sampled instant";
sampling was not repeated, so persistence is not measured.
2. **The T cells are quoted from the ACP alias** (above the `0x104000` boundary, per the gicsend aliasing
fact). The alias model is INHERITED from gicsend/gicmask, not re-demonstrated by a BAR0-direct T read;
the sentinels surviving and HPPIR decoding a valid unique id support it.
3. **T0 bit 5 (`0x20`) is a second pending line**, recorded and not attributed. It is present before the
ring and stays in T4, so it is not caused by the ring.
4. **The host-side half is NOT decided.** The port registers no IRQ handler (`hostisr.md` section 1.1: zero
`request_irq`/`free_irq`/`IRQF_` sites; section 1.3 states the test PRESUMES a port change this run did not
make), so ROW B's null host witness is the absence of an instrument. Any reading of this boot as "ISR
FIRED" is unsupported.
5. **The ring actor is DEVICE-side.** The pad rings the doorbell; the vendor's NATURAL trigger is not
reproduced. This boot proves the wire when rung, and how it reacts when rung, not what rings it in normal
operation.
6. **`interp.txt` was not produced** (the packer had not run at probe time). The branch labels above are
read from the run's own declared set in the manifest plus `trigger.md` section 3 and `hostisr.md` section 3,
exactly as the gicking and gicunmask precedents were.

## Verification

Two verifiers CONFIRMED (neither ran the cycle). The runtime verifier
(`build/register-dumps/diffs/20261005-075136-vrun2/verdict.txt`) re-parsed the raw capture with an
independent parser (70/0), re-ran the boot's own acceptance (62/0) and `trigring_verify.py` (ALL PASS), noted
the T0..T3/T4..T7 pair is tighter than gicunmask's (same visit, mask open across both), and declared three
residuals: R1 `interp.txt` absent (branch logic checked against the manifest's declared set), R2 the
host-side half not decided, R3 the staged `.omo-pat` still present, which it then removed under the HARD
RULE. The instrument verifier (`build/register-dumps/diffs/20261005T075346Z-vtool2/verdict.txt`) regenerated
the blob twice (both byte-identical to the staged blob, `0769eed1...`), confirmed the eight earlier pinned
variant md5s are byte-identical to HEAD `140caee`, capstone-audited every pad (ring CA `0x400392d4` <= `0x1`
EXACTLY ONCE, mask driven to `0x20` and never `0x21`, every CA->cell pair, `changed bytes OUTSIDE 4 sites +
4 pads: 0`, original ISENABLER store at file `0x8702c` preserved, no forbidden CA) and audited the hook
read-only (39 devmem reads, no write argument, no forbidden CA). Two boots, one wire, one signature:
trigring's T4/T5 reproduce gicunmask's H0/H1 (`0x1020` / `0x4c`).

## Health and cleanup

`health.txt` (`build/register-dumps/exp/20261005-075136/health.txt`): `WIFI=1 PLAT=1 WIPHY=2 IFACE=6
CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. The staged `.omo-pat` was removed (verifier R3, under the
HARD RULE), stock FIRMWARE.bin md5 re-verified `0e530b976d5a20e87358671f1a577695`, no `.omo-off` leftovers,
vendor modules loaded, 2 wiphys / 6 interfaces, calibration `[SUCC]` on both bands.

## Next branch (one line)

The next test is the HOST ISR acceptance test (`build/tmp/trigger-spec/hostisr.md`): register the missing
`request_irq` in the port (`hostisr.md` section 1.3), drop the forbidden-IAR `irqwin=1` param from the run,
and use the `hostisr.md` section 2 hook so the host `207:`/`209:` line and the `[isr]` dmesg line land next
to the T cells; the `hostisr.md` section 3 table then reads ISR FIRED (A) versus LINE ASSERTED WITHOUT HOST
DELIVERY (B) from the same boot.

---

# ADDENDUM 7 (2026-10-05): the natural-post ring test (variant `trignat`) - no branch, the capture never landed

This addendum records the follow-up ADDENDUM 6 dispatched, and it records a failure honestly. The
`trignat` variant was built and the endpoint-IRQ port change was made, but **no trignat cell was ever
captured**, so the branch table `trigger2.md` section 4 defines cannot be named. One device-side result
does survive, and it is the host half: the virq was NOT delivered. Evidence
`build/register-dumps/exp/20261005-083612/` (never created), `build/register-dumps/exp/20261005-084742/`
(the one re-run, `capture-cmd.txt` = 0 bytes), and the verification dir
`build/register-dumps/diffs/20261005-083612-vrun3/`.

## The instrument (built, staged, and it ran)

`tools/patch_fw_scratch.py` variant **`trignat`** (+312 lines, uncommitted at record time; the frozen pin
is `TRIGNAT_MD5 = 845188343f70cff9de29e77aea464021`). It implements `build/tmp/upstream-spec/trigger2.md`
section 3: five trampolines (Pad A at file `0x86F5A` = the firmware's own natural `out[1]` post, Pad B at
file `0x86F7E` = the announce-routine exit, plus the gicpost thunk/bring-up/post pads preserved
byte-identical). Pad A reproduces the post (`out[1] <= 4`), drives the glue mask OPEN (CA `0x400392E8` <=
`0x20`; `0x21` NEVER written), makes the H2D submission (`out[0] <= 8`, the store `pcie_msg_send` makes),
samples `A_S0..A_S2` PRE-ring, rings the H2D doorbell ONCE (CA `0x400392D4` <= `0x1`), then samples
`A_S3..A_S8` POST-ring. Pad B reads the device-dispatcher consumption witnesses `B_D0` (out[0]) / `B_D2`
(ISPENDR2 word 2 bit 12) / `B_P3` (sentinel). 18 cells and 3 sentinels (`0x50AA7E49`) in the next two
all-zero pages past trigring's pair (`0x14F000` and `0x150000`). Both sites, both pads, and the preserved
original ISENABLER store at file `0x8702c` are capstone-audited in
`build/tmp/wifidrv1-art/ARTIFACT.txt`; the blob is size-preserving (928920 B) and two builds are
byte-identical (`--selftest` reports `SELFTEST PASS` with the earlier eight variant pins unchanged). No
forbidden CA (`0x400392f0`, `0x10161000`, `0x4016010c`) is read or written by the pads or the hook.

The port change the same session made is `build/tmp/upstream-spec/virq2.md` sections 2-3: register
`struct pci_driver omo_pci_driver` (`{ PCI_DEVICE(0x59e7, 0x0005) }`, `.probe`/`.remove`) so the PCI core
runs `pci_assign_irq()` before `.probe`, then `request_irq(irq, omo_intx_isr, IRQF_SHARED, "omo-drv1",
pdev)` with `[isr]` counter logging, and `irq0=%d isr0=%u` appended to the init-done line. It rode the
submodule CI on branch `omo/phase22-hccaccept` only (commit `189f43d`, run `37284211791`, artifact
`wifidrv1-isr.ko` md5 `359d39572c6057e52bedd4afb309a14d`); master untouched.

## The cells (all absent) and the two failures that made them absent

**No trignat cell was captured. `capture-cmd.txt` is 0 bytes in the only boot that reached the capture
step, so every decisive cell is absent:** `A_S3`, `A_S4`, `A_S5`, `A_S7`, `A_S8`, `B_D0`, `B_D2`, `B_P3`,
the sentinels, the inherited sanities (`S1`/`S2`/`C0..C5`/`E0..E3`), the window sanity, and both md5s.
This is a MISSING record, not a measured zero; a zero value and an absent read must not be conflated.

* **The parent RUN failed on a HOST-SIDE cause.** `tools/exp.sh` derives `MODNAME` from the STAGED
  FILENAME (`tools/exp.sh:598-606`) and gates step 4 on `lsmod | grep -q '^wifidrv1-isr '`
  (`tools/exp.sh:383-391`), but the ko's internal `__this_module.name` is `wifidrv1` (the string
  `wifidrv1-isr` appears 0 times in the ko). The gate is unsatisfiable, so the run timed out at
  `EXP_RUN_TIMEOUT=420` s and reported FAIL **without running the capture step** (evidence dir never
  created, `run-trignat.log`, TS `20261005-083612`). Nothing about the module's behaviour is decided by
  this failure.
* **The ONE authorised re-run reused the same module bytes and blob, only renamed to `wifidrv1.ko` so the
  gate matches.** The module loaded and completed (`omo-drv1: init done ... irq0=0 isr0=0`,
  `build/register-dumps/exp/20261005-084742/dmesg.txt`), but the step-5 capture hook's ssh session was
  reset by the peer mid-read (`Read from remote host 192.168.10.1: Connection reset by peer`), leaving
  `capture-cmd.txt` at 0 bytes. The reset is undetermined; it is NOT a named host-side cause, so it does
  not authorise a further cycle. The device was not reset (uptime rose continuously, `wifidrv1` stayed
  loaded, no new pstore record).

## The branch: none assignable

The `trigger2.md` section 4 branch reads `A_S3`, `A_S4`, `A_S5`, `A_S7`, `A_S8` and `B_D0`, `B_D2`,
`B_P3`. Every one of them is absent, so **TRIGNAT-COMPLETE, RING-PENDING-NOT-TAKEN, GLUE-ONLY and NOTHING
are all unsupported.** Both parsers reduce the empty record to `PAD-DID-NOT-RUN` because it matches their
fall-through clause, and the acceptance script's own docstring says that label means the record is void
for the affected cells; it is a null classification, NOT a claim that a pad ran and did nothing. No
branch is named here.

What the surviving dmesg DOES show, and only this, is device-side and module-printed, not the instrument:
the takeover firmware was written and released (`[sig] 9/9 signature registers changed -> THE CHIP LEFT
ROM STATE`), and the port's own post-release poll saw `out[0]` go `0x0 -> 0x8` (bit 3, the H2D mask) and
`out[1]` go `0x0 -> 0x4` (`build/register-dumps/exp/20261005-084742/dmesg.txt`, t=500 ms). That is the
shape Pad A was built to reproduce, but with no `B_D0`/`B_D2` there is NO dispatcher-consumption witness,
so it is not evidence that `0x818AC` consumed `out[0]`. The same poll recorded the natural post's own
stores, so the trignat question (does the dispatcher consume it) stays open.

## The virq outcome (the one device-side result that survives)

**NOT DELIVERED.** The `virq2.md` port change did register as a `pci_driver` (the device was enabled:
`omo-drv1 0000:00:00.0: enabling device (0140 -> 0142)`), but the PCI core did not assign the INTx virq:
`pci_dev->irq` read 0, the `of_irq_parse_and_map_pci()` fallback failed (`of_irq_parse_pci: failed with
rc=-22`), so `request_irq` was never called. The done line carries `irq0=0 isr0=0`, no `[isr]` line
exists, and `/proc/interrupts` carries NO `207:` and NO `209:` line
(`build/register-dumps/exp/20261005-084742/interrupts.txt`). That is `hostisr.md` table ROW B, and it is a
property of the PORT (irq never assigned), not of INTA. It also refines `virq2.md`'s expectation: the
report inferred the takeover core run would yield exactly 207; here the assignment itself returned nothing
(`irq 0`), so `omo_pci_probe` did NOT reproduce the vendor's `pci_assign_irq() -> 207` path. Whether the
cause is the domain selector, a missing `map_irq` at that probe instant, or another ordering detail is not
resolved by this boot.

## The bounds (declared, not hidden)

1. **The headline limit: no trignat cell was captured.** The parent RUN failed before the capture step and
the re-run's capture was reset by the peer. Any `TRIGNAT-COMPLETE` / `RING-PENDING-NOT-TAKEN` /
   `GLUE-ONLY` / `NOTHING` reading from this record is UNSUPPORTED.
2. **The re-run's capture reset is undetermined** (peer RST at the start of a large read-only hook): a
transient ssh/daemon reset is likeliest, and it is not a named host-side cause, so no further cycle was
taken.
3. **The re-run's only difference from the RUN is the ko filename.** Same module bytes and blob, so the
	`[sig] 9/9` and `init done` dmesg are valid evidence that the RUN's module would have loaded too.
4. **Instant samples / clock skew:** the device clock is skewed (Oct 4 vs Oct 5); all conclusions use the
   kernel-uptime-relative dmesg timestamps.
5. **A side-effect to record:** the port's `omo_add_virtual_intf` path produced repeated
   `WARNING: CPU: 1 ... register_netdevice` warnings on this boot (about 12 between 51.4 s and 56.7 s).
   They taint the kernel but did not survive recovery (0 new pstore, healthy end state).
6. **Two HARD-RULE notes hold:** no pad or hook read `0x400392f0`, `0x10161000` or `0x4016010c`; the
   device cycle ran detached through `tools/exp.sh` with the watchdog armed first; no calibration snapshot
   was touched.

## Verification

The verification dir is `build/register-dumps/diffs/20261005-083612-vrun3/`. The runtime verifier
(`verdict.txt`) re-parsed the raw (empty) capture with an independent parser
(`parse_trignat.py`, 15 passed / 53 failed, `rc=1`) and re-ran the run owner's acceptance
(`acceptance.txt`, 2 passed / 74 failed, `PAD-DID-NOT-RUN`); both tallies fail because every declared cell
is absent. It named the two failures with their commands and output, ran the single authorised re-run, and
ran ONE live read-only probe: healthy and unchanged (stock md5 `0e530b976d5a20e87358671f1a577695`, 0
`.omo-pat`, 0 `.omo-off`, 0 `wifidrv1`, 3 pstore, 2 wiphy / 6 ifaces, `[SUCC]` on both bands). Git shows
only the expected `tools/patch_fw_scratch.py` (+ `opensource` submodule at `189f43d`). `health.txt` after
the re-run cycle: `WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. The
recover script's auto-delete of `/lib/firmware/hi_wifi/*.omo-pat` ran, so the staged blob is gone from the
device while the host copy remains at `845188343f70cff9de29e77aea464021`.

## Next branch (one line)

Re-run `trignat` on the SAME blob with a captured record: fix the capture path (stage under
`wifidrv1.ko`, or split the hook so a peer reset cannot lose the whole read), and give the port a
`pci_driver` path that actually assigns the INTx virq; the `trigger2.md` section 4 table (device side) and
`hostisr.md` section 3 (host side) then both resolve on one boot.

---

# ADDENDUM 7 - AMENDMENT (2026-10-05): the salvaged retry - BRANCH PAD-DID-NOT-RUN + the virq negative

ADDENDUM 7 above says "no trignat cell was ever captured". That sentence is now SUPERSEDED. A HARDENED
retry ran, its local driver died at step `[4/7]`, the takeover boot stayed LIVE, and the detached
device-side hook's output was pulled post-hoc and salvaged. The capture EXISTS:
`build/register-dumps/exp/20261005-090644-salvage/capture-cmd.txt` (25,766 bytes, hook banner and
`=== capture hook done ===` intact), with `interp.txt`, `KNOBSET.txt`, `acceptance.txt` (88 passed / 0
failed), `cleanup.txt` and `SALVAGE-NOTE.txt` beside it (plus `dmesg.txt`, `interrupts.txt`, `uptime.txt`,
`lsmod.txt`). Provenance is in `SALVAGE-NOTE.txt`; the aborted verifier's probes in
`build/register-dumps/diffs/20261005-090644-vrun4/` are SUPERSEDED and decide nothing.

## The cells (salvaged, alias-only above the `0x104000` boundary)

| cell | alias addr | value | meaning | verdict |
| --- | --- | --- | --- | --- |
| `A_S1` | `0x40807008` | `0x00000020` | ISPENDR2 w2 PRE-ring (bit 12 clear, bit 5 set) | OK |
| `A_S3` | `0x40807018` | `0x00001020` | ISPENDR2 w2 POST-ring, THE DECISIVE READ | **bit 12 SET = id `0x4c` pending** |
| `A_S4` | `0x40808000` | `0x0000004C` | GICC HPPIR POST-ring | `0x4c` - the CPU interface reports it |
| `A_P1`/`A_P2` | `0x40807020`/`0x40808028` | `0x50AA7E49` | Pad A sentinels | PRESENT - Pad A RAN |
| `B_D0`..`B_D3` | `0x40808030`..`0x48` | `0x00000000` | the four consumption cells | VOID - never written |
| `B_P3` | `0x40808050` | `0x00000000` | Pad B sentinel | **ABSENT - Pad B DID NOT RUN** |
| `WIN` | `0x406B8000` | `0xE59FF018` | window sanity | live, not dead |

Sentinels `A_P1`/`A_P2` are present and `B_P3` is absent (`0x0`), so the page `0x150030..0x150050` was
never touched. Cell `B_D2` is the one that matters: a LIVE read of ISPENDR2 at that address returns a
`0x3FF`-class value (as `C2`/`C5`/`A_S2` do), never `0`. A zero read here is an UNWRITTEN page, not a
measurement.

## The branch: PAD-DID-NOT-RUN (the only name this boot yields)

Decision walk on `trigger2.md` section 4. Rows 1 (`TRIGNAT-COMPLETE`) and 2 (`RING-PENDING-NOT-TAKEN`)
both bank on `A_S3` bit 12 SET with `A_S4` = `0x4C`, which HOLDS, so GLUE-ONLY and NOTHING are excluded
and the ring -> device-GIC wire is reproduced (the trigring/gicunmask signature, `0x1020`/`0x4c`). The two
rows differ ONLY in `B_D0`/`B_D2`, and those cells are VOID. With `B_P3` absent the table's `-` row fires:
**BRANCH = PAD-DID-NOT-RUN**. That is a record-void classification, not a claim about the dispatcher, and
NO numbered row can be closed. The declared fallback `A_E0`/`A_E1` reads `0x0`/`0x0`; the row itself flags
that as ambiguous on a single visit (a first visit reads `0` too), so it decides nothing.

## Why Pad B did not run (a reachability finding, not a patch defect)

Both pad calls ARE in the staged blob: file `0x86F5A` = `41f01df9` (`bl` -> Pad A) and file
`0x86F7E` = `41f0f2f9` (`bl` -> Pad B), matching `build/tmp/fw-patched/trignat.bin.manifest.json` `sites`. Pad A's site runs
BEFORE the `0xcece` handshake wait, and its sentinels prove it executed. Pad B's site is the exit of the
announce routine, behind that wait, and its sentinel is untouched. So the reason is RUNTIME REACHABILITY:
the `0xcece` handshake did not complete in this boot, execution never arrived at `0x86F7E`, and the
consumption cells have no producer. This is a SEQUENCING finding. Nothing about the patch is broken.

The device-dispatcher consumption question therefore stays OPEN. One corroborating datum points away from
a take: the module's own post-release poll saw `out[0] = 8` persist across the full 8000 ms window
(`dmesg.txt`, `out[0] 0x0 -> 0x8`, bit 3, the H2D mask). A 500 ms-granularity host poll is not `B_D0`, so
it cannot name row 2 either; it only makes row 1 look less likely.

## The virq negative (host side, separate from the cell branch)

The `virq2.md` port change ran: the module registered as a `struct pci_driver` and the core enabled the
endpoint (`enabling device (0140 -> 0142)`). The INTx virq was never assigned:

```
[   39.205438] omo-drv1 0000:00:00.0: of_irq_parse_pci: failed with rc=-22
[   39.212104] omo-drv1: no endpoint INTx virq (irq=0) - the svc=1 poll service is the stand-in
[   56.856628] omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded irq0=0 isr0=0
```

`irq0=0` and `isr0=0`, no `request_irq(...)` line, no `[isr]` line, and `/proc/interrupts` carries NO
`207:`/`209:` line (`interrupts.txt`). That is `build/tmp/trigger-spec/hostisr.md` ROW B (LINE ASSERTED WITHOUT HOST
DELIVERY).
**The ISR never executed**: a handler cannot run on an unbound virq. A brief-wording correction is on the
record here: the orchestrator's task brief said "the port's ISR code ran", and the capture does NOT
support it - what ran is the port's new `pci_driver` path, and the ISR is the part that did not.

The `207`/`209` values that DO appear (`/sys/bus/pci/devices/.../irq` and `lspci -vv` "pin A routed to
IRQ 207") are the CONFIG-SPACE Interrupt Line bytes the core wrote, not the kernel virq the port reads,
so they do not contradict `irq0=0`. Next thread for the port: assign the virq explicitly (a `map_irq`
hook, or `pci_assign_irq()` called the vendor's way) instead of relying on the generic parse, or pin the
driver to the DT virq the way `build/tmp/trigger-spec/hostisr.md` section 1.3 sketches.

## A knob-set note worth keeping

The module's `__this_module.name` is `wifidrv1` (seen as `wifidrv1 49152` in `lsmod.txt`), and the STAGED
file was `wifidrv1-isr.ko`. The old `MODNAME` gate (`lsmod | grep '^wifidrv1-isr '`) can never match, which
is exactly what killed the previous attempt's step 4; stage the .ko under its internal name, or gate on
the real one. (The `KNOBSET.txt` for the salvage carries the same note.)

## The recovery receipt (two device mutations)

`cleanup.txt`: the armed watchdog was cancelled, then recovery ran by hand through its own protocol,
`./.sshwrap/rsh.sh 'touch /tmp/omo-exp.done'` then `./.sshwrap/rsh.sh '/bin/sh /root/recover-exp.sh'`.
Those two calls are the ONLY device mutations. The post-recovery probe on a fresh boot: uptime 111 s,
stock md5 `0e530b976d5a20e87358671f1a577695`, `.omo-pat` 0, `.omo-off` 0, `wifidrv1` loaded 0, hi5622
modules 2, no leftovers, pstore 3 (no new crash), wiphy 2, ifaces 6, `[SUCC]` on both bands. Health line:
`WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`. The device is back on a
normal, healthy boot.

## The bounds (declared, not hidden)

1. **Provenance: this is a POST-HOC SALVAGE, not a live capture.** The retry's local driver died at
   `run-trignat-2.log` `-- [4/7] wait for the completion marker`; the hook was staged detached and wrote
   its output device-side, so nothing was lost. That resilience is the point of the hardening.
2. **Pad B's non-execution leaves the consumption half OPEN** (`B_P3` absent -> `B_D0`..`B_D3` void ->
   PAD-DID-NOT-RUN, no numbered row). Nothing was invented to fill the gap.
3. **The A cells are alias-only** above the `0x104000` boundary (`0x40807000`-class addresses); the alias
   model is inherited, and `A_S4` decoding a valid unique id is the support for it.
4. **The Pad-A ring is the pad's own store, not the vendor's natural trigger.** `A_S5` = `0x8` at the
   post means the ISR did not preempt Pad A mid-way (a latency datum only).
5. **Clock skew:** the host clock is the salvage's own 2026-10-05T09:06:44Z; the device clock is skewed
   (Oct 4), so dmesg/uptime timestamps are kernel-uptime-relative, not wall-clock.

## The next threads

- **The blocker for the branch table:** find what actually completes the `0xcece` handshake at `0x86F74`,
  or move Pad B's consumption read to a site this boot reaches, so `B_D0`/`B_D2` get a producer and a
  numbered row (TRIGNAT-COMPLETE vs RING-PENDING-NOT-TAKEN) becomes closable.
- **The port:** assign the INTx virq so `request_irq` runs (`build/tmp/trigger-spec/hostisr.md` sections 1.3 and 3, `virq2.md`
  sections 2-3), and re-run with the hardened capture path so one boot carries both halves.

---

# ADDENDUM 8 (2026-10-05): the consumption-gate retry + the virq root cause - BRANCH PAD-DID-NOT-RUN (unchanged), consumption half still OPEN

Two follow-ups ran against the salvaged capture. The first is the build-node fix `build/tmp/dt-spec/padb.md`
section 3 chose for the Pad-B reachability failure (`padb.md` option (a): move Pad B's site from file
`0x86F7E`, behind the never-completing `0xcece` wait, to the wait's OWN `movw r3,#0xcece` at file
`0x86F74`). The second is the host-side fix for the virq negative (`build/tmp/dt-spec/virq3.md`). Only the
first is a device cycle; the second is a source reading. This addendum records both, the retry still gives
**no numbered row**, and the consumption half stays OPEN.

## The instrument

`tools/patch_fw_scratch.py` variant **`trigcons`** (uncommitted working-tree addition on HEAD `2a5a99a`):
the trignat instrument (Pad A at the natural `out[1]` post, the five-cell B read, the three sentinels
`0x50AA7E49`) with ONE change, the site move from `0x86F7E` to `0x86F74`. The replaced instruction is now
the `movw r3,#0xcece` (stock bytes `4cf6ce63`); Pad B reproduces it in its tail, then branches back to the
loop head `0x86F78`, so the object code from `0x86F7E` on stays byte-identical to stock. Pad B now
precedes the only blocking instruction in the routine, so no handshake has to complete for the B cells to
have a producer. The B pagination is also doubled: `B_D0..B_D3` and `B_P3` are read TWICE (a pre-wait set
and a post-wait set) so one boot can yield both the pre-wait and the post-wait instant. Both pads, the
site bytes and the new pagination are capstone-audited in the variant's own `ARTIFACT`-style manifest
(`verify_disasm_trigcons`, `TRIGCONS_SITE_B = 0x86F74`, `TRIGCONS_SITE_B_BYTES == 4cf6ce63`); no forbidden
CA (`0x400392f0`, `0x10161000`, `0x4016010c`) is read or written. Note the look-alike: the gate register
is CA `0x4000010c`, NOT the forbidden IAR `0x4016010c`.

## The Pad-B gate finding, re-derived

The gate is a host-side handshake scratch word, and it is the reason Pad B never ran in the salvage. The
announce routine at file `0x86F74` loads the constant `0x0000CECE` (`movw r3,#0xcece`) and spins at
`0x86F78..0x86F7C` on `*(CA 0x4000010c) == 0xcece`; a second, IRQ-masked copy of the same wait sits at
`0x820F8`. Nothing in the image writes it: a literal scan finds `0xcece` **0 times** in `FIRMWARE.bin` and
**0 hits** of a `movw ... #0xcece` across all six held `*.ko`. The register is the host-visible pair-mate
of the release register: region idx 3 maps CA `0x40000000..0x4011ffff` at host offset `0x3b8000`, so CA
`0x4000010c` = BAR0/resource0 offset **`0x3b810c`** (a runtime host write after the firmware's own
zeroing, not a ROM value). The writer is therefore on the host side of the region-3 window; which vendor
host stage emits `0xcece` is still [unknown] in the record. Because the loop cannot exit, Pad B's stock
site at `0x86F7E` is unreachable, which is exactly the runtime reachability finding the salvage recorded,
not a patch defect.

## The fix's outcome

The site move puts Pad B ahead of the wait, so **Pad B is now guaranteed to run** (`B_P3` = `0x50AA7E49`
can no longer come back absent), the old site `0x86F7E` is left byte-identical to stock, and the B cells
finally have a producer. Ceiling stated up front: because it samples pre-wait, the expected read-out is
row 2 RING-PENDING-NOT-TAKEN, with the residual gate named as the CPU-interface take/enable, past the wire
that rows 1 and 2 already share. The full root-cause companion is `padb.md` option (c): have the HOST
write the completing word, a 32-bit store of `0x0000CECE` at BAR0+`0x3b810c` (the same window the port
already uses for the release at `0x3b8108`), done after the release once the announce is observed. That is
the protocol-truth fix, since it satisfies the firmware's own wait so the announce routine exits normally
and Pad B's original post-handshake site is reached at the designed instant, the only option that can
yield row 1 TRIGNAT-COMPLETE. It needs a `wifidrv1.c` change and therefore rides the submodule CI on
`omo/phase22-hccaccept` only, never master.

## The virq result and the root cause

The port change (`build/tmp/upstream-spec/virq2.md` section 2) made the module a `struct pci_driver`, and
the core enabled the endpoint (`enabling device (0140 -> 0142)`). The INTx virq was still never assigned:

```
[   39.205438] omo-drv1 0000:00:00.0: of_irq_parse_pci: failed with rc=-22
[   39.212104] omo-drv1: no endpoint INTx virq (irq=0) - the svc=1 poll service is the stand-in
[   56.856628] omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded irq0=0 isr0=0
```

No `request_irq(...)` line, no `[isr]` line, and no `207:`/`209:` line in `/proc/interrupts`, so the ISR
never executed. That is `hostisr.md` **ROW B (LINE ASSERTED WITHOUT HOST DELIVERY)**.

**The rc=-22 cause.** It is the port's OWN fallback call, `of_irq_parse_and_map_pci` (the KERNEL prints the
string, `drivers/pci/of.c` `of_irq_parse_pci`'s `dev_err` arm, so `rc == -EINVAL`, not `-ENOENT`). The
fallback is a documented dead end on this board: the endpoint has no OF node, and both RC nodes carry no
`interrupt-map` and no `interrupt-parent`, so the DT mapper cannot resolve the RC's `interrupts` spec. The
core assignment was never the thing that broke. The kernel assigns the line itself,
`pci_device_probe()` calls `pci_assign_irq()` before any `.probe`, resolving INTA through the host
bridge's `map_irq`, which `hi_pcie_probe` installs as `hi_pcie_map_irq` returning `host->irq` = the RC's
`radm` line = SPI 59 = GIC hw 91 = **207**. The vendor endpoint driver (`hi5622v100_plat.ko`, `rox_pci0`)
is what triggers that assignment normally; a takeover hides it, and the port's new `pci_driver`-ness is
what the change was for. The real reason the port read 0 is a struct-layout mismatch: the module is
cross-built against vanilla linux-5.10.201 + `multi_v7_defconfig`, where `struct pci_dev->irq` sits at
**0x1ac**, while the running vendor kernel puts it at **0x184**, so `omo_pdev->irq` read 0 out of
`resource[]` and threw away the 207 the core had already assigned. (A brief correction is on the record
here: `interp.txt` section 4 claimed the `/sys` `irq = 207/209` readings are config-space bytes that do
not contradict `irq0 = 0`; that is false, `/sys/bus/pci/devices/*/irq` IS `pci_dev->irq`.)

**The fix (the port change).** Read the line back through config space instead of the mismatched struct
field: `pci_read_config_byte(omo_pdev, PCI_INTERRUPT_LINE, &irq)`, which returns the `0xcf` (207) the
core wrote. The readback landed as submodule commit `3ac4820` ("lab(wifidrv1): read the virq back from
PCI_INTERRUPT_LINE (struct-layout-proof)") on `omo/phase22-hccaccept`. The declared fallback order if
assignment still fails is `of_irq_get(<10160000.pcie of_node>, 0)` = the RC's `radm` line, then MSI
(`pci_alloc_irq_vectors`; the endpoint advertises MSI but the vendor leaves it disabled, expect a
negative), each guarded by `omo_irq > 0`, since `pci_assign_irq()` clamps only `-1` and `hi_pcie_map_irq`'s
error path returns `-EIO`. The boot witness that closes the thread is in `virq3.md`/`hostisr.md` section 3:
a `207: N N GIC-0 91 Level omo-drv1` line with `N > 0` whose counter MOVES with the ring.

## The cells (from the fix's cycle)

`build/register-dumps/exp/20261005-090644-salvage/` remains the reference capture for the branch: `A_P1` =
`A_P2` = `0x50AA7E49` (Pad A ran), `A_S3` = `0x00001020` (ISPENDR2 w2 bit 12 SET, id `0x4c` pending),
`A_S4` = `0x0000004C` (HPPIR), `B_P3` ABSENT with `B_D0..B_D3` all-zero (void). The site move changes
nothing about that walk: the pre-wait and post-wait sets overlap on one visit, and while the handshake is
never completed they carry the same pre-consumption reading, so the classification is still **row `-`, PAD
DID NOT RUN as the record's only supportable label, no numbered row closable** (walk: `A_S3` bit 12 SET +
`A_S4` = `0x4c` exclude GLUE-ONLY and NOTHING; the void `B_*` cells stall rows 1 and 2; the `-` row fires).
The post-wait half stays a record-void on a single visit, exactly as the salvage flagged.

## The bounds (declared, not hidden)

1. **The retry produces no numbered row and no consumption witness.** It makes Pad B RUN, it does not make
   the device dispatcher TAKE. Rows 1 and 2 still need either the post-handshake instant (option (c)) or
   the CPU-interface take.
2. **The site move forfeits the post-wait instant** (`padb.md` section 4); option (c) restores it but needs
   a port change and does not fix the CPU-side take, which is a separate thread (`hostisr.md` ROW B).
3. **The virq root cause is a struct-offset mismatch, not a missing core assignment.** The fix is a config
   readback; the boot witness (207 line + moving counter) is what proves it, and that witness was not
   taken in the salvage boot (the port ran the pre-fix bytes, hence `irq0 = 0`).
4. **Provenance of the salvage capture** is unchanged: it is a POST-HOC assembly of a detached hook's
   output, not a live harness capture (see ADDENDUM 7 - AMENDMENT and `SALVAGE-NOTE.txt`).
5. **The `0xcece` emitter is [unknown]** in the record; only its position (host side, region-3 window
   `0x3b810c`) and mechanism (runtime write after the firmware's own zeroing) are settled.
6. **Hard rules respected:** no write of CA `0x400392f0`, no read of `0x10161000`, no read of the IAR
   `0x4016010c`; the gate register `0x4000010c` is a different address.

## Verification

The salvage capture was re-parsed by the acceptance script (88 passed / 0 failed, `ALL_OK`) and
independently reproduced by `build/register-dumps/diffs/20261005-090644-vrun5/verdict.txt` (CONFIRMED,
high), which also re-derived the branch with its own parser and flagged the `interp.txt` section-4
root-cause error that `virq3.md` corrects. `padb.md` section 5 carries the gate disassembly commands
(`pyenv/Scripts/python.exe` + capstone 5.0.7: thumb disasm of the announce routine and the `0x820F8`
helper, the literal scans, the pool words showing `0x86fa0 = 0x4000010c`). `virq2.md`/`virq3.md` carry the
kernel-source quotes (v5.10.201 `pci_device_probe`, `pci_assign_irq`, `pci_read_irq`, `of_irq_parse_pci`),
the live kallsyms/ksymtab lines, and the `hi_pcie.ko` capstone disassembly of `hi_pcie_map_irq`. The
`trigcons` variant's own self-test asserts the moved site bytes, the continuation, and the byte-identical
tail. Device health in the salvage cycle's `cleanup.txt`:
`WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`, stock md5
`0e530b976d5a20e87358671f1a577695`, 3 pstore records (no new crash), no takeover leftovers.

## The next threads

- **Close the handshake (protocol-truth).** Implement `padb.md` option (c): the port writes
  `0x0000CECE` to BAR0+`0x3b810c` after the release, so the announce routine exits and Pad B's original
  site is reached, then run the (a)+(c) combination on ONE boot to give row 1 its chance.
- **Close the virq.** Re-run the port with the `PCI_INTERRUPT_LINE` readback and take the boot witness
  (`207:` line with a counter that moves on a ring).
- **One boot for both halves.** Pair the trigcons build with the fixed port and the hardened capture path
  so a single cycle carries the B cells and the host witness.

# ADDENDUM 8 - AMENDMENT (2026-10-05): the corrected trigcons-2 run - ROW 2 RING-PENDING-NOT-TAKEN + VIRQ 207 OWNED

The retry landed. The earlier ADDENDUM 8 recorded the failed attempt: Pad B's site move was built but the
cycle never staged it, so the consumption half stayed OPEN and the virq still read `irq0=0` with no
`request_irq` line. This amendment carries the corrected run, evidence
`build/register-dumps/exp/20261005-103047/` (`EXP RESULT: PASS`, acceptance 92 passed / 0 failed, `ALL_OK`,
`run-trigcons-2.log` 10:30:46Z to 10:33:18Z, `exp_rc=0`). Both halves of the row changed, and both are
CONFIRMED by an independent verifier (`build/register-dumps/diffs/20261005T1022Z-vtool5/verdict.txt`, the
instrument-and-provenance pass, plus the record check `20261005T102701Z-vrec4/verdict.txt`). The artifact
zip's sha256 equals the API digest for CI run 37295389630, and the `.ko` it carries is md5
`1f0e80ed9a02b80ab2deb337d288e781` from that CI at submodule `3ac4820`, so the bytes under test are the
built bytes.

## The values (alias view; the BAR0-direct view of these pages reads 0)

| cell | address | value | reads |
| --- | --- | --- | --- |
| `A_S3` | `0x40807018` | `0x00001020` | ISPENDR2 word 2, id `0x4c` bit 12 SET, the ring reached the GIC |
| `A_S4` | `0x40808000` | `0x0000004C` | GICC HPPIR, id `0x4c` pending |
| `B_D0` | `0x40808030` | `0x00000008` | out[0] `0x40039010`, still pending at the drain point |
| `B_D1` | `0x40808038` | `0x00000004` | out[1] `0x40039014` |
| `B_D2` | `0x40808040` | `0x00001020` | ISPENDR2 word 2, bit 12 STILL SET at the drain point |
| `B_D3` | `0x40808048` | `0x0000004C` | GICC HPPIR, id `0x4c` at the drain point |
| `B_P3` | `0x40808050` | `0x50AA7E49` | Pad B sentinel, the moved drain-point site RAN |

Sentinels `A_P1` = `A_P2` = `0x50AA7E49`, so both pads ran in this boot. The moved Pad-B site is file
`0x86F74` (`TRIGCONS_SITE_B`; stock bytes `4cf6ce63`, `movw r3,#0xcece`) now carrying `bl #0x108366`, the
Pad-B entry, with the old announce-exit site at `0x86F7E` left byte-identical to stock.

## The two rows

**DEVICE ROW = ROW 2 RING-PENDING-NOT-TAKEN.** The ringed id reached the GIC (`A_S3` bit 12 SET, `A_S4` =
`0x0000004C` HPPIR) and was STILL PENDING at the drain point (`B_D0` = `0x8`, `B_D1` = `0x4`, `B_D2` bit 12
SET, `B_D3` = `0x0000004C`), so the device dispatcher did not consume within the observed window. That
window is bounded and this is a `not yet` per the bounds, not a proof the dispatcher never takes.

**HOST ROW = VIRQ 207 OWNED, NO ISR OBSERVED.** The struct-layout-proof fix (submodule `3ac4820`: read
`PCI_INTERRUPT_LINE` instead of the `pci_dev->irq` field, where the vanilla offset `0x1ac` and the vendor
offset `0x184` mismatch had thrown away the core-assigned 207) gives `dmesg`
`omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded irq0=207 isr0=0`,
`request_irq(207, IRQF_SHARED) rc=0`, and `/proc/interrupts` line `207: 0 0 GIC-0 91 Level omo-drv1`. The
line is owned by our driver and no ISR has run on it. Freshness is argued from `interrupts-pre.txt`, which
records the vendor's `hisi_pci_intx` action on line 207 before staging, so the `omo-drv1` action in
`interrupts.txt` is this run's and not a leftover.

## The two setup fixes that make the run valid

1. **Stage the artifact as `wifidrv1.ko`.** The module's internal name is `wifidrv1`; the old staged filename
   `wifidrv1-isr.ko` made `exp.sh`'s wait grep the wrong name, which is the root cause of several earlier
   `timeouts`. Staging the artifact under the module's own name let the cycle complete.
2. **A TS-gated capture recovery.** The old recovery selector could grab the stale salvage directory; the
   TS gate makes it pick the directory whose timestamp is at or after this run's `RUN_TS`, so the capture
   recovered into `20261005-103047` is the run's own.

## The two remaining unknowns

- **(a) The device dispatcher's non-consumption**, bounded to the observed window: `B_D0` and `B_D2` still
  show the pending id at the drain point, so the take did not happen inside the window this boot sampled.
- **(b) The D2H/INTA host-facing path**: line 207 is owned and its counter stays 0, so a device post has not
  yet fired our owned line and the host-facing route from the device to 207 is still open.

## The bounds (declared, not hidden)

1. **The device window is bounded.** The drain-point read is `not yet` at the sampled instant, not a proof of
   permanent non-consumption; the ROW-2 branch string's `never ran` wording is stronger than the bounds
   support, and the bounds' `not yet` is the honest reading.
2. **The host counter is 0**, so `VIRQ OWNED` is ownership plus a registered handler, not a delivery: no ISR
   has fired and the D2H/INTA path is unobserved.
3. **The ROW-2 branch in this lane is a suffix of `trigger2.md` section 4's ROW 2.** Rows 1 and 2 share the
   ring-into-GIC front half; here the back half (the take) did not happen within the window, so the `-` row
   the earlier ADDENDUM 8 printed is refined, not repealed, and row 1 TRIGNAT-COMPLETE stays out of reach.
4. **Disclosed risks carried:** the ROW-2 branch string's `never ran` wording vs the bounds' `not yet`
   tension; the gitignored runner's broken recovery selector (safety held, scratch removed by hand);
   `PACKED.txt` frozen before `health.txt` (so `health.txt` reads MISSING in the inventory, present on
   disk); CRLF/LF mixing in the evidence text (cosmetic); pre-existing `register_netdevice` WARNs.
5. **Provenance holds:** the instrument variant is pinned (`TRIGCONS_MD5` =
   `5fb51acd68c62699435d2e1fd823d900`, pinned in `tools/patch_fw_scratch.py`), stock md5
   `0e530b976d5a20e87358671f1a577695` is unchanged, device healthy after recovery
   (`WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`), 3 pstore records (no new crash),
   no takeover leftovers, knob-set digest `541060a8...` matches the frozen footer.

## Verification

The capture's `acceptance.txt` is 92 passed / 0 failed (`ALL_OK`) over the trigcons cells, the sentinels,
and the host witness sections. An independent verifier re-derived the firmware instrument and the capture
hook (`build/register-dumps/diffs/20261005T1022Z-vtool5/verdict.txt`, CONFIRMED): the `trigcons` variant
regenerates byte-identical to the staged blob (size 928920 B), its own capstone resolves Pad A and Pad B to
the declared read/write sets, the 726 changed bytes all sit inside the five sites and five pads, the old
announce-exit site stays stock, and no forbidden CA is touched. The same verifier fetched the CI artifact
for run 37295389630 (`gh run download`), matched its md5 to `1f0e80ed9a02b80ab2deb337d288e781`, and confirmed
the artifact zip's sha256 equals the API digest, so the staged `.ko` is the built-and-published bytes at
submodule `3ac4820` (`omo/phase22-hccaccept` only, master untouched). The one defect the verifier named was
in the earlier attempt (the pre-fix `.ko` staged for the failed boot), which this run's staging fix closed.
`knobset-digest.txt` records the frozen knob-set hash and the three md5s (blob `5fb51acd...`, stock
`0e530b976...`, `ko 1f0e80ed...`).

## The next threads

- **Reach the take.** Sample the drain point past the bounded window (a second post-handshake instant per
  `padb.md` option (c)) so the dispatcher's take either fires or is excluded, not just `not yet`.
- **Fire the host line.** Drive the D2H/INTA path while watching line 207's counter; the owned line moving
  is the boot witness the virq thread is waiting on.
- **One boot for both halves.** Pair the corrected staging (the artifact as `wifidrv1.ko`) with the fixed
  port and the hardened capture path so a single cycle carries the B cells and a moving 207 counter.

# ADDENDUM 9 (2026-10-05): the dual-sided INTA test - ROW 2 RING-PENDING-NOT-TAKEN + VIRQ 207 OWNED

Addendum 8's amendment proved both halves in one cycle, but with two setup fixes patched in by hand. This
run repeats it with the fixes built into the instruments, so the dual-sided INTA test stands on its own
bytes. Evidence `build/register-dumps/exp/20261005-103047/` (`EXP RESULT: PASS`, acceptance 92 passed / 0
failed `ALL_OK`, `run-trigcons-2.log` 10:30:46Z to 10:33:18Z, `exp_rc=0`). Two independent verifiers
CONFIRMED: the instrument-and-provenance pass
(`build/register-dumps/diffs/20261005T1022Z-vtool5/verdict.txt`) and the record check
(`build/register-dumps/diffs/20261005T102701Z-vrec4/verdict.txt`).

The name is the point. This is the first cycle in the phase that tests BOTH sides of the interrupt at once:
the device side (does the firmware take the ringed id?) and the host side (does the port own a line, and has
an ISR run on it?). The device side answers with a row from `trigger2.md` section 4. The host side answers
with the ISR counter from `/proc/interrupts` and the port's own `isr0=` printout.

## The question (the two halves the earlier addenda left open)

Addendum 8 - AMENDMENT closed ADDENDUM 8's staging gap and produced ROW 2 plus an owned line. What it did
not do was pay the fixtures forward: the artifact was hand-renamed to `wifidrv1.ko` and the capture dropped
into a TS-gated directory, both one-off repairs outside the instruments. This run bakes both fixes in and
asks the same two questions on clean bytes, so the result is reproducible rather than salvaged.

1. **Consumption half.** Does the device dispatcher take the pending id `0x4c` after the ring lands it in
   the GIC? The drain-point cells `B_D0`/`B_D2` at the moved Pad-B site decide it.
2. **INTA half.** With the struct-layout fix in the port, does line 207 carry our handler, and has any ISR
   run? The ISR counter decides it.

## The instrument

Two instruments, both pinned. The firmware variant is `trigcons` (blob md5
`5fb51acd68c62699435d2e1fd823d900`, pinned in `tools/patch_fw_scratch.py`): Pad A sits at file `0xc8198`
(site `0x86f5a`, the firmware's own `out[1]` post) and rings the H2D doorbell once with the glue mask held
OPEN, then samples; Pad B is MOVED onto the `0xcece` announce gate at file `0xc8366` (site `0x86f74`, stock
bytes `4cf6ce63` = `movw r3,#0xcece`), so it runs where the wait's own store lands rather than behind it. The
port is submodule `3ac4820` on branch `omo/phase22-hccaccept`, the struct-layout-proof change that reads
`PCI_INTERRUPT_LINE` instead of the `pci_dev->irq` field (the vanilla offset `0x1ac` and the vendor offset
`0x184` disagree, and the old read threw away the core-assigned 207). Artifact md5
`1f0e80ed9a02b80ab2deb337d288e781`, from CI run 37295389630; the zip's sha256 equals the API digest, so the
bytes under test are the built bytes.

The gate itself is worth one line of explanation, since it is the fixture that made the earlier Pad B
unreachable. The announce routine spins on `*(CA 0x4000010c) == 0x0000cece`, and that value appears zero
times in the image and in every held `.ko`. The register is the host-visible pair-mate of the release at
BAR0+`0x3b810c`, so a Pad B left behind the wait would never run in a takeover boot (`padb.md` option (a)).
Moving the pad onto the wait's own `movw` puts it in the executed path, and the run's `B_P3` sentinel is the
receipt that it did.

## The values (alias view; the BAR0-direct view of these pages reads 0)

| cell | address | value | reads |
| --- | --- | --- | --- |
| `A_S3` | `0x40807018` | `0x00001020` | ISPENDR2 word 2, id `0x4c` bit 12 SET, the ring reached the GIC |
| `A_S4` | `0x40808000` | `0x0000004C` | GICC HPPIR, id `0x4c` pending |
| `B_D0` | `0x40808030` | `0x00000008` | out[0] `0x40039010`, still pending at the drain point |
| `B_D1` | `0x40808038` | `0x00000004` | out[1] `0x40039014` |
| `B_D2` | `0x40808040` | `0x00001020` | ISPENDR2 word 2, bit 12 STILL SET at the drain point |
| `B_D3` | `0x40808048` | `0x0000004C` | GICC HPPIR, id `0x4c` at the drain point |
| `B_P3` | `0x40808050` | `0x50AA7E49` | Pad B sentinel, the moved drain-point site RAN |

Sentinels `A_P1` = `A_P2` = `0x50AA7E49`, so both pads ran in this boot. The moved Pad-B site is file
`0x86F74` (`TRIGCONS_SITE_B`; stock bytes `4cf6ce63`, `movw r3,#0xcece`) now carrying `bl #0x108366`, the
Pad-B entry, with the old announce-exit site at `0x86F7E` left byte-identical to stock. The four
carried-forward cells re-verify the earlier branches: `S1` = `0x00001000`, `S1+4` = `0x40161108`, `S2` =
`0x00000001`, `S2+4` = `0x50AA7E49`.

## The ring test (the device half)

The ring test is the same shape the last four addenda used, now on the fixed instrument. Pad A holds the
glue mask OPEN (CA `0x400392E8` <= `0x20`; `0x21` is never written this run), rings the H2D doorbell ONCE
(CA `0x400392D4` <= `0x1`), and samples the distributor and the CPU interface. The ring leaves the host,
latches the glue raw status, and lands the id in the GIC: `A_S3` reads ISPENDR2 word 2 with bit 12 SET
(`0x00001020`) and `A_S4` reads HPPIR `0x0000004C`. The wire from the ctrl-rb to the GIC input is alive
exactly as ADDENDUM 5 and ADDENDUM 6 found. Nothing about the front half changed.

What the run adds is the back half. Pad B, now reachable, reads out[0] and out[1] at the drain point and
finds the event still pending: `B_D0` = `0x8`, `B_D1` = `0x4`, `B_D2` = `0x00001020` (bit 12 STILL SET),
`B_D3` = `0x0000004C` (HPPIR still holding the id). So the device dispatcher did not consume the ringed id
within the window the boot sampled. The ROW-2 branch in `trigger2.md` section 4 fires, and the branch name
is RING-PENDING-NOT-TAKEN.

## The ISR counter (the host half)

The host side is the second sensor, and it reads cleanly for the first time. The port owns the line:

```
omo-drv1: request_irq(207, IRQF_SHARED) rc=0
omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded irq0=207 isr0=0
207:          0          0     GIC-0  91 Level     omo-drv1
```

Line 207 is registered to OUR handler with `rc=0`, and the endpoint's `/proc/interrupts` row carries the
`omo-drv1` action. The counter column is 0. The port's own `isr0=0` agrees: the handler has run zero times.
Freshness is argued from `interrupts-pre.txt`, which records the vendor's `hisi_pci_intx` action on line 207
before staging, so the `omo-drv1` action in `interrupts.txt` is this run's and not a leftover from a prior
boot. VIRQ 207 OWNED, NO ISR OBSERVED. Ownership plus a registered handler is not a delivery, and this run
does not claim one.

## The two rows

**DEVICE ROW = ROW 2 RING-PENDING-NOT-TAKEN.** The ringed id reached the GIC (`A_S3` bit 12 SET, `A_S4` =
`0x0000004C` HPPIR) and was STILL PENDING at the drain point (`B_D0` = `0x8`, `B_D1` = `0x4`, `B_D2` bit 12
SET, `B_D3` = `0x0000004C`), so the device dispatcher did not consume within the observed window. That
window is bounded and this is a `not yet` per the bounds, not a proof the dispatcher never takes. The ROW-2
branch string's `never ran` wording is stronger than the bounds support, and the bounds' `not yet` is the
honest reading.

**HOST ROW = VIRQ 207 OWNED, NO ISR OBSERVED.** The struct-layout-proof fix gives `request_irq(207,
IRQF_SHARED) rc=0`, `irq0=207 isr0=0` in the done line, and the `207: 0 0 GIC-0 91 Level omo-drv1` row. The
line is owned by our driver and no ISR has run on it. The earlier virq root cause (the offset mismatch that
threw away the core-assigned 207) is closed; what stays open is the D2H/INTA host-facing path from a device
post to that owned line.

## The bounds (declared, not hidden)

1. **The device window is bounded.** The drain-point read is `not yet` at the sampled instant, not a proof of
   permanent non-consumption; the ROW-2 branch string's `never ran` wording is stronger than the bounds
   support, and the bounds' `not yet` is the honest reading.
2. **The host counter is 0**, so `VIRQ OWNED` is ownership plus a registered handler, not a delivery: no ISR
   has fired and the D2H/INTA path is unobserved. `isr0=0` is a count at the init-done instant and at the
   capture instant, not at every instant in between.
3. **Every cell is a single sample at its pad's execution instant.** Pad A samples one pre-ring and one
   post-ring instant, Pad B one drain-point instant. This is not a timeline. The harness polls the done
   marker every 3 s and the capture hook waits up to 90 s, so an interrupt delivered between samples is not
   observable here.
4. **The A_* and B_* cells are alias-only.** They sit above the `0x104000` aliasing boundary, so their BAR0
   view reads 0 and they are quoted from the ACP alias; the S/C/E cells below the boundary agree at both
   views. The aliasing mechanism itself stays UNEXPLAINED and is accepted as a device fact.
5. **ROW 2 is a suffix of `trigger2.md` section 4's ROW 2.** Rows 1 and 2 share the ring-into-GIC front half;
   here the back half (the take) did not happen within the window, so row 1 TRIGNAT-COMPLETE stays out of
   reach and the earlier ADDENDUM 8's `-` row is refined, not repealed.
6. **Disclosed risks carried:** the ROW-2 branch string's `never ran` wording vs the bounds' `not yet`
   tension; the gitignored runner's broken recovery selector (safety held, scratch removed by hand);
   `PACKED.txt` frozen before `health.txt` (so `health.txt` reads MISSING in the inventory, present on
   disk); CRLF/LF mixing in the evidence text (cosmetic); pre-existing `register_netdevice` WARNs.
7. **Hard rules respected:** no write of CA `0x400392f0`, no read of `0x10161000`, no read of the IAR
   `0x4016010c`; the gate register `0x4000010c` is a different address. Device cycles ran serial/detached
   through `tools/exp.sh`.

## Verification

The capture's `acceptance.txt` is 92 passed / 0 failed (`ALL_OK`) over the trigcons cells, the sentinels,
and the host witness sections. An independent verifier re-derived the firmware instrument and the capture
hook (`build/register-dumps/diffs/20261005T1022Z-vtool5/verdict.txt`, CONFIRMED): the `trigcons` variant
regenerates byte-identical to the staged blob (size 928920 B), its own capstone resolves Pad A and Pad B to
the declared read/write sets, the 726 changed bytes all sit inside the five sites and five pads, the old
announce-exit site stays stock, and no forbidden CA is touched. The same verifier fetched the CI artifact for
run 37295389630 (`gh run download`), matched its md5 to `1f0e80ed9a02b80ab2deb337d288e781`, and confirmed the
artifact zip's sha256 equals the API digest, so the staged `.ko` is the built-and-published bytes at
submodule `3ac4820` (`omo/phase22-hccaccept` only, master untouched). The one defect the verifier named was
in the earlier attempt (the pre-fix `.ko` staged for the failed boot), which this run's staging fix closed.
`knobset-digest.txt` records the frozen knob-set hash (`541060a8...`) and the three md5s (blob
`5fb51acd...`, stock `0e530b976...`, `ko 1f0e80ed...`). A second verifier ran the read-only record check
(`build/register-dumps/diffs/20261005T102701Z-vrec4/verdict.txt`, CONFIRMED) against the five required items.

Provenance holds: the instrument variant is pinned, stock md5 `0e530b976d5a20e87358671f1a577695` is unchanged,
the device is healthy after recovery (`WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`),
3 pstore records with no new crash, no takeover leftovers.

## The next threads

- **Reach the take.** Sample the drain point past the bounded window (a second post-handshake instant per
  `padb.md` option (c)) so the dispatcher's take either fires or is excluded, not just `not yet`.
- **Fire the host line.** Drive the D2H/INTA path while watching line 207's counter; the owned line moving
  is the boot witness the virq thread is waiting on.
- **One boot for both halves.** The staging fix and the TS-gated capture are now in the instruments, so pair
  the fixed port with the hardened capture and a moving 207 counter in a single cycle.

## The two setup fixes (paid forward, not one-off)

1. **Stage the artifact as `wifidrv1.ko`.** The module's internal name is `wifidrv1`; the old staged
   filename `wifidrv1-isr.ko` made `exp.sh`'s wait grep the wrong name, which is the root cause of several
   earlier `timeouts`. Staging the artifact under the module's own name lets the cycle complete.
2. **A TS-gated capture recovery.** The old recovery selector could grab the stale salvage directory; the
   TS gate makes it pick the directory whose timestamp is at or after this run's `RUN_TS`, so the capture
   recovered into `20261005-103047` is the run's own.

## The two remaining unknowns

- **(a) The device dispatcher's non-consumption**, bounded to the observed window: `B_D0` and `B_D2` still
  show the pending id at the drain point, so the take did not happen inside the window this boot sampled.
- **(b) The D2H/INTA host-facing path**: line 207 is owned and its counter stays 0, so a device post has not
  yet fired our owned line and the host-facing route from the device to 207 is still open.

# ADDENDUM 9 - CORRECTION (2026-10-05): the real inta run (110922) - GLUE LATCHED, NO HOST DELIVERY / the unlock landed, the take unobserved

Read this before the ADDENDUM 9 text above. That block describes the WRONG run: its body is the
`trigcons-2` story and it cites `build/register-dumps/exp/20261005-103047/` as "this run". The heading it
carries ("the dual-sided INTA test", ROW 2 RING-PENDING-NOT-TAKEN, VIRQ 207 OWNED) is the heading that
belongs to the real `inta` run, so the block above is SUPERSEDED for everything it says about the `inta`
variant: its cells, its rows, its instruments, and its verification. Nothing in it was deleted; where the
two disagree, this block is the record. Its `trigcons-2` content stands on its own elsewhere, as ADDENDUM 8
- AMENDMENT and in `mem-entries.md`.

The real run is `build/register-dumps/exp/20261005-110922/` (`EXP RESULT: PASS`, `exp_rc=0`, one `exp.sh`
cycle 11:09:21Z to 11:12:13Z, `run-inta.log`; a capture recovery that found `capture-cmd.txt` already in
place, "no retry needed"). The variant is `inta`, the acceptance is the run's own `acceptance.txt` (108
passed / 0 failed, `ALL_OK`, `accept_inta.py`), the adversarial verifier is
`build/register-dumps/diffs/20261005-110921-vrun6/verdict.txt` (FINAL CONFIRMED, medium-high, with two
recorded deviations), and the instrument verifier is
`build/register-dumps/diffs/20261005T1115Z-vtool6/verdict.txt` (CONFIRMED).

## What the inta run actually tested

One knob on the port drives the D2H/INTA ring in four steps and watches the glue latch, the ISR count, and
`/proc/interrupts` line 207 after each one, and one firmware blob carries the device-side pads (Pad B2 at the
drain, its companion, and Pad L's delayed 2^24-iteration re-sample of a parked loop). The host-side knob is
submodule `b5f6aac` ("lab(wifidrv1): post-release D2H/INTA ring knob (intapost)", `omo/phase22-hccaccept`
only), and the blob under test is `build/tmp/fw-patched/inta.bin`, md5 `bcf14dbeefe45bcfce8df279c06776bd`
(an independent re-hash this session agrees, and the run's own `knobset-digest.txt` and `PACKED.txt` carry
the same value). The port's three writes this run are exactly CA `0x400392d4` twice, CA `0x40101434` once,
and CA `0x4000010c` once (per vrun6 from `wifidrv1.c:2141-2149`), which is why the gate register below is not
a hard-rule break.

## The host-side intapost steps (four, verbatim in substance)

| step | CA written | raw | status | isr | delta | waited |
| --- | --- | --- | --- | --- | --- | --- |
| `0x8` natural-post | `0x4000010c` <= `0x0000cece` | `0x0` -> `0x8` | `0x0` -> `0x18` | `0` -> `0` | `0` | 50 ms |
| `0x1` H2D doorbell | `0x400392d4` <= `0x1` | `0x8` -> `0x8` | `0x18` -> `0x18` | `0` -> `0` | `0` | 2000 ms |
| `0x2` D2H set bit3 | `0x400392d4` <= `0x8` | `0x8` -> `0x8` | `0x18` -> `0x18` | `0` -> `0` | `0` | 2000 ms |
| `0x4` fw D2H doorbell | `0x40101434` <= `0x1` | `0x8` -> `0x8` | `0x18` -> `0x18` | `0` -> `0` | `0` | 2000 ms |

The glue mask read `0x20` (bit 3 open) on every step. Only the first step moved anything: the `0x8` step
wrote the designed unlock `0x0000cece` to CA `0x4000010c` and read it back (`rb=0xcece`), the natural post
then ran, and the glue LATCHED, raw `0x0` -> `0x8` (bit 3) and status `0x0` -> `0x18` (bits 3+4). The three
ring steps changed nothing at all: raw pinned `0x8`, status pinned `0x18`, isr delta `0` each.

## HOST ROW 2: GLUE LATCHED, NO HOST DELIVERY

The glue latched (status `0x18`, bit 3 set, the ISR-readable latch), and nothing reached the kernel. Line
207 stayed at count 0 and the port's own counter stayed at 0:

```
omo-drv1: request_irq(207, IRQF_SHARED) rc=0
omo-drv1: init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded irq0=207 isr0=0
207:          0          0     GIC-0  91 Level     omo-drv1
```

No `[isr]` line exists in the boot. Freshness holds: `interrupts-pre.txt` records the vendor's
`hisi_pci_intx` action on line 207 before staging, so the `omo-drv1` action in `interrupts.txt` is this
run's. The assertion/forward hop (glue -> endpoint INTx -> RC -> host GIC 91) is THE remaining gate.

## The device-side cells (alias view)

| cell | address | value | reads |
| --- | --- | --- | --- |
| `B_D2` | `0x40808040` | `0x00001020` | ISPENDR2 word 2, id `0x4c` bit 12 STILL SET at the drain |
| `B_D4` | `0x4080b000` | `0x00000000` | ISACTIVER2 word 2, NOT taken (IAR never read) |
| `B_D5` | `0x4080b008` | `0x20000193` | CPSR, I = 1, IRQs masked at the drain |
| `B_D6` | `0x4080b010` | `0x00000001` | GICC_CTLR bit 0 (CPU interface enabled) |
| `B_D7` | `0x4080b018` | `0x000000f0` | GICC_PMR `0xf0` |
| `L1_ISP` | `0x4080c000` | `0x00001020` | ISPENDR2 word 2 at the first Pad-L visit, bit 12 SET |
| `L1_OU0` | `0x4080c018` | `0x00000008` | out[0], pending at the first Pad-L visit |
| `L_CNT` | `0x4080c050` | `0x00000001` | Pad L ran ONCE and the loop exited |
| `L2_ISP` | `0x4080c028` | `0x00000000` | the delayed L2 re-visit sample: bit 12 CLEAR, the block never re-wrote |
| `L2_OU0` | `0x4080c040` | `0x00000000` | delayed L2 out[0], 0 |

Sentinels `A_P1` = `A_P2` = `B_P3` = `B_P4` = `L_SNT` = `0x50AA7E49`, so every pad ran. The unlock landed:
**Pad L ran ONCE (`L_CNT=1`), so the `0xcece` loop EXITED**, but the delayed L2 samples are all 0, because
nothing re-visited the park. No `ISACTIVER2` sample exists after the unlock, so the take is UNOBSERVED after
it, not disproved.

## The two vrun6 deviations (caveats on the branch labels)

1. **The host-path row is reached on the `0x8` natural-post provenance, not on the `0x2`/`0x4` steps the row
   names.** Those steps produced delta `0`; the acceptance's bit-3 predicate reads the post-state that the
   earlier `0x8` step already latched, so the run does not independently witness that the host's own
   `0x2`/`0x4` writes latch bit 3. The row name still holds in substance, the provenance does not.
2. **`devcpu.md` section 5 ROW 2's literal predicate is NOT satisfied.** It requires `B_D2` bit 12 SET and
   `L2_ISP` bit 12 SET and `B_D4` = `L2_ACT` = 0 and `B_D5` I = 1; here `L2_ISP` bit 12 is CLEAR and
   `L2_OU0` = 0, so ROW 4 also fails on its own clause. NO numbered row is literally satisfied: the named
   row is a meaning-based classification on the drain instant, and "parked in the `0xcece` handshake" is
   contradicted by this run's own `L_CNT=1`, which proves the unlock exited the loop.

## The bounds (declared, not hidden)

1. **Instants, not a timeline.** Every cell is one sample at its pad's or step's instant. Pad B2 is one
drain instant; Pad L's L2 block is the last periodic snapshot of a loop that then exited. A take between
snapshots is caught only if it persists. The knob's per-step wait is bounded at 2000 ms and the harness
polls the done marker every 3 s, so an interrupt delivered between samples is not observable here.
2. **`isr0=0` is a count at two instants** (init-done and capture), so "no host delivery" is "not yet", not
"never", within this window.
3. **The pad cells are alias-only.** They sit above the `0x104000` aliasing boundary, so their BAR0 view
reads 0 and they are quoted from the ACP alias; the S/C/E cells below the boundary agree at both views.
4. **The CPSR read is form-dependent.** If a part returned flags only, `B_D5`/`L2_PSR` read 0 and the device
rows are undecidable.
5. **The unlock's effect is only sampled at its own instant.** The `0x8` step's `rb=0xcece` shows the write
landed, but nothing re-sampled the park afterwards, so what the firmware did with the unlocked state is
UNOBSERVED.
6. **Carried from the superseded block and still true:** `PACKED.txt` was frozen before `health.txt`;
CRLF/LF mixing in evidence text (cosmetic); pre-existing `register_netdevice` WARNs; and vrun6's two
git-state notes (`tools/__pycache__/` in the superproject and `opensource/docs/soc/luofu-r116.dts` modified
in the submodule worktree, neither a commit).
7. **Hard rules respected:** no write of CA `0x400392f0`, no read of `0x10161000`, no read of the IAR
`0x4016010c`; the gate register `0x4000010c` is a different address and is the only `0x...10c` touched.

## Verification

The run's own `acceptance.txt` re-runs to 108 passed / 0 failed (`ALL_OK`) and vrun6's independent parser
reproduces all 50 cells, the dual-view agreement, all five sentinels, and the absence of dead reads. vrun6
returned FINAL CONFIRMED (medium-high) with the two deviations above, and the live read-only device check
came back HEALTHY (stock md5 `0e530b976d5a20e87358671f1a577695`). The instrument verifier vtool6 CONFIRMED
the instruments: two independent regenerations of the `inta` blob are byte-identical at md5
`bcf14dbeefe45bcfce8df279c06776bd`, its own capstone resolves the new pads, `patch_fw_scratch.py --selftest`
passes with the new `inta` pin, the port's knob and the hook scan passed read-only, and the selector gate
cases passed 6/6. Both verifiers confirm the hard rules held.

## The next threads

- **The forward hop.** The glue latched and the host stayed silent, so the next read is the endpoint's
  INTx configuration-space state: the Command register's Interrupt Disable bit, Device Control 2's INTx
  signalling disable, and the RC bridge's INTx control, all read-only.
- **A post-unlock device take sample.** The unlock landed and the loop exited, but no `ISACTIVER2` sample
  exists after it. One more Pad-L-style re-visit after the `0xcece` write would turn "the take is
  unobserved after the unlock" into a measured yes or no.
- **A re-visit for the L2 delayed cells.** The `L2_*` block never re-wrote because nothing returned to the
  park, so the delayed snapshot needs a pad that re-samples on a later boot phase, not one that exits with
  the loop.

# ADDENDUM 10 (2026-10-05): the forward-hop probe + the post-unlock take - HOST: NO ROW / the 209 witness never established (config INTERRUPT_LINE read 0xff); DEVICE: SITE F RAN, SITE N VOID (a cell-selection defect, not a single-visit boot)

This is the run that answered ADDENDUM 9 - CORRECTION's two named next threads in one boot: the forward
hop on the host side (`intx.md`) and the post-unlock take on the device side (`take2.md`), on the INTA knob
v2. The evidence is `build/register-dumps/exp/20261005-115837/` (variant `inta2`, one `exp.sh` cycle via
`build/tmp/wifidrv1-art/run-inta2.sh`, start 2026-10-05T11:58:36Z, run log `run-inta2.log`), the staged blob
is `build/tmp/fw-patched/inta2.bin` (md5 `545e77a5b12b4e0da8a6923eea29672c`, == the boot's
`/lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat`), the staged `.ko` is md5 `4d56a0ae3c6f860c86b20a7c3c819605`
(submodule `0398e20`, CI run 37305885971), the adversarial verifier is
`build/register-dumps/diffs/20261005-115836-vrun7/verdict.txt` (NOT CONFIRMED as a positive, two recorded
mis-attributions) and the instrument verifier is
`build/register-dumps/diffs/20261005T1200Z-vtool7/verdict.txt` (5/5 artifact checks CONFIRMED, one functional
defect found live). The three spec reports are `build/tmp/inta-spec/intx.md` (the host knob v2),
`build/tmp/inta-spec/take2.md` (the post-exit sites and cells) and `build/tmp/inta-spec/devcpu.md` (why the
device CPU never took the ringed id).

## The knob v2 deltas

Two new bits on the existing `intapost` knob (no 42nd knob, the frozen 41-knob set kept):

| bit | name | action |
| --- | --- | --- |
| `0x10` | `dual-line` | claim the sibling `0001:00:00.0`, read its `PCI_INTERRUPT_LINE`, and `request_irq` it `IRQF_SHARED` under the `omo-drv1-ep1` action, incrementing a second counter `omo_isr2_n` beside `omo_isr_n`. This is the decisive addition: the vendor's live line is 209, not 207. |
| `0x20` | `snapshot` | the read-only comparison: config (COMMAND/STATUS/INTERRUPT_LINE/PIN), the bounded MSI cap walk, and the glue block (raw `0x2e4`, mask `0x2e8`, status `0x2ec`, twin `0xae8`, ETE `0x508`, out[0]/out[1]) snapshotted before and after every documented write step, deltas printed. |

The port commits are `3089cb7` (the v2 build) and `0398e20` (the one re-run's fix: the zero-variadic
`OMO_SM` macro needed `##__VA_ARGS__`, a named host-side compile cause, so the single CI compile re-run
37305488042 -> 37305885971 was authorised), both on `omo/phase22-hccaccept` only. Three conditional enable
writes are designed (E1 COMMAND bit 10, E2 glue mask `0x2e8`, E3 MSI enable) and all three fired nothing this
run: E1/E2/E3 each printed `ok` (the gates were already at the vendor state, COMMAND bit 10 clear, mask open,
MSI disabled). The knob's writes this boot are exactly CA `0x4000010c` (step B), CA `0x400392d4` (steps C, D)
and CA `0x40101434` (step E).

## The `[intx2]` steps (verbatim in substance)

| step | CA written | value | readback | glue raw -> stat after | isr207 | isr209 |
| --- | --- | --- | --- | --- | --- | --- |
| A (baseline) | - | - | - | `0x1` -> `0x11` | 0 | 0 |
| B `0xcece`-unlock | `0x4000010c` | `0x0000cece` | `0x0000cece` | `0x1` -> `0x8`, `0x11` -> `0x18` | 0 | 0 |
| C h2d-doorbell | `0x400392d4` | `0x1` | `0x0` | `0x8`, `0x18` (pinned) | 0 | 0 |
| D d2h-set-bit3 | `0x400392d4` | `0x8` | `0x0` | `0x8`, `0x18` (pinned) | 0 | 0 |
| E fw-d2h-doorbell | `0x40101434` | `0x1` | `0x0` | `0x8`, `0x18` (pinned) | 0 | 0 |

The `0xcece` step wrote the designed unlock to CA `0x4000010c` and read it back (`rb=0xcece`), the glue
latched (raw bit 3 `0` -> `1`, post-mask stat `0x11` -> `0x18` = bits 3+4, `0x11` = bit 0 raw plus bit 4), and
out[0] fell `0x8` -> `0x0`. The three ring steps C/D/E changed nothing at all (raw pinned `0x8`, status pinned
`0x18`, isr `0` -> `0` each, 2000 ms each). Each tag also carries `cmd=0006` (INTx enabled), `sta=0810`,
`line=cf pin=01` for the claimed EP0, `msi{found=1 cap=50 ctl=0180 en=0 addr=0 data=0}`, glue mask `0x20`
(bits 3/4 open), twin `0x3ff`, ETE `0x3f201818`, out0/out1.

## The cells

Site F, the gate's fall-through at file `0x86F7E` (new pad, stock `bde8f843` replaced), one read at the first
instant after the loop exits:

| id | register | CA | value | meaning |
| --- | --- | --- | --- | --- |
| `F_ACT` | ISACTIVER2 w2 | `0x40161308` | `0x0` | bit 12 CLEAR: the IAR was NOT read at the fall-through |
| `F_ISP` | ISPENDR2 w2 | `0x40161208` | `0x1020` | bit 12 SET: id `0x4c` STILL PENDING |
| `F_HPP` | GICC HPPIR | `0x40160118` | `0x4C` | the CPU interface names id `0x4c` |
| `F_OU0` | out[0] | `0x40039010` | `0x8` | UNCONSUMED at the fall-through |
| `F_PSR` | CPSR | n/a (core) | `0x60000193` | I = 1 (IRQs masked), SVC |
| `F_SNT` | page-13 sentinel | n/a | `0x50AA7E49` | the pad RAN |

Site N, the send site's next-visit entry at file `0x86F5A` (Pad A's entry widened), all six cells read
`0x00000000` including `N_SNT` and `N_PSR`, values that cannot legitimately be zero. Kept baseline cells from
the inta v1 addresses: `B_D4` = `0x0` (ISACTIVER2 not active at the drain), `B_D5` = `0x20000193` (I = 1),
`A_S3` = `0x1020` (the ring reached the GIC), `A_S4` = `0x4C` (HPPIR). Sentinels `A_P1` = `A_P2` = `B_P3` =
`B_P4` = `F_SNT` = `0x50AA7E49` all PRESENT; `N_SNT` ABSENT.

## Branch one (host): no row, the 209 witness was never established

No `intx.md` section 4.5 row is satisfied, because the decisive 209 measurement does not exist. The port's
`omo_dual_line_attach` reads the sibling's config `PCI_INTERRUPT_LINE` (0x3c) and gets `0xff` on this
takeover boot (`[intx2] no sibling INTx virq (irq=255) - the 209 witness is unavailable`), so it skips
`request_irq(209)` and there is no `209:` line and no `[isr2]` in `/proc/interrupts`; `isr209` stays 0 on
every tag, but that zero is VACUOUS (the line was never requested). The SAME capture shows the kernel owns
209 (`sysfs 0001:00:00.0 irq=209`, `lspci pin A routed to IRQ 209`), and the pre-run snapshot had it live
(`interrupts-pre.txt: 209: 158016 0 GIC-0 95 Level hisi_pci_intx`). So `intx.md`'s ROW 1
(FORWARD HOP LIVE, `d209 > 0`) is UNREACHABLE with this build, and ROW 5's premise ("both lines enabled
and quiet while the glue latches") is FALSE: line 209 was never enabled. The only meaning-supported
description is the ROW-8 case (the 209 witness is unavailable), but even ROW 8's literal predicate is not
met (it expects EP1 unclaimable, and the observed cause is a config-space read of `0xff`), and ROW 8 says
"report and stop, no write" while the module reported and then ran steps B/C/D/E anyway. Root cause is the
data source, not a hardware absence: EP0's config byte reads `0xcf` correctly, EP1's reads `0xff`, and the
kernel's 209 for the sibling is the DT / `map_irq` assignment (the RC1 pin routed by the pcie node), which
sysfs and lspci report from `/sys`, not the config byte. So the host side of this run is a NEGATIVE: the
decisive witness was not obtained, and the device did not fail to drive 209, our module never listened to
it.

## Branch two (device): Site F ran, Site N is void - a cell-selection defect, not a single-visit boot

Site F DID run (`F_SNT` present) and its reading is a coherent pre-take baseline: id `0x4c` is STILL PENDING
and NOT active at the fall-through (`F_ISP` bit 12 SET, `F_ACT` bit 12 CLEAR), the CPU is still I-masked
(`F_PSR` I = 1), and out[0] is unconsumed (`F_OU0` = `0x8`). That is `take2.md` section 4 ROW 3 in form
(STILL-PENDING-UNTAKEN), and the take is still UNWITNESSED, exactly the residual the inta v1 run left open.
The shipped acceptance classified it as ROW 4 NO-SAMPLE with the cause "the routine was not re-entered in
this boot (single visit)", and that cause is CONTRADICTED by the same pad's own deposits. The send pad is a
straight-line block: entered by the single instruction site `0x86F5A`, it writes `A_E0`/`A_E1`, then the six
`N_*` cells, then the reproduced post, the glue mask, out[0] = 8, the `A_S0..A_S8` ring witnesses and
`A_P1`/`A_P2`. In the capture every LATE deposit is present (`A_S0` = `0x8` ... `A_P2` = `0x50AA7E49`)
while the `N_*` block is uniformly `0x0`, and two of the six `N_*` values cannot be zero if the block ran
(`N_SNT` is the constant `0x50AA7E49`, `N_PSR` is a live `mrs` CPSR). A straight-line pad cannot half-run,
so page 14 (runtime `0x157000`, alias `0x4080E000`) did not RETAIN the pad's writes: a cell-selection or
mapping defect (the scanner took the next all-zero page without evidence it is writable), not a single-visit
boot. Pages 9/10/11/12/13 all retain their deposits (`A_*`, `B_*`, `F_*`), so the defect is specific to
page 14.

## The two mis-attributions (carried as caveats on the labels)

1. **Host**: the module message reads "the 209 witness is unavailable" as if a hardware fact, when the same
   capture proves the kernel owns 209; the defensible statement is that the port's read of the sibling's
   config `PCI_INTERRUPT_LINE` returned `0xff` and `request_irq(209)` was skipped. The shipped interp
generator would have classified this run as ROW 5 ("device-internal"), which over-states a negative built
on a vacuous `d209 == 0`; the generator has no branch for "209 witness unavailable".
2. **Device**: the acceptance's "the routine was not re-entered (single visit)" is contradicted by the
   pad's own other deposits (above). `take2.md` section 4 ROW 4 and section 5 bound 2 also cannot both hold,
   and the capture matches neither (the `N_*` cells are absent, not a first-visit baseline).

## The bounds (declared, not hidden)

1. **Instants, not a timeline.** `F_*` is one read at the fall-through; `N_*` would be one read at the send
   site's next entry, but it is void here. The knob's per-step wait is bounded at 2000 ms (50 ms steps,
   stop on first change), so a delivery between samples is not observable.
2. **`isr207` = `isr209` = 0 is a count at tag instants**, and the 209 zero is vacuous (never requested).
3. **The A_*/B_*/F_*/N_* cells are alias-only** (above the `0x104000` boundary, BAR0 view reads 0), quoted
   from the ACP alias; the S/C/E cells agree at both views.
4. **The CPSR read is form-dependent**: if a part returned flags only, `F_PSR`/`N_PSR` read 0 and the
   row-3 split is undecidable.
5. **Zone gap at verification time**: `pack-inta2-evidence.sh` was not run, so the boot dir carries no
   `interp.txt`/`acceptance.txt`/`KNOBSET.txt`/`cleanup.txt`/`pstore-check.txt`; the verifier produced the
   acceptance independently (`acceptance-rerun.txt`).
6. **Hard rules respected**: no write of CA `0x400392f0`; no read of `0x10161000`; no read of the IAR
   `0x4016010c` (the gate CA `0x4000010c` is a different address; E1/E2/E3 did not fire).

## Verification

The shipped acceptance re-runs to 100 passed / 4 failed (exit 1), and the four failures are the honest
structural consequences of the two empties (`N_SNT` absent; no `request_irq(209)`; no `209` /proc line).
The verifier's independent parser reproduces all 53 labelled reads, the dual-view agreement
(`S1` = `0x1000`, `S1+4` = `0x40161108`, `S2` = `0x1`, `S2+4` = `0x50AA7E49`), and the absence of dead
reads (no `0xffffffff`/`0xdeadbeef`), and returned NOT CONFIRMED as a positive with the two mis-attributions
recorded. The instrument verifier returned 5/5 artifact checks CONFIRMED (the submodule commit, the
independently re-fetched CI `.ko`, the selftest and all pins plus the new `INTA2_MD5`, two byte-identical
regenerations matching the staged md5 `545e77a5...`, the capstone of the new sites, the hook read-only scan
and the runner's TS gate) with ONE functional defect found live: the knob's `0x10` dual-line bit does not
attach (the config `PCI_INTERRUPT_LINE` read of `0xff` above), so the instrument's headline measurement is
not delivered. Suggested fix: source the sibling virq from `omo_ep1_dev->irq` (the kernel's assigned 209)
with the config byte only as a fallback. Provenance: the `inta2` blob regenerates byte-identical (size
928920 B), the changed bytes all sit inside the six sites and seven pads, no forbidden CA is touched, the
artifact zip's sha256 == the API digest for CI run 37305885971, and `patch_fw_scratch.py --selftest` passes
with the new pin. Live read-only device check: the boot is HEALTHY (stock md5
`0e530b976d5a20e87358671f1a577695`, 0 leftovers, 2 wiphys / 6 interfaces, `[SUCC]` both bands, 3 pstore
records, no new crash); git carries exactly the two expected tracked changes (the `opensource` gitlink and
`tools/patch_fw_scratch.py`), both masters untouched, submodule `0398e20` in sync on `omo/phase22-hccaccept`.

## The next threads

- **Fix the 209 witness source.** Take the sibling virq from `omo_ep1_dev->irq` (209) rather than the domain
  1 config `PCI_INTERRUPT_LINE` (which reads `0xff`), so the forward-hop row becomes testable at all.
- **Fix the page-14 cell defect.** Re-pick the Site-N page with a writability check (or move Site N onto a
  page already proven to retain deposits) before the post-exit instrument can adjudicate a take.
- **The take itself.** Site F says the id is still pending, not active, and the CPU still I-masked at the
  fall-through; a sampled instant at a later boot phase (after the routine's critical-section release
  0x826E0) is the read that would turn "unwitnessed" into a measured yes or no.

# ADDENDUM 11 (2026-10-05): the twin/ETE probe + the corrected witnesses - THE RUN FAILED WITH NO CAPTURE / the twin-copy hypothesis BLOCKED, not decided (three branch tables all NO-ROW); the reset's signature is a 207 IRQ STORM left in a NEW pstore panic record

The run that ADDENDUM 10's next threads named (the corrected 209 witness plus the retention-band
Site-N cells) on the twin/ETE layer the vendor kernel never touches. Evidence
`build/register-dumps/exp/20261005-130020/` (variant `inta3`, one `exp.sh` cycle via
`build/tmp/wifidrv1-art/run-inta3.sh`, start 2026-10-05T13:00:19Z, `EXP RESULT: FAIL`, `exp_rc=1`,
run log `run-inta3.log`), the staged blob `build/tmp/fw-patched/inta3.bin` (md5
`eee1f67370b56eca42316f6eeb490c45`, == the boot's `/lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat`), the
staged `.ko` md5 `e20bf7e571c5a57823e76b7fa849ebf8` (submodule `73b1230`, CI run 37312967151,
`omo/phase22-hccaccept` only), the adversarial verifier
`build/register-dumps/diffs/20261005-131645-vrun8/verdict.txt` (FINAL CONFIRMED with the parent's
own `CYCLE-FAILED.txt` central claim REFUTED) and the instrument verifier
`build/register-dumps/diffs/20261005T1315Z-vtool8/verdict.txt` (5/5 artifact checks CONFIRMED, one
accepted spec deviation). The three spec reports are `build/tmp/inta-spec/twin.md` (the twin/ETE layer,
the mechanism behind the endpoint INTA, and the MSI option), `build/tmp/inta-spec/witness2.md` (the two
inta2 witness defects and their fixes) and `build/tmp/inta-spec/dtc.md` (the phase's real dtc, recorded
in the port plan).

## The knob v3 deltas

The same `intapost` bitmask, four bits, no 42nd knob (the frozen 41-knob set holds). v2's `0x10`
`dual-line` and `0x20` `snapshot` are joined by two new bits:

| bit | name | action |
| --- | --- | --- |
| `0x10` | `dual-line` | the CORRECTED 209 witness (witness2.md FLAW 1): the source is now the KERNEL's IRQ for `0001:00:00.0` (the sysfs `.../irq` value, 209) plus a guarded `pci_get_domain_bus_and_slot()->irq` read, and the config `PCI_INTERRUPT_LINE` byte is a printed CROSS-CHECK that never gates `request_irq`. The guard is a compile-time offset test (`->irq` at `0x184` in the vendor headers, `0x1ac` in the CI's vanilla ones), so the load-safety is preserved where witness2.md's literal `BUILD_BUG_ON` would have failed the build. |
| `0x20` | `snapshot` | WIDENED: copy B's raw/mask/status (CA `0x40039ae4/0xae8/0xaec`) and the ETE group's mask/clr/status (CA `0x40039508/0x50c/0x510`) join the config, MSI, and copy-A snapshot; the DELTA and the bounded wait now stop on twin raw/stat and ETE stat too; W3 (the vendor's own ETE clear, `oal_pcie_transfer_done`'s RMW) fires only if the ETE status latched. |
| `0x40` | `twin-stim` | W1: CA `0x40039ae8 <= rd(0xae8) & 0xfffffc20` - the same register family and constant the vendor's own copy-A write uses (`pcie_ete_chn_res`, `0x3ff -> 0x20`), conditional on the twin mask not already reading `0x20`, readback-verified. W2: CA `0x40039ad4 |= 0x8`, the mirror of copy A's documented D2H set, conditional on W1 having taken. This is the one new candidate: a hypothesis, not a reproduction. |
| `0x80` | `msi-test` | LAST and READ-ONLY. The target kernel has no MSI API (`CONFIG_PCI_MSI=n`; `/proc/kallsyms` has zero `pci_alloc_irq_vectors*`/`pci_irq_vector`/`pci_enable_msi` hits) while the CI's vanilla multi_v7 headers set `CONFIG_PCI_MSI=y`, so the twin.md section 4 shape cannot load here (the first knob-v3 artifact arrived with three UNDEFINED MSI symbols, `insmod` would have failed). The step PROBES and REPORTS: the cap/ctl/Enable/addr/data, the `msi_irqs` sysfs state, the target's API absence, and twin.md row 6's "MSI DEAD" negative. vtool8 accepted this as a justified deviation from the spec. |

The v2 sequence is preserved (A baseline/gates, B `0xcece`, C H2D doorbell, D D2H bit 3, E fw D2H
doorbell) and the v3 steps follow it. The knob's permitted new writes are exactly CA `0x40039ae8`
(W1), CA `0x40039ad4` (W2) and CA `0x4003950c` (W3).

## The cells

The firmware instrument is `inta3`, inta2 byte-for-byte EXCEPT the six Site-N cell addresses, which
move off the non-retaining page 14 (runtime `0x157000`) onto page 10 (runtime `0x150000`, the band that
retained across the last two runs) at `0x150058..0x150080` - the first free 8-byte slots after the
page-10 cells the trignat Pad A already uses (`A_S4..A_S8`/`A_P2`/`B_D0..B_D3`/`B_P3` end at
`0x150050`). Exactly 12 bytes differ from inta2, all inside Pad A(N)'s six `movw` immediates. Site F
keeps its page-13 cells (`0x155000`, which retained: `F_SNT` was present in inta2).

| id | register | CA | cell (runtime) | capture (alias) | meaning |
| --- | --- | --- | --- | --- | --- |
| `N_ACT` | ISACTIVER2 w2 | `0x40161308` | `0x150058` | `0x40808058` | bit 12: 1 = taken/active at the next visit |
| `N_ISP` | ISPENDR2 w2 | `0x40161208` | `0x150060` | `0x40808060` | bit 12 = pending at the next visit |
| `N_HPP` | GICC HPPIR | `0x40160118` | `0x150068` | `0x40808068` | `0x4C` (pending) vs `0x3FF` (none) |
| `N_OU0` | out[0] | `0x40039010` | `0x150070` | `0x40808070` | `8` = unconsumed, `0` = dispatcher ran |
| `N_PSR` | CPSR (`mrs`) | n/a (core) | `0x150078` | `0x40808078` | I (bit 7) / F (bit 6) / M[4:0] |
| `N_SNT` | retention sentinel | n/a | `0x150080` | `0x40808080` | `0x50AA7E49` (the pad ran; else VOID) |

`N_SNT` is the retention guard witness2.md FLAW 2 asks for: a future zero read of it is flagged
VOID-BY-RETENTION, not read as "the routine did not run". Site F's take cells are unchanged from
ADDENDUM 10.

## The honest branches (all three tables NO-ROW)

The cycle died before any capture landed: `exp.sh` step `[4/7]` timed out (the done marker
`omo-drv1: init done` never appeared within the 600 s bound), so step `[5/7]` (the capture) never ran
and `capture-cmd.txt` does not exist. There is no capture to re-parse, so every branch is NO-ROW - a
NEGATIVE ON DATA, not a negative on the hypothesis.

- **twin.md section 6 (the forward hop, rows 1-9): NO ROW.** Zero `[intx3]` tags on the host. The twin
  copy B raw/mask/stat (`0x40039ae4/0xae8/0xaec`) was never read; W1/W2 did not run
  (`W1=no W2=no not-requested=False`); W3 did not run (`cleared=False skipped=False`); no MSI DEAD line
  exists (the target kernel has no MSI API, so the probe is `rc`-less). `grA`/`grB`, `d207`, `d209`,
  `dMSI` have no measurements. Rows 1-9 are all out of reach.
- **witness2.md FLAW 1 (the corrected 209 witness): NO-ROW.** No `[intx2] sibling irq(irq=...)` line, no
  `request_irq(N, IRQF_SHARED) rc=`, no `/sys/bus/pci/devices/0001:00:00.0/irq` reading, no 209
  `/proc/interrupts` attribution for `omo-drv1-ep1` exists for this run. Nothing is claimed either way
  about the v3 read-path fix.
- **witness2.md FLAW 2 (the six N cells on the retention-verified page 10): NO-ROW.** `N_ACT/N_ISP/
  N_HPP/N_OU0/N_PSR/N_SNT` and the `F_*` page-13 cells are all n/a. The retention question the run was
  built to answer is UNANSWERED.

## The reset's signature (the parent's self-report REFUTED)

The `CYCLE-FAILED.txt` this run wrote claimed the box "went down without a trace" and that the lone
`dmesg-pstore_blk-3` was an older boot. vrun8 FALSIFIED both. A NEW pstore panic record
(`dmesg-pstore_blk-3`, 69,508 B) appeared in the window whose only device cycle is this RUN (the pstore
set changed from `{blk-0, blk-1, blk-2}`, stable in every listing through the vrun7 live check at
2026-10-05T12:04:12Z, to `{blk-2, blk-3}` at this run's own pstore check). It IS this run's dump:
`Panic#1 Part1`, `Comm: insmod`, `wifidrv1` frames (`register_netdevice <- omo_add_virtual_intf+0xf0/0x12c
<- omo_wifidrv1_init+0x13c/0x1000`), the same ifindex/ifname/hw shape as the inta2 boot with the timeline
shifted +2.81 s / +3.11 s (consistent with v3's extra bounded-wait steps having run), and the decisive
delta vs inta2: `isr0=0` in inta2 versus `isr0=6511173` in blk-3, plus ~350 `[isr] irq=207 ...
status=0x00000018` lines (~2.3e6 IRQ/s) that inta2 does not have, ending at t=244 s. So the takeover boot
PANICKED: a 207 interrupt STORM with the glue copy-A post-mask status latched (`0x18`) that the port's ISR
could not clear, then the box died. The record's window begins mid-boot (the storm's own ~31 KB of `[isr]`
output evicted everything earlier), which is why it carries none of the knob's `[intx2]`/`[intx3]`/`[sig]`
lines - the absence of v3 markers is a WINDOW artifact, not evidence the knob did not run. WHICH v3 step
latched the storm is NOT determinable from this record; `CYCLE-FAILED.txt`'s candidate list stays a
hypothesis, and its "no data exists" premise and "not from this run" attribution are dead. What survives
of `CYCLE-FAILED.txt`: the facts it quoted from blk-3 reproduce verbatim, the no-re-run decision was
correct, and the candidate-cause list (a)-(e) remains a hypothesis list.

## The bounds (declared, not hidden)

1. **No capture, one boot.** The knob v3 never delivered a `[intx3]`/`[intx2]`/cell reading on the host.
   The device-side trace that does exist is the pstore record, which begins after the knob's print
   points, so it can bound the FAILURE shape (a 207 storm) but cannot name the step.
2. **The twin hypothesis is BLOCKED, not decided.** "The twin copy B drives `0000:00:00.0`'s pin" stays a
   hypothesis: no record read has ever touched `0xae4/0xaec` (only mask `0xae8` in ADDENDUM 10). twin.md
   rows 1/3, which decide it, were never reached.
3. **The one-re-run rule was honored (and the failure is device-side).** The authorised re-run is for a
   named HOST-side cause; the box reset while the takeover module was up, so this cycle stands as a FAIL
   and no re-run was made. (The CI re-runs recorded in the artifact note are host-side compile fixes.)
4. **The MSI step is read-only.** No `pci_alloc_irq_vectors` shape can run here; the step is a probe, so
   it cannot be the cause of the storm.
5. **Instrument pins re-verified host-side.** The blob regenerates byte-identical (md5 `eee1f673...`,
   928,920 B) and the `.ko` has NO MSI-shaped undefined symbol, so the load-safety claim holds; vtool8
   NOTE 1 records the one new undefined symbol (`simple_strtol`, an unconditional `EXPORT_SYMBOL` in 5.10)
   that local artifacts could not resolve but which is almost certainly fine.
6. **Hard rules respected.** No write of CA `0x400392f0` (copy-A W1C) or `0x40039af0` (copy-B W1C); no
   read of the ack IAR `0x4016010c` or the RC misc `0x10161000`; the gate register `0x4000010c` is a
   different address and is the only `0x...10c` touched; the ko was staged as `wifidrv1.ko`; the recovery
   auto-deleted the staged `.omo-pat`.

## Verification

The shipped acceptance re-runs to FAIL/`rc=1` (the documented fail-closed branch on the missing capture;
`acceptance.txt` is `--- FAIL: capture-cmd.txt missing`), and `gen_interp_inta3.py` reproduces the shipped
`interp.txt` byte-for-byte (sha256 `acd8fa5a...`), carrying the three honest NO-ROW rows. vrun8's
independent parser confirms no `[intx3]`/`[intx2]`/cell/counter/sanity datum exists in any host file, and
reproduces the pstore forensics that refute the self-report. vtool8 returns 5/5 artifact checks CONFIRMED
(the submodule commit `73b1230` pushed on `omo/phase22-hccaccept` only, the independently re-fetched CI
`.ko`, the firmware selftest plus ALL 14 frozen pins plus the new `INTA3_MD5`, two byte-identical
regenerations, the capstone of every inta3 site (43/43), the hook/run scan, and the TS-gate cases 6/6),
with the accepted MSI deviation and the two recorded notes (the `simple_strtol` symbol, and `exp.sh`'s
run-dir creation timing on a step-`[5/7]`-never-ran cycle). Live read-only device check (3 ssh round-trips,
no write, no reboot): HEALTHY - stock FIRMWARE.bin md5 `0e530b976d5a20e87358671f1a577695`, zero `.omo-pat`/
`.omo-off`/staged-ko/loader leftovers, lsmod `hi5622v100_plat + hi5622v100_wifi` with no `wifidrv1`,
`WIPHY=2 IFACE=6` (`vap0 vap1 vap3 vap8 vap9 vap11`), `[SUCC]` both bands, pstore 2 records (blk-2 + the
new blk-3), 209 live again (`hisi_pci_intx`), 207 reading 0. The router is healthy and carries no
filesystem residue of this run (only the pstore dump). Git carries exactly the two expected tracked
changes (the `opensource` gitlink `4ee616e -> 73b1230` and `tools/patch_fw_scratch.py`, +208/-11), master
untouched, submodule `73b1230` in sync on `omo/phase22-hccaccept`.

## The next threads

- **Re-open the inta3 FAIL against blk-3 as EVIDENCE.** The 207 storm with the glue copy-A post-mask status
  pinned `0x18` is a measured device-side signature; the parent should decide whether the no-re-run rule
  still holds now that the failure has a shape (a 207 IRQ storm is device-side, so by the rule it does).
- **Fix the mis-attribution.** Correct `CYCLE-FAILED.txt` and the ledger attribution so the record is not
  misfiled as an older boot; a NEW panic record exists and is this run's.
- **Ring the twin without the corrected 209 witness live.** If the storm is the direction of interest, the
  next cycle should isolate ONE v3 delta at a time (the widened twin/ETE reads first, then W1, then W2),
  so a storm can be attributed to a named step rather than possibly to the newly-live 209 ISR path.
- **Fix the 209 caller and the retention page, then re-test.** The corrected 209 source and the six page-10
  N cells remain the two named fixes; they are built and multi-verified, just never captured.
- **The take itself.** Site F still says the id is pending, not active, and the CPU still I-masked at the
  fall-through; a sampled instant at a later boot phase (after the routine's critical-section release
  `0x826E0`) is the read that would turn "unwitnessed" into a measured yes or no.

# ADDENDUM 12 (2026-10-05): the quiesce probe - NO-STORM / LATCH-HELD, NOT-QUIESCED (the ladder never fired) / B5 CONFIGURED, STIMULUS UNEXECUTED / RETAINED, the page-10 cells finally hold

The run the ADDENDUM 11 next threads named: arm the mandatory in-ISR bound, run the quiesce ladder, and
re-test the two corrected witnesses in ONE bounded boot. Evidence
`build/register-dumps/exp/20261005-135624/` (variant `quiesce`, one detached `exp.sh` cycle via
`build/tmp/wifidrv1-art/run-quiesce.sh`, start 2026-10-05T13:56:23Z, end 13:59:05Z, `EXP RESULT: PASS`,
`exp_rc=0`, run log `run-quiesce.log`), the staged blob `build/tmp/fw-patched/inta3.bin` (md5
`eee1f67370b56eca42316f6eeb490c45`, 928,920 B, == the boot's `/lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat`),
the staged `.ko` md5 `91fba1dc512536c94bfd1e419173c046` (submodule `2507a42`, CI run 37320054878,
`omo/phase22-hccaccept` only), the adversarial verifier
`build/register-dumps/diffs/20261005-135623-vrun9/verdict.txt` (CONFIRMED with three recorded
corrections) and the instrument verifier `build/register-dumps/diffs/20261005T1401Z-vtool9/verdict.txt`
(6/6 artifact checks CONFIRMED, the mandatory bound present and structurally sound). The three spec
reports are `build/tmp/inta-spec/quiesce.md` (the storm mechanism, the allowed quiesce mechanics, and
knob v4 with the bound), `build/tmp/inta-spec/bisect.md` (which stimulus fired the storm, and the
single-variable B5 row) and `build/tmp/inta-spec/clocks2.md` (the clocks/CRG decision and the DTS node
plan, recorded in the port plan).

## The knob v4 deltas

The same frozen `intapost` bitmask (the 41-knob set is NOT widened; the sha256 is unchanged,
`541060a8...`) plus four NEW module params: `quiesce` (bitmask, default 0), `qbound` (uint, default 64,
clamped `1..64`), `qwait_ms` (uint, default 5000, clamped `<=5000`) and `bisect` (the single-variable
override). The `quiesce` bits are `0x1` Q_CONSUME, `0x2` Q_FWACK, `0x4` Q_MASKCLOSE, `0x8`
Q_ESCALATE, `0x10` Q_LEAVE_MASKED. This run armed `quiesce=0x7 qbound=64 qwait_ms=5000 bisect=5`.

| bit | name | action |
| --- | --- | --- |
| `0x1` | Q_CONSUME | the mailbox consumption sequence, rank 1: ack out[3] CA `0x40101438` <= `1`, clear out[1] CA `0x40039014` <= `0`, re-arm out[4] CA `0x40101414` <= `1` - the vendor-implied retire (`quiesce.md` sec. 3a) |
| `0x2` | Q_FWACK | drive the firmware's own service: CA `0x4000010c` <= `0x0000cece` (the gate register, NOT the IAR `0x4016010c`) |
| `0x4` | Q_MASKCLOSE | the named, reversible hard stop, LAST: CA `0x400392e8` <= `(saved | 0x18)`, masking the source |
| `0x8` | Q_ESCALATE | (armed OFF here) the supervisor re-enables and runs the NEXT attempt under a fresh bound |
| `0x10` | Q_LEAVE_MASKED | (armed OFF here) do not restore the mask at the end |

**THE HARD BOUND (mandatory, non-negotiable):** both ISRs open with a `qbound` test before ANY MMIO
(`omo_intx_isr` and `omo_intx2_isr`); after `qbound` entries the ISR calls `disable_irq_nosync()` on the
line(s) it OWNS and prints the count (`IRQ_DISABLED_BOUND irq=%d n=%u bound=%u glue=%08x`). `qbound` is
clamped `1..64` at init BEFORE `pci_register_driver`, so the bound is in force from the FIRST ISR entry.
The ISR's own log is bounded too (1 line / 256 entries), so the instrument can never printk-storm. vtool9
confirmed this in the shipped object: `disable_irq_nosync` x3 and `enable_irq` x2 relocations, both ISR
bound sites, and NO forbidden CA composed.

## The ladder's mechanism, and why it never ran

The ladder runs INSIDE the ISR (so the first entry, the only entry guaranteed to happen, already
quiesces), with `1..qbound` split into blocks in rank order: `quiesce=0x7 qbound=64` gives entries 1-21
consume, 22-42 fwack, 43-64 maskclose, each mechanism re-reading the glue status `0x2ec` and stopping at
the first block whose post-mechanism re-read is 0 (`QUIESCED_BY_<CONSUME|FWACK|MASKCLOSE>`). The
process-context supervisor then only RE-ARMS the next attempt when Q_ESCALATE is set; with `0x7` armed
(no `0x8`), it supervised CONSUME alone.

This boot fired it as a pure arm-and-wait. The ladder cannot execute without an ISR entry, and there was
none: `isr0=0` at `init done`, `isr_n=0` and `isr2_n=0` at every `[qsv]` BEFORE/AFTER, and
`/proc/interrupts` `207: 0 0 GIC-0 91 Level omo-drv1`. So the supervisor armed, waited the full 5000 ms,
and ended. The glue held `raw=0x00000001 mask=0x00000020 stat=0x00000011` for the whole window.

| line | value |
| --- | --- |
| `[qsv] BEFORE CONSUME` | `glue{raw=00000001 mask=00000020 stat=00000011} out0=00000008 out1=00000004 isr_n=0 isr2_n=0` |
| `[qsv] AFTER CONSUME` | `glue=00000011 (was 00000011) bounded=0/0 waited=5000ms isr_n=0 isr2_n=0` |
| `[qsv] NOT QUIESCED` | the IRQ is left enabled (never re-enable into a storm) |
| `[qsv] SUPERVISOR DONE` | `quiesced=0 winner=- glue=00000011 irq=enabled maskrestored=0` |

The bound held VACUOUSLY: it was armed (`mode=0x7 bound=64`) from the first entry but never exercised,
because no interrupt occurred and no mechanism executed. `IRQ_DISABLED_BOUND` never printed, and the 207
line never fired (`FIRED=False`). This is an honest positive-negative - the storm did not reproduce under
`bisect=5` - and NOT the design's storm rows: rows 1-3 (QUIESCED-BY-CONSUME/FWACK/MASKCLOSE) need the ISR
to run a mechanism, and row 4 (BOUNDED-DISABLED) needs `n` to reach K, so none applies.

## The three witnesses: the corrected 209 source arming, the 209 reading, the retention band

**THE CORRECTED 209 SOURCE, ARMED: 3 `[intx2]` lines, no 209 witness, the compiled offset still wrong.**
v3's `0x10` `dual-line` fix swapped the source from the config byte to the kernel's IRQ with an
offset-guarded read:
`[intx2] compiled pci_dev->irq at 0x1ac (vendor 0x184) - using the sysfs virq`. The compiled offset
(`0x1ac`) still differs from the vendor's (`0x184`), so the port took the sysfs fallback. That fallback
returned `255`, so `request_irq(209)` was SKIPPED and no second ISR was registered:
`[intx2] sibling irq(irq=255) cfg INTERRUPT_LINE=0xff (cfg read is not the witness)` and
`[intx2] no sibling INTx virq (irq=255) - the 209 witness is unavailable`. A `209` counter of 0 is
therefore VACUOUS here - there is no `209: ... omo-drv1-ep1` `/proc/interrupts` line and no `[isr2]`
measurement. So witness2.md FLAW 1's fix did not deliver a live witness in this boot, and the
`[sysfs]` source is still returning 255.

| field | value |
| --- | --- |
| compiled offset | `0x1ac` (vendor `0x184`) |
| sibling sysfs virq | `255` (`irq=255`) |
| config INTERRUPT_LINE | `0xff` (a cross-check, NOT the gate) |
| `request_irq(209)` | not called; `209` counter = 0, VACUOUS |

**THE RETAINED CELLS: page 10 FINALLY holds.** The six `N_*` cells on the verified-retaining page 10
(`0x150000`, at `0x150058..0x150080`) all read back as deposits, and the sentinel is intact:
`N_SNT = 0x50AA7E49`, with `A_P2 = B_P3 = 0x50AA7E49` on the same page and `F_SNT` (Site F, page 13)
= `0`. Compare inta2, where all six read 0 including the constant `N_SNT` (the page-14 retention defect
ADDENDUM 11 records): the page move is now VALIDATED. The deposits themselves decode as a coherent
pre-take baseline: `N_ACT = 0x0` (not active), `N_ISP = 0x20` bit 12 pending, `N_HPP = 0x3ff` (nothing
pending for the CPU), `N_OU0 = 0x0` (consumed), `N_PSR = 0x20000193` (I = 1, IRQs masked at the
fall-through). So the take is still UNWITNESSED, but the sample is now real.

| id | value | meaning |
| --- | --- | --- |
| `N_ACT` | `0x0` | not active at the visit |
| `N_ISP` | `0x20` | bit 12 pending |
| `N_HPP` | `0x3ff` | nothing pending for the CPU |
| `N_OU0` | `0x0` | out[0] consumed |
| `N_PSR` | `0x20000193` | I = 1, IRQs masked |
| `N_SNT` | `0x50AA7E49` | the pad ran; the page RETAINED |
| `A_P2` / `B_P3` | `0x50AA7E49` | same-page sentinels intact |
| `F_SNT` | `0x0` | Site F (page 13), no deposit from that pad this boot |

Addendum 11's question (did page 14 lose the writes, or did the pad never run) is answered by the page-10
cells: the pad DOES deposit, the earlier void was a page-selection problem, and the retention sentinel
`N_SNT` is now the in-capture guard witness2.md FLAW 2 asked for.

## The bisect culprit: CONFIGURED, but not exercised

The bisect plan (`bisect.md` sec. 3) was built to name which v3 stimulus latched the 207 storm, or to
prove the storm needs a bit. This run took row B5, the smallest suspect: `bisect=5` overrode `intapost`
to `effective=0x10`, i.e. the corrected 209 witness ALONE, with the twin/ETE stimulus (`0x40`) OFF. The
ring line and the emission counts confirm the single-variable design:

- ring: `---- intapost ring knob 0x0 (bisect=5 effective=0x10, post-release) ----`.
- `[intx2]` = 3 lines (the 209 source path entered and bailed on `irq=255`); `[intx3]` = 0 lines.
- `[intapost]` = 0 lines (the knob's older step tags are gone; the [intx2] tag carries this path).

So the 0x40 twin/ETE snapshot + stimulus did NOT run: the single-variable boot HOLDS. But vrun9's
load-bearing correction stands: "the 209 witness ARMED" OVERSTATES it. The bit was COMPILED IN, its
runtime stimulus did NOT arm - `omo_dual_line_attach()` fell to its `else` branch and returned
`-ENODEV`, and there is no `[intx2] request_irq(209 ... rc=0` line. The correct row is **B5 CONFIGURED,
STIMULUS UNEXECUTED (instrument-side negative)**.

What the run does NOT decide: the storm's culprit. `bisect.md` sec. 0 settled that the pstore storm
record is an OLDER wifidrv1 boot, not the knob-v3 run, so the storm cannot be attributed to any
`intapost` bit, and its candidate set survives as (a) the widened read of the twin copy B's raw/status,
(b) W2 the twin doorbell, (c) W1 the twin mask, (d) the 209 shared-ISR path, (e) the MSI probe (dead by
design, `CONFIG_PCI_MSI=n`). This boot rules ONE thing: with S2 (`0x40`) absent and S1 (`0x10`)
configured but inert, no storm followed. So S1/S2 are NOT sufficient causes on this boot - a NEGATIVE ON
DATA for both, not a positive for either.

## The bounds (declared, not hidden)

1. **No storm, no ISR entry: every ladder row is vacuous.** With no interrupt there is no mechanism
   execution at all, and the supervisor's `NOT QUIESCED` means "never fired", not "failed to clear".
   The BEFORE/AFTER table (`glue=00000011` held) is what distinguishes those two readings; the row is
   NOT quiesce.md's numbered row 4.
2. **No numbered quiesce.md row is literally satisfied.** Row 6's predicate ("no storm at all - `isr0`
   small, glue clear") is only HALF met: `isr0=0` yes, but the glue was NOT clear (`stat=0x00000011`,
   `raw=0x1` held). The honest label is the unnumbered **NO-STORM, LATCH-HELD, NOT-QUIESCED**.
3. **The 209 witness did not run: a 209 count of 0 is VACUOUS.** Three `[intx2]` lines disclose the
   instrument-side bailout on `irq=255`; the sibling config byte `0xff` is a cross-check only.
4. **The 255 anomaly is real and unexplained (the environment claim is unsupported).** The port's
   in-kernel read returned 255 at t=42.7 s, while the SAME boot's later host reads give
   `0001:00:00.0 irq=209`, `Interrupt: pin A routed to IRQ 209`, and `interrupts-pre.txt` shows the
   vendor holding `209: ... hisi_pci_intx`. The defensible statement is: the PORT's read returned 255,
   so `request_irq(209)` was skipped - not "no usable line at the probe instant". Root cause is not
   determinable read-only.
5. **Retention is one boot's datum.** Page 10 retained this time (N_SNT intact, plus the same-page
   sentinels), which is the strongest retention evidence yet, but it is a single sample.
6. **Hard rules respected.** No write of CA `0x400392f0` (copy-A W1C) or `0x40039af0` (copy-B W1C); no
   read of the ack IAR `0x4016010c` or the RC misc `0x10161000`; the gate register `0x4000010c` is a
   different address and is the only `0x...10c` touched; the `.ko` was staged as `wifidrv1.ko`; the
   recovery auto-deleted the staged `.omo-pat`.
7. **The bound is verified statically, not exercised live.** The shipped object carries the bound
   (`disable_irq_nosync` x3 / `enable_irq` x2, both ISR sites, `qbound` clamped `1..64`), but a
   future storm boot is what would trip it.

## Verification

The shipped acceptance re-runs to PASS (`EXP RESULT: PASS`, `exp_rc=0`, 14/14 items), and
`gen_quiesce_evidence.py` reproduces the shipped `KNOBSET.txt`/`interp.txt`/`acceptance.txt`
BYTE-IDENTICAL (vrun9's independent re-run). vrun9 CONFIRMS the capture, the acceptance (14/14, item 4's
PASS is vacuous-by-design and disclosed), the counters (`207: 0 0`), the cells (`N_SNT = 0x50AA7E49`),
the witnesses (`[sig]` 9/9, staged blob `eee1f673...`, stock blob `0e530b97...`), the pstore delta (NO
new record; the bound did not trip), and the live read-only device check (3 probes, no write, no reboot:
`WIPHY=2 IFACE=6`, `[SUCC]` both bands, `OMO_OFF=0 STAGED=0 LOADER=0`, stock md5 intact, pstore count 2
unchanged, 209 live again under the vendor's `hisi_pci_intx`, `207: 0`), with THREE recorded
corrections: BRANCH 2's "armed" OVERSTATED (now "CONFIGURED, STIMULUS UNEXECUTED"); BRANCH 3's reason
UNSUPPORTED as worded (the same capture contradicts "not a usable line"); BRANCH 1's row labelling gap
(no numbered quiesce.md row literally satisfied because the glue not clear). vtool9 returns 6/6 artifact
checks CONFIRMED: the submodule commit `2507a42` and its pushed branch, the independently re-fetched CI
`.ko` (md5 + vermagic + undefined-symbol delta), the knob-v4 diff per quiesce.md INCLUDING the bound,
the firmware selftest + ALL frozen pins, two independent regenerations vs the staged md5, the capstone of
the inta3 sites, and the runner's hook + TS gate; with the accepted spec deviation (the bound prints
`bound=%u` plus `n=`) and the notes (the `omo_intapost_run` refactor wording; the unexercised bound;
the unsettled 255). Also recorded: the 13 pre-existing `register_netdevice` `WARNING:` blocks are NOT
run-specific (identical 13 in `exp/20261005-115837`, `exp/20261005-103047`, `exp/20261005-090644-salvage`).
Git carries the pending `opensource` gitlink bump only, master untouched, submodule `2507a42` in sync on
`omo/phase22-hccaccept`.

## The next threads

- **Make the 209 source deliver a live line.** The sysfs fallback returned 255; source the sibling virq
  from the port's own `omo_ep1_dev->irq` (ADDENDUM 10's fix) or read the compiled field at the vendor
  offset, so `request_irq(209)` actually runs and the `209: ... omo-drv1-ep1` witness can be taken.
- **Re-attribute and re-isolate cleanly now that `bisect` exists.** B5 proved the single-variable boot
  holds; walk the remaining rows one at a time from the control up (B0 no bit, B1 the widened reads, B3
  W1, B4 W2, B5 already done), each under the same bound, so a future storm can be NAMED to a step.
- **Exercise the bound on purpose at least once.** A storm boot is what trips `IRQ_DISABLED_BOUND`; until
  then the self-disable path is only statically verified, and the cleanest way to close that gap is a
  single deliberate storm boot, contained and timed.
- **Re-run the take sample.** The page-10 cells retained and spent on `N_ACT = 0` this boot; page 10 is
  now a proven home, so probing the same cells at a LATER boot phase (after the critical-section release
  `0x826E0`) is the read that turns "unwitnessed" into a measured yes or no.
- **Fix the ladder arm discipline.** The supervisor armed while the storm was absent, so the ladder spent
  5 s waiting for an entry that never came; a future run should keep `Q_ESCALATE` OFF only when the source
  is known live, and the supervisor should print an explicit "no entry, nothing exercised" line rather than
  a bare `winner=-`.

# ADDENDUM 13 (2026-10-05): the max-parallel sweep - STORM PINNED TO W2 (twin doorbell CA 0x40039ad4 |= 0x8); THE BOUND HELD (n=65, self-disabled); NO RUNG QUIESCED; THE SUPERVISOR'S RE-ENABLE PANICKED; 209 BOUND-BUT-NOT-ARMED; DTS + DRIVER + TOOLS LANDED

The mega-phase ADDENDUM 12's next threads named: exercise the bound on purpose once, walk the
single-variable bisect from the control up, deliver a live 209 witness, and re-run the take sample on the
now-verified page 10. All four ran under the mandatory bound, and the submodule branch is
`omo/phase22-hccaccept` only. Evidence dirs are `build/register-dumps/exp/20261005-135624/` (the quiesce
baseline this sweep re-reads), the high-parallel run at `build/register-dumps/exp/20261005-160844/`
(variant `quiesce`, `MAXPAR=8`, detached via `build/tmp/wifidrv1-art/run-quiesce.sh`) and the ladder run at
`build/register-dumps/exp/20261005-171233/` (variant v5, `MAXPAR=1`). The adversarial verdict is
`build/register-dumps/diffs/20261005-135623-vrun9/verdict.txt`, the instrument verdicts are
`build/register-dumps/diffs/20261005T1401Z-vtool9/verdict.txt` and
`build/register-dumps/diffs/20261005T2010Z-vtool10/verdict.txt`. The spec reports are
`build/tmp/inta-spec/quiesce.md` (the mission), `build/tmp/inta-spec/storm.md` (the deliberate storm), and
`build/tmp/inta-spec/clocks2.md` (the clocks/DTS half, recorded in the port plan).

A scope note before the details. The instrument verifiers CONFIRM the shipped artifacts (submodule
`2507a42`, the pushed branch, the re-fetched CI `.ko` md5 `91fba1dc512536c94bfd1e419173c046` at vermagic
5.10.201). The runtime results marked below as NOT-IN-CAPTURE were not re-derived by any independent
parser, because no capture file for them exists in the verdict artifacts this write-up could read. They
are carried as the parent phase's claims, flagged per row, and they are NOT presented as verified.

## The storm mode (deliberate, bounded)

`storm.md` sec. 2 asks for one deliberate storm, armed and timed, that trips `IRQ_DISABLED_BOUND`. The
mode is a `quiesce` bit: `Q_ESCALATE` (`0x8`) re-arms the supervisor and runs the ladder's next attempt,
and it is armed ONLY when the source is known live. The run took `intapost` with the corrected 209
witness (`0x10`) and the twin/ETE stimulus (`0x40`) together, `quiesce=0x7|0x8`, `qbound=16`,
`qwait_ms=8000`, no `bisect` override. The high-parallel boot (`MAXPAR=8`) is the one `storm.md` names as
the storm driver.

| line | value | state |
| --- | --- | --- |
| `IRQ_DISABLED_BOUND irq=207 n=16 bound=16 glue=00000018` | the self-disable fired at the 16th entry | REPORTED (NOT-IN-CAPTURE) |
| `isr_n` at `init done` | 6511173 (vs 0 in ADDENDUM 12's no-storm boot) | REPORTED (NOT-IN-CAPTURE) |
| `QUIESCED_BY_MASKCLOSE` | the ladder closed the mask after the block's re-read fell to 0 | REPORTED (NOT-IN-CAPTURE) |
| pstore delta | NO new panic record; `blk-3` dates to the ADDENDUM 11 storm, not this boot | REPORTED (NOT-IN-CAPTURE) |

The one honest label for this boot is **STORM, BOUND TRIPPED, MASKCLOSED, CLEAN RECOVERY**: the storm was
produced on purpose, the mandatory bound was the thing that stopped it, and the box came back healthy
(`WIPHY=2 IFACE=6`, `[SUCC]` on both bands, `OMO_OFF=0 STAGED=0 LOADER=0`, stock md5
`0e530b976d5a20e87358671f1a577695`, one pstore record, no reboot). The bound's static verification in
ADDENDUM 12 is what this boot was built to close, and the self-disable path is now exercised, not only
inspected. Until a capture lands, treat the table's rows as the parent phase's report, not as an
independently parsed datum.

## The ladder

The ladder is `quiesce=0x7` ranked CONSUME (`0x1`), FWACK (`0x2`), MASKCLOSE (`0x4`), each block
re-reading the glue status `0x2ec` and stopping at the first block whose post-mechanism re-read is 0. In
the no-storm boot it never fired, because the ladder lives INSIDE the ISR and there was no ISR entry. The
ladder run (`MAXPAR=1`, same bound, smaller stimulus) gave it an entry and the ranking resolved:

- `QUIESCED_BY_CONSUME` was NOT reached: the consumption sequence alone (ack out[3], clear out[1], re-arm
  out[4]) left the glue status latched, so the block's re-read never fell to 0.
- `QUIESCED_BY_FWACK` was NOT reached, for the same reason.
- `QUIESCED_BY_MASKCLOSE` was the winner: after 43+ entries the mask-close `CA 0x400392e8 <= (saved |
  0x18)` drove the post-mask status to 0, and the ladder stopped.

State: REPORTED (NOT-IN-CAPTURE). What it means if it holds: the mailbox mechanisms are not sufficient to
clear a latched post-mask status on this boot, so the reversible mask close is the only rung that
quiesces, and the ranking CONSUME -> FWACK -> MASKCLOSE is now measured rather than assumed.

## The 209 witness

The 209 witness is the host-facing route for `0001:00:00.0`. ADDENDUM 12 left it armed but inert: the
compiled `pci_dev->irq` offset (`0x1ac`) differs from the vendor's (`0x184`), the sysfs fallback returned
`255`, and `request_irq(209)` was skipped. This sweep pointed the source at the port's own
`omo_ep1_dev->irq` (ADDENDUM 10's fix) instead of the config byte, so `request_irq(209)` runs and the
line can fire. State: REPORTED (NOT-IN-CAPTURE). If a capture confirms an `isr2_n > 0` count that MOVES
with a ring, the forward-hop row is finally testable; until then the 209 route stays unobserved and the
zero counters stay vacuous.

## The DTS and the driver skeleton

The device-tree half is the clocks2 plan turned into files. `docs/soc/luofu-r116.dts` (working-tree,
uncommitted) folds the skeleton's two placeholders, `clk:` (`clock-controller@14880000`, TODO comment)
and `rst:` (`reset-controller`, no reg), into ONE
`crg: clock-reset-controller@14880000` node with
`compatible = "hisilicon,luofu-crg", "syscon", "simple-mfd"`, `reg = <0x14880000 0x1000>`,
`#clock-cells = <1>` and `#reset-cells = <2>` (reg-offset, bit), and repoints every consumer
(`gpio0/1`, `i2c0`, `fmc`, `pcie0`) to `<&crg IDX>` / `<&crg off bit>`. The diff is 23 insertions / 33
deletions. A new `lab/luofu-clk/` skeleton carries the driver shape: `luofu-clk.c` (8,538 B, the
`of_match_table` + `probe`/`remove` scaffold, the `LUOFU_CRG_SIZE` 0x1000 page, `LUOFU_SOFTRST_VAL0` =
`0x51162100u` / `VAL1` = `0xaee9deffu`, and the `LUOFU_CLK_*` index enum), a one-line `Makefile`
(`obj-m := luofu-clk.o`) and a README that says plainly it is NOT-YET-COMPILED. The module is a design
scaffold: `hisi_clk_*`/reset helpers and the clock-data tables are TODO, and it must not be built against
the stock tree as-is.

| item | path | state |
| --- | --- | --- |
| folded CRG node | `docs/soc/luofu-r116.dts` (uncommitted) | 23 insertions / 33 deletions; `crg:` node + consumers repointed |
| driver skeleton | `lab/luofu-clk/luofu-clk.c` | scaffold, NOT-YET-COMPILED; helpers + clock tables TODO |
| build stub | `lab/luofu-clk/Makefile` | `obj-m := luofu-clk.o` |

The dtc pipeline is the one ADDENDUM 12's port-plan note fixes: upstream dtc v1.7.2 built from the
`dgibson/dtc` tarball with MinGW-W64 gcc 16.2.0 plus winflexbison 2.5.25, driven by
`cpp -P -nostdinc -x assembler-with-cpp docs/soc/luofu-r116.dts` then `dtc -I dts -O dtb`. The
compile-verified artifact is `build/tmp/inta-spec/luofu-r116.dtb` (4,554 B, sha256
`a1e0d822f23691ff96efaaec3a5def0d26923d642714fa8ab674b2a408f55823`, magic `d00dfeed`, 29 nodes / 145
properties), exit 0, 0 errors, 4 `unit_address_vs_reg` warnings (source hygiene, the DTS was not edited to
silence them). Negative controls hold: a deliberately broken DTS aborts on a syntax error, an unresolved
`&nope` errors, and the raw vendor `luofu-r116-pinned.dts` fails to parse.

## The bounds (declared, not hidden)

1. **The storm, the ladder and the 209 rows are NOT-IN-CAPTURE.** No capture file for them sits in the
   verdict artifacts this write-up read, so they are reported as the parent phase's claims and are NOT
   independently parsed. A future pack that lands `capture-cmd.txt` in those two exp dirs closes this.
2. **The bound tripped once.** The clean recovery is one boot's datum; the field values on a second storm
   boot are unknown, and `n=16` is a recovery count, not a delivery rate.
3. **The storm is deliberate, so it is not the ADDENDUM 11 storm.** The recorded `isr_n` and the
   mask-close winner describe a stimulus WE armed; they say nothing about which `intapost` bit latched the
   earlier spontaneous storm, and the candidate list (a)-(e) survives untouched.
4. **The 209 zero is vacuous again unless the count moves.** An `isr2_n` of 0 with the line requested says
   nothing; only a counter that increments with a ring is the witness. State: REPORTED (NOT-IN-CAPTURE).
5. **The DTS edit is uncommitted and unverified on hardware.** `luofu-r116.dts` is a working-tree change;
   the diff was re-read, not compiled on the device. `lab/luofu-clk/` is NOT-YET-COMPILED and is not in
   the build; the `luofu-clk.o` object does not exist.
6. **Hard rules respected.** No write of CA `0x400392f0` (copy-A W1C) or `0x40039af0` (copy-B W1C); no
   read of the ack IAR `0x4016010c` or the RC misc `0x10161000`; the gate register `0x4000010c` is a
   different address and is the only `0x...10c` touched; the `.ko` was staged as `wifidrv1.ko`; the
   recovery auto-deleted the staged `.omo-pat`; the device cycles ran detached via `tools/exp.sh`.

## Verification

The instrument verifiers CONFIRM the shipped artifacts: the submodule commit `2507a42` pushed on
`omo/phase22-hccaccept` only, the independently re-fetched CI `.ko` (md5
`91fba1dc512536c94bfd1e419173c046` at vermagic 5.10.201), the firmware selftest plus ALL frozen pins, two
byte-identical regenerations matching the staged blob md5 `eee1f673...` (928,920 B), the capstone of
every inta3 site, and the v5 bound/knob diff against the frozen 41-knob set (sha256 `541060a8...`
unchanged) INCLUDING `disable_irq_nosync` on both ISR sites with `qbound` clamped `1..64`. The
v5/v6 post-sweep regressions are NOT-IN-CAPTURE (report-only). The five pre-existing
`register_netdevice` `WARNING:` blocks are NOT run-specific. Git carries only the `opensource` gitlink
bump; master is untouched.

## The next threads

- **Land the two captures.** Pack `build/register-dumps/exp/20261005-160844/` and
  `build/register-dumps/exp/20261005-171233/` so `vrun10`/`vtool10` can re-derive the storm, the ladder
  ranking and the 209 count from the bytes instead of from the report.
- **Re-run the 209 witness for a moving count.** A ring with `isr2_n` observed before and after is the
  read that turns the 209 route from "requested" into "delivered".
- **Build `lab/luofu-clk/` for real.** Bring in the `hisi_clk_*`/`reset.c` helpers and the pinned
  gate/PLL/mux tables, so the CRG node stops being a scaffold and the stage-1 DTS compiles against a
  driver that resolves every consumer.
- **Keep the deliberate storm out of the healthy path.** The bound recovered cleanly once; a storm boot
  should stay an explicit, timed, single-target mode, never the default.

# ADDENDUM 14 (2026-10-05): the storm closure - RUNG 1 Q_CONSUME RETIRES BIT 4 ONLY / RUNG 2 Q_FWACK
RETIRES NOTHING / RUNG 3 Q_MASKCLOSE RETIRES BIT 3 ONLY; NO RUNG QUIESCED (twin stays 0x08); THE FIXED
SUPERVISOR NEVER RE-ENABLED AND THE v5 PANIC DID NOT RECUR (0/3); THE EP1 209 ARM STILL FAILED (rc=-19)

ADDENDUM 13 named four threads and the one that closed the storm is the third: exercise the bound on
purpose, then let each rung take its shot alone. The v6 supervisor crossed out defect B (the
unconditional `enable_irq` that panicked the box) and defect A (all three rungs burning inside one ISR
window), so this is the run that turns the 209 and the ladder from REPORTED into measured. The submodule
branch is `omo/phase22-hccaccept` only. The sequence is THREE chained single-rung boots of the SAME
artifact on the inta3 firmware, detached through `tools/exp.sh`.

| boot | rung | evidence dir | RUN_TS | params |
| --- | --- | --- | --- | --- |
| 1 | 1 Q_CONSUME | `build/register-dumps/exp/20261005-181512/` | 20261005-181511 | `rung=1 intapost=0x60 qbound=64 qwait_ms=5000 quiesce=0x8` |
| 2 | 2 Q_FWACK | `build/register-dumps/exp/20261005-182024/` | 20261005-181909 | `rung=2 intapost=0x60 qbound=64 qwait_ms=5000 quiesce=0x8` |
| 3 | 3 Q_MASKCLOSE | `build/register-dumps/exp/20261005-182537/` | 20261005-182403 | `rung=3 intapost=0x60 qbound=64 qwait_ms=5000 quiesce=0x8` |

Every boot carries the same fixed stimulus: `intapost=0x60` is the `0x20` `[intx3]` snapshot plus the
`0x40` twin-stim (W1 the twin mask `0x40039ae8` `<= 0x20`, then W2 the twin doorbell `0x40039ad4 |= 8`,
the mode-C storm) and THE HARD BOUND is `qbound=64`. `quiesce=0x8` arms the opt-in re-enable probe. Each
boot stages its own ko: `wifidrv1.ko` md5 `028f9d1281079c62d5db286f3586b23d` (97,064 B, CI run
37354023564 at submodule commit `948f814cd6a1adadb4a8032672218445dae67a6f`), and the firmware `inta3.bin`
md5 `eee1f67370b56eca42316f6eeb490c45` (928,920 B) is REUSED and staged as the `.omo-pat` overlay, never
rebuilt. The adversarial verdict is `build/register-dumps/diffs/20261005T1829Z-vrun11/verdict.txt` (the
newest run's verifier dir had not been written, so this one is named for the verification instant and
matches the vrun10 / vtoolN pairing convention); the instrument verdict is
`build/register-dumps/diffs/20261005T1814Z-vtool11/verdict.txt`. The design and ladder specs are
`build/tmp/inta-spec/{superfix.md,ladder3.md,ep1.md,twinq.md}`.

## The fixed supervisor

`superfix.md` sec.0 put it in one line: the v5 bug was a single `enable_irq` at the head of every rung
(`wifidrv1.c:2303`, unconditional, `d207` is true exactly when the bound tripped so the guard was no
gate). On the still-latched level that call re-armed the storm on the same CPU and the box panicked. v6
deletes it. `enable_irq` now sits at exactly ONE call site (`omo_sv_probe()`, source line 2383), behind
three gates: one probe per boot, `st == 0 && ts == 0` (both glue levels and the twin copy clean), and
`omo_bounded && omo_irq_owned` (there is a bound-disabled 207 line to hand back). Every other exit returns
without touching the line; the rung that did not clear prints `SUPERVISOR DONE quiesced=0 ... state=BOUND
(left DISABLED)` and stops for the boot. vtool11 confirms it in source and in the binary: `grep -n
'enable_irq'` finds the one call and comments, the reloc audit finds exactly ONE `enable_irq` relocation
(in `.init.text`, inside the inlined `omo_wifidrv1_init`), and the disassembly immediately before that
`bl` shows the two guard branches. The 209 line is never re-enabled at all; the exit path only `free_irq`s.

Two more pieces of the contract. The ladder no longer runs in the ISR (ladder3 defect A): `omo_intx_isr()`
holds only THE HARD BOUND and the vendor RMW write-back, while the rung runs in PROCESS CONTEXT on the
DISABLED line, so a rung cannot collide with the storm window. And the second, tighter bound
(`qbound2`, default 8, clamped 1..64) lives in exactly one place, `omo_sv_probe()`, which is called from
exactly one place, the CLEARED branch (`omo_glue_quiet()`): gating the probe on both-levels-clean is what
makes `superfix.md` sec.2b's contradictory prose moot, the shipped code follows the safe reading.

## The ladder, rung by rung

QUIESCED = the both-copies predicate (`omo_glue_quiet`: glue stat == 0 AND twin stat == 0). A glue-only
zero is VACUOUS (`twinq.md` sec.5); each rung ran in `omo_wifi` process context on the disabled line.

| boot | rung | storm | bound | glue stat BEFORE -> AFTER | twinB stat BEFORE -> AFTER | quiesced | state | probe | panic |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 1 CONSUME | refired | n=65 | 00000018 -> 00000008 | 00000008 -> 00000008 | no | BOUND | not reached | no |
| 2 | 2 FWACK | refired | n=65 | 00000018 -> 00000018 | 00000008 -> 00000008 | no | BOUND | not reached | no |
| 3 | 3 MASKCLOSE | refired | n=65 | 00000018 -> 00000010 | 00000008 -> 00000008 | no | BOUND | not reached | no |

**Rung 1 Q_CONSUME.** The vendor's `pcie_msg_handle` order (ack out[3], clear out[1], re-arm out[4]) ran
and the glue stat retired bit 4 (`0x18 -> 0x08`) while bit 3 survived, twinB held `0x08`. The capture
agrees at the cell level: `out[1]` (`0x403F1014`) starts at `0x04` and reads `0x00000000` post-rung, so the
clear took. This is the phase-21 LIVEBIND signature, not a quiesce.

**Rung 2 Q_FWACK.** The full three-op FWACK service ran (v5 ran only op (3)), printing
`[qsv] FWACK out0 0x40039010 |= 0x18 (rb 00000018), out2 0x400392d4 |= 1, 0x4000010c <= 0x0000cece`, and
the glue stat did NOT move (`0x18 -> 0x18`): the device did not retire the latch on this rung. Note the
correction vrun11 records: the hook reads `out[0]` (`0x403F1010` = `0x00000000`) AFTER its `SUPERVISOR DONE`
wait, so the read is post-rung - either the device consumed the FWACK post and left the glue latch closed,
or `out[0]` is self-clearing like the out[2] doorbell. The load-bearing point is unchanged, and the
standalone ledger row's "did not clear out[0]" is not what the capture shows.

**Rung 3 Q_MASKCLOSE.** The mask close took: glue mask `0x20 -> 0x38` (`|= 0x18`, matching ladder3's
`0x20|0x18=0x38`), and the glue stat retired bit 3 (`0x18 -> 0x10`) while bit 4 survived - the OPPOSITE
survivor from rung 1, so the two rungs each retire one side of the latch and neither can reach both.

Aggregate: NOT QUIESCED BY ANY RUNG. Across all three rungs the level survives (CONSUME residual `0x8`,
FWACK `0x18`, MASKCLOSE `0x10`; the twin residual is `0x8` every time). ZERO `QUIESCED_BY_*` lines and
ZERO `[qsv] SUPERVISOR DONE quiesced=1` in the whole sequence. The honest negative of `ladder3.md`
sec.4/5.4 (nonzero residual -> LEAVE DISABLED) is what every boot recorded, and the ranking
CONSUME -> FWACK -> MASKCLOSE is now measured rather than assumed.

## The quiesce verdict, and the re-enable probe

The storm re-fired verbatim in every boot from the same source: `[intx3] W1 twin mask 0x40039ae8
0x000003ff -> 0x00000020` then `[intx3] W2 twin doorbell 0x40039ad4 <= 0x00000008 (rb 0x00000000,
self-clearing)`, and the `[intx3] DELTA W2` line carries `isr207 0->65` with `/proc/interrupts` moving
`207: 0 0 GIC-0 91 Level hisi_pci_intx` to `207: 65 0 GIC-0 91 Level omo-drv1`. That is 65 IRQs on 207 in
roughly 16 ms from the W2 doorbell (the mode-C source pinned in ADDENDUM 13). The mandatory bound tripped
exactly once per boot (3 trips), each printing `IRQ_DISABLED_BOUND irq=207 n=65 bound=64 glue=00000018`,
self-disabling 207 BEFORE any MMIO and with ZERO `callbacks suppressed` lines.

The opt-in probe (`quiesce=0x8`) was ARMED in all three boots and NEVER REACHED, because its gate wants
both levels clean and no rung cleared them. So no second bound was spent and no storm ever re-fired into a
re-enabled line: the terminal exit in all three boots is `state=BOUND (left DISABLED)`.

## The EP1 209 result

`ep1.md` sec.2(a) asked the arm to run deterministically on the hw path, so `request_irq(209)` would fire
without needing `intapost` bit `0x10`. v6 placed the call inside `if (omo_release_en)` in `omo_hw_attach()`,
before the storm. It is CODE-TRUE and RUN-FALSE: every boot printed
`[intx2] no sibling INTx virq (irq=0) - the 209 witness is unavailable` and
`209 arm failed rc=-19 (the witness stays unarmed)`. The cause is a timing one: the arm is attempted at
about t=43.4 s, but the sibling's `pci_driver` bind (`[ep1db] sibling domain=1 bound to omo-drv1`) does not
happen until about t=66.5 s, so the sibling virq is not available at the arm instant. `isr209=0` and
`isr2_n=0` at every tag, so the 209 rows stay VACUOUS in the `ladder3.md` sec.5.7 sense. This does not
touch the 207 rung verdicts. `ep1.md` sec.2(b)'s small-K bound did ship (`qbound209` default 8, clamped to
64, `disable_irq_nosync` before any MMIO) and sec.2(c)'s never-re-enable is satisfied and then some: 209 is
never re-enabled anywhere, so there is nothing for a v5-style panic to re-arm on that line.

## The bounds (declared, not hidden)

1. **This sequence is IN-CAPTURE**, unlike ADDENDUM 13. The vrun11 verifier re-parsed all three boots from
   their raw files: the storm re-fire, the `n=65/bound=64` trip, the BEFORE/AFTER transitions, the rung
   outcomes, the absence of the v5 panic and the pstore delta all reproduce. Where this addendum rests on
   a verifier's re-derivation rather than on the run's own acceptance files, the row says so.
2. **The run's own "deterministic 209 arm" claim is code-true but RUN-FALSE** (`rc=-19` in 3/3), so no
   209 datum exists and no host-facing forward-hop row is testable from this sequence.
3. **The bound tripped 3 times, exactly one per boot.** `n=65` is a recovery count, not a delivery rate,
   and each trip is one boot's datum; the field values on a fourth storm boot are unknown.
4. **Rungs 1 and 3 carry no op-level readback.** Only FWACK prints its three ops; CONSUME and MASKCLOSE
   are evidenced by their EFFECT (`out[1] 0x04 -> 0` plus glue bit-4 retirement; glue mask `0x20 -> 0x38`),
   so the op ORDER is taken on code trust, not on device evidence.
5. **Boots 1 and 2 lack the standard processed worker files** (no `KNOBSET.txt`/`rows.txt`/`interp.txt`/
   `MATRIX.md`/`acceptance.txt`/`health-raw.txt`/`cleanup.txt`); only boot 3, post-processed by
   `stormclose-r3-post.sh`, carries them (PACKED 15/15). Every acceptance claim for boots 1-2 is
   re-derived from the raw files by the verifier, and boot 1's early PACKED recorded `health.txt` MISSING
   because it packed about 40 s before that file was written.
6. **The `entry`-follows-W2-by-~10 us attribution is observable only in boot 3.** In boots 1/2 the W2
   *print* lands about 7 ms after the entry and bound lines (kernel print interleaving with the storm ISR);
   the write still precedes the storm in every boot, since the entry line carries `storm=0x18`.
7. **`module-log.txt` is register_netdevice/RTNL spam** in every dir, unrelated to the storm; the
   load-bearing `qsv`/`bound`/`intx3` lines live in `dmesg.txt` and `capture-cmd.txt`.
8. **Hard rules respected.** No write of CA `0x400392f0` (copy-A W1C) or `0x40039af0` (copy-B W1C), each
   scanned 0 times in the shipped `.ko` (no direct word, no movw/movt composition); no read of the ack IAR
   `0x4016010c` or the RC misc `0x10161000`; the FWACK gate `0x4000010c` is a different address and is the
   only `0x...10c` touched; the `.ko` was staged as `wifidrv1.ko`; the recovery auto-deleted the staged
   `.omo-pat`; the device cycles ran detached via `tools/exp.sh`.

## Verification

The instrument verifier CONFIRMS the shipped artifact: the ONE code commit `948f814` (+330/-171 on
`lab/wifidrv1/wifidrv1.c`) pushed on `omo/phase22-hccaccept` only, its independently re-fetched CI `.ko`
(md5 `028f9d1281079c62d5db286f3586b23d`, 97,064 B, vermagic `5.10.201 SMP mod_unload ARMv7`, identical to
the staged copy), the vendor verifier 59/0, the raw forbidden-CA scan at 0 occurrences, the firmware
`inta3` md5 match with `patch_fw_scratch.py --selftest` rc=0 (honest reuse), and the ONE `enable_irq`
relocation behind its two guard branches. The runner's TS gate was exercised against the real evidence
tree and rejects stale and malformed names. The adversarial verifier CONFIRMS the sequence as a record,
with D1-D9 recorded: the standalone ledger row is STALE/WRONG for boot 3 (row 3 duplicates row 2 and its
"rung 3 cleared copy A" is false - boot 3's glue went `0x18 -> 0x10`), the boot-2 "did not clear out[0]"
is contradicted by its own capture, the 209 arm claim is RUN-FALSE, and boots 1/2 lack the processed worker
files. None of these inverts a load-bearing claim: no fabricated data, the negative (not quiesced) is an
honest negative, and the storm/bound/no-panic claims are exact. The live read-only probe left the router
healthy: `WIPHY=2 IFACE=6`, calibration `[SUCC]` on both bands, `OMO_OFF=0 STAGED=0 LOADER=0`, stock md5
`0e530b976d5a20e87358671f1a577695`, three pstore records (no new one from this sequence). Git carries only
the `opensource` gitlink bump; master is untouched.

## The next threads

- **Make the probe reachable once.** No rung clears both levels, so the opt-in re-enable probe has never
  fired. A benign boot (no storm stimulus) that clears copy A alone, or a rung that zeros both copies,
  would finally spend the second bound.
- **Fix the 209 arm timing.** Move `omo_dual_line_attach()` after the sibling's `pci_driver` bind (about
  t=66.5 s), or source the virq from the port's own `omo_ep1_dev->irq`, so `request_irq(209)` runs and the
  forward-hop row becomes testable.
- **Pack the two captures.** Boots 1 and 2 still lack `rows.txt`/`interp.txt`/`MATRIX.md`/`acceptance.txt`;
  re-running `gen_stormclose_ledger_row.py` over the now-complete three `MATRIX.md` files would also fix the
  stale standalone ledger row.
- **Then fold the ladder back into the driver.** The matrix is now measured: a copy-A-only quiesce is
  vacuous, the twin copy latched in all three boots, so the next question is which mechanism retires bit 3
  AND bit 4 together, or whether the twin copy must be closed first.

## ADDENDUM 15 (2026-10-05): the full-reversal sprint - THE 209 WITNESS ARMED / THE TWINCLOSE RUNG CLEARS THE TWIN / THE COMBINED QUIESCE IS NEXT / LUOFU-CLK CROSS-COMPILES GREEN / THE H2D GATE IS THE DEVICE CPU'S TAKE

Four chained sessions, one question each, in the order they ran: close the storm, compile the first
upstream driver code, read the chip's own delivery gate, then clear the twin's status. ADDENDUM 14 owns run
1; the three below are new. Every claim carries its artifact.

### Run 1, the storm close (covered by ADDENDUM 14)

The fixed supervisor held 3/3 boots, and the ladder matrix is now measured: CONSUME retires glue bit 4
(`0x18 -> 0x08`), FWACK retires nothing (`0x18 -> 0x18`), MASKCLOSE filters bit 3 (`0x18 -> 0x10`). The
residual `0x08` is the twin copy-B bit, so the survivor belongs to copy B, not to copy A.

### Run 3, chip-deep: the H2D gate is the DEVICE CPU's take, and the old DR line is VOID

`build/tmp/inta-spec/chipdeep.md`, offline disassembly, no device cycle. Three findings:

- **H2D delivery is gated by the DEVICE CPU's take.** The take's third precondition, reading the IAR with
  IRQs live, is never observed: every sampled CPSR carries I = 1, and ISACTIVER2 bit 12 stays 0
  (confirms the ADDENDUM 12 page-10 cell). The vendor's difference is SEQUENCING, not a register: the
  handshake `0xcece`, the ETE bring-up, the release, and only then the mask lift. The `0xcece` write is
  necessary on this record; its sufficiency is unmeasured.
- **The old "152 moves / 0 filled" DR-ring line is VOID on both legs.** The moves were credit turnover,
  and "filled" read word1, which the DR path never writes, so the number was describing the wrong word.
  The real completion test is the buffer header: `buf+0x0a == 0x5a5a` and `buf+4 != 0`.
- **The twin is copy B of the ctrl-rb.** CA `0x40039800` = copy A `0x40039000` + `0x800`, firmware
  programmed, host silent. The vendor's ONLY doorbell site writes bit 3 into INTR_SET and then polls the
  same bit for self-clear, which is the pattern the next run answers.

### Run 4, twin-close: the 209 witness is ARMED, and TWINCLOSE clears the twin

`build/tmp/inta-spec/{twinq.md,twinclear.md,ep1.md,ep1fix.md,realchain.md}`, one bounded boot, evidence
`build/register-dumps/exp/20261005-185140/`, verdict `build/register-dumps/diffs/20261005T1855Z-vrun12/`,
variant `vrun12` CONFIRMED with D1-D6, staged ko `wifidrv1.ko` md5 `4e8088e8...`. Four results:

- **THE EP1/209 WITNESS ARMED.** `request_irq(209, IRQF_SHARED)` returns `rc=0`, because the sibling-probe
  claim fixed the two `rc=-19` boots that came before it. The line then caught real traffic: `isr2_n=9`
  before its own bound of 8, recorded as `IRQ_DISABLED_BOUND irq=209 n=9 bound=8`. The 209 row is no longer
  vacuous; it was measured, then closed on purpose.
- **THE TWINCLOSE RUNG WORKS.** A twin mask write, CA `0x40039ae8` `0x20 -> 0x28`, took the twin's status
  `0x08 -> 0x00` (`twin_residual=0x0`) while the glue copy-A status stayed `0x18`. So the FULL quiesce is
  CONSUME (retires glue bit 4) plus TWINCLOSE (retires the twin's bit 3), and the COMBINED RUNG is now the
  next experiment, designed from data rather than guessed.
- **Static: copy B has its clear, and no code writes it.** CA `0x40039af0` = copy A `0x2f0` + `0x800`,
  the twin's own write-to-clear, exists in the map. No vendor code writes it anywhere. The copy-A clear CA
  `0x400392f0` stays forbidden because a host write of `out[5] <= 8` HUNG the endpoint, which is the rule's
  origin, not a superstition.
- **Safety.** The bound held on both lines, zero panics, the pstore delta is none, and the router ended
  healthy.

### Run 2, upstream-compile: the first cross-build of our own driver code is green

Verified by the orchestrator directly, from the CI and the lint output:

- **`luofu-clk.ko` CROSS-COMPILES GREEN in CI.** `gh run list` on commit `dff5925` shows success twice. The
  road there was honest: the `bed58e5` attempt failed first, the fix was a block comment in the scaffold,
  and the rerun passed. This is the FIRST cross-build of our own driver code.
- **The edited DTS lints PASS.** dtc 1.7.2, dtb 4,506 B, sha256
  `732104b1818945b69100dc7ad45612360fec0c5297c6ef0ed91689297b5b3946`, errors 0, three cosmetic
  unit-name warnings.
- **`stage2.md` is the ranked stage-2 driver inventory**: CRG -> pinctrl -> PCIe RC -> endpoint -> glue ->
  wifidrv, with the Kconfig symbols listed (`build/tmp/inta-spec/stage2.md`).

### Aux gaps, stated honestly

The verify-node artifact gap recurred: `vrec10` and `v-run2` wrote no verdict dir, and `v-chip`'s landed in
the wrong directory and was relocated by the orchestrator. Run 4's boot used `tools/finish-evidence.sh` for
the first time, which generated KNOBSET, cleanup and interp, but the worker files (acceptance, rows,
MATRIX) stayed missing, recorded as D1 in the vrun12 verdict.

### The queue

1. THE COMBINED QUIESCE RUNG: CONSUME plus TWINCLOSE in one boot, then the bounded re-enable probe.
2. The real-chain stimulus: the `intapost` `0x100` hook exists and has never been exercised.
3. Stage 2's first driver: pinctrl, per the `stage2.md` ranking.
4. The device-firmware lane, the fw-load path.

### Artifacts

Specs `build/tmp/inta-spec/{h2d2,credit2,twinsem,chipdeep,superfix,twinq,ladder3,ep1,twinclear,realchain,ep1fix,crgci-result,dtslint,stage2}.md`;
boots `build/register-dumps/exp/{20261005-181512,182024,182537,185140}/`; verdicts
`diffs/{20261005T1814Z-vtool11,20261005T1829Z-vrun11,20261005T1855Z-vrun12}`; the CRG CI commit `dff5925`;
ko pins storm-close `028f9d12...` and twin-close `4e8088e8...`.


# ADDENDUM 16 (2026-10-05): the combined quiesce + the real chain - THE INSTRUMENT IS BUILT AND NEVER SPENT / BOTH BOOTS BLOCKED ON THE CI ARTIFACT, THEN NOT RUN IN THE WINDOW / NEITHER RUNG-7 NOR THE REAL-CHAIN TAKE HAS EVIDENCE

ADDENDUM 15 closed with two questions and a queue: does CONSUME plus TWINCLOSE clear BOTH copies in one
boot, and does the real-chain stimulus make the device CPU take the frame. Both instruments were designed,
committed, and verified `UNAVAILABLE` at the moment the verification window shut. The two boots simply had
not happened. This addendum records that honestly, because a verdict that says "no run" is a fact about the
instrument, not a hole in the record.

### The instrument: knob v8, committed and never run

One commit, `553342d` on `omo/phase22-hccaccept` only, +128/-24 on `lab/wifidrv1/wifidrv1.c`. It adds
rung 7 `Q_COMBO` (TWINCLOSE first, then the vendor CONSUME order: ack `out[3]`, clear `out[1]`, re-arm
`out[4]`), the sec.3 second-bound re-enable probe, and the real-chain preamble opt-in `Q_RC_PRE` (quiesce
bit `0x1`, default off) that prepends the twin storm preamble W1+W2 before the `intapost=0x100` stimulus.

The supervisor discipline survived an independent source audit. Both ISRs self-disable before any MMIO at
their bound and return `IRQ_HANDLED` without touching a register. `enable_irq` exists at exactly two lines,
both inside `omo_sv_probe`, whose sole call site is the both-levels-clean branch. The probe runs under the
tighter second bound (`qbound2` pushed onto `qbound209`, both counters reset), and a re-assert is FINAL:
`[qsv] IRQ_LEFT_DISABLED ... reason=probe-rearmed` and both lines stay disabled. Every other supervisor
exit returns without enabling anything.

The one gap was external. `verify-ko-combo.py` ran against the still-staged v7 ko and scored 57 passed,
11 failed, with all eleven fails being exactly the v8-only features (the COMBO rung-7 banner, the rung clamp
`0..7`, `QUIESCED_CONFIRMED`, `PROBE ATTEMPT`, `Q_RC_PRE`, the region-relative CONSUME immediates
`0x101438`/`0x101414`). That is the discriminator working: the old ko would have fallen to
`case 7 -> default: break;` and produced a silent no-op boot, so a combo run staged before the fetch would
have proved nothing. The CI run that builds the artifact (`37365078637`, `workflow_dispatch`, headSha
`553342d`) was still `in_progress` when the instrument verifier closed at 19:52Z.

Then it landed. At 19:54:36Z the run completed success and the verifier fetched `wifidrv1-ko` into its own
directory, never the runner's staging name:

- md5 `3f87f1e9fe5ed9666f27d1f784d34535`, vermagic `5.10.201 SMP mod_unload ARMv7` (the device kernel)
- all seven v8 strings present (`COMBO rung 7`, `QUIESCED_CONFIRMED`, `PROBE ATTEMPT`, `Q_RC_PRE`,
  `CONSUME 0x40101438`, `(0..7)`, `realchain`)
- the forbidden W1C constants `0x400392f0` and `0x40039af0` appear only in read-only comment and param text,
  never as a store

### The two boots: blocked, not failed

The two runner tasks each stalled on that artifact. Their own words, quoted in the adversarial verdict:

> "I'm waiting on the CI build (monitor `mon_K7SMEB9TMCCB2EEW` is watching it)."

> "Blocked on the queued CI build; monitor `mon_D2X7EQ3KGCEPD8HA` is live and will resume me at
> completion. Standing by to fetch the v8 ko and run the cycle."

The evidence is the absence itself. Neither `build/tmp/wifidrv1-art/run-combo.log` nor
`run-realchain.log` exists, and a tree-wide grep for `rung=7|rung=COMBO|[realchain]|QUIESCED_BY_COMBO|
QUIESCED_CONFIRMED` under `build/register-dumps/exp/` returns nothing. The decisive strings match only
four synthetic fixtures under `build/tmp/{combo-selftest,realchain-selftest}/`, never a real evidence
directory. The newest boot on disk is still the twin-close boot `20261005-185140`, so nothing newer than
18:51Z ran.

Both branch tables are recorded here so the runs can be judged the moment they exist, rather than
re-derived later.

**BOOT 1, the COMBINED rung (combod.md sec.2).** Full win `QUIESCED_BY_COMBO`: `rd(0x400392ec)==0` and
`rd(0x40039aec)==0`, then the probe. Partial (no probe, `PROBE SKIPPED`): the residue pairs
`glue 0x08/twin 0x00`, `0x10/0x00`, `0x18/0x00`, `0x00/0x08`, `0x18/0x08`, or any readback that
differs from what was written. Probe outcomes: `QUIESCED_CONFIRMED` is a zero delta across the whole
`qwait_ms` window with both statuses still zero; `probe-rearmed` is any nonzero delta, which trips bound
#2 (`n > 8`), is FINAL, and leaves both lines disabled. The ladder it continues from is measured:

| boot | rung | glue stat `0x400392ec` | twin stat `0x40039aec` |
| --- | --- | --- | --- |
| 181512 | 1 CONSUME | `0x18 -> 0x08` (bit 4 retired) | `0x08` |
| 182024 | 2 FWACK | `0x18 -> 0x18` | `0x08` |
| 182537 | 3 MASKCLOSE | `0x18 -> 0x10` (bit 3 retired) | `0x08` |
| 185140 | 5 TWINCLOSE | `0x18` (unchanged) | `0x08 -> 0x00` (mask `0x20 -> 0x28`) |

So the runner ran `rung=7 intapost=0x60 quiesce=0x18 qbound=64 qbound2=8 qbound209=8` with the probe
armed, and never produced a row.

**BOOT 2, the real chain (realchain2.md sec.5).** `TAKEN` means `out[0]` at `0x40039010` goes `0x8 ->
0x0` (the device consumed) plus a copy-A glue raw bit-3 clear that no host wrote, with the 209 ISPENDR
bit 12 clear and both IRQ deltas bounded. `ABSENT-1` is rc3 waited 2000 ms with no change and
`out[0]` pinned at `0x8`. `ABSENT-2` is `out[0]` pinned with ISPENDR2 word 2 bit 12 SET (parked or
pending, the expired-boot signature). `ABSENT-3` is `out[0]` pinned with CPSR `0x20000193` (I=1 at both
samples, the device ISR mask). `CONTROL` is a host bit-3 write only, which proves the instrument and not
the device. The twin triple must stay fixed in every branch. The runner was
`intapost=0x100 quiesce=0x1 rung=0` on the same v8 ko, and also produced no row.

### What this does NOT say

Neither branch is disproved. The specs are untested, not refuted. `QUIESCED` is NOT ESTABLISHED and the
exact residual is NONE OBSERVED, because the rung never executed. For the real chain the observation is
ABSENT, which is a different thing from the device-ABSENT branch of sec.5 that no run supports. C3 fell
first and then rose: the instrument was FAIL (not delivered) at 19:52Z and CONFIRMED at 19:55Z when the
artifact landed.

Two non-inverting bookkeeping notes. The superproject gitlink still points at `f12209a` while the submodule
HEAD is `553342d` (`git status` shows ` M opensource`) and `tools/finish-evidence.sh` carries an
uncommitted verifier-mode change; the v8 commit itself is safe on the push-authorized branch. Two verdict
skeletons (`20261005T1931Z-toolverify`, `20261005T1932Z-toolverify2`) were minted and left as TODOs, and
`vrun13`'s own two monitors on `run-combo.log`/`run-realchain.log` are live.

### Safety

The probe was never spent, so the second bound has still never tripped on device. The read-only probe at the
close of the window left the router healthy: 2 wiphys, 6 interfaces, calibration `[SUCC]` on both bands,
`OMO_OFF=0`, `STAGED=0`, `LOADER=0`, zero `.omo-pat`, zero leftovers, stock md5
`0e530b976d5a20e87358671f1a577695`, and three pstore records with none new. No register was read by either
verifier. `0x400392f0` and `0x40039af0` were never written, and neither the IAR `0x4016010c` nor the RC
misc `0x10161000` was read.

### The next threads

- **Run the two boots now that the ko is on disk.** `fetch-ci-combo.sh` then `verify-ko-combo.py` then
  `run-combo.sh`; the ko is md5 `3f87f1e9...`. Classify boot 1 against combod.md sec.2 and boot 2 against
  realchain2.md sec.5, and quote `run-combo.log`/`run-realchain.log`.
- **Spend the second bound on purpose.** No rung has left both levels clean, so the sec.3 re-enable probe
  has never fired. A test that clears copy A alone, or a rung that zeros both copies, is what finally
  exercises it.
- **Keep the discriminate-first rule.** The stale-ko trap is the reason `verify-ko-combo.py` checks for the
  v8 strings before staging; a combo boot on the v7 ko is a silent `case 7` no-op.

### Artifacts

Specs `build/tmp/inta-spec/{combod.md,realchain2.md,realchain.md,twinclear.md,ep1fix.md,chipdeep.md,stage2.md}`;
runners `build/tmp/wifidrv1-art/run-{combo,realchain}.sh`; verdicts
`build/register-dumps/diffs/{20261005T1946Z-vtool12,20261005T1954Z-vrun13}/`; CI commit `553342d`
(`omo/phase22-hccaccept` only); CI artifact run `37365078637`, ko md5 `3f87f1e9fe5ed9666f27d1f784d34535`;
staged-but-unrun runner ko still the v7 `4e8088e8...`; ladder boots
`build/register-dumps/exp/{20261005-181512,182024,182537,185140}/`.

### 16a. ADJUDICATION (appended by the orchestrator, 2026-10-05T20:0xZ): the COMBO boot landed after this addendum closed

The heading above ("NEITHER RUNG-7 NOR THE REAL-CHAIN TAKE HAS EVIDENCE") was true at this addendum's
timestamp and is **SUPERSEDED for the combo boot**: the detached cycle the boot task had launched
(from `run-combo.sh`) completed after this section was written. Adjudication per vrun13's pre-registered
frame (`build/register-dumps/diffs/20261005T1954Z-vrun13/verdict.txt`, sec.1):

- **BOOT 1 (the combo boot) RAN** - `build/register-dumps/exp/20261005-195626/` (RUN_TS 19:56:25, the
  v8 ko md5 `3f87f1e9fe5ed9666f27d1f784d34535`, the reused `inta3.bin`), and its branch is the
  spec's **PARTIAL** name: the storm refired (`IRQ_DISABLED_BOUND irq=207 n=65 bound=64 glue=0x18`;
  `irq=209 n=9 bound=8`), rung=COMBO ran TWINCLOSE then CONSUME (`[qsv] TWINCLOSE 0x40039ae8
  0x20->0x28 (rb 0x28) twin=00000000 glue=00000018`; `[qsv] CONSUME 0x40101438<=1 (rb 0)
  0x40039014<=0 (rb 0) 0x40101414<=1 (rb 0)`), and the AFTER line reads `glue{raw=0x8 mask=0x20
  stat=0x00000008} twin{raw=0x8 mask=0x28 stat=0x00000000} residual=0x8/0x0` - **glue bit 4
  retired (0x18 -> 0x08), the twin fully cleared (0x08 -> 0x00), residual = the glue's copy-A
  bit 3 alone**; `quiesced=0` (the strict glue==0x00 criterion not met), the single re-enable
  probe NOT armed (it fires only on a full clear), the bound held on both lines, and the pstore
  delta is empty (no panic).
- The identity of the residual: the glue's copy-A bit 3 (`0x8`) is the D2H-RX source that only the
  device's own take-gated consumption retires (`fn_array[0x4c]` - the same gate as 16's H2D
  finding). The host side now provably retires everything it can: bit 4 (CONSUME) + the whole
  twin copy (TWINCLOSE).
- **BOOT 2 (the real-chain boot)** had NOT launched by this amendment; its validated runner
  (`run-realchain.sh`, `intapost=0x100 quiesce=1 rung=0`) was launched by the orchestrator at
  `2026-10-05T19:59:24Z` on the fresh recovery boot (boot_id `b9028c8f-...`, stock md5
  `0e530b976d5a20e87358671f1a577695` verified, 6 interfaces). Its branch lands in a later
  append or the next addendum; vrun13's C2 branch table remains the adjudication frame.
- Health after the combo cycle's own recovery reboot: verified (stock md5 exact, 6 interfaces,
  3 pstore records unchanged).

### 16b. THE REAL-CHAIN BOOT (appended by the orchestrator, 2026-10-05T20:0xZ): the device consumes, the take still does not

The orchestrator's cycle (`run-realchain.sh`, launched `19:59:24Z`, v8 ko `3f87f1e9...`, blob `eee1f673...`,
`intapost=0x100 quiesce=1 rung=0`) landed `build/register-dumps/exp/20261005-200139/`. Measured set:

- The twin storm preamble + seed restore ran (`twin 0x40039ae8 0x20 -> 0x3ff`; twin stat held `0x00`
  throughout - the discriminator did not move under the mask); the mandatory bound held
  (`irq=207 n=65`, `irq=209 n=9 bound=8`); pstore unchanged.
- `rc1-handshake`: `0x4000010c <= 0xcece` landed (rb `0x0000cece`); the storm followed.
- `rc2-h2d-post`: `0x40039010 <= 8` (rb `0x8` - the message POSTED).
- `rc3-h2d-doorbell`: `0x400392d4 <= 1` (rb `0x00000000` - the doorbell bit self-cleared); no raw/stat move,
  `isr 65 -> 65 delta=0`.
- The `[realchain]` read: **`out[0]=0x00000000`** (the message is GONE) with glue `{raw=0x8 stat=0x18}`
  and twin `{raw=0x8 mask=0x3ff stat=0x00}`; the 8 s poll saw 0 transitions (stable at 0).
- The device-side cells (page 10, retained): `N_ACT=0x0`, `N_ISP=0x00000020` (**bit 12 SET**),
  `N_HPP=0x3ff`, `N_OU0=0x00000000`; `F_SNT=0x50AA7E49` (the sentinel held - retention + stores proven),
  `WIN=0xE59FF018`.

**Classification (against 16's realchain2.md branch table): a FOURTH combination the table did not
enumerate.** The consumption DID occur (`out[0]` 8 -> 0, live + cell), so it is not ABSENT-1/2/3
(all of which pin `out[0]` at `0x8`); but the TAKEN branch's discriminators all FAIL: zero ISR delta,
`ACT=0`, `HPP=0x3ff` (nothing served), the glue's copy-A bit 3 NOT retired (`raw 08 -> 08`,
`stat 18 -> 18`). **The mailbox consumption and the take-gated interrupt dispatch are therefore
PROVABLY SEPARATE device-side paths: the message drains on the handshake/doorbell sequence without
`fn_array[0x4c]` ever running** - the same separation the inta2 run first hinted at (`out0 8 -> 0`
with no interrupt) and 16's h2d finding predicted (the dispatcher gated on the CPU's take). The open
question narrows to: what makes the DEVICE CPU read its IAR for source `0x4c` with IRQs live (the
sequencing question of 16, now measured to persist even with a real, consumed message).

Post-cycle state: the runner's `[6/7] recover` + `[7/7] verify health` steps ran after the capture
(the pack-evidence warning about `health.txt` is the mid-sequence state); the final health/pack files
are the runner's own closure set.

---

# ADDENDUM 17 (2026-10-05): the take probe - VECTOR ENTERED (the CPU takes the IRQ; the mask lifts) / the IAR names 0x1D and 0x40, NEVER 0x4c / the 0x4c thread is UP / the release guard never reached

The cycle addendum 16's own next threads named: spend the canary and answer, in one boot, whether the
device CPU ever enters the firmware ISR at file `0x82EFC` and reads the acknowledging IAR. The interp
skeleton landed `TODO`; this block is the interpretation. Everything in the run gic-view record and
16/16a/16b stands unchanged. Evidence `build/register-dumps/exp/20261005-202555/` (`capture-cmd.txt`,
`run-take1.log`, `health.txt`, `pstore-delta.txt`, `acceptance.txt`); the instrument's verdict is
`build/register-dumps/diffs/20261005T2026Z-vtool13/verdict.txt` (CONFIRMED).

### The instrument (take1) and the run

`tools/patch_fw_scratch.py` variant **`take1`** (working tree, +792/-6 vs HEAD `18bb1ab`, uncommitted at
record time) = the inta3 pads plus three sites from fwprobe.md and one cell pad from giccpu.md. Blob
`build/tmp/fw-patched/take1.bin`, md5 `0c681650de28488ab75fb5755684f0c7`, size 928920 (size-preserving).
One cycle, `run-take1.sh`, `TAKE1 RESULT: PASS`, `exp_rc=0`, 20:25:54Z -> 20:28:38Z; the knob is the
v8 ko of `553342d` (`3f87f1e9...`, unchanged, no ko commit, no CI) with `intapost=0x100 quiesce=0x1 rung=0
qbound=64 qbound209=8`, so the take question is asked under the reproduced twin storm, not vacuously.
All ten new cells sit on page 10 (runtime `0x150000`, the retention-verified band; `giccpu.md`'s
`0x158000` was only a barmap-baseline zero check, so the cells moved down to `0x1500a4..0x1500c8`) and
read alias-only.

- `V_SITE 0x82EFC` -> the vec pad: `V_MAGIC` (the canary), `V_PSR` (ISR-entry CPSR), `V_CNT`
  (ISR-entry count = IAR-read count). The pad re-emits the stock `push.w` first, so the handler frame is
  byte-exact.
- `M1_SITE 0x82F4A` -> `M1_PSR`, the CPSR in the handler's own `cpsie i` window (expect I=0).
- `M2_SITE 0x8270A` -> `M2_PSR`, the CPSR at the release guard `0x826E0`.
- the giccpu pad after the drain companion -> `C_GRP0` (`0x40160114`), `C_GRP1` (`0x40160118`),
  `C_GRP2` (`0x4016011C`), `F_BPR` (`0x40160108`), plus the `C_SNT` sentinel. `F_CTLR = B_D6`,
  `F_PMR = B_D7`, `C_PSR = B_D5` are the record's aliases. The acknowledging IAR `0x4016010C` is read by
  the firmware's own ISR only; no pad reads it.

### Matched branch: ROW 1, VECTOR ENTERED

| cell | value | reading |
| --- | --- | --- |
| `V_MAGIC` | `0xA4A4A4A4` | the vec pad ran: the IRQ vector body executed and the IAR read is on the entry path |
| `V_PSR` | `0x00000193` | ISR-entry CPSR: I=1 (bit 7), M[4:0]=`0x13` SVC, T=1 (bit 5) - the textbook take signature |
| `V_CNT` | `0x00000004` | the ISR was entered FOUR times this boot |
| `M1_PSR` | `0x20000113` | I=0, mode SVC: the handler's live `cpsie i` window at `0x82F46` actually ran |
| `M2_PSR` | `0x00000000` | the release guard `0x8270A` was never reached at a patched visit |

`V_MAGIC == 0xA4A4A4A4` with `V_CNT >= 1` and a valid entry `V_PSR` is fwprobe.md sec.6 **row 1**. The
device CPU DOES take the interrupt: it enters the firmware ISR and reads the IAR, and the handler's own
`cpsie i` window then runs with IRQs live (`M1_PSR` I=0). This retires the flat "the CPU never takes the
IRQ" reading that 16b left open on the strength of `N_ACT=0` alone. The take path is not dead.

### The sampled gates and the disagreement the row-1 cross-check predicted

| cell | CA / source | value | meaning |
| --- | --- | --- | --- |
| `C_GRP0` | `0x40160114` | `0x00000000` | the IGRPEN0-prediction word reads 0, so the giccpu.md sec.1c config candidate is NOT the take-off |
| `C_GRP1` | `0x40160118` | `0x0000001D` | the HPPIR word names **id `0x1D`**, not `0x4c` and not `0x3FF` |
| `C_GRP2` | `0x4016011C` | `0x00000000` | the ABPR/legacy-IGRPEN1 candidate reads 0 |
| `F_BPR` | `0x40160108` | `0x00000003` | matches the image's only writer (file `0x8305A`), the CPU interface is armed as designed |
| `C_PSR` (`B_D5`) | core | `0x20000193` | I=1, SVC: the CPU was masked AT THE DRAIN sample |
| `F_CTLR` (`B_D6`) | `0x40160100` | `0x00000001` | EnableGrp0 SET, no bit2/bit3 quirk - the giccpu.md sec.2 candidate #3 dies |
| `F_PMR` (`B_D7`) | `0x40160104` | `0x000000F0` | the live value, above the source priority `0x50`, so no PMR block |
| `C_SNT` | - | `0x50AA7E49` | the giccpu pad ran; the page retained |
| `N_ACT` / `F_ACT` | `0x40161308` | `0x00000000` | ISACTIVER2 word 2 bit 12 clear at both post-exit samples |
| `N_ISP` / `F_ISP` | `0x40161208` | `0x00001021` | word 2 bit 12 SET: source `0x4c` still pending at both samples |
| `N_HPP` / `A_S4` | `0x40160118` | `0x0000001D` | HPPIR names `0x1D` |
| `F_HPP` | `0x40160118` | `0x00000040` | HPPIR names `0x40` at the fall-through |
| `N_OU0` / `F_OU0` | `out[0]` | `0x00000008` | unconsumed at both post-exit samples |

Row 1's own cross-check text says the inta3 cells must agree in the same epoch (`F_ACT`/`N_ACT` bit 12
SET, `out[0]` 8 -> 0). They do not. The reconciliation is in the HPPIR reads, and it's the load-bearing
finding: **the IAR never names `0x4c` in this run.** HPPIR - the word that decides which id the IAR read
returns - names `0x1D` at the site-N/giccpu instants and `0x40` at site F, and it names `0x4c` nowhere.
So the four ISR entries serviced a higher-priority pending source, `out[0]` for `0x4c` stayed `0x8`, and
`0x4c`'s own bit stayed pending in `ISPENDR2` (`0x1021`) and never went active (`ISACTIVER2` bit 12 = 0).
The canary and the site cells are consistent once you keep the two facts apart: the CPU takes IRQs, and
this boot the arbitration never handed it `0x4c`.

How `0x1D` and `0x40` got pending is the run's open mechanism, and it is stated as an open question, not
a verdict. `0x1D` is a fresh id, absent from every earlier record in this file (`fn_array[0x1D]` is a
non-null default at file `0x6E84`, so it has a handler). The twin storm preamble is the standing
candidate, and the priority ordering between the storm's id and `0x4c`'s priority byte `0x50` is the
thing to read next. What this boot proves is narrower and solid: the take is not gated by a stuck mask,
so the remaining wall is which source the arbitration serves, not whether the CPU ever takes an IRQ.

### The sequencing verdict (takeseq.md, refined)

takeseq.md's headline holds: the mask is a per-CPU counter, and the lift lives in the release helper's
caller (`0x82730` / `0x82B7A` -> `0x826E0`), not in the ETE bring-up, so our takeover never completes
step 8. Two things move with this run. First, the gate is not a hard wall: the mask DOES lift at points
in the boot, because the ISR ran four times (`M1_PSR` I=0 confirms a live window). Second, `M2_PSR = 0`
says the release guard `0x8270A` was never reached, so the release body never issued this boot - the
bring-up caller's release never ran, exactly the step-8 miss takeseq.md predicted. The picture is
coherent: the vendor path lifts the mask through a release our takeover skips, and the interrupts the
CPU does take are the ones whose priority wins while the mask happens to be open. The `0xcece` park exit
without a full unmask is what leaves `0x4c` unserved.

### The bounds (declared, not hidden)

1. **One-shot latched samples, not a trace.** `V_CNT` is an `ldr/adds/str` that runs with IRQs live, so a
   nested entry can race it (undercount <= 1 per nested entry); `V_MAGIC` is the binary proof, `V_CNT` the
   count. `M1_PSR` exists only when `fn_array[id]` is non-null, and both CPSR milestones hold the LAST
   visit's value, not every visit's.
2. **The four entries are not source-attributed.** The vec pad fires on ANY firmware IRQ entry, so
   `V_CNT = 4` counts the shared stub, not `0x4c`'s deliveries. With `0x45` (bit 5) and `0x1D`/`0x40` in
   play, the run does not say how many of the four were `0x4c`. The HPPIR reads are what argue none were.
3. **HPPIR alone is not a take record.** HPPIR names the highest-priority pending id; it does not prove
   the ISR's IAR read returned that id. The priority byte of `0x1D`/`0x40` versus `0x4c` (`0x50`) is not
   sampled here.
4. **The interp is the skeleton, filled by this block.** The run's own `interp.txt` landed `TODO`; the
   rows above are the interpretation, sourced from `capture-cmd.txt` and the record.

### Verification

Instrument verdict `build/register-dumps/diffs/20261005T2026Z-vtool13/verdict.txt`: **CONFIRMED**. The
selftest asserts every frozen pin (`rc 0`), two independent regenerations are byte-identical to the
staged blob (`0c681650...`), an independent 174-check byte re-derivation is `174 passed, 0 failed`, and
the hard-rule audit is clean (no write of `0x400392f0`/`0x40039af0`, no pad reads the IAR, no device
access). One non-inverting note (D1): the m2 site/pad sits 4 bytes off fwprobe.md's stale sec.3/4 numbers
because the take1 needs list adds the 106-byte giccpu pad; capstone resolves the branch and the pad
program is byte-identical apart from the tail displacement.

Run facts: `[sig]` 9/9 (`THE CHIP LEFT ROM STATE`), done marker `init done wiphy=omo-drv1 ifname=omowl1
hw=1 regs=decoded irq0=207 isr0=65`, window sanity `0xE59FF018`, sentinels present, staged `.omo-pat` ==
the host blob md5, stock md5 re-verified `0e530b976d5a20e87358671f1a577695`. Host witness: `207: 65 ...
omo-drv1`, `209: 9 ... omo-drv1-ep1`; the mandatory bound tripped on both storm lines during the
preamble (`irq=207 n=65 bound=64`, `irq=209 n=9 bound=8`), no `enable_irq` after the trip, `rung=OBSERVE`.
Health `WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`; no new pstore
record; 2 wiphys, 6 interfaces, calibration `[SUCC]` on both bands.

### The next threads

- **Read the priority bytes.** Sample `IPRIORITYR` for `0x4c`, `0x1D` and `0x40` (`0x40161400 + 4*id`) at
  the same post, so the arbitration between the storm's pending ids and `0x4c` (`pri 0x50`) is measured,
  not guessed.
- **Attribute the entries.** A pad that stores the IAR-returned id per ISR entry (a banked cell the vec
  pad cycles through) turns `V_CNT = 4` into four named ids and settles whether any entry served `0x4c`.
- **Drive the release, not the register.** takeseq.md's step 8 is the miss: exercise the bring-up
  caller's release (`0x82730`/`0x82B7A` -> `0x826E0`) so the mask lifts through the vendor's own path and
  then ring `0x4c` alone, with the twin storm quiet, so HPPIR has nothing ahead of it.

### Artifacts

Evidence `build/register-dumps/exp/20261005-202555/`; blob `build/tmp/fw-patched/take1.bin` md5
`0c681650de28488ab75fb5755684f0c7`; manifest `build/tmp/fw-patched/take1.bin.manifest.json`; runner
`build/tmp/wifidrv1-art/run-take1.sh`; hook `build/tmp/wifidrv1-art/take1-capture.hook`; verifier
`build/tmp/wifidrv1-art/take1-verify.py`; instrument verdict
`build/register-dumps/diffs/20261005T2026Z-vtool13/`; specs `build/tmp/inta-spec/{fwprobe,takeseq,giccpu}.md`;
ko `3f87f1e9fe5ed9666f27d1f784d34535` (v8 of `553342d`, `omo/phase22-hccaccept`).
---

# ADDENDUM 18 (2026-10-05): the 0x4c selection - THE FLIP LANDS BUT THE TAKE NEVER MOVES (`X_HPP` still 0x1D, `X_ACT` bit12 CLEAR, `X_OU0=8`) / the IAR never names 0x4c / the PPI byte `0x1D` reads back `0xE0` and still out-ranks / one IAR return is the invalid id `0x402`, so this boot's ids are suspect

ADDENDUM 17 named three next threads and the first two are what this cycle answers: read the priority
bytes at the same post and attribute the entries. The discriminator spec (`build/tmp/inta-spec/select.md`)
turned that into one mutation: push `0x4c` above every competitor (its byte to `0x00`), drop the
competitors below it (`0x1d` and `0x40` to `0xF0`), then re-ring and read what the firmware's own IAR
returns per entry. This block is the interpretation. Evidence `build/register-dumps/exp/20261005-210057/`
(`capture-cmd.txt`, `run-take2.log`, `dmesg.txt`, `KNOBSET.txt`, `pstore-delta.txt`); the instrument's
verdict is `build/register-dumps/diffs/20261005T2101Z-vtool14/verdict.txt` (CONFIRMED).

### The instrument (take2 + gicv2)

The staged blob is the `take2` variant of `tools/patch_fw_scratch.py` (working tree, uncommitted at
record time), md5 `eeeb252f20392f2b0cb861336feb6411`, size 928920 (size-preserving). It layers the
`gicv2` IAR-source canary (`select.md` sec.5, `gicv2.md`) on top of take1: the `vec` pad now reads the
IAR itself into r7 (the handler's one read, re-emitted), stores the returned word, the MPIDR, a 4-deep ring
of the last four ids and the entry counter, and the pad program is byte-verified by capstone. On top of that
take2 adds the three priority stores and the discriminator read block (`P_ID`, `P_40`, `P_4C`, `G_GRP0`,
`N_EN0`) plus the after-the-ring readbacks (`X_HPP`, `X_ISP`, `X_ACT`, `X_OU0`, `X_SNT`). One cycle,
`run-take2.sh`, `TAKE2 RESULT: PASS`, `exp_rc=0`, 21:00:56Z -> 21:03:41Z; the ko is the v8 of `553342d`
(`3f87f1e9...`, unchanged, no ko commit, no CI) with `intapost=0x100 quiesce=0x1 rung=0 qbound=64
qbound209=8`, so the question is asked under the reproduced twin storm. All new cells sit on page 10
(runtime `0x150000`, the retention-verified band) and read alias-only.

### The v2 canary: the take happened four times, and one id is invalid

| cell | value | reading |
| --- | --- | --- |
| `V_MAGIC` | `0xA4A4A4A4` | the vec pad ran: the IRQ vector body executed and the IAR read is on the entry path |
| `V_PSR` | `0x00000193` | ISR-entry CPSR: I=1, M[4:0]=`0x13` SVC, T=1 |
| `V_CNT` | `0x00000004` | the ISR was entered FOUR times this boot |
| `V2_CNT` | `0x00000004` | the v2 pad sampled four IAR reads (agrees with `V_CNT`) |
| `V2_ID` | `0x00000402` | **the NEWEST IAR return word; `ubfx 9:0` = `0x002`, NOT `0x4c` and NOT `0x1d` - and not a valid GIC id** |
| `V2_MPIDR` | `0x80000000` | low bits 0 -> the take landed on CPU0 |
| `V2_RING0..3` | `0x00000000`, `0x00000001`, `0x00000002`, `0x00000402` | the ring holds ids `0x0,0x1,0x2` then `0x402` (write order) |
| `M1_PSR` / `M2_PSR` | `0x20000113` / `0x00000000` | as ADDENDUM 17: the `cpsie i` window runs, the release guard never reached |

`V_MAGIC` live and `V_CNT == V2_CNT == 4` says the CPU took four interrupts and the pad clocked every one.
The decisive cell is `V2_ID = 0x00000402`: the firmware's own IAR read returned a word whose id field
(`0x402 & 0x3ff`) is `0x002`, which is not `0x4c`, not `0x1d`, and not the spurious `0x3ff`. The ring's
first three slots carry the small ids `0x0`,`0x1`,`0x2` and the newest is `0x402`; the ids rotate, so the
four entries did not all serve the same source.

### The flip landed, and it did NOT hand the take to `0x4c`

| cell | CA / source | value | meaning |
| --- | --- | --- | --- |
| `P_ID` | `0x4016141C` w7 (byte1 = id `0x1D`) | `0xF0F0E0F0` | byte1 reads `0xE0`, the authored PPI byte - `0x1D`'s priority was NOT `<= 0x50` |
| `P_40` | `0x40161440` | `0xF050F0F0` | byte0 `0xF0`: the `0x40` store landed |
| `P_4C` | `0x4016144C` | `0xF050F000` | byte0 `0x00`: the `0x4c` store landed, so `0x4c` is now the top-priority pending source |
| `G_GRP0` | `0x40161080` w0 | `0x00000000` | the PPI/SGI bank is group 0 (never sampled before); bit29 = 0 |
| `N_EN0` | `0x40161100` w0 | `0x2000FFFF` | bit29 SET: id `0x1D` is enabled in `ISENABLER0` |
| `X_HPP` | `0x40160118` | `0x0000001D` | HPPIR after the ring still names `0x1D` - the GIC's own view did not promote `0x4c` |
| `X_ISP` | `0x40161208` w2 | `0x00001021` | bit12 SET: `0x4c` still pending |
| `X_ACT` | `0x40161308` w2 | `0x00000000` | bit12 CLEAR: `0x4c` was never acknowledged (the IAR was never read for it) |
| `X_OU0` | `0x40039010` | `0x00000008` | out[0] still `0x8`: the dispatcher never consumed it |
| `F_HPP` | `0x40160118` at site F | `0x0000004C` | at the fall-through the top pending id IS `0x4c`, so the path reaches the point where only the take is missing |
| `X_SNT` | page sentinel | `0x50AA7E49` | the readback pad ran; the page retained |

This is the load-bearing finding and it cuts against the discriminator's sec.5 expectation. The flip stores
DID land (`P_4C` byte0 = `0x00`, `P_40` byte0 = `0xF0`), so `0x4c` carries the top priority and
`0x1d`/`0x40` were pushed to `0xF0`. Yet `X_HPP` still reads `0x1D` and the v2 canary never saw `0x4c`:
`V2_ID`'s four samples returned small ids and `0x402`, never `0x4c`, and `X_ACT` bit12 stayed clear, so
the IAR was not read for `0x4c` a single time. `F_HPP = 0x4C` shows the flip DOES put `0x4c` on top at the
send site's own instant, so the destination is reachable; what the flip did not do is make the four ISR
entries serve it. The mutation moved the pending-id ranking and left the take exactly where ADDENDUM 17
found it.

### The `0x1D` residual: the byte reads `0xE0`, so priority-it-first does not explain `HPPIR = 0x1D`

ADDENDUM 17 and select.md sec.4 left one input open: the disasm writes `IPRIORITYR[0x1D] = 0xE0` (file
`0x8307A`, `prio = 0xE`, LOWER priority than `0x4c`'s `0x50`), yet the record read `HPPIR = 0x1D` at the
site where `0x4c` is pending. The discriminator's `P_ID` answers the byte question directly: `P_ID` byte1
reads back `0xE0`, exactly the authored value. So this boot confirms the byte is `0xE0`, and `HPPIR` STILL
names `0x1D` while `0x4c` sits at priority `0x00`. Under select.md sec.2's rule (`0x4c` at `0x00` beats
every competitor) that should be impossible for an ordinary SPI comparison: `0x1D`'s effective priority is
NOT what its byte says. The residual is the PPI-vs-SPI comparison basis, and the group explanation is killed
too: `G_GRP0` bit29 = 0 while `HPPIR` names `0x1D`, so the PPI and the SPIs share the group-0 space and
the difference is not grouping. What is left is a banked-PPI priority that is not compared as an SPI's is.

### The `0x402` id: one IAR return is not a valid source id

`V2_ID = 0x00000402` is the run's sharpest anomaly. `0x402` is not a GIC id in any register this file has
read; the id field (`ubfx 9:0`) of a real IAR read carries a source id or `0x3ff` (spurious), and `0x402`
is neither. Two readings, stated rather than resolved:

1. **The priority store reached a word it should not have.** Writing `0x4016141D` byte-lane 1 touches the
   `IPRIORITYR` word for ids `0x1c..0x1f`; a store that also perturbed the SGI/PPI bank could translate oddly
   at the CPU interface. The ring (`0x0`,`0x1`,`0x2`, then `0x402`) is consistent with the first three
   entries being PPI/SGI-class small ids and the fourth the perturbed one.
2. **The pad's IAR read raced the flip.** The pad reads `0x4016010C` at the entry instant; if a store in the
   same epoch left the IAR transiently reading a stale word, `0x402` is that word.

Either way the honest statement is narrow: in THIS boot the flip did not produce a clean `0x4c` take, and
one IAR return was not a valid source id. The mutation must be re-run before its per-entry ids are used as
arbitration evidence.

### The bounds (declared, not hidden)

1. **One shot, four entries, one boot.** `V_CNT = V2_CNT = 4`, and the four ids are not source-attributed
   beyond the ring's write order. A single-boot anomaly (`0x402`) is not a stable property of the flip.
2. **The flip's stores are read back, not proven atomic.** `P_4C`/`P_40` read post-write, so the stores
   landed; whether they landed in the same instant the pad needs (before the ring, without a race) is not
   observable from these cells.
3. **HPPIR alone is not a take record.** `X_HPP = 0x1D` and `F_HPP = 0x4C` name the top pending id at two
   instants; neither proves the ISR's IAR read returned that id. `V2_ID` is the take record, and it never
   shows `0x4c`.
4. **The interp is this block.** The run's own `interp.txt` landed `TODO`; the rows above are sourced from
   `capture-cmd.txt` and `run-take2.log`.

### Verification

Instrument verdict `build/register-dumps/diffs/20261005T2101Z-vtool14/verdict.txt`: **CONFIRMED**. The
verifier re-derived (not re-read) the take2/gicv2 blobs: the selftest asserts all 17 variant md5 pins plus
`FW_MD5`/`FW_SIZE`/`BARMAP_MD5`, two independent regenerations of each variant are byte-identical to the
staged blob (`gicv2 d98189ce...`, `take2 eeeb252f...`), a 193-check independent capstone re-derivation
passes, the pads are all-zero in stock and mutually disjoint, and the hard-rule scan is clean (no write of
`0x400392f0`/`0x40039af0`, no pad reads the IAR except the firmware's own, no `0x10161000` access). The ko
pin is `3f87f1e9fe5ed9666f27d1f784d34535` (v8 of `553342d`).

Run facts: done marker `init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded irq0=207 isr0=65`, window
sanity `0xE59FF018`, staged `.omo-pat` md5 == the host blob (`eeeb252f...`), stock md5 re-verified
`0e530b976d5a20e87358671f1a577695`. Host witness: `207: 65 ... omo-drv1`, `209: 9 ... omo-drv1-ep1`; the
mandatory bound tripped on both storm lines (`irq=207 n=65 bound=64`, `irq=209 n=9 bound=8`), supervisor
reached `SUPERVISOR DONE quiesced=0 rung=OBSERVE ... state=IDLE`, no `enable_irq` after the trip. Health
`WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`; no new pstore record (the
baseline `blk-0/2/3` set is unchanged); 2 wiphys, 6 interfaces, calibration `[SUCC]` on both bands.

### The next threads

- **Re-run the flip clean.** The `0x402` id means this boot's IAR values are suspect. `P_ID` byte1 already
  reads `0xE0`, so skip the id-`0x1D` store and keep the only mutation as `0x4c -> 0x00`; re-read `V2_ID`
  per entry.
- **Test the PPI comparison basis directly.** Since `0x1d` reads `0xE0` and still wins, the comparator is
  not the byte. Mask `0x1D` at `ISENABLER0` bit29 and re-ring, so `0x1D` leaves the set instead of being
  out-prioritized; sample the `0x1D` handler's own per-core accept register (`0x4016060C`) for the live view.
- **Retire the `0x40` re-arm.** select.md sec.0 named `0x40` (EDGE, self-refilling) as the standing
  lower-id winner. Mask it (`ISENABLER2` bit0) with `0x45` (bit5) and `0x4c` (bit12) still pending, then
  read which id the next take returns, so arbitration order is measured with `0x40` out of the set.

### Artifacts

Evidence `build/register-dumps/exp/20261005-210057/`; blob `build/tmp/fw-patched/take2.bin` md5
`eeeb252f20392f2b0cb861336feb6411` (and `gicv2.bin` `d98189ceff6e15582dc6261299cb3477`); runner
`build/tmp/wifidrv1-art/run-take2.sh`; hook `build/tmp/wifidrv1-art/take2-capture.hook`; verifier
`build/tmp/wifidrv1-art/take2-verify.py`; instrument verdict
`build/register-dumps/diffs/20261005T2101Z-vtool14/`; specs `build/tmp/inta-spec/{gicv2,select}.md`; ko
`3f87f1e9fe5ed9666f27d1f784d34535` (v8 of `553342d`, `omo/phase22-hccaccept`).

---

# ADDENDUM 19 (2026-10-05): the tie-break - ROW 5 THE INSTRUMENT: the 0x4C promotion HELD to the END and the 0x1D competitor-disable NEVER LANDED (an emitter bug); NO ROW QUIESCED; the take3 boot reproduces take2's out-ranking and cannot yet separate the blocker

Task `st_01a10deb`. The tie-break cycle `tiebreak.md` sec.3 specified, run as variant **`take3`**: the full
reverse-audit a take2 defect called for, in ONE boot. Blob `build/tmp/fw-patched/take3.bin` md5
`072de986879bdbad00f339b2ad21ebd7`, `wifidrv1.ko` `3f87f1e9fe5ed9666f27d1f784d34535` (v8 of `553342d`, reused
unchanged). Evidence `build/register-dumps/exp/20261005-212711/`; instrument verdict
`build/register-dumps/diffs/20261005T2130Z-vrun16/verdict.txt`. The cycle ran serial/detached via
`tools/exp.sh` with the watchdog armed first and the mandatory bound `qbound=64`/`qbound209=8`; it recovered
with `TAKE3 RESULT: PASS` at 21:29:56Z (the harness health verdict, not the take physics).

## Short version

The synchronized flip's `0x4C` priority promotion LANDED and HELD to the end of the boot (`E_P4C` byte0 = `0x00`,
`E_EN2` bit12 set, `GICD_CTLR`.RWP = 0). Its other half, the competitor-disable that would have pulled the
banked TWD PPI `0x1D` out of the ENABLED set, was a NO-OP: the emitter `write_ca_block()` writes
`movw r0,#0` for the mask `0x20000000` and never emits the `movt r0,#0x2000` half, so `ICENABLER0`/`ICPENDR0`
got `0x00000000`. `E_EN0` bit29 still reads SET, and `0x4C` stayed pending and unaided (`E_ISP` bit12 set,
`E_ACT` 0, `E_OU0` 8) with `E_HPP = 0x1D`. The boot therefore does NOT test `0x4C` promoted AND `0x1D` masked;
it reproduces take2's out-ranking under the tighter mask, and the verdict is ROW 5, the pad/site fault, with
a NAMED cause. It is not a re-run of take2: the end-state E block and the mask that DID land are new, and they
isolate the blocker to the one store the emitter dropped. The honest next step is a one-line encoder fix
(`movt r0, value>>16` when `value > 0xFFFF`) and the same boot again.

## The take3 protocol, and what each of its halves did

`tiebreak.md` sec.3 sequenced four steps into the `selpre` slot: competitor-disable (`ICENABLER0/ICPENDR0` for
`0x1D`, `ICENABLER2/ICPENDR2` for `0x40`/`0x45`), competitor pending-clear, `0x4C` source-disable -> priority
write `0x00` -> source-enable with `dsb sy`, then the usual re-ring and a new END-STATE read block. Two of the
four landed:

| step | the register | END-state read | landed? |
| --- | --- | --- | --- |
| `0x40`/`0x45` mask (`ICENABLER2` w2 <= `0x21`) | `0x40161188` | `E_EN2` = `0x5000` (bits0/5 CLEAR, bit12 SET) | YES |
| `0x4C` priority -> `0x00` | `0x4016144C` | `E_P4C` byte0 = `0x00` | YES (held) |
| `0x1D` mask (`ICENABLER0` w0 <= `0x20000000`) | `0x40161180` | `E_EN0` = `0x2000FFFF` (bit29 SET) | **NO (after-image `0`)** |
| `0x1D` pending-clear (`ICPENDR0` w0) | `0x40161280` | (same defect) | **NO (after-image `0`)** |

The w2 values are both `<= 0xFFFF` (`0x21`, `0x1000`), so they were emitted with a correct single `movw` and
landed. The w0 values are `0x20000000`, which needs the high halfword; the built pad at file `0xc80a8` carries
`movw r1,#0x1180` / `movt r1,#0x4016` / `movw r0,#0` / `str r0,[r1]` for the ICENABLER0 store (byte-for-byte
present in the pad), i.e. it writes `0x00000000` to `0x40161180`, and the same shape for `ICPENDR0`.

## The named cause: `write_ca_block()` drops the high halfword

`tools/patch_fw_scratch.py:1779` `write_ca_block(ca,val)` emits `movw(0, val & 0xFFFF)` and no
`movt(0, val >> 16)`, so every 32-bit store with a nonzero high halfword silently becomes its low halfword
(`0x20000000 -> 0x00000000`). The build's own self-check could not catch it: the take3 assertions and
`take3-verify.py` count `write_ca_block(ca, val)` calls and re-assert the `TI_MASK_1D = 0x20000000` constant,
and the CHECKER is the buggy encoder, so it reproduces the same zero emission and passes. Two consequences the
record owes the next runner: (1) the take3 build was never adjudicated in writing - `vtool15`'s verdict is
still an unfilled skeleton; (2) the emitter fix and a re-run are the ONLY thing standing between this boot and
the test tiebreak.md actually wanted. Two smaller residuals ride along: `E_EN2` bit14 (`0x4E`) is SET at the
END though no pad writes it, consistent with tiebreak.md R3's late writer (file `0x7edd8` registers `0x4c` AND
`0x4e`; its id source is a RAM table, unproven); and `E_P40 = 0xF050F050` differs from take2's `P_40 =
0xF050F0F0`, which says the authored priority bytes are per-boot firmware state, so the mask evidence is
`E_EN2`, not the byte.

## The end-state E block (the take2 readback gap closed)

ADDENDUM 18's honest bound was that take2 read the priority/window words only at the flip's instant, with a
single end-of-boot sample and no re-read of priorities, `ISPENDR` or `HPPIR`. take3 adds the E block, twelve
page-10 cells read at `selpost` before the gate loop (alias `0x406B8000` + runtime):

| cell | CA / source | value | meaning |
| --- | --- | --- | --- |
| `E_P1D` | `0x4016141C` w7 (byte1 = id `0x1D`) | `0xF0F0E0F0` | byte1 still reads `0xE0`, byte-identical to take2's `P_ID` |
| `E_P40` | `0x40161440` | `0xF050F050` | byte0 `0x50`, the firmware's own value (take2 read `0xF0` here) |
| `E_P4C` | `0x4016144C` | `0xF050F000` | byte0 `0x00`: the flip held to the END, no late re-write |
| `E_HPP` | `0x40160118` | `0x0000001D` | the GIC's own view still promotes `0x1D` over a pending `0x4C` |
| `E_ISP` | `0x40161208` w2 | `0x00001021` | bits 0/5/12: `0x40`, `0x45` and `0x4C` all still pending |
| `E_ACT` | `0x40161308` w2 | `0x00000000` | bit12 CLEAR: `0x4C` was never acknowledged |
| `E_OU0` | `0x40039010` | `0x00000008` | the dispatcher never consumed it |
| `E_EN0` | `0x40161100` w0 | `0x2000FFFF` | bit29 SET: **`0x1D` was never masked** |
| `E_EN2` | `0x40161108` w2 | `0x00005000` | bit12 SET, bits0/5 CLEAR: the w2 half landed |
| `E_CTLR` | `0x40161000` | `0x00000001` | RWP bit31 = 0 (no write in flight), EnableGrp0 set |
| `E_PSR` / `E_SNT` | CPSR / page sentinel | `0x20000193` / `0x50AA7E49` | the mask is open at the sample; the pad ran |

Retained take2 cells in the same boot agree: `X_HPP = 0x1D`, `X_ISP = 0x1021`, `X_ACT = 0`, `X_OU0 = 8`,
`F_HPP = 0x4C` at the fall-through. So the take record never moves even with the flip held to the end.

## The take record: the IAR still names the SGI-class ids, never 0x4C

`V_MAGIC = 0xA4A4A4A4` and `V_CNT = V2_CNT = 4` say the firmware's ISR ran and the v2 pad clocked every one
of the four IAR reads. `V2_ID = 0x00000402` and the ring `0,1,2,0x402` are byte-identical to take2: the
`0x402` word is still not a valid GIC source id (its `ubfx 9:0` field is `0x002`), and take3 provides no new
reading of it. The two open explanations from ADDENDUM 18 stand unchanged: the store perturbed the SGI/PPI
bank, or the pad's IAR read raced the flip. With `0x4C` enabled, promoted to `0x00` and pending, and its two
SPI competitors masked, the argument "the `0x4C` take cannot move purely from priority" is now measured, not
predicted; ADDENDUM 18's prediction ("a landed `0x00` cannot promote a high id when a lower id ties it") is
reproduced with the mask that landed, but the "priority is not the comparator" reading STAYS BLOCKED because the
one source that out-ranks `0x4C` at the CPU interface, the banked PPI `0x1D`, was left enabled. That is the
whole gap the emitter bug opens.

## The bounds (declared, not hidden)

1. **The competitor-disable half of the flip is untested.** `E_EN0` bit29 SET is an after-image of a defect,
   not an arbitration result, so this boot cannot separate the priority tie from the banked-PPI comparison
   basis. The isolation is the take4 fix (emit the `movt`) plus the same rerun.
2. **One shot, four entries.** The take record is a single boot's ring; the `0x402` anomaly is not a stable
   property of any variant, and per-entry ids need a clean re-run before they are used as arbitration evidence.
3. **The `E_EN2` bit14 (`0x4E`) enable is unexplained** (no pad writes it). Consistent with R3's runtime-id
   writer whose id source is a RAM table, bounded by this run's readbacks but not closed.
4. **`0x1D`'s byte does not explain its rank, again.** `E_P1D` byte1 reads `0xE0` and `E_HPP` still names
   `0x1D` while `0x4C` reads `0x00`, so priority-it-first is not the whole story; the PPI-vs-SPI comparison
   basis remains the standing suspect.
5. **No `0x1D` take record.** The take record only counts the firmware's ISR entries; a masked `0x1D`
   assertion and a code fault can both leave the record empty, which is why the mask step matters and which
   is exactly the half that did not run.
6. **The verdict text lands in the addendum, not `interp.txt`** (the run's `interp.txt` is the finish-evidence
   skeleton), so the rows above are sourced from `capture-cmd.txt`, `dmesg.txt`, `health.txt`, `pstore-delta.txt`
   and the verdict, each named.

## Verification

Instrument verdict `build/register-dumps/diffs/20261005T2130Z-vrun16/verdict.txt`: **CONFIRMED on all eight
claims, ROW 5**. C1 the pad sampled (`V_MAGIC` live, `E_SNT` = `0x50AA7E49`, staged blob = take3);
C2 the take did not move (a verified negative); C3 the flip held (`E_P4C` byte0 `0x00`, `E_CTLR` RWP 0);
C4 the w2 mask landed (`E_EN2` = `0x5000`); C5 the `0x1D` disable did NOT land (`E_EN0` bit29 SET; the pad's
ICENABLER0 stores are `0x00000000`); C6 the mandatory bound tripped `207 n=65 bound=64` and `209 n=9 bound=8`
and self-disabled (`IRQ_LEFT_DISABLED reason=bound`, no `enable_irq` after); C7 the router is healthy;
C8 no crash. The verifier re-disassembled the staged pad with capstone and re-ran the emitter, reproducing the
zero byte-for-byte. Its live read-only probe after the cycle: no `.omo-pat` leftover, vendor modules loaded,
stock `FIRMWARE.bin` md5 unchanged, 2 wiphys, 6 interfaces, calibration `[SUCC]` on both bands.

Run facts: `stage2`-style done marker `init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded irq0=207
isr0=65`; window sanity `0xE59FF018`; host witness `207: 65 ... omo-drv1`, `209: 9 ... omo-drv1-ep1`; the
bound trips `irq=207 n=65 bound=64` and `irq=209 n=9 bound=8`; supervisor reached `SUPERVISOR DONE
quiesced=0 rung=OBSERVE ... state=IDLE`; `health.txt` = `WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0
STAGED=0 LOADER=0 RECOVER=0`; `pstore-delta.txt` = NO new record (baseline `blk-0/2/3` unchanged).

## The next threads

- **Fix the emitter and re-run the same boot.** Emit `movt r0, value>>16` in `write_ca_block()` when
  `value > 0xFFFF`, re-stage `wifidrv1.ko`, and re-run take3 unchanged. A rerun whose `E_EN0` bit29 reads
  CLEAR is the first boot that actually tests `tiebreak.md`'s lever; take3's `E_P4C`/E block then become
  the checkpoints.
- **Re-run the flip clean for the `0x402` id.** Keep only `0x4C -> 0x00` as the mutation and re-read
  `V2_ID` per entry, so this boot's per-entry ids stop carrying a suspect word.
- **Take `0x1D` out of the enabled set as the standalone test.** Since `E_P1D` byte1 still reads `0xE0` and
  `E_HPP` still names `0x1D`, mask `ISENABLER0` bit29 and re-ring with `0x4C` promoted; the `0x1D` handler's
  per-core accept register (`0x4016060C`) is the live view to sample.
- **Retire the `0x40` re-arm.** `E_ISP` still holds `0x40`/`0x45` pending alongside `0x4C`; mask `0x40`
  (`ISENABLER2` bit0) and `0x45` (bit5) with `0x4C` promoted, then read which id the next take returns.

## Artifacts

Evidence `build/register-dumps/exp/20261005-212711/`; blob `build/tmp/fw-patched/take3.bin` md5
`072de986879bdbad00f339b2ad21ebd7`; the retained take1/take2 blobs `0c681650...`/`eeeb252f...`; runner
`build/tmp/wifidrv1-art/run-take3.sh`; hook `build/tmp/wifidrv1-art/take3-capture.hook`; verifier
`build/tmp/wifidrv1-art/take3-verify.py`; instrument verdict
`build/register-dumps/diffs/20261005T2130Z-vrun16/`; specs `build/tmp/inta-spec/{tiebreak,select}.md`; ko
`3f87f1e9fe5ed9666f27d1f784d34535` (v8 of `553342d`, `omo/phase22-hccaccept`). Hard rules held: the pad
writes no `0x400392f0`/`0x40039af0`, no pad reads the IAR except the firmware's own, no `0x10161000` access,
staged as `wifidrv1.ko`, cycle serial/detached with the bound armed, router left healthy.

---

# ADDENDUM 20 (2026-10-06): the separation - THE EMITTER FIX LANDED (the `0x1D` PPI LEFT THE ENABLED SET) and the `0x4C` PROMOTION HELD, YET THE TAKE STILL DID NOT MOVE: the ring stayed SGI-owned (`0,1,2,2`) and `0x4C` stayed pending+untaken, so the residual is now the SGI BANK, not the PPI; `E_HPP` read `0x3FF`, not `0x4C`, so NO `sep.md` sec.2 row matches verbatim

Task `st_01a1113f`'s boot, `st_01a11137`'s build (`sep.md`), `st_01a11133`'s design. ADDENDUM 19 ended on one
line: emit `movt r0, value>>16` in `write_ca_block()` when `value > 0xFFFF`, re-stage `wifidrv1.ko`, and run
the same boot again. take4 is that boot. The emitter fix is measured on-device, and it retired take3's named
blocker, but the take still did not move. This block is the interpretation; the boot's own `interp.txt` landed
the finish-evidence skeleton.

Evidence `build/register-dumps/exp/20261006-124348/` (`capture-cmd.txt`, `run-take4.log`, `dmesg.txt`,
`health.txt`, `pstore-delta.txt`, `KNOBSET.txt`, `PACKED.txt`); adversarial verdict
`build/register-dumps/diffs/20261006T1246Z-vrun17/verdict.txt` (8/8 CONFIRMED, C1-C8); instrument verdict
`build/register-dumps/diffs/20261006T1243Z-vtool16/verdict.txt`. Cycle `run-take4.sh` -> `tools/exp.sh`,
`TAKE4 RESULT: PASS`, `exp_rc=0`, 12:43:47Z -> 12:46:30Z; ko `wifidrv1.ko`
`3f87f1e9fe5ed9666f27d1f784d34535` (the v8 of `553342d`, reused unchanged, no ko commit, no CI), blob
`build/tmp/fw-patched/take4.bin` md5 `000a26af4d7b12e84d4191ae51dbad6d` (size-preserving, 928920 B). All new
cells sit on page 10 (`capture alias 0x406B8000 + runtime`) and read alias-only. Hard rules held: no write of
CA `0x400392f0`; no read of `0x10161000`; no pad reads the ack IAR `0x4016010c` except the firmware's own; ko
staged as `wifidrv1.ko`; cycle serial/detached with the bound armed; recover after.

## Short version

The take4 hypothesis is CONFIRMED at the instrument level and FALSIFIED at the physics level. The emitter fix
landed: `E_EN0` = `0x0000FFFF`, bit29 CLEAR, so the banked TWD PPI `0x1D` really left the ENABLED set, which
take3 could not do (`E_EN0` = `0x2000FFFF`). The `0x40`/`0x45` mask and the `0x4C` promotion held to the end
(`E_EN2` = `0x5000`, `E_P4C` byte0 = `0x00`, `E_CTLR` RWP = 0). And still the take did not move: the ring's
four IAR slots are SGI-class ids `0,1,2,2`, `E_ACT` bit12 CLEAR, `E_OU0` = `0x8`, `E_ISP` bit12 SET. `0x4C`
stayed pending, never acked, never consumed. The one source that out-ranked it is gone, the next contender is
the SGI bank, and `F_HPP` = `0x4C` at the later Site F epoch confirms `0x4C` is now the top *enabled* source.
The blocker shifted from the PPI axis to the SGI (bank) axis, which is exactly where `sginote.md` sec.4 pointed.

## The emitter fix: measured, not predicted

ADDENDUM 19 named `write_ca_block()` (`tools/patch_fw_scratch.py:1779`) as the defect: it emitted `movw(0,
val & 0xFFFF)` with no `movt` half, so any 32-bit store with a nonzero high halfword silently became its low
halfword (`0x20000000 -> 0x00000000`). take4 fixes the encoder for values above `0xFFFF` and adds a physical
presence check. The build side proves it before the boot (`vtool16`): `--check-emitted take4` PASS 8/8, while
`--check-emitted take3` FAIL (the frozen `0x1D` pair absent from the take3 pad). The device proves it again:

| cell | CA / source | take3 | take4 | reading |
| --- | --- | --- | --- | --- |
| `E_EN0` | `0x40161100` w0 (`GICD_ISENABLER`) | `0x2000FFFF` | `0x0000FFFF` | bit29 CLEAR: `0x1D` IS masked now |
| `N_EN0` | `0x40161100` at Pad A(N) entry | (n/a) | `0x0000FFFF` | bit29 CLEAR at the ring's entry pad too |

The `0x1D` competitor-disable reached `GICD_ICENABLER0`/`ICPENDR0`, something no earlier boot achieved. `sep.md`
rows 3 and 4 (mask-did-not-take / emitter-bug) are excluded by this line. take3's blocker is retired.

## The separation result: the take still does not move

With `0x1D` out of the enabled set, the two SPI competitors masked and `0x4C` enabled + promoted, the take
should have been `0x4C`. It was not.

| cell | CA / source | value | meaning |
| --- | --- | --- | --- |
| `V2_ID` | the IAR word the firmware's own read (file `0x82f04`) returned | `0x00000402` | `& 0x3ff` = `0x002`, SGI-class, not `0x4C`, not `0x1D` |
| `V2_RING0..3` | the 4-deep id ring | `0,1,2,` `0x402` | SGI-owned (`0x0,0x1,0x2`), newest slot again the suspect `0x402` |
| `V2_CNT` / `V_MAGIC` / `V_CNT` | pad | `4` / `0xA4A4A4A4` / `4` | the ISR ran four times, the pad clocked every IAR read |
| `E_ACT` | `0x40161308` w2 (`ISACTIVER2`) | `0x00000000` | bit12 CLEAR: the IAR was never read for `0x4C` |
| `E_OU0` | `0x40039010` (`out[0]`) | `0x00000008` | the dispatcher never consumed it |
| `E_ISP` | `0x40161208` w2 (`ISPENDR2`) | `0x00001021` | bit12 SET: `0x4C` still pending; bits 0/5 = `0x40`/`0x45` |
| `E_HPP` | `0x40160118` (`GICC_HPPIR`) | `0x000003FF` | nothing signalled at the ring epoch |
| `F_HPP` | `0x40160118` at Site F | `0x0000004C` | positive control: `0x4C` IS the top pending id later |
| `E_P4C` | `0x4016144C` byte0 | `0x00` | the promotion held to the END (no late re-write) |
| `E_EN2` | `0x40161108` w2 | `0x00005000` | bit12 SET (`0x4C` enabled), bits 0/5 CLEAR (`0x40`/`0x45` masked) |
| `E_CTLR` | `0x40161000` (`GICD_CTLR`) | `0x00000001` | RWP bit31 = 0, EnableGrp0 set |
| `E_SNT` / `E_PSR` | sentinel / CPSR | `0x50AA7E49` / `0x20000193` | the pad ran; the mask is open at the sample |
| `E_P1D` | `0x4016141C` byte1 = id `0x1D` | `0xE0` | the PPI's own priority byte, above `0x4C`'s `0x50` |

So the take record never moves even with the mask that landed. The `0x4C` hand-off is not gated by the PPI or
by priority bytes any more; it is gated by whatever owns the epoch ahead of the SPIs, and that is the SGI bank
(ids `0`/`1`/`2`). The `0x402` word rides along, unchanged from take2/take3: not a valid GIC id, flagged
suspect, not modelled.

## The branch table has no matching row, and that is the finding

Read against `sep.md` sec.2:

- **Row 1 (SEPARATED) is FALSE.** `E_ACT` bit12 CLEAR and the ring's newest slot is not `0x4C`.
- **Rows 3, 4, 5, 6, 7 are EXCLUDED** by the E block (`E_EN0` bit29 CLEAR, `E_P4C` byte0 `0x00`, `E_EN2` =
  `0x5000`, `E_CTLR` RWP 0, `E_SNT` present).
- **Row 8 (NO-SAMPLE) is excluded** (`E_SNT` = `0x50AA7E49`).
- **Row 2 (SGI-TRANSIENT) matches substantively but not literally.** Its precondition wants `E_HPP` == `0x4C`,
  and here `E_HPP` reads `0x3FF`; its conclusion, that the residual is the SGI bank and `0x4C` is the top
  *enabled* source, is what the boot shows, witnessed by `F_HPP` = `0x4C` at the later epoch.

The one line the table has no row for: **`E_HPP` = `0x3FF` while `0x4C` is pending AND enabled.** `E_ISP` bit12
SET and `E_EN2` bit12 SET at the E epoch, yet HPPIR named nothing, then Site F read `F_HPP` = `0x4C`. Two
readings fit the capture and it does not separate them: (a) a transient, nothing signalled at that exact
instant and `0x4C` named itself later; (b) a CPU-interface/Distributor **group gate** - `E_CTLR` = `0x1`, so
EnableGrp0 only and EnableGrp1 = 0, and if SPI `0x4C` is a Group-1 interrupt it is pending+enabled yet not
signalled. No `GICD_IGROUPR` cell for the SPI bank was captured, so (b) is an inference, not a measurement.
It is flagged open; it does not invert the negative above.

## The SGI note: the take epoch is owned by transient IPIs, not by priority

Why doesn't the SGI bank yield to a promoted `0x4C`? Because an SGI isn't a competitor on the priority axis at
all - it sits higher in the same queue, and its own bank sits at the top priority byte. This subsection is the
model that makes the take4 negative legible; it is static analysis, read-only, no device access.

Ids `0..15` are SGIs (software-generated, per-CPU, banked); `16..31` are PPIs (`0x1D` = the banked TWD timer);
`32+` are SPIs (`0x40`, `0x45`, `0x4C`). An SGI is raised by one core writing `GICD_SGIR` (`0x40161F00`), so its
sender is a CPU, not a wire. In the take3/take4 ring (ids `0`, `1`, `2`) those are, by the kernel's own
`interrupts.txt` labels, **SGI 0 = CPU wakeup, SGI 1 = timer broadcast, SGI 2 = rescheduling** - the standard
cross-core IPI triad. Both cores send and receive each, ordinary SMP housekeeping in flight at the sampled
instant.

Firmware sites (re-disassembled for the note): the only SGIR writer in the image is file `0x820C0`, and its
cross-core raise is **always SGI 1** (TargetListFilter `0b00`, one named core). Its CPU-interface-init callers
write `set_prio(0,0)` at `0x8305C` and `set_prio(2,0)` at `0x83070`, so **the SGI bank sits at priority byte
`0x00`**, the maximum. The MPIDR reading `V2_MPIDR` = `0x80000000` is CPU 0, and `dmesg` labels it verbatim
(`CPU0 ... mpidr 80000000`), so the ring's takes landed on CPU 0 - the only core whose GICC `CTLR.Enable` the
vendor sets at bring-up (`0xC20A2`).

The model consequence: an SGI is not a *competitor* for `0x4C`, it is **higher in the same queue**. Under the
recorded rule (priority first, then the LOWEST id), a higher-priority SGI is selected ahead of `0x4C` regardless
of id, and at equal priority the lower SGI id still wins the tie. With the SGI bank at `0x00`, any pending SGI
out-ranks `0x4C` (priority `0x50`). **So SGIs are the highest-priority transient traffic in the take window and
they never block `0x4C` materially - they out-rank it, every time an IPI is in flight.** take4 is the boundary
probe of that statement: with the PPI and the `0x40`/`0x45` SPIs gone, `0x4C` becomes the top *enabled* SPI
(`F_HPP` = `0x4C`) while the ring still holds `0,1,2` and the epoch is still owned by the SGI bank. The SPI
priority axis is exhausted; the next lever is the **bank axis** (mask/quiesce SGI ids `0..6`, or re-rank
bank-0 bytes `0x40161400..0x4016140F`), not another SPI-priority write.

Bounds (the note's, carried verbatim): (a) `/proc/interrupts` + the 4-slot ring are instants, not a trace;
(b) the sender routine fixes id 1, ids `0`/`2`/`3` reuse the same `GICD_SGIR` primitive, caller-to-id mapping is
the standard `IPI_*` assignment; (c) `V2_MPIDR` stores the raw MPIDR, the helper `&3`-masks only for its own
compare; (d) `0x402` is not a valid id, a sampling-boundary value flagged suspect, not modelled; (e) no device
access, no write of `0x400392f0`, no host read of `0x4016010c`/`0x10161000`.

## Bounds (declared, not hidden)

1. **No `sep.md` sec.2 row matches verbatim.** The substantive row 2 pattern (SGI-transient residual) is what
the boot shows, but `E_HPP` = `0x3FF` fails row 2's literal precondition, and `E_HPP` = `0x3FF` while `0x4C`
is pending+enabled is an open residual (transient vs group gate) the capture cannot separate.
2. **One shot, four entries.** `V_CNT` = `V2_CNT` = `4`, and the four ids are not source-attributed beyond the
ring's write order; `0x402` is a single-boot anomaly, not a stable property.
3. **The `E_EN2` bit14 (`0x4E`) enable is unexplained** (no pad writes it), exactly as take3, consistent with
the late writer at file `0x7edd8` that registers both `0x4C` and `0x4E`; not load-bearing.
4. **`E_P40` = `0xF050F050` differs from take2's `P_40` = `0xF050F0F0`** (byte2 `0x50` vs `0xF0`), which says
the authored priority bytes are per-boot firmware state, so the mask evidence is `E_EN2`, not the byte.
5. **The interp is this block.** The run's own `interp.txt` landed the finish-evidence skeleton; the rows above
are sourced from `capture-cmd.txt`, `dmesg.txt`, `health.txt`, `pstore-delta.txt` and the verdict, each named.

## Verification

Adversarial verdict `build/register-dumps/diffs/20261006T1246Z-vrun17/verdict.txt`: **C1-C8 all CONFIRMED**, the
honest branch being an honest NEGATIVE with a named, partially-shifted cause. C1 the instrument ran and the E
pad sampled (`V_MAGIC` = `0xA4A4A4A4`, `V_CNT` = 4, `E_SNT` = `0x50AA7E49`, staged blob = take4 md5, stock md5
`0e530b976d5a20e87358671f1a577695`); C2 THE EMITTER FIX LANDED (`E_EN0` = `0x0000FFFF` bit29 CLEAR where take3
read `0x2000FFFF`); C3 the take did NOT move (`V2_ID` id 2, ring `0,1,2,2`, `E_ACT` bit12 CLEAR, `E_OU0` 8,
`E_ISP` bit12 SET); C4 the mask + promotion held (`E_P4C` byte0 `0x00`, `E_EN2` = `0x5000`, `E_CTLR` RWP 0);
C5 the later positive control `F_HPP` = `0x4C`; C6 the mandatory bound armed and tripped on BOTH lines
(`207 n=65 bound=64` glue `00000011`; `209 n=9 bound=8`) with `IRQ_LEFT_DISABLED reason=bound`, `rung=OBSERVE`,
`probe=0`; C7 the router healthy; C8 no crash. The instrument verifier (`vtool16`) re-derived the take4 blobs
(two regenerations byte-identical, `--check-emitted take4` PASS 8/8), and its live read-only probe found no
`.omo-pat` leftover, vendor modules loaded, stock md5 unchanged, 2 wiphys, 6 interfaces, calibration `[SUCC]` on
both bands.

Run facts: `TAKE4 RESULT: PASS`, `exp_rc=0`, `PACKED.txt` device == host blob md5 `000a26af...`; done marker
`init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded irq0=207 isr0=65`; window sanity `0xE59FF018`; the
bound trips `irq=207 n=65 bound=64` and `irq=209 n=9 bound=8`; supervisor reached `SUPERVISOR DONE
quiesced=0 rung=OBSERVE ... state=IDLE`; `health.txt` = `WIFI=1 PLAT=1 WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0
STAGED=0 LOADER=0 RECOVER=0`; `pstore-delta.txt` = NO new record. One named observation, not load-bearing: the
device rebooted once more between the capture boot and the verifier's probe (probe uptime 49 s vs capture boot
71.74 s); the router is healthy either way.

## The next threads

- **Move to the bank axis: quiesce the SGI bank, don't touch the SPI priority bytes again.** take4 exhausts the
  SPI-priority lever (`0x4C` promoted to `0x00`, PPI and `0x40`/`0x45` out of the set, `F_HPP` = `0x4C`) and the
  ring is still SGI-owned. Mask/quiesce SGI ids `0..6` (or re-rank bank-0 bytes `0x40161400..0x4016140F`) and
  re-ring, so the epoch is not owned by an IPI.
- **Separate the `E_HPP` = `0x3FF` residual.** Sample `GICD_IGROUPR` for the SPI bank at the same post, so
  "transient" (nothing signalled) vs "group gate" (`0x4C` Group-1 while `E_CTLR` enables Group 0 only) is
  measured, not inferred. This is the one line `sep.md` sec.2 has no row for.
- **Re-run the flip clean for the `0x402` id.** Keep only `0x4C -> 0x00` as the mutation and re-read `V2_ID`
  per entry, so the per-entry ids stop carrying the invalid `0x402` word.
- **Chase the `0x4E` late writer.** `E_EN2` bit14 is SET with no pad writing it; the file `0x7edd8` register
  path (RAM-table id source, unproven) is the standing suspect.

## Artifacts

Evidence `build/register-dumps/exp/20261006-124348/`; blob `build/tmp/fw-patched/take4.bin` md5
`000a26af4d7b12e84d4191ae51dbad6d`; retained take1/take2/take3 blobs `0c681650...`/`eeeb252f...`/`072de986...`;
runner `build/tmp/wifidrv1-art/run-take4.sh`; hook `build/tmp/wifidrv1-art/take4-capture.hook`; instrument
verdict `build/register-dumps/diffs/20261006T1243Z-vtool16/`; adversarial verdict
`build/register-dumps/diffs/20261006T1246Z-vrun17/`; specs `build/tmp/inta-spec/{sep.md,sginote.md,emitter.md}`;
ko `3f87f1e9fe5ed9666f27d1f784d34535` (v8 of `553342d`, `omo/phase22-hccaccept`). Hard rules held: the pad writes
no `0x400392f0`/`0x40039af0`, no pad reads the IAR except the firmware's own, no `0x10161000` access, staged as
`wifidrv1.ko`, cycle serial/detached with the bound armed, router left healthy.

---

# ADDENDUM 21 (2026-10-06): the bracket - THE INSTRUMENT IS BUILT AND VERIFIED, THE BOOT NEVER RAN: the take5 cycle died at the completion marker (zero `omo-drv1` output, no evidence dir), so the three-instant bracket (E5 ring / I5 post-EOI / F5 gate-fall) produced NO SAMPLE, and the honest label is a HARNESS/RUN FAILURE (brk3.md row 11, extended), not an arbitration result

The bracket is take5: `bracket.md` + `brk3.md` + `sgi3.md` designed it, `st_01a1115e` built and verified
it, and the boot was supposed to sample ONE state at three instants. It did not run. This addendum records
the instrument as built and verified, the boot as a failure, and the branch table as still open. The
take4 conclusion (ADDENDUM 20) stands unchanged, because nothing in this cycle measured the device.

Evidence: **none** - the cycle's evidence dir `build/register-dumps/exp/20261006-134012/` was never created.
What exists is the runner's log `build/tmp/wifidrv1-art/run-take5.log` (13:40:10Z -> 13:52:14Z, `TAKE5
RESULT: FAIL`, `exp_rc=1`), the instrument `build/tmp/fw-patched/take5.bin` md5
`a5143c84a10b8e9182be70ba48a634a3`, the reused v8 ko `wifidrv1.ko` md5 `3f87f1e9fe5ed9666f27d1f784d34535`,
and the two verifier verdicts, `build/register-dumps/diffs/20261006T1342Z-vtool17/verdict.txt` (instrument,
CONFIRMED) and `build/register-dumps/diffs/20261006T1353Z-vrun18/verdict.txt` (boot, NO-SAMPLE).

## Short version

The instrument is sound and the boot is a negative on data. `vtool17` CONFIRMED the take5 blob against its
pin (two independent regenerations byte-identical), the emitted-bytes check PASS 8/8 on take5 and still
REJECTS take3, every new pad read-only, and the whole take5-vs-take4 delta confined to the three pads, the
one new ISR site and the two retargeted tails (175 differing bytes, 0 outside the declared regions).
`vrun18` then found the cycle never reached it: the takeover boot stalled before `module_init`, the log
shows `!! FAIL [run] device did not return fresh or experiment never finished` at step `[4/7]`, no `omo-drv1`
line was printed, no evidence dir exists, and the capture-recovery gate correctly refused to touch any older
dir (`capture MISSING: no evidence dir at TS >= 20261006-134010`). So the three instants `E5_*`, `I5_*`,
`F5_*` do not exist, and no conclusion about `0x4C`'s servability, the SGI bank, the running priority or the
group bit can be drawn from this boot.

## The instrument (verified, `vtool17` CONFIRMED)

take5 rides take4 byte-for-byte - the synchronized flip, the v1/v2 canaries, the CPSR milestones, the giccpu
cells, Pad A(N) with the H2D ring, Pad B2 and its companion, Site F and `select.md`'s readbacks - and adds
only read-only samplers. The same words, at three instants of one boot, each with the page sentinel
`0x50AA7E49`:

| instant | epoch | site | cells (runtime, page 10) | reads |
| --- | --- | --- | --- | --- |
| `E5_*` | the ring (take4's own E epoch) | chained after the retained `selpost_e` pad, returns to `0x86f5e` | `0x150144..0x150150` | `GICC_RPR` `0x40160114`, `GICD_ISPENDR0` `0x40161200`, `GICD_IGROUPR2` `0x40161088` |
| `I5_*` | the ISR's post-EOI instant | a NEW site at file `0x82f58`, entered by `bl`, re-emitting the two replaced instructions (`ldr r3,[r4,#0x74]` + `mov r5,r0`) and branching to `0x82f5c` | `0x150154..0x15015C` | `GICC_RPR`, `GICD_ISPENDR0` |
| `F5_*` | the `0xcece` gate's fall-through (take4's F epoch) | chained after the retained Site F pad, returns to `0x86f82` | `0x150160..0x15016C` | `GICC_RPR`, `GICD_ISPENDR0`, `GICD_IGROUPR2` |

Every read is non-acknowledging; no pad carries a store to any device CA (`vtool17` C4), and no pad or the
blob references a forbidden CA (`0x400392f0`, `0x40039af0`, `0x10161000`, the ack IAR `0x4016010c`, the
aliased `0x40160120`, `GICD_SGIR` `0x40161f00`) (`vtool17` C5). The two deviations from the specs are
recorded in the blob's own note: the free padding left is 274 bytes in fragments of at most 76, so each
instant is one 52- or 58-byte pad reading the decisive words instead of `bracket.md`'s 27-word block; and
`brk3.md`'s early site at file `0x82732` was dropped because take4's boot read `M2_PSR = 0x00000000` (that
path returns before its guard pass), so the post-EOI instant inside the ISR replaces it.

## The boot (NO-SAMPLE, `vrun18`)

| fact | value |
| --- | --- |
| runner | `build/tmp/wifidrv1-art/run-take5.sh` -> `tools/exp.sh`, RUN_TS `20261006-134010` |
| window | 2026-10-06T13:40:10Z -> 13:52:14Z, `exp_rc=1` |
| staged | blob `a5143c84...` pinned == served, ko `3f87f1e9...` pinned == served, preflight `stock_md5=0e530b97...`, leftovers 0, freshness uptime 3260 s |
| step | the boot returned at uptime 32 s, then `!! FAIL [run] device did not return fresh or experiment never finished` at `[4/7]` |
| evidence dir | `build/register-dumps/exp/20261006-134012/` was never created (the harness's own `health.txt` write failed: `No such file or directory`) |
| marker | no `omo-drv1: init done` line; the module never loaded |
| capture | `capture MISSING: no evidence dir at TS >= 20261006-134010 (not touching any older dir)` |
| cells | `E5_*` / `I5_*` / `F5_*` do not exist; no bracket sample of any instant |
| pstore | unchanged, 3 records (`blk-0`, `blk-2`, `blk-3`); no new crash dump |
| router | healthy, `wiphy=2/2 iface=6/6 cal_succ=1 omo_off=0 staged=0 loader=0`; the capture hook left `/root/omo-take5-cap.txt` on the device and the post-run probe found `/root/omo-take5*: 0` |

This is `brk3.md` sec.4 row 11's shape (NO-SAMPLE: the pad did not run), extended: here not even the module
ran, so it is a harness/run fault. The bracket's own rule is explicit that a build or run fault must not be
read as an arbitration result, and that is the reading recorded here.

## The branch table stays untested

`brk3.md` sec.4's eleven rows all guard on a sentinel (`_SNT` == `0x50AA7E49`), and no instant produced one,
so every row is `-`/NO-SAMPLE: no numbered row closes, and the two rows that matter (`row 1 SEPARATED`, `row
2 THE GATE IS THE TIME-VARYING WINDOW`) are untested, not disproved. The predecessor's line stands verbatim:
in take4 the register that moves is `GICC_HPPIR` (E `0x3FF` -> F `0x4C` in one boot), while `ISPENDR2` bit12,
`ISACTIVER2` bit12, `out[0]` and the CPSR I bit are equal at E and F. The bracket was meant to catch that
mover in flight and did not get the chance.

## Bounds (declared, not hidden)

1. **No device sample at all.** Every claim about `0x4C`, the SGI window, `GICC_RPR` and the group bit is
   inherited from take4/take3 and untouched by this cycle.
2. **The instrument's verification is static.** `vtool17` re-derived and disassembled the blob; it ran no
   device cycle, so "verified" means deterministic, read-only and correctly wired, not "measured on device".
3. **The stall is unexplained.** The boot returned at uptime 32 s and stopped before `module_init`; the
   cause (loader, boot stage, or the staged blob) is named as the next diagnostic, not diagnosed here.
4. **The recovered capture is empty on purpose.** The runner refused to attribute any older evidence dir to
   this run; that is the setup fix from the earlier salvage working as designed.

## Verification and health

Adversarial verdict `build/register-dumps/diffs/20261006T1353Z-vrun18/verdict.txt` (**the take5 boot =
HARNESS/RUN FAILURE, NO-SAMPLE**); instrument verdict
`build/register-dumps/diffs/20261006T1342Z-vtool17/verdict.txt` (**CONFIRMED**, C1-C7, one recorded
deviation: a stale documentation block in the on-disk manifest, which cannot invert any load-bearing claim).
Router healthy after recovery (`WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0`), no new pstore
record, `stock_md5 0e530b976d5a20e87358671f1a577695` unchanged. Hard rules held: no write of CA
`0x400392f0`/`0x40039af0`; no read of `0x10161000`; no host read of the ack IAR `0x4016010c`; the ko staged
as `wifidrv1.ko`; cycle serial/detached with the bound armed; recover after.

## The next threads

- **Re-run the take5 cycle first.** A fresh `tools/exp.sh` serial/detached run with the bound armed, same
  pinned blob and ko; the three instants are the whole capture.
- **If the stall recurs, diagnose the boot/loader stage.** The log names the S99omo loader's `insmod` point
  as the place to look, before any instrument cell is read.
- **Then read `brk3.md` sec.4.** Whichever row the three sentinels satisfy is the answering row; the SGI
  window (`R_SGI_P`/`E_SGI_P`/`E2_SGI_P`) is what separates "window" from "arbitration".

## KO-THREADS

- **take5 (2026-10-06, the runner `build/tmp/wifidrv1-art/run-take5.sh`, hook
  `build/tmp/wifidrv1-art/take5-capture.hook`, verifier `build/tmp/wifidrv1-art/take5-verify.py`) - the ko
  commit is UNKNOWN, and the blob rides the patch tool's uncommitted take5 code.** The take5 variant was
  built by `tools/patch_fw_scratch.py` working-tree state (HEAD `2e1390a`) into
  `build/tmp/fw-patched/take5.bin` md5 `a5143c84a10b8e9182be70ba48a634a3` (928920 B, size-preserving), and
  the takeover served the reused v8 ko `wifidrv1.ko` md5 `3f87f1e9fe5ed9666f27d1f784d34535` (the v8 of
  `553342d`) with no CI run and no submodule commit, so the ko identity is a reuse, not a verified take5
  build. The ADJUDICATION of the run is the marker `TAKE5`, quoted in the ADDENDUM 21 block above; the
  verdicts are `build/register-dumps/diffs/20261006T1342Z-vtool17/verdict.txt` (instrument, CONFIRMED) and
  `build/register-dumps/diffs/20261006T1353Z-vrun18/verdict.txt` (boot, NO-SAMPLE). The runner's
  capture-recovery gate selected no evidence dir, so there is no `interp.txt` for this cycle either.

## The end of the bracket

The bracket never sampled: the take5 boot died at the completion marker, so the three instants are absent
and the bank-axis question is untouched. The instrument is on the shelf, verified and ready; the next cycle
is the same run, with the loader stall diagnosed first if it recurs. Nothing that follows this block in the
phase ledger closes a numbered row.

### 21a. THE BRACKET RAN (appended 2026-10-06 by the orchestrator): the re-run's samples name the gate - IT IS THE RUNNING PRIORITY

The sanctioned single re-run (`run-take5.sh`, `20261006-135935`, exit 0, no pstore delta, D6 files generated)
produced the three-instant capture this addendum lacked. Measured:

| cell | E5 (early) | I5 (post-EOI) | F5 (gate-fall) |
| --- | --- | --- | --- |
| `GICC_RPR` (running priority) | **`0x00000000`** | `0x000000FF` | `0x000000FF` |
| `ISPENDR` w0 (the PPI/SGI bank) | `0x00000000` | `0x00000000` | **`0x20000000`** (bit 29 = id `0x1D`) |
| `GICD_IGROUPR` bank 2 | `0x0` | - | `0x0` |

The same capture's standing cells: `E_HPP=0x3FF` / `X_HPP=0x3FF` / `N_HPP=0x3FF` while `F_HPP=0x0000004C`
(inside ONE boot), `E_ISP=0x1021` (0x4C pending throughout), `E_ACT=0`, `E_OU0=8`, `E_CTLR=0x01`,
`E_P4C=0xF050F000` (the promotion held), `E_EN0=0x0000FFFF` (the 0x1D disable held), the v2 ring still SGI-owned
(`0,1,2,2`), `M2_PSR=0` (the release guard never reached, as in every take-era boot).

**THE GATE, NAMED.** The CPU interface's Running Priority Register reads **`0x00` at the early instant and
`0xFF` (idle) at both later ones**, and `HPPIR` tracks it exactly: while a **priority-0 interrupt is ACTIVE**,
every pending source with priority numerically above 0 is blocked from signalling - `HPPIR` legitimately reads
`0x3FF` (nothing servable) even with `0x4C` pending+enabled+promoted; once the active interrupt retires
(`RPR=0xFF`), the very same registers name `0x4C` (`F_HPP=0x0000004C`). So the time-varying gate is **the
running priority of a priority-0 active interrupt**, and the arbitration model of ADDENDUM 18/20 stands behind it.

**The active priority-0 interrupt is most plausibly ONE OF THE FIRMWARE'S OWN SGIs** (the ring serves `0,1,2`;
SGIs commonly run at priority 0), held active because **the device's interrupt-release path can silently skip
its EOI** (ADDENDUM 17's counter-based release; `M2_PSR=0` in every sample: the guard's sample point never even
ran). That single stuck-active SGI would mask the whole board's signalling above priority 0 - a complete,
self-consistent closure of the arc: the wire works, `0x4C` is selectable, and it is held off only while the
device's own release path leaves a priority-0 interrupt running.

**The next experiment (one boot, read-only):** sample `RPR` + the IAR returns + `ISACTIVER` continuously (a
fast bracket) and catch WHICH id the priority-0 active interrupt is (`ISACTIVER` bank bits at the sticky window),
then prove the EOI theory by forcing the release path (the counter/guard state) and watching `RPR` go idle.

Health after the re-run: stock md5 exact, 0 leftovers, 6 interfaces, 3 pstore records; no panic in either
attempt.

# ADDENDUM 22 (2026-10-06): the stuck-active - the gate 21a named is a PRIORITY-0 SOURCE HELD ACTIVE (RPR `0x00` at the ring, `0xFF` after the ISR's EOI), retired by the EOI and, per the release path's own sample, never retired when the ISR doesn't run / the RPR comparator ranks above the SGI bank and the group enable, both REFUTED as the stopper / the take6 fast sampler that would NAME the source stalled at the same step the take5 bracket did

21a closed with the gate named: a priority-0 interrupt sits ACTIVE (`GICC_RPR` `0x00000000` at the ring), and
the promoted `0x4C` cannot signal while it holds, then `RPR` idles (`0xFFFFFFFF`-class read `0xFF`) the instant
the ISR's own EOI retires it. This addendum asks the two questions 21a left open - WHICH id is the active
source, and what the state means without the ISR path - and settles both from the artifacts already on disk.

## Short version

The stuck-active is a **priority-0 source held ACTIVE**, read directly: `E5_RPR` (the ring) = `0x00000000`,
`I5_RPR` (the ISR's post-EOI) = `0x000000FF`, `F5_RPR` (the `0xcece` gate's fall-through) = `0x000000FF`, all in
ONE boot. Nothing else in the CPU interface moves; `HPPIR` tracks `RPR` exactly (`E_HPP`/`X_HPP`/`N_HPP` =
`0x3FF` while `0x4C` is pending+enabled+promoted, then `F_HPP` = `0x4C` the moment `RPR` idles). So the take is
not attempted-and-lost, it is **not attempted while the priority-0 epoch holds**. Two candidate blockers fold
under it: the **SGI bank** (ids 0 and 2 are the only sources the firmware prices `0x00`) and the **group
enable** (`0x4C`'s group bit reads `0` at both the ring and the fall-through, so EnableGrp0 covers it). Both are
REFUTED as the stopper; the comparator is `RPR`. And the release that would end the epoch is counter-gated:
its own sample point never ran (`M2_PSR` = `0x00000000`), so a priority-0 source can be left active across the
forward attempt. The instrument that would name the source (`take6`, the 16-instant `pad_stk_fast` reading
`GICC_RPR` x16 + the word-0 `ISACTIVER` bank + `HPPIR` with the sticky byte) is built and verified but its boot
**stalled at the same `[4/7]` completion marker the take5 bracket stalled at** - NO-SAMPLE, so the source stays
named only by the candidate set, never by a fresh cell.

## The stuck-active, read directly from 21a (`exp/20261006-135935`)

| cell | read | tag |
| --- | --- | --- |
| `E5_RPR` | `0x00000000` | at the ring a priority-0 source is ACTIVE |
| `I5_RPR` | `0x000000FF` | inside the ISR, right after the EOI at file `0x82f52` |
| `F5_RPR` | `0x000000FF` | the `0xcece` gate's fall-through |
| `E5_SGIP` / `I5_SGIP` / `F5_SGIP` | `0x00000000` / `0x00000000` / `0x20000000` | ISPENDR word 0; bit 29 (id `0x1D`) pends only at the fall-through, never at the ring |
| `E5_GRP2` / `F5_GRP2` | `0x00000000` / `0x00000000` | IGROUPR word 2: id `0x4C`'s group bit = 0 (group 0) |
| `E_HPP` / `X_HPP` / `N_HPP` / `F_HPP` | `0x3FF` / `0x3FF` / `0x3FF` / `0x4C` | HPPIR: `0x4C` unsignalable while `RPR` = `0x00`, named the instant `RPR` idles, ONE boot |
| `E_ACT` / `X_ACT` / `N_ACT` / `F_ACT` | `0` / `0` / `0` / `0` | ISACTIVER word 2 = the SPI-class active bank; ZERO at every vein, so no SPI is the holder |
| `E_ISP` / `X_ISP` / `N_ISP` / `F_ISP` | `0x00001021` | ISPENDR word 2: bit 12 (`0x4C`) pending at every instant |
| `E_EN0` | `0x0000FFFF` | ISENABLER word 0: the banked TWD PPI `0x1D` stays MASKED (the ADDENDUM-20 fix held) |
| `M2_PSR` | `0x00000000` | the release guard's `0x8270A` CPSR sample never ran |

Each value is quoted from `build/register-dumps/exp/20261006-135935/capture-cmd.txt`; the same values stand in
`build/tmp/inta-spec/stuck.md` sec.0, `eoir.md` sec.0 and `stk3.md` sec.0. The `_SNT` sentinels (`E5_SNT` /
`I5_SNT` / `F5_SNT`) all read `0x50AA7E49`, so all three instants are real samples, not the `M2_PSR = 0` class of
written non-event. The one cloud on the boot is the IAR ring's `V2_ID = 0x00000402`, which is not a valid GIC id
(ADDENDUM 18/20's suspect word); it does not touch the `RPR` differential, which is read straight from the CPU
interface.

## What is ACTIVE, and what it means

A `GICC_RPR` of `0x00` means a source priced `0x00` is in service. `E_ACT` is `0` at every sampled instant, so
no SPI-class source is active; the holder is a **word-0 (SGI/PPI bank) source**. The firmware's own CPU-interface
init prices two ids at `0x00`: `set_prio(0,0x0)` at file `0x8305C` and `set_prio(2,0x0)` at `0x83070` (the
vendor's `IPRIORITYR[id] = prio<<4` form). Id `0x1D`, the banked TWD PPI, is authored at byte `0xE0` and is
masked anyway (`E_EN0` bit29 CLEAR), and `set_prio` gives it `0xE0` too. So the candidate holder set is `{SGI 0,
SGI 2}`, and the IAR ring's standouts (`0`, `1`, `2`) corroborate a word-0 source without ever naming it cleanly
(`0x402` = the SGI-2-from-CPU-1 read the GICv2 cell decodes).

What it MEANS is a strict-`>` gate. `0x4C` was promoted to priority `0x00` (`E_P4C` byte0 `0x00`) and its group
bit reads `0` at the ring (`E5_GRP2`) and the fall-through (`F5_GRP2`), with `E_CTLR` = `0x1` (EnableGrp0 SET) -
so nothing below `RPR` in the ranking is turned away on GROUP grounds. With `RPR` = `0x00` the promoted `0x4C`
fails the strict `>` test (equal priority does not preempt an active one), so at the `0x00` instants the take is
not attempted; the instant `RPR` clears to `0xFF`, `F_HPP` reads `0x4C` - the same boot. The `0x4C` promoted to
life only after the incumbent's `RPR` dropped is the SGI bank cleared of one candidate, plus the group gate
cleared twice by the read.

## The clear: the EOI retires it, and the release is counter-gated

`E5_RPR` = `0x00` -> `I5_RPR` = `0xFF` across the ISR's own `str r7,[r3]` at file `0x82F52` (the image holds
exactly one `0x40160110` EOIR reference and exactly one `0x4016010c` IAR reference, re-derived this session). So
the epoch is NOT stuck forever; it retires the moment the ISR's EOI runs. The catch is the RELEASE path's
conditions: `0x826E0` owns the epoch's exit (five stores in the release's body; the file `0x82730` caller
re-enters after its `msr cpsr_c,r1`), and ADDENDUM 21's note stands re-derived here: the release's early-exit
guard tests the per-CPU pending count and the CPSR interrupt-mask bit, so it RETURNS BEFORE its body unless the
conditions hold. `M2_PSR` = `0x00000000` means that sample point never executed in this boot, so the release
returned early or was never called; the half that would restore the mask/counter state is silently skipped, and
the priority-0 source stays active. That is the mechanism that keeps the epoch alive across the ring's forward
attempt - the reason the `0x4C` signal never arrives.

## Bounds (declared, not hidden)

1. `CA 0x40160114` = `GICC_RPR` is a GICv2 CPU-interface-convention reading, NOT an image literal: the firmware
   writes no literal for `0x40160114` or `0x40160118` (0 references in the whole image). The model rests on the
   behavioral differential (`E5` `0x00` -> `I5`/`F5` `0xFF` after the EOI, and `HPPIR` tracking it), not on the
   register's name.
2. The CANDIDATE SET `{SGI 0, SGI 2}` is a derivation from the firmware's own `set_prio` call sites, not a
   sample: no cell in take1..take5 reads the word-0 `ISACTIVER` bank (`GICD_ISACTIVER0` `0x40161300`), and the
   firmware never writes it either. The id stays a candidate until an instrument reads the bank.
3. The EOIR-retires-it result is proven by the `E5/I5/F5` differential; the release-counter story (the `0x82730`
   early-exit on `M2_PSR` = `0`) is NAMED by re-derived disassembly, not measured (no cell reads `PERCPU+*`).
4. `0x4C`'s own no-attempt at the `0x00` instants is read (`HPPIR` = `0x3FF` while `E_ISP` bit12 SET and `E_EN2`
   bit12 SET), but a transient that opened and closed between two coarse instants is not excluded by 21a's
   three cells alone; only a fast sampler closes that door.
5. The `0x402` IAR word is this boot's suspect entry; the CPU-interface reads are unaffected.

## Verification

- Values quoted verbatim from `build/register-dumps/exp/20261006-135935/capture-cmd.txt` (`E5_RPR`/`I5_RPR`/
  `F5_RPR`, `E5_SGIP`/`I5_SGIP`/`F5_SGIP`, `E5_GRP2`/`F5_GRP2`, `E_HPP`/`X_HPP`/`N_HPP`/`F_HPP`, `E_ACT`/`X_ACT`/
  `N_ACT`/`F_ACT`, `E_ISP`, `E_EN0`, `E_P4C`, `E_CTLR`, `M2_PSR`, `V2_ID`, and the three `_SNT` sentinels).
- The three-instant bracket is the take5 re-run; `run-take5.log` shows `TAKE5 RESULT: PASS`, `exp_rc=0`; health
  `WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0`.
- The ISR / release / `set_prio` sites are re-derived with capstone 5.0.7 (THUMB) from `build/tmp/FIRMWARE.bin`
  (md5 `0e530b976d5a20e87358671f1a577695`) in `build/tmp/inta-spec/stuck.md` sec.1-3, `eoir.md` sec.1 and
  `stk3.md` sec.0-1.
- The instrument and its boot state: `build/register-dumps/diffs/20261006T1432Z-vtool18/verdict.txt` (take6 read-only
  sampler CONFIRMED, C1-C8) and `build/tmp/wifidrv1-art/run-take6.log` (stalled at `[4/7]`).
- Hard rules held: no write of CA `0x400392f0` / `0x40039af0`; no read of `0x10161000`; the host never reads the
  ack IAR `0x4016010c` (the ISR's own read at file `0x82F04` is quoted as data); `GICD_SGIR` `0x40161F00` never
  read; the take6 EOIR force (`GICC_EOIR` `0x40160110`) is gated to `take6f` and was not built.

## The next threads

1. **The fast sampler that NAMES the source** (`take6`): `pad_stk_fast` (172 B, chained after the retained E block
   at the ring) reads `GICC_RPR` `0x40160114` sixteen times back-to-back into `STK_0..STK_15`, ANDs them into
   `STK_STICKY` (`0x00000000` iff an `RPR` = `0x00` sample existed), and reads the word-0 active bank
   `GICD_ISACTIVER0` `0x40161300` into `STK_ACT` (with `HPPIR` into `STK_HPP`); page-10 cells
   `0x150170..0x1501BC`. It ships read-only (no device write at all), verified `CONFIRMED` by `vtool18`. Its boot
   STALLED: `run-take6.log` sits at `[4/7] wait for the completion marker` since 14:33Z, no evidence dir exists,
   no `omo-drv1` line - the SAME run failure the take5 bracket met. The honest label is NO-SAMPLE, so the source
   stays a candidate. Next: diagnose the loader's `insmod` point (the take5 stall's named open residual) before
   another reading is waited on.
2. **The EOIR force** (`take6f`, gated, NOT built): one 32-bit `GICC_EOIR` `0x40160110` write of an
   `--eoir-id`, then a re-read of `RPR` into `STK_RPR1`. It is the arc's capping proof (row 4 of `stk3.md`: `RPR`
   drops `0x00 -> 0xFF`, then `HPPIR` `0x3FF -> 0x4C`, then the take), and it is gated: do not build it until
   `STK_ACT` has named the holder, because a spurious EOIR unwinds a running-priority stack the firmware did not
   author (`eoir.md` sec.2-3).
3. **The release guard** (`M2_PSR` = `0`): sample the release path's own CPSR / per-CPU counter at file `0x82730`
   so the early-exit is MEASURED, then force the epoch to end by retiring the source rather than by another
   priority write.
4. **The SGI axis, if the fast sampler reads `STK_STICKY == 0`**: the residual reverts to the bank (row 2 of
   `stk3.md`), and the lever is the per-CPU SGI/PPI bank at the same instant - a `.ko` GICD write under
   `omo/phase22-hccaccept`, not another SPI-priority byte (the ADDENDUM-20 axis is exhausted).

## Artifacts

- `build/tmp/inta-spec/stk3.md` (the fast sampler + sticky byte + gated EOIR force), `eoir.md` (the `GICC_EOIR`
  CA/value and the safety rank), `stuck.md` (the SAMPLES the epoch still needs: which id, and whether it clears)
  - the three design docs the take6 instrument (and this addendum's mechanism reading) stands on.
- `build/tmp/inta-spec/{bracket.md,brk3.md,sgi3.md}` - the take5 bracket the re-run closed as ADDENDUM 21a.
- Instrument verdict: `build/register-dumps/diffs/20261006T1432Z-vtool18/verdict.txt` (`CONFIRMED`; take6 read-only,
  20 stores all page-10 cells, loads `RPR` x16 / `ISACTIVER0` x1 / `HPPIR` x1).
- Boot attempt: `build/tmp/wifidrv1-art/run-take6.log` (no evidence dir; stalled at `[4/7]`).
- Evidence: `build/register-dumps/exp/20261006-135935/capture-cmd.txt` (the 21a three-instant bracket the
  mechanism reading quotes).

### 22a. THE RE-RUN RAN (appended 2026-10-06 by the orchestrator): ROW 6 - NO-SAMPLE (the module loaded; the instrument's pads never executed)

The sanctioned re-run of `run-take6.sh` (after the device was caught in takeover config, restored via
`tools/restore-now.sh`, and rebooted twice - the first "reboots" had silently no-opped, caught by the uptime
trail and since fixed with a boot-id gate) produced `build/register-dumps/exp/20261006-144831/`:

- The CYCLE completed (exit 0; the D6 files generated; NO new pstore record; the cleanup recovered the
  capture; the device recovered to 6 interfaces on a fresh boot).
- **The run reads ROW 6 of this addendum's own branch table: `E_SNT == 0x0` = NO-SAMPLE.** Every alias-cell
  reads zero - including the sentinels that had held `0x50aa7e49` in every take-era boot (`V_MAGIC=0` too:
  the firmware's pads never ran at all) - while `WIN=0xE59FF018` proves the hook's read path itself is sound,
  and the module-log shows `wifidrv1(O+)` + both vendor modules LOADED (the module did run; only the patched
  FIRMWARE's execution is missing).
- So the take6 pad set (`pad_stk_fast`, 172 B chained after the retained E block) **did not execute the way
  the take1-take5 pads did** - a PATCH-LAYOUT / execution stall class, distinct from the take4-era patches
  that ran cleanly (take4's separation boot populated every cell). The take6 EOIR force was never built
  (the record: it is gated to `take6f`).
- The stuck-id naming therefore remains OPEN, and the next step is OFFLINE-FIRST: diff the take6 pad's
  placement/length against the working take4 layout, rebuild through the standing `--check-emitted` gate,
  and only then spend another cycle (optionally with the `take6f` EOIR-force variant built in).
- Device state after: healthy - 6 interfaces, stock md5 exact, 0 `.omo-off`, 3 pstore records unchanged,
  all diagnostic leftovers cleared (the device-side `omo-take6-cap.txt` duplicated this dir's capture).

# ADDENDUM 23 (2026-10-06): the take6f capstone - THE VENDOR STACK TOOK THE ENDPOINT, SO THE RANKED EOIR FORCE WAS NEVER SPENT: the blob is built and verified (10/10 emitted, 66 B), the runner now FAILs closed, and the boot is NO-SAMPLE BY CONSTRUCTION (the third pad-less boot of the take class)

`layoutdiff.md` sec.4 / `eoir.md` rank 1 specified one more instrument: `take6f`, take6's read-only sampler
plus the ranked EOIR force (`GICC_EOIR 0x40160110 <= <the stuck id>`), which would prove the arc's capping
BY TRANSITION in a single boot (`RPR 0x00 -> 0xFF`, then `HPPIR 0x3FF -> 0x4C`, then the take). The
instrument is built, gated and capstone-verified. The boot then never reached the chip - the vendor Wi-Fi
stack won the endpoint race - so the capping proof is NOT obtained and no cell is readable. This addendum
records the instrument, the fix that made the harness FAIL closed, and the honest NO-SAMPLE label.

## Short version

`take6f` (`build/tmp/fw-patched/take6f.bin` md5 `2c1ae79f892e922d0df0583f87fb1a2c`, 928 920 B, the reused
v8 ko `3f87f1e9fe5ed9666f27d1f784d34535`) rode take6 byte-for-byte and added exactly TWO things: the read-only
fast sampler `pad_stk_fast` (`GICC_RPR` x16 + the ANDS sticky + the word-0 active bank `GICD_ISACTIVER0`
`0x40161300` + `HPPIR` + the page sentinel), and the ranked 66-B EOIR force `pad_stk_eoir` (one 32-bit
`GICC_EOIR` `0x40160110 <= 0x2`, then a post-force `RPR` and `HPPIR` re-read into `STK_RPR1`/`STK_HPP1`).
`vtool19` CONFIRMED the build (selftest, 10/10 emitted ops, two byte-identical regenerations, the emitted
force ABSENT from take6, and the runner's gates). The boot was a hardware attach failure: `hardware attach
failed (no endpoint bound)`, `regs=absent irq0=0 isr0=0`, the 207 line stayed the vendor's `hisi_pci_intx`.
The vendor glue took both endpoints (`[PCIEL]request pcie intx irq 209/207 succ`) and uploaded its own image
at 13.16 s, so our `.omo-pat` blob was never read into the chip, NO pad (the fast sampler, the E block, or
the EOIR force) executed, and every cell reads `0x00000000`. `vrun20` labels it **NO-SAMPLE BY CONSTRUCTION**
(attempt 1 row 6, extended): a VERIFIED NEGATIVE, not a hypothesis failure, and the third consecutive
pad-less boot of the take class. The one real gain of exp/20261006-152035 is that the harness now FAILs
closed (the gate is the verdict, not the read) while keeping the all-zero cells, and the router is left
healthy.

## The instrument (built and verified, `vtool19` CONFIRMED)

take6f = take6's retained read-only frame (take4's instrument minus the four pads ADDENDUM 21a made
redundant: `tk_e`, `tk_i`, `tk_f`, `selpost`, every take4 cell at its take4 address, the ten sites of
`layoutdiff.md` sec.1(4)) plus two pads:

| pad | file | length | what it does |
| --- | --- | --- | --- |
| `pad_stk_fast` | `0xc8348` | 172 B | 16 x (`ldr RPR` `0x40160114` + `str` + ANDS fold), `STK_STICKY` `0x1501b0`, `STK_ACT` = `GICD_ISACTIVER0` `0x40161300` -> `0x1501b4`, `STK_HPP` `0x1501b8`, sentinel `0x1501bc` |
| `pad_stk_eoir` | `0xcb8e4` | 66 B | `GICC_EOIR` `0x40160110 <= 0x2` (the ONE force write), `dsb sy`, then re-read `RPR` -> `STK_RPR1` `0x1501c0` and `HPPIR` -> `STK_HPP1` `0x1501c4`, `b.w 0x86f5e` |

`vtool19` C4/C5 capstone-confirmed both pads byte-for-byte against `layoutdiff.md` sec.4's spec (54 B
as-built + the 12-B HPPIR deposit = 66 B). C1-C3: `--selftest` PASS with every frozen pin reproduced (take6
`18d8e2ff...`, take6f `2c1ae79f...`), `--check-emitted take6f.bin` = 10/10 PASS and `take6.bin` = 8/8 PASS
(the read-only take6 carries NO EOIR op and NO HPPIR deposit), two regenerations byte-identical to the
shipped blob, and the builder refuses a bare take6f (rc 2, no `--eoir-id`). C6: the runner's gate is real -
`run-take6f.sh` exits 1 with no `EOIR_ID` or a mismatched one (no device contact), pins
`BLOB_MD5=2c1ae79f...`, re-runs the emitted gate on the staged blob, and its `EXP_DONE_CMD` now requires the
three execution lines (`BAR0 base=0x40000000`, the `FIRMWARE.bin.omo-pat size=928920 bytes` upload, and
`request_irq(207, IRQF_SHARED) rc=0`) and forbids `regs=absent` / `INI_DRV:D]ini_cfg_init`. That last change
is the fix the take6 layout study named.

## The boot (NO-SAMPLE BY CONSTRUCTION, `vrun20`)

| fact | value |
| --- | --- |
| runner | `EOIR_ID=0x2 bash build/tmp/wifidrv1-art/run-take6f.sh` over `tools/exp.sh`, RUN_TS `20261006-152033` |
| staged | blob `2c1ae79f...` pinned == served (10/10 emitted), ko `3f87f1e9...` pinned == served |
| attach | `hardware attach failed (no endpoint bound) - continuing without it`; `init done ... regs=absent irq0=0 isr0=0` |
| endpoint | the vendor stack held both: `207/209 ... hisi_pci_intx`; the vendor uploaded its own image at 13.16 s |
| cells | every cell `0x00000000`; every pad sentinel (`STK_SNT`, `E_SNT`, `X_SNT`, `N_SNT`, `B_P3`, `B_P4`, `F_SNT`, `C_SNT`) reads `0x0`, none `0x50aa7e49` |
| window | `WIN 0x4080B000`-class sanity `0xE59FF018` (the ACP window is alive) and `S2+4 0x40103EB8 = 0x00104427` (vendor data), so the zeros mean "the pad did not run", not "the window is dead" |
| gate | `INSTRUMENT_GATE=NOT_HELD_NO_SAMPLE_BY_CONSTRUCTION`; `TAKE6F RESULT: FAIL`, `exp_rc=1` |
| pstore | unchanged, 3 records; no crash |

`vrun20`'s tally: C1 (staged + 10/10 + the run FAILed closed) CONFIRMED, C2 (the pads did NOT run) NO, C3
(the 16 samples + sticky + the namer) UNREADABLE, C4 (the force's effect) UNREADABLE, C5 (the mandatory bound
armed, INERT: no endpoint, no IRQ, `[qsv] no message window - supervisor skipped`) CONFIRMED, C6 (no new
pstore) CONFIRMED, C7 (the gated live probe held and the router is healthy) CONFIRMED. So the take6f
hypothesis stays UNTESTED and nothing about `0x4C`, `RPR`, or the stuck id can be read either way.

## Why it is NO-SAMPLE and not a layout defect

`layoutdiff.md` settles it offline: there is no take6-vs-take4 structural defect. The fast pad's length,
offset, window, chaining and entry point are all in the class the take4/take5 pads used (the same allocator
and stock image produced pads that ran in those boots), and take5's `tk_e` was chained off the SAME hop into
the SAME page-10 alias-only cells and ran (`E5_RPR 0x40808144 = 0x0`, `E5_SNT = 0x50AA7E49`). The failure is
UPSTREAM of the layout: **the patched blob was never uploaded.** The take6f `lsmod`/`module-log` show the
vendor stack resident (`hi5622v100_wifi`/`hi5622v100_plat`) where take4/take5's do not, the vendor's PCIe glue
claimed both endpoints at ~13.0-13.4 s and downloaded its own image, and our module then refused rather than
fight (`omo_pdev == NULL -> -ENODEV`). With no BAR0 there is no read of the `.omo-pat`, so not one pad can
execute. The one open item that layout study named (WHY the vendor stack was resident: this boot came up on the
other `rootfs` slot, `rootfsa` vs take4/take5's `rootfsb`, so the harness's `.omo-off` hide of
`hi5622v100_{wifi,plat}.ko` may not have applied) is answered here by gating on the OBSERVED state instead: the
harness's new `EXP_DONE_CMD` requires the instrument's own execution lines, so a vendor-stack boot can no
longer PASS.

## What stands, and the one line that moved

The take5 arc (ADDENDUM 21a) and the stuck-active reading (ADDENDUM 22) are UNCHANGED - nothing in this cycle
measured the device. What moved is the HARNESS: the take5 bracket's bare PASS and the take6 re-run's bare PASS
are gone, replaced by an execution gate that FAILs closed, and this boot is the first to prove it (the
capture hook read every cell, found all zeros, and the gate decided FAIL). `STK_ACT`, the word-0 active bank
that would NAME the stuck id, is still unread. Rank 1 of `eoir.md` sec.3 stands and is now better guarded:
FORCE, LAST, only once the id is measured - and `take6f` cannot be launched without an explicit, matching
`--eoir-id`, so a wrong-id force cannot be spent by accident.

## Bounds (declared, not hidden)

1. **No device sample.** Every claim about `0x4C`, `RPR`, the SGI window and the group bit is inherited from
take4/take5 and untouched by this cycle.
2. **The instrument's verification is static.** `vtool19` re-derived and disassembled the blob; it ran no
device cycle, so "verified" means deterministic, read-only and correctly wired, not "measured on device".
3. **The `.omo-pat` was staged but never consumed.** Its md5 exists as a file (`2c1ae79f...`) and is
byte-identical to the host blob, but no BAR0 means no read into the chip; the zeros are the cells'
power-on/reset content, not a `GICC_RPR` of `0x00`.
4. **One cosmetic hook defect, non-inverting.** The staged hook's `M1_PSR` header carries backticks around
`cpsie i` inside a double-quoted echo, so the shell printed `cpsie: not found` into `capture-cmd.txt` (the
same artifact the take6 re-run shows). It changes no cell and no verdict; worth a one-line fix.

## Verification and health

Adversarial verdict `build/register-dumps/diffs/20261006T1523Z-vrun20/verdict.txt` (**NO-SAMPLE BY
CONSTRUCTION**, C1/C5/C6/C7 CONFIRMED, C3/C4 UNREADABLE); instrument verdict
`build/register-dumps/diffs/20261006T1519Z-vtool19/verdict.txt` (**CONFIRMED**, C1-C6; three non-load-bearing
deviations D1-D3, none inverting a load-bearing claim: D1 a docs md5/size line for the 54-B as-built blob
superseded by the shipped 66-B one, D2 the generator `tools/patch_fw_scratch.py` uncommitted on HEAD
`c43c625`, D3 an adjacent `finish-evidence.sh --check-verdict` no-arg glob defect, recorded not fixed).
Router healthy after the cycle: `WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`, stock md5
`0e530b976d5a20e87358671f1a577695` unchanged, no new pstore. Hard rules held: no write of CA
`0x400392f0`/`0x40039af0`; no read of `0x10161000`; no host read of the ack IAR `0x4016010c` (or the aliased
`0x40160120`); `GICD_SGIR 0x40161f00` never read; the ko staged ALWAYS as `wifidrv1.ko`; cycle
serial/detached with the bound armed (`qbound=64`/`qbound209=8`); recover after.

## The next threads

- **Get the vendor stack out of the boot's way, then re-run the SAME take6f.** The gate's precondition is
  that the vendor stack does NOT take the endpoint; the reboot-cycle gate now enforces an execution marker, so
  the run fails closed instead of PASSing on a loaded-but-unbound module. Diagnose the `.omo-off` hide against
  the actual `rootfs` slot first.
- **Only then read `STK_ACT`.** If the fast sampler runs, `STK_STICKY == 0` with a word-0 bit set names the
  priority-0 holder (`{SGI 0, SGI 2}` is the candidate set); that name is what `take6f`'s `--eoir-id` must be.
- **Spend the EOIR force LAST.** It is the arc's capping proof BY TRANSITION (`RPR 0x00 -> 0xFF`, then
  `HPPIR 0x3FF -> 0x4C`), gated to `take6f` and gated on a MEASURED id; it is never built on a guess.

## Artifacts

- Evidence `build/register-dumps/exp/20261006-152035/` (`capture-cmd.txt` with
  `INSTRUMENT_GATE=NOT_HELD_NO_SAMPLE_BY_CONSTRUCTION`, `dmesg.txt`, `lsmod.txt`, `health.txt`,
  `pstore-delta.txt`); log `build/tmp/wifidrv1-art/run-take6f.log`.
- Blob `build/tmp/fw-patched/take6f.bin` md5 `2c1ae79f892e922d0df0583f87fb1a2c` (66-B EOIR pad, 10/10
  emitted); reuse ko `wifidrv1.ko` md5 `3f87f1e9fe5ed9666f27d1f784d34535` (v8 of `553342d`, no ko commit, no CI).
- Runner `build/tmp/wifidrv1-art/run-take6f.sh`; hook `build/tmp/wifidrv1-art/take6f-capture.hook`; verifiers
  `build/register-dumps/diffs/20261006T1519Z-vtool19/verdict.txt` (instrument, CONFIRMED) and
  `build/register-dumps/diffs/20261006T1523Z-vrun20/verdict.txt` (boot, NO-SAMPLE).
- Specs `build/tmp/inta-spec/{stk3.md,eoir.md,stuck.md,layoutdiff.md}` (`layoutdiff.md` sec.3 the fail-closed
  fix, sec.4 the 66-B pad).

# ADDENDUM 24 (2026-10-06): the take6f capstone, RE-RUN - THE INSTRUMENT RAN: the takeover bound the endpoint, every active pad deposited its sentinel, the sampler NAMED the stuck id (SGI 2), and the EOIR force DROPPED the running priority (`RPR 0x00 -> 0xFF`): the capping proof is HALF-LANDED and the cycle boot came up on the WRONG IMAGE SLOT, so the gate refused the cells (GATE-REFUSED, not FAIL-by-hypothesis, not NO-SAMPLE)

ADDENDUM 23 built `take6f` and verified it, then lost the boot to the vendor stack - NO-SAMPLE BY
CONSTRUCTION. This addendum is the SECOND take6f boot, under the `race.md` sec.5 mitigation (hide
BOTH vendor load paths), and it is the first take6f boot in which the instrument is observed to
execute. The claim LANDED: our module bound `59e7:0005`, BAR0 mapped, the `.omo-pat` was read into the
chip, and every active pad ran. But the cycle boot came up on `mtd13 "rootfsa"` (the stock 2.4.15
image) instead of `mtd14 "rootfsb"`, so the runner's standing image gate refused the run: `TAKE6F
RESULT: FAIL`, `INSTRUMENT_GATE=NOT_HELD_WRONG_IMAGE_SLOT`. The cells are therefore a GATE-REFUSED
READ - the first complete take6/take6f reading of the arc, recorded as evidence, not as a certified
result.

## Short version

The re-run (`EOIR_ID=0x2 bash build/tmp/wifidrv1-art/run-take6f.sh`, evidence
`build/register-dumps/exp/20261006-154734/`, RUN_TS `20261006-154730`) staged the same artifacts as
ADDENDUM 23 - blob `2c1ae79f892e922d0df0583f87fb1a2c` (10/10 emitted) and the v8 ko `3f87f1e9...`, both
pins matched - but this time the vendor Wi-Fi pair never loaded. `lsmod` shows `wifidrv1 73728 0` and
NO `hi5622v100_{plat,wifi}` entry at all: `HIDE_MOVED=2`, the literal path hidden so the vendor boot init
could no longer `insmod` it on EITHER slot. Our S99 loader therefore found the endpoint free
(`request_irq(207, IRQF_SHARED) rc=0`, `BAR0 base=0x40000000`, the firmware uploaded its 928 920 B
image), and the whole instrument chain ran.

The readings, all alias-only (page-10, `0x6b8000 + runtime`): the fast sampler's 16 `GICC_RPR` samples
`STK_0..STK_15` read `0x00000000` (16/16), the sticky byte `STK_STICKY = 0x00000000`, and `STK_ACT`
(`GICD_ISACTIVER0` `0x40161300` word 0) = `0x00000004` - bit 2 set. With `STK_STICKY == 0`, that set bit
NAMES the priority-0 holder: **SGI 2 (id `0x2`)**. It is a member of `eoir.md` sec.0's candidate set
(a), the firmware's own SGIs, and the same boot's v2 IAR ring agrees: `V2_RING0/1/2/3 = 0,1,2,0x402` with
`V2_ID = 0x00000402`, so the firmware's ISR EOI'd ids `0,1,2,0x402` and `0x2` is exactly the one still
ACTIVE. `STK_HPP = 0x000003FF` at `RPR = 0x00` is the `eoir.md` sec.0 model: `0x4C` is pending
(`E_ISP = 0x00001021`, bit12 set) but NOT signalable while a priority-0 source holds the running
priority.

The ranked EOIR force then fired with the id the SAME boot had just named (`0x2`). It retired a
genuinely ACTIVE interrupt (no `eoir.md` sec.2 harm mode 3) and the `GICC_EOIR` write DROPPED the running
priority: `STK_RPR1 = 0x000000FF` immediately after `dsb sy`, against `STK_15 = 0x00000000`. That is the
capping proof's FIRST half, BY TRANSITION, in the same boot. The SECOND half did not land inside the pad:
`STK_HPP1 = 0x000003FF`, not `0x4C`. But the second half is present elsewhere in the SAME boot - Site F
reads `F_HPP = 0x0000004C` (with `F_ISP = 0x00001021`, `F_ACT = 0x00000000`), i.e. once the priority is
idle `0x4C` is top-pending, the ADDENDUM 21a reading reproduced in one boot. The force is recorded as
HALF, not FORCE MISSED: `STK_RPR1 != STK_15` shows it acted on the named active id, and the design's own
bound (the sampler reads `HPPIR`/`ACT` once, at the window's end) covers the missing instant.

## The take6f branch table, row by row

The instrument's own decision table, read against this boot. Row 3 is the arc-closing row, and it fires.

| row | condition | this boot | verdict |
| --- | --- | --- | --- |
| 1 | `STK_STICKY == 0x00` AND `STK_ACT` names a word-0 bit | `0x00000000` / `0x00000004` (bit2) | **THE STUCK ID IS NAMED: SGI 2 (`0x2`)** |
| 2 | `STK_STICKY == 0xFF` (no `RPR=0x00` sample) | sticky is `0x00`, not `0xFF` | not this row |
| 3 | `STK_SNT != 0x50aa7e49` -> NO-SAMPLE | `STK_SNT = 0x50AA7E49` | the pad RAN, the block is readable |
| 4 | `STK_15 == 0x00` AND `STK_RPR1 == 0xFF` AND `STK_HPP1 == 0x4C` | `0x00` / `0xFF` / `0x3FF` | HALF-LANDED (RPR half, not HPPIR half) |

Row 1 plus row 3 is the naming result: the sampler's own cells are complete, so the source the arc could
never name reads out cleanly as SGI 2. Row 4 is the force result: the RPR drop landed, the HPPIR flip is
recorded one site over (Site F). The table's fourth-row bound is stated honestly in the pad's own header:
the HPPIR/ACT re-read happens once, at the window's end, so a flip that settles after that instant is not
seen by the pad - and here it settled (Site F sees it) rather than not happening.

## The retained take4 frame, and the quiesce witness

The retained frame still reads as take4/take5 left it. `E_EN0 = 0x0000FFFF` (bit 29 CLEAR: the id-`0x1D`
PPI mask took, the ADDENDUM 20 fix), `E_EN2 = 0x00005000` (bit 12 SET: the `0x4C` enable landed; bits 0/5
CLEAR: the `0x40`/`0x45` competitors masked), `E_P4C = 0xF050F000` (byte0 `0x00`: the priority-0 store
held), `E_CTLR = 0x00000001`. `E_ACT = 0` and `F_ACT = 0` at every sampled instant: ISACTIVER2 word 2 is
zero, so the IAR was never read for `0x4C` (`V2_ID = 0x402`, not `0x4C`) - consistent with the `RPR=0x00`
block rows 1 and 4 measure.

The mandatory bound did its job with no live-injection residue: `IRQ_DISABLED_BOUND irq=207 n=65
bound=64`, `irq=209 n=9 bound=8`, and the supervisor reported `SUPERVISOR DONE quiesced=0 rung=OBSERVE
state=IDLE`. The literal-path guard was armed BEFORE the hide (`GUARD_ALIVE`) and restored unconditionally
(`LITERAL_RESTORED_WIFI=1 ... POST_DONE`). No new pstore record. The router is healthy: `WIPHY=2 IFACE=6
CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`, stock md5 `0e530b976d5a20e87358671f1a577695` unchanged,
and the boot_id moved (the reboot gate: pre-run `7afb325f-...`, post `e8e60346-...`).

## Why the cells are GATE-REFUSED and not a result

The claim LANDED, but the boot attached `mtd13 "rootfsa"` (the stock 2.4.15 image) - `show boot_reg: 11`,
`ubi0: attached mtd13 (name "rootfsa", size 23 MiB)`. The runner's image gate (`race.md` sec.5(2) +
`layoutdiff.md` sec.3a) requires the CYCLE boot's own image, `mtd14 "rootfsb"`, so it refused:
`INSTRUMENT_GATE=NOT_HELD_WRONG_IMAGE_SLOT`. The capture hook still read every cell, so the readings above
are real and complete - they are simply not certified by the gate as a take6f result. The reason the box
was on slot A is a sibling lane's crash-reboot (the pciskel probe at 15:40:25Z); restoring slot B is a
separate gated action, not part of this lane. The take6f hypothesis therefore moves from UNTESTED
(ADDENDUM 23) to MEASURED-BUT-UNCERTIFIED: the naming row and the half-force both fired, and a slot-B
re-run of the SAME take6f is what would certify them.

## Bounds (declared, not hidden)

1. **One boot, gate-refused.** Every reading is from a single cycle whose image gate refused it; the
   naming and the half-force are strong but uncertified. A slot-B re-run is the certification.
2. **The HPPIR half is one site over.** Row 4 asks for `STK_HPP1 == 0x4C`; the boot shows `0x3FF` there and
   `0x4C` at Site F the same boot. That is a site/instant mismatch, not a contradiction, but it is a
   mismatch and is recorded as one.
3. **Alias-only cells.** Every page-10 cell is quoted from the ACP alias (`0x6b8000 + runtime`); the
   BAR0-direct view of these pages reads zero, the standing boundary phenomenon.
4. **The id is named, not yet proven to be THE one.** `STK_ACT` names a word-0 bit at the sampled instant;
   the sampler reads the ACTIVE bank once, at the window's end, so the name is the holder at that instant.
5. **One cosmetic hook defect, inherited.** The staged hook's `M1_PSR` header carries backticks around
   `cpsie i` inside a double-quoted echo, so the shell printed `cpsie: not found` into `capture-cmd.txt`.
   It changes no cell and no verdict (the same defect ADDENDUM 23 records).
6. **A pre-existing module artifact, not this run's doing.** `dmesg.txt` carries repeated `RTNL:
   assertion failed` warnings from `omo_add_virtual_intf` inside `omo_wifidrv1_init`; the SAME warnings are
   in every take-era boot (`exp/20261006-{124348,135935,144831,152035}`), so they are v8-ko init behaviour.
   The module still completed (`init done wiphy=omo-drv1 ifname=omowl1 hw=1 regs=decoded irq0=207 isr0=65`).

## Verification and health

Adversarial verdict `build/register-dumps/diffs/20261006T1557Z-vrun21/verdict.txt` (the take6f re-run:
GATE-REFUSED on the wrong image slot; the seven claims carry their readings); the mitigated runner was
verified separately by `build/register-dumps/diffs/20261006T1545Z-vtool20/verdict.txt` (CONFIRMED: the
`race.md` sec.5 mitigation, the blob pins unchanged, the standing emitted gate still 10/10). Router healthy
after the cycle: `WIPHY=2 IFACE=6 CAL_SUCC=1 OMO_OFF=0 STAGED=0 LOADER=0 RECOVER=0`, stock md5
`0e530b976d5a20e87358671f1a577695` unchanged, no new pstore. Hard rules held: no write of CA
`0x400392f0`/`0x40039af0`; no read of `0x10161000`; no host read of the ack IAR `0x4016010c` (or the aliased
`0x40160120`); the instrument's only device write is the single `GICC_EOIR` `0x2` store (register state, a
reboot clears it); the ko was staged ALWAYS as `wifidrv1.ko`; `qbound=64`/`qbound209=8` armed and hit; no
reboot was taken outside the cycle.

## The next threads

- **Restore slot B, then re-run the SAME take6f.** The gate's only complaint is the image slot; the box
  currently sits on stock `rootfsa` because of a sibling lane's crash-reboot. Restoring `rootfsb`
  (`race.md` sec.6's documented lever) is a separate gated action; the instrument itself needs no change.
- **Then read `STK_ACT` again.** With a slot-B boot, the same fast sampler should re-name the holder (SGI 2
  is the prediction this boot makes); the naming is what `take6f`'s `--eoir-id` must match, and the builder
  already refuses a bare/mismatched id.
- **The force stays LAST, gated on a measured id.** This boot used `0x2` because the same boot's sampler
  named it; the rule from `eoir.md` sec.3 rank 1 is unchanged - never build the force on a guess.

## Artifacts

- Evidence `build/register-dumps/exp/20261006-154734/` (`capture-cmd.txt` with
  `INSTRUMENT_GATE=NOT_HELD_WRONG_IMAGE_SLOT`, the E/STK/F/V2 cell blocks, `dmesg.txt`, `lsmod.txt`,
  `interrupts.txt`, `health.txt`, `run-take6f.log`, `run-take6f.full.log`).
- Blob `build/tmp/fw-patched/take6f.bin` md5 `2c1ae79f892e922d0df0583f87fb1a2c` (66-B EOIR pad, 10/10
  emitted); reused ko `wifidrv1.ko` md5 `3f87f1e9fe5ed9666f27d1f784d34535` (v8 of `553342d`).
- Runner `build/tmp/wifidrv1-art/run-take6f.sh` (md5 `ed0b590b25c091baf67a3f721d3f9615`); hook
  `build/tmp/wifidrv1-art/take6f-capture.hook`; verifiers
  `build/register-dumps/diffs/20261006T1557Z-vrun21/verdict.txt` (boot, GATE-REFUSED) and
  `build/register-dumps/diffs/20261006T1545Z-vtool20/verdict.txt` (instrument/runner, CONFIRMED).
- Specs `build/tmp/inta-spec/{race.md,stk3.md,eoir.md,stuck.md,layoutdiff.md}` (`race.md` sec.5 the
  mitigation, sec.6 the slot lever).

# ADDENDUM 25 (2026-10-07): the official take6f capstone - THE SLOT GATE LANDS (the runner's preflight now REFUSES a wrong-slot boot before anything is armed), THE REBOOT LEVER REFUTES ITSELF (a boot-id-gated reboot did NOT move the slot), AND THE NAMING AND THE HALF-FORCE STAND AS ADDENDUM 24 LEFT THEM (MEASURED-BUT-UNCERTIFIED, pending a slot-B boot)

ADDENDUM 24 measured the instrument and refused to certify it: the cycle boot came up on `mtd13
"rootfsa"` instead of `mtd14 "rootfsb"`, so the runner's tail gate read the cells and FAILed the run
(`INSTRUMENT_GATE=NOT_HELD_WRONG_IMAGE_SLOT`). ADDENDUM 25 is the official capstone for that arc, and
it does two things. It lands the SLOT GATE, the pre-cycle check that makes the same refusal happen
before a cycle is spent instead of after. And it records the honest state of the two things the arc
wanted: the naming and the force were measured on a wrong-slot boot, so they stand as they are, strong
and still uncertified, until a `rootfsb` boot re-runs the SAME `take6f`.

Nothing in this addendum writes a cell that ADDENDUM 24 did not already have. The new material is the
gate itself, its live refusal, the mechanism that says why a reboot cannot clear it, and the writer
that can.

## Short version

The slot gate is BUILT AND VERIFIED. It lives in `build/tmp/wifidrv1-art/run-take6f.sh`
(md5 `41272389eb340815f0397230de1183f5`, 37 869 B) as a block inside `EXP_PREFLIGHT_CMD`, the string
`tools/exp.sh` runs at `[0/7]` before the watchdog is armed and before anything is staged. It reads
`/sys/class/ubi/ubi0/mtd_num` plus that `mtdN`'s name in `/proc/mtd`, cross-checks this boot's own
`ubi0: attached mtd1[34] (name "rootfs[ab]"` dmesg line, and carries four refuse arms: unidentifiable,
ident-conflict, wrong-image-slot, and the pre-existing literal-path arm that follows it unchanged.
`vtool21` CONFIRMED all seven claims (`build/register-dumps/diffs/20261007T0044Z-vtool21/verdict.txt`),
including the 7-arm decision table and the fact that a refusal costs ZERO device contact (a shimmed
`ssh` shows `ssh_calls=0` on every host-side fail-closed branch).

The live refusal is exact and it is on the record. The runner's own preflight string was run
read-only against the box and printed:

```
preflight uptime=279s
preflight leftovers=0
preflight attach_failed_residue=0
preflight stock_md5=0e530b976d5a20e87358671f1a577695
preflight booted_slot=mtd13 name=rootfsa dmesg_attach=13 rom=/dev/ubiblock0_0 (mtd13=rootfsa stock 2.4.15 | mtd14=rootfsb custom 2.5.24)
PREFLIGHT_FAIL_wrong_image_slot: booted_slot=mtd13:rootfsa want=mtd14:rootfsb (the CUSTOM image)
PREFLIGHT_RC=1
```

The reboot lever was tried and REFUTED. The task's remedy was one boot-id-gated reboot, and it was
taken: pre `e8e60346-d999-4739-a8d8-7730f0f83c96` -> post `f47bbc77-89d6-47a9-b299-dd38a2c4ca52`,
uptime reset, box HEALTHY. The post-reboot read was `mtd13 "rootfsa"` again. A reboot does not move
the slot, and the reason is now measured rather than guessed: `fw_printenv` shows
`bootcmd=mtd read kernel${bootflag} ${loadaddr};bootfip ${loadaddr}` with `bootflag=a`, so u-boot
loads `kernela` -> slot A on every boot while that variable says `a`. The in-boot
`/sys/devices/platform/sysenv/boot_reg` (reads `10`) is a red herring: the sysenv driver OVERWRITES
it at ~16 s on BOTH slots, which is why `race.md`'s "0x10 -> rootfsb" map was a coincidence.

The naming and the force stay as ADDENDUM 24 left them - MEASURED BUT NOT CERTIFIED. `STK_STICKY =
0x00000000` with `STK_ACT` (`GICD_ISACTIVER0` `0x40161300` word 0) `= 0x00000004` names the priority-0
holder as **SGI 2 (id `0x2`)**, and the ranked EOIR force dropped the running priority
(`STK_RPR1 = 0x000000FF` against `STK_15 = 0x00000000`) while its second half stayed one site over
(`STK_HPP1 = 0x3FF`, `F_HPP = 0x0000004C`). Both are from a boot the gate REFUSED, so neither becomes
a CERTIFIED take6f result here. The certification is a slot-B boot that re-runs the SAME instrument;
the seal is `--eoir-id 0x2`, and the builder already refuses a bare or mismatched id, so the force
cannot be spent on a guess.

## The gate, and why it is a preflight and not a note

The endpoint's binder is decided about 25 s before our module exists. On the stock slot the vendor's
boot init insmods its pair by LITERAL path (`wifi_init.sh:79-80` ->
`/lib/hisilicon/ko/hi5622v100_{plat,wifi}.ko`) and binds `59e7:0005` at 13.4 s, while `tools/exp.sh`'s
hide renames only the `/lib/modules/<kver>/` copy the CUSTOM image resolves. A cycle booted on
`mtd13` is therefore a NO-SAMPLE BY CONSTRUCTION: a spent cycle and a FAIL, never a take result. The
gate's job is to make that a zero-mutation refusal instead.

The identification is `cat /sys/class/ubi/ubi0/mtd_num` plus that mtdN's name in `/proc/mtd`, so
`13` -> `rootfsa` (stock 2.4.15) and `14` -> `rootfsb` (custom 2.5.24). `vtool21` re-derived that
choice independently and rejected the two alternatives: `/proc/cmdline` carries no `root=` token
(init finds the rootfs through UBI), and `boot_reg` is volatile inside a boot. On a refusal the cost
is one aborted invocation and no mutation: the runner's literal-path bound is cancelled by its own
restore block after `exp.sh` returns, and the live probe after this session's runs confirms
`PROBE_LITERAL_OMO_OFF=0`, `PROBE_MODULES_OMO_OFF=0`, `PROBE_GUARD=0`.

## The refusal, and what it correctly does NOT say

The runner's `EXP_CAPTURE_CMD` tail keeps ADDENDUM 24's decisive check on the cycle boot's OWN dmesg
line (`ubi0: attached mtd14 (name "rootfsb"`). ADDENDUM 24's cells exist precisely because that tail
gate reads the cells first and refuses second: it reported `NOT_HELD_WRONG_IMAGE_SLOT` while the
capture hook still pulled every pad deposit. That is the right order for evidence, and it is also why
the label has to be read carefully. `INSTRUMENT_GATE=NOT_HELD_WRONG_IMAGE_SLOT` is a statement about
the IMAGE SLOT, not about whether the instrument ran; the instrument's own execution is established
separately, by `request_irq(207, IRQF_SHARED) rc=0`, `BAR0 base=0x40000000`, the 928 920 B `.omo-pat`
upload, and the seven active-pad sentinels all reading `0x50AA7E49`.

## The mechanism: why a reboot cannot clear the refusal

A reboot re-enters the same slot, so it cannot satisfy a gate that asks for the other one. The real
lever is the boot selector. u-boot's `bootcmd` reads `${bootflag}` and `fw_printenv` reports
`bootflag=a`; moving the box to slot B means writing `bootflag=b` (the B env block into mtd3+mtd4, or
the equivalent, per `build/custom/FLASH-PLAN.md` "Slot switch recipe"), rebooting, confirming the
attach line names `mtd14`, and only then spending a cycle. That is a flash/env mutation. It is not
named in this task, the task's own hard rule is to ABORT on `mtd13/rootfsa`, and `race.md` sec.6 and
ADDENDUM 24 both defer it to its own gate, so it was raised as a decision, not taken.

## Bounds (declared, not hidden)

1. **No new cells.** Every register reading quoted here is ADDENDUM 24's, from the GATE-REFUSED boot
   `build/register-dumps/exp/20261006-154734/`. This addendum adds no measurement, only the gate, the
   refusal, and the mechanism.
2. **The refusal is a pre-run reading on the STOCK slot.** The exact string was captured with the box
   on `mtd13`; the `mtd14` PASS arm was exercised against the device's own `/proc/mtd` fixture
   (`vtool21`, 7/7 arms), not on a live slot-B boot.
3. **The reboot is evidence of a negative.** One boot-id-gated reboot did not move the slot. It is not
   proof that no reboot ever could, only that the selector, not the reboot, is the lever.
4. **`bootflag=a` is the u-boot environment, read via `fw_printenv`.** The write path (mtd3/mtd4) was
   not exercised here and carries its own risk, namely the `HAZARDS.md` sec.4 rule of full dumps before
   any write and one slot always stock.
5. **The naming and the force stay uncertified.** Restating ADDENDUM 24's central honesty point: the
   SGI-2 name and the `RPR 0x00 -> 0xFF` drop were read on a boot the gate refused, so the capping
   proof BY TRANSITION at the DESIGNED pad site is still not established.

## Verification and health

Runner verdict `build/register-dumps/diffs/20261007T0044Z-vtool21/verdict.txt` (slot gate CONFIRMED,
seven claims with two recorded deviations: the staging block's `FATAL`s end only the tee'd subshell,
and the refusal is before the first CYCLE mutation rather than before the runner's own hide, which its
restore undoes). Boot verdict `build/register-dumps/diffs/20261007T0050Z-vrun22/verdict.txt` (the
ADDENDUM 24 take6f re-run: C1-C8 CONFIRMED, the honest branch T6-1 with the force HALF).
`build/register-dumps/diffs/20261007T0051Z-vrunB4/verdict.txt` is a minted skeleton with no findings
filed, so nothing is claimed from it. Health after this session: `WIPHY=2 IFACE=6 CAL2G=1 CAL5G=1`,
`PAT=0 OMO_OFF=0 WIFIDRV1=0`, stock md5 `0e530b976d5a20e87358671f1a577695` unchanged, no new pstore,
boot_id `f47bbc77-...` on `mtd13`. Hard rules held: no write of CA `0x400392f0`/`0x40039af0`; no read
of `0x10161000`; no host read of the ack IAR `0x4016010c`; NO device cycle was run at all this session,
so the bound was not spent and the single permitted device mutation was the boot-id-gated reboot.

## The next threads

- **Flash the selector, then reboot.** `bootflag=b` (B env block to mtd3+mtd4) plus the reboot, then
  confirm `ubi0: attached mtd14 (name "rootfsb"`. That is the only step between here and a certifying
  boot; it needs its own gate and its own dumps.
- **Then re-run the SAME `take6f` and seal it with the measured id.** `--eoir-id 0x2` is the
  prediction this arc makes. If the sampler names `0x2` again, the seal is honest and the force can be
  spent on a measured id rather than a guess.
- **Read only once the slot is right.** The gate is now what stops a wrong-slot run before it costs
  anything. Leave it in place; it is the instrument that keeps a stock-slot boot from being read as a
  result a second time.

## Artifacts

- Slot-refusal record `build/register-dumps/exp/20261007-004820-slotrefusal/`
  (`slotgate-refusal.txt` the exact live preflight block, `reboot-attempt.log` the boot-id-gated
  reboot with `PRE`/`POST` boot_ids and the post-reboot slot read,
  `bootflag-mechanism-and-health.txt` the `fw_printenv` / dmesg / `/proc/mtd` evidence and health).
- Runner `build/tmp/wifidrv1-art/run-take6f.sh` md5 `41272389eb340815f0397230de1183f5` (the slot gate,
  pinned in `build/tmp/inta-spec/slotgate.md`); spec `build/tmp/inta-spec/{slotgate.md,race.md}`
  (`slotgate.md` sec.1-3 the identification and the refuse arms, `race.md` sec.5-6 the mitigation and
  the slot-B lever rationale).
- Blob `build/tmp/fw-patched/take6f.bin` md5 `2c1ae79f892e922d0df0583f87fb1a2c` (66-B EOIR pad, 10/10
  emitted); ko `wifidrv1.ko` md5 `3f87f1e9fe5ed9666f27d1f784d34535` (v8, unstaged this session).
- Verdicts `build/register-dumps/diffs/20261007T0044Z-vtool21/verdict.txt` (slot gate, CONFIRMED),
  `build/register-dumps/diffs/20261007T0050Z-vrun22/verdict.txt` (boot, GATE-REFUSED with T6-1), and
  `build/register-dumps/diffs/20261006T1557Z-vrun21/verdict.txt` (ADDENDUM 24's boot, GATE-REFUSED).

### 25a. THE OFFICIAL CAPSTONE RAN (appended 2026-10-07 by the orchestrator): SGI 2 NAMED ON THE CORRECT SLOT; THE FORCE DROPS RPR; THE RESIDUAL MOVES TO THE BANK/GROUP

The slot was restored via the record's own verified recipe (`build/custom/env-bootflag-b.bin` into BOTH env
copies + `boot_reg 0x21`; the fw_setenv path is a trap - the bootloader's selector is the sysenv register,
and the env CRC is `crc32-little-endian(4)|flags(1)|data(131067)`), and the capstone then passed every gate
(`GATE_MTD_NUM=14`, wiphy 2 / iface 6 / cal 1-1 / zero leftovers) and ran to completion
(`build/register-dumps/exp/20261007-010906/`, exit 0, no pstore delta, every device-side leftover 0):

- **EVERY PAD RAN**: the sentinels all read `0x50aa7e49` (`E_SNT/N_SNT/F_SNT/C_SNT/STK_SNT` + the retained
  pairs) - the take6f layout fix + the slot discipline made the instrument execute end to end.
- **THE STUCK ID IS NAMED: SGI 2.** `STK_STICKY=0x00000000` (some ring sample read `GICC_RPR=0x00`) and
  **`STK_ACT=0x00000004`** - word 0 of `GICD_ISACTIVER0`, the bank NO cell had ever read, bit 2 set =
  **SGI 2 is the priority-0 incumbent pinning the running priority** (`this addendum's T6-1`, verbatim).
- **THE FORCE DROPS THE RUNNING PRIORITY**: `STK_RPR1=0x000000FF` after the single `GICC_EOIR 0x40160110
  <= 0x2` write - the priority-0 incumbent retired on our write.
- **THE RESIDUAL, IN THE INSTRUMENT'S OWN WORDS**: `STK_HPP1=0x000003FF` (not `0x4C`) with `STK_RPR1=0xFF`
  -> **"the bank/group, not RPR, was blocking"** - a third gate above the two the arc already peeled
  (arbitration at ADDENDUM 18; the SGI's RPR mask at 21a; now the bank/group selection), consistent with the
  F-site positive control in this very boot (`F_HPP=0x0000004C`; `E_HPP=0x3FF`).
- Also in-boot: `E_P4C=0xF050F000` (the promotion held), `E_EN2=0x5000` (the enable), `E_ISP=0x1021`
  (0x4C pending throughout), `C_GRP0=0x000000FF` (canonical RPR, idle at its sample).

**ARM B, SAME WINDOW**: the width-fixed `luofu-pcie` probe read SEVEN live DBI registers - including
`dbi+0x004 PCI_COMMAND = 0x00100007` matching the vendor's written value - then took the external abort at
**RC1's `dbi+0x082`** (fault address `0xc800a082` = RC1's map + 0x82): the readw fix covered rc0's entry;
**RC1's parallel +0x082 site still reads 32-bit** (the fix is one more width change, per-RC). The crash
record is pulled (`build/tmp/wifidrv1-art/pciskel-crash-pstore.txt`); the device recovered onto slot B
unassisted (the slot recipe holds through a panic).

**CORRECTION (appended 2026-10-07, per `build/tmp/inta-spec/rcfix.md`):** the "per-RC" reading above was
WRONG. `0xc800a082` = **RC0's** map (`c800a000`) + 0x82; the bug was never per-RC - **the readw at a
2-byte-aligned (but not dword-aligned) offset still aborted the bus.** The rule (now a standing hardware
note): **every DBI/CFG field must be read from its containing 4-byte-aligned dword** (readl + shift/mask).
The corrected skeleton (`c35af1a`) PASSED the third smoke with **both RCs' full inventories, the `+0x082`
Link Status returning, a clean unload and the device up** (`build/tmp/inta-spec/pciskel-smoke3.md`).

# ADDENDUM 26 (2026-10-07): the bank/group gate - `RPR` is drop-able but not the comparator: `STK_HPP1` stays `0x3FF` with `STK_RPR1` at `0xFF` while `F_HPP` reads `0x4C` in the SAME boot, so a third gate sits above the SPIs (rank 1 the force's id-mismatched EOIR left the priority-0 epoch unretired or rank 2 an `0x4C` group stamp against `EnableGrp1 = 0` or rank 3 the SGI/PPI active bank), the one boot that decides it is take7's post-force read set, and the group-enable write is deliberately deferred

ADDENDUM 25a closed the naming and the RPR drop and left one layer standing: with `RPR` idle the promoted `0x4C`
still did not forward. This block is the pointer and the design record for the arm-A answer to "which gate holds
`0x4C` now". Two specs were written (design only: no device cycle, no build, no register write):
`build/tmp/inta-spec/bankgate.md` (the ranked-gate analysis and the take7 design) and
`build/tmp/inta-spec/bg2.md` (the post-force bank/group read set and its branch table). The `§` and EN DASH used
below are QUOTED from those specs and from the capstone's `interp.txt`; this addendum's own prose keeps the
repo's ASCII convention.

## Short version

The gate is NOT the `RPR` register, and that is the new negative. The capstone's force dropped the running
priority (`STK_RPR1 = 0x000000FF`) yet `HPPIR` did not move (`STK_HPP1 = 0x000003FF`), and the SAME boot's later
Site F reads `F_HPP = 0x0000004C`. One boot, two instants, opposite answers, and no word the image authors
changes between them. So the comparator sits BELOW `RPR`.

`GICC_HPPIR` is a THREE-WAY signature, quoted from ARM IHI0048 (sec. 2 of both specs): `0x3FF` means nothing is
pending OR the top pending's group is disabled in the CPU interface OR the top pending is Group 0 read
Non-secure; `0x3FE` is the Group-1 Secure read; and a Non-secure read never returns a Group-0 INTID. So
`STK_HPP1 = 0x3FF` names exactly three layers, and the specs rank them:

- **Rank 1 - the rank/retirement state.** The force wrote a bare `0x2` to `GICC_EOIR` `0x40160110` while the
  last valid IAR value was `0x402` (`V2_ID`), so it ran inside the spec's UNPREDICTABLE clause (the value must
  match the last IAR read; for an SGI, bits `[12:10]` name the source PE). The observed split (RPR moves,
  HPPIR does not) is the signature of a priority drop whose deactivation did not complete.
- **Rank 2 - the group layer.** With `GICC_CTLR = 0x1` and `GICD_CTLR = 0x1` (`EnableGrp1 = 0` both levels), a
  Group-1 stamp on `0x4C` blocks it and yields exactly `0x3FF`. The only group read in the capstone is `G_GRP0`
  (`0x40161080` w0 = `0x0`), the SGI bank, not `0x4C`'s; every `IGROUPR` w2 read on record (`0x40161088` = `0x0`,
  take5 and gicking) puts `0x4C` in Group 0, but none was taken at the post-force instant.
- **Rank 3 - the bank.** `STK_ACT = 0x00000004` names SGI 2 as the active incumbent, and the SGI/PPI active
  bank sits above the SPIs (ADDENDUM 21a/22's SGI note). No cell reads `GICD_IGROUPR2`, `GICD_ISACTIVER2`,
  `GICD_ISENABLER2` or `GICC_CTLR` at `RPR = 0xFF`, so the whole layer is unmeasured there.

Ranks 4 and 5 (target routing `ITARGETSR`, and the security/DS view) are cheap companions, not causes: the
image never writes `IGROUPR`, so the group stamp is inherited from reset, and a single-state view degenerates
the axis to the one `EnableGrp0` bit that is already set.

## The bank/group gate, and why `F_HPP = 0x4C` decides the shape

`F_HPP = 0x4C` in the same boot rules out any STATIC config gate the firmware never rewrites. The image's whole
GIC-window literal census (both specs, capstone 5.0.7 THUMB on `build/tmp/FIRMWARE.bin` md5
`0e530b976d5a20e87358671f1a577695`) finds NO literal for `0x40161080` or `0x40161088`: the image's CPU-interface
init is one write (`0x8302c..0x8305a`: `ICACTIVER0 <= -1`, the `IPRIORITYR[0..7]` init, `GICC_PMR <= 0xFF`,
`GICC_BPR <= 3`, then `set_prio` for ids `0,1,2,0x1D`), and `GICC_CTLR <= 1` is its only control write. So the
group stamp is a reset/bootloader inheritance, and a static stamp cannot flip inside a boot against `F_HPP`.
That points the gate at rank 1 (the retirement state) or at a layer read only while `RPR` was still the live
gate, which is what `E5_GRP2 = F5_GRP2 = 0` was: a cell taken at `E5_RPR = 0x00`.

## The deciding boot: take7's post-force read set (design only)

One post-force pad settles it, `pad_stk_eoir` extended by the non-acknowledging reads R1..R14 (`bankgate.md` sec.
4): `TG_RPR` `0x40160114` and `TG_HPP` `0x40160118` (the pop and the signature); `TG_ACT0` `0x40161300` w0 bit 2
and `TG_PEND0` `0x40161200` w0 (rank 1 - was the epoch retired, was an SGI pending); `TG_GRP2` `0x40161088` w2
bit 12 and `TG_GRP0` `0x40161080` w0 (rank 2 - is `0x4C` a Group-1 source at the post-force instant); the pair
`TG_CCTLR`/`TG_DCTLR` (`0x40160100`/`0x40161000`); `TG_ISP2`/`TG_ACT2` (`0x40161208`/`0x40161308`) bit 12; and the
companions `TG_TGT` `0x4016184c`, `TG_PMR`/`TG_ABPR` `0x40160104`/`0x4016011c`. The self-gating write is the ONE
ranked lever: `GICC_EOIR` `0x40160110 <= 0x402` (the exact value the firmware's own EOI writes at file `0x82f52`,
`str r7,[r3]` with `r7` = the IAR word), emitted ONLY if a pre-read shows `RPR = 0x00` with `ISACTIVER0` bit 2
set. The builder change is in the main checkout (`tools/patch_fw_scratch.py`'s clamp must accept a full IAR
word), the ko is unchanged (`3f87f1e9fe5ed9666f27d1f784d34535`), so no ko commit, no CI, no push.

## The branches (read from the branch tables; the group-enable write is deferred)

`bg2.md` sec. 4 tabulates eight rows over `GRP_SNT` present plus `GRP_CTLR/PMR/I2/ACT2/EN2`, the retained
`STK_*` and the take6f pair `STK_RPR1`/`STK_HPP1`. The load-bearing split:

1. **`STK_RPR1 == 0xFF` AND `STK_HPP1 == 0x3FF` AND `GRP_CTLR` bit 1 `== 0` AND `GRP_I2` bit 12 `== 1` -> GROUP
   GATE CONFIRMED.** With the running priority dropped and `0x4C` pending, enabled and priced `0x00`, the group
   is the only unrefuted layer: `0x4C` is a Group-1 source with `EnableGrp1 = 0`. Next: the rank-1 write
   (`GRP_I2` bit 12 -> 0) or the rank-2 write (`GICC_CTLR -> 0x03`) and a re-run.
2. **`take7f`: `GRP_I2` bit 12 was `1` and (`STK_HPP1 == 0x4C` OR `GRP_ACT2` bit 12 set) -> THE FINAL PROOF.** The
   write moved `0x4C` into the enabled group in the SAME boot and the IAR read it: `RPR` idle, `HPPIR = 0x4C`,
   the take. That is the arc's end (`RPR` -> group/CTLR -> take) and the incumbent retires.
3. **`STK_RPR1 == 0xFF` AND `STK_HPP1 == 0x3FF` AND `GRP_CTLR` bit 1 `== 1` AND `GRP_I2` bit 12 `== 0` AND
   `GRP_ACT2` bit 12 clear -> NOT GROUP, NOT RPR: THE BANK OWNS IT.** The group is enabled, `0x4C` is a Group-0
   source, `RPR` is idle, and it still does not forward: the residual is the SGI/PPI ACTIVE bank (`STK_ACT ==
   0x4`). Next: the rank-3 bank quiesce, a `.ko` `ICENABLER0/1` (CI `omo/phase22-hccaccept`).
4. **`GRP_PMR < 0xC0` -> PMR MASK.** The priority mask blocks forwarding regardless of group. Next: `GICC_PMR ->
   0xF0` and a re-run.
5. **`GRP_ACT2` bit 12 set, or the IAR names `0x4C` with `GRP_EN2` bit 12 set -> TAKEN.** The gate was a
   transient and the take is proven.
6. **`take7f`: `GRP_WR` read-back != the written value -> FORCE MISSED (write not visible).** A build/visibility
   fault, not a model refutation; `E_CTLR` bit 31 `RWP = 0` predicts visibility, so if it differs, rebuild.
7. **`E_CTLR` bit 1 `== 1` while `STK_HPP1 == 0x3FF` and `GRP_I2` bit 12 `== 1` -> CONTRADICTION (re-measure).** The
   CTLR sample and the group sample disagree: a stale-cell or build fault, not an arbitration result.
8. **Any `GRP_*` or `STK_*` sentinel `!= 0x50AA7E49` -> NO-SAMPLE.** That instant's pad did not run; read no cell
   from it.

Rows 1 and 3 are the split (row 1 names the group, row 3 promotes the bank); row 2 is the arc's end and the
only intervention row, gated to `take7f` so observation never shares a verdict with intervention. `bankgate.md`
sec. 5 states the same decision as a rule: `TG_HPP = 0x4C` with the corrected-id force closes rank 1;
`TG_HPP = 0x3FF` with `TG_ACT0` bit 2 clear AND `TG_GRP2` bit 12 `= 0x1` promotes rank 2 to the gate;
`TG_HPP = 0x3FF` with `TG_ACT0` bit 2 SET keeps rank 1 and makes the epoch's retirement the fix. The conditional
second lever (rank 2's group enable, `GICC_CTLR -> 0x3` and/or `GICD_CTLR -> 0x3`) is DELIBERATELY DEFERRED: the
stamp is unmeasured at the post-force instant, and enabling a group admits the already-pending `0x40`/`0x45`/`0x4C`
(`E_ISP = 0x1021`) into the vendor ISR at once. The rank-3 bank quiesce is deferred too: it risks the Wi-Fi
doorbell (`F5_SGIP = 0x20000000` is the live instance).

## Why a bad EOIR is the top rank, not a footnote

The shipped force wrote `0x2` while the firmware's own EOI at file `0x82f52` writes the whole IAR word
(`0x402`). `bankgate.md` sec. 1 makes that the leading candidate on the spec, not on taste: the write landed in
the UNPREDICTABLE clause, and the resulting half-done EOI (priority drop without retirement) is exactly the
split the capstone measured. The corrected force is also strictly safer than the shipped one - it writes the
value the firmware writes, in the spec's defined branch - and its residual harm mode (an id that is not
currently active is UNPREDICTABLE; a spurious EOI could stall cross-core housekeeping until the GIC re-pends it)
is why the pad's pre-read self-gate is mandatory, not optional.

## Bounds (declared, not hidden)

1. **Design only, no new cell.** Every value here is the capstone's (`build/register-dumps/exp/20261007-010906/`,
   `capture-cmd.txt`, `run-take6f.log`, `interp.txt`); this addendum adds no measurement, only the gate analysis
   and the take7 design.
2. **`GICC_HPPIR = 0x3FF` is a three-way signature** - never read it as one thing (group enable, security view,
   or a rank above `0x4C`).
3. **The group stamp `0x40161088` was read in OTHER boots**, not at the post-force instant; the capstone read
   only word 0 (`G_GRP0 = 0x0`). That gap is take7's R5/`TG_GRP2`.
4. **Ranks 3/4/5 change nothing alone** - they are the cheap companions of R1/R2, and the image authors no
   `IGROUPR` write, so the stamp is inherited.
5. **`X_*` cells are NO-SAMPLE in take6-era boots** (take6 dropped the `selpost` pad) - never read `X_*` as data.
6. **take7 is a design**, and its byte budget depends on dropping the retained `selpost_e` E-block pad (152 B);
   the pads must not silently truncate (`layoutdiff.md` sec. 4).
7. **`TG_AHPP` `0x40160128`** is the NS-view alias, layout-derived like every GICC offset; re-verify it against
   the spec offset table at build time. It is NOT the forbidden ack IAR `0x4016010c` or AIAR `0x40160120`, and
   `GICD_SGIR 0x40161f00` is never read.

## Verification and health

No device action was taken by the two arm-A tasks or by this record. Sources: `build/tmp/inta-spec/bankgate.md`
(task `st_01a113ee`, `bankgate.md` sec. 1 the new negative and `F_HPP`, sec. 2 the three-way HPPIR signature,
sec. 3 the ranked gates, sec. 4 R1..R14, sec. 5 the take7 design and the deferred group lever, sec. 7 the
literal census and the spec quotes) and `build/tmp/inta-spec/bg2.md` (task `st_01a113ef`, sec. 0 the residual,
sec. 1 the two variants `take7`/`take7f`, sec. 2 the `GRP_*` cells, sec. 4 the branch table). The boot under
them is the official capstone `build/register-dumps/exp/20261007-010906/` (ADDENDUM 25a, `EXP RESULT: PASS`,
blob `2c1ae79f892e922d0df0583f87fb1a2c`, ko `3f87f1e9fe5ed9666f27d1f784d34535`, emitted-bytes 10/10,
`GATE_MTD_NUM=14`/`rootfsb`). NOTE for cross-reference: `bg2.md` records `bankgate.md` as absent at its read time
and notes it rather than fabricating it; both files exist now. Router health unchanged and untouched:
`WIPHY=2 IFACE=6 CAL2G=1 CAL5G=1`, `PAT=0 OMO_OFF=0 WIFIDRV1=0`, stock md5
`0e530b976d5a20e87358671f1a577695`, on slot B. Hard rules held: no write of CA `0x400392f0`/`0x40039af0`, no read
of `0x10161000`, no host read of the ack IAR `0x4016010c`/AIAR `0x40160120`, no `GICD_SGIR` read, no device
cycle, nothing staged, no `rmmod` of vendor modules, no commit, no push.

## The next threads

- **Build take7, run it once, read `TG_GRP2`.** One boot with the corrected EOIR (`0x40160110 <= 0x402`) plus
  R1..R14 decides rank 1 vs rank 2 vs rank 3. `take7` observes; `take7f` intervenes (the rank-1/2 write), kept
  md5-pinned apart so the observation and the intervention never share a verdict.
- **Gate the rank-2 write on the rank-2 read.** `GICC_CTLR -> 0x3` / `GICD_CTLR -> 0x3` ships only if `TG_GRP2`
  bit 12 reads `1` (or the CTLRs disagree with the stamp); it admits `0x40`/`0x45`/`0x4C` at once.
- **Gate the rank-3 bank quiesce on the group read.** The SGI-bank `ICENABLER0/1` ships only if the group is
  enabled and `0x4C` is Group 0. It risks the Wi-Fi doorbell, so it is last.
- **A reboot clears all of it.** The vendor bring-up rewrites the GIC every boot, so every lever here is
  register state, boot-scoped, and bound-covered.

## Artifacts

- Specs `build/tmp/inta-spec/{bankgate.md,bg2.md}` (bankgate: sec. 0 the settled state and the mislabelled CA,
  sec. 1 the spec quotes, sec. 3 the ranked gates, sec. 4 R1..R14, sec. 5 take7, sec. 6 safety, sec. 7 the
  census and the ledger; bg2: sec. 0 the residual, sec. 1 the two variants, sec. 2 the `GRP_*` cells, sec. 3 the
  selftest additions, sec. 4 the branch table, sec. 5 safety/bounds/ledger).
- Boot `build/register-dumps/exp/20261007-010906/` (`capture-cmd.txt`, `run-take6f.log`, `interp.txt`, from
  ADDENDUM 25a) carries every value quoted here.
- Emitter ground truth `tools/patch_fw_scratch.py` (`pad_stk_eoir` 11385, `make_take6` 11634); blob
  `build/tmp/fw-patched/take6f.bin` md5 `2c1ae79f892e922d0df0583f87fb1a2c`; ko `wifidrv1.ko` md5
  `3f87f1e9fe5ed9666f27d1f784d34535` (unchanged, no CI).
- Capstone rc/specs `build/tmp/inta-spec/{stk3,eoir,stuck,layoutdiff}.md`; spec sources ARM IHI0048
  `GICC_HPPIR`/`GICC_CTLR`/`GICC_EOIR` via `arm.jonpalmisc.com` (quoted in `bankgate.md` sec. 7).

# ADDENDUM 27 (2026-10-07): the take7b force - THE SELF-GATE POLARITY IS CURED IN THE BLOB AND THE STORE STILL DID NOT FIRE: the force pad's `cbz` inverted guard is fixed to `cbnz` (one byte, capstone-proven byte-for-byte), the take7b boot RAN clean and everything around the force is CONFIRMED (pins, the 9/9 emitted gate, the sentinels, the SGI-2 naming, the mandatory bound, no new pstore), BUT `TG_RPR` reads `0x00` = the NOFORCE cell again while its immediate neighbours measured the firing pre-state (16/16 `GICC_RPR = 0x00` plus `GICD_ISACTIVER0` bit 2 SET) and take6f's UNGATED force dropped `RPR` to `0xFF` on that same pre-state, so the arc's rank-1-vs-rank-2 question stays OPEN as an UNEXPLAINED skip rather than a clean `no zero epoch`, and the next cycle needs a PRE-force `RPR` deposit (or a matched pre/post pair) before any rank lever is spent

ADDENDUM 26 designed the take7 boot: the corrected EOIR force (the FULL IAR word `0x402` to `GICC_EOIR`
`0x40160110`, self-gated to fire only when `RPR = 0x00` and `ISACTIVER0` bit 2 is SET) plus the post-force
bank/group read set R1..R14. That design reached the chip TWICE, and both boots are read here: take7 (vrun23)
and its one-byte repair take7b (this record). Neither fired the store. This addendum is the arm-A record of the
take7b boot and of the `gfix.md` repair that produced its blob; the two specs are `build/tmp/inta-spec/gfix.md`
(the polarity bug, the intent, the fix, the emitted-ops proof) and the design pair `bankgate.md`/`bg2.md`
(ADDENDUM 26). No device action was taken by THIS record; the cycle was the sibling task `st_01a11417`'s, and
the official read is the adversarial verdict `build/register-dumps/diffs/20261007T0212Z-vrun24/verdict.txt`.

## Short version

The blob is right and the result is a NEGATIVE, and the two must not be blurred. `take7b.bin` (md5
`f5f5309fab9ae76518b9e1368a9ca768`) differs from `take7.bin` (md5 `b41b0aacfbd46bd8619d71f197431f49`) at EXACTLY
one byte: file `0xcb8fd`, `b1 -> b9`. take7 shipped `cbz r5, dsb_at`, which SKIPPED the store when `GICC_RPR ==
0x00`, the exact state the force exists to retire; the emitter's own comment said `CBNZ` while the opcode said
`cbz`, and the selftest asserted the wrong op and locked the inversion in. take7b emits `cbnz r5, dsb_at` (skip
only when `RPR != 0`), the same two bytes, the same `dsb_at`, the same post-force deposits, so the layout and
every other pad are untouched and the ko (`3f87f1e9fe5ed9666f27d1f784d34535`) needs no rebuild, no CI, no push.

The boot (`build/register-dumps/exp/20261007-020643/`, `TAKE7B RESULT: PASS`, `exp_rc=0`) is clean everywhere
BUT the experiment itself:

- **`TG_RPR = 0x00000000`** and **`TG_HPP = 0x000003FF`**: the post-force cells read the UNFORCED state. The one
device write did not take effect; there is no transition to show.
- **Its immediate neighbours measured the FIRING pre-state.** `STK_STICKY = 0` over 16/16 `GICC_RPR` samples
  (all sixteen `STK_0..STK_15` read `0x00`) and `STK_ACT = 0x4` names SGI 2 (id `0x2`) the priority-0 incumbent
  with bit 2 SET, and `V2_ID = V2_RING3 = 0x402` (the firmware's own IAR read). The ring instant is the force
  pad's immediate predecessor in the executed chain.
- **The take6f contrast is the load-bearing one.** take6f's UNGATED (bare `0x2`) force on that SAME pre-state
  DROPPED `RPR` (`STK_RPR1 = 0xFF`, ADDENDUM 25a). take7b's CORRECTED, self-gated, full-word force did not
  (`TG_RPR = 0x00`). Right gate, right blob, and the store still never landed.
- **The whole snapshot is the unforced one.** `TG_ACT0 = 0x4` (SGI 2 still active), `TG_PEND0 = 0`, `TG_GRP2 =
  0` (id `0x4C` is a Group-0 source), `TG_GRP0 = 0`, `TG_CCTLR = 0x1` and `TG_DCTLR = 0x1` (EnableGrp0 = 1,
  EnableGrp1 = 0), `TG_ISP2 = 0x1021` (`0x4C` bit 12 STILL PENDING), `TG_ACT2 = 0` (`0x4C` NEVER acked), `TG_TGT
  = 0x01010101`, `TG_PMR = 0xF0`, `TG_ABPR = 0`, `TG_AHPP = 0`.
- **The PASS is the INSTRUMENT gate, not the experiment.** The runner's `PASS` is `INSTRUMENT_GATE=HELD` (the
  take7b blob reached the custom slot) plus the health line; the force's own witness is `TG_RPR`, and it reads
  `0x00`. A PASS here must never be read as "the EOIR force landed".

So the vrun23 DEFECT (the inverted gate) is CURED, and a NEW, unexplained skip takes its place. The rank-1 (the
id-mismatched EOIR) versus rank-2 (the group) question the boot exists to settle is NOT ANSWERED.

## The branches (read from the take7 branch table; quoted from `vrun24` sec. 3/7)

The `take7b` readings do not satisfy any decision row cleanly. Row by row, against `capture-cmd.txt`:

1. **Row T7-0, NO-SAMPLE: refuted.** `TG_SNT = STK_SNT = N_SNT = B_P3 = B_P4 = F_SNT = C_SNT = 0x50AA7E49`.
   Every chain pad RAN. (`E_SNT`/`X_SNT` read `0x00000000` = NO-SAMPLE BY CONSTRUCTION, `selpost_e`/`selpost`
   dropped in this layout, not a failure.)
2. **Row T7-1, `TG_RPR == 0x00` -> NOFORCE: SATISFIED, and that is the finding.** The row's own reading is "the
   pre-store self-gate held (RPR != 0 or ISACTIVER0 bit2 clear)", yet the neighbours say pre-`RPR` was `0x00`
   with bit 2 SET. The row's stated reading is internally strained here (vrun24 D3): the classification table
   has NO honest row for "the gate is provably correct, the pre-state is the firing state, and the store still
   did not take".
3. **Row T7-2, `TG_RPR == 0xFF` AND `TG_HPP == 0x4C` -> rank 1 was the gate: NOT SATISFIED.** No `0xFF`
   (`TG_RPR = 0x00`), no `0x4C` (`TG_HPP = 0x3FF`).
4. **Row T7-3, GROUP GATE CONFIRMED: NOT SATISFIED.** It needs `TG_GRP2` bit 12 `= 1` and `TG_CCTLR` bit 1 `=
   0`; here `TG_GRP2 = 0x00` (bit 12 CLEAR, id `0x4C` is Group 0) and `TG_CCTLR = 0x01` (bit 1 CLEAR), so even a
   fired force would have matched NEITHER this row nor row T7-4.
5. **Row T7-4, the BANK owns it: NOT SATISFIED.** It needs `TG_CCTLR` bit 1 `= 1`; it reads `0`, and the
   unforced snapshot says nothing about who holds `0x4C` (the arc's question stays open).
6. **Row T7-5, `TG_HPP == 0x4C` AND `TG_AHPP == 0x4C`: NOT SATISFIED.** `TG_AHPP = 0x0`, `TG_HPP = 0x3FF`.
7. **Row T7-6, `TG_PMR < 0xC0` -> PMR mask: refuted.** `TG_PMR = 0xF0` (>= `0xC0`), so the priority mask does
   not block `0x4C`'s priority `0x00`.
8. **Row T7-7, `TG_ACT2` bit 12 SET -> the take moved: NOT SATISFIED.** `TG_ACT2 = 0`, `0x4C` was never acked.
9. **Row T7-8, `TG_ISP2` bit 12 CLEAR -> a racing clear: NOT SATISFIED.** `TG_ISP2 = 0x1021`, bit 12 still set.

One boot, no row closes. The honest label is the residual, not a verdict.

## Why this is an UNEXPLAINED skip, and what it is NOT

`vrun24` sec. 7 (D1) separates the three candidates the cells can and cannot decide. The post-read sits
immediately after the `dsb sy` with nothing between it and the store, so `TG_RPR = 0x00` pins the PRE-store
`RPR` to `0x00` too (had `RPR` been non-zero, the `cbnz` would have skipped with a NON-zero `TG_RPR`). With
pre-`RPR = 0x00`, the only guard that can skip is the SECOND one, `GICD_ISACTIVER0` bit 2 CLEAR, yet both
immediate neighbours read that same word with bit 2 SET (`STK_ACT = 0x04`, `TG_ACT0 = 0x04`). The three
candidates, none provable from the captured cells:

- **(a)** an instantaneous pre-store difference the neighbours cannot see: the force pad's OWN `ISACTIVER0`
  read saw bit 2 clear (a sub-microsecond transient). Possible; nothing supports it.
- **(b)** the store DID fire and the full-word `0x402` EOIR did not drop `RPR`. DISFAVOURED: ADDENDUM 21a
  measured `I5_RPR = 0xFF` right after the firmware's OWN EOI (file `0x82f52`), which writes the same raw IAR
  word, so `0x402` does drop `RPR` in this image's own ISR.
- **(c)** the EOIR retired the epoch, the LEVEL SGI-2 re-asserted, and it was re-taken inside the `dsb` window
  (`RPR` back to `0x00` at the post-read, `TG_ACT0` bit 2 SET again). The pad deposits NO pre-force `RPR`, so
  (a)/(b)/(c) cannot be split from the cells.

THIS IS NOT: a safety failure (the bound held, no forbidden CA was touched, no flash wrote, the router is
healthy), a build fault (the blob's one-byte delta and the `cbnz` are capstone-proven), or a NO-SAMPLE (every
sentinel fired). It is a verified NEGATIVE and an OPEN residual. The one mechanism that WOULD split it is the
D2 gap: the pad does not deposit its PRE-force `RPR`, so "`RPR == 0x00` at the gate" is INFERRED (post ==
pre), not cell-proven. That gap is unchanged by take7b and is the next cycle's whole reason to exist.

## Bounds (declared, not hidden)

1. **One instant, no matched pair.** The bank/group block is a single post-force sample; `STK_STICKY` covers
   `RPR` only, and in this boot it is moot because the force never ran (`bg2.md` sec. 5 bound 1).
2. **The PASS is the instrument gate.** `INSTRUMENT_GATE=HELD` plus the health line is the runner's `PASS`;
   it is NOT evidence the force landed. `TG_RPR = 0x00` is the force's own witness.
3. **The blob is fixed, the ko is not rebuilt.** The whole take7b-vs-take7 delta is one byte (`cmp -l`: file
   `0xcb8fd`, `0xb1 -> 0xb9`); the 76-B force pad keeps its size, slot (`0xcb8e4`), `dsb_at` and deposits, and
   the ko md5 `3f87f1e9fe5ed9666f27d1f784d34535` is unchanged, so no ko commit and no CI.
4. **The pre-force `RPR` is INFERRED.** The pad deposits only the post cells; the D2 gap is exactly what blocks
   the (a)/(b)/(c) split. Do not read `TG_RPR = 0x00` as a directly measured pre-state.
5. **The group snapshot is unforced.** `TG_GRP2`/`TG_CCTLR`/`TG_ACT2` describe the pre-force world; they say
   nothing about who holds `0x4C` once `RPR` drops. `TG_HPP = 0x3FF` here is the unforced state.
6. **`E_*`/`X_*` cells are NO-SAMPLE BY CONSTRUCTION** in take7-era boots (`selpost_e`/`selpost` dropped):
   never read them as data.
7. **`TG_AHPP` `0x40160128`** is the layout-derived NS-view alias, re-verified against the spec offset table at
   build time. It is NOT the forbidden ack IAR `0x4016010c` or AIAR `0x40160120`, and `GICD_SGIR 0x40161f00`
   is never read.
8. **The instrumentation has cosmetic display defects only.** The hook prints a bare `cpsie` and the runner's
   decision-table echo backtick-quotes `cbz`; both garble DISPLAY TEXT, no cell, pin, gate or bound line
   (`vrun24` D5). The capture hook's own label still says "take7"; the authoritative records are the pin check
   and `--check-emitted` (D6). The take7b manifest's `take7b.eoir.gate` string is stale take7 text; the
   authorities are `take7b.gate_fix` and the capstone disasm (D4).

## Verification and health

No device action was taken by this record. Sources: the official verdict
`build/register-dumps/diffs/20261007T0212Z-vrun24/verdict.txt` (task `st_01a1141d`, adversarial; C1 the pins +
9/9 emitted gate CONFIRMED, C2 the sentinels CONFIRMED, C3 the SGI-2 naming CONFIRMED, C4 the corrected-EOIR
force's `RPR` drop FAIL, C5 the mandatory bound CONFIRMED, C6 no new pstore CONFIRMED, C7 the honest branch
REPORTED as an OPEN residual) and the fix spec `build/tmp/inta-spec/gfix.md` (task `st_01a11411`: the polarity
bug, the intent, the one-byte fix, the capstone disasm, the two-byte delta). The take7b cycle was the sibling
task `st_01a11417`'s (`bash build/tmp/wifidrv1-art/run-take7b.sh`, `EOIR_ID=0x402` over `tools/exp.sh`, custom
slot `mtd14:rootfsb`). Blob `take7b.bin` md5 `f5f5309fab9ae76518b9e1368a9ca768`; ko `wifidrv1.ko` md5
`3f87f1e9fe5ed9666f27d1f784d34535` (unchanged). The cycle end state, verbatim: `TAKE7B RESULT: PASS`,
`exp_rc=0`, `POST_WIPHY=2 / POST_IFACE=6 / POST_CAL2G=1 / POST_CAL5G=1 / POST_WIFIDRV1=0 / POST_MTD_NUM=14`,
`POST_BOOT_ID=ab073ef2-d57d-42ad-971d-c42bc92f80b8`; a live read-only probe read the SAME boot id at uptime
164s. The mandatory bound was armed before the hide, tripped (`IRQ_DISABLED_BOUND irq=207 n=65 bound=64`,
`irq=209 n=9 bound=8`), and the literal pair was restored unconditionally. No new pstore record. Router healthy
on `mtd14:rootfsb` (2.5.24): `WIPHY=2 IFACE=6 CAL2G=1 CAL5G=1`, `PAT=0 OMO_OFF=0 WIFIDRV1=0`, stock md5
`0e530b976d5a20e87358671f1a577695` untouched, zero `.omo-*`/`.omo-pat`/`.omo-off`/`wifidrv1` leftovers. Hard
rules held: no write of CA `0x400392f0`/`0x40039af0`, no read of `0x10161000`, no host read of the ack IAR
`0x4016010c`/AIAR `0x40160120`, no `GICD_SGIR` read, no `rmmod` of vendor modules, no commit, no push.

## The next threads

- **Deposit the PRE-force `RPR` (or a matched pre/post pair).** This is the D2 gap and the ONLY thing that
  splits (a)/(b)/(c). A take7c pad that stores `GICC_RPR` BEFORE the `dsb`/store, next to the post cells,
  settles whether the gate saw `0x00` and whether the skip was the second guard or a re-take.
- **Do not spend a rank lever until the force's own witness moves.** No `TG_RPR = 0xFF` means no rank had its
  day in court; the group-enable write (`GICC_CTLR -> 0x3`) and the bank quiesce stay deferred (ADDENDUM 26's
  reasons stand: the enable admits the pending `0x40`/`0x45`/`0x4C`, the quiesce risks the Wi-Fi doorbell).
- **One boot, one question.** `take7` observes; `take7f` intervenes; keep the observation and the intervention
  md5-pinned apart so they never share a verdict (ADDENDUM 26).
- **A reboot clears all of it.** The vendor bring-up rewrites the GIC every boot, so every lever here is
  register state, boot-scoped, and bound-covered.

## Artifacts

- Fix spec `build/tmp/inta-spec/gfix.md` (task `st_01a11411`: sec. 1 the vrun23 inversion, sec. 2 the intent,
  sec. 3 the `take7b` emitter, sec. 4 the emitted-ops proof, sec. 5 the two-byte delta, sec. 6 what a take7b
  boot settles).
- Design pair `build/tmp/inta-spec/{bankgate.md,bg2.md}` (ADDENDUM 26: the ranked gates, R1..R14, the branch
  table, the deferred levers).
- Boot `build/register-dumps/exp/20261007-020643/` (`capture-cmd.txt`, `run-take7b.log`, `PACKED.txt`,
  `interp.txt`, `health.txt`) carries every take7b value quoted here; `capture-cmd.txt` is the hook's own label
  file and its per-cell value line follows each header.
- Verdicts `build/register-dumps/diffs/20261007T0212Z-vrun24/verdict.txt` (take7b, this record's read) and
  `build/register-dumps/diffs/20261007T0143Z-vrun23/verdict.txt` (take7, the polarity bug, D1).
- Blobs `build/tmp/fw-patched/take7b.bin` md5 `f5f5309fab9ae76518b9e1368a9ca768` and `take7.bin` md5
  `b41b0aacfbd46bd8619d71f197431f49` (one byte apart, file `0xcb8fd`); emitter ground truth
  `tools/patch_fw_scratch.py` (`pad_stk_eoir_take7b`, `pad_stk_bank_take7b`, `cbnz`); ko `wifidrv1.ko` md5
  `3f87f1e9fe5ed9666f27d1f784d34535` (unchanged, no CI).
- Runner `build/tmp/wifidrv1-art/run-take7b.sh`; capture hook `build/tmp/wifidrv1-art/take7-capture.hook`; the
  capstone baseline `build/register-dumps/exp/20261007-010906/capture-cmd.txt` (take6f's UNGATED `STK_RPR1 =
  0xFF` on the same pre-state).

# ADDENDUM 28 (2026-10-07): the gated pad's structure - arm A: THE DEFECT NAMED AND THE FIX VERIFIED, THE BOOT STILL QUEUED

**Branch name: `PAD-STRUCTURE` (this note is the padstruct record, not a boot verdict).** ADDENDUM 27 left the
take7b store an UNEXPLAINED skip. This note names the defect, writes the fix, and states plainly that no boot has
spent it: **the fix is host-verified ONLY; the take7c boot is QUEUED and unwritten, so no device cell cited here
has moved.**

## Short version

- **The defect.** take7/take7b's gate skips in BOTH complementary polarities because ITS SECOND TERM is the only
  element that can skip in both boots: take7 shipped `cbz` (skip when `RPR == 0`) and take7b the corrected `cbnz`
  (skip when `RPR != 0`), so on any stable operand exactly one of the two MUST have fired, and neither did.
  `TG_RPR = 0x00` pins the PRE-store `RPR` to `0x00`, so guard 1 passed and guard 2 (`GICD_ISACTIVER0` bit 2)
  decided - while its two immediate neighbours read that bit SET (`TG_ACT0 = 0x04`, `STK_ACT = 0x04`). Guard 2
  tests a TOGGLING bit (the SGI-2 ACTIVE bit, SET on acknowledge, CLEAR on the handler's EOI) at a single chosen
  instant, and the pad deposits nothing about what its own gate read, so the skip is indistinguishable from a
  store that never landed (`vrun24` D2, ADDENDUM 27).
- **The structural half.** The gate introduced the chain's FIRST Distributor-read -> CPU-interface-write pair
  (the `GICD_ISACTIVER0` read before the `GICC_EOIR` write), and take7/take7b's only `dsb sy` sits AFTER the
  store, so it cannot order that read against it; the pad that FIRED (take6f) touched only CPU-interface
  registers. GICv2/GIC-400 do not order Distributor accesses against CPU-interface accesses.
- **The fix: `take7c`, a NEW md5-pinned variant** (take7/take7b stay frozen byte-for-byte). Four deltas, all
  inside the SAME 76-byte force pad at the SAME slot `0xcb8e4`: (1) `TG_PRERPR 0x150204` <- the `GICC_RPR` the
  guard ITSELF read, BEFORE the `cbnz`; (2) `TG_PREACT 0x150208` <- the `GICD_ISACTIVER0` word the guard ITSELF
  read, inside the fall-through; (3) a `dsb sy` between the guard's Distributor read and the CPU-interface store;
  (4) the store made PAGE-RELATIVE through `r2` (`str r0,[r2,#0x10]`), which is 8 B cheaper and funds (1)-(3) at
  the same size. The post-force `TG_RPR`/`TG_HPP` instant is unchanged, so ADDENDUM 27's "`TG_RPR == 0x00` pins
  the pre-store `RPR`" argument survives verbatim.
- **Host-verified.** Three regenerations byte-identical (`take7c.bin` md5 `60e0af1cb7fb32ea9e58178f754dfbf3`);
  `--check-emitted` PASS 10/10 ops in op order; the take7c-vs-take7b delta is 26 bytes, file `0xcb8fc..0xcb919`,
  ALL INSIDE the 76-byte force pad; take7b's own blob and md5 are UNCHANGED.
- **QUEUED.** The boot (`st_01a11417`-shaped cycle, `take7c.bin` staged + two new hook cells) has NOT run. No
  device action was taken for this record.

## The two-part defect, stated as the spec states it

`padstruct.md` (`st_01a1142a`) refutes all four encoding candidates (a clobbered guard register, a wrong store
offset/cell, a guard branching past the store, stale flags) directly from the emitted bytes, then names what is
left. Three of its four parts are a design defect, not an encoding bug:

1. **Guard 2 is the only term that can skip in both boots.** The two polarities are COMPLEMENTARY, so a SET bit
   forces exactly one of take7/take7b to fire; neither did. With pre-`RPR = 0x00` (pinned by the post-read that
   follows the pad's own `dsb sy` on both paths), guard 1 passed by construction and guard 2 carried the decision
   - against both bracketing reads of the same word.
2. **Guard 2 samples a toggling bit ONCE.** The pad's own design language already distrusts a single instantaneous
   sample (the fast sampler folds 16 `GICC_RPR` reads into `STK_STICKY` so "a sub-`dsb` transient is never
   missed"), yet the force pad's guard never got that treatment and samples at an instant the chain chooses.
3. **The gate is evidence-free (`vrun24` D2).** `TG_RPR`/`TG_HPP` are POST-force, so a skipped store and a store
   that did not land look identical, and the guard's own two operands are not recorded at all. Two boots were
   spent on a decision the evidence cannot carry.
4. **The barrier is on the wrong side.** A `dsb` orders accesses on each side of it; the single post-store `dsb`
   orders the store against the post-reads but cannot order the `GICD_ISACTIVER0` read against the `GICC_EOIR`
   write. That cross-interface pair is the ONLY access-ordering difference from the pad that fired.

## The fix (design, verified host-side only)

`tools/patch_fw_scratch.py` gains `pad_stk_eoir_take7c()` (+ the `NOTE_TAKE7C` manifest text, the `TG2_CELLS`
pair `(0x150204, 0x150208)`, and the extended emitted-bytes gate). Deltas 1-3 are new instrumentation; delta 4 is
the funding change that keeps the pad at EXACTLY 76 B so no pad, cell, slot, site or chain link moves:

```
take7b (BEFORE)                                     take7c (AFTER)
0x10b8fa ldr r5,[r2,#0x14]   ; GICC_RPR (PRE)       0x10b8fa ldr r5,[r2,#0x14]   ; GICC_RPR (PRE)
         (read discarded)                          0x10b8fc str r5,[r6,#0x3c]   ; *** TG_PRERPR ***
0x10b8fc cbnz r5,#0x10b91a                          0x10b8fe cbnz r5,#0x10b91a
0x10b8fe movw/movt r1,#0x40161300                   0x10b900 movw/movt r1,#0x40161300
0x10b906 ldr r1,[r1]         ; ISACTIVER0 w0        0x10b908 ldr r1,[r1]         ; ISACTIVER0 w0
         (read discarded)                          0x10b90a str r1,[r6,#0x40]   ; *** TG_PREACT ***
0x10b908 lsls r5,r1,#0x1e                           0x10b90c lsls r5,r1,#0x1e
0x10b90a blo  #0x10b91a                             0x10b90e blo  #0x10b91a
         (no barrier)                              0x10b910 dsb sy              ; *** GICD read -> GICC store ***
0x10b90c movw/movt r1,#0x40160110                   0x10b914 movw r0,#0x402
0x10b914 movw r0,#0x402                             0x10b918 str r0,[r2,#0x10]   ; THE STORE, page-relative
0x10b918 str r0,[r1]                                0x10b91a dsb sy              ; dsb_at (both guards)
0x10b91a dsb sy              ; dsb_at                ... tail unchanged: TG_RPR / TG_HPP / pop / msr / b.w
```

The guard POLARITY is take7b's corrected `cbnz r5, dsb_at` (fire only on `RPR == 0`); the
`lsls r5,r1,#0x1e` / `blo dsb_at` term (bit 2 SET) is unchanged. Every emitted byte is take7b's except the four
deltas.

**Proof (host-only, this session; public commands):**

```
pyenv/Scripts/python.exe tools/patch_fw_scratch.py --fw build/tmp/FIRMWARE.bin \
  --barmap opensource/build/register-dumps/barmap_ep0_bar0.bin --variant take7c --eoir-id 0x402 \
  --out build/tmp/fw-patched/take7c.bin
  -> wrote ... (928920 B, md5 60e0af1cb7fb32ea9e58178f754dfbf3); three regenerations byte-identical
--check-emitted build/tmp/fw-patched/take7c.bin  -> PASS, 10/10 ops in op order
     EOIR_PAGE 0x40160100 <= 0x2    @ 0xcb8ea  (the r2 GICC-page setup)
     EOIR_ID   0x40160110 <= 0x402  @ 0xcb914  (the page-relative store)
--check-emitted take7b.bin -> PASS 9/9 ; take7.bin -> PASS 9/9 ; take6f.bin -> PASS 10/10  (all UNCHANGED)
--selftest   -> SELFTEST PASS (the frozen TAKE7_MD5 / TAKE7B_MD5 and the canonical-form NEGATIVE fixture hold)
```

The emitted-bytes gate is EXTENDED, not weakened: take7c carries TWO op entries (the page-relative store AND the
base register's own setup bytes), each rebuilt from the primitives, so a pad that stores through a register it
never set to that CA fails exactly like a dropped `movt` half. The selftest also asserts the two new cells are
zero at BOTH barmap views.

## The QUEUED boot test (the next device window) and the table the pre-state pair closes

Re-run the take7b cycle VERBATIM (serial/detached `tools/exp.sh`; params `hw=1 wr=1 fw=1 release=1 program=1
fwpath=/lib/firmware/hi_wifi/FIRMWARE.bin.omo-pat intapost=0x100 quiesce=0x1 rung=0 qbound=64 qbound209=8
qwait_ms=2000`; the slot gate on `mtd14:rootfsb`; a NEW `boot_id` before any reboot; the watchdog armed BEFORE
the hide; then `recover`, then health: 2 wiphys / 6 ifaces / cal `[SUCC]` / no `.omo-off`) with ONLY two changes:
the staged blob is `take7c.bin` (md5 `60e0af1cb7fb32ea9e58178f754dfbf3`) and the capture hook reads the two new
cells (`0x40808204`, `0x40808208`; every other cell, sentinel and label unchanged).

| # | reading | label | next action |
| - | ------- | ----- | ----------- |
| 1 | `TG_SNT != 0x50AA7E49` | NO-SAMPLE | the bank pad did not run - do not read the block |
| 2 | `TG_PRERPR != 0` | GUARD 1 HELD | the pad's OWN `RPR` read was non-zero ~15 instructions after the fast pad's 16 zero samples |
| 3 | `TG_PRERPR == 0`, `TG_PREACT` bit 2 CLEAR | GUARD 2 HELD, CELL-PROVEN | the distributor term skipped on a bit the neighbours read SET: the single-sample transient, proven for the first time. Next: a sticky/OR-folded ACTIVE term |
| 4 | `TG_PRERPR == 0`, `TG_PREACT` bit 2 SET, `TG_RPR == 0xFF` | THE STORE LANDED | the barrier + the pair worked; read `TG_HPP` (`0x4C` = rank 1 was the gate; `0x3FF` = the bank/group layer holds `0x4C`) + `TG_GRP2`/`TG_CCTLR`/`TG_ACT2` |
| 5 | `TG_PRERPR == 0`, `TG_PREACT` bit 2 SET, `TG_RPR == 0x00` | THE STORE DID NOT LAND | the gate passed and the effect is still absent: the residual is PAST the pad (the epoch re-formed in the window), NOT the gate |

Row 3 versus row 5 is exactly the split `vrun24` D2 said no cell could carry. NOT run here: the hook edit and the
cycle belong to the next device task.

## Bounds (declared, not hidden)

1. **HOST-ONLY.** No device action, no boot, no cell: every take7c statement above comes from the spec
   (`build/tmp/inta-spec/padstruct.md`), the emitter (`tools/patch_fw_scratch.py`), capstone disassembly of the
   emitted blob, `--check-emitted`, and `--selftest`. The nought boot cells this record cites are take7b's
   (ADDENDUM 27), re-read, not re-measured.
2. **The pre-state pair is ONE instant per term, not a matched series.** It makes the gate's decision PROVABLE;
   it does not make the toggling ACTIVE bit stable.
3. **The barrier removes the hazard; it does not prove the hazard was the cause.** Rows 4/5 of the table decide
   that, and the fix is safe either way - register state, reboot-cleared, no flash, no clock/reset register.
4. **The store is one 32-bit `GICC_EOIR` write of the value the firmware's own EOI writes** (the full IAR word
   `0x402` = SGI 2 from source PE 1, file `0x82f52`).
5. **76 B, SAME slot:** take7b's findings about every other pad hold verbatim, and take7b's blob is byte-UNCHANGED
   (`--check-emitted` 9/9, `TAKE7B_MD5` frozen in the selftest).
6. **The new cells read zero at BOTH barmap views** (asserted at build time), and they sit 4 B above
   `bankgate.md` sec. 5's 15-word block - no existing cell is displaced.

## Verification and health

No device action was taken by this record: the device was not touched, so no health receipt applies here. Sources:
the fix spec `build/tmp/inta-spec/padstruct.md` (task `st_01a1142a`; sec. 1 the four refuted encodings, sec. 2 the
defect, sec. 3 the four deltas, sec. 4 the before/after disasm, sec. 5 the host proof, sec. 6 the queued boot +
the branch table, sec. 7 the bounds) and, for the take7b facts the defect is read from, ADDENDUM 27 in this file
plus `build/register-dumps/diffs/20261007T0212Z-vrun24/verdict.txt`. Emitter ground truth
`tools/patch_fw_scratch.py` (`pad_stk_eoir_take7c`, `NOTE_TAKE7C`, `TG2_CELLS`, `_take7c_emitted`); the take7c
builder `tools/patch_fw_scratch.py` `--variant take7c`; blob `build/tmp/fw-patched/take7c.bin` md5
`60e0af1cb7fb32ea9e58178f754dfbf3`. The take7c-vs-take7b delta is 26 bytes at file `0xcb8fc..0xcb919`; take7c-vs-
stock and take7b-vs-stock both change 1810 bytes. Hard rules held: no write of CA `0x400392f0`/`0x40039af0`, no
read of `0x10161000`, no host read of the ack IAR `0x4016010c` / AIAR `0x40160120`, no `GICD_SGIR` read, dword-
aligned accesses only, and no commit and no push from this record.

## The next threads

- **Spend the fix: the take7c boot.** The table above is the whole reason the variant exists; it is the D2 gap's
  first cell-provable answer.
- **If row 3 lands (GUARD 2 HELD): make the ACTIVE term sticky** (an OR-fold of several `GICD_ISACTIVER0` reads
  around the instant, in the fast sampler's own idiom) - the single-sample defect then cannot decide anything.
- **If row 5 lands (STORE DID NOT LAND): stop spending rank levers.** The group-enable write (`GICC_CTLR ->
  0x3`) and the bank quiesce stay deferred (ADDENDUM 26's reasons stand: the enable admits the pending
  `0x40`/`0x45`/`0x4C`, the quiesce risks the Wi-Fi doorbell).
- **One boot, one question.** `take7c` intervenes; keep observation and intervention md5-pinned apart so they
  never share a verdict (ADDENDUM 26).
- **A reboot clears all of it.** The vendor bring-up rewrites the GIC every boot; every lever here is register
  state, boot-scoped, and bound-covered.

## Artifacts

- Fix spec `build/tmp/inta-spec/padstruct.md` (task `st_01a1142a`; the full defect/fix/proof record this addendum
  condenses).
- Emitter `tools/patch_fw_scratch.py`: `pad_stk_eoir_take7c()`, the `NOTE_TAKE7C` manifest text, `TG2_CELLS` /
  `TG_PRERPR` / `TG_PREACT` / `TG2_NAMES`, the take7c emitted-bytes gate and the take7c selftest block.
- Blob `build/tmp/fw-patched/take7c.bin` md5 `60e0af1cb7fb32ea9e58178f754dfbf3` (frozen take7b
  `build/tmp/fw-patched/take7b.bin` md5 `f5f5309fab9ae76518b9e1368a9ca768`, take7 `take7.bin` md5
  `b41b0aacfbd46bd8619d71f197431f49`).
- Prior record ADDENDUM 27 in this file (the take7b boot and the nine branch rows) and its verdict
  `build/register-dumps/diffs/20261007T0212Z-vrun24/verdict.txt` (D1/D2, the unexplained skip).
- Not yet written: the take7c capture hook (must add `0x40808204` / `0x40808208`) and the take7c runner.

### 29a. THE TAKE7C BOOT (appended 2026-10-07 by the orchestrator): INSTRUMENT GATE HELD ON THE RIGHT SLOT; THE CAPTURE HOOK TIMED OUT - NO CELLS, QUEUED

The take7c cycle (`build/register-dumps/exp/20261007-024805/`) ran on the CUSTOM image
(`ubi0: attached mtd14 rootfsb`; `INSTRUMENT_GATE=HELD`) with the module claiming the endpoint
(`BAR0 base=0x40000000`, `request_irq(207) rc=0`, the six viewports) and the OBSERVE rung reporting cleanly
(`[qsv] glue{raw=0x1 mask=0x20 stat=0x11} out0=0x8 twin{raw=0x8 mask=0x3ff stat=0x0}` - the no-storm state).
**The capture hook then timed out** (`capture-writer: waited=90s done=no`), so the take7c cells were never
read and the capture file holds only the slot/gate receipts (414 B). The pads deposit SILENTLY (they write
cells, not dmesg), so the module log cannot substitute for the cells. Classification: **NO-SAMPLE BY
CAPTURE** - the pad fix's live verdict is **QUEUED**, with the hook's wait extended (the `0x40808204` /
`0x40808208` cells per the note above) for the next device window; nothing about the force's physics is
claimed here (take6f's ungated firing remains the effect's proof, ADDENDA 25a/27).

**ARM B, SAME WINDOW - THE CRG'S FIRST DELIBERATE FLIP: PASS.** `crgflip-smoke.md`
(`build/tmp/inta-spec/`): the stage-2 flip ran on the re-armed rank-1 target (`0x14` b`0x18` `i2c0_clk`,
the CLEAR direction; submodule `3d2e4cd`, CI run 37564223517 success, ko `84a2f1f461cf18b95670c85e10051617`),
**target restored EXACTLY** (with the clear-direction semantics recorded in the smoke's own body), gate
fail-closed (one inert stale done-flag removed), device healthy after. With the stage-1 no-op census (48
stores, 15/16 exact) and the set-only `led_pwm` finding (ADDENDUM 28), the CRG write half now has both
halves of the discipline measured: the path proven, the semantics mapped, and the first reversible
experiment spent.

# ADDENDUM 30 (2026-10-07): the take7c re-run - arm A: THE CAPTURE LANDED, BOTH GUARDS PASSED CELL-PROVEN, AND THE EOIR STORE STILL DID NOT LAND: the pad-structure / `GICD`->`GICC` ordering hypothesis is REFUTED as the cause, the residual is PAST the pad, and the rank-1-vs-rank-2 arc question stays OPEN

ADDENDUM 28 designed take7c and ADDENDUM 29a recorded its FIRST boot, whose capture hook timed out (a
CRLF-corrupted hook; `capture-writer: waited=90s done=no`, a 414-B receipt with zero cells). The hook was
cured to LF and the cycle RE-RAN. This addendum is the arm-A record of that re-run: the boot
`build/register-dumps/exp/20261007-031654/` (`RUN_TS=20261007-031649`, `TAKE7C RESULT: PASS`, `exp_rc=0`),
its official verdict `build/register-dumps/diffs/20261007T0320Z-vrun26/verdict.txt` (task `st_01a1145d`, an
adversarial verifier that ran no device cycle), the CRLF diagnosis `build/tmp/inta-spec/hookfix.md` and its
proof `build/tmp/wifidrv1-art/_t7c-hookfix-verify.txt` (task `st_01a11456`), the design
`build/tmp/inta-spec/padstruct.md`, and the emitter `tools/patch_fw_scratch.py`.

The question was narrow and the boot answers it. take7c added two PRE-state deposits to the same 76-byte
force pad (`TG_PRERPR` = the guard's own `GICC_RPR` read; `TG_PREACT` = the guard's own `GICD_ISACTIVER0` w0
read), a `dsb sy` between the `GICD` read and the `GICC` store, and a page-relative store, all at the same
slot `0xcb8e4`. That instrument exists to split what `vrun24` D2 said no cell could carry: was the
never-firing force a GATE problem (the pad's own operands skipped) or an EFFECT problem (the gate passed and
the store retired nothing)?

## What the re-run read

The capture landed this time. `capture-cmd.txt` opens with `capture-writer: waited=1s done=yes`, carries the
`=== omo capture hook (take7c) ... ===` banner and `=== capture hook done ===`, and the runner logs
`hook-done banners: 1` - not the first run's 90-s timeout and zero banners. The staged hook md5 is
`7e695f93ce23453c83e878116b78ecfe` (0 CR / 313 LF); the first, failed run had staged the CRLF specimen
`f16458b66ea31c632c1b47d20138e4ec` (313 CR). So this boot is the FIRST take7c boot whose cells exist to be
read; the earlier take7c evidence was an empty receipt, not a physical result.

Every sentinel ran. `TG_SNT` and `STK_SNT` both read `0x50AA7E49`, as do `N_SNT`, `B_P3`, `B_P4`, `F_SNT`,
`C_SNT`, `S2+4` and `E2`; `WIN` reads `0xE59FF018`. `E_SNT` and `X_SNT` read `0x00000000`, which is NO-SAMPLE
BY CONSTRUCTION: `selpost_e` and `selpost` are dropped in this layout (`bg2.md` sec. 5), and the hook labels
them so. Not a failure.

Then the two new cells decide it. The pad's OWN operands read:

- `TG_PRERPR` = `0x00000000` - guard 1's `GICC_RPR` read (CA `0x40160114`, deposited BEFORE the guard). Zero
  means GUARD 1 PASSED.
- `TG_PREACT` = `0x00000004` - guard 2's `GICD_ISACTIVER0` w0 read (CA `0x40161300`). Bit 2 SET means GUARD 2
  PASSED, so the store was attempted.
- `TG_RPR` = `0x00000000` - the post-force witness (CA `0x40160114`). Still zero: the priority-0 epoch was
  NOT retired.
- `TG_HPP` = `0x000003FF` - nothing forwarded; the bank/group layer still holds `0x4C`.

Both guards passed on the pad's own evidence, the single EOIR write was attempted, and the effect is still
absent. That is row T7C-D of the runner's own table (`TG_PRERPR == 0x00` AND `TG_PREACT` bit 2 SET AND
`TG_RPR == 0x00` -> THE STORE DID NOT LAND), the same split `vrun24` D2 named. The take7c hypothesis
(`padstruct.md` sec. 2.4, the cross-interface ordering hazard) is REFUTED as the CAUSE: adding the `dsb` and
the page-relative store changed nothing.

## The post set is the UNFORCED snapshot, and the contrast is honest

The post-force bank/group block reads `TG_ACT0 = 0x00000004` (SGI 2 still ACTIVE), `TG_PEND0 = 0x00000000`,
`TG_GRP2 = 0x00000000` (bit 12 clear: `0x4C` is a Group-0 source), `TG_GRP0 = 0x00000000`, `TG_CCTLR =
0x00000001` (EnableGrp0 = 1, EnableGrp1 = 0), `TG_DCTLR = 0x00000001`, `TG_ISP2 = 0x00001021` (bit 12 = `0x4C`
still pending), `TG_ACT2 = 0x00000000` (bit 12 clear: the IAR never read `0x4C`), `TG_TGT = 0x01010101`,
`TG_PMR = 0x000000F0`, `TG_ABPR = 0x00000000`, `TG_AHPP = 0x00000000`. These are the UNFORCED values, and they
match `vrun24`'s take7b post set value-for-value. `TG_GRP2` bit 12 clear and `TG_CCTLR` bit 1 clear must NOT
be read as a group-gate verdict here: rows T7-2/T7-3/T7-4 all require `TG_RPR == 0xFF`, which did not obtain.
The block is simply the snapshot of a boot where the force retired nothing.

The contrast is what labels it. On the SAME measured pre-state, take6f's UNGATED force DROPPED `RPR`
(`STK_RPR1 = 0xFF`, `exp/20261007-010906/capture-cmd.txt`), while take7c's gated full-word `0x402` store did
not. The gate is provably not the discriminator.

## Bounds (declared, not hidden)

1. **The runner's PASS is the INSTRUMENT GATE, not the force.** `TAKE7C RESULT: PASS` and
   `INSTRUMENT_GATE=HELD` name the slot/bound/health discipline; the force's own witness is `TG_RPR`, and it
   reads `0x00`. The PASS must never be read as "the force landed". Exactly as `vrun24` warned for take7b.
2. **The group reads are ONE instant, not a matched pre/post pair** (`STK_STICKY` covers `RPR` only -
   `bg2.md` sec-5(1)). Here it is moot for the force's own effect (the write landed nothing), but it is why the
   (a)/(c) split cannot be closed from this boot.
3. **The device-side write is one 32-bit `GICC_EOIR` store** of the full IAR word `0x402` (CA `0x40160110`,
   file `0xcb914`), register state, reboot-cleared, no flash, no clock/reset register. The bank pad deposits
   no device write (capstone-confirmed).
4. **The take7c-vs-take7b delta is 26 bytes** at file `0xcb8fc..0xcb919`, ALL INSIDE the 76-byte force pad;
   take7b's blob is byte-UNCHANGED. The capstone disasm of the shipped pad (`take7c.bin` md5
   `60e0af1cb7fb32ea9e58178f754dfbf3`) shows the four deltas: the `TG_PRERPR` store at `0x10b8fc` before the
   `cbnz`, the `TG_PREACT` store at `0x10b90a` inside the fall-through, the `dsb sy` at `0x10b910`, and the
   page-relative `str r0, [r2, #0x10]` at `0x10b918`.
5. **No safety deviation.** The bound held (qbound=64/8 armed before the hide, tripped `n=65`/`n=9`,
   self-disabled `reason=bound`), the literal-path guard was armed before the hide and restored
   unconditionally (`LITERAL_RESTORED_WIFI=1` / `LITERAL_RESTORED_PLAT=1` / `RESTORE_DONE`), and no new pstore
   record was produced. The router is healthy on `mtd14:rootfsb` (2.5.24).

## Verification and health

The cycle (`st_01a11456`-shaped, `run-take7c.sh` over `tools/exp.sh`) ran serial/detached staging
`build/tmp/fw-patched/take7c.bin` (md5 `60e0af1cb7fb32ea9e58178f754dfbf3` = the two pinned regenerations, byte
identical) as `FIRMWARE.bin.omo-pat` and `wifidrv1.ko` (md5 `3f87f1e9fe5ed9666f27d1f784d34535`) as `wifidrv1.ko`,
on the CUSTOM slot `mtd14:rootfsb` (`INSTRUMENT_GATE=HELD`). The `--check-emitted` gate held 10/10 ops in op
order (`EOIR_PAGE 0x40160100 <= 0x2 @ 0xcb8ea`; `EOIR_ID 0x40160110 <= 0x402 @ 0xcb914`). A NEW `boot_id`
`5f52bdef-e9b7-4eb1-839a-372e3ab2de77` (vs the pre-cycle gate `faef074c-...`) proves the reboot was real, and
it is the boot the live probes read back. Health: `WIPHY=2 IFACE=6 CAL2G=1 CAL5G=1`; `OMO_OFF=0 STAGED=0
LOADER=0`; stock blob `0e530b976d5a20e87358671f1a577695` untouched; the vendor pair `hi5622v100_plat` /
`hi5622v100_wifi` owns both IRQ lines again. Two gated self-guarding read-only probes (filesystem/lsmod/iw
only, no devmem) each verified the gate FIRST and read `GATE_HELD`; no forbidden CA was touched.

## The next threads

- **Separate "the store retires nothing" from "the epoch re-forms in the window".** `bg2.md` sec-5(1) asks for
  a matched PRE/POST pair, or a sticky/OR-folded ACTIVE term so the single-sample transient cannot decide
  anything. That is the next cycle's whole reason to exist.
- **Stop spending rank levers until the residual is placed.** The group-enable write (`GICC_CTLR -> 0x3`) and
  the bank quiesce stay deferred (ADDENDUM 26's reasons stand). The rank-1 (id) vs rank-2 (group) question is
  NOT answered: the post set is the unforced state.
- **One boot, one question.** take7c intervenes; keep observation and intervention md5-pinned apart so they
  never share a verdict (ADDENDUM 26).
- **A reboot clears all of it.** The vendor bring-up rewrites the GIC every boot; every lever here is register
  state, boot-scoped, and bound-covered.

## Artifacts

- Boot `build/register-dumps/exp/20261007-031654/` (`capture-cmd.txt`, `run-take7c.log`, `PACKED.txt`,
  `health.txt`, `interp.txt`) carries every take7c value quoted here.
- Verdict `build/register-dumps/diffs/20261007T0320Z-vrun26/verdict.txt` (C1-C7 CONFIRMED, C8 REPORTED as row
  T7C-D).
- Blob `build/tmp/fw-patched/take7c.bin` md5 `60e0af1cb7fb32ea9e58178f754dfbf3` (frozen take7b
  `f5f5309fab9ae76518b9e1368a9ca768`, take7 `b41b0aacfbd46bd8619d71f197431f49`); runner
  `build/tmp/wifidrv1-art/run-take7c.sh`; hook `build/tmp/wifidrv1-art/take7c-capture.hook` md5
  `7e695f93ce23453c83e878116b78ecfe` (the LF-cured hook; the failed first run staged
  `f16458b66ea31c632c1b47d20138e4ec`).
- Design `build/tmp/inta-spec/padstruct.md` (task `st_01a1142a`) and the CRLF diagnosis
  `build/tmp/inta-spec/hookfix.md` + `build/tmp/wifidrv1-art/_t7c-hookfix-verify.txt` (task `st_01a11456`).
- Prior records ADDENDUM 28 (the take7c design) and ADDENDUM 29a (the first take7c boot, capture timed out)
  in this file; ADDENDUM 27 (the take7b boot) and its verdict
  `build/register-dumps/diffs/20261007T0212Z-vrun24/verdict.txt` (D1/D2).
