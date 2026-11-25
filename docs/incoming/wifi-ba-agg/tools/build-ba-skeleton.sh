#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# build-ba-skeleton.sh - host build of the fail-closed BA skeleton module
# (lane wifi-ba-agg, task-178).  HOST-ONLY: no device, no firmware write.
#
# Chain: 5 GHz ready base tree (/tmp/wifi-5ghz-ready-build, 57 params,
# sha256 13366151...892d53) + patches/0001-ba-skeleton.patch -> the skeleton
# module (ba_enable default 0, inert, unwired).  The .ko is local evidence and
# is never committed.
#
# Usage: ./tools/build-ba-skeleton.sh
set -eu

LANE=$(cd "$(dirname "$0")/.." && pwd)
P="$LANE/patches/0001-ba-skeleton.patch"
BASE_TREE=${BASE_TREE:-/tmp/wifi-5ghz-ready-build}
WORK=${WORK:-/tmp/wifi-ba-skeleton-build}
ART=${ART:-/tmp/wifi-ba-skeleton-artifact}

KDIR=
STAGING_DIR=
set +u
[ -n "${KDIR_OVERRIDE:-}" ] && KDIR=$KDIR_OVERRIDE
[ -n "${STAGING_OVERRIDE:-}" ] && STAGING_DIR=$STAGING_OVERRIDE
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then KDIR=$c; STAGING_DIR=$root/staging_dir; break; fi
	done
fi
[ -d "$KDIR" ] || { echo "ba-skeleton: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "ba-skeleton: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "ba-skeleton: cross gcc missing" >&2; exit 2; }
[ -f "$P" ] || { echo "ba-skeleton: missing patch $P" >&2; exit 2; }
[ -d "$BASE_TREE" ] || {
	echo "ba-skeleton: base tree missing: $BASE_TREE" >&2
	echo "ba-skeleton: build the 5 GHz base first:" >&2
	echo "  ../wifi-5ghz-bringup/tools/build-5ghz-ready.sh" >&2
	exit 2
}

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order build.log 2>/dev/null ) || true
echo "== 1. base tree staged: $(ls "$WORK" | wc -l) files"

echo "== 2. apply the BA skeleton patch"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P" >/dev/null ) || {
	echo "ba-skeleton: patch does not apply to the 5 GHz base" >&2
	exit 1
}
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "ba-skeleton: conflict markers survived - refusing build" >&2
	exit 1
fi
grep -q 'hi5622_ba\.o' "$WORK/Kbuild" || {
	echo "ba-skeleton: Kbuild does not list hi5622_ba.o" >&2
	exit 1
}

echo "== 3. build the module (linux-6.18.60, armv7 musl)"
export STAGING_DIR
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules \
	> "$WORK/build.log" 2>&1 || {
	echo "ba-skeleton: build failed" >&2
	tail -30 "$WORK/build.log" >&2
	exit 1
}
if grep -Ei 'warning:|error:' "$WORK/build.log" >/dev/null 2>&1; then
	echo "ba-skeleton: build produced warnings/errors" >&2
	grep -Ei 'warning:|error:' "$WORK/build.log" | head -20 >&2
	exit 1
fi
echo "== build clean (0 warnings, 0 errors)"

KO="$WORK/hi5622_wifi.ko"
[ -f "$KO" ] || { echo "ba-skeleton: module missing" >&2; exit 1; }
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || {
	echo "ba-skeleton: debug-strip changed .text" >&2
	exit 1
}
echo "== debug-strip keeps .text identical"
KO="$ART/hi5622_wifi.ko"
cp "$WORK/build.log" "$ART/build.log"

echo "== 4. artifact contract"
PARAMS=$(modinfo -p "$KO" | grep -c '^' || true)
SHA=$(sha256sum "$KO" | cut -d' ' -f1)
SIZE=$(stat -c %s "$KO")
USIZE=$(stat -c %s "$ART/hi5622_wifi.ko.unstripped")
echo "module: $SIZE B debug-stripped, sha256 $SHA, $PARAMS params (unstripped $USIZE B)"
[ "$PARAMS" -eq 58 ] || {
	echo "ba-skeleton: expected 58 params (57 base + ba_enable), got $PARAMS" >&2
	exit 1
}
modinfo -p "$KO" | grep -q '^ba_enable:' || {
	echo "ba-skeleton: ba_enable parameter missing" >&2
	exit 1
}
grep -q '(uint)' <(modinfo -p "$KO" | grep '^ba_enable:') || {
	echo "ba-skeleton: ba_enable is not a uint parameter" >&2
	exit 1
}
echo "ba_enable: $(modinfo -p "$KO" | grep '^ba_enable:')"

# The skeleton must be unwired: no other object may reference a hi5622_ba_*
# symbol (the only definitions/calls are inside hi5622_ba.o itself).
UNREF=$(nm -u "$WORK"/*.o 2>/dev/null | grep 'hi5622_ba_' || true)
[ -z "$UNREF" ] || {
	echo "ba-skeleton: another object references the BA core - not inert:" >&2
	echo "$UNREF" >&2
	exit 1
}
echo "inert check: no external object references hi5622_ba_* (skeleton unwired)"

nm "$KO" | grep -q ' hi5622_ba_check_enabled$' || {
	echo "ba-skeleton: hi5622_ba_check_enabled not linked" >&2
	exit 1
}
echo "linked: hi5622_ba_check_enabled and the BA core are in the module"

echo
echo "ARTIFACT: $ART/hi5622_wifi.ko"
echo "$SHA  hi5622_wifi.ko" > "$ART/SHA256SUMS"
