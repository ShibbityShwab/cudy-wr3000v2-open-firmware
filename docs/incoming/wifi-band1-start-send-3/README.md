# wifi-band1-start-send-3 - the 40-B g0-id1 key-0x0065 VAP start before the band start (R4)

**Lane:** wifi-band1-start-send-3 (shared task **task-209**), 2026-11-25.
**HOST-ONLY**: implementation + host build + host tests + decode
consolidation; no device contact, no write, no fire.  **The Lead fires.**  No
vendor binary, firmware byte, calibration value, MAC or secret is stored here;
the only bytes committed are *host-built protocol bytes* (our own key-0x0065
frame), as in the sibling lanes.

Read in this order:

1. **REPORT.md** - why the 0x0242 band start refuses (resolver CA 0x4AE7C
   needs mctx[0x0E] in {1,2,3}; the key-0x0065 worker CA 0x46394 is the only
   0->1 writer), the exact 40-B frame, what the patch changes (new plan step,
   the fail-closed parameter, the G1..G14 probe gates, G5/G12), the artifact
   and the host tests.
2. **TABLES.md** - the frame field table + KATs, the plan order, the
   firmware code sites, the G1-G14 gate table, the fail-closed matrix, the
   pins/artifact and the fire's read-only probe pack (P1 0x404C4220 = 0x1029,
   P9 0x40817A40, P12 R-image 0x01800060/0x03E6138A, P13 5200 MHz beacon).
3. **FIRE-RECIPE.md** - what the Lead does: the send-2 ladder + 
   'vap_start_g0=1' load, the ch40 AP, the expected dmesg order, the
   PASS/FAIL, the corrected probes, and the abort/fallback rules (R4b, N1,
   N2).
4. **BUILD-NOTES.md** - the pinned chain, artifact hash/size/params,
   contract checks, reproduce.
5. **patches/0001-vap-start-g0.patch** - the change on the pinned send-2 tree
   (2 new files + 3 edited files + Kbuild): the 40-B builder, the new plan
   step between vap_add and desc_table, the fail-closed 0444 'vap_start_g0'
   parameter, the read-only G1..G14 probe pack, the G5 (mctx[0x190]) and G12
   (mctx[0x0E]) gates and the R4b readback.
6. **src/** - the host selftest that compiles the REAL patched-tree models +
   the REAL sibling builders; **tools/** - build script, host-test runner,
   independent Python cross-check; **evidence/** - captured build artifact,
   host tests, frame KATs and pins.

## Key results

* **Root cause (from ../wifi-band-start-prestart) [V]**: the 0x0242
  pre-start CA 0x4C3FC stores state[4]=0xFF (CA 0x4C4EE) unless the band VAP
  resolver CA 0x4AE7C returns the module context, whose per-VAP gate is
  mctx[4]==2 && mctx[0x0E] in {1,2,3}.  On a fresh 5 GHz AP mctx[0x0E] is 0
  and only the H2D g0-id1 **key 0x0065** worker CA 0x46394 (AP branch CA
  0x46418-0x4641A) sets it to 1.  Our ladder never sent it before the 0x0242.
* **The fix [V]**: a 40-B g0-id1 key-0x0065 frame
  ('000100002800010000005a5a000000000100000065000c00000101000000000000000000ffffffff'
  at field B=0; body 00 01 01 00 ...) is now a plan step directly between
  'vap_add' and 'desc_table', behind the fail-closed read-only parameter
  'vap_start_g0' (0 default = wire-identical to the send-2 base; 1 = post;
  >1 refused) and only on a band-1 beacon_static AP whose G1..G13 pack
  passes.  The plan with all knobs 1 is:
  'vap_add vap_start_g0 desc_table dfs_mode band_start ap_chan ap_ssid ap_cfg
  ap_vap_start' (9 steps).
* **G5/G12 [V]**: G5 is the fault gate - the worker dereferences
  mctx[0x190] at CA 0x46470, so the driver waits it (300 ms, read-only) and
  refuses the frame when zero.  G12 reads mctx[0x0E] after the post and only
  value 1 allows the 0x0242; otherwise the sequence aborts with the pack in
  dmesg.  The R4b second observable ([mctx+0x190]+0x10 zero -> state[4]=0x00
  but state[1]=0) is logged read-only on both post lines.
* **Fail-closed matrix [V]**: parameter gates before any frame, per-gate
  preconditions, no re-post on a restart, no 0x0242 on a failed G12.
* **Host tests [V]**: **351 checks, 0 failures** - the byte-exact 40-B frame
  for field B 0 and 4 (+ every index 0..0x12 and a byte-for-byte cross-check
  against the sibling 0x0065 builder), the real plan order (9/8/5 steps), the
  build/plan fail-closed matrix, every G1..G14 gate and G12 0/1/2/3/4/0xFF.
* **Artifact [V]**: host-built /tmp/g0start-artifact/hi5622_wifi.ko,
  **382,072 B**, sha256
  'bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711',
  **65 params** (64 send-2 base + vap_start_g0), vermagic 6.18.60, 0
  warnings; .text sha '3d3b45ff...'.
* **Not fired [U]**: whether the firmware applies the 0x0065 event
  (mctx[0x0E]=1), lets CA 0x4AE7C return the VAP and makes CA 0x4AF0C arm
  the 0x404C4220 0x1029 bits is the Lead's fire.
