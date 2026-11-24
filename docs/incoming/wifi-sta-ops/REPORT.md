# REPORT - wifi-sta-ops: AP client station lifecycle + EAPOL/data RX (task-149)

**Lane:** wifi-sta-ops (shared task task-149), host-only.
**Deliverable:** static RE of the vendor station lifecycle + a fail-closed
implementation on the FINAL ALL-PATCH tree + host tests + an attended ladder.
**Fired by this lane:** nothing - no device contact, no UART, no network, no
vendor binary or secret committed.  The Lead owns the bench.

Machine-readable facts: [TABLES.md](TABLES.md).
Ladder: [LADDER.md](LADDER.md) / [ladders/sta-assoc-eapol.json](ladders/sta-assoc-eapol.json).
Patch: [patches/0001-sta-ops.patch](patches/0001-sta-ops.patch) (9 files,
original clean-room C).  Build identity: [refs/artifact-identity.txt](refs/artifact-identity.txt).

---

## 1. Why this lane exists

After wifi-ap-mgmt (task-143) the AP can be started and hostapd reaches
AP-ENABLED, but a client association dies at three points:

1. hostapd's AP SME calls NL80211_CMD_NEW_STATION for every accepted client
   (handle_assoc -> hostapd_sta_add).  cfg80211 refuses without a driver
   .add_station, so hostapd logs "Could not add STA to kernel driver".
2. the firmware needs a per-STA context (user id + AID + association state)
   before it will accept/send data for that client; without it the received
   EAPOL frames never reach hostapd.
3. EAPOL frames are data frames: they must be converted 802.11 -> Ethernet
   and handed to the netdev (hostapd's l2_packet socket), not to the mgmt
   path.

This lane decoded all three from the vendor host module and the local PD4
firmware image, and implemented the fail-closed driver half.  It does not
implement host->AP data TX (see section 7): the 4-way handshake needs the
driver to transmit hostapd's EAPOL-Key msg1/msg3, which is the next lane.

## 2. Vendor cfg80211 station ops - and why they are stubs

| op | vendor | size | behaviour |
| --- | --- | ---: | --- |
| .add_station | wal_cfg80211_add_station 0x1048f0 | 8 B | returns 0 (stub) |
| .change_station | wal_cfg80211_change_station 0x103ac8 | 8 B | returns 0 (stub) |
| .del_station | wal_cfg80211_del_station 0x102470 | 0x29c | posts the WAL cfg-event kick 0x00d0 |
| .get/.dump_station | 0x10270c / 0x1063e0 | - | real |

The vendor runs its AP SME inside the hmac driver layer, not in hostapd:
RX mgmt is processed by hmac_ap_up_rx_mgmt, the auth/assoc responses are
built by hmac_encap_auth_rsp / hmac_mgmt_encap_asoc_rsp_ap and sent via the
WAL mgmt-tx op (wifi-ap-mgmt lane), and the firmware per-STA context is
created there too.  cfg80211's mutating station ops are only needed by a
host-SME (hostapd), so the vendor leaves them as 8-byte "return 0" stubs.

Our driver runs the AP SME in hostapd, therefore:

* .add_station must install the firmware per-STA context itself (the exact
  vendor hmac sequence - section 3);
* .del_station should mirror the vendor wire (WAL kick 0x00d0) and,
  optionally, the g0 id9 user_del;
* .change_station mirrors the authorization flag host-side;
* .get/.dump_station report the host mirror so hostapd sees a live STA.

## 3. The per-STA firmware context (vendor hmac AP-SME sequence)

Decoded chain hmac_ap_assoc_check_chip_exist_user_proc (0x4e598):

    mac_vap_find_user_by_macaddr
    -> hmac_ap_rx_auth_chip_find_user_proc (0x9fb90)   # 12-bit user id
    -> hmac_user_add (0x8eb34)                         # g0 id7 180 B
         body: u16 AID, MAC[6], type=2, RSSI(+100)
    -> hmac_user_get_valid / hmac_user_init_key / hmac_tid_clear
    -> mac_user_set_asoc_state(user+0xa6c, 1)          # state byte user+0xaa9
    -> hmac_config_user_asoc_state_syn (0x63338)       # g0 id1 cmd 0x07d2
         body 4 B: u16 AID, u8 state=1, u8 pad

The same sequence is used by hmac_encap_auth_rsp_get_user_idx (0x9fd7c) and
hmac_config_add_sta_node (0xa5aa4), i.e. it is the vendor's canonical
"client station exists" transaction.

### The 0x07d2 event is firmware-consumed - proven statically

The vendor key lane established the g0 id1 carrier layout and the firmware
table-A dispatch (CA 0x10c23c, rows {u16 key; u16 pad; u32 worker}).  This
lane parsed the local PD4 image:

* table-A row key 0x07d2 -> worker CA 0x83e25 exists [V];
* the worker (Thumb) reads body[0..1] as the AID and looks the firmware user
  up, reads body[2] as the state, writes firmware user +0x3d (the same
  byte the host mirror sets), and returns 0 / 0x64 (user not found) [V].

That is the decisive difference to the WAL mgmt-tx cmd 0x01F2 (absent from
table A, wifi-ap-mgmt T7): the association-state sync is a real firmware
operation, so a host-SME driver can install the client context with
vendor-shaped messages.

## 4. Host-SME station operation semantics (implemented)

* sta_ops=0 (default): every station op returns -EOPNOTSUPP, nothing is
  built or posted (the previous clean stubs' behaviour);
* sta_ops=1, .add_station: validates MAC/AID, finds the AP VAP, posts
  g0 id7 user_add then g0 id1 cmd 0x07d2 state=1 through the shared
  fail-closed hi5622_frame_post() gate (allow_frames budget, gap, HCC gates),
  records the station in the host table, returns 0 only when both frames
  were committed; any post failure returns the error so hostapd does not
  believe the STA exists;
* .change_station: mirrors sta_flags_mask/set; AUTHORIZED gates non-EAPOL
  data RX; no extra firmware message (semantics [U], documented);
* .del_station: mirrors the vendor wire - WAL cfg-event kick opcode 0x00d0,
  body 16 B {MAC, reason, 0xc0 when subtype==12, 1 at +0x0f}; the g0 id9
  user_del builder is decoded and host-tested but posted only with
  sta_user_del=1 (double-delete is the documented E2 crash class); flush
  (mac==NULL) clears the host table without firmware messages;
* .get/.dump_station report the host mirror (station_info.sta_flags).

## 5. EAPOL / data RX delivery (implemented)

Vendor rules decoded from hmac_rx_ring_entry_proc / _frame /
hmac_rx_frame_80211_to_eth and hmac_rx_get_mac_header_len (TABLES T8/T9):

* strip the 802.11 header: 24 B base, 30 B for 4-address frames, +2 when a
  QoS control field is present, +4 for the HT-control/order bit;
* LLC/SNAP AA AA 03 00 00 00 + big-endian ethertype;
* destination/source from the address fields (AP uplink ToDS=1/FromDS=0:
  src=addr2, dst=addr3; FromDS=1: dst=addr1, src=addr3/addr4);
* EAPOL = ethertype 0x888e; EAPOL-Key = type 3, length > 0x5e, key-info
  bit 3 pairwise; the vendor's "4/4-like" detector adds key-data-length 0
  and is evaluated on TX completion (its authorization hook).

Implementation: with eapol_rx=1, the existing wifi-rx-static dp_rx_drain
path hands each validated data MPDU (payload at buffer+0x28) to
hi5622_sta_ops_rx_deliver(), which parses it, looks the source MAC up in the
host station table, applies the deliverability rule (EAPOL always; other
ethertypes only for an authorized station), converts to Ethernet and calls
netif_rx() on the AP VAP netdev - the standard cfg80211 full-MAC convention.
The vendor itself forwards to its own offload/bridging layer (only its
proxy-ARP/ND path calls netif_rx), so this is the clean-room equivalent, not
a vendor imitation.  eapol_rx=0 (default) keeps the RX lane capture-only.

## 6. Patch inventory (9 files, all original C; no vendor byte)

| file | change |
| --- | --- |
| hi5622_sta_ops_logic.c/.h | new portable builders (user_add 180 B, user_del 24 B, asoc-sync 32 B, kick body 16 B), station table, 802.11->Ethernet + EAPOL parser (host-tested) |
| hi5622_sta_ops.c/.h | new kernel glue: 3 module params, cfg80211 station ops, frame posting, RX delivery, status counters |
| hi5622_cfg80211_ops.c | registers .add/.change/.del_station, routes get/dump to the lane, accounts netdev xmit drops |
| hi5622_cfg80211.h | station table + counters in the wiphy private area |
| hi5622_cfg80211.c | lane init/remove hooks + one status line |
| hi5622_dp_txrx.c | calls the RX delivery from the dp_rx_drain commit path |
| Kbuild | two new objects |

Host tests: tools/sta_ops_selftest.c (112 literal-offset checks: wire KATs,
station table, EAPOL fixture parse/conversion, QoS/4-address headers, refusal
paths) + tools/run-host-check.sh; vendor facts re-derived by
tools/sta_ops_re.py from the local artifacts; module build identity by
tools/build-sta-ops-module.sh (see section 8).

## 7. Integration state and the remaining blocker

* ap-mgmt (task-143) delivers D2H mgmt RX (g1 id13) and the WAL mgmt-tx
  family (0x01F2); the mgmt path is fail-closed at mgmt_tx=0 and 0x01F2 has
  no firmware table-A row (proven by that lane and re-checked here).
* wpa-static (task-139) provides add_key/del_key/set_default_key including
  the pairwise (per-STA MAC) form, gated on ap_started and wpa_keys.
* rx-static (task-137) provides the live-bank drain this lane hooks into.
* Remaining blocker for the full 4-way: host -> AP data TX.  The proven TX
  path is the sysfs raw-frame bench instrument (dp_tx_frame, 4 fixed frame
  types); hostapd sends EAPOL-Key msg1/msg3 through its l2_packet socket ->
  ndo_start_xmit, which this lane only accounts as a drop (cfg80211_status
  "xmit_dropped=N").  Until a peer-aware data TX lane lands, association +
  "STA added" + EAPOL RX arming can be validated but the handshake cannot
  complete from the host side.  This is stated as the top next step, not
  hidden.

## 8. Verification performed (host only)

* portable selftest: 112/112 checks pass (C99 -Wall -Wextra -Werror);
* vendor/firmware RE tool: all derived facts match the lane header/TABLES
  (operator-level functions, user_add write offsets, 0x07d2 table row +
  worker semantics, EAPOL helpers);
* module build gate: patch applies to the FINAL ALL-PATCH tree and builds
  against the 6.18.60 ARM tree with 0 warnings, 50 parameters
  (47 + sta_ops/eapol_rx/sta_user_del), no undefined symbols beyond the
  imported kernel/cfg80211 set, strip leaves .text byte-identical:
  stripped 320,908 B sha256 a0a5444d..., unstripped 1,209,260 B
  sha256 661cc7ab..., vermagic 6.18.60 SMP mod_unload ARMv7 p2v8,
  depends=cfg80211;
* the exact artifact was not fired; the ladder is the first hardware use.
  The base all-patch artifact it patches is itself validated only as its
  constituent features (see wifi-stability FINAL-INTEGRATION).

## 9. Limits / unknowns

* Firmware behavior on a second user_add for the same MAC and on user_del
  ordering vs the kick is not host-observable here; the ladder observes it
  (AP stays up, station re-add works, no exception).
* Association-state values other than 1 (associated) and 3 (FT) are not
  pinned; the driver posts only 1.
* .change_station AUTHORIZED has no decoded firmware message; it is a host
  flag only.  If the ladder shows post-association data is dropped by the
  firmware, a candidate is the vendor's TX-completion 4/4 hook (T9) - a
  separate decode.
* hmac_rx_filter_frame_encrypt's protocol/encryption conditions are decoded
  only at the rule level ([I] in TABLES T9); the lane's own rule (EAPOL
  always, authorized data only) is stricter and fail-closed.
* Per-endpoint state: the tx/rx lanes bind two PCI endpoints but the bench
  AP uses band 0; the lane works per wiphy and does not assume band 0 code.

## 10. Safety / publication

* No vendor binary, firmware byte, calibration value, per-unit MAC/serial or
  secret is contained in the patch, src copies, tests or docs; the vendor
  module and the PD4 image stay local/gitignored and are referenced only by
  derived offsets.
* The RE tool prints derived facts only; the patch never references a local
  vendor path and the host check refuses binary artifacts in the lane.
* No device/UART/network access was performed while producing this lane.
  The ladder is design-only and must be fired by the Lead with the recovery
  door READY.
