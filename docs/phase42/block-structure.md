# Block structure of the normal-op BAR0 windows (phase 42, 2026-10-04)

Static, read-only map of the three healthy-device snapshots. No device access; nothing outside this
file was written.

**Sources (read verbatim; every citation below is `<address> = <value>`, line N of the named file):**

| key | file | window | lines |
| --- | --- | --- | --- |
| msg | `build/tmp/msg-normal-op-20261004.txt` | BAR0 `0x3f1000..0x3f1fff` | 1025 (1 comment + 1024 words) |
| ete | `build/tmp/ete-normal-op-20261004.txt` | BAR0 `0x3f2000..0x3f2fff` | 1025 (1 comment + 1024 words) |
| ete2 | `build/tmp/ete2-normal-op-20261004.txt` | BAR0 `0x3f3000..0x3f3fff` | 1025 (1 comment + 1024 words) |

Layout of a file: line 1 is a comment; lines 2..1025 are the 1024 words in ascending address order,
`<bare-hex-address> = 0x<word>`. For window base `B`, the word for address `A` is on line
`2 + (A-B)/4` (e.g. `msg` line 2 is `3f1000 = 0x00000000`, line 1025 is `3f1ffc = 0x504E502E`).

**Discrepancy with the task's OBSERVED FACTS list (recorded, not resolved here).** The task's list
names the marker word of five blocks as 0x33016000 (0x3f1004), 0x29016000 (0x3f1204), 0x53016000
(0x3f1804), 0x1D016000 (0x3f1a04) and 0x79016000 (0x3f1e04). None of the five named addresses
carries that value in the files. Of the five values themselves, 0x29016000 and 0x1D016000 occur
nowhere in the three files; 0x33016000 occurs once (ete line 643 at 0x3f2a04), 0x53016000 once
(ete line 131 at 0x3f2204) and 0x79016000 nine times - never at a task-named address.
The files actually read: msg line 3 `3f1004 = 0x07016000`, msg line 131
`3f1204 = 0x4B016000`, msg line 515 `3f1804 = 0xEF016000`, msg line 643 `3f1a04 = 0xBF016000`,
msg line 771 `3f1c04 = 0x79016000`. The task's five values are verbatim from
`docs/phase41/d2h-window-resident-ring.md` (the ~19:55 capture), so the task list was carried over
from that earlier capture, not from these three files. This report cites the files. It also means
the marker word *changed between the 19:55 capture and the 20:00 snapshots* - see section 3.

---

## 1. Window geometry: 24 slots of 0x200

The three windows are contiguous and each is 0x1000 bytes, so the captured span
`0x3f1000..0x3f3fff` (3072 words) divides evenly into **24 blocks of 0x200 bytes (512 B)**:

```
msg  : 0x3f1000 0x3f1200 0x3f1400 0x3f1600 0x3f1800 0x3f1a00 0x3f1c00 0x3f1e00
ete  : 0x3f2000 0x3f2200 0x3f2400 0x3f2600 0x3f2800 0x3f2a00 0x3f2c00 0x3f2e00
ete2 : 0x3f3000 0x3f3200 0x3f3400 0x3f3600 0x3f3800 0x3f3a00 0x3f3c00 0x3f3e00
```

The 0x200 stride is exact: every block start is `B + k*0x200`, `k = 0..7`, in each window
(`0x3f1000 = 0x00000000` msg line 2; `0x3f1200` msg line 130; ... `0x3f3e00` ete2 line 898).

## 2. Which offsets are head words vs payload

Every block that *starts a record* carries a **16-byte head occupying `+0x000..+0x00F` (four
32-bit words)**; the payload (a raw L2 frame) begins at **`+0x010`**. Offsets `+0x000..+0x00F` are
never frame bytes: in all 17 record-starting blocks `+0x010` holds the frame's destination MAC and
`+0x00c` is the constant `0x00100000`.

Full block grid (HEAD = `+0x000 == 0x00000000` and the low 20 bits of `+0x004` equal `0x16000`):

| block | window | slot# | `+0x000` | ln | `+0x004` | ln | `+0x008` | ln | `+0x00c` | ln | kind |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0x3f1000 | msg | 0 | 0x00000000 | 2 | 0x07016000 | 3 | 0x00211039 | 4 | 0x00100000 | 5 | HEAD |
| 0x3f1200 | msg | 1 | 0x00000000 | 130 | 0x4B016000 | 131 | 0x00207039 | 132 | 0x00100000 | 133 | HEAD |
| 0x3f1400 | msg | 2 | 0x4F52473A | 258 | 0x504E502E | 259 | 0x370D0A0D | 260 | 0x20313333 | 261 | payload |
| 0x3f1600 | msg | 3 | 0x0A0D0A33 | 386 | 0x3333370D | 387 | 0x0009D001 | 388 | 0x02AC0000 | 389 | payload |
| 0x3f1800 | msg | 4 | 0x00000000 | 514 | 0xEF016000 | 515 | 0x001C9039 | 516 | 0x00100000 | 517 | HEAD |
| 0x3f1a00 | msg | 5 | 0x00000000 | 642 | 0xBF016000 | 643 | 0x00209039 | 644 | 0x00100000 | 645 | HEAD |
| 0x3f1c00 | msg | 6 | 0x00000000 | 770 | 0x79016000 | 771 | 0x00052039 | 772 | 0x00100000 | 773 | HEAD |
| 0x3f1e00 | msg | 7 | 0x370D0A0D | 898 | 0x20313333 | 899 | 0x33370D0A | 900 | 0x0A203133 | 901 | payload |
| 0x3f2000 | ete | 0 | 0x00000000 | 2 | 0x79016000 | 3 | 0x00052039 | 4 | 0x00100000 | 5 | HEAD |
| 0x3f2200 | ete | 1 | 0x00000000 | 130 | 0x53016000 | 131 | 0x00215039 | 132 | 0x00100000 | 133 | HEAD |
| 0x3f2400 | ete | 2 | 0x00000000 | 258 | 0x79016000 | 259 | 0x00052039 | 260 | 0x00100000 | 261 | HEAD |
| 0x3f2600 | ete | 3 | 0x504E502E | 386 | 0x49442E55 | 387 | 0x20313333 | 388 | 0x4F52473A | 389 | payload |
| 0x3f2800 | ete | 4 | 0x00000000 | 514 | 0x79016000 | 515 | 0x00052039 | 516 | 0x00100000 | 517 | HEAD |
| 0x3f2a00 | ete | 5 | 0x00000000 | 642 | 0x33016000 | 643 | 0x00219039 | 644 | 0x00100000 | 645 | HEAD |
| 0x3f2c00 | ete | 6 | 0x00000000 | 770 | 0x79016000 | 771 | 0x00052039 | 772 | 0x00100000 | 773 | HEAD |
| 0x3f2e00 | ete | 7 | 0x49442E55 | 898 | 0x4E464947 | 899 | 0x4F52473A | 900 | 0x504E502E | 901 | payload |
| 0x3f3000 | ete2 | 0 | 0x00000000 | 2 | 0x79016000 | 3 | 0x00052039 | 4 | 0x00100000 | 5 | HEAD |
| 0x3f3200 | ete2 | 1 | 0x00000000 | 130 | 0xBF016000 | 131 | 0x00209039 | 132 | 0x00100000 | 133 | HEAD |
| 0x3f3400 | ete2 | 2 | 0x00000000 | 258 | 0x79016000 | 259 | 0x00052039 | 260 | 0x00100000 | 261 | HEAD |
| 0x3f3600 | ete2 | 3 | 0x370D0A0D | 386 | 0x20313333 | 387 | 0x33370D0A | 388 | 0x0A203133 | 389 | payload |
| 0x3f3800 | ete2 | 4 | 0x00000000 | 514 | 0x79016000 | 515 | 0x00052039 | 516 | 0x00100000 | 517 | HEAD |
| 0x3f3a00 | ete2 | 5 | 0x00000000 | 642 | 0xBF016000 | 643 | 0x00209039 | 644 | 0x00100000 | 645 | HEAD |
| 0x3f3c00 | ete2 | 6 | 0x00000000 | 770 | 0x79016000 | 771 | 0x00052039 | 772 | 0x00100000 | 773 | HEAD |
| 0x3f3e00 | ete2 | 7 | 0x370D0A0D | 898 | 0x20313333 | 899 | 0x33370D0A | 900 | 0x0A203133 | 901 | payload |

Counts: **24 blocks, 17 HEAD, 7 payload-continuation** (`0x3f1400`, `0x3f1600`, `0x3f1e00`,
`0x3f2600`, `0x3f2e00`, `0x3f3600`, `0x3f3e00`).

## 3. The head fields, and the `0x??016000` word in particular

The head is four words; two are constant and two vary together:

| offset | field | values in this capture |
| --- | --- | --- |
| `+0x000` | constant zero | `0x00000000` in 17/17 heads (msg lines 2/130/514/642/770; ete 2/130/258/514/642/770; ete2 2/130/258/514/642/770) |
| `+0x004` | marker/tag word | `0x07016000`, `0x4B016000`, `0xEF016000`, `0xBF016000`, `0x79016000`, `0x53016000`, `0x33016000` |
| `+0x008` | marker companion | `0x00211039`, `0x00207039`, `0x001C9039`, `0x00209039`, `0x00052039`, `0x00215039`, `0x00219039` |
| `+0x00c` | constant | `0x00100000` in 17/17 heads |

**Byte fields of the `0x??016000` word** (`+0x004`). As a little-endian word, value `0x??016000`:

* bits 31..24 = `??` - the **only** varying byte, values `0x07, 0x33, 0x4B, 0x53, 0x79, 0xBF, 0xEF`;
* bits 23..16 = `0x01` (fixed);
* bits 15..0  = `0x6000` (fixed). Equivalently its low 20 bits are the constant `0x16000`.

As memory bytes at `+0x004..+0x007` (little-endian): `00 60 01 ??`, i.e. `[+4]=0x00`, `[+5]=0x60`,
`[+6]=0x01`, `[+7]=??`. So the only byte that moves is the one at the highest address of the word,
`+0x007`.

**Is it a counter?** In these three snapshots the value is *not* monotonic in address order, and it
*repeats*: `0x79016000` occurs 9 times (msg line 771; ete lines 3/259/515/771; ete2 lines 3/259/515/771)
and `0xBF016000` 3 times (msg line 643; ete2 lines 131/643). All 9 `0x79016000` records carry the same
companion `0x00052039` and the same payload prefix (dst MAC `01:80:C2:00:00:13`, section 4), so on
this capture the word behaves as a *per-record-type tag*, not a monotonic counter. It does change
over time: the phase-41 record's 19:55 capture reports (from that doc, not from these files)
`0x33016000/0x29016000/0x53016000/0x1D016000` at the same `msg` offsets where these files read
`0x07016000/0x4B016000/0xEF016000/0xBF016000` (cf. the discrepancy note above). Reading the field as a time/sequence number is *not* established
here - only the position, the byte fields, and the observed values are.

## 4. Payload: start, byte order, typical content

**Start.** The frame begins at `+0x010`. For all 17 HEAD blocks the first six bytes at `+0x010` are a
multicast destination MAC:

* 8 blocks begin `01:00:5e:7f:ff:fa` (the SSDP destination MAC): msg `0x3f1000`, `0x3f1200`,
  `0x3f1800`, `0x3f1a00`; ete `0x3f2200`, `0x3f2a00`; ete2 `0x3f3200`, `0x3f3a00`. In the `msg`
  block the bytes come from `0x3f1010 = 0xFFFAD40D` (msg line 6) and `0x3f1014 = 0x01005E7F`
  (msg line 7).
* 9 blocks begin `01:80:c2:00:00:13` (IEEE 802.1 link-local): msg `0x3f1c00`; ete `0x3f2000`,
  `0x3f2400`, `0x3f2800`, `0x3f2c00`; ete2 `0x3f3000`, `0x3f3400`, `0x3f3800`, `0x3f3c00`. In the
  `ete` block these bytes come from `0x3f2010 = 0x0013D40D` (ete line 6) - i.e. the head word
  `0x0013D40D` decodes to `01 80 c2 00 00 13` once the byte order below is applied.

Then follow the source MAC, the EtherType (`0x0800` for the SSDP frames - `0x3f1018 = 0x08004500`,
msg line 8, carries EtherType `0800` + IPv4 `45`) and the IP/UDP header.

**Byte order (needed to read the payload).** The snapshot's 32-bit words are not in frame byte
order. The frame bytes are recovered by **reversing each aligned 8-byte (64-bit) group**: for a byte
at true frame offset `n` inside a window, its raw address is `base + (n - n%8) + (7 - n%8)`
(equivalently: per 32-bit word pair, emit the *high* word big-endian then the *low* word
big-endian; or: reverse the 8 bytes of every 8-byte group). Verified: applying this to all three
windows yields the complete SSDP strings `NOTIFY * HTTP/1.1`, `HOST: 239.255.255.250:1900`,
`CACHE-CONTROL`, `LOCATION`, `SERVER:`, `CONFIGID`, `ssdp:alive`, `UPnP/1.1`,
`googleusercontent` - none of which appear contiguously without it. Under this rule the head decodes
to `?? 01 60 00 | 00 00 00 00 | 00 10 00 00 | 00 ?? ?? 39`.

**Typical content.** Decoded with that rule, the payloads are raw L2 frames of SSDP/UPnP discovery
advertisements plus other live traffic:

* `msg` `0x3f1010..` (record at `0x3f1000`) - SSDP `NOTIFY * HTTP/1.1` from `192.168.10.1` to
  `239.255.255.250:1900`, `SERVER: Cudy/5.10.201 UPnP/1.1 MiniUPnPd/2.3.3`,
  `LOCATION: http://192.168.10.1:5000/rootDesc.xml`, `NT: urn:schemas-upnp-org:device:InternetGatewayDevice:2`,
  `USN: uuid:344b4f24-d0a5-4490-98d6-7834caa8b5c4::...`, `NTS: ssdp:alive`, `CONFIGID.UPNP.ORG: 1337`.
* `msg` `0x3f1210..`, `0x3f1810..`, `0x3f1a10..` - further SSDP advertisements advertising
  `service:WANIPConnection:2`, `upnp:rootdevice`, `service:DeviceProtection:1`.
* `ete` - the same SSDP advertisement set (`service:DeviceProtection:1`, `WANIPv6FirewallControl:1`,
  `Layer3Forwarding:1`, `WANCommonInterfaceConfig:1`, `WANIPConnection:2`) plus non-SSDP traffic:
  `accounts.google.com` (around `0x3f2600`), `pad.lan`, `play.google.com` (around `0x3f2e00`).
* `ete2` - the same SSDP set plus `googleusercontent.com` text around `0x3f3600`.
* The 9 `01:80:c2:00:00:13` records carry a link-layer control frame (source MAC
  `d4:0d:ab:64:1c:73`, EtherType `0x893a`) whose body embeds the ongoing SSDP text starting at
  `55.255.250:1900\r\nCACHE-CONTROL: max-age=60\r\nLOCATION: ...`.

The payload of the last record in each window runs to the window's final word
(`0x3f1ffc = 0x504E502E` msg line 1025; `0x3f2ffc = 0x0A203133` ete line 1025;
`0x3f3ffc = 0x502E4F52` ete2 line 1025).

## 5. Stride regularity and the gaps

The grid is exactly 0x200; there are no sub-0x200 or non-aligned heads. Every one of the 17 heads
sits at a block base `B + k*0x200`, and its marker word is the *second* word of the block
(`+0x004`), so the head pattern is fully described by which slot index carries a head:

| window | HEAD slot indices | payload-continuation slot indices |
| --- | --- | --- |
| msg | 0, 1, 4, 5, 6 | 2, 3, 7 |
| ete | 0, 1, 2, 4, 5, 6 | 3, 7 |
| ete2 | 0, 1, 2, 4, 5, 6 | 3, 7 |

Gap runs of continuation blocks: `msg` has a 2-block gap at slots 2-3 and a 1-block gap at slot 7;
`ete`/`ete2` have 1-block gaps at slot 3 and slot 7. Slot 7 is never a head in any window, and slots
0, 1, 4, 5, 6 always are (3/3 windows). `msg` is the only window where slot 2 is not a head.

The continuation blocks really do hold the middle of a record: their very first byte is text, not a
head, e.g. `0x3f1400 = 0x4F52473A` (msg line 258) decodes to `PNP.ORG: 1337`, the tail of the
`CONFIGID.UPNP.ORG: 1337\r\n\r\n` of the record that started at `0x3f1200`; `0x3f2600`
(ete line 386) decodes to `ID.UPNP.ORG: 1337`; and `0x3f2e00` (ete line 898) decodes to
`NFIGID.UPNP.ORG: 1337`.

## 6. One ring or several channels?

The evidence points to **three independent 0x1000 block buffers (channels), each with the same
internal record grid** - not one ring wrapping through the three windows:

1. Each window *begins* with a head at slot 0 (`0x3f1000 = 0x00000000` msg line 2;
   `0x3f2000 = 0x00000000` ete line 2; `0x3f3000 = 0x00000000` ete2 line 2), i.e. all three
   windows restart the grid - a single wrapped ring would not restart its head pattern at each
   0x1000 boundary.
2. Each window's last record stops at that window's final word (section 4), and the next window's
   first word is a fresh head word pair (`0x3f2004 = 0x79016000` ete line 3 vs
   `0x3f1c04 = 0x79016000` msg line 771) rather than a mid-record continuation.
3. The per-window slot pattern is essentially identical (section 5) - the signature of three
   copies of the same buffer layout.

Caveat, stated plainly: only a 12 KB contiguous slice is captured. If the device actually keeps one
larger ring and mirrors 0x1000-byte slices of it at these three BAR0 windows, the observations above
would look the same; the distinguishing datum would be the ring's write index / head registers,
which are not in these snapshots. Against the mirror hypothesis, the three windows hold *different*
SSDP advertisement sets (msg advertises `InternetGatewayDevice:2`; ete/ete2 advertise
`DeviceProtection:1`/`WANPPPConnection:1` etc.), which is what three separate capture buffers
accumulating the same broadcast traffic at different times would show.

## 7. What is *not* settled

* The meaning of the `??` byte of the `0x??016000` word and of the `+0x008` companion word (section
  3) is not derivable from static data - only their positions, byte fields and observed values are
  reported.
* Not every record boundary is clean. A record that starts at a head can be cut short by the *next*
  block's head: the record at `0x3f1000` ends mid-token - its last word `0x3f11fc = 0x4E464947`
  (msg line 129) decodes to `ID.U`, the `D.U` of `CONFIGID.U` at true offsets `0x1fe..0x1ff` - where
  the next head starts at `0x3f1200` (msg line 130). One continuation block does not join its
  preceding record: at msg `0x3f1600` (msg line 386) the decoded text restarts at `337\r\n\r\n`,
  whereas the preceding bytes (`0x3f15fc`, msg line 385) end at `...CO`; the missing
  `NFIGID.UPNP.ORG: 1` is not present. The other six continuation blocks do continue their record:
  `0x3f1400` decodes to `PNP.ORG: 1337` and `0x3f1e00`/`0x3f3e00` decode to ` 1337\r\n\r\n 1337`.
  The engine's exact split rule for frames larger than a block needs the descriptor fill code or a
  second capture.

## 8. Verification of the citations

Every `<address> = <value>` pair quoted above was re-checked against the named file: the 24-row block
grid, the 17 head words, the 7 continuation blocks, the marker values, the `+0x00c = 0x00100000`
constant, the `msg` SSDP MAC source words (`0x3f1010 = 0xFFFAD40D` line 6, `0x3f1014 = 0x01005E7F`
line 7), the `ete` link-local MAC source word (`0x3f2010 = 0x0013D40D` line 6) and the three
line-1025 window-end words. The decoded ASCII in section 4 is *derived* from those words with the
8-byte-group reversal rule stated there; the rule itself is reproducible from the cited words
(e.g. `0x3f1038 = 0x54494659` (line 16) and `0x3f103c = 0xD91B4E4F` (line 17) reverse to
`..NO`/`TIFY`, giving the `NOTIFY` banner at `0x3f103a`).
