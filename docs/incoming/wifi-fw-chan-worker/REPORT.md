# REPORT - the firmware 0x0124 channel-config worker: what it stores, what it does not, and why band 1 is a host-gate problem

**Lane:** wifi-fw-chan-worker (shared task **task-180**). **HOST-ONLY**: this
lane read the pinned local vendor firmware image and vendor host module in
place; it never contacted the router, never flashed and never fired. **The
Lead fires.**

**Why this lane exists.** The 5 GHz AP reaches AP-ENABLED with the full vendor
order and the page stays golden, but no 5 GHz beacon reaches the air. The
firmware module channel context read back as `chan=0x00000124` with what the
previous lane called `valid=0`, and the G6b rung (`ap_chan_wait_ms=300`, the
"vendor-synchronous channel activation") was REFUSED after 300 ms with
`word=0x00000124` (Lead's fire log, mem-entries 2026-11-27 05:30). This lane
decodes the worker that writes that word and settles what the word means.

**Source artifacts (local, never committed; read in place):**

| artifact | identity | use |
| --- | --- | --- |
| firmware image `FIRMWARE.bin` | 928,920 B, sha256 `7fc87e20...c3ef311b`, CPU = file + 0x40000 | primary disassembly |
| bench copy `pd4-rebuilt.bin` | 928,920 B, sha256 `cfe24bfb...3037b1` | proves the bench image does not differ in any address this lane decodes (`--pd4` = 89/89) |
| vendor host module `hi5622v100_wifi.ko` | 3,581,748 B, sha256 `22fa789a...532c7d2` (2.4.15 build, unstripped) | the vendor's synchronous cfg-event transaction |
| G6a / G6b fire lines (`mem-entries`, 2026-11-27 05:00 / 05:30) | bench, by the Lead | the evidence this lane explains |

The reproduction is `tools/fw_chan_worker_re.py` (`--selftest` = **89/89 PASS**,
`evidence/selftest.txt`; the other modes are `--decode`, `--tables`,
`--readback`). Every address below was re-derived from the pinned image with
that tool; nothing vendor-derived is committed as bytes.

Evidence markers: **[V]** re-derived in this lane and asserted by the selftest;
**[V\*]** bench/pack-measured by a committed lane or the Lead; **[I]**
inference; **[U]** undecided without a device read.

---

## 0. TL;DR

1. **The worker is fully decoded** [V]. The group-0 id-1 dispatcher
   (CA 0x43D58) reads the key `u16 payload+0x14`, looks it up in key table A
   (CA 0x10C23C; key 0x0124 = A[108]), and calls **worker CA 0x841B8** with
   `(module_ctx, subkey, body=payload+0x18)`. The 4-byte body is our ap_chan
   body `{band, field, channel, 0}`.
2. **The worker's stores are decoded** [V]: after a per-link gate
   (`module+0x4B0 != 0`, else return 0x64) and a channel validation
   (CA 0x10172C; band-0 table = 14 channels 1..14, band-1 table = 29 channels
   36..196; miss returns 0x3E8), it stores
   `body[2] -> ctx+0x10` (channel), `body[0] -> ctx+0x11` (band),
   `body[1] -> ctx+0x12` (the vendor chip-id/per-VAP byte), and
   **`<table index> -> ctx+0x13`**, then runs the post-store device work and
   returns 0.
3. **`ctx+0x13` is NOT a validity flag. It is the channel's index in the
   per-band channel table** [V]. Band 1 channel 36 is index 0 by construction,
   so the previous gate ("byte+0x13 must be 1") **could never pass for ch36**,
   no matter how long the wait: `chan=0x00000124` with byte 0 is the
   *correct, successful* store for band 1 ch36. **The G6b REFUSED line is a
   host-side gate bug, not firmware evidence, and the G6b timing candidate has
   not actually been tested on the bench.** [V] decode + [V\*] fire line.
4. **The worker's device-side work is conditional and silent** [V]. After the
   stores it calls CA 0xFDCE4 (VAP object band-class field), CA 0xF4FD8
   (module+0x4AC bookkeeping), **CA 0x40684** (only when `module+4`, the VAP
   create type, is 2, **and** CA 0x944DE returns 1; then it runs
   **CA 0x101890, the band-1 RF programme**), and CA 0x97008 (when
   `per-link+0x1C8 != 0`). CA 0x944DE requires `module+0x440 == 1` and
   `band_object+0xEC8 == 1`. None of this is reflected in the ctx word, and the
   worker returns 0 even when 0x101890 is skipped: **a stored ctx word proves
   the worker accepted the frame; it does not prove the 5 GHz RF programme
   ran.** The read-only pack in TABLES T7 and `--readback` decide that.
5. **Band 0 differs by design** [V]: CA 0x944DE returns 0 unless the freshly
   stored band is 1, and CA 0x101890 itself returns immediately unless band
   is 1. The band-1 path is the only one with the CA 0x101890 RF programme;
   band 0 applies its channel through the CA 0x97008 branch only. That is
   consistent with the bench: 2.4 GHz beacons on the same five frames, 5 GHz
   silent.
6. **The vendor's "synchronous path" is a request/response transaction** [V],
   decoded from the host module: `wal_send_cfg_event` posts the cfg chunk
   behind a 12-byte request prefix (`flag@0`, `u16 len@6`, `u32 seq@8`), the
   D2H completion handler copies the answer and completes the pending request,
   and `wal_check_and_release_msg_resp` releases it and returns the answer
   pointer. The channel helper only requires the answer to arrive; it
   **discards the answer body**. So the answer is an ordering trigger, not a
   data input. See section 7 for the candidate host trigger.
7. **Deliverables:** this report, TABLES (addresses, tables, store map,
   read-only pack), the runnable decode/selftest tool, and
   **`FIRE-RECIPE.md`**: a **zero-rebuild control** (same bench module, 5 GHz
   AP on **ch40** -> index 1 -> the old gate literally passes), the
   corrected-gate rung, and the read-only 5 GHz RF-gate pack with its decision
   tree.

---

## 1. Dispatch: how a posted 0x0124 reaches the worker [V]

The g0 id-1 dispatcher CA 0x43D58 (prologue `70b5`, `bl 0x409EA` at
0x43D6C, exact windows in TABLES T1):

1. `ctx = lookup(0, payload[3])` (CA 0xFE578: the user-manager context table
   `G + 0x18C + id` with the common offset `u16 G+0x6DC`; `G = CPU 0x170E28`,
   committed by the PD4/ctx-create lanes). `payload[3]` is the module index
   our frames already carry; for the AP it is 0.
2. `key = u16 payload+0x14`; scan **key table A CA 0x10C23C** (217 rows of
   `{u16 key; u16 pad; u32 worker|thumb}`), then **table B CA 0x103BF8**
   (88 rows). Key **0x0124 -> A[108] -> worker CA 0x841B9**.
3. `subkey = u8 payload+0x16`; body = `payload+0x18`. The channel worker
   ignores the subkey.
4. Calls `worker(ctx, subkey, body)`; **the worker's return value is the
   dispatcher status**: 0 = success, anything else is logged and returned.
   There is a sibling worker CA 0x84261 (key 0x03E4, table A[8]) with the same
   body store but **no** post-store device calls - a useful negative control
   for the store map.

The wire/body map is the same one the committed beacon lanes established
(`wifi-beacon-static` and `wifi-5ghz-beacon`): our `ap_chan` 4-byte body
`{band, vap_id, channel, 0}` is byte-identical to the vendor's for a single
AP on chip 0, and it lands at `payload+0x18`.

## 2. The worker CA 0x841B8, line by line [V]

    r7 = [module+0x4B0]                 ; per-link context (the band's bank context)
    r4 = module; r5 = body
    if (r7 == 0)  -> log, return 0x64   ; ERR_PERLINK_NULL, NO store
    validate(band=body[0], channel=body[2], &index)  ; CA 0x10172C
    if (status != 0) -> log, return 0x3E8            ; ERR_BAD_CHANNEL, NO store
    ctx+0x10 = body[2]                  ; channel
    ctx+0x11 = body[0]                  ; band
    ctx+0x13 = index                    ; <-- the channel-table index
    ctx+0x12 = body[1]                  ; vendor chip-id/per-VAP byte
    FDCE4(module)                       ; VAP object band-class field
    F4FD8(module+0x4AC)                 ; bookkeeping object path
    C40684(module, perlink+0xE20, perlink)  ; conditional 5 GHz RF programme
    if (perlink+0x1C8 != 0) C97008(module)  ; band/link channel switch
    ctx+0x17 = 0
    return 0                            ; success, regardless of the RF branch

**Gate semantics.** Both failure returns happen *before* any store, so a
ctx word equal to the request is proof that the worker got past both gates and
the validation. The success return is 0 even when CA 0x40684 skips its body
(see section 5), therefore the return code alone does not prove the RF
programme either; but every failure that matters for activation would skip
the store entirely.

## 3. The validation helper CA 0x10172C and the two channel tables [V]

`validate(band r0, channel r1, u8 *out r2)`:

* band 0 -> 14-entry table **CA 0x117B48** = `1 2 3 4 5 6 7 8 9 10 11 12 13 14`
  (stride 4, first byte of each word);
* band 1 -> 29-entry table **CA 0x117AD4** =
  `36 40 44 48 52 56 60 64 100 104 108 112 116 120 124 128 132 136 140 144
  149 153 157 161 165 184 188 192 196`;
* on a match: return 0 and `*out = the 0-based table index`;
* any other band or an unmatched channel: return **0x3E8** and **do not write
  `*out`** (the caller pre-zeroes the slot, and then never stores it).

So `ctx+0x13` is a table index in `0..28` for band 1 and `0..13` for band 0.
The callers of CA 0x10172C are the 0x0124 worker, the sibling worker (key
0x03E4) and CA 0x101890 (which re-computes the same index for the RF
parameter lookup).

## 4. The context word and the G6b proof [V]

The stores are four separate `strb`s in this order: `+0x10`, `+0x11`, `+0x13`,
`+0x12` (TABLES T2). Read as a u32 at `ctx+0x10`, the word is

    word = channel | (band << 8) | (body[1] << 16) | (index << 24)

| request | index | stored word | old gate (`byte+0x13 == 1`) |
| --- | ---: | ---: | --- |
| band 1, ch 36 (the G6a/G6b 5 GHz AP) | 0 | **0x00000124** | **REFUSE** (the G6b line) |
| band 1, ch 40 | 1 | 0x01000128 | PASS |
| band 1, ch 196 | 28 | 0x1c0001c4 | REFUSE |
| band 0, ch 1 | 0 | 0x00000001 | REFUSE |
| band 0, ch 6 | 5 | 0x05000006 | REFUSE |

The Lead's G6a gate pack read `chan=0x00000124 (channel=36 band=1 field=0
valid=0)` [V\*] - i.e. **the exact successful store for band 1 ch36**, with
index 0. The G6b wait then required `(word>>24)&0xff == 1`; for ch36 that is
impossible, so the "did not report channel=36 band=1 valid after 300 ms
(word=0x00000124)" REFUSED (mem-entries 2026-11-27 05:30) is a **host-side
gate bug**: the firmware had already stored the complete, valid context before
the first poll, and no amount of waiting could ever satisfy the gate.

**Consequences for the bench record:**

* the G6b rung's negative is void, not a firmware negative: the synchronous
  ap_chan wait was never allowed to complete, so the AP frames were never
  posted after a completed wait. The timing/ordering candidate is untested;
* the G6a conclusion "the 5 GHz RF/channel is never programmed" is **not**
  proven by `valid=0`: every byte of the store is present and correct. Whether
  the RF programme ran is decided by the CA 0x40684 gates (section 5), which
  the G6a pack did not read.

## 5. The post-store device work: the 5 GHz RF branch and its silent gates [V]

**CA 0xFDCE4(module).** Reads `module+0x11` (band) and `module+0x12`
(body[1]); computes a 2-bit band class and writes it into the VAP object
`[module+0x190]+0x1A0` bits 1..7. No RF call.

**CA 0xF4FD8(module+0x4AC).** Operates on the module+0x4AC object; no RF call.
(Full semantics of the +0x4AC object are out of scope here.)

**CA 0x40684(module, perlink+0xE20, perlink)** - the only 5 GHz RF path:

    if (module[4] != 2) return;                 ; create type, not AP
    if (CA 0x944DE(module) != 1) return;        ; the 5 GHz gate below
    r6 = CA 0x101890(band=module+0x11, field=module+0x12, channel=module+0x10)
    CA 0xA2FA2(perlink+0xE20, module)
    CA 0x89840(perlink, r6)
    [cleanup calls for three per-link flags]    ; CA 0xC52B4 x3

**CA 0x944DE(module)** is the gate:

    r0 = CA 0xBCDC4(module[0])                  ; CA 0xBCDC4 = lookup(id) -> [ctx+0x4B8]
    if (module+0x11 != 1) return 0;             ; only the 5 GHz store qualifies
    if (module+0x440 != 1) return 0;            ; module+0x440 = band-config flag
    return [r0 + 0xEC8];                        ; band object's +0xEC8 byte

(CA 0xBCDC4 compiled to `lookup(id); return ctx ? ctx+0x4B8 : 0` - the band
object bound at VAP-add time. The caller then compares the returned byte with
1.)

**CA 0x101890(band, field, channel)** is **band-1 only**: `if (band != 1)
return 0`. It re-validates `(band, channel)` through CA 0x10172C, uses the
returned index to select a parameter block from the static table **CA
0x10BE68** (word at CA 0x101BA0 = 0x0010BE68), walks the block through the
per-RF-descriptor mapper CA 0x101650 (which bounds the index to <= 0x1C for
the band-1 table), and returns 1 when one of the checked RF descriptors has
bit 7 set; that return is handed to CA 0x89840(perlink, r6). The exact RF
register writes behind CA 0x101650/CA 0x89840 are **[U]** for this lane (they
are the next firmware-RF decode step).

**CA 0x97008(module)** is called for **both** bands when
`perlink+0x1C8 != 0`. It requires `module[4]` in {1, 2}, reads the stored
channel word `[module+0x10]` (using the low byte as the channel), and applies
it to the band object `[module+0x4B8]`/per-link `[module+0x4B0]` through
CA 0x5FA6C/CA 0x5FB18/CA 0xB819C. This is the general channel-apply path;
the CA 0x101890 branch is the extra 5 GHz RF programme.

**The silent failure mode.** If `module[4] != 2`, or `module+0x440 != 1`, or
`bandobj+0xEC8 != 1`, the CA 0x101890 call is simply skipped: no log, no
error, the ctx word unchanged, the worker returns 0. That is the shape that
matches the bench (accepted 0x0124 + stored ctx, no 5 GHz RF/beacons) without
contradicting the G6a live band contexts.

## 6. Why band 0 behaves differently [V]

* CA 0x944DE returns 0 unless `module+0x11 == 1`; the worker stores the new
  band just before this call, so on a **band-0** 0x0124 the gate returns 0
  immediately and CA 0x40684 returns without calling CA 0x101890.
* CA 0x101890 itself returns 0 unless the band is 1.
* The 0x0124 worker's stores and CA 0xFDCE4/CA 0xF4FD8 run for both bands, and
  the CA 0x97008 channel-apply branch runs when its per-link flag is set.

So the 5 GHz-specific RF programme is a design difference, not a race: band 0
never runs CA 0x101890. That is consistent with the bench's 2.4 GHz beacons on
the same five-frame order while 5 GHz is silent. It also means a 5 GHz
"valid" gate cannot be satisfied by anything a band-0 start does.

## 7. What makes "valid=1", and the vendor's synchronous response [V] + [U]

**Answer to the task question:** nothing "makes valid=1". The byte is the
channel-table index; it is 0 for band 1 ch36 (and for band 0 ch1), 1 for band
1 ch40, and so on. The worker's real completion signals are (a) the ctx store
itself (only reached after both gates and the validation), and (b) the
dispatcher status (0) which the firmware returns. The previous rung's gate
conflated the index with a flag.

**The vendor's synchronous path, decoded host-side [V]** (the local
`hi5622v100_wifi.ko` 2.4.15 build, link-time `.text` offsets; windows asserted
by the selftest):

* the channel helper builds the 8-byte cfg payload `{u16 cmd=0x0124, u16
  len=4, band, chip, channel, 0}` (the same four body bytes we post) and calls
  `wal_send_cfg_event(dev, 1, 8, payload, 1, &resp)`;
* `wal_send_cfg_event` (0x126C98) allocates a request entry with a 12-byte
  prefix: byte 0 = a flag (its arg1), `u16 len` at +6, `u32 seq` at +8 (from
  the global `g_wal_config_seq_num`), copies the payload at +12, then sends
  through `wal_send_cfg_event_msg_tx`; in sync mode (arg5 == 1) it waits for
  the entry to be completed and returns the response handle through the
  caller's out pointer;
* the D2H completion path (`wal_recv_memory_cmd` family, calls at 0xFBAA0/
  0xFBB40) copies the answer body into a fresh buffer and calls
  `wal_set_host_msg_response_by_addr(addr, seq, data, len)` (0x1269D8), which
  completes the matching pending entry;
* `wal_check_and_release_msg_resp` (0x109370) then frees the request and
  returns the answer data pointer; the channel helper at 0x102F48 uses only
  "an answer arrived" - it does not read the answer body [V].

So the vendor's trigger is **ordering**, and the answer **body is not an input
to the firmware**. What the vendor gets that our ladder does not is a
guarantee that the *whole worker* (stores **and** the post-store RF calls)
finished before the next AP frame goes out: the ctx word becomes correct at
the stores, which is *earlier* than the worker's return, while the D2H answer
is emitted after the worker returns.

**Candidate host trigger to test.** Two levels, both fail-closed:

* **CW-1 (small patch):** fix the host gate to require the decoded store
  (`word == channel | band<<8 | body[1]<<16 | index(band,channel)<<24`), then -
  because the store precedes the RF work - add a bounded post-store settle
  before the AP frames (the vendor's wait is the full worker; the decode
  cannot tell how long CA 0x101890 takes). Measure with `ap_chan_wait_ms`.
* **CW-2 (vendor-faithful, larger):** populate the request prefix our frames
  currently zero (the h2d-frames decode documents the payload's request/token
  bytes) and capture the D2H answer that the firmware returns for the 0x0124
  cfg event (the driver already has a read-only D2H service; the rate-ctl
  lane's g1 id3 answer capture is the analogous observable for the g0 id2
  carrier), then use "answer arrived" as the completion signal instead of any
  ctx poll. This is the exact vendor trigger; the wire shape of the answer is
  **[U]** (partially decoded; see TABLES T6).

## 8. The fire plan in one paragraph

The full rung is `FIRE-RECIPE.md`. The **cheapest decisive test needs no
rebuild**: the G6a module is still valid, and the old gate passes for any
channel whose band-1 index is 1 - that is **ch40**. Fire the same 5 GHz AP
rung with the 5 GHz channel set to 40 and `ap_chan_wait_ms=300`: the wait
passes (word 0x01000128, byte+0x13 = 1), the AP frames are posted *after* a
completed 300 ms wait, and the observer scan looks on 5200 MHz. If `cudy-ap-
test-5g` beacons there, the ordering candidate is real and ch36 only failed
because of the host gate; if it stays silent, the wait is not the lever and
the read-only RF-gate pack (below) decides whether CA 0x101890 ran. The
patched module (CW-1) then re-runs the same rung on ch36 with the decoded
gate, and the read-only pack classifies the result.

**Read-only 5 GHz RF-gate pack** (all CPU addresses; BAR0 = CPU + 0x6B8000;
the G6a-expected values are from the Lead's gate pack): module ctx word
CA 0x119768, index byte 0x11976B, VAP pointer 0x1198E8, per-link 0x119C08,
band object 0x119C10, `module+0x440` CA 0x119B98, band-object `+0xEC8`
CA 0x15F438 (BAR0 0x817438), band-object `+0x19A1` CA 0x15FF11, per-link
`+0x1C8` CA 0x175BE0 (BAR0 0x82DBE0), publish words 0x15E408/0x15FF70.
Decision:

| readback | verdict |
| --- | --- |
| `module[4] != 2` | the VAP create type is not the AP type -> host bug candidate, stop |
| `module+0x440 != 1` or `bandobj+0xEC8 != 1` | **CA 0x101890 was skipped**; the 5 GHz failure is this gate -> next lane decodes who sets these bytes |
| both 1, wait passed, no beacon | the RF programme ran; the gap is inside CA 0x101890/CA 0x101650 or the beacon engine -> firmware-RF lane |
| both 1, wait passed, beacon on air | **PASS** (the timing/ordering candidate was the fix) |

## 9. Limits / honest unknowns

* The exact RF register effects of CA 0x101890 -> CA 0x101650 -> CA 0x89840
  are **[U]** here: this lane decodes the worker up to the RF-programme entry,
  the gates, and the parameter-table use, not the RF mailbox transaction
  itself.
* The semantic labels of `module+0x440` (written from the band/config-apply
  path CA 0x455D0 as `[band_object+0x19A1]`, and by the config path CA 0x7100C
  from a config block `+0x30..`) and of `band_object+0xEC8` are **[I]**: the
  code proves the gate equation, not the variable names. The fire read decides
  the values.
* The D2H answer wire body for a g0 id1 cfg event is **[U]**; the host-side
  transaction (request prefix, completion, release) is **[V]** from the vendor
  module.
* The G6a/G6b quotes are the Lead's recorded fire lines [V\*], not re-measured
  here. This lane fired nothing.
* A longer/conservative wait cannot be justified from the decode alone; the
  vendor-equivalent is the answer wait (CW-2), and the fixed settle in CW-1 is
  an explicitly temporary measurement knob.

## 10. Evidence produced by this lane

| evidence | result |
| --- | --- |
| `tools/fw_chan_worker_re.py --selftest --pd4 pd4-rebuilt.bin` | **89/89 PASS** (`evidence/selftest.txt`): firmware identity, all instruction windows, both channel tables, the index semantics, the old-vs-corrected gate, the post-store gates, the PD4 non-overlap, and the vendor-host symbol/code windows |
| `--decode` | the dispatch/store/post-store trace (`evidence/decode.txt`) |
| `--tables` | both firmware channel tables + ctx-word encoding + the G6b table (`evidence/tables.txt`) |
| `--readback` | the read-only BAR0 pack and the 5 GHz RF-gate decision tree (`evidence/readback.txt`) |
| `tools/run-host-tests.sh` | the full host verification: firmware decode **89/89** + portable C model **38/38** (`evidence/host-tests.txt`) |
| `tools/build-chan-worker.sh` | patch applies to the pinned beacon-lane base, clean module build (0 warnings/errors), debug-strip safe, **59 params**, decoded predicate linked; artifact 358,256 B sha256 `9a5b048e...775fa6` (local, uncommitted; `evidence/build-artifact.txt`) |
| fire | **not fired by this lane**: `FIRE-RECIPE.md` is the Lead's one-shot rung |

## 11. Sources

* pinned local firmware image + PD4 bench copy (identities above; read in
  place by the tool);
* pinned local vendor host module 2.4.15 (sha above; `.text` offsets:
  `wal_check_and_release_msg_resp` 0x109370, size 104;
  `wal_set_host_msg_response_by_addr` 0x1269D8, size 216; `wal_send_cfg_event`
  0x126C98, size 1140; `wal_alloc_cfg_event` 0x126AB0, size 340; the channel
  helper's cfg send at 0x102F20-0x102F50; the D2H completion call site at
  0xFBB40);
* committed lanes: `../wifi-beacon-static/REPORT.md` (the five-frame order and
  the worker body-store map; its "valid -> ctx+0x13" reading is corrected
  here), `../wifi-5ghz-beacon/REPORT.md` + TABLES (the synchronous ap_chan
  insight and the corrected readback constants), `../wifi-boot-gate/REPORT.md`
  (the band objects and publish words), `../wifi-ctx-create/REPORT.md` (key
  table A, the dispatcher and the create type), `../wifi-h2d-send/REPORT.md`
  (the H2D carrier that posts these frames);
* the Lead's G6a/G6b fire lines recorded in `mem-entries.md` (2026-11-27
  05:00 and 05:30 entries).
