#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# build-kernel-slice.sh - assemble the first FIP kernel slice for the luofu R116
# target entirely from OUR OWN artifacts.
#
# STATUS: SCHEMATIC / HOST-ONLY / NEVER BOOTED. This script touches no device,
# reads no vendor-derived artifact, and does not prove that the slice boots.
#
# WHAT IT DOES (all host-side, all fail-closed)
#   1. cpp + the build tree's dtc:  dts/luofu-r116.dts          -> luofu-r116.dtb
#   2. the same dtc:                dts/luofu-r116-overlay.dtso -> ...-overlay.dtbo
#      (asserted: valid FDT header, base root compatible, the decoded overlay
#       match properties, and one fragment targeting "/" with a no-op __overlay__)
#   3. the build tree's mkimage:    linux-6.18.60 zImage       -> legacy uImage
#      (asserted: ih_load == ih_ep == (loader_stage & 0xf8000000) + TEXT_OFFSET)
#   4. tools/fip-slice.py:          base DTB + match overlay   -> FIP trailer
#   5. tools/fip-slice.py:          uImage + trailer           -> kernel slice
#      (the loader's libfdt overlay apply is required, not skipped)
#   6. tools/fip-slice.py check + an independent re-read of the gate word, the
#      descriptor totals, the slot budget and the uImage identity bytes
#   7. manifest, SHA256SUMS, SIZES and per-step logs in the output directory
#
# WHAT IT PROVES
#   Every structural check the DECODED stock bootfip performs (see
#   target-skeleton/docs/TRAILER-FORMAT.md) is satisfied by bytes built here:
#   FDT chain, descriptor header/entries, container total, gate magic at
#   0x40 + ih_size + base-DTB totalsize, overlay match properties, libfdt apply.
#
# WHAT IT DOES NOT PROVE
#   That the stock loader accepts a NOVEL slice or that the kernel boots. That
#   needs ONE ATTENDED BOOT on a recovered unit (TARGET-DESIGN.md 5.2, 9;
#   TRAILER-FORMAT.md 8). There is no rootfs, no sysupgrade container, no
#   install path and only the first two drivers (CRG, watchdog) ported; the
#   rest of the driver series is not. Publication remains NOT READY.
#
# INPUTS ARE NEVER FABRICATED
#   The only inputs are our DTS sources, the target kernel build product
#   (zImage) and host tools from the build tree. No vendor trailer, DTB, DTBO,
#   U-Boot image or dump is read; the script refuses input paths under
#   dumps/, firmware/, stock/ and the rootfs dumps.
#
# USAGE
#   tools/build-kernel-slice.sh --out DIR [options]
#
#   --out DIR              output directory (required; must be absent/empty
#                          unless --force)
#   --openwrt-dir DIR      OpenWrt build tree with a built linux-6.18.60
#                          target kernel (default: $HOME/openwrt-lab)
#   --kernel-dir DIR       kernel build dir override (default: discovered under
#                          <openwrt-dir>/build_dir/*/linux-hisilicon_luofu/linux-*)
#   --zimage FILE          zImage override
#   --mkimage FILE         mkimage override
#                          (default: <openwrt-dir>/staging_dir/host/bin/mkimage)
#   --dtc FILE             dtc override (default: <kernel-dir>/scripts/dtc/dtc)
#   --cpp CMD              C preprocessor (default: cpp on PATH)
#   --python3 CMD          Python 3 interpreter (default: python3)
#   --fdtoverlay FILE      libfdt overlay tool (default: the kernel tree's own
#                          scripts/dtc/fdtoverlay, else fdtoverlay on PATH)
#   --kernel-name STRING   uImage image name (default: Linux-<kernel.release>)
#   --source-date-epoch N  uImage ih_time timestamp (SOURCE_DATE_EPOCH). The
#                          default is 0 so reruns are byte-identical; bootfip
#                          does not read this field. Override to embed a real
#                          build time.
#   --base-dts FILE        base board DTS
#                          (default: target-skeleton/dts/luofu-r116.dts)
#   --overlay-dts FILE     loader-match overlay DTS
#                          (default: target-skeleton/dts/luofu-r116-overlay.dtso)
#   --loader-stage ADDR    U-Boot kernel-slot staging base (default: 0x83200000)
#   --text-offset ADDR     kernel TEXT_OFFSET (default: 0x00608000)
#   --expect-board-id N    board strap / board_id the loader must select
#                          (default: 3, measured R116 value)
#   --expect-vendor-id STR environment vendorid the overlay must match
#                          (default: hsan)
#   --match-family STR     compatible family token the loader table matches
#                          (default: luofu)
#   --expect-base-compat S first root compatible the built base DTB must carry
#                          (default: hisilicon,luofu-r116)
#   --force                allow a non-empty output directory
#   -h, --help             this text
#
# The matching environment variables (OPENWRT_DIR, KERNEL_DIR, ZIMAGE, MKIMAGE,
# DTC, CPP, PYTHON3, FDTOVERLAY, KERNEL_NAME, BASE_DTS, OVERLAY_DTS,
# LOADER_STAGE, TEXT_OFFSET, EXPECT_BOARD_ID, EXPECT_VENDOR_ID, MATCH_FAMILY,
# EXPECT_BASE_COMPAT) provide the same overrides; defaults documented above.
#
# EXIT CODES: 0 pass, 1 validation failure, 2 usage error. Every step fails
# closed: a missing input, a failed tool or a failed assertion aborts the run
# before an output file could be mistaken for valid.

set -euo pipefail
export LC_ALL=C
# Never leave __pycache__ behind when the inline assertions import fip-slice.py
# (the tool directory is part of the published skeleton and must stay clean).
export PYTHONDONTWRITEBYTECODE=1

# ---- defaults / option state ---------------------------------------------
OUT=""
FORCE=0
OPENWRT_DIR=${OPENWRT_DIR:-}
KERNEL_DIR=${KERNEL_DIR:-}
ZIMAGE=${ZIMAGE:-}
MKIMAGE=${MKIMAGE:-}
DTC=${DTC:-}
CPP=${CPP:-}
PYTHON3=${PYTHON3:-}
FDTOVERLAY=${FDTOVERLAY:-}
KERNEL_NAME=${KERNEL_NAME:-}
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-}
KERNEL_RELEASE=""
BASE_DTS=${BASE_DTS:-}
OVERLAY_DTS=${OVERLAY_DTS:-}
LOADER_STAGE=${LOADER_STAGE:-}
TEXT_OFFSET=${TEXT_OFFSET:-}
EXPECT_BOARD_ID=${EXPECT_BOARD_ID:-}
EXPECT_VENDOR_ID=${EXPECT_VENDOR_ID:-}
MATCH_FAMILY=${MATCH_FAMILY:-}
EXPECT_BASE_COMPAT=${EXPECT_BASE_COMPAT:-}
KERNEL_SLOT_SIZE=${KERNEL_SLOT_SIZE:-}

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
SKEL_DIR=$(dirname -- "$SCRIPT_DIR")
FIP_TOOL="$SCRIPT_DIR/fip-slice.py"

step() { printf '\n== %s\n' "$*"; }
info() { printf 'build-kernel-slice: %s\n' "$*"; }
die()  { printf 'build-kernel-slice: FAIL: %s\n' "$*" >&2; exit 1; }

usage() {
    sed -n 's/^# \{0,1\}//p' "$0" | sed -n '/^USAGE$/,/^EXIT CODES/p'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --out)              [ "$#" -ge 2 ] || die "--out needs a value"; OUT=$2; shift 2 ;;
        --openwrt-dir)      [ "$#" -ge 2 ] || die "--openwrt-dir needs a value"; OPENWRT_DIR=$2; shift 2 ;;
        --kernel-dir)       [ "$#" -ge 2 ] || die "--kernel-dir needs a value"; KERNEL_DIR=$2; shift 2 ;;
        --zimage)           [ "$#" -ge 2 ] || die "--zimage needs a value"; ZIMAGE=$2; shift 2 ;;
        --mkimage)          [ "$#" -ge 2 ] || die "--mkimage needs a value"; MKIMAGE=$2; shift 2 ;;
        --dtc)              [ "$#" -ge 2 ] || die "--dtc needs a value"; DTC=$2; shift 2 ;;
        --cpp)              [ "$#" -ge 2 ] || die "--cpp needs a value"; CPP=$2; shift 2 ;;
        --python3)          [ "$#" -ge 2 ] || die "--python3 needs a value"; PYTHON3=$2; shift 2 ;;
        --fdtoverlay)       [ "$#" -ge 2 ] || die "--fdtoverlay needs a value"; FDTOVERLAY=$2; shift 2 ;;
        --kernel-name)      [ "$#" -ge 2 ] || die "--kernel-name needs a value"; KERNEL_NAME=$2; shift 2 ;;
        --source-date-epoch) [ "$#" -ge 2 ] || die "--source-date-epoch needs a value"; SOURCE_DATE_EPOCH=$2; shift 2 ;;
        --base-dts)         [ "$#" -ge 2 ] || die "--base-dts needs a value"; BASE_DTS=$2; shift 2 ;;
        --overlay-dts)      [ "$#" -ge 2 ] || die "--overlay-dts needs a value"; OVERLAY_DTS=$2; shift 2 ;;
        --loader-stage)     [ "$#" -ge 2 ] || die "--loader-stage needs a value"; LOADER_STAGE=$2; shift 2 ;;
        --text-offset)      [ "$#" -ge 2 ] || die "--text-offset needs a value"; TEXT_OFFSET=$2; shift 2 ;;
        --expect-board-id)  [ "$#" -ge 2 ] || die "--expect-board-id needs a value"; EXPECT_BOARD_ID=$2; shift 2 ;;
        --expect-vendor-id) [ "$#" -ge 2 ] || die "--expect-vendor-id needs a value"; EXPECT_VENDOR_ID=$2; shift 2 ;;
        --match-family)     [ "$#" -ge 2 ] || die "--match-family needs a value"; MATCH_FAMILY=$2; shift 2 ;;
        --expect-base-compat) [ "$#" -ge 2 ] || die "--expect-base-compat needs a value"; EXPECT_BASE_COMPAT=$2; shift 2 ;;
        --force)            FORCE=1; shift ;;
        -h|--help)          usage; exit 0 ;;
        *)                  die "unknown argument '$1' (see --help)" ;;
    esac
done

[ -n "$OUT" ] || die "--out DIR is required (see --help)"

# ---- default the lab clone and locate the built kernel -------------------
OPENWRT_DIR=${OPENWRT_DIR:-"$HOME/openwrt-lab"}
[ -d "$OPENWRT_DIR" ] || die "OpenWrt build tree not found: $OPENWRT_DIR (pass --openwrt-dir)"
OPENWRT_DIR=$(cd -- "$OPENWRT_DIR" && pwd)

if [ -z "$KERNEL_DIR" ]; then
    shopt -s nullglob
    candidates=( "$OPENWRT_DIR"/build_dir/*/linux-hisilicon_luofu/linux-* )
    shopt -u nullglob
    found=()
    for d in "${candidates[@]}"; do
        [ -n "$d" ] || continue
        if [ -f "$d/arch/arm/boot/zImage" ] && [ -f "$d/scripts/dtc/dtc" ]; then
            found+=( "$d" )
        fi
    done
    if [ "${#found[@]}" -eq 0 ]; then
        die "no built linux-hisilicon_luofu kernel (zImage + dtc) under $OPENWRT_DIR/build_dir; run 'make target/linux/compile' first or pass --kernel-dir"
    fi
    if [ "${#found[@]}" -gt 1 ]; then
        printf 'candidates:\n' >&2
        printf '  %s\n' "${found[@]}" >&2
        die "more than one built kernel matches; pass --kernel-dir"
    fi
    KERNEL_DIR=${found[0]}
fi
[ -d "$KERNEL_DIR" ] || die "kernel build dir not found: $KERNEL_DIR"
KERNEL_DIR=$(cd -- "$KERNEL_DIR" && pwd)

ZIMAGE=${ZIMAGE:-"$KERNEL_DIR/arch/arm/boot/zImage"}
MKIMAGE=${MKIMAGE:-"$OPENWRT_DIR/staging_dir/host/bin/mkimage"}
DTC=${DTC:-"$KERNEL_DIR/scripts/dtc/dtc"}
CPP=${CPP:-cpp}
PYTHON3=${PYTHON3:-python3}
BASE_DTS=${BASE_DTS:-"$SKEL_DIR/dts/luofu-r116.dts"}
OVERLAY_DTS=${OVERLAY_DTS:-"$SKEL_DIR/dts/luofu-r116-overlay.dtso"}
LOADER_STAGE=${LOADER_STAGE:-0x83200000}
TEXT_OFFSET=${TEXT_OFFSET:-0x00608000}
EXPECT_BOARD_ID=${EXPECT_BOARD_ID:-3}
EXPECT_VENDOR_ID=${EXPECT_VENDOR_ID:-hsan}
MATCH_FAMILY=${MATCH_FAMILY:-luofu}
EXPECT_BASE_COMPAT=${EXPECT_BASE_COMPAT:-hisilicon,luofu-r116}
KERNEL_SLOT_SIZE=${KERNEL_SLOT_SIZE:-8650752}
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-0}
[[ "$SOURCE_DATE_EPOCH" =~ ^[0-9]+$ ]] || die "--source-date-epoch is not a decimal number: $SOURCE_DATE_EPOCH"

# ---- resolve/validate the host tools and every input ---------------------
PYTHON3=$(command -v "$PYTHON3" 2>/dev/null || true)
[ -n "$PYTHON3" ] || die "python3 not found on PATH (pass --python3)"
CPP=$(command -v "$CPP" 2>/dev/null || true)
[ -n "$CPP" ] || die "C preprocessor not found on PATH (pass --cpp)"

if [ -z "$FDTOVERLAY" ]; then
    if [ -x "$KERNEL_DIR/scripts/dtc/fdtoverlay" ]; then
        FDTOVERLAY="$KERNEL_DIR/scripts/dtc/fdtoverlay"
    else
        FDTOVERLAY=$(command -v fdtoverlay 2>/dev/null || true)
    fi
fi
[ -n "$FDTOVERLAY" ] && [ -x "$FDTOVERLAY" ] || die "fdtoverlay not found (the kernel tree's scripts/dtc/fdtoverlay or PATH); the loader's libfdt overlay apply is required, not skipped"
# fip-slice.py locates fdtoverlay with shutil.which(), so expose this exact one.
PATH="$(dirname -- "$FDTOVERLAY"):$PATH"
export PATH

require_file() { [ -f "$1" ] || die "$2 missing: $1"; }
require_exec() { [ -x "$1" ] || die "$2 not executable: $1"; }
require_file "$ZIMAGE" "zImage (build the kernel first: make target/linux/compile)"
require_exec "$MKIMAGE" "mkimage"
require_exec "$DTC" "dtc"
require_file "$FIP_TOOL" "fip-slice.py"
require_file "$BASE_DTS" "base board DTS"
require_file "$OVERLAY_DTS" "loader-match overlay DTS"

# No vendor-derived input can slip in: the inputs this script is designed for
# live in the OpenWrt build tree and target-skeleton/dts/ only.
for p in "$ZIMAGE" "$MKIMAGE" "$DTC" "$BASE_DTS" "$OVERLAY_DTS"; do
    case "$p" in
        */dumps/*|*/firmware/*|*/stock/*|*/rootfs-2.4.15/*|*/rootfs-2.5.24/*)
            die "refusing vendor-derived input path: $p" ;;
    esac
done

# ---- addresses: validated and derived, never copied blind ----------------
case "$LOADER_STAGE" in
    0[xX]*|[0-9]*) [[ "$LOADER_STAGE" =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]] || die "--loader-stage is not a number: $LOADER_STAGE" ;;
    *) die "--loader-stage is not a number: $LOADER_STAGE" ;;
esac
case "$TEXT_OFFSET" in
    0[xX]*|[0-9]*) [[ "$TEXT_OFFSET" =~ ^(0[xX][0-9a-fA-F]+|[0-9]+)$ ]] || die "--text-offset is not a number: $TEXT_OFFSET" ;;
    *) die "--text-offset is not a number: $TEXT_OFFSET" ;;
esac
[[ "$EXPECT_BOARD_ID" =~ ^[0-9]+$ ]] || die "--expect-board-id is not a decimal number: $EXPECT_BOARD_ID"
[[ "$KERNEL_SLOT_SIZE" =~ ^[0-9]+$ ]] || die "KERNEL_SLOT_SIZE is not a decimal number: $KERNEL_SLOT_SIZE"
LOADER_STAGE_VAL=$(( LOADER_STAGE ))
TEXT_OFFSET_VAL=$(( TEXT_OFFSET ))
KERNEL_LOADADDR_VAL=$(( (LOADER_STAGE_VAL & 0xF8000000) + TEXT_OFFSET_VAL ))
KERNEL_LOADADDR=$(printf '0x%08x' "$KERNEL_LOADADDR_VAL")

if [ -z "$KERNEL_NAME" ]; then
    KREL_FILE="$KERNEL_DIR/include/config/kernel.release"
    [ -f "$KREL_FILE" ] || die "cannot derive the uImage name: $KREL_FILE missing (pass --kernel-name)"
    KERNEL_RELEASE=$(tr -d '[:space:]' < "$KREL_FILE")
    [ -n "$KERNEL_RELEASE" ] || die "empty kernel release in $KREL_FILE"
    KERNEL_NAME="Linux-$KERNEL_RELEASE"
fi

# ---- output directory: clean by default, refuse to mix old artifacts -----
mkdir -p -- "$OUT"
OUT=$(cd -- "$OUT" && pwd)
if [ -n "$(ls -A -- "$OUT")" ] && [ "$FORCE" -ne 1 ]; then
    die "output directory is not empty: $OUT (use --force to overwrite known artifacts)"
fi
mkdir -p -- "$OUT/logs"

# ---- record input identity / tool versions -------------------------------
step "inputs and tools"
{
    printf 'kernel_dir: %s\n' "$KERNEL_DIR"
    [ -n "$KERNEL_RELEASE" ] && printf 'kernel_release: %s\n' "$KERNEL_RELEASE"
    printf 'uimage_name: %s\n' "$KERNEL_NAME"
    printf 'source_date_epoch: %s\n' "$SOURCE_DATE_EPOCH"
    printf 'zImage: %s bytes sha256=%s\n' "$(stat -c %s "$ZIMAGE")" "$(sha256sum "$ZIMAGE" | cut -d' ' -f1)"
    printf 'base_dts: %s sha256=%s\n' "$BASE_DTS" "$(sha256sum "$BASE_DTS" | cut -d' ' -f1)"
    printf 'overlay_dts: %s sha256=%s\n' "$OVERLAY_DTS" "$(sha256sum "$OVERLAY_DTS" | cut -d' ' -f1)"
    printf 'fip_tool: %s sha256=%s\n' "$FIP_TOOL" "$(sha256sum "$FIP_TOOL" | cut -d' ' -f1)"
    printf 'derivation: (loader_stage %s & 0xf8000000) + text_offset %s = %s\n' "$LOADER_STAGE" "$TEXT_OFFSET" "$KERNEL_LOADADDR"
    printf 'kernel_slot_size: %s\n' "$KERNEL_SLOT_SIZE"
    printf 'overlay_match: vendor_id=%s family=%s board_id=%s\n' "$EXPECT_VENDOR_ID" "$MATCH_FAMILY" "$EXPECT_BOARD_ID"
    printf -- '-- dtc --\n'; "$DTC" --version
    printf -- '-- mkimage --\n'; "$MKIMAGE" -V 2>&1 | head -1
    printf -- '-- cpp --\n'; "$CPP" --version | head -1
    printf -- '-- python3 --\n'; "$PYTHON3" --version
    printf -- '-- fip-slice --\n'; "$PYTHON3" "$FIP_TOOL" --version
    printf -- '-- fdtoverlay --\n'; "$FDTOVERLAY" --version 2>&1 | head -1
} > "$OUT/logs/00-inputs-versions.log" 2>&1
cat -- "$OUT/logs/00-inputs-versions.log"

# ---- 1. base DTB: cpp then the build tree's dtc --------------------------
step "1/7 compile base DTS -> luofu-r116.dtb"
if ! "$CPP" -nostdinc -I"$KERNEL_DIR/include" -I"$KERNEL_DIR/arch/arm/boot/dts" \
        -undef -D__DTS__ -x assembler-with-cpp \
        -o "$OUT/luofu-r116.dts.preprocessed" "$BASE_DTS" \
        > "$OUT/logs/01-cpp-base.log" 2>&1; then
    cat -- "$OUT/logs/01-cpp-base.log" >&2
    die "cpp failed on $BASE_DTS"
fi
if ! "$DTC" -I dts -O dtb -o "$OUT/luofu-r116.dtb" "$OUT/luofu-r116.dts.preprocessed" \
        > "$OUT/logs/02-dtc-base.log" 2>&1; then
    cat -- "$OUT/logs/02-dtc-base.log" >&2
    die "dtc failed on the preprocessed base DTS"
fi
base_warnings=$(grep -c 'Warning' "$OUT/logs/02-dtc-base.log" || true)
[ -f "$OUT/luofu-r116.dtb" ] || die "dtc produced no base DTB"
info "base DTB: $(stat -c %s "$OUT/luofu-r116.dtb") bytes, $base_warnings dtc warning(s) (the documented duplicate unit-address is expected)"

# ---- 2. loader-match overlay DTBO ---------------------------------------
step "2/7 compile loader-match overlay -> luofu-r116-overlay.dtbo"
if ! "$DTC" -I dts -O dtb -o "$OUT/luofu-r116-overlay.dtbo" "$OVERLAY_DTS" \
        > "$OUT/logs/03-dtc-overlay.log" 2>&1; then
    cat -- "$OUT/logs/03-dtc-overlay.log" >&2
    die "dtc failed on $OVERLAY_DTS"
fi
[ -f "$OUT/luofu-r116-overlay.dtbo" ] || die "dtc produced no overlay DTBO"
info "overlay DTBO: $(stat -c %s "$OUT/luofu-r116-overlay.dtbo") bytes"

# ---- 2b. assert the FDTs are what the loader will be given --------------
step "2b/7 assert FDT headers, root compatible and overlay match shape"
if ! "$PYTHON3" - "$FIP_TOOL" "$OUT/luofu-r116.dtb" "$OUT/luofu-r116-overlay.dtbo" \
        "$EXPECT_BASE_COMPAT" "$EXPECT_VENDOR_ID" "$EXPECT_BOARD_ID" "$MATCH_FAMILY" \
        > "$OUT/logs/04-fdt-assert.log" 2>&1 <<'PY'
import importlib.util
import struct
import sys

tool, base_path, ovl_path, exp_compat, exp_vendor, exp_board, fam = sys.argv[1:8]
exp_board = int(exp_board)

spec = importlib.util.spec_from_file_location("fipslice", tool)
fp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fp)


def read(path):
    with open(path, "rb") as fh:
        return fh.read()


def align4(x):
    return (x + 3) & ~3


def parse_node(data, pos, struct_end, strings_start, strings_end):
    if struct.unpack_from(">I", data, pos)[0] != 1:
        raise SystemExit("expected FDT_BEGIN_NODE at 0x%x" % pos)
    pos += 4
    end = data.find(b"\x00", pos, struct_end)
    if end < 0:
        raise SystemExit("unterminated node name")
    name = data[pos:end].decode("ascii")
    pos = align4(end + 1)
    props = {}
    children = {}
    while True:
        if pos + 4 > struct_end:
            raise SystemExit("truncated struct block")
        token = struct.unpack_from(">I", data, pos)[0]
        pos += 4
        if token == 1:
            child, pos = parse_node(data, pos - 4, struct_end, strings_start, strings_end)
            children[child[0]] = child
        elif token == 2:
            return (name, props, children), pos
        elif token == 3:
            length, nameoff = struct.unpack_from(">II", data, pos)
            pos += 8
            if pos + length > struct_end:
                raise SystemExit("property runs past the struct block")
            pname = fp._cstring(data, strings_start + nameoff, strings_end, "property name")
            props[pname] = bytes(data[pos:pos + length])
            pos = align4(pos + length)
        elif token == 4:
            continue
        elif token == 9:
            return (name, props, children), pos
        else:
            raise SystemExit("unexpected FDT token 0x%x" % token)


def parse_root(data, hdr, what):
    start = hdr["off_struct"]
    end = start + hdr["size_struct"]
    strings_start = hdr["off_strings"]
    strings_end = strings_start + hdr["size_strings"]
    root, _ = parse_node(data, start, end, strings_start, strings_end)
    return root


base = read(base_path)
bh = fp.parse_fdt_header(base, 0, "base DTB")
if bh["totalsize"] != len(base):
    raise SystemExit("base DTB file is %d B but its FDT totalsize is %d B" % (len(base), bh["totalsize"]))
broot = parse_root(base, bh, "base DTB")
bcompat = fp.fdt_stringlist(broot[1].get("compatible"), "base DTB compatible")
if bcompat[0] != exp_compat:
    raise SystemExit("base root compatible[0] is %r, expected %r" % (bcompat[0], exp_compat))
if "hisilicon,luofu" not in bcompat:
    raise SystemExit("base root compatible lacks the machine dt_compat token 'hisilicon,luofu'")
print("base DTB:      FDT magic ok, totalsize %d B" % bh["totalsize"])
print("base root:     compatible %s" % bcompat)
if "model" in broot[1]:
    print("base root:     model %r" % broot[1]["model"].rstrip(b"\x00").decode("ascii", "replace"))

ovl = read(ovl_path)
oh = fp.parse_fdt_header(ovl, 0, "overlay DTBO")
if oh["totalsize"] != len(ovl):
    raise SystemExit("overlay file is %d B but its FDT totalsize is %d B" % (len(ovl), oh["totalsize"]))
oinfo = fp.overlay_match_info(ovl, "overlay DTBO")
if fam not in oinfo["compatible"][0]:
    raise SystemExit("overlay compatible[0] %r lacks the loader family token %r"
                     % (oinfo["compatible"][0], fam))
if oinfo["vendor_id"] != exp_vendor:
    raise SystemExit("overlay vendor_id %r != expected %r" % (oinfo["vendor_id"], exp_vendor))
if oinfo["board_id"] != exp_board:
    raise SystemExit("overlay board_id %d != expected %d" % (oinfo["board_id"], exp_board))
oroot = parse_root(ovl, oh, "overlay DTBO")
fragments = [(n, c) for (n, c) in oroot[2].items() if n.startswith("fragment")]
if not fragments:
    raise SystemExit("overlay has no fragment@N node")
root_fragments = 0
for fname, fnode in fragments:
    fprops, fchildren = fnode[1], fnode[2]
    if "__overlay__" not in fchildren:
        raise SystemExit("%s has no __overlay__ node" % fname)
    if "target-path" in fprops:
        target_path = fprops["target-path"].rstrip(b"\x00").decode("ascii")
        if target_path == "/":
            root_fragments += 1
            overlay_node = fchildren["__overlay__"]
            if overlay_node[1] or overlay_node[2]:
                raise SystemExit("the '/' __overlay__ is not a no-op: props=%r children=%r"
                                 % (sorted(overlay_node[1]), sorted(overlay_node[2])))
if root_fragments != 1:
    raise SystemExit("expected exactly one fragment targeting '/', found %d" % root_fragments)
print("overlay DTBO:  FDT magic ok, totalsize %d B" % oh["totalsize"])
print("overlay root:  compatible %s, vendor_id %r, board_id %d"
      % (oinfo["compatible"], oinfo["vendor_id"], oinfo["board_id"]))
print("overlay shape: %d fragment(s), exactly one targets '/', no-op __overlay__"
      % len(fragments))
print("FDT ASSERT: PASS")
PY
then
    cat -- "$OUT/logs/04-fdt-assert.log" >&2
    die "FDT assertions failed (base DTB / overlay)"
fi
cat -- "$OUT/logs/04-fdt-assert.log"

# ---- 3. legacy uImage from the 6.18 zImage ------------------------------
step "3/7 mkimage -> uImage ($KERNEL_NAME, load/entry $KERNEL_LOADADDR)"
# mkimage honors SOURCE_DATE_EPOCH for ih_time; the default 0 makes reruns
# byte-identical. bootfip never reads ih_time (TRAILER-FORMAT.md section 4).
export SOURCE_DATE_EPOCH
if ! "$MKIMAGE" -A arm -O linux -T kernel -C none \
        -a "$KERNEL_LOADADDR" -e "$KERNEL_LOADADDR" \
        -n "$KERNEL_NAME" -d "$ZIMAGE" "$OUT/uImage" \
        > "$OUT/logs/05-mkimage.log" 2>&1; then
    cat -- "$OUT/logs/05-mkimage.log" >&2
    die "mkimage failed"
fi
[ -f "$OUT/uImage" ] || die "mkimage produced no uImage"

step "3b/7 assert uImage identity, load/entry derivation and payload size"
if ! "$PYTHON3" - "$FIP_TOOL" "$OUT/uImage" "$ZIMAGE" "$KERNEL_NAME" "$KERNEL_LOADADDR" \
        > "$OUT/logs/06-uimage-assert.log" 2>&1 <<'PY'
import importlib.util
import os
import sys

tool, uimage_path, zimage_path, kernel_name, load_hex = sys.argv[1:6]
expected = int(load_hex, 0)

spec = importlib.util.spec_from_file_location("fipslice", tool)
fp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fp)

with open(uimage_path, "rb") as fh:
    data = fh.read()
u = fp.parse_uimage(data, uimage_path)
zsize = os.path.getsize(zimage_path)
if u["ih_size"] != zsize:
    raise SystemExit("uImage ih_size %d != zImage size %d" % (u["ih_size"], zsize))
if u["ih_load"] != expected:
    raise SystemExit("uImage ih_load 0x%08x != derived 0x%08x" % (u["ih_load"], expected))
if u["ih_ep"] != expected:
    raise SystemExit("uImage ih_ep 0x%08x != derived 0x%08x" % (u["ih_ep"], expected))
if u["name"] != kernel_name:
    raise SystemExit("uImage name %r != %r" % (u["name"], kernel_name))
ih_os, ih_arch, ih_type, ih_comp = data[28], data[29], data[30], data[31]
if (ih_os, ih_arch, ih_type, ih_comp) != (5, 2, 2, 0):
    raise SystemExit("uImage identity bytes (os,arch,type,comp)=%r, want (5,2,2,0) = Linux/ARM/kernel/none"
                     % ((ih_os, ih_arch, ih_type, ih_comp),))
print("uImage:        name %r, ih_size %d B, load 0x%08x, ep 0x%08x"
      % (u["name"], u["ih_size"], u["ih_load"], u["ih_ep"]))
print("uImage:        os=Linux(5) arch=ARM(2) type=kernel(2) comp=none(0), hcrc ok, dcrc set")
print("uImage ASSERT: PASS")
PY
then
    cat -- "$OUT/logs/06-uimage-assert.log" >&2
    die "uImage assertions failed"
fi
cat -- "$OUT/logs/06-uimage-assert.log"

# ---- 4. FIP trailer from our base DTB + our overlay ---------------------
step "4/7 make-trailer (base DTB + overlay, libfdt apply required)"
if ! "$PYTHON3" "$FIP_TOOL" make-trailer \
        --base-dtb "$OUT/luofu-r116.dtb" \
        --overlay "$OUT/luofu-r116-overlay.dtbo" \
        --output "$OUT/fip-trailer.bin" \
        --expect-board-id "$EXPECT_BOARD_ID" \
        --expect-vendor-id "$EXPECT_VENDOR_ID" \
        --match-family "$MATCH_FAMILY" \
        --overlay-apply require \
        --force \
        > "$OUT/logs/07-make-trailer.log" 2>&1; then
    cat -- "$OUT/logs/07-make-trailer.log" >&2
    die "fip-slice.py make-trailer failed"
fi
grep -q 'FIP trailer generation: PASS' "$OUT/logs/07-make-trailer.log" || die "make-trailer did not report PASS"
cat -- "$OUT/logs/07-make-trailer.log"

# ---- 5. assemble uImage + trailer into the kernel slice -----------------
step "5/7 assemble kernel slice (uImage + trailer)"
if ! "$PYTHON3" "$FIP_TOOL" assemble \
        --uimage "$OUT/uImage" \
        --trailer "$OUT/fip-trailer.bin" \
        --output "$OUT/luofu-kernel-slice.bin" \
        --max-size "$KERNEL_SLOT_SIZE" \
        --loader-stage "$LOADER_STAGE" \
        --text-offset "$TEXT_OFFSET" \
        --force \
        > "$OUT/logs/08-assemble.log" 2>&1; then
    cat -- "$OUT/logs/08-assemble.log" >&2
    die "fip-slice.py assemble failed"
fi
grep -q 'FIP slice check: PASS' "$OUT/logs/08-assemble.log" || die "assemble did not report PASS"
cat -- "$OUT/logs/08-assemble.log"

# ---- 6. check the slice and re-read the gate/descriptor independently ---
step "6/7 fip-slice.py check on the assembled slice"
if ! "$PYTHON3" "$FIP_TOOL" check "$OUT/luofu-kernel-slice.bin" \
        --max-size "$KERNEL_SLOT_SIZE" > "$OUT/logs/09-check-slice.log" 2>&1; then
    cat -- "$OUT/logs/09-check-slice.log" >&2
    die "fip-slice.py check failed"
fi
grep -q 'FIP slice check: PASS' "$OUT/logs/09-check-slice.log" || die "check did not report PASS"
cat -- "$OUT/logs/09-check-slice.log"

step "6b/7 independent gate / descriptor / slot-budget re-read"
if ! "$PYTHON3" - "$FIP_TOOL" "$OUT/luofu-kernel-slice.bin" "$KERNEL_NAME" \
        "$LOADER_STAGE" "$TEXT_OFFSET" "$KERNEL_SLOT_SIZE" \
        > "$OUT/logs/10-gate-assert.log" 2>&1 <<'PY'
import importlib.util
import sys

tool, slice_path, kernel_name, loader_stage, text_offset, max_size = sys.argv[1:7]
loader_stage = int(loader_stage, 0)
text_offset = int(text_offset, 0)
max_size = int(max_size)

spec = importlib.util.spec_from_file_location("fipslice", tool)
fp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fp)

with open(slice_path, "rb") as fh:
    data = fh.read()

info = fp.validate_slice_bytes(data, slice_path, max_size)
u = info["uimage"]
t = info["trailer"]
expected = (loader_stage & 0xF8000000) + text_offset
if u["ih_load"] != expected or u["ih_ep"] != expected:
    raise SystemExit("load/entry 0x%08x/0x%08x != derived 0x%08x"
                     % (u["ih_load"], u["ih_ep"], expected))
if u["name"] != kernel_name:
    raise SystemExit("uImage name %r != %r" % (u["name"], kernel_name))
if info["gate_offset"] != 0x40 + u["ih_size"] + t["base_total"]:
    raise SystemExit("gate offset 0x%x != 0x40 + ih_size + base totalsize"
                     % info["gate_offset"])
if info["gate_value"] != 0xD7B7AB1E:
    raise SystemExit("gate word 0x%08x != 0xd7b7ab1e" % info["gate_value"])
if t["container_total"] != t["descriptor_len"] + sum(e["size"] for e in t["entries"]):
    raise SystemExit("descriptor container total %d != 32 + 32*count + sum(sizes)"
                     % t["container_total"])
if len(data) > max_size:
    raise SystemExit("slice %d B over the %d-byte slot budget" % (len(data), max_size))
hdr = data[:0x40]
if data[0:4] != b"\x27\x05\x19\x56":
    raise SystemExit("uImage magic bytes wrong")
if (hdr[28], hdr[29], hdr[30], hdr[31]) != (5, 2, 2, 0):
    raise SystemExit("uImage identity bytes wrong")
headroom = max_size - len(data)
payload_end = u["payload_end"]
print("slice:         %d B <= slot budget %d B (headroom %d B)" % (len(data), max_size, headroom))
print("gate:          0x%08x at slice offset 0x%x = 0x40 + ih_size 0x%x + base totalsize 0x%x"
      % (info["gate_value"], info["gate_offset"], u["ih_size"], t["base_total"]))
print("base DTB:      totalsize %d B at slice offset 0x%x" % (t["base_total"], payload_end))
print("descriptor:    %d B = 32-byte header + %d x 32-byte entries; container total %d B"
      % (t["descriptor_len"], t["overlay_count"], t["container_total"]))
for i, entry in enumerate(t["entries"]):
    start = payload_end + t["descriptor_offset"] + entry["offset"]
    ov = data[start:start + entry["size"]]
    oi = fp.overlay_match_info(ov, "slice overlay %d" % i)
    print("overlay %-3d    %d B at slice offset 0x%x, compatible %r, vendor_id %r, board_id %d"
          % (i, entry["size"], start, oi["compatible"][0], oi["vendor_id"], oi["board_id"]))
print("uImage:        name %r, ih_size %d B, load/ep 0x%08x/0x%08x, identity Linux/ARM/kernel/none"
      % (u["name"], u["ih_size"], u["ih_load"], u["ih_ep"]))
print("GATE ASSERT: PASS")
PY
then
    cat -- "$OUT/logs/10-gate-assert.log" >&2
    die "independent gate/descriptor assertion failed"
fi
cat -- "$OUT/logs/10-gate-assert.log"

# ---- 7. manifest + hashes ------------------------------------------------
step "7/7 manifest and artifact hashes"
if ! "$PYTHON3" "$FIP_TOOL" manifest \
        --slice "$OUT/luofu-kernel-slice.bin" \
        --output "$OUT/fip-manifest.txt" \
        --max-size "$KERNEL_SLOT_SIZE" \
        --force \
        > "$OUT/logs/11-manifest.log" 2>&1; then
    cat -- "$OUT/logs/11-manifest.log" >&2
    die "fip-slice.py manifest failed"
fi
grep -q 'fip manifest: PASS' "$OUT/logs/11-manifest.log" || die "manifest did not report PASS"
cat -- "$OUT/logs/11-manifest.log"

( cd -- "$OUT" && sha256sum \
    luofu-r116.dtb \
    luofu-r116-overlay.dtbo \
    uImage \
    fip-trailer.bin \
    luofu-kernel-slice.bin \
    fip-manifest.txt ) > "$OUT/SHA256SUMS"
( cd -- "$OUT" && stat -c '%s %n' \
    luofu-r116.dtb \
    luofu-r116-overlay.dtbo \
    uImage \
    fip-trailer.bin \
    luofu-kernel-slice.bin \
    fip-manifest.txt ) > "$OUT/SIZES"
sha256sum "$ZIMAGE" "$BASE_DTS" "$OVERLAY_DTS" "$FIP_TOOL" > "$OUT/INPUTS.sha256"

slice_size=$(stat -c %s "$OUT/luofu-kernel-slice.bin")
slice_sha=$(sha256sum "$OUT/luofu-kernel-slice.bin" | cut -d' ' -f1)
gate_offset=$(sed -n 's/^gate-offset: //p' "$OUT/fip-manifest.txt")
cat -- "$OUT/SHA256SUMS"
cat -- "$OUT/SIZES"

step "result"
printf '%s\n' \
    "build-kernel-slice: PASS (structural only; the slice has NEVER been booted)" \
    "  artifacts:   $OUT" \
    "  uImage:      $OUT/uImage ($(stat -c %s "$OUT/uImage") B, load/ep $KERNEL_LOADADDR, name '$KERNEL_NAME')" \
    "  trailer:     $OUT/fip-trailer.bin ($(stat -c %s "$OUT/fip-trailer.bin") B)" \
    "  slice:       $OUT/luofu-kernel-slice.bin ($slice_size B, gate at $gate_offset)" \
    "  slice sha256: $slice_sha" \
    "  manifest:    $OUT/fip-manifest.txt" \
    "" \
    "PROVEN: the decoded bootfip structure (FDT chain, overlay-container descriptor," \
    "        container total, overlay match props, libfdt apply) and the derived" \
    "        load/entry address $KERNEL_LOADADDR; slot budget respected." \
    "NOT PROVEN: that the stock loader accepts this NOVEL slice or that the kernel" \
    "        boots. One attended boot on a recovered unit is still required" \
    "        (TARGET-DESIGN.md 5.2, 9). No rootfs, no sysupgrade container, no" \
    "        install path, and only the first two drivers (CRG, watchdog) ported;" \
    "        the rest of the driver series is not. Publication NOT READY."
