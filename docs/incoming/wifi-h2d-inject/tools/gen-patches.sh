#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
#
# gen-patches.sh - regenerate patches/ from files/ + the two reference trees.
#
# The committed patches/0001-hi5622-pciet-h2d-inject-{base,b3}.patch are what
# build.sh consumes.  Run this only after changing files/ or when the
# reference transport trees change: it copies ../wifi-transport-port/build
# (base) and ../wifi-transport-port/b3 (b3) twice, applies the deterministic
# integration edits to one copy (tools/apply-edits.py), and diffs the pair.
#
# HOST-ONLY: works on copies under $GEN_DIR (default /tmp); it never modifies
# the reference trees and never touches the bench device.
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
gen=$(printenv GEN_DIR || true)
[ -n "$gen" ] || gen=/tmp/wifi-h2d-inject-gen

rm -rf "$gen"
mkdir -p "$gen"

for kind in base b3; do
	if [ "$kind" = base ]; then
		src=$here/../wifi-transport-port/build
		out=0001-hi5622-pciet-h2d-inject-base.patch
	else
		src=$here/../wifi-transport-port/b3
		out=0001-hi5622-pciet-h2d-inject-b3.patch
	fi
	if [ ! -d "$src" ]; then
		echo "ERROR: reference tree not found: $src" >&2
		exit 1
	fi
	mkdir -p "$gen/$kind"
	cp -a "$src" "$gen/$kind/a"
	cp -a "$src" "$gen/$kind/b"
	# never diff stale build products into the patch
	rm -f "$gen/$kind/a"/*.o "$gen/$kind/a"/*.ko "$gen/$kind/a"/*.mod \
	      "$gen/$kind/a"/*.mod.c "$gen/$kind/a"/modules.order \
	      "$gen/$kind/a"/Module.symvers "$gen/$kind/a"/.*.cmd \
	      "$gen/$kind/b"/*.o "$gen/$kind/b"/*.ko "$gen/$kind/b"/*.mod \
	      "$gen/$kind/b"/*.mod.c "$gen/$kind/b"/modules.order \
	      "$gen/$kind/b"/Module.symvers "$gen/$kind/b"/.*.cmd
	python3 "$here/tools/apply-edits.py" "$gen/$kind/b" "$kind" "$here/files"
	# deterministic patch bytes: fix file mtimes before diffing (diff puts them
	# in the ---/+++ headers)
	find "$gen/$kind/a" "$gen/$kind/b" -exec touch -d '2026-11-23 00:00:00 UTC' {} +
	(
		cd "$gen/$kind"
		rc=0
		diff -ruN a b > "$here/patches/$out" || rc=$?
		if [ "$rc" -ne 1 ]; then
			echo "ERROR: diff failed with rc=$rc" >&2
			exit "$rc"
		fi
	)
	printf 'wrote %s (%s lines)\n' "patches/$out" \
		"$(wc -l < "$here/patches/$out")"
done

echo "patch regeneration OK"
