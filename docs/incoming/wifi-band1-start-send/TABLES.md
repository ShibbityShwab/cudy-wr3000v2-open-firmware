# TABLES - lane wifi-band1-start-send (task-206)

All addresses are CPU addresses (CA) of the pinned firmware image unless marked
phys.  Host read paths: firmware RAM phys = 0x40000000 + CA + 0x6B8000;
device IO phys = CA + 0x3B8000.  The protocol bytes quoted here are
**host-built** frames and decoded field values - no vendor binary, firmware
byte, calibration value, MAC or secret is stored in this lane.

Grades: **[V]** re-derived from the committed sibling decodes
(../wifi-band1-start TABLES/REPORT, ../wifi-fw-desc-table TABLES,
../wifi-fw-dfs-worker); **[V\*]** bench-measured by a committed lane;
**[I]** inference; **[U]** needs the fire.

---

## T1. The H2D g0-id1 key 0x0242 wire frame (primary, 33 B) [V]

Carrier geometry of the g0-id1 config-event family (sibling 0x00F7 frame,
../wifi-fw-desc-table TABLES T2): 12-B HCC header + 8-B token/preamble +
key u16 + len u16 + body + inherited 4-byte FF tail; total = 12+8+2+2+len+4.

| off | len | field | primary value | forced value | notes |
| ---: | ---: | --- | --- | --- | --- |
| 0 | 1 | group | 0x00 | 0x00 | chip group 0 |
| 1 | 1 | alloc/dst nibble | 0x01 | 0x01 | alloc 1, dst 0 |
| 2 | 1 | fa | 0x00 | 0x00 | inherited family byte |
| 3 | 1 | **field B** | module | module | the AP-start module/context index (0 = this ladder default) |
| 4 | 2 | total | 0x0021 | 0x0021 | = wire length 33 |
| 6 | 2 | message id | 0x0001 | 0x0001 | g0 config event |
| 8 | 1 | fc | 0x00 | 0x00 | |
| 9 | 1 | dst | 0x00 | 0x00 | |
| 10 | 2 | tag | 0x5A5A | 0x5A5A | transport tag |
| 12 | 8 | token | 0000000001000000 | same | inherited bench pattern; not parsed by the worker |
| 20 | 2 | **key** | **0x0242** | 0x0242 | WAL set_green_ap; worker CA 0x4C524 |
| 22 | 2 | body len | 0x0005 | 0x0005 | |
| 24 | 5 | body | **01 00 01 00 00** | **02 00 00 00 00** | body[0] = subcommand 1 sync/conditional start / 2 forced start; body[2]=1 sets the run-state bit |
| 29 | 4 | tail | FF FF FF FF | same | inherited pad |
| **33** | | **wire length** | | | SR node word1 = (33 << 16) \| 0x6D2B = **0x00216D2B** |

Byte-exact host frame (evidence/frame.txt):

    primary: 000100002100010000005a5a0000000001000000420205 0001000100 00 ffffffff
             -> 000100002100010000005a5a0000000001000000420205000100010000ffffffff
    forced : 000100002100010000005a5a0000000001000000420205000200000000ffffffff

## T2. The 0x0243 nudge frame (29 B, the second fallback rung) [V]

Same carrier; key **0x0243** (worker CA 0x8419C -> 0x4BDCE(bandobj, 5, 0)),
body {00} (the worker reads no body byte), total field 0x001D, SR node word1
0x001D6D2B:

    000100001d00010000005a5a00000000010000004302010000ffffffff

## T3. Decode join (why this frame arms band 1) [V]

| fact | CA | source |
| --- | ---: | --- |
| per-band MAC start, band-1 branch ORs 1, 8, 0x20, 0x1000 (= **0x1029**) into **0x4010C220** | 0x4AF0C (ORs at 0x4AFB2..0x4AFD4) | ../wifi-band1-start TABLES T1 |
| 0x0242 worker: body[0]=1 -> sync/conditional start -> 0x4AF0C at **0x4C612** | 0x4C524 | T5 |
| 0x0242 worker: body[0]=2 -> forced start -> 0x4AF0C at **0x4C652** | 0x4C524 | T5 |
| 0x0243 worker -> helper 0x4BDCE -> 0x4AF0C at 0x4BEB0; arms only when state[1]==3 | 0x8419C | T2/T6 |
| the worker takes the band object from ctx+0x4B8 (ctx = 0xFE578(0, hdr[3])) | 0x4C524 | T5 |
| the key-0x0064 VAP add binds ctx(N)+0x4B0/+0x4B4/+0x4B8 | 0x449E4 | T8 |
| G+0x370 (CA 0x174B34) has **no writer anywhere**; the hook is not the path | - | T3 |
| the 0x4AF0C guards: state[1]==2 "already started", state[4]==0xFF "no bound VAP" | 0x4AF2C / 0x4AF32 | T1 |

State is per band at **bandobj+0x14D0** (shared by all module contexts of the
band); a successful start writes **state[1] = 2** and increments u32
**state+0x64**.  Our AP ladder posted the 0x0064 VAP add and the 0x0124
channel config but never 0x0242/0x0243 - the band-1 arm bits were never set
(the R3b gap).

## T4. The fail-closed parameter matrix [V]

| band_start | band_start_fallback | beacon_static | band | module | verdict |
| ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 0 | any | any | any | **no frame** - current behaviour, byte-identical to F2 |
| 0 | 1 | any | any | any | **refused -EINVAL** (fallback without the primary rung) |
| 1 | 0 | 1 | 1 | 0..0x12 | **post** the primary 0x0242 sync start only |
| 1 | 1 | 1 | 1 | 0..0x12 | **post** the primary; then forced start + 0x0243 if state[1] != 2 |
| 1 | 0/1 | 1 | 0 | 0..0x12 | **no frame** (band-0 start is a different branch; clean no-post) |
| 1 | 0/1 | 0 | any | any | **refused -EINVAL** ("needs the beacon_static=1 vendor order") |
| >1 | any | any | any | any | **refused -EINVAL** before any frame |
| any | >1 | any | any | any | **refused -EINVAL** before any frame |
| 0/1 | 0/1 | 1 | 1 | >0x12 | **refused -EINVAL** (module-table bound) |

The gates run in start_ap **before any frame is posted** (no partial ladder on
a bad parameter) and the plan/build model repeats the checks before the wire.
All parameters are read-only (0444), default 0.

## T5. Order contract [V]

The beacon_static plan (hi5622_beacon_plan) with all knobs 1:

    vap_add(0x0064) -> desc_table(0x00F7) -> dfs_mode(0x01F7)
      -> band_start(0x0242 [+ 0x0243 fallback inside the step])
      -> ap_chan(0x0124) -> ap_ssid -> ap_cfg(0x0127) -> ap_vap_start(0x0065)

Enum order is the contract: VAP_ADD(1) < DESC_TABLE(2) < DFS_MODE(3) <
BAND_START(4) < CHAN(5) < SSID(6) < AP_CFG(7) < VAP_START(8) < MAX(9).  The
fallback rungs do not add plan steps (they run inside the band_start step).
The 0x0242 post is in hi5622_scan.c **after** the 'dfs_mode' post and
**before** the 0x0124 builder/post; the host test asserts the source order.

## T6. Artifact pins (LOCAL evidence, never committed)

| item | value |
| --- | --- |
| F2 base tree | /tmp/dfsmode-build (the tree that built the bench-fired F2 module) |
| F2 base artifact | /tmp/f2-module.ko, 364,184 B, sha256 12427b8675b9151598d66c0b75410752bcc142178908542ff7d82aa54f059468, 61 params |
| F2 field-B fix | ../wifi-fw-dfs-worker/patches/0001-fieldb-context.patch sha256 0530ca45528e0601af5a6f8b8523e3ed8e43cd2237c2361f6cfd03aa3504f926 |
| lane patch | patches/0001-band-start-send.patch sha256 3ddda8524b782063837fd23a4b2fab1c666f4826b8fa76da394275e6a9de308b |
| lane module | /tmp/bandstart-artifact/hi5622_wifi.ko, **371,212 B**, sha256 **e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028**, **63 params**, vermagic 6.18.60 SMP mod_unload ARMv7 p2v8 |
| unstripped | 1,399,948 B sha256 50f105f406474131a244f217ee539cae3794f58bced2f8346e5f5c4fa86a59d7 |
| .text | sha256 884f0a35da01194598decd72fa81921188a6d32e00b0a4a3b3cd6078c2aea275 |

The artifact is host-built only (never committed; the repo stores no binaries).

## T7. Corrected read-only post-fire probe pack (the result) [V]

**The correction:** 0x4010C220 is a DEVICE IO address: phys = CA + 0x3B8000 =
**0x404C4220**.  The earlier R3 read 0x404C8220 was mis-addressed (an extra
0x4000); **do not use it**.  Use the committed read helper (bar0read /
O_RDONLY resource0 physical addressing, PROT_READ).

| # | object | CA | **phys** | expect after the fire |
| ---: | --- | ---: | ---: | --- |
| P1 | **band-1 arm word 0x4010C220** | 0x4010C220 | **0x404C4220** | **0x1029** (the 0x4AF0C band-1 OR mask; 0 = never armed) |
| P2 | **G+0x370 (the NULL hook)** | 0x174B34 | **0x4082CB34** | **0** - proves the arm came from the direct 0x4AF0C path, not the hook |
| P3 | G+0x374 (control slot) | 0x174B38 | 0x4082CB38 | 0x00057639 |
| P4 | band-0 arm word 0x40108214 | 0x40108214 | **0x404C0214** | 0xF (band-0 reference; unchanged by a band-1 start) |
| P5 | run bits 0x40101220 | 0x40101220 | 0x404B9220 | band-1 bit0 = 1 (0x4AF0C ORs it) |
| P6 | band-1 start gate 0x4011480C | 0x4011480C | 0x404CC80C | bit0 = 1 (0x4AF0C gate OR) |
| P7 | band-1 MAC reset/status 0x40114500/51C/520 | 0x40114500 | 0x404CC500/51C/520 | 0x55/0xFF sequence ran: 51C and 520 = 0xFF |
| P8 | band-1 init marker 0x40114548/54C | 0x40114548 | 0x404CC548/54C | 0x6101 |
| P9 | band-1 run marker (read-only **state**) | bandobj 0x15E570 + 0x14D0 | **0x40817A40** | u8 +1 = **2** (started), u8 +4 != 0xFF (bound VAP), u32 +0x64 incremented |
| P10 | band-1 band object pointer | bandobj+0x1A00 | 0x40817F70 | nonzero (published per-link) |
| P11 | RF mode byte P+0xF04 (F2 context) | - | CPU 0x15F474 | 3 (already programmed by the F2 fire) |
| P12 | RF image R+0x3AC / R+0x3A8 (F2 context) | - | two-step via perlink+0x2D8 | 0x03E6138A / 0x01800060 |
| P13 | on-air 5200 MHz beacon | - | observer scan 5200 | ch40 beacon / the SSID |

Note (decode-lane typo): ../wifi-band1-start/FIRE-RECIPE.md P1.16 prints the
band-1 run-state phys as 0x4081D240; its own tool derives **0x40817A40**
(bandobj 0x15E570 + 0x14D0 = 0x15FA40 -> +0x6B8000 -> 0x40817A40).  This pack
uses the tool-derived value; the P1.1-1.15 rows agree.

**R3b PASS** iff P1 = 0x1029 (strict: the arm bits are set), P9 state[1] = 2
and the counter incremented, P2 = 0, P4 unchanged, P7/P8 consistent, **and**
P13 shows the 5200 MHz ch40 beacon.  If P1/P9 are set but P13 is absent, the
start is armed and the remaining gap is the beacon/TBTT engine (sibling
wifi-fw-beacon-tx / wifi-fw-tbtt-arm), not this lane.

**0x1039 caveat:** the enable-TRX probe CA 0xE8520 writes 0x1039 to
0x4010C220, and 0x1039 already contains 0x1029 (0x1029 is a subset).  A
read-back of 0x1039 is therefore NOT proof of a start; expect exactly
**0x1029** (or 0x1029 | pre-existing non-arm bits, with P9 state[1]=2 as the
independent confirmation).

## T8. dmesg expectations (the FIRE-RECIPE ladder)

Plan line (8 steps):

    cfg80211: start_ap beacon_static=1 plan (8 steps): vap_add desc_table dfs_mode band_start ap_chan ap_ssid ap_cfg ap_vap_start

The band_start frame (after the dfs_mode frame):

    cfg80211: start_ap beacon_static frame 3/8 'band_start' posted (33 B, g0 id1 key 0x0242, subcmd=1 sync start, field B=0) - after the 0x01F7 mode and before the 0x0124 channel post
    cfg80211: band_start post: module=0 bandobject=nonzero state[1]=1 state[4]=0x00 counter=N (the 0x4AF0C start guards are open)

With band_start_fallback=1 on a miss, in order:

    cfg80211: start_ap beacon_static frame 4/8 'band_start' fallback posted (33 B, g0 id1 key 0x0242, subcmd=2 forced start, state[1] was 1)
    cfg80211: start_ap beacon_static frame 5/8 'band_start' fallback posted (29 B, g0 id1 key 0x0243, band-ready nudge, state[1] was 1)
    cfg80211: band_start fallback: start marker state[1]=2 for module=0 (band-1 start armed (read 0x4010C220 for 0x1029))

sysfs (pci device attribute):

    cat /sys/bus/pci/devices/0000:00:00.0/cfg80211_status | grep band_start
    band_start: param=1 (band1-post) fallback=0 beacon_static=1 last_rc=0 posted=1 forced=0 nudge=0 refused=0

Negative controls (fresh boot each): band_start=2 -> "REFUSED - band_start=2
is not a valid band-start parameter"; band_start=1 beacon_static=0 -> "needs
the beacon_static=1 vendor order"; band_start_fallback=1 alone -> "needs
band_start=1"; band_start=0 control -> no band_start line at all.

## T9. Decision tree

| observation | verdict | next |
| --- | --- | --- |
| P1 = 0x1029, P9 state[1] = 2, 5200 beacon | **R3b CLOSED** - 5 GHz AP start and beacon achieved | record; the 5 GHz blocker is retired |
| P1 = 0x1029 / state[1] = 2, no beacon | the band-1 MAC start is armed; the remaining gap is the beacon/TBTT engine | hand to the tbtt-arm/beacon-tx lanes (do not re-post) |
| P1 unchanged, state[1] not 2, dmesg "bandobject=ZERO" | the 0x4C524 worker's ctx+0x4B8 is absent - the VAP add bind failed | hand back to the boot-gate/VAP-bind lanes; do not blind re-post |
| P1 unchanged, state[1] not 2, dmesg "no bound VAP" | state[4] = 0xFF - the bound VAP index did not appear | check the F1 VAP-add readback; one more rung is not the fix |
| P1 = 0x1039 only, state[1] = 2 | the enable-TRX probe value, not the start arm | re-read P9 and P7/P8; treat as inconclusive, no re-post |
| fallback rungs posted, state[1] stays != 2 | 0x4AF0C refused (guards above) | stop; capture the full pack and analyse offline |
