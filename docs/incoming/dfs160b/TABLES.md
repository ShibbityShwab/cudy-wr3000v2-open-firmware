# TABLES - the 160 MHz width selector + the DFS/CAC/NOL feature (lane dfs160b, task-245)

Every constant below is a DECODED protocol fact of the committed decode
lanes and is quoted as text only.  Grades: **[V]** re-derived by a committed
tool on the pinned vendor artifacts (the source lane's tool); **[V\*]** bench
measured by an earlier fired lane; **[I]** inference/design; **[U]** needs a
device read.  Address convention: firmware CPU address CA; file offset =
CA - 0x40000.  Device IO phys = CA + 0x3B8000 (BAR0 window).  Firmware RAM
phys = CA + 0x406B8000.

## T1. Source lanes (the ONLY protocol sources this lane used)

| lane | what it pins |
| --- | --- |
| [../wifi-5g-160-dfs/](../wifi-5g-160-dfs/) (task-205) | the 0x0124 body[1] width selector, the DFS set incl. ch144, the CAC/NOL table (T2/T3/T4/T5/T7/T8) |
| [../wifi-5g-dfs-set/](../wifi-5g-dfs-set/) (task-227) | the 0x07E7 ARM/CLEAR 29-B carrier + worker CA 0xB58FC + the six band-1 SMAC sub-base writes |
| [../wifi-5g-dfs-order/](../wifi-5g-dfs-order/) (task-228) | the forced order 0x0065 -> 0x07E7 -> 0x01F7 -> 0x0242 |
| [../wifi-5g-dfs-relax/](../wifi-5g-dfs-relax/) (task-230) | the +0x4A0 relax: register-only vs full ARM; the A-rung module lineage |
| [../wifi-5g-cac-context/](../wifi-5g-cac-context/) (task-229) | the +0x49C/+0x4A0/+0x4A4 writer CA 0x5F450 and the ARM gate |

## T2. This lane's artefacts (all local except the docs/patch)

| artefact | pin |
| --- | --- |
| patch | `patches/0001-dfs160b.patch`, 14 files (4 new), git-apply verified |
| patch sha256 | `61177c2a2e502d0858f0069e71923f49f520f2b07713b21468002d5091686c6d` |
| base tree | the pinned `wifi-5g-dfs-relax` A-rung lineage (`/tmp/dfs-relax-build`) |
| base Kbuild sha256 | `8f976618...d518c9e9` (all base pins in tools/build-dfs160b.sh) |
| installed module | **383,020 B** sha256 `006a94cf6bcb6f53887c83aa4ad3a0f057358301d9a350a139ae178acac0a183` |
| unstripped module | 1,493,920 B sha256 `f6c6481a...e8dd499` |
| installed .text | **164,980 B** sha256 `5e14008fab4707afdf55ab3ea864129abfc65979c3e4b0b2e572f6593ead5faa` |
| params | **68** = 66 base (R5 + dfs_set) + `ht_bw` + `dfs_cac` |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8; depends cfg80211 |
| build | 0 warnings / 0 errors; the artifact is deterministic from base+patch |
| host tests | C model: **269 checks / 0 failures**; Python patch/order test: [DONE] checks / 0 failures |

## T3. nl80211 chan-width UAPI pins (6.18.60) and the vendor selector

The target kernel's UAPI `enum nl80211_chan_width` is pinned by
`include/uapi/linux/nl80211.h` (6.18.60) and re-derived by
tools/dfs160b_patch_test.py; the kernel glue carries a `_Static_assert` per
pin so a drift is a build error.

| nl80211 width | 6.18 value | vendor 0x0124 body[1] | note |
| --- | ---: | ---: | --- |
| 20_NOHT | 0 | 0 | |
| 20 | 1 | 0 | |
| 40 | 2 | 1 | [I: assemble order] |
| 80 | 3 | 2 | [I] |
| 80P80 | **4** | refused -EOPNOTSUPP | the vendor VHT cap has no 80+80 bit |
| 160 | **5** | **3** | the feature's target width |
| 5/10/1/2/4/8/16 MHz | 6..12 | refused -EOPNOTSUPP | out of scope |
| 320 | **13** | refused -EOPNOTSUPP | the vendor cap has no 320 bit |

**THE CROSS-LANE LESSON:** the pre-5.19 values were 160=4 / 80P80=5 / 320=6.
The sibling task-240 lane (`../dfs160/`) pinned those OLD numbers and fed the
real kernel enum into them, so a real 160 MHz chandef (value 5) was refused
and a real 80+80 (value 4) was classified as 160.  This lane pins the 6.18
values in the model, the selftest, the Python test and the kernel glue.

Vendor width -> value table [V, ../wifi-5g-160-dfs TABLES T7]:
wal_cfg80211_convert_width_to_value @0x101A98 = 20/20/40/80/80/160 (the
vendor's own 80+80 fallback to 80 is NOT implemented here: the capability
word lacks the bit and hostapd would refuse the chandef first).

The 0x0124 body decoded by the committed lane: `{band, bandwidth, channel, 0}`
[V], worker CA 0x841B8 stores body[2]->ctx+0x10, body[0]->ctx+0x11,
body[1]->ctx+0x12, the channel-table index->ctx+0x13.  This lane changes
body[1] from the always-zero legacy byte (it was built from `vap_id`, which
is 0 on the AP-start path) to the planned selector; a 0 selector is
byte-identical to the fired wire.

## T4. 160 MHz / 80 MHz single-segment centres (CCFS1) [I, IEEE]

| segment | control channels | CCFS1 |
| --- | --- | ---: |
| 5.2 GHz | 36-64 | 50 = 5250 MHz |
| 5.5 GHz | 100-128 | 114 = 5570 MHz |
| 5.8 GHz | 149-177 | 163 = 5815 MHz |

80 MHz centres: 36-48->42(5210), 52-64->58(5290), 100-112->106(5530),
116-128->122(5610), 132-144->138(5690), 149-161->155(5775),
165-177->171(5855).  Channels 140/144 are in NO 160 MHz segment: a 160 MHz
chandef there is refused (the driver-side belt; cfg80211 validates first).

## T5. The vendor band-1 channel table and the DFS set [V, TABLES T3/T4]

29 channels: 36 40 44 48 52 56 60 64 100 104 108 112 116 120 124 128 132
136 140 **144** 149 153 157 161 165 184 188 192 196.  The ch144 gap: the
image #3 5 GHz wiphy uses the band5g 29-channel list (ch144 present), but the
non-band5g static template stopped at ch140; this lane restores ch144
(5720 MHz, NO_IR|RADAR|INDOOR_ONLY) in that template too.

- DFS channels: 52-64 and 100-144.
- TDWR/weather window: frequency 5600..5650 MHz (vendor: freq-5600 <= 50) =
  channels **120/124/128** in the band-1 table.  The 600000 ms CAC class.
- The vendor per-channel DFS byte at `band+0x32A+idx*4+3`: bit0 = DFS,
  bit7 = TDWR [V].

## T6. CAC / NOL timing [V, ../wifi-5g-160-dfs TABLES T4/T5]

| constant | value | source |
| --- | ---: | --- |
| CAC default (UNII-2/2e) | 60000 ms | band+0xD38 |
| CAC TDWR (5600-5650 MHz) | 600000 ms | band+0xD3C |
| non-occupancy period (NOL) | 1800000 ms (30 min) | band+0xD58 |
| first CAC timer tick | 63000 ms | hmac_dfs_cac_start (cac_time > 59999 path) |
| re-arm tick | 60000 ms | hmac_dfs_cac_timeout_fn |
| tick count | floor(cac_ms / 60000) | the decoded multiply/shift formula |
| host-side cfg80211 CAC time | the op's `cac_time_ms` argument | cfg80211_chandef_dfs_cac_time |

The model takes `max(vendor class time, cfg80211 cac_time_ms)` so a finish is
never reported before cfg80211's own deadline (the core WARNs on an early
CAC_FINISHED).  60000 ms -> one 63 s tick; 600000 ms -> 63 s + 9 x 60 s =
603 s.  The TDWR 600000 ms reaches cfg80211 cleanly because the lane sets
`ieee80211_channel.dfs_cac_ms = 600000` for the 5600..5650 MHz channels.

## T7. The 0x07E7 DFS SET wire frame [V, ../wifi-5g-dfs-set]

| field | value |
| --- | --- |
| carrier | H2D g0 id1 config event, 12-B header + 8-B token + key/len/body + 4-B 0xFF tail |
| key | 0x07E7 (table A row 94, worker CA 0xB58FC) |
| body | 1 byte: 0x01 = ARM (CAC/radar detect), 0x00 = CLEAR |
| wire length | **29 B** |
| ARM KAT | `00 01 00 00 1d 00 01 00 00 00 5a 5a | 00 00 00 00 01 00 00 00 | e7 07 01 00 01 ff ff ff ff` |
| CLEAR KAT | identical with body byte (offset 24) = 00 |
| ARM register effect | bit0 of six band-1 SMAC sub-base u16 regs: CA 0x4010E000{1} +4{0} +8{1} +0xA0{1} +0xA4{0} +0xA8{1} => pattern **0x2D** |
| ARM flag effect | u8[mctx+0x4A5] = 1 only when `u32[mctx+0x4A0] == 0`; with the 0x0065 CAC context present the ARM is REGISTER-ONLY (flag stays 0) |
| CLEAR | bit0s to 0 only when the 0x2D pattern is read back; flag 0 |
| worker gates | helper u32[mctx+0x4AC] != 0; started module ctx (u8[mctx+0x0E] in 1..3); band = 1 |

## T8. The cfg80211 DFS contract on 6.18.60

```
int  (*start_radar_detection)(struct wiphy *, struct net_device *,
                              struct cfg80211_chan_def *, u32 cac_time_ms,
                              int link_id);
void (*end_cac)(struct wiphy *, struct net_device *, unsigned int link_id);
void cfg80211_cac_event(struct net_device *, const struct cfg80211_chan_def *,
                        enum nl80211_radar_event, gfp_t, unsigned int link_id);
```

The core sets `wdev->links[link_id].cac_started = true` when the op returns 0
(6.18 `nl80211_start_radar_detection`, no driver CAC_STARTED event is
required - mac80211 never sends one).  `cfg80211_cac_event(FINISHED)` WARNs
if it arrives before `cac_start_time + cac_time_ms`; `end_cac` is the abort
path and the core emits NL80211_RADAR_CAC_ABORTED itself, so this lane
reports exactly one event per CAC.  `cfg80211_cac_event` is added to the
lane's provider symvers because the luofu tree builds without CFG80211 (the
OpenWrt kernel provides the symbol at load time).

## T9. The ladder order contract [V\*, ../wifi-5g-dfs-order]

`vap_add -> desc_table(0x00F7) -> ap_chan(0x0124) -> ap_ssid -> ap_cfg(0x0127)
 -> vap_start_g0(0x0065) -> dfs_set(0x07E7) -> dfs_mode(0x01F7) -> band_start(0x0242)`

The 0x07E7 ARM must precede the 0x01F7 mode activation; the 0x0065 VAP start
must precede the 0x07E7 (the worker's started-module gate).  This lane does
NOT change the order: with `dfs_cac=1` the CAC runtime owns 0x07E7 and the
ladder's dfs_set step is forced off (mutual exclusion), i.e. the runtime ARM
must be posted outside the ladder (see REPORT sec. 4, the honest limit).

## T10. The two module parameters and their conflicts

| param | values | meaning |
| --- | --- | --- |
| `ht_bw` | 0 (default), 1 | 0 = legacy 0x0124 body[1]=0 (fired wire); 1 = derive 20/40/80/160 selector from the cfg80211 chandef, 160 MHz CCFS1-checked |
| `dfs_cac` | 0 (default), 1 | 0 = feature off (no op, no frame, pre-feature start_ap); 1 = start_radar_detection/end_cac CAC runtime + start_ap DFS gate |
| >1 | refused -EINVAL | before any frame |
| `dfs_cac=1` + `dfs_set!=0` | refused -EINVAL | both would own the 0x07E7 frames |
| ht_bw/dfs_cac on + `beacon_static=0` | refused -EINVAL | the frames live only in the beacon_static ladder |

## T11. Fire probes (read-only): conversions and expected values

Conversions: firmware RAM **phys = CA + 0x406B8000**; device IO
**phys = CA + 0x3B8000**; `mctx` is printed by the ladder
(`desc_table post: module ctx=0x...` / `dfs_set pre: mctx=0x...`).

| id | probe | expected |
| --- | --- | --- |
| D1 | module pins: size/sha/params | 383020 B / `006a94cf...` / 68 params / ht_bw+dfs_cac present |
| D2 | dmesg `start_ap 160MHz width selector` | `chandef width=5 -> body[1]=3 (160 MHz)` |
| D3 | u32 at FW+0x10 (module ctx channel word) | ch36 160: `0x00030124` (ch=0x24, band=1, bw=3, idx=0); ch52 80: `0x04020134` |
| D4 | dmesg `ap_chan` frame posted | the 0x0124 frame consumed after the post (the ladder line) |
| D5 | legacy control ht_bw=0 | **no** width-selector line; ctx+0x12 stays 0 |
| D6 | dmesg `dfs_cac CAC STARTED` | ch52: cac_ms=60000 tdwr=0 ticks=1 first=63000 NOL=1800000 |
| D7 | six SMAC regs phys 0x404C6000/04/08/A0/A4/A8 | during CAC: bit0 {1,0,1,1,0,1}=0x2D; after CLEAR: bits 0 |
| D8 | u8 at FW+0x4A5 (CAC flag) | 0 during the register-only ARM (the 0x0065 context is present) |
| D9 | CAC finish | >= 63000 ms after STARTED; hostapd gets NL80211_RADAR_CAC_FINISHED |
| D10 | dmesg `start_ap DFS gate PASSED` | after FINISHED; then the 0x0124 ch52 frame and the ladder |
| D11 | TDWR rung ch120 | cac_ms=600000 tdwr=1 ticks=10; finish ~603 s; elapsed >= 600000 ms |
| D12 | negative controls | ht_bw=2 / dfs_cac=2 / dfs_cac+dfs_set REFUSED lines before any frame |
| D13 | page golden | `/sys/bus/pci/devices/0000:00:00.0/boot` 0x86807FEF, status state 1 mask 0 |
| D14 | 5 GHz on-air (jackpot) | `iw dev OBS scan freq 5200` finds the SSID **GATED** on the 5 GHz beacon lane (see FIRE-RECIPE G3) |

## T12. Honest limits (each one is a fire decision)

1. **The 0x07E7 ARM before the 0x0065 VAP start is refused fail-closed.**
   hostapd asks for RADAR_DETECT *before* start_ap; the worker's
   started-module gate (state 1..3) is only set by the 0x0065 ladder step.
   The cold-CAC rung measures the refusal pack; the warm-CAC rung (a 5 GHz
   AP start first, host-side stop, then the DFS start) is the sanctioned path
   that can open the gate (see FIRE-RECIPE R2).
2. **The 5 GHz beacon TX** is still the firmware's band-1 scheduler (ledger
   2026-11-27 05:30 / tx-scheduler lane); the 160 MHz / DFS **on-air** PASS
   is therefore gated, while the host+firmware-state rungs stand alone.
3. **The radar D2H event id is unmapped**: no radar event is faked and the
   NOL gate is the host-side 30 min window after a finished CAC, not a radar
   detection.  A real radar hit path stays for the next lane.
4. `dfs_cac=1` is not testable on a 2.4 GHz-only load: the ladder refuses it
   when beacon_static/5 GHz is not the path (fail closed).
