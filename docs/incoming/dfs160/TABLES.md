# TABLES - keys, selectors, channels, timings and the patch map

Lane dfs160 (task-240), 2026-11-26. Grades as in the sibling decode lanes:
**[V]** decoded/re-derived by a committed lane, **[V*]** bench-measured,
**[I]** inference/implementation choice, **[U]** needs a device read. All
firmware addresses are CPU addresses of the pinned image (CA = file + 0x40000,
image 928,920 B sha256 7fc87e20...311b); host-module offsets are from the
pinned 2.5.24 module (3,581,748 B sha256 22fa789a...c7d2). No vendor bytes.

## T1. This lane's pins (all [V])

| item | value |
| --- | --- |
| patch | 15 files, 1,456 lines, sha256 `98bc135ae4542fe79ce41f6ec34ad7f182ef292aef5517429488b5d68b1ec449` |
| base tree | wifi-5g-dfs-relax lineage (/tmp/dfs-relax-build, 66 params) |
| installed module | 384,072 B, sha256 `0313dcfd5b88af683b9c02221d0e38020885a21200480f9ea4b02be2b89a02ff` |
| installed .text | 165,236 B, sha256 `ec2e7d9f603845657942a40a0867eef6a1ee5eabbccc7c212481406bc543783c` |
| unstripped | 1,494,040 B, sha256 `b850cb11a760b4e10f68e782b6f9845ab587d1fd44b163941bdd8a18d4037e31` |
| parameters | **68** = 66 base + `dfs_cac` + `ht_bw`; vermagic 6.18.60, depends cfg80211 |
| host tests | 133 checks, 0 failed (src/dfs160_selftest.c) |

## T2. The H2D g0-id1 keys this feature uses [V]

| key | table A row | worker CA | role |
| --- | ---: | --- | --- |
| 0x0124 | 108 | 0x841B8 | channel config: body {band, **bandwidth**, channel, 0}; stores ctx+0x10/0x11/0x12/0x13 |
| 0x07E7 | 94 | 0xB58FC | CAC/radar state: body {01}=arm (six band-1 SMAC sub-base bit0 regs + CAC flag) / {00}=clear; host precheck = this lane's `hi5622_dfs_set_precheck` pack |
| 0x01F7 | 93 | 0xB5B9C | DFS/RF mode (mode 3 = ch36-64 RF image); runs AFTER the 0x07E7 ARM on the fired A-rung |
| 0x0147 | 100 | 0x436BC | set bandwidth (MAC bandwidth byte obj+0x531, the WAL set_bw path) - NOT the AP-start width path |
| 0x0136 | 95 | 0xB5F3C | radartool string (not used by this lane) |

## T3. cfg80211 chandef width -> 0x0124 body[1] selector

| NL80211_CHAN_WIDTH | value | MHz (vendor table) | selector | grade |
| --- | ---: | ---: | ---: | --- |
| 20_NOHT | 0 | 20 | 0 | [V] |
| 20 | 1 | 20 | 0 | [V] |
| 40 | 2 | 40 | 1 | [I] assemble order |
| 80 | 3 | 80 | 2 | [I] assemble order |
| 160 | 4 | 160 | **3** | **[I]/[U]** - rung 160-1 measures it |
| 80P80 | 5 | 80+80 | refuse -EOPNOTSUPP | [V] vendor cap 0x00400064 & 0x8 == 0 |
| 320 | 6 | 320 | refuse -EOPNOTSUPP | [V] no 320 support |

Sentinel: `HI5622_DFS160_BW_LEGACY = 0xFF` in the driver structs keeps the
legacy `vap_id` byte, i.e. the byte-identical pre-feature wire for `ht_bw=0`.

## T4. The DFS channel set and the ch144 gap [V]

Vendor band-1 table CA 0x117AD4, 29 channels:
36 40 44 48 **52 56 60 64 100 104 108 112 116 120 124 128 132 136 140 144**
149 153 157 161 165 184 188 192 196.

* DFS set (init'able): **52-64 U 100-144 = 16 channels** (bit0 of the
  per-channel flags); our wiphy list had 52-64 + 100-140 -> **ch144 (5720 MHz)
  was the gap, now added with HI5622_DFS**.
* TDWR/weather window: centre frequency **5600..5650 MHz** (ch 100-128 window;
  bit7 of the flags) -> CAC **600000 ms**.  All other DFS channels 60000 ms.
* 160 MHz segments [I, IEEE]: 36-64 -> CCFS1 50 (5250 MHz); 100-128 -> 114
  (5570 MHz); 149-177 -> 163 (5815 MHz).  80 MHz CCFS1: 36-48->42,
  52-64->58, 100-112->106, 116-128->122, 132-144->138, 149-161->155,
  165-177->171.

## T5. CAC / NOL constants [V] (firmware hmac_dfs_init CA 0xAC588)

| field | value | meaning |
| --- | ---: | --- |
| band+0xD38 | **60000 ms** | CAC time (UNII-2/2e) |
| band+0xD3C | **600000 ms** | CAC time, 5600..5650 MHz TDWR window |
| band+0xD58 | **1800000 ms** | non-occupancy period (30 min) |
| band+0xEA8 / +0xEAC+4*type | 0 | radar counters (type 0..8) |
| CAC timer | first **63000 ms**; re-arm **60000 ms** | hmac_dfs_cac_start / timeout |

## T6. This lane's CAC plan (portable model, host-tested)

| input | cac_ms | ticks | schedule |
| --- | ---: | ---: | --- |
| 52-64 / 132-144 (60 s) | 60,000 | 1 | one callback at 63,000 ms |
| 100-128 TDWR (600 s) | 600,000 | 10 | 63,000 ms then 9 x 60,000 ms = 603,000 ms |
| cfg80211 asks longer | requested | requested/60000 | never shorter than the driver floor |
| non-DFS channel | refuse -EOPNOTSUPP | - | - |

The 63 s first tick is deliberate: cfg80211's CAC_FINISHED
`WARN_ON(!time_after_eq(jiffies, start_time + cac_time_ms))` can never fire
early (net/wireless/mlme.c, 6.18); the vendor uses the same margin.

## T7. The CAC state machine (portable, host-tested)

| state | meaning | transitions |
| --- | --- | --- |
| OFF | no CAC attempted | begin -> ARMED |
| ARMED | 0x07E7{01} posted, timer running | tick -> ARMED/FINISHED; abort/radar -> ABORTED |
| FINISHED | CAC complete, 0x07E7{00} posted | AP gate passes on that channel (if no NOL) |
| ABORTED | end_cac / radar hit / teardown | begin -> ARMED again |

NOL: radar(type 0..8) -> ABORTED + nol_end = now + 1,800,000 ms; the AP gate
stays blocked while the NOL is active.  The radar D2H event is **[U]**, so the
driver wires no event source to it yet (no fabricated events).

## T8. cfg80211 contract used (6.18)

| op / API | signature | this lane |
| --- | --- | --- |
| `.start_radar_detection` | (wiphy, net_device, cfg80211_chan_def, u32 cac_time_ms, int link_id) -> int | hi5622_dfs_cac_op_start |
| `.end_cac` | (wiphy, net_device, unsigned int link_id) -> void | hi5622_dfs_cac_op_end |
| `cfg80211_cac_event` | (net_device, const chandef, event, gfp_t, link_id) | FINISHED after CLEAR (op-owned only); STARTED from the op; ABORTED left to cfg80211 on end_cac |
| symvers | `cfg80211_cac_event` added to cfg80211-provider.symvers (real EXPORT_SYMBOL in 6.18) | build proof |

## T9. Patch-site map (15 files; anchors in patches/0001-dfs160.patch)

| file | change |
| --- | --- |
| hi5622_dfs160.h/.c (new) | width selector + CCFS1 + DFS set + CAC plan/state/NOL + status line |
| hi5622_dfs_cac.h/.c (new) | runtime: ops, timer work, live precheck, AP gate, status |
| Kbuild | two new objects |
| cfg80211-provider.symvers | cfg80211_cac_event |
| hi5622_scan.c/.h | `ht_bw` param, width gate + selector, DFS AP gate, `dfs_cac`/one-owner gate, probe export |
| hi5622_frames_api.h | `bandwidth` in chan_cfg params (body[1] meaning corrected) |
| hi5622_ap_frames.c/.h | `bandwidth` in haf params; body[1] = selector unless legacy |
| hi5622_vapscan_provider.c | pass-through |
| hi5622_cfg80211.c/.h | dfs_cac runtime create/destroy, ch144, status line |
| hi5622_cfg80211_ops.c | `.start_radar_detection`, `.end_cac` |

## T10. Module parameters (68 total; only the two new ones here)

| parameter | default | values | meaning |
| --- | --- | --- | --- |
| `ht_bw` | 0 | 0=off; 1=derive body[1]; >1 refused | 160 MHz width selector; 0 keeps the byte-identical wire |
| `dfs_cac` | 0 | 0=off; 1=on; >1 refused | cfg80211 CAC runtime + AP DFS gate; must not be combined with `dfs_set!=0` |

Existing knobs the fire uses (unchanged): `band5g vap5g desc_table dfs_mode
dfs_set vap_start_g0 band_start band_start_fallback beacon_static ...` -
see ../wifi-5g-dfs-relax/FIRE-RECIPE.md for the proven load line.

## T11. Status-line shape (cfg80211_status)

    dfs_cac: ht_bw=1 (0x0124 width selector) dfs_cac=1 (cfg80211 CAC op)
             state=armed channel=52 cac_ms=60000 ticks=0/1 nol_ms=1800000
             nol_active=0 radar=255 arms=1 finishes=0 aborts=0 refusals=0 last_rc=0
