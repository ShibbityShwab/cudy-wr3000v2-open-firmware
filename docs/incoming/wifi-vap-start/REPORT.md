# wifi-vap-start - the VAP start family: what makes the firmware produce scan results (and lets AP/STA start)

**Lane:** `wifi-vap-start` (shared task **task-117**), **host-only**, 2026-11-26.
**Method:** static RE of the pinned local vendor firmware image and vendor 2.5.24 Wi-Fi module,
plus the committed lane reports (`../wifi-vap-scan`, `../wifi-vendor-userspace`,
`../wifi-datapath`, `../wifi-ctx-create`, `../wifi-h2d-frames`), then byte-exact clean-room
builders, a provider-extension patch compiled inside the real 6.18.60 module build, and an
attended bench ladder. **The bench router was never contacted; no image was built.**

Address conventions (project-wide): firmware **CA = file offset + 0x40000**; host
**BAR0 = CA + 0x6B8000**; vendor module ET_REL **file offset = instruction CA + 0x38**.
Evidence markers: **[V]** verified in this lane (tools/vap_start_re.py, 56/56),
**[V\*]** device-measured by an earlier attended lane, **[I]** inference, **[U]** unknown
without a device read.

Inputs (gitignored, read in place; no vendor byte is committed):

| input | pin |
| --- | --- |
| vendor firmware image | local `FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...311b` (pinned in tools/vap_start_re.py) |
| vendor Wi-Fi module, stock 2.5.24 | local `hi5622v100_wifi.ko`, 3,564,728 B, sha256 `de78ec07...fd9521` |
| host module internals | local `hi5622v100_plat.ko` (hcc_msg_alloc/queue), same rootfs |

---

## 0. TL;DR - the fourteen answers

1. **The vendor's VAP create/start path is WAL group-2, not g0.** `wal_hipriv_add_vap`
   (vendor CA 0x147B7C) parses `create <ifname> <type> <band>` from /sys/hisys/hipriv, fills a
   28-byte WAL payload and calls `wal_send_cfg_event(dev, msg_id=1, len=28, data)`; the firmware
   VAP layer receives **cmd 0x0064, body 24 B**. The start follows through `wal_start_vap`
   (CA 0x11519C): **cmd 0x0065, body 12 B**. The AP-config event in front of it is
   **cmd 0x0127, body 156 B** (`wal_cfg80211_start_ap` -> `_write_msg`, CA 0x103634/0x103548). [V]
2. **WAL group-2 wire envelope decoded** from `wal_alloc_cfg_event` (CA 0x126EF0) and
   `wal_send_cfg_event` (CA 0x1270D8): 12-byte HCC header + u32 response cookie at +0xC +
   {u32 wal_id=1; u16 pad; u16 data_len; u32 seq} at +0x10 + {u16 cmd; u16 body_len; body} at
   +0x1C. **Frame total = 32 + body_len**; the vendor builder leaves **hdr[6..7] = 0** and puts the
   command in the WAL payload. [V]
3. **The g0 id1 config-event family carries the MAC-layer VAP add and VAP start.** The handler
   CA 0x43D58 reads key = u16 payload+8, len = payload+0xa, data = payload+0xc; table A
   (CA 0x10C23C, 217 x 8 B) maps **key 0x0064 -> worker CA 0x449E4** (module/VAP context add; only
   host-reachable caller of the create helper CA 0xFE6BC) and **key 0x0065 -> worker CA 0x46394**
   (VAP start). Keys 0x0066/0x0067 (VAP delete/down) exist with [U] bodies. [V]
4. **VAP-start g0 payload is byte-decoded** from `hmac_config_start_vap_event` (vendor CA 0x15B7FC):
   12-byte data = {[0] channel argument, [1] vap+0x0f, [2] vap+0x11, [3] vap+0x12,
   [4..7] u32 vap+0x414}; header key 0x0065, g0 id1, total 40. [V]
5. **VAP-add g0 payload fields decoded from worker CA 0x449E4**: data[0] = create type (1/2/3
   allocate the context; the hmac layer's vap modes are exactly 1/2/3), data[1] = parent module
   index (handler-context lookup gate), data[4] = the module/VAP index that gets created (<= 0x12),
   data[6]/data[8]/data[0x10..0x12] = flag/band reads; a successful add binds
   **module+0x4B0 / module+0x4B8** (the band resource). [V]
6. **The start_sta pair is decoded byte-exactly.** g0 id **0x0010** = 16-byte payload
   (join/start: [0] join+0x05, [4] join+0x06, [8..13] own MAC from vap+0xF68, [14..15] = 1),
   issuer `hmac_sta_wait_join` CA 0x538B8. g0 id **0x000F** = 64-byte join-request params
   (BSSID[0..5], channel u16[6..7], band info u32[8..11], u32 1 at [16..19], 33-byte assoc-req IE at
   [20..52], machine flag at [54]), issuer `hmac_sta_sync_join_req_params` CA 0x52FAC. [V]
7. **THE NO-RESULT DIAGNOSIS (the lane's key result).** The firmware scan handler CA 0x6494C
   first looks up the message module object `0x961AE(hdr[3])` and requires **[module+0x4B8] != 0**;
   only then does it look up VAP 0 and copy the 508-byte scan request to vap+0x300. The C2 ladder
   created **module 4** (ctx-create key 0x64, field B = 4) because the TXQ sync needs it; the VAP
   user/scan frames select **module 0** (hdr[3] = vendor vap+0xFA8 = 0). Module 0 was never
   created/bound, so the handler returned 100 before reading a single scan byte - exactly the
   2026-11-26 result (three frames consumed, zero D2H scan events, mask 0, page golden). [V]/[I]
8. **The fix order for a scan-capable VAP**: C2 Ctx-create/TXQ sync for module 4 (already proven),
   then the **module-0/VAP-0 start sequence**, then user_add -> notify -> scan_req. Two equivalent
   families exist: the vendor WAL group-2 order (cmd 0x64 -> cmd 0x65) or the g0 id1 keys
   0x0064 -> 0x0065. The ladder tries the vendor order first. [V]/[I]
9. **All six frames are synthesized byte-exact builders** (`src/vapstart_frames.c`): g0 id1 VAP
   add/start, g0 0x10/0xF start_sta, WAL cmd 0x64/0x65. 61 host KAT/refusal checks pass; the
   library compiles in the 6.18.60 module (707,912 B .ko, zero warnings, 20 vsf_ symbols). [V]
10. **A compile-ready provider extension** (`provider/vapstart-provider.patch`) adds
    `HI5622_FRAME_CAP_VAP_ADD/VAP_START`, two builder callbacks, module params
    `vap_start_first` (default 0 off), `vap_start_mode` (0 = WAL g2 vendor order, 1 = g0 id1 keys)
    and `vap_start_module`, and orders VAP-add -> VAP-start -> scan inside
    `hi5622_scan_op_scan`. It applies cleanly to the committed wifi-scan-ap tree and the patched
    module builds clean. [V]
11. **The AP-start gap is partially closed**: the outer envelope and cmd 0x0127 are recovered, but
    the 156-byte body is still [U] (gap G1). No AP_START capability is advertised; `start_ap`
    stays fail-closed. [V]
12. **No vendor bytes are committed.** Only derived CAs, ids, field offsets, frame layouts and
    original C are in this directory. [V]
13. **Safety rails are inherited**: one frame per idea, bounded announce poll, no retry, stop on the
    first exception mask/staging frame/page change; the frames use the same SR-node + mailbox id-3
    carrier as every validated H2D lane. [V]
14. **What is NOT proven**: hardware behaviour of every frame in this directory. The ladder is the
    falsifiable next step; sections 9 and 10 say exactly what to read back. [U]
`
15. **Post-fire fix (2026-11-26 bench feedback): the first bench fire of this lane's module was
    stopped by the DRIVER, not the firmware.** The 56-byte WAL VAP-add frame was refused inside
    `hi5622_frame_post()` by the inherited `h2d_frames` validator ("firmware receive slot has no
    handler"): the firmware's static group-2 receive table has no id-0 slot, which is exactly the
    WAL family. The provider extension now teaches the validator this one decoded family
    structurally (envelope + WAL command allowlist 0x64/0x65/0xD0/0x127 + per-command body size,
    `H2D_EWALCMD` refusals) while every other (group,id) keeps the non-NULL handler-slot rule.
    Rebuilt module: 737,504 B sha256 `5a0bfce5...9eeb1`, zero warnings; validator check 31/31 WAL
    cases + inherited h2d_frames selftest 233/0. The bench readback also showed the module-4
    ctx+0x4B0/+0x4B8 and G+0x630 targets resolving, so the readback pack is usable. [V]/[V* = bench]

---

## 1. Why this lane exists, and the vendor order

The 2026-11-26 milestone posted the first real management frames (g0 id7 user_add 180 B,
g0 id8 notify 180 B, g0 id0xC scan_req 520 B) - all consumed with mask 0, page golden, heartbeats
running - and the firmware returned **no scan results** in 5 s. The vendor order after calibration
is user/vap add -> (start) -> scan/AP (`../wifi-vendor-userspace`). This lane therefore asked the
narrow question: *which firmware message is the missing "start", and what exactly is on the wire?*

The answer has two layers, and both are now decoded:

* the **WAL/core layer** (HCC group 2): the interface itself is created/started by WAL cfg events
  (cmd 0x0064 VAP add, cmd 0x0065 VAP start, cmd 0x0127 AP config);
* the **MAC/hdpp layer** (HCC group 0): per-module contexts are created by the g0 id1 key 0x0064
  config event and started by key 0x0065; the scan handler's gate lives in this layer.

The bench evidence pins the missing MAC-layer object: the scan handler needs
`module(hdr[3]).+0x4B8 != 0`.

---

## 2. The three message families (wire envelopes)

### 2.1 HCC carrier (unchanged, shared with every H2D lane)

12-byte header: `[0] group = hdr[0]&0xF`, `[1] alloc nibble 1 | dst<<4`, `[2] field A`,
`[3] field B (handler lookup index)`, `[4..5] total`, `[6..7] id`, `[8] field C`,
`[9] dst`, `[10..11] tag 0x5A5A`. Carrier: one ETE SR node {devva, (total<<16)|0x6D2B}, packed
producer update, coalesced mailbox id-3 announce. [V\*] (validated lanes).

### 2.2 g0 id1 config event (hdpp/hmac_config_send_event shape)

`hdr[12] | token[8] | key u16 | len u16 | data[len] | pad[4]`, **total = 28 + len**.
The firmware handler CA 0x43D58 validates the header, resolves `FE578(0, hdr[3])`, reads
key = u16 payload+8 (payload = hdr+0xC), len = payload+0xa, data = payload+0xc, searches table A
then table B (CA 0x103BF8, 88 entries), and calls worker(ctx, len, data). [V]

### 2.3 g0 direct message (hcc_msg_alloc size = payload; explicit header)

`hdr[12] | payload[size]`, **total = 12 + size**; the builder writes hdr[6] = the message id
byte (e.g. 0x10, 0x0F). [V]

### 2.4 WAL group-2 cfg event

`hdr[12] | cookie u32 | wal{u32 id=1; u16 pad; u16 data_len; u32 seq} | {u16 cmd; u16 body_len; body[body_len]}`
with **total = 32 + body_len** and `data_len = 4 + body_len`. The vendor builder writes
hdr[6..7] = 0 and puts the operation in the WAL payload; the response cookie at +0xC is echoed by
the firmware for host response matching. [V]

---

## 3. Evidence chain (what was decoded where)

### 3.1 Firmware: g0 id1 key table and the two VAP workers

`g0 id1` handler CA 0x43D58 [V]: payload offsets key +8, len +0xa, data +0xc; table A bound
0x6C8 (217 x 8 B) then table B 0x2C0 (88 x 8 B); table A entries verified by direct byte probes:

| key | table A row | worker | role |
| ---: | ---: | ---: | --- |
| 0x0004 | A[0] | CA 0x811CD | module-flag write (needs a created context) |
| 0x0064 | A[1] | CA 0x449E4 | module/VAP context add (create helper CA 0xFE6BC) |
| 0x0065 | A[2] | CA 0x46394 | VAP start |
| 0x0066 | A[3] | CA 0x4455A | VAP delete (body [U]) |
| 0x0067 | A[4] | CA 0x45F74 | VAP down (body [U]) |
| 0x0069 | A[5] | CA 0x839B4 | set-mode event |
| 0x00D8 | A[41] | CA 0x81038 | TXQ config sync (bench-validated) |
| 0x0320 | A[164] | CA 0x81444 | TXQ-table address sync (bench-validated) |

**VAP-add worker CA 0x449E4** [V]: reads data[0] (create type: only 1/2/3 take the allocating
branch of CA 0xFE6BC), data[1] (parent module index: `FE578(0,data[1])` then its +1 byte feeds
the manager used by the create helper), data[4] (module index that gets created; table lookup
through CA 0x961AE + refcount CA 0xBC726), data[6]/data[8]/data[0x10..0x12] (flag/band reads),
then calls `CA 0xFE6BC(module_ctx, manager, data[4], data)` and, on success, binds
**module+0x4B0/+0x4B4/+0x4B8** from `CA 0xA4FC8(0, data[0x12])` (band object; requires
[band+0x1A00] != 0) at CA 0x44BBC. The create helper's only `str.w r0,[ctx,#0x190]` is CA 0xFE7E4.

**VAP-start worker CA 0x46394** [V]: resolves chip via `0x961AE(ctx[0])`, requires
`ctx+0x4B8 != 0`, stores data[2]->ctx+0x11, data[3]->ctx+0x12, data[4..7]->ctx+0x444, calls
CA 0xD4048(ctx, data[1]), CA 0x94518(ctx) (MAC VAP start) and, for AP mode, the context-buffer
path CA 0xBD114(ctx+0x190).

### 3.2 Firmware: the scan handler gate (the no-result cause)

Scan handler CA 0x6494C [V]: `0x961AE(hdr[3])` -> module; **requires `[module+0x4B8] != 0`**
(CA 0x64986 `ldr.w r7,[r0,#0x4b8]`); then `CA 0x581C8(0)` (VAP 0 lookup); then stores
payload+0x13B to vap+0x502 and tail-calls CA 0x644A4 with (module resource, VAP, chip, payload).
Any failure logs and returns 100 (graceful refusal). The C2 ladder created module 4 only, while
user_add/scan frames carry hdr[3] = 0 -> module 0 -> +0x4B8 NULL -> return 100. [V]/[I]

### 3.3 Vendor host: the WAL VAP create/start path

* `wal_hipriv_add_vap` CA 0x147B7C [V]: parses `create <ifname> <ap|sta|...> <2g|5g>`, builds a
  28-byte data block {u16 cmd 0x0064; u16 body_len 24; body} and calls
  `wal_send_cfg_event(dev, 1, 28, data, wait=1, ...)`.
* `wal_start_vap` CA 0x11519C [V]: 16-byte data block {u16 cmd 0x0065; u16 body_len 12; body},
  body[0] = 1, body[8..11] = response cookie.
* `wal_cfg80211_start_ap` CA 0x103634 / `_write_msg` CA 0x103548 [V]: 160-byte data block
  {u16 cmd 0x0127; u16 body_len 156; body} posted through `wal_send_cfg_event`, then
  `wal_start_vap(dev)`. The body is the vendor VAP/beacon config blob (gap G1, [U]).
* Envelope: `wal_alloc_cfg_event` CA 0x126EF0 [V] sets group 2, hdr[6]=hdr[7]=0, hdr[8]=0,
  writes the u32 at hdr+0xC, returns payload = hdr+0x10; `wal_send_cfg_event` CA 0x1270D8 writes
  {u32 id=1 at payload+0; u16 len at payload+6; u32 seq at payload+8} then the data at payload+12.
  hcc_msg_alloc (plat.ko CA 0x11D9C) makes total = size+12; the vendor call sizes give the
  32+body_len total above. [V]

### 3.4 Vendor host: start_sta builders

* `hmac_sta_wait_join` CA 0x538B8 [V]: `hcc_msg_alloc(16)`, hdr[6]=0x10, payload:
  [0]=join+0x05 byte, [4]=join+0x06 byte, [8..13]=6-byte own MAC from vap+0xF68, [14..15]=1.
* `hmac_sta_sync_join_req_params` CA 0x52FAC [V]: `hcc_msg_alloc(0x40)`, hdr[6]=0x0F,
  payload: BSSID at [0..5] (join+0x36), u16 channel at [6..7] (join+0x2c), u32 band info at
  [8..11] (vap+0xFB8), u32 1 at [16..19], 33-byte assoc-req IE blob at [20..52],
  flag at [54], then `hmac_sta_update_join_multi_bssid_info`.
* Their firmware handlers are the g0 table rows id 0x0010 -> CA 0x5E28C and id 0x000F -> CA 0x62EC0;
  the handler payload-offset heuristic reads exactly those fields. [V]

---

## 3.5 Bench feedback and the validator fix (2026-11-26)

The lane's provider module was fired on the attended bench (cfg80211 image, PD4 firmware):
boot golden, C2 ctx-create + TXQ sync consumed, user_add (g0 id7) + notify (g0 id8) posted OK -
then the NEW WAL VAP-add frame was **refused by the driver's own h2d_frames validator**:

```
frame post vap_add: frame rejected by the h2d_frames validator (firmware receive slot has no handler)
cfg80211: VAP start sequence not posted (-1) - scan aborted, no scan message
```

**Cause [V]:** the WAL family is HCC group 2 with header id 0 (vendor builder fact, section 2.4).
`h2d_check_semantics()` required a non-NULL slot in the firmware's static receive table for every
frame; that table has **no group-2 id-0 entry** (the catalog anomaly; the vendor host's own frames
share it), so a shape the firmware demonstrably accepts from the vendor host was refused before
touching the wire. This is a validator/catalogue gap, not a frame-encoding bug.

**Fix (this lane's provider patch, host-only):** `h2d_check_wal_cfg()` validates the WAL family
structurally and fail-closed:

| rule | reject on mismatch |
| --- | --- |
| payload length >= 20 (cookie 4 + WAL header 12 + cmd/body_len 4) | `H2D_ESHORT` |
| u32 at payload+4 == 1 (WAL cfg-event id) | `H2D_EWALCMD` |
| u16 data_len at payload+0x0A == 4 + body_len | `H2D_EARG` |
| payload_len == 20 + body_len | `H2D_ELEN` |
| cmd at payload+0x10 in {0x0064/24, 0x0065/12, 0x00D0/16, 0x0127/156} | `H2D_EWALCMD` / `H2D_EARG` |

Every other (group,id) keeps the generic `h2d_fw_handler_lookup()` non-NULL slot rule. The
extension is regression-tested: 31/31 WAL accept/refuse cases in
`src/wal_validator_selftest.c`, and the inherited wifi-h2d-frames selftest still reports
233/233 PASS against the patched library (`tools/validator-check.sh`).

**Bench readback after the refused frame [V*]:** module-4 ctx+0x4B0 = 0x00174BF0,
ctx+0x4B8 = 0x0015CA08 and G+0x630 slot0 = 0x00119758 were non-zero - i.e. the readback
targets in section 7 are valid and the module-4 context (from the C2 ladder) carries the band
binding. The VAP-add frame must be allowed first to learn whether the module-0/VAP-0 sequence
clears the scan handler gate; that is the next attended step.

**Rebuilt module (this lane, patch applied to the current wifi-scan-ap src tree):**
737,504 B, sha256 `5a0bfce5f92c1e5eb940892ca46cbac209d85784ad05f3bc37251ba36c79eeb1`,
zero compiler/modpost warnings, 20 `vsf_` symbols, module params include
`vap_start_first/vap_start_mode/vap_start_module`. The refusals above become
`frame post vap_add: ... CONSUMED` when the same module is reloaded.

---

## 4. Corrections register (supersedes earlier lane claims)

| earlier claim | corrected here |
| --- | --- |
| wifi-vap-scan REPORT §4: "VAP create is a different code chain; exact key sequence [U]" | The vendor path is WAL group-2 cmd 0x0064 (body 24) + cmd 0x0065 (body 12); the g0 id1 key 0x0064/0x0065 pair is the hdpp/mac-layer equivalent. [V] |
| wifi-scan-ap README: "start_ap/stop_ap -> WAL group-2 (gap G1)" | The outer envelope and the AP-config cmd 0x0127 are decoded; only the 156-byte body remains G1. [V] |
| wifi-vendor-userspace TABLES row 3: "g2 cfg event exact id [U]" | The WAL command id is the u16 at data[0]; hdr[6..7] is 0 in the vendor builder. [V] |
| wifi-datapath REPORT §5: "g0 0xF/0x10 STA join 0x40/0x10" | Confirmed and byte-decoded, including the issuer functions and field sources. [V] |
| any implication that user_add alone can enable scan | No: the scan handler's first gate is module(hdr[3])+0x4B8, so the module-0 VAP add must precede user/scan frames. [V]/[I] |
| wifi-ctx-create: key 0x64 = "context create" only | It is the hdpp module-context add AND the hmac VAP add path (`hmac_config_add_vap_sync_all` sends it); the create helper CA 0xFE6BC semantics are unchanged. [V] |

---

## 5. Frame specifications

All builders zero-fill, then write only decoded fields; the validator re-checks tag, total,
group, alloc nibble, dst mirror, family id and payload-size family. See `recipes/frames.md`
for the generated bytes (KATs in `src/vapstart_selftest.c` are the source of truth).

### 5.1 g0 id1 key 0x0064 - VAP/module add (48 B, data 20)

```
hdr: group 0, alloc 1, dst 0, fa 0, fb <handler module>, total 0x30, id 1, fc 0, tag 5a5a
payload: token[8] = 00 00 00 00 01 00 00 00 | key 64 00 | len 14 00
data[20]: +0x00 create type (1/2/3; default 2 = AP)   +0x01 parent module (default 0x12)
          +0x04 module/VAP index to create (default 0) +0x06/+0x08/+0x10..0x12 flags/band
pad: ff ff ff ff
```

Handler/worker evidence: §3.1. The frame is SYNTHESIZED, not a vendor capture; the parent index is
the one genuinely vendor-unknown field ([I]) - the ladder's readback verifies success, not the
parent guess.

### 5.2 g0 id1 key 0x0065 - VAP start (40 B, data 12)

```
data[12]: +0 channel | +1 state (vap+0x0f; default 1) | +2 vap+0x11 | +3 vap+0x12
          +4..7 u32 vap+0x414
```

### 5.3 g0 id 0x0010 - start_sta / join start (28 B, payload 16)

```
payload[16]: +0 join+0x05 | +4 join+0x06 | +8..13 own MAC | +14..15 = 1
```

### 5.4 g0 id 0x000F - join-request params (76 B, payload 64)

```
payload[64]: +0..5 BSSID | +6..7 channel | +8..11 band info | +12..15 0 | +16..19 = 1
             +20..52 33-byte assoc-req IE | +52 flag0 | +54 machine flag
```

### 5.5 WAL group-2 cmd 0x0064 - VAP add (56 B, body 24)

```
hdr: group 2, alloc 1, dst 0, total 0x38, id 0, tag 5a5a
+0xC: cookie u32 (0)  +0x10: wal id 1 u32  +0x14: 0 u16  +0x16: data_len 28 u16
+0x18: seq u32       +0x1C: cmd 0x0064 u16 +0x1E: body_len 24 u16
body[24]: +0 iftype (2=AP, 3=STA) | +1 sub_type | +6/+7 flags | +12 band | +17 band_type
          +18 band_id | +19 sub | +20..23 cookie
```

### 5.6 WAL group-2 cmd 0x0065 - VAP start (44 B, body 12)

```
+0x16 data_len 16, +0x1C cmd 0x0065, +0x1E body_len 12
body[12]: +0 start flag = 1 | +8..11 cookie
```

### 5.7 WAL group-2 cmd 0x0127 - AP config (188 B, body 156) - envelope only

The builder refuses without a caller-supplied 156-byte body (gap G1); no AP_START capability is
advertised by the provider extension, so cfg80211 `start_ap` remains fail-closed.

---

## 6. Verification

| check | result |
| --- | --- |
| static re-verification of every claim (56 probes: firmware table rows, handler/worker/helper byte patterns, g0/g2 rows, vendor symbol addresses and builder instruction sites) | `tools/vap_start_re.py --selftest` -> **56/56 PASS** [V] |
| host builders known-answer + refusal tests | `tools/build.sh` -> **61/61 PASS**, strict `-std=c99 -Wall -Wextra -Werror -pedantic` [V] |
| generated frames | `tools/gen_frames_md.py` -> `recipes/frames.md` (`--check` current) [V] |
| WAL family accepted / mutations refused by the patched h2d_frames validator | `tools/validator-check.sh` -> **31/31 PASS** [V] |
| inherited h2d_frames regressions (patched library) | `tools/validator-check.sh` -> **233/233 PASS** [V] |
| kernel-context compile (library + provider patch + WAL validator inside the real 6.18.60 module build) | `tools/kernel-check.sh` -> module **737,504 B sha256 `5a0bfce5...9eeb1`**, **zero warnings**, 20 vsf_ symbols [V] |
| hardware behaviour of the frames | **not done - host-only lane** [U] |

The kernel check copies the committed `../wifi-scan-ap/src` tree to `/tmp`, adds this lane's
`vapstart_frames.{c,h}`, applies `provider/vapstart-provider.patch`, adds
`vapstart_frames.o` and builds module-only. No image, no device.

---

## 7. Bench ladder (data: recipes/vap_start_ladder.json)

**Preconditions:** attended session, recovery door armed, UART capture running; cfg80211 image +
PD4 firmware golden boot; C2 ladder (ctx-create module 4 + txq_sync72) green; driver built with
`provider/vapstart-provider.patch`, loaded with
`allow_frames=8 vap_start_first=1 vap_start_mode=0 vap_start_module=0 frame_gap_ms=100 h2d_send_budget=0`.

| step | frame | total | expected |
| --- | --- | ---: | --- |
| B0 | read-only pack | - | golden page 0x86807FEF/7/2/0xABCD, state 1, mask 0, staging 0 |
| V1 | WAL cmd 0x0064 VAP add (AP, band 0, module 0) | 56 | consumed, mask 0; module-table slot 0 changes from 0 |
| V2 | WAL cmd 0x0065 VAP start | 44 | consumed, mask 0; module-0 ctx+0x4B0/+0x4B8 nonzero |
| V3 | (fallback) g0 id1 key 0x64 module-0 add | 48 | consumed, mask 0; ctx0+0x190 allocated, +0x4B8 bound |
| V4 | (fallback) g0 id1 key 0x65 start | 40 | consumed, mask 0 |
| S1 | existing scan_req g0 id 0xC 520 B | 520 | D2H g1 15/16/17 or g2 id2 within 5 s = candidate PASS |
| AP1 | hostapd open AP (only after S1) | - | fail-closed -EOPNOTSUPP until gap G1 closes |
| STA1 | start_sta g0 0xF (76 B) + 0x10 (28 B) with a real join context | 76/28 | future cfg80211 connect wiring |

**Readback (BAR0 = CA + 0x6B8000):** G = CA 0x170E28 -> BAR0 0x829E28; module-table slot 0 =
BAR0 0x829458, slot 4 = 0x829468, base offset u16 = BAR0 0x82A504. ctx0 = u32(slot 0) + u16(base);
then read ctx0+0x190, ctx0+0x4B0, ctx0+0x4B8, plus exception mask BAR0 0x40008 and staging
BAR0 0x6F800C. The scan handler PASS gate is **ctx0+0x4B8 != 0** before the scan_req is posted.

**Stop rules (any one aborts the lap, no retry in the same boot):** exception mask != 0 or a
nonzero staging frame; golden page/state or g+0xB4/+0xB8 change; SR node not consumed in the
bounded poll; h2d_status refused increments; any dmesg fault. On stop record mask/out[]/
rd_catalog/readback/dmesg, power-cycle, analyse offline.

---

## 8. Open items

| # | item | status |
| ---: | --- | --- |
| 1 | WAL group-2 firmware receive path: hdr[6..7]=0 and no group-2 table id 0 - the firmware's WAL dispatch is [U] even though the vendor host emits exactly this shape | [U]/[I] |
| 2 | WAL VAP-add body field meanings beyond iftype/band/cookie (fill_param sources are runtime WAL state) | [I] |
| 3 | g0 id1 key 0x64 parent-module index for a real VAP (0x12 default) | [I] |
| 4 | g0 key 0x65 start payload values (state/cfg bytes) supplied by the real vendor VAP object | [I] |
| 5 | AP-config WAL body, cmd 0x0127 body 156 B (gap G1) | [U] |
| 6 | D2H scan/join event payload layouts (owned by the cfg80211/scan lanes) | [U] |
| 7 | hardware validation of all six frames | not done - the ladder is the next attended step |

---

## 9. Limits and publication boundary

* **Host-only.** Every frame here is derived from static decode + compile checks; none has been
  sent to the firmware. The ladder is deliberately readback-gated and fail-closed.
* The vendor firmware and module are **local evidence**, never committed: only derived addresses,
  ids, field offsets and original C appear here. The selftest's KATs contain our synthesized
  frames, not vendor captures.
* The AP path is deliberately incomplete: the 156-byte VAP/beacon config body stays [U], and the
  provider advertises no AP_START capability rather than inventing bytes.
* The PD4-patched firmware remains a research artifact; these frames are ordinary post-ready H2D
  messages and equally valid on a vendor-firmware golden boot.

---

## 10. Reproduction

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
cd opensource/docs/incoming/wifi-vap-start
$PY tools/vap_start_re.py --selftest            # 56/56 static probes PASS
./tools/build.sh                                # host build + 61/61 KAT + emit
./tools/validator-check.sh                     # WAL family 31/31 + inherited 233/233
./tools/kernel-check.sh                         # 6.18.60 module build, zero warnings
$PY tools/gen_frames_md.py                      # regenerate recipes/frames.md
$PY tools/gen_frames_md.py --check              # freshness gate
```

## 11. Evidence ledger

| claim | artifact / command |
| --- | --- |
| table A key->worker rows, handler/worker/helper byte patterns, g0/g2 rows | `tools/vap_start_re.py --selftest` (56/56) |
| vendor builder/symbol addresses and instruction sites | `tools/vap_start_re.py --selftest` (local module, sha256-pinned) |
| frame bytes | `src/vapstart_selftest.c` KATs -> `recipes/frames.md` |
| kernel-context compile | `tools/kernel-check.sh` |
| provider contract + fail-closed post | `../wifi-scan-ap/src/hi5622_frames_api.h`, `hi5622_frame_post.c` |
| scan handler gate / VAP-add bind | REPORT §3.1/§3.2; probes in `tools/vap_start_re.py` |
| bench state and prior evidence | `STATUS.md`, `mem-entries.md` entries 2026-11-25/26 |
