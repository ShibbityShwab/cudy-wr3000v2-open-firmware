# TABLES - wifi-sta-ops (task-149)

All addresses are link-time offsets in **hi5622v100_wifi.ko 2.5.24** (md5
4737fcb2..., the bench module; 2.4.15 carries the same functions at its own
offsets) and firmware CA = PD4 image file offset + 0x40000.  This file holds
only DERIVED facts; the vendor artifacts are local/gitignored and are never
committed.  Re-derive with tools/sta_ops_re.py.

Evidence markers: **[V]** decoded from a local artifact (vendor host module
or PD4 firmware); **[V*]** bench-observed by an earlier attended lane and
quoted from its report; **[I]** inference; **[U]** not pinned.

---

## T1 - vendor cfg80211 station ops [V]

| cfg80211_ops slot | vendor function | .text | size | behaviour |
| --- | --- | ---: | ---: | --- |
| .add_station | wal_cfg80211_add_station | 0x1048f0 | 0x8 | mov r0,#0; bx lr - stub success (AP SME lives in hmac) |
| .change_station | wal_cfg80211_change_station | 0x103ac8 | 0x8 | stub success |
| .del_station | wal_cfg80211_del_station | 0x102470 | 0x29c | posts the WAL cfg-event kick, opcode 0x00d0 |
| .get_station | wal_cfg80211_get_station | 0x10270c | - | real |
| .dump_station | wal_cfg80211_dump_station | 0x1063e0 | - | real |

The five callbacks are consecutive 4-byte entries in the module's
.data cfg80211_ops table (order add, del, change, get, dump) [V].
The vendor AP SME is the hmac layer, which is why the two mutating ops are
stubs: the firmware per-STA context is installed by the hmac assoc path, not
by cfg80211.  Our host-SME driver must install it from .add_station.

## T2 - AP client station creation call chain (vendor hmac AP SME) [V]

| step | function | .text | role |
| ---: | --- | ---: | --- |
| 1 | hmac_encap_auth_rsp_get_user_idx | 0x9fd7c | auth-rsp path: find/alloc the user |
| 2 | hmac_ap_assoc_check_chip_exist_user_proc | 0x4e598 | assoc-req path: ensure the user on this chip |
| 3 | mac_vap_find_user_by_macaddr | - | existing-user lookup |
| 4 | hmac_ap_find/delete_exist_user_auth_node | 0x4da00/0x4d8d0 | stale auth-node cleanup |
| 5 | hmac_ap_rx_auth_chip_find_user_proc | 0x9fb90 | allocate/find the 12-bit user id |
| 6 | **hmac_user_add** | 0x8eb34 | posts g0 id7 user_add (T3) |
| 7 | hmac_user_get_valid / hmac_user_init_key / hmac_tid_clear | 0x8c180/0x8f9b8/0xa01c4 | host user object + key/tid init |
| 8 | mac_user_set_asoc_state(user+0xa6c, **1**) | 0x29784 | writes the state byte (user+0xaa9) |
| 9 | **hmac_config_user_asoc_state_syn** | 0x63338 | posts g0 id1 cmd 0x07d2 (T4) |

hmac_config_add_sta_node (0xa5aa4) is the same sequence for the add-sta-node
path, plus hmac_mgmt_timeout_ap registration and, on failure, a
oal_kobject_uevent_env_sta_leave (dev 0x9f... group [U]).

Host user-object field map used by the two messages [V]:
+0xa7e MAC[6], +0xa84 u16 user id / AID, +0xaa9 (= +0xa6c+0x3d)
association state byte.

## T3 - g0 id7 user_add (180 B wire = 12 B HCC header + 168 B body) [V]

HCC header (literal, vendor hmac_user_add and the hardware-proven VAP
user_add carrier):

| off | value |
| ---: | --- |
| +0x00 | group nibble 0 (HSOP_HCC_GROUP_CHIP) |
| +0x01 | alloc nibble 1 (low) / band|chip nibble (high) |
| +0x02 | vendor field [U] (vap+0xfa9); 0 on the bench VAP |
| +0x03 | vap_id |
| +0x04 | u16 total length 180 |
| +0x06 | u16 HCC id **7** |
| +0x08 | 0 |
| +0x09 | destination nibble 0 |
| +0x0a | tag 0x5a5a |

Body (offset from wire+12):

| off | width | field |
| ---: | ---: | --- |
| +0x00 | u16 | user id / **AID** (vendor user+0xa84) |
| +0x02 | 6 | MAC address |
| +0x8e | u8 | user type **2** (normal; AP-mode VAP) |
| +0xa4 | s8 | RSSI, vendor default **+100** when no scanned BSS matches |

There is no association flag in this message - the association state is the
separate T4 event.  hcc_msg_alloc(0xa8) + the 12-byte header is why earlier
lanes call it "180 B"; the body is 0xa8 = 168 B [V].

## T4 - g0 id1 config event cmd 0x07d2 (32 B wire) [V]

Vendor sender hmac_config_user_asoc_state_syn -> hmac_config_send_event
0x57238 -> g0 id1 carrier (the same family the hardware-proven key lane
uses).  The firmware g0 id1 handler receives the message with its data
pointer at the token area: cmd = payload+8, len = payload+10, body =
payload+12 [V, key-lane TABLES T2 and the matching hmac_config_send_event
decode].

Wire layout (32 B):

| region | off | value |
| --- | ---: | --- |
| HCC header | +0x00..+0x0b | group 0, alloc 1, vap/module, total 32, id **1**, tag 0x5a5a |
| token | +0x0c..+0x13 | bytes 0; token[4] = 0x01 (proven carrier pattern) |
| cmd | +0x14 | u16 **0x07d2** |
| len | +0x16 | u16 **4** |
| body | +0x18 | u16 **AID**, u8 **state**, u8 pad 0 |
| pad | +0x1c..+0x1f | inherited 0xff pad |

State values seen in the vendor: **1** = associated
(hmac_ap_assoc_check_chip_exist_user_proc), **3** = FT reassociation
(hmac_ft_assoc).  Our driver posts state 1 from .add_station and does not
fabricate other states.

Firmware dispatch: static table A at **CA 0x10c23c** (8-byte rows
{u16 key; u16 pad; u32 worker}) contains row key **0x07d2 -> worker CA
0x83e25** [V].  Worker semantics (Thumb, CA 0x83e25):

* reads body[0..1] (u16 AID) and looks the firmware user up (call 0x94728);
* reads body[2] (state) and writes it to firmware user **+0x3d** - the same
  offset the vendor host mirror writes via mac_user_set_asoc_state [V];
* returns 0 on success, 0x64 (100) when the user does not exist [V].

=> the 0x07d2 event is a real firmware-consumed per-STA state sync, unlike
the WAL mgmt-tx cmd 0x01F2 which has no table-A row (wifi-ap-mgmt T7).

## T5 - g0 id9 user_del (24 B wire = 12 B header + 12 B body) [V]

Vendor hmac_del_user_notify_dmac 0x8d7f0 (called from hmac_user_del
0x8dac4):

| off | width | field |
| ---: | ---: | --- |
| body+0x00 | u16 | user id / AID |
| body+0x02 | 6 | MAC |
| body+0x08 | u8 | flag; **2** for an AP-mode VAP user (vendor: !=1 -> 2) |
| body+0x09 | 3 | 0 |

HCC header id **9**; body 12 B; total 24 B (hcc_msg_alloc(0xc)).  This is the
counterpart of T3; it is decoded, built and host-tested here but posted only
when the sta_user_del=1 experiment is enabled (double-deleting a firmware
user is the documented E2 crash class).

## T6 - vendor .del_station = WAL cfg-event kick, opcode 0x00d0 [V]

wal_cfg80211_del_station 0x102470 calls wal_cfg80211_start_disconnect
0x128334 with a 16-byte body built on the stack:

| body off | width | field |
| ---: | ---: | --- |
| +0x00 | 6 | peer MAC (params->mac) |
| +0x06 | u16 | params->reason_code |
| +0x08 | u8 | **0xc0 when params->subtype == 12 (deauthentication)**, else 0 |
| +0x0f | u8 | vendor constant 1 |

This refines wifi-sta-static TABLES T3 (which recorded +0x0f = 1 only).
The WAL envelope is the already-decoded 32-byte HCC header + WAL cfg-event
wrapper (opcode 0x00d0, body_len 16, total 48 B) built by
hi5622_sta_wal_frame().

## T7 - host station table + cfg80211 flags [I]

| cfg80211 item | lane handling |
| --- | --- |
| station_parameters.aid | required 1..0x0fff; carried in T3/T4/T5 |
| sta_flags_mask/set | mirrored in the host table; AUTHORIZED drives RX delivery |
| change_station AUTHORIZED | host-side flag only; no extra firmware message ([U] firmware semantics) |
| station_info.sta_flags | filled from the mirror (get/dump) |
| del_station subtype/reason | subtype 12 -> 0xc0 kick flag; reason passed through |
| del_station mac == NULL (flush) | host table cleared, no firmware message (budget-safe) |

## T8 - 802.11 data RX -> Ethernet conversion (vendor rules) [V]

From hmac_rx_ring_entry_proc 0x101e8 -> hmac_rx_ring_entry_proc_frame
0xf258 -> hmac_rx_frame_80211_to_eth 0x125f8 and
hmac_rx_get_mac_header_len 0x13b94:

| rule | value |
| --- | --- |
| MAC header length | 24 B base; 30 B when ToDS+FromDS (4-address); +2 QoS control; +4 HT control/order |
| LLC/SNAP detection | AA AA 03 00 00 00 then the big-endian ethertype |
| FromDS=0 source | addr2 |
| FromDS=0 destination | ToDS ? addr3 : addr1 |
| FromDS=1 source | addr3 (addr4 when 4-address) |
| FromDS=1 destination | addr1 |
| host delivery | vendor forwards to its own offload/bridging layer; this driver uses the cfg80211 full-MAC convention netif_rx() on the AP VAP netdev |

For an AP client uplink (ToDS=1, FromDS=0) this is: src = addr2 (client),
dst = addr3.

## T9 - EAPOL detection (vendor helpers) [V]

| helper | .text | rule |
| --- | ---: | --- |
| mac_is_eapol_key_ptk | 0x23094 | EAPOL type 3, length > 0x5e, key-info bit 3 (pairwise) |
| hmac_is_eapol_key_ptk_4_4 | 0x18750 | ethertype 0x888e, EAPOL type 3, length > 0x5e, pairwise, **key-data-length 0** (msg2/msg4-like) |
| hmac_tx_msdu_dscr_free | 0x19d90 | calls the 4/4-like helper on **TX completion** and sets a per-user flag (the vendor authorization hook) |
| hmac_rx_filter_frame_encrypt | 0xf6ac | RX pass/drop filter; requires a valid user and applies the protocol/encryption state checks ([I] details) |

The lane parser implements the same wire checks (pairwise bit 3 at EAPOL
offset 5, key-data-length at offset 97) and delivers EAPOL before the
station is authorized - the 4-way handshake needs exactly that window.

## T10 - lane module parameters [V/I]

| param | default | meaning |
| --- | ---: | --- |
| sta_ops | 0 | 0 = fail closed (all station ops -EOPNOTSUPP, no frame built); 1 = post the per-STA firmware context and serve the station ops |
| eapol_rx | 0 | 0 = no data RX delivery; 1 = dp_rx_drain delivers data frames (EAPOL always, other ethertypes only for authorized STAs) |
| sta_user_del | 0 | 0 = vendor del_station kick only; 1 = also post the g0 id9 user_del (E2 crash class experiment) |

## T11 - artifact identity (host-built, never fired)

| item | value |
| --- | --- |
| base all-patch stripped | 308080 B sha256 5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41 |
| lane stripped | 320908 B sha256 a0a5444dc35366f62ce25dd4173e7cb6547b15bd9ef7d41760051a1da7c8d66f |
| lane unstripped | 1209260 B sha256 661cc7ab1d98ca915d4faa503706304581e6ee9a76adf3ce6543bd6aeb546c10 |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 ; depends=cfg80211 |
| parameters | 50 = 47 all-patch + sta_ops + eapol_rx + sta_user_del |
| build warnings | 0; strip leaves .text byte-identical |
