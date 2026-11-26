# BUILD-NOTES - lane wifi-band1-start-send-4 (task-211)

HOST-ONLY build.  No device access, no firmware write, no image, no flash.
The .ko is LOCAL EVIDENCE ONLY (/tmp, never committed - this repo stores no
binary artifacts).

## Chain

    ../wifi-band1-start-send/patches/0001-band-start-send.patch on the F2 tree
      = /tmp/bandstart-build + /tmp/bandstart-artifact/hi5622_wifi.ko
        371,212 B, sha256 e2312402...a028, 63 params (fired)
    ../wifi-band1-start-send-2/patches/0001-vap-add-index.patch
      = the send-2 tree, 373,056 B sha b8f6fe35...5b71, 64 params
    ../wifi-band1-start-send-3/patches/0001-vap-start-g0.patch
      = the send-3 tree, 382,072 B sha bb0434c2...c9711, 65 params
        (the R4 module that refused at its own G8 gate)
    this lane's patches/0001-r4b-order.patch
      = the R4b order (0x0124 before 0x0065/0x0242), no new file

## Pins (checked by tools/build-r4b-order.sh)

| pin | value |
| --- | --- |
| fired 0x0242 base files | hi5622_scan.c '272e47ac...', hi5622_vapscan_provider.c 'c306e75f...', vapstart_frames.c '7dc7bc35...', vapstart_frames.h '0f1d13c9...', hi5622_band_start.c '71019718...', hi5622_band_start.h 'dac835ae...' |
| fired 0x0242 artifact | 371,212 B, sha256 e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028, 63 params |
| send-2 patch | sha256 968d70c5c4f13005e6ddee7f1bed2423888c8a58ed7582a7806b8ea031b813fb |
| send-2 tree result | hi5622_scan.c '52a5bc0b...', hi5622_vapscan_provider.c 'b66da889...', hi5622_scan_logic.c '562ff684...', hi5622_scan_logic.h '98c4025f...', Kbuild 'fa58f92c...' |
| send-2 artifact | 373,056 B, sha256 b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71, 64 params |
| send-3 patch | sha256 0a8a1c1bda3ebaaf05933b670d996a2962f2679ecf0075237bf6d4adef30fc4c |
| send-3 tree result | hi5622_scan.c 'da488e3c...', hi5622_scan_logic.c '7e5edbc4...', hi5622_scan_logic.h 'fc4e43d1...', hi5622_band_start.h '3fd2c608...', hi5622_vap_start_g0.c 'ece5c0d6...', hi5622_vap_start_g0.h '68c2eb79...', Kbuild '1bb51085...' |
| send-3 artifact | 382,072 B, sha256 bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711, 65 params |
| lane patch | sha256 3628899843628984b1373f9c1e2ccd6539aff8920ac3188401a24a83b1c21775, 34,330 B |
| patched tree result | hi5622_scan.c 'ab14fa07...', hi5622_scan_logic.c 'ab7b8bac...', hi5622_scan_logic.h '3bfe1239...', hi5622_band_start.h '443b071d...', hi5622_vap_start_g0.h 'fe165a82...' (hi5622_vap_start_g0.c + Kbuild unchanged from send-3) |
| artifact | **382,188 B**, sha256 **b9926c1c4201cfe698c9fb6828a0d4e6a4fc45ba620bb49d745859ef7fda1102**, 65 params |

The script refuses a drifted base, a drifted send-2/send-3 tree, a drifted
patch, a tree result that differs from the pins, conflict markers, build
warnings/errors/undefined symbols, a debug-strip that changes .text, and an
artifact-contract mismatch.  If the /tmp chain is absent it rebuilds it from
the three sibling build scripts (the pinned chain reproduced exactly on
2026-11-28: send-3 artifact bb0434c2... and send-2 e2312402... both came
back byte-identical - evidence/base-chain-rebuild.txt).

## Artifact (host build, not fired, never committed)

| field | value |
| --- | --- |
| stripped | /tmp/r4border-artifact/hi5622_wifi.ko, **382,188 B** |
| sha256 | b9926c1c4201cfe698c9fb6828a0d4e6a4fc45ba620bb49d745859ef7fda1102 |
| unstripped | 1,423,196 B, sha256 a9d5d675b68f441b1a28c291a034aa025d71e20a3ad4d6150cd7d6df3516b0ed |
| .text sha256 | f2848fdc3db7c0913a208aefd7fc85230817cc143158d9f50ef3529e59cbf6ff |
| params | **65** (unchanged from send-3: no new knob) |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| warnings | 0; debug-strip keeps .text byte-identical |
| cfg80211 symvers | 12 lines |
| size delta | +116 B vs the send-3 382,072 B artifact (code moved + strings) |

vap_start_g0 stays a read-only (0444) uint parameter, default **0**; its
description now says "after the 0x0124 channel post and before the 0x0242
band start" (the 6.18 loader drops MODULE_PARM_DESC strings from .modinfo,
so the description is checked in the patched source).

## Contract checks implemented by the build script

1. the fired 0x0242 base files equal the pinned shas;
2. the send-2 patch and the send-3 patch equal their pins and apply with no
   fuzz; both intermediate trees equal their pinned shas; both intermediate
   artifacts (when present) equal their pinned size/sha/params;
3. this lane's patch equals its pin, applies with no fuzz, and the resulting
   tree equals the pinned R4b shas; no conflict markers survive;
4. module build 0 warnings/errors/undefined symbols; debug-strip does not
   change .text;
5. artifact contract: **65 params** (vap_start_g0 still present, send-2
   vap_add_index still carried) + the R4b placement strings ('after the
   0x0124 channel post (mctx[0x10..0x13] stored) and before the 0x0242 band
   start'; the 0x0242 post line naming both R4b predecessors; the reordered
   parameter description) + the G5/G12/G8 strings + 12 cfg80211 symvers;
6. the plan model emits exactly 'VAP_ADD DESC_TABLE DFS_MODE CHAN
   VAP_START_G0 BAND_START SSID' (the R4b order).

## Host tests

    ./tools/run-host-tests.sh

Compiles the patched tree's REAL models (hi5622_vap_start_g0.c,
hi5622_scan_logic.c) and the REAL sibling builders
(../wifi-vap-start/src/vapstart_frames.c for key 0x0065,
../wifi-band1-start-send/src/hi5622_band_start.c for key 0x0242) with a plain
C99 host compiler, then runs the independent Python re-derivation + source
contract.

Result: **386 checks, 0 failures** (evidence/host-tests.txt):

| part | checks | result |
| --- | ---: | --- |
| src/vap_start_g0_selftest.c | 293 | 0 failed |
| tools/r4b_order_frame_test.py | 93 | 0 failed |

The selftest asserts the 40-B frame KATs (field B 0 and 4, every module
index, a byte-for-byte cross-check against the sibling 0x0065 builder), the
R4b plan order 9/8/4 steps, the enum order ap_chan < vap_start_g0 <
band_start, the G8 sequencing model, all G1..G14 gates, G12 and the
fail-closed matrix.  No device contacted.

## Reproduce

    ./tools/build-r4b-order.sh
    ./tools/run-host-tests.sh

KDIR/STAGING come from /home/shibbityshwab/openwrt-lab-upstream-target (else
...-selectorfix-img); override with KDIR_OVERRIDE / STAGING_OVERRIDE.  The
fired base tree can be pointed at with BASE_TREE (default
/tmp/bandstart-build); the R4b build is /tmp/r4border-build.

## Applying this patch on another base

The patch touches hi5622_scan.c, hi5622_scan_logic.c/.h,
hi5622_vap_start_g0.h and hi5622_band_start.h (comments + order only in the
last two).  On an image-#3 merged module those regions may have drifted; the
deliverable is the pinned send-3 base the Lead asked for, and any other
rebase/build is not part of this lane.
