# wifi-h2d-send - the first REAL post-ready H2D message path (g0 id1 TXQ config sync)

Host-only deliverable of the wifi-h2d-send lane (shared task **task-98**,
2026-11-24).  It extends the clean-room Hi5622V100 driver
(`../wifi-driver-clean`) with a validated H2D frame send path that carries the
vendor's **first post-ready message** - group-0 id-1 hdpp TXQ config sync,
byte-exact from the phase38 capture - and a strict grammar for the four EXACT
frames the `wifi-h2d-frames` library can synthesize.  It builds on the PD3
bench result (announce consumed by the firmware, core at `READY_TXQ_SYNC`)
and answers the next protocol question: **is the real 48-byte body inside the
SR ring dispatched and consumed too?**

**Status: host-only.**  Compile-clean against the project 6.18.60 tree
(`hi5622_wifi.ko`, 391,456 B, sha256 `047be179...`, 0 warnings), host logic
selftest 128/128, driver selftest 192/192.  Nothing here has been run on the
device yet; the bench ladder is the attended session.

| file | what |
| --- | --- |
| REPORT.md | the lane report: protocol step, send-path design, gate order, integration diff, verification, risks, safety boundary |
| BENCH-PROCEDURE.md | the attended first-real-message ladder + expected observables + stop rules |
| BUILD-NOTES.md | exact build result (size/sha256/vermagic/warnings), toolchain, selftest counts |
| src/ | the driver tree: clean driver + h2d_frames library + h2d_send_logic/h2d_send + Kbuild |
| tools/build.sh | canonical build: host selftest, kernel module build + evidence, driver selftest |
| tools/h2d_logic_selftest.c | 128 device-free checks: byte-exact emission, SR node/producer plan, full gate matrix, sysfs grammar |
| tools/selftest.py | 192 device-free driver checks (133 inherited + 59 H2D) |
| tools/mkblocks.py | local Wi-Fi PHY custom-block staging (bench only, never committed) |

Quick start (host, no device):

    ./tools/build.sh

Sysfs surface added to the driver (endpoint device directory):

    h2d_txq_sync (WO)  "1" -> canned byte-exact g0 id1 TXQ config sync
    h2d_send     (WO)  txq_sync | stub [dst=N] | edca [dst=N] |
                       del_user user=U [addr=8hex] [dst=N]
    h2d_status   (RO)  gate verdicts, phase, budget, ok/timeout counters
    h2d_plan     (RO)  read-only preflight of the canned TXQ sync frame

The send path commits the SR node `{devva | total<<16 | 0x6D2B}`, advances
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
