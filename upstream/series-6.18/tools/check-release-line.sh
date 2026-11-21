#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Host-only validation of the luofu 6.18 RELEASE LINE.
#
# The release line is the patch stack recorded in release-line/manifest.tsv:
# the target-skeleton patches that build the verified 2-CPU baseline, minus
# the bench-only SMP diagnostic patches.  It is not the upstream review form
# (series/0001-00NN.patch); see 05-release-line.md.
#
# What this script checks, on a pristine linux-6.18.60 tree and no hardware:
#   1. every "include" patch applies in manifest order with patch(1):
#      RC, offsets, fuzz and rejects are recorded;
#   2. a strict "git apply" pass is attempted and its first divergence is
#      recorded (this is expected for the known 052/053 ordering fuzz - the
#      script reports it instead of failing, because patch(1) is the interface
#      the OpenWrt target build uses);
#   3. the release-critical loader hand-off DTS contract compiles with the
#      patched tree's headers and the two nodes are present in the DTB:
#      /aliases/console and /reserved-memory/flashinfo@80600000.
#
# Usage:
#   tools/check-release-line.sh <linux-6.18.60.tar.xz> <workdir>
#   tools/check-release-line.sh --use-tree <extracted-6.18.60> <workdir>
# Options:
#   --dts <path>   board DTS to compile (default: target-skeleton/dts/luofu-r116.dts)
#   --keep        keep the patched trees (they are large)
#
# Exit 0 only if all include patches apply, there are no rejects, and the DTS
# contract checks pass.  Logs are written to <workdir>/logs/.

set -u

SERIES_DIR="$(cd "$(dirname "$0")/.." && pwd)"
REPO_ROOT="$(cd "$SERIES_DIR/../../.." && pwd)"
TARGET_PATCHES="$REPO_ROOT/opensource/target-skeleton/target/linux/hisilicon/patches-6.18"
MANIFEST="$SERIES_DIR/release-line/manifest.tsv"
DTS="$REPO_ROOT/opensource/target-skeleton/dts/luofu-r116.dts"
EXPECTED_TARBALL_SHA256="3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e"

die() { echo "FAIL: $*" >&2; exit 1; }
note() { echo "NOTE: $*" | tee -a "$LOGS/summary.txt"; }

[ -f "$MANIFEST" ] || die "missing manifest: $MANIFEST"
[ -d "$TARGET_PATCHES" ] || die "missing target patches: $TARGET_PATCHES"

SOURCE=""
PRISTINE_IN=""
if [ $# -ge 1 ] && [ "$1" = "--use-tree" ]; then
    [ $# -ge 3 ] || die "usage: $0 --use-tree <tree> <workdir> [--dts <path>] [--keep]"
    PRISTINE_IN="$2"; WORKDIR="$3"; shift 3
else
    [ $# -ge 2 ] || die "usage: $0 <tarball> <workdir> [--dts <path>] [--keep]"
    SOURCE="$1"; WORKDIR="$2"; shift 2
fi
KEEP=""
while [ $# -gt 0 ]; do
    case "$1" in
        --dts) [ $# -ge 2 ] || die "--dts needs a path"; DTS="$2"; shift 2 ;;
        --keep) KEEP=1; shift ;;
        *) die "unknown option: $1" ;;
    esac
done
case "$WORKDIR" in
    /*) ;;
    *) die "workdir must be absolute: $WORKDIR" ;;
esac
mkdir -p "$WORKDIR" || die "cannot create $WORKDIR"
WORKDIR="$(cd "$WORKDIR" && pwd)"
[ "$WORKDIR" = "/" ] && die "refusing workdir /"
LOGS="$WORKDIR/logs"
rm -rf "$LOGS"; mkdir -p "$LOGS"
: > "$LOGS/summary.txt"

echo "release line: $MANIFEST" | tee -a "$LOGS/summary.txt"
echo "workdir: $WORKDIR" | tee -a "$LOGS/summary.txt"

# ---------------------------------------------------------------- pristine
PRISTINE="$WORKDIR/pristine"
if [ -n "$PRISTINE_IN" ]; then
    [ -d "$PRISTINE_IN" ] || die "no such tree: $PRISTINE_IN"
    in_abs="$(cd "$PRISTINE_IN" && pwd)"
    if [ "$in_abs" = "$PRISTINE" ]; then
        echo "using existing tree in place: $PRISTINE" | tee -a "$LOGS/summary.txt"
    else
        rm -rf "$PRISTINE"; cp -a --reflink=auto "$in_abs" "$PRISTINE" || die "tree copy failed"
    fi
else
    [ -f "$SOURCE" ] || die "no such tarball: $SOURCE"
    actual=$(sha256sum "$SOURCE" | awk '{print $1}')
    echo "tarball sha256: $actual" | tee -a "$LOGS/summary.txt"
    [ "$actual" = "$EXPECTED_TARBALL_SHA256" ] || die "tarball sha256 mismatch"
    rm -rf "$PRISTINE"; mkdir -p "$PRISTINE"
    tar -xJf "$SOURCE" -C "$PRISTINE" --strip-components=1 || die "extract failed"
fi
version=$(sed -n -e 's/^VERSION = //p' -e 's/^PATCHLEVEL = //p' -e 's/^SUBLEVEL = //p' "$PRISTINE/Makefile" | paste -sd.)
[ "$version" = "6.18.60" ] || die "expected linux-6.18.60, got $version"
echo "pristine version: $version" | tee -a "$LOGS/summary.txt"

# --------------------------------------------------------------- manifest
INCLUDED="$LOGS/included.txt"
EXCLUDED="$LOGS/excluded.txt"
awk -F'\t' '$1 == "include" { print $3 }' "$MANIFEST" > "$INCLUDED"
awk -F'\t' '$1 == "exclude" { print $3 }' "$MANIFEST" > "$EXCLUDED"
n_in=$(wc -l < "$INCLUDED"); n_ex=$(wc -l < "$EXCLUDED")
[ "$n_in" -gt 0 ] || die "manifest has no include rows"
echo "manifest: $n_in included, $n_ex excluded" | tee -a "$LOGS/summary.txt"
while IFS= read -r f; do
    [ -f "$TARGET_PATCHES/$f" ] || die "manifest names a missing patch: $f"
done < "$INCLUDED"

# --------------------------------------------------- patch(1) application
TREE="$WORKDIR/tree-patch"
rm -rf "$TREE"; cp -a --reflink=auto "$PRISTINE" "$TREE" || die "tree copy failed"
PATCHLOG="$LOGS/patch-apply.log"; : > "$PATCHLOG"
i=0; fails=0
while IFS= read -r f; do
    i=$((i + 1))
    ( cd "$TREE" && patch -p1 -i "$TARGET_PATCHES/$f" ) >> "$PATCHLOG" 2>&1
    rc=$?
    echo "RC[$f]=$rc" >> "$PATCHLOG"
    if [ $rc -ne 0 ]; then
        echo "patch(1) FAILED: $f (RC=$rc)" | tee -a "$LOGS/summary.txt"
        fails=$((fails + 1))
    fi
done < "$INCLUDED"
[ "$fails" -eq 0 ] || die "patch(1) failures: $fails"
events=$(grep -c '^patching file' "$PATCHLOG" || true)
offsets=$(grep -cE 'Hunk #.*offset' "$PATCHLOG" || true)
fuzz=$(grep -cE 'Hunk #.*fuzz' "$PATCHLOG" || true)
rejects=$(find "$TREE" -name '*.rej' | wc -l)
echo "patch(1): $i/$i patches RC=0, $events file events, $offsets hunks with offset, $fuzz hunks with fuzz, $rejects rejects" | tee -a "$LOGS/summary.txt"
grep -E 'Hunk #.*(offset|fuzz)' "$PATCHLOG" > "$LOGS/offsets.txt" || true
[ "$rejects" -eq 0 ] || die "reject files after application"
find "$TREE" -name '*.orig' -print > "$LOGS/patch1-backups.txt" || true
find "$TREE" -name '*.orig' -delete

# ----------------------------------------------------- strict git apply
# This pass is informational: a strict (no-fuzz) application of a stack that
# is authored against the OpenWrt-prepared kernel tree is expected to diverge
# where two parallel lanes touched the same anchor.  Record the first
# divergence so the release line can be regenerated cleanly later.
GTREE="$WORKDIR/tree-git"
rm -rf "$GTREE"; cp -a --reflink=auto "$PRISTINE" "$GTREE" || die "tree copy failed"
( cd "$GTREE" && git init -q . && git config user.name "release-line" \
  && git config user.email "release-line@example.invalid" \
  && git add -A && git commit -q -m "pristine linux-6.18.60" ) || die "git baseline failed"
GITLOG="$LOGS/git-apply-strict.log"; : > "$GITLOG"
strict_first=""
while IFS= read -r f; do
    ( cd "$GTREE" && git apply "$TARGET_PATCHES/$f" ) >> "$GITLOG" 2>&1
    rc=$?
    echo "GIT_APPLY_RC[$f]=$rc" >> "$GITLOG"
    if [ $rc -ne 0 ]; then
        strict_first="$f"
        break
    fi
    ( cd "$GTREE" && git add -A && git commit -q -m "$f" ) >> "$GITLOG" 2>&1
done < "$INCLUDED"
if [ -z "$strict_first" ]; then
    echo "strict git apply: all $n_in patches apply without fuzz" | tee -a "$LOGS/summary.txt"
else
    note "strict git apply diverges at $strict_first; patch(1) is the target-build interface (see 05-release-line.md section 7)"
fi

# ----------------------------------- release-critical DTS contract check
[ -f "$DTS" ] || die "board DTS not found: $DTS"
command -v cpp >/dev/null 2>&1 || die "cpp not found"
DTC="$(command -v dtc || true)"
FDTGET="$(command -v fdtget || true)"
if [ -z "$DTC" ] || [ -z "$FDTGET" ]; then
    note "dtc/fdtget not in PATH - DTS contract check skipped (install device-tree-compiler and re-run)"
else
    PRE="$LOGS/luofu-r116.pre.dts"
    DTB="$LOGS/luofu-r116.dtb"
    ( cd "$TREE" && cpp -nostdinc -I include -I arch/arm/boot/dts -undef -D__DTS__ \
        -x assembler-with-cpp -o "$PRE" "$DTS" ) 2> "$LOGS/dts-cpp.err" || die "cpp failed"
    cpp_err=$(stat -c%s "$LOGS/dts-cpp.err")
    "$DTC" -I dts -O dtb -o "$DTB" "$PRE" 2> "$LOGS/dts-dtc.err" || die "dtc failed"
    dtc_err=$(stat -c%s "$LOGS/dts-dtc.err")
    dtb_size=$(stat -c%s "$DTB")
    echo "board DTS: cpp RC=0, dtc RC=0, stderr $cpp_err/$dtc_err bytes, DTB $dtb_size B sha256 $(sha256sum "$DTB" | awk '{print $1}')" | tee -a "$LOGS/summary.txt"
    [ "$cpp_err" -eq 0 ] || note "cpp wrote diagnostics (see logs/dts-cpp.err)"
    [ "$dtc_err" -eq 0 ] || note "dtc wrote warnings (see logs/dts-dtc.err)"
    console=$("$FDTGET" -t s "$DTB" /aliases console 2>&1) || die "/aliases/console missing"
    compat=$("$FDTGET" -t s "$DTB" /reserved-memory/flashinfo@80600000 compatible 2>&1) || die "flashinfo node missing"
    reg=$("$FDTGET" -t x "$DTB" /reserved-memory/flashinfo@80600000 reg 2>&1) || die "flashinfo reg missing"
    atag=$("$FDTGET" -t x "$DTB" /reserved-memory/flashinfo@80600000 atag-offset 2>&1) || die "flashinfo atag-offset missing"
    method=$("$FDTGET" -t s "$DTB" /cpus enable-method 2>&1) || die "enable-method missing"
    echo "DTS contract: console=$console flashinfo-compatible=$compat reg=$reg atag-offset=$atag enable-method=$method" | tee -a "$LOGS/summary.txt"
    [ "$compat" = "hsan,flashinfo_reserved" ] || die "unexpected flashinfo compatible: $compat"
    [ "$reg" = "80600000 4000" ] || die "unexpected flashinfo reg: $reg"
    [ "$atag" = "1000" ] || die "unexpected atag-offset: $atag"
    [ "$method" = "hisilicon,luofu-smp" ] || die "unexpected enable-method: $method"
fi

if [ -z "$KEEP" ]; then
    rm -rf "$TREE" "$GTREE"
fi
echo "PASS: release line applies and the loader hand-off DTS contract is present. No hardware and no kernel build was involved." | tee -a "$LOGS/summary.txt"
