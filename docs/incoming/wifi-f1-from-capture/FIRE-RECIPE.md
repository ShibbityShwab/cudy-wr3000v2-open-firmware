# FIRE-RECIPE - the last host-message discriminator for F1 (EDCA timeout) + the RX test

**Lane:** `fw-f1-capture` (task-198), 2026-11-28. **Attended bench only, recovery card armed,
UART capture read-only, no blind writes.** This recipe fires the one stock H2D message class our
driver has never sent - the g0-id29 EDCA timeout (see REPORT sec. 3.1/5) - and then re-runs the RX
test. PASS = the laptop's auth produces **host RX frames > 0** with the handshake page still
golden; the outcome is a discriminator either way: if frames stay 0, F1 is **not** a missing H2D
frame (the capture's negative) and escalation is named below.

## 0. Preconditions (read-only gates; stop if any fails)

| # | check | expect | source |
| --- | --- | --- | --- |
| P0.1 | recovery card READY, human at the box, TFTP armed, UART capture running | - | `tools/recovery-card.sh`, `tools/uart-capture.sh` |
| P0.2 | bench link unmanaged at 192.168.1.88/24 + 192.168.10.44/24 | PASS | `tools/bench-link.sh` |
| P0.3 | golden PD4 boot: handshake page CA `0x1BF000` = `0x86807FEF/0x00000007/0x00000002/0x0000ABCD`, state CA `0x118B60` = 1, exception mask CA `0x40008` = 0 | matches | `wifi-irq-unmask` lane |
| P0.4 | `dmesg` shows the ready ladder: firmware upload diffs=0, release 0x5A5A, D2H gate 0xCECE, DEVICE READY, SR0/DR0 rings programmed, core phase READY_TXQ_SYNC/ACTIVE | matches | `wifi-h2d-send` lane |
| P0.5 | 2.4 GHz AP up (beaconing; the reference radio), laptop authenticated once -> **baseline host frames = 0** (the known failure) | 0 | `iw`/`iwpriv` readout + the driver's RX counters (`$D/h2d_status`) |

## 1. Driver delta before the fire (host-only, one small change)

In the `wifi-h2d-send` driver, `h2d_frames.c` `h2d_build_g0_id1d_edca_opt()`: after zeroing the
frame, set the 0xff tail - the stock payload is **20 x 00 then 40 x ff** (TABLES-families sec. 2.1):

```
/* stock hmac_edca_opt_timeout_fn payload: memset(payload,0,60); memset(payload+20,0xff,40) */
memset(f->payload + 20u, 0xff, 40u);          /* f->payload_len stays 0x3c, frame total 72 */
```

No other change: the header builder already emits group 0 / id 0x1d / token 0 / field A 0 /
field B 4 / field C 0, which is the captured frame. Rebuild + host tests and re-load the module by
hand (never packaged - image rule).

## 2. The fire ladder (one idea per step, bounded)

Use the driver's `h2d_send` sysfs token (grammar `edca [dst=N]`); all posts go through the
existing gates (latched boot, released, armed, READY_TXQ_SYNC/ACTIVE, lane open, announce
consumed, per-load budget). Attributes live on the PCI function:
`D=/sys/bus/pci/devices/$(basename $(dirname $(ls -d /sys/bus/pci/devices/*/h2d_status | head -1)))`
(same convention as `wifi-h2d-send/BENCH-PROCEDURE.md`).

**F-A (single frame).**
1. read the gates + page (P0.3/0.4) and `h2d_status` (out[0], mbox_inflight, h2d counters).
2. `echo edca > $D/h2d_send` (dst 0; field B 4).
3. expect within the driver's bounded timeout: node consumed (SR0 consumer advanced, "consumer in
   0 polls" class), `h2d_complete`, out[0] clears; **no D2H reply is expected** (the firmware
   handler CA `0xB568C` sends none) - completion without a reply is PASS.
4. re-read page/state/mask: golden / 1 / 0 required. Exceptions => STOP (sec. 3).

**F-A2 (bounded burst) - only if F-A is clean and the RX test is still 0.** Raise the send budget
for the supervised run (`echo 10 > /sys/module/hi5622_wifi/parameters/h2d_send_budget`, module name
per the loaded module) and post 10 EDCA frames at ~0.4 s intervals (the stock's cadence is one per
~0.44 s per chip). Same gates/observables; stop on the first exception.
Do not exceed one burst per boot.

**RX test after each fire step.** Laptop auth/assoc <= 3 tries; read the driver's RX counters +
`dmesg`; record host frames and any DR0 producer movement.

## 3. PASS / FAIL / STOP

* **PASS**: host RX frames > 0 (laptop associates / D2H data flows), page still
  `0x86807FEF/7/2` and mask 0 => the EDCA-context class was (part of) the F1 gate; report and
  proceed to the BA/rate-control rungs.
* **FAIL**: frames stay 0 with the page golden and no exception => **F1 is not a missing H2D
  message.** Record the outcome and stop the host-message search. Escalate (sec. 4).
* **STOP immediately**: any exception (mask != 0), page not golden, out[0] stuck, ETE stuck
  producer, module warning, or UART panic. No retry in the same boot; power cycle to clear;
  restore the previous module; note it in the ledger.
* **Never**: write CA `0x400392F0`; send g2-id29 (pointer payload, no firmware handler); add an
  H2D heartbeat (it is D2H - REPORT sec. 2); send key 0x0004 (the stock never does); loop the burst.

## 4. If FAIL - the escalation the capture supports

1. **Stock-control, same host port (RX lanes' SC-1)**: stock 2.5.24 + its AP + the same laptop
   auth through the *same* host programming. If the stock delivers RX frames and we do not, the
   difference is firmware-internal, not a host message (the capture already shows the H2D surface
   has no RX enable).
2. **Firmware-internal RX init**: the firmware owns an `RX Thread` name (CA `0x10CFFC`) and a
   `ppdu_dscr_list` spinlock (CA `0x106116`) whose init path has **no H2D trigger** in this
   capture. That is a PD4-patch/firmware-internal investigation lane, not a driver message - do not
   patch the firmware on a hunch.
3. **Host delivery layer**: only re-open if a stock-control run also shows frames = 0 on our port;
   then the difference is the port/ring programming, not the message set.

## 5. Evidence to capture in the run

sampler/decoder logs, `h2d_status` before/after, page/state/mask reads, `dmesg` lines for the
post and any exception, laptop auth result and host frame counter, UART log. Save under `build/`
(gitignored) and add a ledger entry; update README.md with the outcome.
