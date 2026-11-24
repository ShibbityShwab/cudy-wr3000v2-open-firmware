# REPORT - first REAL post-ready H2D message path (g0 id1 TXQ config sync + validated senders)

**Lane:** wifi-h2d-send (shared task **task-98**, extended by **task-99** and
by **task-102** - the ordered context-create precondition, host-only).
**Date:** 2026-11-24.
**Inputs (all committed lanes, read in place; no build-time patch):**
wifi-driver-clean (the clean driver this lane copies and extends),
wifi-h2d-frames (byte-exact builders/validators + the portable SR node /
packed-producer / mailbox-plan model),
wifi-hcc-core (the baked-in protocol core; **not modified**, byte-identical),
wifi-irq-unmask REPORT-PD3 (the bench result this lane's message builds on),
wifi-h2d-catalog / wifi-host-replica / phase20 / phase38 (decoded contract and
the byte-exact first-post capture, cited through the lanes above).
**Nothing in this lane contacted the device.**  The task-98 module was fired
by the Lead on the PD3 golden boot (one frame): the frame reached the device
ring (ETE engine consumed the node) but the firmware faulted and did not
consume the mailbox announce.  The task-99 extension adds the vendor's live
72-byte node-length variant plus a static analysis of the firmware handler
(`REPORT-G0ID1-HANDLER.md`); the 72-byte fire reproduced the same fault on
PD3 and PD4 and the control g0 id0 stub proved the transport itself.  The
task-102 extension adds the **ordered context-create frame** identified by the
wifi-ctx-create lane (task-101): the g0 id1 config event with u16 key 0x0064
that makes the firmware allocate the `ctx+0x190` buffer the key-0xd8 TXQ
worker dereferences.

## 0. One-line result

The clean driver now has the vendor's **first real post-ready H2D message**: a
new `h2d_txq_sync` attribute sends the byte-exact **group-0 id-1 TXQ config
sync** (`total 0x30, cmd 0x00d8, len 0x14, 0xff inherited tail`) through the
proven carrier (SR node `devva | node_len<<16 | 0x6D2B`, packed producer depth
32, coalesced mailbox id-3 announce, bounded out[0] consumption watch), gated on
the **latched** golden boot and core phase `READY_TXQ_SYNC`; the task-99
extension `h2d_txq_sync72` / `h2d_send "txq_sync72"` sends the same 48 frame
bytes with the vendor's captured **72-byte live node buffer** (24 x 0xff pad,
node word1 `(72<<16)|0x6D2B`) under exactly the same gates.  The task-102
extension adds the **ordered precondition**: `h2d_ctx_create` (and the
`ctx_create` grammar token) emits the synthesized **group-0 id-1 context-create
config event** (u16 key `0x0064`, len 0x14, data type 3 / vap 4 / user 4,
declared total 0x30, 0xff inherited tail) through the same carrier and gate
family, once-per-load and READY-only, so the ladder can be create -> read-only
verification -> txq_sync72 without rmmod'ing after the release.  A strict
`h2d_send` attribute exposes only the six **EXACT** frames the h2d-frames
library can synthesize (txq_sync / txq_sync72 / ctx_create / stub / edca /
del_user), never a synthetic payload.  Module `hi5622_wifi.ko`, **399,816 B**,
sha256 `b11ffea6...`, vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, **0
compiler/modpost warnings**; host logic selftest **202/202**, driver selftest
**226/226** and firmware-handler analysis selftest **38/38** PASS.

## 1. Deliverable tree

    src/                       copy of the clean driver tree (wifi-driver-clean)
      h2d_frames.{c,h}         byte-identical copy of the wifi-h2d-frames library
      h2d_catalogue.inc        (generated tables; provenance checked by selftest)
      h2d_send_logic.{c,h}     NEW: portable gate + emission + strict grammar
                               (six EXACT frames incl. the ordered
                               ctx_create; compiles host AND kernel; no IO)
      h2d_send.{c,h}           NEW: kernel glue - sysfs group "h2d", SR commit,
                               bounded out[0] consumption watch, telemetry
      main.c                   + wires the "h2d" attribute group
      hcc.c                    + cross-guard: liveness stub and real frame are
                               one idea (one H2D frame per module load)
      service.c                + hcc_clear resets H2D *telemetry* only (never
                               the one-frame budget)
      hi5622_hcc.h             + H2D send telemetry fields
      Kbuild                   + h2d_frames.o h2d_send_logic.o h2d_send.o
      (all other copied files are byte-identical to wifi-driver-clean;
       tools/selftest.py enforces that parity)
    tools/fw_g0id1_handler.py  host-only static analysis of the firmware
                               g0 id1 receive handler/dispatcher (38/38)
    REPORT-G0ID1-HANDLER.md    the handler/parse/fault/precondition write-up
    tools/build.sh             canonical host+kernel build/verify + evidence
    tools/h2d_logic_selftest.c host, device-free selftest of emission + gating
    tools/selftest.py          driver selftest, extended (133 -> 226 checks)
    tools/mkblocks.py          local Wi-Fi PHY custom-block staging (bench only)
    README.md  REPORT.md  BUILD-NOTES.md  BENCH-PROCEDURE.md

The built `.ko` is local evidence only and is gitignored (`src/.gitignore`).
No vendor binary/object/firmware/calibration byte is committed.

## 2. The protocol step

PD3 (bench, 2026-11-24, firmware sha256 `eea9f2ff...`) proved the full-duplex
HCC protocol on our clean-room host: golden boot (handshake
0x86807FEF/7/2/0xABCD, state 1, mask 0), the ready pair decoded (g4 id1 + g4
id2 payload 0x01060850), **one H2D SR announce consumed by the firmware**
(out[0] 0x8 -> 0 in 0 polls, hcc-core `h2d_complete`, `h2d done=1`), core
phase `READY_TXQ_SYNC`.

The vendor's *next* protocol step is the **first real post-ready H2D frame**:
`hdpp_config_send_event` group-0 id-1, the TX-queue-table address sync
(`hmac_txq_table_addr_sync`), sent only after both ready waits
(wifi-h2d-frames REPORT 4.2(c)).  The live vendor first SR post was captured
byte-exact in phase20 A.5 / phase38:

    header : 00 01 00 04  30 00 01 00  00 00 5a 5a   (group 0, dst 0, fa 0,
                                                       fb 4, total 0x30, id 1,
                                                       fc 0, tag 0x5a5a)
    payload: 00 00 00 00 01 00 00 00   token (inherited)
             d8 00                      cmd 0x00d8
             14 00                      len 0x14 (20)
             01 00 00 00 + 16 x 00      data
             ff ff ff ff                inherited tail
    buffer : 24 x ff past the declared total (the vendor ring buffer was 72 B;
             node word1 declared 72 while the header declares 48)

The h2d-frames lane's `h2d_build_g0_id1_hdpp_txq_sync()` reproduces this
bit-for-bit, including the 0xff tail; the library rule is that the **node
length is the frame total** (the vendor transport takes the length from
hdr[4..5]) and the 72-byte buffer exists only as the known-answer capture
vector.  This driver follows that rule: wire frame and SR node = 48 bytes.
The 72-byte capture is asserted byte-for-byte in the host selftest.

## 3. The send path

### 3.1 Files and layering

* `h2d_send_logic.{c,h}` - portable: message selector, gate, byte-exact
  emission through the library, strict sysfs grammar.  Compiles unchanged in
  the kernel module and in the host selftest; **no IO, no kernel headers**
  (only `<linux/errno.h>` vs `<errno.h>`).
* `h2d_send.c` - kernel glue: fills the live snapshot, serialises under the
  service mutex, writes the SR body/node/producer, calls the core's
  allowlisted commit, watches out[0], exposes sysfs + telemetry.
* The mailbox/gate writes remain the **core's** business
  (`hcc_core_sr_commit -> hcc_core_mbox_send`): out[0] = 1<<3, out[2] |= 1,
  through the existing write allowlist.  `h2d_send.c` never writes a mailbox
  CA, the gate, or out[5] (selftest-enforced).

### 3.2 The exact send sequence (h2d_post)

1. **Build + validate** (`h2d_msg_build`): exact builder -> `h2d_frame_encode`
   (transport + firmware tables + handler + policy: tag 0x5a5a, total == wire
   length, groups 0/2 only, id 5 refused, payload <= 0x674, dst <= 0xf,
   non-NULL firmware handler).
2. **Gate** (under `h->lock`, snapshot `struct h2d_live`):
   armed -> released -> boot latched -> H2D lane open -> core phase
   `READY_TXQ_SYNC`/ACTIVE -> (TXQ: once-only, READY only) -> liveness stub
   unused -> per-load budget -> core announce in flight -> live out[0].
3. **Preflight plan** (`h2d_plan_post`): derives node word0 = buffer devva,
   node word1 = `(total<<16)|0x6D2B`, the packed producer before/after and the
   mailbox id-3 plan; refuses `H2D_ESTATE` when out[0] != 0 (coalescing
   precondition, vendor `pcie_msg_send` semantics).
4. **SR commit, vendor order**: body -> node {devva, word1} -> `wmb()` ->
   producer register (packed index, low10 | phase<<10, depth 32) -> `wmb()` ->
   `hcc_core_sr_commit()` (core mirror + phase transition + coalesced id-3
   announce; the core owns out[0]/out[2]).
5. **Bounded consumption watch**: poll out[0] (CA 0x40039010); the device
   clears the pending word when it consumes the announce.  `msleep_interruptible(5)`
   x <= 200 = the core's 1000 ms bound - **no busy-wait, no retry**.  A timeout
   is counted, logged and left to the core's own bounded timeout (which closes
   the H2D lane; no second frame is possible).

### 3.3 Gate order (first refusal wins; all encoded in `h2d_gate_check`)

| # | gate | refusal reason | meaning |
| --- | --- | --- | --- |
| 1 | armed (hcc_arm) | not_armed | SR rings not programmed; no device write |
| 2 | transport release | not_released | 0x5A5A has not been written |
| 3 | **latched boot** | not_latched | page+8==2 not latched; never H2D during the ROM stage |
| 4 | H2D lane open | lane_closed | core closed the lane after a bounded announce timeout |
| 5 | **core phase READY_TXQ_SYNC or ACTIVE** | phase_not_ready | FAILED is not "later" |
| 6 | TXQ once-only | txq_already_sent / txq_phase_not_ready | the first message is sent once, from READY |
| 7 | liveness stub unused | liveness_probe_used | stub and real frame are one idea per load |
| 8 | per-load budget | budget_exhausted | default 1 frame/load (param `h2d_send_budget`) |
| 9 | core coalescing state | announce_inflight | previous announce not cleared by the core yet |
| 10 | live out[0] == 0 | out0_not_consumed | previous announce not consumed by the firmware |
| 11 | ctx_create: READY-only, once per load | ctx_phase_not_ready / ctx_already_created | the ordered pre-TXQ message is legal only from READY_TXQ_SYNC and allocates one ctx+0x190 |
| 12 | TXQ in ACTIVE after this load's create | txq_phase_not_ready | both messages are g0/id1, so the core phase can be ACTIVE on the create; the driver still allows exactly one real TXQ frame |

Rows 6/12 note: `h2d_live.txq_submitted` is derived from the driver's real
TXQ counter, not from the core's `txq_sync_submitted` flag, because the core
latches its flag on any g0/id1 commit (including the create).  This keeps the
existing single-TXQ behavior intact and adds the ordered exception without
touching the portable core.

### 3.4 sysfs surface

    h2d_ctx_create (WO) "1" -> send the synthesized g0 id1 key-0x0064
                         context-create config event (wire 48, node 48); the
                         ordered precondition of the TXQ sync
    h2d_txq_sync   (WO) "1" -> send the canned byte-exact g0 id1 TXQ config
                         sync (wire 48, node 48 = declared total)
    h2d_txq_sync72 (WO) "1" -> the same 48 wire bytes in the vendor's captured
                         72-byte live node buffer (24 x 0xff pad)
    h2d_send       (WO) grammar: txq_sync | txq_sync72 | ctx_create |
                        stub [dst=N] | edca [dst=N] |
                        del_user user=U [addr=8hex] [dst=N]
    h2d_status    (RO)  phase/boot/lane, gate verdicts, budget, counters
                        (ok/timeout/refused/desync), last out[0], max polls
    h2d_plan      (RO)  read-only preflight: the canned frame hex, the gate
                        verdict, node words, producer before/after, mailbox plan

Module parameter `h2d_send_budget` (uint, 0444/0644 defaults 1): 0 = negative
control, 1 = one frame per idea (bench default), max 8 for a supervised
multi-message session; `hcc_clear` resets H2D telemetry only, never this
budget.

### 3.5 The six synthesized messages (all from EXACT builders)

| attr form | group/id | wire / node | source |
| --- | --- | --- | --- |
| `ctx_create` / `h2d_ctx_create` | 0/1 | 48 B / 48 B | synthesized g0 id1 key 0x0064 create (firmware parser; task-102) |
| `txq_sync` / `h2d_txq_sync` | 0/1 | 48 B / 48 B | phase38 byte-exact first post |
| `txq_sync72` / `h2d_txq_sync72` | 0/1 | 48 B / 72 B (24 x 0xff pad) | the vendor's captured LIVE node length |
| `stub [dst=N]` | 0/0 | 12 B / 12 B | payload-free liveness stub |
| `edca [dst=N]` | 0/0x1d | 72 B / 72 B | live steady-state message A (zeroed 0x3c) |
| `del_user user=U [addr=..]` | 0/9 | 24 B / 24 B | decoded 12-byte del-user payload |

Nothing else is synthesizable: the vendor payload structs are not recoverable
from the firmware image and several firmware handlers dereference payload
words (wifi-h2d-catalog Appendix B).  `h2d_send` therefore names a builder; it
never accepts a raw payload.

## 4. Exactly what changed from wifi-driver-clean

| file | change | why |
| --- | --- | --- |
| main.c | + `#include "h2d_send.h"`, + `{ "h2d", ... }` group, description text | wire the new sysfs group |
| hcc.c | + 8-line cross-guard in the liveness path | liveness stub and real frame share the one-frame-per-idea rule |
| service.c | + 5 lines in `hcc_clear` | clear H2D telemetry, never the budget |
| hi5622_hcc.h | + 11 telemetry fields | H2D send counters/state for sysfs and dmesg |
| Kbuild | + 3 objects | link the library, logic and glue |
| new | h2d_frames.*, h2d_send_logic.*, h2d_send.* | this lane |

Everything else in `src/` is byte-identical to the wifi-driver-clean tree; the
driver selftest asserts the parity file-by-file (modified set = exactly the
five files above).  `hcc_core.{c,h}` and `hcc_core_kernel.{c,h}` are unchanged
and still match the wifi-hcc-core lane hashes.

## 5. Verification (host-only)

| check | command | result |
| --- | --- | --- |
| host H2D logic selftest | `./tools/build.sh` (step 1) | **202/202 PASS** |
| kernel module build (project 6.18.60, -Werror) | `./tools/build.sh` (step 2) | PASS, **0 warnings**, 31 `T h2d_*` symbols |
| module artifact | `modinfo`/`sha256sum` | 399,816 B, sha256 `b11ffea6d1cebb9f03a3a4d162dbc0c5cbd684bb6298bcf22d5e6d9ebb1d882f`, vermagic `6.18.60 SMP mod_unload ARMv7 p2v8` |
| driver selftest (extended, 133 -> 226) | `./tools/build.sh` (step 3) | **226/226 PASS** |
| firmware g0 id1 handler analysis | `python3 tools/fw_g0id1_handler.py --selftest` | **38/38 PASS** (separate host tool; needs the local pinned blob + capstone) |
| h2d library provenance | driver selftest | h2d_frames.c/.h/.inc byte-identical to wifi-h2d-frames; hcc_core identical to wifi-hcc-core |
| no mailbox/gate/out[5] write in the new path | driver selftest | PASS (one `hcc_core_sr_commit` call site, no direct mailbox writel) |

The host selftest covers the emission vectors (48 B wire, 72 B capture, the
ctx_create 48 B frame, stub, edca, del_user), node word
`(total<<16)|0x6D2B`, packed-producer wrap (31 -> 0x400, phase 1), the
`H2D_ESTATE` coalescing refusal, the full gate matrix (all refusal reasons +
positive cases incl. the ordered create -> TXQ exception) and the sysfs
grammar (incl. `ctx_create`).

## 6. Bench expectations (summary; full ladder in BENCH-PROCEDURE.md)

**CURRENT (task-102): the ordered ladder on a golden PD4 boot** - ready pair ->
read-only pre-create pack -> ONE `h2d_ctx_create` -> read-only post-create pack
(ctx4+0x190 non-zero + create-buffer signatures) -> ONE `h2d_txq_sync72` ->
capture-and-stop.  The module is loaded with `h2d_send_budget=2` (one load,
two ordered frames; a reboot would lose the created context and a second module
load would need `rmmod` after the release, which is forbidden).

Expected after the pair: the create consumed (out[0] 0x8 -> 0) and the context
allocated; the TXQ consumed with **no new exception** (mask CA 0x40008 stays 0,
the exception staging frame at BAR0 0x6F800C unchanged/zero), `h2d_status`
`frames_sent=2 txq_sent=1 ctx_sent=1 ok=2`, core phase `ACTIVE`, dmesg
`H2D ctx_create: ... CONSUMED ...` then `H2D txq_sync72: ... CONSUMED after N
polls`, golden page/state unchanged, the D2H reader still flowing.  The core's
`txq_sync` event on the create commit is expected bookkeeping (g0/id1
collision, risk 8 below), not a sign the TXQ ran.  Any new D2H frame (new g4
id, g3, or a g1/g2 catalogue id) is a discovery to capture with
`rd_catalog`/`rd_decode`.  Stop rules: one boot, one ordered pair, one idea
per fire, no retry; timeout/create-not-allocated/page change/abort => capture
and stop.

Historical single-frame expectations for `h2d_txq_sync` alone (Ladder A/B) are
superseded: the solo TXQ faults by design on this firmware (the context does
not exist), which is exactly what the ordered ladder fixes.

## 7. Risks, limits and open items (honest)

1. **Node length 48 vs the vendor's live 72 (first fire RESULT, task-99).**
   The first fire (48-byte node) reached the device ring and the ETE engine
   consumed the node, but the firmware faulted (mask 0x12 = undefined + data
   abort) and did not consume the mailbox announce; boot stayed golden.  The
   **72-byte node-length variant** was then fired twice (PD3, PD4) with the same
   fault, so node length is not the cause.  The PD4/ctx-create work pinned the
   real cause: the g0 id1 key-0xd8 worker dereferences `ctx+0x190`, which only
   the **key-0x64 context-create message** allocates.  That is why task-102
   adds `h2d_ctx_create` and the ordered ladder (section 11).
2. **cmd 0x00d8 provenance is open** (no producing call site in the analysed
   vendor build - wifi-h2d-frames REPORT 8).  The frame is the live capture,
   not a reconstruction; it is still the vendor's first post-ready send.
3. **The firmware may dispatch and then do nothing with the sync** (no D2H
   response).  A clean negative is a valid result: out[0] consumed proves the
   H2D path to the dispatcher; any subsequent frame behaviour is firmware
   policy.
4. **The 72-byte vendor buffer/pad is not sent** (48 B frame + 48 B node).  The
   host selftest pins the capture; the wire choice is documented above.
5. **No device execution in this lane** - everything here is host-verified
   compile + selftest; the first load is the next attended session.
6. `h2d_status`/`h2d_plan` read the core state without the service mutex (like
   the existing `hcc_*` status attrs): telemetry only, no device write, and
   every decision that writes is re-evaluated under the mutex.
7. The driver remains a bench instrument: no cfg80211/mac80211, no data path,
   no PM/heartbeat (vendor boot path leaves them off).
8. **Both ordered messages are group 0 / id 1.**  The portable core keys its
   phase machine on group/id, so the create commit latches its
   `txq_sync_submitted` flag and the phase to `ACTIVE`, and emits a
   `txq_sync` event.  The driver does not change the frozen core; instead it
   tracks the semantics itself: `h2d_status` reports `ctx_sent` and
   `txq_sent`, the once-only TXQ gate uses the real TXQ counter, and the TXQ
   frame is allowed in `ACTIVE` only when this load committed the create.
   Consequence for the bench: **phase ACTIVE after the create is expected** and
   a `txq_sync` core event on the create is bookkeeping, not a pass signal.
9. **The ordered pair needs one module load with `h2d_send_budget=2`.**  The
   created context lives in firmware RAM: a reboot loses it, so the TXQ must
   follow the create in the same boot; and a second module load on the same
   boot would require `rmmod` after the release, which is forbidden.  The
   budget is declared at insmod (supervised multi-message session), never
   raised after a refusal.
10. **The create payload is synthesized (not captured).**  type 3 / vap 4 /
   user 4 comes from the firmware parser constraints (wifi-ctx-create REPORT);
   the vendor key-0x64 sender is unproven in the analysed teardown .ko.  The
   post-create read pack is the acceptance check; a consumed create that
   leaves `ctx4+0x190 == 0` means a parser-gate mismatch (parent module byte 1,
   type) and is a stop, not a retry.  Cross-lane note: the ctx-create lane's
   readback table lists the exception-mask/staging rows with the BAR0 column
   equal to the CPU value (0x00400008/0x0040000C); the correct BAR0 addresses
   are 0x6F8008/0x6F800C (CPU 0x00040008/0x0004000C + 0x6B8000), as used by
   REPORT-PD4 and by this lane's BENCH-PROCEDURE.  The ctx-create report's
   cave row (0x7C0240) is correct.

## 8. Safety + publication boundary

* Encoded hard rules preserved: release-only boot (transport owns 0x5A5A),
  latched gate (hcc-core F1), CECE evidence-only (core), write allowlist
  (out[0..4]+gate; **out[5] CA 0x400392F0 never writable**), never 0xCECE
  during boot, no group-4 H2D, no mailbox id 5, one frame per idea, no retry.
* No vendor binary/object/firmware/calibration bytes are committed.  The
  Wi-Fi PHY custom block stays a local /lib/firmware input; the firmware image
  (and the PD3 patched image) stay local evidence.  The `.ko` is local and
  gitignored.  All new source is original GPL-2.0 clean-room code from this
  repository's own committed contract.

## 9. Reproduce

    cd opensource/docs/incoming/wifi-h2d-send
    ./tools/build.sh                  # host selftest + kernel build + driver selftest

    # optional, host-only pieces:
    cc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -Wshadow \
       -Wstrict-prototypes -Wmissing-prototypes -Isrc \
       -o /tmp/h2d_logic_selftest tools/h2d_logic_selftest.c \
       src/h2d_send_logic.c src/h2d_frames.c src/hcc_core.c
    /tmp/h2d_logic_selftest
    python3 tools/selftest.py

## 10. Handover / next steps

1. **Attended PD4 golden-boot session with the ordered ladder
   (BENCH-PROCEDURE.md, Ladder C):** load this module with
   `h2d_send_budget=2`, reach the latched ready pair on the PD4 image
   (sha256 `cfe24bfb...`), take the read-only pre-create pack, fire ONE
   `h2d_ctx_create`, take the post-create pack (ctx4+0x190 non-zero +
   signatures), then fire ONE `h2d_txq_sync72`.  Capture and stop.
2. Capture the observables (out[0]/out[2]/out[5], phase, events, the two dmesg
   H2D lines, the read packs, D2H catalogue) and the UART log; one boot, one
   ordered pair, no retry.
3. If the create is consumed but `ctx4+0x190` stays 0: STOP (parser-gate
   mismatch; the payload instance or the allocator-return convention needs the
   ctx-create lane, not a resend).  If the TXQ still faults with the context
   present: read-only exception record at BAR0 0x6F800C (CPU 0x0004000C, 17
   words), then power-cycle - that is a different, post-context fault.
4. If the ordered pair is clean (consumed, no exception): the next host-side
   step is the vendor startup order after the TXQ sync (new D2H observations
   first; then further EXACT config keys via a supervised budget), not a
   speculative payload.  The solo-TXQ negative control is already measured;
   do not repeat it.

## 11. task-102: the ordered context-create -> TXQ sync integration

### 11.1 What changed

| file | change |
| --- | --- |
| `src/h2d_send_logic.h` | + `H2D_MSG_CTX_CREATE` selector, the ctx constants (total 0x30 / key 0x0064 / len 0x14 / type 3 / vap 4 / user 4 / module bound 0x12), the `h2d_build_ctx_create()` prototype, two gate reasons (`H2D_GATE_DUP_CTX`, `H2D_GATE_CTX_PHASE`), the `ctx_created` live field and the grammar doc |
| `src/h2d_send_logic.c` | + the ctx token/data/tail vectors, the parameterised builder over `h2d_build_g0_cfg_event()` (key -> payload+8, len -> +0xA, fb == user), the ctx message build case, the READY-only/once gate and the ordered TXQ exception, the `ctx_create` grammar token and name |
| `src/h2d_send.c` | + `h2d_ctx_create` WO attribute, ctx telemetry increment, the semantics-based live snapshot (`txq_submitted` from the real TXQ counter, `ctx_created`), `h2d_ctx_create` display in `h2d_plan`, ctx gate/counters in `h2d_status`, grammar help text |
| `src/hi5622_hcc.h` | + `h2d_ctx_create_sent` telemetry (never reset by `hcc_clear`) |
| `src/main.c` | module description mentions the ordered context-create |
| `tools/h2d_logic_selftest.c` | + the byte-exact ctx_create vector, builder-validation refusals, the ordered gate matrix, the grammar, the end-to-end parse->build (156 -> 202 checks) |
| `tools/selftest.py` | + 22 source/contract checks for the ctx_create selector, vectors, gate, grammar, attribute, telemetry, plan/status and the single commit site (204 -> 226) |
| `BENCH-PROCEDURE.md` | rewritten: Ladder C (ordered create -> TXQ) is the current procedure; A/B kept as fired history |
| `README.md` / `BUILD-NOTES.md` | new surface + build result |

No change to `h2d_frames.c/.h` (still byte-identical to the wifi-h2d-frames
lane), to `hcc_core.*` (still byte-identical to wifi-hcc-core) or to any
existing frame/gate behavior.

### 11.2 The frame (byte-exact)

```
header : 00 01 00 04  30 00 01 00  00 00 5a 5a
payload: 00 00 00 00 01 00 00 00        token[8]
         64 00                          u16 key 0x0064 (table A entry 1 -> worker CA 0x449E4)
         14 00                          u16 len 0x14
         03 04 00 00 04 00 00 00        data[0]=type 3, data[1]=vap 4, data[4]=user 4
         00 .. (12 x 00)                data[8]=0, data[0x11]=data[0x12]=0
         ff ff ff ff                    inherited tail
wire / node : 48 B / 48 B   node word1 = (0x30 << 16) | 0x6D2B = 0x00306D2B
```

The full 48-byte image is pinned in the host selftest and in
`../wifi-ctx-create/recipes/ctx-create-g0id1-key64.json`:
`000100043000010000005a5a0000000001000000640014000304000004000000000000000000000000000000ffffffff`.
The builder refuses data type 0/4/5 (the helper's no-alloc paths), a module
index > 0x12, and any dst != 0 (the byte-exact instance is dst 0).

### 11.3 The ordered gate (why the core needed no change)

Both messages are group 0 / id 1, so the portable core's phase machine cannot
tell them apart: `hcc_core_sr_commit()` latches `txq_sync_submitted` and flips
to ACTIVE on *any* g0/id1 commit.  The integration keeps the frozen core and
moves the distinction into the driver:

* `h2d_ctx_create`: READY_TXQ_SYNC only, once per load, then the ordinary
  one-frame gates (latched/released/armed/lane/liveness/budget/inflight/out0);
* `h2d_txq_sync(|72)`: once per load from the driver's real-TXQ counter; legal
  from READY_TXQ_SYNC (the ordinary path) **or** from ACTIVE when this load
  committed the create (the ordered path);
* everything else (stub/edca/del_user) behaves exactly as before.

The carrier is the same `h2d_post()` path for all messages: exact build ->
policy validation -> gate -> `h2d_plan_post` -> SR body/node -> barrier ->
packed producer -> `hcc_core_sr_commit` (mailbox id-3 announce through the
write allowlist) -> bounded out[0] consumption watch.  No new write site, no
retry, one `hcc_core_sr_commit` call site (selftest-enforced).

### 11.4 Verification (host-only)

| check | result |
| --- | --- |
| `./tools/build.sh` host logic selftest | **202/202 PASS** |
| kernel module build (project 6.18.60, -Werror, modpost) | PASS, **0 warnings** |
| module artifact | **399,816 B**, sha256 `b11ffea6d1cebb9f03a3a4d162dbc0c5cbd684bb6298bcf22d5e6d9ebb1d882f`, vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, `T h2d_*` = 31 (incl. `h2d_build_ctx_create`) |
| driver selftest | **226/226 PASS** |
| firmware g0 id1 handler analysis (separate tool) | 38/38 PASS (unchanged) |

### 11.5 Bench ladder (summary; full text in BENCH-PROCEDURE.md)

Golden PD4 boot (image sha256 `cfe24bfb...`; recovery armed, link PASS, UART
read-only) -> prepare/upload/verify/seed/hcc_arm/hcc_loop/release -> latched
golden page+ready pair (`READY_TXQ_SYNC`) -> read-only pack (ctx4+0x190 =
BAR0 0x7D45E8 == 0; table entry CA 0x171468 = BAR0 0x829468 == 0x0011C458;
ctx4 body CA 0x11C458 = BAR0 0x7D4458 == 0; G CA 0x169E90 = BAR0 0x821E90 ==
0x00170E28; mask CA 0x40008 = BAR0 0x6F8008 == 0; staging BAR0 0x6F800C == 0)
-> ONE `h2d_ctx_create` -> re-read the pack (ctx4+0x190 non-zero; buffer +0x10
= 0x64, +0x14 = 3, +0x238 = 0x92A, +0x52 = 1; ctx4[0] byte = 4, ctx4[4] byte =
3; refcount BAR0 0x8294F4 low byte = 1; mask/staging unchanged) -> ONE
`h2d_txq_sync72` -> expected **no new exception** (mask CA 0x40008 stays 0,
staging at BAR0 0x6F800C unchanged/zero), announce consumed, dmesg
`CONSUMED` lines, page/state golden -> capture and stop.  One module load with
`h2d_send_budget=2` (the decision and its reasons are in BENCH-PROCEDURE.md);
no rmmod after the release; one idea per fire; no retry.

### 11.6 Open items

* O1: the vendor key-0x64 sender remains unproven (wifi-ctx-create O1); the
  firmware-side route is what the frame needs.
* O2: type 3 vs 1/2 (minimal vs vendor-default init) - start with 3; re-send
  with 1/2 only if a later per-context key needs the +0x3E2/+0xF8 defaults.
* O3: the allocator return convention (CPU vs device-CA pointer) is settled by
  the post-create read of BAR0 0x7D45E8; both read conventions are in the
  procedure.
* O4: after a clean ordered pair, no D2H response is expected from the TXQ
  sync; consumed+silent is a positive transport result.