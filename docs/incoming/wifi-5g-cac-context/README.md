# wifi-5g-cac-context - who writes mctx+0x4A0 (and why the 0x07E7 ARM can run anyway)

**Lane:** shared task **task-229**, 2026-11-26. **HOST-ONLY**: static firmware
decode + fire-recipe delta; no device contact, no fire, no build. Deliverables
only under `opensource/docs/incoming/wifi-5g-cac-context/`.

## The question

The R2 fire (`wifi-5g-dfs-order`) refused the 0x07E7 ARM because
`u32[mctx+0x4A0] = 0x0104fd90` at its pre-gate, identical before/after
moving the 0x01F7. The committed fallback named the 0x0065
CA 0xF593C/0x5F450 path. Which firmware code writes `+0x49C/+0x4A0/+0x4A4/+0x4A8`
(the CAC context), when, and does the resulting gate block the ARM?

## The answer (short)

1. **CA 0x5F450, called from the 0x0065 VAP-start worker at 0x464B6, is the
   writer.** It allocates a 1200-B block, has CA 0x5EAE0 build the radar/detect
   config into block+0x118, and stores block -> +0x4A0, block+0x118 -> +0x49C,
   config length (bench 0xA3) -> u16 +0x4A6. CA 0x5E95C zeroes them again
   (0x0067 VAP-stop; 0x07E7 CLEAR when the flag is 1). Nothing about the
   0x01F7. `+0x4A8 = 0x1` has no direct writer in the image ([U]).
2. **The 0x07E7 ARM with a present context is SILENT SUCCESS, not a refusal.**
   The worker writes the six band-1 SMAC sub-base bit0 registers first, then
   at 0xB5B04 sees `+0x4A0 != 0`, branches to 0xB590C (`movs r0,#0`)
   and returns success, skipping only CA 0xF593C/CA 0x5F450/the +0x4A5 flag.
   The R2 refusal was our own host gate. The legitimate rung is: post the ARM
   after the 0x0065 and grade the six registers (0x2D pattern), CAC flag 0.
3. **The committed R1a "D3 all zero" baseline is wrong** (it includes
   `vap_start_g0=1`): the zero state is *before* the 0x0065, not after
   the ladder. The four words become nonzero inside the 0x0065 post.

## Files

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the full decode: producer/clear, the silent-success branch, mctx[0x0E]/CA 0xA4C0E, the phase model, the stock question, limits |
| [TABLES.md](TABLES.md) | CA maps: word writers/readers, CA 0x5F450/0x5E95C line tables, the 0x0065/0x07E7 branch maps, the stock facts |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the host change spec + the A-rung E1-E8 probes + the C-BASE/C-NOG0 controls + the stock S-rung |
| [tools/fw_cac_ctx_re.py](tools/fw_cac_ctx_re.py) | re-derives every claim from the pinned image (70/70 selftest); prints addresses/ids only |
| [evidence/](evidence/) | selftest.txt, decode.txt, pins.txt |

## Reproduce

    python3 tools/fw_cac_ctx_re.py <FIRMWARE.bin> --selftest   # 70/70 PASS, exit 0
    python3 tools/fw_cac_ctx_re.py <FIRMWARE.bin> --decode

Image `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`,
928,920 B, sha256 `7fc87e20...311b` (CA = file offset + 0x40000); an ARM
objdump is required (the project toolchain one is used by default).

## Grades / limits

[V] = re-derived by the tool; [B] = bench; [I] = inference; [U] = needs a
device read. The only [U]s: the +0x4A8 word writer and the stock-phase
question (both have probes in FIRE-RECIPE). No firmware bytes, no vendor
binaries, no secrets in this lane.

## Related lanes

`../wifi-5g-dfs-set` (0x07E7 frame/worker/gates),
`../wifi-5g-dfs-order` (the R2 refusal and the baseline corrected here),
`../wifi-vap-start-gates` (the 0x0065 worker call map),
`../wifi-5g-160-dfs` (the CAC host flow),
`../wifi-f1-from-capture` (the stock frame counts),
`../wifi-rx-smac-complete` (the band-1 sub-base),
`../wifi-band1-start-send-3` (the 40-B 0x0065 frame),
`../wifi-r5-rebuild` (CHAIN.md, the R5 ladder).
