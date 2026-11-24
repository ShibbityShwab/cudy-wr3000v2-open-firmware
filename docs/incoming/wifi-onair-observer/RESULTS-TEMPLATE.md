# External-observer on-air result - `cudy-ap-test`

Copy this file into `results/onair-<date>-<label>.md` after an attended run and
fill every row from the run's `matrix.md` + phase logs.  Text only: no pcap,
no neighbour SSID/BSSID, no credentials, no vendor artifacts.

## Header

- verdict (overall): **HARNESS PASS / HARNESS FAIL / ABORTED**
- on-air beacon gate (L2): **PASS / FAIL**
- association result: **EXPECTED-FAIL at AUTH / UNEXPECTED-PASS / FAIL**
- date/time (local + UTC):
- operator (Lead) / recovery door armed: yes
- bench AP: final all-patch module `008aca5`, stripped 308,080 B sha `5b587949`
  loaded with the standard bench parameters (list them):
- AP fixture used (hostapd config + whether `ieee80211n`/HT block present):
- router console/UART capture path (Lead):
- observer host: kernel / wpa_supplicant / iw versions (from preflight.log):
- observer interface: `<iface>` on phy / MAC (locally-administered):
- evidence directory (local, gitignored):
- run command line:

## Environment / guard

| check | value |
| --- | --- |
| base uplink `wlan0` before (SSID / channel / state) | |
| base uplink after cleanup | |
| wiphy 2-managed combination | |
| NetworkManager drop-in installed/removed | |
| host internet still working after cleanup | |

## Phase verdicts

| phase | expected | actual | evidence file | notes |
| --- | --- | --- | --- | --- |
| L0 preflight | PASS | | preflight.log | |
| L1 second iface | PASS | | iface-up.log | |
| L2 passive scan ch6 | **PASS** | | scan-ch6.txt / scan-ch6.json | SSID / BSSID / freq / DS ch / signal / IEs: |
| L2b monitor capture | PASS or SKIP | | beacon-summary.json (+ pcap local) | beacon count / median interval / auth counters: |
| L3 WPA2 association | EXPECTED-FAIL at AUTH | | supplicant.log, status-final.txt, assoc-result.json | stage: / reason: |
| L3b 4-way handshake | not reached | | handshake-evidence.txt | if reached: PTK/GTK result |
| L4 DHCP | BLOCKED / SKIP | | dhcp-result.json | offer/ack: |
| L4b ping | SKIP / PASS / FAIL | | ping.txt | target: |
| L5 WPA3-SAE | SKIP or EXPECTED-FAIL | | wpa3-sae.log, scan-sae-check.json | SAE seen: |
| cleanup | PASS | | cleanup.log | leftovers: |

## On-air beacon detail (L2, decisive)

- SSID seen: / BSSID: / freq: / DS channel: / signal dBm:
- beacon interval (TU):
- capability bits (Privacy?):
- RSN IE: version / group cipher / pairwise / AKM / capabilities:
- other BSS count (do NOT record neighbour identities):
- BSSID cross-check with the router-side AP VAP MAC:

## Association detail (L3)

- client MAC: / wpa_supplicant config (redacted psk):
- stage reached: AUTH / ASSOC / 4-WAY / COMPLETED
- exact failure line(s) from supplicant.log:
- monitor-side: client AUTH frames seen on air: / AP AUTH replies seen on air:
- if COMPLETED: PTK/GTK cipher, `wpa_cli status` fields, DHCP/ping result
- interpretation vs the documented usable-AP blocker (mgmt TX/RX fail-closed):

## Router-side companion observations (Lead, console/UART only)

- AP-start / GTK dmesg lines:
- RX path: `dp_rx_drain` completions / rptr movement / firmware events while
  the client traffic ran:
- page golden / state / exception mask / staging during the observer run:
- any stop-rule trigger:

## Findings / new evidence

- (e.g. "beacon on air confirmed at ~102 ms interval, RSSI -42 dBm" or
  "no beacon observed - hand off to the beacon path")
- (e.g. "AUTH unanswered on air: reproduces the mgmt-tx blocker")
- follow-ups / next rungs:

## Artefacts and redaction checklist

- [ ] results file is text only
- [ ] no neighbour SSIDs/BSSIDs
- [ ] no pcap committed (stays in local evidence dir)
- [ ] no credentials / per-unit keys / vendor bytes
- [ ] parent ledger / RE report updated if a claim changed
