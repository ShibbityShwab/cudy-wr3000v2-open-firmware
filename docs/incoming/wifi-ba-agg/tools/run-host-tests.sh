#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# run-host-tests.sh - host selftest for the Hi5622V100 fail-closed BA core
# (lane wifi-ba-agg, task-178).  No device, no kernel, no vendor artifact.
set -eu

cd "$(dirname "$0")/.."
CC=${CC:-gcc}
BIN=${BIN:-/tmp/hi5622_ba_selftest}

echo "== ba_selftest (compiles src/hi5622_ba.c standalone) =="
"$CC" -std=c99 -O2 -Wall -Wextra -Werror -DHI5622_BA_HOST_TEST \
	-Isrc src/ba_selftest.c src/hi5622_ba.c -o "$BIN"
"$BIN"
