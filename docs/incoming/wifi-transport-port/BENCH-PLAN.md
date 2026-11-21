# BENCH-PLAN — Hi5622 PCIe/ETE transport skeleton, first attended run

**Lane:** wifi-transport-port. **Status:** PLAN ONLY — nothing below has run;
no device was touched while writing this. **Operator:** human-attended, per
BENCH-FACTS.md / RUNBOOK.md / LESSONS.md. The newest dated ledger entry and
STATUS.md win over this document if they differ.

**Instrument:** hi5622_pciet.ko (this lane, build/), the luofu 6.18.60
hybrid image with the 013 PCIe host driver, DTS pcie0/pcie1 + wifi@0,0 nodes
enabled and the dma-ranges line of build/luofu-host-618-delta.patch applied
(dma-ranges is only *required* for stage B2; B1 works without it).  The
vendor firmware blob must be present on the bench rootfs at
/lib/firmware/hi_wifi/FIRMWARE.bin (928,920 B, sha256 7fc87e20…; local,
never committed).  No vendor module is loaded and nothing is ever rmmod'ed —
this is our own 6.18 image, the vendor stack is not running.

## 0. Preconditions (all must be true BEFORE the bench, no exceptions)

| # | gate | command | pass criterion |
| --- | --- | --- | --- |
| G1 | recovery door armed | tools/recovery-card.sh | READY — physical power-off + RESET-hold + TFTP procedure on the card, human at the bench |
| G2 | bench link | tools/bench-link.sh | PASS (eth2 <-> enp195s0f3u1, 192.168.1.88/24 + 192.168.10.44/24, NM-unmanaged) |
| G3 | UART capture running | tools/uart-capture.sh to an ABSOLUTE path under build/uart-logs/ | read-only capture alive; **no** tty writes, no screen/minicom/picocom, no read-write tty opens |
| G4 | image + module present | sha256 of the 6.18 image per STATUS.md; insmod-able hi5622_pciet.ko (built per build/BUILD-NOTES.md) | modinfo shows vermagic 6.18.60 SMP mod_unload ARMv7 p2v8 |
| G5 | no vendor wifi modules loaded | lsmod | hi5622v100_plat / hi5622v100_wifi absent |
| G6 | firmware blob in place | sha256sum /lib/firmware/hi_wifi/FIRMWARE.bin | matches 928,920 B, sha256 above |

Hard rules that stay in force for every stage: **never write device CA
0x400392f0** (the module asserts this at build + runtime); **never read the
RC misc window** (0x10161000); **UART stays read-only**; every device write
is a single explicit sysfs write, logged to dmesg, in the order below.

## Stage A — probe (no device writes beyond PCI enumeration)

1. insmod hi5622_pciet.ko poll_ms=0 (poll OFF).
2. Expect in dmesg: two probes (0000:00:00.0, 0001:00:00.0), BAR0/BAR2
   mapped, SR0/DR0 ring buffers allocated (host DRAM only),
   "no device writes performed - attended steps via sysfs".
3. Checkpoints (read-only):
   - cat /sys/bus/pci/devices/0000:00:00.0/out1 → bitmap word, likely 0
   - cat .../status → the 11 dev_status_check words (pre-release values are
     expected to be idle/garbage; RECORD them for the post-release diff)
   - cat .../ready1 .../ready2 → 0
4. **Gate A:** probe must log with no external abort/SERROR. Any hang/abort →
   stop, power-cycle (G1), do NOT proceed.

## Stage B1 — the boot handshake (the gate this slice exists to pass)

Run each write as its own step; capture dmesg + UART between steps. Recovery
stays armed (G1) for every step from B1.3 onward.

| step | sysfs write | device effect | expected log / checkpoint |
| --- | --- | --- | --- |
| B1.1 | echo 1 > prepare | programs the 6 endpoint-iATU inbound viewports via BAR2 | 6 × "iatu vp%u … readback=OK"; windows = 1 |
| B1.2 | echo 1 > upload | contiguous copy to BAR0+0x6f8000, chunks 0x80000 | "upload … 928920 bytes …"; chunk 524288 + 404632; fw_loaded = 1 |
| B1.3 | echo 1 > verify | none (read-only) | "firmware alias verify … MATCH" at BAR0+0x40000 |
| B1.4 | echo 1 > release | **writes 0x5a5a to CA 0x40000108** (CPU release) | release log; cpu_released = 1 |
| B1.5 | echo 1 > announce | **writes 0xcece to CA 0x4000010c** (D2H gate; host site [U]) | announce log; gate_announced = 1 |
| B1.6 | echo 1 > poll | none | "D2H poll thread started" |
| B1.7 | cat .../status ×3 (≈1 s apart) | none | dev_status words go LIVE: abank_code free-running, dcoldo_vset ≈ 0x260d4184-class, efuse_chip_id 0x186-class (compare stock log §2.5) |

**HCC ready checkpoints (the hard gates):**

- **id 1** — expect within ≈2 s of B1.4 (vendor waited 200 jiffies; phase19c
  measured the first mailbox word at ≈1.85 s): ready1 → 1, dmesg
  "Device plat ready! (D2H message id 1)", poll log "D2H bitmap 0x00000002".
- **id 2** — expect within ≈10 s (vendor 2000 jiffies): ready2 → 1, dmesg
  "DEVICE READY (D2H message id 2)", poll log "D2H bitmap 0x00000004".
- **out[1] = 0x4** — the task's named checkpoint: the poll log must show the
  bitmap word at CA 0x40039014 containing bit 2 (value 0x4) and the drain
  write back to 0 before the next deposit (device precondition P2).  If only
  0x2 is seen and 0x4 never arrives, the device posted plat-ready but not
  DEVICE READY — record the status words and stop; that is a finding, not a
  license to poke further.
- id 3 (ETE transfer done, bitmap 0x8) / heartbeat (id 4) may appear; both
  have handlers or are logged.

**B1 success definition:** ready1 == ready2 == 1 AND the poll log contains a
0x4 bitmap event. **Any** external abort, SError, stuck poll, or UART panic
→ stop, capture, power-cycle (G1). No troubleshooting-by-extra-writes; each
new idea is a new attended session.

## Stage B2 — rings + H2D smoke (NEXT session, only after B1 success)

1. Precondition: dma-ranges applied in the image (RC inbound window), so the
   ETE ring base registers land in the 0x8xxxxxxx window the engine expects.
2. echo 1 > ringinit → SR0 group A + DR0 group B base/depth/index writes;
   verify against phase42 E.3 live-shape invariants (base 0x8xxxxxxx,
   depth-1 = 0x1f).
3. echo 1 > send → 64 B zero-header post on SR0 + doorbell mask 8
   (pcie_msg_send_irq sequence: 0x40039010 = 8, drain-poll 0x400392d4, write
   pending, twin write stays OFF — twin_enabled=0).
4. Observe dmesg/UART for device-side reaction; the twin-word semantics [U]
   is the one register this stage is allowed to pin (one write, recorded).

## Teardown (every session)

rmmod hi5622_pciet is safe only before B1.4; after the release the device
runs firmware and rmmod does not stop it — power-cycle (G1) is the reset.
Capture the UART log to its absolute path, run tools/bench-teardown.sh --apply
(leave TFTP only if a recovery is genuinely armed), append the dated ledger
entry, regenerate STATUS.md.

## What the bench proves (and what it does not)

B1 proves: the six-region BAR0/iATU map, the contiguous upload, the 0x5a5a
release, the 0xcece gate, and the D2H mailbox service (bitmap/drain/dispatch)
— i.e. the transport contract of wifi-driver-re §4 and wifi-firmware-re §2
reproduced by clean-room code on 6.18.  It does NOT prove: ETE data movement
(B2), the hcc message *semantics* (needs the hdpp/hmac slices), or any data
path (out of scope for the transport slice).  Endpoint INTx delivery is still
unwired in the luofu host driver (status registers live in the never-read
misc window) — the polled bitmap is the bench's D2H "ISR"; the optional
use_irq=1 path is compiled in for when that lands.
