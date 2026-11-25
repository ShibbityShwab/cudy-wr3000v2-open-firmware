# TABLES - lane wifi-band1-start-send-3 (task-209, R4)

Firmware CPU address (CA) = file offset + 0x40000 of the pinned local
FIRMWARE.bin (928,920 B, sha256 `7fc87e20...311b`); firmware RAM phys =
0x40000000 + CA + 0x6B8000.  Host module offsets are link-time `.text`
offsets.  Grades: `[V]` re-derived from the committed sibling decodes and this
lane's host tests; `[V*]` bench-measured by a committed lane; `[I]`
inference; `[U]` needs the fire.

---

## T1. The key-0x0065 g0-id1 VAP-start frame (40 B) [V]

Carrier: **12-B HCC header | 8-B token | key u16 | len u16 | body(12) |
4 x FF tail**, total 40 B, hdr total field 0x0028, SR node word1
0x00286D2B.

| frame offset | width | field | fire value (AP, index 0, band 1) |
| ---: | ---: | --- | --- |
| hdr+0x00 | u8 | group 0 | 0x00 |
| hdr+0x01 | u8 | alloc 1 / dst 0 | 0x01 |
| hdr+0x02 | u8 | fa (inherited) | 0x00 |
| **hdr+0x03** | u8 | **field B = VAP/module index** (dispatcher ctx) | **0x00 = vap_add_index** |
| hdr+0x04 | u16 | total = 0x0028 | 40 |
| hdr+0x06 | u16 | message id 1 | 1 |
| hdr+0x0a | u16 | tag 0x5a5a | 0x5a5a |
| hdr+0x0c | 8 B | token (inherited; not parsed by the key workers) | 00 00 00 00 01 00 00 00 |
| hdr+0x14 | u16 | key 0x0065 | 0x0065 |
| hdr+0x16 | u16 | body length 12 | 0x000c |
| body+0x00 | u8 | stock do_start_vap arg | 0x00 |
| body+0x01 | u8 | vap+0x0F state | 0x01 |
| **body+0x02** | u8 | **vap+0x11 band** | **0x01 = 5 GHz** |
| body+0x03 | u8 | vap+0x12 field | 0x00 |
| body+0x04 | u32 | vap+0x414 word | 0 |
| body+0x0c | 4 B | inherited 0xFF tail | ff ff ff ff |

Byte-exact KATs (evidence/frame.txt):

    fieldB=0: 000100002800010000005a5a000000000100000065000c00000101000000000000000000ffffffff
    fieldB=4: 000100042800010000005a5a000000000100000065000c00000101000000000000000000ffffffff

Derivation: stock emitter `hmac_config_start_vap_event` (vendor hi5622v100
wifi.ko .text+0x15AF88) builds the 12-B body `[0]=arg, [1]=vap+0x0F,
[2]=vap+0x11 band, [3]=vap+0x12, [4..7]=vap+0x414` and sends key 0x0065,
len 0x0C; its AP caller is the BSS-start path 0x4826C, before the green_ap
enable.  The 40-B row is the same carrier the sibling lane
../wifi-vap-start already proved with `vsf_build_vap_start` (there with
body[2]=0); for the band-1 AP body[2] must be 1 (mctx[0x11] is the band the
later 5 GHz RF branch CA 0x40684 reads).  The lane's host test builds the
frame with BOTH builders and asserts the same bytes for every index 0..0x12
[V].

## T2. The AP-start plan order [V]

`hi5622_beacon_plan()` (patched `hi5622_scan_logic.c`) with all knobs 1:

    vap_add -> vap_start_g0 -> desc_table -> dfs_mode -> band_start
            -> ap_chan -> ap_ssid -> ap_cfg -> ap_vap_start        (9 steps)

With `vap_start_g0=0` the plan is the send-2 8-step order unchanged
(`vap_add desc_table dfs_mode band_start ap_chan ap_ssid ap_cfg
ap_vap_start`).  The new step is inserted directly after `vap_add` because
the key-0x0065 worker must run on the module context the 0x0064 add created
and bound, and before EVERY later consumer (0x00F7 finds the band object,
0x0242's resolver CA 0x4AE7C reads the state the 0x0065 sets).  Enum values
were renumbered: VAP_ADD=1, VAP_START_G0=2, DESC_TABLE=3, DFS_MODE=4,
BAND_START=5, CHAN=6, SSID=7, AP_CFG=8, VAP_START=9, MAX=10.

## T3. Firmware code sites the R4 decode relies on [V] (sibling task-208)

| CA | role |
| ---: | --- |
| 0x46394 | key-0x0065 worker (AP branch when mctx[4] == 2) |
| 0x46418-0x4641A | **movs r3,#1; strb r3,[mctx+0x0E]** - the only fresh-band 0->1 store |
| 0x463F6-0x463F8 | mctx[0x444] = body[4..7]; 0x46408-0x4640C mctx[17]=body[2], mctx[18]=body[3] |
| 0x46470 | **dereferences mctx[0x190]** (+0x58 bit2 <- band) - the G5 fault site |
| 0x4AE7C | band VAP resolver: bandobj+0x12 == 1, then per entry requires mctx[4]==2 && (u8)(mctx[0x0E]-1) <= 2 |
| 0x4C3FC | 0x0242 pre-start: calls 0x4AE7C; success stores state[4]=mctx[0] (0x4C448) and state[1]=1 (0x4C4DA) after the [mctx+0x190]+0x10 ring math; failure stores state[4]=0xFF (0x4C4EE) |
| 0x4AF0C | per-band MAC start: refuses state[1]==2 (0x4AF2C) / state[4]==0xFF (0x4AF32); success writes state[1]=2 and increments state+0x64 |
| 0x4C524 | 0x0242 worker (subcmd 1 -> CA 0x4C3FC at 0x4C5F4, then a second 0x4AE7C at 0x4C5FA, then 0x4AF0C at 0x4C612) |

## T4. The G1-G14 probe pack and what the driver does [V]

All reads are relative to the existing bench-proven chain (slot0 CA
0x171458, base CA 0x172504, mctx = slot + base; bandobj = mctx+0x4B8;
perlink = mctx+0x4B0).  G5 is the same word the driver already reads as the
VAP object (HI5622_STAB_VAP_PTR_OFF = 0x190).

| id | probe (read-only) | pass | driver action on failure |
| ---: | --- | --- | --- |
| G1 | u32 mctx | nonzero | REFUSED -ENODEV, no 0x0065 |
| G2 | bandobj != 0 && u8 count(0x12) == 1 | yes | REFUSED -ENOENT |
| G3 | u8 bandobj[0] == vap_add_index | yes | REFUSED -EEXIST |
| G4 | u32 bandobj+0x1A00 (published) | nonzero | REFUSED -EAGAIN |
| **G5** | **u32 mctx+0x190** (bounded 300 ms wait) | nonzero | **REFUSED -EFAULT - posting would fault at CA 0x46470** |
| G6 | u32 mctx+0x4AC | observation | warn only (AP path does not dereference) |
| G7 | u8 mctx+0x04 create type | 2 (AP) | REFUSED -EPROTO |
| G8 | u8 mctx+0x11 band | 1 (5 GHz) | REFUSED -EINVAL |
| G9 | u8 mctx+0x440 band-config flag | 0 | REFUSED -EBUSY |
| G10 | u8 bandobj+0xD6E band-id gate | 0 | REFUSED -EINVAL |
| G11 | u8 bandobj+0xEC8 band-up flag | 0 | REFUSED -EALREADY |
| **G12** | **u8 mctx+0x0E after the post (300 ms wait)** | **1** | **band_start REFUSED -EIO; no 0x0242** |
| G13 | state[0]==0 && state[1]==0 && state[4] in {0x00,0xFF} | fresh | REFUSED -EBUSY |
| G14 | u8 perlink+0x6C6 | observation | logged only |

A failed G1..G11/G13 pre-gate aborts the start BEFORE the 0x0065 frame (no
partial ladder).  A failed G12 aborts with the pack already in dmesg, and only
when `band_start=1` is the sequence refused at that point (with
`band_start=0` the probe-only rung continues, the N1 control).

## T5. Fail-closed matrix (host-tested) [V]

| input | result |
| --- | --- |
| default `vap_start_g0=0` | no 0x0065 frame; wire bytes identical to the send-2 artifact; plan 8 steps |
| `vap_start_g0=1`, beacon_static=1, band 1, gates pass, G12==1 | post the 40-B frame between vap_add and desc_table; 0x0242 allowed |
| `vap_start_g0=1`, gates fail | no frame; start aborted with the pack in dmesg |
| `vap_start_g0=1`, G12 != 1, band_start=1 | no 0x0242; start aborted (-EIO) |
| `vap_start_g0=1`, G12 != 1, band_start=0 | probe-only (N1); the rest of the ladder continues |
| `vap_start_g0=1` without beacon_static=1 | start_ap REFUSED before any frame |
| `vap_start_g0 > 1` | start_ap REFUSED before any frame |
| `vap_start_g0=1` on band 0 | clean no-post (clean-room band-1 repair only) |
| builder with an unarmed plan / module > 0x12 / cap < 40 | -EINVAL / -EINVAL / -ENOSPC |
| restart after a host-side stop | the 0x0065 frame is NOT re-posted (never start a live VAP twice) |

## T6. Pins, artifact, tests [V]

| pin | value |
| --- | --- |
| fired 0x0242 base tree | /tmp/bandstart-build (hi5622_scan.c `272e47ac...`, hi5622_vapscan_provider.c `c306e75f...`, vapstart_frames.c `7dc7bc35...`, hi5622_band_start.c `71019718...`) |
| fired 0x0242 artifact | /tmp/bandstart-artifact/hi5622_wifi.ko, 371,212 B, sha256 `e2312402...a028`, 63 params |
| send-2 patch (carried) | ../wifi-band1-start-send-2/patches/0001-vap-add-index.patch, sha256 `968d70c5...b813fb` |
| send-2 tree | hi5622_scan.c `52a5bc0b...`, hi5622_vapscan_provider.c `b66da889...`; artifact /tmp/vapbound-artifact, 373,056 B, sha256 `b8f6fe35...5b71`, 64 params |
| this lane patch | patches/0001-vap-start-g0.patch, sha256 `0a8a1c1bda3ebaaf05933b670d996a2962f2679ecf0075237bf6d4adef30fc4c` |
| patched tree shas | evidence/patched-tree-sha.txt |
| module artifact | /tmp/g0start-artifact/hi5622_wifi.ko, **382,072 B**, sha256 `bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711`, **65 params**, .text `3d3b45ff...` |
| host tests | **351 checks / 0 failures** (C 278, Python 73) |

## T7. The fire's read-only verification pack [V*]

| # | object | value to read | expect |
| ---: | --- | --- | --- |
| P0 | new dmesg lines | plan / vap_start_g0 pre / post / band_start post | see FIRE-RECIPE section R2 |
| P1 | band-1 arm word | phys **0x404C4220** (= firmware CA 0x4010C220) | **0x1029** after a successful start (0 before) |
| P9 | band-1 run-state | phys **0x40817A40** (= bandobj 0x15E570 + 0x14D0) | u8+4 = **0x00** (not 0xFF); u8+1 = 1 (the 0x4C3FC pre-start marker) or 2 (CA 0x4AF0C ran); u32+0x64 incremented |
| P12 | 5 GHz RF image R+0x3A8 / R+0x3AC (F2 context) | two-step via perlink+0x2D8 (committed ../wifi-band1-start-send TABLES P12) | 0x01800060 / 0x03E6138A |
| P13 | on-air beacon | observer scan on 5200 MHz | the ch40 AP's beacon/SSID |

P1 note: the firmware CA 0x4010C220 maps to host/BAR phys 0x404C4220 (the
send-2 probe pack's corrected address; 0x404C8220 was wrong).  P9 is the
corrected run-state address (0x40817A40, not the old 0x4080xxxx guesses).

## T8. The second observable - recipe R4b [V/I]

The CA 0x4C3FC success sub-branch reads u32[mctx+0x190] and then
u32[that+0x10] (CA 0x4C498-0x4C4A0); if the second word is zero it takes its
tail without writing state[1].  Therefore:

* state[4] = 0x00 **and** state[1] = 1/2: the R4 fix worked (0x0065 set
  mctx[0x0E]=1, the resolver returned the VAP; 0x4AF0C then normalises 2).
* state[4] = 0x00 **and** state[1] = 0: the pre-start succeeded but
  `[mctx+0x190]+0x10` is zero (the driver logs it as `vap+0x10` on both the
  vap_start_g0 post and band_start post lines).  That is the R4b rung: move
  the `ap_chan` (module channel) post before `band_start` - a single change,
  fresh boot.
* state[4] still 0xFF and mctx[0x0E] still 0: the 0x0065 event did not take
  effect (carrier/field-B/timing); capture and stop, do not re-post.
