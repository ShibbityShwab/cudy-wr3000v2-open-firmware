# wifi-h2d-send - the first REAL post-ready H2D message path (g0 id1 TXQ config sync)

Host-only deliverable of the wifi-h2d-send lane (shared task **task-98**,
2026-11-24; extended by **task-99** with the 72-byte live-node-length variant
and the static g0 id1 handler analysis).  It extends the clean-room Hi5622V100 driver
(`../wifi-driver-clean`) with a validated H2D frame send path that carries the
vendor's **first post-ready message** - group-0 id-1 hdpp TXQ config sync,
byte-exact from the phase38 capture - and a strict grammar for the four EXACT
frames the `wifi-h2d-frames` library can synthesize.  It builds on the PD3
bench result (announce consumed by the firmware, core at `READY_TXQ_SYNC`)
and answers the next protocol question: **is the real 48-byte body inside the
SR ring dispatched and consumed too?**

**Status: host-only.**  Compile-clean against the project 6.18.60 tree
(`hi5622_wifi.ko`, 394,272 B, sha256 `4677ceac...`, 0 warnings), host logic
selftest 156/156, driver selftest 204/204, firmware-handler analysis selftest
38/38.  The first fire (task-98 build, 48-byte node) reached the device ring
and the ETE engine, but the firmware faulted (mask 0x12) - see
`REPORT-G0ID1-HANDLER.md` for what the g0 id1 handler parses and the two
remaining benches (72-byte node variant / payload-free g0 id0 control).

| file | what |
| --- | --- |
| REPORT.md | the lane report: protocol step, send-path design, gate order, integration diff, verification, risks, safety boundary |
| REPORT-G0ID1-HANDLER.md | static analysis of the firmware's group-0 id-1 receive handler: what it parses, the key table/key 0xd8 worker, the fault candidates, 48-vs-72 relevance, dispatcher preconditions |
| BENCH-PROCEDURE.md | the attended first-real-message ladder (48-byte and 72-byte node variants) + expected observables + stop rules |
| BUILD-NOTES.md | exact build result (size/sha256/vermagic/warnings), toolchain, selftest counts |
| src/ | the driver tree: clean driver + h2d_frames library + h2d_send_logic/h2d_send + Kbuild |
| tools/build.sh | canonical build: host selftest, kernel module build + evidence, driver selftest |
| tools/fw_g0id1_handler.py | host-only static analysis of the firmware g0 id1 handler/dispatcher (reads the local pinned vendor blob, prints derived annotations; 38/38 selftest) |
| tools/h2d_logic_selftest.c | 156 device-free checks: byte-exact emission (incl. the 72-byte node variant), SR node/producer plan, full gate matrix, sysfs grammar |
| tools/selftest.py | 192 device-free driver checks (133 inherited + 59 H2D) |
| tools/mkblocks.py | local Wi-Fi PHY custom-block staging (bench only, never committed) |

Quick start (host, no device):

    ./tools/build.sh

Sysfs surface added to the driver (endpoint device directory):

    h2d_txq_sync   (WO) "1" -> canned byte-exact g0 id1 TXQ config sync
                         (wire 48, SR node length = declared total 48)
    h2d_txq_sync72 (WO) "1" -> the same 48 frame bytes, but the SR node
                         declares the vendor's live 72-byte buffer
                         (48 + 24 x 0xff, node word1 (72<<16)|0x6d2b)
    h2d_send       (WO) txq_sync | txq_sync72 | stub [dst=N] | edca [dst=N] |
                        del_user user=U [addr=8hex] [dst=N]
    h2d_status   (RO)  gate verdicts, phase, budget, ok/timeout counters
    h2d_plan     (RO)  read-only preflight of the canned TXQ sync frame

The send path commits the SR node `{devva | node_len<<16 | 0x6D2B}` (node_len
= declared total by default; 72 for txq_sync72), advances
the packed producer (depth 32), coalesces the mailbox id-3 announce through
the protocol core (out[0] pending word, out[2] doorbell) and watches out[0]
with a bounded scheduled-sleep poll - no busy-wait, no retry.  It refuses
unless the boot gate is **latched**, the transport is released, the SR side is
armed, the core phase is `READY_TXQ_SYNC` or later, the H2D lane is open,
the previous announce is consumed, and the one-frame-per-idea budget allows
it.  The liveness stub (`hcc_sr_announce`) is cross-gated off after a real
frame.  Hard rules preserved: never write out[5] (CA 0x400392F0), never 0xCECE
during boot, no mailbox id 5, no group-4 H2D, release-only boot.

No vendor binary/object/firmware/calibration byte is committed (the `.ko` is
local evidence, gitignored).  The PD3 firmware image and the Wi-Fi PHY block
are local `/lib/firmware` inputs at bench time.