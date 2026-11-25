# TABLES - addresses, keys, registers and tables for the 160 MHz + DFS decode

Lane wifi-5g-160-dfs (task-205), 2026-11-28. All firmware addresses are CPU
addresses in the project image-linear convention (file offset = CPU - 0x40000).
Host module addresses are .text/.rodata section offsets (the module is
relocatable; section bases: .text file 0x38, .rodata file 0x167290, .data file
0x242c18).
Grades: **[V]** re-derived by tools/fw_5g_160_dfs_re.py from the pinned
artifacts; **[V*]** bench-measured by a committed lane/the Lead; **[I]**
inference; **[U]** needs a device read.

## T1. Pinned artifacts

| artifact | value |
| --- | --- |
| FIRMWARE.bin | 928,920 B, sha256 7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b, CPU = file + 0x40000 |
| hi5622v100_wifi.ko | 3,581,748 B, sha256 22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2 |
| objdump | /home/shibbityshwab/openwrt-lab/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-objdump |
| firmware key table A | CA 0x10C23C, 217 x 8 B {u16 key; u16 pad; u32 worker|thumb} |

## T2. The H2D g0 id1 config keys this lane uses [V]

| key | table A row | firmware worker CA | role |
| --- | --- | --- | --- |
| 0x0124 | 108 | 0x841B8 | channel config: body {band, bandwidth, channel, 0}; stores ctx+0x10=body[2] channel, ctx+0x11=body[0] band, ctx+0x12=body[1] bandwidth, ctx+0x13=channel-table index |
| 0x0147 | 100 | 0x436BC | set bandwidth: body[0] -> obj+0x531 (MAC bandwidth byte; the WAL set_bw path) |
| 0x07E7 | 94 | 0xB58FC | CAC/radar state sync: body[0] 1 = arm (six radar-detect registers + flag obj+0x4A5=1), 0 = disarm |
| 0x0136 | 95 | 0xB5F3C | DFS radartool: body is the userspace radartool STRING (28-entry string table at CA 0x105B68) |
| 0x01F7 | 93 | 0xB5B9C | DFS/RF mode (field-B = per-module context; the F2 fix): mode 3 -> CA 0x89924 ch36-64 RF image |
| 0x00F7 | 92 | 0x45A30 | descriptor-table pool (CA 0x1ADC98) |

## T3. Firmware channel tables [V]

Firmware band-1 table CA 0x117AD4, 29 x u32 = {u8 channel; u8 0xFF; u8 0; u8 0}:

    36 40 44 48 52 56 60 64 100 104 108 112 116 120 124 128 132 136 140
    144 149 153 157 161 165 184 188 192 196

Firmware band-0 table CA 0x117B48, 14 x u32 = channels 1..14.

Host module tables (same channels): g_ast_channel_list_5G .data+0x3F0 (29 u32,
ch<<8 | 0xFF) and the 2.4 GHz list .data+0x3B8 (14 u32, ch<<8 | 0xFF).
hmac_dfs_channel_list_init walks the 29 band-1 entries and ORs the DFS flags
(below) per regdomain.

## T4. DFS channel flags and CAC timing [V]

hmac_dfs_channel_list_init @0xAC600: for each of the 29 channels, byte flags at
band+0x32A+idx*4+3 get bit0 = 1 when the channel is a DFS channel (radar/CAC);
bit7 = 1 additionally when the channel frequency is in **5600..5650 MHz**
(TDWR/weather band: ch 100..128 window; the code computes freq-5600 and tests
<= 50). The regdomain type byte [regdomain+3] is read: type 2/5 also takes the
special CAC path.

hmac_dfs_init @0xAC588 fills the per-band defaults:

| field | value | meaning |
| --- | --- | --- |
| band+0xD30 | 0x01000101 | DFS flags/enables |
| band+0xD38 | **60000 ms** | CAC time (default UNII-2/2e) |
| band+0xD3C | **600000 ms** | CAC time for the 5600-5650 MHz TDWR window |
| band+0xD40 | 360000 ms | radar/off-channel bookkeeping |
| band+0xD44 | 3600000 ms | (60 min) |
| band+0xD58 | **1800000 ms** | non-occupancy period (30 min) |
| band+0xEA0/0xEA4 | self-pointers | empty NOL list head |
| band+0xEA8 | 0 | radar-detect counter |
| band+0xEAC + 4*type | 0 | per-radar-type counters (type 0..8) |

## T5. CAC state machine (host, vendor module) [V]

| symbol | address | role |
| --- | --- | --- |
| hmac_dfs_init | 0xAC588 | band DFS defaults/counters/NOL list |
| hmac_dfs_channel_list_init | 0xAC600 | per-channel DFS/TDWR flag computation |
| hmac_cac_check_and_start_bss | 0x13FFA8 | AP start on a DFS channel: if vap DFS enabled -> hmac_dfs_init_scan_hook, then (on success) start BSS; else start all BSS |
| hmac_dfs_cac_start | 0xAD420 | send 0x07E7{1}; store channel/band at band+0xED3/0xED4; pick CAC time; arm 63 s tick timer; hmac_report_cac_start |
| hmac_dfs_cac_timeout_fn | 0xAE584 | tick: counter++; if counter<max re-arm 60 s tick; else complete: 0x07E7{0}, per-extra-channel cleanup, FSM report, hmac_start_all_bss_of_band, timer unregister, hmac_report_cac_finish |
| hmac_dfs_cac_stop | 0xACD2C | abort: 0x07E7{0}, timer unregister, clear band+0xDC0 bit1, CAC report |
| hmac_report_cac_start | 0xAD22C | HCC report message to the firmware (cac start) |
| hmac_report_cac_finish | 0xACAEC | HCC report message (cac finish) |
| hmac_config_cac_sync_state | 0xAD1D4 | generic 0x07E7 sender (1-byte body) |
| hmac_dfs_radar_detect_event | 0xAC7C0 | D2H radar handler: band from hdr[1]>>4, channel/type fields, counters, hmac_phy_event_stop, AP FSM event 4/arg 2 |
| hmac_dfs_radar_wait | 0xAD7A4 | radar wait window handling |
| hmac_dfs_switch_channel_for_radar | 0xADBCC | CSA to a new channel after radar |
| hmac_dfs_ap_up_radar_handler | 0xAE218 | AP-up radar event entry |
| hmac_dfs_nol_timeout_fn | 0xAB7FC | non-occupancy-period expiry |
| hmac_dfs_off_chan_cac_* | 0xAB034/0xAB7FC | off-channel CAC (multi-AP/off-channel variants) |
| hmac_check_set_channel_radar_status | 0x763D4 | pre/post channel-set radar status check |
| hmac_dfs_set_channel | 0x4799C / 0x48AC4 | channel switch for DFS |
| wal_cfg80211_set_chan_dfs_check | 0x102980 | cfg80211-level DFS channel availability check (status byte & 0x7F vs 4) |
| wal_cfg80211_chan_dfs_check_resume | 0x102B44 | DFS check/resume; sends a manual 0x0124 channel event after the check |

CAC tick arithmetic: hmac_dfs_cac_start compares the band CAC time against
59999; if > 59999 it computes max = (cac_time * 0x45E7B273) >> 46 (== cac_time
/ 60000 floor) and registers the timer with a **63000 ms** timeout; otherwise it
registers the timer with **60000 ms**. hmac_dfs_cac_timeout_fn re-arms with
60000 ms per tick and completes when counter == max (so 60 s CAC = 60000 ms
direct path; 600 s = 600000 ms -> max 10, tick 63 s -> ~630 s).
[V] constants [V] formula; the exact timer unit is the vendor hcc_timer API
(ms [I], consistent with 60000/63000/1800000).

## T6. DFS radartool (H2D key 0x0136) [V]

The vendor userspace sends a radartool command STRING; the host handler
hmac_config_dfs_radartool @0x71CD8 parses the first token against a 33-entry
host command table (.data+0x117C), calls the matched handler, then forwards the
original string verbatim to the firmware as config event key 0x0136. The
firmware worker CA 0xB5F3C parses the same string against a 28-entry table at
CA 0x105B68 and calls the matched firmware handler. The vendor userspace calls
observed in the stock scripts are:
"radartool dfsenable <0|1>", "radartool cacenable <0|1>",
"radartool set_next_chan ..." (stock /lib/netifd/mac80211.sh:1896-1909,1996;
../wifi-vendor-userspace/TABLES.md). The firmware command-name table strings
are read from the pinned image by the tool (derived names only).

## T7. 160 MHz tables and code [V]

| item | location | value |
| --- | --- | --- |
| vendor wiphy VHT cap word (5 GHz) | driver/static table (project hi5622_band5g.c) | 0x00400064 = BIT22 HTC_VHT | BIT2 SUPP_CHAN_WIDTH_160MHZ | BIT5 SGI80 | BIT6 SGI160 |
| cfg80211 width -> vendor value table | host .rodata file 0x170620 (6 x u32) | 20, 20, 40, 80, 80, 160 for NL80211_CHAN_WIDTH 0..5 (5=80+80 falls back to 80) |
| value -> VHT width code | hmac/wal_cfg80211_convert_value_to_vht_width 0x101328 | 80 -> 1, 160 -> 2, else 0 (VHT Operation channel width code) |
| channel config event | hmac_config_set_channel 0x764E4 / hmac_chan_follow_reset_channel 0x76D4C | key 0x0124, len 4, body {band, bandwidth, channel, 0} |
| host bandwidth bookkeeping | hmac_band_set_channel_and_bandwith 0x13F520 | band+0xD29 = channel, band+0x10D2 = bandwidth (host-only; no wire byte) |
| 0x0124 worker | CA 0x841B8 | stores body[2]->ctx+0x10 channel, body[0]->ctx+0x11 band, body[1]->ctx+0x12 bandwidth, index->ctx+0x13 |
| 5 GHz RF branch | CA 0x40684 | requires device-type 2 and CA 0x944DE == 1; calls CA 0x101890(band, field=ctx+0x12, channel), CA 0xA2FA2, CA 0x89840 |
| width/descriptor selector | CA 0x101890 | band != 1 -> 0; validates channel via CA 0x10172C; selects a 1..8-word descriptor window by field (0..6 and 7..14 cases) from table CA 0x10BE68 (row = channel-table index); mapper CA 0x101650; returns the predicate |
| RF descriptor parameter table | CA 0x10BE68 (literal at CA 0x101BA0 = 68 BE 00 10) | per channel-index rows; window offsets +0x138..+0x16C selected by the field |
| RF register image | CA 0xA2FA2; ch36-64 image CA 0x89924 | ~30 words at R+0x3A8..R+0x418; image chosen by channel-range only (ch36-64 vs ch100+) |
| RF predicate latch | CA 0x89840 | latches the CA 0x101890 result into R+0x410 bit0 |

Channel layout (160 MHz, single segment; CCFS2 = 0 because the vendor cap has
the 160 bit but not the 80+80 bit) [I, IEEE channelization]:

| 160 MHz segment | control channels | CCFS1 (center) |
| --- | --- | --- |
| 5.2 GHz | 36-64 | 50 (5250 MHz) |
| 5.5 GHz | 100-128 | 114 (5570 MHz) |
| 5.8 GHz | 149-177 | 163 (5815 MHz) |

80 MHz CCFS1 for reference: 36-48 -> 42, 52-64 -> 58, 100-112 -> 106,
116-128 -> 122, 132-144 -> 138, 149-161 -> 155, 165-177 -> 171.

## T8. Driver channel flags (image #3 module tree) [V]

HI5622_CH5G(ch, freq, fl) with HI5622_DFS = IEEE80211_CHAN_NO_IR |
IEEE80211_CHAN_RADAR | IEEE80211_CHAN_INDOOR_ONLY. The advertised 5 GHz set is
36,40,44,48 (flags 0) and 52,56,60,64,100..140 (HI5622_DFS). Channels 144,
149-165 and 184-196 are NOT advertised today. cfg80211 6.18 accepts a
NL80211_CHAN_WIDTH_160 chandef only if (a) the channel does not carry
IEEE80211_CHAN_NO_160MHZ and (b) the wiphy VHT cap SUPP_CHAN_WIDTH field is
160MHZ or 160_80PLUS80 (net/wireless/chan.c:1270-1286). 0x00400064 & 0xC == 4
-> passes; the DFS flag macro does not set NO_160MHZ (bit8 = 0x100) -> passes.
No driver change is needed for capability eligibility; the width selector and
the hostapd config are the missing pieces.
