# Wi-Fi transport port — SUMMARY

**Lane:** wifi-transport-port (host-only). **2026-11-22.** Full detail: REPORT.md,
bench procedure: BENCH-PLAN.md, code: build/, sizes: census/family-sizes.csv.

## One-line verdict

The hi5622 PCIe/bal transport (pcie_ 101 + bal_ 26 + hcc_ 87 functions, 38,184 B of vendor
function code; the 391-fn pcie/bal/shuangta/hal group = 67,432 B) is now **rebuilt as original
C on the 6.18 tree**: a clean-room skeleton module that maps the full register/window contract
(BAR0 six regions, endpoint iATU viewports, ETE channels, mailboxes/doorbells, boot control
words) onto the luofu PCIe host driver, implements the firmware-upload path, HCC/ETE node alloc
+ ring init, the H2D doorbell sequence and the D2H bitmap dispatch, and **compiles clean
(-Werror) into hi5622_pciet.ko** (93,468 B, vermagic 6.18.60 SMP mod_unload ARMv7 p2v8).

## What exists now

- build/hi5622_pciet.h — the decoded contract as constants/structs with evidence markers.
- build/pciet_main.c — pci_driver probe (no device writes), endpoint-iATU programming,
  firmware upload (chunks of 0x80000 to BAR0+0x6f8000), 0x5a5a release + 0xcece gate,
  pcie_msg_send_irq / pcie_msg_handle reproductions, boot handlers for ready ids 1/2,
  dev_status_check, polled D2H bitmap scan, single-step sysfs actions, never-write asserts.
- build/pciet_ete.c — SR0/DR0 ring alloc/init, 8-byte node fill ((len<<16)|0x6d2b),
  SR send, DR consume with devva resolution via the DMA API (NOT the vendor's VA identity —
  a VMSPLIT trap documented and avoided).
- build/luofu-host-618-delta.patch — the single host-side delta: dma-ranges on both pcie
  nodes so the DWC framework programs the RC inbound window (device 0x8xxxxxxx = phys
  0x80000000, identity); stage-B2 prerequisite only.
- BENCH-PLAN.md — attended plan: insmod (poll off) -> prepare -> upload -> verify ->
  release (0x5a5a) -> announce (0xcece) -> poll; gates: HCC ready id 1 (bitmap 0x2) then
  id 2 (bitmap 0x4 = the out[1]=0x4 checkpoint); UART read-only, recovery armed, teardown.
- census/family-sizes.csv — per-family funcs/bytes/stubs from the vendor .ko symbol sizes.

## What the bench will (and will not) prove

Will: the six-region map, contiguous upload, CPU release, D2H gate, and the mailbox
bitmap/drain/dispatch — i.e. the transport contract reproduced by original code on 6.18.
Will not (yet): ETE data movement (stage B2), message semantics (hdpp/hmac slices), any data
path.  INTx stays unwired in the luofu host driver (status in the never-read misc window), so
the bench's D2H "ISR" is the polled scan; a request_irq path is compiled in for later.

## Next slices (sizes from the vendor .ko)

hdpp_ 238 fns / 52,428 B -> alg_ 222 fns / 97,700 B (densest) -> hal_ remainder 91 fns /
4,832 B -> shuangta_ 173 fns / 36,476 B -> hmac_ ko-only 1,503 fns / 511,364 B -> wal_
ko-only 684 fns / 163,476 B.  The transport group is ~67 KB of the 1,014,560 B ko-only delta;
the hmac/wal long tail is the dominant remaining work.

## Files

REPORT.md (full), BENCH-PLAN.md (attended procedure), build/ (Kbuild, hi5622_pciet.h,
pciet_main.c, pciet_ete.c, luofu-host-618-delta.patch, BUILD-NOTES.md),
census/family-sizes.csv.  Commit: see git log (submodule opensource).
