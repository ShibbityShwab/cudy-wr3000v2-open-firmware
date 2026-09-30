# WR3000 V2.0 Wi-Fi stack: black-box map (2026-10-01)

Everything below was read from the running device (root) or from the vendor binaries pulled from it.
No driver or firmware was modified.

## 1. Silicon and PCIe identity

- Two PCIe endpoints, `0000:00:00.0` and `0001:00:00.0` (one per radio), both:
  - vendor `0x59e7`, device `0x0005`, class `0x028000` (network controller, "other"),
  - subsystem vendor `0x19e5` (HiSilicon), 64-bit memory BARs.
- `iwpriv Hisilicon0 get_chipid` -> `chip id:0x34 version:0x00`.
- `iwpriv Hisilicon0 get_dieid` -> 16-word device-unique die ID (readable, not reproduced here).
- Private ioctl table on the Wi-Fi netdev `Hisilicon0`:
  `get_mode (8BF1)`, `setparam (8BE0)`, `getparam (8BE1)`, `sethwaddr (8BEA)`,
  `get_channel (02B1)`, `channel`, `get_chipid (03E8)`, `get_dieid (02FA)`,
  `alg (0101, set 500 chars / get 1000 chars)`.

## 2. Driver architecture (from the module's own symbols)

`lib/modules/5.10.201/hi5622v100_wifi.ko` (3,564,728 bytes) keeps a full symbol table:
**3,847 functions, 1,243,224 bytes of code**, layers by prefix:

| prefix | functions | role |
| --- | --- | --- |
| `hmac_` | 1853 | host MAC / MLME state machines (rates, aggregation, EDCA, DFS, calibration sync) |
| `wal_` | 990 | WLAN abstraction: cfg80211 glue (`wal_cfg80211_*`), PCIe link work (`wal_pcie0/1_link_down_work`) |
| `mac_` | 299 | MAC helpers |
| `hdpp_` | 208 | host data path (RX/TX queues, rings: `hdpp_stat_rx_*`) |
| `alg_` | 199 | algorithm layer (rate control, calibration parameter engines) |
| `shuangta_` | 97 | chip-codename-specific layer ("ShuangTa") |
| `hal_` | 93, `oal_` 42, `oam_` 33 | hardware abstraction, OS abstraction, operations |

Notable named subsystems: `hmac_tx_msdu_ring_init`, `hmac_chan_tx_cali_sync`,
`hmac_save_cali_data_to_file_2g/5g`, `hmac_config_pcie_magic_num_err_pro_stage`,
117 cfg80211-related symbols, and `hi_wifi_*` hooks into the SoC packet engines.

## 3. Firmware image

- `/lib/firmware/hi_wifi/FIRMWARE.bin`: 928,920 bytes, entropy ~7.06 (dense/compressed mix),
  header starts `71 69 04 00 2d 74 0c 00`.
- The INI defines the load map (addresses are in the chip's own space, not file offsets):
  `firmware_itcm_src_addr=0xf009c len=0xadd0`, `firmware_dtcm_src_addr=0xfae6c len=0x1098`,
  `firmware_custom_src_addr=0x1b2800 len=0x10` -> the driver copies ITCM/DTCM segments into the radio.
- The INI's own header identifies the board build:
  `2g_ipa_5g_ipa_AX3000_0615-oversea_CHENTANG-normal for V600_Cudy-WR3000v2_20250905`.

## 4. RF behaviour is data-driven (`/lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini`, 32 KB, 3 sections)

- `[HOST_WIFI_NORMAL]` (~186 keys): `cali_upc_protect_limit_2g=0x01ff01ff`,
  `cali_temp_change_pow_amend=0x00050005`, `rf_pll_ppm_correct/threshold`,
  `feature_flags=0xc03`, `beacon_2g/5g_tx_policy`, `smartant_board_type`,
  CCA energy-detect deltas per channel width, and the firmware segment table above.
- `[HOST_WIFI_PWR_LIMIT_LU]` (186 keys): per channel and per rate power limits, e.g.
  `pwr_limit_2g_11b_ch01=0x00181818`, `pwr_limit_2g_11g_ch01=0x001E1E1E` (three bytes = one limit
  per spatial stream/mode), plus 11a/11n/11ac/11ax variants per channel.
- `[CUSTOM_REGDOMAIN_CFG]` (16 keys): country-tagged band plans, e.g.
  `custom_regdomain_02_ALPHA2=LU`, `custom_regdomain_02_BAND3=5490,5710,160,0,30,24`
  (start MHz, end MHz, max width, ?, max EIRP dBm, DFS/indoor flag).

## 5. Live calibration API (read-only commands used)

- `iwpriv Hisilicon0 alg get_2g_power_param` -> 27 hex tuples, e.g. `17161605 ... 0a0606ff`
  (per-rate/per-stream factory calibration offsets).
- `iwpriv Hisilicon0 alg get_xo_ppm_cali_param` -> `00006060` (crystal offset).
- `iwpriv Hisilicon0 alg get_rssi_param` -> 10 zero words (no RSSI cal stored on this unit).
- Command vocabulary in the binary includes `get/set` pairs for: 2g/5g power params (normal and
  low-power), 2g/5g all-curve params, xo ppm and xo duty calibration, rssi, edca/wmm, scan, aie,
  whitelist, tx params (86 `*_param`-style commands found).
- Calibration pipeline on the device: `/usr/bin/wifi_cal_init.sh` (reads `/usr/local/factory/wifi.cal`
  and applies it), `/usr/bin/hi5622v100_cal_get.sh` (dumps chip calibration into
  `/usr/local/factory/hi5622v100.cal` via the `alg get_*` commands), plus factory-partition files
  `wifi_cali_data.kv` / `wifi_cali_data_2g.kv`.

## 7. Calibration snapshot toolkit (built and verified 2026-10-01)

- `tools/wifi-cal-snapshot.sh` collects, from the running device into a timestamped bundle under
  `build/cal-snapshots/<UTC>`, with a `MANIFEST.sha256` over every file:
  - `identity.txt`: kernel, firmware marker, slot, chip id (`0x34 rev 0x00`), die id, mode, channel.
  - `alg-values.txt`: every readable `iwpriv Hisilicon0 alg get_*` value. Live captures:
    2g power params (27 tuples), 5g power params (18 tuples), low-power variants for both bands,
    2g/5g all-curve params (12 and 32 words), xo ppm `00006060`, xo duty `000016d7`, RSSI (10 zeros).
    Commands the driver/firmware does not implement answer `[FAIL]` (polynomial, cfg, edca, wme, wmm,
    scan, tx, aie, stru, sub, ...), and `get_ar_dev_param` reports
    `Invalid CMD input, param num[0] invalid` - proof the parser validates arity.
  - `factory-cal.txt`: `/usr/local/factory/wifi.cal` (empty on this unit), `hi5622v100.cal` (holds the
    same calibration values the driver reports), and the binary `wifi_cali_data*.kv` files.
  - `module-params.txt` (`g_en_rx_packet_4096_length=0`), `wireless-config.txt` (UCI: radio0 type
    `mac80211`, chiptype `hsanwifi`, phy0, txpower 100 %, htbw auto, maxstanum 64, hwmodes 11bgnax),
    plus the Wi-Fi INI and the 929 KB firmware blob.
- `tools/wifi-cal-restore.sh <snapshot> [--apply]` verifies the manifest, prints the exact factory files
  and `alg set_*` values it would write, and refuses to touch anything without `--apply`.
- Verified this run: snapshot `build/cal-snapshots/20260930-195439`, `sha256sum -c` OK for all 8 files,
  and the dry-run restore printed the full plan.
- Persistence test (2026-10-01): after a full reboot the chip reports the **same** calibration values
  (2g power tuples byte-identical, xo ppm `00006060`), so the state is persistent, re-loaded from
  `/usr/local/factory/hi5622v100.cal` at boot. A restore bundle is therefore insurance against an
  accidental runtime change, not a per-boot step.

## 9. Live power observables (read-only anchors for any tuning, captured 2026-10-01)

- `iw phy phy0 info` (2.4 GHz): every channel listed at **30.0 dBm** max.
- `iw phy phy1 info` (5 GHz): **23.0 dBm** on channels 36-48, **24.0 dBm** on channels 52-56.
- `iw dev`: live interfaces 5 GHz on channel 44 at 160 MHz and 2.4 GHz on channel 6 at 40 MHz, all at
  **23.00 dBm**; UCI has `radio0.txpower='100'`, `htbw='auto'`, `maxstanum='64'`, `hwmodes='11bgnax'`.
- These readouts are the observable a power-table or `txpower` change would move, so every future
  experiment can be validated without any client-side measurement rig.

## 10. Regulatory tables vs the configured country (observed 2026-10-01)

- The shipped INI carries **two power tables**: `[HOST_WIFI_NORMAL]` (166 `pwr_limit_*` keys, all of
  them `0x003C3C3C`, i.e. a uniform 30 dBm under the 0.5 dBm/LSB reading) and `[HOST_WIFI_PWR_LIMIT_LU]`
  (166 keys, Luxembourg/EU, reduced on selected channels), plus `[CUSTOM_REGDOMAIN_CFG]` with three
  country tags (BZ, DE, LU) and 13 band rows.
- Cross-artifact inference (three independent observations agree): the whole-file histogram is
  `0x3C` x208 = 166 (NORMAL, all default) + 42 (LU), and the live `iw phy` readout advertises
  30.0 dBm on 2.4 GHz. Since the device is configured `country='US'` and the INI has no US section,
  the driver is evidently running the **NORMAL** table, not the LU override. (Hypothesis, but the
  counts and the live advertisement both point the same way.)
- The device's own configuration says `wireless.radio0/1.country='US'` (both `/etc/config/wireless`
  entries `option country 'US'`), so the configured country does not match the shipped LU table set.
- Live readouts (`iw phy`): 30.0 dBm advertised on 2.4 GHz channels, 23.0/24.0 dBm on 5 GHz; the live
  interfaces run at 23.00 dBm with UCI `txpower='100'`.
- Consequence for any tuning work: the active table is selected somewhere between the UCI country and
  the driver's INI sections, and that mapping is not yet proven. Any experiment must therefore verify
  the effective limits with `iw phy <phy> info` before and after, and must stay within the regulatory
  limits of the country the operator is actually in.

## 12. The `iwpriv alg` dispatch chain (recovered and independently re-verified 2026-10-01)

Anchor: the private-ioctl table registers `alg` as cmd `0x0101`, set 500 chars / get 1000 chars
(confirmed on the device with `iwpriv Hisilicon0`).

1. `wal_algcmd_char_extra_adapt` (`.text+0x10c564`, 700 B) hard-codes `movw ip,#0x101` and calls
   `wal_wlan_cfg_module_process_entry` (`.text+0x127af8`).
2. The 12-byte module table at `.data+0x1d58` is `{u32 cmd; u32 flags; u32 handler}` with the handler
   slots filled by `.rel.data`: `0x1d60 -> wal_wlan_cfg_alg_process_entry` (`.text+0x14b46c`),
   `0x1d6c -> wal_wlan_cfg_cali_process_entry` (`.text+0x14b6ec`). So there are TWO channels: `alg`
   (0x0101) and an unlisted `cali` (0x0102).
3. Names are matched by `alg_cfg_search_process_info_by_cfg_name` (`.text+0x14aefc`) against
   `g_ast_alg_cfg_process_info_table` (`.data+0x35c8`, 4,968 B = 414 entries x 12 B, `{name, cfg_id,
   dir}`), then `wal_wlan_cfg_alg_process_entry` scans `g_alg_cfg_lut` (`.data+0x4934`, 73,216 B =
   416 entries x 176 B) by `{cfg_id, dir}` and invokes the handler at `entry+8`.
4. Nine commands are mapped name -> cfg_id -> LUT entry -> handler, e.g. `get_2g_power_param`
   (cfg_id 0x0dae) -> `alg_cfg_args_param_analyse_equipment_param` (`.text+0x1572c0`).
5. Firmware boundary reference: `hmac_sync_dmac_alg_cfg_rsp_entry` (`.text+0x87d70`, 352 B),
   `hmac_sync_dmac_cali_cfg_rsp_entry` (`.text+0x881a4`, 148 B), `hmac_chan_tx_cali_sync`
   (`.text+0x46dac`, 280 B).
6. Not proven (honest gap): the exact wext index path that reaches `wal_algcmd_char_extra_adapt`
   (0x0101 sits below `SIOCIWFIRSTPRIV`), the non-`+8` pointer slots in the LUT, and the
   driver-to-firmware wire format.

Verification: the lead re-derived the symbol set and the dispatch mapping independently (symbol table
addresses/sizes matched, and `.rel.data` entries at 0x1d60/0x1d6c named the two handlers), then the
phase-2 verification lane re-ran the checks and wrote `ulw/phase2/verification.md`.

## 13. What this means for reverse engineering (measured, not guessed)

- The MAC/MLME logic is **host-side and symbol-rich**: rates, aggregation, EDCA, DFS, ring/queue
  handling, cfg80211 operations. Disassembly with names is practical (ARM32, capstone/pyelftools
  available in the workspace venv), and the `alg` dispatch was recovered end to end down to handler
  functions - including a second, unregistered command channel (`cali`, 0x0102).
- The radio's *behaviour* is largely **data-driven** from the INI: two power tables (uniform default +
  country overrides), a country band/regulatory table, calibration protect limits, temperature
  compensation and CCA thresholds. Per-unit calibration is read back through the `alg` interface,
  persists across reboots, and is file-backed in the factory partition.
- The genuinely closed core is the **ITCM/DTCM firmware** (929 KB, entropy ~7.06), the
  **driver<->firmware wire format**, and the chip's **register map**. Those are what a from-scratch
  driver would have to re-invent, and the driver's own named sync symbols
  (`hmac_sync_dmac_alg_cfg_rsp_entry`, `hmac_sync_dmac_cali_cfg_rsp_entry`, `hmac_chan_tx_cali_sync`)
  mark exactly where that boundary sits.
- Honest split: **understanding, measuring, tuning, and targeted patching are reachable today** with
  the artifacts in this note. A **full clean-room replacement driver** is still a large project, but
  the part that must be invented is now bounded and identifiable, not a blank wall. And because the
  module itself declares `license=GPL`, the source request remains the shortest path to doing it at
  source level.
- Practical menu that the evidence now supports: (1) snapshot/restore calibration before any change
  (`tools/wifi-cal-*.sh`, done); (2) verify effective limits per country with `iw phy <phy> info`
  before and after a power-table or `txpower` change; (3) use the recovered dispatch to enumerate the
  full `alg` command set (the name table has 414 entries, of which only ~86 are `*_param` shaped);
  (4) disassemble individual handlers (e.g. `alg_cfg_args_param_analyse_equipment_param`) to learn the
  calibration math.
