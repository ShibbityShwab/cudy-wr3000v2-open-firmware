# BUILD-NOTES - lane wifi-band1-start-send-2 (task-207)

HOST-ONLY build.  No device access, no firmware write, no image, no flash.
The `.ko` is LOCAL EVIDENCE ONLY (/tmp, never committed - this repo stores no
binary artifacts).

## Chain

    ../wifi-band1-start-send/patches/0001-band-start-send.patch on the F2 tree
      = /tmp/bandstart-build + /tmp/bandstart-artifact/hi5622_wifi.ko
        371,212 B, sha256 e2312402...a028, 63 params (fired 2026-11-28 05:00)
    this lane's patches/0001-vap-add-index.patch
      = the explicit vap_add_index parameter + data[4]/data[1]/hdr[3]
        agreement + the cfg80211 fail-closed gates + the read-only
        post-add readback (64 params)

## Pins (checked by tools/build-vap-bound.sh)

| pin | value |
| --- | --- |
| fired base files | hi5622_scan.c `272e47ac...`, hi5622_vapscan_provider.c `c306e75f...`, vapstart_frames.c `7dc7bc35...`, vapstart_frames.h `0f1d13c9...`, hi5622_band_start.c `71019718...`, hi5622_band_start.h `dac835ae...` |
| fired base artifact | 371,212 B, sha256 `e231240292f059e431ede6a2e9a347c357cdc2c3713a1695d46fd38547afa028`, 63 params |
| lane patch | sha256 `968d70c5c4f13005e6ddee7f1bed2423888c8a58ed7582a7806b8ea031b813fb` |

## Artifact (host build, not fired, never committed)

| field | value |
| --- | --- |
| stripped | /tmp/vapbound-artifact/hi5622_wifi.ko, **373,056 B** |
| sha256 | `b8f6fe359c7e6138669c63be426733d4898266cd2da4be965b483bddc9ff5b71` |
| unstripped | 1,402,780 B, sha256 `28e14fb4a5dd8d1c60985ab001b218e94dd32544796a2872306a3a570237c1dd` |
| .text sha256 | `ff4fb171e16212ee4b7038b15855ac14f402d3e2b30c7ac64d6194955a8ae99c` |
| params | **64** (63 fired base + `vap_add_index`) |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| warnings | 0; debug-strip keeps .text byte-identical |
| cfg80211 symvers | 12 lines |
| size delta | +1,844 B vs the fired 0x0242 artifact |

`vap_add_index` is a read-only (0444) uint parameter, default **0**; the
built module's .modinfo carries `parmtype=vap_add_index:uint` (the 6.18
loader drops MODULE_PARM_DESC strings, so the description is checked in the
patched source).

## Contract checks implemented by the build script

1. the fired base files equal the pinned shas (refuse a drifted base);
2. the fired 0x0242 artifact (when present) equals 371,212 B / `e2312402...`
   / 63 params;
3. the lane patch equals its pin, applies to the base with no fuzz, and no
   conflict markers survive;
4. module build 0 warnings/errors/undefined symbols;
5. debug-strip does not change the .text;
6. artifact contract: **64 params** incl `vap_add_index` + the sibling
   params, the two VAP-index gate strings, the post-add readback string, the
   0x0242 band_start line, 12 cfg80211 symvers;
7. the patched source still carries `MODULE_PARM_DESC(vap_add_index)`.

## Host tests

    ./tools/run-host-tests.sh

Compiles the portable contract model (src/) and the **real sibling builders**
(../wifi-vap-start/src/vapstart_frames.c for key 0x0064,
../wifi-band1-start-send/src/hi5622_band_start.c for key 0x0242) with a plain
C99 host compiler, then runs the independent Python re-derivation + source
contract.

Result: **283 checks, 0 failures** (evidence/host-tests.txt):

| part | checks | result |
| --- | ---: | --- |
| src/vap_index_selftest.c | 260 | 0 failed |
| tools/vap_add_frame_test.py | 23 | 0 failed |

No device contacted.

## Reproduce

    ./tools/build-vap-bound.sh
    ./tools/run-host-tests.sh

KDIR/STAGING come from /home/shibbityshwab/openwrt-lab-upstream-target (else
...-selectorfix-img); override with KDIR_OVERRIDE / STAGING_OVERRIDE.  The
fired base tree can be pointed at with BASE_TREE (default /tmp/bandstart-build).

## Applying this patch on another base

The patch touches only `hi5622_vapscan_provider.c` and `hi5622_scan.c`.  On
the image #3 merged module those regions are unchanged, so it should apply and
rebuild; that rebase/build is **not** part of this lane (the deliverable is the
pinned 0x0242 base the Lead asked for).
