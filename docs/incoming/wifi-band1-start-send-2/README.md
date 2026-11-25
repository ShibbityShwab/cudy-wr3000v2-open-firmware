# wifi-band1-start-send-2 - the explicit VAP/module index of the key-0x0064 VAP add (R3b iteration 2)

**Lane:** wifi-band1-start-send-2 (shared task **task-207**), 2026-11-25.
**HOST-ONLY**: host build + host tests + decode; no device contact, no flash,
no fire.  **The Lead fires.**  No vendor binary, firmware byte, calibration
value, MAC or secret is stored here; the only bytes committed are
*host-built protocol bytes* (our own key-0x0064/0x0242 frames), as in the
sibling lanes.

Read in this order:

1. **REPORT.md** - what the R3b fire showed, the verification that the fired
   0x0064 frame already carried `data[4] = 0`, the one-field fix this lane
   actually ships (the index is explicit and the bind ctx / start ctx can no
   longer diverge), the firmware decode of where `state[4]` comes from, and
   the new read-only post-add readback that makes the next fire decisive.
2. **TABLES.md** - frame field tables, firmware code sites, the readback
   keys, the fail-closed matrix, pins/artifact and the probe pack.
3. **FIRE-RECIPE.md** - what the Lead does: the pinned 0x0242 base knobs plus
   `vap_add_index=0`, the ch40 (5200 MHz) AP, the read-only probes, the
   PASS/FAIL branches, and the negative controls.
4. **BUILD-NOTES.md** - the pinned chain, artifact hash/size/params, contract
   checks, reproduce.
5. **patches/0001-vap-add-index.patch** - the change on the fired
   0x0242 base (2 files): the explicit `vap_add_index` parameter, the
   body/header agreement in the key-0x0064 builder, the cfg80211 fail-closed
   gates, and the read-only post-add readback.
6. **src/** - the portable contract model (`hi5622_vap_index.c/h`) + its host
   selftest; **tools/** - build script, host-test runner, independent Python
   cross-check; **evidence/** - captured host-test, frame, artifact and pin
   outputs.

## Key results

* **Premise corrected [V]**: the 0x0064 frame the Lead fired already emitted
  **data[4] = 0** (the VAP/module index) and data[0x12] = 1 (band 1).  The
  fired module's builder source sets it (`gp.module = p->module`,
  `p->module = vap_start_module = 0`); this lane's host test proves the exact
  bytes with the real builder.  So the R3b refusal is **not** a missing
  data[4] value.
* **What this lane fixes [V]**: the index was *implicit* (it followed
  `vap_start_module`) and the frame's header **field B (hdr[3]) was not tied
  to it** (it kept the builder default 0).  A fire with a non-zero
  `vap_start_module` would have created ctx(N) while the dispatcher and the
  0x0242 start resolved ctx(0).  The lane adds a read-only `vap_add_index`
  parameter (default **0** = the single AP), writes it into **data[4],
  data[1] (self-parent) and hdr[3]**, and the cfg80211 layer refuses
  `vap_add_index > 0x12` or `vap_add_index != vap_start_module` **before any
  frame**.  At the defaults the 0x0064/0x0242 bytes are unchanged.
* **Why state[4] can still be 0xFF [V]**: firmware decode shows the per-band
  run-state `state[4]` is written by exactly one path - inside the
  key-0x0242 subcmd-1 pre-start helper **CA 0x4C3FC**: it stores 0xFF on its
  failure branch (CA 0x4C4EE-0x4C4F0) and stores `CA 0x4AE7C(bandobj)[0]` on
  success.  CA 0x4AE7C scans the band object's VAP list
  (bandobj+0x12 count, entries at bandobj+0); that list is appended only by
  the key-0x0064 worker (CA 0x44BF8-0x44C32) after its bind.  CA 0x4AE7C
  returns 0 unless the list has **exactly one** entry and its own gate /
  RF-programme branch yields the VAP (CA 0x4AEB4..0x4AEE2).  So the observed
  state[4]=0xFF has two possible owners; the new readback discriminates them.
* **New read-only evidence [V]**: after a successful key-0x0064 post the
  driver logs
  `cfg80211: vap_add post: index=.. band=.. ctx=.. perlink=.. vap=..
  bandobject=.. bandlist_count=.. published=.. (verdict)`.
  `bandobject=ZERO` = the bind never ran; `bandlist_count=1` = the VAP add
  succeeded and the state[4]=0xFF is in the 0x4C3FC/0x4AE7C path, **not** a
  frame-field problem.
* **Fail-closed params [V]**: `vap_add_index` read-only 0444 uint default 0;
  > 0x12 refused; != `vap_start_module` refused; both refusals happen in the
  start_ap parameter block before any frame.  The provider builder also
  refuses (and the kernel layer aborts the start) if the module argument and
  the parameter disagree.
* **Host tests [V]**: **283 checks, 0 failures** - the gate/plan matrix, the
  byte-exact key-0x0064 frames for index 0 and 4 (data[4]/data[1]/hdr[3]/
  data[0x12]), the frames through the real builder validator, the key-0x0242
  field B == the same index for every 0..0x12, the verdict strings, the
  independent Python re-derivation, and the source/order contract.
* **Artifact [V]**: host-built `/tmp/vapbound-artifact/hi5622_wifi.ko`,
  **373,056 B**, sha256
  `b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71`,
  **64 params** (63 fired 0x0242 base + `vap_add_index`), vermagic 6.18.60,
  0 warnings; `.text` sha `ff4fb171...`.
* **Not fired [U]**: whether the band-list readback shows the add succeeded
  and whether state[4] then becomes 0 is decided by the Lead's fire.
