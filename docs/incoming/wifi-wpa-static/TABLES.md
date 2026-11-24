# wifi-wpa-static - TABLES (derived constants, addresses, layouts)

**Lane:** wifi-wpa-static (shared task task-139), host-only static RE, 2026-11-28.
**Evidence markers:** [V] re-verified in this lane from a named local artifact;
[V*] device-measured by an earlier attended lane and cited; [I] inference;
[U] undecoded.  **No vendor byte/object/firmware content is reproduced** - only
derived addresses, offsets, sizes and constants.  Vendor artifacts stay local
and gitignored.

Local artifacts used (not committed):
`/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko`
(2.4.15, md5 `1745ef77e728c411cef1921af3dc8731`), the shipped 2.5.24 module
`/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko`
(md5 `4737fcb21a1a2262a96f84d780ad8b35`), the pinned firmware image
`/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`
(928,920 B, sha256 `7fc87e20...311b`; firmware CPU = file offset + 0x40000).

---

## T1. Vendor cfg80211 key ops (2.4.15 module; 2.5.24 = +0x208, identical code) [V]

| cfg80211_ops | internal function | 2.4.15 addr | 2.5.24 addr | size |
| --- | --- | ---: | ---: | ---: |
| field 6 `add_key` | `wal_cfg80211_add_key` | 0x1007f4 | 0x1009fc | 848 B |
| field 7 `get_key` | `wal_cfg80211_get_key` | 0x100b44 | 0x100d4c | 596 B |
| field 8 `del_key` | `wal_cfg80211_remove_key` | 0x100d98 | 0x100fa0 | 612 B |
| field 9 `set_default_key` | `wal_cfg80211_set_default_key` | 0x100ffc | 0x101204 | 412 B |
| field 10 `set_default_mgmt_key` | `wal_cfg80211_set_default_mgmt_key` | 0x101198 | 0x1013a0 | 400 B |

The 2.5.24 build is byte-for-byte the same instruction stream for
`wal_cfg80211_add_key` (same 848 B, offset +0x208 only), so the RE is not
version-specific.  `wal_cfg80211_add_key` argument mapping is the 5.10
`cfg80211_ops` ABI: r0=wiphy, r1=netdev, r2=key_index, r3=pairwise,
stack+0=mac_addr, stack+4=struct key_params*.

Vendor `struct key_params` layout in this kernel tree (5.10.201 with the
OpenWrt cfg80211 backport, the same as 6.18.60):

| off | field | add_key read |
| ---: | --- | --- |
| +0x00 | `const u8 *key` | copied into body+0x08 (max 32 B) |
| +0x04 | `const u8 *seq` | copied into body+0x28 (max 16 B) |
| +0x08 | `int key_len` | validated <= 0x20, stored body+0x38 |
| +0x0c | `int seq_len` | validated <= 0x10, stored body+0x3c |
| +0x10 | `u16 vlan_id` | not used by the key op |
| +0x14 | `u32 cipher` | stored body+0x40 verbatim |
| +0x18 | `enum nl80211_key_mode mode` | not used by the key op |

Call sequence inside `wal_cfg80211_add_key` [V]:

1. NULL checks on wiphy/netdev/params; `key_len > 0x20` or `seq_len > 0x10`
   -> -EINVAL (0xffffffea).
2. Build a 0x44-byte key body (layout T3) on the stack; memcpy key and seq;
   never reject a NULL `key` when `key_len==0` at this layer (cfg80211
   validates first).
3. `memcpy` the body behind a `{u16 cmd; u16 len}` header:
   cmd `0x0078`, len `0x44`, total 0x48.
4. `wal_send_cfg_event(netdev, group=1, len=0x48, data, sync=1, &resp)`:
   the WAL cfg-event service (same envelope the vendor uses for the AP-start
   family), then `wal_check_and_release_msg_resp()` waits for the firmware
   config-sync response.
5. Errors return -EINVAL/-1 and log through `oam_error_log*`; success returns 0.

`wal_cfg80211_remove_key` [V] builds a **0x08-byte** body and calls
`wal_send_cfg_event(netdev, group=1, len=0x0c, data={0x007a,0x08,body})`:
body+0 = key_index (r2), body+1 = pairwise (r3), body+2..+7 = mac_addr
(stack arg).  `wal_cfg80211_set_default_key` [V] builds a **0x28-byte** body:
body+0 = key_index, body+1 = unicast (r3), body+2 = multicast (stack arg), rest
zero; cmd `0x0079`, len 0x28.  `wal_cfg80211_set_default_mgmt_key` [V] uses the
same cmd `0x0079`/len 0x28 with body+0 = key_index and body+2 = 1 (mgmt class
flag).  `wal_cfg80211_get_key` [V] sends its own cfg event and parses the reply
into cfg80211 key_params; its reply payload is not needed for WPA2-PSK and is
[U] here.

## T2. Firmware dispatch: table A rows and workers [V]

The firmware group-0 id-1 handler (CA 0x43D58) reads `key = u16 payload+0x08`,
`len = u16 payload+0x0a`, `data = payload+0x0c` and dispatches through the
static table A (CA 0x10C23C, 8-byte entries `{u16 key; u16 pad; u32 worker}`):

| table A entry | key | worker CA (Thumb) | role |
| ---: | ---: | ---: | --- |
| 78 | **0x0078** | **0x0A5825** | key install (add_key) |
| 79 | **0x007A** | **0x0A4271** | key remove (del_key) |
| 80 | **0x0079** | **0x0A5C9D** | default key (set_default_key / mgmt) |

These are the same values the wifi-datapath lane generated into
`wifi-datapath/tables/firmware-config-keys.md` (entries 78-80) and the vendor
host senders confirm them: `hmac_install_rekey` / `hmac_user_add_key`
(2.4.15 0x1638) -> `hdpp_config_send_event` (0x59c0) with cmd 0x78/len 0x44,
and `hmac_enc_11i_default_key_msg` (0xa3c9c) -> `hmac_config_send_event` with
cmd 0x79 [V].

## T3. Add-key body layout, 0x44 B (firmware worker CA 0x0A5825) [V]

| off | type | field | worker decode |
| ---: | --- | --- | --- |
| +0x00 | u8 | key_index | `ldrb [body]`; `>5` -> error 0x520a |
| +0x01 | u8 | pairwise | `ldrb [body+1]`; 1 = STA lookup, 0 = VAP user |
| +0x02 | u8[6] | mac_addr | pairwise: user lookup by MAC (`0x1028f4`/`0xbd430`); group: ignored |
| +0x08 | u8[32] | key | key cache payload (worker bound 0x20) |
| +0x28 | u8[16] | seq | sequence/IV data (worker bound 0x10) |
| +0x38 | u32 | key_len | `ldr [body+0x38]`; `>0x20` -> error 0x5209 |
| +0x3c | u32 | seq_len | `ldr [body+0x3c]`; `>0x10` -> error 0x5209 |
| +0x40 | u32 | cipher | `ldrb [body+0x40]` (low byte only), classified per T5 |

Cross-check [V]: `hmac_user_key_to_key_params` (0x274c) converts the vendor's
in-driver per-user key slot into exactly this 0x44-byte layout:
`params+0x38 = slot+0x04` (key_len), `params+0x3c = slot+0x08` (seq_len),
`params+0x40 = slot+0x00` (cipher), key copy to `params+0x08` (32 B), seq copy
to `params+0x28` (16 B), key_index from the user flag field, pairwise=1, MAC
from `user+0x18`.  The per-user key slot stride is 0x3c at `user+0x58`.

After validation the worker finds the owner: pairwise keys by MAC lookup,
group keys through the VAP user id at `module_ctx+0xacc` (`0xd6a1e`), stores
cipher/key index in `user+0x310`/`user+0x311` (data cipher) or
`user+0x312`/`user+0x313` (mgmt/BIP cipher), and installs the key cache entry
through the key-table manager at `module_ctx+0x4b8` (`0xa43e8`).  Every key
cache write happens inside the firmware; the host writes no MAC registers for
keys [V].

## T4. Remove / default-key bodies [V]

Remove body, 8 B (worker CA 0x0A4271):

| off | field | worker decode |
| ---: | --- | --- |
| +0x00 | u8 key_index | `r7 = [body]` |
| +0x01 | u8 pairwise | `r8 = [body+1]`; 1 = lookup user by MAC |
| +0x02 | u8[6] mac_addr | passed to the user lookup / removal helper |

Default-key body, 0x28 B (worker CA 0x0A5C9D):

| off | field | worker decode |
| ---: | --- | --- |
| +0x00 | u8 key_index | `[body]`; `>3` routes to the mgmt/default-mgmt check |
| +0x01 | u8 unicast | `[body+1]`; unicast and multicast must not both be 0 |
| +0x02 | u8 multicast | `[body+2]` |
| +0x04 | u8 (mgmt cipher) | only read on the key_index>3 path; not sent by our data-key op |

The data path of the default-key worker is a no-op returning success for
non-WEP keys (`0xfff6c` decides the WEP legacy path); the WPA2 CCMP/GCMP
group-key selection is therefore a safe, consumed event.  The mgmt path
(key_index 4/5) additionally wants a cipher byte at +0x04, which the vendor
builder does not fill either [I] - one more reason this lane leaves
`set_default_mgmt_key` unregistered.

## T5. Firmware cipher classes - cfg80211 suite low byte [V]

The worker does `cipher = uxtb(body+0x40)`, rejects `>0x0d`, then tests a
single-bit mask:

| class | mask | accepted low bytes | nl80211 cipher suites (low byte) | key index rule |
| --- | ---: | --- | --- | --- |
| data | 0x0714 | 2,4,8,9,10 | TKIP(2), CCMP(4), GCMP(8), GCMP-256(9), CCMP-256(10) | index <= 3 |
| mgmt | 0x3840 | 6,11,12,13 | AES-CMAC/BIP(6), BIP-GMAC-128(11), BIP-GMAC-256(12), BIP-CMAC-256(13) | index 4..5 |
| WEP | 0x0022 | 1,5 | WEP40(1), WEP104(5) | index 0..5 |

Unknown low bytes (0, 3, 7, >=14) are refused by both the firmware and this
lane's portable validator (-EOPNOTSUPP, never a guessed enum).  The clean-room
builder stores the full cfg80211 suite value little-endian in the wire field;
the firmware only reads the low byte.

## T6. Carrier: the g0 id-1 config event [V]/[V*]

`hdpp_config_send_event` (2.4.15 0x59c0) is the vendor host builder for the
bench-proven group-0 id-1 family.  `hmac_user_add_key` calls it with
`(vap, cmd=0x0078, len=0x44, key_body)`; the builder allocates
`len + 0x10` and fills: header byte0 = group 0, byte1 = alloc nibble | dst
nibble from `vap+4`, byte2 = `vap+6`, byte3 = `vap+1` (module/VAP index),
bytes4-5 = total, bytes6-7 = id 1, byte8 = 0; payload+0x08 = cmd, payload+0x0a
= len, payload+0x0c = body.  The clean-room builder in
`src/hi5622_key_frames.c` emits exactly that family (12-byte HCC header,
token[8], key, len, body, 4 pad bytes) with tag 0x5a5a; the same family is
already bench-executed by the AP-start and TXQ-sync frames [V*].

## T7. hostapd WPA2-AP nl80211 sequence (hostapd v2.12 source in the project
image build) [V]

`hostapd_setup_interface()` order: `hostapd_start_beacon()` (NL80211_CMD_
NEW_BEACON -> driver `.start_ap`, AP-ENABLED), then
`wpa_init_keys()` -> `wpa_group_sm_step()` -> WPA_GROUP_SETKEYSDONE ->
`wpa_group_config_group_keys()`:

1. `wpa_auth_set_key(broadcast, GN=1, GTK, KEY_FLAG_GROUP_TX_DEFAULT)`
   -> `nl80211_new_key`: NL80211_CMD_NEW_KEY, group key, key_idx=1,
   cipher=WLAN_CIPHER_SUITE_CCMP (0x000fac04), 16-B key, no MAC attribute.
   cfg80211 -> `rdev_add_key(... pairwise=false, mac_addr=NULL)`.
2. because the group key is TX/default, `nl80211_set_key` also sends
   NL80211_CMD_SET_KEY with NL80211_KEY_DEFAULT + DEFAULT_TYPE_MULTICAST
   -> `rdev_set_default_key(key_idx=1, unicast=false, multicast=true)`.

Per associated STA (after the 4-way handshake, M4): NL80211_CMD_NEW_KEY,
pairwise, key_idx=0, peer MAC -> `rdev_add_key(... pairwise=true, mac=sta)`;
on deauth: NL80211_CMD_DEL_KEY -> `rdev_del_key(...)`.

cfg80211 pairing rule enforced at wiphy registration
(`net/wireless/core.c` 6.18.60 line 530):
`WARN_ON(ops->add_key && (!ops->del_key || !ops->set_default_key))` - this
lane therefore registers all three together.

## T8. What is still absent (honest gap table)

| piece | state in this lane | evidence |
| --- | --- | --- |
| AP-mode GTK install + default selection (hostapd startup) | decoded + implemented, fail-closed behind wpa_keys=0 | T1/T3/T7, ladder R2 |
| STA pairwise PTK install (needs associated STA user in firmware) | decoded + implemented; firmware user lookup will refuse an unknown MAC until `.add_station`/mgmt RX exist | T3, ladder R3 |
| del_key for a disconnecting STA | decoded + implemented | T4 |
| get_key | vendor sends its own cfg event; reply layout [U], not required by hostapd WPA2-PSK | T1 |
| set_default_mgmt_key | vendor sends cmd 0x79 with a class flag; worker's mgmt path wants a cipher byte the vendor leaves 0 | T4 [I], not registered |
| EAPOL/assoc RX -> full 4-way handshake | blocked on the RX capture path (0 captures) and mgmt RX; out of scope | project STATUS |
| STA `.connect`/`.disconnect` | still clean -EOPNOTSUPP stubs: vendor `wal_cfg80211_connect` (2.4.15 0x100388 / 2.5.24 0x100590) builds a WAL join request whose payload (`g0 id0xF` family) is [U], and completion needs g2 id3/5 + beacon RX | this lane, REPORT 8 |
