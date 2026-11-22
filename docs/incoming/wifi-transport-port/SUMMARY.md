# Wi-Fi transport port — SUMMARY

**Lane:** wifi-transport-port (host-only until 2026-11-22, then attended bench on the
project unit). **Written:** 2026-11-22; **updated after the stage A/B1/B2 bench runs.**
Full detail: REPORT.md, bench procedure: BENCH-PLAN.md (now executed), code: build/,
sizes: census/family-sizes.csv.

## One-line verdict

The hi5622 PCIe/bal transport (pcie_ 101 + bal_ 26 + hcc_ 87 functions, 38,184 B of vendor
function code; the 391-fn pcie/bal/shuangta/hal group = 67,432 B) is **rebuilt as original
GPL-2.0 C on the 6.18 tree** and the transport contract is **device-verified end-to-end**:
the module boots the Wi-Fi chip (firmware upload verify diffs=0, CPU release, D2H DEVICE
READY) and passes the ETE-ring + H2D doorbell smoke on the open stack.  It is a bench
instrument / transport layer, **not** a Wi-Fi driver (no cfg80211/mac80211, no data path).

## Bench status (2026-11-22)

- **A - enumerate:** both endpoints probed on the working 6.18 link
  (0000:00:00.0 BAR0 0x40000000/BAR2 0x41800000, 0001:00:00.0 BAR0 0x58000000/59800000);
  device memory reads work, ETE rings allocated.
- **B1 - boot the chip:** prepare (6/6 endpoint iATU viewports read back), upload
  928,920 B firmware, **verify target window BAR0+0x6f8000 diffs=0**, release 0x5a5a ->
  CA 0x40000108, announce 0xcece -> CA 0x4000010c, poll -> D2H bitmap 0x4 and
  **DEVICE READY (D2H message id 2)**; status words match the stock vendor run.
- **B2 - ETE/H2D:** the 6.18 DWC framework programs the RC inbound window from the board
  DTS `dma-ranges` (vp0 0x80000000..0x8fffffff verified), ETE SR0 ch 0x400 base
  0x8360c000 / DR0 ch 0x590 base 0x83634000 programmed (depth-1 0x1f), H2D smoke
  64 B on SR0 with doorbell mask 8 - no abort, device status live.
- Device timing lesson: DEVICE READY posts about 3 s after the release; the poll thread
  latches its first scan 2 s late and polls every 50 ms (commit 9700250).
- Module: built from the committed `build/` sources; bench-built `hi5622_pciet.ko`
  sha256 `416d1db0...` (short prefix; the .ko itself is not committed - text only).

## What exists now

- build/hi5622_pciet.h — the decoded contract as constants/structs with evidence markers.
- build/pciet_main.c — pci_driver probe (no device writes at probe), endpoint-iATU
  programming (compact 7-word viewports, ABSOLUTE PCI base/limit), firmware upload,
  target-window verify, 0x5a5a release + 0xcece announce, pcie_msg_send_irq /
  pcie_msg_handle reproductions, boot handlers for ready ids 1/2, dev_status_check,
  polled D2H bitmap scan, single-step sysfs actions, never-write asserts.
- build/pciet_ete.c — SR0/DR0 ring alloc/init, 8-byte node fill ((len<<16)|0x6d2b),
  SR send, DR consume with devva resolution via the DMA API (NOT the vendor's VA identity —
  a VMSPLIT trap documented and avoided).
- build/luofu-host-618-delta.patch — the original mapping note for the RC inbound window
  (dma-ranges).  **Superseded:** the board DTS now carries the identity `dma-ranges`
  (submodule commit 5a6a500) and it is bench-validated (image W26a); keep the patch as
  history, do not apply.
- BENCH-PLAN.md — the attended plan that was executed (A/B1/B2); kept as the procedure record.
- census/family-sizes.csv — per-family funcs/bytes/stubs from the vendor .ko symbol sizes.

## What the bench proved (and did not)

Proved: the six-region BAR0 map, the compact 7-word absolutely-addressed endpoint iATU,
contiguous upload, target-window verify, CPU release, D2H announce/bitmap handling, the
RC inbound window from dma-ranges, ETE ring programming and the H2D doorbell — i.e. the
transport contract reproduced by original code on 6.18.
Not proved: ETE data movement (no payload round trip), message semantics (hdpp/hmac
slices), the ETE enable sequence (+0x00/+0x08/+0x48 still [U]), INTx delivery (still no
host MSI/INTx demux; polled D2H is the standing design), and any Wi-Fi data path.

## Next slices (sizes from the vendor .ko)

hdpp_ 238 fns / 52,428 B -> alg_ 222 fns / 97,700 B (densest) -> hal_ remainder 91 fns /
4,832 B -> shuangta_ 173 fns / 36,476 B -> hmac_ ko-only 1,503 fns / 511,364 B -> wal_
ko-only 684 fns / 163,476 B.  The transport group is ~67 KB of the 1,014,560 B ko-only delta;
the hmac/wal long tail is the dominant remaining work.

## Files

REPORT.md (full, updated with the bench results), BENCH-PLAN.md (attended procedure),
build/ (Kbuild, hi5622_pciet.h, pciet_main.c, pciet_ete.c, luofu-host-618-delta.patch,
BUILD-NOTES.md), census/family-sizes.csv.  Commits: see git log (submodule opensource);
the bench-fix and poll-hardening commits are 9700250 and the transport-lane follow-ups.
