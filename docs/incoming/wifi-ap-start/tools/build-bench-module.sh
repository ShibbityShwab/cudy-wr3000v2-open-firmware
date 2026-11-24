#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/build-bench-module.sh - build the deployable AP-start bench module and
# print its identity (host-only: no image build, no device contact).
#
# BASE = the hardware-fired provider tree opensource/docs/incoming/
# wifi-scan-complete/src (module 748,888 B sha256 b42ff4ec..., the D2 TRUE PASS
# provider; it already contains the wifi-vap-start provider integration) plus
# THIS lane's builder library and provider patch (AP-start sequence, hostapd
# capability gates, corrected SET_INTERFACE handling).
#
# The .ko is LOCAL EVIDENCE only and is NOT committed; copy it to the bench
# router with tools/rscp.sh.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
# v3: the current hardware-fired base, NOT the older wifi-scan-ap tree.
KO_SRC=$DOCS/wifi-scan-complete/src
PATCH=$LANE_DIR/provider/apstart-provider.patch
WORK=/tmp/wifi-ap-start-bench
ART=/tmp/wifi-ap-start-bench-artifact

KDIR=
STAGING_DIR=
set +u
if [ -n "$KDIR_OVERRIDE" ]; then KDIR=$KDIR_OVERRIDE; fi
if [ -n "$STAGING_OVERRIDE" ]; then STAGING_DIR=$STAGING_OVERRIDE; fi
set -u
if [ -z "$KDIR" ]; then
	for root in /home/shibbityshwab/openwrt-lab-upstream-target \
		    /home/shibbityshwab/openwrt-lab-selectorfix-img; do
		c=$root/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
		if [ -d "$c" ]; then
			KDIR=$c
			STAGING_DIR=$root/staging_dir
			break
		fi
	done
fi
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-

if [ -z "$KDIR" ]; then echo "build-bench: kernel tree not found" >&2; exit 2; fi
[ -d "$KO_SRC" ] || { echo "build-bench: missing base tree $KO_SRC" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "build-bench: missing $PATCH" >&2; exit 2; }

rm -rf "$WORK" "$ART"
mkdir -p "$WORK" "$ART"
cp -a "$KO_SRC/." "$WORK/"
# Fresh generated artifacts only; keep cfg80211-provider.symvers (Kbuild needs
# it for MODPOST) and the checked-in sources.
rm -f "$WORK"/*.o "$WORK"/*.ko "$WORK"/*.mod "$WORK"/*.mod.c "$WORK"/.*.cmd "$WORK"/Module.symvers
cp "$LANE_DIR/src/hi5622_ap_frames.h" "$LANE_DIR/src/hi5622_ap_frames.c" "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" )

# Prove the H2D message path is the hardware-fired one: these files must be
# byte-identical between the base tree and the build tree after patching.
H2D_UNCHANGED=1
for f in h2d_send.c h2d_send_logic.c h2d_send.h h2d_send_logic.h \
	 hcc.c hcc_core.c hcc_core.h h2d_frames.c h2d_frames.h; do
	if ! cmp -s "$KO_SRC/$f" "$WORK/$f"; then
		echo "build-bench: WARNING $f differs from the hardware-fired base" >&2
		H2D_UNCHANGED=0
	fi
done
if [ "$H2D_UNCHANGED" = "1" ]; then
	echo "== H2D/core/provider files byte-identical to the 748,888 B fired base"
else
	echo "== H2D files changed - inspect before deploying" >&2
fi

export STAGING_DIR
make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 || true
make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules \
     2>&1 | tee "$WORK/build.log" >/dev/null

KO=$WORK/hi5622_wifi.ko
[ -f "$KO" ] || { echo "build-bench: BUILD FAILED" >&2; tail -30 "$WORK/build.log" >&2; exit 1; }
if grep -iE 'warning:|error:' "$WORK/build.log"; then
	echo "build-bench: warnings/errors found - refusing artifact" >&2
	exit 1
fi

# Deployable artifact: debug-stripped so the sha is path-independent.  The
# load-relevant sections must be identical to the unstripped build.
STRIP="$CROSS_COMPILE"strip
OBJCOPY="$CROSS_COMPILE"objcopy
"$STRIP" --strip-debug -o "$ART/hi5622_wifi.ko" "$KO"
"$OBJCOPY" -O binary --only-section=.text "$KO" "$WORK/.text.unstripped"
"$OBJCOPY" -O binary --only-section=.text "$ART/hi5622_wifi.ko" "$WORK/.text.stripped"
if ! cmp -s "$WORK/.text.unstripped" "$WORK/.text.stripped"; then
	echo "build-bench: STRIP CHANGED .text - refusing artifact" >&2
	exit 1
fi
echo "== strip check: .text byte-identical stripped vs unstripped"

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
VERMAGIC=$("$CROSS_COMPILE"strings "$KO" | grep -m1 '^vermagic=')
KO_ART=$ART/hi5622_wifi.ko
echo "== AP-start bench module (v3, fired base)"
echo "base     : wifi-scan-complete/src (748,888 B b42ff4ec fired tree)"
echo "artifact : $KO_ART (debug-stripped, path-independent)"
echo "size     : $SIZE"
echo "sha256   : $SHA"
echo "$VERMAGIC"
echo "ap builder : $("$CROSS_COMPILE"nm "$KO_ART" | grep -c ' vapscan_ap_start') symbol(s)"
echo "connect gate: $("$CROSS_COMPILE"nm "$KO_ART" | grep -c ' hi5622_cfg11_connect') symbol(s)"
echo "haf_ syms : $("$CROSS_COMPILE"nm "$KO_ART" | grep -cE ' [Tt] haf_')"
echo "depends    : $(strings -a "$KO_ART" | grep -m1 '^depends=' || true)"
echo "note: .ko is local evidence only (never committed); copy with tools/rscp.sh"
