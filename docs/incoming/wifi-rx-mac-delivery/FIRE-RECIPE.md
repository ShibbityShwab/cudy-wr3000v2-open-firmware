# FIRE-RECIPE - the MAC-RX intake attribution (measurement pass first; no blind write)

**Lane:** wifi-rx-mac-delivery (task-220).  This decode justifies **no device write**: the
residual layer (the SMAC RX-completion producer) has no host message in the decoded
surface, and inventing one would be a blind fire.  The recipe is a **read-only probe pass
plus a stock control**, with an escalation that stays read-only unless a new lane names a
concrete host message.  The Lead owns any run; this lane never contacts the device.

## 0. Preconditions (physical, unchanged)

Bench link PASS (`tools/bench-link.sh`), recovery door READY (`tools/recovery-card.sh`),
read-only UART capture to an absolute path (`tools/uart-capture.sh`), golden page/status
(f1-thread P0), `STATUS.md` next-action gate.  No `rmmod`, no write to CA 0x400392F0, no
RC misc window, no flash.  Any fire follows the supervised protocol.

## R1 - probe pass during the client auth burst (read-only, ~2 min)

1. Boot the normal 2.4 GHz AP ladder (the F1 reproduction state: page golden, h2d_status
   healthy, AP-ENABLED).
2. Record **PROBES P0** (host D2H catalogue: ready pair + HEARTBEAT, no g1 id13) and
   **P1-P10** (one coherent pass).
3. Laptop sends the 3 auth frames to the AP MAC (the clean F1 reproduction).
4. Immediately record **P0** and **P1-P10** again; dmesg tail.
5. Decide with the PROBES decision table.

**Acceptance:** both passes coherent (P1 = 0x001060F7), no page/mask anomaly; the
pre/post tuple recorded in one line per boot.

**Interpretation shortcuts:**
* counters move + g1 id13 in P0 -> the path works; F1 moves to the host decoder.
* counters move + no g1 id13 -> consumer ran; the drop is in the builder gate matrix
  (PROBES -> find dmesg 0xA4E/0x49F/0x497).
* counters do not move + bit 10 transitions -> the break is exactly the missing producer
  (SMAC PPDU completion -> RX ISR -> node): the lane's main hypothesis B1.
* counters do not move + bit 10 unchanged -> the MAC did not run the RX completion path in
  this boot; re-check the earlier R0 parity state before drawing conclusions.

## R2 - stock control (read-only, decisive for the producer claim)

Repeat the same P1-P10 pass on a **stock boot** (vendor image + vendor AP), same phase,
with a client that successfully authenticates (the committed stock control already proved
stock RX works).  The comparison:
* stock counters move (and/or a stock-side datapath equivalent is active) while ours do
  not -> the difference is in the firmware RX producer state that our host-only boot does
  not build; the host cannot fix it by a message we do not know -> F1 is a firmware-state
  bug, escalate R3.
* stock counters also do not move -> the counters are not the right observation point for
  management RX; fall back to P0 (g1 id13) and P9/P10 and the UART capture, and record the
  negative.
* stock page/status differs -> record; do not compare boot phases that differ.

## R3 - escalation (read-only unless a new lane delivers a named message)

1. **Producer localisation [U]:** the SMAC RX-completion ISR is named in the name table
   (CA 0x12266C: `smac_rx_complete_2g/5g_isr`) but not addressable from the image
   statically (no code refs).  A follow-up lane can:
   (a) scan the image for the interrupt dispatch that reads the SMAC interrupt status and
   indexes a handler table (the ARM exception stubs at 0x103090-0x103140 and the
   Thumb exception entries at 0xC6FC8-0xC6FE0 are decoded in TABLES; the SMAC IRQ
   controller config data block is at CA 0x122500);
   (b) match the handler to the crash-dump name table by order/line-number usage;
   (c) only then propose a host action.
2. **Any write** (e.g. re-firing the EDCA message, a guessed RX/TRX enable) is **not**
   authorised by this lane and is not indicated by the decode: the stock capture contains
   no RX-enable message, and the EDCA message only writes a per-chip MAC context.
3. **Firmware-state escalation (if the producer is proven silent):** the remaining lever
   is the boot/init sequence that arms the SMAC RX completion path, which is
   firmware-internal.  Document the stock-vs-ours producer state (R2) as the bug report;
   the fix belongs to a firmware-side decode lane, not a host fire.

## STOP rules

* Stop the pass on any page/mask golden loss, exception announce, or read abort.
* Stop and power-cycle if the MAC live bank becomes unreadable.
* Never continue a pass on a boot whose P1 does not match; the addresses may be wrong.
* Recovery actions are physical and attended; an agent cannot recover alone.

## Acceptance / rollback

* R1 acceptance: pre/post P0+P1-P10 recorded, page golden, no write performed.
* R2 acceptance: same-phase stock tuple recorded or a documented negative.
* No recovery action is expected from a read-only pass.  Any deviation is handled by the
  normal attended recovery cycle.
