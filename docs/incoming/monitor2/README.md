# monitor2 - the monitor mode / spectral feature, materialised and verified

**Lane:** `monitor2` (shared task **task-246**), **HOST-ONLY**, 2026-11-26.
**Status:** the monitor v1 prototype the earlier `monitor-mode` lane (task-241, commit
1a2d068) specified in prose is now **real code, host-tested, cross-built and committed**,
plus an independent audit of that lane's claims and one more step of the spectral (PSD)
decode. **No bench contact, no fire, no UART, no vendor bytes committed.**

## Why this lane exists (the discovery)

Commit `1a2d068` (`monitor-mode`, task-241) committed **only six markdown files**. The
`src/`, `patches/`, `tools/` and `evidence/` directories it documents are *empty*, and the
BUILD-NOTES.md it references is absent. Its REPORT/CFG80211-MONITOR/FIRE-RECIPE describe a
"implemented here, host-tested, compile-clean" patch that never existed in the repository.
This lane rebuilt that deliverable from scratch (and re-derived the cfg80211 contract from
the pinned 6.18.60 tree, which caught two errors in the earlier design), then continued the
one decode it left open (spectral S-1: the PSD register-bank CA).

## One-line result

Monitor mode is **one patch and one firmware gate away**: the IMAGE #3 driver's cfg80211
*interface* half was already complete, the missing `.set_monitor_channel` op and the
radiotap RX mirror are implemented here (v1, fail-closed at `monitor=0`, never writes
firmware), and the only remaining blocker for actual capture is the **F1 RX gate** (the
host-bound `g1 id13` copy) - the mirror hooks sit exactly at the two sinks that the F1 will
make live. Spectral is a host-driven PSD register block; this lane decoded the **pointer
chain to its bank object** (S-1 is now half-closed: object layout + mapping helper + defaults;
the .rodata relocation that names the exact 10-word bank table is the last check).

## Deliverable index

| file | content |
| --- | --- |
| [REPORT.md](REPORT.md) | verdict, the audit of task-241, the materialised prototype, the spectral S-1 progress, limits |
| [VERIFY.md](VERIFY.md) | independent verification table of the task-241 claims + corrections found |
| [CFG80211-MONITOR.md](CFG80211-MONITOR.md) | the corrected design + exact patch delta list (9 files) |
| [SPECTRAL-S1.md](SPECTRAL-S1.md) | the PSD pointer chain (state+0x150 -> 64-B object -> +0x10 -> bank), mapping helper, defaults, remaining check |
| [BUILD-NOTES.md](BUILD-NOTES.md) | the prototype build: base tree, patch, host tests, module facts |
| [FIRE-RECIPE.md](FIRE-RECIPE.md) | the corrected attended ladder (R1 now needs the cfg80211 monitors-only ordering; R2 after the F1; S-rungs) |
| `src/` | portable logic + kernel integration snapshot (4 files) |
| `patches/0001-monitor-mirror.patch` | the 543-line, 9-file patch against the IMAGE #3 merged tree |
| `tools/` | host selftest + build + test runners |
| `evidence/` | base-tree manifest, host tests, patched/control builds, patch round-trip |

**Grades:** **[V]** verified here from a named local artifact (disassembly, source file,
build log); **[V*]** committed earlier lane and re-checked here; **[M]** bench-measured
earlier; **[I]** inference; **[U]** unknown.

**Rules followed:** host-only; commits inside `opensource/` only; no bench/UART/flash; no
vendor object/firmware/calibration bytes in tracked text (addresses, symbols and decoded
field maps only); no secrets; fail-closed defaults in every code path.
