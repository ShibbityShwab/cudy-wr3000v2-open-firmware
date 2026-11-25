# wifi-rx-crx-gating - README

**Lane:** wifi-rx-crx-gating (shared task **task-164**). **Host-only**: no bench
contact, no device access, no firmware/module bytes committed. The Lead fires.

**Question:** the router's MAC RX captures nothing from the air; the client's auth
frames reach the AP (proven) but the host sees no D2H management-RX event (HCC group
1 / id 13). What in the firmware/event path gates the CRX delivery, and what is the
host's missing step?

**Answer (short):** the firmware's CRX delivery path is complete and autonomous; the
host is missing no message/mask/ring/table entry. The missing step is the *attribution*
step, and this lane implements it fail-closed and read-only. Full analysis in
[REPORT.md](REPORT.md); evidence tables in [TABLES.md](TABLES.md); bench ladder in
[FIRE-RECIPE.md](FIRE-RECIPE.md).

| deliverable | content |
| --- | --- |
| [REPORT.md](REPORT.md) | full reversal: the firmware builder, the host dispatch table, the gate matrix, the evidence-backed negative for 'a missing host enable', the crash write-surface analysis, the probe design, verification |
| [TABLES.md](TABLES.md) | T1 firmware builder map, T2 host hmac dispatch table, T3 gate matrix, T4 probe plan, T5 firmware MAC-block access census, T6 RX-write fault surface |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the attended ladder (R0 control .. R3 data) with PASS/FAIL and stop rules |
| [BUILD-NOTES.md](BUILD-NOTES.md) | host test + kernel build identity, patch round-trip |
| [patches/0001-crx-delivery-gate.patch](patches/0001-crx-delivery-gate.patch) | the 4-file driver patch (strict CRX decoder + read-only `crx_gate` probe; no device writes) |
| [src/hi5622_crx_gate.h](src/hi5622_crx_gate.h), [src/hi5622_crx_gate.c](src/hi5622_crx_gate.c) | post-patch sources (portable + kernel halves) |
| [tools/crx_gate_selftest.c](tools/crx_gate_selftest.c) | 66-check strict C99 host test |
| [tools/build.sh](tools/build.sh) | host-side verification driver (selftest, patch apply, kernel build) |
| [evidence/](evidence/) | selftest log, patch apply/compare log, kernel build log, dispatch-table parse, firmware xref extracts |

**Licensing / clean-room:** all code in this lane is original GPL-2.0 code written
from derived facts (addresses, constants, wire layouts). No vendor source text, object
code, firmware byte, calibration value or per-unit identity is reproduced or committed.
Vendor artifacts are read in place at their local paths and are never copied here.

**Status:** static reversal complete; patch host-tested (66/66) and kernel-built
(0 warnings); **not fired on the bench** (the Lead's attended session).
