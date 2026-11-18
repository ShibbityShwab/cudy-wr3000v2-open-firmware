# Bench read-only inventory 2026-11-18 (inv1): measured register/DT baseline

**Status 2026-11-18 (task-45, lane `inventory-analysis`): analysis of the first attended read-only
inventory run plus its follow-up DT/clk readback. Host-side analysis only - no device access was
made by this lane, no write was performed on the router, and the box was not rebooted.** Every
number in this document is **[V]** for this one stock boot unless it is explicitly marked **[I]**
(interpretation) or **[U]** (still unknown). The bench is idle/stock; fires are stopped.

This is the measured companion to the four evidence-graded specs it updates:
[PIE-DATAPATH.md](PIE-DATAPATH.md), [LSW-SPEC.md](LSW-SPEC.md),
[MISC-SOC-BLOCKS.md](MISC-SOC-BLOCKS.md) and [SMP-RE.md](SMP-RE.md); the target-level
consequences are noted in [TARGET-DESIGN.md](TARGET-DESIGN.md). The mirror of this run's context
(the live DT after the U-Boot overlay) is why the queue/flow DDR base and the PIE ring geometry
questions can now be marked closed.

**Evidence bundle (parent repository, gitignored):**
`parent:build/bench-inventory/20261118T130512Z-inv1-run/` - inventory.json/TSV, plan.tsv,
inventory.log, commands.txt, batches, health-pre/post.raw, context capture and SHA256SUMS, plus
`followup/` with the raw DT property bytes, cmdline, boot_reg, clk_summary and their hashes.
The tool is `parent:tools/bench-inventory.sh` (v1, read-only by construction: static address
allowlist, generated read-only scripts, write-token scan, RC-misc/CA denylist, boot_id
unchanged check). Neither the tool nor this document writes to the device.

---

## 1. Run metadata and gates

| item | value |
| --- | --- |
| run | `20261118T130512Z-inv1-run` (UTC), generated `2026-11-18T13:05:56Z` |
| tool / mode | `tools/bench-inventory.sh` version 1, `inventory` mode, groups `lsw,pie,crg,sysctrl` |
| repos | parent `491ab5d`, `opensource/` `54c4f95` |
| reads | 600 planned = 476 lsw + 108 pie + 13 crg + 3 sysctrl; batch 16, repeat 2; 38 batches run, 0 failed |
| result | **COMPLETE**: 600/600 status `ok`, 0 error, 0 not-attempted, 0 partial |
| stability | every static register read identically in both passes; 11 entries moved and all are expected-dynamic (5 PIE ring-pointer words + 6 LSW DFX counters) |
| expected-value checks | **40 pass / 16 fail / 544 unknown** (37 PIE + 2 CRG + 1 SYSCTRL pass; see section 8) |
| gates | 8/8 PASS: attended, transport, no-active-fire, bench-link PASS, recovery-door READY, read-only UART capture running, no stale writers, stock-health |
| pre-run health | 13/13 PASS: stock 5.10.201 with the stock build stamp, OpenWrt 22.03.6, 2 wiphys + calibration, 40 `hi_` modules, no `.omo-off`, mtd11-14 present, bootflag=a, selector low byte 0x10, running rootfs mtd13, devmem usable |
| post-run health | 4/4 PASS: probe answered, boot_id unchanged (one boot; uptime 47,467 s -> 47,489 s), uptime monotonic, 2 wiphys still up, no `.omo-off` |
| integrity | both SHA256SUMS files verify (the run directory's from inside the directory, the follow-up's from the parent repo root); `inventory.json` sha256 `95d02a383329b9a16b36d685ff955be2d7a93bc34c467ed806fe94bb66c9657a` |

**Known capture limitation (recorded, not hidden).** The run's built-in context capture produced
0 binary DT properties because the device's stock busybox has no `od`; the raw context block only
lists the property *names*. The run's `inventory.json` therefore indexes only the two text
context files (`/proc/mtd`, `/proc/cmdline`). The missing live-DT values were fetched afterwards
by the lead as a **read-only follow-up through `tools/rsh.sh` on the same boot** (boot_id
unchanged, `boot_reg` 0x10, no writes, no reboot) and saved under `followup/` with a README, the
raw big-endian property bytes, decoded.txt, cmdline/boot_reg/clk_summary and SHA256SUMS. The
follow-up is the evidence for the live-DT values in sections 2, 3 and 5, and for the clk_summary
in section 4.2. The tool owner is reworking the context capture to fetch raw
property bytes over SSH and decode them host-side; a future run should make the bundle
self-contained.

**Reproduce the analysis (host-only, no device):**

```sh
cd <parent repo root>
R=build/bench-inventory/20261118T130512Z-inv1-run
( cd "$R" && sha256sum -c SHA256SUMS )
sha256sum -c "$R/followup/SHA256SUMS"   # followup entries are repo-relative paths
python3 -c 'import json; d=json.load(open("build/bench-inventory/20261118T130512Z-inv1-run/inventory.json")); [print(e["addr"],e["expected"],e["value"],e["check"]) for e in d["entries"] if e["check"]=="fail"]'
```

---

## 2. PIE (0x10a70000 window, 64 KiB): the live channel model and programmed state

The base-DT pie node (pinned DTS) and the live boot node differ: the U-Boot overlay
([soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 8, fragment 6) reprograms the ring
properties for board_id=3. The **live** post-overlay node was read back in the follow-up and is
the configuration the vendor driver actually programmed. [V]

| live DT property | value (big-endian u32 words) |
| --- | --- |
| `rx_ring` | `<0x8 0x600 0x8 0x600>` = **2 pairs** (2 RX channels), N=8, size word 0x600 = 1536 |
| `tx_ring` | `<0xc 0x2>` = N=0xc, **2 TX channels** |

The other pie node properties (`reg = <0x10a70000 0x10000>`, `interrupts`, `dma-coherent`,
`hwver`) were not re-read byte-wise in the follow-up; their base-DT values are unchanged in the
pinned conversion source and are used as [V] elsewhere ([PIE-DATAPATH.md](PIE-DATAPATH.md)
section 3).

Base-DT comparison (for the delta only): the pinned base DTS says
`rx_ring = <0xa 0x800> x4` (4 pairs) and `tx_ring = <0xc 0x4>` (4 TX channels). The live values
are the overlay's 2-channel wired/2-CPU configuration; the base-DT values remain in our conversion
source but are **not** what the stock driver runs with.

**Active-channel model (measured).** Only channels 0 and 1 are armed:

| register | ch0 | ch1 | ch2 | ch3 | ch4-7 |
| --- | --- | --- | --- | --- | --- |
| `0x2000 + ch*0x10` TX ring base | `0x825c0000` | `0x82600000` | 0 | 0 | not read (outside the allowlist; the P3 plan reads ring blocks ch0-3) |
| `0x2004 + ch*0x10` TX channel config | `0x0000000c` | `0x0000000c` | 0 | 0 | not read |
| `0x3000 + ch*0x10` RX ring base | `0x82420000` | `0x82518000` | 0 | 0 | not read |
| `0x3004 + ch*0x10` RX channel config | `0x00000008` | `0x00000008` | 0 | 0 | not read |
| `0x3504 + ch*0x10` size config | `0xc` | `0xc` | `0xc` | `0xc` | not read |

Key readings and closures:

- **Ring-pair first word == programmed channel config.** TX config `0xc` equals live
  `tx_ring[0]`; RX config `0x8` equals live `rx_ring[0]`. This is the direct readback the
  PIE model needed and closes the "do the ring pair first words encode N?" [I] row: the
  config register holds the same first word (N), for both TX and RX. [V]
- **Size config `0x3504`..`0x3534` all read `0xc`.** `0xc` = live size word 0x600 >> 7
  (1536 / 128). It does **not** match the base-DT-derived expectation 0x100 (32 KiB ring >> 7)
  and it also reads 0xc on the two unconfigured channels. [I] The register reflects the per-channel
  size word in 128-byte units (or a global/mirrored copy of it); the exact "buffer size vs ring
  byte size" semantic and why inactive channels read the same value remain [U]. Do not treat
  `0x3504` as a per-channel ring descriptor-count register.
- **2 RX + 2 TX channels, not 4.** The live node has 2 RX pairs and a TX channel count of 2; the
  driver armed exactly those. The 4 interrupt vectors still exist (SPI 88-91, per-vector channel
  pairs `pie-ch0,7`..`ch3,4`), but only the vectors covering ch0/ch1 have a configured channel
  in this boot. PIE-DATAPATH section 5.3's "4 RX + 4 TX on this board" was derived from the
  base-DT values and is corrected here.
- **Per-channel reg-init words are written for all 8 channel slots** (not only wired ch0-3):
  `0x180/0x200/0x380 + ch*4 = 1` and `0x3210/0x3290/0x3310 + ch*4 = 3/2/8` for every ch0-7.
  This confirms those are reg-init defaults, not per-active-channel configuration; their
  semantics stay [U].
- **RX IRQ status `0x300 + ch*4 = 0` for all ch0-7** (idle boot, no RX pending at sample time).
  **RX IRQ mask `0x400 + ch*4`: ch0=0, ch1=0, ch2=1, ch3=1, ch4-7=0.** With only ch0/ch1
  configured, the 1 on ch2/ch3 matches "reg_init leaves channels masked and only configured
  channels are unmasked"; the 0 on ch4-7 does not fit that write model (the register array or the
  channel loop may only cover 4 entries) - recorded raw, remaining **[U]**, not hidden.
- **TX/RX pointers are live and moving**: TX ch0 `+0x08=0x639, +0x0c=0x06390639`; RX ch0
  `+0x08=0x00e600e5, +0x0c=0xe7`; ch1 analogues nonzero; both passes differed for several
  pointer words. Of the 11 entries that moved between passes, 5 are these PIE pointer words and 6
  are moving LSW DFX counters - the expected volatile set, not a fault.

**Global / per-channel control words measured [V]:** `0x1000 = 0x000a01c8` (bit0 clear,
bits6-8 set - matches the reg_init model), `0x2200 = 0x0045e000`, `0x3200 = 0x00002001`
(bit0 global RX enable set; bit13 also set), `0x3204 = 0`, `0x3208 = 0x7d`, `0x3404 =
0x00001b04`, `0x3408 = 0`, `0x3700 = 0x0000ffff`.

Two model corrections follow from the measured words: the board has **no `gro_ring`**, yet
`0x3700` reads `0xffff` (not zero/unwritten) and `0x3404` is a non-trivial `0x1b04`. Either
the bootloader or an unconditional reg_init write sets them; the documented "written only when a
GRO ring exists" model is therefore incomplete. Values recorded, semantics [U].

---

## 3. LSW (0x11xxxxxx/0x12xxxxxx windows): windows, qid tables, W1 baseline

### 3.1 Live DT windows and the queue/flow DDR base

The live post-overlay `lsw_dp`/`lsw_pfe` `reg`/`reg-names` were read back byte-for-byte in the
follow-up. [V]

`lsw_dp` - `reg-names = "queue flow dp eps eqm fam ips"`, 7 (address, size) pairs:

| name | address | size | note |
| --- | --- | --- | --- |
| `queue` | `0x80000000` | `0x400000` (4 MiB) | **re-based by the U-Boot overlay**; base DTS had `0x87b00000` |
| `flow` | `0x80400000` | `0x100000` (1 MiB) | **re-based by the U-Boot overlay**; base DTS had `0x87f00000` |
| `dp` | `0x11000000` | `0x80000` | register page |
| `eps` | `0x11280000` | `0x80000` | register page |
| `eqm` | `0x11200000` | `0x80000` | register page |
| `fam` | `0x11100000` | `0x80000` | register page |
| `ips` | `0x11080000` | `0x80000` | register page |

`lsw_pfe` - `reg-names = "ifc l2 napt mcc opp pa pe pfe_glb prbs pub"`, 10 (address, size) pairs,
all `0x80000`: `ifc 0x12100000`, `l2 0x12180000`, `napt 0x12280000`, `mcc 0x12380000`,
`opp 0x12400000`, `pa 0x12080000`, `pe 0x11180000`, `pfe_glb 0x12000000`,
`prbs 0x11380000`, `pub 0x12480000`.

This closes LSW-SPEC section 5.4 / 13.2 item 3: the two unregistered windows are DDR the engine
consumes, and the **live base is 0x80000000 (queue) / 0x80400000 (flow)**, not the base-DT
0x87b00000/0x87f00000. The U-Boot overlay's rebase (documented in
[soc/vendor-dt-notes.md](soc/vendor-dt-notes.md) section 8) is now confirmed on the running
kernel. A clean-room driver must reserve/account for this DDR for the engine rather than map it as
a register page or program its own buffers there; the upstream binding still (deliberately) does
not model the regions - that is a design decision, not a missing measurement.

### 3.2 Selected vendor-initialised register state (W1 baseline) [V]

| address | field | value |
| --- | --- | --- |
| `dp+0x1c` | probe halfword OR 0x10000 | `0x00018000` |
| `dp+0x64` / `dp+0x68` | probe cell-size selector / memory number | `0x1` / `0x0` |
| `eps+0x1008` | queue/DFX | `0x0` |
| `eqm+0x00` | queue table command/status base | `0x1` |
| `eqm+0x18` / `eqm+0x38` / `eqm+0x40` | queue/cell config / probe-service enable | `0x80001cc0` / `0x7f0` / `0x80000000` |
| `eqm+0x48` | indirect table command/status | `0x41100012` (bit31 busy=0, bit5 error=0 -> idle) |
| `eqm+0x5c` | indirect write-data | `0xbb8` |
| `eqm+0xa4` / `eqm+0x504` / `eqm+0x600c` | queue-table config / counter selector / queue-table word | `0x1fc0` / `0x0` / `0x39` |
| `fam+0x504` | queue-count/cell comparison | `0x2000` |
| `ips+0x00` / `0x08` / `0x10` | probe/config, init 42, init bit16 | `0x0` / `0x2a` / `0x790011` |
| `eps+0xB00 + ch*4` (ch0-7) | PIE channel map words | `0x880, 0x980, 0xa80, 0xb80, 0x90, 0x81090, 0x82090, 0x83090` |
| `eps+0xB80 + ch*4` (ch0-7) | channel map companion words | `0x8000ffff, 0x8001ffff, 0x8000ffff, 0x8001ffff, 0x8004ffff, 0x8105ffff, 0x8106ffff, 0x8107ffff` |

Counter blocks (eqm+0x2004.., eps+0x2084.., l2/napt/mcc/opp/pa/pe/pfe_glb/prbs/pub +0x00/+0x1004/
+0x1008) were read: mostly zero on the idle boot, with a handful of live monotonic counters
(e.g. eqm+0x2008 `0x115d9`, eqm+0x2030 `0x1162b`, eps+0x2094 `0x10799`, ips+0x1344
`0x1163a`, pa+0x1004 `0x1168c`, pe+0x1004 `0x116d4`, mcc+0x00 `0x201410`,
pfe_glb+0x00 `0x7af`). They are volatile and not a decode base.

### 3.3 QID tables A and B - the measured vendor map [V]

Both tables are in the `opp` window (`0x12400000`). Entry shape from the vendor code:
`(0x100 enable bit) | (qid low byte)`; `port_qid_map_set` writes 8 slots per port in the
0..15- and 40..47-class layouts.

**Table A, `opp+0x42800` (128 words = 16 ports x 8 slots): 72/128 words non-zero.** Ports 4..12
are populated; ports 0-3 and 13-15 are all zero. Each port row is 8 slots, values contiguous:

| port | slot0..7 raw values | qid low bytes |
| --- | --- | --- |
| 4 | `0x101..0x108` | 1..8 |
| 5 | `0x109..0x110` | 9..16 |
| 6 | `0x111..0x118` | 17..24 |
| 7 | `0x119..0x120` | 25..32 |
| 8 | `0x121..0x128` | 33..40 |
| 9 | `0x129..0x130` | 41..48 |
| 10 | `0x131..0x138` | 49..56 |
| 11 | `0x139..0x140` | 57..64 |
| 12 | `0x141..0x148` | 65..72 |

**Table B, `opp+0x43800` (256 words = 8 ports x 8 slots x 4 words): 128/256 words non-zero.**
**It is not all-zero on this boot** - ports 40..43 are populated, ports 44..47 are zero. Each
16-byte entry holds the same value four times (the 40..47-class write shape):

| port | slot0..7 (each word == shown value, 4 identical words per entry) | qid low bytes |
| --- | --- | --- |
| 40 | `0x149, 0x14a, 0x14b, 0x14c, 0x14d, 0x14e, 0x14f, 0x150` | 73..80 |
| 41 | `0x151..0x158` | 81..88 |
| 42 | `0x159..0x160` | 89..96 |
| 43 | `0x161..0x168` | 97..104 |
| 44-47 | all `0` | - |

**Interpretation [I], not yet a hardware statement:** the single QID space continues across the
two tables - table A is qids 1..72 (0x100|qid), table B is qids 73..104 for the special
40..47-class ports 40..43. That gives 13 populated ports x 8 slots = 104 queues, contiguous, with
qid 0 unused. Ports 4-7 are the four PIE/EPS queue ports and ports 8-12 the five MAC ports per
LSW-SPEC section 6.2 [I]; what ports 40..43 are (Wi-Fi/WoC class?) remains **[U]** - do not
name them from this sample. What is [V] is the table contents, the 0x100 flag, the 8-slot rows,
and the port ranges.

This is the **D1b W1 baseline**: the vendor-initialised dp/eps/eqm/opp state before any
clean-room write. W2 (derive the write set on paper) is now possible from these values; W3-W6
(clock/reset only, one qid-map write, remaining ports, forwarding) still need attended write
experiments and are untouched.

---

## 4. CRG (0x14880000): gates, resets, mux, PLLs, clk_summary

### 4.1 Raw words [V]

| offset | value | note |
| --- | --- | --- |
| `0x14` | `0x01360ef1` | gate word 0x14 (sfc/gpio/i2c/led_pwm group) |
| `0x20` | `0x02f8300b` | gate word 0x20 (gemac/mdio/lsw/pcie/pie group) |
| `0x2c` | `0x0d300e01` | reset word |
| `0x30` | `0xfe7fe05b` | reset word (lsw dp/pfe, pie, mdio) |
| `0x34` | `0x000ff309` | reset word (PCIe RC / efuse / core) |
| `0x38` | `0x00200000` | resume/soft-reset word |
| `0x90` | `0x6a010008` | PLL status (mask 0x48000000 pass: bits 30 and 27 both set) |
| `0x100` | `0x00000000` | watchdog ISTATUS |
| `0x138` | `0x00018042` | clock muxes |
| `0x198` / `0x19c` | `0x8103c11c` / `0x11000000` | PLL1 control words (CPU PLL) |
| `0x1e0` / `0x1e4` | `0x81064548` / `0x11000000` | PLL2 control words (LSW PLL) |

Cross-checks (all [V] consistency, not new semantics):

- Gate word `0x20` bits set: 0 (lsw_dp_clk0), 1 (lsw_pfe_clk0), 3 (mdio_clk0), 12/13
  (pcie0/1), 19-23 (gemac_clk0-4), 25 (pie_clk0) - exactly the 11 gates of the pinned DT's
  0x20 group, all left enabled by the vendor drivers. `clk_summary` agrees except gemac_clk0,
  which has no consumer (enable_count 0) but is still hardware-enabled.
- Gate word `0x14` bit0 (sfc_clk) set, bit14 (led_pwm) clear - matches sfc_clk enabled (1 user)
  and led_pwm disabled (0 users) in clk_summary. Other set bits in `0x14` are outside the
  pinned-DT gate nodes available for cross-check; [U].
- Reset word `0x30` bits 0/1 (dp_logic/dp_srst), 3/4 (pfe_logic/pfe_srst) and 25 (PIE reset
  `0x19`) are set -> the LSW/PFE/PIE blocks are out of reset on the running vendor system.
- Reset word `0x34` bits 16-19 (PCIe RC resets `0x10`-`0x13`) are set -> both RCs out of
  reset; pcie0/1 clocks enabled in clk_summary.
- Resume word `0x38` = `0x00200000`: CPU0/CPU1 resume bits `0x16/0x18/0x17/0x19` all read 0
  on the running 2-CPU boot [I consistent with the release having cleared them]; bit21=1 is [U].
- Mux word `0x138` bits: 1=1 (efuse mux source = osc), 3=0 (CPU mux source = CPU PLL) -
  consistent with efuse_ring/efuse-clk-mux at 20 MHz and cpu-clk at 1.2 GHz. Bits 6/15/16 are
  other muxes; [U] do not decode.

### 4.2 clk_summary measured rates (live debugfs tree) [V]

| clock | rate | enable count | notes |
| --- | --- | --- | --- |
| osc | 20 MHz | 1 | reference |
| corrector_clk | **1.2 GHz** | 1 | base DTS fixed-clock says 1 GHz - measured value differs, see below |
| fixed_factor_clk | **300 MHz** | 1 | child of corrector_clk |
| efuse_ring_clk / efuse-clk-mux | 20 MHz | 0 / 0 | |
| lsw-clk | 100 MHz | 0 | |
| cpu-clk / cpu-clk-mux | 1.2 GHz / 1.2 GHz | 0 / 0 | |
| lsw_pfe_clk / lsw_pfe_clk0 | 333 MHz / 333 MHz | 1 / 1 | |
| lsw_dp_clk / lsw_dp_clk0 | 166 MHz / 166 MHz | 1 / 1 | exactly half the PFE rate |
| gemacN_clk | 125 MHz | 5 | mdio_clk0 + gemac_clk1-4 enabled; gemac_clk0 unused |
| ahb_clk / pie_clk0 | 200 MHz / 200 MHz | 1 / 1 | PIE clock |
| apb_clk | 100 MHz | 9 | pcie1/pcie0/sfc enabled, gpio/i2c/led_pwm idle |

The fixed-clock/mux parents in MISC-SOC-BLOCKS section 5.2 are confirmed for the gates that have
a consumer. Two measured-vs-model deltas: (a) `corrector_clk` = 1.2 GHz and
`fixed_factor_clk` = 300 MHz, where the pinned base DTS declares a 1 GHz fixed clock and /4 =
250 MHz - the vendor corrector clock is evidently not the static DT value on this boot; do not
change our DTS fixed-clock value from this sample alone [U] (reason not decoded). (b)
`clk_summary` shows only the registered tree; the TWD clock and some fixed inputs are not rows,
so this is not a full clock census.

### 4.3 PLL rate math against the measured rates [V]/[I]

Equation from patch 019 / the vendor `hi_pll_recalc_rate`:
`rate = 20 MHz * (int + frac/2^24) / fbdiv`, then each non-zero divider-select field as an
integer division; word0 = `int = (w0>>12)&0xfff`, `frac = w1&0xffffff`, `fbdiv =
(w0>>24)&0x3f`; word0 byte0 bit3 = integer-only path, bits 4..6 and 8..10 = divider-select.

**PLL1 (CPU):** `w0=0x8103c11c`, `w1=0x11000000` -> int=60, frac=0, fbdiv=1, byte0=0x1c
(integer-only set; divsel bits4-6=1, bits8-10=1). Rate = 20 * 60 / 1 = **1200 MHz**, which is
exactly the measured `cpu-clk` 1.2 GHz. **The decoded equation is validated by measurement.** [V]

**PLL2 (LSW):** `w0=0x81064548`, `w1=0x11000000` -> int=100, frac=0, fbdiv=1, byte0=0x48
(integer-only set; divsel bits4-6=4, bits8-10=5). Raw = **2000 MHz**, but the LSW units run at
333/166/100 MHz. Arithmetic to record, not settle:

- a naive single divider field (4) gives 500 MHz, which is not observed anywhere in the live tree;
- the two fields multiplied (4*5=20) give exactly 100 MHz, matching `lsw-clk`;
- 2000/6 = 333.3 MHz and 2000/12 = 166.7 MHz match `lsw_pfe_clk`/`lsw_dp_clk` (displayed 333/166),
  but 6 and 12 are not the two decoded fields.

So the PLL integer/fraction arithmetic is validated by PLL1, while **how PLL2 is divided down to
the LSW rates is [U]**: the divider-select field encoding, a possible intermediate post-divider,
and the downstream LSW clock tree (mux/gate/fixed-factor cells not read in this run) remain open.
Do not implement `set_rate` from this sample. The PLL lock status word `0x90` reads bits 30
(CPU PLL) and 27 (LSW PLL) set, consistent with both PLLs locked.

---

## 5. SYSCTRL (0x10100000): the three cells

| offset | value | grade / meaning |
| --- | --- | --- |
| `0xc00` | `0x80609680` | [V] live SMP release/pen word. It is **exactly** the SMP-RE decoded `__pa_symbol(secondary_startup)` (vendor kernel physical base 0x80608000 + 0x9680), so the decoded release target is confirmed on hardware. [I] this validates the decoded value for the vendor kernel; our own kernel's `TEXT_OFFSET` scheme should produce the same 0x80609680, but that still needs our first fire to read it back. |
| `0xc08` | `0x00000010` | [V] boot selector: low byte 0x10 = pair A (bootflag=a), **upper 24 bits are zero**. This closes the selector upper-bits [U] for this boot (SELECTOR-0x21 R3); the sysfs shadow crosscheck (`boot_reg` 0x10) passes. |
| `0xc10` | `0x82000000` | [V] first live reset-record baseline; layout/writer still [U] - recorded, not modelled. |

Also captured: `/proc/cmdline = "noinitrd cma=0 console=ttyS0,115200 earlycon maxcpus=2 nr_cpus=2
additional_cpus=1"` - the loader passes the 2-CPU bootargs through to the kernel [V] (this is the
vendor boot's cmdline; whether our kernel receives the same from the stock loader is still [U]),
and `/sys/devices/platform/sysenv/boot_reg = 10` (hex without prefix) matched by both reads.
The base/pinned DT values the specs cite (`system-controller@10100000 reg = <0x10100000 0x1000>`,
`smp-offset = <0xc00>`, `rstinfo record_addr = <0x10100c10>`) are unchanged in the conversion
source, but the live sysctrl/rstinfo nodes were **not** re-read byte-wise in the follow-up - the
cells above are the measured evidence. See [SMP-RE.md](SMP-RE.md) and
[MISC-SOC-BLOCKS.md](MISC-SOC-BLOCKS.md) section 6.3.

---

## 6. The 16 expected-token corrections, and why

The run's 16 non-zero expected-value checks are **model corrections, not tool or device faults**:
the allowlist tokens were written from the *base* DT and from the pre-inventory write model, and
the live U-Boot-overlay configuration differs. All 16 are PIE; there were 40 passes and 544
"unknown" (deliberately unasserted) checks.

| # | register(s) | expected token | measured | why the token was wrong / correction |
| --- | --- | --- | --- | --- |
| 1-2 | `0x3004` ch0/ch1 (RX channel config) | `0xa` | `0x8` | token from base-DT `rx_ring[0]=0xa`; live overlay sets `rx_ring[0]=0x8` and the driver programs that. The measured RX config equals the live DT first word - model confirmed, token corrected. |
| 3-6 | `0x3504` ch0-3 (RX size config) | `0x100` | `0xc` | token read as "32 KiB descriptor ring >> 7" from base-DT N=0xa. Live size word is 0x600; 0x600>>7 = 0xc. Readback follows the size word, not the descriptor-ring byte size. Exact semantic [U] (see section 2). |
| 7-10 | `0x2000`/`0x2004` ch2/ch3 (TX ring base/config) | `nonzero` / `0xc` | `0` / `0` | token assumed 4 TX channels (base-DT `tx_ring[1]=4`); live overlay sets 2 TX channels, so the driver never arms ch2/ch3. |
| 11-14 | `0x3000`/`0x3004` ch2/ch3 (RX ring base/config) | `nonzero` / `0xa` | `0` / `0` | token assumed 4 RX pairs (base-DT); live overlay sets 2 RX pairs, so ch2/ch3 are unarmed. |
| 15-16 | `0x400 + ch*4` ch2/ch3 (RX IRQ mask) | `0x0` (unmasked) | `0x1` (masked) | the model "init leaves every channel masked, then unmasks the configured ones" is right; ch2/ch3 are not configured, so they stay masked. (The ch4-7 masks read 0, which does not fit that write model - [U].) |

What the corrections change in the specs: PIE-DATAPATH's TL;DR/channel-model/register-status text
is updated to the 2-active-channel live model; LSW-SPEC's window/queue baseline and the 12.3 W1
row are updated; MISC-SOC-BLOCKS gets the measured CRG/SYSCTRL values and the PLL-validated
equation; SMP-RE gets the pen-word readback; TARGET-DESIGN's closed-[U] rows are updated. The
inv1 allowlist tokens themselves are tool input and are not edited here (the tool owner owns
them; a re-run would carry corrected tokens or `unknown` for the overlay-dependent values).

---

## 7. What closes which [U] item

| spec item | was | now |
| --- | --- | --- |
| PIE-DATAPATH 10, "ring pair-word semantics" | [I] | **[V] measured**: TX config = live `tx_ring[0]`, RX config = live `rx_ring[0]`; second word readback discussed in section 2 |
| PIE-DATAPATH 10, "per-channel words 0x180/0x200/0x3210/0x3290/0x3310/0x3204/0x3208/0x0380" | [U] | **[V] raw values measured** for all 8 channel slots; field meanings still [U] |
| PIE-DATAPATH 10, "0x3404/0x3408/0x3700" | [I] writes recovered | **[V] measured** (0x1b04 / 0 / 0xffff); semantics still [U] |
| PIE-DATAPATH 11.2 **P3** read-only inventory | open gate | **closed for the register/model rows above** (this run + follow-up); P4/P5 remain |
| PIE-DATAPATH 10, "bootloader/firmware pre-initializes PIE?" | [U] | **partially closed**: post-boot state captured, including the unexplained 0x3700=0xffff; pre-Linux state still needs another method |
| PIE-DATAPATH 10, "channel pairing model" | [I]/[U] | **partially closed**: live active set is ch0/ch1 of 8 slots; the `pie-chN,M` pairing digit is still [U] |
| LSW-SPEC 5.4 / 13.2 #3, queue/flow DDR live base | [U] | **closed**: queue 0x80000000/4 MiB, flow 0x80400000/1 MiB after the overlay (section 3.1) |
| LSW-SPEC 7.3 qid tables | vendor write-shape only | **[V] runtime contents**: table A ports 4-12 qids 1..72, table B ports 40-43 qids 73..104, 0x100 flag on every entry, table B **not** zero (section 3.3) |
| LSW-SPEC 12.3 / D1b W1 baseline | planned | **W1 measured**; W2 paper write-set can start; W3-W6 still attended write work |
| LSW-SPEC 6.3 port_bitmap | [V] read, [U] semantics | [V] live value = base-DT `<0xf01 0x1ff0>` unchanged by the overlay; semantics still [U] |
| MISC-SOC-BLOCKS 5.3/5.5 "attended PLL rate readback" | open | **closed**: clk_summary rates + both PLL word pairs; equation validated via PLL1; PLL2 divider mapping still [U] |
| MISC-SOC-BLOCKS 6.3 SMP word / selector upper bits / reset record | [U] | **pen word [V] = 0x80609680; selector upper 24 bits [V] = 0**; reset record value recorded, layout [U] |
| SMP-RE U4 (our TEXT_OFFSET scheme gives the vendor pen value?) | [U] | **half closed**: the vendor pen value is now measured at 0x80609680, exactly as decoded; our-kernel equivalence still needs our first fire readback |
| SMP-RE U10 (do the vendor bootargs reach the kernel?) | [U] | **partially closed**: the stock boot's `/proc/cmdline` carries `maxcpus=2 nr_cpus=2 additional_cpus=1`; our-kernel boot still [U] |
| TARGET-DESIGN 6.2 internal-switch row, queue/flow live base | [U] | **[V] measured** (same as LSW 5.4); not modelled upstream by design |
| TARGET-DESIGN 6.2 CRG row, "one attended PLL rate readback" | open | **done** (this run); `set_rate` path still not implemented/written |
| TARGET-DESIGN 6.2 SMP row | pen value computed only | **vendor pen word [V] measured**; release on our kernel still never run |

---

## 8. What remains unmeasured (do not claim these)

- **No write experiments.** The run is read-only; the D1b write set, gate/reset polarity
  experiments, qid-map writes, PLL `set_rate`, SMP release and any driver programming remain
  unvalidated. Nothing here proves a write will behave.
- **PIE channels 4-7 and the WoC rings** were not in the allowlist (ring blocks were read for
  ch0-3 only); `woc_rx_ring`/`woc_tx_ring` live values, channel pairing and the
  `pie-chN,M` semantics are open. IRQ status was sampled idle (all 0) - behavior under traffic
  is not measured.
- **PIE semantics** of 0x2200, 0x3200 bit13, 0x3404, 0x3408, 0x3700, the size-config register
  (0x3504), the ch4-7 IRQ-mask zeros, and the descriptor formats are still [U]; this run captured
  values, not behavior.
- **LSW internals**: EQM indirect command-word layout (only the idle status word was read), the
  queue-table entry layout, per-port VLAN/PVID, L2/IFC encodings, the 40..47 port class meaning,
  the internal port-id mapping and the PIE<->LSW binding timing. The `/proc/hsan` DFX dumps were
  not taken (separate bounded read).
- **CRG internals**: how PLL2 reaches 333/166/100 MHz (divider-select encoding / downstream
  cells), corrector_clk 1.2 GHz vs the 1 GHz DT fixed-clock, `0x38` bit21, `0x138` bits
  6/15/16, non-gate `0x14` bits, reset-record layout.
- **Our kernels**: no lab/upstream kernel booted; all values are the vendor system's post-boot
  state under its own drivers. Our init may program different channel counts, ring sizes, qid
  maps and clock parents.
- **Board variance**: one unit, one boot, vendor stock configuration (wired-only in the sense of
  the switch path; Wi-Fi modules were loaded). Re-run after our drivers exist, and on a second
  unit if one becomes available.

---

## 9. Do not overfit this snapshot

This is **one boot of one unit of the stock vendor system**, sampled 47,467 s after boot,
read-only, with no traffic injected and no write of any kind. Rules that follow:

1. **Do not bake volatile state into a binding or driver.** Ring pointers, counters, DMA bases
   (0x82420000-class addresses) and table contents are runtime state; only the DT properties,
   register offsets and static configuration words are model facts.
2. **Do not treat the qid map as an ABI.** It is what the vendor queue manager happened to
   allocate on this boot (qids 1..104, ports 4-12 and 40-43). Our driver must derive its own
   allocation from the API/model, not hard-code this row.
3. **Do not infer write polarity from reads.** Gate/reset words are final state; the vendor's
   disable-polarity quirk and the write-1-set latch class are not measurable from here.
4. **Do not read the 2-channel live configuration as the hardware limit.** The hardware has 8
   channel slots and 4 IRQ vectors; the vendor overlay sized the rings for this board's 2 CPUs /
   wired build. A different DT/overlay or our driver can configure differently.
5. **Do not treat the 16 "failures" as regressions.** They are the spec's base-DT-derived
   expectations meeting the live overlay; the pass/unknown counts are for the same run. A future
   run should update the tokens (or assert only stable model facts).
6. **Single sample.** Stability here means two passes seconds apart on one boot; it is not a
   rate/behavior measurement (LESSONS-style: no rate claim from n<5, and no claim about
   behavior under traffic/soak).
7. **All values are vendor-driver state.** "PIE RX enabled" means the vendor's PIE driver enabled
   it after its own init; the same register may read differently when our driver owns the block.

---

## 10. Changelog

- 2026-11-18: initial analysis of inv1 (`20261118T130512Z-inv1-run`) plus its read-only DT/clk
  follow-up. Companion measured sections added to [PIE-DATAPATH.md](PIE-DATAPATH.md),
  [LSW-SPEC.md](LSW-SPEC.md), [MISC-SOC-BLOCKS.md](MISC-SOC-BLOCKS.md) and
  [SMP-RE.md](SMP-RE.md); [TARGET-DESIGN.md](TARGET-DESIGN.md) rows updated where a [U] closed.
  No device access, no writes, no commits by this lane.
