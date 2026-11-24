# BUILD-NOTES - wifi-scan-full refined builder (task-138)

**Lane:** wifi-scan-full, host-only.  **Date:** 2026-11-24.
**Diff base:** wifi-scan-tuning R4 tree (`patches/0002-scan-tuning-on-polish.patch`
applied; the two builder files are identical in the R4 and hardened trees).
This lane changes **two files** (`hi5622_band_cfg.h`, `hi5622_band_cfg.c`) and
adds its own host selftest/tools.  No other module source file is touched, so
the patch does not conflict with the polish/scan/dp/hardening stacks.
Built modules are local evidence only and are NOT committed.

## Patch stack

1. base = bench-validated combined AP+DP tree (fired module 242,572 B sha `44c6cc12...`);
2. image-polish module patch (scan_dwell_ms default 60);
3. `wifi-scan-tuning/patches/0002-scan-tuning-on-polish.patch` (R4: H1 builder,
   repost cap, per-batch BSS readback, len-prefix BSS decode, chained scan);
4. **this lane:** `patches/0001-scan-full-vendor-shape-bandcfg.patch`;
5. hardening patch (wifi-stability) - order-independent with 4 (different hunks).

The patch was dry-run-applied to a clean R4 tree copy (`patch -p1` OK).

## Build line

    BASE_TREE=/tmp/wifi-stability-patchcheck \
    ./opensource/docs/incoming/wifi-scan-full/tools/build.sh

Canonical environment (project tree):

    KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
    CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

## Result (canonical host-only run, 2026-11-24)

| item | value |
| --- | --- |
| host selftest `scan_full_selftest` | **90/90 PASS** |
| static evidence `fw_scan_full_re.py` | **65/65 PASS** (firmware sha pinned) |
| inherited scan-tuning selftest | **37/37 PASS** |
| inherited scan-complete selftest | **196/196 PASS** |
| module (with debug info) | 979,512 B, sha256 `5a00e7adc1eee265...` |
| module (stripped) | 263,704 B, sha256 `4dd008d8aecf9261...` |
| ELF / vermagic | 32-bit LSB relocatable, ARM, EABI5 / `6.18.60 SMP mod_unload ARMv7 p2v8` |
| depends / cfg80211 imports | cfg80211 / 10 |
| compiler/modpost warnings | **0** (with `STAGING_DIR` exported) |

Sizes/shas are build-path dependent (debug info records the build dir); the
stripped figure is the deployable one.  The two-file patch adds no new
external symbol and no new cfg80211 import.

## Behaviour changes relative to R4 (important for ladder / review)

1. **Wire length 36 -> 44 B** (vendor shape: 12-byte HCC header + 32-byte
   payload).  `hi5622_scan.c` sizes its stack buffer with
   `HI5622_BAND_CFG_TOTAL`, so the recompiled tree is required (no ABI change).
   H1 dmesg lines now print 44 B.
2. **Subtype-2 flag moved to payload+0x14** (firmware read site); payload+0x08
   is no longer written.  `bp.sub2_flag = 0` (the R4 caller value) still
   means "clear slot" and is unchanged on the wire.
3. **Field labels/limits corrected** in the header; validation remains
   fail-closed (slot > 7, unknown subtype, short buffer -> -EINVAL; `len` is
   optional).  The compiled default of `scan_band_cfg` is still 0.
4. **R4's `wifi-scan-tuning/tools/scan_band_cfg_selftest.c` is superseded**:
   it asserts the old 36-byte shape.  Do not run it against the patched
   builder; `tools/scan_full_selftest.c` is its replacement (same API, new
   wire shape and field map).  This is a doc/handoff note, not a changed file
   outside this lane's write scope.
