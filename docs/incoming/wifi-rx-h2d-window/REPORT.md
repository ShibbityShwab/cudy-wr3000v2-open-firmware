# REPORT - the stock H2D trace retry: why v1 saw no ring, the stock's own window, and the boot timing

**Lane:** `rx-h2d-window` (task-194). **Date:** 2026-11-28. **Sequel to:** `wifi-rx-h2d-trace`
(task-176), whose first session returned the honest negative `ring not seen within 120 s`
(`mem-entries.md:28340`; retry tasked at `mem-entries.md:28394`).
**Method:** host-only static decode of committed artifacts + the captured stock UART log; no
device contact. Addresses and decoded text only; no vendor binary/firmware bytes.

Grades: **[V]** = proven by code path/constant/live dump quoted here; **[I]** = inferred;
**[U]** = unmeasured. The one on-device confirmation this lane could not run is named in §6.

---

## 1. What the first session actually recorded (facts)

* The v1 sampler (`sha256 317d1f41…`, built from
  `wifi-rx-h2d-trace/tools/omo_h2dsampler.c`) ran from the S08 aid on the stock pair-A boot and
  waited for `hi5622v100_plat` + a non-zero SR ring at the hard-coded window **`0x403f2000`**
  (v1 source `:49`, `:314-328`).
* Its log ended **`E ts_ms=125851 msg=ring-not-seen wait_sec=120 module_loaded=1`**
  (`mem-entries.md:28340`) - the module *was* loaded, the ring registers read **base=depth=0**
  for the whole wait.
* The stock capture boot (last stock boot of
  `build/uart-logs/20261125T113518Z-uart-h2d-stock.log`, U-Boot line 6491 -> reboot line 7750)
  brought both radios up (hostapd radio0 `24.63 s`, radio1 `31.11 s`), so the ring was live
  and streaming while the sampler was polling.
* The task's working hypothesis was a window/timing mismatch ("the window was derived from OUR
  PD4 image"). §2 and §3 show the window claim is half right (EP0 vs EP1) but **the window was
  not the blocker**; §4 shows the timing was never a problem either.

## 2. ROOT CAUSE (decisive): `pread()` on `/dev/mem` cannot read MMIO below RAM on this kernel [V]

The ETE SR registers are MMIO, read by v1 as:

```
n = pread(mem_fd, buf, len, (off_t)phys);   /* v1 :89-96, mem_fd = open("/dev/mem", O_RDONLY) :296 */
if (n != len) return -1;                    /* the caller ignores the error :314-328 */
```

On 32-bit ARM that read never reaches the device. `/dev/mem` `read` goes through
`read_mem()`, which starts with:

```
if (!valid_phys_addr_range(p, count))   /* drivers/char/mem.c:127-128 */
        return -EFAULT;
```

and this kernel's ARM implementation is (verbatim, `arch/arm/mm/mmap.c:151-159`):

```
int valid_phys_addr_range(phys_addr_t addr, size_t size)
{
        if (addr < PHYS_OFFSET)
                return 0;
        ...
}
```

On the stock boot **PHYS_OFFSET = 0x80600000**, printed by the kernel itself:
`[0.000000] OF: fdt: Ignoring memory range 0x80500000 - 0x80600000` (UART log line 6503; the
warning is exactly `base < phys_offset` in `drivers/of/fdt.c:1141`), followed by
`Normal [mem 0x80600000-0x87ffffff]` (line 6508). **Every candidate window is below it**
(`0x403f2000` and `0x583f2000` are both < `0x80600000`), so `pread` returns `-EFAULT`
before any translation happens - for *any* `-w`. The sampler assigns the error to a
`return -1` that the wait loop does not inspect; `base`/`depth` stay 0, which is exactly the
observed `ring-not-seen` with `module_loaded=1`. **This is a silent-instrument bug, not a
stock-behaviour finding.**

Why the earlier "read-only /dev/mem mechanism was proven" (`wifi-rx-h2d-trace/README.md:12-13`)
did not cover this: phase 25's proof used **busybox `devmem`** (phase25
`live-vendor-ring-ground-truth.md:11`), which **mmap()s** `/dev/mem`; the mmap path uses the
separate `valid_mmap_phys_addr_range()` check (`arch/arm/mm/mmap.c:171-175`, "not beyond
PHYS_MASK" - MMIO passes) and `remap_pfn_range()` -> ioremap, so it works. The same window
read by `devmem` is quoted live in phase 17 (`fw-download.md:494`) and phase 23
(`ROOT-CAUSE-window-base.md:43`, `0x403f12e8 = 0x00000020`). **`read()` and `mmap()` on
`/dev/mem` are different code paths on ARM; v1 used the one that cannot reach MMIO.** The
`arch/arm/include/asm/io.h:431` manual (`#define xlate_dev_mem_ptr(p) __va(p)`) means even an
address *above* PHYS_OFFSET would only read the kernel's direct-map RAM alias, never the
device - so no `read()` outcome could ever yield ETE register contents.

**Consequence for the retry:** replacing `-w` alone cannot fix the trace. The instrument must
read the ETE window with **mmap()** (like `devmem`); pread stays correct only for the host-DRAM
ring nodes and message buffers, whose addresses (`0x8xxxxxxx`) are above PHYS_OFFSET.

## 3. The window, decoded on the stock's own terms: EP1 / `0x583f2000` [V]

The stock driver does not resolve the ETE CAs through EP0. `chip->dev[]` is keyed on
`cfg[0xff8] & 0xf` ("phy_devid"), and the live vendor boot prints the mapping
(`phase21/sibling-ep.md:92-118`, `phase32/doorbell-endpoint-path.md:12-23,189-214`):

| endpoint | domain | cfg[0xff8] | phy_devid | vendor slot | BAR0 | region-3 host base | ETE window |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0000:00:00.0` | 0 | `0x00011521` | 1 | `dev[1]` | `0x40000000` | `0x403b8000` | `0x403f2000` |
| `0001:00:00.0` | 1 | `0x00011520` | **0** | **`dev[0]`** | `0x58000000` | `0x583b8000` | **`0x583f2000`** |

* `chip->dev[0]` is the phy_devid-0 function = `0001:00:00.0`; the vendor's message/doorbell
  map (`pcie_msg_init -> shuangta_pcie_msg_reg_map`) and the ETE ring init resolve every device
  CA through `dev[0]->[4]`, i.e. **EP1's region-3 window**
  (`phase32/doorbell-endpoint-path.md:160-224`).
* The ETE block is CA `0x4003a000`; region 3 maps host `base` to CA `0x40000000`, and the
  vendor's region-3 offset is `0x3b8000` for both functions
  (`phase22/rc-routing.md:220`; live dmesg `(ep1) region idx3 paddr:0x583b8000` in
  `phase21/sibling-ep.md:222`). So `host = base + (CA - 0x40000000)`:
  `0x583b8000 + 0x3a000 = **0x583f2000**` (EP1), `0x403b8000 + 0x3a000 = 0x403f2000` (EP0).
* **Both functions alias the same on-chip register instance**: the dynamic SR0 base
  (`CA 0x4003a410`) reads `0x83A52000` through *both* windows on a live vendor boot
  (`phase21/sibling-ep.md:198-220`). So `0x403f2000` was not "wrong" for reading the
  registers - but it is *our* port/PD4 endpoint's window, and the stock's own init path is EP1.
  The retry should use `-w 0x583f2000`; `0x403f2000` is a documented equivalent (see
  TABLES-window.md). The vendor also enables the probe-time inbound remap
  (`0x4003a200 = 1`, `phase16/endpoint-init.md:110-127`), which is on in every normal stock
  boot and does not change the alias result above.

## 4. The timing, decoded from the stock UART log: ring at ~14.2 s, aid at ~10 s [V]

**Who writes the ring, and when.** `pcie_main_init` (plat.ko `:0x704`) calls, in order,
`oal_pcie_enable_remap` (`:0x8dc`), **`pcie_ete_init` (`:0x8e8`, the ETE/SR rings)** and
`pcie_comm_init -> pcie_msg_init` (`:0x944`) (`phase16/endpoint-init.md:110-130`). The ring
register write is `pcie_ete_init @0x7820 -> pcie_ete_init_src_ring @0x7068 -> pcie_ete_sr_init
@0x14b98 -> pcie_ete_sr_reg_init @0x14a48` (build/wifi-rx-h2d-trace/hi5622v100_plat.dis, call
sites `:0x7178` and `:0x14c2c`; phase 20 A.1 documented the base-write formula).
`pcie_main_init` runs inside plat.ko's module init, and `/proc/modules` shows a module only
after its init completes - so **when `module_loaded("hi5622v100_plat")` is first true, the ring
is already programmed**. The v1 wait loop's first poll after load should therefore have seen it.

**The boot timeline (capture boot = last stock boot, UART log lines 6491-7750).**

| boot t | event | UART line |
| --- | --- | --- |
| 9.763 s | `kmodloader: done` (rcS about to run S00…) | 6909 |
| ~9.8-10.5 s | **S08 aid starts the sampler** (stock rc.d has no S08; the aid is the S08 symlink; next stock step is S09hsan_prepare) | - |
| 11.701 s | `PCIe:0 switch to RC mode` (hi_pcie.ko from S15 `hsan_start` -> `wifi_init.sh`) | 6910 |
| 12.117 s | `hwifi_config_init` INI reads (plat.ko init starts) | 6953 |
| 14.097 s | `wlan_pm_open chip_id[0]` | 7130 |
| 14.104 s | `[PCIEL]pcie_main_init chip[0], bus[0]` | 7131 |
| 14.183 s | `oal_pcie_enable_remap:chip_id=0` | 7145 |
| 14.188-14.214 s | `pcie_msg_init` … `wlan_pm_init ok` (**ETE/SR ring already programmed here**) | 7146-7152 |
| 14.229-14.536 s | firmware download (`enter firmware_download_function` … success) | 7153-7158 |
| 14.557 s | `Device plat ready!` | 7161 |
| 15.477 s | `excp wifi callback register chip0` | 7192 |
| 24.631 / 31.115 s | hostapd radio0 / radio1 up | 7425 / 7500 |
| 294.374 s | reboot (to pair B) | 7750 |

**Reading:** the S15 module load is the stock's `START=15` `hsan_start` ->
`/usr/bin/wifi_init.sh` -> `insmod hi5622v100_plat.ko` (rootfs-2.5.24 `hsan_start:4,45`,
`wifi_init.sh:79-80`); the plat.ko probe that programs the ring runs **~4 s after the S08 aid**.
The v1 sampler's 120 s window had ~115 s of unused margin; **`-W` was never the problem**. The
proposed retry `-W 180` is only single-shot insurance (`-t 240` covers boot handshake + AP
start + steady stream; the failed session's `-t 180` would also have sufficed).

## 5. The v2 instrument (delivered in this directory)

`tools/omo_h2dsampler-mmap.c` is v1 with the minimum changes that matter:

1. **mmap() the ETE window** (one `0x2000` mapping of the page containing `-w`) and read the
   SR registers through it; `pread` is kept for the DRAM ring nodes and message buffers (valid
   addresses, above PHYS_OFFSET). **Default window is now `0x583f2000`** (the stock's own EP1
   window; `-w 0x403f2000` reproduces the v1/EP0 view).
2. **Ring validation**: accept a ring only when `base` is in the stock DRAM range
   (`0x80000000..0x87ffffff`) and `1 <= depth <= 1024`; log the last raw register values on
   failure (v1 logged nothing that could distinguish "not programmed" from "not readable", which
   is why this root cause was invisible).
3. **`-p` read-only probe mode**: map the window, print `SR ch0..2 ctrl/base/depth/prod/cons`
   and, as a control, one `pread()` of the same address with its `errno`. This is the T2.5 gate
   in RUNBOOK-DELTA.md: on the stock it should show a live DRAM base and **`pread_control=EFAULT
   (EINVAL-class)`**, confirming §2 on the device in one command.
4. **Error accounting**: `misreads=` in the heartbeat `E` lines, so a future instrument fault
   is visible in the log itself.

Log format is unchanged (the committed `tools/decode_h2d.py` keeps working); the version line
becomes `# omo_h2dsampler v2 … access=mmap`.

## 6. Honest limits [I]/[U]

* This lane touched no device. §2's `-EFAULT` is a **code-path proof + symptom match**; the
  v2 `-p` gate is the on-device confirmation, and it is step 1 of the retry.
* The EP1/EP0 table and the alias claim are quoted from committed live-vendor dumps
  (`phase21`, `phase22`, `phase32`), not re-measured here.
* "ETE window" = the CA `0x4003a000` block (SR ch0 regs at `+0x408..+0x41c`), i.e. the
  register window v1 was sampling. The HCC message *content* capture is still task-176's goal;
  this lane only fixes the instrument and the window/timing frame.
* The exact S08 start time is bounded by the rc ordering (after `kmodloader: done` 9.763 s,
  before S09/S15) and by the aid's own one-shot design; the retry log will carry exact
  `dt_wait_ms` (`E msg=ring-seen`), which makes it [V].
