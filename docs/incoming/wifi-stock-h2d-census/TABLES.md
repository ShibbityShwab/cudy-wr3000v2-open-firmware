# TABLES - the stock H2D capture, corrected census and byte-level decodes

Source: /tmp/h2d-stock.log (335,359 B, sha256 in [evidence/LOG-SHA256.txt](evidence/LOG-SHA256.txt);
1129 M records, 86.5 s, channel 0 only). Regenerate every number here with
python3 tools/decode_stock_census.py --input LOG [--aliases|--family G ID].
Grades: [V] re-derived here from the named artifact/bytes, [V*] committed earlier lane, [I]
inference, [U] unknown. Frames are decoded protocol bytes (MACs redacted to d4:0d:ab:XX:XX:XX).

## T1 - raw family census vs length-valid census

Validity rule (same as wifi-rx-h2d-trace H2D-FORMATS F6 + wifi-f1-from-capture sec. 1): a record is
a real send iff node len == frame total, 12 <= total <= 0x700, read == len, and u16@+10 == 0x5a5a.
All 1129 records pass tag@+10 (5a5a); **793/1129 (70.2%) fail len == total** and are torn.

| raw family | raw n | length-valid n | verdict |
| --- | ---: | ---: | --- |
| g0 id29 | 214 | **181** | EDCA config (hmac_edca_opt_timeout_fn) [V*] |
| g0 id1 | 243 | **142** | 26 distinct config-event keys [V] |
| g2 id29 | 382 | 0 | 16-B HCC timer class (torn) [V] |
| g4 id0 | 143 | 0 | D2H OAM/device log (torn) [V*] |
| g4 id4 | 93 | 0 | D2H heartbeat (torn) [V*] |
| g2 id0 | 12 | 0 | torn, config-event-shaped [U] |
| g1 id37 | 7 | 0 | torn D2H event class [I] |
| **g12 id29** | **7** | 0 | **torn 16-B HCC timer, stale head** [V] |
| g1 id3 | 6 | 0 | torn D2H class [I] |
| g0 id2 | 4 | **4** | alg request (hmac_config_alg_send_event) [V*] |
| **g12 id1** | **4** | 4 | **4 stale-head aliases, not a family** [V] |
| g1 id18 | 3 | 0 | torn D2H class [I] |
| g0 id32 | 2 | **2** | app IE (hmac_config_h2d_send_app_ie) [V] |
| g1 id35 | 2 | 0 | torn D2H class [I] |
| g0 id12 | 1 | **1** | scan request (hmac_enc_scan_send_req) [V*] |
| g0 id22 | 1 | **1** | channel sync (hmac_chan_do_sync) [V] |
| g0 id36 | 1 | **1** | BSS-color bitmap (hmac_scan_config_bss_color) [V] |
| g8 id0 | 1 | 0 | torn, over-read only [U] |
| g2 id9 | 1 | 0 | torn D2H log class [I] |
| g2 id37 | 1 | 0 | torn D2H event class [I] |
| g1 id38 | 1 | 0 | torn D2H class [I] |

Totals: raw 1129 = **valid 332 + alias 4 + torn 793**; span 86.5 s; record-rate peaks at 6-14 s
(bring-up), 64-67 s (restart churn) and 76.2 s (re-post burst).

## T2 - the 7 length-valid H2D families (332 records)

| family | n | frame geometry (protocol bytes as text) | builder [grade] | firmware receive handler [V*] |
| --- | ---: | --- | --- | --- |
| g0 id29 | 181 | 72 B: 00 01 00 <chip> 48 00 1d 00 00 00 5a5a + 20x00 + 40xff | hmac_edca_opt_timeout_fn [V*] | g0 id 0x1d -> file 0x7568d |
| g0 id1 | 142 | 12-B header + 8-B preamble + u16 key @+20 + u16 dlen @+22 + data + 4-B tail; total = 28+dlen | hmac_config_send_event [V*] | g0 id 1 -> file 0x03d59 |
| g0 id2 | 4 | 39 B: token word, key 0x0101, subkey 0x0b, 15-B data | hmac_config_alg_send_event [V*] | g0 id 2 -> file 0x96069 |
| g0 id32 | 2 | 23 B = header + 11-B payload (alloc(dlen+11), dlen=0) | hmac_config_h2d_send_app_ie [V] | g0 id 0x20 -> file 0x45631 |
| g0 id36 | 1 | 20 B = header + 8-B bitmap payload | hmac_scan_config_bss_color [V] | g0 id 0x24 -> file 0x755e1 (reads payload+0[W]) |
| g0 id12 | 1 | 520 B = header + 508-B scan request | hmac_enc_scan_send_req [V*] | g0 id 0x0c -> file 0x6494d |
| g0 id22 | 1 | 52 B = header + 40-B channel-sync struct | hmac_chan_do_sync [V] | g0 id 0x16 -> file 0x22d89 |

Timing [V]: bring-up cluster t=0.5..14.5 s (id32 1.6 s, id36 7.3 s, id2 8.2..14.4 s, id12
10.2 s, id22 13.2 s, config keys throughout); a second config re-post cluster at t=76.2 s
(13 records, chips 9/10/11, incl. key 0x0065 VAP start dvap=1). The g12/timer churn sits at
t=64.3..66.3 s.

## T3 - the stock config-event keys (g0 id1, 142 valid records, 26 keys)

0x00d8=39, 0x0073=11, 0x0069=9, 0x07e8=9, **0x0127=8 (set_beacon)**, 0x01cf=6, 0x0052=6,
0x07d9=5, 0x0124=5, 0x000b=5, 0x0000=4, 0x0005=4, 0x015d=4, **0x0065=4 (VAP start, dvap
0/0/0/1)**, 0x003c=4, 0x07e7=3, 0x03e4=3, 0x00f7=3, 0x011e=2, 0x0078=2,
0x0079/0x02bd/0x02c2/0x00ec/0x006f/0x00e8=1 each.
Chips seen: 0,1,3,4,5,6,7,8,9,10,11; bid byte always 0x01 (dst nibble 0).
The set_beacon posts are at t = 11.9, 13.1, 13.3, 40.2, 59.9, 65.9, 67.1, 67.3 s (chips 7,8,10,11).
Our driver's documented send set (f1 lane sec. 6) is the subset {key 0x00d8, key 0x0065, direct
ids 0x0006/0x0007/0x0008, WAL g2 cmds}; the rest of T2/T3 is stock-only traffic, not RX enables.

## T4 - the alias table (4 records; valid length, stale head) [V]

| alias | n | len | stale head | folds onto | identical after the 4 head bytes |
| --- | ---: | ---: | --- | --- | --- |
| g12 id1 | 1 | 48 | 0c 18 30 00 | g0 id1 key 0x00d8 (TXQ/AMSDU MIB config) | **44/44** (diff offsets: none) |
| g12 id1 | 1 | 184 | 0c 18 30 00 | g0 id1 key 0x0127 (set_beacon) | 174/180 (stale preamble bytes 12-19) |
| g12 id1 | 1 | 184 | 0c 18 30 00 | g0 id1 key 0x0127 | 172/180 (stale preamble: host pointer bytes) |
| g12 id1 | 1 | 184 | 0c 18 30 00 | g0 id1 key 0x0127 | **180/180** (only the 4 head bytes differ) |

The 0c 18 30 00 prefix is never a valid head: the low nibble (0x0c) is outside the observed H2D
group space (0 and 2) and no firmware table registers group 12. Same conclusion as the f1 lane's
"stale first 4 bytes" rule [V*], now with the set_beacon/0x00d8 attribution confirmed here.

## T5 - the torn 16-B HCC-timer class (389 records; the "g12" mystery) [V]

| head variant | n | note |
| --- | ---: | --- |
| 02 01 00 00 | 381 | canonical, all g2 id29 records |
| **0c 18 30 00** | **7** | the 7 "g12 id29" records (torn head; t=64.3/65.8/66.3 s) |
| 02 01 30 00 | 1 | torn vap byte |

Content behind every head (bytes 4..15, identical across variants):
10 00 | 1d 00 | 00 00 | 5a 5a | <4-B host pointer> -> total 16, id 0x1d, tag, payload = a host
kernel pointer (c0ddbcbf -> 0xbfbcddc0 in the g12 read; other reads 580ec0bf, 407150c4, ...).
This is plat.ko hcc_timer_timeout_proc (f1 lane [V]: hcc_msg_alloc(4,0), hdr[0]=2, hdr[3]=0,
hdr[6]=0x1d, hdr[12]=r4). The firmware group-2 table has **no id 0x1d handler**
(wifi-h2d-catalog) and the group-0 id 0x1d handler expects a 60-B EDCA payload - so this class is
dropped device-side. **Do not fire** (pointer payload).

## T6 - byte-level decode of the new families [V]

g0 id22 - hmac_chan_do_sync @wifi.ko file 0x47c58 (this lane): hcc_msg_alloc(40) -> total 52;
hdr[6]=0x16; hdr[1] = (ctx[+2]<<4) | stale; hdr[2]=ctx[+1]; hdr[3]=ctx[+0]; payload = the caller's
40-B channel-sync struct (payload[0] is passed to mac_get_channel_idx_from_num and stored at
ctx[+0x10]; payload[2] -> ctx[+0x12]). Captured frame (t=13.2 s, chip 10):
40 01 00 0a | 34 00 | 16 00 | 00 00 | 5a 5a + 24 01 07 00 | 00*8 | 00 07 00 00 | 00*20 |
01 00 00 00. payload[0]=0x24=**36** -> a 5 GHz channel (UNII-1) [I]; payload[2]=0x07.

g0 id32 - hmac_config_h2d_send_app_ie @0x600cc: hcc_msg_alloc(dlen+11) -> total dlen+23;
hdr[6]=0x20; payload = the caller struct, first u32 = IE length. Captured (t=1.6 s, chip 4, x2):
00 01 00 04 | 17 00 | 20 00 | 00 00 | 5a 5a | 00 00 00 00 | 04 00 00 00 | 00 00 00
-> dlen 0, no IE bytes, flag word 0x00000004.

g0 id36 - hmac_scan_config_bss_color @0x99c10: hcc_msg_alloc(8) -> total 20; hdr[6]=0x24;
hdr[2]=ctx[+0xfa9], hdr[3]=ctx[+0xfa8]; payload = a 64-bit mask ORed from entry[0x85] over a list
(+ a list counter increment). Captured (t=7.3 s, chip 7):
00 01 00 07 | 14 00 | 24 00 | 00 00 | 5a 5a | 09 00 01 02 00 00 00 04
-> words 0x02010009 / 0x04000000, set bits {0,3,24,25,58} [I: per-entry indices].

g0 id12 - scan request, 520 B. Captured payload offsets: +16 d4 0d ab XX XX XX (radio MAC,
redacted) + 00 01 ...; +0x140 channel list of (u16 code, u16 index) entries:
0x0124,0x0128,0x012c,0x0130,0x0134,0x0138,0x013c,0x0140 then 0x0164,0x0168,0x016c,0x0170,
0x0174,0x0178,0x017c,0x0180,0x0184,0x0188,0x018c,0x0190,0x0195,0x0199,0x019d,0x01a1,0x01a5
[V decode of the pattern]: u16 = (band<<8)|channel -> band 1 (5 GHz), channels
36,40,44,48,52,56,60,64,100,104,...,140,144,149,153,157,161,165. A 5 GHz scan request.

## T7 - the IEEE 1905.1 fragment in the sampler over-read (171 records) [V]

Layout of the 72-B timer reads (total 16, over-read 56 B): bytes 0-15 = the 16-B message; bytes
16-31 = 16x00; bytes 32-49 = 18xff; bytes **50-71** = 22-B Ethernet header prefix:

01 80 c2 00 00 13 | d4 0d ab XX XX XX | 89 3a | 00 00 00 00 00 34 00 80

* DA 01:80:c2:00:00:13 = the IEEE 1905.1 group MAC (IEEE Std 1905.1; "transmission of IEEE
  1905.1 control packets", IEEE group-MAC registry);
* EtherType 0x893a = IEEE 1905.1 (Multi-AP / EasyMesh CMDU);
* SA = the device MAC (redacted). The last 10 B vary between reads (... 00 34 00 80 vs
  ... 00 3e 00 80) -> different CMDU message IDs.
* The fragment starts at exactly len-22 / over-read offset 50 in every carrier, i.e. it is
  **adjacent heap content read past the message allocation**, not message payload. The stock
  firmware/host stack therefore runs IEEE 1905.1 control traffic (Multi-AP) [I]; it is not an
  H2D message and must not be fired as one.

## T8 - torn/D2H residue classes (793 records)

| class | n | decode |
| --- | ---: | --- |
| g2 id29 timer | 382 | T5; canonical head 02 01 00 00 |
| g4 id0 D2H OAM | 143 | plat.ko chip-4 device log; head 04 01 00 00, 0x7e-prefixed records [V*] |
| g0 id1 torn/over-read | 101 | 40 carry a complete config geometry (key/dlen) behind a stale node - real config sends; 61 are partial/over-read (e.g. len 72 / total 12 + junk tail) |
| g4 id4 D2H heartbeat | 93 | 22-B HEARTBEAT\0 builder, plat.ko chip 4 [V*] |
| g0 id29 torn | 33 | EDCA frames behind stale node lengths |
| g2 id0 | 12 | [U] config-event-shaped (e.g. key 0x000b dlen 0xa0 -> total 188) but (g2,id 0) has no host builder and no firmware handler -> torn, not a send |
| g1 id37/id3/id18/id35/id38, g2 id9/id37 | 22 | D2H event/log class: group byte 1/2 = wifi.ko chip id; no H2D builder with this (group,id) geometry, content is device data (counters, "vapN" strings, ff tails) [I] (g1 id3 D2H already [V*]) |
| g8 id0 | 1 | [U] head 08 00 08 00 invalid in both spaces; content total 128/id 0 + 4-B host pointer; over-read artifact, not a trustworthy send |
