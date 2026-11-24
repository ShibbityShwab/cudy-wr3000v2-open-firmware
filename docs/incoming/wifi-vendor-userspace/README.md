# Vendor userspace Wi-Fi stack — interface scope after DEVICE READY

Host-only static RE deliverable (task-96, 2026-11-24). Read:

* **[REPORT.md](REPORT.md)** — the analysis: post-ready timeline, the five
  userspace→kernel surfaces, startup command trace and HCC (group/id) mapping, minimum
  viable interface for the clean-room driver, gap analysis, recommended architecture and
  milestones.
* **[TABLES.md](TABLES.md)** — extracted evidence tables (artifact pins/version deltas,
  init chain, interface inventory, `/sys/hisys/hipriv` and `iwpriv` command
  vocabularies, the startup→HCC mapping, D2H tables, component classification, gaps,
  regeneration ledger).
* **[tools/regen-evidence.sh](tools/regen-evidence.sh)** — regenerates every table's
  source data from a local extracted rootfs (default `/tmp/rootfs2524b`); prints
  only file names, hashes, symbol names and grep hits — no vendor bytes.

**Scope/limits.** Static analysis of the stock 2.5.24 rootfs (with a 2.4.15 delta check),
cross-referenced against the committed HCC/H2D/transport lanes. No device was contacted and
no vendor binary, firmware byte, calibration value or per-unit identity is included. The
document defines the interface a clean-room kernel driver must expose after DEVICE READY; it
does not claim that interface is reachable on the bench today (the firmware post-ready
IRQ/thread layer remains the gating blocker — see REPORT §0.5, §7 G10).
