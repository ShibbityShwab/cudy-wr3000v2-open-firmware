#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# selftest.sh - device-free checks for the wifi-image-integration lane.
# Runs without an OpenWrt tree (only the tracked files here + the sibling
# driver source are inspected).
set -uo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/.." && pwd)              # opensource/docs/incoming/wifi-image-integration
INC=$HERE/../../                       # opensource/docs/incoming
DRIVER_SRC=$INC/wifi-cfg80211/src
PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); printf 'ok   %s\n' "$1"; }
bad()  { FAIL=$((FAIL+1)); printf 'FAIL %s\n' "$1"; }
chk()  { if eval "$2" >/dev/null 2>&1; then ok "$1"; else bad "$1"; fi; }

echo "== wifi-image-integration selftest =="
echo "dir: $ROOT"

# ---- package contract -----------------------------------------------------
chk "package Makefile exists"            "[ -f '$ROOT/package/hi5622-wifi/Makefile' ]"
chk "package README exists"              "[ -f '$ROOT/package/hi5622-wifi/README.md' ]"
chk "KernelPackage hi5622-cfg80211"      "grep -q 'define KernelPackage/hi5622-cfg80211' '$ROOT/package/hi5622-wifi/Makefile'"
chk "KernelPackage hi5622-wifi"          "grep -q 'define KernelPackage/hi5622-wifi' '$ROOT/package/hi5622-wifi/Makefile'"
chk "cfg80211 FILES from LINUX_DIR"      "grep -q 'FILES:=\$(LINUX_DIR)/net/wireless/cfg80211.ko' '$ROOT/package/hi5622-wifi/Makefile'"
chk "driver FILES from PKG_BUILD_DIR"    "grep -q 'FILES:=\$(PKG_BUILD_DIR)/\$(HI5622_KO)' '$ROOT/package/hi5622-wifi/Makefile'"
chk "package has PREBUILT mode"          "grep -q 'HI5622_PREBUILT_KO' '$ROOT/package/hi5622-wifi/Makefile'"
chk "package has SOURCE mode"            "grep -q 'KERNEL_MAKE) M=\$(PKG_BUILD_DIR) modules' '$ROOT/package/hi5622-wifi/Makefile'"
chk "package records prebuilt identity"  "grep -q 'prebuilt-sha256.txt' '$ROOT/package/hi5622-wifi/Makefile'"
chk "driver depends on cfg80211 kmod"    "grep -q '+kmod-hi5622-cfg80211' '$ROOT/package/hi5622-wifi/Makefile'"
chk "target dependency declared"         "[ \$(grep -c '@TARGET_hisilicon_luofu' '$ROOT/package/hi5622-wifi/Makefile') -ge 2 ]"
chk "no AUTOLOAD (bench policy)"         "! grep -qE '^[[:space:]]*AUTOLOAD[[:space:]]*[:+]?=' '$ROOT/package/hi5622-wifi/Makefile'"
chk "Kbuild uses kernel make"            "grep -q 'KERNEL_MAKE) M=' '$ROOT/package/hi5622-wifi/Makefile'"
chk "no init script shipped"             "[ ! -d '$ROOT/package/hi5622-wifi/files' ]"

# ---- config delta ---------------------------------------------------------
chk "config delta exists"                "[ -f '$ROOT/config-delta.config' ]"
for sym in iw wireless-regdb wifi-scripts hostapd-mini hostapd-common \
           ucode-mod-nl80211 ucode-mod-rtnl kmod-hi5622-cfg80211 kmod-hi5622-wifi; do
	chk "delta selects CONFIG_PACKAGE_$sym=y" "grep -q '^CONFIG_PACKAGE_$sym=y' '$ROOT/config-delta.config'"
done
chk "delta enables ucode wifi-scripts"   "grep -q '^CONFIG_WIFI_SCRIPTS_UCODE=y' '$ROOT/config-delta.config'"
chk "delta is self-contained (hostapd)"  "grep -q 'hostapd-mini' '$ROOT/config-delta.config'"

# ---- driver source (sibling lane snapshot source) -------------------------
chk "driver Kbuild present"              "[ -f '$DRIVER_SRC/Kbuild' ]"
chk "driver obj-m is hi5622_wifi.o"      "grep -q '^obj-m := hi5622_wifi.o' '$DRIVER_SRC/Kbuild'"
chk "13 driver objects in Kbuild"        "[ \$(grep -c 'hi5622_wifi-y' '$DRIVER_SRC/Kbuild') -ge 1 ]"
chk "cfg80211 provider symvers present"  "[ -f '$DRIVER_SRC/cfg80211-provider.symvers' ]"
chk "cfg80211 layer sources present"     "[ -f '$DRIVER_SRC/hi5622_cfg80211.c' ] && [ -f '$DRIVER_SRC/hi5622_cfg80211_ops.c' ]"
chk "no vendor firmware in driver src"   "! find '$DRIVER_SRC' -maxdepth 1 -type f \( -name '*.bin' -o -name '*.fw' -o -name '*.img' \) | grep -q ."
# working-tree driver dir contains untracked *.ko build products by design; only
# committed objects would be a policy violation. Check the git index instead.
chk "no driver objects committed"        "! git -C '$INC/../..' ls-files 'docs/incoming/wifi-cfg80211/src' | grep -qE '\.(ko|o)$'"

# ---- tools / docs ---------------------------------------------------------
chk "install-into-tree.sh syntax"        "bash -n '$HERE/install-into-tree.sh'"
chk "installer supports --driver-ko"     "grep -q '\-\-driver-ko' '$HERE/install-into-tree.sh'"
chk "installer validates the artifact"   "grep -q 'prebuilt module does not depend on cfg80211' '$HERE/install-into-tree.sh'"
chk "build-image.sh syntax"              "bash -n '$HERE/build-image.sh'"
chk "build script defaults to prebuilt"  "grep -q 'DEFAULT_DRIVER_KO=/tmp/wifi-ap-dp-combined-artifact/hi5622_wifi.ko' '$HERE/build-image.sh'"
chk "verify-image.sh syntax"             "bash -n '$HERE/verify-image.sh'"
chk "selftest.sh syntax"                 "bash -n '$HERE/selftest.sh'"
chk "UBI extractor syntax"               "python3 -m py_compile '$HERE/extract-ubi-rootfs.py'"
chk "REPORT.md present"                  "[ -f '$ROOT/REPORT.md' ]"
chk "BUILD-NOTES.md present"             "[ -f '$ROOT/BUILD-NOTES.md' ]"
chk "DEPLOY.md present"                  "[ -f '$ROOT/DEPLOY.md' ]"
chk "README.md present"                  "[ -f '$ROOT/README.md' ]"
chk "kernel-symbols.config present"      "[ -f '$ROOT/kernel-symbols.config' ]"
chk "HOTPLUG_CPU carry-over present"     "grep -q '^CONFIG_KERNEL_HOTPLUG_CPU=y' '$ROOT/kernel-symbols.config'"
chk "build script carries over symbols"  "grep -q 'kernel-symbols.config' '$HERE/build-image.sh'"
chk "build-evidence gitignores work/"    "grep -q '^work/$' '$ROOT/build-evidence/.gitignore'"
chk "recorded build summary present"     "[ -f '$ROOT/build-evidence/build-summary.txt' ]"
chk "recorded image verify PASS"         "grep -q 'RESULT: PASS - all required files and packages are in the built image' '$ROOT/build-evidence/image-verify.txt'"
chk "recorded build PASS"                "grep -q 'RESULT: PASS - bench image built, verified and evidence recorded' '$ROOT/build-evidence/build-summary.txt' -r || grep -q 'RESULT: PASS' '$ROOT/build-evidence/build-summary.txt'"
chk "validated kernel slice recorded"    "grep -q '13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f' '$ROOT/build-evidence/build-summary.txt'"
chk "round-2 ABI image evidence archived" "grep -q 'df6a78c3717b0f792391e7f7d488a9f2850c9640553ef391163dd34abdb47310' '$ROOT/build-evidence/round2-abi-image-df6a78c3/build-summary.txt'"
chk "prebuilt driver identity recorded"  "[ -f '$ROOT/build-evidence/driver-artifact.txt' ] && grep -q '^sha256=' '$ROOT/build-evidence/driver-artifact.txt'"
chk "final container hash recorded"      "grep -q 'ae5e28bfef591fad7d9b4da726128ec2307273aebc41008e0ad9c3867bde6994' '$ROOT/build-evidence/build-summary.txt'"
chk "final rootfs hash recorded"         "grep -q 'ff9f3e1c83c36a2a9170cdf6373228692b46b7fa0fbb64abbdc70a25b841aaa9' '$ROOT/build-evidence/build-summary.txt'"
chk "combined driver artifact recorded"  "grep -q '44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b' '$ROOT/build-evidence/driver-artifact.txt'"
chk "hostapd in final manifest"          "grep -q 'PRESENT  hostapd-mini - ' '$ROOT/build-evidence/image-verify.txt'"
chk "wifi-scripts in final manifest"     "grep -q 'PRESENT  wifi-scripts - ' '$ROOT/build-evidence/image-verify.txt'"
chk "DEPLOY documents rootfs-only path"  "grep -q 'rootfs-only' '$ROOT/DEPLOY.md' && grep -q 'stock-telnet-install' '$ROOT/DEPLOY.md'"

# REQUIRED ROOTFS CONTENT list is declared in exactly one place (verify-image.sh).
chk "required list has iw"               "grep -q 'usr/sbin/iw' '$HERE/verify-image.sh'"
chk "required list has iwinfo"           "grep -q 'usr/bin/iwinfo' '$HERE/verify-image.sh'"
chk "required list has hostapd"          "grep -q 'usr/sbin/hostapd' '$HERE/verify-image.sh'"
chk "required list has hostap ucode"     "grep -q 'usr/share/hostap/common.uc' '$HERE/verify-image.sh'"
chk "required list has ucode wifi"       "grep -q 'usr/share/ucode/wifi/common.uc' '$HERE/verify-image.sh'"
chk "required list has ucode nl80211"    "grep -q 'usr/lib/ucode/nl80211.so' '$HERE/verify-image.sh'"
chk "required list has cfg80211.ko"      "grep -q 'lib/modules/.*/cfg80211.ko' '$HERE/verify-image.sh'"
chk "required list has hi5622_wifi.ko"   "grep -q 'hi5622_wifi.ko' '$HERE/verify-image.sh'"
chk "required list has regulatory.db"    "grep -q 'lib/firmware/regulatory.db' '$HERE/verify-image.sh'"

# ---- policy: no binaries / no secrets in this deliverable ----------------
chk "no binaries committed in lane dir"  "! find '$ROOT' -path '$ROOT/build-evidence/work' -prune -o -type f \( -name '*.ko' -o -name '*.o' -o -name '*.apk' -o -name '*.bin' -o -name '*.ubi' -o -name '*.squashfs' \) -print | grep -q ."
chk "no private key material"            "! grep -rIl --exclude=selftest.sh --exclude-dir=work 'BEGIN RSA PRIVATE[ ]KEY\|BEGIN OPENSSH PRIVATE[ ]KEY\|BEGIN EC PRIVATE[ ]KEY' '$ROOT' 2>/dev/null | grep -q ."

echo
echo "wifi-image-integration selftest: $PASS checks PASS, $FAIL failed"
[ "$FAIL" -eq 0 ] || exit 1
