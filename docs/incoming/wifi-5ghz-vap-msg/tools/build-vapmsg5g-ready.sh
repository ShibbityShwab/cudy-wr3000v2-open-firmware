#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-vapmsg5g-ready.sh - build the 5 GHz AP module with the
# fail-closed vendor-order patch (lane wifi-5ghz-vap-msg, task-174).
# HOST-ONLY: compiles against the project 6.18.60 kernel tree; no device
# access, no firmware write, no image, no flash.  The .ko is local evidence
# only and is never committed.
#
# Chain:
#   1. ../wifi-5ghz-bringup/tools/build-5ghz-ready.sh
#      = association tree + sibling mgmt carrier + band5g patches
#      (the 57-param module under bench test, sha 13366151...892d53)
#   2. this lane's patches/0001-vap5g-order.patch
#      = the fail-closed vap5g order patch (default 0)
#
# Usage:
#   ./tools/build-vapmsg5g-ready.sh
#   BASE_TREE=/tmp/wifi-5ghz-ready-build ./tools/build-vapmsg5g-ready.sh
set -eu
LANE=$(cd $(dirname "$0")/.. && pwd)
DOCS=$(cd "$LANE/.." && pwd)
P=$LANE/patches/0001-vap5g-order.patch
WORK=/tmp/wifi-vapmsg5g-build
ART=/tmp/wifi-vapmsg5g-artifact
BASE_TREE=${BASE_TREE:-/tmp/wifi-5ghz-ready-build}
BASE_ART=${BASE_ART:-/tmp/wifi-5ghz-ready-artifact/hi5622_wifi.ko}
BASE_SHA=1336615135113113d7dd57ba81f9433ac6bb0332482be019adbe242464892d53

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
[ -d "$KDIR" ] || { echo "vapmsg5g: 6.18.60 kernel tree not found" >&2; exit 2; }
[ -n "$STAGING_DIR" ] || { echo "vapmsg5g: staging dir not found" >&2; exit 2; }
CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS}gcc" ] || { echo "vapmsg5g: cross gcc missing" >&2; exit 2; }
[ -f "$P" ] || { echo "vapmsg5g: missing patch $P" >&2; exit 2; }

if [ ! -d "$BASE_TREE" ]; then
	echo "== 0. build the 5 GHz base (wifi-5ghz-bringup lane)"
	"$DOCS/wifi-5ghz-bringup/tools/build-5ghz-ready.sh"
fi
[ -d "$BASE_TREE" ] || { echo "vapmsg5g: base tree missing: $BASE_TREE" >&2; exit 2; }

# Pin the base module (the exact artifact the bench has fired).
if [ -f "$BASE_ART" ]; then
	BS=$(sha256sum "$BASE_ART" | cut -d' ' -f1)
	[ "$BS" = "$BASE_SHA" ] || {
		echo "vapmsg5g: base artifact sha $BS != pinned $BASE_SHA" >&2
		echo "vapmsg5g: the 5 GHz chain drifted - re-review the base lane first" >&2
		exit 1
	}
	echo "== base artifact pinned: ${BASE_SHA%??????????????????????}..."
fi

rm -rf "$WORK" "$ART"
mkdir -p "$ART"
cp -a "$BASE_TREE/." "$WORK/"
( cd "$WORK" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order .modules.order.cmd build.log 2>/dev/null ) || true
echo "== 1. base tree staged: $(ls "$WORK" | wc -l) files"

echo "== 2. apply the vap5g order patch"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$P" >/dev/null ) || {
	echo "vapmsg5g: patch does not apply to the 5 GHz base" >&2
	exit 1
}
if grep -rln '^<<<<<<<' "$WORK" >/dev/null 2>&1; then
	echo "vapmsg5g: conflict markers survived - refusing build" >&2
	exit 1
fi

# The portable helper is host-tested from src/; it must be byte-identical
# to the patched driver source (refuse source drift).
for f in hi5622_vapmsg5g.c hi5622_vapmsg5g.h; do
	cmp -s "$LANE/src/$f" "$WORK/$f" || {
		echo "vapmsg5g: src/$f drifted from the patched tree" >&2
		exit 1
	}
done
echo "== 3. src/ matches the patched driver sources"

export STAGING_DIR
( cd "$WORK" && make -s ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 ) || true
make ARCH=arm CROSS_COMPILE="$CROSS" -C "$KDIR" M="$WORK" modules > "$WORK/build.log" 2>&1 || {
	echo "vapmsg5g: BUILD FAILED" >&2
	tail -40 "$WORK/build.log" >&2
	exit 1
}
if grep -iE 'warning:|error:|undefined!' "$WORK/build.log"; then
	echo "vapmsg5g: warnings/errors/undefined symbols - refusing artifact" >&2
	exit 1
fi
echo "== 4. module build clean (0 warnings/errors)"

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "vapmsg5g: no .ko" >&2; exit 1; }
"${CROSS}strip" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"${CROSS}objcopy" -O binary --only-section=.text "$KO" "$WORK/.t1"
"${CROSS}objcopy" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.t2"
cmp -s "$WORK/.t1" "$WORK/.t2" || { echo "vapmsg5g: strip changed .text" >&2; exit 1; }
echo "== 5. debug-strip keeps .text identical"
cp "$KO" "$ART/hi5622_wifi.ko.unstripped"
cp "$WORK/build.log" "$ART/build.log"

# ---- contract checks on the artifact ---------------------------------
MI=$(modinfo -p "$ART/hi5622_wifi.ko")
printf '%s\n' "$MI" | grep -q '^vap5g:' || { echo "vapmsg5g: missing module param vap5g" >&2; exit 1; }
NP=$(printf '%s\n' "$MI" | wc -l)
[ "$NP" = "58" ] || { echo "vapmsg5g: param count $NP != 58 (57 base + vap5g)" >&2; exit 1; }
for s in "user_add (g0 id7/id8) DEFERRED" \
	 "start_ap vendor order: posting the DEFERRED user_add" \
	 "deferred to start_ap (vap5g=1 vendor order" \
	 "known vap5g=%u (%d), fail closed" \
	 "no structural user_add (latch already set)"; do
	strings -a "$ART/hi5622_wifi.ko" | grep -qF "$s" || {
		echo "vapmsg5g: feature string missing: $s" >&2
		exit 1
	}
done
SV=$(wc -l < "$WORK/cfg80211-provider.symvers")
[ "$SV" = "12" ] || { echo "vapmsg5g: cfg80211 symvers != 12 lines" >&2; exit 1; }
echo "== 6. artifact contract PASS (58 params incl vap5g, feature strings, 12 symvers)"

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$KO")
TEXT_SHA=$(sha256sum "$WORK/.t1" | cut -d' ' -f1)
VERMAGIC=$("${CROSS}strings" "$KO" | grep -m1 '^vermagic=')
echo "== 5 GHz AP module with the vap5g vendor-order patch (host-built, NOT fired)"
echo "artifact  : $ART/hi5622_wifi.ko (debug-stripped)"
echo "size      : $SIZE B"
echo "sha256    : $SHA"
echo "unstripped: $USIZE B"
echo ".text sha : $TEXT_SHA"
echo "$VERMAGIC"
echo "params    : $NP (57 ready module + vap5g)"
echo "chain     : 5ghz-ready (assoc + band5g) + vap5g order patch"
echo "vap5g     : default 0 (fail closed; 1 = vendor order, >1 refused)"
