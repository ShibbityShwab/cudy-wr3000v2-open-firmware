#!/bin/sh
# Host-only tests for the wifi-stock-h2d-census lane. No device access.
set -eu
cd "$(dirname "$0")/.."
echo "== decode_stock_census.py --selftest =="
python3 tools/decode_stock_census.py --selftest
if [ -r /tmp/h2d-stock.log ]; then
  echo "== census on the local capture =="
  python3 tools/decode_stock_census.py --input /tmp/h2d-stock.log | head -14
else
  echo "capture /tmp/h2d-stock.log absent - selftest only (expected on a fresh host)"
fi
echo "HOST-TESTS: PASS"
