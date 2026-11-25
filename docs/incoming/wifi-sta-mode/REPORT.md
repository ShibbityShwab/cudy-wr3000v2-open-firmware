# STA (client) mode: join + host-SME auth/assoc + CRX association completion

**Lane:** `wifi-sta-mode` (shared task **task-155**), host-only, no bench
contact. **Written:** 2026-11-25.
**Chain context:** FINAL ASSOCIATION module (all-patch + wpa3-sae + ap-mgmt +
sta-ops + data-tx, `wifi-stability`) + corrected device-facing management-TX
carrier (`wifi-sibling-gpl`, task-151) + hardened WAL 0x123 join body
(`wifi-sta-join`, task-146).

**Evidence markers:** [V] re-derived from the pinned local vendor module
`hi5622v100_wifi.ko` 2.5.24 (relocatable disassembly, read-only local
artifact; never committed), [V*] device-measured by an earlier attended lane
(cited doc), [W] public Hi3881V100 GPL sibling source, [I] inference,
[U] unknown.

**No vendor binary, firmware byte, calibration value, MAC/serial or secret is
committed by this lane.** All analysis is static/read-only; the module is
host-built and never loaded on the bench by this lane.

---

## 1. Problem statement and result

The project already had: the boot protocol golden, full-duplex HCC with the
corrected mailboxes, the cfg80211 wiphy + AP path, WPA2/WPA3 AP on air, the
hardened STA join post and the data TX/RX paths. What was missing for
**router-as-client** was a complete, decoded association flow: what the
firmware does with the join config, which device->host event carries the
association result, and how to report it to cfg80211 without dereferencing
untrusted firmware pointers.

Result: the association completion is the **received association response
management frame on the CRX path** (D2H chip group 1 id 13) consumed by a
driver-side kernel SME, which then calls `cfg80211_connect_done()`. This lane
implements the whole flow (join -> auth -> assoc -> completion -> disconnect)
in portable, host-tested logic plus a thin kernel glue layer, and provides
the first STA-mode ladder. Not fired.

---

## 2. Why the completion is a CRX event: the vendor is a split-MAC host SME

### 2.1 Evidence from the pinned vendor module [V]

1. **STA FSM response table.** `.rodata+0x8074` holds four function pointers
   in order: `hmac_handle_scan_rsp_sta`, `hmac_handle_join_rsp_sta`,
   `hmac_handle_auth_rsp_sta`, `hmac_handle_asoc_rsp_sta`
   (relocation records at `hi5622v100_wifi-data.dis` offsets
   0x8074/0x8078/0x807c/0x8080). These are response handlers of a STA state
   machine, i.e. the host driver processes the *responses*, not the firmware.
2. **The handlers are reached from management RX.** The sibling analysis of
   the same module found `hmac_fsm_call_func_sta` is called only from the
   RX-management direction (`hmac_rx_process_mgmt_event_proc`), and the
   project's committed decode ties event id 13 to
   `hmac_rx_process_mgmt_event` on D2H chip group 1
   (`wifi-sibling-gpl/MGMT-CARRIER-CORRECTION.md` T3; local handler-table
   relocations). Therefore the association response arrives as a **WLAN_CRX**
   management frame, exactly the family the ap-mgmt lane already decodes as
   D2H group 1 id 13 (12-byte HCC header + 48-byte RX-info + 802.11 frame).
3. **The assoc handler parses the received frame and syncs state.**
   `hmac_handle_asoc_rsp_sta` (`.text+0x99040`) extracts the assoc-response
   status, then builds an **H2D** message: `hcc_msg_alloc(28)` at
   `.text+0x99090` and `memcpy_s(payload+12, 28, fsm_record, 28)` at
   `.text+0x99134`, with a 12-byte HCC header whose group nibble = 2 and
   id = 3 (stores at `.text+0x990f0/0x990f8`). That is the association-state
   sync to the device (and its 28-byte payload has the same record shape the
   wifi-sta-join lane decoded as the g2 id3 completion payload).
4. **The cfg80211 result path.** After the response is processed, the vendor
   reports the connect result through the WAL asoc-completion handler
   (`wal_asoc_comp_proc_sta`, WAL chip-group-2 id 3 -> 
   `oal_cfg80211_connect_result` -> `cfg80211_connect_done`). The WAL
   group-2 dispatch is host-side plumbing in this generation; the group-2 id
   13 correction (host-internal mgmt-TX-status relay vs device event) showed
   the same pattern. Hence for the split-MAC model the *device* event is the
   CRX frame; the WAL id 3 record is the host-internal completion notice.

### 2.2 What this corrects

* The wifi-sta-join lane decoded the 28-byte g2 id3 payload (status +0x02,
bssid +0x04, IE lengths +0x0c/+0x10, firmware IE addresses +0x14/+0x18) and
kept it log-only, leaving "wiring the completion" to the RX lanes. This lane
shows the correct completion *event* is the CRX auth/assoc response and wires
that. The g2 id3 decoder is retained: it is the full-MAC alternative and a
useful diagnostic if a future firmware build emits it.
* The completion does **not** arrive on the MAC data ring: the ring carries
data MPDUs (EAPOL after association, ARP/DHCP/ping), never the association
response. Zero ring captures pre-association is therefore expected.
* The firmware-supplied IE addresses in the g2 record are never needed for
the CRX path: the driver already has the assoc request IEs it sent and the
assoc response IEs it just received, so `cfg80211_connect_done()` gets real,
locally-owned buffers. No firmware pointer is dereferenced anywhere.

---

## 3. Implemented flow

### 3.1 Sequence

```
cfg80211 .connect (kernel SME; wpa_supplicant uses it because .connect exists)
  1. precheck: sta_connect mode consistent with sta_join, own MAC valid,
     BSS channel present, timeout in range, allow_frames budget >= 3   [fail-closed]
  2. WAL cfg-event opcode 0x123 (108-B body, hardened device-address join)
     -> hi5622_frame_post() one-shot post          [wifi-sta-join + this lane]
  3. mode_begin: store own MAC/BSSID/SSID/privacy/channel/req_ie;
     build 802.11 authentication request (open system, 30 B)
     -> corrected HCC g0 id 6 mgmt carrier, inline frame, post
  4. arm sta_connect_timeout_ms timer; state AUTH_SENT
  5. CRX (D2H g1 id13) auth response -> state AUTH_DONE; build assoc request
     (SSID/rates IEs) -> post; state ASSOC_SENT
  6. CRX assoc response -> state CONNECTED; cfg80211_connect_done(status=0,
     bssid, req_ie=assoc request IEs, resp_ie=assoc response IEs)
  7. .disconnect -> WAL opcode 0x00d0 post -> cfg80211_disconnected()
     (a pending connect is failed first with status=-1)
```

Failure paths: an AP status != 0 completes with that 802.11 status; a post
refusal, an out-of-order/corrupt response, or the bounded timeout complete
with `status=-1` (timeout carries the cfg80211 timeout_reason AUTH/ASSOC).
Every state transition is validated by the portable layer; one connect per
module load; no retry anywhere.

### 3.2 Management-TX carrier

Auth/assoc requests use the **corrected** device-facing carrier from
wifi-sibling-gpl (task-151): raw HCC group 0 id 6 control message, 48-byte
device TX-control block + inline 802.11 frame. The AP `.mgmt_tx` op already
uses it; this lane adds an internal path for the kernel-SME frames (the op
itself refuses non-AP interfaces). Fields: channel from the connect request,
dst nibble 0, VAP id 0, band 0, cookie = sequence & 0xf, user_id = 0
(no-user -> 0x0fff). Every frame goes through `hi5622_frame_post()`
(allow_frames budget, inter-frame gap, armed/released/ACTIVE lane, structural
+ firmware-handler validation, bounded announce poll, no retry).

### 3.3 Frame builders and decoders (portable C99)

* Authentication request: 24-B three-address header (FC 0x00b0, DA=BSSID,
  SA=own, BSSID, sequence), body {algorithm 0, sequence 1, status 0} = 30 B.
* Association request: 24-B header + capability (ESS | Short Preamble |
  Privacy when WPA) + listen interval 10 + SSID IE + supported-rates IE
  (1/2/5.5/11/6/9/12/18) + extended supported-rates IE (24/36/48/54).
* Auth/assoc response decoders: strict subtype, minimum length, 802.11
  status/AID extraction, and an element-by-element IE boundary walk (a
  truncated or odd trailing IE is refused, never partially forwarded).
* H2D association-state sync builder: 12-B header (group 2, id 3, total 40,
  tag 0x5a5a) + 28-B payload (status, BSSID, IE lengths, device IE
  addresses). Host-tested only - see open items.

---

## 4. EAPOL / data path in STA mode

No new data-path code is needed for STA mode:

* **RX:** the MAC completion ring / `dp_rx_drain` path (wifi-rx-static,
  wifi-datapath lanes) delivers data frames, including EAPOL (ethertype
  0x888e, a normal data frame to the netdev - confirmed in both the public
  sibling source [W] and the vendor architecture), to the AP/STA netdev.
  Pre-association the ring is empty by design (management is the CRX path).
* **TX:** `ndo_start_xmit` -> dp TX ring path (wifi-data-tx lane, proven for
  EAPOL msg1/msg3 in AP mode) sends the STA's EAPOL/data frames; the same
  peer-aware path is used once the STA is associated.
* The kernel-SME connect completion makes wpa_supplicant start the 4-way
  handshake over the netdev; WPA2 GTK/IGTK and WPA3-SAE AP paths are already
  proven on air (AP role) and the STA key path is the inherited key lane's
  behaviour. The ladder's data rung is the first end-to-end proof.

---

## 5. Verification performed (host-only)

| gate | command | result |
| --- | --- | --- |
| host unit tests | `gcc -std=c99 -Wall -Wextra -Werror -pedantic` + `tools/sta_mode_selftest.c` | **140/140 PASS** |
| chain reconstruction | `tools/build-sta-mode-module.sh` (association tree + carrier patch + sta-join patch + this patch) | applies cleanly, no rejects/orig files |
| module build | 6.18.60 luofu tree, cross gcc 14.4.0 | **0 compiler/modpost warnings**, no undefined symbols |
| artifact | debug-stripped | **364,572 B sha256 `8c0adf93995fd5014b73107de7fed77c825e3eee88fec8cc7a1c284f501c2059`**; unstripped 1,396,116 B sha `01912d661c0643b46bd7439cda80fc776ed4495d066264c0a85d263eb5ddb938`; strip does not change `.text` |
| params | `modinfo -p` | **58** (association 56 + `sta_connect` + `sta_connect_timeout_ms`); `sta_join`, `allow_frames` retained |
| cfg80211 provider | `cfg80211-provider.symvers` + `nm -u` | **14** symbols; `cfg80211_connect_done` and `cfg80211_disconnected` imported |
| vermagic | cross strings | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| features | strings | host-SME connect, CRX completion, timeout param present |

The artifact is local evidence only (`/tmp/wifi-sta-mode-artifact/`) and is
never committed.

---

## 6. Open items / [U]

1. **On-air validation of the STA flow** - the ladder (not fired) answers:
   does the firmware accept the 0x123 join, does the host-SME auth request
   leave the device on the corrected carrier, do the CRX responses arrive
   with plausible FC/BSSID, does the AP associate us, and does the data path
   open.
2. **H2D association-state sync (g2 id 3)** - the 28-byte state sync shape is
   [V] decoded but the firmware receive-table entry for group 2 id 3 on our
   image is [U]; `hi5622_frame_post()` would refuse it. Not posted.
3. **Auth algorithm** - only open-system authentication is built. For WPA2/WPA3
   PSK APs the authentication is still open system (the security is in the
   association/4-way), so this is expected to suffice; SAE (WPA3) needs the
   separate SAE exchange through cfg80211 (wpa_supplicant-level, future work).
4. **HT/VHT association IEs** - the assoc request carries the mandatory
   SSID/rates only; HT/VHT capability IEs (band/ht_mode lanes) are a follow-up
   if the AP requires them for higher rates.
5. **Firmware-supplied g2 id3 IE pointers** - validated, never dereferenced;
   whether the firmware can ever emit them on our image remains [U].
6. **RF** - the bench client association is RF-limited (~44 dB loss without
   antennas; flaky laptop MT7922). Antennas being attached; the ladder is
   ordered so the mgmt/rx evidence can be captured before data rungs.

---

## 7. Reproduction

```sh
# 1. host logic tests (no device, no vendor bytes)
gcc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
    -o /tmp/sta_mode_selftest tools/sta_mode_selftest.c src/hi5622_sta_sme.c
/tmp/sta_mode_selftest                     # PASS: 140/140

# 2. full chain + module build (uses the project 6.18.60 tree)
ASSOC_TREE=/tmp/wifi-assoc-build/integ ./tools/build-sta-mode-module.sh

# 3. patch dry-run on a fresh base
cd <association-tree> && patch -p1 < <sibling-gpl>/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
patch -p1 < <sta-join>/patches/0001-sta-join-hardening.patch
patch -p1 --dry-run < <this-lane>/patches/0001-sta-mode-connect-crx.patch
```

Ladder: [TEST-LADDER.md](TEST-LADDER.md). Tables: [TABLES.md](TABLES.md).
