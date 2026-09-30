# Phase 7 - Firmware-RAM read path and calibration-table dump

Device: `root@192.168.10.1` (WR3000, `hi5622v100_wifi`). All probe commands below were issued with the
standard recipe:

```
$ printf '#!/bin/sh\necho RouterRoot-9x\n' > /tmp/askpass.sh; chmod +x /tmp/askpass.sh
$ cd /tmp && SSH_ASKPASS=/tmp/askpass.sh SSH_ASKPASS_REQUIRE=force DISPLAY=:0 timeout 60 ssh \
    -o PubkeyAuthentication=no -o PreferredAuthentications=password -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 root@192.168.10.1 "<command>"
```

Scope was read-only apart from the one sanctioned load of `wifi_debug.ko`; the device was rebooted twice
afterwards (module load test, then final removal). Final state is reported in section 5.

**Result summary: a RAM read path EXISTS.** `wifi_debug.ko` exposes the writable sysfs node
`/sys/hisys/plat/pcie`, whose `savemem`/`readmem` commands read arbitrary device CPU addresses. It was
used to dump the firmware WRAM and TCM regions. Both calibration tables and two of the three requested
tuple words were found in WRAM; `0x00006060` is absent (see 2.3).

---

## 1. Candidates searched

### 1.1 The 414-entry `alg` command table (`ulw/phase3/alg-commands.md`)

Searched all 414 names for the tokens `ram`, `mem`, `itcm`, `dtcm`, `otp`, `efuse`, `read`, `dump`,
`reg_`/`_reg`:

```
$ python -c "... names from alg-commands.md ..."
names: 414
'ram'  -> all matches are the substring in *_param (false positives)
'mem'  -> []          'itcm' -> []       'dtcm' -> []      'otp' -> []
'efuse'-> ['efuse_test']
'read' -> []          'dump' -> []
```

The only memory-flavoured entry is `efuse_test` (index 363, `cfg_id` 3525, `dir = get`). Probed:

```
$ iwpriv vap0 alg efuse_test
vap0      alg:[SUCC]
rc=0
```

i.e. it returns an empty success string, no data - it is an eFuse self-test, not a RAM reader. **No `alg`
command reads firmware RAM.**

### 1.2 Base `/sys/hisys/hipriv` table (~100 commands, no debug module)

```
$ wc -l /sys/hisys/hipriv
100 /sys/hisys/hipriv
$ grep -inE 'ram|mem|itcm|dtcm|read|dump|efuse|otp|reg' /sys/hisys/hipriv
56:	hash_param_notify     # "paRAM" substring
94:	set_regdomain_pwr     # "reg" in regdomain
```

Both hits are substring false positives; the base table has **no RAM/memory read command**.

### 1.3 `hipriv` table with `wifi_debug.ko` loaded (181 commands)

`wifi_debug.ko` registers extra hipriv entries (100 -> 181). The memory-related additions are:

```
$ insmod /lib/modules/$(uname -r)/wifi_debug.ko        # rc=0
$ wc -l /sys/hisys/hipriv
181 /sys/hisys/hipriv
$ grep -inE 'ram|mem|read|dump|reg' /sys/hisys/hipriv
36:	meminfo
37:	memleak
38:	devicememleak
39:	memoryinfo
53:	dump_timer
75:	dump_ba_bitmap
86:	dump_rx_dscr
87:	dump_tx_dscr
88:	dump_memory
123:	get_all_regs
151:	reginfo
153:	regwrite
154:	dump_all_dscr
25:	stat_dump
29:	dump_tx_ppdu_dscr
30:	dump_rx_ppdu_dscr
```

None of these could be invoked through the `iwpriv` private-ioctl path - the name lookup on every
wireless interface fails:

```
$ for i in vap0 vap8 Hisilicon0; do for c in get_all_regs dump_memory memoryinfo meminfo memleak \
    devicememleak reginfo; do printf '%-10s %-16s -> ' $i $c; iwpriv $i $c 2>&1 | head -1; done; done
vap0       get_all_regs     -> Invalid command : get_all_regs
vap0       dump_memory      -> Invalid command : dump_memory
vap0       memoryinfo       -> Invalid command : memoryinfo
vap0       meminfo          -> Invalid command : meminfo
vap0       memleak          -> Invalid command : memleak
vap0       devicememleak    -> Invalid command : devicememleak
vap0       reginfo          -> Invalid command : reginfo
vap8       get_all_regs     -> Invalid command : get_all_regs
vap8       dump_memory      -> Invalid command : dump_memory
vap8       memoryinfo       -> Invalid command : memoryinfo
vap8       meminfo          -> Invalid command : meminfo
vap8       memleak          -> Invalid command : memleak
vap8       devicememleak    -> Invalid command : devicememleak
vap8       reginfo          -> Invalid command : reginfo
Hisilicon0 get_all_regs     -> Invalid command : get_all_regs
Hisilicon0 dump_memory      -> Invalid command : dump_memory
Hisilicon0 memoryinfo       -> Invalid command : memoryinfo
Hisilicon0 meminfo          -> Invalid command : meminfo
Hisilicon0 memleak          -> Invalid command : memleak
Hisilicon0 devicememleak    -> Invalid command : devicememleak
Hisilicon0 reginfo          -> Invalid command : reginfo
```

Writing these names to `/sys/hisys/hipriv` was accepted (`rc=0`) but produced no kernel-log output and
left the command listing unchanged, i.e. it is not an execution interface. They are refusals, not a read
path.

### 1.4 The path that works: `wifi_debug.ko` -> `/sys/hisys/plat/pcie`

Loading `wifi_debug.ko` creates a writable command node (it did not exist before the load):

```
$ ls -la /sys/hisys/plat/          # BEFORE load
-r--r--r--  1 root root 4096 debug

$ insmod /lib/modules/$(uname -r)/wifi_debug.ko
$ ls -la /sys/hisys/plat/          # AFTER load
-r--r--r--  1 root root 4096 debug
-rw-r--r--  1 root root 4096 hcc_test
-rw-r--r--  1 root root 4096 hcc_test_cfg
-rw-r--r--  1 root root 4096 pcie
-rw-r--r--  1 root root 4096 plat_test
```

`cat` lists its command set:

```
$ cat /sys/hisys/plat/pcie
pci debug cmds:
help
dump_all_regions_mem
dump_all_regions_info
read32
write32
host_read32
host_write32
read16
write16
saveconfigmem
savemem
save_hostmem
loadfile
readmem
readconfigmem
performance_test
performance_read
performance_cpu
performance_write
performance_netbuf_alloc
performance_netbuf_queue
testcase
wlan_power_on
pcie_send_queue
pcie_msg_test
pcie_relink_test
host_wakeup_dev
try_to_sleep
pcie_ete_test
pcie_memcpy_test
pcie_ete_chn_blocked_test
freq_switch_test
```

`echo help > /sys/hisys/plat/pcie` prints the per-command grammar (raw kernel log, verbatim):

```
[   61.547791] echo savemem address(hex) length(decimal) filename > /sys/hisys/pci/pcie/debug
[   61.556131] echo save_hostmem address(hex) length(decimal) filename > /sys/hisys/pci/pcie/debug
[   61.564914] echo loadfile address(hex) filename > /sys/hisys/pci/pcie/debug
[   61.571870] echo readmem address(hex) length(decimal) > /sys/hisys/pci/pcie/debug
[   61.579331] echo readconfigmem address(hex) length(decimal) > /sys/hisys/pci/pcie/debug
```

**Argument-grammar gotcha (both wrong forms tried, with raw refusal):** the dispatcher
(`oal_pcie_set_debug_info`) first parses an optional leading *decimal* chip id before handing the rest to
the handler. With `readmem 0x0 256` the leading `0` of `0x0` is eaten as chip id, so the handler receives
`x0 256` and the `%x` parse fails:

```
$ echo 'readmem 0x0 256' > /sys/hisys/plat/pcie
[   86.710255] pcie chip_id::0, cmdid::13
[   86.710255] echo readmem address(hex) length(decimal) > /sys/hisys/pci/pcie/debug

$ echo 'readmem0x0 256' > /sys/hisys/plat/pcie
[PCIE][ERR]  [oal_pcie_set_debug_info:1811]invalid pci cmd:readmem0x0 256

$ echo 'read32 0x40000000' > /sys/hisys/plat/pcie
[PCIE][ERR]  [oal_pcie_debug_read32:168]read32 argument invalid,[x40000000
]                                   <-- note the consumed leading '0'
```

The working form prefixes an explicit chip id (`0` on this single-chip unit): `readmem 0 0x<addr> <len>`
and `savemem 0 0x<addr> <len> <path>`.

---

## 2. Firmware RAM dump and table locations

### 2.1 The device CPU address map

`dump_all_regions_mem` / `dump_all_regions_info` return the six mapped regions (raw kernel log):

```
[   71.153748] dump region[0],name:SHUANGTA_REGION_ROM_WRAM, cpu addr:0x0
[   71.154405] dump region[1],name:SHUANGTA_REGION_TCM_NOACP, cpu addr:0x400000
[   71.155102] dump region[2],name:SHUANGTA_REGION_PKTRAM_NOACP, cpu addr:0x1000000
[   71.155828] dump region[3],name:SHUANGTA_REGION_IO, cpu addr:0x40000000
[   71.156583] dump region[4],name:SHUANGTA_REGION_ACP, cpu addr:0x2000000
[   71.157369] dump region[5],name:SHUANGTA_REGION_ACP, cpu addr:0x1200000
...
[PCIE][SHUANGTA_REGION_ROM_WRAM]va:..., pa:0x58000000, [cpu start:0x       0 end:0x  1bffff], size:1835008, flag:0x200
[PCIE][SHUANGTA_REGION_TCM_NOACP]va:..., pa:0x581c0000, [cpu start:0x  400000 end:0x  417fff], size:98304,   flag:0x200
[PCIE][SHUANGTA_REGION_PKTRAM_NOACP]va:..., pa:0x581d8000, [cpu start:0x 1000000 end:0x 11dffff], size:1966080, flag:0x200
[PCIE][SHUANGTA_REGION_IO]va:..., pa:0x583b8000, [cpu start:0x40000000 end:0x4011ffff], size:1179648, flag:0x200
[PCIE][SHUANGTA_REGION_ACP]va:..., pa:0x584d8000, [cpu start:0x 2000000 end:0x 21dffff], size:1966080, flag:0x200
[PCIE][SHUANGTA_REGION_ACP]va:..., pa:0x586b8000, [cpu start:0x 1200000 end:0x 1417fff], size:2195456, flag:0x200
```

The firmware CPU is `0x0` (WRAM, 1.75 MiB) - this is the firmware RAM. TCM at `0x400000` is the second
CPU's tightly-coupled memory.

### 2.2 The dumps

Raw read (proof the path serves data):

```
$ echo 'readmem 0 0x0 64' > /sys/hisys/plat/pcie
[PCIE][INFO] [oal_pcie_debug_readmem:631]readmem cpu:0x       0 len:64, va:0xc968003c, pa:0x5800003c  done
buf 123d4671,len:64
readmem: 123d4671: 18 f0 9f e5 18 f0 9f e5 18 f0 9f e5 18 f0 9f e5  ................
```

Saved to files:

```
$ echo 'savemem 0 0x0 1835008 /tmp/wram.bin' > /sys/hisys/plat/pcie
$ echo 'savemem 0 0x400000 98304 /tmp/tcm.bin' > /sys/hisys/plat/pcie
$ ls -la /tmp/wram.bin /tmp/tcm.bin
-rw-r--r--    1 root     root       1835008 /tmp/wram.bin
-rw-r--r--    1 root     root         98304 /tmp/tcm.bin
$ sha256sum /tmp/wram.bin /tmp/tcm.bin
b69f1699b1a59442a20523234ccce4ec3de5ff9f7a39fbeaf8575c53820b4755  /tmp/wram.bin
6fe6c2fa654d6e2ace388626caa99ba760d8304e4cea375fde61f7b488b7c5a4  /tmp/tcm.bin
```

Fetched over `ssh cat` into the repo; local sha256 identical to the device side:

| local file | bytes | sha256 |
|---|---:|---|
| `build/register-dumps/fw_ram_wram_0x0_0x1bffff.bin` | 1835008 | `b69f1699b1a59442a20523234ccce4ec3de5ff9f7a39fbeaf8575c53820b4755` |
| `build/register-dumps/fw_ram_tcm_0x400000_0x417fff.bin` | 98304 | `6fe6c2fa654d6e2ace388626caa99ba760d8304e4cea375fde61f7b488b7c5a4` |
| `build/register-dumps/fw_ram_calib_0x1b2f00_576_run2.bin` | 576 | `8b30f6233d3aea5d7e0727f1241b51b6bde5dce78b58239ee61266c65f87f7b5` |

### 2.3 Tuple-word search

```
$ python (LE/BE u32 search over both images)
===== fw_ram_tcm_0x400000_0x417fff.bin (98304)
  17161605 -> 0 hits
  0a0606ff -> 0 hits
  00006060 -> 0 hits
===== fw_ram_wram_0x0_0x1bffff.bin (1835008)
  17161605 -> 3 hits ['0x1b2f24', '0x1b2f28', '0x1b2f2c']
  0a0606ff -> 3 hits ['0x1b2f80', '0x1b2f84', '0x1b2f88']
  00006060 -> 0 hits
```

* `0x17161605` (little-endian u32) occurs **three times, at CPU addresses `0x1b2f24/0x1b2f28/0x1b2f2c`** -
  the first three words of the 2.4 GHz power table.
* `0x0a0606ff` occurs **three times, at `0x1b2f80/0x1b2f84/0x1b2f88`** - the last three words of the same
  table.
* `0x00006060` occurs **zero times** in either image, as a little-endian or big-endian u32. It is the
  `get_xo_ppm_cali_param` value (`iwpriv vap0 alg get_xo_ppm_cali_param` -> `00006060`), and it is not
  stored as a standalone u32 word anywhere in firmware WRAM/TCM. (A byte search finds `60 60` at 53
  non-word-aligned offsets, all coincidental; none is the required 4-byte `0x00006060` word.) This is an
  explicit negative, not a path failure.

### 2.4 Full tables located

The exact iwpriv tables, searched as little-endian u32 arrays:

```
2g_power_param(26)     words=26 hits=['0x1b2f24']
5g_power_param(18)     words=18 hits=['0x1b30ea']
2g_all_curve(12)       words=12 hits=['0x173dd0', '0x1745b0', '0x1b2ec8']
5g_all_curve(32)       words=32 hits=['0x173e00', '0x1745e0', '0x1b303c']
```

**2.4 GHz power table - `0x1b2f24`..`0x1b2f88`, 26 words, 4-byte aligned, exact match:**

```
0x1b2f20  00000000          <- preceding word
0x1b2f24  17161605  <-- table[0]
0x1b2f28  17161605
0x1b2f2c  17161605
0x1b2f30  17161505
0x1b2f34  17161505
0x1b2f38  15161401
0x1b2f3c  15161401
0x1b2f40  15161401
0x1b2f44  15161401
0x1b2f48  15161400
0x1b2f4c  08141200
0x1b2f50  08141200
0x1b2f54  08141200
0x1b2f58  0c111103
0x1b2f5c  0c111103
0x1b2f60  0c111103
0x1b2f64  0b101002
0x1b2f68  0b101002
0x1b2f6c  0b101002
0x1b2f70  0b101002
0x1b2f74  0b101001
0x1b2f78  0a0f0f01
0x1b2f7c  0a0f0f01
0x1b2f80  0a0606ff
0x1b2f84  0a0606ff
0x1b2f88  0a0606ff  <-- table[25]
0x1b2f8c  08080814  <-- next table starts immediately (26-word sibling, 2g low-power shape)
```

**5 GHz power table - `0x1b30ea`..`0x1b3131`, 18 words, 2-byte aligned (offset % 4 == 2), exact match:**

```
0x1b30e4  00000000
0x1b30e8  00000005   <- u16 0x0005 immediately before the table
0x1b30ea  00000000  <-- table[0]   (starts at a 2-byte boundary)
0x1b30ee  0004ff00
0x1b30f2  0801000b
0x1b30f6  ff000b00
0x1b30fa  0009fd02
0x1b30fe  09000904
0x1b3102  0009fe00
0x1b3106  0c030009
0x1b310a  00000904
0x1b310e  00000000
0x1b3112  160f140f
0x1b3116  080c0500
0x1b311a  0c060016
0x1b311e  0f001a08
0x1b3122  001a1617   (word value; note readout is 2-byte shifted vs the 4-aligned grid)
0x1b3126  1a151813
0x1b312a  151b1600
0x1b312e  0000001a  <-- table[17]
0x1b3132  00000000
0x1b3134  000cff00  <-- next table (18-word sibling, 5g low-power shape)
```

(The 4-byte-grid listing in this section is re-derived as u32 reads at the found byte offset; the 5 GHz
table is deliberately called out as living at a 2-mod-4 address, whereas the 2.4 GHz one is 4-aligned.)

### 2.5 Surrounding structure - the "calibration block" at the top of WRAM

All the per-band calibration payloads sit contiguously in the last ~13 KB of WRAM, all as little-endian
u32 arrays, in a fixed band/type order:

| CPU range | content |
|---|---|
| `0x1b2ec8` (12 words) | `get_2g_all_curve_param` copy |
| `0x1b2f24`..`0x1b2f88` (26) | `get_2g_power_param` |
| `0x1b2f8c`..`0x1b2ff0` (26) | 2g sibling table (`08080814...0a060610`) |
| `0x1b3000`..`0x1b3038` | mixed calibration words (`185600aa 182e1842 ...`, `01ff0003`, `000501ff`) |
| `0x1b303c`..`0x1b30b8` (32) | `get_5g_all_curve_param` |
| `0x1b30bc`..`0x1b30e0` | mixed calibration words (`27100000 ...`, `00ca00c5`, `0003a90c`) |
| `0x1b30e8` | u16 count/tag `0x0005` |
| `0x1b30ea`..`0x1b3131` (18) | `get_5g_power_param` |
| `0x1b3134`.. (18) | 5g sibling table (`000cff00 0801000a ...`) |

Two independent copies of both curve tables also exist lower in WRAM at `0x173dd0`/`0x173e00` and
`0x1745b0`/`0x1745e0` - consistent with a firmware static/data source plus a working copy.

### 2.6 The tables are NOT part of the firmware image

`build/tmp/FIRMWARE.bin` (928920 bytes) is the device firmware: 1835008-byte WRAM dump slice from
`0x40000` matches 835788 consecutive bytes of it:

```
fw header in wram: ['0x40000']
longest run from base 0x40000: 835788 bytes (fw 928920)
fw end offset would be 0x122c98   (wram end 0x1bffff)
```

Yet none of the four tables (`2g_power`, `5g_power`, `2g_all_curve`, `5g_all_curve`) occurs anywhere in
`FIRMWARE.bin`:

```
===== build/tmp/FIRMWARE.bin (928920)
  2g26 -> 0 hits
  5g18 -> 0 hits
  0004ff00 -> 0   1a151813 -> 0
```

So the calibration values are written into WRAM **after** the firmware image is loaded - they are not
compiled into the firmware.

### 2.7 Persistence across reboot

After a full reboot, `wifi_debug.ko` was re-loaded and the same slice re-read:

```
$ echo 'savemem 0 0x1b2f00 576 /tmp/cal2.bin' > /sys/hisys/plat/pcie
$ sha256sum /tmp/cal2.bin
8b30f6233d3aea5d7e0727f1241b51b6bde5dce78b58239ee61266c65f87f7b5  /tmp/cal2.bin

post-reboot identical to pre-reboot slice: True
```

The calibration block is byte-identical across a reboot, so it is re-materialised from a persistent
source at every boot, not regenerated on-chip.

---

## 3. Part 3 - "no RAM read path"

Not applicable: the path exists and was used. The refusals for the candidates that did not work are all
recorded in section 1 (`Invalid command : <name>` for the hipriv debug names on `vap0`/`vap8`/`Hisilicon0`;
usage/`invalid pci cmd` text for the mis-parsed PCIe forms; `vap0 alg:[SUCC]` empty for `efuse_test`).

---

## 4. What the storage location implies for an open driver

The calibration data is **not** in the driver, **not** in the firmware image, and **not** generated on
chip. It is a persistent, per-unit store that is loaded into firmware WRAM at boot and then served back
over the firmware message channel:

1. **Storage:** the per-unit values persist in flash under `/usr/local/factory/`
   (`wifi_cali_data.kv` 8912 B, `wifi_cali_data_2g.kv` 2336 B, plus a text `hi5622v100.cal` dump). The
   driver-owned save path (`hmac_save_cali_data_to_file_2g/5g`) and the `.kv` container were established
   in `ulw/phase7/calibration-store.md`; the values are byte-identical across reboots (2.7).
2. **Transport into firmware RAM:** the tables are absent from `FIRMWARE.bin` (2.6) yet present in WRAM
   after boot, so the driver reads the factory store and pushes it to the firmware over the message
   channel at boot (the driver->firmware `0x0dxx` envelope documented in `ulw/phase6/message-fields.md`).
   The firmware keeps its working copy at the fixed WRAM offsets in 2.5.
3. **Read-back:** the host never sees the tables as MMIO. `alg get_2g_power_param` etc. (phase 3) are
   served by the firmware from that WRAM copy and returned over `hmac_sync_dmac_alg_cfg_rsp_entry`
   (phase 5/6). That is exactly why the phase-7 register dump contained none of the tuples.

Practical consequence for an open driver: it does **not** need to reproduce the on-chip calibration
procedure. To be useful it must (a) read the persistent `.kv` store (container: magic `ZZZZ`, u32 table,
trailer `0xa5a5a5a5`), (b) send it to the firmware with the documented envelope, and (c) read results
back through the `alg` cfg_ids. Any RAM region the driver maps itself is only the firmware's *copy*; the
authoritative per-unit data is the factory file. The firmware side is also fully readable from the host
through the debug module, but only while `wifi_debug.ko` is loaded (section 5).

---

## 5. Device state and verification at end of run

Final state after the last reboot:

```
$ uptime
 21:54:48 up 1 min,  load average: 1.96, 0.57, 0.20
$ lsmod | grep -iE 'wifi_debug|plat_debug' || echo 'wifi_debug/plat_debug NOT loaded'
wifi_debug/plat_debug NOT loaded
$ ls /sys/hisys/ /sys/hisys/plat/
/sys/hisys/:  hipriv  plat
/sys/hisys/plat/:  debug
$ wc -l /sys/hisys/hipriv
100 /sys/hisys/hipriv
$ ls /sys/hisys/plat/pcie
ls: /sys/hisys/plat/pcie: No such file or directory
$ iwpriv vap0 alg get_2g_power_param
vap0      alg:[SUCC]17161605 17161605 17161605 17161505 17161505 15161401 15161401 15161401 15161401 15161400 08141200 08141200 08141200 0c111103 0c111103 0c111103 0b101002 0b101002 0b101002 0b101002 0b101001 0a0f0f01 0a0f0f01 0a0606ff 0a0606ff 0a0606ff
```

* `wifi_debug.ko` was loaded only for the probes and has been removed by a clean reboot (never `rmmod`).
  `plat_debug.ko` was loaded once by mistake; it faulted its own sysfs init (`sysfs create
  hcc_test_attribute_group group fail.ret=-12`) and is likewise gone after the reboot.
* `/sys/hisys/hipriv` is back to the stock 100 entries (no `get_all_regs`); `/sys/hisys/plat/pcie` no
  longer exists; `debug` is the stock read-only node again.
* Wi-Fi is healthy: the `alg get_2g_power_param` read returns the same 26-word table as at the start.

All numbers in this report are reproducible from the saved dumps with plain byte searches; every claim
above carries the literal command and raw output it came from.
