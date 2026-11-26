# BUILD-NOTES - lane wifi-band1-start-send-5 (task-213, the R5 order)

HOST-ONLY build.  No device access, no firmware write, no image, no flash.
The .ko is LOCAL EVIDENCE ONLY (/tmp, never committed - this repo stores no
binary artifacts).

## Chain (extended from the gate-inverted module)

    /tmp/r4border-build                       (R4b tree, ../wifi-band1-start-send-4)
      + ../wifi-vap-start-gates/patches/0001-g9-g11-rf-enable-invert.patch
      = the gate-inverted tree   (the deployed module 9bf1d846, 382,376 B)
      + this lane's patches/0001-r5-order.patch
      = /tmp/r5-build   ->  /tmp/r5-artifact/hi5622_wifi.ko

The two wifi-vap-start-gates patches are both used: 0001 reproduces the
deployed module's source (G9'/G11' == 1), 0002 patches the send-4 C selftest
copy.  The R5 build script applies them in exactly this order and refuses a
drifted base, a drifted patch, a drifted tree result, conflict markers, build
warnings/errors/undefined symbols, and an artifact-contract mismatch.

## Pins (enforced by tools/build-r5-order.sh)

| pin | value |
| --- | --- |
| R4b input tree | /tmp/r4border-build (reproduce with ../wifi-band1-start-send-4/tools/build-r4b-order.sh) |
| gates patch | ../wifi-vap-start-gates/patches/0001-g9-g11-rf-enable-invert.patch |
| gate-inverted source pins | hi5622_scan.c `ab14fa07...`, hi5622_scan_logic.c `ab7b8bac...`, hi5622_scan_logic.h `3bfe1239...`, hi5622_vap_start_g0.c `346b5441...`, hi5622_vap_start_g0.h `92b17b15...`, hi5622_band_start.h `443b071d...` |
| base artefact (deployed) | /tmp/vsg-artifact/hi5622_wifi.ko, 382,376 B, sha256 9bf1d846e730ce57a5adfef9a2fed9ab589fcba159b6cd08165339917673cf82, 65 params |
| lane patch 0001 | sha256 e464f4cf0038859705be492967771e7c3294ef7ba84275c96bb4b473a6b86cf7, 25,415 B |
| lane patch 0002 | sha256 f35d34e4370da5469d24b0defd078aec8dacd4895b2e1226d8135c72d32cba4e, 4,857 B |
| R5 tree pins | hi5622_scan.c `86d6c57e...`, hi5622_scan_logic.c `15c50d34...`, hi5622_scan_logic.h `6af774ef...`, hi5622_vap_start_g0.c `346b5441...` (unchanged), hi5622_vap_start_g0.h `81533d9c...`, hi5622_band_start.h `e2f059cf...`, hi5622_band_start.c `71019718...` (unchanged), Kbuild `1bb51085...` (unchanged) |
| artifact | **/tmp/r5-artifact/hi5622_wifi.ko, 383,240 B**, sha256 **24f7221bfc4c79971ce1a0aa93cfdc2c0293261226c24111037c094ab13fa70c** |
| unstripped | 1,424,680 B, sha256 233df986cfcd2c6a5936a5cab532e9d1986e473b40ca194144f02a2a036a0cfa |
| .text sha256 | a727c339714b8826ea069194a50d376622175ec5adf8e7b537442edc7731d11d |
| params | **65** (unchanged: no new knob) |
| vermagic | 6.18.60 SMP mod_unload ARMv7 p2v8 |
| warnings | 0 |
| cfg80211 symvers | 12 lines |
| size delta | +864 B vs the deployed 9bf1d846 (helper + the AP-config phase split) |

Full shas: evidence/base-chain.txt and evidence/build-artifact.txt.

## Contract checks implemented by the build script

1. the R4b source tree exists (else it names the R4b build script);
2. gate patch 0001 applies, and the resulting gate-inverted tree equals the
   six pinned source shas (the deployed 9bf1d846 tree - no base drift);
3. the R5 patch 0001 equals its pin (checked by content, no fuzz: patch
   applies with no .rej/.orig) and the resulting tree equals the eight pinned
   R5 shas; the frame-builder sources (hi5622_band_start.c, Kbuild,
   hi5622_ap_frames.c) are untouched;
4. module build 0 warnings/errors/undefined symbols; the stripped artifact is
   65 params; the R5 strings are present ('AP config posted BEFORE the
   0x0065 VAP start / 0x0242 band start (R5)', the beacon-template line, both
   reordered post lines, the corrected G9'/G11' verdict strings);
5. the base artifact (9bf1d846) is reported if present;
6. host tests: the send-4 C selftest copy + gates 0002 + R5 0002 compiles with
   -Werror and passes, and the independent Python R5 order/contract test
   passes against the R5 tree.

## Host tests

    ./tools/run-host-tests.sh          # against an already-built /tmp/r5-build
    ./tools/build-r5-order.sh          # full chain + build + tests

The C selftest compiles the REAL patched-tree models (hi5622_vap_start_g0.c,
hi5622_scan_logic.c) and the REAL sibling builders (../wifi-vap-start/src/
vapstart_frames.c for key 0x0065, ../wifi-band1-start-send/src/
hi5622_band_start.c for key 0x0242) with a plain C99 host compiler.  The
Python test re-derives the 40-B key-0x0065 frame independently and runs the
source contract on the tree.

Result: **405 checks, 0 failures** (evidence/host-tests.txt):

| part | checks | result |
| --- | ---: | --- |
| the patched send-4 selftest (src/vap_start_g0_selftest.c + gates 0002 + R5 0002) | 295 | 0 failed |
| tools/r5_order_frame_test.py | 110 | 0 failed |

The selftest asserts the 40-B frame KATs (byte-identical - the R5 order
change touches no wire byte), the 9/8/4-step plan orders, the R5 enum order,
the G8 sequencing model, all G1..G14 gates (with the gate-inverted G9'/G11'
polarity), G12 and the fail-closed matrix.  No device contacted.

## Reproduce

    ./tools/build-r5-order.sh
    ./tools/run-host-tests.sh

KDIR/STAGING come from /home/shibbityshwab/openwrt-lab-upstream-target (else
...-selectorfix-img); override with KDIR_OVERRIDE / STAGING_OVERRIDE on the
R4b/gates scripts.  The R4b base tree can be pointed at with TREE_SRC (default
/tmp/r4border-build); the R5 build is /tmp/r5-build.

## Applying this patch on another base

The patch touches hi5622_scan.c, hi5622_scan_logic.c/.h,
hi5622_vap_start_g0.h and hi5622_band_start.h (order + comments only in the
last two).  On an image-#3 merged module those regions may have drifted; the
deliverable is the pinned gate-inverted base the Lead asked for, and any other
rebase/build is not part of this lane.
