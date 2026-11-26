# REPORT - the R5 reorder: the AP config frames BEFORE the 0x0065 VAP start and the 0x0242 band start

**Lane:** wifi-band1-start-send-5 (shared task **task-213**), 2026-11-26.
**HOST-ONLY**: no device contact, no fire.  **The Lead fires.**
**Deliverable:** patches/0001-r5-order.patch (the pure reorder on the pinned
gate-inverted base = the tree of the deployed module 9bf1d846) + the updated
host tests + the build script + this recipe set.

Grades: **[V]** re-derived/committed decode; **[V\*]** bench-measured by a
committed lane or the Lead; **[I]** inference; **[U]** still needs a device read.

## 1. What the 2026-11-26 fire showed [V\*]

The fire loaded the gate-inverted module 9bf1d846 (the R4b order + the
corrected G9'/G11' polarity) with the R4b load line and a ch40 AP.  It worked:
the corrected gates, the R4b order, the 0x0065 and the 0x0242 all succeeded.

~~~
cfg80211: vap_start_g0 post: mctx[0x0e]=1 mctx[0x11]=1 state1=.. state4=0x00 ... (G12 pass - the 0x0242 band start may be posted)
cfg80211: band_start post: module=0 bandobject=nonzero state[1]=2 state[4]=0x00 counter=1 (...)  (already started (state[1]=2) - CA 0x4AF0C returns at 0x4AF2C)
~~~

Read-only probes after the fire:

| object | value | meaning |
| --- | --- | --- |
| 0x40817A40 (band-1 run-state) | **0x01000219** | state[0]=0x19, state[1]=0x02, state[4]=0x00 |
| P+0xF04 (band mode) | **3** | the 0x01F7 RF-mode byte |
| R+0x3A8 / R+0x3AC (RF image) | **0x01800060 / 0x03E6138A** | the 5 GHz RF programme ran |
| page/state/mask | **golden:1** | no firmware fault |
| AP state | **AP-ENABLED** | hostapd start completed |
| 5200 MHz observer scan | **no beacon** | the remaining rung |

So the band is armed, the RF is programmed, the gates pass, the state[4] is
0x00 - and still nothing is transmitted at the TBTT.

## 2. Root cause: the band is started BEFORE the AP config stages the beacon data [V decode + I ordering]

The stock order is the answer: channel config (key 0x0124) -> AP config
(key 0x0127: ap_ssid + ap_cfg) -> start_vap (the g0-id1 key 0x0065) ->
green_ap (key 0x0242).  The R4b ladder was
vap_add -> desc_table -> dfs_mode -> ap_chan -> vap_start_g0 -> band_start ->
ap_ssid -> ap_cfg -> vap_start: the band STARTED BEFORE the AP config was
posted.

The committed beacon-TX decode (../wifi-fw-beacon-tx, task-192) says why that
cannot work:

* key **0x0127** (worker CA 0x84DA4) stages VAP fields only - VAP+0x10 =
  beacon interval, VAP+0x14 = DTIM, the RSN/vendor IE copies, VAP+0x138/0x13A
  MDID/capability bits, VAP+0x34C/0x360 HT/VHT hints.  It has **no band field
  and no beacon-enable bit**, and it creates no beacon object (T4 there).
* key **0x0065** (worker CA 0x46394) is where the per-link beacon state is
  **created**: on the first start of the VAP on the band (guard
  `u8(perlink+0x6C6) != 1`) it allocates the 280-B beacon buffer
  (perlink+0x6B8), the 92-B beacon descriptor (perlink+0x6C0), builds the
  0x310-byte beacon object from **ctx+0x4AC**, publishes perlink+0x6BC, sets
  +0x6C4 = 16 / +0x6C6 = 1 - and only then calls the band start CA 0x971D8
  (T5 there).  The same lane's cause C1 states the fix: "a single ordered
  re-post (VAP add band=1 -> AP config -> VAP start)".

Therefore in the R4b order the 0x0065 created the beacon state, and the
0x4AF0C/0x971D8 band start armed the TBTT, **before** the 0x0127 had staged
the beacon interval, the DTIM period and the beacon template IEs.  The beacon
buffer/descriptor were built from a VAP whose +0x10/+0x14 and IE slots were
still what the vap_add left there, and the per-link +0x6C6 = 1 guard means a
later 0x0065 re-post cannot rebuild them (and our ladder never re-posts a
consumed frame).  That is exactly the observed residual: armed, RF programmed,
AP enabled - no beacon.  The R0 readback of ../wifi-fw-tbtt-arm
(per-link +0x6C4 = 16, +0x6C6 = 1, beacon buffer pointer nonzero [V\*])
confirms the beacon state exists but was created too early.

**The R5 fix is the stock order**: post the AP config frames
(ap_ssid + ap_cfg) **before** the 0x0065 VAP start, and the 0x0242 band start
after both, so the firmware has the beacon interval/DTIM/template in the VAP
when the 0x0065 builds the per-link beacon state and when the TBTT fires.

## 3. The change (the whole patch)

Five source files, no frame change, no new file, no new parameter:

| file | change |
| --- | --- |
| hi5622_scan_logic.h | enum renumber: VAP_ADD=1, DESC_TABLE=2, DFS_MODE=3, **CHAN=4, SSID=5, AP_CFG=6, VAP_START_G0=7, BAND_START=8**, VAP_START=9; plan/order documentation updated (R5 paragraph) |
| hi5622_scan_logic.c | hi5622_beacon_plan() emits vap_add -> desc_table -> dfs_mode -> **ap_chan -> ap_ssid -> ap_cfg -> vap_start_g0 -> band_start -> ap_vap_start**; the R5 comment explains the beacon-template requirement |
| hi5622_scan.c | the start_ap post sites are split: a new static helper `hi5622_scan_post_ap_seq_frames()` carries the unchanged -ENODATA sequence contract; the beacon_static path posts the **AP config phase** (frames 0..2 = ap_ssid, ap_auth on WAL, ap_cfg) after the 0x0124 post + channel-activation wait and **before** the 0x0065 block, then the 0x0065, then the 0x0242, then a **phase-B** call for the carrier-owned trailing VAP-start frame (WAL g2 index 3).  The legacy (non-beacon_static) path keeps the whole sequence after the mode-1 VAP base, byte-identical.  New dmesg line: "AP config posted BEFORE the 0x0065 VAP start / 0x0242 band start (R5): beacon_interval=.. dtim=.. head=.. tail=.. - the firmware beacon template is in place for the TBTT".  Parameter descriptions corrected to the real order (the band_start description had been stale since R4b). |
| hi5622_vap_start_g0.h | placement contract: AFTER the 0x0124 channel post **and the AP config frames** (0x0127: beacon interval, DTIM, beacon template), before the 0x0242 |
| hi5622_band_start.h | placement contract: AFTER the 0x01F7, the 0x0124 post, **the AP config frames** and the 0x0065 VAP start; the beacon template is already in the VAP context when the TBTT fires |

The full plan with all knobs 1 is **9 steps**:

    vap_add desc_table dfs_mode ap_chan ap_ssid ap_cfg vap_start_g0 band_start ap_vap_start

The frames are untouched: the patch touches no carrier/builder source
(vapstart_frames.c, hi5622_band_start.c, hi5622_ap_frames.c, Kbuild all keep
their pinned base shas; see BUILD-NOTES).  The 40-B key-0x0065 KATs, the 33-B
key-0x0242 frame and the 184-B ap_cfg frame are byte-identical.

## 4. All fail-closed gates kept [V]

Nothing was relaxed.  The G1..G14 probe pack, the G12 latch and the
fail-closed parameter gates are exactly the gate-inverted (task-212) code:

* **G8** (`u8[mctx+0x11] == 1`, 5 GHz) still runs after the 0x0124 ap_chan
  post - in R5 it now runs after the AP config too, which does not touch
  mctx[0x11].
* **G9'/G11'** (mctx[0x440] == 1, bandobj+0xEC8 == 1 - the corrected
  RF-enable/band-up polarity of ../wifi-vap-start-gates) are unchanged.
* **G5** (u32[mctx+0x190] != 0) still hard-gates the 0x0065 post; the vap_add
  is still posted first and the AP config frames do not touch the VAP object
  pointer.
* **G12** (mctx[0x0E] == 1 after the 0x0065 post) still alone allows the
  0x0242; the band_start block still refuses -EIO otherwise.
* The fail-closed parameter gates (vap_start_g0 > 1, vap_start_g0=1 without
  beacon_static=1, band_start/desc_table/dfs_mode sets) run before any frame.
* **65 module parameters** (no new knob; same load line).

## 5. Artifact and host tests [V]

* Host-built module: /tmp/r5-artifact/hi5622_wifi.ko, **383,240 B**,
  sha256 **24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c**,
  unstripped 1,424,680 B sha 233df986..., .text
  a727c339..., 65 params, vermagic 6.18.60, **0 warnings**, 12 cfg80211
  symvers; +864 B vs the gate-inverted base (382,376 B 9bf1d846...) from the
  helper + the phase split.  Local evidence only (never committed).
* The chain reproduces from the committed patches: the gate-inverted base
  source pins match (hi5622_scan.c ab14fa07..., hi5622_scan_logic.c
  ab7b8bac..., hi5622_scan_logic.h 3bfe1239..., hi5622_vap_start_g0.c
  346b5441..., hi5622_vap_start_g0.h 92b17b15..., hi5622_band_start.h
  443b071d...) and the deployed base artifact 9bf1d846... is present; the R5
  tree pins are in BUILD-NOTES and enforced by the build script.
* Host tests: **405 checks, 0 failures** (C 295 + Python 110), evidence/
  host-tests.txt: the byte-identical 40-B KATs, the 9/8/4-step plan orders,
  the enum order ap_chan < ap_ssid < ap_cfg < vap_start_g0 < band_start <
  ap_vap_start, the start_ap post-site order (AP config -> 0x0065 probe/post
  -> G12 -> 0x0242 build -> trailing carrier VAP start), the G8 sequencing
  model and the corrected G9'/G11' polarity.  No device contacted.

## 6. Residual risks and what the fire resolves [U]

The change is an order-only rung, but the fire tests one decode assumption
that only the firmware can answer (abort rules in FIRE-RECIPE R5):

1. **Does the 0x0065 worker build a usable beacon state once the 0x0127 has
   staged VAP+0x10/+0x14/IEs?**  The beacon-TX decode says the state is built
   at 0x0065 from ctx+0x4AC and staged by 0x0127 [V decode]; the fire's
   P13/P14 readback (per-link +0x6C6, beacon buffer, the 5200 beacon) is the
   answer.  If the beacon still does not appear while P1 = 0x1029,
   state[4] = 0x00 and P12 = the RF image, the next rung is a new lane that
   decodes what the 0x0065 frame builder at ctx+0x4AC actually reads
   (interval/DTIM vs the staging offsets), **not** another reorder.
2. **Does the AP config phase change any gate input?**  0x0127 writes VAP
   fields only (no band, no enable, no VAP-object pointer), so G5/G8/G9/G10/
   G11/G13 are expected to read exactly what the R4b fire read.  If a gate
   now refuses, that is a NEW firmware fact: capture the pack and stop; do
   not relax a gate in that boot.
3. **Restart after a host-side stop:** the restart path re-posts the AP config
   against the live VAP and does not re-post the 0x0065 (the never-start-a-
   live-VAP-twice guard), so the beacon state from the first start stays; the
   restart stays inside this lane's order semantics.
4. **WAL carrier (ap_start_mode=1):** the trailing carrier-owned VAP start is
   still posted after the 0x0242 (phase B); the g0 carrier (the fire's
   default) ends its sequence at ap_cfg and posts its VAP start after the
   0x0242 exactly as the plan says.  No frame bytes change on either carrier.

Everything above is fail-closed: a refusal before the wire is a clean
negative, never a half-started ladder.

## 7. References

* ../wifi-band1-start-send-4/ - the R4b order + the fire recipe lineage.
* ../wifi-vap-start-gates/ - the gate-inverted module 9bf1d846 (the base),
  the G9'/G11' polarity and the R4b fire evidence.
* ../wifi-fw-beacon-tx/ - the beacon TX chain: 0x0127 stages VAP+0x10/+0x14/
  IEs, 0x0065 creates the per-link beacon state and calls the band start;
  cause C1 / the ordered-re-post note.
* ../wifi-fw-tbtt-arm/ - the per-band TBTT/beacon-state probes (R0 readback).
* ../wifi-band1-start-send-3/ + ../wifi-band1-start-send/ - the 0x0065 and
  0x0242 frames and their gates.
* root ledger ../../../../mem-entries.md - the 2026-11-26 fire [V\*].
