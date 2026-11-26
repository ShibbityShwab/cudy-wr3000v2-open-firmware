#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# assemble-target.sh - install the luofu v2 (Cudy WR3000 v2.0 / HiSilicon
# Hi5671Y board R116) OpenWrt target into an OpenWrt tree from the project's
# single sources.
#
# Lane:  opensource/docs/incoming/upstream-dts (task-238).
# Source: opensource/target-skeleton/ (the single source; this script never
#         carries a copy of the target, the DTS or the image recipe).
#
# Mapping (target-skeleton/README.md section 4):
#   target/linux/hisilicon/**                    -> <openwrt>/target/linux/hisilicon/
#   dts/luofu-r116.dts, luofu-r116-overlay.dtso  -> <openwrt>/target/linux/hisilicon/dts/
#   tools/{fip-slice,luofu-image,pad-dtb}.py     -> <openwrt>/scripts/
#
# Usage:
#   assemble-target.sh --check                      dry-run: validate sources, print summary
#   assemble-target.sh --manifest FILE              dry-run, write the manifest TSV to FILE
#   assemble-target.sh --apply DEST [--manifest F]  install into the OpenWrt tree DEST
#   assemble-target.sh --verify DEST                recompute DEST files against the sources
#
# Fail-closed: refuses a DEST that is not an OpenWrt tree, refuses to delete
# anything outside DEST/target/linux/hisilicon and the three named scripts/ files.
set -euo pipefail

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
OS_ROOT="$(cd "$SELF_DIR/../../../.." && pwd)"
SKELETON="$OS_ROOT/target-skeleton"
TARGET_SRC="$SKELETON/target/linux/hisilicon"
DTS_DIR="$SKELETON/dts"
SCRIPTS="fip-slice.py luofu-image.py pad-dtb.py"

die() { echo "assemble-target: ERROR: $*" >&2; exit 1; }
say() { echo "assemble-target: $*"; }

MODE=check
DEST=""
MANIFEST=""
while [ $# -gt 0 ]; do
	case "$1" in
	--check) MODE=check ;;
	--apply) MODE=apply; shift; DEST="$1" ;;
	--verify) MODE=verify; shift; DEST="$1" ;;
	--manifest) shift; MANIFEST="$1" ;;
	--skeleton) shift; SKELETON="$1" ;;
	-h|--help) sed -n '2,30p' "$0"; exit 0 ;;
	*) die "unknown argument: $1 (try --help)" ;;
	esac
	shift
done

TARGET_SRC="$SKELETON/target/linux/hisilicon"
DTS_DIR="$SKELETON/dts"

[ -d "$TARGET_SRC" ] || die "single-source target dir missing: $TARGET_SRC"
[ -d "$DTS_DIR" ]    || die "single-source dts dir missing: $DTS_DIR"
for s in "$DTS_DIR/luofu-r116.dts" "$DTS_DIR/luofu-r116-overlay.dtso"; do
	[ -f "$s" ] || die "single-source DTS missing: $s"
done
for s in $SCRIPTS; do
	[ -f "$SKELETON/tools/$s" ] || die "single-source script missing: $SKELETON/tools/$s"
done

# ---- source file list: dest-rel<TAB>source-abs -----------------------------
# One line per file the target installation owns. Nothing outside this list is
# written or deleted by --apply.
list_files() {
	local f rel
	while IFS= read -r -d '' f; do
		rel="$(realpath --relative-to="$TARGET_SRC" "$f")"
		printf 'target/linux/hisilicon/%s\t%s\n' "$rel" "$f"
	done < <(find "$TARGET_SRC" -type f -print0 | sort -z)
	for f in "$DTS_DIR"/luofu-r116.dts "$DTS_DIR"/luofu-r116-overlay.dtso; do
		printf 'target/linux/hisilicon/dts/%s\t%s\n' "$(basename "$f")" "$f"
	done
	for f in $SCRIPTS; do
		printf 'scripts/%s\t%s\n' "$f" "$SKELETON/tools/$f"
	done
}

write_manifest() {
	local out="$1" dest_rel src mode sha
	{
		printf 'dest_rel\tmode\tsha256\tsource_rel\n'
		while IFS=$'\t' read -r dest_rel src; do
			mode="$(stat -c '%a' "$src")"
			sha="$(sha256sum "$src" | cut -d' ' -f1)"
			printf '%s\t%s\t%s\t%s\n' "$dest_rel" "$mode" "$sha" "$(realpath --relative-to="$OS_ROOT" "$src")"
		done < <(list_files)
	} > "$out"
	say "manifest written: $out"
}

if [ "$MODE" = check ]; then
	n=0; bytes=0
	while IFS=$'\t' read -r dest_rel src; do
		n=$((n + 1)); bytes=$((bytes + $(stat -c '%s' "$src")))
	done < <(list_files)
	[ "$n" -gt 0 ] || die "source file list is empty"
	say "sources OK: $SKELETON"
	say "  files: $n ($(ls "$TARGET_SRC/patches-6.18" | wc -l) patches), bytes: $bytes"
	say "  dts: $(wc -l < "$DTS_DIR/luofu-r116.dts") lines base / $(wc -l < "$DTS_DIR/luofu-r116-overlay.dtso") lines overlay"
	if [ -n "$MANIFEST" ]; then write_manifest "$MANIFEST"; else
		say "  run --manifest FILE to write the table of contents, --apply DEST to install"
	fi
	exit 0
fi

# ---- destination validation (both --apply and --verify) --------------------
[ -n "$DEST" ] || die "missing DEST"
[ -d "$DEST" ] || die "DEST is not a directory: $DEST"
DEST="$(cd "$DEST" && pwd -P)"
case "$DEST" in
/) die "refusing to operate on /" ;;
esac
{ [ -f "$DEST/Makefile" ] || [ -f "$DEST/BSDmakefile" ]; } \
	&& [ -d "$DEST/target/linux" ] && [ -d "$DEST/scripts" ] \
	|| die "DEST does not look like an OpenWrt tree (expects Makefile/BSDmakefile, target/linux, scripts/): $DEST"
[ "$DEST" != "$OS_ROOT" ] || die "refusing to install the target into the source repository"
[ "$DEST" != "$SKELETON" ] || die "refusing to install the target over its own single source"

OWNED_TARGET="$DEST/target/linux/hisilicon"
OWNED_SCRIPTS=""
for s in $SCRIPTS; do OWNED_SCRIPTS="$OWNED_SCRIPTS $DEST/scripts/$s"; done

if [ "$MODE" = verify ]; then
	rc=0
	while IFS=$'\t' read -r dest_rel src; do
		df="$DEST/$dest_rel"
		if [ ! -f "$df" ]; then echo "MISSING $dest_rel" >&2; rc=1; continue; fi
		if ! cmp -s "$src" "$df"; then echo "DIFF    $dest_rel" >&2; rc=1; fi
	done < <(list_files)
	if [ "$rc" -eq 0 ]; then say "verify OK: DEST matches the single sources"; exit 0; fi
	exit 1
fi

# ---- apply -----------------------------------------------------------------
say "DEST (OpenWrt tree): $DEST"
say "replacing: $OWNED_TARGET"
for f in $OWNED_SCRIPTS; do say "replacing: $f"; done

# Deletions are limited to the exact owned paths, all proven to be inside DEST.
rm -rf --one-file-system "$OWNED_TARGET"
for f in $OWNED_SCRIPTS; do rm -f "$f"; done
mkdir -p "$OWNED_TARGET" "$DEST/scripts"

while IFS=$'\t' read -r dest_rel src; do
	mkdir -p "$DEST/$(dirname "$dest_rel")"
	cp -p "$src" "$DEST/$dest_rel"
done < <(list_files)

# Post-copy verification: every installed file must be byte-identical to its
# single source; a mismatch aborts (no silently divergent fourth DTS copy).
bad=0
while IFS=$'\t' read -r dest_rel src; do
	if ! cmp -s "$src" "$DEST/$dest_rel"; then echo "assemble-target: ERROR: copy mismatch: $dest_rel" >&2; bad=1; fi
done < <(list_files)
[ "$bad" -eq 0 ] || die "post-copy verification failed"
for s in $SCRIPTS; do chmod 755 "$DEST/scripts/$s"; done

[ -f "$DEST/target/linux/hisilicon/dts/luofu-r116.dts" ] || die "installed base DTS missing"
[ -f "$DEST/target/linux/hisilicon/image/luofu.mk" ]     || die "installed image recipe missing"
[ -f "$DEST/target/linux/hisilicon/luofu/config-6.18" ]  || die "installed kernel config missing"

say "installed OK: $(list_files | wc -l) files"
say "  target dir : $OWNED_TARGET"
say "  patches    : $(ls "$OWNED_TARGET/patches-6.18" | wc -l)"
say "  next       : make defconfig && make target/linux/prepare && make target/linux/compile"
if [ -n "$MANIFEST" ]; then write_manifest "$MANIFEST"; fi
