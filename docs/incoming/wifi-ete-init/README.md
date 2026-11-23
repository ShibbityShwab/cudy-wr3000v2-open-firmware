# wifi-ete-init lane (shared task task-81)

Where the vendor's ETE channel initialisation actually lives, and what the B3 P0 ringinit was
really missing. **Host-only lane:** the vendor modules are read in place as local evidence; no
device was contacted, no image was built, no vendor byte is committed.

Deliverables:

* [REPORT.md](REPORT.md) — the complete vendor `pcie_ete_*` init sequence (exact registers,
  values, order, conditions, 2.5.24 vs 2.4.15), proof that the CA `0x4003AC00+4i` /
  `0x4003AC18+4i` send words are device-authored and never host-written, the golden-value
  derivation (`0x04050405`/`0x040E040E` = live boot traffic, packed ring indices), the B3 patch
  rationale, bench observables, stop rules and the firmware-side fallback conclusion.
* [ETE-INIT.patch](ETE-INIT.patch) — compile-ready diff against
  `../wifi-transport-port/b3/`: vendor-exact ETE interrupt-word RMW (`0x40039508 &=
  0xFFE0F8F8`), bfi-faithful depth/ctrl read-modify-writes, SR send-word "already sending"
  check, and the **read-only** `b3_ete_send` / P0 baseline watch. Built clean with the B3
  tooling (zero warnings; sha256 `d577bed5…` for the verified build dir).
* [BENCH-PROCEDURE.md](BENCH-PROCEDURE.md) — the attended-run delta on B3-PLAN: three
  `b3_ete_send` snapshots (pre-release / post-announce / post-watch), expected observables
  E1-E5 and the stop rules.
* [tools/ete_scan.py](tools/ete_scan.py) — reproducible verifier (pyelftools + capstone):
  `selftest` asserts every quoted fact (41/41 PASS), `inventory`/`sendregs`/`compare` print the
  evidence.

Reproduce:

    PY=<repo>/.provision-venv/bin/python
    cd opensource/docs/incoming/wifi-ete-init
    $PY tools/ete_scan.py selftest
    $PY tools/ete_scan.py inventory
    $PY tools/ete_scan.py sendregs
    $PY tools/ete_scan.py compare

Bottom line: the vendor host driver's only global ETE init write is the interrupt-word mask; the
ring fields are per-channel base/depth/producer/ctrl/published writes; the send/flag words are
device state and no host init may seed them. The golden diff measures vendor boot traffic, so
if our send words stay 0 the blocker is the firmware-side D2H pump, not a missing host write.
