#!/bin/sh
# Host test for the g0 id36 hmac_scan_config_bss_color builder (lane
# wifi-bss-color, task-233). Pure C99, no device, no kernel tree needed.
set -eu
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
OUT="${TMPDIR:-/tmp}/hi5622_bss_color_selftest"
$CC -std=c99 -Wall -Wextra -Werror -pedantic -I src \
	src/bss_color_selftest.c src/hi5622_bss_color.c -o "$OUT"
exec "$OUT"
