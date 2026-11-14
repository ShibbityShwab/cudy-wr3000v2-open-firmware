#!/bin/sh
# patch-vendor-modules.sh - make the vendor's binary-only .ko files load on a stock ARM kernel.
#
# The vendor built every module for a no-PLT kernel, so they carry no .plt / .init.plt sections and
# module_frob_arch_sections() rejects them outright ("module PLT section(s) missing"). Appending two
# zero-size sections satisfies the loader without touching code or relocations. This is safe only when
# the module is placed within branch range of the kernel text; see docs/VENDOR-STACK-ON-MAINLINE.md.
#
# Usage: OBJCOPY=... patch-vendor-modules.sh <dir-with-.ko-files>
set -eu

OBJCOPY=${OBJCOPY:-arm-none-linux-gnueabihf-objcopy}
DIR=${1:?usage: patch-vendor-modules.sh <dir>}

if ! command -v "$OBJCOPY" >/dev/null 2>&1; then
    echo "error: $OBJCOPY not found; set OBJCOPY to an ARM binutils objcopy" >&2
    exit 1
fi

EMPTY=$(mktemp)
trap 'rm -f "$EMPTY"' EXIT INT TERM
: > "$EMPTY"

ok=0
fail=0
for f in "$DIR"/*.ko; do
    [ -e "$f" ] || continue
    if "$OBJCOPY" --add-section .plt="$EMPTY" --add-section .init.plt="$EMPTY" "$f" 2>/dev/null; then
        ok=$((ok + 1))
    else
        echo "FAILED: $f" >&2
        fail=$((fail + 1))
    fi
done

echo "patched=$ok failed=$fail"
[ "$fail" -eq 0 ]
