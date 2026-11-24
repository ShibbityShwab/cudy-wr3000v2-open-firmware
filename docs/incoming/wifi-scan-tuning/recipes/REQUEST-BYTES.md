# g0 id0xC scan request - exact bytes and firmware read sites (task-133)

Regenerate with `tools/request_dump.c` (host; builds only the lane's
clean-room frame builder).  The request below is the one the driver posts for
`iw dev wlan0 scan` on the bench band (2.4 GHz, channels 1..13, wildcard SSID,
vendor-default dwell 60 ms).  It is **byte-identical to the fired baseline**
with respect to every field the firmware reads; the only thing the tuning
changes is how many entries a single request carries (batch split), never a
field encoding.

- wire total **520 B** = 12-B HCC header + 508-B payload (`VS_LEN_SCAN_REQ`)
- header: `00 01 00 00 08 02 0c 00 00 00 5a 5a`
  (group 0 chip, alloc=1/dst=0, VAP 0, chip 0, total=0x0208, family 0x0C,
  tag 0x5a5a)
- 42 x 4-B channel slots exist; only the first 13 are non-zero

```
0000  00 01 00 00 08 02 0c 00 00 00 5a 5a 01 00 01 01  ..........ZZ....
0010  00 00 00 00 00 00 00 02 ff ff ff ff ff ff 00 00  ................
0020  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
...
0140  00 00 00 00 01 00 00 00 00 00 00 00 01 00 00 00  ................
0150  02 00 00 01 03 00 00 02 04 00 00 03 05 00 00 04  ................
0160  06 00 00 05 07 00 00 06 08 00 00 07 09 00 00 08  ................
0170  0a 00 00 09 0b 00 00 0a 0c 00 00 0b 0d 00 00 0c  ................
0180  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00  ................
...
01f0  00 00 00 00 00 00 00 00 0d 00 3c 00 00 01 0b 00  ..........<.....
0200  00 00 00 00 00 00 00 00                          ........
```

## Field map (payload offsets; wire offset = 0xC + payload offset)

| payload | value (this scan) | meaning | firmware read site |
| ---: | --- | --- | --- |
| +0x000 | 1 | flags0 | engine param block (copied whole to VAP+0x300) |
| +0x001 | 0 | flags1 (dual-band flag) | copied whole |
| +0x002 | 1 | has BSSID | copied whole |
| +0x003 | 1 | n_ssids | copied whole |
| +0x00B | 2 | type_b (active variant) | copied whole |
| +0x00C | ff:ff:ff:ff:ff:ff | broadcast BSSID | copied whole |
| +0x018 | 8 x 36 B, slot0 zero | SSID slots (wildcard = zeroed) | copied whole |
| +0x131 | 0 | scan flag byte | copied whole |
| +0x138 | 1 | second SSID count | copied whole |
| +0x139 | 0 | skip-timeout bit [U] | copied whole |
| +0x13B | 0 | per-VAP scan flag | handler CA 0x649B8 -> VAP+0x502 |
| +0x13C | 0 | scan-timeout mode [U] | copied whole |
| +0x140 + 4*i | i=0: 01 00 00 00; i=12: 0d 00 00 0c | channel entry {num, band, pad, idx}; idx = num-1 ([I], vendor channel-copy helper) | engine CA 0x646AE: `ldrb ip,[r1,#0x441]` (band byte of entry i) |
| +0x1EC | **13** (u8) | channel count | engine CA 0x6462A: `ldrb r6,[r4,#0x4EC]` = VAP+0x300+0x1EC; also CA 0x63D24 / 0x63EDA / 0x64766 reads |
| +0x1ED | 0 | (pad after count) | - |
| +0x1EE | 60 (u16 LE: 3c 00) | dwell ms | engine CA 0x6477A: `ldrh r3,[r4,#0x4EE]` = payload+0x1EE |
| +0x1F0 | 0 | scan_type (4/8 rejected by engine CA 0x644B0/0x644E0) | engine CA 0x64600: `ldrb r3,[r4,#0x4F0]` |
| +0x1F1 | 1 | one | copied whole |
| +0x1F2 | 11 | vendor STA scan type | copied whole |
| +0x1F8 | 0 (u32) | tail | copied whole |

## What the firmware does with it

1. **Handler** CA 0x6494C: header -> chip lookup CA 0x961AE -> VAP-0 lookup
   CA 0x581C8 -> stores payload+0x13B into VAP+0x502 -> tail-calls the scan
   engine CA 0x644A4.
2. **Engine** CA 0x644A4: validates scan_type (payload+0x1F0), checks the
   VAP scan-in-progress byte (VAP+0x51C), sets it to 1, `memcpy_s` of the
   **0x1FC-byte request** into VAP+0x300 (CA 0x645A2-0x645B8), starts a timer
   (`movw #0x9c40` = 40000 at CA 0x64616), then enumerates the request:
   count = `[VAP+0x4EC]`, end pointer = VAP + count*4, one band byte per
   channel at VAP+0x440+4i+1 (CA 0x64624-0x646D0).  **With our request the
   count read is 13** - the wire field is not the reason only 6 were scanned.
3. **Per-channel scan**: CA 0x63CB8 takes ONE channel out of the request array
   (index = link[0xfc]+link[0xfd], entry at VAP+0x440+4*index) and starts a
   single-channel scan; the firmware even builds its own internal request with
   `count = 1`, `scan_type = 4`, `dwell = 20` (CA 0x4B88C) - i.e. the vendor
   architecture itself is "one channel per inner request".
4. **Result/next loop**: on a channel result (CA 0x642D0-0x64374) the firmware
   emits g1 id17 (slot = link[0xfc]+link[0xfd], 108-B record from ctx+0x1688)
   and advances: `step = link[0xfd]`, `end = link[0xfe]`, continue while
   step < end (CA 0x64298, next channel CA 0x643EE).  A hard-coded **6** is
   written into link[0x106] at CA 0x643CC when that field is zero.

See REPORT.md sec 3-4 for the ranked explanation of the 6-channel stop and the
bench discriminator for each candidate.
