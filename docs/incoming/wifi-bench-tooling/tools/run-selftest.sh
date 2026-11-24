#!/bin/sh
# run-selftest.sh - device-free selftest for the wifi-bench-tooling lane.
#
# usage: ./tools/run-selftest.sh [-v]
# Host-only: synthetic data in a temporary directory; never reads the vendor
# firmware blob and never contacts the router.
set -eu
PY=${PY:-python3}
exec "$PY" "$(dirname "$0")/selftest.py" "$@"
