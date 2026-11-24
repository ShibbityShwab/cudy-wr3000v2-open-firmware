#!/bin/sh
# Host-only build + selftest for the wifi-datapath lane. No device access, no vendor input.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
OUT="${OUT:-/tmp/wifi-datapath-build}"
mkdir -p "$OUT"
echo "== compile (-std=c99 -Wall -Wextra -Werror -pedantic) =="
$CC -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow -Wstrict-prototypes \
    -Wmissing-prototypes -I src -o "$OUT/dp_selftest" src/dp_selftest.c src/dp_frames.c
echo "== run selftest =="
"$OUT/dp_selftest"
echo "BUILD OK (host-only; no device contact)"
