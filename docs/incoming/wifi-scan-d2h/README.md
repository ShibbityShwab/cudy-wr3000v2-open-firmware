# wifi-scan-d2h - scan result D2H RE + cfg80211 BSS decode

Shared task **task-118** (lane wifi-scan-d2h), host-only, 2026-11-26.
Cudy WR3000 v2.0 / Hi5622V100 clean-room host.

**Read REPORT.md first.** It contains the full decode, evidence markers, the
firmware precondition proof (why the 2026-11-26 scan produced no results), the
bench observables and the open items.

## Contents

| path | what |
| --- | --- |
| REPORT.md | main deliverable: D2H scan formats, firmware conditions, implementation, bench observables |
| src/hi5622_scan_d2h.h | ids/structs/API for the tolerant scan D2H decoder |
| src/hi5622_scan_d2h.c | portable C99 decoder (host + 6.18.60 kernel) |
| patches/0001-hi5622-scan-d2h-decoder.patch | integration into the wifi-scan-ap provider (build + parse_scan_result + parse_scan_complete) |
| tools/scan_d2h_selftest.c | host unit tests (synthetic frames) |
| tools/scan_d2h_dump.c | deterministic field dump for test-vector validation |
| tools/gen_vectors.py | generates/validates testvectors/scan-d2h-vectors.json |
| tools/fw_scan_d2h_re.py | regenerable static evidence: firmware sender CA 0x641B4, scan-request handler CA 0x6494C/0x644A4, host handler files/offsets |
| tools/kernel-check.sh | optional 6.18.60 module-context compile check (scratch copy, no image, no device) |
| tools/selftest.sh | runs all of the above (no device, no network) |
| recipes/bench-observables.md | copy-paste bench checklist (read-only) |
| recipes/scan-d2h-ladder.json | machine-readable ladder/observables |
| testvectors/scan-d2h-vectors.json | synthetic vectors, cross-checked against the C decoder |

## Quick start

~~~
cd opensource/docs/incoming/wifi-scan-d2h
./tools/selftest.sh              # host checks (decoder, vectors, RE evidence)
./tools/kernel-check.sh          # optional: 6.18.60 module build (needs the lab tree)
~~~

## Status at delivery

- host decoder selftest: 4076/4076 PASS
- RE evidence self-check: 50/50 PASS
- test vectors: 85/85 PASS
- kernel-context compile: PASS, 0 warnings (hi5622_wifi.ko 703,476 B)
- no device contact, no vendor bytes committed
