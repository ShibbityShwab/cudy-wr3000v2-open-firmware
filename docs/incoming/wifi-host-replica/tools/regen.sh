#!/bin/sh
# regen.sh - host-only regeneration of the wifi-host-replica evidence.
# Usage: ./regen.sh <module.ko> <module.ko> ...
# Writes annotated listings + the report's key tables under a scratch dir
# (default /tmp/hr-regen).  Nothing vendor-derived is committed.
set -e
TC=${TC:-/home/shibbityshwab/openwrt-lab-selectorfix-img/staging_dir/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin/arm-openwrt-linux-muslgnueabi-}
OUT=${OUT:-/tmp/hr-regen}
mkdir -p "$OUT"
HERE=$(cd "$(dirname "$0")" && pwd)

for ko in "$@"; do
    base=$(basename "$ko" .ko)
    "$TC"objdump -dr --no-show-raw-insn "$ko" > "$OUT/$base.s"
    python3 "$HERE/annotate.py" "$ko" "$OUT/$base.s" "$OUT/$base.ann.s"
    "$TC"nm -n "$ko" > "$OUT/$base.nm"
    echo "annotated: $OUT/$base.ann.s ($(wc -l < "$OUT/$base.ann.s") lines)"
done

# key tables from the 2.5.24 plat module
PLAT=/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko
if [ -f "$PLAT" ]; then
    echo "== boot HCC table (chip group 4, .data+0x2660)"
    python3 "$HERE/tables.py" "$PLAT" .data 0x2660 5 12
    echo "== quit descriptor (.data+0x26a4)"
    python3 "$HERE/tables.py" "$PLAT" .data 0x26a4 3 4
    echo "== ETE channel config (.rodata+0x101c)"
    python3 "$HERE/tables.py" "$PLAT" .rodata 0x101c 7 12
    echo "== dev-status table (.rodata+0xcac)"
    python3 "$HERE/tables.py" "$PLAT" .rodata 0xcac 11 8
    echo "== exception dump ranges (.data+0x26b8)"
    python3 "$HERE/tables.py" "$PLAT" .data 0x26b8 3 16
fi
