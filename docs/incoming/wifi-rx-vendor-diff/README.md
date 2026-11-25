# wifi-rx-vendor-diff (task-162) - the RX VENDOR-vs-OURS state diff

**Host-only lane.**  Reads the committed vendor register dump/disassembly and
the committed clean-room driver sources; builds and host-tests a module; never
touches the bench (the Lead fires).

**Result in one line:** the vendor's whole RX enable (free-ring/completion-ring
program + fill + int mask) is already replicated byte-for-byte by our driver -
there is no missing RX-enable register/write.  The diff instead surfaces five
**device-owned vendor-nonzero live-bank words** nobody has ever read on our box,
the **status-service write** the vendor ISR does and we never did, and makes the
suspected-faulty drain **observe-only by default** so the next fire can prove RX
capture with zero device writes.

## Files

| path | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the definitive diff report: vendor static RX enable, the config-block / live-bank word tables, status-bit decode, writes-diff, crash analysis, host verification |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the attended ladder R0-R4 (read-only first), exact sysfs commands, PASS/FAIL, stop rules, crash sampling |
| [BUILD-NOTES.md](BUILD-NOTES.md) | chain, reproduce, artifact identity, limitations |
| [patches/0001-rx-vendor-diff.patch](patches/0001-rx-vendor-diff.patch) | 6-file patch on the FINAL association + corrected carrier + watermark tree |
| [src/](src/) | portable host-testable logic (hi5622_rx_diff.{c,h}) + host test source |
| [tools/](tools/) | run-host-tests.sh, build-rx-diff-module.sh |
| [evidence/](evidence/) | host-test log, build log, identity, patch verify log |

## New driver surface

- **params:** rx_drain_write (default 0 = observe-only), rx_status_service
  (default 0 = read-only), plus the inherited rx_enable / rx_clear_status /
  rx_drain_max / rx_refill_wm.
- **cat dp_rx_parity** (read-only): full word-by-word vendor-vs-ours diff,
  status decode, DEVICE_ZERO verdict.
- **echo 1 > dp_rx_drain**: with rx_drain_write=0 it is observe-only - it
  reads completion-ring memory and returned buffers, validates them, logs
  frames, and publishes **nothing**.  With rx_drain_write=1 it is the exact old
  rx-static/watermark write path (one explicit opt-in).
- **echo 1 > dp_rx_status_clear**: W1C of cfg +0x44 **bit 2 only**, behind
  rx_status_service=1 + rx_enable + arm + write gate.
- **echo 1 > dp_rx_arm**: now also logs the status decode and the five
  device-owned live-bank words against the vendor stock values.

## Artifact (host-built, NOT fired)

361 872 B stripped sha256 81ab72c1..., 59 params, 0 warnings, .text identical
before/after strip.  Full identity + reproduction in [BUILD-NOTES.md](BUILD-NOTES.md).

## Ownership / policy

Committed text carries derived offsets/values and original C only.  No vendor
binary, firmware blob, calibration data or per-unit identity.  The vendor
disassembly used is already committed in docs/incoming/wifi-shim-port/raw (local
non-committed artifacts only referenced).  Bench contact/firing is the Lead's.

