# FIRE-RECIPE - what (not) to post from the stock-census decode

Lane: wifi-stock-h2d-census (task-232). Host-only decode; this lane fires nothing. Any rung below
is a bench action and inherits BENCH-FACTS.md / RUNBOOK.md gates.

## Preconditions for any rung (unchanged, non-negotiable)

1. A human at the bench; recovery door armed (tools/recovery-card.sh READY).
2. tools/bench-link.sh PASS and tools/check-repo.sh clean before and after.
3. STATUS.md names the delivery plan; tools/fire-preflight.sh PASS; tools/check-init-shadow.sh PASS.
4. UART read-only capture to an absolute path; never type to the tty; no rmmod of vendor modules.
5. One variable per rung; abort on the first unexpected UART line or a missed heartbeat.

## R0 - no fire needed (this lane's result)

The census decode is complete from the host-side capture: the g12 family is dissolved (timer
class + aliases), the new families are builder-attributed, and the 5 GHz beacon question is
answered negative on H2D (REPORT sec. 2-4). No bench read or write is required to reproduce it.
R0 rungs that remain useful and are read-only on the next attended session:
* R0a: re-run the v2 sampler (source uncommitted - commit it first) for 90 s on a normal stock
  boot and confirm the same validity split (expect valid/torn approx 0.3/0.7).
* R0b: on our kernel, sample the H2D ring while our 5 GHz VAP is up and confirm no
  g0 id22/g0 id36 frames are sent by our driver (expected: absent).

## R1 - post candidates from this capture (graded; require an attended fire)

All are **host->device control messages** with an existing firmware receive handler; none is a
beacon gate. Post only after R0, with the H2D sampler running, and compare against the stock
timing in TABLES T2.

C1 (best candidate): g0 id22 - hmac_chan_do_sync for 5 GHz ch 36.
  Frame (52 B, MACs none; decoded bytes as text):
  40 01 00 0a 34 00 16 00 00 00 5a 5a 24 01 07 00 00 00 00 00 00 00 00 00 00 07 00 00
  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 00 00 00
  Why: the stock sends it right after the 5 GHz channel context is set; firmware handler exists
  (g0 id 0x16 -> file 0x22d89). Expected observability: no UART error; H2D capture shows the
  message accepted (no D2H error class); our 5 GHz VAP keeps its channel/beacon schedule.
  Risk: medium - it rewrites the MAC channel context (ctx[+0x10..0x13]); if our channel differs
  from ch 36, sync the captured payload's channel byte to our channel instead of replaying 0x24.
  Acceptance: message dispatched once, no trap/OAM error, link survives; otherwise roll back by
  restoring the previous channel context (write nothing, the driver re-syncs on channel change).
  Status: optional completeness rung - NOT a beacon-path fix.

C2: g0 id36 - hmac_scan_config_bss_color with an inert bitmap.
  Frame (20 B): 00 01 00 07 14 00 24 00 00 00 5a 5a 09 00 01 02 00 00 00 04
  Why: firmware handler g0 id 0x24 -> file 0x755e1 reads payload+0[W]; the stock sends the
  per-entry bitmap at bring-up and our driver does not. Expected: no visible effect (BSS color
  is an HE optimization). Risk: low. Acceptance: no UART error and no change in scan/beacon
  cadence; revert on any anomaly.

C3: g0 id32 - hmac_config_h2d_send_app_ie with dlen=0.
  Frame (23 B): 00 01 00 04 17 00 20 00 00 00 5a 5a 00 00 00 00 04 00 00 00 00 00 00
  Why: stock-only family; firmware handler g0 id 0x20 -> file 0x45631; zero-length IE is the
  safest geometry. Risk: low. Acceptance: dispatched once, no error class.

## DO NOT fire (explicit)

* The 16-B HCC-timer class in any form (g0/g2/g12 id29, total 16, id 0x1d): payload is a host
  pointer and the firmware group-2 table has no id 0x1d handler (f1 lane + this census). The
  group-0 id 0x1d handler expects a 60-B EDCA payload, so a re-grouped replay would corrupt
  per-chip EDCA state.
* g2 id0 torn blob (12 records): no host builder, no firmware handler - a stale buffer, not a
  message.
* The D2H residue classes (g4 id0/heartbeat, g1 id*, g2 id9/id37): device->host content read
  through stale nodes; firing them as H2D is undefined.
* Any bytes from the g12 id29 over-read (the IEEE 1905.1 Ethernet prefix): heap content, not a
  message. Do not build a TX path from it.
* Do not add g0 id2 (alg) or g0 id12 (scan) replies: they are stock-only and already decoded;
  our driver does not run those offloads.

## Beacon (the thing this lane cannot fix from H2D)

There is no host message that arms the 5 GHz beacon: the stock posts the beacon template
(key 0x0127), the channel context (key 0x0065 / g0 id22) and then the firmware raises the beacon.
The committted wifi-5g-tx-scheduler rungs (0x40101220 bit0/bit1 write test R1, 0x40101208 suspend
latches) remain the only beacon path. This census closes the "look for an H2D beacon-enable key"
line of inquiry: negative [V].
