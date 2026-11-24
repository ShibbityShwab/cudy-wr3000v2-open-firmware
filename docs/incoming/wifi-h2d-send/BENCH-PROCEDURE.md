# BENCH-PROCEDURE - first REAL post-ready H2D message (g0 id1 TXQ config sync)

Host-only lane deliverable (wifi-h2d-send; task-98 ladder A, task-99 ladder B).
This is the attended ladder for the next session: golden PD3 boot -> ready pair
-> ONE frame -> capture-and-stop.  One variable, one boot, one frame, no
retries.  **Ladder A** fires the 48-byte node (`h2d_txq_sync`); **Ladder B**
fires the vendor's 72-byte live-node variant (`h2d_txq_sync72`) and is the
task-99 recommendation (see REPORT-G0ID1-HANDLER.md).  The task-98 module was
already fired once on a PD3 golden boot (Ladder A, 48-byte node): the frame
reached the device ring (ETE engine consumed the node) but the firmware
faulted (mask 0x12) and did not consume the mailbox announce - the boot stayed
golden.  Ladder B exists to change exactly one variable (node length) and to
collect the exception record that names the fault site.  Global rules unchanged: recovery door armed first, UART
**read-only** capture to an absolute path, no blind flash writes, no `rmmod`
after the release, no CECE during boot.

## Preconditions

* recovery door READY (`tools/recovery-card.sh`), bench link PASS
  (`tools/bench-link.sh`), UART read-only capture running to an absolute path;
* the PD3-patched firmware is staged locally as `/lib/firmware/hi_wifi/FIRMWARE.PD3.bin`
  (sha256 `eea9f2ff9345dc8d16badd65e77a5f8b82cf58740b7203239838259adc3905ce`,
  local evidence only - never committed);
* the Wi-Fi PHY custom block `/lib/firmware/hi_wifi/wifi_block.bin` is staged
  from the local stock capture with `tools/mkblocks.py` (never committed);
* the module from THIS tree is on the rootfs:
  `hi5622_wifi.ko` (`./tools/build.sh` reports size/sha256; the hash is
  build-path dependent - record the value printed by the build you deploy);
* Lead decision recorded: this session fires **exactly one** post-ready H2D
  frame (`h2d_send_budget=1`, the default).

## Sysfs surface used here

Each attribute lives in the endpoint device directory
`/sys/bus/pci/devices/<bdf>/` (`<bdf>` = the Hi5622V100 function):

    transport: prepare upload verify seed release gate_repair status boot dev_status
    service:   hcc_arm hcc_loop hcc_service hcc_clear hcc_status hcc_ready
               hcc_events hcc_rings hcc_plan
    hcc:       hcc_sr_announce hcc_cece_kick hcc_dr_republish
    h2d:       h2d_txq_sync (WO)  h2d_txq_sync72 (WO)  h2d_send (WO)
               h2d_status (RO)  h2d_plan (RO)
    d2h:       rd_poll rd_advance rd_status rd_decode rd_catalog

## Ladder

| step | action | expected / stop rule |
| --- | --- | --- |
| S0 | `insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD3.bin` | probe only (no device writes). dmesg: "HCC service ready", "HCC core: portable hcc_core initialised". `cat h2d_status` shows `budget=1 frames_sent=0 txq_sent=0` and gates `not_armed`. STOP if the `h2d_*` attrs are absent (wrong module loaded) |
| S1 | `echo 1 > prepare` | six endpoint-iATU viewports, 6/6 readback OK |
| S2 | `echo 1 > upload ; echo 1 > verify` | PD3 image upload 928,920 B, verify diffs=0 |
| S3 | `echo 1 > seed` | platform descriptor (built-in) + wifi block (local file) written and read back; STOP on readback diff |
| S4 | `echo 1 > hcc_arm` | vendor-exact 7-channel ETE init, readback OK; NEVER write CECE here |
| S5 | `echo 1 > hcc_loop` | service thread started (core tick + direct reader); start BEFORE the release |
| S6 | `echo 1 > release` | 0x5A5A only; refused unless windows+upload+verify+seed are done. The core arms its boot watch |
| S7 | watch `boot hcc_ready hcc_events rd_status` | LATCHED golden handshake 0x86807FEF/7/2/0xABCD, state CA 0x118B60=1, mask CA 0x40008=0; direct reader decodes g4 id1 (<=2.0 s) then g4 id2 (<=20.0 s); core phase `READY_TXQ_SYNC`. STOP if the ready pair does not arrive (that is a different experiment) |
| S8 | **dry-run preflight (no write)**: `cat h2d_plan ; cat h2d_status` | `h2d_plan: gate=ok` with `msg=txq_sync wire_total=48` and `wire=000100043000010000005a5a0000000001000000d800140001000000...ffffffff`, `node_word1=0x00306d2b`, `producer=0x00000000 -> 0x00000001`, `mailbox id=3 out[0]=0x00000008 out[2]|=1`. `h2d_status` gates: `txq_sync=ok real_msg=ok`. **STOP if the gate is not "ok"** - do not raise the budget, do not retry; capture the gate reason |
| S9 | **fire one frame**: `printf 1 > h2d_txq_sync` | the store returns after the bounded consumption window (<= ~1 s) and returns success only if out[0] was consumed. On the shell: `echo $?`. Do NOT repeat the command |
| S10 | immediately capture (read-only): `cat h2d_status h2d_plan hcc_events hcc_ready boot rd_status rd_catalog` + `dmesg \| tail -40` + UART log | see the observables table below; then STOP (one boot, one frame) |

Do **not** use `hcc_sr_announce` in this load: the liveness stub and a real
message are one idea per module load (the driver refuses the second one with
an explanatory message).  The PD3 session already proved the 12-byte announce
is consumed; this session asks whether the real 48-byte body is.

## Expected observables (S10)

| # | observable | pass / meaning |
| --- | --- | --- |
| 1 | the S9 sysfs write returned success | frame was committed and the announce was consumed within the bounded window |
| 2 | `h2d_status`: `frames_sent=1 txq_sent=1 ok=1 timeout=0 refused=0 desync=0`, `last_out0=0x00000000`, small `max_polls`, phase `ACTIVE` | full consumption path |
| 3 | dmesg `H2D txq_sync: frame 48 B committed on SR0 slot 0 devva 0x... node word1 0x00306d2b producer 0x00000000 -> 0x00000001; coalesced announce id3 out[0]=0x00000000 CONSUMED after N polls; core phase=ACTIVE` | exact commit evidence (record N and the devva) |
| 4 | `hcc_events`: `h2d: pub=1 done=1 ...` (or pub>1 if the tick coalesced), core `phase=ACTIVE`, the `txq_sync` event in dmesg | the core observed the SR commit and the announce completion |
| 5 | `boot`: page/state/mask still golden (page may show the PD3 overwrite; the **latched** gate is authoritative; `page_lost_after_latch` counts it), mask CA 0x40008 = 0 | no boot-state damage |
| 6 | `rd_status`/`rd_catalog` keep decoding; the direct reader is still primary | receive path unaffected |
| 7 | out[5] CA 0x400392F0 reads 1 (read-only observation; the driver never writes it) | PD3 dispatcher ack behaviour, if the firmware path ran |
| 8 | any NEW D2H frame (a new g4 id, a g3 frame, or a g1/g2 catalogue id) | **discovery**: capture the raw frame with `rd_decode` and stop |

There is no guarantee of a D2H response to the TXQ sync: **consumed with a
silent firmware is a valid, positive result** (the first real post-ready H2D
reached the firmware dispatch path).  Record the exact capture; "no response"
is data, not failure.

## Stop rules (all mandatory)

* **One boot, one frame, no retry.**  The store refuses a second frame
  (`budget_exhausted`, and the core closes the lane on timeout).  A repeat
  experiment needs a fresh boot and a Lead decision.
* `h2d_status timeout=1` or dmesg "announce NOT consumed": STOP.  Capture
  out[0], out[2], out[5], `hcc_rings` (SR0 producer/consumer), boot page/mask/
  state.  Do not re-fire, do not reload to retry the same boot.
* The golden page / state / mask change, or `hcc_ready` leaves
  `READY_TXQ_SYNC`/`ACTIVE` in the wrong direction: STOP and capture.
* Any data abort / SError / wedged register read in dmesg or on UART: capture
  dmesg + UART, then **power-cycle**.  Do not `rmmod` after the release.
* Any unexpected D2H burst or new message id: capture and stop (discovery -
  next session analyses it).
* The gate at S8 is not `ok`: do not fire, capture the reason (see the gate
  table in REPORT.md 3.3) and stop.

## Ladder B - the 72-byte LIVE-node fire (task-99)

**Question it answers:** does the vendor's captured 72-byte SR node buffer
(48 frame bytes + 24 x 0xff pad, node word1 `(72<<16)|0x6d2b`, header total
still 0x30) change the firmware's behaviour versus the 48-byte node of the
first fire?  The static analysis says the g0 id1 handler parses no length
field, so this isolates the transport/engine layer.

**Module:** the task-99 build of this tree (`hi5622_wifi.ko`, **394,272 B**,
sha256 **4677ceac...**, vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, 0
warnings, host 156/156 + driver 204/204).  Record the hash printed by
`./tools/build.sh` of the build you deploy (it is build-path dependent).

**Preconditions** are Ladder A's, plus: a fresh PD3 golden boot (the lane is
closed after a fault; never reuse a boot that already fired).

| step | action | expected / stop rule |
| --- | --- | --- |
| B0 | `insmod hi5622_wifi.ko fw_path=hi_wifi/FIRMWARE.PD3.bin` | as A/S0; `h2d_status` shows the new gate line `txq_sync72=...`; STOP if the attr/module is the task-98 build |
| B1-B6 | prepare / upload / verify / seed / hcc_arm / hcc_loop / release | as A/S1-S6 (release 0x5A5A only, no CECE during boot) |
| B7 | watch `boot hcc_ready hcc_events rd_status` | golden page 0x86807FEF/7/2/0xABCD, state 1, mask 0, ready pair id1+id2, phase READY_TXQ_SYNC; STOP if not |
| B8 | **dry-run preflight (no write)**: `cat h2d_plan ; cat h2d_status` | `h2d_plan` prints BOTH variants: `msg=txq_sync node_len=48 node_word1=0x00306d2b` and `msg=txq_sync72 node_len=72 node_word1=0x00486d2b pad=0xff`, plus `gate=ok`; `h2d_status` gates `txq_sync=ok txq_sync72=ok`. STOP if the gate is not ok - do not raise the budget |
| B9 | **fire one frame**: `printf 1 > h2d_txq_sync72`; note `echo $?` | success only if out[0] was consumed within the bounded window; **do NOT repeat** |
| B10 | capture (read-only): `cat h2d_status h2d_plan hcc_events hcc_ready boot rd_status rd_catalog`, `dmesg | tail -40`, UART log | see observables below; then STOP |
| B11 | **read-only exception record** (only if B9 failed or mask != 0): bench read helper at CA **0x0004000C..0x0004004F** (region-0 alias, BAR0 = CA; 17 words: r0-r12, sp, lr, PC, SPSR of the LAST exception) | PC 0x8103C/0x81040 => the g0 id1 handler's field-B context fault; a cave address (0x4000C..0x4004F) => the PD3 poller-clobber cascade; SPSR mode 0x17/0x1B = ABT/UND. **Read only - never write this area** |

### Expected observables (B10) - 72-byte variant

| # | observable | pass / meaning |
| --- | --- | --- |
| 1 | B9 store returned success | frame committed and the announce consumed within the window |
| 2 | `h2d_status`: `frames_sent=1 txq_sent=1 ok=1 timeout=0`, phase ACTIVE, `last attempt ... wire_total=48 node_len=72` | full consumption path with the 72-byte node |
| 3 | dmesg: `H2D txq_sync72: frame 48 B in a 72 B node committed on SR0 slot 0 devva 0x... node word1 0x00486d2b producer ... -> ...; coalesced announce id3 out[0]=0x00000000 CONSUMED after N polls` | exact commit evidence (record N/devva) |
| 4 | SR0 producer/consumer from `hcc_rings` | consumer advanced (ETE engine took the node) |
| 5 | `boot`: page/state/mask still golden; mask CA 0x40008 = 0 | no exception taken |
| 6 | `hcc_events`: `h2d{pub=1 done=1 ...}`, `vector_mask=0x0` | clean |
| 7 | read-only out[5]/out[2] diagnostics | not pass criteria (write-pulse observables) |

### Interpreting the 72-byte result

| symptom | meaning | next host-side step |
| --- | --- | --- |
| success (rows 1-6) | node length 72 is accepted; node length was not the fault cause; compare with the 48-byte fire -> transport layer was the difference or nothing was | report; next question is the field-B context/precondition (REPORT-G0ID1-HANDLER sec 2/4) |
| same fault signature as the 48-byte fire (3x `out[1]=4`, mask 0x12, out[0] stuck) | node length is **not** the cause; the g0 id1 handler/dispatcher path faults (context or cave cascade) | B11 read-back; then fresh boot with the payload-free g0 id0 control ('stub') to separate transport from payload |
| clean transport but new D2H frames | the firmware reacted to the frame | capture and stop (discovery) |

**Stop rules** are Ladder A's, plus: after any exception/mask change, B11 is
read-only once and the session ends (power-cycle; never rmmod after release).
One boot, one frame, no retry.  A second experiment needs a fresh boot and a
Lead decision.

## Interpreting the negative cases

| symptom | most likely meaning | next step (host-only) |
| --- | --- | --- |
| gate `not_latched` / `phase_not_ready` | the boot did not reach the latched ready state this time | capture boot/hcc_ready; the ready-pair experiment is the gate, not this frame |
| gate `liveness_probe_used` | `hcc_sr_announce` was already fired in this load | fresh boot; choose ONE of the two |
| gate `announce_inflight` / `out0_not_consumed` | a previous announce word is still pending | fresh boot; never stack/fire again |
| write returns `-ETIMEDOUT` / dmesg "NOT consumed" | the firmware did not clear the real body announce within 1000 ms (the PD3 stub announce was consumed in 0 polls - so this isolates the real frame/body path) | record SR0 producer/consumer + out[0]; compare node length 48 vs the vendor live 72 (REPORT risk 1); no retry in-session |
| consumed, no firmware reaction | the TXQ-sync handler ran and produced nothing observable (or nothing after the sync) | clean negative; decide the next exact message (edca/del_user) in a supervised session |
| consumed, new D2H frames | the firmware reacted to the first real message | capture and stop; new experiment |

## Read-out cheat sheet

* `h2d_status` - the H2D send path's own view: phase/latched/lane, both gate
  verdicts, budget/frames, ok/timeout/refused/desync counters, last out[0],
  max polls, last attempt.
* `h2d_plan` - read-only preflight of the canned TXQ sync: frame hex, gate,
  node words, producer before/after, mailbox plan.  Safe to read at any time
  (it performs no write).
* `hcc_events` / `hcc_status` / `hcc_ready` - core view: phase, mailbox
  pub/done/timeouts, CECE policy, ready pair, direct-reader state.
* `boot` - handshake page + gate + state/mask + the six mailbox words
  (out[5] read-only).
* `rd_catalog` / `rd_decode` / `rd_status` - device->host frames decoded
  directly from the device group-A rings.

Recovery is physical: power-off + RESET held through the counter + TFTP
(`tools/recovery-card.sh`).  An agent cannot recover the box alone.