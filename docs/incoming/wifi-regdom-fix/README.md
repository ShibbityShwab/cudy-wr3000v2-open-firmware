# wifi-regdom-fix - the 5 GHz regdomain fix (cfg80211 signed-regdb requirement)

Status: HARDWARE-VALIDATED on the bench (2026-11-26): after this fix the box reports
country US: DFS-FCC and the 5 GHz band is IR (5150-5250 @ 23 dBm); the 5 GHz AP is no
longer refused by the regdomain (the remaining 5 GHz blocker is the firmware VAP message,
see docs/incoming/wifi-5ghz-vap-msg/).

## Root cause

The image's kernel is built with CONFIG_CFG80211_REQUIRE_SIGNED_REGDB=y (hidden behind
CONFIG_CFG80211_CERTIFICATION_ONUS, which depends on CONFIG_EXPERT). The shipped
regulatory.db (and even the host's matched signed pair) fails the kernel's pkcs7 verify on
this build -> cfg80211 rejects the regdb -> the world regdomain -> 5 GHz is NO-IR -> an AP
on ch36 is refused.

## Fix (host build + a targeted module swap; no flash needed)

1. kernel config (both the target template and the build tree):
   - CONFIG_EXPERT=y
   - CONFIG_CFG80211_CERTIFICATION_ONUS=y  (makes the prompt visible)
   - # CONFIG_CFG80211_REQUIRE_SIGNED_REGDB is not set
2. rebuild ONLY the cfg80211 module in the kernel tree:
   make -C <kdir> ARCH=arm CROSS_COMPILE=<...> net/wireless/cfg80211.ko
   (the resulting module has 0 pkcs7/x509 undefined refs; vermagic 6.18.60)
3. deploy to the box: /lib/modules/6.18.60/cfg80211.ko (back up the original),
   rmmod cfg80211; modprobe cfg80211, then iw reg set US.

Artifact identity (this build): 2,317,952 B sha256 bc4d9c73f5dae02b...

## Image integration

The next image bake MUST carry the rebuilt cfg80211.ko (and the config change, so a kernel
module rebuild reproduces it): the kernel modules in the image come from the kernel build
tree - rebuild the modules after the config change, or replace
/lib/modules/6.18.60/cfg80211.ko in the image rootfs with the rebuilt one, and re-run
tools/verify-final-image-2.sh with the new hash pinned.

## Verification

    iw reg get                    # expect: country US: DFS-FCC
    iw reg get | grep 5170-5250   # expect the IR flags (no PASSIVE-SCAN)

## Two image-update paths

### A. Re-bake (clean, recommended)
The bake source tree (OWRT, /home/shibbityshwab/openwrt-lab-selectorfix-img) now carries the
config change (target/linux/hisilicon/luofu/config-6.18 + the build tree .config), and the
built net/wireless/cfg80211.ko in the kernel tree is the fixed one. A re-bake via
docs/incoming/wifi-final-image-2/tools/bake-final-image-2.sh will rebuild the kernel modules
with the new config and carry the fixed cfg80211.ko. A full re-bake is a long build; combine
it with the next pending image changes (e.g. the 5GHz VAP fix, docs/incoming/wifi-5ghz-vap-msg/).

### B. Targeted rootfs patch (fast, for the existing image #2)
1. unsquashfs /home/shibbityshwab/wifi-final-image-2-artifacts/root.squashfs -d /tmp/root-patch
2. cp <fixed>/cfg80211.ko /tmp/root-patch/lib/modules/6.18.60/cfg80211.ko
3. mksquashfs /tmp/root-patch /tmp/root-new.squashfs -comp xz -b 262144 -Xbcj arm -noappend
   (the original superblock: block 262144, xz, "Filters selected: arm" - the ARM BCJ filter)
4. ubinize (the original rootfs.ubi volume layout; the extractor
   docs/incoming/wifi-image-integration/tools/extract-ubi-rootfs.py documents the volume map)
5. rebuild the sysupgrade container (the final-image-2 lane's structure: the kernel slice
   unchanged + the new rootfs.ubi + the manifest/header)
6. re-run docs/incoming/wifi-final-image-2/tools/verify-final-image-2.sh with the new
   rootfs/squashfs hashes pinned.

