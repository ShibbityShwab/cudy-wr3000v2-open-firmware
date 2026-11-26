# TABLES - the vendor WEXT `set_vendor_ie` call sequence (app/vendor IE slot storage)

Lane appie-wext2 (task-239). Grades: **[V]** verified in the local vendor driver
disassembly (`hi5622v100_wifi.ko`, gitignored) or a committed lane; **[I]**
inference from the decoded code/usage; **[U]** unknown/unproven.

Source of the addresses: the local vendor driver disassembly
`wifi.dis.txt`/`wifi.nm.txt` (symbol table; image base CA). All CAs are function
addresses in that module. Wire bytes below are protocol layouts, not vendor
bytes.

## W1 - the WEXT private-ioctl entry [V]

Dispatch: `wal_witp_wifi_priv_cmd_proc_set` (**CA 0x118d94**) switches on
`u32 cmd = *(u32 *)arg` with `idx = cmd - 0x8ee1; idx <= 11`:

| idx | cmd | handler |
| ---: | ---: | --- |
| 0 | 0x8ee1 | max-user/param |
| 1 | 0x8ee2 | `wal_ioctl_set_country_code` |
| 2 | 0x8ee3 | `wal_ioctl_set_ssid` |
| 3 | 0x8ee4 | max-user |
| 4 | 0x8ee5 | `wal_netdev_set_mac_addr` |
| 5 | 0x8ee6 | netdev open-ext |
| 8 | 0x8ee9 | netdev change-mtu |
| 9 | 0x8eea | netdev open-ext |
| **11** | **0x8eec** | **`wal_ioctl_set_vendor_ie` CA 0x11820c - the `nlwifi_set_app_ie` path** |

Userspace request layout read by the handler (arg = r4): **[V]**
`{ u32 cmd; u32 ie_len; u8 type; u8 pad[3]; u8 ie[ie_len <= 496]; }`
(reads `+4` = ie_len u32, `+8` = type u8, copies `+12` for ie_len bytes).
The whole input is copied into a zeroed 592-byte kernel struct, so
`ie_len > 496` cannot reach the cfg event.

## W2 - the cfg-event request the WEXT ioctl builds [V]

`wal_ioctl_set_vendor_ie` sends **one cfg-event RPC** (never the bare H2D
frame). The message body is a 508-byte struct on the stack (then carried by
`wal_send_cfg_event`):

| off | size | field | value/meaning |
| ---: | ---: | --- | --- |
| 0 | u16 | event | **0x0276** = SET_VENDOR_IE |
| 2 | u16 | payload len | **0x01f8** = 504 |
| 4 | u32 | ie_len | from userspace |
| 8 | u8 | type | from userspace (firmware IE class byte) |
| 9 | u8 | mode | **0** (path A; the WEXT path never sends mode != 0) |
| 10 | u8[2] | pad | 0 |
| 12 | u8[496] | ie | userspace IE bytes (zero padded) |

The hmac-side request struct has the same shape minus the 4-byte event header:
`{u32 ie_len; u8 type; u8 mode; u8 pad[2]; u8 ie[]}` (the direct g0 id 0x20
builder's caller struct, sibling TABLES T2/T2b).

Two other vendor callers send the same event: `wal_hipriv_set_beacon_ext_ie`
(CA ~0x1127xx, beacon extended IE) and `wal_net_device_ioctl` (CA ~0x11a220,
an alternate ioctl entry); `wal_cfg80211_set_okc_ie` (CA 0x127fd4) uses event
0x276 with a 504-byte payload too. It is therefore a generic app-IE RPC, not a
one-off.

## W3 - the WAL cfg-event envelope: HCC frame geometry [V geometry, [I] two fields]

`wal_send_cfg_event(dev, arg1=1, msg_len=0x1fc, msg, sync=1, &resp)`
CA 0x126c98, allocates through `wal_alloc_cfg_event` CA 0x126ab0:

| step | decode |
| --- | --- |
| alloc | `hcc_msg_alloc(msg_len + 16)` = 508+16 = **524** byte allocation; wire total = 524+12 = **536 B** [V formula, [I] the +16] |
| context | `ctx = *(u32 *)(dev + 0x328)` = the netdev's hmac/WAL context; `hdr[3] = ctx[0]`, `hdr[2] = ctx[1]`, `hdr[1] bits7:4 = ctx[2] & 0xf` |
| group/id | `hdr[0]` **low nibble = 2** (group 2); `hdr[6..7] = 0` (message id 0) |
| tag | `hdr[10..11] = 0x5a5a` (HCC tag, preserved by the alloc) |
| payload | pointer returned = `hdr + 16` |
| private word | `hdr+12` (4 B) holds the caller's request/wait object (host private; a wire cookie in transport terms) [I] |

The resulting frame (536 B, fixed size - unlike the variable g0 0x20 frame):

| off | size | field | value |
| ---: | ---: | --- | --- |
| 0 | u8 | b0 | 0x02: group 2 in the **low nibble** (the high nibble is preserved) |
| 1 | u8 | b1 | bits7:4 = ctx[2] & 0xf; low nibble 1 (allocator destination nibble, same as the g0 0x20 stock frame) |
| 2 | u8 | b2 | ctx[1] |
| 3 | u8 | b3 | ctx[0] = the VAP/netdev context index |
| 4 | u16 | total | **0x0218** = 536 |
| 6 | u16 | id | 0 |
| 8 | u8[2] | - | 0 |
| 10 | u16 | tag | 0x5a5a |
| 12 | u32 | cookie/private | 0 for a driver post [I] |
| 16 | u32 | rpc tag | 1 (`arg1` of `wal_send_cfg_event`) |
| 20 | u16 | reserved | 0 [I] (vendor leaves it uninitialised) |
| 22 | u16 | msg len | **0x01fc** = 508 |
| 24 | u32 | seq | `g_wal_config_seq_num` atomic counter |
| 28 | u16 | event | **0x0276** |
| 30 | u16 | payload len | **0x01f8** = 504 |
| 32 | u32 | ie_len | |
| 36 | u8 | type | |
| 37 | u8 | mode | 0 |
| 38 | u8[2] | pad | 0 |
| 40 | u8[496] | ie | zero padded |

**Group correction vs the older lane docs:** the HCC group is the **low nibble of
hdr[0]** (the app-IE builder clears bits 0..3; the channel-switch builder
CA 0x46fb8 likewise; the cfg-event builder inserts 2 there), and the message id is
`u16` at `hdr[6..7]`. Sibling TABLES T1 said "group = hdr[0] high nibble"; that
is wrong (harmless there because the stock value is 0). The stock capture's
unexplained "g2 id0" family is consistent with this class, but the census's 12
records are torn config-key-shaped reads, so no cfg-event-shaped stock send was
captured ([U], see REPORT).

## W4 - the call sequence after the cfg event (the hmac chain) [V]

| # | CA | function | behaviour |
| ---: | --- | --- | --- |
| 1 | 0x12b960 | `wal_config_set_vendor_ie` | pure thunk `b hmac_config_set_vendor_ie` (no .text caller -> registered in a dispatch table [I]) |
| 2 | 0x60a6c | `hmac_config_set_vendor_ie(vap, -, req)` | `type = req[4]`; `hmac_config_set_app_ie_to_vap(vap, req, type)`; if `type == 16` also mirror `req->ie` to `vap+0x38c` and set `vap+0x390 = ie_len - 6` |
| 3 | 0x60298 | `hmac_config_set_app_ie_to_vap(vap, req, type)` | (a) strip a `0x7f` IE from `req->ie` via `mac_find_ie`; (b) `type == 7` special path updates the chip adaptive-11r byte; (c) `mac_vap_save_app_ie(vap, req, type)`; (d) `hmac_config_h2d_send_app_ie(vap, req)` |
| 4 | 0x2ac18 | `mac_vap_save_app_ie` | **host mirror** of the slot table: `slot = vap + 20*type + 0x24c`; type <= 19; `ie_len == 0` -> free/clear ptr/len/cap and zero the 6-byte key; else (re)alloc if cap < ie_len, `memcpy(ie_len)` from req+8, `slot+0x250 = ie_len`, zero the 6-byte key |
| 5 | 0x600cc | `hmac_config_h2d_send_app_ie` | the direct **g0 id 0x20** H2D frame already shipped by sister lane wifi-app-ie (allocation ie_len+11, total ie_len+23, trio from the same vap bytes) |

So the vendor's host-side sequence is: **save the host mirror, then post the
direct frame** - and both are reached through the cfg-event RPC, on a **running
AP netdev context** (dev+0x328), not from the AP-start builder ladder.

## W5 - the slot table both sides use [V]

`slot = vap + 0x24c + 20*type` (type 0..19):
`{u32 ie_ptr; u32 ie_len; u32 ie_cap; u8 key[6]}` at +0x24c/+0x250/+0x254/+0x258.
The firmware side is the receive handler CA 0x85631 (sibling TABLES T4); the host
mirror is CA 0x2ac18 (W4/#4); both implement identical semantics, so the fire's
read-back (firmware RAM) is the correct witness.

## W6 - what our AP-start posting did vs the vendor sequence [V vs [I]]

| dimension | vendor WEXT path | app-ie-image3 fire | grade |
| --- | --- | --- | --- |
| wire object | 1x **536 B group-2/id-0 cfg-event RPC (event 0x0276)** + a direct g0 id 0x20 frame on the consumer side | 1x direct g0 id 0x20 frame (variable size) | [V] |
| timing | runtime, on a **running** AP netdev (WEXT ioctl from softapd) | inside `start_ap()`, after `ap_vap_start`, **before the AP is enabled** | [V]-ish vs [V] |
| context | trio from `dev+0x328` (the netdev's hmac/WAL vap) | trio from `vap_start_module` (0 in the fires) | [V] vs [V] |
| host mirror | `mac_vap_save_app_ie` always runs first | not implemented | [V] vs [V] |
| 0x7f strip | yes | no (0xDD collector only) | [V] |
| size cap | 496 B (WEXT userspace struct) | 608 B (firmware bound) | [V] |
| class | userspace-chosen | module parameter | [V] |
| response | waits <= 2000 jiffies for the cfg-event response, returns its status | fire-and-forget; "consumed" is only the HCC ring consumption | [V] |

**The class sweep varied only the class:** none of the deltas above is covered by
it, and any one of them can explain a green HCC consumption with an empty slot
(the handler's VAP lookup/state can fail silently; "consumed" proves the ring
consumer, not the handler return).

## W7 - open questions and what settles them [U]

| question | candidate | settling observation |
| --- | --- | --- |
| where the group-2/id-0 cfg event is consumed | host WAL "core" dispatch table vs firmware queue service | the module registers hcc chip tables and one core table (CA 0x12e0: core table group 0, 7 entries); the firmware's group-2 receive table (19 ids) has **no id 0** -> a bare firmware post would be dropped (firmware receive tables are files 0xccc18/0xcccfc) |
| whether the WEXT path's reply is required before the slot store | yes (the ioctl returns the handler status) | fire W3 watches for a D2H cfg-event response |
| the netdev->vap->[0] value the WEXT path would use | unknown; the AP-start module index 0 in the fires | fire W2 sweeps hdr[3] |
| mode != 0 (path B) | not reachable from WEXT; classes 12/17/18 only | never fired from this lane |

## W8 - the fire ladder derived from W6/W7

| rung | change | what it tests | cost |
| --- | --- | --- | --- |
| **W1** | post the same direct g0 id 0x20 frame **after the AP is enabled** (delayed work, ~1 s after `start_ap()` returns), i.e. the WEXT running-AP timing | timing/context delta (H1) | one module load |
| **W2** | direct g0 id 0x20 frame with `hdr[3] = 0..7` sweep (existing module, no rebuild; loop reads the slot each time) | VAP index delta (H2) | one module load, 8 posts |
| **W3** | the vendor-exact **536 B group-2/id-0 cfg-event 0x276 envelope** (this lane's patch, mode 1) | the WEXT RPC mechanism itself (H3) | one module load |
| **W0** | `app_ie=0` control and a class-4 direct frame control | no-regression + negative | shares a load |

Fire recipe: `FIRE-RECIPE.md`. All rungs are read-only on air (the laptop scan is
the observer); one load per rung, no retry, recovery armed.
