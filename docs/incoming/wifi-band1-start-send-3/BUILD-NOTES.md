# BUILD-NOTES - lane wifi-band1-start-send-3 (task-209)

HOST-ONLY build.  No device access, no firmware write, no image, no flash.
The .ko is LOCAL EVIDENCE ONLY (/tmp, never committed - this repo stores no
binary artifacts).

## Chain

    ../wifi-band1-start-send/patches/0001-band-start-send.patch on the F2 tree
      = /tmp/bandstart-build + /tmp/bandstart-artifact/hi5622_wifi.ko
        371,212 B, sha256 e2312402...a028, 63 params (fired 2026-11-28 05:00)
    ../wifi-band1-start-send-2/patches/0001-vap-add-index.patch
      = the send-2 tree (/tmp/vapbound-build)
        373,056 B, sha256 b8f6fe35...5b71, 64 params
    this lane's patches/0001-vap-start-g0.patch
      = the H2D g0-id1 key-0x0065 VAP-start step + the G1..G14 gates
        65 params

## Pins (checked by tools/build-vap-start-g0.sh)

| pin | value |
| --- | --- |
| fired 0x0242 base files | hi5622_scan.c '272e47ac...', hi5622_vapscan_provider.c 'c306e75f...', vapstart_frames.c '7dc7bc35...', vapstart_frames.h '0f1d13c9...', hi5622_band_start.c '71019718...', hi5622_band_start.h 'dac835ae...' |
| fired 0x0242 artifact | 371,212 B, sha256 e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028, 63 params |
| send-2 patch | sha256 968d70c5c4f13005e6ddee7f1bed2423888c8a58ed7582a7806b8ea031b813fb |
| send-2 tree result | hi5622_scan.c '52a5bc0b...', hi5622_vapscan_provider.c 'b66da889...', hi5622_scan_logic.c '562ff684...', hi5622_scan_logic.h '98c4025f...', Kbuild 'fa58f92c...' |
| send-2 artifact | 373,056 B, sha256 b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71, 64 params |
| lane patch | sha256 0a8a1c1bda3ebaaf05933b670d996a2962f2679ecf0075237bf6d4adef30fc4c |
| patched tree result | hi5622_scan.c 'da488e3c...', hi5622_scan_logic.c '7e5edbc4...', hi5622_scan_logic.h 'fc4e43d1...', hi5622_band_start.h '3fd2c608...', hi5622_vap_start_g0.c 'ece5c0d6...', hi5622_vap_start_g0.h '68c2eb79...', Kbuild '1bb51085...' (full list: evidence/patched-tree-sha.txt) |

The script refuses a drifted base, a drifted send-2 tree, a drifted patch, a
tree result that differs from the pins, conflict markers, build
warnings/errors/undefined symbols, a debug-strip that changes .text, and an
artifact-contract mismatch.

## Artifact (host build, not fired, never committed)

| field | value |
| --- | --- |
| stripped | /tmp/g0start-artifact/hi5622_wifi.ko, **382,072 B** |
| sha256 | bb0434c24b333f889daf6b062b6e771cb5b045e28ad2ef9c9c2c05f2a78c9711 |
| unstripped | 1,422,988 B, sha256 05ca89cc9e348cf3f99270d4c5c94b2dc9e3a09e3ebbbf3cfa290e84b6ea0cc4 |
| .text sha256 | 3d3b45ffe483a7740c93aa6458a02461bf1226578371350d55df85ea8b972cc3 |
| params | **65** (64 send-2 + vap_start_g0) |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| warnings | 0; debug-strip keeps .text byte-identical |
| cfg80211 symvers | 12 lines |
| size delta | +9,016 B vs the send-2 373,056 B artifact |

vap_start_g0 is a read-only (0444) uint parameter, default **0**; the built
module's .modinfo carries 'parmtype=vap_start_g0:uint' (the 6.18 loader drops
MODULE_PARM_DESC strings, so the description is checked in the patched
source).

## Contract checks implemented by the build script

1. the fired 0x0242 base files equal the pinned shas (refuse a drifted base);
2. the fired 0x0242 artifact (when present) equals 371,212 B / e2312402... /
   63 params; the send-2 patch equals its pin and applies with no fuzz; the
   resulting send-2 tree equals its pinned shas; the send-2 artifact (when
   present) equals 373,056 B / b8f6fe35... / 64 params;
3. this lane's patch equals its pin, applies to the send-2 tree with no fuzz,
   and the resulting tree equals the pinned patched shas; no conflict markers
   survive;
4. module build 0 warnings/errors/undefined symbols;
5. debug-strip does not change the .text;
6. artifact contract: **65 params** incl 'vap_start_g0' and 'vap_add_index'
   (the send-2 base is carried) + the sibling params; the 0x0065 plan/log
   strings ('vap_start_g0 pre: mctx=0x%08x', 'vap_start_g0 post:
   mctx[0x0e]=%u', "'vap_start_g0' posted", the >1 refusal, the G5 fault
   warning 'the key-0x0065 worker would fault at CA 0x46470', the G12
   band_start refusal, the sysfs line); 12 cfg80211 symvers;
7. the patched source still carries 'MODULE_PARM_DESC(vap_start_g0,'.

## Host tests

    ./tools/run-host-tests.sh

Compiles the patched tree's REAL models (hi5622_vap_start_g0.c and
hi5622_scan_logic.c) and the REAL sibling builders (../wifi-vap-start/src/
vapstart_frames.c for key 0x0065, ../wifi-band1-start-send/src/
hi5622_band_start.c for key 0x0242) with a plain C99 host compiler, then runs
the independent Python re-derivation + source contract.

Result: **351 checks, 0 failures** (evidence/host-tests.txt):

| part | checks | result |
| --- | ---: | --- |
| src/vap_start_g0_selftest.c | 278 | 0 failed |
| tools/vap_start_g0_frame_test.py | 73 | 0 failed |

No device contacted.

## Reproduce

    ./tools/build-vap-start-g0.sh
    ./tools/run-host-tests.sh

KDIR/STAGING come from /home/shibbityshwab/openwrt-lab-upstream-target (else
...-selectorfix-img); override with KDIR_OVERRIDE / STAGING_OVERRIDE.  The
fired base tree can be pointed at with BASE_TREE (default
/tmp/bandstart-build).

## Applying this patch on another base

The patch touches hi5622_scan.c, hi5622_scan_logic.c/.h and
hi5622_band_start.h and adds hi5622_vap_start_g0.c/.h + the Kbuild line.  On
the image-#3 merged module those regions may have drifted; the deliverable is
the pinned send-2 base the Lead asked for, and any other rebase/build is not
part of this lane.
