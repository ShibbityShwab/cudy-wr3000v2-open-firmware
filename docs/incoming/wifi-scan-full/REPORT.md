# Full 13-channel scan via the g0 id10 per-slot context array - static RE + refined builder

**Lane:** wifi-scan-full (shared task-138), host-only clean-room static analysis.
**Written:** 2026-11-24.  **Base:** wifi-scan-tuning R4 (task-133) + the
bench-validated combined/hardened tree.  **Nothing here was sent to the bench
router by this lane.**  The Lead fires; TEST-LADDER-R11.md is the procedure.

Evidence markers: **[V]** re-verified from a named local artifact (firmware CA
or vendor-module address), **[V\*]** hardware-measured by an earlier attended
wave and quoted from its report, **[I]** inference, **[U]** unknown.

## 0. Headline

1. **The R4 dispatch anchor was off by one row.**  The firmware's group-0 H2D
   table is at CA **0x10CCFC** (12-byte rows `{u32 id; u32 fn; u32 aux}`), not
   0x10CD08.  The row for **id 10** is at CA **0x10CD5C** =
   `{10, 0x5FF65, 0}`; 0x10CD08 is the row for id 1.  Handler entry CA
   **0x5FF64** is unchanged [V].
2. **Complete payload field map decoded for both subtypes**, including every
   read site and the write target relative to the per-slot context
   (base = object + slot*0x20 + 0xEC, 8 slots of 0x20 B) - sec. 3/4 [V].
3. **The message is the vendor host's relay of a received BA management frame,
   not a channel-plan message.**  The only explicit group-0 id 10 senders in
   `hi5622v100_wifi.ko` 2.5.24 are `hmac_mgmt_rx_addba_rsp` (`.text` 0x45A94,
   payload type 3/subtype 1) and `hmac_mgmt_rx_delba` (0x45D88, type 3/subtype 2).
   Both allocate a **32-byte payload / 44-byte frame** (`hcc_msg_alloc(32)`),
   so the R4 builder's 36-byte frame was **8 bytes shorter than the vendor
   shape** [V].  Refined builder fixes the wire shape and the subtype-2 flag
   location (payload+0x14, not +0x08) - patch 0001.
4. **The "program slots 0..7 to cover ch1..13" premise is falsified.**  The
   scan engine consumes only the **first** slot context (offset 0xEC = slot 0)
   as its start/step/end + mode + counters, and **no subtype-1 payload field
   writes the count/end byte (context+0xFE)** - only the subtype-2 clear path
   writes it (0, CA 0x5FEE0) [V].  A second/third configured slot therefore
   cannot extend a 13-entry request.  The measured 7-8 channel ceiling is
   structural for this message [I, high confidence; falsifiable by R11.6].
5. **What the R4 H1 experiment actually changed** is consistent with aliasing
   the slot-0 **pacing** fields of the scan context: payload+0x10 -> +0x104
   (per-channel result counter preset) and payload+0x12 -> +0x103 (mode); the
   subtype-2 companion then clears +0xFE/+0x106/+0x05.  That explains the
   R10b/C sweep shape (field10 25 -> 8 channels, results=0: result waiting is
   skipped, more channels fit the host window) [I].  It is not a channel-count
   control.
6. **Delivered:** refined builder + patch (vendor-shaped 44 B frame, corrected
   field map, fail-closed default), 90/90 host checks, 65/65 static-evidence
   checks, kernel module builds clean on 6.18.60 (0 warnings), inherited
   scan-tuning suites still green (37/37, 196/196), R11 ladder.

**Bottom line for the Lead:** do not spend more fires trying to reach 13/13 by
sending more subtype-1 slot configs - the firmware has no path from that
message to the scan count.  The R11 ladder is still worth one short wave
because it can (a) prove/disprove the pacing model and (b) close the H1 line
with a vendor-shaped frame; the honest ceiling remains 6 without H1 and 7-8
with it.

## 1. Inputs and pins

| input | pin |
| --- | --- |
| local vendor firmware (RE only, never committed) | `rootfs-2.5.24/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e2051e80b5e...`; CA = file offset + 0x40000, Thumb-2 |
| local vendor host module (RE only, never committed) | `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko`, 3,564,728 B, ARM mode, not stripped; 2.5.24. Addresses below are `.text` offsets |
| R4 base tree | `opensource/docs/incoming/wifi-scan-tuning/src/` (10-file delta; `patches/0002-scan-tuning-on-polish.patch`) |
| host verification tree | hardened combined tree (/tmp/wifi-stability-patchcheck) + this lane's two files; 6.18.60 kernel tree at the project `KDIR` |
| static-evidence tool | `tools/fw_scan_full_re.py` (65/65 PASS; reads the firmware in place, asserts every CA cited here) |
| host selftest | `tools/scan_full_selftest.c` (90/90 PASS) |

Safety: no device access by this lane; `scan_band_cfg` default stays 0
(nothing transmitted); no vendor byte, firmware byte, calibration value or
secret is copied into this directory - the tool only prints addresses and
derived annotations.

## 2. Corrections to the R4 finding (wifi-scan-tuning)

| R4 claim | correction | evidence |
| --- | --- | --- |
| g0 dispatch table at CA 0x10CD08, row `{10, 0x5FF65}` | table CA **0x10CCFC**; the id-10 row is at **0x10CD5C**; 0x10CD08 is the id-1 row. Entry form: 12-byte `{u32 id; u32 fn; u32 aux}` (fn has the Thumb bit set) | [V] `tools/fw_scan_full_re.py` F1; raw words at 0x10CCFC.. |
| frame payload "0x18/36 B total" | vendor shape is **32 B payload / 44 B total** (`hcc_msg_alloc(32)` -> hdr[4..5] = 12 + 32). Handler reads payload+0x14/+0x15 = frame bytes 0x20/0x21 | [V] plat.ko `hcc_msg_alloc` CA 0x11D9C sets total=size+12; wifi.ko builders at 0x45A94/0x45D88 |
| subtype-2 flag is payload+0x08 | subtype-2 reads **payload+0x14** as a byte flag (0 = clear slot). payload+0x08 is read by **neither** subtype (0x5FF64 has no access to hdr+0x14) | [V] handler 0x6018A loads `[r4,#0x14]`; 0x5FF64 has no load of `[r4,#0x14]` |
| +0x0C -> +0xFC only "if zero" | exactly: `strh` at context+0x10 happens only if context+0xFC == 0 **or** payload+0x0C < context+0xFC (min rule); independently, context+0x1A = max(1,payload+0x0C) | [V] CA 0x600A2-0x600B2 |
| "+0x14 cmp field" | it is a u16 compared with the u16 at context+0x15/0x16; mismatch with context+0x12 != 0 takes the 0x950C4 side path (CA 0x60054..0x60086). For subtype 2 the same byte at +0x14 is the clear/arm flag | [V] CA 0x60048/0x6018A |
| "the message configures a scan-context slot seed" | it is the ADDBA-response/DELBA relay; the firmware maps the BA parameters into per-TID/link contexts at 0xEC+slot*0x20. The scan engine sees the **same layout** only because slot 0's context offsets coincide with the scan/pacing fields it uses | [V] builder identities + symmetric read sites; [I] same-object identity |

## 3. Handler CA 0x5FF64 - complete decode

### 3.1 Entry gates

    r0 == NULL                          -> log + return 0x64              CA 0x5FF6A
    hdr = hcc_msg_get_header(msg)                                        CA 0x5FF80
    module = 0x961AE(hdr[3])            -> NULL -> log + return 0x64      CA 0x5FF88
    band = [module+0x4B8]               -> NULL -> log + return 0x64      CA 0x5FF90
    payload[0] must be 3                -> else log + return              CA 0x5FFA4
    payload[1] = 1 -> subtype-1 path    CA 0x5FFCE
    payload[1] = 2 -> subtype-2 path    CA 0x60184
    else                                -> log + return                   CA 0x5FFB8

Payload = header + 0xC, so `payload+n = hdr+n+0xC`.

### 3.2 Subtype 1 (payload[1] = 1) - field map

| payload | wire offset | firmware action | CA |
| --- | --- | --- | --- |
| +0x00 u8 | hdr+0x0C | type, must be 3 | 0x5FFA4 |
| +0x01 u8 | hdr+0x0D | subtype 1 | 0x5FFAC |
| +0x02 u16 | hdr+0x0E | **object key**: `obj = 0xD6A1E(key)`; NULL -> log + return | 0x5FFCE/0x5FFD0 |
| +0x07 u8 | hdr+0x13 | **slot** 0..7; >7 -> log + return | 0x5FFF0/0x5FFF4 |
| +0x09 u8 | hdr+0x15 | **action**: nonzero -> 0x5FEC0(module,obj,slot) clear path, return; 0 -> configure path | 0x60032/0x601C2 |
| +0x0C u16 | hdr+0x18 | context+0x10 (min rule, load CA 0x600A8 store 0x600B2) and context+0x1A = max(1,value) (load 0x60096 store 0x6009E) | 0x6009E/0x600B2 |
| +0x10 u16 | hdr+0x1C | context+0x18 (load CA 0x6008E store 0x60092) | 0x60092 |
| +0x12 u8 | hdr+0x1E | context+0x17 (mode) | 0x600BA |
| - | - | context+0x13 = 2 (state/enable) | 0x600BE/0x600C0 |
| +0x14 u16 | hdr+0x20 | compared with u16 at context+0x15; mismatch + context+0x12!=0 -> 0x950C4(module,obj,slot,1,0x20) | 0x60048..0x60086 |
| +0x15 u8 | hdr+0x21 | logged only | 0x6005A/0x600E8 |
| +0x08 u8 | hdr+0x14 | **never read** on this path | - |

Context base arithmetic (verbatim): `lsl.w sb, sl, #5` (stride 0x20),
`add.w fp, sb, #0xEC`, `add fp, r5` -> `fp = obj + slot*0x20 + 0xEC`
(CA 0x6003E/0x60042/0x60046) [V].

After the writes the handler calls `0x95188(module, {slot, payload+0x0C,
obj+0x18, 0})` (CA 0x60110/0x60112), optionally `0x8D024(obj,slot,4/8)`
(CA 0x6014C), `0xB764C(obj)` (CA 0x60152) and `0xBCD54([module+0x4B8],
fp)` (CA 0x6015C) before returning 0.

### 3.3 Subtype 2 (payload[1] = 2) - clear/remove

| payload | action | CA |
| --- | --- | --- |
| +0x02 u16 | same object lookup 0xD6A1E | 0x60184/0x6018E |
| +0x07 u8 | slot (<=7) | 0x60186/0x601A6 |
| +0x14 u8 | flag: **0** -> `0x5FEC0(module,obj,slot)`; nonzero -> `0x5FF06(obj+slot*0x20+0x108 record)` | 0x6018A/0x601BC/0x601C8/0x601D6 |
| +0x08 u8 | never read | - |

`0x5FEC0` (CA 0x5FEC0) zeroes context+0x12 (CA 0x5FEE0), context+0x1A
(CA 0x5FEF0), context+0x05 (CA 0x5FEF2) and calls `0xBCD54([module+0x4B8],
context)`.  This is why the R4 H1 sequence (subtype 1 then subtype 2 with
flag 0) left only payload+0x10/+0x12 (and the min-rule +0x10 bytes) alive.

## 4. The per-slot context array

- Object table: `0xD6A1E(u16)` -> `0x94728(0,id)` -> `0xC1FF4(0x171510,id)`,
  a pool of **0x92 (146) objects of 0x388 B** at CA 0x126D18, built by the
  firmware discriminator init (CA 0x492B6: table {count=0x92 at +0, stride=0x388
  at +4, data base 0x126D18 at +0x14}), each object storing its own index at
  +0x18 (CA 0x48330) [V].  The identity/role of these objects is [I]
  (BA-session/user context is the consistent reading; see sec. 6).
- The context array starts at **object+0xEC**; **8 slots**, **stride 0x20**
  (CA 0x6003E/0x60042).  Slot n context = `obj + 0xEC + n*0x20`.
- Slot-0 context offsets **coincide exactly** with the fields the scan engine
  uses (see sec. 5): +0x10 (0xFC, start), +0x11 (0xFD, step/current),
  +0x12 (0xFE, end/count), +0x13 (0xFF, enable), +0x17 (0x103, mode),
  +0x18 (0x104, result counter), +0x1A (0x106, stride/default-6).
- The subtype-1 message writes **+0x10, +0x13, +0x17, +0x18, +0x1A** of the
  selected slot (plus the 0x95188 side path).  It **cannot** write +0x12 or the
  per-slot channel bindings +0x00/+0x02/+0x04 (those are initialized by the
  sibling g0 id 11 handler CA 0x5E2F4 -> `0x95188(module, payload)` with
  payload +1 slot / +2 u16 / +4 u16 / +6 flag [V]; exact semantics [U]).

## 5. How the scan engine consumes the slots - and why slots 1..7 cannot add channels

All [V] unless marked:

- Scan state object: `scan = [[module+0x4B8]+0x1A00]` (scheduler CA 0x63CD2;
  result loop CA 0x641C0: `sl = band+0x1A00; r4 = [sl]`) [V].
- Scheduler CA 0x63CB8: `index = [scan+0xFC] + [scan+0xFD]`
  (CA 0x63D10/0x63D14); abort if `[scan+0xFD] > [scan+0xFE]`
  (CA 0x63D34/0x63D3A); indexes the 13-entry variant array at VAP+0x440.
- Result loop CA 0x641B4: per-channel result counter `[scan+0x104]`
  (CA 0x6423C) vs expected `[scan+0x105]` (CA 0x64240); when satisfied it
  advances `[scan+0xFD]` (CA 0x64262) and stops when `[scan+0xFD] >=
  [scan+0xFE]` (CA 0x6429C).  Mode `[scan+0x103]` gates the batch/dwell
  branch (CA 0x643B4) whose stride is `[scan+0x106]`, default **6** written
  by the firmware itself when 0 (CA 0x643CC) - the origin of the "6-channel"
  number.
- **There is no slot selector.**  Scheduler and result loop read `scan+0xFC..`
  directly, i.e. they always use the context at offset 0xEC = slot 0.  The
  subtype-1 message can address slots 0..7, but nothing in the scan path picks
  slot n != 0.
- **No subtype-1 field reaches the count/end byte.**  A full-image scan for
  `strb [rX,#0xFE]` finds the clear helper only (CA 0x5FEE0, writes 0); the
  handler CA 0x5FF64..0x601FC contains no store to +0xFE
  (`tools/fw_scan_full_re.py` F7).  Hence a second slot config cannot make one
  13-entry request scan 13 channels [V for the writes; I for the conclusion].

**Answer to the lane question:** the correct single-request configuration is
not a multi-slot plan.  The message's reachable levers are only (a)
start/step bytes +0xFC/+0xFD via payload+0x0C (min rule), (b) mode +0x103 via
payload+0x12, (c) result-counter preset +0x104 via payload+0x10, (d)
stride +0x106 = max(1,payload+0x0C).  +0xFE (count) is firmware-owned.  So
"full 13/13 by programming slots 0..7" is not achievable with this message;
the R4 ceiling stands.

## 6. Vendor host cross-check - the message is a BA relay

The vendor 2.5.24 `hi5622v100_wifi.ko` (ARM mode, not stripped) has exactly
two explicit group-0 id 10 senders [V, `tools/fw_scan_full_re.py` F8]:

| builder | address | payload[0]/[1] | shape |
| --- | --- | --- | --- |
| `hmac_mgmt_rx_addba_rsp` | `.text` 0x45A94 | 3 / **1** | `hcc_msg_alloc(32)` -> 32 B payload, 44 B total; writes +0x02, +0x07, +0x09, +0x0C, +0x10, +0x12, +0x14, +0x15 |
| `hmac_mgmt_rx_delba` | `.text` 0x45D88 | 3 / **2** | 32 B payload, 44 B total; writes +0x02, +0x07, +0x08 (unused by fw), +0x14 |

`hcc_msg_alloc(size)` (plat.ko CA 0x11D9C) sets `hdr[4..5] = size + 12`, so
the vendor frames are **44 B**: 12-byte HCC header + 32-byte payload.  The R4
builder's 36 B frame was short of the vendor shape (it still fitted the
handler's reads because payload+0x14/+0x15 = frame bytes 32/33 < 36, but it
was not the vendor form).  The peer builders fill [user+0xA84] into payload+0x02
and the mgmt-rx record into +0x07/+0x09/+0x0C/+0x10/+0x12/+0x14/+0x15, i.e.
BA parameters relayed to the firmware; that is consistent with the object
being a BA-session context and with the 8 slots being per-TID/link contexts
[I].  Consequence for research: the values are not a channel list.  The R4
sweep was changing aliased pacing state, not programming channels.

## 7. What the H1 result means (pacing model)

R10b/R10c measured [V\*]: baseline (no H1) 6 channels/request; H1 subtype1
(start=12, field10=13, mode=2) -> 7; field10=25 -> 8 with results=0; mode=1 ->
4 dense channels/14 BSS; start/use2 inert; page golden, mask 0 on every rung.
The only H1 fields that survive the subtype-2 companion clear are
payload+0x10 -> context+0x18 (= scan +0x104: result counter preset) and
payload+0x12 -> +0x17 (= +0x103: mode).  Raising the result-counter preset
skips the per-channel wait for results (CA 0x6423C compares counter vs
expected) -> channels advance faster -> more channels fit the fixed host
window, with fewer/no decoded BSS.  This matches the sweep exactly (field10
25 = 8 channels + results=0; mode=1 = dwell/flush branch = fewer channels,
richest BSS) [I, high confidence].

## 8. Delivered artifacts and verification

| artifact | content |
| --- | --- |
| `src/hi5622_band_cfg.{c,h}` | refined builder: vendor-shaped 44 B frame, decoded field map, subtype-2 flag at +0x14, fail-closed validation (slot>7, bad subtype, short buffer) |
| `patches/0001-scan-full-vendor-shape-bandcfg.patch` | delta over the R4 tree (`hi5622_band_cfg.{c,h}`); applies cleanly to the R4 tree and to the hardened tree |
| `tools/scan_full_selftest.c` | 90/90 host checks (wire shape, every field, subtype-2 flag location, unused bytes zero, wide values, fail-closed) |
| `tools/fw_scan_full_re.py` | 65/65 static-evidence checks; regenerates every CA claim (F1-F8) from the local firmware (+ optional vendor .ko cross-check) |
| `tools/build.sh` | one-command rebuild: host tests + 6.18.60 module build + metadata |
| `TEST-LADDER-R11.md` | attended bench ladder (Lead fires), includes the falsifiers |
| `BUILD-NOTES.md` | build line, results, patch-stack note |
| `evidence/` | host-test output, build summary, cfg80211 imports, RE tool output |

Verified host-only: **module builds with 0 warnings** on the 6.18.60 tree
(vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, depends cfg80211, 10 cfg80211
imports, local artifact 979,512 B unstripped / 263,704 B stripped - local
evidence only, not committed); host selftest **90/90**; static evidence
**65/65**; inherited scan-tuning suite **37/37**, inherited scan-complete
suite **196/196**.

## 9. Open items / limits

| item | state |
| --- | --- |
| identity of the 146-object pool (BA user context vs other) | [I]; not needed for the builder, but it is the cleanest way to close the semantics |
| exact semantics of payload+0x10/+0x0C/+0x12 as BA parameters | [U]; the firmware write targets are [V], the protocol meaning is [I] |
| whether the scan engine's object IS the same object 0xD6A1E returns | [I] (field offsets coincide exactly; scheduler dereferences `[[module+0x4B8]+0x1A00]`); R11 ladder is written so mismatch is observable |
| id-11 sibling handler 0x5E2F4 writes the per-slot channel bindings via 0x95188 | [V] call path, [U] values; if a channel-plan encoding exists it is here, not in id 10 - a separate lane would need it |
| full 13/13 in one request | not reachable from this message [I]; needs a different host-visible firmware path or accept batching (13/13 via 3 requests is already the shipped fallback) |