# REPORT - the 0x07E7 DFS SET: frame, firmware writer, driver knob, build

**Lane:** `wifi-5g-dfs-set` (shared task **task-227**), 2026-11-26. **HOST-ONLY**:
static firmware decode + clean-room driver patch + host build. No device contact,
no fire. Grades: **[V]** re-derived by [tools/fw_dfs_set_re.py](tools/fw_dfs_set_re.py)
(89/89 selftest), **[V\*]** committed earlier lane/bench, **[I]** inference,
**[U]** needs a device read. All firmware addresses are CA = file offset + 0x40000
of the pinned 928,920 B image.

---

## 0. TL;DR

1. **The frame is decoded [V].** H2D group-0/id-1 config event, key at hdr+20 =
   **0x07E7**, u16 length at hdr+22 = 1, body at hdr+24 = **one byte**: `01` =
   ARM, `00` = CLEAR. 29 B total on the same carrier as the committed 0x00F7 /
   0x01F7 frames (12-B HCC header + 8-B token + key + len + body + 4 x 0xFF).
2. **The firmware worker is decoded [V].** Table A row 94 -> worker **CA 0xB58FC**.
   After three gates (hcc ctx byte +17 == 1; per-link word +0x4B0 != 0; the
   **started-module scan CA 0xA4C0E** needs >= 1 module ctx with `u8[ctx+0x0E]`
   in 1..3), the ARM writes **bit0 of six u16 registers of the band-1 SMAC
   sub-base CA 0x4010E000** - +0 := 1, +4 := 0, +8 := 1, +0xA0 := 1, +0xA4 := 0,
   +0xA8 := 1, preserving the other bits - and then sets the per-module
   **CAC-active flag** `u8[mctx+0x4A5] = 1` (gated by `u32[mctx+0x4A0] == 0`)
   through CA 0xF593C + 0x5F450. CLEAR writes bit0 = 0 on the six (only when it
   reads the ARM pattern back) and clears the flag via CA 0xF58D8 + 0x5E95C.
   **There are no channel/width/CAC/radar fields on the wire** - the channel and
   width ride in the preceding 0x0124/0x01F7 frames, the CAC duration
   (60 s / 600 s) and NOL (30 min) are **host-side** constants, and the radar
   detection itself is firmware-side (the six registers).
3. **The driver knob is implemented, fail-closed, and placed correctly.**
   `dfs_set` (0=off, 1=ARM, 2=ARM+CLEAR, 3=CLEAR, >3 refused) + the 29-B
   builder + a read-only probe pack (9 gates) + the post-read verification. The
   step is posted **after the 0x0065 VAP start and before the 0x0242 band
   start** - the only slot where the worker's own gate 3 can pass in our ladder,
   and still "CAC armed before the MAC start" like the vendor.
4. **The module is built on the byte-exact R5 base.**
   `/tmp/dfs-set-artifact/hi5622_wifi.ko` = **391,388 B, sha256 1b47f0daff7e...,
   66 params** (65 + dfs_set), `.text` `a8b737a46610...`; 84/84 C + 46/46 Python
   host checks. The .ko is local evidence only.
5. **The fire rung is ready** ([FIRE-RECIPE.md](FIRE-RECIPE.md)): load the R5
   ladder with `dfs_set=1`, read the six sub-base registers (+ the CAC flag)
   before/after, and check the page golden / AP-ENABLED / 5200 scan. The honest
   expectation: the rung proves the **proper firmware writer** of the band-1
   SMAC sub-base (the manual bar0write3 flip is retired); whether the beacon
   appears too is the deeper TBTT question - the CAC-completion path
   (`hmac_start_all_bss_of_band`) is the vendor's next step, not this frame's.

---

## 1. Why this lane exists (the bench state)

The 2026-11-26 21:00 bench entry: on the stock's working 5 GHz AP the band-1
SMAC sub-base `[0x4010E000]` reads **bit0 = 1** (phys 0x404C6000 = 1); on our
R5 boot it reads **0**, and a manual bar0write3 flip of +0 and +8 aligned those
two words but the 5200 MHz beacon still did not appear. The committed lanes:
* `../wifi-5g-smac-keys` T2/T5: the only stock-only key whose handler writes
  the band-1 SMAC sub-base is **0x07E7** (DFS CAC) - 226 instructions;
* `../wifi-5g-160-dfs`: the 0x07E7 body semantics, the CAC 60 s/600 s and the
  NOL 30 min, the worker CA and the helpers;
* `../wifi-r5-rebuild` + `../wifi-band1-start-send-5`: the byte-exact R5
  module chain (the ladder this lane extends);
* `../wifi-f1-from-capture`: the stock boot sends 0x07E7 **3 times** (104 s
  capture) - i.e. it is part of the normal 5 GHz AP start, not a DFS-channel-only
  event.

This lane decodes the writer end-to-end, implements the missing frame, and
builds the rung.

---

## 2. The firmware decode (all [V] unless marked)

### 2.1 Dispatcher and worker

The g0-id1 dispatcher (CA 0x43D58, committed wifi-5g-smac-keys T1) reads
`key = u16 hdr+20`, passes `payload = hdr+24`, and dispatches through table A
(CA 0x10C23C, 217 x 8-B rows `{u16 key; u16 pad; u32 worker|1}`). Row **94** is
key **0x07E7** -> worker **CA 0xB58FC**. The worker's ABI: `r0` = the HCC config
ctx, `r1 = u8 hdr[22]`, `r2 = hdr+24` (the worker uses only r0/r2).

### 2.2 The worker's gates

    B5900  ldrb r3,[r0,#17]     ; gate 1: config-ctx state byte
    B5904  cmp  r3,#1           ;   must be 1, else log 0x619 and return 0
    B5914  ldr  r3,[r0,#0x4B0]  ; gate 2: per-link context
    B5932  ldr  r0,[r3,#0xE20]  ;   the module-id list root
    B5936  bl   0xA4C0E         ; gate 3: started-module scan
    A4C1C  ldrb r3,[r7,#18]     ;   count
    A4C44  subs r3,#1
    A4C46  cmp  r3,#2           ;   state u8[ctx+0x0E] in 1..3
    A4C4A  addls r4,#1          ;   >= 1 started module ctx

Gate 3 is the one that matters for our ladder: it is the same "VAP state" idea
as the 0x0242 band-start resolver, and in our ladder only the **0x0065 VAP
start** worker (CA 0x46394) sets `mctx[0x0E] = 1`. Any 0x07E7 post before the
0x0065 is a no-op returning 0x64 (log 0x61F) - fail-closed.

### 2.3 The body and the arm pattern

`body[0] = u8(hdr+24)`; the branch is at 0xB5996 (`cmp r4,#0; beq clear`).
The six band-1 sub-base literals are loaded once (0xB595C/62/6A/72/7A/88 ->
literal pool 0xB5B7C..0xB5B90 = 0x4010E000/+4/+8/+A0/+A4/+A8). ARM writes (each
`(old & ~1) | bit`): **+0 := 1, +4 := 0, +8 := 1, +A0 := 1, +A4 := 0, +A8 := 1**
(sites 0xB59E8, 0xB59D6, 0xB59C6, 0xB5A18, 0xB5A08, 0xB59FA) and logs any
nonzero pre-state (line 0x24FF). Pattern **{1,0,1,1,0,1} = 0x2D**.

CLEAR reads the six; when the read pattern is exactly the ARM pattern it writes
bit0 = 0 on all six (0xB5A8E..0xB5AC8 + the shared store 0xB5A18); a mismatch
takes a diagnostic log (line 0x2514) without writing.

### 2.4 The context flag and helpers

After the register writes the worker resolves the module context
(`0x961AE(u8[hcc_ctx])`) and then:

| body | path |
| --- | --- |
| ARM | `u32[mctx+0x4A0]` must be 0 and the flag `u8[mctx+0x4A5]` must be 0; then `CA 0xF593C(u32[mctx+0x4AC], u8[mctx+4])` + `CA 0x5F450(mctx)`; success -> flag = 1 |
| CLEAR | `u32[mctx+0x4A0] != 0` and flag == 1; then `CA 0xF58D8(u32[mctx+0x4AC])` + `CA 0x5E95C(mctx)`; success -> flag = 0 |

The register writes happen **before** the flag logic, so even an "already
active" ARM re-normalises the six bit0s (and returns without changing the flag).

### 2.5 The host-side CAC machinery (quoted from the committed lane, NOT re-derived)

The 160-dfs lane decoded the host caller: `hmac_dfs_cac_start` (CA 0xAD420)
sends 0x07E7{01}, stores the band/channel, arms a 60 s (or 10 min TDWR) CAC
timer and reports; `hmac_dfs_cac_timeout_fn` (CA 0xAE584) re-arms per tick and
on completion sends 0x07E7{00} + `hmac_start_all_bss_of_band` (the beacons
start); `hmac_dfs_cac_stop` (0xACD2C) aborts the same way. Constants: CAC
**60,000 ms** (UNII-2/2e) / **600,000 ms** (5600..5650 MHz window), NOL
**1,800,000 ms**. This lane deliberately does **not** implement that state
machine: the missing piece on our ladder is the firmware sub-base writer, and
the frame is the whole wire interface to it.

---

## 3. The driver integration

### 3.1 The two keys are different

| | `dfs_mode` (committed, R5 ladder) | `dfs_set` (this lane) |
| --- | --- | --- |
| key | **0x01F7** | **0x07E7** |
| body | {03} = RF/DFS mode 3 | {01} ARM / {00} CLEAR |
| firmware writer | CA 0xB5B9C: band object RF mode byte +0xF04 + the ch36-64 RF register image R+0x3A8.. | CA 0xB58FC: band-1 SMAC sub-base six u16 bit0s + CAC-active flag |
| role | programs the 5 GHz RF | the SMAC sub-base's only firmware writer |
| order | after 0x00F7, before 0x0124 | after 0x0124/0x0065, before 0x0242 |

They share only the g0-id1 carrier geometry. `dfs_set=1` runs **in addition**
to `dfs_mode=1`; the R5 ladder's other knobs are unchanged.

### 3.2 The build

`hi5622_dfs_set.h/.c` (new, pure C99) hold the fail-closed plan
(`hi5622_dfs_set_plan`), the 29-B frame builder (`hi5622_dfs_set_build`), the
six CA constants + arm-pattern helpers, the pre/postcheck gate model, the
verdict strings and the status line. `hi5622_scan.c` owns the parameter,
the read-only probe (`hi5622_scan_dfs_set_probe`), the post site and the
post-read dmesg evidence. `Kbuild` gains `hi5622_dfs_set.o`. The patch is
`patches/0001-dfs-set.patch` (33,313 B); [BUILD-NOTES.md](BUILD-NOTES.md) has
the base pins and the reproduce command.

### 3.3 Placement

`vap_add -> desc_table(0x00F7) -> dfs_mode(0x01F7) -> ap_chan(0x0124) ->
ap_ssid -> ap_cfg(0x0127) -> vap_start_g0(0x0065) -> dfs_set(0x07E7) ->
band_start(0x0242) -> ap_vap_start`

The worker's gate 3 forces the position: it needs `mctx[0x0E] in 1..3`, which
only the 0x0065 sets in our ladder (the vendor gets that state earlier through
the 0x0069 VAP FSM). Posting it after the 0x0242 would also lose the vendor's
"CAC armed before the MAC start" semantics. The build's Python test asserts
this order in the source.

### 3.4 The fail-closed gates

`dfs_set` 0..3 (0444, default 0); >3 refuses -EINVAL before any frame. Every
non-zero value additionally requires `beacon_static=1` and a band-1 AP. The
pre-post probe (read-only firmware RAM + the six device register bit0s) is
TABLES T5: mctx, helper object, band, `mctx[0x0E]`, `bandobj+0xEC8`,
`mctx[0x4A0]`, CAC flag, and (CLEAR) the read-back pattern. The post-read must
show CAC flag 1 after ARM / 0 after CLEAR, else the whole AP-start sequence
aborts (no band start on an unverified writer). CLEAR-only (`dfs_set=3`) on a
fresh boot refuses (-EPERM) by design - it is the fail-closed negative test.

---

## 4. The build (host-only)

Base: the byte-exact R5 tree (`/tmp/r5-build`) whose rebuilt module is
383,240 B / `24f7221b...` (wifi-r5-rebuild CHAIN.md). The build script asserts
the base module sha, the two pristine R5 file shas, the patch sha, then builds
6.18.60, strips, checks 66 params + the new strings, and runs the host tests.

| item | value |
| --- | --- |
| module | `/tmp/dfs-set-artifact/hi5622_wifi.ko` |
| size / params | **391,388 B / 66** (65 + `dfs_set`) |
| sha256 | `1b47f0daff7e2e48d0ac6dcbc34b4135872f6a16088e8d20b88c71d47c684acb` |
| .text sha256 | `a8b737a466106cfed51fecc54f4e836a23b6e2509056681a76feaeb3f558d335` |
| host tests | **84/84** C contract ([evidence/host-selftest-c.txt](evidence/host-selftest-c.txt)) + **46/46** independent Python integration ([evidence/host-test-python.txt](evidence/host-test-python.txt)) |

`tools/build-dfs-set.sh` reproduces all of it (and re-runs
`../wifi-r5-rebuild/tools/reproduce.sh` if the R5 base is absent).

---

## 5. Fire rung (summary; the exact recipe is [FIRE-RECIPE.md](FIRE-RECIPE.md))

One fresh boot, the R5 load line + `dfs_set=1`, the ch40 AP start. Expected
dmesg (between the 0x0065 and 0x0242 lines): `dfs_set pre:` (gates open,
`state=1 ec8=1 b4a0=0 cac=0`), the `dfs_set_arm` frame posted (29 B, key
0x07E7, body 0x01), and `dfs_set arm post: cac=1 regs_pattern=0x2d (PASS - the
firmware armed the band-1 SMAC sub-base + CAC flag)`. Host probes: the six
sub-base registers at phys 0x404C6000/04/08/A0/A4/A8 with bit0s
`{1,0,1,1,0,1}` (+0x08 raw 0xFFFF on our boot), the CAC flag
`u32(MCTX+0x4A5)`, the band-1 MAC enable 0x404C4220, the RF mode/R image, the
page golden and the 5200 MHz scan. The manual bar0write3 path is **retired** -
this rung uses the firmware's own writer.

---

## 6. Open items / limits

* **The beacon is not guaranteed by this frame alone.** The vendor's chain after
  CAC completion is 0x07E7{00} + `hmac_start_all_bss_of_band`; our driver does
  not run the CAC timer / NOL / completion FSM. The rung's PASS is the
  **firmware writer verified**, not necessarily the beacon. If the beacon
  appears, the arm path also kicked the SMAC/TBTT init - record it as the
  jackpot.
* **[U]** the other three registers (+A0/+A4/+A8) were never read on the bench;
  the fire probes them for the first time.
* **[U]** whether the stock's +0x08 word ended at 0x1 because its other 15 bits
  were 0 at arm time (bit0 is the semantic match; we cannot see the stock's
  pre-state).
* **[I]** the exact semantics of the six bits beyond "radar detect enable"
  (the committed 160-dfs lane's reading) and of the helper pair CA
  0xF593C/0x5F450 vs 0xF58D8/0x5E95C (arm vs clear side effects on the
  firmware detector state).
* The full CAC state machine (timer, NOL list, radar D2H event) remains a later
  lane; the committed 160-dfs REPORT-DFS.md maps the host functions and the D2H
  handler but not the D2H event id.
