# HOST-IMPLEMENTATION - the driver and hostapd changes for 160 MHz + DFS

Lane wifi-5g-160-dfs (task-205). Scope: what the clean-room cfg80211 driver and
the hostapd userspace need, on top of the current image #3 module tree
(local build tree /tmp/wifi-image-3-module; the committed lane sources are
../wifi-5ghz-beacon/, ../wifi-desc-table-send/, ../wifi-dfs-mode-send/ and the
image #3 lane). Nothing here is built by this lane; this is the implementation
spec, graded [V] where the vendor path is decoded and [I] where the encoding is
inferred.

## A. 160 MHz

### A1. cfg80211 width -> vendor bandwidth selector (new)

The 0x0124 body byte 1 is the bandwidth selector consumed by the firmware
(REPORT-160MHZ.md section 1.4). Map the chandef width:

| NL80211_CHAN_WIDTH | value | selector | source |
| --- | --- | --- | --- |
| 20_NOHT | 0 | 0 | [V] vendor table 20 |
| 20 | 1 | 0 | [V] vendor table 20 |
| 40 | 2 | 1 | [I] assemble order 40 |
| 80 | 3 | 2 | [I] assemble order 80 |
| 160 | 4 | 3 | [I] assemble order 160 (PROBES P-160a decides) |
| 80P80 | 5 | refuse -EOPNOTSUPP | [V] vendor cap lacks the 80+80 bit |
| 320 | 6 | refuse -EOPNOTSUPP | [V] 320MHZ unsupported |

The vendor reference for the MHz values is wal_cfg80211_convert_width_to_value
(0x101A98, table 20/20/40/80/80/160) and the reverse
wal_cfg80211_convert_value_to_vht_width (0x101328). Our driver keeps its own
table; the [I] assemble values must be pinned by P-160a before the 160 rung is
treated as anything other than an experiment.

### A2. The 0x0124 frame builder

Where: hi5622_band5g_ap_chan_prepare() (hi5622_band5g.c) and its callers in
hi5622_scan.c / hi5622_ap_frames.c. Today the builder returns a struct with
band/channel only and the ladder's 4-byte body is {band, 0, channel, 0}
(hi5622_band5g.h documents it as {band, vap, channel, 0} - the vendor decode
proves the second byte is the bandwidth selector).

Change:
1. Add a bandwidth selector to struct hi5622_band5g_ap_chan (u8 bandwidth).
2. Extend hi5622_band5g_ap_chan_prepare() with the cfg80211 width enum (or a
   new hi5622_band5g_width_to_bw(width, &bw) helper) - fail closed for
   80P80/320/unknown.
3. In the frame builder, body[1] = selector instead of 0. Keep every other
   byte and the 2.4 GHz path byte-identical (2.4 GHz width stays 0/1 and the
   firmware band-0 path is not the RF path this feature touches).
4. The driver's start_ap should also reject width 160 when the chandef is not
   a valid contiguous 160 segment for the control channel (center_freq1 must
   equal the CCFS1 of the segment; cfg80211 already validates this - keep the
   cfg80211 error if it does).
5. Preserve the ap_chan_wait_ms readback gate: after the 0x0124 post, the
   module channel word (ctx+0x10..0x13) is channel | band<<8 |
   bandwidth<<16 | index<<24 (../wifi-fw-chan-worker TABLES T2); the wait
   predicate should compare the channel/band and now also allow the new
   bandwidth byte. Do NOT change the wait gate before the bandwidth value is
   confirmed by P-160a (a mismatch would keep failing closed - acceptable).

### A3. he_caps/HE160

The image #3 module already advertises 5 GHz HE caps including the 160 MHz
channel width (he_caps=1, ../wifi-ax-he-caps/). For the first rung keep
ieee80211ax=0 (VHT160) so only one variable (the width selector) changes; the
HE160 rung follows after a VHT160 PASS.

### A4. hostapd

Use hostapd/hostapd-160.conf: hw_mode=a, channel=36, ieee80211ac=1,
vht_oper_chwidth=2, vht_oper_centr_freq_seg0_idx=50, ssid open. The driver
wiphy already carries the 160 bit (0x00400064), so no vht_capab override is
needed; hostapd uses the driver's caps when vht_capab is unset. If the local
hostapd build refuses the width (no 160 support in the built VHT caps), add
an explicit vht_capab with the VHT160 token - but do not change the driver
capability word, it is vendor-proven.

## B. DFS/CAC

### B1. cfg80211 radar-detect op (new)

Register in hi5622_cfg11_ops (hi5622_cfg80211_ops.c):

    .start_radar_detection = hi5622_scan_op_start_radar_detection,

Implement (fail closed, one meaning per call):
1. Refuse unless the wdev is AP mode, the driver is above the boot-ready gate
   (the same gate as start_ap) and the channel is one of the driver's
   radar-flagged channels (52-64/100-144).
2. Send H2D g0 id1 key 0x07E7, 1-byte body {01} (the existing
   hi5622_frame_post path; reuse the 0x01F7/0x00F7 posting helpers).
3. Record cac_channel, cac_band, cac_time = 60000 ms (TDWR window 5600-5650:
   600000 ms) and arm a kernel timer; cfg80211 core emits
   NL80211_RADAR_CAC_STARTED to hostapd.
4. Timer callback: send 0x07E7{0}, call cfg80211_cac_event(wdev,
   NL80211_RADAR_CAC_FINISHED), clear state. This mirrors the vendor
   hmac_dfs_cac_start/timeout_fn (REPORT-DFS.md T5) with the same constants.
5. Implement .end_cac / abort on stop_ap: send 0x07E7{0}, cancel timer,
   cfg80211_cac_event(NL80211_RADAR_CAC_ABORTED) if needed.
6. Radar D2H wiring: the radar event handler is decoded (CA 0xAC7C0 reads
   band/channel/type) but the D2H dispatch id is not yet mapped [U]. Until
   it is, do not fake radar events; the CAC-only path is honest and safe.

### B2. Channels and flags

In hi5622_channels_5g (hi5622_cfg80211.c):
1. Add ch144 (5720 MHz) with HI5622_DFS. The vendor band-1 table includes it;
   the DFS set is 52-64 U 100-144.
2. Keep NO_IR|RADAR|INDOOR_ONLY on both DFS groups (the US/DFS-FCC regdomain
   from the wifi-regdom-fix lane is the prerequisite; without the fixed
   cfg80211 the regdb rejects US).
3. Do not set NO_160MHZ on any channel (it would break the 160 MHz chandef
   check); the flag macro must stay as is.
4. If the driver ever grows an ACS/channel-status path, mirror the vendor
   status byte semantics (band+0x32A+idx*4+3 & 0x7F == 4 usable).

### B3. hostapd

Use hostapd/hostapd-dfs.conf: hw_mode=a, channel=52, ieee80211h=1,
ieee80211d=1, country_code=US, ieee80211n=1, ieee80211ac=1,
vht_oper_chwidth=0, ssid open. hostapd (v2.12, the project's hostapd package
set) then runs the CAC before enabling the AP. Caveat: hostapd's DFS module
needs the driver to advertise the DFS channels with the RADAR flag and may
need the wiphy's radar_detect_widths / NL80211_EXT_FEATURE_RADAR_DETECT
capability if it decides to offload; for the in-firmware CAC we implement the
driver-side start_radar_detection op and hostapd uses the nl80211 CAC flow
(not dfs_offload). Do not set dfs_offload=1.

### B4. Dependency gates

Both A and B are gated on the 5 GHz band-1 MAC start (H2D 0x0242, body
{01,00,01,00,00}, field B = the fresh band-1 module context; tasks 197/206).
The driver must NOT enable the 0x0124 width selector or the 0x07E7 CAC path
until that rung is bench-proven: post them behind new read-only module
parameters with fail-closed defaults (like band5g/desc_table/dfs_mode/
he_caps), so the current bench-validated behaviour stays byte-identical by
default. Suggested: ht_bw (0=off/current, 1=selector from chandef) and
dfs_cac (0=off, 1=start_radar_detection Cac op + ch144). One meaning per
load; refuse impossible values at use time before any frame.
