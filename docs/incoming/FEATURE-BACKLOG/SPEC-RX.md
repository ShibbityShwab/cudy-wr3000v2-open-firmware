# BACKLOG SPEC - the RX gate (RX-1..RX-5) - the critical path

The one bug that blocks every live feature (association, 4-way, DHCP, WPA3 client, traffic,
throughput). Proven state: the device works (the stock control: the laptop authenticated +
associated with the vendor AP on the same chip); our host's RX enable/format/device-state/
address-transform/WOC-class/completion-phase are all vendor-correct; the MAC receives the
client's frames (the sticky status bit10) but writes NOTHING to host memory (frames=0); with
rx_enable=0 the same frames are harmless; the receive path faults the firmware intermittently.

---

## RX-1 - the stock-image H2D trace (the decisive reference)

- Goal: capture the vendor driver's OWN host->device message sequence during a working boot,
  so we can diff it against ours and find what a working RX path starts that we never start.
- The method (decoded by the rx-h2d-trace lane, docs/incoming/wifi-rx-h2d-trace/):
  the vendor's H2D messages are plain HCC frames in host DRAM, posted as 8-byte SR nodes into
  a PCIe-visible ring -> traceable READ-ONLY from userspace on the stock image via
  (a) the endpoint BAR0 window + /dev/mem sampling, or (b) dynamic kprobes on hcc_msg_tx
  (tracefs; the one-shot init aid that arms them inside the module-load window was proven in
  phase 21). The vendor debug modules carry no H2D log (their save_hostmem is a DDR dump).
- The exact source: the stock pair-A image (kernel 5.10.201 + the vendor hi5622v100_wifi.ko /
  plat.ko); the hcc_msg_tx symbol in the stock wifi module; the SR ring at the stock's
  boot-programmed BAR offsets (the RUNBOOK T1-T7 lists the reads).
- PASS: a captured list of the vendor's H2D messages (group/id/length/sequence) from
  power-on to the AP-ENABLED state, saved under build/ (never committed as vendor bytes -
  only the decoded message table in text).
- FAIL: no capture (then the kprobe path or the static alternative).
- Test: docs/incoming/wifi-rx-h2d-trace/RUNBOOK.md T1-T7 (attended; recovery armed).
- Deps: the stock pair-A boot; the recovery door; the UART read-only capture.
- State: READY (the RUNBOOK + the sampler; not yet executed).
- Owner: the Lead (the fire); no further lane work needed unless the trace fails.

## RX-2 - the firmware RX-init identification (the fix)

- Goal: identify the host message (or message sequence) that makes the firmware's RX path
  hand received frames to the host.
- The source: the firmware's RX handlers (the strings smac_msg_proc_rx_ring_reset_req @file
  0xE28BC / enable_trx_req @0xE29A8; the dispatch table mapping ids to those handlers - the
  table itself is [U]); the vendor host's senders of those ids (the hcc_msg_tx call sites in
  the wifi module; the 82-sender list in docs/incoming/wifi-rx-crx-gating/).
- PASS: a message (group/id/payload) whose posting (in our driver) is followed by
  dp_rx_capture frames>0 with the page golden and a real client auth frame arriving.
- FAIL: no candidate from the trace -> the firmware-side state was already initialised and the
  bug is elsewhere (then the RX-3 probe verdict classifies).
- Test: the same R2 fire (the laptop auth; the dp_rx_probe).
- Deps: RX-1 (the trace is the decisive source) or a deeper firmware dispatch-table decode.
- State: candidates exhausted (the boot-chain sends no RX init; the config-event keys 0x64/
  0x65/0x66/0x67/0x69/0xD8/0x320 mapped; key 0x0004 has no vendor sender); the trace remains.

## RX-3 - the RX probe verdict (the classifier)

- Goal: classify the failure once the rings are armed and a real client sends frames.
- The source: dp_rx_probe (the rx-format lane) - read-only; DEVICE-WROTE-FRAMES /
  MAC-WROTE-NOTHING / FORMAT-SETUP-MISMATCH.
- PASS: a verdict recorded on the bench (any of the three is a valid datum).
- Test: the R2 fire with the probe module (docs/incoming/wifi-rx-format/FIRE-RECIPE.md).
- State: the tool is implemented; not fired with the final module (the earlier fires used
  the dp_rx_capture path, not the probe).

## RX-4 - the candidate fires (evidence of what is NOT the cause)

- Done: the completion-phase prepublish (bit2 ticks, frames=0), the WOC buffer class (loads,
  frames=0), the rx-init key 0x0004 (accepted, frames=0), the identity transform (correct),
  the F4 candidates (the status forwarding not implemented), the rx_drain_write=0 observe
  (frames=0), the parity (21/21 + the device words match the stock).
- Value: each negative narrows the space; all are documented with the fire evidence.

## RX-5 - the F4 status forwarding (a plausible missing host write)

- Goal: the vendor's ISR forwards the status bits 1/8 to the firmware core as HCC messages
  (helpers 0x39558 / 0x3920c in the wifi module); our driver never forwards.
- PASS: the forwarding implemented (a bounded, masked, read-back-verified message on the
  status service) + the fire where the frames still do NOT appear (a negative) or DO appear.
- Test: a fire with rx_status_forward=1 (a new param) + the laptop auth + the probe.
- Deps: the exact message decode from the helpers (the group/id/payload).
- State: the helpers are known; the message bodies are not decoded yet.

## The order
1. RX-1 (the trace) - the decisive reference
2. RX-2 (the fix from the trace)
3. RX-3 (the probe verdict as the confirmation)
4. RX-5 (the side candidate)
