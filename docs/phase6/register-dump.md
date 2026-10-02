# Phase 6 - Vendor SoC register dump (`shuangta_read_soc_to_file`) captured live

Device: Cudy WR3000 V2 (`192.168.10.1`, `uname` -> `Linux WR3000 5.10.201 ... armv7l`),
vendor Wi-Fi stack loaded (`hi5622v100_wifi` 3,387,392 B, `hi5622v100_plat` 323,584 B).
All device I/O via the recorded ssh recipe (Git Bash). Date of capture: 2026-09-30 (device clock).

## 0. TL;DR

* **The trigger is real and was found.** The vendor ships an extra debug module,
  `/lib/hisilicon/ko/wifi_debug.ko`, which is **not loaded by default**. Loading it adds the
  hipriv command **`get_all_regs`**. One read-only write to sysfs is enough:
  `echo "Hisilicon0 get_all_regs all" > /sys/hisys/hipriv`.
* It calls the chain
  `wal_hipriv_get_all_reg_value` -> cfg cmd `0x178` -> `wal_config_get_all_reg_value`
  -> `hmac_config_get_all_reg_value` -> `shuangta_read_all_reg_info` **and**
  `shuangta_read_soc_to_file`, which writes the dump to `/config/work/firmware/reg_all.txt`.
* The dump is **709,089 bytes / 25,166 lines**; it contains **72 contiguous register windows**
  in three groups (`soc_register`, `2g_*`, `5g_*`).
* Raw artifact saved locally: `build/register-dumps/reg_all.txt`,
  sha256 `a8461c98a464a8368bcb91969ddeb609a6d6d0083c124b76bc09a940b336d6f4`.
  A second run (`reg_all_run2.txt`, sha256 `382db1fe...`) was taken seconds later and diffed to
  separate live counters from static configuration (310 addresses changed).
* It is **not** reachable from the documented `alg` command channel and **not** from the base
  driver's 100-entry hipriv table; it needs the debug module (details in S1.2).
* Cleanup: `wifi_debug.ko` was unloaded. **Caveat:** the `rmmod` immediately preceded a device
  reboot (see S4.2). The module is absent after the reboot. No tracefs/kprobe probes were created
  at any point (`kprobe_events` verified empty, S4.3).

---

## 1. How the dump was triggered

### 1.1 Static call graph (established locally from the .ko files)

The window lists this task references come from `ulw/phase4/mmio-map.md` S2.2
(`shuangta_read_all_reg_info` `.text+0x35600`, `shuangta_read_soc_to_file` `.text+0x3617c`).
I resolved the callers of both with the relocation tables of `hi5622v100_wifi.ko`
(`.rel.text` symbol index -> caller function by `[st_value, st_value+st_size)`):

```
$ python (pyelftools) - relocations targeting the two read functions:
.rel.text 0x63f50 -> shuangta_read_all_reg_info   | in function: hmac_config_get_all_reg_value
.rel.text 0x63f78 -> shuangta_read_soc_to_file    | in function: hmac_config_get_all_reg_value
```

`hmac_config_get_all_reg_value` is at `.text+0x63e40` (452 B). Its only caller is the 4-byte
veneer `wal_config_get_all_reg_value` `.text+0x16263c`:

```
$ python - relocations targeting wal_config_get_all_reg_value / hmac_config_get_all_reg_value:
.rel.text  0x16263c -> hmac_config_get_all_reg_value | in function: wal_config_get_all_reg_value (4 B)
.rel.rodata 0x24f00  -> wal_config_get_all_reg_value
```

`.rel.rodata` at `0x24f00` is the `handler` word of a 12-byte WAL-config table entry whose
`cmd_id` word at `.rodata+0x24ef8` is **`0x00000178`**:

```
$ python - dump of .rodata around 0x24ef8:
0x24ef8: 0x178
0x24efc: 0x0
0x24f00: <reloc -> wal_config_get_all_reg_value>
```

The WAL-config dispatcher is `wal_config_process_entry` `.text+0xfc91c` (872 B), reached from
`wal_config_process_pkt` (`bl` @0xfd070) and registered as a driver-config hook by
`wal_recv_config_cmd` `.text+0xfd80c` (both call sites in `wal_drv_cfg_func_hook_init`
@`.rel.text` 0xfba00/0xfba04). So `cmd 0x178` is a *driver-config* command, not an `alg` command.

The output path literal is in `hi5622v100_wifi.ko` at file offset `0x1bd7f4`:
`/config/work/firmware/reg_all.txt`. The same function logs
`Reading register!` / `Reading part of soc_register!` / `Reading %d G Register!`
(file offsets 0x1bd860 / 0x1bd874 / 0x1bd894).

The user-facing entry point lives in the shipped debug module `/lib/hisilicon/ko/wifi_debug.ko`
(the same blob is in the local vendor rootfs at
`rootfs-2.4.15/squashfs-root/lib/hisilicon/ko/wifi_debug.ko`).
`wal_hipriv_get_all_reg_value` `.text+0x1c0b8`
(424 B) parses one argument (`strcmp(arg, "all")`; else `2g`/`5g` select a band), builds a
config message with `strh 0x178` at `[sp+0xc]` plus the band byte, and calls `wal_send_cfg_event`.
The command name `get_all_regs` is in that module's extended hipriv table
(`g_ast_hipriv_cmd_debug`), along with `reginfo` -> `wal_hipriv_reg_info` and
`regwrite` -> `wal_hipriv_reg_write`.

Chain, summarised:

```
hipriv "Hisilicon0 get_all_regs all"          (writes /sys/hisys/hipriv)
  -> wal_hipriv_sys_write / wal_hipriv_parse_cmd        (wifi_debug.ko)
  -> wal_hipriv_get_all_reg_value .text+0x1c0b8         (wifi_debug.ko)  builds cfg cmd 0x178
  -> wal_send_cfg_event -> wal_recv_config_cmd          (base wifi.ko, drv-cfg hook)
  -> wal_config_process_entry .text+0xfc91c  {cmd 0x178 -> wal_config_get_all_reg_value}
  -> wal_config_get_all_reg_value .text+0x16263c        (base wifi.ko)
  -> hmac_config_get_all_reg_value .text+0x63e40        (base wifi.ko)
  -> shuangta_read_all_reg_info  .text+0x35600  }  writes /config/work/firmware/reg_all.txt
  -> shuangta_read_soc_to_file   .text+0x3617c  }
```

The `wal_send_cfg_event` -> local-dispatch step is inferred from the message layout
(cmd 0x178 and the handler table entry are both verified); everything else is a verified
relocation/xref.

### 1.2 Candidates tried, and why two of them are dead ends

**(a) `alg` command channel - NOT reachable.** The 414-entry alg table
(`g_ast_alg_cfg_process_info_table`, phase 3) has no register-dump command:

```
$ grep -in "reg_value|all_reg|get_reg|reg_all|register" ulw/phase3/alg-commands.md ulw/phase2/alg-dispatch.md
ulw/phase2/alg-dispatch.md:433:   What I could **not** pin down is which concrete registered private-ioctl handler invokes

$ grep -in "soc" ulw/phase3/alg-commands.md
(no output)
```

There is no `soc`/`dump`/`reg` name among the 414 entries, so `iwpriv <dev> alg ...` cannot
produce the dump.

**(b) Base driver's hipriv table - NOT reachable.** The base `hi5622v100_wifi.ko` exposes
`/sys/hisys/hipriv` with 100 commands. Searching it for register/dump names:

```
$ cat /sys/hisys/hipriv | tr -s ' \t' '\n' | grep -i -E 'reg|soc|dump'
get_unassoc_sta_rssi
set_regdomain_pwr
```

No `reg_info`, `regwrite` or `get_all_regs`. (Full table is 100 entries; the only reg-ish names
are the two above.)

**(c) `wifi_debug.ko` - WORKS.** The module exists on the device but was not loaded:

```
$ lsmod | grep -i debug
(no output)

$ insmod /lib/hisilicon/ko/wifi_debug.ko
$ echo "rc=$?"
rc=0
$ lsmod | grep -i debug
wifi_debug            364544  0

$ cat /sys/hisys/hipriv | tr -s ' \t' '\n' | grep -i -E 'reg|soc|dump'
stat_dump
dump_tx_ppdu_dscr
dump_rx_ppdu_dscr
dump_timer
dump_ba_bitmap
dump_rx_dscr
dump_tx_dscr
dump_memory
set_regdomain_pwr_p
lpm_soc_mode
get_all_regs
reginfo
regwrite
dump_all_dscr
```

### 1.3 The actual trigger (exact commands / exact outputs)

```
$ mkdir -p /config/work/firmware
mkdir_rc=0
$ ls -la /config/work/
drwxrwx--x    4 root     root             0 Sep 30 22:19 .
drwxr-xr-x    6 root     root            66 Jul 27 04:23 ..
drwxr-xr-x    2 root     root             0 Sep 30 22:19 firmware

$ echo "Hisilicon0 get_all_regs all" > /sys/hisys/hipriv
write_rc=0
$ sleep 3
$ ls -la /config/work/firmware/
-rwxr-xr-x    1 root     root        709089 Sep 30 22:19 reg_all.txt

$ sha256sum /config/work/firmware/reg_all.txt
a8461c98a464a8368bcb91969ddeb609a6d6d0083c124b76bc09a940b336d6f4  /config/work/firmware/reg_all.txt
```

`/config/work/firmware/` did not exist before; it was created by hand so the driver's
`/config/work/firmware/reg_all.txt` path could be opened. The command itself only *reads* MMIO
(via `oal_pcie_devca_to_hostva` + `ldr`) and writes the file - no radio-state change.

---

## 2. The captured dump

### 2.1 Location, size, hash, structure

| item | value |
|---|---|
| on-device path | `/config/work/firmware/reg_all.txt` |
| size | 709,089 bytes |
| lines | 25,166 (12 non-data header lines + 25,154 `addr = ..., value = ...` lines) |
| local copy | `C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/build/register-dumps/reg_all.txt` |
| sha256 | `a8461c98a464a8368bcb91969ddeb609a6d6d0083c124b76bc09a940b336d6f4` (device value == local value) |

Format is plain ASCII, one u32 per line: `addr = <hex CA>, value = <hex>`. The file is split by
header lines into the three groups of `hmac_config_get_all_reg_value`:

```
$ head -2 reg_all.txt
Reading register!
Reading part of soc_register!
$ grep -n -v '^addr = ' reg_all.txt
1:Reading register!
2:Reading part of soc_register!
4001:Reading 2 G Register!
4002:Reading 2g_soc_register!
5048:Reading 2g_rf_and_abb_register!
5927:Reading 2g_mac_register!
10990:Reading 2g_phy_register!
14571:Reading 5 G Register!
14572:Reading 5g_soc_register!
15480:Reading 5g_rf_and_abb_register!
16516:Reading 5g_mac_register!
21579:Reading 5g_phy_register!
```

### 2.2 Per-window table

The 11 `shuangta_read_soc_to_file` windows of `mmio-map.md` S2.2 all appear under
`Reading part of soc_register!` (the cell end is the last dumped address; the mmio-map end is
exclusive), **plus one extra 13-word run at address `0x00000000..0x00000030`** (see S4.1).
The `2g_*` / `5g_*` groups are wider than the 8 windows listed in `mmio-map.md` S2.2: they also
sweep the 2.4/5 GHz MAC blocks (`.rodata` block table, mmio-map S2.2/S4.4) and the PHY blocks.

Columns: `words` = u32 count in the contiguous run; `non-zero` = count with a non-zero value;
`changed run1->run2` = addresses whose value differed between the two dumps; `pattern` derived
from that (0 changed = static/config; some changed = live/counter candidate).

| section | window (start..end) | words | non-zero | changed run1->run2 | pattern |
|---|---|---:|---:|---:|---|
| Reading part of soc_register! | `0x40000000..0x40000660` | 409 | 65 | 4 | mixed: 4 of 409 move |
| Reading part of soc_register! | `0x40002000..0x400024bc` | 304 | 44 | 0 | static (config/ID) |
| Reading part of soc_register! | `0x40003000..0x40003444` | 274 | 39 | 0 | static (config/ID) |
| Reading part of soc_register! | `0x40004000..0x40004250` | 149 | 11 | 0 | static (config/ID) |
| Reading part of soc_register! | `0x40100000..0x40100638` | 399 | 55 | 0 | static (config/ID) |
| Reading part of soc_register! | `0x40030000..0x40030490` | 293 | 37 | 1 | mixed: 1 of 293 move |
| Reading part of soc_register! | `0x00000000..0x00000030` | 13 | 13 | 0 | static (config/ID) |
| Reading part of soc_register! | `0x4003a000..0x4003ac30` | 781 | 113 | 24 | mixed: 24 of 781 move |
| Reading part of soc_register! | `0x40039000..0x40039558` | 343 | 23 | 1 | mixed: 1 of 343 move |
| Reading part of soc_register! | `0x40039800..0x40039d58` | 343 | 22 | 1 | mixed: 1 of 343 move |
| Reading part of soc_register! | `0x40101000..0x40101930` | 589 | 79 | 0 | static (config/ID) |
| Reading part of soc_register! | `0x40031000..0x40031190` | 101 | 9 | 0 | static (config/ID) |
| Reading 2g_soc_register! | `0x40108000..0x40108618` | 391 | 34 | 3 | mixed: 3 of 391 move |
| Reading 2g_soc_register! | `0x40110000..0x40110a34` | 654 | 32 | 0 | static (config/ID) |
| Reading 2g_rf_and_abb_register! | `0x40109000..0x40109098` | 39 | 29 | 0 | static (config/ID) |
| Reading 2g_rf_and_abb_register! | `0x4010a000..0x4010ad18` | 839 | 621 | 0 | static (config/ID) |
| Reading 2g_mac_register! | `0x40040000..0x400400ac` | 44 | 15 | 0 | static (config/ID) |
| Reading 2g_mac_register! | `0x40042000..0x40042928` | 587 | 127 | 0 | static (config/ID) |
| Reading 2g_mac_register! | `0x40044000..0x40044abc` | 688 | 128 | 7 | mixed: 7 of 688 move |
| Reading 2g_mac_register! | `0x40046000..0x40046ffc` | 1024 | 25 | 0 | static (config/ID) |
| Reading 2g_mac_register! | `0x40048000..0x40048e4c` | 916 | 120 | 0 | static (config/ID) |
| Reading 2g_mac_register! | `0x4004a000..0x4004ae5c` | 920 | 3 | 0 | static (config/ID) |
| Reading 2g_mac_register! | `0x4004c000..0x4004c920` | 585 | 53 | 6 | mixed: 6 of 585 move |
| Reading 2g_mac_register! | `0x40050000..0x40050040` | 17 | 8 | 0 | static (config/ID) |
| Reading 2g_mac_register! | `0x40052000..0x400522d0` | 181 | 36 | 34 | mixed: 34 of 181 move |
| Reading 2g_mac_register! | `0x40054000..0x4005418c` | 100 | 52 | 6 | mixed: 6 of 100 move |
| Reading 2g_phy_register! | `0x40080000..0x4008001c` | 8 | 3 | 0 | static (config/ID) |
| Reading 2g_phy_register! | `0x40080800..0x40080de0` | 377 | 289 | 0 | static (config/ID) |
| Reading 2g_phy_register! | `0x40081000..0x400813f8` | 255 | 157 | 64 | mixed: 64 of 255 move |
| Reading 2g_phy_register! | `0x40081800..0x40081c70` | 285 | 236 | 1 | mixed: 1 of 285 move |
| Reading 2g_phy_register! | `0x40082000..0x4008253c` | 336 | 287 | 19 | mixed: 19 of 336 move |
| Reading 2g_phy_register! | `0x40082800..0x400829ec` | 124 | 89 | 0 | static (config/ID) |
| Reading 2g_phy_register! | `0x40083000..0x400833e8` | 251 | 45 | 0 | static (config/ID) |
| Reading 2g_phy_register! | `0x40083800..0x400839b0` | 109 | 14 | 7 | mixed: 7 of 109 move |
| Reading 2g_phy_register! | `0x40090000..0x40090004` | 2 | 1 | 0 | static (config/ID) |
| Reading 2g_phy_register! | `0x40090800..0x40090ba4` | 234 | 198 | 11 | mixed: 11 of 234 move |
| Reading 2g_phy_register! | `0x40091000..0x40091604` | 386 | 96 | 1 | mixed: 1 of 386 move |
| Reading 2g_phy_register! | `0x40091800..0x40091bb4` | 238 | 234 | 0 | static (config/ID) |
| Reading 2g_phy_register! | `0x400a0000..0x400a03f0` | 253 | 38 | 1 | mixed: 1 of 253 move |
| Reading 2g_phy_register! | `0x400a0400..0x400a07ec` | 252 | 213 | 1 | mixed: 1 of 252 move |
| Reading 2g_phy_register! | `0x400a0800..0x400a0b58` | 215 | 62 | 0 | static (config/ID) |
| Reading 2g_phy_register! | `0x400a0c00..0x400a0ff8` | 255 | 142 | 0 | static (config/ID) |
| Reading 5g_soc_register! | `0x4010c000..0x4010c618` | 391 | 38 | 3 | mixed: 3 of 391 move |
| Reading 5g_soc_register! | `0x40114000..0x4011480c` | 516 | 30 | 0 | static (config/ID) |
| Reading 5g_rf_and_abb_register! | `0x4010d000..0x4010d104` | 66 | 56 | 0 | static (config/ID) |
| Reading 5g_rf_and_abb_register! | `0x4010e000..0x4010ef20` | 969 | 693 | 2 | mixed: 2 of 969 move |
| Reading 5g_mac_register! | `0x40060000..0x400600ac` | 44 | 15 | 0 | static (config/ID) |
| Reading 5g_mac_register! | `0x40062000..0x40062928` | 587 | 127 | 0 | static (config/ID) |
| Reading 5g_mac_register! | `0x40064000..0x40064abc` | 688 | 131 | 8 | mixed: 8 of 688 move |
| Reading 5g_mac_register! | `0x40066000..0x40066ffc` | 1024 | 25 | 0 | static (config/ID) |
| Reading 5g_mac_register! | `0x40068000..0x40068e4c` | 916 | 120 | 0 | static (config/ID) |
| Reading 5g_mac_register! | `0x4006a000..0x4006ae5c` | 920 | 3 | 0 | static (config/ID) |
| Reading 5g_mac_register! | `0x4006c000..0x4006c920` | 585 | 52 | 2 | mixed: 2 of 585 move |
| Reading 5g_mac_register! | `0x40070000..0x40070040` | 17 | 8 | 0 | static (config/ID) |
| Reading 5g_mac_register! | `0x40072000..0x400722d0` | 181 | 38 | 38 | mixed: 38 of 181 move |
| Reading 5g_mac_register! | `0x40074000..0x4007418c` | 100 | 54 | 11 | mixed: 11 of 100 move |
| Reading 5g_phy_register! | `0x400b0000..0x400b0028` | 11 | 3 | 0 | static (config/ID) |
| Reading 5g_phy_register! | `0x400b0800..0x400b0de0` | 377 | 287 | 0 | static (config/ID) |
| Reading 5g_phy_register! | `0x400b1000..0x400b13f8` | 255 | 141 | 0 | static (config/ID) |
| Reading 5g_phy_register! | `0x400b1800..0x400b1c70` | 285 | 235 | 2 | mixed: 2 of 285 move |
| Reading 5g_phy_register! | `0x400b2000..0x400b253c` | 336 | 291 | 28 | mixed: 28 of 336 move |
| Reading 5g_phy_register! | `0x400b2800..0x400b29ec` | 124 | 89 | 0 | static (config/ID) |
| Reading 5g_phy_register! | `0x400b3000..0x400b33e8` | 251 | 33 | 0 | static (config/ID) |
| Reading 5g_phy_register! | `0x400b3800..0x400b39b0` | 109 | 21 | 8 | mixed: 8 of 109 move |
| Reading 5g_phy_register! | `0x400c0000..0x400c0014` | 6 | 2 | 0 | static (config/ID) |
| Reading 5g_phy_register! | `0x400c0800..0x400c0ba4` | 234 | 199 | 14 | mixed: 14 of 234 move |
| Reading 5g_phy_register! | `0x400c1000..0x400c1604` | 386 | 98 | 1 | mixed: 1 of 386 move |
| Reading 5g_phy_register! | `0x400c1800..0x400c1bb4` | 238 | 234 | 0 | static (config/ID) |
| Reading 5g_phy_register! | `0x400d0000..0x400d03f0` | 253 | 112 | 0 | static (config/ID) |
| Reading 5g_phy_register! | `0x400d0400..0x400d07ec` | 252 | 225 | 1 | mixed: 1 of 252 move |
| Reading 5g_phy_register! | `0x400d0800..0x400d0b58` | 215 | 194 | 0 | static (config/ID) |
| Reading 5g_phy_register! | `0x400d0c00..0x400d0ff8` | 255 | 141 | 0 | static (config/ID) |

### 2.3 Value patterns: identity markers, counters vs configuration

**Block-ID words.** The first word of most `soc`/MAC/PHY blocks is a small sequential identifier,
not a normal register. Ordered by value they are `0x100,0x101,0x102,0x103,0x104,0x105,0x106,
0x107,0x108,0x109,0x10a,0x10b,0x10c,0x10d,0x10e,0x112` (0x10f / 0x110 / 0x111 are absent from
this dump). Examples (address = value):

```
0x40000000=101  0x40002000=102  0x40004000=103  0x40100000=104  0x40030000=105
0x4010c000=106  0x40108000=107  0x40114000=108  0x40110000=109  0x4003a000=10a
0x40039000=10b  0x40039800=10c  0x40101000=10d  0x40031000=10e  0x40003000=112
0x40040000=100  0x40060000=100  (the two MAC base blocks share 0x100)
```

Two more clear patterns inside the dump:

* Many blocks end in a "signature" word pair, e.g. `0x40108618 = 0xaaaa` and
  `0x4010c618 = 0xaaaa`, and the RF/ABB blocks end in `...8591 7782` / `...7778 7784`.
* Dense but *static* configuration lives in the `rf_and_abb`, MAC and PHY blocks
  (e.g. `0x4010a000..0x4010ad18` is 74% non-zero yet 0 words changed between the two dumps).

**Counters.** The two dumps were taken seconds apart; **310 addresses changed value**. The
changed addresses cluster into exactly the blocks you would expect to tick (MAC queues, PHY
RX/TX statistics), and several groups are *replicated across 16 or 32 identical words*, i.e.
per-queue / per-lane counter banks. Representative groups (run1 -> run2):

| block | addresses | run1 -> run2 | note |
|---|---|---|---|
| 2g MAC | `0x40052038..0x40052090` (timestamps), `0x40052268..0x400522a4` (16 words, all identical) | `0x2b34 -> 0x2b5d` (+41) | 16-deep replicated counter bank |
| 2g MAC | `0x40044044..0x40044050`, `0x40044048` etc. | `0x0..0x4 -> 0xb..0xf` (+11) | small monotonic counters |
| 2g PHY | `0x40081200..0x4008127c` (32 words), `0x40081280..0x400812fc` (32 words) | `0xa6bf0 -> 0xa494d`, `0x8493d -> 0x84ab7` | 32-deep replicated RX/TX statistic banks |
| 2g PHY | `0x400909a8..0x400909c4`, `0x40090a24..0x40090a2c` | e.g. `0xf33 -> 0x3087` | per-antenna / per-chain stats |
| 5g MAC | `0x40072268..0x400722a4` (16 words, all identical) | `0x147825 -> 0x14e2a7` | 16-deep replicated bank |
| 5g MAC | `0x400740b0..0x4007412c`, `0x40072038..0x40072090` | monotonic changes | queue/timestamp counters |
| 5g PHY | `0x400b24b8..0x400b24d8`, `0x400b3930..0x400b3960`, `0x400c09a8..0x400c09c4` | small +/- deltas | PHY stat banks |

Conversely, the entire `soc_register` group (the `shuangta_read_soc_to_file` /
`shuangta_read_all_reg_info` windows of `mmio-map.md`) is essentially **static**: only 4 words
move in `0x40000000..0x40000660` and 1 each in a couple of `0x40039xxx` windows, out of ~4,000
words. Those windows are configuration/ID/PCIe-message registers, not counters. So within this
one dump the useful split is: `soc*` + `rf_and_abb*` = configuration; `*_mac_*` and `*_phy_*`
upper address blocks = live counters/status.

Note: `0x40000000=0x101`, `0x40000004=0x110`, `0x40000008=0x2`, `0x40000108=0x5a5a`,
`0x4000010c=0xdeaf`, `0x40000110=0xf6c4` are the only non-zero words in the first 64 of the
first window; nothing there looks like a free-running counter.

---

## 3. Raw artifact and hashes

Saved under `C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/build/register-dumps/`:

| file | bytes | sha256 |
|---|---:|---|
| `reg_all.txt` (reference dump, run 1) | 709089 | `a8461c98a464a8368bcb91969ddeb609a6d6d0083c124b76bc09a940b336d6f4` |
| `reg_all_run2.txt` (counter-diff run, ~seconds later) | 709133 | `382db1fe622ed5d60374ca11742e258658d64fbd2073b1d76753dea1d9c5989c` |

Verification that the local copy is byte-identical to the device file:

```
device : sha256sum /config/work/firmware/reg_all.txt
         a8461c98a464a8368bcb91969ddeb609a6d6d0083c124b76bc09a940b336d6f4  /config/work/firmware/reg_all.txt
local  : sha256sum build/register-dumps/reg_all.txt
         a8461c98a464a8368bcb91969ddeb609a6d6d0083c124b76bc09a940b336d6f4 *build/register-dumps/reg_all.txt
```

The device file was left in place (containing run 2 after the second trigger); the reference
copy is the run-1 file above.

---

## 4. Unresolved / caveats (honest)

### 4.1 The 13-word run at address `0x00000000..0x00000030`
Inside `Reading part of soc_register!`, between the `0x40030000..0x40030490` and
`0x4003a000..0x4003ac30` runs, the dump prints 13 words at CA `0x0..0x30`:

```
addr = 0, value = e59ff018
addr = 4, value = e59ff018
addr = 8, value = e59ff018
addr = c, value = e59ff018
addr = 10, value = e59ff018
addr = 14, value = e320f000
addr = 18, value = e59ff014
addr = 1c, value = e59ff014
addr = 20, value = 8c
addr = 24, value = 3c
addr = 28, value = 4c
addr = 2c, value = 5c
addr = 30, value = 6c
```

The values are ARM instruction encodings (`e59ff018` = `ldr pc,[pc,#0x18]`, `e320f000` = `nop`),
i.e. one window was read through a translation of device CA 0 that resolved to driver `.text`
instead of silicon. This is a driver bug/quirk in the window list (12 runs printed where
`mmio-map.md` S2.2 documents 11 `shuangta_read_soc_to_file` windows). **I did not establish which
sub-window is mis-based.** Treat those 13 lines as an artifact, not registers.

### 4.2 `rmmod wifi_debug` was followed by a device reboot
Cleanup command and observation:

```
$ lsmod | grep -i wifi_debug          # before
wifi_debug            364544  0
$ rmmod wifi_debug
rmmod_rc=0
                               <-- ssh session: "Connection reset by peer"
$ ping -n 1 192.168.10.1        # 3 x 100% loss, then replies at ~20 s
$ ssh root@192.168.10.1 'uptime'
 21:52:39 up 0 min,  load average: 2.20, 0.56, 0.19
```

The device rebooted about 20 s after `rmmod` (uptime `0 min`; fresh boot dmesg). I can state the
correlation but not the exact fault (no panic string was captured because the reset cut the
session). After the reboot the module is **absent** (`lsmod | grep wifi_debug` -> no match), so
the debug interface is removed and stays removed; the base Wi-Fi driver came up normally. If the
dump is needed again, load `wifi_debug.ko`, trigger, copy the file off, then **reboot** to drop
the module rather than `rmmod` it.

### 4.3 No tracefs probes
No kprobe/uprobe was created in this phase. Verified after the reboot:

```
$ cat /sys/kernel/debug/tracing/kprobe_events
(empty)
```

### 4.4 Windows beyond `mmio-map.md`
The `2g_mac_register` and `5g_mac_register` / `*_phy_register` sections cover block ranges that
`ulw/phase4/mmio-map.md` only lists as `.rodata` block tables (S2.2/S4.4), not as the 8
`shuangta_read_all_reg_info` windows. The dump is therefore a superset of the window list in
`mmio-map.md` S2.2; the exact function that emits the MAC/PHY windows was not chased down here
(out of timebox).

---

## Appendix A - exact reproduce recipe

```sh
# 0. prerequisites on the workstation (from the task recipe)
printf '#!/bin/sh\necho RouterRoot-9x\n' > /tmp/askpass.sh; chmod +x /tmp/askpass.sh
sshx() { cd /tmp && SSH_ASKPASS=/tmp/askpass.sh SSH_ASKPASS_REQUIRE=force DISPLAY=:0 \
  timeout 120 ssh -o PubkeyAuthentication=no -o PreferredAuthentications=password \
  -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o ConnectTimeout=10 root@192.168.10.1 "$@"; }

# 1. load the vendor debug module (adds the get_all_regs hipriv command)
sshx 'insmod /lib/hisilicon/ko/wifi_debug.ko; echo rc=$?; lsmod | grep wifi_debug'

# 2. capture
sshx 'mkdir -p /config/work/firmware; echo "Hisilicon0 get_all_regs all" > /sys/hisys/hipriv; sleep 3; \
      ls -la /config/work/firmware/reg_all.txt; sha256sum /config/work/firmware/reg_all.txt'

# 3. pull it back
mkdir -p build/register-dumps
sshx 'cat /config/work/firmware/reg_all.txt' > build/register-dumps/reg_all.txt
sha256sum build/register-dumps/reg_all.txt     # must equal the device hash

# 4. cleanup - reboot (do NOT rmmod; see S4.2)
sshx 'reboot'
# after ~30 s verify the module is gone:
sshx 'lsmod | grep wifi_debug; echo rc=$?'
```

Live-interface candidates that were checked and are dead ends for this dump:

* `iwpriv Hisilicon0 alg <name>` - the 414-name alg table has no register-dump entry.
* base `/sys/hisys/hipriv` (100 commands) - no `get_all_regs` / `reginfo` / `regwrite`.
* `hipriv "Hisilicon0 get_all_regs all"` only exists after `wifi_debug.ko` is loaded.
