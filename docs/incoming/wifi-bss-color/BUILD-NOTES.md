# BUILD-NOTES - how the BSS-colour module is produced, what is pinned, how it is tested

**HOST-ONLY.** Everything here runs on the workstation; nothing contacts the router,
the bench link or the UART. The `.ko` is local evidence and is **never committed**
(only the text receipts in [evidence/](evidence/) are).

## Chain

    base: the IMAGE #3 module source tree (the flashed+accepted build)
          snapshot /tmp/wifi-image-3-module, 97 entries, 64-param module a717c86d;
          pinned hashes in evidence/base-pin.txt
      + patches/0001-bss-color.patch   (2 new files + 2 edits; see below)
      = 65-param module with the fail-closed `bss_color` knob (default 0)

The base is the image #3 tree because that is the stack the Lead will fire; the lane
does **not** fork it. `tools/build-module.sh <image-3-module-dir> <work-dir>` applies the
patch to a copy, builds and reports the artifact. The patch is verified to apply with
`patch -p1` and to reproduce the work tree byte-for-byte (same comparison used here).

## The patch (patches/0001-bss-color.patch, 13,322 B, 5 files)

* `hi5622_bss_color.h` (new): the frame contract, the bitmap/colour policy, the printf-safe
  kernel prototype. Portable C99 (kernel + host selftest compile the same code).
* `hi5622_bss_color.c` (new): `hi5622_bss_color_build/validate/mask_from_color/policy`
  (portable) + the `bss_color` module parameter (0644, default 0) + `hi5622_bss_color_post_ap()`
  (kernel): read `settings->beacon.he_bss_color` (`NL80211_ATTR_HE_BSS_COLOR`), build the
  20-byte g0 id36 frame and post it through `hi5622_frame_post()` (the one validated wire path).
* `hi5622_scan.c` (edit): include + one call in `hi5622_scan_op_start_ap` after the AP config /
  VAP start, before the committed-final log; a failed/skipped post is logged and **never**
  aborts an AP that is already configured.
* `Kbuild` (edit): add `hi5622_bss_color.o`.

One wire frame per start_ap, only when `bss_color=1` **and** `allow_frames>0` (the inherited
frame-post gate; image #3 ships allow_frames=32) **and** the start_ap carried an enabled HE
BSS colour. `bss_color>1` is refused (no frame, logged). Default 0 = byte-identical
behaviour to the image #3 module.

## Build (reproduced here)

    KDIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir
    make ARCH=arm CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi- \
         -C $KDIR M=/tmp/bss-color-work modules

## Artifact (host-built, local; receipt in [evidence/build-artifact.txt](evidence/build-artifact.txt))

    unstripped 1,457,944 B sha256 3ddbf146a56d700e6f102b55d0e68aaf4ff79755c7b26451b868d507f332487f
    stripped     382,404 B sha256 d87fbdcb38a8f37b93f88d4ec39a2f9de0cd426551d3231a857cd67c92befdcb
    vermagic 6.18.60 SMP mod_unload ARMv7 p2v8 ; depends=cfg80211
    params   65 (64 base + bss_color:uint) ; 0 compiler/modpost warnings ; .text identical after strip

The stripped sha is the fire identity **for a build in /tmp/bss-color-work**; a different
build directory can change `.note.gnu.build-id`, so re-derive both hashes if rebuilt
elsewhere (`.text` is the strip-invariant identity, hash recorded in the receipt).

## Host checks

    ./tools/run-host-tests.sh        # 29 checks, 0 failed (evidence/host-tests.txt)

* KATs: K1 = the captured stock frame byte-exact (`00 01 00 07 14 00 24 00 00 00 5a 5a |
  09 00 01 02 00 00 00 04`, colours {0,3,16,25,58}); K2 = hostapd he_bss_color=25
  (`... 00 01 00 00 ... | 00 00 00 02 00 00 00 00`); K3 = colour 1/chip 4; K4 = colour
  63/chip 15; a bid_hi/fa byte-placement vector.
* validation negatives (wrong id/length/tag, truncation, short buffer, bid_hi > 0xf);
* colour policy (1..63 only; 0/64 refused), bitmap bit tests, module-param policy (0/1 pass, 2 refused).

## Safety / policy

* the parameter defaults to 0: every bench-proven image #3 load line keeps working
  byte-identically unless `bss_color=1` is set;
* the frame is the decoded vendor g0 id36 (firmware handler exists in the committed
  firmware table; payload is a value bitmap, no pointer dereference - REPORT sec. 3), and
  every post still passes `hi5622_frame_post()`'s structural/firmware-table/gate checks;
* no vendor binary, object, firmware byte, calibration value or secret is committed; the
  decode addresses and derived wire constants carry evidence markers in REPORT/TABLES.
