# REPORT - the stock H2D capture decoded: the real message set, the HEARTBEAT is D2H, and what that says about F1

**Lane:** `fw-f1-capture` (shared task **task-198**), 2026-11-28. **Host-only**: decode of the
successful stock H2D trace (`/tmp/h2d-capture.log`) against the local vendor modules and the
firmware image. No device contact; no vendor binary/firmware byte or raw capture committed.
Companions: [README.md](README.md), [TABLES-families.md](TABLES-families.md),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [tools/decode_f1_capture.py](tools/decode_f1_capture.py),
[evidence/](evidence/).

Grades: **[V]** verified here from a named artifact/instruction; **[V\*]** committed earlier lane;
**[M]** bench-measured earlier; **[I]** inference; **[U]** unknown. CA = file offset + 0x40000.

---

## 0. Verdict

1. **The raw "18 families" split is not 18 messages.** 1112 of the 1716 M-records are torn node
   reads (`node_len != frame total`); only **604 are valid H2D sends** and they reduce to **four
   message classes** (g0-id1 config events, g0 id29 EDCA timeout, g0 id12 scan request, g0 id2 alg
   request) plus the torn-only plat HCC timer (g2 id29). See TABLES sec. 1-3.
2. **The HEARTBEAT (g4 id4) is a device->host message, not a missing host send.** The captured
   22-B frame is **byte-identical to the firmware's own builder** at CA `0xC6BB2`: it copies its
   payload from the `HEARTBEAT` literal at CA `0x106328` and sends it with the firmware tx
   `0x102760`. No vendor host module contains the literal; the vendor host's g4-id4 handler
   (`heartbeat_msg_process`, plat.ko `0x10214`) only restarts a 10-s hrtimer and sends nothing.
   The capture sees it only because the sampler read a recycled buffer at a torn node. Our driver's
   "heartbeat off" (the D2H liveness monitor) is therefore **not** a bug and adding an H2D
   heartbeat would be adding a frame the vendor never sends.
3. **The two big "unmatched" families are now named:** g0 id29 = `hmac_edca_opt_timeout_fn`
   (byte-exact builder in wifi.ko; firmware handler CA `0xB568C` -> hook `G+0x378` = `0x68C0A`
   writes per-chip MAC context); g2 id29 = plat.ko `hcc_timer_timeout_proc` (payload = the HCC
   timer's host context pointer). Neither is an RX/TRX enable.
4. **The capture's F1 result is a negative: no stock H2D message is an RX-ring/TRX enable.** The
   valid stock H2D surface is smaller and narrower than the "18 candidates": 25 config keys (only
   key 0x0069 among the ones we do not send has a live firmware worker), the EDCA timeout, the alg
   request and one scan request. It contains **no** `enable_trx`/`rx_ring_reset` message; key
   `0x0004` (the previous lane's candidate) is **never sent by the stock**; the firmware's
   `smac_msg_proc_enable_trx_req` / `..._rx_ring_reset_req` names remain a firmware-internal
   debug blob with no H2D id (already [V\*] in `wifi-rx-format`). F1 is therefore **not a missing
   H2D frame**; the last host-side discriminator is the EDCA timeout message (already implemented
   in our driver as `edca`), and the fire recipe below runs it once before F1 escalates to the
   firmware-internal/host-delivery path.

---

## 1. Method and inputs

* capture: `/tmp/h2d-capture.log` (sha256 `970b8006...`, 2603 lines, 1716 `M` records, 210 s,
  sampler `v2 ... window=0x583f2000 module=hi5622v100_plat ... access=mmap`);
* the committed wire formats (`wifi-rx-h2d-trace/H2D-FORMATS.md`), the committed vendor sender
  catalogue (`wifi-rx-init-msg/TABLES-h2d-builders.md`), the committed firmware receive tables
  (`wifi-h2d-catalog`) and the callback install map (`wifi-fw-callback-install`);
* local vendor modules + firmware (hashes in evidence/DECODE-LOG.md); module disassembly with the
  project OpenWrt objdump, firmware with capstone Thumb.

The decoder is tools/decode_f1_capture.py (stdlib only, `--selftest` 5/5): it classifies each
record valid/torn, collapses torn content signatures onto valid messages, folds partially rewritten
headers onto their canonical message, and reports residue.
---

## 2. The HEARTBEAT decode (a) - decisive

### 2.1 The firmware builds the captured frame

Firmware code at CA `0xC6BB2` (file `0x86BB2`, Thumb; disassembled here) in the heartbeat
worker around CA `0xC6B6A`:

```
c6b96-c6ba2  copy 10 B from the literal CA 0x106328 ("HEARTBEAT" + NUL) to the stack
c6bac        hcc_msg_alloc(0xa, 6)          ; firmware alloc (CA 0x102822)
c6bd6-c6be8  hdr[1] &= 0x0f ; hdr[0] = 4 ; hdr[3] = 0 ; hdr[6] = 4 ; hdr[7] = 0
c6bf4-c6bfc  memcpy(hdr + 12, "HEARTBEAT" + NUL, 10)
c6c0a        hcc_msg_tx(msg)                 ; firmware tx (CA 0x102760)
c6c1c-c6c3c  count; every 30th iteration an OAM log (the 0x1e divisor)
```

The allocator sets `hdr[1]` low nibble = 1, so the frame is exactly

```
04 01 00 00 16 00 04 00 00 00 5a 5a 48 45 41 52 54 42 45 41 54 00        (22 B)
```

which is **byte-identical to every one of the 83 captured g4-id4 records** (payload
`HEARTBEAT` + NUL, total 22, tag 0x5a5a) [V]. Direction: firmware tx = device->host.

### 2.2 The host does not send it, and does not reply

* no vendor HOST module contains the `HEARTBEAT`/`HEART` literal (byte scan; only
  `EXCP_HEART`/`EXCP_HEARTBT_TIMEOUT` strings in the plat exception table) [V];
* the vendor host's g4-id4 handler is plat.ko `heartbeat_msg_process` (CA `0x10214`): it
  validates the message, reads `hdr[1]`'s resource nibble, and on a valid chip resource restarts
  a 10-s `hrtimer` (`hrtimer_start_range_ns`); it **sends nothing** [V];
* all host message allocators are accounted for (78 `hcc_msg_alloc` call sites in wifi.ko, 2 in
  plat.ko; only `hmac_edca_opt_timeout_fn` and `hcc_timer_timeout_proc` build non-config frames,
  neither builds g4/id4) [V];
* the capture's g4-id4 records are all torn (node len 72/29/48 vs frame total 22) at only 3-4
  recurring buffer addresses => recycled-buffer residue, not ring sends [V].

### 2.3 Consequence for our driver

Our driver logs "PM off, heartbeat off": that flag is the **D2H liveness monitor**
(`hcc_core` `heartbeat_armed=false`; the vendor boot path also leaves it off, and the handler
semantics are "restart the host hrtimer"). Adding an H2D heartbeat is wrong and must not be done.
The liveness the heartbeat carries is device->host liveness; it cannot gate host RX delivery.

---

## 3. What the stock really sends (valid H2D), and the two named families

Full table in TABLES-families.md sec. 2. Summary:

| class | n | builder | firmware consumer |
| --- | ---: | --- | --- |
| g0 id1 config events (25 keys) | 254 | `hmac_config_send_event`/`hdpp_config_send_event` [V*] | group-0 id-1 handler CA `0x43D58`; only 8 live keys in this image [V*] |
| g0 id29 EDCA timeout | 329 | `hmac_edca_opt_timeout_fn` wifi.ko `0xef43c` [V] | g0 id `0x1d` handler CA `0xB568C` -> `G+0x378` = `0x68C0A` [V] |
| g0 id12 scan request | 1 | `hmac_enc_scan_send_req` (alloc 508) [V] | g0 id `0x0c` handler CA `0x6494D` [V*] |
| g0 id2 alg request | 2 | `hmac_config_alg_send_event` [V*] | g0 id 2 handler CA `0xD6069` key table [V*] |
| g2 id29 HCC timer (torn only) | 328 | `hcc_timer_timeout_proc` plat.ko `0x13410` [V] | group-2 table has **no** id `0x1d` entry [V*] |

### 3.1 g0 id29 ("the 381 sends") [V]

Builder decode is in TABLES sec. 2.1: payload buffer = 60 B, zeroed then `memset(offset 20, 0xff,
40)`; `hcc_msg_alloc(60, clear=0)`; `hdr[0]` low nibble cleared (group 0), `hdr[6] = 0x1d`,
`hdr[7] = 0`, `hdr[2] = ctx[+6]`, `hdr[3] = ctx[+1]` (the per-chip field B), `hdr[1]` =
`(dst<<4)|alloc`, 60-B payload copied, `hcc_msg_tx`. Capture: `00 01 00 <fb> 48 00 1d 00 00 00
5a5a` + 20 x 00 + 40 x ff, `fb` in {1,2,4,5}, 329 valid records from t=26.8 s at ~0.44 s.

Firmware handler CA `0xB568C`: validates the message, copies the 60-B payload to a local, then
calls the installed hook `G+0x378` with (hdr[3], payload). `G+0x378 = 0x68C0A` (installer
`0x5C3B6`, [V*]); it resolves the per-chip module context (`0x961AE`), requires the RF-chain
word, reads the payload as four 5-byte EDCA entries (payload[0..19]) and writes the per-chip fields
`+0x4aa` (a checksum), `+0x533` (per-AC/per-user flags) and `+0xdb` bit 2. **EDCA/MAC-context
sync; no ring/TRX enable; no D2H reply** (so the host sees only the consumed announce).

### 3.2 g2 id29 ("the 328 sends") [V]

plat.ko `hcc_timer_timeout_proc` (CA `0x13410`): `hcc_msg_alloc(4, 0)`; `hdr[0] = 2`;
`hdr[3] = 0`; `hdr[6] = 0x1d`; `hdr[7] = 0`; `hdr[1]` from the timer-state nibble;
`hdr[12] = r4` (the HCC timer/context pointer); `hcc_msg_tx`. The captured payloads are host
kernel pointers (`...c45022e0`, `...bfbcd930`) => host-built. The firmware group-2 receive table
(19 ids) has no id `0x1d`, so the frame is probably dropped device-side. **Do not fire it**: a
payload the device may dereference plus no registered handler is a fault class.

### 3.3 The other "families" - closed [V/I]

* `g12 id1` (21) = **alias of g0 id1 key 0x0127 (set_beacon)**: 18 records are length-valid but
  their first 4 header bytes are stale; `(total, payload)` is byte-identical to the key-0x0127
  config events (TABLES sec. 4). Closed, not a family.
* `g4 id0` (153), `g2 id9` (60), `g1 id37` (28), `g1 id3` (12), `g1 id17` (1), `g1 id38`
  (1), `g2 id37` (4), `g9 id29` (4) are **not H2D sends**: either firmware D2H content in
  recycled buffers (g4 id0 = OAM/device log; g2 id9 = text-ish `vapN`+SSID) or D2H event
  classes with no host builder and (group 1/9) no firmware receive table [I].
* `g2 id0` (136) remains **[U]**: torn residue whose visible prefix is a config-event-style blob
  (key fragments 0xD8/0x65, host pointers) but no vendor builder or firmware handler matches a
  group-2 id-0 message; it is not a trustworthy send and not an RX candidate.
* the parent map's `g4 id9329` is a parse artifact of one 48-byte record, not a family.
---

## 4. The RX question (F1): candidates ranked, with the evidence

The RX failure we are chasing: our AP runs, the laptop's auth frame reaches the MAC, but the
firmware writes **host frames = 0** (no D2H deposit). The capture was supposed to name the missing
host message. What it shows:

| rank | candidate | evidence | expectation |
| ---: | --- | --- | --- |
| 1 | **g0 id29 EDCA timeout** (never fired in our ladder; builder already in the driver as `edca`) | the only stock message class that is (a) datapath-class, (b) periodic/steady-state, (c) has a registered firmware handler that writes MAC/VAP context, (d) missing from our sends | **low**: handler is EDCA-context sync, not an RX enable; fire it to close the last host-message candidate |
| 2 | **g0 id1 key 0x0069 set_mode** (7 stock sends; the only live firmware config key we do not send - worker CA `0x839B4` [V*]) | stock sends it 7x in a burst at t~35.6 s; live key table [V*] | low-medium: mode arm is per-chip; the 5 GHz lanes already arm mode through other keys. A cheap second one-shot if rank 1 is inert |
| 3 | g2 id29 HCC timer | host-pointer payload; firmware has no group-2 id-0x1d handler | **do not fire** (fault risk) |
| x | g4 id4 HEARTBEAT | device->host, byte-identical firmware builder (sec. 2) | closed - not an H2D message |
| x | g0 id1 key 0x0004 | the stock never sends it; key table A's only unaccounted live key [V*] | **closed** - it is dead in this vendor build |
| x | g12 id1 | alias of key 0x0127 | closed |
| x | g2 id0 / g4 id0 / g2 id9 / g1 ... | residue, not sends | closed |

**F1 conclusion [V/I]:** the vendor's pre-RX/steady-state H2D surface contains **no RX-ring reset,
TRX enable or delivery-gate message**. `smac_msg_proc_enable_trx_req` / `..._rx_ring_reset_req` are
firmware-internal (no H2D id, no dispatch entry; the name literals sit at CA `0x1228BC` /
`0x1229A8`, file `0xE28BC`/`0xE29A8`, this lane's string scan, and the vendor send catalogue has
no builder for them [V*]), and the stock boot - whose RX path is the known-good reference - runs
with the same message classes our driver already replicates (the parent diff's 108 matches are
consistent with 104 x key 0xD8 + 4 x key 0x65) plus the datapath-class EDCA timeout. So F1 is **not a missing H2D frame**; the discriminating options left
are (i) the missing datapath context that the EDCA timeout maintains (fire recipe rank 1), (ii) the
host delivery layer (already heavily excluded by the RX lanes; a stock-control run on the same
host port is the reference), or (iii) a firmware-internal RX init that a host-only boot never runs
(the firmware has an `RX Thread` name CA `0x10CFFC` and the `ppdu_dscr_list` spinlock CA
`0x106116`, but no H2D trigger for them appears in the capture).

---

## 5. Implementation path

1. **No new family is needed for the HEARTBEAT or key 0x0004.** Do not implement them.
2. **EDCA (g0 id29)**: already implemented in the `wifi-h2d-send` driver as the
   `edca [dst=N]` grammar token (`h2d_build_g0_id1d_edca_opt`, `H2D_EDCA_FA/FB/FC = 0/4/0`,
   60-B payload). Two deltas before the fire:
   * **byte-exactness**: the builder currently leaves the whole 60-B payload zero, while the stock
     frame is **20 x 00 then 40 x ff** (TABLES sec. 2.1). Fill `payload[20..59] = 0xff` in
     `h2d_frames.c` (one `memset`; the frame's declared total stays 72).
   * **field B**: `H2D_EDCA_FB = 4` matches one of the stock's values ({1,2,4,5}); keep 4 for the
     first fire and repeat with the band's own field B only if needed (the driver's status shows
     the header field B used).
3. **Fire order**: ready ladder -> TXQ sync (already proven) -> **one `edca` post** -> RX test ->
   if inert, a **bounded burst** (10 posts at ~0.4 s; the stock sends one every ~0.44 s) -> RX
   test again. Full bench ladder, PASS/FAIL and STOP rules: FIRE-RECIPE.md.
4. **Escalation if frames stay 0**: F1 leaves the host-H2D space. The reference experiment is a
   **stock-control run on the same host port** (the RX lanes' SC-1: stock image + stock AP + the
   same laptop auth; if the stock delivers RX through our port programming, the difference is
   firmware-internal). Do not patch the firmware on a hunch from this lane.

---

## 6. Honest limits

* The capture is a read-only userspace sample of the SR ring; torn nodes are expected and are the
  reason the naive family map was misleading. The valid-record classification and the aliasing
  rule are documented and self-tested, but a torn record can never be *proved* to be a given
  message - only its content signature can (that is why residue is never trusted as an H2D send).
* The direction calls for residue classes (D2H vs H2D) rest on (a) the byte-exact firmware builder
  match for the HEARTBEAT, (b) the presence of host kernel pointers in g2 id29, (c) the absence of
  a host builder/firmware receive handler for the others. g2 id0 stays [U].
* The EDCA handler chain (`0xB568C` -> `G+0x378` -> `0x68C0A`) is decoded statically; its
  execution on the bench is unproven (and it sends no reply, so only the consumed announce and the
  absence of an exception are observable).
* This lane ran with no device contact; the fire recipe is written for an attended Lead run.
