#!/bin/bash
# build-sampler-mmap.sh - cross-compile the v2 (mmap) read-only stock H2D sampler.
# Usage: tools/build-sampler-mmap.sh [openwrt-lab-clone]
# Output: build/wifi-rx-h2d-window/omo_h2dsampler-mmap (gitignored local artifact).
# Lane rx-h2d-window / task-194. v1 builder: wifi-rx-h2d-trace/tools/build-sampler.sh.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)
CL=${1:-${OPENWRT_LAB:-/home/shibbityshwab/openwrt-lab-selectorfix-img}}
CC=$(ls "$CL"/staging_dir/toolchain-*/bin/*-muslgnueabi-gcc 2>/dev/null | head -1)
[ -n "$CC" ] || CC=$(ls "$CL"/staging_dir/toolchain-*/bin/*-gcc 2>/dev/null | head -1)
if [ -z "$CC" ]; then
	echo "build-sampler-mmap: no ARM cross gcc under $CL/staging_dir/toolchain-*/bin" >&2
	exit 2
fi
export STAGING_DIR="$CL/staging_dir"

STRIP=$(dirname "$CC")/arm-openwrt-linux-muslgnueabi-strip
[ -x "$STRIP" ] || STRIP=$(dirname "$CC")/arm-openwrt-linux-strip
[ -x "$STRIP" ] || STRIP=
OUTDIR="$ROOT/build/wifi-rx-h2d-window"
OUT="$OUTDIR/omo_h2dsampler-mmap"
mkdir -p "$OUTDIR"
"$CC" -O2 -static -Wall -Wextra -D_FILE_OFFSET_BITS=64 -o "$OUT" \
	"$HERE/omo_h2dsampler-mmap.c" || exit 1
[ -n "$STRIP" ] && "$STRIP" "$OUT" 2>/dev/null
echo "CC=$CC"
file "$OUT"
ls -l "$OUT"
sha256sum "$OUT"
exit 0
