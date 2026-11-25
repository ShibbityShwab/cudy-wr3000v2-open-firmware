# MATRIX - association E2E PASS/FAIL matrix

This is the live result sheet for the attended fire. Every row is filled from
actual captured evidence (router snapshot + host client evidence) - no row is
pre-marked PASS. The **Expected now** column is the documented prediction from
the 2026-11-25 state; anything different is a new finding, not a harness defect.

Legend for **Expected now**: PASS = should pass on the corrected module;
FIRST = first-ever combined run (unknown, capture it); DEP = image-dependent;
OPT = optional rung; NOT-RUN until the Lead fires.

## A. Router / host infrastructure rungs

| ID | rung | action | PASS evidence (must be captured) | FAIL signatures | Expected now |
| --- | --- | --- | --- | --- | --- |
| M0 | host artifacts | verify module + host tests | verifier prints RESULT: PASS (size 344372, sha ab9e18a4, 56 params); 23/23 tests; 41/41 self-checks | size/sha/param mismatch, old WAL carrier string present, patch provenance drift | PASS (host-verified 2026-11-25) |
| M1 | router golden boot | --check + --load KO | insmod rc=0; page 0x86807fef/7/2/0xabcd; state 1; mask 0; staging 0; 11 feature params enabled | page/mask/staging deviation; insmod failure; "module already loaded" | PASS expected |
| M2 | boot ladder + C2 | --boot --c2 | prepare/upload/verify/seed/hcc_arm/hcc_loop/release rc=0; hcc_ready id1_seen=1 id2_seen=1; h2d_ctx_create + h2d_txq_sync72 consumed 0 polls | ready timeout; announce not consumed; H2D lane closed; page lost | PASS expected |
| M3 | AP start (profile P) | --stage ap | hostapd AP-ENABLED; 5-message order in dmesg ("vap_add ap_chan ap_ssid ap_cfg ap_vap_start" + "vendor AP-start order complete"); iw dev wlan0 info type AP ssid P | hostapd not AP-ENABLED; ap_seq_dirty refusal; stale-VAP guard; announce timeout; page deviation | PASS expected (open/wpa2 fired before; wpa3 on air proven) |
| M4 | DP + RX arm | --stage dp | "DP setup: OK"; "DP txq_sync: g0 id1 key 0x0320"; "DP rx_arm: live bank CA 0x40050000 status=0x00000182 mask=0xfffffcc1 norm/small rptr=0x0020"; drain loop running | status 0x02 (AP-not-first order); rx_arm refused; dp gate refusal; page deviation | PASS expected when AP started first |
| M5 | RF gate | host scan + watch | SSID found on 2437, BSSID 06:05:04:03:02:01, RSSI >= -70 dBm (target >= -55) | SSID not found; wrong channel; RSSI below gate | DEP on antennas (no-antenna baseline -63..-65 dBm) |
| M6 | client reset | host reset | RESET DONE; one managed iface; no supplicant bound; NM unmanaged for the client iface | cannot create wlan1 (-EBUSY) -> rerun --use-base; stale supplicant survives | PASS expected on MT7922 (2-managed combo) |

## B. Association rungs

| ID | rung | profile | PASS evidence | FAIL signatures | Expected now |
| --- | --- | --- | --- | --- | --- |
| A1 | OPEN control association | open | client JSON stage=COMPLETED; dmesg mgmt RX subtype 0x0b then 0x00; frame post mgmt_tx g0 id6 86 B consumed 0; sta_ops STA added; AP-STA-CONNECTED | AUTH-TIMEOUT (no response = mgmt TX/RX/RF); ASSOC-TIMEOUT; NETWORK-NOT-FOUND | FIRST combined run; design PASS |
| A2 | WPA2 4-way handshake | wpa2 | A1 chain + PTK add_key (0x0078 idx=0 pairwise=1) + msg1/msg3 data_tx TX COMPLETED + EAPOL RX (eapol>=2 delivered) + hostapd "pairwise key handshake completed" + client COMPLETED | FOURWAY-FAILED (PSK/EAPOL path); msg1 not TX (data_tx gate); msg2 not RX (drain/rx_arm/eapol_rx); PTK add_key refused | FIRST; primary target |
| A3 | WPA3-SAE association | wpa3 | SAE auth mgmt TX/RX (2 auth frames each way) + assoc chain + 4-way + client COMPLETED key_mgmt=SAE | SAE-AUTH-TIMEOUT (mgmt path); FOURWAY-FAILED; hostapd "invalid key_mgmt SAE" -> IMAGE NOT-RUN | DEP (needs CONFIG_SAE hostapd) |
| A4 | data plane after assoc | any | ping both ways on 192.168.77.0/24; data_tx posts advance; sta-ops rx/ delivered advance | no ARP/RX (drain not running, rx_arm missing, eapol_rx=0); unicast refused (unauthorized) | OPT |

## C. Client-side classification (what the host harness will report)

| stage (JSON) | exit | wpa_supplicant shape | router-side correlation to check |
| --- | --- | --- | --- |
| COMPLETED | 0 | wpa_state=COMPLETED / CTRL-EVENT-CONNECTED | find the 4-way evidence (A2 rows); iw link shows the BSSID + signal |
| NETWORK-NOT-FOUND | 1 | CTRL-EVENT-NETWORK-NOT-FOUND | AP not beaconing or not visible: M3 + M5, channel/RF |
| AUTH-TIMEOUT | 1 | "Authentication with ... timed out." | dmesg: is there "mgmt RX delivered (subtype 0x0b ...)"? no -> RF or mgmt_rx; yes -> mgmt TX (g0 id6) refused/lost |
| SAE-AUTH-TIMEOUT | 1 | same shape, variant wpa3 | same as above but SAE; check AP image hostapd SAE support first |
| ASSOC-TIMEOUT | 1 | "Association with ... timed out." | AUTH both ways worked; check assoc request mgmt RX + assoc response mgmt TX + sta_ops/.add_station logs |
| FOURWAY-FAILED | 1 | "WPA: 4-Way Handshake failed" | check msg1/msg3 data_tx TX COMPLETED, EAPOL RX counters, pairwise add_key in dmesg |
| DISCONNECTED | 1 | CTRL-EVENT-DISCONNECTED reason=N | reason 15 = 4-way timeout; reason 2 = invalid auth; correlate with router snapshot |
| DRIVER-ERROR | 2 path | nl80211 / interface errors | host problem: reset again; driver reload; --use-base fallback |

## D. RF gate thresholds (antenna test)

| RSSI | class | action |
| --- | --- | --- |
| >= -55 dBm | good | proceed with A1/A2/A3 |
| -56 .. -70 dBm | marginal (PASS but WARN) | association may work; 4-way timing is the risk - align antennas, re-run watch |
| < -70 dBm | FAIL | do not run association rungs; fix RF (antennas/orientation/distance) |

Reference: the no-antenna baseline recorded on the workstation saw the AP at
-63..-65 dBm (beacon -62 dBm in a monitor capture) at BSSID
06:05:04:03:02:01, ch 6 (2437 MHz, SSID wr3000v2-apstart /
cudy-wr3000v2-wpa3 in prior scans).

## E. Evidence checklist per row

Each matrix row must link/point to:

1. Router: build/assoc-e2e or /tmp/assoc-e2e/snapshot.txt (boot, hcc_status,
   dp_status, cfg80211_status, data_tx_status, hostapd tail, dmesg markers).
2. Host: build/wifi-assoc-e2e/<UTC-stamp>/ (scan-*.json/txt, supplicant.log,
   assoc-result.json, iw-link.txt, iface-addr.txt, client-dmesg-tail.txt,
   watch.csv).
3. The one-line verdict plus the first failing stage if FAIL.
