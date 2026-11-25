# TABLES - STA (client) mode: join + host-SME auth/assoc + CRX completion

**Lane:** `wifi-sta-mode` (task-155), host-only. **Written:** 2026-11-25.
Evidence markers: [V] local pinned vendor module 2.5.24 relocatable
disassembly (never committed), [W] public Hi3881V100 GPL sibling, [I]
inference, [U] unknown.

---

## T1 - Evidence sources and anchors

| item | value |
| --- | --- |
| vendor module | `hi5622v100_wifi.ko` 2.5.24 (local only; 2.4.15 carries the same constants) |
| relocatable disassembly | `docs/incoming/wifi-shim-port/raw/hi5622v100_wifi.dis` / `-data.dis` (local-only artifacts, gitignored) |
| STA FSM table | `.rodata+0x8074`: `hmac_handle_scan_rsp_sta`, `hmac_handle_join_rsp_sta`, `hmac_handle_auth_rsp_sta`, `hmac_handle_asoc_rsp_sta` [V] |
| assoc-response handler | `hmac_handle_asoc_rsp_sta = .text+0x99040` (.text offsets, link-time, 2.5.24) [V] |
| H2D state-sync build inside it | `hcc_msg_alloc(28)` @0x99090; header stores group 2 @0x990f0, id 3 @0x990f8; `memcpy_s(payload+12, ..., 28)` @0x99134; `hcc_msg_tx` @0x99150 [V] |
| mgmt RX carrier | D2H HCC group 1 id 13 = `hmac_rx_process_mgmt_event` [V handler-table relocations; `wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md` T3] |
| mgmt TX carrier | raw HCC group 0 id 6 control message, 48-B device TX-control + inline frame (`wifi-sibling-gpl` task-151) [V] |
| join carrier | WAL cfg-event opcode `0x123`, 108-B body (`wifi-sta-join` task-146) [V] |
| target kernel | luofu 6.18.60, VMSPLIT_3G, dma-ranges identity 0x80000000..0x8fffffff |
| kernel tree used | `/home/shibbityshwab/openwrt-lab-upstream-target/.../linux-6.18.60` |

## T2 - Vendor STA response FSM (split-MAC evidence) [V]

| idx | handler | interpretation |
| ---: | --- | --- |
| 0 | `hmac_handle_scan_rsp_sta` | scan response processed in host |
| 1 | `hmac_handle_join_rsp_sta` | join response processed in host |
| 2 | `hmac_handle_auth_rsp_sta` | 802.11 auth response -> assoc request |
| 3 | `hmac_handle_asoc_rsp_sta` | 802.11 assoc response -> cfg80211 connect result |

Reached only from the RX-management direction (`hmac_fsm_call_func_sta` called
from `hmac_rx_process_mgmt_event_proc`; sibling cross-check [W]
`hmac_sme_sta.c`). Consequence: the association completion is the received
assoc response frame on the CRX path.

## T3 - Authentication request (driver -> AP), 30 B

| off | width | value |
| ---: | ---: | --- |
| 0 | u8 | FC0 = `0xb0` (management, subtype auth) |
| 1 | u8 | FC1 = 0 |
| 2 | u16 | duration 0 |
| 4 | u8[6] | addr1 = DA = AP BSSID |
| 10 | u8[6] | addr2 = SA = our STA MAC |
| 16 | u8[6] | addr3 = BSSID |
| 22 | u16 | sequence control (`seq & 0xf` << 4) |
| 24 | u16 | auth algorithm = 0 (open system) |
| 26 | u16 | auth sequence = 1 |
| 28 | u16 | status = 0 |

## T4 - Association request (driver -> AP), 28 + IEs

| off | width | value |
| ---: | ---: | --- |
| 0..23 | - | header: FC0 = `0x00` (subtype assoc req), DA=BSSID, SA=own, BSSID, seq |
| 24 | u16 | capability: ESS 0x0001 + Short Preamble 0x0020 (+ Privacy 0x0010 when WPA) |
| 26 | u16 | listen interval = 10 |
| 28 | - | IEs: SSID (0), supported rates (1, 8 B: 0x82 0x84 0x8b 0x96 0x0c 0x12 0x18 0x24), extended supported rates (50, 4 B: 0x30 0x48 0x60 0x6c) |

Buffer bound `HISM_MAX_FRAME`; the same IE list is stored as the
`cfg80211_connect_done()` req_ie after the assoc request is posted.

## T5 - Authentication response (AP -> driver) decode

| off | width | field |
| ---: | ---: | --- |
| 0 | u8 | FC0 must be 0xb0 (protocol 0, type mgmt, subtype auth) |
| 4 | u8[6] | addr1 must equal our MAC |
| 16 | u8[6] | addr3 must equal the BSSID |
| 24 | u16 | algorithm (not interpreted, must be present) |
| 26 | u16 | sequence |
| 28 | u16 | **status** (0 = success, else connect fails with that status) |

Minimum length 30 B; other subtypes return -ENODATA-like refusal.

## T6 - Association response (AP -> driver) decode

| off | width | field |
| ---: | ---: | --- |
| 0 | u8 | FC0 must be 0x10 (subtype assoc response) |
| 4 / 16 | u8[6] | addr1 = own MAC / addr3 = BSSID (checked) |
| 24 | u16 | capability info |
| 26 | u16 | **status** (0 = success) |
| 28 | u16 | AID (masked to 0x3fff) |
| 30.. | - | IEs: boundary-walked element by element; **truncated/odd tails refused** |

Success -> `cfg80211_connect_done(status=0, bssid, req_ie, resp_ie)`;
failure -> completion with the AP status, no retry.

## T7 - SME state machine (portable layer, every transition validated)

| state | meaning | allowed next |
| --- | --- | --- |
| `OFF` (0) | no connect | JOIN_POSTED on .connect |
| `JOIN_POSTED` (1) | join frame accepted | (auth post) -> AUTH_SENT |
| `AUTH_SENT` (2) | auth request posted | AUTH_DONE (status 0) / FAILED (status != 0) |
| `AUTH_DONE` (3) | auth OK | (assoc post) -> ASSOC_SENT |
| `ASSOC_SENT` (4) | assoc request posted | CONNECTED (status 0) / FAILED |
| `CONNECTED` (5) | cfg80211_connect_done(0) sent | .disconnect -> OFF |
| `FAILED` (6) | completion sent with failure | next load only |

Out-of-order responses are refused (-2) and never complete a connect; a
pending connect that times out completes with `status=-1` and
`timeout_reason` AUTH (2) or ASSOC (3).

## T8 - Module parameter contract and gate matrix

| param | default | values | effect |
| --- | --- | --- | --- |
| `sta_join` | 0 | 0/1/2 (0444) | join frame: fail-closed / dry / one post per load |
| `sta_connect` | 0 | 0/1/2 (0444) | **new**: off (join-only path) / dry / live host-SME connect |
| `sta_connect_timeout_ms` | 4000 | 500..60000 (0644) | bounded auth/assoc response timeout |
| `allow_frames` | 16 | budget | provider frame budget; LIVE needs >= 3 (join+auth+assoc) |

| sta_join | sta_connect | behaviour |
| ---: | ---: | --- |
| any | 0 | legacy join-only path (nothing new; AP bench behaviour unchanged) |
| 0 | 1/2 | -EINVAL (modes must agree) |
| 1 | 1 | join body + auth/assoc frames built+validated, **nothing posted** |
| 2 | 2 | join posted one-shot, then live auth/assoc/completion |
| 2 | 1 | -EINVAL |
| >2 | any | -EINVAL |

## T9 - CRX management-RX event layout (D2H group 1 id 13) [V]

| field | offset | notes |
| --- | ---: | --- |
| HCC group/id | hdr[0] & 0xf = 1, hdr[6..7] = 13 | tags 0x5a5a at hdr+10 |
| RX-info block | payload +0 | 48 B (vendor `hcc_msg->cb` copy) |
| channel | payload +7 | maps to centre frequency |
| frame length | payload +17 (u16) | must equal wire total - 60 |
| RSSI | payload +39 (s8) | vendor raw; used only in a plausible dBm range or 0 |
| 802.11 frame | payload +48 | management only (FC type 0) |

The STA SME intercepts auth (0xb0) and assoc response (0x10) frames addressed
to us before the AP-delivery path; everything else keeps the inherited
behaviour (`mgmt_rx=1` -> `cfg80211_rx_mgmt` for the AP wdev).

## T10 - H2D association-state sync (decoded, **not posted**)

| off | width | value |
| ---: | ---: | --- |
| 0 | u8 | group nibble 2 |
| 1 | u8 | `(dst << 4) | 0x01` |
| 2 / 3 | u8 | vendor VAP bytes (0) |
| 4 | u16 | total length = 40 |
| 6 | u16 | id = 3 |
| 8 / 9 | u8 | flags 0 / dst |
| 10 | u16 | tag 0x5a5a |
| 12 | u16 | status |
| 14 | u8[6] | BSSID |
| 20 | u16 | reserved (payload +0x0a, [U]) |
| 24 | u32 | req_ie length |
| 26 | u32 | resp_ie length |
| 30 | u32 | req_ie device address |
| 34 | u32 | resp_ie device address |

Shape [V] from `.text+0x99090/0x99134`; firmware receive-table entry for
group 2 id 3 is [U] on our image, so only the portable builder + host test
exist (the kernel glue does not post it).

## T11 - Artifact identity and build assertions (host-built, NOT fired)

| assertion | value |
| --- | --- |
| chain | FINAL ASSOCIATION 61f7880 + sibling-gpl carrier patch + sta-join hardening patch + this patch |
| patch | 8 files, 1354 insertions (`patches/0001-sta-mode-connect-crx.patch`) |
| host selftests | **140/140 PASS** (strict C99) |
| compiler/modpost | **0 warnings / 0 errors / no undefined symbols** |
| stripped artifact | **364,572 B sha256 `8c0adf93995fd5014b73107de7fed77c825e3eee88fec8cc7a1c284f501c2059`** |
| unstripped | 1,396,116 B sha256 `01912d661c0643b46bd7439cda80fc776ed4495d066264c0a85d263eb5ddb938` |
| strip check | `.text` byte-identical before/after |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| params | **58** (56 inherited + `sta_connect` + `sta_connect_timeout_ms`) |
| cfg80211 symvers | **14** lines (adds `cfg80211_connect_done`, `cfg80211_disconnected`; both imported) |

## T12 - Corrections and open items

| id | claim | status |
| --- | --- | --- |
| C1 | The STA association completion is the CRX auth/assoc response frame (D2H g1 id13), consumed by the driver SME | [V] handlers; [I] on-air |
| C2 | WAL g2 id3 is the host-internal completion relay / device-state sync family, not a data-ring record | [V] handler shapes + sibling correction pattern |
| C3 | The firmware-supplied g2 id3 IE addresses are never needed or dereferenced on the CRX path | implemented |
| C4 | EAPOL/data for STA = inherited data ring + ndo_start_xmit; no new carrier | [V]/[W] architecture |
| U1 | Does the firmware accept the 0x123 join and let the host-SME auth/assoc proceed? | bench ladder B3 |
| U2 | H2D g2 id3 state-sync receive-table entry on our image | [U] - not posted |
| U3 | HT/VHT association IEs required by the AP? | follow-up |
| U4 | WPA3-SAE STA exchange (SAE frames through cfg80211) | follow-up |
| U5 | RF path (antennas, ~44 dB loss) | bench |
