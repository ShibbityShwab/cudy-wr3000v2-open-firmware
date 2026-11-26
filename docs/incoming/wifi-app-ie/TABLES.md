# TABLES - the H2D g0 id 0x20 app/vendor IE decode and the integration contract

Lane: wifi-app-ie (task-234).  Grades: **[V]** verified (byte-level
disassembly/capture evidence in this lane or the cited committed lane),
**[I]** inference from decoded code/usage, **[U]** unknown/unproven.

## T1 - the captured stock frame [V]

From sibling lane **wifi-stock-h2d-census** (task-232, commit e977557),
`evidence/NEW-FAMILIES.txt`, family `g0 id32`: n=2 (unique=1), len == total
(23 B), t=1.6 s, and the full decoded frame:

```
00 01 00 04  17 00  20 00  00 00  5a 5a | payload:
header 12 B                              | 00 00 00 00  04 00 00 00  00 00 00
```

| field | bytes | value | meaning [V] |
| --- | --- | --- | --- |
| group | hdr[0] high nibble | 0 | HCC H2D group 0 |
| b1 | hdr[1] | 0x01 | bits 7:4 = ctx[2]&0xf (0 here); low nibble 1 = the alloc/dst nibble the vendor allocator leaves |
| b2 | hdr[2] | 0x00 | ctx[1] (the stock boot's value) |
| b3 | hdr[3] | 0x04 | ctx[0] = the **VAP/module index** the firmware handler looks the VAP up by |
| total | hdr[4..5] u16 LE | 0x0017 = 23 | frame total = alloc + 12 |
| id | hdr[6..7] u16 LE | 0x0020 = 32 | `hmac_config_h2d_send_app_ie` |
| b8,b9 | hdr[8],hdr[9] | 0,0 | zeroed by the builder |
| tag | hdr[10..11] u16 LE | 0x5a5a | the HCC H2D tag |
| ie_len | payload+0 u32 LE | 0 | IE blob length (0 = clear the slot) |
| type | payload+4 u8 | 4 | firmware per-VAP IE class (T6) |
| mode | payload+5 u8 | 0 | 0 = path A (save/clear); != 0 = path B (T4) |
| pad | payload+6..7 | 0 | copied from the zeroed caller struct |
| tail | payload+8+ie_len .. +2 | 0,0,0 | 3 bytes the builder copies past the used struct (T3) |

Reproduced byte for byte by this lane's builder: `evidence/FRAMES.txt` [1]/[2]
and the host selftest check "stock: builder == captured stock frame".

## T2 - the builder: wifi.ko `hmac_config_h2d_send_app_ie` CA 0x600cc [V]

Decoded from the committed local disassembly of the vendor wifi.ko
(`hmac_config_h2d_send_app_ie`, symbol table entry CA 0x600cc; the same
address the census lane cites).  Signature:
`int hmac_config_h2d_send_app_ie(vap_ctx *ctx, app_ie_req *req)`.

| step | code | meaning |
| --- | --- | --- |
| 1 | null-check both args; `r6 = u16(req->ie_len + 11)`; `hcc_msg_alloc(r6,0)` | alloc = ie_len + 11 bytes (payload size), total = alloc + 12 |
| 2 | `ip = *(msg + 0x118)` (the HCC header pointer) | hdr = payload_base - 12 |
| 3 | `hdr[6] = 0x20`; `hdr[0] = (hdr[0] & 0xf0)` (group 0); `hdr[1] = (hdr[1]&0x0f) | ((ctx[2]&0xf)<<4)`; `hdr[2] = ctx[1]`; `hdr[3] = ctx[0]`; `hdr[7] = hdr[8] = 0` | 12-B HCC header, T1 map |
| 4 | `memcpy_s(hdr+12, r6, req, r6)` | copies **ie_len + 11** bytes from the caller struct: u32 ie_len, u8 type, u8 mode, 2 pad, ie[ie_len], **3 bytes past it** |
| 5 | `hcc_msg_tx(msg)`; on failure free + log and return 0x8b2d | one HCC post; success 0 |
| 6 | error paths log via oam_*_log and return 100 (alloc/header) / 0x8b2d (tx) | fail-closed |

The caller struct (from the caller, T2b): `{u32 ie_len; u8 type; u8 mode; u8
pad[2]; u8 ie[ie_len];}` = 8 + ie_len used bytes; the builder's copy count of
ie_len + 11 adds the 3-byte zero tail the stock capture shows (T1).

## T2b - the vendor caller chain [V]

| CA | symbol | behaviour |
| --- | --- | --- |
| 0x60298 | `hmac_config_set_app_ie_to_vap(vap, req, type)` | mac_find_ie(0x7f) may strip a 0x7f IE from req; `mac_vap_save_app_ie(vap, req, type)` mirrors the table in the host VAP; then `hmac_config_h2d_send_app_ie(vap, req)`. Types > 19 refused. |
| 0x2ac18 | `mac_vap_save_app_ie(vap, req, type)` | host mirror of the firmware slot table: slot = vap+20*type+0x24c; ie_len==0 frees/clears (ptr/len/cap and the 6-byte field at +0x258); else (re)alloc + memcpy ie bytes from req+8 + clear the 6-byte field + debug log. |
| 0x60a6c | `hmac_config_set_vendor_ie(vap, ?, req)` | calls 0x60298 with type = req[4]; for **type 16** also copies the blob into vap+0x38c with length ie_len-6 (a separate vendor-IE mirror). |
| 0x608a8 | `hmac_config_set_wps_ie` | WPS IE wrapper, type from the caller. |
| 0x60618 | `hmac_config_set_wps_p2p_ie` | WPS/P2P wrapper, type from ioctl[4] (<= 19); type 7 takes a mac-chip special path. |
| 0x604f0 | split internal wrapper | WEXT-ioctl shaped input (len at +0x68, data at +4, <=496 B) with the type **hardcoded 3**. |
| 0x97804 | `hmac_cfg80211_start_scan_sta` | posts the scan IE list with type **1**. |
| 0x1d5d0 | `mac_add_app_ie(vap, pos, base, max, out_len, slot)` | appends slot`[0x250]` bytes from slot`[0x24c]` to a frame; used by the host mgmt builders. |
| 0x9eef8 | `hmac_mgmt_encap_asoc_rsp_ap` | appends slots **4** and **9** to the association response. |
| 0xa0fa0 | `hmac_mgmt_encap_asoc_req_sta` | appends slots **3** and **9** to the association request. |
| userspace | vendor `softapd` | drives it through the Hi5622/WEXT ioctl `nlwifi_set_app_ie` (committed vendor-userspace report sec. 4), i.e. not nl80211. |

## T3 - payload geometry and bounds [V]

| quantity | value | evidence |
| --- | --- | --- |
| allocation | ie_len + 11 B | builder CA 0x600cc: `add r6,#11; uxth` |
| frame total | ie_len + 23 B | hcc layer adds the 12-B header; stock 23 B at ie_len 0 |
| payload length accepted | <= 616 B | firmware handler: `u16(hdr[4]) - 12` compared with 0x268 |
| ie_len accepted | <= 608 B | firmware handler: `u32(payload)` compared with 0x260 |
| copy count | ie_len + 11 | builder `memcpy_s(hdr+12, alloc, req, alloc)` |
| tail | 3 zero bytes | the caller struct's 3 bytes past `ie[]` (caller memsets 504 B); stock frame payload ends `00 00 00` |

## T4 - the firmware handler: CA 0x85631 (file 0x45631, group 0 / id 0x20) [V]

Reached via the firmware group-0 HCC receive table (40 entries, table CA
0x10CCFC, registered by CA 0x102866; entry 27 is `{id 0x20, handler CA
0x85631}` - the h2d-catalogue lane's tool reproduces the table and this lane
disassembles the handler).  Behaviour:

1. null msg -> log, return 100.  `hdr = hcc_msg_get_hdr(msg)` (CA 0x409EA).
2. `vap = lookup(0, hdr[3])` (CA 0xFE578) - hdr[3] is the VAP/module index;
   null -> log, return 100.
3. `payload_len = u16(hdr[4]) - 12`; `ie_len = u32(payload+0)`;
   if payload_len > 616 or ie_len > 608 -> log, return 0x8b2d (35629).
4. memset_s(local 616 B, 0); memcpy_s(local, 616, payload, payload_len);
   log `ie_len`/`type`.
5. **mode = payload[5]**:
   * **mode == 0 (path A)**: call CA 0xFDB72 with (vap, payload, type=payload[4]):
     - type > 19 refused;
     - slot = vap + 20*type + 0x24c;
     - ie_len == 0 -> free ptr, clear ptr/len/cap, memset 6 B at slot+0x258
       = **clear the class**;
     - else -> if ie_len > stored cap or ptr==NULL: free + alloc u16(ie_len)
       (CA 0xBF128), store ptr; memcpy_s(ptr, cap, payload+8, ie_len);
       slot+0x250 = ie_len; memset 6 B at slot+0x258; debug-log the IE prefix.
     - return 0.
   * **mode != 0 (path B)**: type must be 12, 17 or 18 (else silently
     returns 0).  Payload = u32 ie_len, type, mode, pad[2], **6-byte key** at
     +8, IE bytes at +14 with length ie_len-6; slot+0x258 = the 6-byte key;
     ie_len == 8 clears the slot.  This lane does **not** ship path B.
6. teardown/consumer evidence [I]: a firmware 20-slot teardown path frees the
   table (CA 0x44748 region), and a firmware function (CA 0x4FA24 region)
   moves slot fields (ptr/len/cap/6-byte) into another object and clears the
   source - i.e. the table is consumed when the IE set is attached to
   firmware-built frame state.  The exact frame classes the firmware appends
   the IEs to are **[U]** (T6); the on-air fire (FIRE-RECIPE) is the test.

**Grade split:** the wire format, validation bounds, the slot table layout and
path A/B semantics are [V] (disassembly + the captured frame); the *frame-class
meaning* of the slot index is [I] (caller usage, T6).

## T5 - the stock operation [V]

The capture's only 0x20 sample is `ie_len = 0, type = 4, mode = 0`: a
**clear of class 4** on the VAP/module index 4 (two identical reads = one send).
So the vendor boot clears an IE class it does not use; it does not prove what
class 4 is appended to on air (T6).

## T6 - the per-VAP IE classes (payload byte 4) [I]

Both the firmware handler and the host mirror accept classes **0..19** (a
32-bit check against 19).  Observed uses (T2b):

| class | observed use | grade |
| ---: | --- | --- |
| 1 | scan IE list (vendor cfg80211 start-scan path) | [V] call value |
| 3 | internal WEXT wrapper hardcodes it; assoc-request builder | [V] call value |
| 4 | stock capture clears it; assoc-response builder appends it | [V] call value |
| 7 | WPS/P2P wrapper special-cases it | [V] code |
| 9 | assoc-request AND assoc-response builders append it (a common IE set) | [V] call value |
| 12,17,18 | path B (6-byte prefix + IE) only | [V] handler |
| 16 | `hmac_config_set_vendor_ie` mirrors the blob to vap+0x38c (len-6) | [V] code |
| rest | not observed | [U] |

The mapping class -> on-air frame class is **[U]**: the vendor host uses the
same slots for host-built management frames, while the H2D message feeds the
firmware's own table.  This is why the integration exposes the class as an
explicit module parameter (`app_ie_type`, default **4** = the only class the
stock capture touches) and the fire recipe starts a class sweep instead of
hard-coding a guess.

## T7 - the cfg80211 / nl80211 delivery of AP vendor IEs [V]

No `NL80211_ATTR_VENDOR_IE` / `NL80211_CMD_SET_VENDOR_IE` exists in the
6.18.60 kernel UAPI.  The actual mechanism is the AP beacon data: cfg80211's
`start_ap`/`change_beacon` receive `struct cfg80211_beacon_data`
(`include/net/cfg80211.h`) and nl80211 maps:

| cfg80211_beacon_data field | nl80211 attribute (6.18 UAPI) |
| --- | --- |
| `head`, `tail` | `NL80211_ATTR_BEACON_HEAD`, `NL80211_ATTR_BEACON_TAIL` |
| `beacon_ies` | `NL80211_ATTR_IE` |
| `proberesp_ies` | `NL80211_ATTR_IE_PROBE_RESP` |
| `assocresp_ies` | `NL80211_ATTR_IE_ASSOC_RESP` |

hostapd places its `vendor_elements=` config option into the beacon buffer
(beacon tail) and into the probe-response IE buffer
(`src/ap/ap_drv_ops.c:216-217`, `src/ap/beacon.c:2580-2583`), and its
nl80211 driver sends them through those attributes
(`src/drivers/driver_nl80211.c:5931,6099-6109,12576-12586`).  hostapd also
exposes a runtime ubus method `set_vendor_elements`
(`src/ap/ubus.c:1432-1488`, hex string) - it calls
`ieee802_11_update_beacons()`, which needs a `change_beacon` driver op;
this driver does not implement one, so the fire uses the **start_ap** path
(config option present before the AP start), which is also where the vendor's
own cfg event runs (T2b).

Our clean-room driver's `hi5622_scan_op_start_ap()` already receives the
full `struct cfg80211_ap_settings`; the integration (T8) collects the 0xDD
vendor IEs from those four buffers and synthesizes the 0x20 frame.

## T8 - the integration contract (fail-closed) [V for the code, [I] for the placement]

| module parameter | meaning |
| --- | --- |
| `app_ie` (uint, 0444) | 0 = off (default, byte-identical behaviour); 1 = post ONE 0x20 frame in the beacon_static AP-start ladder, only when (`beacon_static=1` AND `vap_start_g0=1` AND the cfg80211 AP settings carry >= 1 well-formed 0xDD IE); > 1 = REFUSED before any frame |
| `app_ie_type` (uint, 0444) | firmware IE class 0..19 (default 4 = the stock-observed class, T6); > 19 = REFUSED before any frame |

Ladder placement: plan step `app_ie` is inserted **after `vap_start_g0`
(the H2D key-0x0065 VAP start) and before `band_start` (the H2D key-0x0242
band-1 start)** - the firmware handler stores the IE table on the (started)
VAP, and the band arm/TBTT must already find it.  Rationale [I]: the vendor
posts it as VAP configuration before the VAP start (T2b/stock t=1.6 s); our
ladder needs the started VAP for the handler's lookup and the band start is
the last pre-beacon step.  hdr[3] = the AP-start module/VAP index
(`hi5622_vap_add_index`, the same value the 0x0065 step uses).

SYNTH-SIDE: `hdr[3] = vap`, `ie_len` = collected blob size, `type` =
`app_ie_type`, `mode = 0`, the 3-byte zero tail.  FAIL-CLOSED: malformed
cfg80211 IE TLVs abort the start before any frame; an empty vendor-IE list
logs and skips the step; no frame is built when `app_ie=0`.

READ-BACK (added by the integration, diagnostic/non-gating): after the post the
step resolves the firmware module/VAP context (the same module-table path the
desc_table/dfs steps use) and reads the slot `vap + 0x24c + 20*type`
`{u32 ptr; u32 len; u32 cap; u8 key[6]}`; the dmesg line
`cfg80211: app_ie post: mctx=... vap=... slot(class N)=... ptr=... len=...
cap=...` reports `firmware slot MATCHES the posted IE list` when
`ptr != 0 && len == ie_len`.  It is the host-side witness the fire records; the
decisive on-air check is the laptop scan (FIRE-RECIPE H4).

## T9 - limits / open items

| item | grade |
| --- | --- |
| wire format + firmware acceptance | [V] |
| firmware stores the IE blob in the per-VAP class slot | [V] |
| the class -> on-air frame mapping | [U] - the fire's class sweep is the test |
| path B (mode != 0, classes 12/17/18) | decoded [V], **not shipped** in this lane |
| our driver's `change_beacon` (runtime hostapd update) | not implemented - out of scope; the start_ap path is what the fire uses |
| the vendor's vap+0x38c vendor-IE mirror (class 16) | decoded [V], not shipped |
