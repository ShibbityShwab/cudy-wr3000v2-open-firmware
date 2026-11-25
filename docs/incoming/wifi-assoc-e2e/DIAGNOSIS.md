# DIAGNOSIS - symptom -> cause -> next action (association E2E)

Use the client JSON stage from assoc_e2e.py plus the router snapshot
(sh /root/assoc-router-fire.sh --stage snapshot) for every row. The rule of
thumb from the task brief: **no CRX event = RF/client; CRX but no response =
mgmt TX; association but no EAPOL = data TX/RX/ring.** Keep it: check the
earliest missing link, never the last symptom.

## 0. Quick triage

    client stage COMPLETED?            -> done; capture 4-way evidence (MATRIX A2)
    client NETWORK-NOT-FOUND?          -> S1 (AP not seen)
    client AUTH-TIMEOUT / SAE-AUTH?    -> router: did mgmt RX subtype 0x0b arrive?
                                          no  -> S2 (client frames not heard)
                                          yes -> S3 (mgmt TX path)
    client ASSOC-TIMEOUT?              -> S4 (assoc RX/TX or .add_station)
    client FOURWAY-FAILED?             -> S5 (EAPOL TX) / S6 (EAPOL RX) / S7 (keys)
    client DISCONNECTED reason=N?      -> S7/S8 with the reason code
    any page/mask/staging deviation?   -> S9 STOP
    host error class (DRIVER-ERROR)    -> S10

## S1 - client cannot see the AP (NETWORK-NOT-FOUND / scan FAIL)

Checks, in order:

1. Router: is hostapd AP-ENABLED and does dmesg show the 5-message order?
   (--stage snapshot; grep "vendor AP-start order complete" / "wlan0: AP-ENABLED").
   No -> AP rung (MATRIX M3) failed; do not debug the client.
2. Router: iw dev wlan0 info -> channel 6, type AP. A wrong channel (e.g. ch 14
   probe) means the profile did not apply.
3. Host RF: is any SSID visible? The full-band fallback in the scan log lists
   neighbours; if the whole band is empty, the NIC/regdom is wrong
   (iw reg get must allow 2437; the harness can iw reg set US).
4. Host quirk: prior wlan1 passive scans returned only the 5 GHz neighbour and
   missed the AP while wlan0 scans/monitor saw it. Try, in order:
   sudo python3 scripts/assoc_e2e.py scan --iface wlan1 --profile open
   sudo python3 scripts/assoc_e2e.py scan --use-base --profile open
   (with --use-base, NetworkManager disconnects wlan0 for the rung).
5. Antennas: no-antenna baseline -63..-65 dBm; below the -70 dBm gate the
   association rung is not meaningful. Align antennas and re-run watch --scan-only.

## S2 - client AUTH sent, router sees NO management RX event (RF / RX side)

Router snapshot dmesg: absence of "cfg80211: mgmt RX delivered (subtype 0x0b" or
any mgmt RX line.

Likely causes:

| cause | evidence | action |
| --- | --- | --- |
| RF: client TX not heard (weak/no antennas, wrong channel) | client scan RSSI marginal; router RX ring counters 0; no CRX | fix RF first (MATRIX D); verify client's AUTH actually left (client supplicant log has "Trying to authenticate" and no immediate driver error) |
| client on a different channel | router AP ch6; client scan log freq 2437 expected | fix with --use-base/scan; check no other AP with the same SSID |
| mgmt RX decode/param off | module param mgmt_rx=0 | reload ONLY with the association param set (mgmt_rx=1); never reload in the same boot after a fire |
| no AP wdev | wiphy registered but no wdev (AP start failed silently) | re-run --stage ap; if AP-ENABLED is missing, fix M3 first |
| CRX decode rejected | driver has rx_malformed counters (not in sysfs today) | record dmesg, keep as a finding; the decoder expects D2H group1 id13 with a 60-B prefix (12-B HCC + 48-B RX info), frame after |

## S3 - mgmt RX arrives, but the AP never sends the response (mgmt TX path)

Router snapshot: "cfg80211: mgmt RX delivered (subtype 0x0b ...)" present but no
"frame post 'mgmt_tx': g0 id6 ...".

| cause | evidence | action |
| --- | --- | --- |
| mgmt_tx param off | /sys/module/hi5622_wifi/parameters/mgmt_tx = 0 | the fix is the association load line (mgmt_tx=1); one load per boot |
| hostapd did not subscribe / refused to respond | hostapd log: "Could not send ... response", "Failed to ..."; no AP-ENABLED | restart with the correct fixture; verify wiphy mgmt_stypes (dmesg "cfg80211: mgmt_stypes advertised (AP rx=0x3c2a ...)") |
| frame post refused (budget/gap/H2D) | dmesg "frame post 'mgmt_tx': ... NOT consumed", "H2D lane closed", "posted N/32" exhausted, "previous announce/gap did not clear" | STOP: one attempt per load; power-cycle. Check allow_frames=32 and frame_gap_ms=100 |
| .mgmt_tx returned EOPNOTSUPP | dmesg "mgmt_tx op reached - mgmt_tx=0 fail-closed" only when 0; otherwise check the AP-only policy (non-AP iftype refused) | verify the AP wdev iftype is AP (iw dev wlan0 info) |
| frame posted but not on air | "frame post 'mgmt_tx': g0 id6 total 86 B ... consumed 0 polls" is a local acceptance fact; device completion 0x01F3 is not decoded | RF/antenna alignment is the next hypothesis; an external monitor capture of the client's AUTH + AP response is the decisive datum |

## S4 - AUTH completed, ASSOC times out

AUTH response went out (S3 passed) and the client sent the assoc request, but
the assoc response / station add chain is missing.

| cause | evidence | action |
| --- | --- | --- |
| sta_ops param off | dmesg "hi5622: .add_station reached - fails closed (sta_ops=0...)" | load line must have sta_ops=1 |
| .add_station refused | "sta_ops: .add_station ... user_add (g0 id7) NOT posted (...)" or "association sync ... NOT posted" | budget/H2D gate; STOP and power-cycle if an announce was lost |
| duplicate station | .add_station returns -EEXIST on a stale station table entry | a re-association in the same load; power-cycle for a clean run |
| assoc response mgmt TX refused | no second "frame post 'mgmt_tx'" after the assoc request | same as S3 (mgmt TX) |
| assoc request mgmt RX missing | only one "mgmt RX delivered (subtype 0x0b)" and no 0x00 | client side: assoc request never left (client log stops at "Trying to associate"); RF |

## S5 - association OK, NO EAPOL from the AP (data_tx path)

Router snapshot: sta_ops STA added; hostapd log "authenticated/associated" but
no "data_tx: EAPOL-Key msg1/4 TX COMPLETED".

| cause | evidence | action |
| --- | --- | --- |
| data_tx param off | dmesg "xmit_dropped" in sta-ops status; data_tx=0 | load line data_tx=1 |
| data plane not set up in this load | dmesg "DP tx_frame/dp gate refused"; dp_setup/txq sync missing | run --stage dp (AP already started) and confirm DP setup OK |
| TXQ publish not enabled | dp_tx_status shows POST-ONLY/no completion | the final module has txq_publish=1 default; verify data_tx_status posts/completed |
| hostapd did not start EAPOL | hostapd log "WPA: ..." absent; station not authorized yet | expected: msg1 is sent before authorization (EAPOL is allowed pre-auth by design) |
| ACL / l2_packet issue | hostapd "l2_packet" errors | capture hostapd log; restart hostapd cleanly (stop + ap) is allowed only before any frame was posted? If a frame was posted: STOP |

## S6 - msg1 TX COMPLETED but no msg2 (EAPOL RX / ring)

Router snapshot: "data_tx: EAPOL-Key msg1/4 TX COMPLETED" present; cfg80211_status
"sta-ops wire: rx frames=0 eapol=0".

| cause | evidence | action |
| --- | --- | --- |
| RX not armed in this load | no "DP rx_arm" dmesg OR "rx_arm: REFUSED" | the order is AP -> dp -> rx_arm; if rx_arm was refused (-EALREADY on a second attempt) power-cycle and redo the order |
| drain loop not running | no "DP rx_drain" lines after traffic | --stage drain-start; drain cadence is ASSOC_DRAIN_SEC (default 1 s) |
| eapol_rx=0 | cfg80211_status sta-ops line: eapol=0 delivered=0 while rx frames>0 | load line eapol_rx=1 |
| client never sent msg2 | client supplicant log: "Sending EAPOL-Key ..." absent, or retries | client side: PSK in use must match the AP fixture; the harness manages the passphrase; check the staged config's ssid/psk path |
| EAPOL RX frame malformed | cfg80211_status bad counter grows; dmesg anomalies | record and STOP (new finding), keep the drain log |

## S7 - msg2 received but the 4-way does not complete (keys)

| cause | evidence | action |
| --- | --- | --- |
| PTK add_key refused | no "cfg80211: add_key posted (g0 id1 cmd 0x0078 ... pairwise=1)"; hostapd log shows a failure | check wpa_keys=1, allow_frames budget; pair-wise key install is firmware worker 0x0A5825 |
| PSK mismatch | client "WPA: 4-Way Handshake failed - pre-shared key may be incorrect"; hostapd "WPA: ... mismatch" | the router's staged config passphrase (ASSOC_PSK) and the client's must match; both are lab values only |
| msg3 not TX | no "data_tx: EAPOL-Key msg3/4 TX COMPLETED" | same as S5 |
| msg4 not RX | EAPOL counters stop after msg2 | same as S6 |
| PMF rung (WPA3) | MFP-required; client 4-way fails after SAE | verify the client profile has ieee80211w=2 for the WPA3 fixture; the AP-side IGTK path is proven |

## S8 - COMPLETED but no data

| cause | evidence | action |
| --- | --- | --- |
| no IP | ping "Network is unreachable" | static IPs on both ends (RUNBOOK R7); no DHCP server on the bench AP |
| station not authorized | hostapd log lacks AP-STA-CONNECTED; .change_station not logged | the driver's authorization mirror is hostapd-driven; check hostapd completed the 4-way |
| unicast TX refused | data_tx_status dropped(peer/unauth) grows | data_tx_peer=1 refuses unicast to unknown/unauthorized stations; the client must be associated + authorized |
| RX ring starved | completion ring at 0 and drain "no new completion" | the RX watermark lane may be needed for sustained traffic; record the counters |

## S9 - page/mask/staging deviation, firmware exception

STOP immediately. Do not retry in the same load and do not reload the module
expecting a different result. Power-cycle the router physically (the SoC reboot
does NOT reset the Wi-Fi chip). Record: page line, exception mask, staging,
the last dmesg line, the stage that was running. This is a new firmware finding,
not a client issue.

## S10 - host client problems (MT7922)

| symptom | cause | action |
| --- | --- | --- |
| iw scan returns -EBUSY | stale supplicant / iface in use / NM racing | reset (kills stale supplicants, NM unmanaged, down/up settle); the harness retries once after a mini-settle |
| "Not connected." after a previous attempt | supplicant from the previous attempt left the iface down/owned | reset; cleanup; never start two supplicants |
| P2P-device wdev appears | wpa_supplicant P2P default | the generated config sets p2p_disabled=1; the harness kills only our instances |
| NetworkManager grabs the iface back | NM default policy | reset sets DEVICE managed no; cleanup restores it |
| cannot create wlan1 (-EBUSY / not supported) | wiphy combination/no second iface | rerun all host phases with --use-base (uplink drops until cleanup) |
| host uplink lost | --use-base run did not finish cleanup | sudo nmcli device connect wlan0 (or nmcli networking on) |
| driver wedged after heavy churn (prior MT7922 behaviour) | MT7922 reload | run the host reset with ASSOC_RELOAD_DRIVER=1? (not automated: use sudo modprobe -r mt7921e; sudo modprobe mt7921e; wait for wlan0) - then reset again |

## S11 - image / config blockers (not driver failures)

| symptom | cause | action |
| --- | --- | --- |
| hostapd "invalid key_mgmt SAE" | the image's hostapd was built without CONFIG_SAE | WPA3 rung NOT-RUN; use the final image #2 (SAE hostapd) or Image B |
| hostapd does not know the fixture path | /etc/hi5622/*.conf not baked | copy the fixture from the lane (configs/) or the image-polish rootfs to /etc/hi5622/ |
| passphrase placeholder still in the config | fixture ships CHANGEME-lab-only | the router script substitutes from ASSOC_PSK; if it cannot, fix the staged config (lab values only) |
| module sha mismatch | wrong artifact (lane-built WAL carrier instead of ab9e18a4) | verify with scripts/verify-association-module.sh; do not load the old artifact |
