# REPORT - the 160 MHz width selector + the DFS/CAC/NOL runtime

**Lane:** dfs160 (shared task **task-240**), 2026-11-26. **HOST-ONLY**:
static decode reuse ([V] facts from the committed lanes), clean-room driver
implementation, host build + host selftest. No device contact, no fire. The
Lead fires with [FIRE-RECIPE.md](FIRE-RECIPE.md).

Grades: **[V]** re-derived by a committed decode lane; **[V*]** bench-measured
by a committed fire; **[I]** inference/implementation choice; **[U]** needs a
device read. The decode evidence is not duplicated here - addresses, frames
and timings are quoted from `../wifi-5g-160-dfs/TABLES.md` (T2/T4/T5/T7),
`../wifi-5g-dfs-set/` and `../wifi-5g-cac-context/`.

## 1. Result

The feature is implemented as a **patch delta on the bench-fired A-rung
lineage** (`../wifi-5g-dfs-relax`; order
`0x0065 VAP start -> 0x07E7 ARM -> 0x01F7 dfs_mode -> 0x0242 band start`).
Two fail-closed module parameters were added:

* **`ht_bw`** - puts the decoded vendor bandwidth selector into the 0x0124
  body[1] (20M=0/40M=1/80M=2/160M=3), validates the chandef first and refuses
  80+80/320 (the vendor wiphy VHT cap 0x00400064 has neither bit);
* **`dfs_cac`** - implements the cfg80211 radar-detect contract: a host CAC
  timer with the decoded 60 s/600 s plan and vendor tick arithmetic, the live
  fail-closed ARM precheck, the 0x07E7{00} CLEAR on completion,
  CAC_FINISHED/ABORTED reporting, an AP-start DFS gate and the 30 min NOL
  model; it refuses to share the 0x07E7 key with `dfs_set` (one owner).

ch144 (5720 MHz) is restored in the 5 GHz wiphy channel list ("the ch144 gap"
of TABLES T8). The module builds with **0 warnings / 0 errors**, **68
parameters** (66 base + `dfs_cac` + `ht_bw`), installed form **384,072 B
sha256 `0313dcfd...`**, `.text` **165,236 B sha256 `ec2e7d9f...`**; the
portable model's host selftest is **133/133** ([evidence/](evidence/)).

The on-air half stays gated on the documented 5 GHz band-1 beacon blocker (the
firmware TX scheduler, bench 2026-11-27: every host-visible element armed,
beacon absent). The first rungs therefore prove the selector in the firmware
module context (an existing readback prints it) and measure the DFS ARM gates
with an explicit probe pack - the runtime never fabricates a CAC completion.

## 2. The 160 MHz width selector

### 2.1 The decoded path (recap, [V] unless marked)

    cfg80211 chandef width (hostapd vht_oper_chwidth=2 = 160 MHz)
      -> our start_ap: settings->chandef.width / center_freq1 / center_freq2
      -> ht_bw=1: width -> vendor selector, segment-centre check
      -> H2D g0-id1 key 0x0124 body {band, bandwidth, channel, 0}   [V]
      -> firmware worker CA 0x841B8: body[1] -> module ctx+0x12      [V]
      -> band-1 RF branch CA 0x40684 -> CA 0x101890(band, field=ctx+0x12,
         channel) -> per-bandwidth descriptor window (CA 0x10BE68)     [V]
      -> CA 0xA2FA2 / the ch36-64 image CA 0x89924 / latch CA 0x89840 [V]

Width -> selector table (T7/TABLES): 20_NOHT/20 -> 0; 40 -> 1; 80 -> 2;
160 -> 3 **[I] the 160 value** (HAL assemble-enum order; the fire measures it,
sec. 2.4); 80+80 -> refused, **320 -> refused** (decode: no 80+80 bit, no 320
support).  160 MHz segments [I, IEEE]: 36-64 -> CCFS1 50; 100-128 -> CCFS1
114; 149-177 -> CCFS1 163.  CCFS2 must be 0 (no 80+80).

### 2.2 The implementation (patch sites)

1. **Portable decision** `hi5622_dfs160_width_selector()` /
   `hi5622_dfs160_width_check()` (`src/hi5622_dfs160.c`): the width map, the
   160/80 CCFS1 checks and the fail-closed refusals.
2. **start_ap gates** (`hi5622_scan.c`): `ht_bw>1` refused before any frame;
   with `ht_bw=1` the chandef is validated (width, channel, centre pair) and
   the selector stored in the plan-local `ht_bw_sel`; a dmesg line
   `cfg80211: ht_bw=1 width selector: cfg80211 width=.. -> 0x0124 body[1]=..`
   records the decision.
3. **Wire path**: `struct hi5622_chan_cfg_params` (hi5622_frames_api.h) and
   `struct haf_ap_params` (hi5622_ap_frames.h) gain a `bandwidth` byte;
   the provider passes it through (`hi5622_vapscan_provider.c`) and
   `haf_build_body_chan()` writes `body[1] = bandwidth` unless the sentinel
   `HI5622_DFS160_BW_LEGACY (0xFF)` selects the old `vap_id` byte.  With the
   default `ht_bw=0` every byte of the pre-feature 0x0124 frame is identical
   (the bench-validated F2/A-rung wire is preserved).
4. **The firmware witness**: the existing dmesg line
   `start_ap beacon_static module ctx readback: ... (channel=%u band=%u field=%u valid=%u)`
   prints `field = ctx+0x12` = the stored bandwidth selector, and the
   `ap_chan` post line gains
   `... carries the ht_bw=1 bandwidth selector body[1]=...`.  Together with
   the new `width selector` line these are the E-probes for rung 160-1.

### 2.3 Segment rules (fail closed)

* a 160 MHz chandef whose CCFS1 is not the IEEE segment centre of the control
  channel is refused before any frame (-EOPNOTSUPP);
* CCFS2 != 0 is refused (no 80+80);
* the 80 MHz CCFS1 (when supplied) must match the channel's segment;
* the cfg80211 chandef checks run first (cfg80211 validates channel/centre
  pairs); the driver check is the belt: it never posts a selector for a width
  the firmware cannot select.
* The 36-64 160 MHz segment contains the DFS channels 52-64: the rung is
  capability-only until the DFS path is proven; the DFS-clean 160 segment is
  100-128 (fixture note in hostapd/160-36.conf and FIRE-RECIPE 160-2).

### 2.4 The [I] 160 selector value and how the fire pins it (PROBES P-160a)

The vendor HAL assemble-enum order makes 3 the natural 160 value, but the
decode lane grades the exact value **[I]/[U]**.  This lane does not hard-code
the belief as proven: rung 160-1 fires `ht_bw=1` with the VHT160 hostapd
chandef and reads the pre-existing module readback line's `field=` byte
(ctx+0x12) plus the new `body[1]=` line.  PASS-record = the two bytes agree
with the chandef width; a mismatch is a measured correction to T3, not a
host-side edit.

## 3. The DFS / CAC / NOL runtime

### 3.1 The decoded vendor machinery (recap, [V])

* CAC arm/stop = H2D g0-id1 key **0x07E7**, body **{01}/{00}**, 29-byte frame
  (the frame builder already landed in `hi5622_dfs_set.c`, fired as the
  register-only ARM on the 2026-11-27 A-rung);
* CAC times: **60000 ms** default, **600000 ms** for the 5600..5650 MHz TDWR
  window (ch100-128 window); tick arithmetic: CAC>59999 -> a **63000 ms**
  first timer, max = cac_ms/60000, then 60000 ms re-arms; completion posts
  {00} and starts all BSS;
* non-occupancy period **1800000 ms** (30 min) after a radar hit ([V]
  constant; the radar D2H event id/layout is **[U]**, no committed lane maps
  it);
* the init'able DFS set is **52-64 U 100-144** (16 channels, incl. ch144).

### 3.2 The cfg80211 contract in this kernel (6.18)

* `start_radar_detection(wiphy, dev, chandef, cac_time_ms, link_id)` - return
  0 and cfg80211 marks the wdev CAC started; the driver must later call
  `cfg80211_cac_event(dev, chandef, NL80211_RADAR_CAC_FINISHED|ABORTED,
  GFP_KERNEL, link_id)`;
* `end_cac(wiphy, dev, link_id)` - cfg80211's abort/teardown; the driver only
  stops its own state;
* **`cfg80211_cac_event(FINISHED)` WARNs when it arrives before
  `cac_start_time + cac_time_ms`** (net/wireless/mlme.c) - therefore the
  first timer is the vendor 63000 ms, not 60000 ms.  The model's schedule is
  exactly the vendor's: 60 s CAC -> one 63 s callback; 600 s TDWR -> 63 s + 9
  x 60 s = 603 s (never early, matching the vendor's own first-tick margin).

### 3.3 What the runtime does (patch sites)

* `hi5622_dfs_cac.c` (new kernel object): the `start_radar_detection` op
  validates AP iftype + 5 GHz + DFS channel, begins the portable CAC plan,
  takes the **same live precheck pack as the AP-start dfs_set step**
  (`hi5622_scan_dfs_set_probe()` + `hi5622_dfs_set_precheck()`) and only then
  posts the 29-byte 0x07E7{01} ARM through the one `hi5622_frame_post()`
  path; it arms a `delayed_work` timer, reports `NL80211_RADAR_CAC_STARTED`,
  and on completion posts 0x07E7{00} CLEAR and reports CAC_FINISHED (only
  when the op owns the CAC).  `end_cac` cancels the timer outside the lock,
  posts CLEAR once and aborts.
* **AP-start DFS gate** (`hi5622_scan.c` + `hi5622_dfs_cac_ap_gate()`): with
  `dfs_cac=1`, a DFS channel cannot reach start_ap's frame ladder unless the
  CAC on exactly that channel finished and no NOL is active; the refusal is
  -EAGAIN with the dfs_cac status line as evidence.
* **One owner per load**: `dfs_cac=1` together with `dfs_set!=0` is refused
  before any frame (both would post key 0x07E7; the fired A-rung uses
  `dfs_set=1`, the runtime rung uses `dfs_cac=1`).
* **Status line**: `cfg80211_status` gains
  `dfs_cac: ht_bw=.. dfs_cac=.. state=.. channel=.. cac_ms=.. ticks=../..
  nol_ms=.. radar=.. arms=.. finishes=.. aborts=.. refusals=.. last_rc=..`.
* **Nothing is fabricated**: the radar D2H path is not decoded, so the
  runtime never emits NL80211_RADAR_DETECTED; the NOL model (30 min, radar
  types 0..8) is implemented and unit-tested but wired to no event source.

### 3.4 The pre-VAP-start gate - the honest expectation for rung DFS-2

cfg80211 calls `start_radar_detection` **before** `start_ap`.  The 0x07E7
firmware worker's started-module gate (module ctx state mctx[0x0E] in 1..3)
is written by the AP ladder's **0x0065 VAP start**, i.e. later.  The runtime
therefore fails closed with **-EAGAIN plus the probe pack** (mctx/band/state/
ec8/cac/ctx_present/b4a0/regs) instead of posting a frame the worker would
silently drop:

    cfg80211: dfs_cac ARM REFUSED (-EAGAIN): ... (the firmware ARM worker needs
              the 0x0065 VAP start's started module context; no frame posted)

That refusal *is* the measurement.  If the bench shows the gates already pass
(e.g. an interface-creation path sets the module context), the op posts the
ARM and the full CAC clock runs (rung DFS-2 PASS).  Either outcome is a
first-class result; the follow-up for the refusal branch is the vendor's own
VAP-create ordering, not a synthetic event.  The AP-start DFS gate is the
belt that keeps a DFS AP from beaconing without CAC even if hostapd could be
configured to skip DFS.

## 4. ch144 (the gap in the advertised channel list)

The 5 GHz wiphy list (hi5622_cfg80211.c) advertised 36-48 and 52-64/100-140.
The vendor band-1 table CA 0x117AD4 has 29 channels including **144**; the
DFS set the firmware can initialise is 52-64 U 100-144 (TABLES T3/T8).  This
lane adds `HI5622_CH5G(144, 5720, HI5622_DFS)`.  It is in the vendor channel
table (the band5g gate validates against the same 29-channel table), so no
other capability change is needed.  160 MHz segment 100-128 does not span
ch144 - ch144 matters for the DFS-list completeness and for 80 MHz segment
132-144.

## 5. The patch (15 files, 4 new) and build

    new    hi5622_dfs160.h/.c        portable model (width/CFS/plan/state)
    new    hi5622_dfs_cac.h/.c       kernel runtime (ops, timer, gates, status)
    Kbuild                           + two objects
    cfg80211-provider.symvers        + cfg80211_cac_event (EXPORT_SYMBOL)
    hi5622_scan.c/.h                 ht_bw + gates + 0x0124 selector + probe export
    hi5622_frames_api.h              chan_cfg bandwidth field
    hi5622_ap_frames.c/.h            bandwidth field + body[1] write
    hi5622_vapscan_provider.c        pass the field through
    hi5622_cfg80211.c/.h             runtime pointer + ch144 + status line
    hi5622_cfg80211_ops.c            start_radar_detection + end_cac

Build (pinned, [evidence/BUILD.txt](evidence/BUILD.txt)): base tree
`/tmp/dfs-relax-build`, OpenWrt 6.18.60 toolchain/kernel tree
(selectorfix-img, has Module.symvers), `make ... M=... modules` -> **0
warnings, 0 errors**; installed form 384,072 B `0313dcfd...`, .text 165,236 B
`ec2e7d9f...`, **68 params**, vermagic 6.18.60, depends cfg80211.  Host
selftest **133/133** (width map, CCFS1, segment refusals, DFS set, TDWR,
CAC plan incl. the 603 s TDWR schedule, state machine, gate, NOL, status).

## 6. Fire summary (details: FIRE-RECIPE.md)

* **160-1 (selector)**: `ht_bw=1` + the VHT160 ch36 hostapd fixture.  PASS =
  the `width selector` and `body[1]=` lines show 3, the module ctx readback
  `field=` shows 3, hostapd accepts VHT160 and reaches AP-ENABLED, page
  golden.  On-air beacon is the optional jackpot (band-1 beacon blocker).
* **160-2 (80 MHz control)**: `ht_bw=1` + an 80 MHz ch36 chandef (selector 2)
  in a separate fresh boot: proves the width path actually varies.
* **DFS-2 (CAC)**: `dfs_cac=1` + the DFS ch52 hostapd fixture
  (`ieee80211h=1 country_code=US`): rung PASS is either (a) ARM posted ->
  63 s CAC timer -> CLEAR -> hostapd CAC-finished -> AP-ENABLED, or (b) the
  documented -EAGAIN pre-VAP gate with the probe pack; both are recorded, in
  both cases the page must stay golden.
* **Controls**: `ht_bw=2` and `dfs_cac=2` refused at load/use; `dfs_cac=1 +
  dfs_set=1` refused before any frame; `dfs_cac=0` = the byte-identical
  pre-feature AP path.

## 7. Limits and open items

* **Not fired** by this lane; every rung needs the Lead's attended bench
  procedure (recovery armed, UART read-only, fresh boot per rung).
* **Base lineage**: the patch is authored and built on the fired A-rung
  lineage (`/tmp/dfs-relax-build`), not on IMAGE #3's merged module.  The
  rebase onto IMAGE #3 is a separate integration step (the IMAGE #3 plan has
  no 0x0065/0x0242 steps, so the DFS order rung does not exist there yet);
  the portable model/runtime sources are lineage-agnostic.
* **[U] 160 selector value 3** - measured by rung 160-1 (sec. 2.4).
* **[U] pre-VAP ARM gate** - measured by rung DFS-2 (sec. 3.4).
* **[U] radar D2H event** - not decoded; no radar handling, no off-channel
  CAC, no 160 MHz DFS-wide-span accounting beyond the AP gate.
* HE160 is deliberately out of scope for the first rung (VHT160 isolates the
  width variable; the he_caps=1 advertisement already carries the HE 160
  channel-width bit from the HE lane).
* No bench contact, no vendor bytes, no secrets; the .ko and the patched
  tree are local evidence only.
