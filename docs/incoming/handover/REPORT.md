# handover - the top-level docs refresh (task-203, lane docs-refresh)

**Status: delivered 2026-11-28.** HOST-ONLY lane: it read the ledger, `STATUS.md`, `BENCH-FACTS.md`
and the committed lane directories; it never contacted the router, never flashed and never
fired. The Lead owns every bench action.

## What was refreshed (superproject root)

| file | change |
| --- | --- |
| `STATUS.md` | header to **2026-11-28 04:30 +07** (the newest ledger entry at the time); a new **section 3n** with the hardware-proven Wi-Fi-6 state (2.4 GHz AP on air, 802.11ax live, the 5 GHz 13 links + RF programmed + beacon state, the band-1 start gap and the corrected probe address, the RX H2D trace, the fail-closed decode set, images #1-#3) and the bench state; **section 4 Next action** rewritten to the real one (band1-start-send, fw-f1-capture, the image #3 rootfs-only flash delivery plan, the host-only lanes in flight, what is not achievable today); a section-1 pointer noting the W27b record is the 2026-11-23 baseline (history) |
| `README.md` | the stale "bench fires are stopped / v17 ceiling" track text replaced by the current clean-room Wi-Fi-6 line; a **Current capability matrix (2026-11-28)** table (transport, 2.4 GHz AP, 802.11ax, 5 GHz RF, 5 GHz beacon pending, RX/live-client open, BA/RC/AX decoded fail-closed, the image chain, the upstream target); the handover added to "Start here" |
| `BENCH-FACTS.md` | header date + three new fact sections: **7** the stock ETE/H2D access rules (pread of /dev/mem is EFAULT below PHYS_OFFSET - mmap only; EP1 stock window `0x583f2000` vs EP0 `0x403f2000` alias; the v2 sampler identity + the mandatory `-p` gate + capture args); **8** the pair-switch procedure (env `bootflag` + selector cell together, the interfaces, both directions, the post-recovery two-stage rule); **9** the TFTP shortcut (managed server, UFW on the box-link interface, busybox `tftp` get/put, verification) |
| `SESSION-HANDOVER.md` (new) | the short handover: state, ranked open items, the exact next commands (session start, the band-1 fire, the image #3 rootfs-only flash, the RX trace retry, close-out rules), the lane inventory table |

The published-half mirror/snapshot of the handover is
`opensource/docs/incoming/handover/SESSION-HANDOVER.md` (paths are superproject-root-relative;
the canonical file is the superproject root one - refresh the snapshot when that changes).

## Sources used (all committed evidence, no device)

- `mem-entries.md` newest entries 2026-11-28 03:00 / 03:30 / 04:00 / 04:30 (the RX H2D trace,
  the H2D family analysis, the second-swarm launch, the fw-band1-start verdict + the corrected
  probe address).
- Lane directories under `opensource/docs/incoming/`: `wifi-image-3/` (HASHES, VERIFY,
  FLASH-FEASIBILITY), `wifi-band1-start/` (README/REPORT/FIRE-RECIPE), `wifi-rx-h2d-window/`
  (README/REPORT/RUNBOOK-DELTA), `wifi-fw-beacon-tx/`, `wifi-fw-tbtt-arm/`,
  `wifi-ax-advanced/`, `wifi-ax-he-caps/`, `wifi-ba-agg/`, `wifi-rate-ctl/`,
  `wifi-final-image-2/`, `wifi-image-update/`.
- `RUNBOOK.md` (pair rules, recovery, install), `opensource/docs/SELECTOR-0x21.md`,
  `docs/incoming/wifi-rx-h2d-trace/RUNBOOK.md`.

## Discipline followed

- Date discipline: `STATUS.md` **Updated:** is not older than the newest dated ledger entry;
  `tools/check-repo.sh` PASS (run at delivery).
- No vendor binaries/firmware bytes in tracked text: only addresses, hashes, decoded field
  values and command lines. No secrets, no live unit MAC/serial values.
- The files say what is hardware-proven vs host-verified vs in flight; the lead-owned fires
  are named as such and the image #3 delivery plan is stated (AGENTS rule).

## How to refresh this snapshot

Edit `STATUS.md` sections 3n/4 to the newest ledger entry, update `README.md`'s capability
matrix and `BENCH-FACTS.md` only if a physical invariant changed, then rewrite
`SESSION-HANDOVER.md` (do not append) and copy it over the snapshot here with the paths
flattened. Run `tools/check-repo.sh` before committing.
