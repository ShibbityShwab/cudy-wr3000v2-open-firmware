# REPORT - the R4b reorder: 0x0124 channel post before the 0x0065 VAP start and the 0x0242 band start

**Lane:** wifi-band1-start-send-4 (shared task **task-211**), 2026-11-28.
**HOST-ONLY**: no device contact, no fire.  **The Lead fires.**
**Deliverable:** patches/0001-r4b-order.patch (the pure reorder on the pinned
send-3 tree) + the updated host tests + the build script + this recipe set.

## 1. What the R4 fire showed [V]

Root ledger, 2026-11-28 07:00 entry.  The fire loaded the send-3 module
bb0434c2 with 'vap_start_g0=1 band_start=1 desc_table=1 dfs_mode=1' and a
ch40 AP.  The ladder rc=0 and the C2 pair passed, but the 0x0065 step
refused itself **before any wire bytes**:

    cfg80211: vap_start_g0 REFUSED (-22): G8: mctx[0x11] is not 1 (5 GHz) - no frame posted

then hostapd 'Failed to set beacon parameters' and the AP never completed.
The read-only probes afterwards: state 0x40817A40 = 0x01000000,
0x404C4220 = 0x1039 (the TRX probe value, not the 0x1029 arm bits), mode = 0,
R+0x3A8 = 0x05A00060 / R+0x3AC = 0x05940596 (the pre-programme values of a
fresh boot whose AP start aborted), page golden:1; the 5200 MHz observer scan
saw only the neighbours (no beacon).

## 2. Root cause: a pure ordering bug [V]

The send-3 plan put the 0x0065 VAP-start step directly after the 0x0064 VAP
add, i.e. **before** the 0x0124 channel post.  Three reads make that wrong:

1. **G8 reads mctx[0x11] (the band byte).**  That byte is stored by the
   0x0124 channel-config worker (the fw-chan-worker lane: the worker validates
   the channel table, stores the channel/band/index word at ctx+0x10..0x13,
   then runs its 5 GHz RF branch).  Before the 0x0124 it is 0, so the
   fail-closed G8 gate refuses - exactly what the R4 dmesg shows.
2. **The R4b second observable is channel-context data.**  The 0x0242
   pre-start success sub-branch CA 0x4C498-0x4C4A0 reads
   u32[u32[mctx+0x190]+0x10]; the prestart lane's R4b row identified that word
   as channel-context data written by the channel config.  It cannot exist
   before the 0x0124 either.
3. **The stock order is VAP add -> channel config -> VAP start -> green_ap
   enable** (prestart TABLES T8).  The stock host posts the key-0x0065 VAP
   start (hmac_config_start_vap_event, its AP caller 0x4826C) after the
   channel config and before the green_ap/0x0242 enable; our ladder had the
   first two swapped.  The send-3 fire simply exposed the swap through its
   own G8 gate.

Nothing about the frames, the parameter or the gate set was wrong - the
NUMBER of the step in the ladder was.

## 3. The change (the whole patch)

Five edited files, no new file, no frame change:

| file | change |
| --- | --- |
| hi5622_scan_logic.h | enum reorder: VAP_ADD=1, DESC_TABLE=2, DFS_MODE=3, **CHAN=4, VAP_START_G0=5, BAND_START=6**, SSID=7, AP_CFG=8, VAP_START=9; plan/order documentation updated |
| hi5622_scan_logic.c | hi5622_beacon_plan() emits vap_add -> desc_table -> dfs_mode -> **ap_chan** -> **vap_start_g0** -> **band_start** -> ap_ssid -> ap_cfg -> ap_vap_start; comments rewritten (the old "band_start before the 0x0124" text removed) |
| hi5622_scan.c | the start_ap post sites are moved to that order: the 0x0124 channel post block (with its ap_chan_wait_ms wait) now precedes the 0x0065 VAP-start block, which precedes the 0x0242 band-start block; the 0x0065 parameter comment/description and the post-line texts updated |
| hi5622_vap_start_g0.h | the placement contract text now says AFTER the 0x0124 channel post (plus the R4b paragraph) |
| hi5622_band_start.h | the band_start parameter description no longer claims it runs before the 0x0124 channel post |

The plan with all knobs 1 is **9 steps**:

    vap_add desc_table dfs_mode ap_chan vap_start_g0 band_start ap_ssid ap_cfg ap_vap_start

and the start_ap post sites execute in exactly that order (the 0x0124 block
is the same code, just earlier; the 0x0065 block is byte-for-byte the same
code, just later).

## 4. All fail-closed gates kept [V]

Nothing was relaxed.  The G1..G14 probe pack and the G12 latch are exactly
the send-3 code:

* **G1..G7, G9..G11, G13** - unchanged pre-post gates.  They now read the
  firmware state *after* the 0x0124 channel post instead of before it.
* **G5** (u32[mctx+0x190] != 0) - still a hard gate before the 0x0065 post
  (the worker dereferences it at CA 0x46470; it is populated by the 0x0064
  VAP add, so it is unaffected by the reorder).
* **G8** (u8[mctx+0x11] == 1, 5 GHz) - still a hard gate; it is THE
  observable that failed in R4 and the reorder is what makes it pass.
* **G12** (mctx[0x0E] == 1 after the 0x0065 post) - unchanged; still the latch
  that alone allows the 0x0242 to be posted, and the band_start block still
  refuses -EIO otherwise.
* Fail-closed parameter gates (vap_start_g0 > 1, vap_start_g0=1 without
  beacon_static=1, band_start/desc_table/dfs_mode parameter sets) are
  unchanged and still run before any frame.

The 65 module parameters are unchanged (no new knob, same load line).

## 5. Artifact and host tests [V]

* Host-built module: /tmp/r4border-artifact/hi5622_wifi.ko, **382,188 B**,
  sha256 **b9926c1c4201cfe698c9fb6828a0d4e6a4fc45ba620bb49d745859ef7fda1102**,
  65 params, vermagic 6.18.60, 0 warnings, .text
  'f2848fdc3db7c0913a208aefd7fc85230817cc143158d9f50ef3529e59cbf6ff',
  +116 B vs the send-3 R4 artifact.  Local evidence only (never committed).
* The pinned chain was rebuilt from the committed patches during this lane
  and reproduced the fired 0x0242 (371,212 B e2312402...), the send-2
  (373,056 B b8f6fe35...) and the send-3 R4 (382,072 B bb0434c2...) artifacts
  byte-identically - evidence/base-chain-rebuild.txt.
* Host tests: **386 checks, 0 failures** (C 293 + Python 93), including the
  byte-identical 40-B frame KATs, the new 9/8/4-step plan orders, the enum
  order ap_chan < vap_start_g0 < band_start, the start_ap post-site order and
  the G8 sequencing model (band 0 refused / band 1 passes).

## 6. Residual risks and how the fire resolves them [U]

The reorder is a one-line-number fix, but the fire will test three assumptions
that only the firmware can answer (one rung per boot; the abort rules are in
FIRE-RECIPE R5):

1. **Does the 0x0124 worker really store mctx[0x11]=1 for the ch40 AP before
   the G8 read?**  The fw-chan-worker decode says yes (the ctx word for ch40
   is 0x01000128, band 1 at ctx+0x11), and the R4 fire's own G8 refusal is
   consistent with the byte being 0 only because the 0x0124 had not run.  If
   G8 still refuses after the 0x0124, the store did not happen (wrong channel
   message / wrong module) - capture and stop, do not reorder further.
2. **Does the 0x0124 post set mctx+0x440 (G9) or bandobj+0xEC8 (G11)?**  The
   committed fw-chan-worker decode has their writer as CA 0x455D0 in the
   band/config-apply path [I], not the 0x0124 worker itself, so G9/G11 are
   expected to read 0 here.  If the new order makes one of them nonzero, the
   pre-post gate will refuse (-EBUSY/-EALREADY) and that is a NEW firmware
   fact: capture the pack and report; do not relax a gate in that boot.
3. **Does the 0x0124's own 5 GHz RF programme run at the new position?**
   The fw-chan-worker decode says that programme (CA 0x40684 -> 0x101890) is
   silently gated on module+0x440==1 && bandobj+0xEC8==1.  At this position
   those bytes are expected still 0 (their writer runs later in the start
   ladder), so the 0x0124 may again skip the RF writes - the R4b claim is the
   ORDER and the gates, not the RF programme.  If the ordering PASSes
   (G8/G12 fine, state[4]=0x00) but P1/P12/P13 stay at the pre-programme
   values, the next single rung is a lane that decodes the 0x440/0xEC8 writer
   (or a re-post of the 0x0124 after the 0x0242) - not part of this lane.

Everything above is fail-closed: a refusal before the wire is a clean
negative, never a half-started ladder.

## 7. References

* ../wifi-band-start-prestart/ (TABLES T7/T8/T10/T11, FIRE-RECIPE R4b row) -
  the resolver gate, the stock order, the probe pack, the R4b observation.
* ../wifi-band1-start-send-3/ - the R4 module and its own G1..G14 gates.
* ../wifi-fw-chan-worker/ (TABLES T5/T6) - the 0x0124 worker stores and the
  silent 5 GHz RF gate.
* ../wifi-band1-start-send/ + ../wifi-band1-start-send-2/ - the 0x0242 frame
  and the vap_add_index base (unchanged).
* root ledger ../../../../mem-entries.md, 2026-11-28 07:00 - the R4 fire.
