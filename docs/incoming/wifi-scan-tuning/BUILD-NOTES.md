# BUILD-NOTES - wifi-scan-tuning module (task-133)

**Lane:** wifi-scan-tuning, host-only. **Date:** 2026-11-27.
**Diff base:** the bench-validated **combined AP+DP** source tree (ap-start v7 +
wifi-dp-air merge + scan-complete; the tree that produced the fired module
242,572 B sha256 `44c6cc12...`).  This lane changes four files
(`hi5622_scan.c`, `hi5622_scan.h`, `hi5622_scan_logic.c`,
`hi5622_scan_logic.h`) and one host selftest; nothing else.  The built `.ko` is
local evidence only and is NOT committed (no binary, vendor object, firmware
byte or calibration value is involved).

## Build line

    export BASE_TREE=/tmp/wifi-combined-build   # local copy of the fired tree
    ./tools/build.sh

Canonical environment (project tree):

    KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
    CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

## Result (canonical run 2026-11-27, host-only)

The recording session executed every step of `tools/build.sh` individually
(host selftests, kernel module build, strip, metadata, request-dump compile)
and validated the patch round-trip; the wrapper was not invoked in one shot
because the host was under heavy parallel load.  `tools/build.sh` encodes the
same command lines and is the intended one-command re-run.

| item | value |
| --- | --- |
| module (with debug info) | 935,032 B, sha256 `aa9a6ce7da699e4da1d1010e605ceb524b3fdb52a33cfb3b8170e276d2d8c5ad` |
| module (stripped for deployment) | **246,688 B**, sha256 `05dd5f6ac83daf45df37e1e6761dde93c6f7ce9d7d4c03c81adf67f369e5244a` |
| ELF | 32-bit LSB relocatable, ARM, EABI5 |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| depends | cfg80211 |
| cfg80211 imports (U) | 9 (unchanged) |
| compiler/modpost warnings | 0 (`-Werror` tree) |
| host scan-tuning selftest | **37/37 PASS** (new) |
| host scan-complete selftest | **196/196 PASS** (inherited, window cadence updated) |
| host inherited scan/provider selftest | 290/290 PASS |
| host inherited cfg80211 regression | 454/454 PASS |
| host inherited H2D regression | 242/242 PASS |
| previous fired module (combined) | 242,572 B stripped sha256 `44c6cc12...` (base only; no kernel config change) |

Size/sha are build-path dependent (debug info records the build dir); the
stripped number above is the deployable figure.  The new code adds no external
symbol, no new `depends` and no new cfg80211 import.

## Module parameters (new in this lane)

| param | default | meaning |
| --- | ---: | --- |
| `scan_chan_time_ms` | 0 | per-channel cost for the auto window; 0 = compiled 250 ms (measured ~156 ms + margin); valid 50..2000, out-of-range ignored |
| `scan_req_batch` | **6** | channels per g0 id0xC request. 0 or >= n = one request (legacy); 1 = one request per channel; 6 = the measured firmware per-request completion |
| `scan_batch_gap_ms` | 300 | pause between chained requests (firmware scan-in-progress clear + late BSS flush), 0..5000 |

Changed semantics of existing parameters:

| param | change |
| --- | --- |
| `scan_timeout_ms` | still 0 = auto, but the auto term is per **request/batch** now: `clamp(1500 + n*scan_chan_time_ms, 1500, 120000)`; manual values are clamped to [1000, 120000] (the 45 s clamp is raised to 120 s) |
| `scan_dwell_ms` | unchanged (0 = vendor 60, 20..200 sweep) |

## Tree quirks

Inherited, unchanged: cfg80211-provider.symvers for the 9 modpost symbols,
heap-allocated h2d_frame / vs_frame, shared KDIR must be idle during a world
build.  New: the lane `src/` is a **four-file delta**, not a full tree - build.sh
copies `BASE_TREE` and overlays it, which is also how the patch was made.
