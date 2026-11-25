# BUILD-NOTES - lane wifi-band1-start-send (task-206)

HOST-ONLY build.  No device access, no firmware write, no image, no flash.
The `.ko` is LOCAL EVIDENCE ONLY (/tmp, never committed - this repo stores no
binary artifacts).

## Chain

    ../wifi-5ghz-beacon/tools/build-beacon5g-ready.sh        (356,600 B, 59 params)
    ../wifi-r1-band-diff/tools/build-r1-band.sh              (356,612 B e8b17f72..., 59)
    ../wifi-desc-table-send/patches/0001-desc-table-send.patch   (60 params, 0x00F7)
    ../wifi-dfs-mode-send/patches/0001-dfs-mode-send.patch       (61 params, 0x01F7)
    ../wifi-fw-dfs-worker/patches/0001-fieldb-context.patch     (the F2 fire module:
        364,184 B sha 12427b86...9468, 61 params, fired 2026-11-27 23:30 -
        the 5 GHz RF was PROGRAMMED)
    this lane's patches/0001-band-start-send.patch
        = the H2D 0x0242 band-1 MAC-start send + the optional 0x0242-forced /
          0x0243 fallback rungs + the fail-closed band_start /
          band_start_fallback parameters (63 params)

The first two scripts are invoked by ../wifi-dfs-mode-send/tools/build-dfs-mode-send.sh
when the base tree is absent; tools/build-band-start-send.sh then applies the
F2 field-B patch and this lane's patch on top of that tree.  When the F2 tree
(/tmp/dfsmode-build) is present it is used as-is after the pin checks.

## Pins (checked by tools/build-band-start-send.sh)

| pin | value |
| --- | --- |
| F2 base tree scan/logic/Kbuild shas | hi5622_scan.c `8fbc8e1e...`, hi5622_scan_logic.c `75b9bdd5...`, hi5622_scan_logic.h `b83e74cc...`, Kbuild `900fb204...` |
| F2 base tree field-B files | hi5622_dfs_mode.h `cd3b76aa...` (FB = 0x00), hi5622_desc_table.c `5af935f2...` (b[3] = 0x00) |
| F2 artifact (when present) | /tmp/f2-module.ko, 364,184 B, sha256 `12427b8675b9151598d66c0b75410752bcc142178908542ff7d82aa54f059468`, 61 params |
| F2 field-B patch | ../wifi-fw-dfs-worker/patches/0001-fieldb-context.patch sha256 `0530ca45528e0601af5a6f8b8523e3ed8e43cd2237c2361f6cfd03aa3504f926` |
| lane patch | patches/0001-band-start-send.patch sha256 `3ddda8524b782063837fd23a4b2fab1c666f4826b8fa76da394275e6a9de308b` |
| lane portable sources | src/hi5622_band_start.c `71019718...`, src/hi5622_band_start.h `dac835ae...` |

## Artifact (host build, not fired, never committed)

| field | value |
| --- | --- |
| stripped artifact | /tmp/bandstart-artifact/hi5622_wifi.ko, **371,212 B** |
| sha256 | `e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028` |
| unstripped | 1,399,948 B sha256 `50f105f406474131a244f217ee539cae3794f58bced2f8346e5f5c4fa86a59d7` |
| .text sha256 | `884f0a35da01194598decd72fa81921188a6d32e00b0a4a3b3cd6078c2aea275` |
| params | **63** (F2 61 + `band_start` + `band_start_fallback`) |
| vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| warnings | 0; debug-strip keeps .text byte-identical |
| cfg80211 symvers | 12 lines (unchanged) |
| size delta | +7,028 B vs the 364,184 B F2 artifact |
| key immediates | `objdump -d hi5622_band_start.o`: `movw r3, #578 @ 0x242` (sync/forced start) and `movw r3, #579 @ 0x243` (nudge) |

`band_start` and `band_start_fallback` are read-only (0444) uint parameters,
default **0**; the built module's .modinfo carries `parmtype=band_start:uint`
and `parmtype=band_start_fallback:uint` (the 6.18 loader drops
MODULE_PARM_DESC strings, so the description is checked in the patched source).

## Contract checks implemented by the build script

1. the F2 base tree files equal the pinned shas (refuse a drifted base);
2. the F2 field-B fix is present (FB 0x00 in hi5622_dfs_mode.h and
   b[3] = 0x00 in hi5622_desc_table.c) - the base is the F2 lineage;
3. the F2 artifact (when present) equals 364,184 B / `12427b86...` / 61
   params;
4. the F2 field-B patch, this lane's patch and its two portable sources equal
   the pins;
5. the lane patch applies to the F2 base with no fuzz/conflict markers;
6. `src/hi5622_band_start.{c,h}` are byte-identical to the patched driver
   sources (source-drift guard);
7. module build 0 warnings/errors/undefined symbols;
8. debug-strip does not change the .text;
9. artifact contract: 63 params incl `band_start` + `band_start_fallback`,
   both declared as read-only uint with MODULE_PARM_DESC in the patched
   source, the 0x0242/0x0243 feature strings, the two key immediates
   (`movw #578` / `movw #579`) in the built unit, 12 cfg80211 symvers.

## Host tests

    ./tools/run-host-tests.sh

Compiles the portable model (src/) and the real patched ordering model
(hi5622_scan_logic.c) + the real frame validator (h2d_frames.c) from the tree
with a plain C99 host compiler and runs `band_start_selftest`,
`order_test.c`, `validate_frame_test.c` and `band_start_send_test.py`
(independent frame re-derivation + source contract checks).

Result: **216 checks, 0 failures** (evidence/host-tests.txt):

| part | checks | result |
| --- | ---: | --- |
| 1. portable model (src/band_start_selftest.c) | 83 | 0 failed |
| 2a. real frame_post validator (tools/validate_frame_test.c) | 21 | 0 failed |
| 2b. ordering contract (tools/order_test.c) | 15 | 0 failed |
| 2c. independent cross-check (tools/band_start_send_test.py) | 97 | 0 failed |
| 3. artifact contract (when built) | PASS | 371,212 B, sha `e2312402...`, 63 params |

No device contacted.

## Reproduce

    ./tools/build-band-start-send.sh
    ./tools/run-host-tests.sh

KDIR/STAGING come from /home/shibbityshwab/openwrt-lab-upstream-target (else
...-selectorfix-img); override with KDIR_OVERRIDE / STAGING_OVERRIDE.  The F2
tree can be pointed at with BASE_TREE=/tmp/dfsmode-build (default).

## Applying this patch on another base

The patch touches only `hi5622_scan.c`, `hi5622_scan_logic.{c,h}`, `Kbuild`
and adds `hi5622_band_start.{c,h}`.  On a tree that also carries the he_caps /
ba / rx-watermark branches (the image #3 merged module) those regions are
unchanged, so the patch should apply and rebuild; that rebase/build is **not**
part of this lane (the deliverable is the pinned F2 base the Lead asked for).
