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
