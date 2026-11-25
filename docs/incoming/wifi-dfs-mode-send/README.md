# wifi-dfs-mode-send - the H2D 0x01F7 DFS/RF mode send (F1, implementation)

**Lane:** wifi-dfs-mode-send (shared task **task-190**), 2026-11-25.
**HOST-ONLY**: implementation + host build + host tests; no device contact,
no flash, no fire. **The Lead fires.** No vendor binary, firmware byte,
calibration value, MAC or secret is stored here; the only bytes committed are
*host-built protocol bytes* (our own 0x01F7 frame), as in the sibling lanes.

Read in this order:

1. **REPORT.md** - what was implemented and why: the F1 context from the
   sibling lanes, the exact 29-byte frame and its offset derivation (anchored
   on the sibling 0x00F7 carrier), the fail-closed `dfs_mode` parameter, the
   ordering/placement, the host tests, the built artifact and the honest
   limits.
2. **TABLES.md** - the frame/offsets, the fail-closed matrix, the integration
   and order contract, the artifact pins, the corrected read-only post-fire
   probe pack, the dmesg expectations and the decision tree.
3. **FIRE-RECIPE.md** - what the Lead does: load with `desc_table=1 dfs_mode=1
   band5g=1 vap5g=1 ap_chan_wait_ms=300`, the ch40 (5200 MHz) AP, the
   read-only probe verification (mode byte = 3, R+0x3AC = 0x03E6138A,
   R+0x3A8 = 0x01800060, R+0x410 bit0 = 1, the 5200 scan), PASS/FAIL/abort.
4. **BUILD-NOTES.md** - the pinned chain (beacon5g + r1-band + the sibling
   0x00F7 patch + this lane), artifact hash/size/params, contract checks,
   reproduce.
5. **patches/0001-dfs-mode-send.patch** - the whole change on the pinned
   desc-table-send base (6 files: 2 new, 4 edits).
6. **tools/** - `build-dfs-mode-send.sh`, `run-host-tests.sh`,
   `order_test.c`, `validate_frame_test.c`, `dfs_mode_send_test.py`;
   **src/** - the portable model
   (`hi5622_dfs_mode.c/h`) + its host selftest; **evidence/** - the captured
   host-test, frame, objdump-key and artifact outputs.

## Key results

* **Frame [V]**: H2D HCC **group 0 / id 1**, key **0x01F7** at hdr+20,
  body length **0x0001** at hdr+22, one body byte **0x03** (mode 3) at
  hdr+24, inherited `FF FF FF FF` tail: **29 B** total, hdr total field
  **0x001D**, SR node word1 **0x001D6D2B**.  The frame is the sibling 0x00F7
  carrier (12-B HCC header + 8-B token + key + len + body + tail) with only
  the key, length, body and total changed.  (The task brief estimated "~23 B";
  the carrier geometry derives exactly 29 = 12+8+2+2+1+4, total = len + 28.)
* **Decoder join [V]**: key 0x01F7 is firmware table-A row 93 (CA 0x10C524)
  -> worker CA 0xB5B9C, the **only** writer of the band object's RF mode byte
  `P+0xF04` (CA 0xB5D1C).  It reads the mode from body[0] (1..5) and programs
  the mode's RF register image.  The stock host trigger is the WAL command
  `set_country_for_dfs` (hmac_config_set_country_for_dfs, wifi.ko
  .text+0x5762C, MOVW r1,#0x01F7 at +0x5763C -> hmac_config_send_event).  The
  ch36..64 RF programme CA 0xA2FA2 requires mode **3**.
* **Fail-closed param [V]**: `dfs_mode` uint, 0444, default **0** = no
  frame, current behaviour; **1** = post on a band-1 AP start with
  `beacon_static=1`, **after** the 0x00F7 desc-table step and **before** the
  0x0124 channel post; **>1 refused (-EINVAL) before any frame**; 1 without
  beacon_static refused too.  Band 0 never posts.
* **Integration [V]**: `hi5622_scan.c` start_ap beacon_static branch +
  `hi5622_scan_logic.{c,h}` ordering plan + Kbuild + the new portable
  `hi5622_dfs_mode.{c,h}`; sysfs `cfg80211_status` gains a
  `dfs_mode: param=... posted=... refused=...` line; dmesg gains the post
  line, the plan step ("dfs_mode") and the worker-precondition readback
  (published per-link + band attach flag).
* **Host tests [V]**: 133 checks, 0 failures - byte-exact 29-B frame (C
  builder + independent Python re-derivation from the sibling carrier), the
  frame passing the **real `hi5622_frame_post()` validator** (h2d_frames.c),
  order (vap_add -> desc_table -> dfs_mode -> ap_chan), fail-closed matrix,
  builder refusals, worker precheck, status line, source contract.
* **Artifact [V]**: host-built `/tmp/dfsmode-artifact/hi5622_wifi.ko`,
  **364,192 B** sha256
  `f540856776e77c00ff1a526582466763bac9a3e63834264099faf3a3a10266ac`,
  **61 params** (60 + `dfs_mode`), vermagic 6.18.60, 0 warnings;
  `objdump` of the built unit shows the `movw #0x1F7` key immediate.
* **Not fired [U]**: whether mode 3 actually makes CA 0xA2FA2 write the
  ch36..64 image and the beacon start is decided by the Lead's fire and the
  read-only probe pack.  The lane makes no bench claim.
