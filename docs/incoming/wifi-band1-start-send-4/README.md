# wifi-band1-start-send-4 - the R4b reorder: the 0x0124 channel post before the 0x0065 VAP start and the 0x0242 band start

**Lane:** wifi-band1-start-send-4 (shared task **task-211**), 2026-11-28.
**HOST-ONLY**: implementation + host build + host tests + the fire recipe; no
device contact, no write, no fire.  **The Lead fires.**  No vendor binary,
firmware byte, calibration value, MAC or secret is stored here; the only bytes
committed are *host-built protocol bytes* (already committed in the sibling
lanes, not re-stored) and quoted dmesg/decoded text.

This lane is the smallest possible change on top of
../wifi-band1-start-send-3: **a pure order fix**.  The R4 fire refused at the
0x0065 step's own G8 gate because the step ran before the 0x0124 channel post
that stores the band byte; this lane moves the 0x0124 step ahead of it.

Read in this order:

1. **REPORT.md** - the R4 refusal, the root cause, exactly what the reorder
   changes (plan model + the start_ap post sites), what is NOT changed, the
   artifact, the host tests, the residual risks and abort rules.
2. **TABLES.md** - the old vs new plan, the file/hunk map, the gate table
   with the new ordering ("why G8 now passes, why G5/G12 still gate"), the
   host-test matrix, the pins/artifact and the fire probe pack.
3. **FIRE-RECIPE.md** - what the Lead does: the send-3 ladder + the same
   'vap_start_g0=1' load (no new knob), the ch40 AP, the expected dmesg
   order, PASS/FAIL, the corrected probes and the abort rules.
4. **BUILD-NOTES.md** - the pinned chain, artifact hash/size/params,
   contract checks, reproduce.
5. **patches/0001-r4b-order.patch** - the change on the pinned send-3 tree
   (5 edited files, no new file): the plan order + step enum, the moved
   start_ap post sites and the comment/parameter-description updates.
6. **src/** - the host selftest (the real patched-tree models + the real
   sibling builders); **tools/** - build script, host-test runner,
   independent Python contract check; **evidence/** - build artifact, host
   tests, tree pins, the R4 refusal and the pinned-chain rebuild.

## Key results

* **The R4 refusal [V]** (root ledger 2026-11-28 07:00): the fire's ladder
  rc=0 and the C2 passed, but the 0x0065 step refused itself:
  'vap_start_g0 REFUSED (-22): G8: mctx[0x11] is not 1 (5 GHz) - no frame
  posted'.  The R4 order posted the 0x0065 VAP start **before** the 0x0124
  channel post, and the G8 gate reads the band byte mctx[0x11] that the
  0x0124 worker stores.  Fail-closed gate doing its job.
* **The fix [V]**: the plan is now
  'vap_add -> desc_table -> dfs_mode -> ap_chan -> vap_start_g0 ->
  band_start -> ap_ssid -> ap_cfg -> ap_vap_start' (9 steps; 8 without
  vap_start_g0), and the start_ap post sites follow that order: the 0x0124
  channel post (with its ap_chan_wait_ms wait) runs first, then the 0x0065
  VAP start (G8 sees mctx[0x11]=1), then the 0x0242 band start (still behind
  G12).  The stock order is VAP add -> channel config -> VAP start ->
  green_ap enable.
* **Unchanged [V]**: every frame builder byte (the 40-B 0x0065, 33-B
  0x0242, 0x00F7, 0x01F7, 0x0124 frames), the 65 parameters (no new knob),
  and all fail-closed gates G1..G14 - G5 (mctx[0x190]!=0), G8
  (mctx[0x11]==1) and G12 (mctx[0x0E]==1) are still hard gates.
* **Host tests [V]**: **386 checks, 0 failures** (C 293 + Python 93) - the
  byte-identical frame KATs, the new plan order and enum order, the start_ap
  post-site order (desc_table plan -> 0x0124 post -> 0x0065 probe/precheck ->
  0x0065 post -> G12 -> 0x0242 build), the G8 sequencing model (band 0
  refused / band 1 passes), the G5/G12 gates and the fail-closed matrix.
* **Artifact [V]**: host-built /tmp/r4border-artifact/hi5622_wifi.ko,
  **382,188 B**, sha256
  'b9926c1c4201cfe698c9fb6828a0d4e6a4fc45ba620bb49d745859ef7fda1102',
  **65 params** (unchanged), vermagic 6.18.60, 0 warnings; .text
  'f2848fdc...'; +116 B vs the send-3 R4 artifact.
* **Not fired [U]**: whether the 0x0124 worker's band byte store makes G8
  pass and the rest of the ladder delivers the 5200 MHz beacon is the Lead's
  fire.  Residual unknowns are listed in REPORT section 6 and the abort
  rules in FIRE-RECIPE R5.
