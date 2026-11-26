# TABLES - wifi-5g-cac-context (the +0x49C/+0x4A0/+0x4A4/+0x4A8 writer map)

All CAs are of the pinned 928,920 B FIRMWARE.bin (sha256 `7fc87e20...311b`);
CA = file offset + 0x40000. [V] = re-derived by
[tools/fw_cac_ctx_re.py](tools/fw_cac_ctx_re.py) (70/70); [B] = bench datum.

## T1. The four words: writers, readers, effects

| word | writer(s) | site(s) | value | reader / role |
| --- | --- | --- | --- | --- |
| `u32[mctx+0x49C]` | CA 0x5F450 (producer) | 0x5F552 | 1200-B block + 0x118 (the config payload) | CA 0x5E95C clear path; 0x07E7 ARM CLEAR branch |
| `u32[mctx+0x4A0]` | CA 0x5F450 (producer) | 0x5F54E | the 1200-B block base | **0x07E7 ARM full-arm gate** (0xB5B04); 0x0065 realloc check (0x4648E); CA 0x5E95C clear (0x5E980) |
| `u16[mctx+0x4A6]` (low half of the bench `+0x4A4` word) | CA 0x5F450 | 0x5F54A | CA 0x5EAE0 config length (<= 920 B; bench 0xA3) | CA 0x5F254 / CA 0x623xx config use |
| `u8[mctx+0x4A5]` (byte 1 of the `+0x4A4` word) | **0x07E7 worker only** | 0xB5B3C (1) / 0xB5B42 (0) | CAC-active flag | 0x07E7 ARM gate 0xB5B10; CLEAR gate 0xB5B4E |
| `u32[mctx+0x4A8]` | **none found [U]** | - | bench 0x1 | no reader in the CAC paths; likely a neighbour field |

Bench cross-check [B] (R2): +0x49C 0x0104FEA8 = block+0x118, +0x4A0 0x0104FD90 =
block, +0x4A4 0x00A30000 = 0xA3 length, +0x4A5 = 0 (flag), +0x4A8 = 1 [U].

## T2. The producer CA 0x5F450 (line map, [V])

| CA | effect |
| ---: | --- |
| 0x5F450 | entry, r0 = mctx |
| 0x5F458 | gate: `u32[mctx+0x4B0]` (per-link) != 0, else return 100 |
| 0x5F462/0x5F466 | load +0x4A0 / +0x49C |
| 0x5F4E2/0x5F4F0 | alloc 1200 B (CA 0xBF128, pool 3346, line 312, type 5) |
| 0x5F510/0x5F518 | memset 1200 B (CA 0xC1E0C) |
| 0x5F51C | r6 = block + 0x118 (the +0x49C payload) |
| 0x5F4AC | CA 0x5EAE0(mctx, block+0x118, &u16len) builds the radar/detect config |
| 0x5F4B0/0x5F4B6 | len check `<= 920` (else error log) |
| **0x5F54A** | `strh u16[mctx+0x4A6] = len` |
| **0x5F54E** | `str u32[mctx+0x4A0] = block` |
| **0x5F552** | `str u32[mctx+0x49C] = block+0x118` |
| 0x5F556/0x5F558 | CA 0xF5A18(helper, u16[vap+0x10]) - per-channel detect field |
| 0x5F564 | CA 0x5F254(mctx, block) - config/message build-and-send |
| 0x5F576 | CA 0xB524C(perlink, block, u8[helper+2]) |
| 0x5F57E | CA 0xF50B4(helper) |
| 0x5F582 | return 0 |

Fail paths: mixed +0x4A0/+0x49C -> error 35629 (0x5F522); helper/vap failures ->
error logs; the +0x0065 worker surfaces a nonzero return.

## T3. The clear CA 0x5E95C (line map, [V])

| CA | effect |
| ---: | --- |
| 0x5E95E | load +0x4A0; if 0 -> log, still clear the helper bits |
| 0x5E96A | CA 0x95CB0(`u32[mctx+0x4B8]`) |
| 0x5E972 | CA 0xF4FD8(`u32[mctx+0x4AC]`) - radar object |
| **0x5E97C** | `str 0 -> u32[mctx+0x49C]` |
| **0x5E980** | `str 0 -> u32[mctx+0x4A0]` |
| 0x5E984.. | clear the detect bitmap word selected by the per-link/module index |

Callers: 0x0067 VAP-stop worker (0x46030), 0x0065 realloc path (0x464A8), and
0x07E7 CLEAR (0xB5B62).

## T4. The 0x0065 worker CA 0x46394: the CAC-prep lines [V]

| CA | effect |
| ---: | --- |
| 0x463AE | resolve module ctx (CA 0x961AE); fail -> 100 |
| 0x463D4 | band object = `u32[mctx+0x4B8]`; 0 -> 100 |
| 0x463EC..0x463F8 | body[2] -> mctx[0x11], body[3] -> mctx[0x12], body[4..7] -> u32 mctx[0x444] |
| 0x463FC | CA 0xD4048(mctx, body[1]) |
| 0x46404 | CA 0x94518(mctx) MAC VAP start |
| 0x46408..0x4641A | type==2 (AP): CA 0xBD114; **mctx[0x0E] = 1** |
| 0x46422..0x46430 | CA 0xD41AC; `[bandobj+0x1A00]` read (log only) |
| 0x46434..0x4644A | CA 0xF593C(`u32[mctx+0x4AC]`, type) |
| 0x46450..0x4648A | per-link/vap setup; `vap = mctx[0x190]` deref at 0x46470 |
| 0x4648E..0x464B0 | if `u32[mctx+0x4A0] != 0`: log + CA 0x5E95C + CA 0x971D8 |
| **0x464B6** | CA 0x5F450(mctx) - the producer |
| 0x464BE/0x4672E | success: CA 0x947E4, CA 0x8E51A, CA 0x4E4DC(...,19), CA 0x4C3FC(bandobj,1) |

AP-only: for type != 2 the 0x46460 dispatch takes 0x46734 and returns without
CA 0x5F450 - no CAC context for a non-AP VAP. No body field gates the call.

## T5. The 0x07E7 worker CA 0xB58FC: the ARM branch map [V]

| CA | effect |
| ---: | --- |
| 0xB5900/0xB5904 | gate 1: hcc ctx byte +17 == 1, else silent return 0 |
| 0xB5914/0xB592E | gate 2: `u32[hcc+0x4B0] != 0`, else log + return 100 |
| 0xB5936 | gate 3: CA 0xA4C0E scan, else log + return 100 |
| 0xB594A | body[0] = u8(hdr+24); 0 -> CLEAR branch 0xB5A34 |
| **0xB59C6..0xB5A18** | **the six band-1 SMAC sub-base bit0 writes** (+8, +4, +0, +A8, +A4, +A0 -> {1,0,1,1,0,1} = 0x2D) |
| 0xB5A1A/0xB5A24 | resolve mctx; fail -> log + return 0 |
| 0xB5AF0 | log line (type, flag) |
| 0xB5B04 | load `u32[mctx+0x4A0]` |
| **0xB5B0C** | **!= 0 -> 0xB590C = `movs r0,#0` = SILENT SUCCESS** |
| 0xB5B10..0xB5B16 | flag read; != 0 -> silent success |
| 0xB5B20/0xB5B26 | CA 0xF593C + CA 0x5F450 (full arm only) |
| 0xB5B3A/0xB5B3C | success -> `u8[mctx+0x4A5] = 1` |
| 0xB5B48..0xB5B62 | CLEAR: needs b4a0 != 0 and flag == 1, then CA 0xF58D8 + CA 0x5E95C |
| 0xB5B42 | flag -> 0 |

## T6. The started-module scan CA 0xA4C0E (gate 3) [V]

| CA | effect |
| ---: | --- |
| 0xA4C1C | count = u8[list+18] |
| 0xA4C2C | resolve each id: CA 0xBC524(u8 list[i]) |
| 0xA4C42..0xA4C4A | `u8[ctx+0x0E]-1 <= 2` (state 1..3) -> count++ |
| 0xA4C24 | return the count; gate 3 = count != 0 |

Not band-filtered; any started module ctx in the per-link list satisfies it.
Single-VAP fresh ladder: only the 0x0065 sets +0x0E (AP -> 1).

## T7. The phase model (what the probes must see) [V] + [U]

| phase | +0x49C | +0x4A0 | +0x4A4 | +0x4A8 |
| --- | --- | --- | --- | --- |
| fresh mctx (after vap_add/ap_chan/dfs_mode) | 0 | 0 | 0 | 0 |
| after the 0x0065 (AP) | block+0x118 | block | 0x00A3xxxx class | [U] (bench 1) |
| after 0x07E7{1} with the context present | unchanged | unchanged | unchanged | unchanged |
| after 0x07E7{0} (flag 1) | 0 | 0 | flag 0 | [U] |
| after the 0x0067 VAP stop | 0 | 0 | 0 | [U] |

## T8. Stock reference facts used (item 4)

| fact | value | grade |
| --- | --- | --- |
| stock mctx four words at AP-up | all 0 | [B] 2026-11-26 23:00 |
| stock band-1 SMAC sub-base +0 bit0, earlier same boot | 1 (armed) | [B] 2026-11-26 21:00 |
| stock capture frames (210 s boot) | 0x0065 x4, 0x07E7 x3, 0x01F7 x1, 0x0124 x3 | [V*] wifi-f1-from-capture |
| stock AP order | 0x0124 -> 0x0005 -> 0x006D -> 0x0127 -> 0x0065 | [V*] wifi-vap-start-gates / wifi-re-report |
| DFS AP flow | 0x07E7{1} -> CAC 60 s -> 0x07E7{0} -> start all BSS (emits the 0x0065) | [V*] wifi-5g-160-dfs |
