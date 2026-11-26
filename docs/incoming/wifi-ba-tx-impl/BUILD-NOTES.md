# BUILD-NOTES - lane wifi-ba-tx-impl (task-217)

**HOST-ONLY build.** No device access, no firmware write, no image, no flash.
The `.ko` is LOCAL EVIDENCE ONLY (/tmp, never committed - this repo stores no
binary artifacts).

## Chain

    /tmp/r5-build                                  (the F2/R5 merged-module base:
                                                    the R5 fire tree, 65 params,
                                                    module 24f7221b / 383,240 B)
      + patches/0001-ba-tx-impl.patch              (this lane: 4 new files +
                                                    6 integration edits)
      = /tmp/ba-tx-build -> /tmp/batx-artifact/hi5622_wifi.ko

The base tree is reproduced by `../wifi-band1-start-send-5/tools/build-r5-order.sh`
(the R4b tree -> gate inversion -> R5 order); the build script refuses a
drifted base by content pin.

## Pins (enforced by tools/build-ba-tx.sh)

| pin | value |
| --- | --- |
| base `Kbuild` | `1bb51085...b1fc141d` |
| base `main.c` / `hcc.c` | `605545ac...` / `7481791d...` |
| base `hi5622_mgmt.c` / `hi5622_data_tx.c` | `7491cf32...` / `2bc0aa82...` |
| base `hi5622_scan.c` (the R5 pinned file) | `86d6c57e...` |
| base `hi5622_vap_start_g0.h` / `hi5622_scan_logic.c` / `.h` | `81533d9c...` / `15c50d34...` / `6af774ef...` |
| base `cfg80211-provider.symvers` | `bd18897a...` (12 lines) |
| lane patch | `patches/0001-ba-tx-impl.patch`, 2,258 lines, sha256 `0964fbf7...` |
| patched `Kbuild` / `main.c` / `hcc.c` | `c50e86f0...` / `37f14be9...` / `07fa2a50...` |
| patched `hi5622_mgmt.c` / `hi5622_data_tx.c` / `hi5622_scan.c` | `2d6d791d...` / `2babf88e...` / `0fc1d58a...` |
| patched `hi5622_ba.h/.c` / `hi5622_ba_glue.h/.c` | `ed6d472c...` / `d865a3e4...` / `3573b7de...` / `f08c7952...` |
| artifact | **/tmp/batx-artifact/hi5622_wifi.ko, 406,168 B**, sha256 **`234b3d4b961037e1a5465cc4cc38ab4603b2c71c451d06ac5ba7212b0104d841`** |
| unstripped | 1,515,740 B, sha256 `658a5887a17cbf69e12824bedff40222f9f6909eeda707f9bd13f715afeb8ddf` |
| `.text` | sha256 `702591233a117f0ccf7db2c6ce75ea0ee81c1e4307c10769bda4b7e2d20ec8fc` |
| params | **66** (65 R5 + `ba_enable`, read-only 0444, default 0) |
| vermagic / depends / license | `6.18.60 SMP mod_unload ARMv7 p2v8` / `cfg80211` / GPL |

The whole-file artifact sha is reproducible at the default WORK/ART paths
(`/tmp/ba-tx-build`, `/tmp/batx-artifact`). An out-of-tree build in a
different WORK path changes 20 bytes of out-of-tree build metadata outside
`.text` (the project's documented build-id caveat); then the script keeps the
`.text`/size/params pins and reports the non-default path. The full list of
base/patched shas is in `evidence/artifact-pins.txt`.

## Contract checks implemented by the build script

1. the base tree exists and equals the nine pinned content shas (no drift);
2. the lane patch equals its pin and applies with no fuzz, leaving no
   `.rej`/`.orig`; the patched tree equals the ten post-apply pins;
3. module build 0 warnings / 0 errors / no undefined symbols;
4. the stripped artifact is **66 params** including `ba_enable: (uint)`, the
   size and (default-path) whole-file sha match, `.text` sha matches;
5. the feature strings are present (the armed/off/invalid messages, the
   `ba_relay`/`ba_ampdu`/`ba_notify`/`ba_mgmt_tx` post labels, the AP up/down
   lines, the status line);
6. the glue entry points are linked (`hi5622_ba_glue_{d2h,mgmt_rx,tx_data,
   ap_up,status_line}`);
7. the host tests pass twice: the C model standalone (211 checks) and against
   the patched tree's `h2d_frames.c` (225 checks - the driver's own H2D
   decode gate), plus the independent Python contract test (99 checks).

## Host tests

    ./tools/run-host-tests.sh /tmp/ba-tx-build     # C model, both parts
    python3 tools/ba_tx_frame_test.py /tmp/ba-tx-build
    ./tools/build-ba-tx.sh                         # full chain + build + tests

Result: **335 checks, 0 failures** - C 211 standalone + 225 with the driver
H2D decode gate (the 225 include the 211: the same binary with
`-DHAVE_H2D_FRAMES`), Python 99. Transcripts: `evidence/host-tests.txt`,
`evidence/frame-contract-test.txt`, `evidence/build.txt`.

The C model compiles the REAL portable core (`src/hi5622_ba.c`) and, in the
second part, the patched tree's `h2d_frames.c`; it asserts every wire byte
(complete HCC header included), the action frames, the parser rejects, the
session state machine, the negotiation sequence (idle -> pending -> active ->
teared down), the in-kernel originator step, the burst bound, the error
rollbacks and the fail-closed matrix.

The Python test re-derives the 44/20/32-B wires from the decoded field maps
(a second implementation, not the C code), checks the driver's H2D catalogue
advertises a handler for every (group,id) the glue posts, and checks the six
integration hooks plus the gate contract in the patched source.

## Reproduce

    ./tools/build-ba-tx.sh

KDIR/STAGING come from /home/shibbityshwab/openwrt-lab-upstream-target (else
...-selectorfix-img); override with TREE_SRC / WORK / ART / OUT in the
environment. The base tree is /tmp/r5-build (the R5 fire line).

## Applying this patch on another base

The patch touches `Kbuild`, `main.c`, `hcc.c`, `hi5622_mgmt.c`,
`hi5622_data_tx.c`, `hi5622_scan.c` and adds `hi5622_ba.[ch]` +
`hi5622_ba_glue.[ch]`. On an image-#3 merged module (F2 + T4 + HE + BA
skeleton) those regions have drifted (e.g. the Kbuild object list and the
data-TX/mgmt files), and the BA skeleton's `hi5622_ba.[ch]` would collide
with this lane's files. The deliverable is the pinned F2/R5 base the Lead
asked for; any other rebase/build is not part of this lane. The reviewable
exact-match integration edit list is `tools/apply-integration.py`
(`--check` validates it against a pristine tree; the committed patch is its
output).
