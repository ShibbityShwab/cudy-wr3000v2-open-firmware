# REPORT - the H2D 0x00F7 descriptor-table send (the C1 fix, implementation)

**Lane:** wifi-desc-table-send (shared task **task-188**), 2026-11-25.
**HOST-ONLY**: implementation, host build and host tests only; no device
contact, no flash, no fire. **The Lead fires.** No vendor binary, firmware
byte, calibration value, MAC or secret is stored here: the only bytes in this
lane are **host-built protocol bytes** (our own 0x00F7 frame).

Companions: [README.md](README.md), [TABLES.md](TABLES.md),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [BUILD-NOTES.md](BUILD-NOTES.md),
[patches/0001-desc-table-send.patch](patches/0001-desc-table-send.patch),
[tools/run-host-tests.sh](tools/run-host-tests.sh),
[evidence/](evidence/).

Grades: **[V]** re-derived by the sibling lane cited; **[V*]** committed
sibling decode/measurement; **[I]** inference; **[U]** needs a device read.

## 0. Verdict

Implemented and host-verified: the clean-room driver can now post the
68-byte **H2D group-0 / id-1 config event with key 0x00F7** - the firmware's
descriptor-table (band/channel pool) install message - in the 5 GHz AP-start
path, immediately **before** the 0x0124 channel post, behind a new
**fail-closed module parameter `desc_table`** (default **0** = current
behaviour, **1** = post on a band-1 AP start, **>1 refused** before any
frame).  The portable builder emits the exact spec frame (cc **"US"**, 2
records, 5 GHz record word **0x00800201** armed, 5 GHz bitmap
**0x000000FF**); the blob is the firmware pool image for N=2.  Host tests
pass 108/108 checks (frame bytes, order, fail-closed matrix).  The built
module is 360,448 B, sha256 `aac6177c...e800e3`, 60 params.  **Not fired**:
the FIRE-RECIPE is for the Lead, with the read-only probe pack that closes or
re-ranks C1.

## 1. Why this frame is the C1 fix (committed sibling decode)

The problem (sibling lanes wifi-fw-rf-programme, wifi-fw-desc-pool): the
5 GHz AP reaches AP-ENABLED but never beacons.  The firmware's 5 GHz-only RF
programme (CA 0x40684) is gated by the band-1 descriptor predicate
(CA 0x101890), which returns 1 only when the resolved pool record's **byte2
bit7 = 0x00800000** is set [V*].  The boot-default pool (CA 0x1ADC98) holds
country "99" and 5 GHz record word **0x00000201** - valid bit **clear** - so
the predicate is 0 and the RF image is never written [V*].

The only host->device path that rewrites that pool is an H2D group-0/id-1
config event with **key 0x00F7** (sibling wifi-fw-desc-table) [V]:
`hmac_config_set_country` (.text+0x63B6C) -> `hmac_config_send_event(ctx,
0xF7, 8+blob[4]*16, blob)`; the firmware worker CA 0x45A30 strcmp-dedupes the
pool country string, memcpy_s's `8 + body[4]*16` bytes into the pool
(destMax 0x148), runs the channel-map populator (CA 0x1017F8) and programs the
band feature bits.  No shipped host module sets byte2 bit7 - it is the host's
policy bit; posting a "US" table with the 5 GHz record armed is exactly the
C1 candidate.  The gate is a string compare: **cc "99" would be silently
skipped**, so the builder refuses it.

This lane does not re-derive the decode; it consumes the frozen committed
facts and wires the send.  Everything in the frame is checked against
../wifi-fw-desc-table/TABLES.md T2/T3 (offset by offset, see TABLES.md T2/T3
here) and against the committed frame emitter's golden bytes.

## 2. What was built

Patch `patches/0001-desc-table-send.patch` applies to the pinned base
**wifi-5ghz-beacon tree + the r1-band one-field VAP-start fix**
(`/tmp/r1-band-build`; base module 356,612 B sha `e8b17f72...4abd4`,
59 params) and changes exactly six entries:

| file | change |
| --- | --- |
| **hi5622_desc_table.h** (new) | portable model: frame geometry/values, plan gate, builder, status line |
| **hi5622_desc_table.c** (new) | the 68-B wire builder + the fail-closed decision + sysfs status |
| hi5622_scan_logic.h | `HI5622_BEACON_STEP_DESC_TABLE` added to the ordering plan enum; plan signature gains the desc_table knob |
| hi5622_scan_logic.c | plan inserts the step between `vap_add` and `ap_chan` for `desc_table == 1`; step name "desc_table" |
| hi5622_scan.c | `desc_table` module param (uint, 0444, default 0); fail-closed parameter gate before any frame; plan call; the 0x00F7 post in the beacon_static branch before `ap_chan`; read-only band-object dmesg; sysfs status line |
| Kbuild | `hi5622_desc_table.o` |

The portable model compiles unchanged in the kernel module and in the host
selftest (pure C99, no kernel-only dependencies).

### 2.1 The frame

68 bytes on the wire (`TABLES.md` T2/T3), identical to
../wifi-fw-desc-table/FIRE-RECIPE R1:

```
000100044400010000005a5a 0000000001000000 f700 2800
5553000002000000
000080001e002c0100000000ff1f0000
010280001e002c0100000000ff000000
ffffffff
```

* header: group 0, alloc/dst 0x01, fa 0, fb **0x04**, total **0x0044**,
  id **1**, fc/dst 0, tag **0x5A5A**;
* token 8 B `0000000001000000`, key **0x00F7** at hdr+20, len **0x0028** at
  hdr+22;
* blob: cc **"US"**, NUL, chip 0, count **2**; rec0 word **0x00800000**,
  +4 30, +6 300, bitmap **0x1FFF**; rec1 word **0x00800201** (band 1,
  20 MHz, **armed bit23**), +4 30, +6 300, bitmap **0x000000FF**
  (ch36,40,44,48,52,56,60,64 - ch40/5200 included);
* tail `FF FF FF FF`; SR node word1 = **0x00446D2B**, derived by the existing
  `hi5622_frame_post()` transport (no new transport code).

### 2.2 The fail-closed parameter and the order

* `desc_table=0` (default): no 0xF7 frame; the plan line still reads the
  bench-fired 5 steps; behaviour is byte-identical to the r1-band base.
* `desc_table=1`: only on a **band-1** AP start with `beacon_static=1`; the
  frame is posted **after** the VAP base (0x0064) and **before** the 0x0124
  channel post.  Rationale: the 0x00F7 worker needs the module/band object
  bound by the VAP base, and the 0x0124 worker's band-1 RF branch evaluates
  the armed record at post time.  In this ladder the vendor-order VAP start
  (0x0065) is posted last (after the AP config), so the effective order is
  `0x0064 -> [0x0067 user_add] -> 0xF7 -> 0x0124 -> 0x0127 -> 0x0065`; the
  module object the 0xF7 worker tests exists after 0x0064 [V*].  A read-only
  dmesg line prints the live module ctx + band object right after the post so
  the Lead can see whether the worker's band-object test (ctx+0x4B8) could
  pass (the worker still installs the pool and populates the maps when it
  cannot; only the band feature bits are skipped) [V*].
* `desc_table>1` or `desc_table=1` without `beacon_static=1`: **refused
  (-EINVAL) before any frame**, with a distinct dmesg line; nothing is
  silently ignored.
* Band-0 AP start with a valid parameter: clean no-post (per-band gate).
* Read-only sysfs status appended to `cfg80211_status`:
  `desc_table: param=1 (band1-post) beacon_static=1 last_rc=0 posted=1 refused=0`.

## 3. Host tests (no bench)

`tools/run-host-tests.sh` (all green, evidence/host-tests.txt):

1. **Portable model** (`src/desc_table_selftest.c`, 63 checks): the exact
   68-byte frame against a golden array (and every header/blob field
   individually), the recorder word/bitmap/armed bit, the cc "99"/empty/
   non-printable refusals, count != 2, unarmed plan, short buffer, NULLs, the
   fail-closed plan matrix, and the sysfs status format.
2. **Ordering contract** (`tools/order_test.c` against the real patched
   `hi5622_scan_logic.c`, 13 checks): the plan is
   `vap_add -> desc_table -> ap_chan -> ap_ssid -> ap_cfg -> ap_vap_start` for
   desc_table=1, the base 5-step ladder for 0/2, the ladder-isolation rung,
   truncation, enum ordering and the step name.
3. **Independent cross-check** (`tools/desc_table_send_test.py`, 32 checks):
   re-derives the frame from the spec offsets and requires the C builder's
   bytes to be identical; verifies in the patched `hi5622_scan.c` that the
   parameter gate runs before the VAP add, that the **'desc_table' post line
   is before the 'ap_chan' post line** and after the VAP-base readback,
   that the post is gated by the plan, and that the Kbuild/status/dmesg
   contract entries exist.
4. **Artifact contract** (when built): 60 params incl `desc_table`, the
   feature strings, 12 cfg80211 symvers.

## 4. Build and artifact (host-built, local evidence only, never committed)

`tools/build-desc-table-send.sh` pins the r1-band base (provider sha
`c306e75f...`, artifact 356,612 B sha `e8b17f72...`), applies the patch,
refuses if `src/hi5622_desc_table.*` drifted from the tree, builds against
the project 6.18.60 tree with the OpenWrt GCC 14.4 toolchain (0 warnings/
errors/undefined symbols), debug-strips (proves .text unchanged) and checks
the artifact contract.  Result (evidence/build-artifact.txt):

| field | value |
| --- | --- |
| stripped | **360,448 B** sha256 **`aac6177ce84c972dafff67061951c2fe28e31a330f636c7a185c1511dfe800e3`** |
| unstripped | 1,370,144 B; .text sha256 `6cf0cabd...b24149` |
| params | **60** (59 + `desc_table`; `desc_table` uint, default 0) |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |

## 5. What the Lead must do (see FIRE-RECIPE.md)

Load the module with the beacon/r1-band line plus `beacon_static=1
beacon_vap_start=1 band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1`, start
the **ch40 (5200 MHz)** AP, then run the read-only probe pack (TABLES T7):
pool CPU 0x1ADC98 = "US"/02, rec1 CPU 0x1ADCB0 = 0x00800201, band-1 map row
CPU 0x117AD4 byte1 = 01, [P+0x1A00] nonzero, [P+0xF04] byte 3, R+0x410 bit0
= 1, R+0x3AC = 0x03E6138A, and a 5200 beacon on the observer.  The dmesg
`desc_table` post line and the band-object line are the host-side evidence
that the frame was committed and the worker had a module object.

## 6. Honest limits / limits of the claim

* **Not fired** by this lane; no bench contact at all.  Whether the armed bit
  actually makes the RF programme write the image and start the beacon is
  **[U]** until the Lead's fire (the sibling lane's own note: if the RF image
  appears while bit0 stays 0, C1 is not the predicate gate and must be
  re-ranked).
* The vendor/hmac producer of a byte2-bit7 table is not statically visible
  (the producer of the vendor 0xF7 call is missing in the shipped modules
  [V]); the armed bit is committed as a **host policy** bit - that is the
  point of the experiment, not a claim that the vendor ships it.
* If the module band object (ctx+0x4B8) is absent at post time, the pool
  install/populator still run but the band feature bits are skipped
  (worker returns 0x64) [V*]; the new dmesg band-object line makes that
  visible without a probe.
* The 5 GHz bitmap is the ch36..64 fix (0xFF).  Wider plans / 40-80 MHz /
  other cc values are deliberately not parameterised here (keep the experiment
  one-variable); the sibling FIRE-RECIPE lists those variants if needed.
* No calibration, MAC, secret or vendor byte is used or stored; the probe
  output stays in `build/` (gitignored) per the project rules.

## 7. Reproduction (host only)

```
./tools/build-desc-table-send.sh     # pinned base + patch -> /tmp/desctable-artifact
./tools/run-host-tests.sh            # 108 checks, all green
```

The base chain itself is generated by
`../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh` then
`../wifi-r1-band-diff/tools/build-r1-band.sh` when `/tmp/r1-band-build` is
absent; the build script pins every base hash and refuses drift.
