# wifi-h2d-send - the first REAL post-ready H2D message path (g0 id1 TXQ config sync)

Host-only deliverable of the wifi-h2d-send lane (shared task **task-98**,
2026-11-24; extended by **task-99** with the 72-byte live-node-length variant
and the static g0 id1 handler analysis, and by **task-102** with the ordered
context-create precondition).  It extends the clean-room Hi5622V100 driver
(`../wifi-driver-clean`) with a validated H2D frame send path that carries the
vendor's **first post-ready message** - group-0 id-1 hdpp TXQ config sync,
byte-exact from the phase38 capture - the **context-create config event**
(g0 id1 key 0x0064) that must precede it, and a strict grammar for the six
EXACT frames the `wifi-h2d-frames` library + this lane can synthesize.  It
builds on the PD3/PD4 bench results (announce consumed by the firmware, core
at `READY_TXQ_SYNC`, control stub fault-free, solo TXQ sync fault at the
key-0xd8 worker) and answers the next protocol question: **does the ordered
create -> TXQ sync pair run without the handler fault?**

**Status: host-only.**  Compile-clean against the project 6.18.60 tree
(`hi5622_wifi.ko`, **400,732 B**, sha256 `774c4f1f...`, 0 warnings), host
logic selftest **242/242**, driver selftest **233/233**, firmware-handler
analysis 38/38, create-helper type analysis 26/26.  Fired history: task-98 (48-byte node) and task-99
(72-byte node, PD3 and PD4) TXQ-sync fires faulted in the g0 id1 handler
(mask 0x12); the payload-free g0 id0 control was consumed in 0 polls with no
exception.  The task-102 ordered ladder (create -> read-only pack ->
txq_sync72 on a golden PD4 boot) is the current bench procedure; its first lap
(type 3) consumed the create and wrote `ctx[0]=4` but left `ctx+0x190` = 0,
and the type-dispatch analysis (`tools/ctx_type_dispatch.py`, REPORT.md sec 12)
shows types 1/2/3 share the same allocator, so the driver now exposes the
create-helper **type variants** (`h2d_ctx_create` write value 1|2|3,
`ctx_create_t1`/`ctx_create_t2`) for the type-1 follow-up lap with extended
PACK diagnostics.

| file | what |
| --- | --- |
| REPORT.md | the lane report: protocol step, send-path design, gate order, integration diff, verification, risks, safety boundary, the task-102 ordered context-create integration (sec 11) and the create-helper type dispatch + first-lap result (sec 12) |
| REPORT-G0ID1-HANDLER.md | static analysis of the firmware's group-0 id-1 receive handler: what it parses, the key table/key 0xd8 worker, the fault candidates, 48-vs-72 relevance, dispatcher preconditions |
| BENCH-PROCEDURE.md | the attended **ordered context-create -> TXQ sync ladder** (Ladder C, current) + expected observables + stop rules; fired Ladders A/B kept as history |
| BUILD-NOTES.md | exact build result (size/sha256/vermagic/warnings), toolchain, selftest counts |
| src/ | the driver tree: clean driver + h2d_frames library + h2d_send_logic/h2d_send + Kbuild |
| tools/build.sh | canonical build: host selftest, kernel module build + evidence, driver selftest |
| tools/fw_g0id1_handler.py | host-only static analysis of the firmware g0 id1 handler/dispatcher (reads the local pinned vendor blob, prints derived annotations; 38/38 selftest) |
| tools/h2d_logic_selftest.c | 242 device-free checks: byte-exact emission (incl. the 72-byte node variant and the ctx_create t1/t2/t3 type variants), SR node/producer plan, full gate matrix (incl. the ordered create -> TXQ path), sysfs grammar |
| tools/selftest.py | 233 device-free driver checks (133 inherited + 100 H2D) |
| tools/ctx_type_dispatch.py | create-helper type-dispatch + allocator analysis (26/26); reads the pinned local blob in place |
| tools/mkblocks.py | local Wi-Fi PHY custom-block staging (bench only, never committed) |

Quick start (host, no device):

    ./tools/build.sh

Sysfs surface added to the driver (endpoint device directory):

    h2d_ctx_create (WO) "1".."3" -> synthesized g0 id1 key-0x64
                         context-create config event (wire 48 = node 48) with
                         data[0] = create-helper type 1|2|3; the ordered
                         precondition of the TXQ sync (READY-only, once/load)
    h2d_txq_sync   (WO) "1" -> canned byte-exact g0 id1 TXQ config sync
                         (wire 48, SR node length = declared total 48)
    h2d_txq_sync72 (WO) "1" -> the same 48 frame bytes, but the SR node
                         declares the vendor's live 72-byte buffer
                         (48 + 24 x 0xff, node word1 (72<<16)|0x6d2b)
    h2d_send       (WO) txq_sync | txq_sync72 | ctx_create |
                        ctx_create_t1 | ctx_create_t2 | stub [dst=N] |
                        edca [dst=N] | del_user user=U [addr=8hex] [dst=N]
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