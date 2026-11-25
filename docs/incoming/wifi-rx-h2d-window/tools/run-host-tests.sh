#!/bin/bash
# run-host-tests.sh - host-side tests for the v2 (mmap) H2D sampler, lane rx-h2d-window.
# Builds a NATIVE copy, runs --selftest, then exercises the mmap window path and the
# ring-detection logic against a crafted fake window file (OMO_H2D_FAKE_WIN test hook).
# No device, no /dev/mem, no root needed. Exit 0 iff every check passes.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)
OUTDIR="$ROOT/build/wifi-rx-h2d-window"
CC=${HOSTCC:-cc}
PY=${PY:-python3}
BIN="$OUTDIR/sampler-host"
FAKE="$OUTDIR/fake-window.bin"
ZERO="$OUTDIR/fake-window-zero.bin"
LOG="$OUTDIR/fake-run.log"
NEGLOG="$OUTDIR/fake-run-zero.log"
PASS=0; FAIL=0
ok()   { PASS=$((PASS+1)); echo "PASS: $*"; }
bad()  { FAIL=$((FAIL+1)); echo "FAIL: $*"; }
mkdir -p "$OUTDIR"
rm -f "$BIN" "$FAKE" "$ZERO" "$LOG" "$NEGLOG"

echo "== native build =="
"$CC" -O1 -g -Wall -Wextra -D_FILE_OFFSET_BITS=64 -o "$BIN" "$HERE/omo_h2dsampler-mmap.c" || { echo "BUILD FAILED"; exit 1; }
ok "native build (-Wall -Wextra, no warnings above)"

echo "== --selftest (ring validator + window/register address math) =="
if "$BIN" --selftest; then ok "--selftest"; else bad "--selftest"; fi

echo "== fake window fixture =="
"$PY" - "$FAKE" "$ZERO" <<'EOF'
import struct, sys
def mk(path, base, depth, prod, cons):
    b = bytearray(0x2000)
    def put(off, v): struct.pack_into('<I', b, off, v)
    put(0x408, 0); put(0x410, base); put(0x414, depth); put(0x418, prod); put(0x41c, cons)
    open(path, 'wb').write(b)
mk(sys.argv[1], 0x848F6000, 0x1f, 0x14, 0x14)   # live-looking SR ch0
mk(sys.argv[2], 0, 0, 0, 0)                      # unprogrammed window
EOF
[ -s "$FAKE" ] && [ -s "$ZERO" ] && ok "fixtures written" || bad "fixtures"

echo "== -p probe on a live-looking window (mmap path) =="
if OMO_H2D_FAKE_WIN="$FAKE" "$BIN" -p -w 0x583f2000; then ok "-p ring=YES (exit 0)"; else bad "-p ring=YES"; fi

echo "== -p probe on an unprogrammed window =="
if OMO_H2D_FAKE_WIN="$ZERO" "$BIN" -p -w 0x583f2000; then bad "-p must exit 1 when ring=NO"; else ok "-p ring=NO (exit 1)"; fi

echo "== ring detection + ring-seen (fake window, no DRAM) =="
if OMO_H2D_FAKE_WIN="$FAKE" "$BIN" -o "$LOG" -t 1 -r 10 -W 5 -q; then ok "capture exit 0"; else bad "capture exit"; fi
grep -q 'omo_h2dsampler v2' "$LOG" && ok "log header v2" || bad "log header v2"
grep -q 'msg=win-probe' "$LOG" && ok "log win-probe" || bad "log win-probe"
grep -q 'msg=ring-seen' "$LOG" && ok "log ring-seen" || bad "log ring-seen"
grep -q 'msg=heartbeat misreads=' "$LOG" && ok "log heartbeat with misreads" || bad "log heartbeat"

echo "== negative: unprogrammed window logs ring-not-seen + last regs =="
OMO_H2D_FAKE_WIN="$ZERO" "$BIN" -o "$NEGLOG" -t 1 -r 10 -W 1 -q
rc=$?
if [ "$rc" = "4" ]; then ok "negative exit 4"; else bad "negative exit 4 (got $rc)"; fi
grep -q 'msg=ring-not-seen.*last=base=0x00000000' "$NEGLOG" && ok "negative logs last regs" || bad "negative logs last regs"

echo "== summary =="
echo "HOST TESTS: $PASS passed, $FAIL failed"
[ "$FAIL" = "0" ]
