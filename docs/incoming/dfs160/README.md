# dfs160 - the 5 GHz 160 MHz width selector + DFS/CAC/NOL feature (task-240)

**Lane:** dfs160 (shared task **task-240**), 2026-11-26. **HOST-ONLY**: static
decode reuse from the committed lanes + clean-room driver patch + host build +
host tests; **no bench contact, no device write, no flash**. The Lead fires.
Deliverables live under `opensource/docs/incoming/dfs160/` (the task's advisory
write scope).

## The gap this lane closes

The 2026-11-27 bench state (IMAGE #3 clean-room 6.18.60 stack) still lists
DFS/160 MHz as an open feature. The committed decode lane
[`../wifi-5g-160-dfs`](../wifi-5g-160-dfs/) proved *what* to send:

1. **160 MHz** - the H2D g0-id1 key **0x0124** channel-config body is
   `{band, bandwidth, channel, 0}`; **byte[1] is the vendor bandwidth
   selector** (20M=0 / 40M=1 / 80M=2 / 160M=3), stored by the firmware worker
   CA 0x841B8 at module ctx+0x12 and consumed by the band-1 RF branch
   CA 0x40684 -> CA 0x101890.  Our builder sent `{band, 0, channel, 0}`
   (the byte was mis-documented as a VAP index), so the firmware never got a
   80/160 selector;
2. **DFS/CAC** - the H2D g0-id1 key **0x07E7** with a 1-byte body
   `{01}`=ARM / `{00}`=CLEAR, CAC **60000 ms** (UNII-2/2e) /
   **600000 ms** (5600..5650 MHz TDWR window), the vendor tick arithmetic
   (63000 ms first timer, 60000 ms re-arm) and the **1800000 ms** (30 min)
   non-occupancy period.  The 0x07E7 frame *writer* was landed by the fired
   sibling lanes [`../wifi-5g-dfs-set`](../wifi-5g-dfs-set/) and
   [`../wifi-5g-dfs-relax`](../wifi-5g-dfs-relax/), but **no host CAC state
   machine, no cfg80211 radar-detect ops and no AP-start DFS gate existed**;
3. **ch144** - the vendor band-1 table CA 0x117AD4 carries ch144 (5720 MHz)
   and the init'able DFS set is 52-64 U 100-144; our wiphy list stopped at
   ch140 ("the ch144 gap").

The bench dependency (documented in the decode lane and unchanged here): the
5 GHz band-1 MAC start / firmware beacon-TX scheduler is still the last
on-air blocker (the 2026-11-27 fires armed every host-visible element and the
beacon stays absent; the last layer is firmware-internal).  **Both features
are therefore implemented and host-proven first**: the selector is read back
from the firmware module ctx (existing dmesg `field=` byte + the new
`ht_bw=1` line) and the DFS/CAC rung measures the live ARM gates with an
explicit probe pack - it never fabricates a CAC completion.

## Deliverables

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the implementation: 160 MHz selector path, DFS/CAC runtime + cfg80211 contract, the ch144 fix, fail-closed matrix, build, grades, limits |
| [TABLES.md](TABLES.md) | T1-T10: pins, H2D keys, width mapping, DFS channel set, CAC/NOL constants + tick plan, state machine, cfg80211 contract, patch-site map, module parameters |
| [patches/0001-dfs160.patch](patches/0001-dfs160.patch) | the driver integration against the fired **wifi-5g-dfs-relax** A-rung base (15 files, 4 new) |
| [src/hi5622_dfs160.{h,c}](src/) | the portable model: width selector, CCFS1 checks, DFS set incl. ch144, CAC/NOL plan + state machine |
| [src/hi5622_dfs_cac.{h,c}](src/) | the kernel runtime: cfg80211 `start_radar_detection`/`end_cac`, the host CAC timer, the live ARM precheck, the AP-start DFS gate, the status line |
| [src/dfs160_selftest.c](src/dfs160_selftest.c) | host selftest, **133 checks, 0 failed** |
| [tools/build-dfs160.sh](tools/build-dfs160.sh) | reproducible pinned build + artefact identity + host tests |
| [tools/run-host-tests.sh](tools/run-host-tests.sh) | the host selftest runner (no kernel/device) |
| [BUILD-NOTES.md](BUILD-NOTES.md) | base/source/artefact pins, build chain, reproduce |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the attended fire: 160 selector rung + DFS CAC rung + negative controls; probes E1-E9; PASS/FAIL/ABORT |
| [hostapd/](hostapd/) | the two AP fixtures (VHT160 ch36; DFS ch52 with ieee80211h=1) |
| [evidence/](evidence/) | build evidence + host-test log (text only) |

## Pins (see BUILD-NOTES.md)

    base      wifi-5g-dfs-relax lineage (/tmp/dfs-relax-build, the fired A-rung
              chain 0x0065 -> 0x07E7 ARM -> 0x01F7 -> 0x0242)
    patch     15 files, sha256 98bc135a...  (4 new: hi5622_dfs160.{c,h}, hi5622_dfs_cac.{c,h})
    module    installed 384,072 B sha256 0313dcfd5b88af683b9c02221d0e38020885a21200480f9ea4b02be2b89a02ff
              .text 165,236 B sha256 ec2e7d9f603845657942a40a0867eef6a1ee5eabbccc7c212481406bc543783c
              68 params (66 base + dfs_cac + ht_bw), vermagic 6.18.60, depends cfg80211
    host      build 0 warnings / 0 errors; selftest 133/133

## The two knobs (both fail-closed default 0, one meaning per load)

* `ht_bw=0/1` - 0 keeps the byte-identical pre-feature 0x0124 wire (legacy
  vap_id byte); 1 derives body[1] from the cfg80211 chandef width and checks
  the 160 MHz segment centre (80+80/320 refused, the vendor cap has neither).
* `dfs_cac=0/1` - 0 keeps the pre-feature AP path and registers no DFS op;
  1 enables the cfg80211 `start_radar_detection`/`end_cac` runtime, the AP
  start DFS gate, and forbids a simultaneous `dfs_set` (one 0x07E7 owner).

## Read order

1. [REPORT.md](REPORT.md) sec. 1 (result) and sec. 2-3 (the two features).
2. [TABLES.md](TABLES.md) T3/T5/T6 (width/DFS/timing) and T9 (patch map).
3. [FIRE-RECIPE.md](FIRE-RECIPE.md) - the Lead's procedure.
4. [BUILD-NOTES.md](BUILD-NOTES.md) - the build chain and pins.

## Reproduce (host only)

    opensource/docs/incoming/dfs160/tools/build-dfs160.sh      # pinned build + pins + selftest
    opensource/docs/incoming/dfs160/tools/run-host-tests.sh    # 133 checks, no kernel needed

The base tree is reproduced by `../wifi-5g-dfs-relax/tools/build-dfs-relax.sh`;
the .ko is local evidence only (`/tmp/dfs160-artifact`), never committed.

## Status / honesty

* **Host-side complete**: decode reuse [V], implementation [I] where noted,
  build and artefact pins [V], host selftest [V].
* **Not fired.** Every rung is the Lead's attended fire on a fresh boot.
* **[U] the 160 MHz selector value 3** ([I] from the HAL assemble-enum order):
  the fire's existing module-ctx readback prints `field=ctx+0x12` after the
  0x0124 post, so rung 160-1 *measures* the value (PROBES P-160a).
* **[U] the pre-VAP-start ARM gate**: cfg80211 calls `start_radar_detection`
  *before* `start_ap`, while the 0x07E7 worker's started-module gate
  (mctx[0x0E] in 1..3) is written by the AP ladder's 0x0065 VAP start.  The
  runtime therefore **fails closed with -EAGAIN and the live probe pack**
  instead of posting a frame the worker would drop; rung DFS-2 records the
  measured gate and the follow-up (if it refuses) is the vendor's own
  VAP-create ordering, not a fake event.
* No radar D2H decode exists yet ([U] in the decode lane), so the runtime
  never emits a radar event: the NOL model is ready but wired to nothing.
  No 80+80/320; no HE160-specific selector (VHT160 is the isolated rung).
* No vendor source/object/firmware byte, no calibration value, no secret in
  this lane - protocol constants and decoded addresses only.
