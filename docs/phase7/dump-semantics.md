# Phase 7 - Giving the register dump meaning: classification of all 72 windows

Source (local, read-only; no device access):
- `C:/Users/ShibbityShwab/router-openwrt/build/register-dumps/reg_all.txt` (run 1) - sha256 `a8461c98a464a8368bcb91969ddeb609a6d6d0083c124b76bc09a940b336d6f4`
- `C:/Users/ShibbityShwab/router-openwrt/build/register-dumps/reg_all_run2.txt` (run 2, seconds later) - sha256 `382db1fe622ed5d60374ca11742e258658d64fbd2073b1d76753dea1d9c5989c`
- Cross-reference: `ulw/phase6/register-windows.md`, `ulw/phase6/register-dump.md`, `ulw/phase4/mmio-map.md`, `ulw/phase6/message-fields.md`
- Tooling: `C:/Users/ShibbityShwab/router-openwrt/pyenv/Scripts/python.exe`

Every classification below is produced by the diff/scan scripts in **Appendix A**. The `nz` column is the
non-zero word count, the `ch` column is the word count that differs between run 1 and run 2.

---

## 0. TL;DR

1. The dump is 25,154 u32 words = **72 contiguous windows** (a window = a maximal run of `+4`-spaced
   addresses). The dump's own header lines label only 12 of them; the other 60 are grouped by the
   `2g/5g_mac`, `2g/5g_phy`, `2g/5g_soc`, `2g/5g_rf_and_abb` headers that precede each group.
2. **310 words changed** between the two runs. They are not spread evenly: they cluster into a handful of
   replicated / monotonic banks in the *MAC* and *PHY* windows. The whole `soc_register` group (glue,
   eFuse, PCIe, TCXO) is effectively static - 31 changed words out of ~4,000.
3. **No window is host RAM.** All 72 are device chip-address (MMIO/BAR) space reached through
   `oal_pcie_devca_to_hostva`; the sole non-register window is window 1 (`0x0..0x30`), which is an **ARM
   exception vector table** (5x `ldr pc,[pc,#0x18]`, a NOP, 2x `ldr pc,[pc,#0x14]`, then the 5 branch
   targets `0x8c,0x3c,0x4c,0x5c,0x6c`).
4. The `iwpriv` 2.4/5 GHz power tables are **not visible** in any window: the exact tuple `0x17161605`
   (and every other word of the 26-word `get_2g_power_param` table) has **0 occurrences** in the dump.
   The tables live in firmware RAM and are delivered over the firmware message channel, not through these
   SoC registers (`ulw/phase6/message-fields.md` S2.3).
5. Driver code touches **7 of the 72 windows** by explicit offset (all in the `soc_register`/glue group);
   the MAC base blocks are additionally mapped at runtime from a `.rodata` table. The other windows are
   read only by the diagnostic dumper.

---

## 1. All 72 windows

Group labels come from the dump header that precedes the window in file order (`A.1`). `ID` is the first
word of `soc`/MAC blocks (a hardware block identifier; see S3.1). `ch` = changed words (`A.2`).

| # | name (dump group) | start | end | size | ID | nz | ch | classification (evidence) |
|---:|---|---|---|---|---|---:|---:|---|
| 1 | CA0 offset loop | `0x00000000` | `0x00000030` | `0x34` | - | 13 | 0 | **CPU vector table, not MMIO** (`A.3`: all words are ARM `ldr pc,[pc,#imm]`/NOP + branch targets) |
| 2 | soc 0x40000000 eFuse/strap/pinmux (soc) | `0x40000000` | `0x40000660` | `0x664` | 0x101 | 65 | 4 | **static config + 4 live status words** (`A.2`: ch=4) |
| 3 | soc 0x40002000 PCIe early-init (soc) | `0x40002000` | `0x400024bc` | `0x4c0` | 0x102 | 44 | 0 | **static config** (`A.2`: ch=0) |
| 4 | soc 0x40003000 (soc) | `0x40003000` | `0x40003444` | `0x448` | 0x112 | 39 | 0 | **static config** (`A.2`: ch=0) |
| 5 | soc 0x40004000 chip status temp/lock (soc) | `0x40004000` | `0x40004250` | `0x254` | 0x103 | 11 | 0 | **static config** (`A.2`: ch=0; `temp`@0x4110, `lock_status`@0x4208) |
| 6 | soc 0x40030000 (soc) | `0x40030000` | `0x40030490` | `0x494` | 0x105 | 37 | 1 | **static config + 1 live word** (`A.2`: ch=1) |
| 7 | soc 0x40031000 (soc) | `0x40031000` | `0x40031190` | `0x194` | 0x10e | 9 | 0 | **static config** (`A.2`: ch=0) |
| 8 | soc 0x40039000 PCIe0 glue/L1SS (soc) | `0x40039000` | `0x40039558` | `0x55c` | 0x10b | 23 | 1 | **static config + 1 live word** (`A.2`: ch=1) |
| 9 | soc 0x40039800 PCIe1 glue/L1SS (soc) | `0x40039800` | `0x40039d58` | `0x55c` | 0x10c | 22 | 1 | **static config + 1 live word** (`A.2`: ch=1) |
| 10 | soc 0x4003a000 remap/ETE + live (soc) | `0x4003a000` | `0x4003ac30` | `0xc34` | 0x10a | 113 | 24 | **counter/status bank** (`A.2`/`A.4`: replicated pairs @0x418/0x438, 4-word run @0x848) |
| 11 | 2g MAC 0x40040000 block header (2g_mac) | `0x40040000` | `0x400400ac` | `0xb0` | 0x100 | 15 | 0 | **static config** (`A.2`: ch=0) |
| 12 | 2g MAC 0x40042000 (2g_mac) | `0x40042000` | `0x40042928` | `0x92c` | - | 127 | 0 | **static config** (`A.2`: ch=0; repeated constant `0xab641c73`, S3.2) |
| 13 | 2g MAC 0x40044000 (2g_mac) | `0x40044000` | `0x40044abc` | `0xac0` | - | 128 | 7 | **counter bank** (`A.4`: 4-word run @0x44 `0x0->0xb`) |
| 14 | 2g MAC 0x40046000 (2g_mac) | `0x40046000` | `0x40046ffc` | `0x1000` | - | 25 | 0 | **static config** (`A.2`: ch=0) |
| 15 | 2g MAC 0x40048000 replicated config (2g_mac) | `0x40048000` | `0x40048e4c` | `0xe50` | - | 120 | 0 | **static config** (`A.2`: ch=0; `0x02050260`x32, `0x708`x32) |
| 16 | 2g MAC 0x4004a000 (2g_mac) | `0x4004a000` | `0x4004ae5c` | `0xe60` | - | 3 | 0 | **static config** (`A.2`: ch=0) |
| 17 | 2g MAC 0x4004c000 (2g_mac) | `0x4004c000` | `0x4004c920` | `0x924` | - | 53 | 6 | **counter bank** (`A.4`: 3-word run @0x00 `0x1->0x4`) |
| 18 | 2g MAC 0x40050000 (2g_mac) | `0x40050000` | `0x40050040` | `0x44` | - | 8 | 0 | **static config** (`A.2`: ch=0) |
| 19 | 2g MAC 0x40052000 16-deep bank (2g_mac) | `0x40052000` | `0x400522d0` | `0x2d4` | - | 36 | 34 | **counter bank** (`A.4`: 16 identical words @0x26c) |
| 20 | 2g MAC 0x40054000 (2g_mac) | `0x40054000` | `0x4005418c` | `0x190` | - | 52 | 6 | **counter bank** (`A.4`: 3-word run @0x14) |
| 21 | 5g MAC 0x40060000 block header (5g_mac) | `0x40060000` | `0x400600ac` | `0xb0` | 0x100 | 15 | 0 | **static config** (`A.2`: ch=0) |
| 22 | 5g MAC 0x40062000 (5g_mac) | `0x40062000` | `0x40062928` | `0x92c` | - | 127 | 0 | **static config** (`A.2`: ch=0; `0xab641c75`, S3.2) |
| 23 | 5g MAC 0x40064000 (5g_mac) | `0x40064000` | `0x40064abc` | `0xac0` | - | 131 | 8 | **counter bank** (`A.4`: 4-word run @0x44) |
| 24 | 5g MAC 0x40066000 (5g_mac) | `0x40066000` | `0x40066ffc` | `0x1000` | - | 25 | 0 | **static config** (`A.2`: ch=0) |
| 25 | 5g MAC 0x40068000 replicated config (5g_mac) | `0x40068000` | `0x40068e4c` | `0xe50` | - | 120 | 0 | **static config** (`A.2`: ch=0; `0x02050980`x32, `0x1770`x32) |
| 26 | 5g MAC 0x4006a000 (5g_mac) | `0x4006a000` | `0x4006ae5c` | `0xe60` | - | 3 | 0 | **static config** (`A.2`: ch=0) |
| 27 | 5g MAC 0x4006c000 (5g_mac) | `0x4006c000` | `0x4006c920` | `0x924` | - | 52 | 2 | **static config + 2 live words** (`A.2`: ch=2) |
| 28 | 5g MAC 0x40070000 (5g_mac) | `0x40070000` | `0x40070040` | `0x44` | - | 8 | 0 | **static config** (`A.2`: ch=0) |
| 29 | 5g MAC 0x40072000 16-deep bank (5g_mac) | `0x40072000` | `0x400722d0` | `0x2d4` | - | 38 | 38 | **counter bank** (`A.4`: 16 identical words @0x268) |
| 30 | 5g MAC 0x40074000 (5g_mac) | `0x40074000` | `0x4007418c` | `0x190` | - | 54 | 11 | **counter bank** (`A.4`: monotonic runs @0x14/0x30) |
| 31 | 2g PHY 0x40080000 (2g_phy) | `0x40080000` | `0x4008001c` | `0x20` | - | 3 | 0 | **static config** (`A.2`: ch=0) |
| 32 | 2g PHY 0x40080800 dense config (2g_phy) | `0x40080800` | `0x40080de0` | `0x5e4` | - | 289 | 0 | **static config** (`A.2`: ch=0) |
| 33 | 2g PHY 0x40081000 2x32-deep bank (2g_phy) | `0x40081000` | `0x400813f8` | `0x3fc` | - | 157 | 64 | **counter bank** (`A.4`: 32+32 identical words @0x200/@0x280) |
| 34 | 2g PHY 0x40081800 RF chain words (2g_phy) | `0x40081800` | `0x40081c70` | `0x474` | - | 236 | 1 | **static config + 1 live word** (`A.2`: ch=1; repeated `0x85907782`) |
| 35 | 2g PHY 0x40082000 (2g_phy) | `0x40082000` | `0x4008253c` | `0x540` | - | 287 | 19 | **counter bank** (`A.4`: 7-word run @0x4c0) |
| 36 | 2g PHY 0x40082800 (2g_phy) | `0x40082800` | `0x400829ec` | `0x1f0` | - | 89 | 0 | **static config** (`A.2`: ch=0) |
| 37 | 2g PHY 0x40083000 (2g_phy) | `0x40083000` | `0x400833e8` | `0x3ec` | - | 45 | 0 | **static config** (`A.2`: ch=0) |
| 38 | 2g PHY 0x40083800 (2g_phy) | `0x40083800` | `0x400839b0` | `0x1b4` | - | 14 | 7 | **counter bank** (`A.4`: 5-word run @0x154) |
| 39 | 2g PHY 0x40090000 (2g_phy) | `0x40090000` | `0x40090004` | `0x8` | - | 1 | 0 | **static config** (`A.2`: ch=0) |
| 40 | 2g PHY 0x40090800 8-deep stat bank (2g_phy) | `0x40090800` | `0x40090ba4` | `0x3a8` | - | 198 | 11 | **counter bank** (`A.4`: 8-word run @0x1a8) |
| 41 | 2g PHY 0x40091000 (2g_phy) | `0x40091000` | `0x40091604` | `0x608` | - | 96 | 1 | **static config + 1 live word** (`A.2`: ch=1; `0x67776777`x48) |
| 42 | 2g PHY 0x40091800 mask pattern (2g_phy) | `0x40091800` | `0x40091bb4` | `0x3b8` | - | 234 | 0 | **static config** (`A.2`: ch=0; `0x0f0f0f0f`x76) |
| 43 | 2g PHY 0x400a0000 (2g_phy) | `0x400a0000` | `0x400a03f0` | `0x3f4` | - | 38 | 1 | **static config + 1 live word** (`A.2`: ch=1) |
| 44 | 2g PHY 0x400a0400 (2g_phy) | `0x400a0400` | `0x400a07ec` | `0x3f0` | - | 213 | 1 | **static config + 1 live word** (`A.2`: ch=1) |
| 45 | 2g PHY 0x400a0800 (2g_phy) | `0x400a0800` | `0x400a0b58` | `0x35c` | - | 62 | 0 | **static config** (`A.2`: ch=0) |
| 46 | 2g PHY 0x400a0c00 (2g_phy) | `0x400a0c00` | `0x400a0ff8` | `0x3fc` | - | 142 | 0 | **static config** (`A.2`: ch=0) |
| 47 | 5g PHY 0x400b0000 (5g_phy) | `0x400b0000` | `0x400b0028` | `0x2c` | - | 3 | 0 | **static config** (`A.2`: ch=0) |
| 48 | 5g PHY 0x400b0800 dense config (5g_phy) | `0x400b0800` | `0x400b0de0` | `0x5e4` | - | 287 | 0 | **static config** (`A.2`: ch=0) |
| 49 | 5g PHY 0x400b1000 (5g_phy) | `0x400b1000` | `0x400b13f8` | `0x3fc` | - | 141 | 0 | **static config** (`A.2`: ch=0) |
| 50 | 5g PHY 0x400b1800 RF chain words (5g_phy) | `0x400b1800` | `0x400b1c70` | `0x474` | - | 235 | 2 | **static config + 2 live words** (`A.2`: ch=2; `0x72787380` chain) |
| 51 | 5g PHY 0x400b2000 (5g_phy) | `0x400b2000` | `0x400b253c` | `0x540` | - | 291 | 28 | **counter bank** (`A.4`: 9-word run @0x4b8) |
| 52 | 5g PHY 0x400b2800 (5g_phy) | `0x400b2800` | `0x400b29ec` | `0x1f0` | - | 89 | 0 | **static config** (`A.2`: ch=0) |
| 53 | 5g PHY 0x400b3000 (5g_phy) | `0x400b3000` | `0x400b33e8` | `0x3ec` | - | 33 | 0 | **static config** (`A.2`: ch=0) |
| 54 | 5g PHY 0x400b3800 (5g_phy) | `0x400b3800` | `0x400b39b0` | `0x1b4` | - | 21 | 8 | **counter bank** (`A.4`: 3-word run @0x130) |
| 55 | 5g PHY 0x400c0000 (5g_phy) | `0x400c0000` | `0x400c0014` | `0x18` | - | 2 | 0 | **static config** (`A.2`: ch=0) |
| 56 | 5g PHY 0x400c0800 8-deep stat bank (5g_phy) | `0x400c0800` | `0x400c0ba4` | `0x3a8` | - | 199 | 14 | **counter bank** (`A.4`: 8-word run @0x1a8) |
| 57 | 5g PHY 0x400c1000 (5g_phy) | `0x400c1000` | `0x400c1604` | `0x608` | - | 98 | 1 | **static config + 1 live word** (`A.2`: ch=1) |
| 58 | 5g PHY 0x400c1800 mask pattern (5g_phy) | `0x400c1800` | `0x400c1bb4` | `0x3b8` | - | 234 | 0 | **static config** (`A.2`: ch=0; `0x0f0f0f0f`x76) |
| 59 | 5g PHY 0x400d0000 (5g_phy) | `0x400d0000` | `0x400d03f0` | `0x3f4` | - | 112 | 0 | **static config** (`A.2`: ch=0) |
| 60 | 5g PHY 0x400d0400 (5g_phy) | `0x400d0400` | `0x400d07ec` | `0x3f0` | - | 225 | 1 | **static config + 1 live word** (`A.2`: ch=1) |
| 61 | 5g PHY 0x400d0800 (5g_phy) | `0x400d0800` | `0x400d0b58` | `0x35c` | - | 194 | 0 | **static config** (`A.2`: ch=0) |
| 62 | 5g PHY 0x400d0c00 (5g_phy) | `0x400d0c00` | `0x400d0ff8` | `0x3fc` | - | 141 | 0 | **static config** (`A.2`: ch=0) |
| 63 | soc 0x40100000 (soc) | `0x40100000` | `0x40100638` | `0x63c` | 0x104 | 55 | 0 | **static config** (`A.2`: ch=0) |
| 64 | soc 0x40101000 TCXO/PLL + MAC msg (soc) | `0x40101000` | `0x40101930` | `0x934` | 0x10d | 79 | 0 | **static config** (`A.2`: ch=0; `tcxo_pll_*`@0x230/0x234, msg regs) |
| 65 | 2g SOC 0x40108000 (2g_soc) | `0x40108000` | `0x40108618` | `0x61c` | 0x107 | 34 | 3 | **static config + 3 live words** (`A.2`: ch=3, small AGC-like deltas) |
| 66 | 2g RF/ABB 0x40109000 (2g_rf_abb) | `0x40109000` | `0x40109098` | `0x9c` | - | 29 | 0 | **static config** (`A.2`: ch=0) |
| 67 | 2g RF/ABB 0x4010a000 calib-like (2g_rf_abb) | `0x4010a000` | `0x4010ad18` | `0xd1c` | - | 621 | 0 | **static config** (`A.2`: ch=0; byte-duplicated table, S3.3) |
| 68 | 5g SOC 0x4010c000 (5g_soc) | `0x4010c000` | `0x4010c618` | `0x61c` | 0x106 | 38 | 3 | **static config + 3 live words** (`A.2`: ch=3) |
| 69 | 5g RF/ABB 0x4010d000 (5g_rf_abb) | `0x4010d000` | `0x4010d104` | `0x108` | - | 56 | 0 | **static config** (`A.2`: ch=0) |
| 70 | 5g RF/ABB 0x4010e000 calib-like (5g_rf_abb) | `0x4010e000` | `0x4010ef20` | `0xf24` | - | 693 | 2 | **static config + 2 live words** (`A.2`: ch=2; signature `0x7778 0x7677`) |
| 71 | 2g SOC 0x40110000 (2g_soc) | `0x40110000` | `0x40110a34` | `0xa38` | 0x109 | 32 | 0 | **static config** (`A.2`: ch=0) |
| 72 | 5g SOC 0x40114000 (5g_soc) | `0x40114000` | `0x4011480c` | `0x810` | 0x108 | 30 | 0 | **static config** (`A.2`: ch=0) |

Classification totals: **static/config-only 41**, **static config + live words 15**, **counter/status bank
15** (14 replicated/monotonic counter banks + the window-10 glue live block), **CPU vector table 1**.
No window classified as RAM; none left unknown (every window has a dump header plus a diff signature).

Structural note (`A.5`): the 5g MAC/PHY window set is a near-exact **address mirror** of the 2g set at
`+0x20000` (MAC) and `+0x30000` (PHY). Word-for-word mirror holds for e.g. window 42 vs 58 (238/238 words
identical) and window 12 vs 22 (only the first word differs); window 13 vs 23 = 226/256 identical,
window 32 vs 48 = 145/256. So the "5g" blocks are the same hardware design re-instantiated one band up.

---

## 2. Counter banks (the 310 moving addresses)

The 310 changed words form 106 contiguous runs; grouping by constant stride gives the banks below
(`A.4`). "replicated" means every word in the bank holds the *same* value and moves by the same delta,
i.e. one counter broadcast into N per-queue/per-lane slots.

| bank | base | stride | count | run1 -> run2 (first word) | plausible meaning |
|---|---|---:|---:|---|---|
| 2g MAC per-queue count/timestamp | `0x4005226c` | 4 | 16 (+1 variant @0x2a8) | `0x2b34 -> 0x2b5d` | 16-deep per-queue (16 TID/QoS queues) value, all slots identical; the +1 slot differs (`0x100010b -> 0x100000c`) |
| 5g MAC per-queue count/timestamp | `0x40072268` | 4 | 16 (+1 variant @0x2a8) | `0x147825 -> 0x14e2a7` | same 16-deep per-queue bank for the 5 GHz MAC |
| 2g MAC free-running timestamp | `0x40052038` | 4 | 5 (also @0x5c,0x70,0x90) | `0x30879 -> 0x3025d` | free-running / TSF-like counter (value *decreases* => wraps or is a "time to next" countdown) |
| 5g MAC free-running timestamp | `0x40072038` | 4 | 10 (also @0x3c-0x5c) | `0x2ef06 -> 0x2f1e7` | free-running counter, *increases* between runs |
| 2g MAC small counters | `0x40044044` | 4 | 4 | `0x0 -> 0xb` | monotonic small counters (TX/RX event or error counts) |
| 5g MAC small counters | `0x40064044` | 4 | 4 | `0x4 -> 0xf` | mirror of the above in the 5 GHz MAC |
| 2g MAC small counters (2) | `0x4004c000` | 4 | 3 | `0x1 -> 0x4` | monotonic counters at block base |
| 2g PHY RX/TX statistic bank A | `0x40081200` | 4 | 32 | `0xa6bf0 -> 0xa494d` | 32 lanes/queues, same value in all 32 - RX or TX byte/packet statistic |
| 2g PHY RX/TX statistic bank B | `0x40081280` | 4 | 32 | `0x8493d -> 0x84ab7` | second 32-deep bank immediately after bank A |
| 2g PHY per-chain stats | `0x400909a8` | 4 | 8 | `0x13d5 -> 0x3029` | per-antenna/per-chain counters (last 3 words are packed 16-bit pairs) |
| 5g PHY per-chain stats | `0x400c09a8` | 4 | 8 | `0xfffc -> 0xfe5a` | mirror of the 2g per-chain bank (can be negative-going: AGC/gain or RSSI) |
| 5g PHY small counters | `0x400b24b8` | 4 | 9 | `0xa9 -> 0xab` | monotonic small counters (queue/BF/CCA events) |
| 2g PHY small counters | `0x400824c0` | 4 | 7 | `0xc3 -> 0xc1` | mirror of the 5g small counters (here decreasing) |
| 2g PHY counter | `0x40083954` | 4 | 5 | `0x1000000 -> 0x0` | 5-word run, non-monotonic => status/flags rather than a pure counter |
| 5g PHY counter | `0x400b3930` | 4 | 3 | `0xffff03fb -> 0xffff040e` | 3-word run, increments by 0x13 |
| soc/glue live block (window 10) | `0x4003a418` | 4 | 2 pairs + 4-word run @0x848 | `0x1d -> 0x41c` | glue/ETE queue pointers or event counters in the `0x4003a000` block |
| 2g SOC AGC-like readbacks | `0x40108244` | - | 3 isolated | `0xf5 -> 0xf3` | small per-chain gain/RSSI readback (window 65, `0x244/0x274/0x40c`) |
| 5g SOC AGC-like readbacks | `0x4010c254` | - | 3 isolated | `0xf5 -> 0x9c` | mirror of the 2g SOC readback (window 68) |

Two asymmetries are worth recording:

* The 64-deep 2g PHY bank at `0x40081200` has **no 5g counterpart that moved** - window 49
  (`0x400b1000`, the 5g mirror) has `ch=0`. Either those 64 words are 2g-only PHY statistics, or the
  5 GHz chain was idle during both captures.
* The 5g MAC windows moved in *more* places than the 2g ones (38 vs 34 in the 16-deep windows,
  11 vs 6 in window 20 vs 30), i.e. the two bands carry independent traffic; the bank *structure* is
  identical but the counts are not.

---

## 3. Static-configuration windows: notable values and correlation with known settings

### 3.1 Block-ID words (all `soc`/MAC base blocks)

The first word of each `soc` block is a small sequential hardware block identifier, not a normal register
(`A.1`): `0x40000000=0x101, 0x40002000=0x102, 0x40004000=0x103, 0x40100000=0x104, 0x40030000=0x105,
0x4010c000=0x106, 0x40108000=0x107, 0x40114000=0x108, 0x40110000=0x109, 0x4003a000=0x10a,
0x40039000=0x10b, 0x40039800=0x10c, 0x40101000=0x10d, 0x40031000=0x10e, 0x40003000=0x112`, plus the two
MAC base blocks `0x40040000=0x100, 0x40060000=0x100`. This is the single most useful "is my mapping right"
check: if a window's first word is not in `0x100..0x112` the window was read through a wrong mapping.

### 3.2 Named/known registers found in the windows

| address | window | value | correlation |
|---|---|---:|---|
| `0x400002a8` | 2 | (live) | `efuse_chip_id` - low 7 bits compared against 5 by `exception_pcie1_link_down` (phase-6 S3.6) |
| `0x400002d4` | 2 | (live) | `dcoldo_efuse` - bits[7:5] DC-DC LDO trim |
| `0x40000554`, `0x400005bc` | 2 | | PCIe L1SS pinmux (set bits 5/9; function select = 6) |
| `0x40004110` | 5 | (live) | `temp` - die temperature |
| `0x40004208` | 5 | (live) | `lock_status` - PLL lock status |
| `0x40039224` | 8 | | `pcie0_status` |
| `0x40039220`, `0x400392d0` | 8 | | PCIe0 L1SS set/clear latches (`0x40`/`7`, clear ORs `0x80`/`0x100`) |
| `0x40039010/14/2d4/2f0` | 8 | | PCIe host<->device message registers 0/1/2/5 |
| `0x40039a24` | 9 | | `pcie1_status` (bits[14:9] decoded by `exception_pcie1_link_down`) |
| `0x40039a20`, `0x40039ad0` | 9 | | PCIe1 L1SS latches |
| `0x4003a200` | 10 | 0 | inbound-remap enable (written to 1 by `shuangta_pcie_enable_remap`) |
| `0x40101230`, `0x40101234` | 64 | | `tcxo_pll_mux_sel`, `tcxo_pll_status` |
| `0x40101414`, `0x40101438` | 64 | | MAC-side PCIe message registers |
| `0x40000004` (=`0x110`) | 2 | `0x110` | second block-ID-like word (boards/straps?) |
| `0x40000108`, `0x4000010c` | 2 | `0x5a5a`, `0xdeaf` | firmware-download magic/identity words |
| `0x400485ec`, `0x400685e8` | 15, 25 | `0x19181716`, `0x17161514` | descending byte-sequence identity markers (0x14,0x15,0x16,0x17 / 0x16..0x19), MAC/PHY block tags |

### 3.3 The `iwpriv` power/calibration tables are NOT in these windows

Requested search: the 26-word 2.4 GHz power table printed by
`iwpriv Hisilicon0 alg get_2g_power_param` = `17161605 17161605 17161605 17161505 17161505 15161401
15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002
0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff`
(`ulw/phase6/message-fields.md` S2.1/S2.3).

Command (Appendix A.6) and result:

```
$ for w in 17161605 17161505 15161401 15161400 08141200 0c111103 0b101002 0b101001 0a0f0f01 0a0606ff; do
    grep -ic "value = $w" reg_all.txt; done
0    (every tuple word -> zero occurrences)
```

The 5g power table and the 2g/5g "all curve" tables are likewise absent (curve words `0x78420492`,
`0x2df042d7` => 0 hits). **Conclusion: the calibration/power tables are not exposed in any of the 72
windows.** They live in firmware RAM and are returned over the firmware message channel
(cfg_id `0x0db0`, payload `+0x8a`), which is exactly what `message-fields.md` established. So a driver
cannot read back its own per-rate power calibration from this register dump - it must ask the firmware.

What the dump *does* contain is calibration-like raw material, all static across the two runs:

* window 67 (2g RF/ABB, `0x4010a000`): byte-duplicated words `0x1a1a` x20, `0x1d1d` x14, `0x1c1c` x14,
  i.e. a per-chain RF/ABB trim table. Ends with signature `0x8591 0x7782`.
* window 70 (5g RF/ABB, `0x4010e000`): `0x2c2c` x58, `0x8888` x30, `0x1248` x24; ends `0x7778 0x7677`.
* window 66/69 (RF/ABB scratch): small trim words (`0x2080`, `0xcd5d`; `0x2000`, `0x8000`).
* window 34/50: RF chain config chains `0x85907782...` / `0x72787380...`.
* windows 42/58: `0x0f0f0f0f` x76 - a nibble bit-mask template (identical in 2g and 5g).

These are the *RF front-end trim and chain configuration* registers, not the rate/power table. They are
read-only observables of what the firmware programmed at bring-up.

---

## 4. Driver-touched windows vs dumper-only windows

Cross-referenced against `ulw/phase6/register-windows.md` S2/S4 and `ulw/phase4/mmio-map.md` S2.

### 4.1 Windows with an explicit per-offset driver consumer (7 of 72)

| window(s) | offsets consumed | consumer (module) | access |
|---|---|---|---|
| 2 `0x40000000` | `+0x2a8`, `+0x2d4`, `+0x554`, `+0x5bc`, `+0x108` | `exception_pcie1_link_down`, `dev_status_check`, `pcie_l1ss_dev_pinmux_set`, `firmware_download_function` (plat) | R (0x2a8/0x2d4), RMW (0x554), W (0x5bc) |
| 3 `0x40002000` | `+0x2210` | `pcie_main_init` (plat, `.text.unlikely`) | RMW (bits[5:0] kept, bits[8:7] := 11) |
| 5 `0x40004000` | `+0x4110`, `+0x4208` | `dev_status_check` (plat) | R |
| 8 `0x40039000` | `+0x10`,`+0x14`,`+0x220`,`+0x224`,`+0x2d0`,`+0x2d4`,`+0x2f0` | `shuangta_pcie_msg_reg_map`, `shuangta_pcie_l1ss_set/_clear` (plat) | R, W (0x40 / 7), RMW OR 0x80/0x100 |
| 9 `0x40039800` | `+0x220`,`+0x224`,`+0x2d0` | same, endpoint 1 | R, W, RMW |
| 10 `0x4003a000` | `+0x200` | `shuangta_pcie_enable_remap` (plat) | W (constant 1) |
| 64 `0x40101000` | `+0x230`,`+0x234`,`+0x414`,`+0x438` | `dev_status_check`, `shuangta_pcie_msg_reg_map` (plat) | R |

Evidence: every row above is a disassembly excerpt or the `.rodata+0xcac` `{name,ca}` table in
`register-windows.md` S3.6 (reproduced by its Appendix A.2/A.4). All are in the **`soc_register`/glue
group** - i.e. the driver's real MMIO work is chip bring-up and PCIe, not the radio.

### 4.2 Windows mapped but only touched at the block base / a few offsets

The MAC block list is a static `.rodata` table (`.rodata+0xb60`, 10 CAs
`0x40040000,0x40042000,0x40044000,0x40046000,0x40048000,0x4004a000,0x4004c000,0x40050000,0x40052000,
0x40054000`; `mmio-map.md` S2.2) copied into the per-chip struct at `[dev+0x14c]`. Offsets inside a mapped
MAC block that the driver explicitly touches are `+0x00` (host-MAC interrupt status), `+0x44` (clear),
`+0x48` (mask) and `+0xa8` (TX FCS error-inject enable), all in windows 11/21. The `.rodata+0xa80`/
`.rodata+0xbb0` tables additionally name `0x4006xxxx`, `0x40072000`, `0x40074000` and the whole
`0x4009/0x400a/0x400b/0x400c/0x400d` PHY range as MAC/PHY register blocks used during init. So these
windows **are** reachable by the driver, but no per-offset consumer was resolved for them in phase 4/6.

### 4.3 Dumper-only windows (no driver consumer found)

* `0x00000000..0x30` (window 1, the CA-0 loop; see S0.3),
* `0x40003000` (4), `0x40030000` (6), `0x40031000` (7), `0x40100000` (63),
* all eight `shuangta_read_all_reg_info` windows: `0x40108000` (65), `0x40109000` (66),
  `0x4010a000` (67), `0x4010c000` (68), `0x4010d000` (69), `0x4010e000` (70), `0x40110000` (71),
  `0x40114000` (72),
* the whole 2g/5g PHY window set (31-62) except where the runtime tables in S4.2 apply.

This is 55+ of the 72 windows: they exist in the dump purely because `hmac_config_get_all_reg_value` /
`shuangta_read_all_reg_info` sweep them. That is precisely why the dump is useful *as a diagnostic* and
useless as a "driver register map": over three quarters of the addresses have no static consumer in the
two `.ko` files, and none of their bits are named.

---

## 5. Summary: what a driver must configure vs what the firmware maintains

**Driver-owned (must be configured/written by host code)** - all in the `soc_register`/PCIe glue group:

1. PCIe early init: `0x40002210` (refclk/PLL/power-up strap, RMW).
2. PCIe L1SS latches: `0x40039220`, `0x400392d0`, `0x40039a20`, `0x40039ad0` (set/clear).
3. PCIe inbound-address remap: `0x4003a200` (write 1).
4. L1SS pinmux: `0x40000554` (RMW bits 5/9) and `0x400005bc` (write 6).
5. Message registers used to talk to the MAC side: `0x40039010/14`, `0x400392d4/f0`,
   `0x40101414/1438`.
6. nvRAM/eFuse-dependent workarounds read from `0x400002a8` (chip id) and `0x400002d4` (LDO trim).
7. MAC interrupt control at MAC-base `+0x00/+0x44/+0x48` and TX error-injection at `+0xa8`.

**Firmware-maintained (host code only reads, or never touches)**:

* Everything the two dumps show as a **counter bank** (S2): the 2g/5g per-queue and per-lane
  statistic counters in windows 13,17,19,20,23,29,30,33,35,38,40,51,54,56 and the glue live block
  (window 10). A driver may read these for diagnostics; it must not write them.
* The entire **RF/ABB trim and PHY configuration** content (windows 32,34,36,42,44,48,50,58,66,67,69,70):
  the firmware programs these at bring-up from its own calibration files; the host has no register
  access path to them and (per `mmio-map.md` S2.5) no calibration routine in either `.ko` calls any
  MMIO accessor.
* The **per-rate power/curve tables**: not present in any window at all (S3.3). They are firmware RAM,
  fetched over the message channel (`get_2g_power_param` etc.).
* The block-ID/status words, `temp`, `lock_status`, `pcie0/1_status`, `tcxo_pll_*`: read-only status.

**Rule of thumb for this chip:** if a window's words move between two dumps, it is firmware/live state
(read-only to the driver). If it is static *and* appears in S4.1, it is driver configuration. If it is
static and does not appear in S4.1 (the large majority - all PHY/RF windows), it is firmware-owned
register state that only the diagnostic dumper reads.

---

## Appendix A - reproduction commands

All scripts run with `C:/Users/ShibbityShwab/router-openwrt/pyenv/Scripts/python.exe` from
`C:/Users/ShibbityShwab/router-openwrt`, reading `build/register-dumps/reg_all.txt` (`a`) and
`build/register-dumps/reg_all_run2.txt` (`b`).

### A.1 Parse + window/header/resize scan

```python
import re
def parse(p):
    d={}; hdr={}; cur=None
    for line in open(p):
        line=line.strip()
        if line.startswith('addr = '):
            m=re.match(r'addr = ([0-9a-f]+), value = ([0-9a-f]+)',line)
            a=int(m.group(1),16); d[a]=int(m.group(2),16); hdr[a]=cur
        else: cur=line
    return d,hdr
a,ha=parse('build/register-dumps/reg_all.txt')
addrs=sorted(a); runs=[]; s=addrs[0]; prev=addrs[0]
for x in addrs[1:]:
    if x!=prev+4: runs.append((s,prev)); s=x
    prev=x
runs.append((s,prev))
for i,(s,e) in enumerate(runs,1):
    print(i,hex(s),hex(e),hex(e-s+4),ha.get(s))
```

### A.2 Diff -> changed words per window

```python
b,_=parse('build/register-dumps/reg_all_run2.txt')
for i,(s,e) in enumerate(runs,1):
    ch=[x for x in range(s,e+1,4) if a[x]!=b[x]]
    print(i,hex(s),'changed',len(ch))
```

### A.3 Window-1 (vector table) decode

```python
for x in range(0,0x34,4): print(hex(x),hex(a[x]))
# 0xe59ff018 = ldr pc,[pc,#0x18]; 0xe320f000 = nop; 0x8c/0x3c/0x4c/0x5c/0x6c = branch targets
```

### A.4 Counter-bank extraction (per window: changed offsets + constant-stride grouping)

```python
for i,(s,e) in enumerate(runs,1):
    ch=[x for x in range(s,e+1,4) if a[x]!=b[x]]
    if not ch: continue
    start=ch[0]; prev=ch[0]; st=None; segs=[]
    for x in ch[1:]:
        d=x-prev
        if st is None: st=d
        if d==st: prev=x
        else: segs.append((start,prev,st)); start=x; prev=x; st=None
    segs.append((start,prev,st))
    for gs,ge,gst in segs:
        n=(ge-gs)//(gst or 4)+1
        print(hex(gs),hex(ge),'stride',gst,'n',n,hex(a[gs]),'->',hex(b[gs]))
```

### A.5 Mirror check (2g vs 5g)

```python
for a0,b0 in ((0x40044000,0x40064000),(0x40048000,0x40068000),(0x40080800,0x400b0800),
              (0x40091800,0x400c1800),(0x400a0400,0x400d0400)):
    same=diff=0
    for off in range(0,0x400,4):
        if (a0+off) in a and (b0+off) in a:
            same += a[a0+off]==a[b0+off]; diff += a[a0+off]!=a[b0+off]
    print(hex(a0),hex(b0),same,diff)
```

### A.6 The `iwpriv` power-table tuple search (negative result)

```bash
for w in 17161605 17161505 15161401 15161400 08141200 0c111103 0b101002 0b101001 0a0f0f01 0a0606ff; do
  echo -n "$w "; grep -ic "value = $w" build/register-dumps/reg_all.txt
done        # -> 0 for every word
```

```python
for w in (0x78420492,0x2df042d7):                       # 5g all-curve words
    print(hex(w),[hex(x) for x in sorted(a) if a[x]==w]) # -> []
```

### A.7 Block-ID and notable-value scan

```python
import collections
for i,(s,e) in enumerate(runs,1):
    ws=[a[x] for x in range(s,e+1,4)]; nz=[v for v in ws if v]
    c=collections.Counter(nz)
    print(i,hex(s),'first',ws[:4],'distinct',len(c),'top',[(hex(v),n) for v,n in c.most_common(3)])
```
