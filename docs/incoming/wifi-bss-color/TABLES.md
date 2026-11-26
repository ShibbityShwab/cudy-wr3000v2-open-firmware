# TABLES - g0 id36 hmac_scan_config_bss_color: layout, KAT, addresses

Evidence markers: [V] verified from committed decode/local disassembly + capture; [D] our
design; [I] inference; [U] unknown. Vendor bytes below are decoded protocol bytes quoted as
text (no binary committed).

## T1. Frame layout (12-byte HCC header + 8-byte payload = 20 B) [V]

| off | size | field | captured value (stock t=7.3 s) | source in builder (0x99C10) |
| ---: | ---: | --- | --- | --- |
| 0x00 | 1 | group | 0x00 | `bfi hdr[0], r8(=0), #0,#4` @0x99C84 |
| 0x01 | 1 | alloc nibble / dst hi | 0x01 | low nibble = 1 by hcc_msg_alloc [V]; high nibble = scan_obj[+0xFAA] @0x99C98 |
| 0x02 | 1 | FA | 0x00 | scan_obj[+0xFA9] @0x99CA4 |
| 0x03 | 1 | FB | 0x07 | scan_obj[+0xFA8] @0x99CAC - destination chip id [V] |
| 0x04 | 2 | total length LE | 0x0014 (20) | hcc_msg_alloc(8) |
| 0x06 | 2 | message id LE | 0x0024 (36) | `mov r2,#36; strb r2,hdr[6]` @0x99C8C/0x99C90 |
| 0x08 | 1 | fc | 0x00 | `strb r8(=0),hdr[8]` @0x99CB0 |
| 0x09 | 1 | dst | 0x00 | hcc_msg_alloc/tx |
| 0x0A | 2 | transport tag LE | 0x5A5A | hcc_msg_alloc/tx |
| 0x0C | 8 | **BSS-color bitmap (u64 LE)** | 0x0400000002010009 | `memcpy_s(payload, 8, bitmap, 8)` @0x99D28-0x99D38 |

## T2. Payload field definition [V]

* 64-bit little-endian bitmap; **bit i = BSS color i** (i in 0..63). One bit per distinct
  colour observed by the vendor scan; the vendor sets bits from `entry[+0x85] & 0x3F`.
* Producer provenance: `hmac_scan_proc_scan_result_handle` @0x9D870 stores
  `(HE-Operation colour-info byte & 0x3F)` into the scan entry; the builder ORs the bits of
  every entry in mac_dev+0x88's result list.
* Captured mask 0x0400000002010009 -> colours **{0,3,16,25,58}** [V arithmetic].
  NOTE: the task-232 census TABLES T6 printed {0,3,24,25,58}; the fourth bit is **16**
  (`0x00010000`), not 24 - corrected here from the same captured payload bytes.
* The bitmap may contain bit 0 (vendor producer is 6 bits); the cfg80211 AP-colour
  attribute cannot (kernel policy 1..63).

## T3. KAT vectors (byte-exact; host selftest)

K1 - the captured stock frame (20 B, reproduced by the portable builder with
fa=0x00, fb=0x07, bid_hi=0x00, mask=0x0400000002010009):

    00 01 00 07 14 00 24 00 00 00 5a 5a | 09 00 01 02 00 00 00 04

K2 - our AP-colour posting, hostapd `he_bss_color=25` (mask = 1<<25), band chip id = 0
(fa=0, fb=0, bid_hi=0):

    00 01 00 00 14 00 24 00 00 00 5a 5a | 00 00 00 02 00 00 00 00

K3 - colour 1, chip 4: mask = 1<<1 -> payload `02 00 00 00 00 00 00 00`.

K4 - colour 63, chip 15: mask = 1<<63 -> payload `00 00 00 00 00 00 00 80`.

K5 - reject vectors: total len != 20; id != 0x24; tag != 0x5a5a; len != 0x14; bitmap with
a bit >= 64 impossible (u64) - builder API rejects colour > 63; AP path rejects colour 0.

## T4. Address map

| item | address | note |
| --- | --- | --- |
| builder (vendor host .text) | 0x99C10 | `hmac_scan_config_bss_color`, 0x254 B [V] |
| builder caller | 0x9A00C | in `hmac_scan_proc_scan_comp_event` (0x99EAC) [V] |
| colour-field writer | 0x9D870 | `hmac_scan_proc_scan_result_handle` + 0xB34 [V] |
| HE-Operation parser | 0x222B0 | `mac_ie_proc_he_opern_ie`; called as `mac_ie_parse_bss_he_oper` @0x22608 [V] |
| firmware g0 table | CA 0x10CCFC (file 0xCCCFC) | idx 30 = id 0x24 -> handler file 0x755E1 [V catalog] |
| firmware handler | CA 0xB55E1 (file 0x755E1) | Thumb; per-chip dedupe + callback global+0x3EC [V] |
| firmware image | 928,920 B | local; `build/fw-h2d-stall/stock-thumb.dis` |

## T5. cfg80211 / nl80211 path for the AP colour [V]

| element | evidence |
| --- | --- |
| attr | `NL80211_ATTR_HE_BSS_COLOR` nested {COLOR u8 1..63, DISABLED flag, PARTIAL flag} - kernel 6.18 `net/wireless/nl80211.c` (parse fn line 6199, policy 472, beacon parse call 6307) |
| settings | `struct cfg80211_beacon_data.he_bss_color` + `he_bss_color_valid` (include/net/cfg80211.h) -> reachable from `settings->beacon` in the start_ap op |
| hostapd | 2026.08 `driver_nl80211.c:6203`: sends the nested attr when `params->freq->he_enabled` and `nl80211_attr_supported()` (`attr <= nl80211_maxattr`), i.e. any modern kernel - hostapd `he_bss_color=N` arrives in our start_ap |
| our knob | `bss_color=0` off (default, fail closed); `1` = post once per start_ap from the cfg80211 colour [D] |

## T6. The implemented artifact (image #3 module base) [V]

| item | value |
| --- | --- |
| base | image #3 module source tree (64-param module a717c86d; evidence/base-pin.txt) |
| patch | patches/0001-bss-color.patch, 13,322 B, 5 files (2 new, 2 edits) |
| knob | `bss_color` uint 0644 default 0; 0=off, 1=post AP HE BSS colour, >1 refused (no frame) |
| posting point | `hi5622_scan_op_start_ap`, after the AP config / VAP start; failure logged, never aborts |
| artifact | stripped 382,404 B sha256 d87fbdcb...dcb; 65 params; 0 warnings; .text strip-identical |
| host tests | 29/29 PASS (captured frame K1 + he_bss_color=25 K2 + validation/policy) |

## T7. Failure/status codes in the vendor paths [V]

* builder: 0 success, **100 (0x64)** on alloc/buf/memcpy/tx failure and on NULL args; logs
  via oam_error_log* (line numbers 0x145/0x14D/0x154/0x16E/0x175).
* firmware handler: 0 consumed (including the per-chip dedupe hit), **100** on NULL/error.
