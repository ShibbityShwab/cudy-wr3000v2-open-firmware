# Scan D2H RE + cfg80211 BSS decode (lane wifi-scan-d2h, task-118)

**Status:** host-only, complete. The scan-result D2H formats are decoded from
the pinned local vendor artifacts, a tolerant decoder is implemented and
kernel-compile-verified, the integration patch for the scan-ap module is
included, and the bench observables that separate "firmware produced nothing"
from "our decode has a gap" are specified. Nothing was fired at the device.

**Date:** 2026-11-26. **Task:** shared task-118. **Write scope:**
opensource/docs/incoming/wifi-scan-d2h/ only.

Evidence markers follow the project convention: **[V]** verified from a named
artifact/decode in this lane, **[V\*]** hardware-measured by an earlier bench
lane, **[I]** inference, **[U]** unknown (never guessed in code).

---

## 0. Headline

The 2026-11-26 real-scan session posted g0 id0xC (520 B) into the firmware and
then saw **zero** D2H scan events within 5 s (ledger 2026-11-26 06:30). This
lane establishes why, and what the firmware would have sent if the scan had
been accepted:

1. **The 2026-11-26 result is a firmware-side refusal, not a decode gap.**
   The g0 id0xC handler (firmware CA 0x6494C) resolves **VAP id 0** through CA
   0x581C8; if that firmware VAP object does not exist it logs and returns
   100 (0x64) **without scheduling a scan**. Our boot has a user object (g0
   id7) but no per-radio VAP object - the family the wifi-vap-scan lane marked
   [U]. The scan engine CA 0x644A4 therefore never runs and no result frame is
   ever generated. [V]
2. **The g1 id15 wire format is 802.11 frame + a 20-byte trailing record.**
   The vendor host parser computes frame length as skb_len - 0x14 before it
   touches the 802.11 body and hands the tail pointer to its BSS filler; the
   filler consumes u32@+0, u8@+4, s8@+5, s8@+6, u32@+8, u32@+0xC, u8@+0x10
   and a PHY-mode byte @+0x11. [V] The 20-byte trim assumption of the earlier
   scan-ap decoder is now backed by this decode.
3. **g1 id17 (per-channel result) is fully decoded, including the firmware
   sender.** Firmware CA 0x641B4 allocates a 0x70-byte payload, writes group 1
   / id 0x11 / field B 0x12, stores the slot index at payload[0], copies a
   108-byte (0x6C) per-channel record from ctx+0x1688 to payload+4 and calls
   hcc_msg_tx. The vendor host handler reads exactly those fields. [V]
4. **g1 id16 and g2 id2 are completion events** whose status bytes the vendor
   host consumes (frame+0xC and frame+0xD respectively); g2 id2 then drives
   wal_inform_all_bss() - the point where the vendor feeds cfg80211. [V]
5. A **tolerant decoder** (src/hi5622_scan_d2h.c, pure C99, host + 6.18.60
   kernel) probes the frame/trailer boundary by walking the IE chain instead of
   assuming a fixed split, and is wired into the scan-ap provider by
   patches/0001-hi5622-scan-d2h-decoder.patch. **Host selftest 4076/4076
   PASS; kernel-context compile PASS with 0 warnings; test vectors 85/85 PASS;
   RE evidence self-check 50/50 PASS.**

What remains open (always [U], never guessed in code): which trailer byte is
the RSSI (signal is deliberately reported as 0 unless a caller opts into the
trailer heuristic), the exact firmware senders for g1 id15/id16 (they are not
among the 56 direct hcc_msg_tx callers and are most likely produced by the
chained MAC/scan engine, see section 8), and the status code space.

---

## 1. Inputs, pins, conventions, safety

Local artifacts (never committed; only derived facts appear in this lane):

| artifact | pin |
| --- | --- |
| vendor firmware image | 928,920 B, sha256 7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b (rootfs-2.4.15/rootfs-2.5.24 copies are identical) |
| vendor Wi-Fi module, stock 2.5.24 | 3,564,728 B, sha256 de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521 (md5 4737fcb2...) |
| vendor Wi-Fi module, stock 2.4.15 | 3,581,748 B, sha256 22fa789a3d431f49f9c52c76c402e5740d1d14118b5e47a347f2cc247532c7d2 |

Conventions:
- firmware CPU address (CA) = file offset + 0x40000; Thumb-2;
- vendor .ko offsets below are .text **section** offsets (the ELF section
  starts at file offset 0x38); the module is ARM mode;
- the project already pins the same two files in wifi-vap-scan/REPORT.md,
  wifi-datapath/REPORT.md and wifi-re-report/EVIDENCE-INDEX.md; hashes above
  match those pins.

Safety: all analysis is static, read-only, host-only. No device register was
touched, no frame was sent, no vendor byte is committed. The verification
scripts open the local blobs read-only and print offsets/decoded instructions
only (tools/fw_scan_d2h_re.py).

How the decoder receives data in our driver: the direct group-A D2H reader
stages each firmware frame as a 12-byte HCC header + payload (d2h.c
rd_deliver()) and hands the scan layer **header and payload separately**
(hi5622_scan_deliver(ctx, hdr, payload, avail)). The decoder therefore works on
the payload only; the header (group/id) arrives separately for classification.

---

## 2. Envelope and dispatch (recap + what this lane adds)

D2H frames are HCC messages: 12-byte header at +0, payload at +0xC.

| off | width | field |
| --- | --- | --- |
| +0x00 | u8 | group (dispatch nibble) |
| +0x01 | u8 | low nibble alloc state / high nibble core (thread) index |
| +0x02 | u8 | field A |
| +0x03 | u8 | field B (per message; the scan/id17 sender puts 0x12 here) |
| +0x04 | u16 | total length = payload + 12 |
| +0x06 | u16 | message id |
| +0x08 | u8 | field C |
| +0x09 | u8 | dst |
| +0x0A | u16 | transport tag 0x5A5A |

The scan families and their vendor host handlers (wifi.ko .symtab, all
byte-verified by this lane's RE tool):

| group/id | vendor host handler (.text off) | role |
| --- | --- | --- |
| g1 0x0F | hmac_scan_proc_scanned_bss @0x9E00C -> hmac_scan_proc_scan_result_handle @0x9CF7C -> hmac_fill_bss_dscr @0x9CCB4 | one BSS result [V] |
| g1 0x10 | hmac_scan_proc_scan_comp_event @0x9A0EC | scan completion [V] |
| g1 0x11 | hmac_scan_process_chan_result_event @0x9EB54 | per-channel result [V] |
| g2 0x02 | wal_scan_comp_proc_sta @0x125714 -> wal_inform_all_bss (reloc @0x1257EC) | WAL/STA completion, feeds cfg80211 [V] |

The vendor dispatch tables for these ids were already decoded by the project
(phase38/d2h-vocabulary-verified.md, wifi-h2d-catalog); this lane adds the
payload layouts and the firmware sender.

---

## 3. g1 id15 - one BSS result (the scan result frame)

### 3.1 Payload layout [V]

Payload = **802.11 beacon or probe-response frame** followed by a **20-byte
trailing record**. Evidence (vendor host, ARM):

- hmac_scan_proc_scanned_bss reads msg->data at [msg+0x118] and dispatches
  (file offsets .text+0x9E01C / calls the big parser via REL28 relocation).
- hmac_scan_proc_scan_result_handle computes
  'sub r3, sl, #0x14' (frame length = skb_len - 20; .text+0x9D104), copies
  exactly that many bytes to its BSS record, walks the IE chain, derives the
  channel with mac_ie_get_chan_num (reloc @0x9D160) and calls
  hmac_fill_bss_dscr (reloc @0x9D180) with the trailer pointer in r8.
- hmac_fill_bss_dscr consumes the trailer: ldrb r3,[r8,#4] (.text+0x9CD6C),
  ldr r3,[r8] (.text+0x9CD7C), ldrb r3,[r8,#0x11] (.text+0x9CDC0), i.e. the
  trailer is a real, read, 20-byte record - not padding.
- the same function trims the trailing 0x14 bytes before handing a management
  frame to hmac_rx_mgmt_frame_notify / hmac_rx_mgmt_hsan_rpt (two guarded
  skb_trim(len-0x14)/skb_put(0x14) pairs), which independently confirms the
  20-byte tail.

The frame itself is a complete 802.11 management frame: frame control must be
version 0, type 0, subtype 8 (beacon) or 5 (probe response). The decoder
validates exactly that and refuses anything else.

### 3.2 Fields the vendor consumes [V]

802.11 frame body (offsets from the frame start):

| field | offset | evidence |
| --- | --- | --- |
| BSSID | +0x10 (addr3, 6 B) | hmac_fill_bss_dscr copies frame+0xA..+0x15 into its BSS record (addr2+addr3) |
| timestamp | +0x18 (u64) | hmac_fill_bss_dscr reads the fixed fields via the frame copy |
| beacon interval | +0x20 (u16) | mac_get_beacon_period path |
| capability | +0x22 (u16) | stored into the BSS record |
| IEs | +0x24 .. frame end | mac_find_ie(0 SSID), mac_find_ie(3 DS), mac_find_ie(61 HT op), mac_ie_get_chan_num, vendor IE 0x50F2, WMM IE |

Trailer record (20 B):

| off | width | consumer evidence (.text) | meaning |
| --- | --- | --- | --- |
| +0x00 | u32 | hmac_fill_bss_dscr ldr r3,[r8] | low byte lands in the BSS record at +6; [U] |
| +0x04 | u8 | ldrb r3,[r8,#4] -> BSS record +0 | [U] |
| +0x05 | s8 | ldrb/ldrsb [r2,#5] -> record +0x75 | [I] signal-like (rx/tx power or RSSI) |
| +0x06 | s8 | ldrsb [r2,#6] -> record +0x76 | [I] signal-like (SNR or RSSI) |
| +0x08 | u32 | ldr [r2,#8] -> record +0x78 | [U] |
| +0x0C | u32 | ldr [ip,#0xc] -> record +0x7c | [U] |
| +0x10 | u8 | ldrb [ip,#0x10] -> record +0x80 | [U] |
| +0x11 | u8 | ldrb r3,[r8,#0x11]; 4->0x0F, 8->0x10, 12->0x11 in record+0x3A | PHY mode discriminator [V offset, I naming: 4=HT, 8=VHT, 12=HE] |
| +0x12..+0x13 | 2 B | not read by the decode path | [U] |

RSSI policy: the trailer is the only per-frame metadata; which byte is RSSI is
not proven statically (two plausible signed bytes at +5/+6). The decoder
therefore reports **signal_mbm = 0 by default** and only fills it from trailer
+5 when the caller sets opts->trust_trailer_signal (bench correlation pending).
This keeps cfg80211 entries honest instead of inventing a measurement.

### 3.3 Fragmentation / BSS list framing

One event carries exactly one BSS frame; there is no fragmentation or
segmentation in this path. The vendor host deduplicates by BSSID: the parser
walks its scan-BSS list, compares BSSIDs and keeps the entry with the stronger
RSSI (it copies the better signed-byte value over the newer record when the
older one is stronger and younger than 1 s, then unlinks/keeps one record).
The config-sync event (g1 id3) is a different family and is not part of this
payload.

### 3.4 The layout-probe (why the decoder is tolerant)

Whether the 20-byte trailer is produced on-wire by the firmware scan engine or
appended by the vendor host RX path from a per-frame descriptor is **not
decidable from the two .ko/firmware blobs alone** (the host's skb conversion
for these events is inside plat.ko and is not symbol-resolved; the HCC header
pointer/offset conventions differ between message kinds). Our direct reader
sees whatever the firmware staged. The decoder therefore probes:

1. frame at payload[0], trailer 20 - accept only if the IE chain ends exactly
   at frame end (the vendor arithmetic layout) [preferred];
2. frame at payload[0], no trailer - IE chain ends exactly at payload end;
3. defensive: 12-byte prefix then the two variants above (only if
   payload[0] looks like a group byte and payload[12] is a valid frame
   control).

Each candidate is bound-checked; failure of all candidates returns
-EOPNOTSUPP, which the caller must count as a decode gap (never as a valid BSS).

---

## 4. g1 id16 - scan completion

Vendor handler hmac_scan_proc_scan_comp_event @0x9A0EC. It reads (frame
pointer view): +1 high nibble = core/band (0x9A100), +3 = chip id (0x9A118),
+0xC = **status/result byte** (0x9A154, 0x9A184, 0x9A1FC, 0x9A214) which is
compared with 1 and stored into the MAC device state; a non-1 status unregisters
the scan timer on the success path (0x9A2C8 hcc_timer_unregister). So the
completion carries a status code whose full space is [U] but whose success
value is 1 [V].

Decoder API: hi5622_scan_d2h_parse_complete() reports status = payload[0] and
up to 16 raw bytes. The payload length of the firmware sender is not pinned
(the sender was not found among the direct hcc_msg_tx callers) - the decoder
needs >= 1 byte and is length-tolerant.

---

## 5. g1 id17 - per-channel result (fully decoded, incl. firmware sender)

### 5.1 Firmware sender [V]

CA 0x641B4 (called from the scan state machine at 0x643EE as
0x63CB8/0x5FC24/0x61CFC family; the caller passes chip ctx, VAP 0, band and
the scan-state object):

- 0x642DA: 'movs r0,#0x70' - payload size 112 bytes;
- 0x6430A: hcc_msg_get_header(CA 0x409EA);
- 0x64314: bfi group nibble = 1 (value set at 0x6430E movs r2,#1);
- 0x6431C/0x64320: 'movs r2,#0x11' then strb id at header+6 (0x11);
- 0x64328/0x6432C/0x64334: header+1 high nibble = [ctx+0xd6C] (core),
  0x64338: header+2 = [ctx+0xd6E];
- 0x64348: header+3 = 0x12 (field B);
- 0x6434C: header+0xC = payload[0] = slot index (r8 = sum of the two scan
  counters [ctx+0x1A00 object +0xFC] + [+0xFD]);
- 0x64350/0x64352: 'movs r2,#0x6C', blx 0x1036AC - copy 108 bytes from
  [ctx+0x1688] to header+0x10 = payload+4;
- 0x64358: hcc_msg_tx.
Total frame on the wire = 12 + 112 = **124 bytes (0x7C)** - a fixed bench
observable.

### 5.2 Payload layout [V]

| off | width | field |
| --- | --- | --- |
| +0x00 | u8 | slot index (BSS-index into the per-channel record array) |
| +0x01..+0x03 | 3 B | zero (allocator clear) |
| +0x04..+0x6F | 108 B | per-channel record copied from ctx+0x1688 |
| +0x04 | u8 | channel number (host reads it as the channel) |
| +0x0C | u8 | slot index again (host uses payload[0xC] = record[8] as slot) |
| rest | [U] | per-channel counters/aggregates, not decoded |

Cross-check: the vendor host handler reads data[+0xC] as the slot and
data[+0x10] as the channel number, and copies 108 bytes from data+0x10; with
the header-inclusive frame pointer convention used by the other handlers this
is payload[0], payload[4] and payload+4 - exactly the firmware layout. [V]

Decoder API: hi5622_scan_d2h_parse_chan() -> slot, channel, is_5g, freq,
108-byte raw block. This event is the natural "scan is alive" heartbeat on the
bench (fixed 124-byte total, frequent per channel).

---

## 6. g2 id2 - WAL/STA scan completion (the cfg80211 feed point)

Vendor handler wal_scan_comp_proc_sta @0x125714 reads (frame view) +1 high
nibble = core/band, +2 = u8, +3 = chip id, +0xD = **status/result byte**
(0x1257D0), then - when the chip scan flag [chip+0x508] bit0 is clear and a
wiphy exists - calls wal_inform_all_bss(wiphy, mac_dev, chip_id) (REL28
@0x1257EC) and continues with the scan-done path. In other words: g1 id15
collects BSS records into the host MAC device list; **g2 id2 is the event that
pushes them to cfg80211** (wal_inform_all_bss -> oal_cfg80211_inform_bss_frame
-> cfg80211_inform_bss_frame_data). [V]

Decoder API: hi5622_scan_d2h_parse_sta_comp() exposes status (payload[0xD] with
a payload[1] fallback for the header-stripped reading), core/band and raw
bytes; status semantics are [I]/[U].

Note for our own scan implementation: we do not need to replay the vendor's
two-layer WAL handoff - our cfg80211 scan layer decodes g1 id15 directly and
completes on g1 id16/g2 id2 or the bounded timeout. What matters is that both
completion families must be treated as completion candidates, exactly as the
scan-ap model's hi5622_scan_classify() already does.

---

## 7. Firmware conditions that emit scan results (does it need a started VAP?)

This is the answer to the 2026-11-26 zero-result session.

### 7.1 The scan request path [V]

g0 id0xC handler CA 0x6494C (Thumb-2, fully re-decoded here):

- 0x64964 hcc_msg_get_header; 0x6496A chip lookup from header[3]
  (field B = chip/endpoint id) via CA 0x961AE;
- 0x64986 per-chip object [r0+0x4B8];
- 0x6499A **VAP lookup: r0 = 0 -> CA 0x581C8**; if the result is NULL, log and
  **return 0x64 (100)** - a graceful refusal, no exception, no scan;
- on success: 0x649B8 reads header+0x147 (= payload+0x13B) into VAP+0x502,
  then tail-calls the scan engine CA 0x644A4 with (chip, VAP, band, payload).

### 7.2 The scan engine [V]

CA 0x644A4: validates payload+0x1F0 (rejects 4, special-cases 8), checks the
VAP scan state byte VAP+0x51C (must not be 1 = scan already in progress),
then 0x645A2 sets VAP+0x51C = 1 and copies the **508-byte (0x1FC) request**
into VAP+0x300 (memcpy_s reloc relocated from 0xC1E44) and starts the scan
timers/work. The result events of sections 3-5 are generated by that engine
after the request has been accepted.

### 7.3 Consequence for our boot

- The g0 id7/g0 id8 user-add frames create a firmware **user** object but not
  the per-radio **VAP** object (wifi-vap-scan/REPORT.md section 4; [V*]).
- With no VAP object, handler CA 0x6494C returns 100 immediately. The 520-byte
  frame is still consumed by the transport (that is why the bench saw
  'consumed 0 polls'), but **no scan is scheduled** and no g1 15/16/17 or
  g2 2 frame can ever appear.
- Therefore the 2026-11-26 "no results within 5 s -> scan aborted" outcome is
  the expected, documented refusal. It is NOT evidence that the response
  format is missing or that the scan request was malformed.
- Exact precondition for results: firmware **VAP object 0 must exist** before
  the scan request. Whether it must additionally be in a "started" state is
  [U]; the handler's only VAP-side check is the lookup itself, while the
  engine checks the scan-in-progress byte. The VAP create/start family remains
  the wifi-vap-scan lane's open item ([U] there); this lane's observables are
  ready for the moment it lands.

---

## 8. What is still open (never guessed)

| item | state | note |
| --- | --- | --- |
| g1 id15 firmware sender identity | [U] | not among the 56 direct hcc_msg_tx callers (whole-image BL scan); the BSS result is almost certainly emitted by the chained MAC scan engine (the appended second image / the 0x5E118/0x5FC24/0x63CB8 scan-callback family) rather than a standalone sender; the wire layout is nonetheless pinned by the host parser + the trailer arithmetic |
| g1 id16 firmware sender identity | [U] | same reason; payload status offset pinned from the host handler |
| trailer byte semantics (+0/+4/+8/+0xC/+0x10, RSSI choice +5/+6) | [U]/[I] | decoder exposes all raw bytes; signal opt-in only |
| g1 id16 / g2 id2 status code space | [U] | only 'success = 1' is proven (scan comp path compares with 1) |
| VAP create/start family | [U] | wifi-vap-scan lane; this lane's precondition proof makes it the single remaining firmware blocker for scan results |

Search-space evidence for the missing senders: the whole firmware image has
exactly 56 BL call sites to hcc_msg_tx (CA 0x102760) and 54 to hcc_msg_alloc
(0x102822); every enclosing function was classified (alloc size, group/id
store); only the g1 id17 sender matches a scan id (0x11) at a scan-engine
address. g1 id15/id16 are therefore not built by a plain sender function with
the config-sync template (which stores header fields relative to header+0x18);
they are produced further inside the scan engine's callback chain.

---

## 9. Implementation delivered by this lane

### 9.1 Portable decoder

- src/hi5622_scan_d2h.h - ids, structs, confidence markers, API.
- src/hi5622_scan_d2h.c - tolerant IE-walk layout probe, BSS/complete/chan/
  STA-complete decoders, channel->frequency table, bounded copies everywhere.
  No kernel dependency beyond <linux/types.h>/<linux/string.h> under
  __KERNEL__; plain C99 host-side.

### 9.2 Integration patch (wifi-scan-ap)

patches/0001-hi5622-scan-d2h-decoder.patch:
- adds hi5622_scan_d2h.o to the module build;
- replaces the provider's best-effort parse_scan_result (the old unconditional
  'trim 20 from the end' decoder) with the vendor-verified decoder plus a shim
  that fills struct hi5622_scan_result;
- implements the previously empty parse_scan_complete provider slot for
  g1 id16 (status -> reason/aborted).
Integration: copy src/hi5622_scan_d2h.{c,h} into wifi-scan-ap/src/, then
'patch -p1 < patches/0001-hi5622-scan-d2h-decoder.patch' there.
The patch is applied and compiled by tools/kernel-check.sh on a scratch copy;
no other lane's files are modified by this lane.

### 9.3 Verification (all host-only, all reproducible)

| check | command | result |
| --- | --- | --- |
| host decoder unit tests | "make test" -> tools/scan_d2h_selftest.c | **4076/4076 PASS** |
| RE evidence self-check | tools/fw_scan_d2h_re.py --selftest | **50/50 PASS** |
| committed test vectors | tools/gen_vectors.py --check | **85/85 PASS** |
| kernel-context compile (6.18.60) | tools/kernel-check.sh | **PASS**, 0 warnings, hi5622_wifi.ko 703,476 B sha256 d58785e1d30216a9ff6b541157d2155e32742a9af2153df8314593b12aa91466, 11 hi5622_scan_d2h symbols |

### 9.4 Test vectors

testvectors/scan-d2h-vectors.json - 9 synthetic vectors (2.4 GHz beacon +
trailer, 5 GHz probe response + HT op IE + trailer, frame-only layout, hidden
SSID, two negative cases, completion, channel result, STA completion). Every
frame is constructed from the layout in this report; none is a vendor capture.

---

## 10. Bench observables: "no results" vs "decode gap"

Once the VAP object exists (wifi-vap-scan lane) and a scan request is posted,
the following distinguishes the two failure classes **without guessing**:

### 10.1 Watch list (read-only)

| observable | where | expected on success |
| --- | --- | --- |
| g1 0x0F count | rd_catalog / rd_status seen table, or dmesg "D2H DIRECT ... hdr g=1 id=15" | >= 1 per discovered BSS |
| g1 0x0F total | same lines | frame_len + 12 + 20; a minimal beacon (24+12+SSID/DS IEs) is >= ~72 B; g1 id15 total is variable |
| g1 0x10 count | same | typically 1 per band scan (completion) |
| g1 0x11 count | same | 1 per scanned channel, **total = 124 (0x7C) fixed** |
| g2 0x02 count | same | completion of the WAL/STA path |
| driver scan counters | "cfg80211: scan DONE (... bss=%u complete=%u channel=%u sta=%u unknown=%u dropped_busy=%u)" dmesg line and the scan sysfs status (results, bss_events, complete_events, channel_events, sta_events, parse_errors, invalid_results, inform_failures) | bss_events > 0 and results > 0 for discovered APs |

### 10.2 Decision table

| firmware events seen | driver counters | verdict |
| --- | --- | --- |
| none of g1 15/16/17, g2 2; rd_catalog shows only the boot catalogue (e.g. g1 id3) | results=0, bss_events=0 | **firmware produced nothing**: scan was refused (VAP object missing / scan state) or the interval was too short. Check the 100-return path: the request frame was consumed (transport OK) but no scan ran. This is the 2026-11-26 state. |
| g1 0x11 present (124 B) but g1 0x0F count 0 | bss_events=0, channel_events>0 | scan is running, channels are being reported, no BSS decoded from the air; verify the channel list in the request is what you meant and the AP is in range. |
| g1 0x0F present but driver counts parse_errors/invalid_results | bss_events>0, results=0 or < bss_events | **decode gap**: capture the raw payload (dmesg already hex-dumps the first 16 payload bytes; use the rd log frame snapshot / full-frame read for the tail) and extend the decoder. The decoder returns -EOPNOTSUPP, never a fake result. |
| g1 0x0F present, inform_failures>0 | results>0 but inform failures | the decoded BSS has a frequency our wiphy does not list (channel 14 or a 5 GHz channel outside the LU plan); the frame is understood, cfg80211 just has no channel object. |

### 10.3 Procedure

1. Boot golden, run the C2-like ladder so ctx+TXQ are clean, add the VAP (VAP
   start lane), post the g0 id0xC 520-byte scan request.
2. Read rd_status/rd_catalog immediately after the request (before the 5 s
   cfg80211 timeout) - the direct reader's seen table is the authoritative
   "what the firmware sent" record; then read the scan sysfs status/dmesg.
3. Classify with section 10.2. Only a row-3 result justifies decoder work;
   rows 1-2 are firmware/precondition results.
4. Pass criteria for D2 flips to PASS: cfg80211 scan_done with aborted=0 and at
   least one BSS informed with a plausible freq/SSID; no exception, boot page
   golden. The signal value stays 0 until the trailer RSSI byte is correlated
   against a known AP (then opts->trust_trailer_signal can be enabled in the
   provider patch).

recipes/bench-observables.md contains the copy-paste command checklist;
recipes/scan-d2h-ladder.json contains the machine-readable ladder/observables.

---

## 11. Provenance and clean-room statement

- Every layout in this report was derived from static analysis of the locally
  held vendor artifacts and from the project's committed vendor-user-space
  reverse engineering; no vendor source, object code, firmware byte,
  calibration value or secret is reproduced in this lane.
- The committed C code is original: field offsets/ids are derived protocol
  constants, and the selftest builds all frames synthetically.
- No device contact, no writes to the router, no image or slot touched.
- Files: src/hi5622_scan_d2h.{h,c}, tools/{fw_scan_d2h_re.py,
  scan_d2h_selftest.c, scan_d2h_dump.c, gen_vectors.py, kernel-check.sh,
  selftest.sh}, patches/0001-hi5622-scan-d2h-decoder.patch, recipes/,
  testvectors/.

## 12. Lane map / handoffs

- **wifi-vap-scan** (task-110): the VAP create/start family is the single
  remaining firmware blocker for scan results (section 7). Its 20-byte footer
  assumption is confirmed by section 3.
- **wifi-scan-ap** (task-113): apply this lane's patch to replace the
  best-effort decoder; the hi5622_scan_classify() ids/classes are correct.
- **wifi-bench-tooling** (bench2): D2's PASS row can add the rd_catalog
  watch-list of section 10.1 as diagnostic gates (g1 0x11 total == 0x7C is a
  cheap liveness check).
- **wifi-cfg80211**: no cfg80211 API change is needed; the decoder returns the
  same struct hi5622_scan_result the provider contract already uses.
