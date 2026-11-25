# wifi-ba-agg - the vendor BA machinery and the minimal A-MPDU TX/RX (task-178)

**Lane:** rx-ba-agg (shared task-178). HOST-ONLY; this lane never contacted the
router. Deliverable path: `opensource/docs/incoming/wifi-ba-agg/` (the task's
write-scope alias says `wifi-rx-ba-agg`; no such directory exists and no other
lane owns this path - the Lead's instruction names this one).

This lane decodes the vendor 2.5.24 host module's block-ack machinery
(`hmac_ba_*`, the ADDBA/DELBA action-frame builders, the host<->firmware BA
message contract and the firmware dispatch rows) and turns it into:

1. the decoded **A-MPDU session-setup message sequence** (both directions:
   AP-as-TX-originator and AP-as-RX-recipient);
2. a **fail-closed patch skeleton** (`ba_enable=0` default, no frame, no
   message when off) with byte-exact builders/parsers for the six message
   classes and the three over-the-air action frames;
3. **host tests** (no device, no vendor artifact) that pin every byte of the
   decoded frames/payloads and the state machine;
4. a **fire recipe** for the rungs that can fire once a peer can associate -
   plus the one rung that is safe to fire *today* (module load with
   `ba_enable=1` and no peer: no BA frame may appear, page golden).

The bench gate is RX: without the RX capture no client associates, so no BA
session can be negotiated end-to-end. This lane therefore delivers the
static+host half now and the fire recipe gated behind RX; the one "BA smoke"
rung is written to be evaluable on the current healthy chip with zero peer.

## What is here

| path | content |
| --- | --- |
| `REPORT.md` | the decode: architecture, session structs, window/scoreboard, the six message classes, the two-direction setup sequence, the minimal implementation, limits |
| `TABLES.md` | T1-T10 evidence tables: symbol map, structs, message field maps, firmware dispatch, frame byte maps, fail-closed contract |
| `FIRE-RECIPE.md` | rungs B0-B7 with exact PASS/FAIL gates (the Lead fires; B1/B2 evaluable now, B3+ blocked on RX) |
| `BUILD-NOTES.md` | the pinned build chain, the 58-param artifact contract, host/static test commands |
| `patches/0001-ba-skeleton.patch` | fail-closed skeleton against the 5 GHz ready base tree: new `hi5622_ba.[ch]` + Kbuild line; inert unless `ba_enable=1` |
| `src/hi5622_ba.{c,h}` | portable, host-testable BA core: frame builders/parsers, payload builders, per-TID session state machine, callback plumbing |
| `src/ba_selftest.c` | host selftest (compiles `src/hi5622_ba.c` standalone; no device) |
| `tools/run-host-tests.sh` | runs the selftest |
| `tools/ba_decode_check.py` | regenerable static evidence: reads the LOCAL vendor .ko/firmware in place and asserts every anchor cited in TABLES (addresses, payload offsets, frame bytes, fw dispatch rows) |
| `evidence/` | host-test and static-check transcripts |

## Run (host-only)

    ./tools/run-host-tests.sh                       # no deps, gcc only
    .provision-venv/bin/python tools/ba_decode_check.py         --ko /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko         --fw /home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin

## The parameter (fail-closed)

    ba_enable=0   default: the BA core is compiled in but every entry point is a
                  no-op returning -EOPNOTSUPP; no frame, no H2D message, no
                  state change (byte-identical behaviour to the current module)
    ba_enable=1   BA sessions may be negotiated (only after the RX gate: the
                  skeleton is not wired into the data path yet, so this only
                  arms the handlers)
    ba_enable>1   refused (-EINVAL at first use; never guessed)

## Status

Decode, report, tables, skeleton patch, host tests, static evidence and fire
recipe are DONE and committed.

* host selftest: **183 checks, 0 failures** (`evidence/host-tests.txt`);
* static evidence: **147 checks, 0 failed** against the pinned local vendor
  module + firmware (`evidence/static-checks.txt`);
* skeleton module: builds clean against linux-6.18.60, **58 params**
  (`ba_enable=0` default), debug-stripped 359,992 B sha256
  `78460841...cc33de02`, and is **provably unwired** (no object outside
  `hi5622_ba.o` references a `hi5622_ba_*` symbol) - see BUILD-NOTES.

The patch is a **skeleton**: it compiles into the module and is inert by
default, but it is not yet wired into the TX/RX data path - the hook points are
listed in REPORT.md section 9 and are the next rung after the RX gate. The fire
is the Lead's; this lane never contacts the router.
