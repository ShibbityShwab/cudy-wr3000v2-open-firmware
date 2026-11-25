# wifi-band1-start-send - the H2D 0x0242 band-1 MAC-start send (R3b, implementation)

**Lane:** wifi-band1-start-send (shared task **task-206**), 2026-11-25.
**HOST-ONLY**: implementation + host build + host tests; no device contact,
no flash, no fire.  **The Lead fires.**  No vendor binary, firmware byte,
calibration value, MAC or secret is stored here; the only bytes committed are
*host-built protocol bytes* (our own 0x0242/0x0243 frames), as in the sibling
lanes.

Read in this order:

1. **REPORT.md** - what was implemented and why: the R3b gap, the decode join
   (the per-band MAC start CA 0x4AF0C and the host-reachable 0x0242/0x0243
   workers), the exact 33-byte frame and its offset derivation (anchored on
   the sibling 0x00F7/0x01F7 carriers), the fail-closed parameters, the
   ordering/placement, the host tests, the built artifact and the honest
   limits.
2. **TABLES.md** - the frame/offset tables (primary, forced, 0x0243 nudge),
   the fail-closed matrix, the order contract, the artifact pins, the
   **corrected read-only post-fire probe pack** (0x4010C220 at phys
   **0x404C4220**), the dmesg expectations and the decision tree.
3. **FIRE-RECIPE.md** - what the Lead does: load with `band_start=1` plus
   `band5g=1 vap5g=1 ap_chan_wait_ms=300 desc_table=1 dfs_mode=1`, the ch40
   (5200 MHz) AP, the read-only probe verification, PASS/FAIL/abort, and the
   optional `band_start_fallback=1` F3/F4 rung.
4. **BUILD-NOTES.md** - the pinned chain (F2 fire module + this lane),
   artifact hash/size/params, contract checks, reproduce.
5. **patches/0001-band-start-send.patch** - the whole change on the pinned F2
   base (6 files: 2 new, 4 edits).
6. **tools/** - `build-band-start-send.sh`, `run-host-tests.sh`,
   `order_test.c`, `validate_frame_test.c`, `band_start_send_test.py`;
   **src/** - the portable model (`hi5622_band_start.c/h`) + its host
   selftest; **evidence/** - the captured host-test, frame, objdump-key and
   artifact outputs.

## Key results

* **The gap [V]**: our F-ladder posts the 0x00F7 table and the 0x01F7 mode but
  never the band-start keys.  The stock arms the 5 GHz (band-1) MAC start by
  **direct calls to CA 0x4AF0C** (the G+0x370 VAP-start hook has no writer
  anywhere): the host-reachable call sites are the H2D g0-id1 config-event
  workers of **key 0x0242** (the WAL `set_green_ap` command; worker CA
  0x4C524; body[0]=1 sync start -> 0x4AF0C at 0x4C612, body[0]=2 forced start
  -> 0x4AF0C at 0x4C652) and **key 0x0243** (worker CA 0x8419C -> 0x4BDCE ->
  0x4AF0C at 0x4BEB0).  CA 0x4AF0C's band-1 branch is the only path that ORs
  **0x1029** into **0x4010C220** - exactly what R3 never saw.
* **Frame [V]**: H2D HCC **group 0 / id 1**, key **0x0242** at hdr+20, body
  length **0x0005** at hdr+22, body **{01,00,01,00,00}** at hdr+24 (subcmd 1
  sync start; body[2]=1 sets the run-state bit), inherited `FF FF FF FF`
  tail: **33 B** total, hdr total field **0x0021**, SR node word1
  **0x00216D2B**.  **field B (hdr[3])** = the AP-start module/context index
  (0 = this ladder's `vap_start_module` default; the F2 field-B lesson: it
  must match the AP's context).  The fallback rungs are the 33-B forced start
  (`body[0]=2`) and the 29-B **0x0243** nudge.
* **Fail-closed params [V]**: `band_start` uint, 0444, default **0** = no
  frame, current behaviour; **1** = post the 0x0242 sync start on a band-1 AP
  start with `beacon_static=1`, **after** the 0x01F7 dfs_mode step and
  **before** the 0x0124 channel post; **>1 refused (-EINVAL) before any
  frame**; band 0 never posts; 1 without beacon_static refused.
  `band_start_fallback` (default 0, >1 refused) opts into the decoded F3/F4
  rungs after the primary: forced-start 0x0242 then the 0x0243 nudge if the
  per-band start marker `state[1]` is not 2; requires band_start=1.
* **Integration [V]**: `hi5622_scan.c` parameter gates + step block +
  sysfs status line, `hi5622_scan_logic.{c,h}` step/order contract, Kbuild,
  and the new portable `hi5622_band_start.{c,h}`; the plan becomes
  `vap_add -> desc_table -> dfs_mode -> band_start -> ap_chan -> ...` when the
  knobs are 1.
* **Host tests [V]**: **216 checks, 0 failures** - byte-exact 33-B frames
  (C builder + independent Python re-derivation from the sibling carrier),
  the frames passing the **real `hi5622_frame_post()` validator**
  (h2d_frames.c), order (dfs_mode -> band_start -> ap_chan), the fail-closed
  matrix, builder refusals, the guard precheck, the status line and the
  source contract (evidence/host-tests.txt).
* **Artifact [V]**: host-built `/tmp/bandstart-artifact/hi5622_wifi.ko`,
  **371,212 B**, sha256
  `e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028`,
  **63 params** (F2 61 + `band_start` + `band_start_fallback`), vermagic
  6.18.60, 0 warnings; `objdump` shows the `movw #0x242` / `movw #0x243`
  key immediates.
* **Not fired [U]**: whether the frame actually arms the band and beacons is
  decided by the Lead's fire and the read-only probe pack.  The lane makes no
  bench claim.
