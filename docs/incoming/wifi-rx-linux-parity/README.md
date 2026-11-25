# wifi-rx-linux-parity - Linux RX-enable order vs our Hi5622V100 MAC (task-166)

**Host-only lane.** No bench/device/UART/SSH contact was made producing this
lane; the Lead fires. The lane answers one question and prepares one
experiment: *which steps of the canonical Linux RX-enable sequence does our
clean-room driver not perform, and which of them explain that the MAC RX
captures nothing from the air?*

## The finding

The Linux drivers separate **ring programming** from **engine/ring enable**,
**RX filtering**, and **completion ownership**; our driver (verified against
the vendor-stock register capture) reproduces the ring program but writes **no
explicit RX enable word and no RX filter word**, and its completion/pointer
writes had three width/ownership divergences from the vendor path. The lane:

1. extracts and maps the Linux order (mt76, iwlwifi, ath10k, ath11k, brcmfmac)
   onto the Hi5622V100 live bank / MAC config block / 14-write program / D2H
   CRX events -> [REPORT.md](REPORT.md) section 3-4;
2. shows from `opensource/dumps/reg_all.txt` that the 14-write program already
   covers every vendor non-zero word it can own, leaving exactly **four
   stock-zero host-writable candidate words** (0x08/0x0c/0x14/0x18) plus the
   already-experimented +0x5c/+0x6c -> REPORT section 6;
3. ranks the RX-side host accesses that could explain the firmware exception
   the Lead observed after extended draining, and fixes the four divergences
   -> REPORT section 5;
4. implements a **read-only parity probe** (`cat dp_rx_parity`) + host tool
   (`tools/compare_stock.py`) that measures the remaining gap against the
   vendor capture on the live boot, and two **opt-in, one-shot, fail-closed**
   write experiments (`dp_rx_kick`, `dp_rx_ctl`) for the attended fire;
5. host-tests everything (118 parity checks, 11 compare-stock tests),
   kernel-context-compiles the new/patched objects, and writes the fire
   recipe ([TEST-LADDER.md](TEST-LADDER.md)).

## Deliverables

| path | what |
| --- | --- |
| [REPORT.md](REPORT.md) | the full comparison: canonical Linux sequences, our-HW mapping, gap analysis, crash analysis, patch behaviour, limits |
| [patches/0001-rx-linux-parity.patch](patches/0001-rx-linux-parity.patch) | 6-file patch (applies to the current RX source state; see BUILD-NOTES for rebasing) |
| [src/](src/) | patch result (2 new files, 4 edited) |
| [tools/rx_parity_selftest.c](tools/rx_parity_selftest.c) | 118 device-free checks of every rule the patch applies |
| [tools/compare_stock.py](tools/compare_stock.py) + [tools/test_compare_stock.py](tools/test_compare_stock.py) | probe-vs-vendor-capture differ + 11 unit tests |
| [tools/build.sh](tools/build.sh) | canonical host verification (reconstruct -> patch -> test -> kernel compile -> publication) |
| [TEST-LADDER.md](TEST-LADDER.md) | the attended rungs R0-R6, PASS/FAIL and stop rules (Lead fires) |
| [BUILD-NOTES.md](BUILD-NOTES.md) | base-tree identity, verification results, integration instructions |
| [evidence/](evidence/) | host logs for the above |

## Safety

* No register is written by the read-only path; `dp_rx_parity` reads only
  blocks the vendor's own dumper reads (its output is the committed stock
  capture).
* The two new write actions are inert by default (`rx_ring_kick=0`,
  `rx_ctl_enable=0`), gated exactly like the drain (rx_enable=1 + arm +
  write gate), one shot per load, allowlisted (`rx_ctl`: only 0x08/0x0c/0x14/
  0x18), read-back verified, and fail closed on any mismatch.
* No access is added outside MAC band 0; the live bank is read-only; CA
  0x400392f0 and the RC misc window are never touched.
* Every divergence fix removes a write the vendor path does not perform
  (32-bit RMW publish, raw-status W1C, reads of never-read config words).
