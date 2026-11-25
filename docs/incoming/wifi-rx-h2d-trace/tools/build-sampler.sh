#!/bin/bash
# build-sampler.sh - cross-compile the read-only stock H2D sampler (static ARM/musl, local artifact).
# usage: tools/build-sampler.sh [openwrt-lab-clone]
# The output goes to build/wifi-rx-h2d-trace/omo_h2dsampler (gitignored; never committed).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)
CL=${1:-${OPENWRT_LAB:-/home/shibbityshwab/openwrt-lab-selectorfix-img}}
CC=$(ls "$CL"/staging_dir/toolchain-*/bin/*-muslgnueabi-gcc 2>/dev/null | head -1)
[ -n "$CC" ] || CC=$(ls "$CL"/staging_dir/toolchain-*/bin/*-gcc 2>/dev/null | head -1)
if [ -z "$CC" ]; then
	echo "build-sampler: no ARM cross gcc under $CL/staging_dir/toolchain-*/bin" >&2
	exit 2
fi
export STAGING_DIR="$CL/staging_dir"

STRIP=$(dirname "$CC")/arm-openwrt-linux-muslgnueabi-strip
[ -x "$STRIP" ] || STRIP=$(dirname "$CC")/arm-openwrt-linux-strip
[ -x "$STRIP" ] || STRIP=
OUTDIR="$ROOT/build/wifi-rx-h2d-trace"
OUT="$OUTDIR/omo_h2dsampler"
mkdir -p "$OUTDIR"
"$CC" -O2 -static -Wall -Wextra -D_FILE_OFFSET_BITS=64 -o "$OUT" "$HERE/omo_h2dsampler.c" || exit 1
[ -n "$STRIP" ] && "$STRIP" "$OUT" 2>/dev/null
echo "CC=$CC"
file "$OUT"
ls -l "$OUT"
sha256sum "$OUT"
exit 0
