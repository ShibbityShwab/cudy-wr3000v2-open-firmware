# REPORT - the 5 GHz 160 MHz width selector + the DFS/CAC/NOL feature

**Lane:** shared task **task-245** (owner `dfs160b`), 2026-11-26.
**HOST-ONLY:** no device contact, no UART, no fire, no flash; the Lead fires.
Deliverables only under `opensource/docs/incoming/dfs160b/`; commits only
inside `opensource/`.  The `.ko` is local evidence only and is never
committed; no vendor binary, firmware byte, calibration value, MAC or secret
is stored here (decoded protocol bytes are quoted text only).

## 0. TL;DR

The feature the committed decode lanes prepared is implemented end-to-end,
independently of the parallel `../dfs160/` lane:

1. **the 160 MHz width selector** - the H2D g0-id1 key **0x0124 body[1]** is
   the vendor bandwidth selector (0/1/2/3 = 20/40/80/160 MHz); it is now
   planned from the cfg80211 chandef width, 160 MHz CCFS1-checked, behind the
   fail-closed module parameter **`ht_bw`** (0 = the fired wire byte 0).
2. **the DFS key 0x07E7 / CAC / NOL** - the cfg80211
   **`start_radar_detection` / `end_cac`** runtime posts the 29-B
   **0x07E7 {01} ARM** after the same live fail-closed precheck the ladder
   uses, runs the vendor **CAC tick arithmetic** (60000 ms class, 600000 ms
   TDWR class, first tick 63000 ms then 60000 ms), posts the **{00} CLEAR**
   and reports **NL80211_RADAR_CAC_FINISHED**; the **start_ap DFS gate**
   refuses an AP start on a DFS channel with no finished CAC inside the
   **1800000 ms NOL** window.  All behind the parameter **`dfs_cac`**
   (0 = the pre-feature behaviour, no op, no frame).
3. **the driver integration** - `patches/0001-dfs160b.patch` (14 files, 4
   new) on the fired `wifi-5g-dfs-relax` A-rung lineage, with
   `tools/build-dfs160b.sh` reproducing the **383,020 B** module
   (`006a94cf...`, 68 params, .text 164,980 B `5e14008f...`) with **0
   warnings / 0 errors** and **269/269** host checks.
4. **the fire recipe** - [FIRE-RECIPE.md](FIRE-RECIPE.md): the 160 MHz rungs,
   the cold/warm CAC rungs, the TDWR rung, the negative controls, the probes
   and the gated on-air expectations.

One independent-verification finding is carried here because it changes what
the Lead may fire: the sibling task-240 lane pins the **pre-5.19
nl80211_chan_width numbering** (160=4 / 80P80=5) while the 6.18.60 target
numbers are 80P80=4 / 160=5 / 320=13, so its `ht_bw=1` rung cannot accept a
real 160 MHz chandef.  This lane re-derived the pins from the kernel header
and pins them with `_Static_assert` in the kernel glue (sec. 6).

## 1. What is implemented, where

| file | role |
| --- | --- |
| `src/hi5622_dfsb.{h,c}` | the portable model: 0x0124 width plan + 160/80 CCFS1, the vendor band-1 channel table incl. ch144 + DFS/TDWR classes, the CAC/NOL state machine, the 29-B 0x07E7 frame builder, the ladder order contract, the combined start_ap plan.  Pure C99, kernel + host. |
| `src/hi5622_dfsb_k.{h,c}` | the kernel glue: `ht_bw`/`dfs_cac` parameters, the delayed-work CAC runtime, the two cfg80211 ops, the live dfs_set precheck reuse, the start_ap plan wrapper, the 7 nl80211 width `_Static_assert` pins. |
| `src/dfs160b_selftest.c` | 269 host checks (width pins, plans incl. the fixed 6.18 160 case, channels/ch144/TDWR, CCFS1, the full CAC tick sequences incl. the TDWR 10-tick run and the u32 wrap NOL gate, the ARM/CLEAR frame KAT, the order, the plan/gate matrix). |
| `tools/run-host-tests.sh` | builds + runs the model selftest with a plain host cc (`-Wall -Wextra -Werror`). |
| `tools/dfs160b_patch_test.py` | independent integration test: re-derives the width pins from the real 6.18.60 UAPI header, proves the patch carries the lane sources byte-identically, checks every patch hunk fact, the build-script pins and the docs. |
| `tools/build-dfs160b.sh` | the reproducible host build (base+source+patch pins -> make -> strip -> artefact identity -> host selftest). |
| `patches/0001-dfs160b.patch` | the integration patch (git-apply verified) on the pinned A-rung tree. |
| `hostapd/` | the 160 MHz (ch36) and DFS (ch52) fixtures. |
| `evidence/` | HOST-TESTS, BUILD, PATCH-TESTS (derived values only). |

## 2. The 160 MHz width selector

**Decode (committed, ../wifi-5g-160-dfs):** the 0x0124 channel-config body is
`{band, bandwidth, channel, 0}`; the firmware worker CA 0x841B8 stores
body[1] at module ctx+0x12, and the 5 GHz RF branch CA 0x40684 hands it to
the descriptor selector CA 0x101890.  The vendor wiphy VHT cap is
0x00400064 (HTC_VHT | 160 MHz | SGI80 | SGI160) - the driver already
advertises the capability; only the width byte was missing (it was built
from `vap_id`, always 0 on the AP-start path).

**Implementation:** `hi5622_dfsb_width_plan()` maps the cfg80211 width
(20_NOHT/20/40/80/160) to the vendor selector 0..3 and validates, for
80/160, that CCFS1 is the single-segment centre and CCFS2 is 0 (the vendor
cap has no 80+80 bit).  80+80, 320 MHz and the narrow 5/10/1/2/4/8/16 MHz
widths are refused `-EOPNOTSUPP`; a 2.4 GHz 80/160 request is refused too;
unknown values are `-EINVAL`.  `hi5622_scan.c` plans it before any frame
and sets `cp.bandwidth`; `hi5622_ap_frames.c` writes `body[1] = bandwidth`
(the previous source of that byte, `vap_id`, is 0 on this path, so
`ht_bw=0` is byte-identical to the fired wire).  The frame builder fails
closed on a selector outside 0..3.

**The width is verified host-side by** the plan unit (the dmesg line prints
`chandef width=5 -> body[1]=3`) and **on the box by** the module channel
context word `u32[mctx+0x10] = channel | band<<8 | bandwidth<<16 | idx<<24`
(for ch36 160 MHz: `0x00030124`).  That is the D3 probe.

## 3. The DFS key 0x07E7, the CAC and the NOL

**Runtime (implemented in `hi5622_dfsb_k.c`, model in `hi5622_dfsb.c`):**

1. `.start_radar_detection(wiphy, dev, chandef, cac_time_ms, link_id)`:
   AP interface, 5 GHz, `dfs_cac=1`; model begin (DFS channel required,
   TDWR from the 5600..5650 MHz class, `cac_ms = max(vendor class time,
   cac_time_ms)`); then the SAME live precheck the AP-start dfs_set step
   uses (`hi5622_scan_dfs_set_probe` + `hi5622_dfs_set_precheck`); on
   refusal it returns `-EAGAIN` with the whole probe pack in dmesg and posts
   nothing; on success it posts `0x07E7{01}` and schedules the first
   **63000 ms** tick.
2. The tick work: `ticks_done++`; while `ticks_done < ticks` re-arm with
   **60000 ms** (the vendor hmac_dfs_cac_timeout_fn arithmetic); when the
   count is reached post `0x07E7{00}`, record FINISHED and report
   **NL80211_RADAR_CAC_FINISHED** with the saved chandef + link_id.  A failed
   CLEAR aborts and reports **NL80211_RADAR_CAC_ABORTED** (a CAC that cannot
   be disarmed is not a passed CAC).  The finish is never early relative to
   cfg80211's `cac_time_ms` (the core WARNs), because 63 s > 60 s and
   603 s > 600 s.
3. `.end_cac()`: cancel the work, post `0x07E7{00}` if armed, abort the
   model.  The core emits NL80211_RADAR_CAC_ABORTED itself, so the driver
   sends no event here (one event per CAC).
4. The **start_ap DFS gate**: with `dfs_cac=1` and a DFS chandef, start_ap
   calls `hi5622_dfsb_k_ap_plan()` before any frame and refuses (-EAGAIN)
   unless a CAC finished on exactly this channel inside the 1800000 ms NOL
   window.  The gate runs before the VAP add, so a refused start sends
   nothing.
5. **TDWR:** the 600000 ms class is expressed cleanly host-side by setting
   `ieee80211_channel.dfs_cac_ms = 600000` for the 5600..5650 MHz channels
   (ch120/124/128); cfg80211 then passes `cac_time_ms=600000` to the op and
   the model runs the 10-tick, 603 s sequence.
6. **NOL:** the model keeps the 1800000 ms window after the finish and the
   start_ap gate enforces it (wrap-safe u32 ms difference; the radar D2H
   event id is still unmapped, so a real radar-triggered NOL is NOT faked).

**Parameters:** `ht_bw` and `dfs_cac` (0/1, 0444; >1 refused at use), both
requiring the beacon_static ladder; `dfs_cac=1` is mutually exclusive with
`dfs_set!=0` (both would own the 0x07E7 frames).

## 4. The honest limit: the ARM is gated on the 0x0065 VAP start

The firmware 0x07E7 worker gates on a started module context (state
u8[mctx+0x0E] in 1..3).  Our ladder sets that only in the **0x0065 VAP-start
step**, which runs *inside start_ap* - while cfg80211/hostapd ask for CAC
**before** start_ap.  The op therefore fails closed with `-EAGAIN` and the
full pack on a cold interface; it never posts a frame the worker would drop.
The sanctioned way to open the gate without changing the firmware contract is
the **warm CAC rung** (FIRE-RECIPE R2b): run one 5 GHz AP start (the R5
ladder sets the module ctx state), host-side stop it (the stability path
keeps the firmware AP alive and marks `ap_stopped_hostside`), then start the
DFS channel: RADAR_DETECT's precheck now sees the started module context and
the ARM can fire; after CAC the restart start_ap passes the DFS gate.  The
rungs test exactly this; the cold rung measures the refusal pack.

## 5. Build and tests

`tools/build-dfs160b.sh` (pinned base tree + patch + lane sources):

- 6.18.60 luofu kernel tree, OpenWrt GCC 14.4.0, 0 warnings / 0 errors;
- installed (OpenWrt strip, NO_RENAME=1) **383,020 B** sha256
  `006a94cf6bcb6f53887c83aa4ad3a0f057358301d9a350a139ae178acac0a183`;
- **68** params (66 base + `ht_bw` + `dfs_cac`), vermagic 6.18.60;
- .text **164,980 B** sha256 `5e14008f...`;
- the artifact is deterministic from base+patch (rebuilt twice, same pins);
- host: **269/269** model checks + the Python integration test.

Evidence: [evidence/BUILD.txt](evidence/BUILD.txt),
[evidence/HOST-TESTS.txt](evidence/HOST-TESTS.txt),
[evidence/PATCH-TESTS.txt](evidence/PATCH-TESTS.txt).  Reproduce:
`tools/build-dfs160b.sh` (base tree via
`../wifi-5g-dfs-relax/tools/build-dfs-relax.sh`).

## 6. The cross-lane verification finding (task-240)

The Lead was warned in-session.  The sibling lane `../dfs160/` pins
`NL80211_CHAN_WIDTH_160 = 4` / `80P80 = 5` / `320 = 6` (the pre-5.19
values) while the 6.18.60 UAPI is 80P80=4 / **160=5** / 320=13, and its
integration patch feeds the real kernel `p->width` into that model.  Direct
probe of its committed files (`cc main.c hi5622_dfs160.c`): width 5 ->
`-EOPNOTSUPP`, width 4 -> selector 3.  Its 133/133 selftest passes only
because it uses its own mis-pinned constants.  **Its `ht_bw=1` rung is
invalid as committed.**  This lane's pins are re-derived from the kernel
header by `tools/dfs160b_patch_test.py` and enforced in the module build by
`_Static_assert`.

## 7. Limits / honesty

- Host-only: nothing was loaded, no frame was posted, no beacon was observed.
  Every bench claim in [FIRE-RECIPE.md](FIRE-RECIPE.md) is a prediction with
  a probe.
- The 5 GHz **on-air** PASS (160 MHz AP beacon / DFS AP serving) is gated on
  the firmware band-1 TX scheduler (ledger 2026-11-27 05:30 and the
  tx-scheduler lane); the host+firmware-state rungs stand alone and are
  fired first.
- The radar **D2H event id is unmapped**: no radar event is faked, the NOL is
  the host-side window after a finished CAC.
- The 0x0124 body[1] 40/80/160 values 1/2/3 are [I] from the vendor
  width->value table + the assemble order; probe D2/D3 pins them.
- The warm-CAC rung depends on the post-stop module context state (bench
  datum in R2b); if it is gone, the cold rung's pack is the honest result.
- No vendor binaries/firmware bytes/secrets; protocol bytes are quoted text.

## 8. Read in this order

1. [FIRE-RECIPE.md](FIRE-RECIPE.md) - the Lead's procedure (R0 gates, R1
   160 MHz, R2 CAC, R3 negatives, D-probes, PASS/FAIL/ABORT).
2. [TABLES.md](TABLES.md) - every pin, conversion and probe expectation.
3. [BUILD-NOTES.md](BUILD-NOTES.md) - the build chain + reproduce.
4. This report, sec. 4 (the honest limit) and sec. 6 (the sibling defect).
