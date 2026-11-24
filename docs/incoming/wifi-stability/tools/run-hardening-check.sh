#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# tools/run-hardening-check.sh - host checks for the stability hardening patch.
#
#   1. compile+run the pure policy-gate host test (hardening_selftest.c)
#   2. static patch checks (expected hunks/anchor strings present)
#   3. patch-apply dry run on the polished image source tree
#   4. optional --with-build: reproduce fired base (44c6cc12) + polished module
#      (d145a205) + build the hardened module - needs the 6.18.60 tree, minutes
#
# HOST-ONLY: no device, no image, no flash, no vendor bytes.
set -eu
LANE_DIR=$(cd "$(dirname "$0")/.." && pwd)
DOCS=$(cd "$LANE_DIR/.." && pwd)
PATCH=$LANE_DIR/patches/0001-stability-hardening.patch
HDR=$LANE_DIR/patches/files/hi5622_stability.h
OUT=/tmp/wifi-stability-hostcheck
rm -rf "$OUT"
mkdir -p "$OUT"
fail=0

echo "== host policy gate test"
cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 \
   -DHI5622_STABILITY_HOST -I"$LANE_DIR/patches/files" \
   -o "$OUT/hardening_selftest" "$LANE_DIR/tools/hardening_selftest.c" || {
	echo "HOSTCHECK compile FAIL" >&2; exit 1; }
"$OUT/hardening_selftest" || fail=1

echo "== patch static checks"
[ -f "$PATCH" ] || { echo "missing $PATCH" >&2; exit 1; }
[ -f "$HDR" ] || { echo "missing $HDR" >&2; exit 1; }
files=$(grep -c '^+++ b/' "$PATCH")
[ "$files" = "10" ] || { echo "PATCH-FILES want 10 got $files" >&2; fail=1; }
for anchor in \
	"hi5622_transport_reload_refused" \
	"chip_booted_at_probe" \
	"fw_vap_deleted" \
	"hi5622_scan_vap_object_valid" \
	"HI5622_STAB_MODULE_TABLE_SLOT0_CPU" \
	"VAP-base/AP latches cleared" \
	"controlled stop_ap" \
	"stop_ap_mode" \
	"hcc_trace" \
	"hcc_kernel_event_hot" \
	"stale/dead VAP" \
	"HI5622_STAB_AP_BASE_WRONG_IFTYPE" \
	"scan REFUSED" \
	"start_ap REFUSED" \
	"hi5622_cfg80211_remove(ctx)" ; do
	if ! grep -qF "$anchor" "$PATCH"; then
		echo "PATCH-ANCHOR missing: $anchor" >&2
		fail=1
	fi
done
if ! grep -q '^+++ b/hi5622_stability.h' "$PATCH"; then
	echo "PATCH-HEADER missing new header" >&2; fail=1
fi
if grep -q '^-static bool hi5622_vap_base_posted;' "$PATCH"; then :; else
	echo "PATCH-LATCH missing module-global latch removal" >&2; fail=1
fi
# the patch must not re-add the polish lane's mgmt_diag (it is already there)
if grep -q '^+unsigned int hi5622_mgmt_diag;' "$PATCH"; then
	echo "PATCH-DUPLICATE mgmt_diag re-added (image-polish already owns it)" >&2
	fail=1
fi

echo "== patch apply check (dp-tuning + polished image source tree)"
WORK=/tmp/wifi-stability-patchcheck
rm -rf "$WORK"; mkdir -p "$WORK"
cp -a "$DOCS/wifi-dp-air/src/." "$WORK/"
cp "$DOCS/wifi-ap-start/src/hi5622_ap_frames.h" \
   "$DOCS/wifi-ap-start/src/hi5622_ap_frames.c" "$WORK/"
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < \
	"$DOCS/wifi-ap-start/provider/apstart-provider.patch" >/dev/null )
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < \
	"$DOCS/wifi-dp-tuning/patches/0001-txq-consumer-tuning.patch" >/dev/null )
( cd "$WORK" && patch -p1 --no-backup-if-mismatch < \
	"$DOCS/wifi-image-polish/patches/0001-polish-module-defaults-and-mgmt-diag.patch" >/dev/null )
( cd "$WORK" && patch -p1 --dry-run --no-backup-if-mismatch < "$PATCH" >/dev/null ) \
	|| { echo "PATCH-APPLY FAILED" >&2; fail=1; }
echo "PATCH-APPLY OK"

if [ "${1:-}" = "--with-build" ]; then
	echo "== reproduce fired base + polished + hardened module"
	"$LANE_DIR/tools/build-hardened-module.sh" || fail=1
fi

if [ "$fail" = "0" ]; then
	echo "HOSTCHECK PASS"
else
	echo "HOSTCHECK FAIL" >&2
	exit 1
fi
