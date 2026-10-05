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
