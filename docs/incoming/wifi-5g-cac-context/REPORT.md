# REPORT - the CAC context (+0x4A0) writer: the 0x0065 VAP-start path

**Lane:** `wifi-5g-cac-context` (shared task **task-229**), 2026-11-26.
**HOST-ONLY**: static firmware decode + fire-recipe delta; no device contact, no
fire, no binary build. Grades: **[V]** re-derived by
[tools/fw_cac_ctx_re.py](tools/fw_cac_ctx_re.py) (70/70 selftest),
**[V\*]** committed earlier lane/bench, **[B]** bench datum, **[I]** inference,
**[U]** needs a device read. All firmware addresses are CA = file offset +
0x40000 of the pinned 928,920 B image (sha256 `7fc87e20...311b`).

---

## 0. TL;DR

1. **The four words are written by CA 0x5F450, called from the 0x0065 VAP-start
   worker at 0x464B6 [V].** CA 0x5F450 allocates a 1200-B block, has CA 0x5EAE0
   build the radar-detect config into block+0x118, and publishes
   `u32[mctx+0x4A0] = block`, `u32[mctx+0x49C] = block+0x118`, `u16[mctx+0x4A6] = config length`.
   The bench word `+0x4A4 = 0x00A30000` is exactly that u16 at +0x4A6 =
   **0xA3** (the built config's length). The pair that zeroes them again is
   CA 0x5E95C (the 0x0067 VAP-stop worker and the 0x0065/0x07E7 clear paths).
2. **The 0x0065 AP branch is therefore the CAC-prep producer**: it sets
   `mctx[0x0E]=1` @0x4641A, calls CA 0xF593C @0x4643C, frees a stale
   context with CA 0x5E95C when `mctx[0x4A0] != 0` @0x464A8, then calls
   CA 0x5F450. **The 0x01F7 is not involved** (matches the R2 fire).
3. **The decisive correction: the 0x07E7 ARM does NOT refuse when
   `mctx+0x4A0 != 0` [V].** The worker writes the six band-1 SMAC
   sub-base bit0 registers first (0xB59C6..0xB5A18), then at 0xB5B04 tests
   +0x4A0; when nonzero it branches to **0xB590C = `movs r0,#0` = silent
   **success** - it only skips CA 0xF593C, CA 0x5F450 and the +0x4A5 flag set.
   The R2 refusal was **our driver's own fail-closed pre-gate**, not the
   firmware. Posting the ARM with the 0x0065-allocated context is a
   **legitimate, firmware-designed sequence** (the non-DFS / no-CAC variant).
4. **So the repair is host-side, small and safe**: drop the +0x4A0 pre-gate to
   a warning, post the 29-B ARM after the 0x0065 as planned, and pass on
   `regs_pattern = 0x2D` + CAC flag 0. The six registers (the R5 fire's
   missing band-1 sub-base writer) are written by this path.
5. **The committed R1a baseline is wrong**: R1a includes `vap_start_g0=1`, so
   D3 must be **NONZERO** after the ladder; the "all zero" stock state is a
   pre-0x0065 / post-CAC-clear state, not the same phase. R1b's "the 0x01F7
   is the CAC starter" inference cannot work: the 0x0065 alone populates D3.
6. **[U]** the 4th word `+0x4A8 = 0x1` has **no direct writer in the
   image** (only two unrelated immediate-1192 stores exist, 0x5B446 and
   0x97ADA). Treat +0x4A8 as an observation, never a gate.

---

## 1. The bench question

The R2 fire (module `a5ec150c`, the reordered dfs_set) printed

    dfs_set pre: mctx=... band=1 state=1 ec8=1 b4a0=0x0104fd90 b4ac=0x010067a8
                 cac=0 regs_pattern=0x00 (u32[mctx+0x4A0] gate is nonzero) -> REFUSED (-16)

identical before/after moving the 0x01F7 late. The stock reference boot reads
`+0x49C/+0x4A0/+0x4A4/+0x4A8 = 0`; ours are the firmware-RAM block
pointer + block+0x118 + the 0xA3 class word + 1. The committed fallback named
the 0x0065 CA 0xF593C/0x5F450 path as the next hypothesis. This lane had to
answer: **who writes the four words, when, and why does the resulting gate
block - or does it?**

## 2. Who writes the words [V]

The 0x0065 worker CA 0x46394 (table A row 2) AP branch (`mctx[4]==2`) ends in:

| CA | effect |
| ---: | --- |
| 0x46418/0x4641A | `mctx[0x0E] = 1` (the state the 0x07E7 gate 3 scans) |
| 0x4643C | CA 0xF593C(`u32[mctx+0x4AC]`, type) - sets the radar-detect bitmap bit |
| 0x4648E | read `u32[mctx+0x4A0]`; if != 0: log + CA 0x5E95C (free) @0x464A8 + CA 0x971D8 |
| **0x464B6** | **CA 0x5F450(mctx)** - the four-word producer |
| 0x4672E/0x4674A | success continuation: CA 0x947E4, CA 0x8E51A, CA 0x4E4DC(...,19), CA 0x4C3FC(bandobj,1) |

CA 0x5F450 (`r0 = mctx`) exactly:

1. gate `u32[mctx+0x4B0]` (per-link) != 0, else return 100;
2. read +0x4A0 / +0x49C; both 0 -> **alloc 1200 B** (CA 0xBF128) + memset
   (CA 0xC1E0C), r6 = block+0x118; both nonzero -> reuse; mixed -> error 35629;
3. `vap = u32[mctx+0x190]`; **CA 0x5EAE0(mctx, block+0x118, &len)**
   builds the radar/CAC config (len capped at 920 B; our bench value 0xA3);
4. **strh u16[mctx+0x4A6] = len** @0x5F54A; **str u32[mctx+0x4A0] = block**
   @0x5F54E; **str u32[mctx+0x49C] = block+0x118** @0x5F552;
5. CA 0xF5A18(helper, vap[0x10]); CA 0x5F254(mctx, block); CA 0xB524C(perlink,
   block, helper[2]); CA 0xF50B4(helper); return 0.

The bench values match byte-for-byte: +0x4A0 = 0x0104FD90 (block),
+0x49C = 0x0104FEA8 = block+0x118, +0x4A4 = 0x00A30000 = u16[+0x4A6] = 0xA3.
CA 0x5E95C is the clear: `ldr r1,[mctx+0x4A0]`; helper CA 0x95CB0 +
CA 0xF4FD8 on +0x4B8/+0x4AC; then `str 0 -> +0x49C/+0x4A0` and the
detect-bitmap bit clear. Callers (all [V], full closure):

| helper | callers |
| --- | --- |
| CA 0x5F450 | **0x464B6 (0x0065)** and **0xB5B26 (0x07E7 ARM)** - only these two |
| CA 0x5E95C | 0x46030 (0x0067 VAP-stop worker, table row 4), 0x464A8 (0x0065), 0xB5B62 (0x07E7 CLEAR) |
| CA 0xF593C | 0x4643C (0x0065), 0x64844, 0xB5B20 (0x07E7 ARM) |

## 3. The 0x07E7 ARM against a present context is a SILENT SUCCESS [V]

The ARM body (0xB5996..0xB5A18) writes the six band-1 SMAC sub-base bit0
registers (+0/+4/+8/+A0/+A4/+A8 -> pattern {1,0,1,1,0,1} = 0x2D). Then:

    0xB5AF0  log line 1516 (type, flag)
    0xB5B04  ldr.w r3, [r5, #1184]      ; u32[mctx+0x4A0]
    0xB5B08  cbz   r4, 0xB5B48          ; body 0 = CLEAR branch
    0xB5B0A  cmp   r3, #0
    0xB5B0C  bne.w 0xB590C              ; <-- context present
    0xB590C  movs  r0, #0               ; <-- RETURN 0 = success
    0xB5B10..0xB5B3C  flag read/calls/flag=1 (only when b4a0==0 and flag==0)
    CLEAR 0xB5B48: b4a0==0 or flag!=1 -> 0xB590C (silent success, no clear)

So the firmware's own contract for "ARM with an existing context" is:
**write the six registers, skip the CAC-context/flag side effects, return 0**.
That is exactly the non-DFS/no-CAC behaviour a ch40 5 GHz AP wants. The R2
`REFUSED (-16)` never reached the firmware: our driver's pre-gate stopped
the 29-B frame client-side. The fail-closed gate was correct for its *intent*
(never mutate an unexpected CAC state) but wrong for this firmware: the state
is the 0x0065's expected output and the worker handles it.

## 4. mctx[0x0E] and the started-module scan CA 0xA4C0E [V]

Gate 3 of the 0x07E7 worker calls CA 0xA4C0E(`u32[perlink+0xE20]`), which
iterates the module-id list: count = u8[list+18]; resolve each id with
CA 0xBC524; for each ctx count `u8[ctx+0x0E] in 1..3`; gate 3 passes iff
the count is nonzero. **It is not filtered by band and not by our mctx** - any
started module context in the per-link list qualifies. In a single-VAP fresh
ladder only the 0x0065 sets +0x0E (1 for AP type 2, 4 for type 1, no store for
other types). Consequence: posting the 0x07E7 **before** the 0x0065 is a
gate-3 no-op in our ladder (returns 100), no matter what the body says - the
committed forced slot `0x0065 -> 0x07E7 -> 0x01F7` remains correct.

## 5. When each word becomes nonzero (the ladder) - and the R1a correction

`+0x49C`, `+0x4A0`, `u16[+0x4A6]` become nonzero at the **0x0065 post**, inside
the worker call CA 0x5F450, before the worker returns. They are zero:
* on a fresh mctx (after vap_add/desc_table/dfs_mode/ap_chan/ap_cfg), and
* after a successful 0x07E7 CLEAR (flag 1 -> CA 0x5E95C zeroes them), and
* after the 0x0067 VAP-stop worker.

Therefore the committed `wifi-5g-dfs-order` FIRE-RECIPE R1a ("vap_start_g0=1,
D3 all zero") is **wrong**: D3 must be nonzero (the 0x0104fd90 class). The
true zero baseline needs `vap_start_g0=0` or a probe point before the 0x0065.
R1b's expectation (D3 nonzero with the late 0x01F7) would have been produced by
the 0x0065 alone, so R1b as written cannot measure the 0x01F7's causality.

## 6. Why the gate blocked, and the legitimate sequences

The full-arm branch requires `u32[mctx+0x4A0]==0 && u8[mctx+0x4A5]==0`. The call
site is unconditional in the 0x0065 AP branch, so no 0x0065 body field (body[0]
arg, body[1] vap state, body[2] band, body[3] field, body[4..7] vap word) and
no g0-id1 field B can skip the CAC-prep for an AP; a non-AP type skips it but
is not our VAP. Valid sequences:

| # | sequence | verdict |
| ---: | --- | --- |
| S1 | `0x0065 -> 0x07E7{1}` (b4a0 != 0) | **RECOMMENDED** for ch40/non-DFS: six SMAC regs armed, flag stays 0, worker returns 0. Needs the host pre-gate relaxed. |
| S2 | `0x07E7{1}`  before the 0x0065 with a started module ctx (vendor DFS CAC order) | correct for a DFS CAC, but gate 3 needs another started module (the stock's 2.4 GHz VAP); not reachable in our single-VAP ladder, and our ch40 target does not need CAC. |
| S3 | keep the refusal | the current R2 behaviour: loses the only firmware writer of the band-1 SMAC sub-base. |
| S4 | 0x07E7{0} CLEAR after S1 | no-op (flag 0) - harmless, not needed. |

## 7. The stock question (item 4)

Known [B]/[V\*]: the stock 5 GHz AP is up with +0x49C..+0x4A8 = 0 (23:00 read);
the same reference boot earlier read the band-1 sub-base bit0 = 1 (armed); the
stock sends g0-id1 key 0x0065 x4 and 0x07E7 x3 in a 210 s boot capture; the
stock AP order is 0x0124 -> 0x0005 -> 0x006D -> 0x0127 -> 0x0065; a DFS AP
start arms CAC (0x07E7{1}), and on CAC completion sends 0x07E7{0} then starts
all BSS (which emits the 0x0065). What the static decode cannot settle: if the
stock's 5 GHz VAP start is the g0 0x0065, CA 0x5F450 *should* leave +0x4A0
nonzero at AP-up. Candidate resolutions, all [U]: (i) the zero read is a
phase between a 0x07E7 CLEAR and the VAP's 0x0065 (or after a VAP stop);
(ii) CA 0x5F450's error path returned nonzero on that boot; (iii) the stock's
5 GHz BSS start did not use the g0 0x0065 for that interface. A stock-side
read-only rung (FIRE-RECIPE S-rung) settles it; it is not needed for the
host fix. **On the non-DFS question:** vendor-wise the 0x07E7 is the CAC /
radar-detect arm, not a VAP-start requirement - a non-DFS AP does not need a
CAC; but its firmware side effect (the six band-1 SMAC sub-base bit0s + the
per-band detect context) is exactly the piece our ladder lacks, and the 21:00
bench already showed a manual sub-base flip is not sufficient for the beacon
(the TBTT/SMAC init is a separate step). So S1 is worth firing as the proper
writer rung, with the beacon graded separately.

## 8. Probes and fire-recipe delta

See [FIRE-RECIPE.md](FIRE-RECIPE.md): the **A-rung** (ARM-with-context, the
host pre-gate relaxed) with E1..E7 phase probes, and the **corrected R1a**
baseline. The recipe also lists the exact host source change for the module
that implements it (this lane is docs-only; the build lane applies it).

## 9. Limits / honest unknowns

* [U] **+0x4A8 = 0x1**: no direct writer exists in the image (only the
  unrelated immediate-1192 stores at 0x5B446/0x97ADA; neither is in the
  CAC cluster 0x5E000..0x5F700 nor takes the module ctx). Do not gate on it.
* [U] the stock phase question of section 7.
* [I] the exact meaning of the CA 0x5EAE0 config's length field and of the
  F593C bitmap beyond "radar detect"; this decode is address/flow proof only.
* The ARM-with-context path skips CA 0xF593C on the frame's pass; the 0x0065
  already called F593C, so the bitmap is set once. If a future boot lacks the
  0x0065 F593C, arm order matters again.
* The frame bytes, the 29-B builder and the sub-base pattern are unchanged
  from `../wifi-5g-dfs-set`; this lane changes the host gate policy and
  the expected post-state, not the wire.

## 10. Reproduce

    python3 tools/fw_cac_ctx_re.py <FIRMWARE.bin> --selftest   # 70/70, exit 0
    python3 tools/fw_cac_ctx_re.py <FIRMWARE.bin> --decode     # the CA map

Needs the pinned image (sha256 `7fc87e20...311b`) and an ARM objdump; the
tool prints only re-derived addresses/ids/lengths (no vendor bytes). Evidence:
[evidence/selftest.txt](evidence/selftest.txt),
[evidence/decode.txt](evidence/decode.txt),
[evidence/pins.txt](evidence/pins.txt).

## 11. Citations

* `mem-entries.md` 2026-11-26 23:00 (the R2 refusal, the four-word bench
  values, the stock zero) and 2026-11-27 00:00 (the R2 reorder no-effect) [B].
* `../wifi-5g-dfs-set` REPORT 2.2-2.4 (the 0x07E7 worker decode this lane
  extended with the branch-target semantics), FIRE-RECIPE [V].
* `../wifi-5g-dfs-order` REPORT sec. 6/7, FIRE-RECIPE R1a/R1b/R2 (the
  baseline this lane corrects) [V].
* `../wifi-vap-start-gates` REPORT sec. 2/4 (the 0x0065 worker call map and
  the stock AP order) [V\*].
* `../wifi-5g-160-dfs` REPORT-DFS sec. 1/2 (the CAC host flow, CAC times,
  the DFS arm-before-BSS order) [V\*].
* `../wifi-f1-from-capture` FAMILY-TABLE (stock sends 0x0065 x4, 0x07E7 x3)
  [V\*].
