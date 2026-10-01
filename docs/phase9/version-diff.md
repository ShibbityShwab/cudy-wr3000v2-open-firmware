# Vendor release diff: 2.4.15 versus 2.5.24 (phase 9, 2026-10-01)

Produced by the lab loop: the same analyzers over the two official rootfs images, output diffed.
Inputs: `lib/firmware/hi_wifi/FIRMWARE.bin` and the driver module from each image (2.4.15 keeps its
modules in `lib/hisilicon/ko/`, 2.5.24 in `lib/modules/5.10.201/`).

## Headline

**The Wi-Fi firmware blob did not change between the two releases.** Both images ship
`FIRMWARE.bin` with sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b`, byte for
byte. Every firmware-side artifact therefore matches: the dispatcher dump and the symbol tables are
identical.

**The driver module did change.** sha256 `22fa789a...` (2.4.15) versus `de78ec07...` (2.5.24), and the
change is legible from the symbol tables alone:

| metric | 2.4.15 | 2.5.24 |
| --- | --- | --- |
| functions | 4,247 | 3,847 |
| `.text` size | `0x16285c` | `0x1630cc` |
| command table | 414 entries | identical, 414 entries |

**Removed in 2.5.24** (a whole functional family): the host-side spectral-scan analysis, e.g.
`alg_cfg_args_analysis_spectral_scan_deci_coef_en`, `..._agc_lock_en`, `..._add_win`,
`alg_cfg_args_analysis_spectral_dbm_offset`, plus `alg_cfg_analysis_args_head`,
`alg_cfg_args_analysis_hardamard_enable/_mode`, `alg_cfg_args_analysis_protect_mode` and
`alg_cfg_args_analysis_always_rx_set_delay`.

**Added in 2.5.24:** per-chip user limits and RSSI thresholds, e.g.
`hmac_config_get_chip_max_user`, `wal_config_get_chip_max_user`, `wal_ioctl_get_max_user`,
`wal_config_get_rssi_access_th`, `wal_config_get_rssi_warn_th`,
`wal_config_set_rssi_access_th`, `wal_config_set_rssi_warn_th`.

## Why this matters

- The radio firmware is frozen across these releases, so any behaviour difference between 2.4.15 and
  2.5.24 is driver-side. That is a useful boundary for anyone reproducing or porting this stack.
- The command surface did not change (414 entries in both), which means the `alg` interface is stable
  across the vendor's releases: a control tool written against one works on the other.
- The added RSSI threshold controls are the kind of feature that shows up in the vendor UI as better
  roaming or client-limit handling; the removed spectral-scan family suggests the feature was either
  dropped or moved into the firmware without changing the blob.

## Reproduce

    PY=<venv>/python.exe sh lab/run_all.sh 2.4.15 /path/2.4.15/FIRMWARE.bin /path/2.4.15/hi5622v100_wifi.ko
    PY=<venv>/python.exe sh lab/run_all.sh 2.5.24 /path/2.5.24/FIRMWARE.bin /path/2.5.24/hi5622v100_wifi.ko
    diff -r lab/out/2.4.15 lab/out/2.5.24
