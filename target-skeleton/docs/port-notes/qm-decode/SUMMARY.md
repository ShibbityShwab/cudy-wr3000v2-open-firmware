# incoming/qm-decode - round 4c: MAC config decode + doorbell bisection (patch 041)

**Lane:** `qm-decode`, shared task `task-61`, 2026-11-20 (round 8).  Prior record: `032` (tiered
W7), `036` (W8 PFE), `038` (W9 MAC max-frame), `040` (W9 diag/write split + formula fix).
This package delivers **patch 041**: the decoded vendor MAC config sequence, the doorbell-bisection
gate, and TX-halt containment.  No device access in this lane.

Apply `shared.patch` from the `opensource/` root, then move this SUMMARY to
`target-skeleton/docs/port-notes/qm-decode/SUMMARY.md`.

## 1. Diag-safe run result (image 6cf5de37 = 001-040, W9 read-only, trace=1, oops=panic)

- Boot stable, seven gates armed, W9 diag logged on the GEMACs:
  `max-frame 0x100 = 0x334005ee (limit 1518), ctrl 0x0 = 0x0, port_mode 0x40 = 0xc0408; no write`.
- Traffic test: no frames, then the box **hung and the hardware watchdog reset it** (fell back to
  stock; **no Oops/panic captured** - a bus hang, not a fault).
- Trace: exactly one frame was handed down.  `gmac-trace ... pre: ctrl=0x0 maxframe=0x334005ee
  port_mode=0x10c0008`, the pie-trace descriptor + `TX doorbell=1`, `gmac-trace ... post: ctrl=0x0
  maxframe=0x334005ee port_mode=0x10c0008`, then silence until the reset.  So the transport
  (descriptor build, DMA map, doorbell write) completed; the wedge is the engine processing the
  descriptor afterwards (or a bus hang triggered by it).

## 2. Finding: reg 0x0 is not the MAC control/enable word

Decoded from the pinned hi_kmac.ko: the vendor link-up path calls `hi_mac_hw_config@0x258`
(through the MAC ops chain), which writes:

| write | value |
| --- | --- |
| 0x18 | 1 (reset/config assert) |
| 0x14 | speed/duplex code: bits 1..0 = 2 (1000), 1 (100), 0 (other); bit 2 = full duplex |
| 0x18 | 0 (release) |
| 0x1c | 1 |
| 0x40 | RMW: clear bits 8..15 and bit 24, then set bit 24 |

There is **no write to reg 0x0** in that path, and the bench reads 0x0 back as 0 even after our
measured link-up write - so 0x0 is not the readable control/enable.  The 0x40 RMW applied to the
found baseline 0xc0408 yields exactly 0x10c0008 (what our driver writes), so the port-mode word is
already vendor-consistent; the missing pieces were the 0x14/0x18/0x1c sequence.

## 3. What patch 041 adds (three attended diagnostics)

- **`luofu_pie.nodoorbell=1`** - TX descriptors are built and published to the ring but the
  channel doorbell (TX wptr write) is suppressed.  If the box survives, the descriptor build/DMA
  mapping is proven safe and the hang is in the engine's post-doorbell processing; if it still
  hangs, look before the doorbell.  Logs `pie-trace TX doorbell SUPPRESSED`.
- **`luofu_gmac.macconf=1`** - at link-up, instead of relying on the unattested 0x0 write,
  replays the vendor sequence above (speed/duplex code computed from phylink's speed/duplex) and
  logs every pre/post register value (0x14/0x18/0x1c/0x40/0x0) for direct comparison with the
  vendor's expected values.
- **`luofu_gmac.txhalt=1`** - stops the TX queue after the first xmit, so at most one
  descriptor can reach the engine per boot; contains the hang for diagnostics.

## 4. Recommended next run order (each from stock install, no recovery needed)

1. `luofu_pie.nodoorbell=1 luofu_gmac.trace=1` + the full seven-gate chain: survive =>
   descriptor build/DMA is safe and the engine's post-doorbell processing is the hazard; hang =>
   bisect before the doorbell.
2. `luofu_gmac.macconf=1 luofu_gmac.trace=1 luofu_gmac.txhalt=1`: does the missing vendor MAC
   config sequence change the TX behaviour?  Compare the macconf pre/post lines with the expected
   values (0x40 should become 0x10c0008 from 0xc0408).
3. `luofu_gmac.txhalt=1` with the normal doorbell: confirm one descriptor alone still wedges.
4. If the hang persists with the doorbell, capture it with `oops=panic panic=0` already in the
   cmdline (no oops appeared because this is a bus hang, not a fault; the kernel printk may stop at
   the hang, so the last line is the marker).

## 5. Deliverables

| file | what it is | sha256 |
| --- | --- | --- |
| `target-skeleton/target/linux/hisilicon/patches-6.18/041-net-gmac-macconf-nodoorbell.patch` | vendor MAC config replay + PIE nodoorbell bisection + TX-halt containment | `328cdcc1e26393246c3213e1911d2a8fcbd26bb34f550cdef7b5f24bb2aefebb` |
| `target-skeleton/docs/incoming/qm-decode/shared.patch` | LSW-SPEC 7.11 round-4c note, SKELETON-SMOKE round-4c session, README 041 row | see handover |
| `target-skeleton/docs/incoming/qm-decode/SUMMARY.md` | this file | - |
| `target-skeleton/docs/port-notes/qm-decode/SUMMARY.md` | integrated copy | - |

## 6. Validation

- **Series apply (current 001-041):** 41/41 applied, 0 failed hunks, 0 .rej/.orig;
  `luofu-gmac.c` and `luofu-pie.c` byte-identical to the lane tree
  (`logs/10-series-apply-001-041.log`).
- **WERROR compile:** both drivers rebuilt clean under `W=1 -Werror`.
- **checkpatch:** 0 errors, 0 checks, only the expected "missing commit description" warning
  (`logs/11-checkpatch-041.log`).
- No devicetree/binding/Kconfig change; no vendor binary or per-unit value added.

## 7. Status

The hang is now narrowed to: one accepted frame -> descriptor + doorbell completed -> engine
processing wedges the SoC.  W9 is not written in the diag-safe image, so the regression is in the
armed data path's TX-processing behaviour, not in the max-frame value.  Wait for the nodoorbell /
macconf results before any next-layer decode.
