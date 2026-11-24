# TABLES - wifi-data-tx (task-150)

Machine-readable facts the lane's code is written from.  Evidence markers:
**[V]** decoded from a named local vendor artifact by an earlier lane (the
artifact itself is local/gitignored, never committed); **[V\*]** measured by an
attended bench run and quoted from the committed report of that lane; **[I]**
inference; **[U]** unknown (the code fails closed rather than guessing).
CPU address (CA) = firmware file offset + 0x40000; host-module offsets are
link-time .text offsets of the 2.4.15/2.5.24 vendor hi5622v100_wifi.ko.

Base chain: FINAL ALL-PATCH clean-room module
(stripped 308,080 B sha256 5b587949..., 47 params, wifi-stability
FINAL-INTEGRATION) **+ wifi-sta-ops** patch (task-149, 50 params).

---

## T1 - 802.3 frame -> 802.11 data MPDU (AP transmit, FromDS=1 / ToDS=0)

| element | value | source |
| --- | --- | --- |
| Ethernet header consumed | 14 B (DA at 0, SA at 6, type at 12); VLAN tags refused | 802.3 |
| FC data, FromDS | 0x0208 (ToDS=0, FromDS=1) | [V] mirrors the decoded vendor RX address rules (sta-ops T8) and 802.11-2016 |
| FC QoS data, FromDS | 0x0288 (this lane transmits non-QoS; the builder supports QoS for tests) | [I] |
| Protected bit | FC bit 14 = 0x4000 | 802.11-2016 |
| header length | 24 B (26 B with QoS control) | sta-ops decoded hmac_rx_get_mac_header_len [V] |
| addr1 | DA (the client MAC) | FromDS=1 address rules |
| addr2 | BSSID / TA = AP netdev address | FromDS=1 address rules |
| addr3 | SA = Ethernet source (hostapd's own frames: = BSSID) | FromDS=1 address rules |
| LLC/SNAP | AA AA 03 00 00 00 + Ethernet type big-endian | same prefix the bench-proven ARP builder used (wifi-dp-txrx) [V\*] |
| sequence control | 12-bit seq << 4 | 802.11-2016 |
| FCS | not built (the MAC appends it) | wifi-dp-txrx FRAME-SPECS [V] |

Transmitter gotcha: the previous bench frame (broadcast ARP) used FC 0x0088
(subtype QoS data) with a 24-byte header (no QoS control field).  This lane
keeps subtype 0 (plain data) so the header is self-consistent; whether the
firmware cares about the subtype is [I] and the ladder's L3/L4 answer it.

## T2 - Frame buffer handed to the MSDU ring (same as the proven TX path)

    +0x00  u32  host bookkeeping word ("HDTX" = 0x48584454)      [V] vendor skb ptr
    +0x04  16 B device-visible header, all zero                  [V] hmac_tx_msdu_dscr_add 0x19b40
    +0x14  802.11 MPDU (ctl length counts only these bytes)      [V*] TX COMPLETED
    ring entry buf_devva = buffer + 4 = DMA address + HDTX_FRAME_HOST_HDR

One coherent 0x400 B buffer per in-flight frame, 16 per wiphy
(HDTX_POOL_DEPTH), allocated at cfg80211 init only when data_tx=1; every
buffer is range-checked against the device window
DP_HOST_DMA_LO..HI = 0x80000000..0x8fffffff (dp_rings.h) before use.

## T3 - MSDU ring entry, publish order and TXQ-table update [V]

    8-byte entry: { u32 buf_devva; u32 ctl }
      ctl[11:0] frame len, [14:12] path class 0, [15] header flag 0 (data),
      [19:16] tid 0, [21:20] vap type 2 (AP data path)

    host order per post (identical to the hardware-fired dp_tx_frame):
      1. write entry at (user 0, tid 0) ring slot (host-DRAM 8x64x8)
      2. dma_wmb()
      3. publish packed next producer index into device-RAM table entry +4
         (12-byte entry {ring_devva, wptr, rptr}; packed = idx[14:0]|phase<<15)
      4. wmb(); read back all three dwords; fail closed on mismatch
      5. TXQ-table queue-state publish (hmac_update_user_tid_txq_table order)
         into the 0x2524 B host-DRAM table synced by g0 id1 key 0x0320
    completion observable: device rptr in table entry +8 [V*] (dp_tuning
    "COMPLETED": rptr == published wptr, no retry)

This lane adds only a spinlock (tx_post_lock) shared with the sysfs
instrument so the per-TID producer index is never advanced by two writers.

## T4 - EAPOL / EAPOL-Key classification (TX side)

Offsets are inside the EAPOL frame (Ethernet type 0x888e at 12; EAPOL starts
at 14); identical to the vendor rules wifi-sta-ops re-derived [V]:

| field | offset | meaning |
| --- | ---: | --- |
| EAPOL type | 1 | 3 = EAPOL-Key |
| EAPOL length | 2..3 (BE) | must be >= 95 (vendor: > 0x5e) |
| key info | 5..6 (BE) | bit 3 (0x0008) = pairwise |
| key data length | 97..98 (BE) | vendor 4/4-like detector requires 0 |

Message classes used for evidence (AP TRANSMIT perspective, i.e. frames the
AP sends):

| class | rule | 4/4-like shape |
| --- | --- | --- |
| msg1 | pairwise + ACK, no MIC | **yes** (msg1 has key-data-length 0) |
| msg3 | pairwise + ACK + MIC | no (msg3 carries the GTK in the key data) |
| other | any other EAPOL-Key | depends |

hmac_is_eapol_key_ptk_4_4 (+0x18750) calls mac_is_eapol_key_ptk (+0x23094) and
requires key-data-length 0; hmac_tx_msdu_dscr_free (+0x19d90) runs it on
**TX completion** and sets a per-user flag (the vendor authorization hook)
[V].  The vendor detector is therefore ambiguous between the AP's msg1 and a
supplicant's msg4: msg3 (the handshake's decisive AP frame) does NOT match it.

**We do not auto-authorize on this detector.**  In the host-SME AP flow msg1
matches the shape, so using it as an authorization trigger would authorize a
peer before the 4-way ran - a security bug.  The lane records the detector
(data-tx eapol: ... 4_4_like=N and one dmesg line per completion) and lets
hostapd's .change_station own AUTHORIZED.  This is a deliberate deviation
from the vendor hook, justified by the AP-mode semantics.

Hostapd ordering (hostapd 2026.08.07, wpa_auth.c, host-only, not vendor
material): PTKINITNEGOTIATING installs the pairwise PTK with
KEY_FLAG_PAIRWISE_RX **before** sending msg3 and PTKINITDONE installs it
RX_TX after msg4.  Consequence: **msg3 leaves the driver unprotected** (no TX
key yet) and **msg4 is the STA's first protected frame**; the peer is
AUTHORIZED only after msg4.  The Protected-bit policy in T6 matches that.

## T5 - Peer-aware TX decision (fail-closed)

| input | decision |
| --- | --- |
| multicast/broadcast DA | allowed (no peer context needed); protected only if data_tx_protect says so |
| unicast, DA not in the sta-ops station table | refused (EPEER) when data_tx_peer=1; allowed unprotected only in the explicit diagnostic mode data_tx_peer=0 |
| unicast, known peer, not authorized, EAPOL | allowed (the 4-way needs it) |
| unicast, known peer, not authorized, non-EAPOL | refused (EUNAUTH) - no pre-handshake data leak |
| unicast, authorized peer | allowed, AUTO protection on |

Station table = struct hsop_sta_table in the wiphy private area, installed by
wifi-sta-ops (task-149) .add_station / .change_station (AUTHORIZED).  The xmit
path reads it locklessly (aligned u8/int reads; the table is written only from
process-context cfg80211 ops, and a peer can only receive frames after it was
added).  Known limitation, documented.

## T6 - Protected-bit policy (data_tx_protect)

| value | policy |
| ---: | --- |
| 0 (default) | AUTO: unicast to an authorized peer -> Protected; multicast -> Protected iff wpa_keys=1 (hostapd's WPA2 GTK path ran); everything else clear |
| 1 | force Protected (bench bound-test) |
| 2 | force clear (4-way msg1/msg3 form and the L4 ARP smoke rung) |

The MAC/firmware must encrypt protected host-originated data using the
per-user key installed by cfg80211 add_key (g0 id1 cmd 0x0078, pairwise form
decoded by wifi-wpa-static [V]).  Whether the firmware actually does that for
host-posted frames is **[U]** - no RE evidence covers TX-side key selection;
the ladder is the experiment (data_tx_protect lets it be isolated).

## T7 - Completion, timeout and pool policy

* completion source: device-consumed rptr of the per-TID MSDU table entry (the
  field the hardware-fired dp_tuning COMPLETED verdict read) [V*];
* poll: delayed work every 5 ms while frames are in flight; each completion
  frees the oldest in-flight skb and runs the EAPOL-Key TX-completion detector;
* ring advance uses the packed-index phase arithmetic (same packed =
  idx[14:0] | phase<<15 as the vendor) and is fail-closed: a
  backwards/inconsistent rptr is refused and counted, never trusted;
* bounded timeout (data_tx_timeout_ms, default 200): the lane closes for the
  module load (stalled=1, no retry, a reload/power-cycle is the reset); the net
  stack's skbs are returned but the DMA buffers are NOT reused after a stall
  (device parity is kept);
* pool full: frame dropped and counted (dropped_full); no queue-stop/wake path
  is used (the AP netdev is created with tx_queue_len=0);
* no mixing: the sysfs dp_tx_frame instrument must not be used while the data
  path is live (both share the ring; the spinlock makes the writes safe but
  completion attribution would be ambiguous - documented).

## T8 - Module parameters added by this lane (all in modinfo)

| name | default | meaning |
| --- | ---: | --- |
| data_tx | **0** | master gate: 0 = fail closed (old accounted drop), 1 = real data TX |
| data_tx_peer | 1 | require a station-table entry for unicast (peer-aware) |
| data_tx_protect | 0 | Protected-bit policy 0/1/2 (T6) |
| data_tx_timeout_ms | 200 | bounded completion timeout |

Resulting module: **54 params** = 47 all-patch + 3 sta-ops + 4 data-tx.

## T9 - Source trail (derived, not copied)

| fact | lane / artifact | committed reference |
| --- | --- | --- |
| 802.11 data header/addr/QoS length rules | wifi-sta-ops T8; hmac_rx_frame_80211_to_eth +0x125f8, hmac_rx_get_mac_header_len +0x13b94 | ../wifi-sta-ops/TABLES.md |
| EAPOL-Key offsets, 4/4-like detector | wifi-sta-ops T9; mac_is_eapol_key_ptk +0x23094, hmac_is_eapol_key_ptk_4_4 +0x18750, hmac_tx_msdu_dscr_free +0x19d90 | ../wifi-sta-ops/TABLES.md |
| station table + AUTHORIZED flag | wifi-sta-ops (task-149) | ../wifi-sta-ops/REPORT.md |
| MSDU ring entry ctl, publish order, TXQ table | wifi-dp-txrx / wifi-dp-tuning; hmac_tx_msdu_ring_push 0x18f18, hmac_tx_frame_control_dscr_set 0x18b9c, hmac_update_user_tid_txq_table 0x1b294 | ../wifi-dp-txrx/FRAME-SPECS.md, ../wifi-dp-tuning/REPORT.md |
| frame buffer layout | wifi-dp-txrx; hmac_tx_msdu_dscr_add 0x19b40, shuangta_get_msdu_info_ring_addr 0x38ad8 | ../wifi-dp-txrx/FRAME-SPECS.md |
| pairwise key install (0x0078 body) | wifi-wpa-static | ../wifi-wpa-static/TABLES.md |
| TX COMPLETED verdict / TXQ publish | wifi-dp-tuning (bench-fired) | ../wifi-dp-tuning/REPORT.md |
| hostapd PTK-install ordering (RX-only before msg3) | hostapd 2026.08.07 source (host tool, not a vendor artifact) | REPORT.md T4 |

No vendor binary, object, firmware or calibration byte is committed by this
lane; the local RE artifacts stay local/gitignored per project policy.
