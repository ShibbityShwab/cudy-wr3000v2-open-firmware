#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-he-caps-module.sh - build the fail-closed HE-capability module
# on top of the pinned 58-param vap5g base (lane wifi-ax-he-caps, task-177).
#
# HOST-ONLY: builds against the project 6.18.60 kernel tree; no device access,
# no image, no flash.  The .ko is local evidence only and is NEVER committed.
#
# Chain:
#   1. ../wifi-5ghz-vap-msg/tools/build-vapmsg5g-ready.sh
#      -> /tmp/wifi-vapmsg5g-build : the 58-param vap5g tree, artifact
#         353,652 B stripped sha256 13e7bf4c44772011e88903136c0c051d06d9ae5eab3c5e24df1ebb20f2cc1abf
#   2. copy this lane's portable helper (src/hi5622_he_caps.{c,h}) into it
#   3. patches/0001-he-caps.patch (he_caps module parameter, default 0)
#   4. build against linux-6.18.60 (armv7 musl toolchain) and assert the
#      artifact contract below; strip --strip-debug and verify .text identity.
#
#   ./tools/build-he-caps-module.sh
#   KDIR_OVERRIDE=... STAGING_OVERRIDE=... ./tools/build-he-caps-module.sh
set -eu
LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
BASE_TREE=/tmp/wifi-vapmsg5g-build
BASE_KO=/tmp/wifi-vapmsg5g-artifact/hi5622_wifi.ko
BASE_SHA=13e7bf4c44772011e88903136c0c051d06d9ae5eab3c5e24df1ebb20f2cc1abf
WORK=/tmp/wifi-he-caps-build
ART=/tmp/wifi-he-caps-artifact
ARTWORK=$ART.work
PATCH=$LANE/patches/0001-he-caps.patch

KDIR=
STAGING_DIR=
set +u
[ -n "$KDIR_OVERRIDE" ] && KDIR=$KDIR_OVERRIDE
[ -n "$STAGING_OVERRIDE" ] && STAGING_DIR=$STAGING_OVERRIDE
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -n "$KDIR" ] && [ -d "$KDIR" ] || { echo "he-caps: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] && [ -d "$STAGING_DIR" ] || { echo "he-caps: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "$CROSS"gcc ] || { echo "he-caps: cross gcc missing" >&2; exit 2; }
command -v patch >/dev/null || { echo "he-caps: patch required" >&2; exit 2; }
for f in "$LANE/src/hi5622_he_caps.c" "$LANE/src/hi5622_he_caps.h" "$PATCH"; do
	[ -f "$f" ] || { echo "he-caps: missing $f" >&2; exit 2; }
done

# 0. the pinned base: rebuild it when absent; refuse when it drifted.
if [ ! -d "$BASE_TREE" ] || [ ! -f "$BASE_KO" ]; then
	echo "== 0. build the pinned 58-param vap5g base"
	"$DOCS/wifi-5ghz-vap-msg/tools/build-vapmsg5g-ready.sh"
fi
[ -d "$BASE_TREE" ] || { echo "he-caps: base tree missing: $BASE_TREE" >&2; exit 2; }
if [ -f "$BASE_KO" ]; then
	got=$(sha256sum "$BASE_KO" | cut -d' ' -f1)
	if [ "$got" != "$BASE_SHA" ]; then
		echo "he-caps: base artifact drifted ($got != $BASE_SHA) - refusing" >&2
		exit 2
	fi
	echo "base artifact pinned: $BASE_SHA"
else
	echo "he-caps: base artifact not found (tree exists); continuing on the tree" >&2
fi
n=$(modinfo -p "$BASE_KO" 2>/dev/null | wc -l)
[ "$n" = "58" ] || { echo "he-caps: base is not the 58-param vap5g module ($n parms)" >&2; exit 2; }

# 1. stage the base source tree, drop build residue.
rm -rf "$WORK" "$ART" "$ARTWORK"
mkdir -p "$WORK" "$ART" "$ARTWORK"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .modules.order.cmd build.log 2>/dev/null ) || true
echo "base tree staged: $(ls "$WORK" | wc -l) files"

# 2. the portable contract helper is this lane's source, not part of the patch.
cp "$LANE/src/hi5622_he_caps.c" "$LANE/src/hi5622_he_caps.h" "$WORK/"

# 3. the fail-closed patch.
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" >/dev/null )
grep -q 'hi5622_he_caps.h' "$WORK/hi5622_cfg80211.c" || { echo "he-caps: patch not applied" >&2; exit 1; }
grep -q 'hi5622_he_caps_fill' "$WORK/hi5622_cfg80211.c" || { echo "he-caps: patch incomplete (no blob use)" >&2; exit 1; }
grep -q 'hi5622_he_caps.o' "$WORK/Kbuild" || { echo "he-caps: Kbuild not patched" >&2; exit 1; }
# data contract: the capability VALUES live only in the portable header/helper;
# the driver must not duplicate them as literals.
if grep -nE 'rx_mcs_(80|160|80p80) = 0x' "$WORK/hi5622_cfg80211.c"; then
	echo "he-caps: HE capability literals duplicated in the driver - refusing" >&2
	exit 1
fi
if grep -nE '\.phy_cap_info = \{' "$WORK/hi5622_cfg80211.c"; then
	echo "he-caps: static PHY capability array duplicated in the driver - refusing" >&2
	exit 1
fi
echo "patch + data contract: OK"

# 4. build.
export STAGING_DIR
make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "he-caps: BUILD FAILED" >&2; tail -40 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "he-caps: warnings/errors/undefined symbols - refusing artifact" >&2; exit 1
fi
KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "he-caps: no .ko" >&2; exit 1; }
"$CROSS"strip --strip-debug -o "$ARTWORK/hi5622_wifi.ko" "$KO"
"$CROSS"objcopy -O binary --only-section=.text "$KO" "$WORK/.t1"
"$CROSS"objcopy -O binary --only-section=.text "$ARTWORK/hi5622_wifi.ko" "$WORK/.t2"
cmp "$WORK/.t1" "$WORK/.t2" || { echo "he-caps: .text changed by debug strip" >&2; exit 1; }
rm -f "$WORK/.t1" "$WORK/.t2"

# 5. artifact contract.
P=$(modinfo -p "$ARTWORK/hi5622_wifi.ko")
[ "$(printf '%s\n' "$P" | wc -l)" = "59" ] || { echo "he-caps: param count != 59" >&2; exit 1; }
printf '%s\n' "$P" | grep -q '^he_caps: (uint)$' || { echo "he-caps: he_caps parameter missing" >&2; exit 1; }
S=$(strings "$ARTWORK/hi5622_wifi.ko")
[ "$(printf '%s\n' "$S" | grep -c 'HE (802.11ax) caps advertised')" -ge 1 ] || {
	echo "he-caps: HE advertisement feature string missing" >&2; exit 1; }
[ "$(wc -l < "$WORK/cfg80211-provider.symvers")" = "12" ] || {
	echo "he-caps: cfg80211 symvers contract changed (not 12)" >&2; exit 1; }
mv "$ARTWORK/hi5622_wifi.ko" "$ART/hi5622_wifi.ko"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"
rm -rf "$ARTWORK"

echo
echo "artifact : $ART/hi5622_wifi.ko"
echo "size     : $(stat -c %s "$ART/hi5622_wifi.ko") B"
echo "sha256   : $(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)"
echo "params   : 59 (58 vap5g + he_caps, default 0)"
echo "notice   : he_caps=1 advertises the vendor-decoded HE caps (AP iftype)"
