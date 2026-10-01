# endpoint-init: the vendor's endpoint-init / firmware-download sequence, and our first safe steps (phase 16, 2026-10-01)

This document has two parts and the test-boot record:

- **Part A** - the vendor's sequence, recovered from `hi5622v100_plat.ko` (full `.symtab`,
  `build/register-dumps/teardown/hi5622v100_plat.ko`, md5 `23660bc285393e678d5cade1c36c194b`).
  Every step is a disassembly quotation; steps are marked **[proven]** (a constant in the
  instruction stream / a resolved relocation) or **[inferred]** (control flow or table
  semantics, not a literal).
- **Part B** - `lab/epinit/epinit.c`: the provably-safe prefix of that sequence on an unowned
  chip, with every write read back, and a deliberate stop before the firmware transfer.
- **Test record** - the staging, the test boot, and the recovery boot.

The conclusion that drives everything: **the vendor download is not a memcpy of `FIRMWARE.bin`
into BAR0.** It is `firmware_file_send -> bal_write(chip, dev_addr, buf, len)`, a chunked
transfer through the vendor's BAL/HCC DMA + message engine to a device-side window near
`0x01240000`, driven by the ETE rings and the message registers. That path is not
unambiguous from the disassembly, so Part B does not attempt it; it implements the
config-space claim/init and stops.

The evidence for the "raw memcpy is wrong" claim is already in `boot-takeover.md` §3.1/§3.4
(a 928,920-byte write to `BAR0+0x40000` soft-locks at `BAR0+0x100000`; a bounded 768 KiB
prefix completes but does not verify). This document explains *why*.

---

## Part A - the vendor's sequence

### A.0 Method

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko
$PY lab/ko_mmio.py "$KO"                    # BAR-range constants per function
$PY lab/ko_disasm.py "$KO" <func> ...       # function disassembly with relocations named
```

`lab/ko_disasm.py` (added this phase) annotates every `bl`/`b`/literal relocation with the
symbol it targets, which is what makes the call graph and the accessor calls readable.
Raw dumps: `build/register-dumps/endpoint-init/dump_*.txt`.

### A.1 The load chain (proven by `bl` relocations)

`init_module` @ `0x1a75c`:

```
  0x1a760: bl -> plat_res_init
  0x1a764: bl -> oal_main_init
  0x1a770: bl -> oam_main_init
  0x1a77c: bl -> sdt_drv_main_init
  0x1a788: bl -> low_power_init
  0x1a794: bl -> plat_hcc_init
  (on error: plat_exception_exit, sdt_drv_main_exit, oam_main_exit, oal_main_exit)
```

`plat_hcc_init` @ `0x1a690`:

```
  0x1a694: bl -> plat_custom_init
  0x1a6a0: bl -> plat_main_init
  0x1a6ac: bl -> pcie_init_static_res
  0x1a6b0: bl -> plat_debug_sysfs_init
  0x1a6bc: bl -> bal_init
  0x1a6c8: bl -> hcc_init
  0x1a6d4: bl -> plat_exception_init
```

`plat_main_init` @ `0x3054` `bl`s `hwifi_config_init` (`0x3070/0x3078/0x3080`).
`pcie_main_init` @ `0x704` is not reached by a `bl`; it is the `.init` slot of a bus-ops
struct. The relocation table proves it:

```
.rel.data @0x2904 -> pcie_main_init        (slot 1 of g_st_pcie_bus_driver @0x2900)
.rel.data @0x1e74 -> oal_pcie_probe        (probe of the pci_driver struct, id table @0x1e70)
.rel.data @0x1e78 -> oal_pcie_remove
```

So the vendor path is: module init -> `plat_hcc_init` -> `bal_init`/`hcc_init` bring up the
bus abstraction, which invokes the registered `g_st_pcie_bus_driver.init = pcie_main_init`;
`pcie_main_init` registers the `pci_driver` whose probe is `oal_pcie_probe`.

### A.2 `pcie_main_init` @ `0x704` - host/RC bring-up, register `pci_driver`, PHY trim

Ordered steps (disassembly, `.text.unlikely`):

1. `pcie_init_default()`:

```
  0x0768: bl -> pcie_init_default
  ...
  0x8128: pcie_init_default:
  0x812c: mov  r1, #1
  0x8130: bl   -> pcie_via_gpio_power_cfg      ; arg=1: power the endpoint on
```

2. `pcie_bus_getnum()`, then register the driver (`r7 = .LANCHOR3` holds the `pci_driver`):

```
  0x0790: bl -> pcie_bus_getnum
  0x079c: str r0, [r5, #0x58]
  0x07ac: bl -> __pci_register_driver           ; (r0=&driver, r1=__this_module, r2="hi5622v100_plat")
  .LC68 = "hi5622v100_plat"
```

3. `pcie_get_res()` -> `pci_dev_res_init()` -> per-device `oal_pci_lres_init()`:

```
  0x0830: bl -> get_pcie_res
  0x0868: bl -> pci_dev_res_init
  0x0821c: pci_dev_res_init:
  0x0258: bl -> oal_pci_lres_init               ; once per enumerated endpoint
```

4. Conditional remap enable (only when probe count > 1 and `[res+0x10] != 0`):

```
  0x08b8: ldr  r3, [r5, #0x6c]                  ; probe/device count
  0x08c0: cmp  r3, #1
  0x08c8: ldr  r3, [r8, #0x10]
  0x08dc: bl -> oal_pcie_enable_remap.part.0
  ...   -> shuangta_pcie_enable_remap (see A.6)
```

5. ETE rings + comms:

```
  0x08e8: bl -> pcie_ete_init
  0x0944: bl -> pcie_comm_init
  ...
  0x171d0: pcie_comm_init: bl -> pcie_thread_init ; then tail-b -> pcie_msg_init
```

6. PHY voltage trim at device address `0x40002210` **[proven constant]**:

```
  0x0990: movw r1, #0x2210
  0x0994: movt r1, #0x4000          ; r1 = 0x40002210
  0x0998: bl   -> oal_pcie_devca_to_hostva     ; device CA -> host VA
  0x09b8: ldr  r7, [r3]                        ; read
  0x09c0: and  r7, r7, #0x3f
  0x09c4: orr  r7, r7, #0x180
  0x09cc: bl   -> arm_heavy_mb
  0x09d4: str  r7, [r3]                        ; write (old & 0x3f) | 0x180
  .LC77 = "[PCIEL]oal_pcie_set_voltage to 0.875V"
```

7. Reads/creates the linkdown flag file (not hardware):

```
  0x096c: movw r0, -> .LC75
  0x0974: bl   -> filp_open
  .LC75 = "/config/work/firmware/pcie0_linkdown.txt"
```

### A.3 `oal_pci_lres_init` @ `0x7bb0` - the endpoint claim (config space)

```
  0x07be0: bl -> pcie_get_bus_id_host_view
  0x07c30: bl -> pcie_get_chip_id
  0x07c5c: bl -> pci_enable_device             ; [proven] sets PCI_COMMAND MEM/IO
  0x07c7c: str r7, [r6, #0x14]
  0x07c94: pci_read_config_byte(dev, 8, sp+3)  ; [proven] offset 0x08 = revision (read-only)
  0x07cc8: bl -> oal_pcie_host_init
  0x07ce8: bl -> oal_pcie_dev_init
  0x07d08: bl -> oal_pcie_probe_irq_init       ; do_request_irq
```

`oal_pcie_host_init` @ `0xbefc` **[proven reads]**:

```
  0x0bf1c: movw r1, #0xff8
  0x0bf30: bl   -> pci_read_config_dword(dev, 0xff8, &out)   ; vendor/RC capability dword
  0x0bfc0: bl   -> oal_pcie_change_link_state(prt, 2)
```

`oal_pcie_dev_init` @ `0xa090` maps the host/RC regions (not the endpoint BAR):

```
  0x0a1d4: bl -> pcibios_resource_to_bus
  0x0a2a0: movw r0 -> iomem_resource
  0x0a2a8: bl -> __request_region
  0x0a2bc: bl -> ioremap
```

### A.4 Inbound iATU configuration (config-space dwords) **[proven constants]**

`oal_pcie_set_inbound_by_viewport` @ `0x97a8`: for each inbound region, switch the viewport,
program the region, and finally **write the Command register to 7**:

```
  0x97d8: bl -> pcie_inbound_viewport_switch(dev, i, region)
  0x97f8: bl -> pcie_inbound_region_cfg(dev, i, region)
  0x9814: mov r2, #7
  0x9818: mov r1, #4
  0x981c: bl -> pci_write_config_word(dev, 4, 7)   ; PCI_COMMAND = IO|MEM|MASTER
```

`pcie_inbound_viewport_switch` @ `0x9264`:

```
  0x9274: orr  r7, r1, #0x80000000
  0x928c: movw r1, #0x900
  0x929c: bl -> pci_write_config_dword(dev, 0x900, idx|0x80000000)
  0x92b4: bl -> pci_read_config_dword(dev, 0x900, &v)      ; read-back check
  0x92d0: movw r1, #0x908
  0x92d8: bl -> pci_write_config_dword(dev, 0x908, 0)
  0x92f4: and  r2, r2, #7
  0x92f8: lsl  r2, r2, #8
  0x92fc: orr  r2, r2, #0x80000000
  0x9300: bl -> pci_write_config_dword(dev, 0x908, (byte&7)<<8 | 0x80000000)
```

`pcie_inbound_region_cfg` @ `0x94e0` writes the region descriptor at offset `0x90c`
(`0x9518: movw r1, #0x90c; bl pci_write_config_dword`), then the base/limit/target dwords
that follow it.

**[measured, endpoint 59e7:0005]** these offsets are *not* implemented on the endpoint's own
config space: the test boot read `cfg[0x900]=0xffffffff`, `cfg[0x904]=0`, `cfg[0x908]=0`,
`cfg[0x90c]=0`. They are the *root complex*'s iATU viewport registers, i.e. the A.4 writes
target the RC, not this endpoint. `epinit` therefore reads them (documented) but never writes
them to the endpoint.

### A.5 L1-substates (config space + BAR0 registers) **[proven constants]**

`shuangta_pcie_l1ss_set` @ `0x1aeb8` (called from the bus ops; `clear` is the inverse at
`0x1ac2c`):

```
  0x1aee4: mov r1, #0x80
  0x1aee8: mov r2, #0x43
  0x1aeec: movt r2, #0x1012
  0x1af00: bl -> pci_write_config_dword(dev, 0x80, 0x10120043)

  0x1af0c: movw r1, #0x9220 ; movt r1, #0x4003   ; 0x40039220
  0x1af14: bl -> oal_pcie_devca_to_hostva
  0x1af24: bl -> arm_heavy_mb
  0x1af28: mov sb, #0x40
  0x1af30: str sb, [r3]                            ; BAR0+0x39220 = 0x40

  0x1af34: movw r1, #0x92d0 ; movt r1, #0x4003   ; 0x400392d0
  0x1af44: bl -> oal_pcie_devca_to_hostva
  0x1af54: bl -> arm_heavy_mb
  0x1af58: mov r6, #7
  0x1af60: str r6, [r3]                            ; BAR0+0x392d0 = 0x7

  0x1af64: movw r1, #0x158
  0x1af6c: mov r2, #0xc ; movt r2, #0x40a0
  0x1af74: bl -> pci_write_config_dword(dev, 0x158, 0x40a0000c)
  0x1af80: movw r1, #0x98
  0x1af7c: mov r2, #0x400
  0x1af84: bl -> pci_write_config_dword(dev, 0x98, 0x00000400)

  ; repeated for the second endpoint (dev = [res+4]):
  0x1afb4: movw r1, #0x9a20 ; movt r1, #0x4003   ; 0x40039a20 -> 0x40
  0x1afd8: movw r1, #0x9ad0 ; movt r1, #0x4003   ; 0x40039ad0 -> 0x7
```

### A.6 Remap enable and message-register map **[proven constants]**

`shuangta_pcie_enable_remap` @ `0x1ae3c`:

```
  0x1ae40: movw r1, #0xa200 ; movt r1, #0x4003    ; 0x4003a200
  0x1ae60: bl -> oal_pcie_devca_to_hostva
  0x1ae6c: dsb st
  0x1ae70: bl -> arm_heavy_mb
  0x1ae74: mov r2, #1
  0x1ae7c: str r2, [r3]                           ; BAR0+0x3a200 = 0x1
```

`shuangta_pcie_msg_reg_map` @ `0x1b1a0` resolves host VAs for six device addresses and stores
them into a struct (`[r4+0x00..0x14]`):

```
  0x1b1b0: movw r1, #0x9010 ; movt r1, #0x4003    ; 0x40039010 -> [r4+0x00]
  0x1b1e8: movw r1, #0x9014 ; movt r1, #0x4003    ; 0x40039014 -> [r4+0x04]
  0x1b20c: movw r1, #0x92d4 ; movt r1, #0x4003    ; 0x400392d4 -> [r4+0x08]
  0x1b230: movw r1, #0x1438 ; movt r1, #0x4010    ; 0x40101438 -> [r4+0x0c]
  0x1b254: movw r1, #0x1414 ; movt r1, #0x4010    ; 0x40101414 -> [r4+0x10]
  0x1b278: movw r1, #0x92f0 ; movt r1, #0x4003    ; 0x400392f0 -> [r4+0x14]
```

These are the message/doorbell registers used by `pcie_msg_init`/`pcie_msg_send`. Note two of
them (`0x40101414`, `0x40101438`) are past `BAR0+0x100000`.

### A.7 The BAR0 window the vendor maps **[proven constant table]**

`g_shuangta_region_types` (.data+0x29d8, 30 x 16 bytes) contains the region list. Entry 16/17:

```
  [16] 0x00000000 0x00000000 0x40000000 0x00000000
  [17] 0x4011ffff 0x00000000 0x40000000 0x00000000
```

i.e. the BAR0 device window is `0x40000000-0x4011ffff` (1.125 MiB). And entry 26/27:

```
  [26] 0x00000000 0x00000000 0x01200000 0x00000000
  [27] 0x01417fff 0x00000000 0x01200000 0x00000000
```

i.e. a second, larger device window `0x01200000-0x01417fff` (2.1 MiB). This is the region the
firmware lands in (A.9).

### A.8 `hwifi_config_init` @ `0x19a0` - config tables, no BAR access **[inferred]**

`plat_main_init` calls `hwifi_config_init(0x11..0x14)`; the dispatcher (`0x19c8: cmp r3,#3;
ldrls pc,[pc,r3,lsl#2]`) routes the four parameter classes into in-memory tables
(`.LANCHOR0`, e.g. `str r1,[r3,#0x14]`, `str r2,[r3,#0x2c]`), populated from
`get_cust_conf_int32` / `hwifi_config_init_nvram` / `original_value_for_dts_params`. No
`pci_*` or `ioremap`/`str` to a BAR appears in it; it is a host-side configuration-stage
function. **[inferred]**: this is the ini/DTS configuration that precedes the hardware
download.

### A.9 The firmware download **[proven structure; target inferred]**

`wlan_pm_open` @ `0xe4c` -> `wlan_power_on` @ `0xe46c`:

```
  0x0eac: bl -> wlan_power_on
  0x0e490: wlan_power_on:
  0x0e494: bl -> wlan_bal_init_process
  0x0e4b0: bl -> hwifi_rf_cali_file_load
  0x0e4d0: bl -> firmware_download_function
```

`firmware_download_function` @ `0xf9f8` builds the descriptor from the per-chip tables
(`.LANCHOR0` descriptors, `.bss`), calls `get_firmware_path` and stores the path, then
`kime_get`, then:

```
  0x0fa78: bl -> get_firmware_path
  0x0fab0: bl -> printk                ; .LC24 "enter firmware_download_function"
  0x0fabc: bl -> firmware_download
  0x0fb1c: bl -> printk                ; .LC26 "firmware_download success"
```

`firmware_download` @ `0xf834`:

```
  0x0f860: bl -> firmware_check_version
  0x0f86c: ldr r3,[r4,#0x10] / [r4,#0xc]      ; file table + count
  0x0f880: bne 0xf8d0                          ; both nonzero -> file-send path
  0x0f8dc: firmware_mem_try_alloc(0x80000, 0x1000, &phys)   ; 512 KiB contiguous
  0x0f930: firmware_file_send(desc, i, buf, phys)           ; i = 0..count-1
```

`firmware_file_send` @ `0xf56c` **[proven]**:

```
  0x0f5d0: ldr r1,[r2, r1, lsl #3]   ; desc[0x10][i].path
  0x0f5e8: bl -> filp_open
  0x0f630: bl -> vfs_llseek           ; SEEK_END -> size
  0x0f64c: bl -> vfs_llseek           ; SEEK_SET
  0x0f6b4: bl -> firmware_kernel_read ; read a chunk into buf
  0x0f704: ldr r3,[r4,#0x10]
  0x0f710: add r3,r3,r2
  0x0f718: ldr r1,[r3,#4]             ; desc[0x10][i].target_addr
  0x0f720: add r1,sl,r1               ; + running offset
  0x0f724: bl -> bal_write            ; bal_write(chip, target+off, buf, n)
```

`bal_write` @ `0x10c04` does **not** touch a BAR; it dispatches to the registered BUS
callback:

```
  0x10c0c: movw r0 -> .LANCHOR1
  0x10c18: ldr ip,[r0,#0x14]          ; bus context
  0x10c1c: ldr lr,[ip,#0x3c]          ; callback != NULL?
  0x10c28: ldr lr,[ip,#0x38]          ; fn
  0x10c2c: ldr r0,[r0,#0x18]          ; handle
  0x10c38: bx  ip                      ; -> HCC/ETE xfer path
```

The per-chip firmware target base is a static table entry at `.data+0x269c`:

```
  .data+0x269c = 0x00000000   (path pointer, filled by get_firmware_path)
  .data+0x26a0 = 0x01240000   (target base for chip 0)
```

and `0x01240000` lies inside the `0x01200000-0x01417fff` window from A.7. **[inferred]**:
the firmware is laid down at device address `0x01240000` in that 2.1 MiB window, through the
BAL/HCC/ETE engine (the same engine `pcie_ete_init`/`pcie_msg_init` set up), not by writing
`FIRMWARE.bin` into `BAR0+0x40000`.

### A.10 Ordered vendor sequence (summary)

| # | step | access | value | status |
| - | ---- | ------ | ----- | ------ |
| 1 | `pcie_via_gpio_power_cfg(1)` | host GPIO | 1 | proven |
| 2 | `__pci_register_driver` (`oal_pcie_probe`) | host | id table `.data+0x1e70` | proven |
| 3 | `pci_enable_device` | cfg `0x04` | MEM/IO | proven |
| 4 | `pci_read_config_byte(dev,8)` | cfg `0x08` | read | proven |
| 5 | `oal_pcie_host_init` | cfg `0xff8` | read | proven |
| 6 | `oal_pcie_dev_init` -> `__request_region`, `ioremap` | host RC | - | proven |
| 7 | iATU viewport select | cfg `0x900` | `idx\|0x80000000` | proven |
| 8 | iATU viewport control | cfg `0x908` | 0, then `(b&7)<<8\|0x80000000` | proven |
| 9 | iATU inbound region | cfg `0x90c`.. | region desc | proven |
| 10 | `pci_write_config_word(dev,4,7)` | cfg `0x04` | 7 | proven |
| 11 | `pcie_ete_init` (rings) | ETE/DMA | runtime | proven (structure) |
| 12 | `pcie_msg_init` (msg regs) | `+0x39010/14`, `+0x392d4/f0`, `+0x101414/38` | runtime | proven map |
| 13 | voltage trim | `BAR0+0x2210` | `(old&0x3f)\|0x180` | proven |
| 14 | remap enable | `BAR0+0x3a200` | 1 | proven |
| 15 | L1SS set | cfg `0x80/0x98/0x158`, `BAR0+0x39220/d0`, `+0x39a20/d0` | `0x10120043`, `0x400`, `0x40a0000c`, `0x40`, `0x7` | proven |
| 16 | `firmware_check_version`, `firmware_mem_try_alloc(0x80000)` | host | - | proven |
| 17 | `firmware_file_send` -> `bal_write(chip, 0x01240000+off, buf, n)` | BAL/HCC/ETE | file bytes | target inferred |
| 18 | `bal_irq_enable`, radio up | - | - | proven |

Not in this module: no direct `memcpy_toio` to any BAR, no `pci_resource_start` reliance.

---

## Part B - our implementation

`lab/epinit/epinit.c` (`lab/epinit/Makefile`: `obj-m := epinit.o`). Built by the existing
GitHub Actions workflow (`.github/workflows/build-load-test-module.yml`, step "build epinit
module", artifact `epinit-ko`).

What it does, in order, and why each step is safe on an unowned chip:

- **stage 1 (default, what the test boot runs):**
  1. `pci_get_domain_bus_and_slot(domain,0,0)` - find the endpoint.
  2. **Read-only** dump of the whole documented config-space set: `0x00,0x04,0x08,0x0c`,
     BAR0-5 `0x10..0x24`, `0x2c,0x30,0x34`, and the vendor offsets `0x80,0x98,0x158,0x900,
     0x904,0x908,0x90c,0xff8`. Nothing is written before this.
  3. `pci_enable_device` (the vendor's own first endpoint write: Command MEM/IO);
     logs `command` before -> after.
  4. `pci_request_mem_regions` - our claim (the vendor relies on probe registration).
  5. `pci_write_config_word(dev, PCI_COMMAND, 0x0007)` - **the vendor's documented init
     write** (`oal_pcie_set_inbound_by_viewport`, cfg `0x04` = IO|MEM|MASTER), read back and
     logged `match=YES/NO`.
  6. Map BAR0 read-only and log the ROM vector page (`BAR0+0x0`, 8 words) - same identity
     words as phase 11/15/16.
  7. **Stop.** No BAR0 register write, no firmware, no reset, no CPU start, no DMA rings.
- **stage 2 (opt-in, `stage=2`, NOT run by the test boot):** the two constant BAR0 register
  writes from A.10 - `BAR0+0x2210 = (old&0x3f)|0x180` and `BAR0+0x3a200 = 1` - each read
  back. Both offsets are inside the first 256 KiB of BAR0, the region the phase-16 test boot
  proved writable, but a single register write has never been proven safe on an unowned
  chip, so it is gated off by default.

This is the boundary the task asks for: **the module stops at the first step that is not
provably safe** (any BAR0 write) rather than reproducing the raw-memcpy failure mode.

### Recovery command - recorded BEFORE any test reboot

Installed on the device at `/root/recover-epinit.sh` and run as `sh /root/recover-epinit.sh`:

```sh
#!/bin/sh
set -x
cd /lib/modules/5.10.201 || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-epinit
rm -f /etc/init.d/omo-epinit
rm -f /lib/modules/5.10.201/epinit.ko
sync
reboot
```

The one-shot loader deletes its own rc.d symlink before `insmod` (same watchdog safety as
phase 16): a hang resets into a reachable boot with the vendor modules still hidden and no
`epinit`, so the recovery command can run. Fallback if the box does not come back: U-Boot
slot A (stock).

---

## Test record

Artifacts in `build/register-dumps/endpoint-init/` (gitignored): `000_baseline.txt`,
`010_staging.txt`, `015_live_precheck.txt`, `020_testboot_evidence.txt`,
`030_recovery_run.txt`, `040_recovery_evidence.txt`, `050_final_state.txt`.
CI: run `36853503238` (commit `d3dd50d`), artifact `epinit-ko`, `epinit.ko` md5
`fa2b77320fbeec62e66690e7e8f8a562`, `vermagic=5.10.201 SMP mod_unload ARMv7`.

### Staging (before the reboot; `010_staging.txt`)

    epinit.ko md5 fa2b77320fbeec62e66690e7e8f8a562  (== CI artifact)
    hi5622v100_wifi.ko.omo-off  e21629d226ec7de9a860a8955952d311  (== baseline)
    hi5622v100_plat.ko.omo-off  23660bc285393e678d5cade1c36c194b  (== baseline)
    /etc/rc.d/S99omo-epinit -> ../init.d/omo-epinit
    sh -n on both scripts: OK

### Test boot (`020_testboot_evidence.txt`) - vendor hidden, `epinit` via S99

    [vendor]:  (none: hi5622v100_wifi + hi5622v100_plat + rox_pci0 absent)
    epinit                 16384  0

    [   39.559586] omo-epinit: pci_enable_device rc=0 command 0x0140 -> 0x0142 [vendor oal_pci_lres_init]
    [   39.568539] omo-epinit: pci_request_mem_regions rc=0 (MEM BARs claimed)
    [   39.575214] omo-epinit: cfg[0x004] <= 0x0007 readback=0x0006 MEM|MASTER=set (I/O bit RO0, no I/O BAR) (oal_pcie_set_inbound_by_viewport)
    [   39.587626] omo-epinit: BAR0 base=0x40000000 (config-space read)
    [   39.593833] omo-epinit: ROM vector page BAR0+0x0: 00000101 00000110 00000002 00000000 00000000 00000000 00000000 00000000
    [   39.605205] omo-epinit: stage=1: BAR0 register writes NOT performed (not provably safe on an unowned chip); stopping here
    [   39.616211] omo-epinit: done - endpoint claimed, config-space init verified, firmware NOT loaded (BAL/HCC download path not implemented)

    0000:00:00.0 enable=1   (our pci_enable_device)     driver: none
    0001:00:00.0 enable=0   (untouched)                driver: none
    S99 rc.d link: removed by the script before insmod (watchdog safety)
    br-lan 192.168.10.1/24 up; SSH up; no Wiphy / 0 wlan ifaces (wifi down by design)
    pstore: no new record (only the older blk-0/2/3 files, mtimes 10:41/10:26/10:34, all pre-test)

Config space read before any write (selected): id `0x000559e7`, command `0x00100140`, revision/class
`0x02800000` (network controller), BAR0 `0x40000004`, BAR2 `0x41800004`, BAR4 `0x41000004`,
subsystem `0x000019e5`, L1SS `0x80=0x10120000`, `0xff8=0x00011521`.

The write and its read-back are both logged. The `0x0006` read-back is the expected behaviour
(bit 0 is read-only-0 with no I/O BAR); `MEM|MASTER` is set, which is exactly the vendor's
intent in `oal_pcie_set_inbound_by_viewport`. No BAR0 write, no firmware, no reset. The box did
not fault and no pstore record was produced.

### Live pre-check (`015_live_precheck.txt`)

The same binary was also `insmod`-ed live in the takeover state (`insmod ... stage=1`, rc=0, left
loaded, `rmmod` rc=0), confirming the S99 boot result independently of the boot path.

### Recovery (`030_recovery_run.txt`, `040_recovery_evidence.txt`, `050_final_state.txt`)

`sh /root/recover-epinit.sh` renamed the modules back, removed
`/etc/rc.d/S99omo-epinit`, `/etc/init.d/omo-epinit` and `/lib/modules/5.10.201/epinit.ko`,
`sync`, `reboot`. The recovered boot:

    hi5622v100_plat  323584  3 hi5622v100_wifi      (same use count as baseline)
    hi5622v100_wifi 3387392  1
    md5 hi5622v100_wifi.ko e21629d226ec7de9a860a8955952d311  (baseline)
    md5 hi5622v100_plat.ko 23660bc285393e678d5cade1c36c194b  (baseline)
    leftovers: epinit.ko / *.omo-off / S99omo-epinit / init.d/omo-epinit all absent
    0000:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
    0001:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
    Wiphy phy0 + phy1, 6 wlan interfaces, /tmp/wifi_done present, iwpriv lists Hisilicon0
    br-lan 192.168.10.1/24 up; PC ping 192.168.10.1: 2/2, 0% loss
    pstore: no new record

**Acceptance state reached: the router is healthy with the vendor stack restored.**

---

## Limits

- **The firmware is not downloaded.** The disassembly shows the vendor's transfer is
  `firmware_file_send -> bal_write(chip, 0x01240000+off, buf, n)` through the BAL/HCC/ETE DMA +
  message engine, not a BAR0 memcpy. That engine (ETE rings, message registers, the per-chip
  device window, `bal_write`'s registered callback) is not implemented in `epinit`, so the
  download path is **not** unambiguous and is deliberately not attempted.
- **The module stops at the first step that is not provably safe.** The config-space claim/init
  is performed and verified; the two constant BAR0 register writes (`0x2210`, `0x3a200`) are
  implemented (`stage=2`) but gated off: a bulk BAR0 write already soft-locked the bus once, and
  a single register write has never been proven safe on an unowned chip.
- **The vendor's iATU writes (cfg `0x900/0x908/0x90c`) are aimed at the RC, not the endpoint.**
  On the endpoint those offsets read `0xffffffff`/`0`; `epinit` reads them but does not write
  them. Reproducing the vendor's iATU setup would mean driving the RC, which `hi_pcie` owns.
- **The test boot is one boot with Wi-Fi intentionally down** (by design), as in phase 16; the
  LAN/SSH path never dropped.
- **Only one endpoint is touched.** `domain=0` (2.4 GHz, `0000:00:00.0`); `0001:00:00.0` is
  left unbound and `enable=0`.
