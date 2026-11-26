# dfs160b - the 5 GHz 160 MHz width selector + the DFS/CAC/NOL feature

**Lane:** shared task **task-245** (owner `dfs160b`), 2026-11-26.
**HOST-ONLY:** static decode + portable model + clean-room driver patch +
reproducible host build; no device contact, no UART, no fire, no flash.  The
Lead fires with [FIRE-RECIPE.md](FIRE-RECIPE.md).
**Commits:** only inside `opensource/`.  **Never committed:** vendor
binaries, firmware bytes, module artifacts, calibration values, MACs or
secrets (decoded protocol bytes as quoted text only).

This lane is the **independent** implementation of the feature the committed
decode lanes prepared; the protocol facts come ONLY from those lanes:

| source lane | what it pins |
| --- | --- |
| [../wifi-5g-160-dfs/](../wifi-5g-160-dfs/) (task-205) | the 0x0124 body[1] width selector, the DFS set incl. the ch144 gap, the CAC/NOL table, the host-implementation spec |
| [../wifi-5g-dfs-set/](../wifi-5g-dfs-set/) (task-227) | the H2D g0-id1 key 0x07E7 ARM/CLEAR 29-B carrier, the six band-1 SMAC sub-base writes, the 0x0065/0x0242 slot |
| [../wifi-5g-dfs-order/](../wifi-5g-dfs-order/) (task-228) | the forced order `0x0065 -> 0x07E7 -> 0x01F7 -> 0x0242` |
| [../wifi-5g-dfs-relax/](../wifi-5g-dfs-relax/) (task-230) | the +0x4A0 relax (register-only vs full ARM) and the A-rung base lineage |
| [../wifi-5g-cac-context/](../wifi-5g-cac-context/) (task-229) | the +0x49C/+0x4A0/+0x4A4 CAC context writer and the ARM gate |

## The feature (all four deliverables delivered)

1. **The 160 MHz width selector.**  The H2D g0-id1 channel config key
   `0x0124` carries `{band, bandwidth, channel, 0}`; byte[1] is the vendor
   bandwidth selector (0/1/2/3 = 20/40/80/160 MHz), NOT a VAP index.  The
   lane maps the cfg80211 chandef width onto it (fail closed for 80+80 and
   320 MHz - the vendor wiphy VHT cap `0x00400064` has neither bit),
   validates the single-segment 160 MHz CCFS1, keeps the legacy
   byte-identical default (`ht_bw=0`), and pins the Linux **6.18** UAPI
   width values with `_Static_assert` in the kernel glue.
2. **The DFS key 0x07E7 / CAC / NOL.**  The 29-B ARM `{01}` / CLEAR `{00}`
   frames, the vendor CAC tick arithmetic (60000 ms class, 600000 ms TDWR
   class, first tick 63000 ms then 60000 ms), CAC 60000/600000 ms, the
   1800000 ms NOL, the cfg80211 `start_radar_detection`/`end_cac` ops, the
   live fail-closed precheck, and the start_ap DFS gate.
3. **The driver integration.**  [patches/0001-dfs160b.patch](patches/0001-dfs160b.patch)
   (14 files, 4 new) on the fired dfs-relax A-rung lineage;
   [tools/build-dfs160b.sh](tools/build-dfs160b.sh) reproduces the installed
   module **383,020 B sha256 `006a94cf...`**, 68 params, .text 164,980 B
   sha256 `5e14008f...`, 0 warnings/errors.
4. **The fire recipe.**  [FIRE-RECIPE.md](FIRE-RECIPE.md): the 160 MHz rungs,
   the cold/warm CAC rungs, the TDWR rung, the negative controls, the
   read-only probes and the gated on-air expectations.

## Deliverables

| path | content |
| --- | --- |
| [README.md](README.md) | this file |
| [REPORT.md](REPORT.md) | the full report: implementation, the cfg80211 contract, the honest limit, the build/test results, the cross-lane finding, limits |
| [TABLES.md](TABLES.md) | every pin/constant/frame/channel/timing/probe in one place |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the Lead's gated procedure (R0 gates, R1 160 MHz, R2 CAC, R3 negatives, PASS/FAIL/ABORT) |
| [BUILD-NOTES.md](BUILD-NOTES.md) | the base/source/artifact pins and the reproduce steps |
| [src/hi5622_dfsb.h](src/hi5622_dfsb.h), [src/hi5622_dfsb.c](src/hi5622_dfsb.c) | the portable model (pure C99, kernel + host) |
| [src/hi5622_dfsb_k.h](src/hi5622_dfsb_k.h), [src/hi5622_dfsb_k.c](src/hi5622_dfsb_k.c) | the kernel glue (params, CAC runtime, cfg80211 ops) |
| [src/dfs160b_selftest.c](src/dfs160b_selftest.c) | 269 host checks (0 failures) |
| [tools/run-host-tests.sh](tools/run-host-tests.sh) | the C host selftest runner |
| [tools/dfs160b_patch_test.py](tools/dfs160b_patch_test.py) | independent integration/order test, 108 checks (0 failures) |
| [tools/build-dfs160b.sh](tools/build-dfs160b.sh) | the reproducible host build |
| [patches/0001-dfs160b.patch](patches/0001-dfs160b.patch) | the integration patch (git-apply verified) |
| [hostapd/hostapd-160.conf](hostapd/hostapd-160.conf), [hostapd/hostapd-dfs.conf](hostapd/hostapd-dfs.conf) | the bench fixtures |
| [evidence/HOST-TESTS.txt](evidence/HOST-TESTS.txt), [evidence/BUILD.txt](evidence/BUILD.txt), [evidence/PATCH-TESTS.txt](evidence/PATCH-TESTS.txt) | captured derived results |

## Status (2026-11-26)

| deliverable | state |
| --- | --- |
| portable model + host selftest | **DONE** - 269 checks / 0 failures |
| driver integration patch + reproducible build | **DONE** - 383,020 B `006a94cf...`, 68 params, .text `5e14008f...`, 0 warnings/0 errors |
| fire recipe + report/tables | **DONE** - FIRE-RECIPE/REPORT/TABLES/BUILD-NOTES |
| bench fire | **NOT FIRED** - host-only lane; the Lead fires |

## The cross-lane verification finding (task-240)

The sibling `../dfs160/` lane pins the **pre-5.19** nl80211 width numbering
(160=4 / 80P80=5 / 320=6) while the 6.18.60 target uses 80P80=4 / **160=5** /
320=13, and its patch feeds the real kernel enum into that model: a real
160 MHz chandef (value 5) is refused, a real 80+80 (value 4) is classified as
160.  Probe of its committed files: width 5 -> `-EOPNOTSUPP`, width 4 ->
selector 3.  **Its `ht_bw=1` rung is invalid as committed** - this lane's
width pins are re-derived from the kernel header by the Python test and
enforced at build time.

## The honest limit

The firmware 0x07E7 ARM worker needs the started module context that the
0x0065 VAP-start step writes, while hostapd asks for CAC **before** start_ap.
The op therefore fails closed with the live probe pack on a cold interface
(the cold rung measures it) and the sanctioned path is the **warm CAC rung**
(one 5 GHz AP start, host-side stop, then the DFS start).  The 5 GHz **on-air**
PASS stays gated on the firmware band-1 beacon TX (tx-scheduler lane); the
host+firmware-state rungs stand alone.  No radar event is faked (the D2H radar
id is unmapped); the NOL is the host-side 30 min window after a finished CAC.

## Read in this order

1. [FIRE-RECIPE.md](FIRE-RECIPE.md) - what the Lead fires, in order.
2. [TABLES.md](TABLES.md) - the pins/converters/probe expectations.
3. [BUILD-NOTES.md](BUILD-NOTES.md) - reproduce the artifact.
4. [REPORT.md](REPORT.md) - the full reasoning, the honest limit, the
   cross-lane finding.
