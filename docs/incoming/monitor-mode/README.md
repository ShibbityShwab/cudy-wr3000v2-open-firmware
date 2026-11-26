# monitor-mode - the cfg80211 monitor interface and the spectral (PSD) feature

**Lane:** `monitor-mode` (shared task **task-241**), **HOST-ONLY**, 2026-11-29.
**Status:** decode + design + a host-tested, fail-closed prototype patch. **No bench contact,
no fire, no vendor binary/firmware bytes committed.** The Lead fires; this lane never does.

**One-line result:** monitor mode is *two layers short* on the image #3 driver - the cfg80211
interface exists but has no channel op and no RX delivery, and the RX delivery itself is gated
by the F1 (`g1 id13`) fix; the chip's vendor driver has **no monitor implementation at all**
to port. The achievable step is a **local-mirror monitor** (mirror the frames the AP VAP
receives, radiotap-wrapped) which is implemented here and can be fired **without any firmware
write**. Spectral is a **host-driven PSD register block** (three 32-bit registers, bitfields
fully decoded here), not a firmware message feature; the missing piece is the register-bank CA
base + the report-memory layout, which is a concrete next decode.

**Deliverable index**

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | the analysis: current driver state, kernel contract, vendor absence, GPL-2.0 family reference, the F1 dependency, prototype, spectral decode, limits |
| [CFG80211-MONITOR.md](CFG80211-MONITOR.md) | the monitor-mode design + exact delta list + the radiotap layout + the fail-closed matrix |
| [SPECTRAL.md](SPECTRAL.md) | the spectral/PSD decode, the register bitfield map, the control flow, and the next decode |
| [TABLES.md](TABLES.md) | evidence tables (kernel lines, driver lines, vendor symbols, family reference, PSD map) |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the attended fire ladder: R1 (ready now), R2 (after the F1), R3/spectral (blocked - do not fire) |
| [BUILD-NOTES.md](BUILD-NOTES.md) | the prototype build: base tree, patch, host tests, module facts |
| `patches/0001-monitor-mirror.patch` | the prototype patch against the IMAGE #3 merged module tree |
| `src/hi5622_monitor_logic.{c,h}`, `src/hi5622_monitor.c` | the portable logic + kernel integration (snapshot) |
| `tools/` | host selftest + build/evidence scripts |
| `evidence/` | base manifest, host-test and build logs, patch round-trip |

**Evidence grades used everywhere:** **[V]** verified here from a named local artifact
(instruction/constant/string); **[V\*]** decoded by an earlier committed lane and cited;
**[M]** bench-measured earlier; **[I]** inference; **[U]** unknown / needs a device read.

**Rules this lane followed:** host-only (no bench, no network to the router, no UART); commits
inside `opensource/` only; no vendor object/firmware bytes in tracked text (addresses, symbol
names and decoded field maps only); no secrets; fail-closed defaults in every code path.
