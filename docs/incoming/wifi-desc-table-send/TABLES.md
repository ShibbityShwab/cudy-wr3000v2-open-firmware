# TABLES - lane wifi-desc-table-send (task-188)

Grades: **[V]** decoded/re-derived by a committed lane (cited); **[V*]** measured
by a committed lane/Lead; **[I]** inference; **[U]** needs a device read. This
lane is HOST-ONLY: it wrote nothing to the router, module or firmware. No vendor
binary, firmware byte, calibration value, MAC or secret appears here - the only
bytes are **host-built protocol bytes** (our own 0x00F7 frame), as in the
sibling lanes.

## T1. Conventions and safety

| item | value |
| --- | --- |
| firmware image (source of the decode) | recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin, 928,920 B, sha256 `7fc87e20...c3ef311b` |
| firmware CPU address | **CPU = file offset + 0x40000** |
| host BAR0 offset | **BAR0 = CPU + 0x6B8000** |
| host physical (read path) | **phys = 0x40000000 + CPU + 0x6B8000 = CPU + 0x46B8000** |
| forbidden | never read the RC misc window 0x10160000..0x1016ffff; never write CA 0x400392F0 |
| this lane | host-only implementation + build + host tests; no bench contact |

## T2. The H2D 0x00F7 wire frame (68 B) [V]

HCC **group 0, message id 1**; key **0x00F7** at header + 20, payload length u16
at +22, descriptor blob at +24; total = len + 28 = **68** (the 4-byte
`FF FF FF FF` tail is the inherited pad every working g0-id1 frame carries).

| off | len | field | value |
| ---: | ---: | --- | --- |
| +0x00 | 1 | group | 0x00 |
| +0x01 | 1 | bid: alloc nibble 1, dst high nibble | 0x01 |
| +0x02 | 1 | fa | 0x00 |
| +0x03 | 1 | fb | **0x04** |
| +0x04 | 2 | total length u16 | **0x0044** |
| +0x06 | 2 | message id u16 | **0x0001** |
| +0x08 | 1 | fc | 0x00 |
| +0x09 | 1 | dst | 0x00 |
| +0x0A | 2 | tag | **0x5A5A** |
| +0x0C | 8 | token/preamble | `00 00 00 00 01 00 00 00` |
| +0x14 | 2 | **config key u16** | **0x00F7** |
| +0x16 | 2 | **payload length u16** | **0x0028** (8 + 2*16) |
| +0x18 | 40 | descriptor blob (T3) | see T3 |
| +0x40 | 4 | inherited tail | `FF FF FF FF` |

Frame (68 B) built by `src/hi5622_desc_table.c` (also emitted by
`tools/fw_desc_table_re.py --frame` in ../wifi-fw-desc-table):

```
000100044400010000005a5a 0000000001000000 f700 2800
5553000002000000
000080001e002c0100000000ff1f0000
010280001e002c0100000000ff000000
ffffffff
```

SR node word1 = `(total << 16) | 0x6D2B` = **0x00446D2B**; the existing
`hi5622_frame_post()` machinery derives it, no new transport code.

## T3. Descriptor blob (the pool image for N = 2) [V]

The firmware worker memcpy_s's `8 + body[4]*16` bytes into the runtime pool
CA 0x1ADC98 (destMax 0x148 = 8+20*16).  Blob = 8-byte header + N*16 records:

| off | len | field | fix value |
| ---: | ---: | --- | --- |
| +0x00 | 2 | country code cc[0..1] | **"US"** - must differ from the boot pool's "99" (install gate is an ARM strcmp dedupe) |
| +0x02 | 1 | NUL (terminates the cc string) | 0x00 |
| +0x03 | 1 | chip / regdomain id | 0x00 |
| +0x04 | 1 | **record count N** | **0x02** |
| +0x05 | 3 | zero | 0 |
| +0x08 | 16*N | records (below) | |

Record i at blob + 8 + 16*i:

| rec off | len | field | rec0 (2.4G) | rec1 (5G) |
| ---: | ---: | --- | --- | --- |
| +0x00 | 4 | word: byte0 band (0=2.4G, 1=5G), byte1 bw code (2=20, 4=40, 5=80), **byte2 bit7 = 0x00800000 armed/valid** | **0x00800000** | **0x00800201** |
| +0x04 | 1 | plan field (vendor 3000/100) | 30 | 30 |
| +0x05 | 1 | zero | 0 | 0 |
| +0x06 | 2 | plan field (vendor 3000/10) | 300 | 300 |
| +0x08 | 4 | zero | 0 | 0 |
| +0x0C | 4 | **channel bitmap** (bit b = map row b; 2.4G 14 rows, 5G 29 rows) | 0x00001FFF (ch1..13) | **0x000000FF** (ch36..64 = rows 0..7) |

Limits: N <= 20; no CRC/length/version inside the blob. ch40 (5200 MHz) is
5G map row 1, inside the 0xFF bitmap.

## T4. The fail-closed `desc_table` matrix (implementation contract) [V]

| desc_table | beacon_static | band | result |
| ---: | ---: | ---: | --- |
| 0 (default) | any | any | **no 0xF7 frame**; ladder byte-identical to the r1-band base |
| 1 | 1 | 1 | **post the 68-B frame** after the VAP base, before ap_chan |
| 1 | 1 | 0 | clean no-post (band-1 AP start only) |
| 1 | 0 | any | **refused, -EINVAL, before any frame** ("needs the beacon_static=1 vendor order") |
| >1 | any | any | **refused, -EINVAL, before any frame** ("not a valid descriptor-table parameter") |

Builder refusals (`hi5622_desc_table_build`): cc "99" / empty / non-printable
-> -EINVAL (the "99" table would be silently skipped by the firmware gate);
record count != 2 -> -EINVAL; unarmed plan -> -EINVAL; cap < 68 -> -ENOSPC;
NULL -> -EINVAL. `hi5622_desc_table_plan` also refuses band > 1 and a NULL out.

## T5. Integration and order contract [V]

| item | location | contract |
| --- | --- | --- |
| module param | hi5622_scan.c `desc_table` (uint, 0444, default **0**) | one meaning per load; read-only |
| fail-closed gate | start_ap, before any frame (after the band5g channel gate) | >1 and 1-without-beacon_static refused |
| plan step | hi5622_scan_logic.{c,h} `HI5622_BEACON_STEP_DESC_TABLE` | plan order: vap_add -> **desc_table** -> ap_chan -> ap_ssid -> ap_cfg -> ap_vap_start |
| frame post | start_ap beacon_static branch, after the VAP-object readback, **before** the ap_chan build/post | `hi5622_frame_post(ctx, wire, len, "desc_table", &out0)` |
| read-only evidence | right after the post | module ctx + band object (ctx+0x4B8) dmesg line |
| sysfs | `hi5622_scan_status` -> `cfg80211_status` | `desc_table: param=N (mode) beacon_static=M last_rc=R posted=P refused=F` |

Why that placement: the 0x00F7 worker (CA 0x45A30) needs a live module object
(the VAP base, 0x0064, binds the per-link/band object at ctx+0x4B0/0x4B8), and
the 0x0124 channel worker's band-1 RF programme (CA 0x40684 -> predicate
CA 0x101890) evaluates the armed record **at 0x0124 post time** - so the table
must be installed before ap_chan.  The current beacon_static ladder posts the
VAP start (0x0065) **last** (vendor order: after the AP config), so the
observable order in this ladder is
**vap_add 0x0064 -> [deferred user_add] -> desc_table 0xF7 -> ap_chan 0x0124 ->
ap_ssid / ap_cfg 0x0127 -> ap_vap_start 0x0065**; the work the 0xF7 worker
needs from 0x0065 (a module object) already exists after 0x0064 [V*].

## T6. Build artifact and pins

| item | value |
| --- | --- |
| pinned base tree | /tmp/r1-band-build = wifi-5ghz-beacon tree + r1-band one-field fix; base provider sha256 `c306e75f...8b79ab` |
| pinned base module (CW-0/R1) | /tmp/r1-band-artifact/hi5622_wifi.ko, **356,612 B** sha256 `e8b17f72...4abd4`, 59 params |
| this lane patch | patches/0001-desc-table-send.patch (601 lines, 6 files) |
| built module | /tmp/desctable-artifact/hi5622_wifi.ko, **360,448 B** sha256 `aac6177c...e800e3`, 60 params (`desc_table` added) |
| unstripped / .text | 1,370,144 B; .text sha256 `6cf0cabd...b24149`; strip keeps .text identical |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| host tests | `tools/run-host-tests.sh`: 63 model + 13 ordering + 32 cross-check checks, 0 failures |

## T7. Read-only post-fire probe pack (the Lead) [V*]

All reads are host physical addresses; `phys = CPU + 0x46B8000`.

| # | object | CPU | phys | len | pass value |
| ---: | --- | ---: | ---: | ---: | --- |
| 1 | pool header cc + count | 0x1ADC98 | 0x40865C98 | 8 | **"US"** (55 53), count **02** |
| 2 | pool rec1 word | **0x1ADCB0** | **0x40865CB0** | 4 | **0x00800201** (bit23/0x800000 set) |
| 3 | pool rec1 bitmap | 0x1ADCBC | 0x40865CBC | 4 | **0x000000FF** (ch36..64) |
| 4 | band-1 map ch36 row (idx 0) | 0x117AD4 | 0x407CFAD4 | 4 | byte1 = **01** (was 0xFF before any 0xF7) |
| 5 | band-0 map ch1 row (idx 0) | 0x117B48 | 0x407CFB48 | 4 | byte1 = **00** (2.4G unchanged) |
| 6 | O = P+0x1A00 | 0x178238 | 0x40830238 | 4 | **nonzero** |
| 6a | P+0xF04 mode byte | 0x17773C | 0x4082F73C | 4 | **nonzero** (byte 3) |
| 7 | R+0x410 | via chain | - | 4 | **bit0 = 1** (the 0x101890 predicate saw the armed record) |
| 8 | R+0x3AC | via chain | - | 4 | **0x03E6138A** (ch36..64 RF image) |
| 9 | on-air beacon | - | - | - | 5200 MHz ch40 SSID cudy-ap-test-5g40 |

Pointer chain (read-only): `ctx0 = u32(0x171458) + u16(0x172504)`;
`perlink = u32(ctx0+0x4B0)`; `P = perlink+0xE20`; `O = u32(P+0x1A00)`;
`R = u32(O+0x2D8)`.  The dmesg line printed right after the post
("desc_table post: module ctx=... bandobject=...") gives the same chain's
ctx/band-object values from the live driver, no probe needed.

Before the fix (r1-band module, no 0xF7): pool cc "99", rec1 word
0x00000201 (valid clear), bitmap 0x1FFFFFFF, band-1 map rows 0xFF
(BAR0 0x7CFAD4 byte1 = 0xFF; earlier rf-lane T7 row 20 misprinted the BAR0 as
0x872AD4 - the desc-pool lane corrected it).

## T8. dmesg expectations [V]

With `beacon_static=1 ... desc_table=1` on a band-1 AP:

```
cfg80211: start_ap beacon_static=1 plan (6 steps): vap_add desc_table ap_chan ap_ssid ap_cfg ap_vap_start
frame post 'vap_add': ...
cfg80211: start_ap beacon_static: VAP-object readback=...
frame post 'desc_table': g0 id1 total 68 B in SR0 slot ... handler risk=PTR_DEREF; posted N/32
cfg80211: start_ap beacon_static frame 1/6 'desc_table' posted (68 B, g0 id1 key 0x00F7, cc=US, 2 records, band1 word=0x00800201 armed, 5G bitmap=0x000000ff) - before the 0x0124 channel post
cfg80211: desc_table post: module ctx=0x........ bandobject=0x........ (the 0x00F7 worker's band-object test can pass)
cfg80211: start_ap beacon_static frame 2/6 'ap_chan' posted (... band=1 channel=36/40 ...)
```

Note the posted counter does not count the structural VAP-add helper frame
(same as the base ladder, where ap_chan was frame 1/5): the plan index of a
step is posted+1 for the plan including vap_add.  With `desc_table=0` the
line is `frame 1/5 'ap_chan'` again (no desc_table step).

`desc_table=0` (default) has no 0xF7 line; the plan line reads
`(5 steps): vap_add ap_chan ap_ssid ap_cfg ap_vap_start`.

Failure/gate lines:

```
cfg80211: start_ap REFUSED - desc_table=2 is not a valid descriptor-table parameter (0=off, 1=post; no frame sent
cfg80211: start_ap REFUSED - desc_table=1 needs the beacon_static=1 vendor order (the 0x00F7 step exists only in that AP-start ladder); no frame sent
cfg80211: start_ap beacon_static: descriptor-table builder refused (%d) - sequence aborted, no frame sent
cfg80211: start_ap beacon_static: descriptor-table frame (g0 id1 key 0x00F7) not posted (%d) - sequence aborted
```

sysfs `cat $D/cfg80211_status` includes:
`desc_table: param=1 (band1-post) beacon_static=1 last_rc=0 posted=1 refused=0`.

## T9. Decision tree after the fire

```
dmesg: 'desc_table' posted + frame post consumed
read 1,2,3: pool cc "US" / rec1 0x00800201 / bitmap 0xFF
  header still "99"  -> install skipped: cc collision (impossible with "US") or
                        the frame body was misplaced -> capture frame bytes, R3a
  cc changed, bit23 clear -> truncated/misplaced frame -> R3b
read 4: band-1 row byte1 == 01 -> populator ran
  still 0xFF -> populator did not run (worker early-out) -> read band object
                dmesg line (0 = the worker returned 0x64 after the install)
read 6/7: R+0x410 bit0 == 1 -> predicate saw the armed record
  read 8: R+0x3AC == 0x03E6138A and a ch40 beacon -> C1 CLOSED
  R+0x3AC stale -> the armed bit is not the RF-image gate -> hand back to the
                   RF lane (C1 re-ranked)
  bit0 == 0 with row=01 and word bit23 set -> the CA 0x40684 branch gates
     (ctx[4]!=2 or gate bytes != 1/1/1) -> sibling gate probes
```
