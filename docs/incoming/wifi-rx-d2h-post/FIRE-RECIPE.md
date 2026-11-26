# FIRE-RECIPE - one attended read-only pass: decide C5 (queue not drained) vs C6 (CRX gates) vs C7 (transport customer)

**Lane:** wifi-rx-d2h-post (task-226). **HOST-ONLY lane; the Lead fires.** This lane delivers
the decode and the probe pack; it performs no device contact. Attended session required; the
recovery door stays armed per the project rules. No writes.

## Preconditions

* A coherent boot already used for the F1 (the one where `band_obj+0xCC0` moved +105 and the
  host catalogue showed g1 id3 + g4 id4) is the ideal tuple. If the box rebooted, re-baseline.
* The direct-D2H reader / catalogue is loaded (`rd_catalog` available) - P0 uses it.
* The read helper (`bar0read`, physical addresses, O_RDONLY) is available.
* No image change is needed for this pass.

## Steps (all read-only)

1. **P0 baseline:** `cat rd_catalog` - record the ready pair, heartbeat, g1 id3; note the
   absence of g2 id3 and g1 id13. Save the line count.
2. **P12-P19 baseline (before the burst):** one read pass per [PROBES.md](PROBES.md). Save the
   raw words (P12/P14/P15/P16 handlers and stats) - especially `item[3].handler` (must be
   0xBBC9C) and the queue-1 list count.
3. **The burst:** run the standard client auth attempt used in the F1 (no host writes beyond
   the normal association attempt; no module reload; no firmware command).
4. **P0 after:** `cat rd_catalog` - look for a delta and for any g2 id3 / g1 id13 frame.
5. **P12-P19 after:** repeat the identical read pass.
6. **Diff** the two passes and apply the [PROBES.md](PROBES.md) decision matrix.

## Stop rules

* Any read returning 0xFFFFFFFF / an abort: record and stop, do not retry another width.
* Any page-loss or mask anomaly: power-cycle, recovery door, UART capture (read-only).
* Never write CA 0x400392F0; never read the RC misc window.
* One coherent boot per tuple; a reboot invalidates the baseline.

## Expected decision (what this pass buys)

* **C5** - item stats grow, queue-1 list count grows by the same delta, no g1 id13: the local
  dispatch queue is not being drained (queue-1 notifier/worker). Next lane: hcc worker/notifier
  registration, with the P15 notifier pointers as the target state.
* **C6** - item stats grow, queue-1 stays 0, `band+0xCC5` grows: the local dispatch ran and the
  CRX gates dropped the frames. Next lane: the CRX gate (per-chip RX ctx `[chip+0x1a00]`,
  forward flag, length windows, total cap).
* **C7** - item stats grow, queue-1 stays 0, `band+0xCC5` stays 0, no g1 id13: the g1 id13 was
  built but not deposited. Next lane: the group-1 transport queue customer (q0) and the
  `0xBA17A` post.
* **g1 id13 appears in P0** - the RX chain works end-to-end; record and close.
