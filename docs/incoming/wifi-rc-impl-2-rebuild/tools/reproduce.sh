#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/reproduce.sh - rebuild the wifi-rc-impl-2 module from committed
# artifacts only (lane wifi-rc-impl-2-rebuild).  HOST-ONLY: no device access,
# no UART, no firmware write, no image, no flash.  The '.ko' is local evidence
# only and is never committed.
#
# What it does:
#   1. rebuild the pinned builder chain up to the dfs-mode tree.  Every
#      builder is committed and asserts its own artifact identity; the deepest
#      one (wifi-dfs-mode-send) recursively recreates its predecessors.
#   2. stage the clean IMAGE #3 merged tree: dfs-mode tree + the four recorded
#      chains (field-B -> F2, T4 watermark, BA skeleton, HE caps) and assert
#      the pinned source manifest.
#   3. run ../wifi-rc-impl-2/tools/build-rc2.sh against it and assert every
#      recorded identity (patch, 14 files, module/.text/activity hashes,
#      67 params, host suites 37/25/76).
#   4. stage the expected artifact in /tmp/rc2-rebuilt/.
#
# Usage:
#   ./tools/reproduce.sh
#   ./tools/reproduce.sh --with-image3-check   # + byte-exact IMAGE #3 base module
set -eu

LANE=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE/.." && pwd)
I3=$DOCS/wifi-image-3-module-artifacts
BASE=/tmp/wifi-image-3-module          # the lane's canonical base path
OUT=/tmp/rc2-rebuilt
WITH_I3=0
for a in "$@"; do
	[ "$a" = "--with-image3-check" ] && WITH_I3=1
done

# --- pins (all recorded in the two lanes' evidence) --------------------------
BASE_MANIFEST=2d87525e1bab6bbf94486244b55fd1396e2ad691645d40c8469d247ffd766c45
RC2_PATCH_SHA=67083c2d31c19d9f8e4e7c0260a8d618d60ec4bf93015421871c116eec2c16bb
RC2_MODULE_SHA=ca22773a861db31b524edd69f0c5522b03862207191b1d8b83ba302fc0c97d8b
RC2_MODULE_SIZE=1519716
RC2_TEXT_SHA=ba81e4b38b4a965143b7a7689c9639525e9af9852e05c968c9da65ff42ab7839
RC2_ACT_SHA=3d43a3cd261d28245e47a964161a0d605caae97a70baa81bea1eee630e294536
RC2_ACT_SIZE=401884
I3_UNSTRIPPED_SHA=88d6537228baee7e22f4587c1a6d957a1f8940df9f278c4bbbebc2673d4e1ef8
I3_ACTIVITY_SHA=545192dfdd9110d0439418b159f4fc6b56af09e295a56bf179b4838b1d713b99
I3_TEXT_SHA=494ccd42100e3e3792738bb0a94419c7e549b86a11812ac505a7b1df2b18f2a1

die() { echo "reproduce: $*" >&2; exit 1; }
sha() { sha256sum "$1" | cut -d' ' -f1; }
size() { stat -c %s "$1"; }

# --- 1. the pinned builder chain -------------------------------------------
echo "== 1. rebuild the pinned chain up to the dfs-mode tree (~10 min)"
"$DOCS/wifi-dfs-mode-send/tools/build-dfs-mode-send.sh"
[ -d /tmp/dfsmode-build ] || die "no dfs-mode tree after the chain build"

# --- 2. stage the clean IMAGE #3 merged tree -------------------------------
echo "== 2. stage the clean IMAGE #3 tree at $BASE"
rm -rf "$BASE"
mkdir -p "$BASE"
python3 - "$BASE" <<'PY'
import os, shutil, sys
src, dst = "/tmp/dfsmode-build", sys.argv[1]
ART = (".o", ".ko", ".mod", ".mod.c", "build.log")
NAMES = ("Module.symvers", "modules.order", ".modules.order.cmd")
n = 0
for name in sorted(os.listdir(src)):
    p = os.path.join(src, name)
    if not os.path.isfile(p) or name.startswith("."):
        continue
    if name.endswith(ART) or name in NAMES:
        continue
    shutil.copyfile(p, os.path.join(dst, name)); n += 1
print("   staged %d source files" % n)
PY

apply() { # $1 = human name, $2 = patch path
	echo "   patch: $1"
	( cd "$BASE" && patch -p1 --no-backup-if-mismatch < "$2" ) >/dev/null \
		|| die "patch failed: $1"
	if grep -rln '^<<<<<<<' "$BASE" >/dev/null 2>&1; then
		die "conflict markers survived: $1"
	fi
}
apply "F2 field-B fix"     "$DOCS/wifi-fw-dfs-worker/patches/0001-fieldb-context.patch"
apply "T4 RX watermark"    "$DOCS/wifi-rx-watermark/patches/0001-rx-refill-watermark.patch"
echo "   patch: BA skeleton (Kbuild hunk offset +2 is expected)"
( cd "$BASE" && patch -p1 --no-backup-if-mismatch \
	< "$DOCS/wifi-ba-agg/patches/0001-ba-skeleton.patch" ) >/dev/null \
	|| die "BA skeleton patch failed"
echo "   patch: HE caps (cfg80211 clean; the Kbuild hunk FAILS as recorded)"
if ( cd "$BASE" && patch -p1 --no-backup-if-mismatch \
	< "$DOCS/wifi-ax-he-caps/patches/0001-he-caps.patch" ) >/dev/null 2>&1; then
	die "HE Kbuild hunk applied wholesale - the F2 tree is wrong"
fi
rm -f "$BASE/Kbuild.rej"
python3 - "$BASE/Kbuild" <<'PY'
import sys
p = sys.argv[1]
s = open(p).read()
old = "\t\t hi5622_desc_table.o hi5622_dfs_mode.o \\\n"
new = "\t\t hi5622_desc_table.o hi5622_dfs_mode.o hi5622_he_caps.o \\\n"
assert s.count(old) == 1, "Kbuild: expected exactly one dfs_mode line"
open(p, "w").write(s.replace(old, new))
PY
cp "$DOCS/wifi-ax-he-caps/src/hi5622_he_caps.c" \
   "$DOCS/wifi-ax-he-caps/src/hi5622_he_caps.h" "$BASE/"
N=$(ls "$BASE" | wc -l)
[ "$N" = "97" ] || die "IMAGE #3 tree has $N files, expected 97"
M=$( cd "$BASE" && python3 "$DOCS/wifi-rc-impl-2/tools/make-patch.py" \
	--manifest --base . | sha256sum | cut -d' ' -f1 )
[ "$M" = "$BASE_MANIFEST" ] || die "source manifest $M != pinned $BASE_MANIFEST"
echo "   IMAGE #3 source manifest OK ($M)"

# --- 2b. optional byte-exact IMAGE #3 base-module check --------------------
if [ "$WITH_I3" = 1 ]; then
	echo "== 2b. byte-exact IMAGE #3 base module check"
	[ -d "$I3" ] || die "pinned IMAGE #3 artifacts absent: $I3"
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
	[ -d "${KDIR:-}" ] || die "6.18.60 kernel tree not found"
	CROSS=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
	[ -x "${CROSS}gcc" ] || die "cross gcc missing"
	export STAGING_DIR
	( cd "$BASE" && make -s ARCH=arm CROSS_COMPILE="$CROSS" \
		-C "$KDIR" M="$BASE" modules )
	[ "$(sha "$BASE/hi5622_wifi.ko")" = "$I3_UNSTRIPPED_SHA" ] \
		|| die "IMAGE #3 unstripped module does not reproduce"
	"${CROSS}strip" --strip-debug -o /tmp/i3-check.activity.ko "$BASE/hi5622_wifi.ko"
	[ "$(sha /tmp/i3-check.activity.ko)" = "$I3_ACTIVITY_SHA" ] \
		|| die "IMAGE #3 activity module does not reproduce"
	"${CROSS}objcopy" -O binary --only-section=.text "$BASE/hi5622_wifi.ko" /tmp/i3-check.text
	[ "$(sha /tmp/i3-check.text)" = "$I3_TEXT_SHA" ] \
		|| die "IMAGE #3 .text does not reproduce"
	echo "   IMAGE #3 base module reproduces byte-for-byte (unstripped/activity/.text)"
	( cd "$BASE" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers \
		modules.order .modules.order.cmd )
fi

# --- 3. the RC-2 build on that base ----------------------------------------
echo "== 3. RC-2 build (../wifi-rc-impl-2/tools/build-rc2.sh)"
( cd "$DOCS/wifi-rc-impl-2" && BASE_TREE="$BASE" ./tools/build-rc2.sh )
A=/tmp/rc2-artifact
[ "$(sha "$DOCS/wifi-rc-impl-2/patches/0001-rate-ctl-2.patch")" = "$RC2_PATCH_SHA" ] \
	|| die "RC-2 patch sha drifted"
[ "$(sha "$A/hi5622_wifi.ko")" = "$RC2_MODULE_SHA" ] \
	|| die "unstripped module sha drifted"
[ "$(size "$A/hi5622_wifi.ko")" = "$RC2_MODULE_SIZE" ] \
	|| die "unstripped module size drifted"
[ "$(sha "$A/hi5622_wifi.activity.ko")" = "$RC2_ACT_SHA" ] \
	|| die "activity module sha drifted"
[ "$(size "$A/hi5622_wifi.activity.ko")" = "$RC2_ACT_SIZE" ] \
	|| die "activity module size drifted"
"${CROSS}objcopy" -O binary --only-section=.text "$A/hi5622_wifi.ko" /tmp/rc2-check.text
[ "$(sha /tmp/rc2-check.text)" = "$RC2_TEXT_SHA" ] || die "module .text drifted"
echo "   RC-2 identities OK (module $RC2_MODULE_SIZE B, activity $RC2_ACT_SIZE B)"

# --- 4. stage the expected artifact ---------------------------------------
echo "== 4. stage the expected artifact in $OUT"
mkdir -p "$OUT"
cp -f "$A/hi5622_wifi.activity.ko" "$OUT/hi5622_wifi.ko"
cp -f "$A/hi5622_wifi.ko" "$OUT/hi5622_wifi.unstripped.ko"
cp -f "$A/build.log" "$OUT/build.log"
sha256sum "$OUT/hi5622_wifi.ko" "$OUT/hi5622_wifi.unstripped.ko"
echo "== PASS - the rebuilt module is byte-identical to the recorded RC-2 artifact"
