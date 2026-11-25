# wifi-desc-table-send - the H2D 0x00F7 descriptor-table send (C1 fix, implementation)

**Lane:** wifi-desc-table-send (shared task **task-188**), 2026-11-25.
**HOST-ONLY**: implementation + host build + host tests; no device contact,
no flash, no fire. **The Lead fires.** No vendor binary, firmware byte,
calibration value, MAC or secret is stored here; the only bytes committed are
*host-built protocol bytes* (our own 0x00F7 frame), as in the sibling lanes.

Read in this order:

1. **REPORT.md** - what was implemented and why: the C1 context from the
   sibling lanes, the exact 68-byte frame and its offset verification, the
   fail-closed `desc_table` parameter, the ordering/placement, the host
   tests, the built artifact, and the honest limits.
2. **TABLES.md** - the frame/blobs/records tables, the fail-closed matrix, the
   integration contract, the artifact pins, the read-only post-fire probe pack,
   the dmesg expectations and the decision tree.
3. **FIRE-RECIPE.md** - what the Lead does: load with `desc_table=1`, the
   ch40 (5200 MHz) AP, the read-only probe verification, PASS/FAIL/abort.
4. **BUILD-NOTES.md** - the pinned chain (beacon5g + r1-band + this lane),
   artifact hash/size/params, contract checks, reproduce.
5. **patches/0001-desc-table-send.patch** - the whole change on the pinned
   r1-band base (6 files: 2 new, 4 edits).
6. **tools/** - `build-desc-table-send.sh`, `run-host-tests.sh`,
   `order_test.c`, `desc_table_send_test.py`; **src/** - the portable model
   (`hi5622_desc_table.c/h`) + its host selftest.

## Key results

* **Frame [V]**: H2D HCC **group 0 / id 1**, key **0x00F7** at hdr+20,
  payload len **0x28** at hdr+22, 40-B blob at hdr+24, inherited
  `FF FF FF FF` tail: **68 B** total, SR node word1 **0x00446D2B**.  Blob =
  the firmware pool image for N=2: cc **"US"** (must not be "99": the install
  gate is an ARM strcmp), count 2, rec0 word **0x00800000** + bitmap
  **0x1FFF**, rec1 word **0x00800201** (5 GHz record **armed**, byte2 bit7)
  + bitmap **0x000000FF** (ch36..64 - ch40/5200 included).  No CRC/length in
  the blob.
* **Fail-closed param [V]**: `desc_table` uint, 0444, default **0** = no
  frame, current behaviour; **1** = post on a band-1 AP start with
  `beacon_static=1`, after the VAP base and **before** the 0x0124 channel
  post; **>1 refused (-EINVAL) before any frame**; 1 without beacon_static
  refused too.  Band 0 never posts.
* **Integration [V]**: `hi5622_scan.c` start_ap beacon_static branch +
  `hi5622_scan_logic.{c,h}` ordering plan + Kbuild + the new portable
  `hi5622_desc_table.{c,h}`; sysfs `cfg80211_status` gains a
  `desc_table: param=... posted=... refused=...` line; dmesg gains the post
  line, the plan step ("desc_table") and the module/band-object readback.
* **Host tests [V]**: 108 checks, 0 failures - byte-exact frame, order
  (vap_add -> desc_table -> ap_chan), fail-closed matrix, builder refusals,
  status line, independent Python re-derivation.
* **Artifact [V]**: host-built `/tmp/desctable-artifact/hi5622_wifi.ko`,
  **360,448 B** sha256
  `aac6177ce84c972dafff67061951c2fe28e31a330f636c7a185c1511dfe800e3`,
  **60 params** (59 + `desc_table`), vermagic 6.18.60, 0 warnings.
* **Not fired [U]**: whether the armed bit closes C1 is decided by the Lead's
  fire and the read-only probe pack (pool "US"/02, rec1 0x00800201, band-1
  row byte1 = 01, R+0x410 bit0 = 1, R+0x3AC = 0x03E6138A, 5200 beacon).
  The lane makes no bench claim.
