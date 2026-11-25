# REPORT - the 0x0242 band-1 MAC-start send (R3b implementation)

**Lane:** wifi-band1-start-send (shared task **task-206**).  **HOST-ONLY**:
implementation + host build + host tests; no device contact, no write, no
fire.  **The Lead fires.**  Companions: [README.md](README.md),
[TABLES.md](TABLES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[BUILD-NOTES.md](BUILD-NOTES.md), [patches/0001-band-start-send.patch](patches/0001-band-start-send.patch),
[tools/](tools/), [evidence/](evidence/).

Grades: **[V]** re-derived from the committed sibling decodes/artifacts or the
lane's own host tests; **[V\*]** bench-measured by a committed lane; **[I]**
inference; **[U]** needs the fire.

---

## 0. Answer in one paragraph

The 5 GHz (band-1) MAC start is produced by CA 0x4AF0C, whose band-1 branch is
the only writer of the arm bits **0x1029** in **0x4010C220**.  The stock does
not reach it through the VAP-start hook G+0x370 (no writer anywhere in the
image); it calls it directly, and the host-reachable call sites are the H2D
g0-id1 config-event workers **key 0x0242** (the WAL `set_green_ap` command;
worker CA 0x4C524) and **key 0x0243** (worker CA 0x8419C -> 0x4BDCE).  Our
AP-start ladder never posted either key.  This lane implements the missing
send: on a **band-1** AP start with the **beacon_static** vendor order, **after**
the 0x01F7 DFS-mode step and **before** the 0x0124 channel post, post the
byte-exact **33-byte g0-id1 key 0x0242** frame with **field B = the AP-start
module/context index (0)** and body **{01,00,01,00,00}** (subcmd 1 sync
start), optionally followed by the decoded fallback rungs (forced start
0x0242 body[0]=2, then the 0x0243 nudge).  Everything is fail-closed: both
parameters default 0 (byte-identical to the F2 ladder), >1 is refused before
any frame, band 0 never posts.  Host-built module: 371,212 B sha256
`e2312402...a028`, 63 params, 216 host checks / 0 failures.  The fire is the
Lead's; the corrected probe is **0x4010C220 at phys 0x404C4220**, expect
**0x1029**.

## 1. Method and inputs

| item | value |
| --- | --- |
| base tree | /tmp/dfsmode-build - the tree that built the bench-fired **F2** module (/tmp/f2-module.ko, 364,184 B, sha256 12427b8675b91515...9468, 61 params) |
| F2 lineage | r1-band base (356,612 B sha e8b17f72...) + ../wifi-desc-table-send/patches/0001 (0x00F7) + ../wifi-dfs-mode-send/patches/0001 (0x01F7) + ../wifi-fw-dfs-worker/patches/0001-fieldb-context.patch (field B = 0) |
| decode inputs | ../wifi-band1-start (TABLES T1-T8: CA 0x4AF0C, the five call sites, the 0x0242 subcommand table, G+0x370 no-writer), ../wifi-fw-desc-table (the g0-id1 carrier), ../wifi-fw-dfs-worker (the field-B lesson) |
| build chain | 6.18.60 kernel tree + arm-openwrt-linux-muslgnueabi gcc 14.4.0 (OpenWrt staging) |
| artifact | /tmp/bandstart-artifact/hi5622_wifi.ko (LOCAL ONLY, never committed) |

## 2. The decode join - why key 0x0242 [V]

CA 0x4AF0C (../wifi-band1-start TABLES T1) is the per-band MAC start:

1. reads the per-band run-state at bandobj+0x14D0; returns if state[1] == 2
   ("already started") or state[4] == 0xFF / unresolved ("no bound VAP");
2. band 1: resets the band-1 MAC block (0x40114500 = 0x55/0xFF, 0x4011451C =
   0xFF), ORs **1, 8, 0x20, 0x1000 = 0x1029** into **0x4010C220**, ORs the
   band-1 gate bit into 0x4011480C, ORs the band-1 run bit into 0x40101220,
   calls 0xF4504/0xF5C54/0x96DBC, writes state[1] = 2 and increments the u32
   start counter at state+0x64;
3. band 0: the same shape with 0x40108214 |= 0xF.

The five direct call sites of 0x4AF0C are 0x4BEB0, 0x4C34E, 0x4C3EC, 0x4C612
and 0x4C652.  The two host-reachable ones inside H2D config-event workers are:

* **key 0x0242** (table A row 76, worker CA 0x4C524): body[0] = 0 diag,
  **1 = sync/conditional start -> 0x4AF0C at 0x4C612**, 2 = forced start ->
  0x4AF0C at 0x4C652, 3 = stop -> 0xBE294.  The subcmd-1 path sets a state bit
  from body[2], calls 0x4C3FC(bandobj, 0) and then, if state[1] != 0 and the
  bound VAP's +0x14 byte is 1, calls the start.  The stock host emitter is
  the WAL command `set_green_ap` (wifi.ko .text+0x12AE20 ->
  hmac_config_send_event(key 0x0242)).
* **key 0x0243** (worker CA 0x8419C): takes ctx+0x4B8, calls
  0x4BDCE(bandobj, 5, 0), which calls 0x4AF0C only when state[1] == 3.

The worker resolves ctx = 0xFE578(0, hdr[3]) and takes the band object from
ctx+0x4B8 - the field the key-0x0064 VAP-add worker binds (ctx(N)+0x4B8 =
bandobj when the published per-link *(bandobj+0x1A00) is nonzero).  So **field
B must be the AP-start module/context index N**: in this ladder N =
`vap_start_module` (default **0**), which is also the field B the sibling
0x00F7/0x01F7 frames now use after the F2 field-B fix.

The gap: our beacon_static ladder posts the 0x0064 VAP add, the 0x00F7 table
and the 0x01F7 mode, then the 0x0124 channel config and the AP frames - never
0x0242/0x0243.  So band 1 never started (0x4010C220 stayed 0 and the R3
differential had no band-1 arm).  Calling the start directly is the missing
rung.

## 3. The frame - derivation and byte-exactness [V]

The g0-id1 config-event carrier (../wifi-fw-desc-table TABLES T2) is
`12-B HCC header + 8-B token + key u16 + len u16 + body + 4-byte FF tail`,
total = 28 + len.  The key 0x0242 frame is that carrier with:

* hdr: group 0, alloc 1 / dst 0, fa 0, **field B = module**, total = 33,
  message id 1, fc/dst 0, tag 0x5A5A;
* token: the inherited bench pattern (payload word at +4 = 1);
* key **0x0242**, body length **5**;
* body **{01,00,01,00,00}** - body[0] = subcmd 1 (sync/conditional start),
  body[2] = 1 (the run-state bit the worker sets before the 0x4C3FC gate);
  body[1]/[3]/[4] are unread by the subcmd-1 path and sent as 0;
* inherited tail FF FF FF FF; total 33 = 12+8+2+2+5+4; SR node word1 =
  (33 << 16) | 0x6D2B = **0x00216D2B**.

Byte-exact (evidence/frame.txt):

    primary 000100002100010000005a5a0000000001000000420205000100010000ffffffff
    forced  000100002100010000005a5a0000000001000000420205000200000000ffffffff
    nudge   000100001d00010000005a5a00000000010000004302010000ffffffff

The three frames were re-derived independently in
tools/band_start_send_test.py from the sibling 0x00F7 wire frame (only the
field-B byte, total, key, length and body change) and compared byte-for-byte
with the C builder; the C frames were also decoded by the **real** h2d_frames
validator (the same call hi5622_frame_post() makes: group 0 / id 1 / dst 0,
tag 0x5A5A, key at payload+8, len at payload+10, body at payload+12).

## 4. The fail-closed parameters [V]

Two read-only (0444) uint parameters, both default **0**:

| param | 0 | 1 | >1 |
| --- | --- | --- | --- |
| `band_start` | no frame (current behaviour, byte-identical to F2) | post the 33-B 0x0242 sync-start frame on a band-1 AP start with beacon_static=1, after dfs_mode, before ap_chan | refused -EINVAL before any frame |
| `band_start_fallback` | no fallback rung | after the primary frame, if the per-band start marker state[1] != 2, post the forced-start 0x0242 (body[0]=2) then the 0x0243 nudge; requires band_start=1 | refused -EINVAL before any frame |

Ordering/fail-closed rules implemented at three layers (host-tested):

1. **start_ap parameter gates** - before any frame: band_start > 1,
   band_start=1 without beacon_static, band_start_fallback > 1,
   band_start_fallback=1 without band_start=1 -> -EINVAL, no partial ladder;
2. **plan decision** (hi5622_band_start_plan) - repeats the checks, validates
   band <= 1 and module <= 0x12 (module-table bound), and sets post only for
   band == 1; band 0 is a clean no-post (the band-0 start branch is a
   different path, 0x40108214 |= 0xF);
3. **frame builder** (hi5622_band_start_build) - refuses an unarmed plan, a
   frame selector the plan does not arm (forced/nudge need fallback), an
   unknown selector and a short buffer; never returns a partial frame.

The fallback trigger is the **per-band run-state**: state[1] is the marker
CA 0x4AF0C writes to 2 on success, read through the same firmware-RAM readback
the driver already uses (ctx0 = u32(0x171458) + u16(0x172504) -> ctx+0x4B8
band object -> +0x14D0 run-state).  The fallback is skipped (logged) when the
band-object/VAP guard refuses (-EAGAIN): a later start frame cannot help then.
A read-only guard line is logged after the primary post
(`band_start post: module=... bandobject=... state[1]=... state[4]=...
counter=...`), mirroring the sibling 0x00F7/0x01F7 readbacks; it does not
gate the frame.

## 5. Integration and order [V]

* `hi5622_band_start.{c,h}` - the new portable model (same host/kernel
  split as the sibling `hi5622_dfs_mode`/`hi5622_desc_table`).
* `hi5622_scan.c` - two module params + gates in the existing fail-closed
  parameter block, the plan call gains `hi5622_band_start`, the step block
  after the dfs_mode block and before the 0x0124 channel builder, the
  read-state helper + guard-name helper, and the sysfs status line.
* `hi5622_scan_logic.{c,h}` - the new plan step
  `HI5622_BEACON_STEP_BAND_START` between DFS_MODE and CHAN (the enum
  renumbers the later steps; the enum order is the executions contract) and
  the plan signature gains `band_start`.
* `Kbuild` - `hi5622_band_start.o`.

Plan with every knob 1:

    vap_add -> desc_table -> dfs_mode -> band_start -> ap_chan -> ap_ssid
      -> ap_cfg -> ap_vap_start

The 0x0242 post is after the 0x01F7 post and before the 0x0124 build/post in
the source (order asserted by tools/order_test.c + the Python source
contract); the fallback rungs live inside the band_start step and add no plan
step, so the plan length is independent of band_start_fallback.

## 6. Host tests [V]

`./tools/run-host-tests.sh` (evidence/host-tests.txt): **216 checks, 0
failures**:

| part | checks | content |
| --- | ---: | --- |
| src/band_start_selftest.c | 83 | plan matrix, 3 byte-exact frames field-by-field, node word1, builder refusals, precheck, status line |
| tools/validate_frame_test.c | 21 | the frames through the real h2d_frames.c frame_post validation path, handler gate, SR node length, negative controls |
| tools/order_test.c | 15 | plan order (dfs_mode -> band_start -> ap_chan), enum order, names, truncation, null/max edges |
| tools/band_start_send_test.py | 97 | independent re-derivation of the 3 frames from the sibling carrier + the source/contract checks (params, gates before plan, post order, fallback fail-closed, Kbuild, status format, F2 field-B base invariant) |

No device contacted.

## 7. The artifact [V]

Host-built only (never committed):

| field | value |
| --- | --- |
| stripped | /tmp/bandstart-artifact/hi5622_wifi.ko, **371,212 B** |
| sha256 | `e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028` |
| unstripped | 1,399,948 B sha256 50f105f406474131a244f217ee539cae3794f58bced2f8346e5f5c4fa86a59d7 |
| .text | sha256 884f0a35da01194598decd72fa81921188a6d32e00b0a4a3b3cd6078c2aea275 |
| params | **63** (F2 61 + band_start + band_start_fallback) |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| warnings | 0; debug-strip keeps .text byte-identical |
| cfg80211 symvers | 12 lines (unchanged) |
| key immediates | objdump hi5622_band_start.o: `movw r3, #578 @ 0x242` and `movw r3, #579 @ 0x243` |
| size delta | +7,028 B vs the 364,184 B F2 artifact |

The build script (tools/build-band-start-send.sh) pins the F2 base tree files
(hi5622_scan.c / scan_logic.c / scan_logic.h / Kbuild / dfs_mode.h /
desc_table.c shas), the F2 artifact (sha/size/params), the F2 field-B patch,
this lane's patch and its two portable sources; it refuses a drifted base,
applies the lane patch, builds, strips, and checks the 63-param contract, the
feature strings, the two key immediates and the 12 symvers.

## 8. Honest limits [U]

* **Not fired.**  Whether the 0x0242 frame actually arms 0x4010C220 and
  beacons is the Lead's fire + read-only probes (FIRE-RECIPE R3).
* **state[1] is a proxy.**  The fallback trigger reads the run-state marker
  CA 0x4AF0C writes on success, not the 0x4010C220 register itself (the
  driver does not probe device-IO registers in the AP-start path).  If
  state[1] == 2 but the arm word were not set, the fallback would not fire;
  the Lead's P1/P9 probes disambiguate.
* **The stock's exact green-AP body is not captured** (../wifi-band1-start
  sec. 7): the body is the minimal shape derived from the worker's own reads
  (body[0] subcommand, body[2] the state bit).  If the worker rejects it, the
  fallback rungs (body[0]=2, then 0x0243) are the decoded next tries.
* **0x1039 is not a start proof.**  The enable-TRX probe CA 0xE8520 writes
  0x1039 to 0x4010C220; 0x1039 contains 0x1029.  Expect exactly 0x1029 and
  cross-check state[1] = 2.
* **The sibling decode lane's FIRE-RECIPE P1.16 phys 0x4081D240 is wrong**
  (its own tool derives 0x40817A40 for bandobj 0x15E570 + 0x14D0); this
  lane's pack uses 0x40817A40.
* **field B assumes N = vap_start_module (0).**  If an operator changes
  vap_start_module, field B follows it (the model takes the index), but the
  driver's state readback still resolves ctx0; the FIRE-RECIPE keeps the
  default 0.
* The merged image-#3 module (image #3, 64 params) does **not** contain this
  lane; a fire of R3b needs either this lane's module or a rebase of this
  patch onto the merged tree (the patch touches only the scan/logic/Kbuild
  files, so it should apply; that rebase is not part of this lane).
