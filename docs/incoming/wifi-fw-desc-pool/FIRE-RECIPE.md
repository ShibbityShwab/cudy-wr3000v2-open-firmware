# FIRE-RECIPE - lane wifi-fw-desc-pool (task-184)

**This lane fires nothing and touches no device. The Lead fires.** Every rung
below is read-only until R2; R2 is a host-code change and keeps the existing
fire gates (preflight, recovery armed, attendance) from the project runbook.
Never write CA 0x400392f0. Never read the RC misc window
0x10160000..0x1016ffff.

Convention: bar0read phys = **0x40000000 + CPU + 0x6B8000**. All rung R0/R1
reads are side-effect free.

---

## R0 - read-only probe pack on the running 5 GHz AP (no fire)

Purpose: decide, without a rebuild, (a) which descriptor map is loaded,
(b) whether the band-1 start callbacks exist, (c) whether the 5 GHz RF image
was ever written. One pass, ~25 reads, 1-2 pointer steps.

Order (full table: TABLES T5):

1. ctx slot0 (phys 0x40829458) + module-table base (0x4082A504) -> ctx0.
2. gates: ctx0+0x004 (0x407D175C), ctx0+0x440 (0x407D1B98),
   bandobj+0xEC8 (0x40817438). Expect 2,1,1.
3. perlink ctx0+0x4B0 (0x407D1C08), band object ctx0+0x4B8 (0x407D1C10),
   bandobj+0x1A00 (0x40817F70). Expect publish word == perlink.
4. RF chain: P+0x1A00 (0x40830238), P+0xF04 byte (0x4082F73C),
   P+0xF87 byte (0x4082F7BF), then O+0x2D8 -> R, then R+0x3A8/0x3AC/0x3B4/
   0x3CC/0x410/0x414.
5. **new in this lane** - the descriptor map:
   * pool header/count, phys **0x40865C98** (u8 count at +4);
   * pool rec1 word, phys **0x40865CB0** (expect 0x00000201 if boot default);
   * current channel map row B1 = phys **0x407CFAD4 + 4*idx**, byte1 = slot;
   * if slot != 1: pool record word at phys 0x40000000 + 0x46B8000 +
     (0x1ADC98 + 16*slot + 8), i.e. 0x40865C98 + 16*slot.
6. **new in this lane** - the band-1 start hooks:
   * G head, phys **0x4082C7C4** (nonzero when the module is started);
   * G+0x370 MAC-start callback, phys **0x4082CB34**;
   * G+0x374 beacon-start callback, phys **0x4082CB38**.
7. VAP: VAP = u32(ctx0+0x190); VAP+0x58 byte (bit2 = band-1 VAP flag),
   VAP+0x13A u16 (beacon interval).
8. optional differential: band-0 object (0x40814A08) vs band-1 object
   (0x40816570), same offsets, working band as reference.

Verdicts:

| readback | verdict |
| --- | --- |
| R == 0 / R+0x3AC == 0 | C1: RF programme never ran -> R1/R2 |
| pool count == 2, row byte1 == 1, rec word == 0x201 | boot-default map; R+0x410 bit0 = 0 by design; C2 excluded -> C4/C5/C1 |
| pool count > 2 or row byte1 >= 2 | a host 0xF7 table was installed; record word decides whether 0x00800000 should be set (C2 meaning) |
| G+0x370 or G+0x374 == 0, or G head == 0 | C5a: band-1 MAC/beacon start hook not installed -> report, R4 |
| callbacks nonzero, VAP+0x58 bit2 == 0 | C5b: band-1 VAP never flagged -> report deep-copy of ctx/VAP |
| callbacks nonzero, bit2 == 1, VAP+0x13A == 0 | C5b/C4: beacon config missing -> check the 0x0127 body field |

R0 never writes. Abort on any exception/page anomaly per runbook.

## R1 - zero rebuild: re-post the 0x0124 frame once after the five-frame order

Motivation: the RF programme is a silent early-return chain; if the gate bytes
were 0 **when** 0x0124 ran, the block never programmed and nothing re-runs it.
Re-posting the same 0x0124 body after the full order (without reloading the
vendor module, or with a second post after reload) tests C1 with no code
change.

Decision: if R+0x3AC becomes the ch36-64 image (0x03E6138A) or the ch100+ image
(0x056B0597) and the beacon appears -> C1 confirmed; make R2 permanent.
If nothing changes and R0 showed C5a (NULL callback), stop and report.

## R2 - small host patch (after C1 evidence)

After the five-frame order: (a) send the 0x0124 channel frame again (same
body), once; (b) optionally poll R+0x3AC through the read-only helper and only
report/log the result (do not start a write loop). Keep the fire gated by
tools/fire-preflight.sh + recovery armed + attendance. Fire ch40 first (short
image) then ch100 (inline image) to compare variants.

Do **not** loop 0x0124 more than twice per session; each post exercises the
descriptor predicate/latch path and there is no evidence a third post helps.

## R3 - only if R0 shows a host 0xF7 map with clear valid bits

If the map has a host slot (count > 2, row byte1 >= 2) and the record word for
the current 5 GHz channel has byte2 bit7 == 0 while the 2.4G counterpart has
it set, the descriptor *source* (host) is wrong. The fix belongs in the host
descriptor-table builder (the 0x00F7 body source), not in a blind frame
re-order. **Do not fabricate an 0x00F7 body on the bench before its exact body
layout is confirmed against the vendor driver**: the worker memcpy_s copies
8 + body[4]*16 bytes into the pool with destMax 0x148 (idx <= 20 enforced), so
a malformed body fails closed but still writes the pool for idx <= 20.

## R4 - C5 branch: report, do not patch blind

If R0 shows G head == 0 or a NULL start callback (C5a), or CA 0x971D8 never
reaches the hook for band 1 (C5b), there is **no host frame identified** that
installs the callback or flags the band-1 VAP. Capture the readback pack, the
dmesg tail and the exact fire order, then stop. Patching the firmware image or
writing G+0x370/G+0x374 is out of scope for a read-only lane and unsafe.

## Abort rules (unchanged)

* page 0x86807fef / state 1 / mask 0 exception -> power-cycle, no retry.
* never write CA 0x400392f0; never read 0x10160000..0x1016ffff.
* no blind flash writes; recovery door armed before any fire; human present.
* one variable per fire; snapshot calibration/state before the experiment.
