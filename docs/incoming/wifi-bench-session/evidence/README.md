# evidence

Session evidence is written by `host/session_driver.py` to
`build/wifi-bench-session/<UTC stamp>/` (gitignored project evidence; /tmp is
never evidence):

| path | content |
| --- | --- |
| `s/` | phase S bench_session dir: session-plan.json, device-script.sh, report.txt (all device READ lines + dmesg), summary.json, protocol-evidence.json, PROTOCOL-SEQUENCE.md, writes-host.jsonl, driver-run.log |
| `a/` | phase A base session dir + `ap-fire-report.txt`, `ap-readback-report.txt`, `ap-fire.log`, `ap-readback.log` |
| `x/` | phase X (dp-merge F-AIR ladder) bench_session dir |
| `SESSION-REPORT.md` | phase table + pending dependencies + cleanup reboot note |
| `session.json` | machine-readable record (records, pin table, pending) |
| `cleanup-reboot.log` | the hardened reboot-and-wait result |

After the attended session: copy the verdict lines (golden page, scan model
line, `scan DONE`, frame-post lines, AP readback, dp_tx_status) into the
ledger entry / EVIDENCE-INDEX with the regeneration command, then delete
nothing - the stamps are the audit trail.

This directory in git only holds this README: no logs are committed (a public
notebook never publishes raw device dumps with per-unit values without
review).
