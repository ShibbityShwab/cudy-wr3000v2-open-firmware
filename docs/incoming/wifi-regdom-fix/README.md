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
