# REPORT - first REAL post-ready H2D message path (g0 id1 TXQ config sync + validated senders)

**Lane:** wifi-h2d-send (shared task **task-98**, host-only).  **Date:** 2026-11-24.
**Inputs (all committed lanes, read in place; no build-time patch):**
wifi-driver-clean (the clean driver this lane copies and extends),
wifi-h2d-frames (byte-exact builders/validators + the portable SR node /
packed-producer / mailbox-plan model),
wifi-hcc-core (the baked-in protocol core; **not modified**, byte-identical),
wifi-irq-unmask REPORT-PD3 (the bench result this lane's message builds on),
wifi-h2d-catalog / wifi-host-replica / phase20 / phase38 (decoded contract and
the byte-exact first-post capture, cited through the lanes above).
**Nothing in this lane contacted the device.**

## 0. One-line result

The clean driver now has the vendor's **first real post-ready H2D message**: a
new `h2d_txq_sync` attribute sends the byte-exact **group-0 id-1 TXQ config
sync** (`total 0x30, cmd 0x00d8, len 0x14, 0xff inherited tail`) through the
proven carrier (SR node `devva | total<<16 | 0x6D2B`, packed producer depth 32,
coalesced mailbox id-3 announce, bounded out[0] consumption watch), gated on
the **latched** golden boot and core phase `READY_TXQ_SYNC`; a strict
`h2d_send` attribute exposes only the four **EXACT** frames the h2d-frames
library can synthesize (txq_sync / stub / edca / del_user), never a synthetic
payload.  Module `hi5622_wifi.ko`, **391,456 B**, sha256 `047be179...`,
vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, **0 compiler/modpost warnings**;
host logic selftest **128/128** and driver selftest **192/192** PASS.

## 1. Deliverable tree

    src/                       copy of the clean driver tree (wifi-driver-clean)
      h2d_frames.{c,h}         byte-identical copy of the wifi-h2d-frames library
      h2d_catalogue.inc        (generated tables; provenance checked by selftest)
      h2d_send_logic.{c,h}     NEW: portable gate + emission + strict grammar
                               (compiles host AND kernel; no IO, no kernel deps)
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
    tools/build.sh             canonical host+kernel build/verify + evidence
    tools/h2d_logic_selftest.c host, device-free selftest of emission + gating
    tools/selftest.py          driver selftest, extended (133 -> 192 checks)
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

### 3.4 sysfs surface

    h2d_txq_sync  (WO)  "1" -> send the canned byte-exact g0 id1 TXQ config sync
    h2d_send      (WO)  grammar: txq_sync | stub [dst=N] | edca [dst=N] |
                        del_user user=U [addr=8hex] [dst=N]
    h2d_status    (RO)  phase/boot/lane, gate verdicts, budget, counters
                        (ok/timeout/refused/desync), last out[0], max polls
    h2d_plan      (RO)  read-only preflight: the canned frame hex, the gate
                        verdict, node words, producer before/after, mailbox plan

Module parameter `h2d_send_budget` (uint, 0444/0644 defaults 1): 0 = negative
control, 1 = one frame per idea (bench default), max 8 for a supervised
multi-message session; `hcc_clear` resets H2D telemetry only, never this
budget.

### 3.5 The four synthesized messages (all from EXACT builders)

| attr form | group/id | wire | source |
| --- | --- | --- | --- |
| `txq_sync` / `h2d_txq_sync` | 0/1 | 48 B (72 B capture vector) | phase38 byte-exact first post |
| `stub [dst=N]` | 0/0 | 12 B | payload-free liveness stub |
| `edca [dst=N]` | 0/0x1d | 72 B | live steady-state message A (zeroed 0x3c) |
| `del_user user=U [addr=..]` | 0/9 | 24 B | decoded 12-byte del-user payload |

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
| host H2D logic selftest | `./tools/build.sh` (step 1) | **128/128 PASS** |
| kernel module build (project 6.18.60, -Werror) | `./tools/build.sh` (step 2) | PASS, **0 warnings**, 29 `T h2d_*` symbols |
| module artifact | `modinfo`/`sha256sum` | 391,456 B, sha256 `047be179f79e395b550876be8acef7352f8f61d7250e4b8eed974c2b37c7adc6`, vermagic `6.18.60 SMP mod_unload ARMv7 p2v8` |
| driver selftest (extended, 133 -> 192) | `./tools/build.sh` (step 3) | **192/192 PASS** |
| h2d library provenance | driver selftest | h2d_frames.c/.h/.inc byte-identical to wifi-h2d-frames; hcc_core identical to wifi-hcc-core |
| no mailbox/gate/out[5] write in the new path | driver selftest | PASS (one `hcc_core_sr_commit` call site, no direct mailbox writel) |

The host selftest covers the emission vectors (48 B wire, 72 B capture, stub,
edca, del_user), node word `(total<<16)|0x6D2B`, packed-producer wrap
(31 -> 0x400, phase 1), the `H2D_ESTATE` coalescing refusal, the full gate
matrix (all refusal reasons + positive cases) and the sysfs grammar.

## 6. Bench expectations (summary; full ladder in BENCH-PROCEDURE.md)

Golden PD3 boot -> ready pair -> READY_TXQ_SYNC -> `printf 1 > h2d_txq_sync`;
expected: out[0] bit3 **consumed** (0x8 -> 0 within the bounded window),
out[2] = 8, out[5] = 1 (read-only), core phase `ACTIVE`, dmesg
`H2D txq_sync: ... CONSUMED after N polls`, core events `h2d_sent`/
`h2d_complete`/`txq_sync`, golden page/state/mask unchanged, the D2H reader
still flowing.  Any new D2H frame (new g4 id, g3, or a g1/g2 catalogue id) is a
discovery to capture with `rd_catalog`/`rd_decode`.  Stop rules: one boot,
one frame, no retry; timeout/page change/abort => capture and stop.

## 7. Risks, limits and open items (honest)

1. **Node length 48 vs the vendor's live 72.** The captured vendor node word1
   declared 72 B (inherited ring buffer) while the header declares 48 B.  This
   driver follows the h2d-frames rule (node length = frame total = 48).  If the
   firmware's SR receive path requires node_len == buffer length or uses
   node_len rather than the header total, the first real frame could be
   rejected/dropped: the bench result will show it (out[0] consumed but no
   handler effect, or a clean rejection in the firmware log).  Fallback is a
   72-byte export/pad variant; the known-answer vector is already in the host
   selftest.
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

1. Attended PD3 golden-boot session: follow BENCH-PROCEDURE.md; load this
   module, run the existing ladder to `READY_TXQ_SYNC`, then send exactly one
   `h2d_txq_sync`.
2. Capture the observables (out[0]/out[2]/out[5], phase, events, D2H catalogue)
   and the exact dmesg H2D line; then stop (one boot, one frame).
3. If consumed but no firmware reaction: the next question is firmware-side
   (what the TXQ-sync handler does); the next host-side messages would be the
   edca / del_user exact frames behind the same gate (raise
   `h2d_send_budget` only for a supervised session).
4. If not consumed: the H2D lane stays closed after the core timeout - record
   and stop; the open item is the mailbox consumer path (PD3 evidence says it
   runs, so a real-message timeout would point at the handler/SR body path,
   not the announce).
