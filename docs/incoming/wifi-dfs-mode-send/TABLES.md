# TABLES - lane wifi-dfs-mode-send (task-190)

Grades: **[V]** decoded/re-derived by a committed lane (cited); **[V*]**
measured by a committed lane/Lead; **[I]** inference; **[U]** needs a device
read. This lane is HOST-ONLY: it wrote nothing to the router, module or
firmware. No vendor binary, firmware byte, calibration value, MAC or secret
appears here - the only bytes are **host-built protocol bytes** (our own
0x01F7 frame), as in the sibling lanes.

## T1. Conventions and safety

| item | value |
| --- | --- |
| firmware image (source of the decode) | recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin, 928,920 B, sha256 `7fc87e20...c3ef311b` |
| firmware CPU address | **CPU = file offset + 0x40000** |
| host BAR0 offset | **BAR0 = CPU + 0x6B8000** |
| host physical (read path) | **phys = 0x40000000 + CPU + 0x6B8000 = CPU + 0x46B8000** |
| forbidden | never read the RC misc window 0x10160000..0x1016ffff; never write CA 0x400392F0 |
| this lane | host-only implementation + build + host tests; no bench contact |

## T2. The H2D 0x01F7 wire frame (29 B) [V]

HCC **group 0, message id 1** (the config-event carrier); key **0x01F7** at
header + 20, body length u16 at +22, body at +24, inherited 4-byte tail;
total = 12 + 8 + 2 + 2 + 1 + 4 = **29** (0x001D).  Same carrier as the
sibling 0x00F7 frame (wifi-fw-desc-table TABLES T2); generic builder total
= len + 28.

| off | len | field | value |
| ---: | ---: | --- | --- |
| +0x00 | 1 | group | 0x00 |
| +0x01 | 1 | bid: alloc nibble 1, dst high nibble | 0x01 |
| +0x02 | 1 | fa | 0x00 |
| +0x03 | 1 | fb (vendor-wire value of the g0-id1 family) | **0x04** |
| +0x04 | 2 | total length u16 | **0x001D** (29) |
| +0x06 | 2 | message id u16 | **0x0001** |
| +0x08 | 1 | fc | 0x00 |
| +0x09 | 1 | dst | 0x00 |
| +0x0A | 2 | tag | **0x5A5A** |
| +0x0C | 8 | token/preamble | `00 00 00 00 01 00 00 00` |
| +0x14 | 2 | **config key u16** | **0x01F7** |
| +0x16 | 2 | **body length u16** | **0x0001** |
| +0x18 | 1 | **body[0] = mode** | **0x03** (mode 3: the value CA 0xA2FA2 requires) |
| +0x19 | 4 | inherited tail | `FF FF FF FF` |

Frame (29 B) built by `src/hi5622_dfs_mode.c` and re-derived by
`tools/dfs_mode_send_test.py`:

```
000100041d00010000005a5a 0000000001000000 f701 0100 03 ffffffff
```

Hex: `000100041d00010000005a5a0000000001000000f701010003ffffffff`

SR node word1 = `(total << 16) | 0x6D2B` = **0x001D6D2B**; the existing
`hi5622_frame_post()` machinery derives it, no new transport code.

## T3. How the frame was derived (not guessed) [V]

| step | fact | source |
| ---: | --- | --- |
| 1 | the wire carrier is 12 B HCC header + 8 B token + u16 key + u16 len + body + 4 B inherited tail; total = len + 28 | wifi-fw-desc-table TABLES T2 + `h2d_build_g0_cfg_event` (`total = len + 0x10 + 12`) |
| 2 | the stock 0x01F7 emitter is hmac_config_set_country_for_dfs (.text+0x5762C): `movw r1,#0x01F7` at +0x5763C, then `hmac_config_send_event(ctx, key, len, data)`; the constructor writes key at hdr+20, len at +22, body at +24 | wifi-fw-rf-descriptor REPORT sec 4.3; wifi-fw-desc-table REPORT sec 2.2 |
| 3 | key 0x01F7 = firmware table A row 93 (CA 0x10C524) -> worker CA 0xB5B9C; the worker reads body[0] as the mode (1..5) and writes `P+0xF04` + the mode's RF image | wifi-fw-rf-descriptor REPORT sec 4.2 |
| 4 | mode 3 is what the ch36..64 RF programme CA 0xA2FA2 requires | wifi-fw-rf-descriptor REPORT sec 2/4 |
| 5 | header bytes (including fb 0x04) and token are inherited from the sibling 0x00F7 frame / the captured g0-id1 family; only total, key, len and body change | wifi-fw-desc-table TABLES T2; phase-38 capture (wifi-irq-unmask REPORT-PD4 sec 6.5) |
| 6 | total = 12 + 8 + 2 + 2 + 1 + 4 = 29 = 0x001D; the hdr total field must equal the wire length (validator) | `h2d_frames.c` decode/encode; model selftest |

Note: the task brief estimated "~23 B"; the carrier geometry gives exactly
**29 B**.  The 29 is asserted four times (model constant, C golden array,
Python derivation, source contract).

## T4. The fail-closed `dfs_mode` matrix (implementation contract) [V]

| dfs_mode | beacon_static | band | result |
| ---: | ---: | ---: | --- |
| 0 (default) | any | any | **no 0x01F7 frame**; ladder byte-identical to the desc-table-send base |
| 1 | 1 | 1 | **post the 29-B frame** after the 0x00F7 step, before ap_chan |
| 1 | 1 | 0 | clean no-post (band-1 AP start only) |
| 1 | 0 | any | **refused, -EINVAL, before any frame** ("needs the beacon_static=1 vendor order") |
| >1 | any | any | **refused, -EINVAL, before any frame** ("not a valid DFS-mode parameter") |

Builder refusals (`hi5622_dfs_mode_build`): unarmed plan -> -EINVAL; mode != 3
-> -EINVAL; cap < 29 -> -ENOSPC; NULL -> -EINVAL.  `hi5622_dfs_mode_plan`
also refuses band > 1 and a NULL out.  Worker precheck
(`hi5622_dfs_mode_precheck`): 0 only when published per-link != 0 and attach
flag == 1; -EAGAIN otherwise (read-only verdict for the dmesg line).

## T5. Integration and order contract [V]

| item | location | contract |
| --- | --- | --- |
| module param | hi5622_scan.c `dfs_mode` (uint, 0444, default **0**) | one meaning per load; read-only |
| fail-closed gate | start_ap, before any frame (after the desc_table gate) | >1 and 1-without-beacon_static refused |
| plan step | hi5622_scan_logic.{c,h} `HI5622_BEACON_STEP_DFS_MODE` | plan order: vap_add -> desc_table -> **dfs_mode** -> ap_chan -> ap_ssid -> ap_cfg -> ap_vap_start |
| frame post | start_ap beacon_static branch, **after** the 0x00F7 post, **before** the ap_chan build/post | `hi5622_frame_post(ctx, wire, len, "dfs_mode", &out0)` |
| read-only evidence | right after the post | published per-link (bandobj+0x1A00) + band attach flag (bandobj+0xD70) |
| sysfs | `hi5622_scan_status` -> `cfg80211_status` | `dfs_mode: param=N (mode) beacon_static=M last_rc=R posted=P refused=F` |

Why that placement (the stock order is [I]): country/regdomain init ->
0x00F7 descriptor table -> **0x01F7 mode** -> 0x0124 channel/re-post.  The
0x00F7 worker installs the pool; the 0x01F7 worker writes the band object's
mode byte `P+0xF04`; the 0x0124 worker's band-1 RF programme CA 0xA2FA2
evaluates the mode byte at post time - so the 0x01F7 step must precede the
0x0124 post.  Observable order in the beacon_static ladder:
**vap_add 0x0064 -> desc_table 0x00F7 -> dfs_mode 0x01F7 -> ap_chan 0x0124 ->
ap_ssid / ap_cfg 0x0127 -> ap_vap_start 0x0065**.

## T6. Build artifact and pins

| item | value |
| --- | --- |
| pinned base tree | /tmp/r1-band-build (beacon5g + the r1-band one-field fix); provider sha256 `c306e75f...8b79ab` |
| pinned base module | /tmp/r1-band-artifact/hi5622_wifi.ko, **356,612 B** sha256 `e8b17f72...4abd4`, 59 params |
| sibling 0x00F7 base | `../wifi-desc-table-send/patches/0001-desc-table-send.patch` sha256 `9c42076f4f5faecf352f6aecf5d6ba8b88edb73a454e26872ec543233cdfce2f`; sibling artifact 360,448 B sha `aac6177c...e800e3`, 60 params |
| this lane patch | patches/0001-dfs-mode-send.patch (6 files: 2 new, 4 edits); src/hi5622_dfs_mode.c sha `977f285c...d13892`, .h sha `c25fa76a...2b559` |
| built module | /tmp/dfsmode-artifact/hi5622_wifi.ko, **364,192 B** sha256 **`f5408567...a10266ac`**, 61 params (`dfs_mode` added) |
| unstripped / .text | 1,381,900 B; .text sha256 `3fdcad2d...b2b6186`; strip keeps .text identical |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| key immediate | `objdump -d hi5622_dfs_mode.o`: `movw r3, #503 @ 0x1f7` |
| host tests | `tools/run-host-tests.sh`: 54 model + 15 frame_post validator + 14 ordering + 50 cross-check checks, 0 failures |

## T7. Corrected read-only post-fire probe pack (the Lead) [V/I]

The rf-descriptor lane (task-189) corrected the chain: `P` is
`u32(perlink+0xE20)` (not `perlink+0x2820`), `O = u32(P+0x1A00)`,
`R = u32(perlink+0x2D8)`.  All reads are read-only host physical addresses;
`phys = CPU + 0x46B8000`.

| # | object | read | pass value |
| ---: | --- | --- | --- |
| 1 | ctx0 = u32(0x171458) + u16(0x172504) | two reads | nonzero |
| 2 | perlink = u32(ctx0+0x4B0) | follow | nonzero |
| 3 | **P = u32(perlink+0xE20)** | CPU **0x176838** | **0x0015E570** (the band object); 0 = chain broken |
| 4 | O = u32(P+0x1A00) | CPU 0x15FF70 | published per-link (nonzero) |
| 5 | **mode = u8(P+0xF04)** | CPU **0x15F474** | **3** after the 0x01F7 arm; 0 = never armed |
| 6 | band attach = u8(P+0xD70) | CPU 0x15F3E0 | **1** |
| 7 | **R = u32(perlink+0x2D8)** | CPU 0x175CF0 | nonzero RF block pointer |
| 8 | **R+0x3A8** | two-step | **0x01800060** (ch36..64 RF image) |
| 9 | **R+0x3AC** | two-step | **0x03E6138A** (ch36..64) / 0x056B0597 (ch100+) |
| 10 | **R+0x410** | two-step | **bit0 = 1** (the CA 0x101890 predicate saw the armed 0x00F7 record) |
| 11 | 0x00F7 pool evidence (sibling) | CPU 0x1ADC98 / 0x1ADCB0 / 0x117AD4 | "US"/02, rec1 0x00800201, map byte1 01 |
| 12 | on-air beacon | observer `iw dev OBS scan freq 5200` | **5200 MHz ch40 beacon**, SSID cudy-ap-test-5g40 |

The dmesg line printed right after the post
(`cfg80211: dfs_mode post: bandobject=... published=... attach=...`) gives
the same worker-precondition values from the live driver, no probe needed.

## T8. dmesg expectations [V]

With `beacon_static=1 ... desc_table=1 dfs_mode=1` on a band-1 AP:

```
cfg80211: start_ap beacon_static=1 plan (7 steps): vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg ap_vap_start
frame post 'vap_add': ...
cfg80211: start_ap beacon_static: VAP-object readback=...
frame post 'desc_table': g0 id1 total 68 B ... handler risk=PTR_DEREF; posted N/32
cfg80211: start_ap beacon_static frame 1/7 'desc_table' posted (68 B, g0 id1 key 0x00F7, cc=US, ...) - before the 0x0124 channel post
cfg80211: desc_table post: module ctx=0x........ bandobject=0x........ (...)
frame post 'dfs_mode': g0 id1 total 29 B ... handler risk=PTR_DEREF; posted N/32
cfg80211: start_ap beacon_static frame 2/7 'dfs_mode' posted (29 B, g0 id1 key 0x01F7, body mode=3) - after the 0x00F7 table and before the 0x0124 channel post
cfg80211: dfs_mode post: bandobject=0x........ published=0x........ attach=1 (the 0x01F7 worker's mode-write preconditions are met)
cfg80211: start_ap beacon_static frame 3/7 'ap_chan' posted (... band=1 channel=40 ...)
```

With `desc_table=0 dfs_mode=0` the plan is the 5-step base ladder and there
are no 0xF7/0x01F7 lines.  Failure/gate lines:

```
cfg80211: start_ap REFUSED - dfs_mode=2 is not a valid DFS-mode parameter (0=off, 1=post; no frame sent
cfg80211: start_ap REFUSED - dfs_mode=1 needs the beacon_static=1 vendor order (the 0x01F7 step exists only in that AP-start ladder); no frame sent
cfg80211: start_ap beacon_static: DFS-mode builder refused (%d) - sequence aborted, no frame sent
cfg80211: start_ap beacon_static: DFS-mode frame (g0 id1 key 0x01F7) not posted (%d) - sequence aborted
cfg80211: dfs_mode post: bandobject=0x00000000 published=0x00000000 attach=0 (ZERO/absent - the 0x01F7 worker will log 1442/1448 and the mode byte stays 0)
```

sysfs `cat $D/cfg80211_status` includes:
`dfs_mode: param=1 (band1-post) beacon_static=1 last_rc=0 posted=1 refused=0`

## T9. Decision tree after the fire

```
dmesg: 'dfs_mode' posted (29 B, key 0x01F7) and before the ap_chan line
read 5: u8(P+0xF04) == 3
  mode still 0 -> the worker did not run:
     dmesg dfs_mode line published=0 or attach=0 -> worker preconditions
     absent (logs 1442/1448): the band object is not bound; do NOT re-post,
     hand back to the boot-gate lane (P/O provenance)
     published/attach OK but mode 0 -> frame did not reach the worker
     (capture the wire bytes / check the handler table row)
  mode == 3 -> the arm landed; read 7,8,9,10
     R+0x3AC == 0x03E6138A and R+0x3A8 == 0x01800060 and R+0x410 bit0 == 1
     -> CA 0xA2FA2 ran with the mode gate open; F1 CLOSED (the 0x00F7 arm
        and the mode arm both landed); look at read 12 for the beacon
     R image stale but mode 3 -> the RF image path is gated elsewhere;
        hand back to the rf-programme/rf-descriptor lanes (C1 re-ranked)
     R+0x410 bit0 == 0 -> the predicate did not see the 0x00F7 armed record;
        re-check the sibling probe pack (T7 row 11); the mode arm is not
        sufficient alone
```
