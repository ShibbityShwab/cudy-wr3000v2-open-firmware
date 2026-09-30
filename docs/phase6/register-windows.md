# Phase 6 - MMIO windows turned into a register map (hi5622v100)

Source (local, read-only; no device access):
- `C:/Users/ShibbityShwab/router-openwrt/build/tmp/hi5622v100_wifi.ko` - sha256 `de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521`
- `C:/Users/ShibbityShwab/router-openwrt/build/tmp/hi5622v100_plat.ko` - sha256 `6f2eac415dbf4d3e8e71c1abd941ea4f9d97b159674d28d5e99d4ecb0e6cd491`

Tooling: `C:/Users/ShibbityShwab/router-openwrt/pyenv/Scripts/python.exe` (pyelftools + capstone 5.0.7, `CS_ARCH_ARM`/`CS_MODE_ARM`).
Both objects are `ET_REL`; **every address below is a section-relative offset** (`st_value` / `sh_offset` inside the named section, usually `.text` = sec1 of each file). Device chip addresses ("CA") are written as absolute 32-bit values because that is how the code materialises them.

Input window list: `ulw/phase4/mmio-map.md` Sec 2.2 (`shuangta_read_all_reg_info` `.text+0x35600`, `shuangta_read_soc_to_file` `.text+0x3617c`); accessor `oal_pcie_devca_to_hostva` (plat `.text+0x6914`).

Register semantics below come from **the driver code that reads/writes each address**, not from the blob dump. Every offset claim carries either a disassembly excerpt or the exact python command that produced it (Appendix A).

---

## 0. Summary of what was established

1. The two dump routines are pure byte-blob loggers: for a window `[startCA, endCA)` they map both ends with `oal_pcie_devca_to_hostva`, then loop `ldr r3, [rX], #4` and print `addr = %x, value = %x`. They carry **no per-offset semantics** except a per-window header string (`Reading 2g_soc_register!` etc.) that names the block.
2. Real semantics live in a handful of consumers. The strongest evidence is a **named register table** in `hi5622v100_plat.ko` at `.rodata+0xcac`: 11 `{char *name, u32 devca}` entries, consumed by `dev_status_check` (`.text+0xf18c`), which prints each as `name=0x%.8x` and bit-decodes two of them. The names are `lock_status`, `pcie0_status`, `pcie1_status`, `dcoldo_efuse`, `dcoldo_vset`, `tcxo_pll_mux_sel`, `tcxo_pll_status`, `efuse_chip_id`, `pbank_code`, `abank_code`, `temp`.
3. Seven of the 19 windows contain at least one address consumed by a concrete driver function with a mask/compare/shift (`0x40000000`, `0x40002000`, `0x40004000`, `0x40039000`, `0x40039800`, `0x4003a000`, `0x40101000`); the other windows are read only by the dump loop (list in Sec 4.1).
4. The `read_all_reg_info` "mac"/"phy" blocks are dumped through **runtime CA tables** (not the 8 literal windows); their CAs are the `0x4009xxxx`-family sub-blocks and are not in the phase-4 list (Sec 4.2).

---

## 1. Window inventory: literal call sites and offsets read

### 1.1 `shuangta_read_all_reg_info` (wifi `.text+0x35600`, size `0xb7c`)

Per-iteration read instruction: `ldr r3, [r4], #4` (e.g. `0x35694`), `r4` = mapped host VA of the window start; loop bound is the mapped end VA. Values are printed by `oal_kernel_file_print(file=r5, fmt=sl, addr=r2, value=r3)` with `fmt = .LC13 = "addr = %x, value = %x\n"`.

| # | window CA range | bytes | header (label string) | start call site | end call site |
|---|---|---|---|---|---|
| 1 | `0x40108000..0x4010861c` | `0x61c` | `Reading 2g_soc_register!` (`.LC76`, printed `0x35630`) | `bl oal_pcie_devca_to_hostva` @`0x35658` | @`0x356b8` |
| 2 | `0x40110000..0x40110a38` | `0xa38` | same (2g_soc group) | @`0x35708` | @`0x35768` |
| 3 | `0x40109000..0x4010909c` | `0x9c` | `Reading 2g_rf_and_abb_register!` (`.LC77`, `0x357a0`) | @`0x357c8` | @`0x3582c` |
| 4 | `0x4010a000..0x4010ad1c` | `0xd1c` | same (2g_rf_and_abb group) | @`0x3587c` | @`0x358e0` |
| 5 | `0x4010c000..0x4010c61c` | `0x61c` | `Reading 5g_soc_register!` (`.LC81`, `0x35a60`) | @`0x35a88` | @`0x35ae8` |
| 6 | `0x40114000..0x40114810` | `0x810` | same (5g_soc group) | @`0x35b38` | @`0x35b98` |
| 7 | `0x4010d000..0x4010d108` | `0x108` | `Reading 5g_rf_and_abb_register!` (`.LC82`, `0x35bd0`) | @`0x35bf8` | @`0x35c5c` |
| 8 | `0x4010e000..0x4010ef24` | `0xf24` | same (5g_rf_and_abb group) | @`0x35cac` | @`0x35d10` |

Additionally the function dumps `2g_mac_register` (`.LC78`, `0x35918`), `2g_phy_register` (`.LC80`, `0x35a24`), `5g_mac_register` (`.LC83`, `0x35d48`) and `5g_phy_register` (`.LC85`, `0x35e54`) via helper loops whose CA comes from a `.rodata` table (`bl` sites @`0x359a0`,`0x359cc`,`0x35dd0`,`0x35dfc`,`0x35ed4`,`0x35f68` all have **no materialised CA**). Those blocks are not in the phase-4 window list.

### 1.2 `shuangta_read_soc_to_file` (wifi `.text+0x3617c`, size `0xa5c`)

Per-iteration read instruction: `ldr r3, [r6], #4` (e.g. `0x361ec` in window 1), `r6` = mapped host VA of the window start, printed with `.LC13` as in 1.1. (The CA-0 window 12 uses `r7` instead, e.g. `0x36600`.) There is no per-window header string for this routine.

| # | window CA range | bytes | start call site | end call site |
|---|---|---|---|---|
| 1 | `0x40000000..0x40000664` | `0x664` | @`0x361b4` | @`0x36210` |
| 2 | `0x40002000..0x400024c0` | `0x4c0` | @`0x36260` | @`0x362c0` |
| 3 | `0x40003000..0x40003448` | `0x448` | @`0x36310` | @`0x36370` |
| 4 | `0x40004000..0x40004254` | `0x254` | @`0x363c0` | @`0x36420` |
| 5 | `0x40100000..0x4010063c` | `0x63c` | @`0x36470` | @`0x364d0` |
| 6 | `0x40030000..0x40030494` | `0x494` | @`0x36520` | @`0x36580` |
| 7 | `0x4003a000..0x4003ac34` | `0xc34` | @`0x36674` | @`0x366d4` |
| 8 | `0x40039000..0x4003955c` | `0x55c` | @`0x36724` | @`0x36784` |
| 9 | `0x40039800..0x40039d5c` | `0x55c` | @`0x367d4` | @`0x36834` |
| 10 | `0x40101000..0x40101934` | `0x934` | @`0x36884` | @`0x368e8` |
| 11 | `0x40031000..0x40031194` | `0x194` | @`0x36938` | @`0x36998` |
| 12* | `0x00000000..0x00000032` | `0x32` | @`0x365cc` (CA 0) | @`0x36628` (CA `0x32`) |

`*` window 12 is present in the binary but not in the phase-4 list; `r1` is `0` from `mov r6,#0` / `mov r1,r6` and `0x32` from `mov r1,#0x32`. This loop prints the **offset** (`r2 = r6`, incremented by 4) rather than a device CA.

Reproduce both tables: Appendix A.3.

---

## 2. Per-window register map with consumer semantics

Legend: **offset** = byte offset from the window base; **consumer** = function (.text offset in its own module) and the base register of the mapped pointer; **operation** = literal instruction sequence; **reading** = my interpretation (marked PLAUSIBLE where it is not literally stated by the code).

### 2.1 Window `0x40000000..0x40000664` (phase-4 dump #1) - efuse / strap / pinmux block

| offset | width | consumer | value lands in | operation | reading |
|---|---|---|---|---|---|
| `0x2a8` | 32 (byte0 used) | `exception_pcie1_link_down` (plat `+0x102e4`), `bl` @`0x10324` | `r2` | `ldr r2,[r3]`; `uxtb r2,r2`; `and r3,r2,#0x7f`; `cmp r3,#5` | named `efuse_chip_id` by `dev_status_check`; low 7 bits compared against 5 (chip/strap id==5 arms the PCIe1 link-down workaround) |
| `0x2d4` | 32 | `dev_status_check` (plat `+0xf18c`) table entry 4 | `fp` | `ldr fp,[r3]`; then `ubfx r3,fp,#5,#3` (bits[7:5]) | named `dcoldo_efuse`; bits[7:5] are the DC-DC LDO efuse trim |
| `0x554` | 32 | `pcie_l1ss_dev_pinmux_set` (plat `+0x1b440`), `bl` @`0x1b448` | `r5` | `ldr r5,[r3]`; `orr r5,r5,#0x220`; `dsb st`; `arm_heavy_mb`; `str r5,[r3]` | set bits 5 and 9 of the pinmux register (PLAUSIBLE: drive the L1SS device pins) |
| `0x5bc` | 32 | same function, `bl` @`0x1b4a4` | - | `mov r2,#6`; `str r2,[r3]` | write value 6 (PLAUSIBLE: pinmux function select for the L1SS pins) |
| `0x108` | 32 | `.data+0x26a4` element of a table reached from `firmware_download_function` (reloc `.rel.text+0xfa64` -> `.LANCHOR1` `.data+0x269c`) | (table field) | stored in a descriptor table, not dereferenced by an `ldr` in the dump path | PLAUSIBLE: firmware-download control/status register; **not resolved to a mask/compare** (Sec 4.4) |

### 2.2 Window `0x40002000..0x400024c0` (dump #2) - PCIe early-init register

| offset | width | consumer | value lands in | operation | reading |
|---|---|---|---|---|---|
| `0x2210` | 32 | `pcie_main_init` (plat `.text.unlikely` sec3 `+0x704`), `bl` @`sec3+0x998` | `r7` | `ldr r7,[r3]`; `and r7,r7,#0x3f`; `orr r7,r7,#0x180`; `dsb st`; `arm_heavy_mb`; `str r7,[r3]`; followed by `filp_open`/`vfs_fsync`/`filp_close` | bits[5:0] preserved, bits[8:7] forced to `0b11`. PLAUSIBLE: PCIe refclk/PLL or PHY power-up strap written while a status file is flushed |

### 2.3 Window `0x40003000..0x40003448` (dump #3)

No consumer found in either object (only the dump loop). Sec 4.1.

### 2.4 Window `0x40004000..0x40004254` (dump #4) - chip status block

| offset | width | consumer | value lands in | operation | reading |
|---|---|---|---|---|---|
| `0x4110` | 32 | `dev_status_check` table entry 11 | `fp` | `ldr fp,[r3]`; printed whole | named `temp` (die temperature) |
| `0x4208` | 32 | `dev_status_check` table entry 1 | `fp` | `ldr fp,[r3]`; printed whole | named `lock_status` (PLL lock status) |

### 2.5 Window `0x40030000..0x40030494` (dump #6)

No consumer found in either object.

### 2.6 Window `0x40031000..0x40031194` (dump #11)

No consumer found in either object.

### 2.7 Window `0x40039000..0x4003955c` (dump #8) - PCIe0 / glue block

| offset | width | consumer | value lands in | operation | reading |
|---|---|---|---|---|---|
| `0x10` | 32 | `shuangta_pcie_msg_reg_map` (plat `+0x1b1a0`), `bl` @`0x1b1cc` | `out[0]` (`str r3,[r4]`) | mapped by `oal_pcie_inbound_ca_to_va`, VA stored | PCIe host<->device message register 0 |
| `0x14` | 32 | same, `bl` @`0x1b1f0` | `out[1]` | ditto | message register 1 |
| `0x220` | 32 | `shuangta_pcie_l1ss_set` (plat `+0x1aeb8`), `bl` @`0x1af14`; `_clear` (`+0x1ac2c`), `bl` @`0x1ac90` | `r5` | set: `str #0x40`; clear: `ldr r5,[r3]`; `orr r5,r5,#0x80`; RMW | L1SS control: write `0x40` to assert, OR bit 7 to release |
| `0x224` | 32 | `dev_status_check` table entry 2 | `fp` | `ldr fp,[r3]`; printed whole | named `pcie0_status` |
| `0x2d0` | 32 | `shuangta_pcie_l1ss_set`, `bl` @`0x1af44`; `_clear`, `bl` @`0x1ac54` | `r5` | set: `str #7`; clear: `ldr r5,[r3]`; `orr r5,r5,#0x100`; RMW | L1SS control: write `7` to assert, OR bit 8 to release |
| `0x2d4` | 32 | `shuangta_pcie_msg_reg_map`, `bl` @`0x1b214` | `out[2]` | ditto | message register 2 |
| `0x2f0` | 32 | `shuangta_pcie_msg_reg_map`, `bl` @`0x1b280` | `out[5]` | ditto | message register 5 |
| base `0x40039000` | - | listed in `.data+0x1fa8` region array (`0x40039000, 0x40039800, 0x40037000, 0x40038000`), immediately before `g_st_rcregion_devarry` (`.data+0x1fb8`) | - | `g_st_rcregion_devarry` is referenced from `oal_pcie_host_init`'s literal pool (`.rel.text+0xc0cc` -> `.data+0x1fb8`) | PLAUSIBLE: PCIe register-region base list (array itself not directly relocated) |

### 2.8 Window `0x40039800..0x40039d5c` (dump #9) - PCIe1 block

| offset | width | consumer | value lands in | operation | reading |
|---|---|---|---|---|---|
| `0x220` | 32 | `shuangta_pcie_l1ss_set`, `bl` @`0x1afbc`; `_clear`, `bl` @`0x1ad08` | `r5` | set: `str #0x40`; clear: `ldr r5,[r3]`; `orr r5,r5,#0x80`; RMW | L1SS control (endpoint 1): write `0x40` to assert, OR bit 7 to release |
| `0x224` | 32 | `dev_status_check` table entry 3 | `fp` | `ldr fp,[r3]`; printed whole | named `pcie1_status` |
| `0x224` | 32 | `exception_pcie1_link_down`, `bl` @`0x1036c` | `r4` | `ldr r4,[r3]`; `ubfx r4,r4,#9,#6`; `sub r4,r4,#3`; `bics r3,r4,#2` -> return `1` iff bits[14:9] is `3` or `5` | PCIe1 link state field; decode of a 6-bit status nibble |
| `0x2d0` | 32 | `shuangta_pcie_l1ss_set`, `bl` @`0x1afe8`; `_clear`, `bl` @`0x1accc` | `r5` | set: `str #7`; clear: `ldr r5,[r3]`; `orr r5,r5,#0x100`; RMW | L1SS control (endpoint 1): write `7` to assert, OR bit 8 to release |

### 2.9 Window `0x4003a000..0x4003ac34` (dump #7)

| offset | width | consumer | value lands in | operation | reading |
|---|---|---|---|---|---|
| `0x200` | 32 | `shuangta_pcie_enable_remap` (plat `+0x1ae3c`), `bl` @`0x1ae60` | `r2` (value written) | `dsb st`; `arm_heavy_mb`; `mov r2,#1`; `str r2,[r3]` | write 1 = enable PCIe inbound address remap |
| base `0x4003a000` | - | `.data+0x2944` (with `0x40039508`) referenced by `shuangta_get_ete_priv_res` (`.rel.text+0x17a84` -> `.LANCHOR1` `.data+0x2944`) | - | returned as a resource pair | PLAUSIBLE: ETE/region descriptor `{devca_base, size}` |

### 2.10 Window `0x40100000..0x4010063c` (dump #5)

No consumer found in either object.

### 2.11 Window `0x40101000..0x40101934` (dump #10) - TCXO/PLL + message block

| offset | width | consumer | value lands in | operation | reading |
|---|---|---|---|---|---|
| `0x230` | 32 | `dev_status_check` table entry 6 | `fp` | `ldr fp,[r3]`; printed whole | named `tcxo_pll_mux_sel` |
| `0x234` | 32 | `dev_status_check` table entry 7 | `fp` | `ldr fp,[r3]`; printed whole | named `tcxo_pll_status` |
| `0x414` | 32 | `shuangta_pcie_msg_reg_map`, `bl` @`0x1b25c` | `out[4]` | mapped by `oal_pcie_inbound_ca_to_va`; VA stored | MAC-side message register |
| `0x438` | 32 | `shuangta_pcie_msg_reg_map`, `bl` @`0x1b238` | `out[3]` | ditto | MAC-side message register |

### 2.12 `read_all_reg_info` windows `0x40108000..0x40114810` (dumps 1-8 of Sec 1.1)

**No consumer other than the dump loop was found in either object.** The only semantics available are the header strings, which classify each window:

| window | header label | reading |
|---|---|---|
| `0x40108000..0x4010861c` | `Reading 2g_soc_register!` | 2.4 GHz band "SOC"/common register bank A |
| `0x40110000..0x40110a38` | (2g_soc group) | 2.4 GHz band "SOC" bank B |
| `0x40109000..0x4010909c` | `Reading 2g_rf_and_abb_register!` | 2.4 GHz RF + ABB (analog baseband) bank A |
| `0x4010a000..0x4010ad1c` | (2g_rf_and_abb group) | 2.4 GHz RF + ABB bank B |
| `0x4010c000..0x4010c61c` | `Reading 5g_soc_register!` | 5 GHz band "SOC" bank A |
| `0x40114000..0x40114810` | (5g_soc group) | 5 GHz band "SOC" bank B |
| `0x4010d000..0x4010d108` | `Reading 5g_rf_and_abb_register!` | 5 GHz RF + ABB bank A |
| `0x4010e000..0x4010ef24` | (5g_rf_and_abb group) | 5 GHz RF + ABB bank B |

---

## 3. Worked examples (disassembly -> offset -> consumer -> interpretation)

### 3.1 `pcie_main_init` -> `0x40002210` (window `0x40002000..0x400024c0`)

```
usec3+0990: movw  r1, #0x2210        ; low half
usec3+0994: movt  r1, #0x4000        ; r1 = 0x40002210
usec3+0998: bl    oal_pcie_devca_to_hostva
usec3+09b4: ldr   r3, [sp, #0xc]     ; mapped host VA
usec3+09b8: ldr   r7, [r3]           ; r7 = *0x40002210
usec3+09c0: and   r7, r7, #0x3f      ; keep bits[5:0]
usec3+09c4: orr   r7, r7, #0x180     ; set  bits[8:7] = 11
usec3+09d4: str   r7, [r3]           ; write back
```
Consumer: `pcie_main_init` (plat sec3 `+0x704`). Value lands in `r7` (32-bit). Interpretation: 6 low bits are preserved, bits 8:7 are forced high - a PCIe reference-clock/PLL or PHY power-up field set during driver probe. PLAUSIBLE.

### 3.2 `pcie_l1ss_dev_pinmux_set` -> `0x40000554`, `0x400005bc` (window `0x40000000..0x40000664`)

```
01b444: movw  r1, #0x554
01b448: movt  r1, #0x4000            ; r1 = 0x40000554
01b468: bl    oal_pcie_devca_to_hostva
01b478: ldr   r5, [r3]               ; r5 = *0x40000554
01b480: orr   r5, r5, #0x220         ; set bits 5 and 9
01b490: str   r5, [r3]
01b494: movw  r1, #0x5bc
01b498: movt  r1, #0x4000            ; r1 = 0x400005bc
01b4a4: bl    oal_pcie_devca_to_hostva
01b4b8: mov   r2, #6
01b4c0: str   r2, [r3]               ; *0x400005bc = 6
```
Consumer: `pcie_l1ss_dev_pinmux_set` (plat `+0x1b440`). `0x40000554` read-modify-write sets bits 5 and 9 (pinmux drive/enable); `0x400005bc` gets the value 6 (mux function select). Interpretation: SoC pinmux for the PCIe L1SS device pins.

### 3.3 `exception_pcie1_link_down` -> `0x400002a8` and `0x40039a24` (windows `0x40000000..` and `0x40039800..`)

```
010320: mov   r1, #0x2a8
010324: movt  r1, #0x4000            ; r1 = 0x400002a8
010328: bl    oal_pcie_devca_to_hostva
010338: ldr   r2, [r3]               ; r2 = *0x400002a8
010344: uxtb  r2, r2                 ; keep byte 0
010350: and   r3, r2, #0x7f          ; bits[6:0]
010354: cmp   r3, #5                 ; == 5 ?
...
010364: movw  r1, #0x9a24
010368: movt  r1, #0x4003            ; r1 = 0x40039a24
01036c: bl    oal_pcie_devca_to_hostva
01037c: ldr   r4, [r3]               ; r4 = *0x40039a24
010388: ubfx  r4, r4, #9, #6         ; bits[14:9]
01038c: sub   r4, r4, #3
01039c: bics  r3, r4, #2             ; r3 = r4 & ~2
0103a0: moveq r0, #1                 ; return 1 iff r4 in {0,2}
0103a4: movne r0, #0
```
Consumer: `exception_pcie1_link_down` (plat `+0x102e4`). Value lands in `r2` then `r4`. `0x400002a8` is `efuse_chip_id` (per the `dev_status_check` table) - low 7 bits equal 5 gate the workaround. `0x40039a24` is `pcie1_status`; the 6-bit field at bits[14:9], minus 3, is compared against the set {0,2}. Interpretation: decode a PCIe1 link-state nibble after an efuse chip-id match.

### 3.4 `shuangta_pcie_l1ss_set` / `_clear` -> `0x40039220/0x400392d0/0x40039a20/0x40039ad0` (windows `0x40039000..`, `0x40039800..`)

```
-- set (plat +0x1aeb8) --
01af0c: movw r1, #0x9220
01af10: movt r1, #0x4003            ; 0x40039220
01af30: str  sb, [r3]               ; sb = 0x40
01af34: movw r1, #0x92d0
01af38: movt r1, #0x4003            ; 0x400392d0
01af60: str  r6, [r3]               ; r6 = 7
01afb4: movw r1, #0x9a20
01afb8: movt r1, #0x4003            ; 0x40039a20
01afd4: str  sb, [r3]               ; 0x40
01afd8: movw r1, #0x9ad0
01afdc: movt r1, #0x4003            ; 0x40039ad0
01b000: str  r6, [r3]               ; 7
-- clear (plat +0x1ac2c) --
01ac64: ldr  r5, [r3]               ; *0x400392d0
01ac6c: orr  r5, r5, #0x100
01ac7c: str  r5, [r3]
01aca0: ldr  r5, [r3]               ; *0x40039220
01aca8: orr  r5, r5, #0x80
...  same pattern for 0x40039ad0 (orr #0x100) and 0x40039a20 (orr #0x80)
```
Consumer: `shuangta_pcie_l1ss_set` (plat `+0x1aeb8`) and `shuangta_pcie_l1ss_clear` (`+0x1ac2c`). "Set" writes constants `0x40`/`7`; "clear" is a read-modify-write that ORs `0x100`/`0x80`. Interpretation: PCIe L1-substate (L1SS) enable/disable latches for endpoint 0 (`0x4003_92xx`) and endpoint 1 (`0x4003_9axx`).

### 3.5 `shuangta_pcie_enable_remap` -> `0x4003a200` (window `0x4003a000..0x4003ac34`)

```
01ae40: mov   r1, #0xa200
01ae44: movt  r1, #0x4003            ; r1 = 0x4003a200
01ae60: bl    oal_pcie_devca_to_hostva
01ae6c: dsb   st
01ae70: bl    arm_heavy_mb
01ae74: mov   r2, #1
01ae7c: str   r2, [r3]               ; *0x4003a200 = 1
```
Consumer: `shuangta_pcie_enable_remap` (plat `+0x1ae3c`). Value 1 lands in `r2`. Interpretation: single write of 1 enables the PCIe inbound address remap (called from `pcie_main_init` when >1 chip/function is present).

### 3.6 `dev_status_check` -> the 11-entry named table (spans windows `0x40000000`, `0x40004000`, `0x40039000`, `0x40039800`, `0x40101000` and three addresses outside)

```
00f198: ldr   sb, [pc, #0x100]       ; literal -> .rodata+0xd04 = "dev_status_check"
...
00f1b0: ldr   r1, [pc, #0xf0]        ; literal -> .rodata+0xcac (the {name,ca} table)
00f1b8: mov   r2, #0x58              ; 11 entries * 8 bytes
00f1d0: bl    memcpy                 ; copy table to stack at r7
00f1d4: ldr   r1, [r7, #4]           ; entry.ca
00f1e0: bl    oal_pcie_devca_to_hostva
00f1f0: ldr   fp, [r3]               ; fp = *ca
00f1f8: ldr   r8, [r7]               ; entry.name
00f204: bl    strcmp                 ; vs "dcoldo_efuse"
00f20c: ubfxeq r3, fp, #5, #3        ; dcoldo_efuse = bits[7:5]
...        strcmp vs "dcoldo_vset"
00f278: ubfxeq r3, fp, #6, #4        ; dcoldo_vset = bits[9:6]
...        otherwise print fp whole via "%s:: %s=0x%x!\r\n"
```
Consumer: `dev_status_check` (plat `+0xf18c`); table at `.rodata+0xcac`. The table (name string in `.rodata.str1.4`, CA in `.rodata`) is:

| entry | name | CA | in a phase-4 window? |
|---|---|---|---|
| 1 | `lock_status` | `0x40004208` | yes (`0x40004000..`) |
| 2 | `pcie0_status` | `0x40039224` | yes (`0x40039000..`) |
| 3 | `pcie1_status` | `0x40039a24` | yes (`0x40039800..`) |
| 4 | `dcoldo_efuse` | `0x400002d4` | yes (`0x40000000..`) |
| 5 | `dcoldo_vset` | `0x4000500c` | no |
| 6 | `tcxo_pll_mux_sel` | `0x40101230` | yes (`0x40101000..`) |
| 7 | `tcxo_pll_status` | `0x40101234` | yes (`0x40101000..`) |
| 8 | `efuse_chip_id` | `0x400002a8` | yes (`0x40000000..`) |
| 9 | `pbank_code` | `0x4000505c` | no |
| 10 | `abank_code` | `0x40005060` | no |
| 11 | `temp` | `0x40004110` | yes (`0x40004000..`) |

Interpretation: a chip bring-up status dump; the two DC-DC LDO trim registers are the only ones bit-decoded (`efuse` -> bits[7:5], `vset` -> bits[9:6]).

### 3.7 `shuangta_read_all_reg_info` loop -> window `0x40108000..0x4010861c`

```
035648: mov   r1, #0x8000
03564c: movt  r1, #0x4010            ; r1 = 0x40108000 (start CA)
035658: bl    oal_pcie_devca_to_hostva
035660: ldreq r4, [sp, #0x14]        ; r4 = mapped VA of start
03566c: movw  fp, #0x861c
035670: movt  fp, #0x4010            ; fp = 0x4010861c (end CA)
03567c: add   sb, sb, #0x108000      ; sb = 0x40108000 - VA  (CA reconstruction)
03568c: cmp   r4, r3                 ; r3 = mapped end VA
035690: bhs   exit
035694: ldr   r3, [r4], #4           ; read one u32, advance by 4
0356a0: bl    oal_kernel_file_print  ; fmt .LC13 "addr = %x, value = %x\n"
```
Consumer: the diagnostic dumper itself; value lands in `r3` and is only formatted into the log. No mask/compare/shift. Interpretation: this window is a 2.4 GHz "SOC" register bank read as a blob; it is diagnostic data, not driver state.

---

## 4. What could not be interpreted, and why

### 4.1 Windows with no consumer
- `0x40003000..0x40003448` (dump #3)
- `0x40030000..0x40030494` (dump #6)
- `0x40031000..0x40031194` (dump #11)
- `0x40100000..0x4010063c` (dump #5)
- all eight `read_all_reg_info` windows `0x40108000..0x40114810`

Reason: an exhaustive search for (a) materialised chip-address immediates (`mov`/`movw`+`movt`/`add`/`sub`/`rsb`) and (b) 4-byte-aligned words in `.rodata`/`.data`/`.text` finds no occurrence of any address inside these ranges in either object, apart from the dump routines' own start/end constants. They are read only by the diagnostic blob logger, so only the header string classifies them (`2g/5g soc`, `2g/5g rf_and_abb`). No bit semantics are derivable from these two objects.

### 4.2 `read_all_reg_info` "mac"/"phy" groups
The `2g_mac`, `2g_phy`, `5g_mac`, `5g_phy` dumps call `oal_pcie_devca_to_hostva` from helper loops (`0x359a0`, `0x359cc`, `0x35dd0`, `0x35dfc`, `0x35ed4`, `0x35f68`) whose CA is loaded from a `.rodata` table at run time (`ldr ip,[pc,#0x840]` @`0x3592c` into a `.text` literal pool). Those sub-blocks are not the eight phase-4 windows and were not enumerated here. The related `.rodata+0xa80`/`.rodata+0xbb0` tables contain `0x40090000`-family and `0x40040000`-family block bases, i.e. entirely outside the dumped windows.

### 4.3 Named registers that fall outside every dumped window
`dev_status_check` names three registers that no `read_*` window covers: `dcoldo_vset` (`0x4000500c`), `pbank_code` (`0x4000505c`) and `abank_code` (`0x40005060`). Also `pcie_main_init` touches `0x40002210`, which is covered by dump #2 - fine - but `firmware_download_function` reaches an un-resolved `0x40000108` through a descriptor table (`.data+0x26a4`), and no mask/compare on it was found.

### 4.4 Region-descriptor arrays
`.data+0x1fa8` (`0x40039000, 0x40039800, 0x40037000, 0x40038000`), `.data+0x2ae0` (`0x40000000, 0x4011ffff` twice) and `.data+0x2944` (`0x4003a000, 0x40039508`) are start/size-style region or resource descriptors. They corroborate that the dumped windows are real PCIe address regions, but I did not prove which instruction consumes each array (the relocation trail leads into literal pools; only `.data+0x2944` -> `shuangta_get_ete_priv_res` and `.data+0x1fb8` -> `oal_pcie_host_init` were resolved).

### 4.5 Address 0 window
`read_soc_to_file` also dumps `[0x0, 0x32)` (CA 0), which is not in the phase-4 list. `oal_pcie_devca_to_hostva` returning success for CA 0 is unexpected and I did not determine what that mapping is (possibly a local/dummy region or PCI config space). Marked unresolved.

### 4.6 Endpoint-1 and BAR space
Unchanged from phase 4 Sec 5.1: `0x41000000`, `0x41800000`, `0x58000000`, `0x59000000`, `0x59800000` are not materialised statically in either object; the endpoint-1 window comes from the runtime `g_pci_chip_res` table (`.bss`), so no static register semantics are derivable.

### 4.7 Bitfield meanings
All bit interpretations above are derived from the literal masks/shifts in the code (`and`, `orr`, `ubfx`, `bic`, `cmp`). No vendor datasheet or register-name table exists in these two objects for the fields themselves, so field *meaning* is labelled PLAUSIBLE wherever the code does not name it.

---

## Appendix A - reproduction commands

### A.1 Hashes / tooling
```
$ sha256sum hi5622v100_wifi.ko hi5622v100_plat.ko
de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521 *hi5622v100_wifi.ko
6f2eac415dbf4d3e8e71c1abd941ea4f9d97b159674d28d5e99d4ecb0e6cd491 *hi5622v100_plat.ko
$ C:/Users/ShibbityShwab/router-openwrt/pyenv/Scripts/python.exe -c "import capstone,elftools;print(capstone.__version__)"
5.0.7
```

### A.2 Disassembly with relocation annotations
The excerpt printer used for every listing in this document:
```
python - <<'PYEOF'
import sys
from elftools.elf.elffile import ELFFile
import capstone
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM)
ko=sys.argv[1]
e=ELFFile(open(ko,'rb')); secs=list(e.iter_sections())
rels={}
for rn in ('.rel.text','.rel.text.unlikely','.rel.init.text'):
    rs=e.get_section_by_name(rn)
    if rs is None: continue
    m=rels.setdefault(rs['sh_info'],{}); st=e.get_section(rs['sh_link'])
    for r in rs.iter_relocations():
        m.setdefault(r['r_offset'],[]).append(st.get_symbol(r['r_info_sym']).name)
for s in e.get_section_by_name('.symtab').iter_symbols():
    if s.name==sys.argv[2] and s['st_info']['type']=='STT_FUNC':
        v,sz,idx=s['st_value'],s['st_size'],s['st_shndx']
sd=secs[idx].data()
for i in md.disasm(sd[v:v+sz],v):
    a=rels.get(idx,{}).get(i.address,[])
    print(f'{i.address:06x}: {i.mnemonic:8s} {i.op_str:34s}'+('  ; '+' '.join(a) if a else ''))
PYEOF
```

### A.3 Window call sites (constant-tracked `bl oal_pcie_devca_to_hostva`)
Register-value tracking over each dump function, printing the CA held in the first argument register at every call to the accessor. Run exactly as the phase-4 Appendix A.2 tracker but print each `bl` site (the output is the "call site" columns of Sec 1.1/1.2).

### A.4 The `{name, ca}` table at `.rodata+0xcac`
```
python - <<'PYEOF'
import struct
from elftools.elf.elffile import ELFFile
e=ELFFile(open('hi5622v100_plat.ko','rb')); secs=list(e.iter_sections())
rs=e.get_section_by_name('.rel.rodata'); base=e.get_section(rs['sh_info']); bd=base.data()
st=e.get_section(rs['sh_link'])
for r in rs.iter_relocations():
    o=r['r_offset']; sym=st.get_symbol(r['r_info_sym'])
    nameoff=struct.unpack_from('<I',bd,o)[0]+sym['st_value']
    ca=struct.unpack_from('<I',bd,o+4)[0]
    if 0x40000000<=ca<=0x4011FFFF:
        d=secs[sym['st_shndx']].data(); end=d.find(b'\x00',nameoff)
        print(d[nameoff:end].decode(), hex(ca))
PYEOF
```
Produces the 11 rows of Sec 3.6 (the name offset is the REL addend stored in the word; the symbol is the `.rodata.str1.4` section).

### A.5 Aligned table scan
Any 4-byte-aligned word in `.rodata`/`.data`/`.text` whose value lies in any of the 19 phase-4 windows, both objects (Sec 4.1). Only `.rodata+0xcac..0xd00` and the `.data` region descriptors matched.
