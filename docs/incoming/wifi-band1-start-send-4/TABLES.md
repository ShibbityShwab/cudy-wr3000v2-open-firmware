# TABLES - lane wifi-band1-start-send-4 (task-211), the R4b reorder

## T1. The plan, old (send-3 R4) vs new (this lane)

| # | send-3 R4 plan | R4b plan (this lane) | posted key |
| ---: | --- | --- | --- |
| 1 | vap_add | vap_add | 0x0064 |
| 2 | vap_start_g0 | desc_table | 0x00F7 |
| 3 | desc_table | dfs_mode | 0x01F7 |
| 4 | dfs_mode | **ap_chan** | 0x0124 |
| 5 | band_start | **vap_start_g0** | 0x0065 |
| 6 | ap_chan | **band_start** | 0x0242 |
| 7 | ap_ssid | ap_ssid | g0 carrier |
| 8 | ap_cfg | ap_cfg | g0 carrier |
| 9 | ap_vap_start | ap_vap_start | WAL g2 0x0065 44-B |
| steps | 9 (8 without vap_start_g0) | 9 (8 without vap_start_g0; 4 with no optionals and vap_start=0) | |

The four steps that move: the 0x0124 channel post moves from 6 to 4; the
0x0065 VAP start moves from 2 to 5; the 0x0242 band start moves from 5 to 6;
everything else keeps its relative order.  The start_ap post sites are moved
to match exactly (the 0x0124 block is earlier in hi5622_scan.c, the 0x0065
block later).

## T2. Files and what changed

| file | hunks | change |
| --- | ---: | --- |
| hi5622_scan_logic.c | plan body + header | hi5622_beacon_plan() order, comments |
| hi5622_scan_logic.h | enum + 2 doc blocks | enum values order, R4b paragraph, plan doc |
| hi5622_scan.c | 6 | post-site block move (a ~148-line block and the ~90-line 0x0124 block), parameter comment/description, post-line text |
| hi5622_vap_start_g0.h | 2 | placement contract + R4b paragraph |
| hi5622_band_start.h | 1 | band_start parameter description |
| new files | 0 | no new builder, no new parameter |

Patch: patches/0001-r4b-order.patch, 34,330 B, sha256
'3628899843628984b1373f9c1e2ccd6539aff8920ac3188401a24a83b1c21775'.

## T3. The gates and their position after the reorder

| gate | reads | hard? | send-3 order (R4) | R4b order |
| --- | --- | --- | --- | --- |
| G1 mctx | u32 | yes | before post | unchanged |
| G2 count/bandobj | u8/u32 | yes | before post | unchanged |
| G3 entry0 | u8 | yes | before post | unchanged |
| G4 published | u32 bandobj+0x1A00 | yes | before post | unchanged |
| G5 mctx[0x190] | u32 | **yes (fault gate)** | after vap_add | after vap_add (unchanged, the 0x0124 does not touch it) |
| G6 helper | u32 mctx+0x4AC | warn only | before post | unchanged |
| G7 type | u8 mctx+4 | yes | before post | unchanged |
| **G8 band** | **u8 mctx+0x11** | **yes** | **BEFORE the 0x0124 -> 0 -> REFUSED (the R4 fire)** | **after the 0x0124 -> 1 -> passes** |
| G9 band-config | u8 mctx+0x440 | yes (==0) | before the 0x0124 | after the 0x0124 (expects 0; the writer is CA 0x455D0 [I]) |
| G10 band-id | u8 bandobj+0xD6E | yes | before the 0x0124 | unchanged |
| G11 band-up | u8 bandobj+0xEC8 | yes (==0) | before the 0x0124 | after the 0x0124 (expects 0; writer CA 0x455D0 [I]) |
| G12 VAP state | u8 mctx+0x0E | **yes (==1)** | after the 0x0065 post | unchanged (0x0065 then 0x0242) |
| G13 fresh run-state | state[0], state[1], state[4] | yes | before the 0x0124 | after the 0x0124 (expects 0/0/0 or 0xFF; the 0x0124 does not write state[4]) |
| G14 per-link | u8 perlink+0x6C6 | observation | before post | unchanged |

## T4. Why the 0x0124 must precede the 0x0065 and the 0x0242

| fact | source | consequence |
| --- | --- | --- |
| the 0x0124 worker validates the channel table and stores the channel/band/index word at ctx+0x10..0x13 | ../wifi-fw-chan-worker/ REPORT 4/T4/T5 (the ch40 store word 0x01000128) | G8 (mctx[0x11]) can only pass after it |
| the 0x0242 pre-start success sub-branch reads u32[u32[mctx+0x190]+0x10] | ../wifi-band-start-prestart/ TABLES T9/T10 + FIRE-RECIPE R4b row | that word is channel-context data - the channel post must come first |
| the stock order is VAP add -> channel config -> VAP start -> green_ap enable | ../wifi-band-start-prestart/ TABLES T8 | the reorder is the stock order, not an invention |
| the 0x0124 5 GHz RF programme is gated on module+0x440==1 && bandobj+0xEC8==1 | ../wifi-fw-chan-worker/ REPORT 5 (CA 0x40684/0x944DE) | see REPORT 6.3: the R4b claim is the order, not the RF programme |

## T5. Host-test matrix (386 checks, 0 failures)

| part | checks | what it proves |
| --- | ---: | --- |
| src/vap_start_g0_selftest.c | 293 | 40-B KATs for field B 0/4 and every module index; byte-for-byte cross-check vs the sibling key-0x0065 builder; R4b plan order 9/8/4 steps and the enum order; fail-closed parameter/build matrix; every G1..G14 gate; G12 0/1/2/3/4/0xFF; the G8 sequencing model (band 0 refused, band 1 passes) |
| tools/r4b_order_frame_test.py | 93 | independent frame re-derivation from the carrier geometry; source contract: plan order in the model and the enum, the start_ap post-site order (desc_table plan -> 0x0124 post -> 0x0065 probe/precheck -> 0x0065 post -> G12 -> 0x0242 build), the R4b placement strings, the parameter description, the frame geometry constants, the 0x0242 builder field B, 12 cfg80211 symvers |

## T6. Pins and artifact

| field | value |
| --- | --- |
| patch | sha256 3628899843628984b1373f9c1e2ccd6539aff8920ac3188401a24a83b1c21775 (34,330 B) |
| base | the pinned send-3 tree (hi5622_scan.c da488e3c..., logic.c 7e5edbc4..., logic.h fc4e43d1..., bstart.h 3fd2c608..., vsg.c ece5c0d6..., vsg.h 68c2eb79..., Kbuild 1bb51085...) |
| patched tree | scan ab14fa07..., logic.c ab7b8bac..., logic.h 3bfe1239..., bstart.h 443b071d..., vsg.h fe165a82... (vsg.c + Kbuild unchanged) |
| artifact | /tmp/r4border-artifact/hi5622_wifi.ko, 382,188 B, sha256 b9926c1c4201cfe698c9fb6828a0d4e6a4fc45ba620bb49d745859ef7fda1102, 65 params, vermagic 6.18.60, .text f2848fdc..., 0 warnings |
| chain rerun | the fired 0x0242 (371,212 B e2312402...), send-2 (373,056 B b8f6fe35...) and send-3 R4 (382,072 B bb0434c2...) artifacts reproduced byte-identically |

## T7. The fire probe pack (read-only; unchanged addresses)

| # | object | phys | expect on PASS |
| ---: | --- | ---: | --- |
| P1 | band-1 arm word 0x4010C220 | **0x404C4220** | **0x1029** (0 = never armed; 0x1039 = the TRX probe value seen after the R4 refusal, NOT a start) |
| P9 | band-1 run-state (bandobj+0x14D0) | 0x40817A40 | u8+4 = **0x00** (not 0xFF); u8+1 = **1** if u32[mctx+0x190]+0x10 != 0; **2** if CA 0x4AF0C also ran; counter+0x64 incremented |
| P12 | 5 GHz RF image R+0x3A8/R+0x3AC | two-step via perlink+0x2D8 | **0x01800060 / 0x03E6138A** |
| P13 | on-air beacon | 'iw dev OBS scan freq 5200' | the ch40 AP beacon/SSID |
| - | dmesg | - | the 'vap_start_g0 post:' / 'band_start post:' lines already carry vap= / vap+0x10= read-only |

## T8. The dmesg order to read on the fire

1. 'start_ap beacon_static=1 plan (9 steps): vap_add desc_table dfs_mode
   ap_chan vap_start_g0 band_start ap_ssid ap_cfg ap_vap_start'
2. vap_add post -> desc_table post -> dfs_mode post -> **ap_chan post**
3. the 5 GHz channel activation wait (ap_chan_wait_ms=300)
4. **vap_start_g0 pre** pack: band=1, ch=40, state4=0x00/0xFF, gates open
5. vap_start_g0 post consumed -> 'vap_start_g0 post: mctx[0x0e]=1
   mctx[0x11]=1 ... (G12 pass ...)'
6. **band_start post** -> state[4]=0x00 (not 0xFF)
7. ap_ssid/ap_cfg/ap_vap_start, then the read-only probe pack P1/P9/P12/P13.
