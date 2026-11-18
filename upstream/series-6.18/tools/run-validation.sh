#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Host-only validation of the luofu 6.18 submission series.
#
# Applies series/NNNN-*.patch in order to a pristine linux-6.18.60 tree with
# both patch(1) and git am, compares the two results, and runs
# scripts/checkpatch.pl from that tree on every series patch.  No network
# access, no device, no build.  The kernel tarball is only read, never
# modified; the work directory is disposable.
#
# Usage:
#   tools/run-validation.sh <linux-6.18.60.tar.xz> <workdir>
#   tools/run-validation.sh --use-tree <already-extracted-6.18.60> <workdir>
#
# Results go to <workdir>/logs/.  Exit status 0 means every step passed; any
# patch failure, tree difference or checkpatch ERROR makes it non-zero.

set -u

SERIES_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SERIES="$SERIES_DIR/series"
EXPECTED_TARBALL_SHA256="3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e"

die() { echo "FAIL: $*" >&2; exit 1; }

SOURCE=""
EXTRACTED=""
WORKDIR=""
if [ $# -eq 3 ] && [ "$1" = "--use-tree" ]; then
    EXTRACTED="$2"
    WORKDIR="$3"
elif [ $# -eq 2 ]; then
    SOURCE="$1"
    WORKDIR="$2"
else
    die "usage: $0 <linux-6.18.60.tar.xz> <workdir> | $0 --use-tree <tree> <workdir>"
fi

[ -d "$SERIES" ] || die "series directory not found: $SERIES"
case "$WORKDIR" in
    /*) ;;
    *) die "workdir must be an absolute path: $WORKDIR" ;;
esac
mkdir -p "$WORKDIR/logs" || die "cannot create $WORKDIR"
WORKDIR="$(cd "$WORKDIR" && pwd)"
if [ "$WORKDIR" = "/" ] || [ "$WORKDIR" = "$SERIES_DIR" ]; then
    die "refusing to use $WORKDIR as a scratch directory"
fi
LOGS="$WORKDIR/logs"
rm -rf "$WORKDIR/patch-tree" "$WORKDIR/amtree"
rm -f "$LOGS"/*.log "$LOGS"/*.txt

echo "series: $SERIES"
echo "workdir: $WORKDIR"
echo "logs: $LOGS"

# ---------------------------------------------------------------- pristine
PRISTINE="$WORKDIR/pristine"
if [ -n "$EXTRACTED" ]; then
    [ -d "$EXTRACTED" ] || die "no such extracted tree: $EXTRACTED"
    extracted_abs="$(cd "$EXTRACTED" && pwd)"
    if [ "$extracted_abs" = "$PRISTINE" ]; then
        echo "using the extracted tree in place: $PRISTINE"
    else
        rm -rf "$PRISTINE"
        cp -a --reflink=auto "$EXTRACTED" "$PRISTINE" || die "copy of extracted tree failed"
    fi
else
    [ -f "$SOURCE" ] || die "no such tarball: $SOURCE"
    actual=$(sha256sum "$SOURCE" | awk '{print $1}')
    echo "$SOURCE  sha256=$actual" | tee "$LOGS/tarball.txt"
    [ "$actual" = "$EXPECTED_TARBALL_SHA256" ] || die "tarball sha256 mismatch"
    rm -rf "$PRISTINE"
    mkdir -p "$PRISTINE"
    tar -xJf "$SOURCE" -C "$PRISTINE" --strip-components=1 || die "extract failed"
fi
version=$(sed -n -e 's/^VERSION = //p' -e 's/^PATCHLEVEL = //p' -e 's/^SUBLEVEL = //p' "$PRISTINE/Makefile" | paste -sd.)
[ "$version" = "6.18.60" ] || die "expected linux-6.18.60, found $version"
echo "pristine version: $version" | tee "$LOGS/version.txt"

# ------------------------------------------------------ patch(1) in order
cp -a --reflink=auto "$PRISTINE" "$WORKDIR/patch-tree" || die "tree copy failed"
patch_log="$LOGS/patch-apply.log"
: > "$patch_log"
n=0
for f in "$SERIES"/*.patch; do
    n=$((n + 1))
    base=$(basename "$f")
    echo "### [$n] $base" >> "$patch_log"
    ( cd "$WORKDIR/patch-tree" && patch -p1 -i "$f" ) >> "$patch_log" 2>&1
    rc=$?
    echo "PATCH_RC[$base]=$rc" >> "$patch_log"
    [ $rc -eq 0 ] || die "patch(1) failed on $base"
done
echo "patch(1): $n/$n patches applied, RC=0"
if grep -qE "FAILED|Reversed \(or previously applied\)" "$patch_log"; then
    die "patch log reports a failed or reversed hunk"
fi
offsets=$(grep -c "Hunk #.*succeeded at .*(offset" "$patch_log" || true)
fuzz=$(grep -c "with fuzz" "$patch_log" || true)
echo "patch(1): hunks with offset=$offsets fuzz=$fuzz" | tee "$LOGS/offsets.txt"
grep -E "Hunk #.*succeeded at .*(offset|fuzz)" "$patch_log" | sed 's/^/  /' || true
find "$WORKDIR/patch-tree" -name '*.orig' -print | sed 's/^/  backup: /' || true
find "$WORKDIR/patch-tree" -name '*.orig' -delete
find "$WORKDIR/patch-tree" -name '*.rej' -print | sed 's/^/  reject: /' || true

# ------------------------------------------------------------ git am order
cp -a --reflink=auto "$PRISTINE" "$WORKDIR/amtree" || die "tree copy failed"
cd "$WORKDIR/amtree" || die "cd failed"
git init -q . || die "git init failed"
git config user.name "series validation"
git config user.email "series-validation@example.invalid"
git add -A && git commit -q -m "pristine linux-6.18.60" || die "baseline commit failed"
cd - >/dev/null
am_log="$LOGS/git-am.log"
: > "$am_log"
n=0
for f in "$SERIES"/*.patch; do
    n=$((n + 1))
    base=$(basename "$f")
    ( cd "$WORKDIR/amtree" && git am "$f" ) >> "$am_log" 2>&1
    rc=$?
    echo "AM_RC[$base]=$rc" >> "$am_log"
    [ $rc -eq 0 ] || die "git am failed on $base"
done
echo "git am: $n/$n patches applied, RC=0" | tee "$LOGS/git-am-summary.txt"
git -C "$WORKDIR/amtree" log --oneline | head -20 > "$LOGS/git-log.txt"
git -C "$WORKDIR/amtree" status --short > "$LOGS/git-status.txt"
[ -s "$LOGS/git-status.txt" ] && die "git am left a dirty work tree"
git -C "$WORKDIR/amtree" diff --shortstat "HEAD~$n..HEAD" > "$LOGS/git-diffstat.txt"

# ------------------------------------------------------- tree equivalence
diff -r --brief --exclude=.git "$WORKDIR/patch-tree" "$WORKDIR/amtree" > "$LOGS/tree-diff.log" 2>&1
rc=$?
if [ $rc -ne 0 ]; then
    cat "$LOGS/tree-diff.log" >&2
    die "trees differ between patch(1) and git am"
fi
echo "tree equivalence: identical" | tee "$LOGS/tree-equivalence.txt"

# ------------------------------------------------------------- checkpatch
CHECKPATCH="$PRISTINE/scripts/checkpatch.pl"
[ -f "$CHECKPATCH" ] || die "checkpatch not found in pristine tree"
"$CHECKPATCH" --version | head -2 > "$LOGS/checkpatch-version.txt"
run_checkpatch() {
    tag="$1"
    mode="$2"
    log="$LOGS/checkpatch-$tag.log"
    : > "$log"
    : > "$LOGS/checkpatch-$tag-summary.txt"
    for f in "$SERIES"/*.patch; do
        base=$(basename "$f")
        echo "########## $base" >> "$log"
        "$CHECKPATCH" --no-tree $mode "$f" >> "$log" 2>&1
        grep -E "^total:" "$log" | tail -1 | sed "s/^/$base :: /" >> "$LOGS/checkpatch-$tag-summary.txt"
    done
    errors=$(grep -cE "^ERROR:" "$log" || true)
    warnings=$(grep -cE "^WARNING:" "$log" || true)
    echo "checkpatch ($tag): errors=$errors warnings=$warnings" | tee -a "$LOGS/checkpatch-summary.txt"
    [ "$errors" -eq 0 ] || die "checkpatch reported errors ($tag); see $log"
}
run_checkpatch default ""
run_checkpatch strict "--strict"
echo "PASS: all validation steps completed; no hardware or build was involved."