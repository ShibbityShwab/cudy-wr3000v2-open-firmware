#!/bin/bash
# run-host-tests.sh - host-only test suite for the H2D trace decode (lane rx-h2d-trace/task-176).
# No device, no network, no vendor binary: synthetic vectors from the documented wire formats.
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../../../.." && pwd)
OUT="$ROOT/build/wifi-rx-h2d-trace/tests"
mkdir -p "$OUT"
FAIL=0
say() { printf '%s\n' "$*"; }

say "== tools/decode_h2d.py --selftest =="
python3 "$HERE/decode_h2d.py" --selftest || FAIL=1

say ""
say "== vectors =="
python3 "$HERE/make_test_vectors.py" --out "$OUT" || FAIL=1

say ""
say "== sampler-format decode + diff =="
python3 "$HERE/decode_h2d.py" --input "$OUT/sampler.log" --ours "$HERE/ours_h2d_set.txt" \
	--diff --jsonl "$OUT/messages.jsonl" 2>"$OUT/sampler.err" | tee "$OUT/sampler-report.txt"
grep -q "CAPTURE VALID: 6 messages, 5 families, 0.5 seconds" "$OUT/sampler-report.txt" \
	&& say "  [PASS] sampler: 6 valid / 5 families / 0.5 s" \
	|| { say "  [FAIL] sampler valid-count/families/span"; FAIL=1; }
grep -q "quarantined=2" "$OUT/sampler-report.txt" \
	&& say "  [PASS] sampler: 2 quarantined" \
	|| { say "  [FAIL] sampler quarantine count"; FAIL=1; }
for want in "\[matched\] 2" "\[drift\] 1" "\[missing\] 1" "\[core\] 1" "\[g2\] 1"; do
	grep -q "$want" "$OUT/sampler-report.txt" \
		&& say "  [PASS] bucket $want" \
		|| { say "  [FAIL] bucket $want"; FAIL=1; }
done
grep -q "g0/key/0x0004" "$OUT/sampler-report.txt" \
	&& say "  [PASS] the F1-a candidate (key 0x0004) surfaces in MISSING-IN-OURS" \
	|| { say "  [FAIL] key 0x0004 not reported missing"; FAIL=1; }
grep -q "g1/id/0x0003" "$OUT/sampler-report.txt" \
	&& say "  [PASS] the band-core dimension surfaces" \
	|| { say "  [FAIL] band-core message not reported"; FAIL=1; }
test -s "$OUT/messages.jsonl" \
	&& say "  [PASS] JSONL written ($(wc -l < "$OUT/messages.jsonl") records)" \
	|| { say "  [FAIL] JSONL empty"; FAIL=1; }

say ""
say "== kprobe-format decode =="
python3 "$HERE/decode_h2d.py" --format kprobe --input "$OUT/kprobe.log" \
	| tee "$OUT/kprobe-report.txt"
grep -q "CAPTURE VALID: 2 messages" "$OUT/kprobe-report.txt" \
	&& say "  [PASS] kprobe: 2 valid messages" \
	|| { say "  [FAIL] kprobe decode"; FAIL=1; }

say ""
say "== bulk-format decode (save_hostmem path) =="
python3 "$HERE/decode_h2d.py" --format bulk --dump "$OUT/bulk.bin" \
	--dump-base 0x82480000 --nodebase 0x82481000 --depth 32 \
	| tee "$OUT/bulk-report.txt"
grep -q "CAPTURE VALID: 2 messages" "$OUT/bulk-report.txt" \
	&& say "  [PASS] bulk: 2 valid messages" \
	|| { say "  [FAIL] bulk decode"; FAIL=1; }

say ""
say "== device sampler cross-build =="
if "$HERE/build-sampler.sh" >"$OUT/build-sampler.txt" 2>&1; then
	say "  [PASS] $(tail -3 "$OUT/build-sampler.txt" | head -1)"
	grep -q "ELF 32-bit" "$OUT/build-sampler.txt" \
		&& say "  [PASS] static ARM ELF produced" \
		|| { say "  [FAIL] not an ARM ELF"; FAIL=1; }
else
	say "  [SKIP] no cross toolchain (host-only decode tests above still valid)"
fi

say ""
if [ "$FAIL" = 0 ]; then
	say "ALL HOST TESTS PASS"
else
	say "HOST TESTS FAILED"
fi
exit "$FAIL"
