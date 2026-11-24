# BENCH-PROCEDURE - first REAL post-ready H2D message (g0 id1 TXQ config sync)

Host-only lane deliverable (wifi-h2d-send, task-98).  This is the attended
ladder for the next session: golden PD3 boot -> ready pair -> ONE
`h2d_txq_sync` frame -> capture-and-stop.  One variable, one boot, one frame,
no retries.  Global rules unchanged: recovery door armed first, UART
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
    h2d:       h2d_txq_sync (WO)  h2d_send (WO)  h2d_status (RO)  h2d_plan (RO)
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
