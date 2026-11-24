#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-only
# host-only selftest for the wifi-perf-baseline harness (no device contact)
set -eu
DIR=$(cd "$(dirname "$0")" && pwd)
exec python3 "$DIR/selftest.py" "$@"
