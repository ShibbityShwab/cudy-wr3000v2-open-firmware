#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# verify-association-module.sh - host-side identity/provenance check for the
# CORRECTED association module (the one the antenna-test fire must load).
#
# HOST-ONLY: never contacts the bench router. Read-only: it inspects a local
# .ko artifact and, when the source trees are present, proves that
#   final association tree + ../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
# reproduces the exact five patched files.
#
# Expected artifact (the hardware-accepted corrected carrier):
#   hi5622_wifi.stripped.ko  344,372 B
#   sha256 ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e
#   56 module params, vermagic 6.18.60
#
# The LANE does not fire anything: this script is run by the Lead on the host
# BEFORE copying the module to the router. If the /tmp artifact is gone, use
# --rebuild to print the exact rebuild recipe (a full build needs the OpenWrt
# staging dir; canonical builders are wifi-stability/tools/build-association-
# module.sh plus the sibling carrier patch).
#
# Usage:
#   ./verify-association-module.sh                 # verify default /tmp artifact
#   ./verify-association-module.sh --artifact F.ko
#   ./verify-association-module.sh --tree /tmp/sibling-mgmt-fix
#   ./verify-association-module.sh --assoc-tree /tmp/wifi-assoc-build/integ
#   ./verify-association-module.sh --rebuild       # print the rebuild recipe
set -eu

self=$0
case $self in
	*/*) here=$(dirname "$self") ;;
	*) here=. ;;
esac
LANE=$(cd "$here/.." && pwd)
SIBLING_PATCH="$LANE/../wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch"
BUILD_ASSOC="$LANE/../wifi-stability/tools/build-association-module.sh"
EXPECT_SIZE=344372
EXPECT_SHA=ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e
EXPECT_PARAMS=56

ART=$(printenv ASSOC_MODULE 2>/dev/null || true)
[ -n "$ART" ] || ART=/tmp/sibling-mgmt-fix/hi5622_wifi.stripped.ko
TREE=$(printenv ASSOC_MODULE_TREE 2>/dev/null || true)
[ -n "$TREE" ] || TREE=/tmp/sibling-mgmt-fix
ASSOC_TREE=$(printenv ASSOC_TREE_DIR 2>/dev/null || true)
[ -n "$ASSOC_TREE" ] || ASSOC_TREE=/tmp/wifi-assoc-build/integ
MODE=verify

while [ $# -gt 0 ]; do
	case $1 in
	--artifact) shift; [ $# -gt 0 ] || { echo "error: --artifact needs a path" >&2; exit 2; }; ART=$1 ;;
	--tree) shift; [ $# -gt 0 ] || { echo "error: --tree needs a path" >&2; exit 2; }; TREE=$1 ;;
	--assoc-tree) shift; [ $# -gt 0 ] || { echo "error: --assoc-tree needs a path" >&2; exit 2; }; ASSOC_TREE=$1 ;;
	--rebuild) MODE=rebuild ;;
	--help | -h) sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
	*) echo "error: unknown argument $1 (see --help)" >&2; exit 2 ;;
	esac
	shift
done

size_of() { wc -c <"$1" 2>/dev/null | tr -d ' '; }
sha256_of() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }
have() { command -v "$1" >/dev/null 2>&1; }
ok0() { [ "$1" = "$2" ] && echo 0 || echo 1; }

if [ "$MODE" = rebuild ]; then
	cat <<EOF
REBUILD RECIPE (host, needs the OpenWrt lab tree; no router contact):

  1. build the final ASSOCIATION tree (all-patch + wpa3 + ap-mgmt + sta-ops + data-tx):
       $BUILD_ASSOC
     (ALLPATCH_TREE=/tmp/wifi-allpatch-build/integ may be reused; the script
      refuses chain drift and publishes /tmp/wifi-assoc-build/integ.)

  2. copy the tree and apply the CORRECTED device mgmt-TX carrier:
       rm -rf /tmp/sibling-mgmt-fix
       cp -a /tmp/wifi-assoc-build/integ /tmp/sibling-mgmt-fix
       cd /tmp/sibling-mgmt-fix && rm -f *.o *.ko *.mod .*.cmd Module.symvers
       patch -p1 < $SIBLING_PATCH

  3. rebuild against the luofu 6.18.60 kernel tree (KDIR/STAGING_DIR from the
     association builder environment), then strip:
       make ARCH=arm CROSS_COMPILE=<staging>/toolchain-*/bin/arm-openwrt-linux-muslgnueabi- \\
            -C <KDIR> M=/tmp/sibling-mgmt-fix modules
       <cross>-strip --strip-debug -o hi5622_wifi.stripped.ko hi5622_wifi.ko

  4. re-run this verifier; PASS requires
       size $EXPECT_SIZE B, sha256 $EXPECT_SHA, $EXPECT_PARAMS params,
       the corrected-carrier feature strings, and NO old WAL 0x01F2 carrier.

  5. The Lead copies the verified .ko to the router (e.g. /root/hi5622_assoc.ko)
     and sha256-checks it there before insmod; see ../RUNBOOK.md.
EOF
	exit 0
fi

fail=0
check() { # check <name> <ok:0/1> <detail>
	if [ "$2" -eq 0 ]; then
		printf 'PASS  %s  %s\n' "$1" "$3"
	else
		printf 'FAIL  %s  %s\n' "$1" "$3"
		fail=1
	fi
}

echo "== corrected association module verifier (host-only, read-only)"
echo "artifact : $ART"
echo "tree     : $TREE"
echo "assoc    : $ASSOC_TREE"

if [ ! -f "$ART" ]; then
	check "artifact.present" 1 "missing: $ART (run --rebuild, or pass --artifact)"
	echo "RESULT: FAIL"
	exit 1
fi
check "artifact.present" 0 "found"

SIZE=$(size_of "$ART")
check "artifact.size" "$(ok0 "$SIZE" "$EXPECT_SIZE")" "size=$SIZE expected=$EXPECT_SIZE"
SHA=$(sha256_of "$ART")
check "artifact.sha256" "$(ok0 "$SHA" "$EXPECT_SHA")" "sha256=$SHA"

if have modinfo; then
	NP=$(modinfo -p "$ART" 2>/dev/null | wc -l | tr -d ' ')
	check "artifact.params" "$(ok0 "$NP" "$EXPECT_PARAMS")" "params=$NP expected=$EXPECT_PARAMS"
else
	check "artifact.params" 1 "modinfo not available"
fi
if have strings; then
	for s in "mgmt_tx op reached" "set_default_mgmt_key posted" \
		 "sta_ops: STA %pM added" "data-tx tx: posts=" "DP rx_arm" \
		 "did not re-arm the scan context"; do
		if strings -a "$ART" | grep -qF "$s"; then
			check "feature" 0 "$s"
		else
			check "feature" 1 "missing: $s"
		fi
	done
	if strings -a "$ART" | grep -qF "WAL cfg-event carrier"; then
		check "carrier.corrected" 1 "old WAL 0x01F2 carrier string still present"
	else
		check "carrier.corrected" 0 "old WAL 0x01F2 carrier absent (g0 id6 carrier)"
	fi
	if strings -a "$ART" | grep -qF "himg_build_hcc_mgmt_tx"; then
		check "carrier.g0id6_builder" 0 "himg_build_hcc_mgmt_tx in the artifact"
	fi
else
	check "artifact.strings" 1 "strings not available"
fi

if [ -d "$ASSOC_TREE" ] && [ -d "$TREE" ] && [ -f "$SIBLING_PATCH" ]; then
	tmp=$(mktemp -d 2>/dev/null || echo /tmp/assoc-e2e-prov.$$)
	rm -rf "$tmp"; mkdir -p "$tmp"
	cp -a "$ASSOC_TREE/." "$tmp/" 2>/dev/null || true
	( cd "$tmp" && rm -f *.o *.ko *.mod *.mod.c .*.cmd Module.symvers modules.order 2>/dev/null ) || true
	if ( cd "$tmp" && patch -p1 --dry-run < "$SIBLING_PATCH" >/dev/null 2>&1 ); then
		check "provenance.patch_applies" 0 "sibling carrier patch applies to the association tree"
		( cd "$tmp" && patch -p1 --no-backup-if-mismatch < "$SIBLING_PATCH" >/dev/null 2>&1 ) || true
		diffs=0
		for f in hi5622_mgmt_logic.h hi5622_mgmt_logic.c hi5622_frames_api.h \
			 hi5622_vapscan_provider.c hi5622_mgmt.c; do
			if [ -f "$TREE/$f" ] && ! cmp -s "$tmp/$f" "$TREE/$f"; then
				diffs=$((diffs + 1))
				echo "      differs: $f"
			fi
		done
		check "provenance.files_match" "$(ok0 "$diffs" 0)" "$diffs of 5 patched files differ"
	else
		check "provenance.patch_applies" 1 "sibling patch no longer applies to $ASSOC_TREE (tree drift?)"
	fi
	rm -rf "$tmp"
else
	echo "SKIP  provenance  (source trees absent: $ASSOC_TREE / $TREE; artifact identity above is authoritative)"
fi

if [ "$fail" -eq 0 ]; then
	echo "RESULT: PASS - this is the corrected association module (ab9e18a4 chain)"
	exit 0
fi
echo "RESULT: FAIL - do NOT load this artifact on the router; see --rebuild"
exit 1
