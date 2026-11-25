#!/bin/sh
# Host-only test runner for the fw-f1-capture lane. No device contact, no vendor bytes.
set -eu
cd "$(dirname "$0")/.."
echo "== decode_f1_capture.py --selftest"
python3 tools/decode_f1_capture.py --selftest
echo
echo "== decode_f1_capture.py on a local capture (optional; skipped if absent)"
if [ -n "${1:-}" ] && [ -f "${1}" ]; then
	python3 tools/decode_f1_capture.py "${1}" | sed -n '1,12p'
else
	echo "skipped (pass the capture path as argv[1] to decode it)"
fi
