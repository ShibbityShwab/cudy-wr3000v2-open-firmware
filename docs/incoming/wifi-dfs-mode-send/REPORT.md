# REPORT - the H2D 0x01F7 DFS/RF mode send (F1, implementation)

**Lane:** wifi-dfs-mode-send (shared task **task-190**), 2026-11-25.
**HOST-ONLY**: implementation, host build and host tests only; no device
contact, no flash, no fire. **The Lead fires.** No vendor binary, firmware
byte, calibration value, MAC or secret is stored here: the only bytes in this
lane are **host-built protocol bytes** (our own 0x01F7 frame).

Companions: [README.md](README.md), [TABLES.md](TABLES.md),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [BUILD-NOTES.md](BUILD-NOTES.md),
[patches/0001-dfs-mode-send.patch](patches/0001-dfs-mode-send.patch),
[tools/run-host-tests.sh](tools/run-host-tests.sh), [evidence/](evidence/).

Grades: **[V]** re-derived by the sibling lane cited; **[V*]** committed
sibling decode/measurement; **[I]** inference; **[U]** needs a device read.

## 0. Verdict

Implemented and host-verified: the clean-room driver can now post the
**29-byte H2D group-0 / id-1 config event with key 0x01F7, len 1, body
{0x03}** - the stock firmware's `set_country_for_dfs` / RF-mode arm message -
in the 5 GHz AP-start path, **after** the 0x00F7 descriptor-table send and
**before** the 0x0124 channel post, behind a new **fail-closed module
parameter `dfs_mode`** (default **0** = current behaviour, **1** = post on a
band-1 AP start, **>1 refused** before any frame).  The portable builder
emits the exact spec frame; the frame is the sibling 0x00F7 carrier with key,
length, body and total replaced (29 B, SR node word1 0x001D6D2B).  Host tests
pass 133/133 checks (frame bytes, the real frame_post validator, order,
fail-closed matrix, source
contract).  The built module is 364,192 B, sha256 `f5408567...66ac`, 61
params.  **Not fired**: the FIRE-RECIPE is for the Lead, with the corrected
read-only probe pack that closes or re-ranks F1.

## 1. Why this frame is F1 (committed sibling decode)

The 5 GHz AP reaches AP-ENABLED but never beacons.  The sibling rf-descriptor
lane (wifi-fw-rf-descriptor, task-189) corrected the RF chain and found:

* the band-1 RF programme **CA 0xA2FA2** (invoked from the band-1 branch
  CA 0x40684) returns **silently** unless `u8(P+0xF04) == 3`, where
  `perlink = u32(ctx+0x4B0)`, `P = u32(perlink+0xE20)` is the band object,
  `O = u32(P+0x1A00)` is its published per-link and `R = u32(perlink+0x2D8)`
  is the RF register block **[V]**;
* the mode byte `P+0xF04` has exactly **one writer in the image**: CA 0xB5D1C
  (`strb.w r5,[r4,#0xF04]`) inside **CA 0xB5B9C**, the worker for the
  H2D group-0/id-1 config event **key 0x01F7** (firmware table A row 93 at
  CA 0x10C524) **[V]**;
* the worker reads the mode from `body[0]` (1..5 via the `tbb` at
  CA 0xB5D72), writes the mode byte and programs the mode's whole RF register
  image `R+0x3A8..R+0x418` **[V]**;
* the only shipped host emitter is **hmac_config_set_country_for_dfs**
  (wifi.ko .text+0x5762C; the WAL command is named `set_country_for_dfs`):
  `.text+0x5763C movw r1,#0x01F7` -> `.text+0x57644 bl hmac_config_send_event`
  -> that constructor writes `u16 key` at hdr+20, `u16 len` at hdr+22 and the
  body at hdr+24 on the **same g0-id1 carrier** the clean driver already
  posts (0x0064/0x0072/0x0073/0x0124/0x0127) **[V]**;
* our five-frame ladder never sends it, so `P+0xF04` stays at its boot
  value (0) and CA 0xA2FA2 takes the silent `mode != 3` return - the F1
  candidate **[V]** emitter/worker, **[I]** that it is the remaining gate.

The stock order is country/regdomain init -> 0x00F7 descriptor table ->
**0x01F7 mode** -> 0x0124 channel (or a re-post after) **[I]**.  This lane
implements the 0x01F7 step in exactly that slot.

## 2. What was built

Patch `patches/0001-dfs-mode-send.patch` applies to the pinned base
**wifi-5ghz-beacon tree + the r1-band one-field VAP-start fix + the sibling
`patches/0001-desc-table-send.patch`** (the 0x00F7 descriptor-table send;
base module 60 params, 360,448 B sha `aac6177c...e800e3`) and changes
exactly six entries:

| file | change |
| --- | --- |
| **hi5622_dfs_mode.h** (new) | portable model: frame geometry/values, plan gate, worker precheck, builder, status line |
| **hi5622_dfs_mode.c** (new) | the 29-B wire builder + the fail-closed decision + sysfs status |
| hi5622_scan_logic.h | `HI5622_BEACON_STEP_DFS_MODE` added to the ordering plan enum; plan signature gains the dfs_mode knob |
| hi5622_scan_logic.c | plan inserts the step after `desc_table` and before `ap_chan` for `dfs_mode == 1`; step name "dfs_mode" |
| hi5622_scan.c | `dfs_mode` module param (uint, 0444, default 0); fail-closed parameter gate before any frame; plan call; the 0x01F7 post in the beacon_static branch after the 0x00F7 post and before `ap_chan`; read-only worker-precondition dmesg; sysfs status line |
| Kbuild | `hi5622_dfs_mode.o` |

The portable model compiles unchanged in the kernel module and in the host
selftest (pure C99, no kernel-only dependencies).

### 2.1 The frame - derived, byte-exact

The frame is the **same carrier as the sibling 0x00F7 frame** (12-B HCC
header + 8-B token/preamble + key u16 at hdr+20 + length u16 at hdr+22 +
body at hdr+24 + the inherited 4-byte tail), with key 0x01F7, len 1 and body
{0x03}.  Derivation:

* carrier geometry (committed, `../wifi-fw-desc-table/TABLES.md` T2 and the
  clean driver's `h2d_build_g0_cfg_event`): header 12 + token 8 + key 2 +
  len 2 + body + tail 4 -> **total = len + 28**;
* the generic g0-id1 builder's total is `len + 0x10 + 12 = len + 28`
  (`h2d_frames.h/c`), so a 1-byte body gives **29 = 0x001D**;
* header bytes are the sibling 0x00F7 frame's: group 0, alloc/dst `0x01`,
  fa 0x00, fb 0x04 (the vendor-wire value of the captured g0-id1 family),
  total 0x001D, message id 1, fc/dst 0, tag 0x5A5A;
* token `00 00 00 00 01 00 00 00` (the inherited bench-executed g0-id1
  pattern);
* key `f7 01`, len `01 00`, body `03`, tail `ff ff ff ff`;
* SR node word1 = `(total << 16) | 0x6D2B` = **0x001D6D2B** (the existing
  `hi5622_frame_post()` transport derives it; no new transport code).

```
00010004 1d00 0100 0000 5a5a
0000000001000000
f701
0100
03
ffffffff
```

Hex: `000100041d00010000005a5a0000000001000000f701010003ffffffff` (29 B).

The task brief estimated "~23 B"; the carrier identity (total = len + 28,
hdr total field = wire length) gives exactly 29 B.  The 29-byte total is
asserted in the model, the C builder, the independent Python re-derivation
and the source contract (TABLES T2).

### 2.2 The fail-closed parameter and the order

* `dfs_mode=0` (default): no 0x01F7 frame; the plan line reads the
  bench-fired 5 steps; behaviour is byte-identical to the desc-table-send
  base.
* `dfs_mode=1`: only on a **band-1** AP start with `beacon_static=1`; the
  frame is posted **after** the 0x00F7 descriptor-table step (when
  `desc_table=1`) and **before** the 0x0124 channel post.  Rationale: the
  stock order is regdomain init -> 0x00F7 -> 0x01F7 -> 0x0124, and the
  0x0124 worker's band-1 RF programme CA 0xA2FA2 evaluates the mode byte at
  post time, so the mode write must already have happened.
* `dfs_mode>1` or `dfs_mode=1` without `beacon_static=1`: **refused
  (-EINVAL) before any frame**, with a distinct dmesg line; nothing is
  silently ignored.
* Band-0 AP start with a valid parameter: clean no-post (per-band gate).
* The parameters are independent: `dfs_mode=1` without `desc_table=1` posts
  the 0x01F7 frame alone; when both are 1 the order is
  `vap_add -> desc_table -> dfs_mode -> ap_chan -> ap_ssid -> ap_cfg ->
  ap_vap_start`.
* Read-only sysfs status appended to `cfg80211_status`:
  `dfs_mode: param=1 (band1-post) beacon_static=1 last_rc=0 posted=1 refused=0`.
* A read-only dmesg line right after the post reports the two live
  preconditions of the 0x01F7 worker: the published per-link
  `u32(bandobj+0x1A00)` must be non-zero (else the worker logs 1442) and the
  band attach flag `u8(bandobj+0xD70)` must be 1 (else log 1448).  The line
  is diagnostic, not gating (matching the sibling 0x00F7 readback).

## 3. Host tests (no bench)

`tools/run-host-tests.sh` (all green, evidence/host-tests.txt):

1. **Portable model** (`src/dfs_mode_selftest.c`, 54 checks): the exact
   29-byte frame against a golden array and every header/field byte, the
   fail-closed plan matrix, the builder refusals, the worker precheck, the
   sysfs status format, and the `--dump` frame used by the cross-check.
2. **Real frame_post validator** (`tools/validate_frame_test.c` against the
   patched tree's `h2d_frames.c`, 15 checks): the exact same
   `h2d_frame_decode(H2D_VF_TRANSPORT | H2D_VF_FIRMWARE | H2D_VF_HANDLER)`
   call `hi5622_frame_post()` makes before any device write **accepts** the
   29-B frame (group 0 / id 1, payload 17, key 0x01F7, body len 1, body {3}),
   the firmware handler slot exists, the post node word1 formula gives
   0x001D6D2B, and corrupted total/tag frames are refused.
3. **Ordering contract** (`tools/order_test.c` against the real patched
   `hi5622_scan_logic.c`, 14 checks): `vap_add -> desc_table -> dfs_mode ->
   ap_chan -> ap_ssid -> ap_cfg -> ap_vap_start` for both knobs, each step
   only for its own value 1, the base ladder for 0/0, the ladder-isolation
   rung, truncation, enum ordering and the step name.
4. **Independent cross-check** (`tools/dfs_mode_send_test.py`, 50 checks):
   re-derives the 29-B frame from the committed sibling 0x00F7 carrier (same
   header/token/tail, key/len/body/total replaced) and requires the C
   builder's bytes to be identical; asserts the carrier identity
   `total == len + 28` and the node word1; and verifies in the patched
   `hi5622_scan.c` that the dfs_mode refusals run before the plan, that the
   **'dfs_mode' post line is after the 'desc_table' post line and before the
   'ap_chan' post line**, that the post is gated by the plan, and that the
   Kbuild/status/dmesg contract entries exist.
5. **Artifact contract** (when built): 61 params incl `dfs_mode`, the
   feature strings, 12 cfg80211 symvers.

## 4. Build and artifact (host-built, local evidence only, never committed)

`tools/build-dfs-mode-send.sh` pins the whole chain (r1-band base provider
and artifact, the sibling 0x00F7 patch and its two portable sources), applies
the sibling 0x00F7 patch, then this lane's patch, refuses source drift, builds
against the project 6.18.60 tree with the OpenWrt GCC 14.4 toolchain
(0 warnings/errors/undefined symbols), debug-strips (proves .text unchanged)
and checks the artifact contract.  Result (evidence/build-artifact.txt):

| field | value |
| --- | --- |
| stripped | **364,192 B** sha256 **`f540856776e77c00ff1a526582466763bac9a3e63834264099faf3a3a10266ac`** |
| unstripped | 1,381,900 B; .text sha256 `3fdcad2ddb1a0c2e81c3ca1dfb1f65058551e09976e3765b5d08081e7b2b6186` |
| params | **61** (60 + `dfs_mode`; `dfs_mode` uint, default 0) |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| size delta | +3,744 B vs the sibling 0x00F7 artifact |
| key immediate | `objdump -d hi5622_dfs_mode.o`: `movw r3, #503 @ 0x1f7` (evidence/objdump-key-1f7.txt) |

## 5. What the Lead must do (see FIRE-RECIPE.md)

Load the module with the beacon/r1-band line plus `beacon_static=1
beacon_vap_start=1 band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1
dfs_mode=1`, start the **ch40 (5200 MHz)** AP, then run the corrected
read-only probe pack: the 0x01F7 frame posted in dmesg before the `ap_chan`
line, the worker-precondition line, then `u8(P+0xF04)` (CPU 0x15F474) = **3**,
`R+0x3A8` = **0x01800060**, `R+0x3AC` = **0x03E6138A**, `R+0x410` **bit0 = 1**,
and a 5200 MHz beacon on the observer.  The dmesg `dfs_mode` post line and
the worker-precondition line are the host-side evidence that the frame was
committed and that the worker could write the mode byte; the probes are the
result.

## 6. Honest limits / limits of the claim

* **Not fired** by this lane; no bench contact at all.  Whether mode 3
  actually makes CA 0xA2FA2 write the ch36..64 image and start the beacon is
  **[U]** until the Lead's fire.
* The exact 1-byte mode the stock userspace passes to `set_country_for_dfs`
  is not statically recoverable (the WAL payload is forwarded verbatim).  F1
  pins it to **3** because CA 0xA2FA2 requires 3; FIRE-RECIPE lists the 1/2/4/5
  enumeration rung if needed.
* The 0x01F7 worker needs the published per-link and the band attach flag
  (worker logs 1442/1448 otherwise).  The dmesg readback makes that visible;
  a ZERO verdict means the mode byte was not written and no re-post will fix
  a missing band object (the probe decides).
* `hdr[3]=0x04` (fa/fb) is inherited from the sibling 0x00F7 carrier (the
  vendor-wire value of the captured g0-id1 family); the 0x01F7 wire frame has
  no capture of its own, so this byte is [V*] by family, not [V] by message.
  The firmware handler routes on group/id and the worker reads only
  `body[0]`; the probe/dmesg is the discriminator if the frame is refused.
* The mode's RF register image comes from the firmware worker (it writes
  R+0x3A8..R+0x418); the host does not pre-compute or verify it.
* No calibration, MAC, secret or vendor byte is used or stored; probe output
  stays in `build/` (gitignored) per the project rules.

## 7. Reproduction (host only)

```
./tools/build-dfs-mode-send.sh     # pinned chain + patches -> /tmp/dfsmode-artifact
./tools/run-host-tests.sh          # 133 checks, all green
```

The base chain itself is generated by
`../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh` then
`../wifi-r1-band-diff/tools/build-r1-band.sh` then the sibling
`../wifi-desc-table-send/tools/build-desc-table-send.sh` when
`/tmp/r1-band-build` is absent; the build script pins every base hash and
refuses drift.

## 8. Sources

* sibling lanes: `../wifi-fw-rf-descriptor/` (task-189: table-A row 93,
  worker CA 0xB5B9C, mode byte writer CA 0xB5D1C, corrected P/O/R chain, the
  stock hmac_config_set_country_for_dfs emitter, modes 1..5, mode 3
  required), `../wifi-fw-desc-table/` (task-187: the g0-id1 wire shape and
  the inherited tail), `../wifi-desc-table-send/` (task-188: the 68-B 0x00F7
  implementation this patch extends and the build-script pattern);
  `../wifi-fw-rf-programme/` (CA 0x40684/CA 0xA2FA2/R image);
  `../wifi-boot-gate/` (per-bank contexts, module+0x4B0/0x4B8 bind);
  `../wifi-datapath/tables/firmware-config-keys.md` (row 93 cross-check);
* pinned local firmware/module artifacts read in place by those lanes; this
  lane re-derives nothing from them and commits no vendor byte.
