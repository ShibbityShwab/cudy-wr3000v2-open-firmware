#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# gen-patches.sh - regenerate patches/0002-hi5622-pciet-hcc-link-b3.patch.
#
# Patch 0002 is an ADD-ON patch: it applies on top of the wifi-h2d-inject
# patch (../wifi-h2d-inject/patches/0001-hi5622-pciet-h2d-inject-b3.patch) on
# the B3 reference tree (../wifi-transport-port/b3).  This script copies that
# tree twice, applies 0001 to BOTH copies, applies the deterministic
# integration edits (tools/apply-edits.py) to one copy, and diffs the pair.
#
# HOST-ONLY: works on copies under $GEN_DIR (default /tmp); it never modifies
# the reference trees and never touches the bench device.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
root=$(CDPATH= cd -- "$here/../.." && pwd)	# opensource/
gen=$(printenv GEN_DIR || true)
[ -n "$gen" ] || gen=/tmp/wifi-hcc-link-gen
src=$here/../wifi-transport-port/b3
p0001=$here/../wifi-h2d-inject/patches/0001-hi5622-pciet-h2d-inject-b3.patch
out=$here/patches/0002-hi5622-pciet-hcc-link-b3.patch

[ -d "$src" ] || { echo "ERROR: reference tree not found: $src" >&2; exit 1; }
[ -f "$p0001" ] || { echo "ERROR: prerequisite patch not found: $p0001" >&2; exit 1; }

rm -rf "$gen"
mkdir -p "$gen/a" "$gen/b"
cp -a "$src"/. "$gen/a"/
cp -a "$src"/. "$gen/b"/
# never diff stale build products into the patch
for t in a b; do
	rm -f "$gen/$t"/*.o "$gen/$t"/*.ko "$gen/$t"/*.mod \
	      "$gen/$t"/*.mod.c "$gen/$t"/modules.order \
	      "$gen/$t"/Module.symvers "$gen/$t"/.*.cmd
	patch -p1 --fuzz=0 --no-backup-if-mismatch -s -d "$gen/$t" < "$p0001"
done
python3 "$here/tools/apply-edits.py" "$gen/b" "$here/files"
# deterministic patch bytes: fix file mtimes before diffing
find "$gen/a" "$gen/b" -exec touch -d '2026-11-23 00:00:00 UTC' {} +
mkdir -p "$here/patches"
(
	cd "$gen"
	rc=0
	diff -ruN a b > "$out" || rc=$?
	if [ "$rc" -ne 1 ]; then
		echo "ERROR: diff failed with rc=$rc" >&2
		exit "$rc"
	fi
)
printf 'wrote %s (%s lines)\n' "$out" "$(wc -l < "$out")"
echo "patch regeneration OK"
