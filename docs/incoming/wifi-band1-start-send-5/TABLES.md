# TABLES - lane wifi-band1-start-send-5 (task-213, the R5 order)

All facts are committed decodes or the 2026-11-26 fire readback; grades:
**[V]** decode re-derived in a committed lane, **[V\*]** bench-measured,
**[I]** inference, **[U]** needs a device read.

## T1. The 5 GHz AP-start order: stock vs R4b vs R5 [V decode / V\* fire]

| # | stock order (real) | R4b ladder (fired, no beacon) | R5 ladder (this lane) |
| ---: | --- | --- | --- |
| 1 | VAP add (0x0064) | vap_add | vap_add |
| 2 | descriptor table (0x00F7) | desc_table | desc_table |
| 3 | DFS/RF mode (0x01F7) | dfs_mode | dfs_mode |
| 4 | channel config (0x0124) + wait | ap_chan | ap_chan (+ the wait) |
| 5 | **AP config (0x0127: ap_ssid, ap_cfg)** | vap_start_g0 (0x0065) | **ap_ssid** |
| 6 | **start_vap (g0-id1 0x0065)** | band_start (0x0242) | **ap_cfg** |
| 7 | green_ap (0x0242) | ap_ssid | **vap_start_g0 (0x0065)** |
| 8 | (host) VAP start frame | ap_cfg | **band_start (0x0242)** |
| 9 | | ap_vap_start | ap_vap_start |

The stock order is VAP add -> channel config -> AP config -> start_vap ->
green_ap enable (../wifi-band-start-prestart T8; ../wifi-fw-beacon-tx
section 2 lists the H2D chain in the same order).  R5 matches it; the R4b
ladder started the band before the AP config was staged.

## T2. The R5 plan (hi5622_beacon_plan, all knobs 1) [V host-tested]

| index | enum | value | step name | enabled by |
| ---: | --- | ---: | --- | --- |
| 0 | HI5622_BEACON_STEP_VAP_ADD | 1 | vap_add | always |
| 1 | HI5622_BEACON_STEP_DESC_TABLE | 2 | desc_table | desc_table == 1 |
| 2 | HI5622_BEACON_STEP_DFS_MODE | 3 | dfs_mode | dfs_mode == 1 |
| 3 | HI5622_BEACON_STEP_CHAN | 4 | ap_chan | always |
| 4 | HI5622_BEACON_STEP_SSID | 5 | ap_ssid | always |
| 5 | HI5622_BEACON_STEP_AP_CFG | 6 | ap_cfg | always |
| 6 | HI5622_BEACON_STEP_VAP_START_G0 | 7 | vap_start_g0 | vap_start_g0 == 1 |
| 7 | HI5622_BEACON_STEP_BAND_START | 8 | band_start | band_start == 1 |
| 8 | HI5622_BEACON_STEP_VAP_START | 9 | ap_vap_start | vap_start == 1 |

The no-0x0065 plan is 8 steps (same order minus vap_start_g0); the minimal
plan (all knobs 0, vap_start 0) is vap_add ap_chan ap_ssid ap_cfg; the legacy
non-beacon_static path keeps its old full-sequence order unchanged.

## T3. The start_ap post sites (hi5622_scan.c, the beacon_static path) [V]

| phase | frame(s) | posted after | posted before |
| --- | --- | --- | --- |
| desc_table | 0x00F7 | vap_add | dfs_mode |
| dfs_mode | 0x01F7 | desc_table | ap_chan |
| ap_chan | 0x0124 + channel-activation wait | dfs_mode | **the AP config phase** |
| **AP config phase** (new helper, frames 0..2) | **ap_ssid (+ ap_auth on WAL), ap_cfg (0x0127: interval/DTIM/template)** | **ap_chan + wait** | **vap_start_g0** |
| vap_start_g0 | 0x0065 (G1..G13 pre, G12 post) | **ap_cfg** | band_start |
| band_start | 0x0242 (G12-gated, optional fallback) | **vap_start_g0** | phase B |
| phase B (new helper, frame 3) | carrier-owned VAP start (WAL g2 only; -ENODATA on g0) | band_start | the g0 'ap_vap_start' post |
| ap_vap_start (g0) | the vendor VAP-start frame | phase B | the commit line |

The helper `hi5622_scan_post_ap_seq_frames()` is the only new code path: it
carries the original per-frame build/post/log body and the -ENODATA
"past the end of the carrier sequence" contract, so the bytes on the wire are
unchanged.  No frame builder, no field-B value and no parameter changed.

## T4. Why the AP config must precede the start [V decode + I]

| message | firmware effect | source |
| --- | --- | --- |
| 0x0127 AP config (worker CA 0x84DA4) | stages VAP fields only: +0x10 beacon interval, +0x14 DTIM, RSN/vendor IE copies, +0x138/+0x13A MDID/capability bits, +0x34C/+0x360 HT/VHT hints; **no band, no enable, no beacon object** | ../wifi-fw-beacon-tx T4 |
| 0x0065 VAP start (worker CA 0x46394) | on the first start (guard `u8(perlink+0x6C6) != 1`) allocates the 280-B beacon buffer (+0x6B8), the 92-B descriptor (+0x6C0), builds the 0x310-byte beacon object from ctx+0x4AC, publishes +0x6BC, sets +0x6C4=16/+0x6C6=1, then calls the band start CA 0x971D8 | ../wifi-fw-beacon-tx T5 |
| band start CA 0x971D8 | G+0x370 MAC-start hook, G+0x374 beacon-start hook (G = CA 0x1747C4, runtime) | ../wifi-fw-beacon-tx section 6 |

In the R4b order the beacon state was created at the 0x0065 **before** the
0x0127 staged the interval/DTIM/template, and the +0x6C6 guard means a later
0x0065 re-post cannot rebuild it; the 0x0242 armed the band anyway (state[4]
0x00, "already started").  The wifi-fw-beacon-tx lane names the same fix
("a single ordered re-post: VAP add band=1 -> AP config -> VAP start",
cause C1).  R5 is exactly that order on the host side.

## T5. Verification probes (read-only) [V\* addresses / U reads]

| # | object | phys | PASS |
| ---: | --- | ---: | --- |
| P1 | band-1 arm word 0x4010C220 | 0x404C4220 | 0x1029 |
| P9 | band-1 run-state 0x40817A40 | 0x40817A40 | u8+4=0x00, u8+1=2, u32+0x64 counter incremented |
| P12 | R+0x3A8 / R+0x3AC | two-step via perlink+0x2D8 | 0x01800060 / 0x03E6138A |
| P13 | on-air beacon | 'iw dev OBS scan freq 5200' | the ch40 SSID |
| P14 | per-link beacon state | ../wifi-fw-tbtt-arm R3 pack | +0x6B8/+0x6C0 nonzero, +0x6C4=16, +0x6C6=1 |

## T6. Chain and artifact pins (host build, never committed)

| item | value |
| --- | --- |
| base tree | /tmp/r4border-build + ../wifi-vap-start-gates/patches/0001 = the gate-inverted tree |
| base artefact | 382,376 B, sha256 9bf1d846e730ce57a5adfef9a2fed9ab589fcba159b6cd08165339917673cf82 (deployed) |
| R5 patch | patches/0001-r5-order.patch, 25,415 B, sha256 e464f4cf0038859705be492967771e7c3294ef7ba84275c96bb4b473a6b86cf7 |
| R5 test patch | patches/0002-r5-selftest-update.patch, 4,857 B, sha256 f35d34e4370da5469d24b0defd078aec8dacd4895b2e1226d8135c72d32cba4e |
| R5 tree pins | hi5622_scan.c 86d6c57e..., hi5622_scan_logic.c 15c50d34..., hi5622_scan_logic.h 6af774ef..., hi5622_vap_start_g0.c 346b5441..., hi5622_vap_start_g0.h 81533d9c..., hi5622_band_start.h e2f059cf..., hi5622_band_start.c 71019718..., Kbuild 1bb51085... (full shas in BUILD-NOTES) |
| R5 artefact | /tmp/r5-artifact/hi5622_wifi.ko, **383,240 B**, sha256 **24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c**, 65 params |
| host tests | 295 C + 110 Python = 405 checks, 0 failures |
