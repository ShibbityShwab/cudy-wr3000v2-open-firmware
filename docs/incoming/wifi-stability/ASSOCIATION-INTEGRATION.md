# ASSOCIATION-INTEGRATION - final association / 4-way-handshake module

**Lane:** wifi-stability (task-136), host-only.  Single module that applies the
association-chain wave on top of the FINAL all-patch chain.  Reproduce with:

    ./tools/build-all-patch-module.sh              # -> all-patch tree + 5b587949 artifact
    ./tools/build-association-module.sh
    # reuse an existing verified all-patch tree:
    ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ ./tools/build-association-module.sh

The .ko is local evidence only and is never committed.

## 1. Chain

    all-patch 5b587949 (308,080 B: base 44c6cc12 -> polished d145a205 ->
      dp-tuning 175a4617 -> scan R4 -> stability b5dfb883 -> rx-static ->
      beacon-static(+72dabe8) -> wpa-static -> sta-static)
      + wifi-wpa3-sae/patches/0001    (AKM PSK+SAE, BIP-GMAC suites,
                                        mgmt_stypes AP tx=0x3c2a, IGTK /
                                        set_default_mgmt_key)
      + wifi-ap-mgmt/patches/0001     (.mgmt_tx WAL cfg-event 0x01F2 carrier,
                                        .mgmt RX D2H g1 id13 ->
                                        cfg80211_rx_mgmt_ext, mgmt_tx/mgmt_rx)
      + wifi-sta-ops/patches/0001     (.add/change/del/get/dump_station,
                                        per-STA firmware context g0 id7 +
                                        g0 id1 cmd 0x07d2, EAPOL/data RX,
                                        sta_ops/eapol_rx/sta_user_del)
      + wifi-data-tx/patches/0001     (ndo_start_xmit -> dp TX ring path,
                                        EAPOL msg1/msg3, data_tx/data_tx_peer/
                                        data_tx_protect/data_tx_timeout_ms)
    = FINAL ASSOCIATION module (56 params, this document).

wpa3-sae was already proven on air (SAE RSN IE, MFP-required, BIP-CMAC-256; the
association stopped at the old fail-closed .mgmt_tx).  This build replaces that
stub with the ap-mgmt carrier and adds the per-STA + data-TX layers.

## 2. Conflicts and resolution (feature-preserving)

Lane patch bases: wpa3-sae / ap-mgmt / sta-ops on the all-patch tree; data-tx
on the sta-ops successor tree (as documented in its ladder).  Per-file 3-way
merge then two checked-in resolves:

| file | resolution |
| --- | --- |
| Kbuild | union of all lane objects: hi5622_mgmt_logic.o hi5622_mgmt.o (ap-mgmt), hi5622_sta_ops_logic.o hi5622_sta_ops.o (sta-ops), hi5622_data_tx_logic.o hi5622_data_tx.o (data-tx) on top of the all-patch list; duplicate band_cfg token collapsed. |
| hi5622_cfg80211.c | union of the teardown hooks: hi5622_data_tx_remove() (data-tx), hi5622_mgmt_remove(ctx) (ap-mgmt), hi5622_sta_ops_remove(c) (sta-ops) all run in hi5622_cfg80211_remove() alongside the existing scan/sta removes; init hooks from all lanes are kept. |

No other conflicts: the wpa3/ap-mgmt/sta-ops/data-tx hunks merge additively.
**mgmt_stypes ownership:** wpa3-sae owns wiphy->mgmt_stypes and sets AP
tx=0x3c2a; ap-mgmt explicitly does not modify the table (its REPORT/TABLES say
so), so the requested 0x3c2a value is what ships.

## 3. Artifact (host-built, NOT fired)

    stripped   344,472 B  sha256 251d08f0c0a579f27cc3a3fb9f96df99c851a3315fa40f1fe2c7df267419d98b
    unstripped 1,323,608 B sha256 1d470e614ef2e2ecae88ffeb2a8116b7f31ba651e775880f84ad59754db77011
    vermagic   6.18.60 SMP mod_unload ARMv7 p2v8 ; depends=cfg80211
    56 module params ; 0 compiler/modpost warnings ; strip leaves .text identical
    cfg80211 provider symvers 12 lines (adds cfg80211_mgmt_tx_status_ext,
      cfg80211_rx_mgmt_ext); the .ko imports cfg80211_bss_iter,
      cfg80211_mgmt_tx_status_ext and cfg80211_rx_mgmt_ext; no undefined symbols.

New association params (all fail-closed defaults): mgmt_tx=0, mgmt_rx=0,
sta_ops=0, eapol_rx=0, sta_user_del=0, data_tx=0, data_tx_peer=1,
data_tx_protect=0, data_tx_timeout_ms=200; plus the 47 all-patch params
(wpa3 has no params; it extends AKM/cipher/mgmt_stypes).

## 4. FIRE RECIPE - association + 4-way handshake bench run

Fresh golden boot, recovery armed, PD4 pushed by hand, local Wi-Fi block staged.
One module load:

    insmod /lib/modules/$(uname -r)/hi5622_wifi.ko \
        fw_path=hi_wifi/FIRMWARE.PD4.bin wifi_block_path=<local block> \
        allow_frames=32 frame_gap_ms=100 h2d_send_budget=10 \
        vap_start_first=1 vap_start_mode=1 vap_start_module=0 \
        hcc_poll_ms=5 hcc_trace=1 stop_ap_mode=0 mgmt_diag=0 \
        rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 \
        sta_join=0 ht_mode=0 vht_cap=0 band_vendor=0 scan_band_cfg=0 \
        mgmt_tx=1 mgmt_rx=1 sta_ops=1 eapol_rx=1 sta_user_del=0 \
        data_tx=1 data_tx_peer=1 data_tx_protect=0 data_tx_timeout_ms=200

(hcc_trace=1 for this first association run - the full frame trace matters;
use hcc_trace=0 for a quiet steady-state soak afterwards.  Controls: the same
load with mgmt_tx=0 / sta_ops=0 / data_tx=0 must fail closed.)

Sequence:
1. boot ladder (prepare/upload/verify/seed/release) -> C2
   (h2d_ctx_create_t1 + h2d_txq_sync72) -> iface __ap -> hostapd with the
   proven WPA2 AP config (open first if a control is wanted).
   Expect AP-ENABLED with the 5-message order (vap_add -> ap_chan ->
   ap_ssid -> ap_cfg -> ap_vap_start) + WPA2 GTK key_add/set_default_key.
2. Wire this module's dp chain once: cat dp_probe -> echo 1 > dp_setup ->
   echo 1 > h2d_txq_addr_sync (TX COMPLETED path).
3. External client association attempt (observer):
   * hostapd AUTH/ASSOC handshake now reaches .mgmt_tx with mgmt_tx=1:
     WAL cfg-event cmd 0x01F2 consumed, cfg80211_mgmt_tx_status_ext called
     (management responses actually transmitted); mgmt RX (auth/assoc)
     arrives as D2H g1 id13 and is delivered via cfg80211_rx_mgmt_ext
     (mgmt_rx=1).
   * accepted STA -> hostapd .add_station -> driver posts the per-STA firmware
     context: g0 id7 user_add 180 B + g0 id1 cmd 0x07d2 asoc state, consumed
     0 polls (sta_ops=1); sta_ops_status shows the station table entry.
4. EAPOL 4-way handshake: hostapd sends msg1, client replies, hostapd sends
   msg3 through the AP netdev -> data_tx path (data_tx=1, data_tx_peer=1):
   data_tx_status 'posts=/completed=' advance, dp TX ring completion advances
   (TX COMPLETED), EAPOL msg1/msg3 counters advance, then hostapd
   set_key/set_default_key (WPA2) or SAE/PMF keys (wpa3) complete the key
   installation.
5. Every rung: page 0x86807FEF/7/2/0xABCD, state 1, mask 0, staging 0; any
   exception -> power-cycle, no retry (stop rules unchanged).

Pass evidence to capture: dmesg (mgmt_tx/1F2 lines, mgmt RX id13, sta_ops
add lines, data_tx status), cfg80211_status (mgmt/sta_ops/data_tx counters),
data_tx_status, dp_tx_status, hostapd log (auth/assoc/EAPOL state), page/mask.

## 5. Verification performed (host)

* tools/build-association-module.sh: all-patch tree staged (artifact identity
  5b587949 asserted by the upstream script), each lane patch applied to its
  declared base, per-file 3-way merge, two checked-in resolves, conflict-marker
  refusal, module build (0 warnings), strip .text compare, 56-param check,
  unique capability bits, 12-line cfg80211 symvers + three imported symbols,
  feature strings (set_default_mgmt_key, sta_ops STA added, data-tx counters,
  mgmt_tx DMA carrier, WAL cfg-event carrier, rx_arm, scan H1), atomic publish.
* Evidence: [evidence/assoc-build.txt](evidence/assoc-build.txt),
  [evidence/assoc-identity.txt](evidence/assoc-identity.txt).

## 6. Limits

* This exact combination is NOT fired.  Its parts: wpa3-sae was proven on air
  as its own module; ap-mgmt / sta-ops / data-tx are host-built (data-tx was
  built on the all-patch+sta-ops tree by its lane, not fired).  The recipe
  above is the first combined association run.
* The association completion still depends on the firmware accepting the
  vendor 0x01F2 mgmt-TX descriptor and the g1 id13 RX events; if an announce is
  not consumed the H2D lane closes - stop and power-cycle, do not retry.
* WPA2 is the primary handshake target; WPA3-SAE path additionally needs
  hostapd SAE config + MFP and is expected to work with mgmt_tx enabled, but
  the 4-way/handshake completion is a bench result to capture.
* STA-join (sta_join=0) and 5 GHz/HT/VHT vendor tables stay default-off.
