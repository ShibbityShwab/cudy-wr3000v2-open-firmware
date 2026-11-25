# TABLES-window - the stock's endpoint/window/register addresses and the /dev/mem access rules

Read-only reference for the H2D trace retry (task-194). Everything here is address arithmetic over
committed decode; no vendor bytes. Grades as in REPORT.md.

## 1. The two PCIe functions (live vendor-boot mapping) [V]

Source: `phase21/sibling-ep.md:92-118`, `phase32/doorbell-endpoint-path.md:189-214`,
`phase22/rc-routing.md:220`.

| | EP0 | EP1 |
| --- | --- | --- |
| BDF | `0000:00:00.0` | `0001:00:00.0` |
| root complex | RC0 `10160000.pcie` (domain 0) | RC1 `10164000.pcie` (domain 1) |
| `cfg[0xff8]` / phy_devid | `0x00011521` / 1 | `0x00011520` / **0** |
| vendor `chip->dev[]` slot | `dev[1]` | **`dev[0]`** |
| BAR0 host base (CPU phys) | `0x40000000` | `0x58000000` |
| BAR2 (endpoint iATU) base | `0x41800000` | `0x59800000` |
| region-3 host base | `0x403b8000` | `0x583b8000` |
| INTx | 207 (0 deliveries) | **209 (live message ISR)** |
| used by | our port / PD4 image, and phase-25-style reads | **the stock vendor driver (H2D/ETE/doorbell)** |

## 2. CA -> host translation (region 3: CA base `0x40000000`) [V]

```
EP0:  host = 0x403b8000 + (CA - 0x40000000)
EP1:  host = 0x583b8000 + (CA - 0x40000000)
```

Both functions' region 3 targets CA `0x40000000`; the two windows alias the same on-chip
register instance (proved for the SR/DR ring block: `CA 0x4003a410` reads `0x83A52000` through
both windows on a live vendor boot, `phase21/sibling-ep.md:198-220`).

## 3. The ETE window and the SR ch0 registers, both windows [V]

ETE block CA `0x4003a000` -> window host `0x403f2000` (EP0) / **`0x583f2000` (EP1, the
stock's own)**. SR ch0 block = window `+0x400` (CA `0x4003a400`); ch1 `+0x50`, ch2 `+0xa0`
(`wifi-rx-h2d-trace/H2D-FORMATS.md:12-26`).

| register (SR ch0) | offset in window | EP0 CPU phys | EP1 CPU phys |
| --- | --- | --- | --- |
| ctrl | `+0x408` | `0x403f2408` | `0x583f2408` |
| **base (node array)** | `+0x410` | `0x403f2410` | **`0x583f2410`** |
| **depth-1** | `+0x414` | `0x403f2414` | **`0x583f2414`** |
| producer | `+0x418` | `0x403f2418` | `0x583f2418` |
| consumer | `+0x41c` | `0x403f241c` | `0x583f241c` |

Observed live values on a normal vendor boot: base `0x848F6000`/`0x83A52000` (per boot, in
DRAM), depth-1 `0x1f`, producer/consumer small counters (`phase25/live-vendor-ring-ground-truth.md:13-16`,
`phase21/sibling-ep.md:207`, `wifi-rx-h2d-trace/H2D-FORMATS.md:15-16`).

## 4. /dev/mem access rules on this 32-bit ARM kernel [V]

* `read()`/`pread()` on `/dev/mem` -> `read_mem()` -> **`valid_phys_addr_range(p, n)`**
  (`drivers/char/mem.c:127-128`), ARM impl `arch/arm/mm/mmap.c:151-159`:
  `addr < PHYS_OFFSET -> 0 -> -EFAULT`. **PHYS_OFFSET on the stock = `0x80600000`**
  (UART log line 6503 `Ignoring memory range 0x80500000 - 0x80600000`; RAM zone
  `0x80600000-0x87ffffff`, line 6508).
* `mmap()` on `/dev/mem` -> `mmap_mem()` -> `valid_mmap_phys_addr_range()`
  (`arch/arm/mm/mmap.c:171-175`: only checks PHYS_MASK) -> `remap_pfn_range()` -> ioremap.
  **This is how busybox `devmem` reads MMIO and why phase 17/23/25 could read `0x403fxxxx`.**
* Therefore:
  * **MMIO below `0x80600000` (both ETE windows): mmap only. `pread` returns -EFAULT.**
  * host DRAM `0x80600000-0x87ffffff` (ring nodes + message buffers): `pread` is fine and is
    what the sampler keeps using.

## 5. Read-only probe commands for the retry

```sh
# v2 sampler, read-only probe of the stock's own (EP1) window:
/usr/bin/omo_h2dsampler -p -w 0x583f2000
#   expect: SR0 base=0x8xxxxxxx depth=0x1f prod/cons present; ring=YES
#           and the v1 control: pread_control=EFAULT  (proves REPORT §2 on the device)
# optional alias check through EP0's window (same on-chip register):
/usr/bin/omo_h2dsampler -p -w 0x403f2000

# busybox equivalents (mmap-based devmem), for the T2.5 record:
devmem 0x583f2410      # SR0 base (EP1)
devmem 0x583f2414      # depth-1 (0x1f)
devmem 0x583f2418      # producer
devmem 0x403f2410      # same register through EP0 (alias check)
```

All of these are read-only. Do not touch the ETE window before `hi5622v100_plat` is listed
(`/proc/modules`) - the vendor PCIe probe has to have programmed the endpoint iATU first.

## 6. Regeneration (host-only)

```sh
# boot timeline + PHYS_OFFSET line (capture boot = U-Boot line 6491 onwards):
sed -n '6491,7760p' build/uart-logs/20261125T113518Z-uart-h2d-stock.log | tr -d '\000' | cat -v \
  | grep -aE 'Ignoring memory range|Zone ranges|kmodloader: done|PCIe:[01] switch|pcie_main_init|enable_remap|pcie_msg_init|firmware_download|Device plat ready|excp wifi callback|hostapd_setup: channel|reboot: Restarting'
# ring-init call chain (vendor plat.ko, committed decode tree):
grep -n 'pcie_ete_init\|pcie_ete_sr_init\|pcie_ete_sr_reg_init' build/wifi-rx-h2d-trace/hi5622v100_plat.nm.txt
grep -n 'bl.*<pcie_ete_init>\|bl.*<pcie_ete_sr_init>\|bl.*<pcie_ete_sr_reg_init>' build/wifi-rx-h2d-trace/hi5622v100_plat.dis
# ARM /dev/mem rules (vendor 5.10.201 tree):
sed -n '151,175p' build/linux-kbuild/linux-5.10.201/arch/arm/mm/mmap.c
sed -n '115,190p' build/linux-kbuild/linux-5.10.201/drivers/char/mem.c
```
