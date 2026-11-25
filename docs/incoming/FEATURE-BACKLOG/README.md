# FEATURE BACKLOG - the Cudy WR3000 v2.0 (Hi5622V100, Wi-Fi 6 802.11ax) clean-room driver

**Status:** 2026-11-27. Every item is the SMALLEST EVALUABLE UNIT: a decode or a patch with a
binary PASS/FAIL, host-testable where possible, and a bench fire where not. Owner lanes are
swarm lanes; the Lead fires.

## 0. The hardware ceiling

- Hi5622V100: **802.11ax (Wi-Fi 6)**, 2x2, dual-band (2.4 + 5 GHz), VHT 160 MHz-capable.
- **Wi-Fi 7 (802.11be) is not on this silicon** - the newest possible target is a complete
  Wi-Fi 6 AP/STA. The stock WR3000 v2.0 is AX3000-class.
- Two endpoints = two host views of ONE chip (never run the boot ladder on 0001).

## 1. The gate: RX capture (blocks: association, WPA3 client, DHCP, real traffic, throughput)

| id | item | evaluation | state |
| --- | --- | --- | --- |
| RX-1 | The stock H2D trace: boot the vendor image, capture its own driver's H2D init sequence | a captured H2D message list from the vendor's working boot, diffed against ours | NOT STARTED (needs the stock boot + the vendor debug) |
| RX-2 | The firmware RX init: identify which vendor H2D message/sequence initialises the firmware's RX rings/PPDU descriptors | a decoded message (group/id/payload) + the firmware handler address | IN PROGRESS (candidates exhausted; the h2d-init lane refuted the boot-chain; the trace remains) |
| RX-3 | The RX probe verdict: with the rings armed + a real client auth, classify DEVICE-WROTE-FRAMES / MAC-WROTE-NOTHING / FORMAT-MISMATCH | the dp_rx_probe verdict on the bench | TOOL READY (not fired with the final module) |
| RX-4 | The completion-phase + WOC-class + rx-init candidates | each = a fired module + the frames>0 + page golden verdict | TESTED NEGATIVE (all) |
| RX-5 | The F4 status-forwarding (the vendor forwards status bits 1/8 to the firmware core) | the forwarding message implemented + the fire | NOT STARTED |

## 2. Client association path (after RX-1..RX-5)

| id | item | evaluation | state |
| --- | --- | --- | --- |
| AS-1 | The client auth/assoc reaches hostapd (the CRX mgmt RX) | hostapd logs the auth/assoc of a real client | BLOCKED (RX) |
| AS-2 | The 4-way handshake (EAPOL) | a successful WPA2 4-way (the keys installed) | BLOCKED (RX) |
| AS-3 | DHCP + first ping | a client IP + ICMP through the AP | BLOCKED (RX) |
| AS-4 | WPA3-SAE client association (the AP already supports SAE) | a SAE client completes the handshake | BLOCKED (RX) |

## 3. 5 GHz

| id | item | evaluation | state |
| --- | --- | --- | --- |
| 5G-1 | The dual-band wiphy (one phy, 2 bands, the secondary endpoint no wiphy) | iw phy shows both bands; the 0001 endpoint logs "SECONDARY host view" | DONE (proven) |
| 5G-2 | The regdomain (country US, 5 GHz IR) | iw reg get: country US + the IR flags | DONE (the cfg80211 rebuild; packaged) |
| 5G-3 | The 5 GHz AP order (vap_add -> the deferred user_add -> ap_chan(band=1) -> ap_ssid -> ap_cfg -> vap_start) | AP-ENABLED + page golden | DONE (proven; module 13e7bf4c) |
| 5G-4 | The 5 GHz beaconing (the band/VAP activation: the module channel ctx) | the observer's 5180 scan finds cudy-ap-test-5g | IN PROGRESS (lane rx-5g-beacon) |
| 5G-5 | DFS/radar (the DFS channels 52-144) | the AP starts on a DFS channel with CAC | NOT STARTED |
| 5G-6 | 160 MHz operation | a 160 MHz AP beacon + an 80+80/160 client | NOT STARTED |

## 4. 802.11ax (Wi-Fi 6) features

| id | item | evaluation | state |
| --- | --- | --- | --- |
| AX-1 | The HE capability advertisement (the wiphy's HE caps + the beacon's HE IE) | the beacon carries the HE IE; an ax client negotiates HE | NOT STARTED (the vendor HE caps decoded in the user_add: 58 B @0x54) |
| AX-2 | HE/EHT user info in the add_user message (the vendor's 24 B VHT @0x10, 44 B HT @0x28, 58 B HE @0x54) | a decoded HE user-info struct + the message bytes | PARTIAL (the sizes known) |
| AX-3 | DL/UL OFDMA (the resource units / the MU EDCA) | the firmware's OFDMA messages + the trigger frames | NOT STARTED |
| AX-4 | MU-MIMO | the sounding + the MU group config | NOT STARTED |
| AX-5 | TWT (target wake time) | the TWT agreement messages | NOT STARTED |
| AX-6 | BSS coloring | the HE BSS color in the beacon + the OBSS PD | NOT STARTED |
| AX-7 | HE sounding/beamforming | the NDPA/NDP + the sounding report | NOT STARTED |

## 5. Aggregation / data plane

| id | item | evaluation | state |
| --- | --- | --- | --- |
| BA-1 | A-MPDU aggregation (the BA session setup: addba req/resp) | an aggregated TX/RX (the BA session established) | NOT STARTED (the vendor hmac_ba_* machinery) |
| BA-2 | A-MSDU | an A-MSDU TX | NOT STARTED |
| BA-3 | The BA window/scoreboard (the firmware's reorder) | the reorder window messages | NOT STARTED |

## 6. Rate control / PHY

| id | item | evaluation | state |
| --- | --- | --- | --- |
| RC-1 | The rate table/algorithm config (the vendor alg engine) | the vendor alg cfg messages decoded + the rate selection observable | NOT STARTED (565 hmac_config_* functions; the alg tables at .data) |
| RC-2 | The per-rate TX power / the regulatory power limits | the power table + the per-channel limits | NOT STARTED |
| RC-3 | The TX power calibration (the cal data usage) | the calibrated power applied | PARTIAL (the calibration lanes) |

## 7. Power management

| id | item | evaluation | state |
| --- | --- | --- | --- |
| PM-1 | The AP-side power save (the TIM/DTIM, the PS-Poll/U-APSD) | a PS client buffers/receives via the TIM | NOT STARTED |
| PM-2 | The chip power gating / the per-VAP power | the vendor's power messages + the idle current | ANALYSED (dead-by-design per the power-perf lane) |

## 8. Roaming / services

| id | item | evaluation | state |
| --- | --- | --- | --- |
| RO-1 | 802.11k (the neighbor reports) | the RRM messages | NOT STARTED |
| RO-2 | 802.11v (the BSS transition) | the BTM frames | NOT STARTED |
| RO-3 | 802.11r (the fast handover) | the FT keys | NOT STARTED |
| SV-1 | WPS | the WPS exchange | NOT STARTED |
| SV-2 | Mesh (802.11s) | a mesh peer link | NOT STARTED |
| SV-3 | Monitor mode / the spectral scan | a monitor capture / the spectral data | PARTIAL (the driver advertises monitor; not exercised) |
| SV-4 | The per-STA statistics | the stats messages | NOT STARTED |

## 9. Image / integration / publication

| id | item | evaluation | state |
| --- | --- | --- | --- |
| IMG-1 | The image bake with the regdomain fix + the latest modules | the baked image verify (the same 84/84) | READY (the config + the module in the OWRT tree) |
| IMG-2 | The image #2 install (rootfs-only) | the post-boot receipts | NOT FIRED (the runbook P2) |
| PUB-1 | The platform/upstream v2 30-patch review form | the folds + the generation | BLOCKED (16 folds + the pipeline decision + one attended boot) |
| PUB-2 | The legacy history purge | the history clean | OWNER-ONLY |
| PUB-3 | The curated-repos sync | the pinned repos current | PREPARED (the publication lane) |

## Priority order (the critical path)

1. **RX-1/RX-2** (the stock H2D trace + the firmware RX init) -> unlocks AS-1..AS-4, everything live
2. 5G-4 (the 5 GHz beaconing) -> a dual-band AP on air
3. BA-1 (the A-MPDU) + RC-1 (the rate control) -> real throughput
4. 5G-5 (DFS) + 5G-6 (160 MHz) -> the full 5 GHz
5. AX-1..AX-7 (the 802.11ax feature set) -> the full Wi-Fi 6
6. The rest (roaming/services/power save)
7. IMG/PUB (the delivery track, parallel)

## The deep specs (each item: the vendor source, the PASS/FAIL, the test, the deps, the state)

| spec | areas |
| --- | --- |
| [SPEC-RX.md](SPEC-RX.md) | the RX gate RX-1..RX-5 (the critical path): the stock H2D trace, the firmware RX init, the probe verdict, the candidate negatives, the F4 forwarding |
| [SPEC-5G.md](SPEC-5G.md) | 5G-1..5G-6 (wiphy/regdomain/order DONE; the beaconing in progress; DFS, 160 MHz) |
| [SPEC-AX.md](SPEC-AX.md) | AX-1..AX-7 (the 802.11ax set; the vendor's HE block decoded; the HE caps partial) |
| [SPEC-BA-RC.md](SPEC-BA-RC.md) | BA-1..3 (the aggregation; the BA-1 partial - the selftest needs the portable model) + RC-1..3 (the rate control) |
| [SPEC-PM-ROAM.md](SPEC-PM-ROAM.md) | PM-1..2 (power save), RO-1..3 (k/v/r), SV-1..4 (WPS/mesh/monitor/stats) |
| [SPEC-IMG-PUB.md](SPEC-IMG-PUB.md) | IMG-1..2 (the image bake/install), PUB-1..3 (the upstream/publication track) |

## Verification status (the Lead's pass, docs/incoming/LANE-VERIFICATION/)

- rx-h2d-trace: PASS (the RUNBOOK + the read-only trace method)
- ax-he-caps: PASS (the tests 109/109 + the patch)
- rx-ba-agg: PARTIAL (the decode usable; the selftest is kernel-header-dependent -> the
  portable model must be committed before the fire)
- rx-5g-beacon / rx-rate-ctl: in progress

