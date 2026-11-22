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
DTS_SERIES_PATCH="$SERIES_DIR/dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch"
EXPECTED_TARBALL_SHA256="3c2f44ef6acc021a7ad444d9a2e6ec1ef488ff09368793d0004a6080f69d621e"

die() { echo "FAIL: $*" >&2; exit 1; }
note() { echo "NOTE: $*" | tee -a "$LOGS/summary.txt"; }

# Check the base-DTB totalsize alignment requirement documented in
# TRAILER-FORMAT.md section 11 and exercise the pad-dtb build step on a copy
# when the compiled blob is unaligned.  A copy is used so the original
# compiled DTB and its hash stay untouched.
check_unaligned_dtb() {
    dtb="$1"
    label="$2"
    logs="$3"
    size=$(stat -c%s "$dtb")
    if [ $((size % 8)) -eq 0 ]; then
        echo "$label: DTB totalsize $size B is 8-byte aligned" | tee -a "$logs/summary.txt"
        return 0
    fi
    padder="$REPO_ROOT/opensource/target-skeleton/tools/pad-dtb.py"
    [ -f "$padder" ] || die "$label: DTB is unaligned but pad-dtb.py is missing"
    padded="$dtb.padded"
    cp "$dtb" "$padded"
    python3 "$padder" "$padded" >> "$logs/pad-dtb.log" 2>&1         || die "$label: pad-dtb.py failed"
    psize=$(stat -c%s "$padded")
    [ $((psize % 8)) -eq 0 ] || die "$label: padded DTB is still unaligned"
    compat2=$("$FDTGET" -t s "$padded" /reserved-memory/flashinfo@80600000 compatible 2>&1)         || die "$label: padded DTB lost the flashinfo node"
    [ "$compat2" = "hisilicon,luofu-flashinfo hsan,flashinfo_reserved" ] || die "$label: padded DTB contract changed"
    echo "$label: DTB totalsize $size B is NOT 8-byte aligned -> the image build MUST run Build/luofu-pad-dtb (TRAILER-FORMAT.md section 11); pad-dtb.py pads it to $psize B and keeps the hand-off contract" | tee -a "$logs/summary.txt"
    rm -f "$padded"
}

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

    # The stock U-Boot bootfip reads the overlay-container gate word as a
    # 32-bit load at base_dtb + FDT-totalsize.  An unaligned totalsize makes
    # that load unaligned; on this ARMv7 it data-aborted and the loader fell
    # back to stock.  The target build must run Build/luofu-pad-dtb; verify the
    # requirement here (TRAILER-FORMAT.md section 11).
    check_unaligned_dtb "$DTB" "target DTS" "$LOGS"
    console=$("$FDTGET" -t s "$DTB" /aliases console 2>&1) || die "/aliases/console missing"
    compat=$("$FDTGET" -t s "$DTB" /reserved-memory/flashinfo@80600000 compatible 2>&1) || die "flashinfo node missing"
    reg=$("$FDTGET" -t x "$DTB" /reserved-memory/flashinfo@80600000 reg 2>&1) || die "flashinfo reg missing"
    atag=$("$FDTGET" -t x "$DTB" /reserved-memory/flashinfo@80600000 atag-offset 2>&1) || die "flashinfo atag-offset missing"
    method=$("$FDTGET" -t s "$DTB" /cpus enable-method 2>&1) || die "enable-method missing"
    echo "DTS contract: console=$console flashinfo-compatible=$compat reg=$reg atag-offset=$atag enable-method=$method" | tee -a "$LOGS/summary.txt"
    [ "$compat" = "hisilicon,luofu-flashinfo hsan,flashinfo_reserved" ] || die "unexpected flashinfo compatible: $compat"
    [ "$reg" = "80600000 4000" ] || die "unexpected flashinfo reg: $reg"
    [ "$atag" = "1000" ] || die "unexpected atag-offset: $atag"
    [ "$method" = "hisilicon,luofu-smp" ] || die "unexpected enable-method: $method"

    # ------------------------------------------------ upstream DTS series entry
    # DTS-0001 is the DTS-series companion the kernel review form cannot carry:
    # it adds the board DTS with the loader hand-off nodes required by SMP
    # patch 0018.  It must apply to the same tree and its DTS must be the
    # byte-identical copy of the target DTS (single source of truth).
    if [ -f "$DTS_SERIES_PATCH" ]; then
        DTREE="$WORKDIR/tree-dts-series"
        rm -rf "$DTREE"; cp -a --reflink=auto "$TREE" "$DTREE" || die "DTS-series tree copy failed"
        ( cd "$DTREE" && patch -p1 -i "$DTS_SERIES_PATCH" ) > "$LOGS/dts-series-apply.log" 2>&1             || die "DTS series patch does not apply"
        IN_TREE="$DTREE/arch/arm/boot/dts/hisilicon/luofu-r116.dts"
        [ -f "$IN_TREE" ] || die "DTS series patch did not add the board DTS"
        # The single-source DTS also carries the release-line-only W23 thermal
        # delta (target patches 066/067), which the upstream review form's
        # thermal binding (0012) cannot describe.  DTS-0001 therefore carries
        # the target DTS as produced by tools/dts-upstream-filter.py, the
        # single definition of the release-line -> upstream DTS delta.
        FILTERED="$LOGS/dts-series-expected.dts"
        python3 "$SERIES_DIR/tools/dts-upstream-filter.py" "$DTS" "$FILTERED"             > "$LOGS/dts-series-filter.log" 2>&1             || die "DTS series: release-line filter failed (see $LOGS/dts-series-filter.log)"
        if cmp -s "$IN_TREE" "$FILTERED"; then
            echo "DTS series: board DTS byte-identical to the filtered target DTS (release-line-only W23 thermal delta stripped)" | tee -a "$LOGS/summary.txt"
        else
            note "DTS series board DTS differs from the filtered target DTS (regenerate DTS-0001 from the single source)"
        fi
        if grep -q -e 'hisilicon,threshold-mc' -e 'hwver = <3>' "$IN_TREE"; then
            die "DTS series: release-line-only W23 properties leaked into DTS-0001"
        fi
        PRE2="$LOGS/luofu-r116.series.pre.dts"
        DTB2="$LOGS/luofu-r116.series.dtb"
        ( cd "$DTREE" && cpp -nostdinc -I include -I arch/arm/boot/dts -undef -D__DTS__             -x assembler-with-cpp -o "$PRE2" "$IN_TREE" ) 2> "$LOGS/dts-series-cpp.err"             || die "cpp failed for the DTS series board DTS"
        "$DTC" -I dts -O dtb -o "$DTB2" "$PRE2" 2> "$LOGS/dts-series-dtc.err"             || die "dtc failed for the DTS series board DTS"
        size2=$(stat -c%s "$DTB2")
        echo "DTS series: patch apply RC=0, cpp+dtc RC=0, stderr $(stat -c%s "$LOGS/dts-series-cpp.err")/$(stat -c%s "$LOGS/dts-series-dtc.err") bytes, DTB $size2 B sha256 $(sha256sum "$DTB2" | awk '{print $1}')" | tee -a "$LOGS/summary.txt"
        check_unaligned_dtb "$DTB2" "DTS series" "$LOGS"
        c2=$("$FDTGET" -t s "$DTB2" /aliases console 2>&1) || die "DTS series: /aliases/console missing"
        f2=$("$FDTGET" -t s "$DTB2" /reserved-memory/flashinfo@80600000 compatible 2>&1) || die "DTS series: flashinfo node missing"
        r2=$("$FDTGET" -t x "$DTB2" /reserved-memory/flashinfo@80600000 reg 2>&1) || die "DTS series: flashinfo reg missing"
        a2=$("$FDTGET" -t x "$DTB2" /reserved-memory/flashinfo@80600000 atag-offset 2>&1) || die "DTS series: atag-offset missing"
        m2=$("$FDTGET" -t s "$DTB2" /cpus enable-method 2>&1) || die "DTS series: enable-method missing"
        [ "$f2" = "hisilicon,luofu-flashinfo hsan,flashinfo_reserved" ] || die "DTS series: unexpected flashinfo compatible"
        [ "$r2" = "80600000 4000" ] || die "DTS series: unexpected flashinfo reg"
        [ "$a2" = "1000" ] || die "DTS series: unexpected atag-offset"
        [ "$m2" = "hisilicon,luofu-smp" ] || die "DTS series: unexpected enable-method"
        echo "DTS series contract: console=$c2 flashinfo=$f2 reg=$r2 atag-offset=$a2 enable-method=$m2" | tee -a "$LOGS/summary.txt"
        [ -z "$KEEP" ] && rm -rf "$DTREE"
    else
        note "no dts-series/0001 patch found - the upstream DTS dependency was not checked"
    fi
fi

if [ -z "$KEEP" ]; then
    rm -rf "$TREE" "$GTREE"
fi
echo "PASS: release line applies and the loader hand-off DTS contract is present. No hardware and no kernel build was involved." | tee -a "$LOGS/summary.txt"
