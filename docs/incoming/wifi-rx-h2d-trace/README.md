# wifi-rx-h2d-trace - the stock-image H2D trace plan + the firmware RX-init (F1) narrowing

**Lane:** `rx-h2d-trace` (shared task **task-176**, FEATURE-BACKLOG **RX-1/RX-2**). **Date:** 2026-11-27.
**Scope:** host-only static decode + a bench procedure the Lead can run; **no device contact, no
fire, no vendor binary/firmware byte committed** (sizes/hashes/addresses only, protocol bytes as
text from committed analysis).

## One-line result

The vendor's host->device messages are **plain HCC frames in host DRAM, posted as 8-byte SR nodes
into a PCIe-visible ring**: the stock pair-A boot can therefore be H2D-traced **read-only** from
userspace, with no vendor hooks, by (a) sampling the live SR ring through the endpoint BAR0 window
and `/dev/mem` (the mechanism was already proven read-only in phase 25) and/or (b) kprobing
`hcc_msg_tx` (tracefs + dynamic kprobes are proven working on the stock 5.10.201, and the one-shot
S08 init-aid that arms them inside the module-load window was proven in phase 21). The vendor debug
modules were decoded end-to-end for this purpose: **they contain no H2D message log** - their useful
facility is `save_hostmem` (a host-physical DDR dump), and their queue-dump stub is a no-op in this
build. The static RX-init (F1) narrowing lands on a **three-space candidate inventory** (config-key
`0x0004`, the never-exercised `hcc_msg_tx_to_core` dimension, and any runtime message the vendor
sends that we do not); the trace is the discriminator and the offline diff is specified here.

## Deliverables

| file | contents |
| --- | --- |
| [REPORT.md](REPORT.md) | the decode: vendor H2D path, decoded debug modules, the stock trace plan, the offline diff plan, the F1 narrowing, honest limits |
| [RUNBOOK.md](RUNBOOK.md) | the exact attended procedure the Lead can run: T0-T7 stock trace, K1-K3 kprobe variant, PASS/FAIL, STOP rules, cleanup |
| [TABLES-debug-interfaces.md](TABLES-debug-interfaces.md) | wifi_debug.ko / plat_debug.ko / plat.ko decode: identity, load, every sysfs/proc/debugfs surface, exact grammar, safety class, verdict per facility |
| [H2D-FORMATS.md](H2D-FORMATS.md) | the wire formats to validate a capture: SR node, ETE registers, 12-byte HCC header, g0-id1 config-event geometry, direct-id and to_core id spaces, captured vendor frame exemplars, decoder rules |
| [tools/omo_h2dsampler.c](tools/omo_h2dsampler.c) + [tools/build-sampler.sh](tools/build-sampler.sh) | the device-side read-only SR-ring sampler (static musl ARM build for the stock image) |
| [tools/decode_h2d.py](tools/decode_h2d.py) + [tools/run-host-tests.sh](tools/run-host-tests.sh) | host-side normaliser/validator + vendor-vs-ours diff; tested against fixtures |
| [evidence/](evidence/) | decode log (exact commands/hashes) and host-test output |

## The trace in one paragraph (details in RUNBOOK.md)

Stock pair A boots the vendor kernel 5.10.201 + vendor stack, which sends its own H2D init sequence;
it can be traced (1) **read-only from softwar**e: an S08 one-shot init aid starts before
`hi5622v100_plat` loads, waits for the ETE ring to come alive, then polls SR ch0 (+ch1/ch2)
producer (+0x18) at ~50 Hz, reads every new 8-byte node `{host-phys buffer, (len<<16)|0x6d2b}`
from host physical memory and every new buffer's 12-byte HCC header+payload from `/dev/mem`,
writing one line per message; (2) **with kprobes** the same aid can arm
`p:h2d_tx hcc_msg_tx` (nested-deref fetch of `+0x118(msg)`) plus `pcie_msg_send` for ordering.
Both instruments were previously proven on this exact vendor stack class (phase 25 for the ring,
phase 3/20/21 for kprobes/the init aid). The log is decoded by `tools/decode_h2d.py`, validated
against the documented invariants (`+4 == descriptor len`, `0x5a5a` tag, group/id bounds) and
diffed against our driver's committed H2D set. **PASS = a byte-valid list of the vendor's boot H2D
sequence exists** (RX-1), and the one message in it that our set lacks is the RX-2/F1 candidate.

## What is proven vs. planned

* **[V]** the vendor's H2D transmit path and its host-DRAM geometry (committed phase 20/24/25 docs);
  the live SR-ring read-only read (phase 25); tracefs kprobes + the S08 arming window (phase 3/20/21);
  the debug-module load and its sysfs surfaces (phase 6/7); both debug modules decoded here.
* **[I]** the stock 2.5.24 boot preserves the same init ordering and `/dev/mem` RAM access; the
  runbook makes this a **read-only preflight gate** (T2) so a mismatch stops, not guesses.
* **[U]** the exact vendor H2D boot list itself (that is precisely what the trace produces), and
  therefore which message closes F1 - the whole point of the fire.
