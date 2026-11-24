#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# build-polished-module.sh - host-only compile verification + artifact build
# for the polished hi5622_wifi.ko (wifi-image-polish lane, task-135).
#
# It reproduces the standard combined module FIRST and asserts its identity is
# the hardware-validated artifact (242,572 B sha256 44c6cc12...), then applies
# this lane's polish patch and builds the polished module.  That isolates the
# delta and makes the build a real ABI/identity check, not just "it compiles":
#
#   stack 1 (proven):  docs/incoming/wifi-dp-air/src
#                      + docs/incoming/wifi-ap-start/provider/apstart-provider.patch
#                      + docs/incoming/wifi-ap-start/src/hi5622_ap_frames.{h,c}
#                      -> hi5622_wifi.ko stripped = 44c6cc12...  (asserted)
#   stack 2 (polish):  + patches/0001-polish-module-defaults-and-mgmt-diag.patch
#                      -> polished hi5622_wifi.ko (20 parms: adds mgmt_diag)
#
# The polished module is HOST-BUILT and NOT hardware-fired.  Deploying it
# needs one attended fire; the zero-risk path is the validated 44c6cc12
# artifact loaded by hi5622-load with explicit parameters (see INSTALL.md).
#
# Host-only: no image build, no device contact, no network.  The .ko is local
# evidence only (never committed; this lane's .gitignore excludes it).
#
# usage: ./tools/build-polished-module.sh [--keep-work]
#   env overrides: KDIR_OVERRIDE, STAGING_OVERRIDE, WORK, ART
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
DP_SRC=$DOCS/wifi-dp-air/src
AP_LANE=$DOCS/wifi-ap-start
PATCH=$LANE_DIR/patches/0001-polish-module-defaults-and-mgmt-diag.patch
# Stack 1 MUST be built in the canonical workdir used for the hardware-fired
# artifact: the toolchain's build-id hashes the debug info, which embeds the
# absolute build path, so a different directory changes only the 20-byte
# build-id (verified: byte-identical elsewhere).  Stack 2 is a new artifact;
# its recorded identity assumes this script's default workdir.
STACK1_WORK=${STACK1_WORK:-/tmp/wifi-combined-build}
PWORK=${PWORK:-/tmp/wifi-image-polish-build}
WORK=
ART=${ART:-/tmp/wifi-image-polish-artifact}
KEEP=0
[ "${1:-}" = "--keep-work" ] && KEEP=1

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
		if [ -d "$c" ]; then
			KDIR=$c
			STAGING_DIR=$root/staging_dir
			break
		fi
	done
fi
[ -d "$KDIR" ] || { echo "build-polished: 6.18.60 kernel tree not found (set KDIR_OVERRIDE)" >&2; exit 2; }
CROSS_COMPILE=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-
[ -x "${CROSS_COMPILE}gcc" ] || { echo "build-polished: toolchain not found" >&2; exit 2; }
[ -d "$DP_SRC" ] || { echo "build-polished: missing base tree $DP_SRC" >&2; exit 2; }
[ -f "$AP_LANE/provider/apstart-provider.patch" ] || { echo "build-polished: missing ap-start provider patch" >&2; exit 2; }
[ -f "$PATCH" ] || { echo "build-polished: missing $PATCH" >&2; exit 2; }
STRIP="${CROSS_COMPILE}strip"
OBJCOPY="${CROSS_COMPILE}objcopy"
NM="${CROSS_COMPILE}nm"
export STAGING_DIR

build_module() { # $1 = log file
	make -s ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" clean >/dev/null 2>&1 || true
	make ARCH=arm CROSS_COMPILE="$CROSS_COMPILE" -C "$KDIR" M="$WORK" modules >"$1" 2>&1 || {
		echo "build-polished: BUILD FAILED (see $1)" >&2
		tail -40 "$1" >&2
		exit 1
	}
	if grep -iE 'warning:|error:' "$1"; then
		echo "build-polished: warnings/errors found - refusing artifact" >&2
		exit 1
	fi
	[ -f "$WORK/hi5622_wifi.ko" ] || { echo "build-polished: no .ko produced" >&2; exit 1; }
}

strip_artifact() { # $1 = output path
	"$STRIP" --strip-debug -o "$1" "$WORK/hi5622_wifi.ko"
	"$OBJCOPY" -O binary --only-section=.text "$WORK/hi5622_wifi.ko" "$WORK/.text-unstripped"
	"$OBJCOPY" -O binary --only-section=.text "$1" "$WORK/.text-stripped"
	if ! cmp -s "$WORK/.text-unstripped" "$WORK/.text-stripped"; then
		echo "build-polished: strip changed .text - refusing artifact" >&2
		exit 1
	fi
}

parm_list() {
	if command -v modinfo >/dev/null 2>&1; then
		modinfo "$1" 2>/dev/null | sed -n 's/^parm:[[:space:]]*\([a-z0-9_]*\):.*/\1/p' | sort
	else
		strings -a "$1" | sed -n 's/^parm=\([a-z0-9_]*\):.*/\1/p' | sort
	fi
}

# ---------------------------------------------------------------------------
# stage stack 1: the proven combined module
# ---------------------------------------------------------------------------
rm -rf "$STACK1_WORK" "$PWORK" "$ART"
mkdir -p "$STACK1_WORK" "$ART"
cp -a "$DP_SRC/." "$STACK1_WORK/"
rm -f "$STACK1_WORK"/*.o "$STACK1_WORK"/*.ko "$STACK1_WORK"/*.mod "$STACK1_WORK"/*.mod.c "$STACK1_WORK"/.*.cmd "$STACK1_WORK"/Module.symvers
cp "$AP_LANE/src/hi5622_ap_frames.h" "$AP_LANE/src/hi5622_ap_frames.c" "$STACK1_WORK/"
( cd "$STACK1_WORK" && patch -p1 --no-backup-if-mismatch < "$AP_LANE/provider/apstart-provider.patch" ) \
	>"$ART/stack1-patch.log" 2>&1 || { echo "build-polished: ap-start patch failed" >&2; exit 1; }

echo "== stack 1: proven combined module (asserts the hardware-validated identity)"
WORK="$STACK1_WORK"
build_module "$WORK/build-stack1.log"
strip_artifact "$ART/hi5622_wifi.base.ko"
BASE_SIZE=$(stat -c %s "$ART/hi5622_wifi.base.ko")
BASE_SHA=$(sha256sum "$ART/hi5622_wifi.base.ko" | cut -d' ' -f1)
echo "  size : $BASE_SIZE"
echo "  sha256: $BASE_SHA"
if [ "$BASE_SIZE" != "242572" ] || [ "$BASE_SHA" != "44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b" ]; then
	echo "build-polished: STACK-1 IDENTITY MISMATCH - expected 242572 B sha 44c6cc12..." >&2
	echo "build-polished: the tree/toolchain does not reproduce the fired module; not a valid compile check" >&2
	exit 1
fi
BASE_PARMS=$(parm_list "$ART/hi5622_wifi.base.ko")
echo "  parms: $(printf '%s\n' "$BASE_PARMS" | wc -l)"

# ---------------------------------------------------------------------------
# stage stack 2: the polish delta (fresh workdir, same source tree)
# ---------------------------------------------------------------------------
echo
echo "== stack 2: polish patch (mgmt_diag default 0 + image defaults)"
WORK=$PWORK
mkdir -p "$WORK"
cp -a "$STACK1_WORK/." "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < "$PATCH" ) \
	>"$ART/stack2-patch.log" 2>&1 || { echo "build-polished: polish patch failed" >&2; exit 1; }

echo "  -- source default assertions (staged tree) --"
assert_line() { # regex description
	if ! grep -q "$1" "$WORK/$2"; then
		echo "build-polished: missing expected source: $1 in $2" >&2
		exit 1
	fi
	echo "  ok $2: $3"
}
assert_line '^#define HI5622_FW_PATH_DEFAULT.*"hi_wifi/FIRMWARE.PD4.bin"' hi5622_contract.h 'fw_path default = hi_wifi/FIRMWARE.PD4.bin'
assert_line '^unsigned int hi5622_allow_frames = 16u;' hi5622_frame_post.c 'allow_frames default = 16'
assert_line '^unsigned int hi5622_frame_gap_ms = 100u;' hi5622_frame_post.c 'frame_gap_ms default = 100'
assert_line '^unsigned int hi5622_h2d_send_budget = 4;' h2d_send.c 'h2d_send_budget default = 4'
assert_line '^unsigned int hi5622_vap_start_first = 1u;' hi5622_vapscan_provider.c 'vap_start_first default = 1'
assert_line '^unsigned int hi5622_vap_start_mode = 1u;' hi5622_vapscan_provider.c 'vap_start_mode default = 1'
assert_line '^static unsigned int hi5622_scan_dwell_ms = 60u;' hi5622_scan.c 'scan_dwell_ms default = 60'
assert_line '^unsigned int hi5622_mgmt_diag;' hi5622_cfg80211_ops.c 'mgmt_diag param declared'
assert_line 'if (hi5622_mgmt_diag)' hi5622_cfg80211_ops.c 'dump_stack gated on mgmt_diag'

build_module "$WORK/build-stack2.log"
strip_artifact "$ART/hi5622_wifi.ko"
cp "$WORK/hi5622_wifi.ko" "$ART/hi5622_wifi.ko.unstripped"

SIZE=$(stat -c %s "$ART/hi5622_wifi.ko")
SHA=$(sha256sum "$ART/hi5622_wifi.ko" | cut -d' ' -f1)
USIZE=$(stat -c %s "$ART/hi5622_wifi.ko.unstripped")
USHA=$(sha256sum "$ART/hi5622_wifi.ko.unstripped" | cut -d' ' -f1)
VERMAGIC=$(strings -a "$ART/hi5622_wifi.ko" | grep -m1 '^vermagic=')
DEPENDS=$(strings -a "$ART/hi5622_wifi.ko" | grep -m1 '^depends=')
POLISHED_PARMS=$(parm_list "$ART/hi5622_wifi.ko")
echo "  parms: $(printf '%s\n' "$POLISHED_PARMS" | wc -l)"

# contract checks on the polished artifact
for p in mgmt_diag fw_path allow_frames frame_gap_ms h2d_send_budget vap_start_first vap_start_mode scan_dwell_ms; do
	printf '%s\n' "$POLISHED_PARMS" | grep -qx "$p" || { echo "build-polished: missing parm $p" >&2; exit 1; }
done
printf '%s\n' "$BASE_PARMS" | grep -qx mgmt_diag && { echo "build-polished: base artifact unexpectedly has mgmt_diag" >&2; exit 1; }
"$NM" "$ART/hi5622_wifi.ko" | grep -qE ' [Tt] vapscan_ap_start' || { echo "build-polished: missing vapscan_ap_start" >&2; exit 1; }
"$NM" "$ART/hi5622_wifi.ko" | grep -qE ' [Tt] hi5622_dp_txrx' || { echo "build-polished: missing data-plane symbols" >&2; exit 1; }

{
	echo "# generated by tools/build-polished-module.sh - host-only compile evidence"
	echo "# input: docs/incoming/wifi-dp-air/src + wifi-ap-start provider patch/AP frames"
	echo "#        + patches/0001-polish-module-defaults-and-mgmt-diag.patch"
	echo "STACK1_SIZE=$BASE_SIZE"
	echo "STACK1_SHA256=$BASE_SHA"
	echo "POLISHED_SIZE=$SIZE"
	echo "POLISHED_SHA256=$SHA"
	echo "POLISHED_UNSTRIPPED_SIZE=$USIZE"
	echo "POLISHED_UNSTRIPPED_SHA256=$USHA"
	# quoted/one-line values so selftest.sh can source this file in POSIX sh
	echo "POLISHED_VERMAGIC='$VERMAGIC'"
	echo "POLISHED_DEPENDS='$DEPENDS'"
	echo "POLISHED_PARM_COUNT=$(printf '%s\n' "$POLISHED_PARMS" | wc -l)"
	echo "STACK1_PARM_COUNT=$(printf '%s\n' "$BASE_PARMS" | wc -l)"
	echo "POLISHED_PARMS='$(printf '%s ' $POLISHED_PARMS)'"
} >"$ART/artifact.env"

echo
echo "== result"
echo "stack1 (proven, hardware-fired): $BASE_SIZE B sha256 $BASE_SHA"
echo "stack2 (polished, NOT fired)   : $SIZE B sha256 $SHA"
echo "unstripped reference           : $USIZE B sha256 $USHA"
echo "vermagic                       : $VERMAGIC"
echo "depends                        : $DEPENDS"
echo "artifact                       : $ART/hi5622_wifi.ko (local only, never committed)"
echo "metadata                       : $ART/artifact.env"
[ "$KEEP" -eq 1 ] || echo "work tree kept at $WORK (re-run with --keep-work is the same)"
