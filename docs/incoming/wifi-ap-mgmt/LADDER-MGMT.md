# LADDER-MGMT - AP management TX/RX ladder (design only, NOT FIRED by this lane)

**Lane:** wifi-ap-mgmt (task-143), host-only.  **Status:** design; the Lead
fires it.  This lane did not contact the bench router.

Base image: the FINAL all-patch module (stability commit `008aca5`) **plus**
[patches/0001-mgmt-tx-rx.patch](patches/0001-mgmt-tx-rx.patch) - 49 params
(47 + `mgmt_tx` + `mgmt_rx`).  The proven fire recipe, the boot ladder
(prepare/upload/verify/seed/release), C2 (`h2d_ctx_create_t1` +
`h2d_txq_sync72`), iface `__ap` and the hostapd fixtures are unchanged from
`../wifi-stability/FINAL-INTEGRATION.md` and `../wifi-wpa-static/`.

## Preconditions (every rung)

* fresh golden boot: page `0x86807FEF/7/2/0xABCD`, state 1, mask 0,
  staging 0; recovery door READY (`tools/recovery-card.sh`); human at bench.
* one module load per boot; no retry after a failed/refused frame.
* client: one dedicated test station (phone/laptop), MAC recorded, on a test
  SSID only; for M2 the on-air verdict needs an **external observer** (second
  radio/phone scan) - our own RX path is not proof of our TX.
* record `/sys/class/net/wlan0/...`, `cfg80211_status`, dmesg, page/mask
  before and after each rung.

## Common insmod (add the rung's two new params)

    insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD4.bin \
        wifi_block_path=<local wifi block> allow_frames=32 frame_gap_ms=100 \
        h2d_send_budget=10 vap_start_first=1 vap_start_mode=1 \
        vap_start_module=0 hcc_poll_ms=5 hcc_trace=0 stop_ap_mode=0 \
        mgmt_diag=0 rx_enable=1 beacon_static=1 beacon_vap_start=1 \
        wpa_keys=1 wpa_key_module=0 sta_join=0 ht_mode=0 vht_cap=0 \
        band_vendor=0 scan_band_cfg=0 \
        mgmt_tx=<0|1> mgmt_rx=<0|1>

Then the proven sequence: boot ladder -> C2 -> iface `__ap` -> hostapd
(OPEN fixture first; the WPA2 fixture only in M3).

## Rungs

### M0 - control: `mgmt_tx=0 mgmt_rx=0` (compiled-default path)
Purpose: prove the new code is invisible at the default.
1. fire the proven AP start (beacon_static=1) and let hostapd reach AP-ENABLED.
2. With the test client, try to auth (or let hostapd log its subscription).
3. Capture dmesg + cfg80211_status.
PASS:
* AP-ENABLED as the project baseline; page/mask/staging unchanged; no
  exception.
* dmesg shows `hi5622: mgmt_tx op reached - mgmt_tx=0 fail-closed` and
  **no** "frame post 'mgmt_tx'" line and **no** "mgmt RX delivered" line.
* `mgmt_tx`/`mgmt_rx` params read 0 in cfg80211_status/modinfo.
FAIL/stop: any of the forbidden writes appeared, page/mask changed, exception.

### M1 - RX decode only: `mgmt_rx=1 mgmt_tx=0`
Purpose: prove the D2H group1-id13 decode + cfg80211 delivery on a real
client frame, with TX still fail-closed.
1. AP start as M0 with `mgmt_rx=1`.
2. Test client sends a management frame (probe request or auth request).
PASS:
* dmesg line: `cfg80211: mgmt RX delivered (subtype 0x0? ch 6 2437 MHz
  rssi -?? len ??)` for the client's frame.
* hostapd receives the frame (its AUTH/ASSOC handling starts) and its reply
  attempt fails with -EOPNOTSUPP (expected: mgmt_tx=0).
* page/mask unchanged, no exception, H2D lane still open.
FAIL/record: malformed decode (the "rx_malformed" counter in the refactor
log), wrong channel/frequency, no delivery.  Never scale a decode failure
into a TX attempt in the same boot.

### M2 - full mgmt TX carrier: `mgmt_tx=1 mgmt_rx=1`
Purpose: decide the [U] - does the PD4 WAL group-2 dispatch accept the vendor
cmd 0x01F2 and **DMA-read the frame** from the descriptor address?
1. AP start as M1 with `mgmt_tx=1`.
2. Test client authenticates; hostapd handles the auth request and replies
   through `.mgmt_tx`.
3. Watch dmesg for the frame-post line and the external observer for the
   auth response on air.
PASS (two-part):
* host: `frame post 'mgmt_tx': g2 id0 total 44 B in SR0 slot ... devva
  0x8....... coalesced announce consumed after N polls` - and no firmware
  exception.
* air: the external observer sees the authentication response (or the client
  reaches "authenticated"), and the AP BSSID is the VAP BSSID.
Interpretation of the three possible outcomes (all are rung results):
* **refused** by our validator/gates (dmesg "frame post 'mgmt_tx': frame
  rejected ..." or allow_frames=0/budget) -> no device write; fix gate/enable
  budget; the WAL carrier is untested.
* **consumed but no frame on air** -> firmware accepted the WAL cfg event but
  did not fetch the frame at the descriptor address: the vendor's
  host-VA==devVA convention is the missing piece on this firmware (see
  REPORT section 6); record and fall back to the data-TXQ carrier design.
* **consumed + auth rsp on air** -> the full host-SME mgmt path works; next
  rung.
FAIL/stop: announce not consumed (H2D lane closed, no retry), exception,
page/mask change -> record + power-cycle.

### M3 - association + WPA2 (cross-lane; expected to expose the next blocker)
Purpose: measure how far the host-SME path gets; NOT expected to complete in
this lane.
1. M2 stack, client continues with the association request.
2. Expect: RX line for the assoc request; then hostapd
   `Could not add STA to kernel driver` because there is no `.add_station`
   op (TABLES T10), so no assoc response is sent.
3. Only if station ops land later: switch to the WPA2 fixture
   (`../wifi-wpa-static/ladders/wpa2-ap.json`) and watch the 4-way
   handshake, which additionally needs EAPOL data-frame delivery to the
   netdev (RX/DP path), not implemented by this lane.
PASS as a lane deliverable: the assoc request is decoded and delivered (RX
line), and the failure point is exactly the documented missing station op.
PASS as an AP: client reaches `AP-STA-CONNECTED` (requires the later lanes).

## Stop rules (physical)

* Firmware exception / golden page lost / state != 1 / mask != 0 /
  staging != 0 -> record dmesg + counters, **power-cycle**, analyse offline.
* `frame post` timeout or out[0] not consumed -> H2D lane closed; **never
  retry**; record and power-cycle.
* Never re-post a frame in the same boot; never write CA 0x400392F0; never
  read the RC misc window 0x10161000; never post mailbox id 5; never send
  HCC groups 1/4 H2D; never `rmmod` the vendor modules.
* A client that produced an exception stays disconnected; do not re-attach
  it in the same boot.
* UART is read-only; recovery is physical (power-off + RESET + TFTP).

## What each rung proves (for the report)

| rung | proves |
| --- | --- |
| M0 | default is byte-inert: no new H2D frame, no new DMA buffer, params visible |
| M1 | D2H group1 id13 layout decoded correctly on live hardware (channel, length, RSSI, frame) |
| M2 | the vendor WAL mgmt-tx carrier is either usable on this firmware or not - the central [U] of the lane |
| M3 | host-SME AP completeness: station ops and EAPOL data delivery are the remaining blockers |
