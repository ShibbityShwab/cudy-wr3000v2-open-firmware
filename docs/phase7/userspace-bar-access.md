# Phase 7 - Direct userspace access to the Wi-Fi chip registers via the PCI BAR (read-only)

Device: Cudy WR3000 (`WR3000`), OpenWrt 22.03.6, `hisilicon/luofu`, `armv7l` / ARMv7 Cortex-A9,
kernel `Linux 5.10.201 #0 SMP`. PCI endpoints: `0000:00:00.0` and `0001:00:00.0`
(`Network controller [0280]: 59e7:0005`). Vendor driver bound: `rox_pci0` (module `hi5622v100_plat`,
Wi-Fi module `hi5622v100_wifi.ko`).

Reference dump: `build/register-dumps/reg_all.txt` (run 1) and `reg_all_run2.txt` (run 2), produced by
the vendor module's `shuangta_read_soc_to_file` / `shuangta_read_all_reg_info` loops. Their addresses are
**device chip addresses (CA)**, reached inside the driver by `oal_pcie_devca_to_hostva()`. The whole map
is CA `0x40000000..0x4011xxxx`.

**Result (short):** reachability and address translation are proven. For device `0000:00:00.0` the
register I/O window is the BAR0 sub-region named `SHUANGTA_REGION_IO`, host physical
`0x403b8000..0x404d7fff` = **resource0 offset `0x3b8000`**. The translation is

```
resource0_offset(CA) = 0x3b8000 + (CA - 0x40000000)      [CA in 0x40000000..0x4011ffff]
host_phys(CA)        = 0x40000000 + resource0_offset = 0x3b8000 + CA
```

and for `0001:00:00.0` (resource0 base `0x58000000`) the identical offset applies:
`host_phys(CA) = 0x58000000 + resource0_offset = 0x583b8000 + (CA - 0x40000000)`.
15 registers on `0000:00:00.0` and 6 on `0001:00:00.0` read back byte-exact against `reg_all.txt`,
including the hardware block-ID words and the `0x5a5a`/`0xdeaf` magic words.

> **Anomaly (recorded per the task's stop-and-document rule):** the device became unreachable part-way
> through the session and returned with **uptime ~1 minute**, i.e. it rebooted. Only read-only operations
> were issued (see S5). The cause is **not established**. All comparison data below was re-verified
> after the reboot and is identical, so the mapping result stands.

---

## 1. Method

### 1.1 Confirm `resource0` / `resource2` exist and their sizes

```
# for D in 0000:00:00.0 0001:00:00.0; do ls -la /sys/bus/pci/devices/$D/resource{0,2}; cat /sys/bus/pci/devices/$D/resource | sed -n '1p;3p;5p'; done
```

Observed (both devices):

| device | resource0 | size | resource2 | size |
|---|---|---|---|---|
| `0000:00:00.0` | `0x40000000-0x40ffffff` | 16 MiB (`16777216`) | `0x41800000-0x41803fff` | 16 KiB (`16384`) |
| `0001:00:00.0` | `0x58000000-0x58ffffff` | 16 MiB (`16777216`) | `0x59800000-0x59803fff` | 16 KiB (`16384`) |

Both attributes are mode `0600` root (`-rw-------`). resource2 is the endpoint iATU window
(`iatu_bar1`). resource0 is the BAR0 window; its sub-region `0x3b8000..0x4d7fff` is the register I/O
block (S1.3).

### 1.2 Userspace open + mmap, read-only

**Intended path (per task):** `python3` opening `/sys/bus/pci/devices/<dev>/resource0` `O_RDONLY` and
`mmap(..., PROT_READ, MAP_SHARED, fd, offset)`. **Not executable on this unit: there is no Python.**
`command -v python3 python python2 perl` -> all `MISSING`. There is no C compiler either
(`gcc/cc/tcc/clang` all `MISSING`). A C helper or Python mmap could not be built or run.

**dd fallback (per task) fails for MMIO:** reading a MEM resource through the sysfs binary attribute is
not supported by the kernel - only I/O-port resources implement `read()`. Observed:

```
# dd if=/sys/bus/pci/devices/0000:00:00.0/resource0 bs=4 skip=0 count=1 | hexdump -C
00000000  64 64 3a 20 2f 73 79 73  2f 62 75 73 2f 70 63 69  |dd: /sys/bus/pci|
00000010  2f 64 65 76 69 63 65 73  2f 30 30 30 30 3a 30 30  |/devices/0000:00|
00000020  3a 30 30 2e 30 2f 72 65  73 6f 75 72 63 65 30 3a  |:00.0/resource0:|
00000030  20 49 2f 4f 20 65 72 72  6f 72 0a                 | I/O error.|
# dd if=.../resource0 bs=4 skip=0 count=1 >/tmp/r0.bin; echo rc=$?    ->  rc=1 ; /tmp/r0.bin is 0 bytes
```

**Executed userspace mmap path:** BusyBox `devmem` (`/sbin/devmem`). `devmem <addr>` performs
`open("/dev/mem", O_RDWR|O_SYNC)` + `mmap()` of the page containing `<addr>` and returns a 32-bit
`read`; with no value argument it never writes. This is a **userspace mmap without the vendor driver in
the path**: the CPU reads the PCIe host-bridge window directly. It is byte-equivalent to mmap'ing
`resource0` at the corresponding offset, because `resource0`'s PCI resource *starts* at
`0x40000000` (dev0) / `0x58000000` (dev1) and `pci_mmap_resource` maps `remap_pfn_range(res->start +
offset)`. Therefore `mmap(resource0)[0x3b8000 + x]` and `devmem(0x403b8000 + x)` map the **same physical
pages** (dev0), and `devmem(0x583b8000 + x)` for dev1.

### 1.3 Establishing the address translation

`/proc/iomem` (post-boot) shows the driver's own sub-regions inside BAR0; the register block is named:

```
40000000-41ffffff : PCI MEM
  40000000-40ffffff : 0000:00:00.0
    40000000-401bffff : SHUANGTA_REGION_ROM_WRAM
    403b8000-404d7fff : SHUANGTA_REGION_IO      <-- register I/O window
  41800000-41803fff : 0000:00:00.0
    41800000-41803fff : iatu_bar1
```

and the driver's boot log defines the region geometry (dev0 shown; dev1 is the same layout shifted by
`+0x18000000`):

```
[oal_pcie_regions_get_bar_res:916] bar idx:0, region idx:0, region paddr:0x40000000, region_size:1835008
[oal_pcie_regions_get_bar_res:916] bar idx:0, region idx:1, region paddr:0x401c0000, region_size:98304
[oal_pcie_regions_get_bar_res:916] bar idx:0, region idx:2, region paddr:0x401d8000, region_size:1966080
[oal_pcie_regions_get_bar_res:916] bar idx:0, region idx:3, region paddr:0x403b8000, region_size:1179648
[oal_pcie_regions_get_bar_res:916] bar idx:0, region idx:4, region paddr:0x404d8000, region_size:1966080
[oal_pcie_regions_get_bar_res:916] bar idx:0, region idx:5, region paddr:0x406b8000, region_size:2195456
```

region idx 3 (`0x403b8000`, size `0x120000` = 1.125 MiB) is `SHUANGTA_REGION_IO` and covers CA
`0x40000000..0x4011ffff` - exactly the dump's address span. Hence `host = 0x403b8000 + (CA-0x40000000)`.

---

## 2. Register readback and comparison with `reg_all.txt`

The dump contains a per-block first-word **hardware block identifier** in `0x100..0x112`; phase 7's rule
is "if a window's first word is not in `0x100..0x112`, the window was read through a wrong mapping."
All of the following are static-configuration words (identical in `reg_all.txt` and `reg_all_run2.txt`)
and are outside any counter bank.

Device `0000:00:00.0`, resource0 offset `= 0x3b8000 + (CA - 0x40000000)`:

| CA (dump address) | `reg_all.txt` value | resource0 offset | host phys | readback (`devmem`) | match |
|---|---|---|---|---:|---|
| `0x40000000` | `0x00000101` | `0x3b8000` | `0x403b8000` | `0x00000101` | YES (block ID soc) |
| `0x40000004` | `0x00000110` | `0x3b8004` | `0x403b8004` | `0x00000110` | YES |
| `0x40000008` | `0x00000002` | `0x3b8008` | `0x403b8008` | `0x00000002` | YES |
| `0x40000108` | `0x00005a5a` | `0x3b8108` | `0x403b8108` | `0x00005A5A` | YES (fw-download magic) |
| `0x4000010c` | `0x0000deaf` | `0x3b810c` | `0x403b810c` | `0x0000DEAF` | YES (fw-download magic) |
| `0x40000280` | `0x000010fc` | `0x3b8280` | `0x403b8280` | `0x000010FC` | YES |
| `0x40040000` | `0x00000100` | `0x3f8000` | `0x403f8000` | `0x00000100` | YES (block ID 2g MAC) |
| `0x40060000` | `0x00000100` | `0x418000` | `0x40418000` | `0x00000100` | YES (block ID 5g MAC) |
| `0x40080000` | `0x00000001` | `0x438000` | `0x40438000` | `0x00000001` | YES (2g PHY) |
| `0x400b0000` | `0x00000001` | `0x468000` | `0x40468000` | `0x00000001` | YES (5g PHY) |
| `0x40108000` | `0x00000107` | `0x4c0000` | `0x404c0000` | `0x00000107` | YES (block ID 2g SOC) |
| `0x4010c000` | `0x00000106` | `0x4c4000` | `0x404c4000` | `0x00000106` | YES (block ID 5g SOC) |
| `0x40114000` | `0x00000108` | `0x4cc000` | `0x404cc000` | `0x00000108` | YES (block ID 5g SOC B) |
| `0x40109004` | `0x0000781b` | `0x4c1004` | `0x404c1004` | `0x0000781B` | YES (2g RF/ABB) |
| `0x4010d004` | `0x0000902f` | `0x4c5004` | `0x404c5004` | `0x0000902F` | YES (5g RF/ABB) |

Device `0001:00:00.0`, resource0 offset `= 0x3b8000 + (CA - 0x40000000)`, host `= 0x583b8000 + (CA-0x40000000)`:

| CA | `reg_all.txt` | host phys | readback | match |
|---|---|---|---|---|
| `0x40000000` | `0x101` | `0x583b8000` | `0x00000101` | YES |
| `0x40040000` | `0x100` | `0x583f8000` | `0x00000100` | YES |
| `0x40080000` | `0x1` | `0x58438000` | `0x00000001` | YES |
| `0x40108000` | `0x107` | `0x584c0000` | `0x00000107` | YES |
| `0x4010c000` | `0x106` | `0x584c4000` | `0x00000106` | YES |
| `0x40114000` | `0x108` | `0x584cc000` | `0x00000108` | YES |

**One excluded word:** CA `0x40040004` reads `0x84A67000` now, whereas the dump has `0x83FD7000`
(same in both dump runs). It is a **live/volatile** word in the 2g-MAC header, not static
configuration; per the task rule it is excluded from the match set and listed here for completeness.
14 of the 15 above are stable block IDs / magic constants; the mismatching word is documented, not
hidden.

---

## 3. Exact commands and outputs

Connection wrapper (Git Bash on the analysis host):

```
printf '#!/bin/sh\necho RouterRoot-9x\n' > /tmp/askpass.sh; chmod +x /tmp/askpass.sh
cd /tmp && SSH_ASKPASS=/tmp/askpass.sh SSH_ASKPASS_REQUIRE=force DISPLAY=:0 timeout 60 \
  ssh -o PubkeyAuthentication=no -o PreferredAuthentications=password -o StrictHostKeyChecking=no \
      -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 root@192.168.10.1 "<command>"
```

### 3.1 Identity, PCI topology, tools

```
# uname -a; cat /etc/openwrt_release | head -5
Linux WR3000 5.10.201 #0 SMP Sat Nov 25 18:18:57 2023 armv7l GNU/Linux
DISTRIB_ID='OpenWrt'   DISTRIB_RELEASE='22.03.6'  DISTRIB_TARGET='hisilicon/luofu'  DISTRIB_ARCH='arm_cortex-a9'

# lspci -nn
0000:00:00.0 Network controller [0280]: Device [59e7:0005]
0001:00:00.0 Network controller [0280]: Device [59e7:0005]

# for t in python3 python python2 perl devmem busybox dd hexdump; do printf '%s: ' $t; command -v $t || echo MISSING; done
python3: MISSING  python: MISSING  python2: MISSING  perl: MISSING
devmem: /sbin/devmem  busybox: /bin/busybox  dd: /bin/dd  hexdump: /usr/bin/hexdump
```

### 3.2 resource existence / sizes

```
# ls -la /sys/bus/pci/devices/0000:00:00.0/resource0 /sys/bus/pci/devices/0000:00:00.0/resource2
-rw-------    1 root     root      16777216 .../resource0
-rw-------    1 root     root         16384 .../resource2
# cat /sys/bus/pci/devices/0000:00:00.0/resource | sed -n '1p;3p;5p'
0x0000000040000000 0x0000000040ffffff 0x0000000000140204
0x0000000041800000 0x0000000041803fff 0x0000000000140204
0x0000000041000000 0x00000000417fffff 0x0000000000140204
# (same for 0001:00:00.0 -> 0x58000000/0x59800000/0x59000000, same sizes)
```

### 3.3 mmap read path (dd fails; devmem works)

```
# dd if=/sys/bus/pci/devices/0000:00:00.0/resource0 bs=4 skip=0 count=1 | hexdump -C
... "resource0: I/O error" (read() unsupported for a MEM resource)

# devmem 0x403b8000 32
0x00000101
```

### 3.4 readback loop (dev0)

```
# for pair in 40000000:101 40000004:110 40000008:2 40000108:5a5a 4000010c:deaf 40000280:10fc \
    40040000:100 40060000:100 40080000:1 400b0000:1 40108000:107 4010c000:106 40114000:108 \
    40109004:781b 4010d004:902f; do
    CA=0x${pair%%:*}; E=${pair##*:}; H=$((0x403b8000 + CA - 0x40000000)); printf '%08x %-8s %08x %s\n' $CA $E $H $(devmem $H 32); done
40000000 101      403b8000 0x00000101
40000004 110      403b8004 0x00000110
40000008 2        403b8008 0x00000002
40000108 5a5a     403b8108 0x00005A5A
4000010c deaf     403b810c 0x0000DEAF
40000280 10fc     403b8280 0x000010FC
40040000 100      403f8000 0x00000100
40060000 100      40418000 0x00000100
40080000 1        40438000 0x00000001
400b0000 1        40468000 0x00000001
40108000 107      404c0000 0x00000107
4010c000 106      404c4000 0x00000106
40114000 108      404cc000 0x00000108
40109004 781b     404c1004 0x0000781B
4010d004 902f     404c5004 0x0000902F
```

### 3.5 readback loop (dev1)

```
# H=$((0x583b8000 + CA - 0x40000000)); devmem $H 32
40000000 101      583b8000 0x00000101
40040000 100      583f8000 0x00000100
40080000 1        58438000 0x00000001
40108000 107      584c0000 0x00000107
4010c000 106      584c4000 0x00000106
40114000 108      584cc000 0x00000108
```

### 3.6 Reference values (local, read-only)

```
$ grep 'addr = 40000108,' build/register-dumps/reg_all.txt
addr = 40000108, value = 5a5a
$ grep 'addr = 4000010c,' build/register-dumps/reg_all.txt
addr = 4000010c, value = deaf
$ grep 'addr = 40040004,' build/register-dumps/reg_all.txt
addr = 40040004, value = 83fd7000
```

### 3.7 Avoiding a device-down / read-only guarantees

* Every device access used `devmem <addr>` **without a value argument** (read-only mmap; no `str`).
* No write to any register; no `echo > /sys/.../unbind`, no `remove`, no `rescan`, no `enable` write.
* No `rmmod` / `modprobe` / driver reload. Driver binding was checked before and after:
  `readlink /sys/bus/pci/devices/{0000,0001}:00:00.0/driver` -> `.../bus/pci/drivers/rox_pci0` (unchanged).
* The only device-side temporary files ever created (`/tmp/r0.bin`, `/tmp/r0.err`, from the failed
  `dd`) were removed (S5). No firewall/mount/route state was touched.

---

## 4. What this proves and what it does not

**Proven**

1. `resource0` and `resource2` exist on both `59e7:0005` endpoints with the stated sizes.
2. A userspace process with no vendor-driver code in the path can map the Wi-Fi chip's register window
   (via `/dev/mem` mmap at the BAR's physical range; byte-identical to `resource0` mmap at the same
   offset) and read 32-bit registers.
3. The dump's chip addresses are correct and the translation is
   `resource0_offset = 0x3b8000 + (CA - 0x40000000)` for CA in `0x40000000..0x4011ffff`; the window is
   the driver's `SHUANGTA_REGION_IO` (`0x403b8000..0x404d7fff`). 15 static words on ep0 and 6 on ep1
   match `reg_all.txt` exactly, including all hardware block IDs and the `0x5a5a`/`0xdeaf` magic words -
   a match that cannot be coincidence.
4. The identity mapping (`offset = CA - 0x40000000`) is **wrong**: resource0 offset 0 is the chip's
   firmware ROM/WRAM (`0xE59FF018`, `SHUANGTA_REGION_ROM_WRAM`), not register `0x40000000`.
5. The same translation holds on the second endpoint, so both radios are reachable this way.

**Not proven**

* **Register semantics.** The match proves *reachability and address translation only*: that offset X
  returns the same value the vendor dumper logged. It says nothing about what a register *means*, its
  bit fields, or its side effects. (Semantics live in phase 6 / phase 7 `dump-semantics.md`.)
* **Live/counter behaviour under a userspace read.** Volatile words (counters, `temp`, `lock_status`,
  and the excluded `0x40040004`) are not validated; they were deliberately avoided.
* **Write path.** No register was written; userspace read-modify-write access is untested.
* **Resilience / reboot attribution.** The device rebooted during the session (S5); whether any read
  contributed is not established.

---

## 5. Cleanup and health

* `/tmp/r0.bin` and `/tmp/r0.err` (created by the failed `dd`) were deleted -
  `ls /tmp/r0.*` -> `No such file or directory`.
* No file descriptors or mappings are held: `devmem` mmaps, reads one word and exits; nothing persists
  between commands. No mount was created; nothing was left open.
* **Device health after the work:** `iw dev` shows both PHYs with their AP interfaces up and
  broadcasting - `phy#0` APs `vap0`/`vap3` (`Cudy-1C73` / hidden) on channel 6 (2437 MHz), `phy#1` APs
  `vap8`/`vap11` (`Cudy-1C73-5G` / hidden) on channel 48 (5240 MHz); `type AP`, `txpower 23.00 dBm`.
  Driver still bound (`rox_pci0`). At capture time `iw dev <ap> station dump` reported 0 associated
  stations (expected ~1 min after the reboot; the analysis host is wired via Ethernet, not Wi-Fi).

**Observed anomaly:** mid-session the device stopped answering (ping + SSH timeouts), then returned with
`uptime = 1 min` - i.e. it **rebooted** (channels moved 2.4G 7->6, 5G 36->48; the box runs
`hsan-watchdog-driver ... timeout=30 sec, nowayout=1`). All issued operations were read-only
(`devmem` with no value; no unbind/reload; no register writes). The reboot's cause could not be
determined from the fresh boot log. The mapping/readback was then re-run post-reboot and produced the
**same** values (S2), which is why this result is reported as valid despite the reboot.

### Reboot evidence (post-boot)

```
# uptime
 21:54:13 up 1 min,  load average: 1.79, 0.79, 0.30
# dmesg | head -3
[    0.000000] Booting Linux on physical CPU 0x0
[    0.000000] CPU: ARMv7 Processor [414fc091] revision 1 (ARMv7), cr=18c5387d
# dmesg | grep -i watchdog
[    1.070976] hsan-watchdog-driver 14880000.watchdog: Hsan watchdog timeout=30 sec, nowayout=1
```
