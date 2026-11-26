# REPORT - the stock pair-A H2D capture: census, the g12 answer, new families, the 5 GHz verdict

Lane: wifi-stock-h2d-census (task-232). Input: /tmp/h2d-stock.log (first-ever stock H2D capture,
pair-A vendor boot, sha256 in evidence/LOG-SHA256.txt). Host-only; no device contact. Grades [V],
[V*], [I], [U] as in TABLES.md. The capture log is local (vendor-derived DRAM content, not
committed); all quoted frames are decoded protocol bytes as text with MACs redacted.

## 1. Method and the validity split

The sampler emits one M record per ring node; the record carries the node's own length word
(len) and the frame's own +4 total field (total), read separately from host DRAM. A real host send
requires node len == frame total (the F2/F6 invariant); a difference means the read crossed a host
update or a recycled buffer. Additionally the sampler re-walks the whole ring when the packed
producer index is ambiguous after a burst (its drain_channel comment documents this), which turns
old ring entries into extra records - some of them over-reads: the stale length word can be longer
than the frame, so the sampler reads past the message allocation into adjacent host heap.

Consequence: raw family counts measure reads, not sends. Results (TABLES T1): 1129 records,
332 length-valid, 4 stale-head aliases, 793 torn. The raw "22-family census" in the parent task is
the read census; the **send census is 7 H2D families** (TABLES T2) plus D2H residue classes
(T8). This is the same over-count the f1 lane documented (1716 raw / 604 valid), and the same
corrected-census method applies.

The v2 sampler header (mmap window 0x583f2000, 10 ms period, duration 90) is new relative to the
committed v1 source; the record format is unchanged, so the committed decoder and this tool parse
it. Caveat C3 covers the flags field.

## 2. The g12 family: FALSIFIED - it is not a family and not an Ethernet channel

The 7 g12 id29 records. All 7 are byte-identical, live records at t = 64.3, 65.8, 66.3 s, all
pointing at node buffer 0x84ac9e40 with len=72 / total=16. Their bytes:

    0c 18 30 00 | 10 00 | 1d 00 | 00 00 | 5a 5a | c0 dd bc bf | <16x00> <18xff> <22-B IEEE 1905.1 prefix>

Reading it correctly: bytes 4..15 are the current message - total 16, id 0x1d, transport tag
5a5a, and a 4-byte payload that decodes as a host kernel pointer (0xbfbcddc0; other reads of the
same class carry 580ec0bf, 407150c4, ...). That is exactly the committed f1 lane's
hcc_timer_timeout_proc class (plat.ko, hcc_msg_alloc(4,0), hdr[0]=2, hdr[3]=0, hdr[6]=0x1d,
hdr[12]=timer context pointer) which the f1 lane saw 328 times as g2 id29. In this capture the
same class appears 389 times: **381 with the canonical head 02 01 00 00, 7 with a torn head
0c 18 30 00, 1 with a torn vap byte 02 01 30 00** (TABLES T5). The 7 g12 reads are the torn-head
tail of that class, not a new family: bytes 4..15 are identical across all three head variants and
the tag invariant holds on all 793 torn records (u16@+10 = 5a5a in 1129/1129 records). The
0c/0x0c low nibble is outside the H2D group space (group 0 and, for the timer, destination 2) and
no firmware receive table registers group 12, so the head is stale - the committed f1 rule.

The 4 g12 id1 records are aliases (TABLES T4): one 48-B record is the g0 id1 key 0x00d8
(TXQ/AMSDU MIB config) frame with only the 4 head bytes replaced (44/44 identical after the
head); three 184-B records are g0 id1 key 0x0127 (set_beacon) frames - one is 180/180 identical
after the head, the other two carry stale preamble bytes (a host pointer, and ASCII "HEARTBEA",
i.e. bytes recycled from a D2H heartbeat payload). This reproduces the f1 lane's g12 id1 ->
key 0x0127 alias on a fresh capture and adds the 0x00d8 case.

Why the "per-VAP Ethernet channel" reading is wrong. The Ethernet-looking bytes are the IEEE
1905.1 header prefix (TABLES T7):

    01 80 c2 00 00 13   IEEE Std 1905.1 group MAC ("transmission of IEEE 1905.1 control packets")
    d4 0d ab XX XX XX   the device MAC (redacted)
    89 3a               EtherType 0x893a = IEEE 1905.1 (Multi-AP / EasyMesh CMDU)

They start at the end of the *over-read* region (offset len-22 = 50) in all 171 carrier records,
with different CMDU message-id bytes between reads, i.e. they are adjacent heap content from the
stock image's 1905.1/Multi-AP control traffic, not payload of a 16-B message. The node len 72 is a
stale descriptor from an older 72-B message, so the sampler over-reads 56 B past the 16-B
allocation. No H2D message in this capture carries an Ethernet frame: all length-valid H2D frames
are group 0 (332/332 after alias folding) and none has an Ethernet header at a payload offset.
Conclusion: the g12 "family" is the 0x1d HCC-timer class + stale-head aliases + heap over-read;
it must not be added to any send set [V].

## 3. The other raw-new families: which are real sends

Length-valid and decoded with builder evidence (TABLES T2/T6, REPORT sec. 5 for the 5 GHz ones):

* g0 id22, n=1, 52 B - hmac_chan_do_sync (wifi.ko 0x47c58, re-derived here [V]). Geometry:
  alloc(40) -> total 52; hdr[6]=0x16; hdr[1] = (ctx[+2]<<4) | stale low nibble; hdr[2]=ctx[+1];
  hdr[3]=ctx[+0]; payload is the caller's 40-B channel-sync struct. Captured payload[0]=0x24=36
  -> the MAC channel context is being synced to **5 GHz channel 36** [I].
* g0 id32, n=2, 23 B - hmac_config_h2d_send_app_ie (0x600cc [V]): alloc(dlen+11); payload = the
  caller struct with the first u32 = IE length. Captured dlen=0 (no IE bytes), flag word 4.
* g0 id36, n=1, 20 B - hmac_scan_config_bss_color (0x99c10 [V]): alloc(8); payload = 64-bit mask
  of list-entry indexes (bits {0,3,24,25,58} in this capture) plus a list-counter side effect.
* g0 id2, n=4, 39 B - hmac_config_alg_send_event, already decoded [V*] (key 0x0101, subkey 0x0b).
* g0 id12, n=1, 520 B - hmac_enc_scan_send_req, already decoded [V*]; the channel list here is
  (band<<8)|channel with band 1 = **5 GHz**, channels 36..64 and 100..165 (TABLES T6) [V].

Not real sends (torn/D2H residue, TABLES T8): g4 id0 (143, D2H OAM log), g4 id4 (93, D2H
HEARTBEAT), g2 id29 (382, timer), g0 id1 torn (101, 40 of which carry a complete config geometry =
real config sends behind stale descriptors), g0 id29 torn (33), g2 id0 (12, [U] config-shaped but
ungreeted by builder or firmware table), g1 id3/id18/id35/id37/id38, g2 id9/id37 (22, D2H
event/log class: group byte = source chip id; content is device data), g8 id0 (1, [U];
out-of-range head - over-read artifact).

Net new versus the committed f1 capture: the stock send census extends by g0 id32 (app IE),
g0 id36 (BSS-color bitmap) and g0 id22 (channel sync), and the g12 id1 alias set gains the
0x00d8 case. Nothing here is an RX/beacon enable.

## 4. The 5 GHz beacon candidate: NONE on the H2D ring

The committed wifi-5g-tx-scheduler lane proved the 5 GHz beacon gate 0x40101220 bit0/bit1 (plus
the 0x40101208 suspend latches) is firmware-write-protected and driven by firmware-internal
beacon_tx resume/suspend messages; a host register copy cannot arm it. The open question this
capture was meant to settle: does the stock host arm the 5 GHz beacon through some H2D message we
never sent? Answer: **no**.

Evidence (all [V] from this capture):
1. This is a 5 GHz-active boot: hmac_chan_do_sync for ch 36 at t=13.2 s; a 5 GHz channel list in
   the scan request; set_beacon template posts at t=11.9/13.1/13.3/40.2/59.9/65.9/67.1/67.3 s
   (chips 7,8,10,11); VAP starts (key 0x0065) at t=7.3/14.5/76.2 s.
2. Every length-valid H2D family in the capture is accounted for by a known builder (T2/T6); none
   is a beacon/TBTT-arm builder. After each set_beacon post the only H2D traffic is the steady
   EDCA (g0 id29) cadence + occasional config re-posts - there is no "beacon enable" send.
3. The parent candidate, g12 id29, is the torn 0x1d timer (sec. 2), not a beacon message: it
   carries a host pointer payload and the firmware group-2 table has no id 0x1d handler, so it is
   dropped device-side.

Interpretation: the stock host posts the beacon template (key 0x0127) and the channel context
(key 0x0065 / g0 id22), and the firmware raises the beacon when its internal band state says so -
the enable is not a host message. This is exactly the committed 0x40101220 result, now confirmed
on the H2D side. The g12-as-beacon-candidate hypothesis is closed negative; keep the
wifi-5g-tx-scheduler R0/R1 register rungs as the beacon path.

The one genuinely new 5 GHz-flavoured host message is g0 id22 hmac_chan_do_sync(ch36): a MAC
channel-context sync. It is a candidate for completeness (see FIRE-RECIPE C1), not a beacon
enable. Note our driver's documented 5 GHz bringup already reaches the beacon-posted state, so the
absence of chan_do_sync is not the beacon gap; the gate is.

## 5. Relation to our driver (the 18-family set of the f1 lane)

Our documented send set (f1 lane sec. 6): config keys 0x0064/0x00d8/0x0320/0x0065, direct ids
0x0006/0x0007/0x0008, WAL group-2 commands. The stock capture's valid families that intersect:
key 0x00d8 (39), key 0x0065 (4) - consistent with the committed bench diff (108 matched / 0
drift). The stock's other 24 config keys and the g0 direct ids 0x02/0x0c/0x16/0x20/0x24 are
stock-only traffic; the ones with a documented purpose are alg offload (0x02), scan (0x0c),
channel sync (0x16), app IE (0x20), BSS color (0x24). Only g0 id22 and g0 id36 are plausibly
relevant to our 5 GHz path (channel context and HE BSS color); both are optional for the beacon
question (sec. 4). Do not add: the timer class, the D2H residue classes, the g2 id0 torn blob.

## 6. Caveats

C1. Torn-record timestamps are sampler re-walk times, not send times; duplicate records exist.
T2/T3 timings use length-valid records only (still subject to re-walk duplication).
C2. The capture has no explicit stimulus marker. Two host-side clusters are candidates for the
"wifi down; wifi up" stimulus: the restart churn at t=64.3-67.3 s (timer burst + set_beacon
re-posts) and the 13-record config re-post at t=76.2 s. Attribution is [I].
C3. flags: the v2 sampler prints flags=0x0000 for all 1129 records, while H2D-FORMATS F2 says
the vendor node word1 low bits are 0x0d2b (0x6d2b whole). Either the stock H2D nodes genuinely
carry zero flag bits in this boot, or the v2 sampler's field differs from v1 (source not
committed). Do not derive a driver difference from this; the committed decode_h2d.py would flag
all records as flags-unexpected for this reason.
C4. Host pointer values in timer payloads are quoted as decoded protocol bytes only; they are not
stable identifiers and carry no secrets.
C5. The 1905.1 fragment is heap content; the specific CMDU type cannot be decoded from 8 payload
bytes. The claim is only "IEEE 1905.1 control traffic exists in the stock image's host memory".

## 7. Evidence index and reproduction

| evidence | content | regenerate |
| --- | --- | --- |
| evidence/CENSUS.txt | the corrected census above | python3 tools/decode_stock_census.py --input LOG |
| evidence/ALIASES.txt | alias fitting + timer head variants + 1905 carriers | ... --aliases |
| evidence/NEW-FAMILIES.txt | byte dumps of g0 id22/32/36/2/12, g12 id1, g12 id29 | ... --family G ID [--torn] |
| evidence/LOG-SHA256.txt | capture size/sha256/header line | sha256sum LOG |
| evidence/HOST-TESTS.txt | selftest output | python3 tools/decode_stock_census.py --selftest |
| build/wifi-stock-h2d-census/h2d-stock.log | local copy of the capture (gitignored) | - |
