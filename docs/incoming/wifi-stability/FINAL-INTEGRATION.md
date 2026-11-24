# FINAL-INTEGRATION - all-patch module (static-RE wave + b5dfb883 chain)

**Lane:** wifi-stability (task-136), host-only.  This is the single module that
applies every static-RE wave lane on top of the bench-validated b5dfb883 chain.
Reproduce with:

    ./tools/build-all-patch-module.sh
    # or, reusing an already verified chain source tree:
    CHAIN_TREE=/tmp/wifi-stability-build/combined ./tools/build-all-patch-module.sh

The .ko is local evidence only and is never committed.

## 1. Chain

    base 44c6cc12 (hardware-fired combined AP+DP)
      + wifi-image-polish/patches/0001        -> 242,920 B d145a205
      + wifi-dp-tuning/patches/0001           -> 251,924 B 175a4617 (TX COMPLETED)
      + wifi-scan-tuning/patches/0002 (R4)    -> scan R4 (H1 band-config builder)
      + wifi-stability/patches/0001           -> b5dfb883 (269,688 B, guard timing
                                                 fix, E2 plain-restart, hcc_trace)
      + wifi-rx-static/patches/0001           (rx_arm / rx_drain live consumer)
      + wifi-beacon-static/patches/0001       (5-message AP order + 72dabe8 guard fix)
      + wifi-wpa-static/patches/0001          (g0 id1 0x0078/0x0079 key ops, GTK)
      + wifi-sta-static/patches/0001          (sta_join WAL 0x123 + HT/VHT/band)

## 2. Conflict resolution (minimal, feature-preserving)

All four lane patches apply cleanly on their own to the b5dfb883 chain; the
script applies each to a private tree and then merges per file with
`git merge-file`.  Six files genuinely overlap and are replaced by the
checked-in `integration/resolved/` versions (union of both lanes):

| file | lanes | resolution |
| --- | --- | --- |
| hi5622_frames_api.h | beacon + wpa | union of capability bits, supported-caps mask, builder pointers. **Collision fixed:** beacon CHAN_CFG already used bit 9 and WPA's key caps also started at bit 9; the WPA key family is renumbered to bits 12/13/14 (internal provider capability only - no wire/ABI meaning). |
| hi5622_scan_logic.c | beacon + wpa | union of the ops validators + cap-name/format entries for chan_cfg and key_add/key_del/key_default; the cap-format loop bound moved to KEY_DEFAULT. |
| hi5622_vapscan_provider.c | beacon + wpa | union capability OR (AP_START look-alike set includes CHAN_CFG and the three KEY caps). |
| Kbuild | rx + wpa + sta | union of objects (hi5622_rx_live.o, hi5622_key_frames.o, hi5622_keys.o, hi5622_sta_logic/frames/sta.o; band_cfg already in R4) with duplicates collapsed. |
| hi5622_cfg80211.c | wpa + sta | union of includes (keys.h + sta.h/sta_logic.h); both lanes' register/capability code merged. |
| hi5622_cfg80211_ops.c | wpa + sta | union of includes and of the ops table: add_key/del_key/set_default_key (wpa) and the sta connect/disconnect path (sta) coexist with start_ap/stop_ap/mgmt_tx/change_beacon. |

No feature was dropped.  If an upstream lane patch changes later, the script
refuses when a patch no longer applies; re-run the per-file merge and refresh
`integration/resolved/`.

## 3. Artifact (host-built, NOT fired as this exact combination)

    stripped   308,080 B  sha256 5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41
    unstripped 1,138,024 B sha256 a50680915eb432a4b9ec64f6180a5629773950989aa98b69677ab103961abc56
    vermagic   6.18.60 SMP mod_unload ARMv7 p2v8 ; depends=cfg80211
    params     47 ; 0 compiler/modpost warnings ; strip leaves .text byte-identical
    cfg80211  import contract 10 lines in cfg80211-provider.symvers,
              cfg80211_bss_iter imported (scan chain), no undefined symbols.

New feature parameters in the artifact (all present in modinfo):
`rx_enable`, `beacon_static`, `beacon_vap_start`, `wpa_keys`,
`wpa_key_module`, `sta_join`, `ht_mode`, `vht_cap`, `band_vendor`
plus the chain set (`hcc_trace`, `stop_ap_mode`, `scan_band_cfg*`,
`scan_req_batch`, `scan_batch_gap_ms`, `txq_publish`, `mgmt_diag`, all
dp/chain params).

## 4. FIRE RECIPE - final validation (AP-ENABLED 5-step order + WPA2 GTK +
## TX COMPLETED + rx_arm)

Fresh golden boot, recovery armed, PD4 firmware pushed by hand
(`/lib/firmware/hi_wifi/FIRMWARE.PD4.bin`, never packaged), local Wi-Fi block
staged (never committed).  One module load:

    insmod /lib/modules/$(uname -r)/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local block> \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 \
        sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0

Then the proven boot ladder (prepare/upload/verify/seed/release), C2
(`h2d_ctx_create_t1` + `h2d_txq_sync72`), iface `__ap`, and hostapd with the
WPA2 AP config from `../wifi-wpa-static/ladders/wpa2-ap.json` (open first if a
control is wanted).

Expected evidence:
* AP start: `vap_add` -> `ap_chan` (WAL 0x0124 / g0 id1, 32 B, band=0 ch=6) ->
  `ap_ssid` -> `ap_cfg` -> `ap_vap_start` (vendor 5-message order), then
  `wlan0: AP-ENABLED`, ssid cudy-ap-test.
* WPA2: `key_add` g0 id1 0x0078 96 B idx=1 multi cipher 0xfac04 and
  `set_default_key` 0x0079 68 B idx=1 multi=1, both consumed 0 polls
  (firmware workers CA 0x0A5825 / 0x0A5C9D in the lane report).
* TX: `dp_probe` -> `dp_setup` -> `h2d_txq_addr_sync` -> `dp_tx_frame` ->
  `dp_tx_status`: COMPLETED (rptr==wptr, TXQ publish).
* RX: `echo 1 > .../dp_rx_arm` -> live bank CA 0x40050000 status=0x182,
  mask=0xfffffcc1, norm/small rptr=0x20; `dp_rx_drain` only with rx_enable=1.
* Clean state after every rung: page 0x86807FEF/7/2/0xABCD, state 1, mask 0,
  staging 0; hcc_trace=1 only if a full frame trace is wanted.
* Boot params `allow_frames=32 h2d_send_budget=10` are required for the
  WPA/RX H1-class rungs; stop rules unchanged (exception -> power-cycle).

## 5. Verification performed (host)

* `tools/build-all-patch-module.sh`: chain identity assertions + lane patch
  application + conflict-marker refusal + module build (0 warnings) + strip
  `.text` compare + 47-param check + unique cap bits + 10-line cfg80211
  contract + feature strings (H1, E2 restart, rx_arm, add_key, beacon_static,
  sta_join) + atomic artifact publish.
* Evidence: [evidence/allpatch-build.txt](evidence/allpatch-build.txt),
  [evidence/allpatch-identity.txt](evidence/allpatch-identity.txt).

## 6. Limits

* This exact all-patch artifact was NOT fired.  Its constituent features were
  individually hardware-fired by the Lead on the b5dfb883 chain (rx_arm,
  5-message AP order, WPA2 GTK, plus the earlier AP/TX/scan rungs); the final
  validation recipe above is the first combined run.
* STA join (`sta_join=1/2`), WPA2 station-side, 5 GHz tables and the H1
  `scan_band_cfg` combos remain default-fail-closed/opt-in experiments - keep
  them 0 unless a ladder enables them.
* The WPA capability-bit renumbering is internal-only (provider bitmask); the
  frame content and firmware handlers are unchanged from the hardware-proven
  wpa-static/beacon-static lanes.
