# wifi-wpa3-sae - TABLES (derived constants, layouts, hostapd/kernel facts)

**Lane:** wifi-wpa3-sae (shared task task-144), **host-only**, 2026-11-24.
**Evidence markers:** [V] re-verified in this lane from a named local artifact;
[V*] device-measured by an earlier attended lane and cited; [I] inference;
[U] undecoded.  **No vendor byte/object/firmware content is reproduced** - only
derived addresses, offsets, masks, field layouts and hostapd/kernel source
facts.  Vendor artifacts stay local and gitignored.

Local artifacts used (not committed):
* pinned firmware image `FIRMWARE.bin` (928,920 B, sha256
  `7fc87e20...311b`; firmware CPU = file offset + 0x40000) for the key-worker
  decode, disassembled locally only;
* hostapd v2.12 source tarball from the OpenWrt bench tree
  `dl/hostapd-2026.08.07~831364bf.tar.zst` (sha256
  `d1b9ca0e...d50393`) built for the host with CONFIG_SAE+CONFIG_IEEE80211W
  (see evidence/config-gate.txt) and read for the AP-side call chains;
* Linux 6.18.60 tree (the project compile target) for the cfg80211 contracts.

---

## T1. cfg80211 / kernel contracts this lane relies on [V]

| contract | source | what it means for this lane |
| --- | --- | --- |
| `set_default_mgmt_key` op exists in 6.18 | `include/net/cfg80211.h` (`int (*set_default_mgmt_key)(wiphy,netdev,link_id,key_index)`) | the driver can register it; the vendor key worker has a matching mgmt branch (T5) |
| SET_KEY default-mgmt without the op is refused | `net/wireless/nl80211.c` nl80211_set_key(), `key.defmgmt` branch: `if (!rdev->ops->set_default_mgmt_key) return -EOPNOTSUPP;` | **root cause of the PMF AP-start failure**: hostapd's IGTK default selection hits this path |
| BIP cipher keys must be group keys with idx >= 4 | `net/wireless/util.c` cfg80211_validate_key_settings() BIP case | the IGTK add (idx 4) is accepted; a mis-shaped key is refused before the driver op |
| mgmt-frame TX is gated by the wiphy TX bitmap | `net/wireless/mlme.c` cfg80211_mlme_mgmt_tx(): `wiphy->mgmt_stypes[iftype].tx & BIT(subtype)` | the AP TX mask must carry the *response* subtypes, not a copy of the rx mask |
| mgmt-frame registration is gated by the wiphy RX bitmap | `net/wireless/mlme.c` cfg80211_mlme_register_mgmt() | already satisfied by the wifi-ap-start lane's rx mask 0x3c15 |
| AKM suites are dumped to userspace | `net/wireless/nl80211.c` nl80211_send_wiphy(): NL80211_ATTR_AKM_SUITES (wiphy) and NL80211_ATTR_IFTYPE_AKM_SUITES (per iftype) | what this lane's advertisement feeds |
| no cfg80211 validation of the beacon RSN AKM suites | grep of `net/wireless/` (akm_suites used only in nl80211 dump + wext connect) | setting the advertisement cannot reject the AP start; it is a capability-contract fix |
| cfg80211 pairing WARN for key ops | `net/wireless/core.c` wiphy registration: add_key requires del_key+set_default_key | unchanged by this lane (the three were already registered) |

## T2. hostapd v2.12 AP-side facts this lane relies on [V]

All read from the same tarball the bench image is built from; not a claim about
the vendor's 2.5.24 userspace.

| fact | source location (v2.12) |
| --- | --- |
| AP setup = start beacon first, then initial keys | `src/ap/hostapd.c` hostapd_setup_interface(): `hostapd_start_beacon()` then `wpa_init_keys()`; failure tears the interface down |
| initial keys = GTK **then IGTK** | `src/ap/wpa_auth.c` wpa_group_config_group_keys(): GTK at `GN` with KEY_FLAG_GROUP_TX_DEFAULT; then if `wpa_auth_pmf_enabled()` IGTK at `GN_igtk` (=4) with `conf->group_mgmt_cipher` |
| IGTK is 16 bytes for every BIP cipher | `src/common/wpa_common.h` WPA_IGTK_LEN 16, `wpa_cipher_key_len()` |
| BIP key TX/default selection becomes NL80211_KEY_DEFAULT_MGMT | `src/drivers/driver_nl80211.c` wpa_driver_nl80211_set_key(): `wpa_alg_bip(alg) ? NL80211_KEY_DEFAULT_MGMT : ...` |
| wpa_init_keys failure is fatal for the interface | `src/ap/wpa_auth.c` wpa_init_keys() -> WPA_GROUP_FATAL_FAILURE; `src/ap/hostapd.c` returns -1 |
| hostapd AP does NOT gate on driver AKM capability | grep `capa.key_mgmt` / `WPA_DRIVER_CAPA_KEY_MGMT` under `src/ap/` and `hostapd/`: no hits |
| wpa_supplicant STA DOES gate on driver AKM capability | `wpa_supplicant/wpa_supplicant.c` `wpa_s->drv_key_mgmt = capa.key_mgmt`; fallback list in `src/drivers/driver_nl80211_capa.c` wpa_driver_nl80211_capa() **excludes SAE** unless SME/FILS flags |
| hostapd's RSNXE is written into the beacon tail | `src/ap/beacon.c` ieee802_11_build_ap_params(): `hostapd_get_rsnxe()` before the WPA IE / WMM |
| the WPA/RSN IE is in the beacon **tail** | same function: `hostapd_get_wpa_ie(hapd, tailpos, ...)` |

## T3. wiphy capability patch (hi5622_cfg80211.c) [V + host-build]

| item | before | after | why |
| --- | --- | --- | --- |
| `wiphy->akm_suites` | NULL (no NL80211_ATTR_AKM_SUITES) | `{PSK}` | wiphy default for the STA/connect path |
| `wiphy->iftype_akm_suites` | NULL | `iftypes_mask=BIT(NL80211_IFTYPE_AP), {PSK, SAE}` | the usable-AP target advertises WPA3-Personal where it is actually configured |
| `wiphy->cipher_suites` | CCMP, GCMP, AES_CMAC, BIP_CMAC_256, ... | + BIP_GMAC_128, BIP_GMAC_256 | firmware mgmt-class mask 0x3840 includes 11/12 (T5); the WPA3 default BIP-CMAC-256 was already advertised |
| AP `mgmt_stypes.tx` | `0x3c15` (a copy of rx: assoc/probe **request** bits) | `0x3c2a` = (re)assoc resp, probe resp, disassoc, auth, deauth, action | cfg80211 refuses TX of a subtype bit not set; hostapd's AP SME replies with assoc/auth responses |
| AP `mgmt_stypes.rx` | `0x3c15` | unchanged `0x3c15` | hostapd subscription gate (already proven) |

AKM suite selectors: PSK `0x000fac02`, SAE `0x000fac08` (`include/linux/ieee80211.h`).
Cipher selectors: AES-CMAC `0x000fac06`, BIP-GMAC-128 `0x000fac0b`,
BIP-GMAC-256 `0x000fac0c`, BIP-CMAC-256 `0x000fac0d`.

## T4. PMF AP-start key sequence and the frames this patch can produce [V/T5]

hostapd order (AP startup, PMF profile): GTK add -> GTK default ->
IGTK add -> default management key.  Wire frames are the bench-proven
group-0 id-1 cfg-event family (12-byte HCC header + 8-byte token + u16 cmd +
u16 len + body + 4 pad bytes).

| # | hostapd action | cfg80211 op | wire cmd | body | expected log label | status |
| ---: | --- | --- | ---: | ---: | --- | --- |
| 1 | GTK add (idx 1, CCMP, 16 B) | `.add_key` | 0x0078 | 0x44 | `key_add` (class=data) | [V*] bench-proven (wifi-stability F16) |
| 2 | GTK default multicast | `.set_default_key` | 0x0079 | 0x28 | `key_default` | [V*] bench-proven |
| 3 | IGTK add (idx 4, BIP-CMAC-256, 16 B) | `.add_key` | 0x0078 | 0x44 | `key_add` (class=mgmt) | decoded [V]; not yet fired |
| 4 | IGTK default management | **`.set_default_mgmt_key`** | 0x0079 | 0x28 | `key_default_mgmt` | decoded [V]; not yet fired; **op added by this lane** |

Frame sizes: 28 + 0x44 = 96 B (add_key) and 28 + 0x28 = 68 B (default key /
default mgmt key).  The mgmt default-key body is
`{key_index=4, unicast=0, multicast=1, reserved=0, cipher=0x0d, ...rest 0}`
(T5 requires the cipher byte).

## T5. firmware key workers - new/default management-key decode [V]

Firmware table A keys: 0x0078 -> key-install worker CA 0x0A5825,
0x0079 -> default-key worker CA 0x0A5C9D (Thumb entries, key from the
group-0 id-1 body).  Derived facts only; full decode in
[evidence/fw-default-mgmt-decode.txt](evidence/fw-default-mgmt-decode.txt).

**Default-key worker 0x0A5C9D, 0x28-byte body:**

| offset | field | worker behaviour |
| ---: | --- | --- |
| +0x00 | u8 key_index | >5 -> error 0x520a |
| +0x01 | u8 unicast | `(body[1] \| body[2]) == 0` -> error 0x545 |
| +0x02 | u8 multicast | data path: class selection; mgmt path: class flag (1) |
| +0x03 | u8 | read only on the WEP/legacy branch |
| +0x04 | u8 mgmt cipher | **mgmt path only**: >0x0d -> error 0x520b; `(1<<cipher) & 0x3840 == 0` -> error 0x520b (cipher byte 0 is rejected) |
| - | - | key_index <= 3 -> data path; key_index 4..5 -> mgmt path: VAP user lookup (module_ctx+0xacc via CA 0xD6A1E), then `user+0x312 = key_index`, `user+0x313 = cipher`, success |

**Key-install worker 0x0A5825 (cross-check):** cipher low byte from body+0x40,
class masks data 0x0714 / mgmt 0x3840 / WEP 0x0022; mgmt class requires
key_index 4..5, data class requires <=3; mgmt success stores the same
`user+0x312/+0x313` pair.

Consequence: the IGTK *install* needs no new layout (the existing add-key
builder already accepts mgmt-class ciphers and idx 4..5); the *default
selection* needs the cipher the host is not given by cfg80211, so the driver
latches it from the preceding IGTK add (T6).

## T6. management-cipher latch design (hi5622_keys.c) [V + host-test]

| state | value |
| --- | --- |
| latch | two bytes, index 4 and 5, zero = "not seen" |
| set when | a successful group (pairwise=0) add_key with a mgmt-class cipher completes |
| cleared when | a successful del_key for that mgmt index |
| read by | `.set_default_mgmt_key`; empty latch -> -EINVAL, no frame (never posts a guessed cipher) |
| wire on success | cmd 0x0079, 0x28 body, +2=1 multicast class flag, +4=latched cipher |
| valid ciphers | AES-CMAC (6), BIP-GMAC-128 (11), BIP-GMAC-256 (12), BIP-CMAC-256 (13) |
| gated by | `wpa_keys=1`, provider KEY_DEFAULT cap, committed AP start (same gate as the WPA2 key path) |

## T7. hostapd fixture matrix (this lane's configs) [V host-gate]

| fixture | AKM | PMF | group mgmt cipher | pairwise | expected startup key sequence |
| --- | --- | --- | --- | --- | --- |
| `configs/hostapd-wpa3-sae.conf` | SAE only | required (ieee80211w=2) | BIP-CMAC-256 | CCMP | GTK pair + IGTK pair (T4) |
| `configs/hostapd-wpa2-wpa3.conf` | WPA-PSK + SAE | optional (ieee80211w=1), SAE stations must do PMF | BIP-CMAC-256 | CCMP | GTK pair + IGTK pair |
| `configs/hostapd-wpa2-hardened.conf` | WPA-PSK only | required (ieee80211w=2) | BIP-CMAC-256 | CCMP | GTK pair + IGTK pair |

`sae_pwe=2` (both hunting-and-pecking and H2E) in the SAE profiles is
deliberate: hostapd's RSNXE is part of the beacon tail (T2), but the
bench-proven AP-config message carries only one 64-byte RSN IE slot and one
vendor (WMM) IE slot (wifi-beacon-static T4/T5), so RSNXE is not delivered to
the firmware today.  Strict H2E-only (`sae_pwe=1`) needs RSNXE transport
first (gap G-RSNXE).

## T8. functional state (exact) [V/V*]

| capability | WPA3-SAE profile | transition profile | WPA2-hardened profile | evidence |
| --- | --- | --- | --- | --- |
| config parses + hostapd_config_check passes | YES | YES | YES | evidence/config-gate.txt (31 checks) |
| RSN IE carried to the firmware at AP start | YES if the beacon capability privacy bit is set (hostapd sets it for wpa=2) - same ap_cfg path as the proven WPA2 GTK run | same | same | wifi-beacon-static T4 (ap_cfg +0x09..0x48 RSN copy, gated by +0x08) |
| beacon observed on air | not yet (on-air proof is a separate running lane) | same | same | project STATUS/re-report v5 |
| GTK install at AP start | works if wpa_keys=1 (bench-proven for WPA2) | same | same | F16 |
| IGTK install + default mgmt key | decoded + host-implemented (this patch); **not yet fired** | same | same | T4/T5/T6 |
| AP-ENABLED for a PMF profile | expected after R2 on hardware; currently blocked before the patch by the missing op | same | same | T1/T2 |
| WPA2 4-way (PTK) | needs association + RX/EAPOL + per-STA firmware user | same | needs same | T9 |
| WPA3 SAE authentication | needs AP management TX/RX (SAE commit/confirm are auth frames) + association + PTK | same | n/a | T9 |
| client association | needs cfg80211 `.add_station`/`.del_station` + mgmt TX/RX | same | same | T9 |

## T9. what remains for a *usable* WPA3 AP (independently owned)

| gap | owner / lane | facts |
| --- | --- | --- |
| WAL management-frame TX message family (`wal_cfg80211_mgmt_tx`) for SAE commit/confirm + auth/assoc responses | wifi-ap-mgmt lane (task-143, in progress) | vendor does AP auth/assoc in host software: `hmac_ap_up_rx_mgmt` -> `hmac_mgmt_encap_auth_rsp`/`..._asoc_rsp_ap` -> `wal_cfg80211_mgmt_tx` (wifi-beacon-static T10 [V]) |
| management-frame RX delivery (cfg80211_rx_mgmt) | wifi-ap-mgmt lane + MAC RX | MAC RX arm is proven (live bank CA 0x40050000); on-air delivery still needs traffic |
| `.add_station`/`.del_station` for the associated STA (firmware user context) | not implemented today | hostapd calls NL80211_CMD_NEW_STATION on association; without the cfg80211 op it logs "Could not add STA to kernel driver" and sends no assoc response (hostapd/src/ap/ieee802_11.c) - so association is blocked even with mgmt TX/RX present |
| pairwise PTK/IGTK per STA | this lane's add_key covers the wire path; needs the firmware user (add_station) and M3/M4 | wpa-static T8 |
| strict SAE H2E (sae_pwe=1) | needs RSNXE in the beacon path | T7 |
| AP-side SAE external-auth flag | not needed: hostapd implements AP SAE in host software; `WPA_DRIVER_FLAGS_SAE` is a wpa_supplicant offload concept (C6 in wifi-re-report) | T2 |
