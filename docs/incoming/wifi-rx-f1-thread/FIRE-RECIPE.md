# FIRE-RECIPE - the EDCA auth test: one g0-id29 timeout, then the laptop auth, then a bounded burst

**Lane:** wifi-rx-f1-thread (task-216).  **Attended bench only** - recovery card
READY, human at the box, UART capture read-only, bench link PASS, one rung per
boot.  This is the last host-message discriminator for F1 (rank 1 of the
fw-f1-capture lane).  The lane never fires; the Lead fires.

## 0. What this rung tests and what it cannot test

The stock host sends the **g0-id29 EDCA timeout** once per ~0.44 s in steady
state; our driver has the frame (`edca` token, group 0 id 0x1d, field B 4) but
has never fired it.  The firmware handler chain (CA 0xB568C -> hook G+0x378 ->
CA 0x68C0A) updates the per-chip EDCA context (`ctx+0x4AA/+0x533/+0x0DB`) and
**sends no reply**.  So the rung's discriminators are:

1. the frame is **consumed** (dmesg + counters) and the page stays golden;
2. the per-chip context fields change as decoded (PROBES P4 items 6/7/8; weak if
   `ctx+0x4AA` is already 0 pre-fire);
3. the laptop's auth produces **host RX frames > 0** (the F1 PASS condition).

A negative is informative: it closes the last host-message candidate and sends F1
to the firmware-internal path (sec. 4).

## 1. Preconditions (read-only; stop on any mismatch)

| # | check | expect | source |
| ---: | --- | --- | --- |
| P0.1 | recovery card READY, human present, TFTP armed, UART capture running (`tools/uart-capture.sh start --label f1-edca --timeout 900`) | - | tools |
| P0.2 | `tools/bench-link.sh` PASS (`enp195s0f3u1`, 192.168.1.88/24 + 192.168.10.44/24) | PASS | tools |
| P0.3 | module identity: the current gate-inverted build (the R5 tree from `wifi-band1-start-send-5`, rebuilt with this lane's EDCA delta if the tree must be rebuilt) - record size/sha/param count | matches the build evidence | `modinfo -p`, sha256 |
| P0.4 | golden boot: `cat $D/boot` = 0x86807FEF/7/2/0xABCD; `cat $D/status` state 1, mask 0 | golden | wifi-irq-unmask |
| P0.5 | ready ladder in dmesg: upload diffs=0, release 0x5A5A, gate 0xCECE, DEVICE READY, SR0/DR0 programmed, phase READY_TXQ_SYNC/ACTIVE | all | wifi-h2d-send BENCH-PROCEDURE |
| P0.6 | 2.4 GHz AP up and beaconing; the observer sees `cudy-ap-test*`; **baseline laptop auth -> host frames = 0** (the known failure) | 0 | `iw` + `$D/rd_status` |
| P0.7 | probe pack R7 pre-pass recorded: P1/P2/P3/P4/P5 (`PROBES.md`) | recorded | this lane |

Do not start if P0.3-P0.6 are not all green: this rung assumes a *known-good*
boot where the only change is the EDCA post.

## 2. Host-only delta (the only code change)

In the driver's `h2d_frames.c`, `h2d_build_g0_id1d_edca_opt()`: after the frame
is zeroed and the 0x3c payload length set, make the payload byte-exact with the
stock capture (**20 x 00 then 40 x ff**):

```
/* stock hmac_edca_opt_timeout_fn: memset(payload,0,60); memset(payload+20,0xff,40) */
memset(f->payload + 20u, 0xff, 40u);   /* payload_len stays 0x3c, frame total 72 */
```

[patches/0001-edca-payload-ff.patch](patches/0001-edca-payload-ff.patch) carries
this hunk.  Functional note: the firmware handler reads only payload[0..19] (the
four 5-byte EDCA entries), so the zero payload's first 20 bytes are already the
stock bytes; the tail is conformance-only.  If the tree is not rebuilt, the fire
may proceed on the current builder - the header (group 0/id 0x1d/field B 4/token
0) and payload[0..19] are the functional bytes; record which builder was loaded.

**Budget:** load with a budget that covers the AP start + the EDCA posts in one
load.  The R5 5 GHz plan uses 9 posts at `h2d_send_budget=10`; for this 2.4 GHz
rung load with **`h2d_send_budget=20`** (AP start <= 9 + one EDCA + a 10-post
burst = 20).  This is the first fire above budget 10: it is deliberate and
supervised; if the Lead prefers the minimum, run the single-frame rung at
budget 10 first, then a fresh boot at 20 for the burst.  Never raise the budget
mid-session.

## 3. The fire ladder (one idea per step, bounded)

Driver attribute directory:
`D=/sys/bus/pci/devices/$(basename $(dirname $(ls -d /sys/bus/pci/devices/*/h2d_status | head -1)))`.

**F-A - one EDCA frame.**
1. read the gates (`cat $D/h2d_status`), the page/state/mask, and P1/P4 again;
2. `printf edca > $D/h2d_send` (dst 0, field B 4);
3. expect within the bounded poll window, in this order:
   * dmesg `H2D edca: frame 72 B in a 72 B node committed on SR0 slot ... node word1 0x00486d2b producer ...; coalesced announce id3 out[0]=0x... CONSUMED after N polls; core phase=ACTIVE`
     (no D2H reply is expected: the handler sends none);
   * `h2d_status`: `frames_sent` +1, `timeout=0`, `refused=0`, `desync=0`, `mbox_inflight=0`, `out[0]=0`;
   * page/state/mask still golden / 1 / 0;
4. re-read PROBES P4 items 6/7/8: predicted `ctx+0x4AA = 0x0000`, `ctx+0x533 = 0x00`, `ctx+0x0DB` bit2 = 0 (record the pre-fire values; if they are already 0, the rung is outcome-only).
5. If the announce is not consumed, or any gate/mask/page changes: **STOP** (sec. 5), do not re-post.

**F-A2 - the RX auth test (after a clean F-A).**
6. laptop auth/assoc to the 2.4 GHz AP, <= 3 tries (keep the same NIC and channel);
7. read `$D/rd_status`, `$D/rd_catalog`, `dmesg | tail -60`, and PROBES P1/P2/P3/P4 post;
8. **PASS** = host RX frames > 0 (`rd_status` frames, or a decodable D2H frame with
   tag 0x5a5a in `rd_catalog`) **and** page golden 0x86807FEF/7/2/0xABCD, mask 0.

**F-B - bounded burst (only if F-A was clean and F-A2 frames = 0).**
9. post 10 EDCA frames at ~0.4 s intervals (the stock cadence is one per
   ~0.44 s), same gates; stop on the first exception/page change;
10. repeat F-A2.  **One burst per boot.**
11. **FAIL** = frames still 0 with the page golden and no exception: F1 is not a
    missing H2D message; record and go to sec. 4.

## 4. PASS / FAIL / STOP

| outcome | meaning | next |
| --- | --- | --- |
| F-A consumed + golden; F-A2 frames > 0 | the EDCA context class was (part of) the F1 gate | report; proceed to the BA/rate-control rungs |
| F-A consumed + golden; F-A2 frames = 0 | F1 is firmware-internal (the capture's negative confirmed) | sec. 4 escalation; do not add more host messages |
| F-A refused by a gate | do not fire; the gate names the reason | capture `h2d_status` + plan; power-cycle; wrong build / wrong phase |
| announce not consumed / out[0] stuck | host side desync | STOP, power-cycle, capture SR0 producer/consumer |
| page not golden / mask != 0 / UART panic at any point | firmware fault (the R2b class) | STOP, power-cycle, capture UART + the fault words; no retry in the same boot |
| the laptop cannot auth after 3 tries | not an RX result | STOP; record; the same boot is burned for this rung |

**Never:** write CA 0x400392F0; read the RC misc window; send g2-id29 (pointer
payload, no firmware handler); add an H2D heartbeat (it is D2H); send key
0x0004; loop the burst; re-post a consumed frame; raise the budget mid-session.

## 5. Evidence to capture (gitignored, under `build/`)

* the module identity (size/sha/params) and the two builder variants (with/without
  the 0xff tail) recorded;
* the full `h2d_status` before/after, the F-A dmesg block, the page/state/mask
  triple before/after;
* PROBES R7 pre/post (P1/P2/P3/P4/P5) with raw hex words;
* the laptop NIC log / auth result and the host RX frame counters
  (`rd_status`, `rd_catalog`);
* the UART capture (absolute path).

## 6. If FAIL - the escalation path (highest information first)

1. **Stock-control on the same host port (the RX lanes' SC-1)**: stock 2.5.24 +
   its AP + the same laptop auth.  If stock delivers RX through the same host
   programming, the difference is firmware-internal.
2. **The stock capture around a client auth (not just AP boot)**: the committed
   H2D trace was taken on the stock host during bring-up; if it did not include a
   client association, the missing piece may be a **first-client** H2D message
   rather than an AP-start message.  Repeat the sampler (wifi-rx-h2d-trace runbook)
   around a stock laptop auth and diff the captured families, including the D2H
   side (the direct device group-A reader in the same module can dump DR nodes).
3. **The firmware-internal path**: PROBES P4 (PPDU list) + P3 (DR node staging)
   over the stock-control auth.  If stock moves P3/P4 and ours does not, the
   missing step is inside the firmware RX consumer - a PD4-patch/firmware-internal
   investigation, not another host frame.
4. **Do not patch the firmware on a hunch** from this lane; a firmware patch needs
   its own static evidence and its own fire recipe.
