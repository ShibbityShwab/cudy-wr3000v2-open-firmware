# Wi-Fi chip MMIO register-map skeleton (hi5622v100)

Source (local, read-only, no device access):
- `C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/build/tmp/hi5622v100_wifi.ko` — sha256 `de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521`, 3,564,728 bytes
- `C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/build/tmp/hi5622v100_plat.ko` — sha256 `6f2eac415dbf4d3e8e71c1abd941ea4f9d97b159674d28d5e99d4ecb0e6cd491`, 364,660 bytes

Tooling: `C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2/pyenv/Scripts/python.exe` — capstone 5.0.7 (`CS_ARCH_ARM`, `CS_MODE_ARM`), pyelftools.
All addresses in this document are **section-relative offsets in the relocatable ELF** (both objects are `ET_REL`; every `sh_addr == 0`, so `st_value` is a section offset and there is no link-time absolute VA). `.text` of the wifi module is `0x1630cc` bytes, of the plat module `0x1d324` bytes; `.text.unlikely` is a separate section.

---

## 0. Key findings up front

1. **There are no `readl`/`writel` call sites in the Wi-Fi module, and no `ioremap` call sites in it either.** A full `.symtab` scan of `hi5622v100_wifi.ko` (21,495 symbols; 3,847 defined functions) finds no symbol matching `readl|writel|readb|writeb|readw|writew|ioremap|iounmap|__raw_readl|__raw_writel|memcpy_fromio|memcpy_toio|memset_io`.
2. The Wi-Fi module reaches registers through **`oal_pcie_devca_to_hostva()`** (device chip-address → host VA), imported from `hi5622v100_plat.ko`, followed by ordinary `ldr`/`str` pointer dereferences. 63 call sites in 19 wifi functions, 19 call sites in 8 plat functions.
3. `hi5622v100_plat.ko` is the layer that owns the actual mapping: it imports `ioremap`/`iounmap` and calls `ioremap` twice inside `oal_pcie_dev_init` (`.text+0xa2bc`, `.text+0xa4b4`) after `__request_region`, for each PCIe resource of each chip.
4. The **0x40000000 device-CA window is the endpoint-0 register origin used verbatim** throughout `.text`, e.g. `mov r1,#0x40000000` in `shuangta_read_soc_to_file`. The window `0x40100000..0x4011FFFF` is the endpoint-0 MAC/CFG register area (8 windows dumped by `shuangta_read_all_reg_info`, plus further windows in `shuangta_read_soc_to_file`).
5. The endpoint-0 MAC hardware block list is a **static 10-word device-CA table in `.rodata`** (`.rodata+0xb60`) consumed by `shuangta_host_initialize_machw` + its helper at `.text+0x33fcc`.
6. **I could not find `0x41000000`, `0x41800000`, `0x58000000`, `0x59000000` or `0x59800000` as any materialised immediate or any aligned 32-bit table entry in either object.** Any aligned scan of `.rodata`/`.data`/`.text` for those values in endpoint-1's range returns nothing (see §5.1). The only ep-1 possibility is the runtime resource table `g_pci_chip_res` (a `.bss` object, see §3), i.e. those windows are resolved at run time, not statically.

---

## 1. Method

### 1.1 ELF/section survey

Command (reproducible verbatim):

```
python - <<'EOF'
from elftools.elf.elffile import ELFFile
e=ELFFile(open('hi5622v100_wifi.ko','rb'))
for i,s in enumerate(e.iter_sections()):
    print(i, s.name, hex(s['sh_addr']), hex(s['sh_offset']), hex(s['sh_size']), s['sh_type'])
print('symtab', e.get_section_by_name('.symtab').num_symbols())
EOF
```

Observed: every section has `sh_addr = 0x0` (relocatable), `.text` at file offset `0x38`, `.rodata` at `0x167b00`, `.data` at `0x243ab0`, `.bss` at `0x2632e8`, `.symtab` with 21,495 symbols. This is why every offset below is a section-relative value.

### 1.2 Finding the accessors

```
python - <<'EOF'
from elftools.elf.elffile import ELFFile
import re
for ko in ('hi5622v100_wifi.ko','hi5622v100_plat.ko'):
    e=ELFFile(open(ko,'rb')); und=set(); fns=[]
    for s in e.get_section_by_name('.symtab').iter_symbols():
        if not s.name: continue
        if s['st_shndx']=='SHN_UNDEF': und.add(s.name)
        elif s['st_info']['type']=='STT_FUNC': fns.append(s.name)
    pat=re.compile(r'readl|writel|ioremap|iounmap|__raw|memcpy_fromio|memcpy_toio|memset_io')
    print(ko,'io-ish undef:',sorted(n for n in und if pat.search(n)))
    print(ko,'io-ish funcs:',sorted(n for n in fns if pat.search(n)))
EOF
```

- `hi5622v100_wifi.ko`: io-ish undef = `[]`; io-ish funcs = `[]`.
- `hi5622v100_plat.ko`: io-ish undef = `['ioremap','iounmap']`; io-ish funcs (names containing those patterns) = `[]`.

So the "accessor" is not a standard kernel name. The import list of `hi5622v100_wifi.ko` exposed the real one: undefined symbols include `oal_pcie_devca_to_hostva`, `memmap_get_dev_acp_addr`, `pcie_if_hostca_to_devva`, `get_pci_chip_res`, `pcie_wlan_func_register`. All are defined in `hi5622v100_plat.ko`, which also defines `oal_pcie_inbound_ca_to_va`, `pcie_hostca_to_devva`, `pcie_get_dev_ca`.

### 1.3 Relocation-driven call graph

Because the objects are relocatable, `bl` instructions carry `R_ARM_CALL` relocations whose symbol names give the callee. I built a `{section_offset: [symbol]}` map from `.rel.text` / `.rel.text.unlikely` and disassembled each function section with capstone, annotating every instruction that has a relocation. Counting call sites:

```
python - <<'EOF'
# (full script in Appendix A.1) - counts call relocations per target and resolves the
# containing function by [st_value, st_value+st_size)
EOF
```

Observed for `oal_pcie_devca_to_hostva`:
- `hi5622v100_wifi.ko`: 63 call sites in 19 functions — `shuangta_read_soc_to_file` (24), `shuangta_read_all_reg_info` (22), `hmac_get_user_tid_tx_msdu_info_ring_table_addr` (1), `hmac_tx_msdu_ring_sn_dump`, `hmac_tx_msdu_ring_post_proc`, `hmac_tx_msdu_ring_proc`, `shuangta_rx_get_next_sub_msdu`, `shuangta_irq_host_mac_isr` (1 each), plus 11 sites inside unnamed/ordinal functions near `0x3400c`–`0x35348` and `0x3a870`/`0x3a974`.
- `hi5622v100_plat.ko`: 19 sites in 8 functions — `shuangta_pcie_l1ss_clear/set` (4+4), `exception_handler` (4), `exception_pcie1_link_down` (2), `pcie_l1ss_dev_pinmux_set` (2), `dev_status_check`, `shuangta_pcie_enable_remap`, `pcie_main_init` (1 each).

`ioremap` in plat: 2 sites in `oal_pcie_dev_init`, 1 in `exception_handler`.

### 1.4 Finding the base addresses

Two independent techniques, both in Appendix A:

1. **Constant formation tracking.** Disassemble `.text`/`.text.unlikely` per function and track `movw`/`movt` halves, `mov #imm`, `add/sub/rsb #imm`, invalidating a register on any other write. Flag values in the BAR/device-CA ranges. This produced the endpoint-0 addresses in §2.
2. **Aligned 32-bit table scan** of `.rodata`/`.data` for the same ranges. This found the MAC block-descriptor tables (`.rodata+0xb60`, `.rodata+0xbb0`), the `.rodata+0xcb0` PCIe link-down address list, and `.data+0x1fa8`/`.data+0x2944` region lists.

Both techniques agree, and the `.rodata` table at `+0xb60` is confirmed by resolving the `R_ARM_ABS32` relocation at `.text+0x34adc` (the literal-pool word for the `ldr ip,[pc,#0x16c]` at `.text+0x34968`): symbol = unnamed section-11 marker, addend `0xb60` ⇒ `.rodata+0xb60`.

---

## 2. Register / offset table, grouped by module

Legend: **CA** = device chip address (the value passed to `oal_pcie_devca_to_hostva` / `oal_pcie_inbound_ca_to_va`); **blk** = offset inside a mapped register block whose base comes from a runtime struct field. Width `32` means the code uses `ldr`/`str`/`bfi`/`ldrh` on 32-bit words unless stated.

### 2.1 PCIe glue (all `hi5622v100_plat.ko`)

| CA / struct offset | width | accessing function (.text+) | evidence (disasm / command) | suggested purpose |
|---|---|---|---|---|
| `0x40039010` | 32 | `shuangta_pcie_msg_reg_map` `+0x1b1a0` | `movw r1,#0x9010` / `movt r1,#0x4003` @`1b1b0`,`1b1b4`; result → `out[0]` | PCIe host↔device message register 0 |
| `0x40039014` | 32 | `shuangta_pcie_msg_reg_map` | `movw r1,#0x9014`/`movt r1,#0x4003` @`1b1e8`,`1b1ec`; → `out[1]` | message register 1 |
| `0x400392d4` | 32 | `shuangta_pcie_msg_reg_map` | `movw r1,#0x92d4`/`movt r1,#0x4003` @`1b20c`,`1b210`; → `out[2]` | message register 2 |
| `0x40101438` | 32 | `shuangta_pcie_msg_reg_map` | `movw r1,#0x1438`/`movt r1,#0x4010` @`1b230`,`1b234`; → `out[3]` | MAC-side message register |
| `0x40101414` | 32 | `shuangta_pcie_msg_reg_map` | `movw r1,#0x1414`/`movt r1,#0x4010` @`1b254`,`1b258`; → `out[4]` | MAC-side message register |
| `0x400392f0` | 32 | `shuangta_pcie_msg_reg_map` | `movw r1,#0x92f0`/`movt r1,#0x4003` @`1b278`,`1b27c`; → `out[5]` | message register 5 |
| `0x40039220` | 32 | `shuangta_pcie_l1ss_set` `+0x1aeb8`, `shuangta_pcie_l1ss_clear` `+0x1ac2c` | capstone constants @`1af10`/`1ac84` (set/clear) | L1 substate (L1SS) control |
| `0x400392d0` | 32 | `shuangta_pcie_l1ss_set`/`_clear` | constants @`1af38`/`1ac34` | L1SS control |
| `0x40039a20` | 32 | `shuangta_pcie_l1ss_set`/`_clear` | constants @`1afb8`/`1acfc` | L1SS control |
| `0x40039ad0` | 32 | `shuangta_pcie_l1ss_set`/`_clear` | constants @`1afdc`/`1acc0` | L1SS control |
| `0x4003a200` | 32 | `shuangta_pcie_enable_remap` `+0x1ae3c` | constant @`1ae44` | inbound-remap enable |
| `0x400002a8` | 32 | `exception_pcie1_link_down` `+0x102e4` | constant @`10324`; also in `.rodata+0xce8` pair `{0x9268, 0x400002a8}` | PCIe1 link-down handling |
| `0x40039a24` | 32 | `exception_pcie1_link_down` | constant @`10368` | link-down handling |
| `0x40000554`, `0x400005bc` | 32 | `pcie_l1ss_dev_pinmux_set` `+0x1b440` | constants @`1b448`,`1b498` | L1SS pinmux |
| `0x40002210` | 32 | `pcie_main_init` (`+.text.unlikely+0x704`) | constant @`994` | early PCIe init |
| `0x40004208`,`0x40101230`,`0x40101234`,`0x40004110`,`0x400002d4`,`0x4000500c`,`0x4000505c`,`0x40005060` | 32 | `exception_handler` / link-down path | aligned `.rodata+0xcb0` table, pairs `{0x91fc,0x40004208},{0x9208,0x40039224},{0x9218,0x40039a24},{0x9228,0x400002d4},{0x9238,0x4000500c},{0x9244,0x40101230},{0x9258,0x40101234},{0x9268,0x400002a8},{0x9278,0x4000505c},{0x9284,0x40005060},{0x9290,0x40004110}` | link/pinmux/voltage register list used for the PCIe link-down save |

Evidence for the `.rodata+0xcb0` table (Appendix A.2 prints it verbatim):

```
--- hi5622v100_plat.ko .rodata+0xc80 ---
  +0x30: 0x40004208   +0x38: 0x40039224   +0x40: 0x40039a24
  +0x48: 0x400002d4   +0x50: 0x4000500c   +0x58: 0x40101230
  +0x60: 0x40101234   +0x68: 0x400002a8   +0x70: 0x4000505c
  +0x78: 0x40005060   +0x80: 0x40004110
```

### 2.2 MAC (device-CA windows, `hi5622v100_wifi.ko`)

The endpoint-0 MAC/CFG register space is `0x40100000..0x4011FFFF`; two block-dump routines enumerate it. The **window bounds are exact** (start CA, end CA, length = end−start); individual register offsets inside a window are not individually resolved by these routines (they read one u32 per iteration), so I list the windows and the CA they start at.

`shuangta_read_all_reg_info` `+.text+0x35600` (size 2940) — 8 windows:

| window CA range | length | dumping function | evidence |
|---|---|---|---|
| `0x40108000..0x4010861c` | `0x61c` | `shuangta_read_all_reg_info` | `mov r1,#0x8000`/`movt r1,#0x4010` @`35648`,`3564c`; end `movw fp,#0x861c`/`movt fp,#0x4010` @`3566c`,`35670` |
| `0x40109000..0x4010909c` | `0x9c` | same | start @`357b8`/`357bc`; end @`357e0`/`357e4` |
| `0x4010a000..0x4010ad1c` | `0xd1c` | same | start @`35868`/`3586c`; end @`35894`/`35898` |
| `0x4010c000..0x4010c61c` | `0x61c` | same | start @`35a78`/`35a7c`; end @`35a9c`/`35aa0` |
| `0x4010d000..0x4010d108` | `0x108` | same | start `mov r1,#0xd000`/`movt r1,#0x4010` @`35be8`,`35bec`; end `movw fp,#0xd108`/`movt fp,#0x4010` @`35c10`,`35c14` |
| `0x4010e000..0x4010ef24` | `0xf24` | same | start @`35c98`/`35c9c`; end @`35cc4`/`35cc8` |
| `0x40110000..0x40110a38` | `0xa38` | same | `mov r1,#0`/`movt r1,#0x4011` @`356f4`,`356f8`; end `movw fp,#0xa38`/`movt fp,#0x4011` @`3571c`,`35720` |
| `0x40114000..0x40114810` | `0x810` | same | start `mov r1,#0`/`movt r1,#0x4011` @`35b24`,`35b28`; end `movw fp,#0x4810`/`movt fp,#0x4011` @`35b4c`,`35b50` |

`shuangta_read_soc_to_file` `+.text+0x3617c` (size 2652) — 11 windows:

| window CA range | length | evidence (start / end immediates) |
|---|---|---|
| `0x40000000..0x40000664` | `0x664` | start `mov r1,#0x40000000` @`36198`; end `movw sb,#0x664`/`movt sb,#0x4000` @`361c4`,`361c8` |
| `0x40002000..0x400024c0` | `0x4c0` | start `3624c`/`36250`; end `sl` `36274`/`36278` |
| `0x40003000..0x40003448` | `0x448` | start `362fc`/`36300`; end `sl` `36324`/`36328` |
| `0x40004000..0x40004254` | `0x254` | start `363ac`/`363b0`; end `sl` `363d4`/`363d8` |
| `0x40030000..0x40030494` | `0x494` | start `3650c`/`36510`; end `sl` `36534`/`36538` |
| `0x40031000..0x40031194` | `0x194` | start `36924`/`36928`; end `sl` `3694c`/`36950` |
| `0x40039000..0x4003955c` | `0x55c` | start `36710`/`36714`; end `sl` `36738`/`3673c` |
| `0x40039800..0x40039d5c` | `0x55c` | start `367c0`/`367c4`; end `sl` `367e8`/`367ec` |
| `0x4003a000..0x4003ac34` | `0xc34` | `36660`/`36664`, `36688`/`3668c` |
| `0x40100000..0x4010063c` | `0x63c` | `3645c`/`36460`, `36484`/`36488` |
| `0x40101000..0x40101934` | `0x934` | `36870`/`36874`, `3689c`/`368a0` |

MAC hardware block list (10 blocks), resolved in `shuangta_host_initialize_machw` `+.text+0x3491c` and stored into the per-chip struct at `[dev+0x14c]`:

| CA | width of entry | evidence |
|---|---|---|
| `0x40040000`, `0x40042000`, `0x40044000`, `0x40046000`, `0x40048000`, `0x4004a000`, `0x4004c000`, `0x40050000`, `0x40052000`, `0x40054000` | 32-bit CA each | `.rodata+0xb60` (file `0x168660`), 10 words, copied to the stack by `ldm ip!,{...}` at `0x34980`–`0x349b0`; helper `+.text+0x33fcc` maps each and stores it at `[dev+0x14c]+{0,4,8,0xc,0x10,0x14,0x18,0x1c,0x20,0x24}` |

Additional MAC blocks seen in the same `.rodata` region (`.rodata+0xa80`, `.rodata+0xbb0`): `0x40060000…0x4006c000`, `0x40072000`, `0x40074000`, `0x40090000/0x40090800/0x40091000/0x40091800`, `0x400a0000/0x400a0400/0x400a0800/0x400a0c00`, `0x400b0000…0x400b3800` (stride `0x800`), `0x400c0000…0x400c1800`, `0x400d0000/0x400d0400/0x400d0800/0x400d0c00`. These are additional register blocks used by the MAC init tables but I did not tie each to a specific consumer function.

Register offsets inside mapped MAC blocks (block base obtained from `[dev+0x14c]` sub-words or other chip structs), all wifi module:

| blk offset | width | function (.text+) | evidence | suggested purpose |
|---|---|---|---|---|
| `+0x00` | 32 | `shuangta_get_host_mac_int_status` `+0x34ae0` | `ldr r3,[r0,#0x14c]` @`34af0`; `ldr r0,[r3,#0x20]` @`34afc`; `ldr r3,[r0]` @`34b04` | host MAC interrupt status |
| `+0x44` | 32 | `shuangta_clear_host_mac_int_status` `+0x34be8` | `ldr r0,[r3,#0x14]` @`34c04`; `str r1,[r0,#0x44]` @`34c0c` | host MAC interrupt clear |
| `+0x48` | 32 | `shuangta_get_host_mac_int_mask` `+0x34b60` | `ldr r0,[r3,#0x14]` @`34b84`; `ldr r3,[r0,#0x48]` @`34b8c` | host MAC interrupt mask |
| `+0xa8` | 32 | `shuangta_disable_tx_err_inj_en` `+0x36cec` | `ldr r3,[r0,#0x14c]` @`36cfc`; `ldr r0,[r3]` @`36d08`; `ldr r3,[r0,#0xa8]`/`bfc r3,#0,#1`/`str` @`36d10`–`36d18` | test-mode TX FCS error injection enable |
| `+0x11c`,`+0x120` | 32 (8-bit fields) | `shuangta_get_cfg_power0_ref` `+0x33ed0` | `ldr r4,[r0,#0x150]` @`33ee4`; `ldr r4,[r4,#0x10]` @`33f08`/`33f4c`; `ldr r3,[r4,#0x120]` @`33f10`, `ldr r3,[r4,#0x11c]` @`33f64`; `uxtb`, `sxth` | band-0/band-1 power reference (calibration-style readback) |

### 2.3 DMA / rings

ETE (event-transfer-engine) ring registers — `hi5622v100_plat.ko`. The block base is a runtime field of a descriptor struct: `[desc+0xdc]` for the source ring, `[desc+0x50]` for the destination ring. The offsets *inside the block* are static:

| blk offset | width | function (.text+) | evidence | suggested purpose |
|---|---|---|---|---|
| SR `+0x08` bits[2:0] | 32 (3-bit field) | `pcie_ete_sr_reg_init` `+0x14a48` | `ldr r2,[r3,#8]` @`14ae8`; `ldrb r1,[r1,#5]`; `bfi r2,r1,#0,#3`; `str r2,[r3,#8]` @`14af0`–`14af4` | SR control (queue/band select) |
| SR `+0x10` | 32 | `pcie_ete_sr_reg_init` | `bl pcie_hostca_to_devva` @`14aac`; `str r0,[r5,#0x10]` @`14ab0` | SR ring base device address |
| SR `+0x14` bits[9:0] | 32 | `pcie_ete_sr_reg_init` | `ldr r1,[r2,#0x14]` @`14ac4`; `sub r3,r3,#1`; `bfi r1,r3,#0,#0xa`; `str r1,[r2,#0x14]` @`14ad0` | SR depth − 1 |
| SR `+0x18` | 32 | `pcie_ete_sr_reg_init` | `ldr r2,[r4,#0xc]`; `str r2,[r3,#0x18]` @`14ad8`–`14adc` | SR write pointer / watermark |
| DR `+0x30` | 32 | `pcie_ete_dr_reg_init` `+0x1483c` | `bl pcie_hostca_to_devva` @`14884`; `str r0,[r6,#0x30]` @`14888` | DR ring base device address |
| DR `+0x34` bits[9:0] | 32 | `pcie_ete_dr_reg_init` | `ldr r1,[r2,#0x34]` @`1489c`; `bfi r1,r3,#0,#0xa`; `str r1,[r2,#0x34]` @`148a4`–`148a8` | DR depth − 1 |
| DR `+0x38` | 32 | `pcie_ete_dr_reg_init` | `ldr r2,[r4,#0x1c]`; `str r2,[r3,#0x38]` @`148b0`–`148b4` | DR pointer/watermark |

MAC MSDU-info ring table — `hi5622v100_wifi.ko`. Base is a **runtime device CA** held in the chip struct at `[dev+0x92c]`; entry index is computed as `12 * (user*8 + tid)` and the result is translated with `oal_pcie_devca_to_hostva`:

| item | width | function (.text+) | evidence |
|---|---|---|---|
| table base CA `[dev+0x92c]` | 32 | `hmac_get_user_tid_tx_msdu_info_ring_table_addr` `+0x183a0` | `ldr ip,[r4,#0x92c]` @`183c8`; `add r5,r2,r1,lsl #3`; `mov r1,#0xc`; `mla r5,r1,r5,ip` @`183e8`–`183fc`; `bl oal_pcie_devca_to_hostva` @`18404` |
| table base CA `[dev+0x92c]` | 32 | `hmac_tx_msdu_info_ring_table_addr_init` `+0x184f0` | `str r1,[r4,#0x92c]` @`1851c` (writer of the base) |
| table entry: pointer at mapped_base`−4`, then u16 at `+0x42` | 32 / 16 | `hmac_tx_msdu_ring_post_proc` `+0x1a370` | `bl oal_pcie_devca_to_hostva` @`1a3b4`; `ldr r5,[sp,#0x14]` @`1a3c0`; `ldr r1,[r5,#-4]` @`1a3d0`; `ldrh r1,[r1,#0x42]` @`1a3d4` |
| LMI/cfg ring-ptr-table register (shadow) | — | `shuangta_set_msdu_info_ring_ptr_table_base` `+0x38ebc` → `hal_set_msdu_info_ring_ptr_table_base_cfg_msdu_info_ring_ptr_table_base` via `bl` @`38ee8` | writes into the RAM shadow, not MMIO (see §5.4) |

### 2.4 PHY / algorithm

The PHY/algorithm configuration seen in phases 2–3 travels on the firmware **message** path (`hmac_config_send_event`, DMAC algorithm command table `g_ast_alg_cfg_process_info_table`, `hmac_sync_dmac_alg_cfg_rsp_entry`), not on direct MMIO, so these are not register offsets. The only register-shaped PHY accessors in the binary are the PSD (power-spectral-density) accessors, and they too operate on a RAM shadow struct:

| shadow offset | width | function (.text+) | evidence |
|---|---|---|---|
| `[board+0x430]` bits[31:16] | 32 | `hal_get_psd_cfg0_cfg_psd_count` `+0x32854` | `ldr r3,[r1,#0x430]`; `lsr r3,r3,#0x10`; `str r3,[r2]` |
| `[board+0x1c]` bits[15:0] | 32 | `hal_set_rx_data_buff_len_cfg_rx_norm_buff_len` `+0x326d4` | `ldr r3,[r1,#0x1c]`; `bfi r3,r2,#0,#0x10`; `str r3,[r1,#0x1c]` |
| `[board+0x1b0]` | 32 | `hal_set_cfg_intr_mask_host_cfg_intr_mask_host` `+0x328b4` | `str r3,[r1,#0x1b0]` |

(`r1` in these is the pointer returned by `hal_device_get` `+0x3be0c`, i.e. `g_pst_hal_board + idx*0x1b0 + 0x10` — a `.bss` shadow, not device memory.)

### 2.5 Calibration

No calibration routine calls `oal_pcie_devca_to_hostva`, `ioremap`, `pcie_hostca_to_devva` or `oal_pcie_inbound_ca_to_va`. The calibration path (`hmac_sync_dmac_cali_cfg_rsp_entry`, `hmac_chan_tx_cali_sync`, `hmac_save_cali_data_to_file_2g/5g`, `hwifi_rf_cali_file_load*`, `/config/work/firmware/*cali*`) moves data over the firmware message channel and files. **No calibration MMIO offsets could be substantiated from these two objects.** (This matches the phase-3 finding that those four calibration functions never fired and carry no direct register access.)

---

## 3. Accessor functions and addresses used

All addresses are `.text`-relative (`.text.unlikely` marked) in the given module. Sizes are `st_size`.

| symbol | module | address | size | role |
|---|---|---|---|---|
| `oal_pcie_devca_to_hostva` | plat | `+0x6914` | 164 | **primary MMIO accessor**: device CA → host pointer. Bound-checks the CA against the region descriptor reached through `.LANCHOR0` (`[[*(\.LANCHOR0)]+4]+0xc4`); in-range path calls `pcie_devva_to_hostca` (`bl` @`0x697c`), otherwise tail-calls `oal_pcie_inbound_ca_to_va` (`b` @`0x69ac`). Returns `0` on success and fills the caller’s 2-word output struct; callers dereference the **first** word as the mapped pointer |
| `oal_pcie_inbound_ca_to_va` | plat | `+0x8dd8` | 348 | inbound CA→VA using a per-device region array `[dev+0x20]`, stride `0x50`, fields `[+0x28,+0x2c]` lo/hi and `[+0x30,+0x34]`; used by the PCIe message-register map |
| `pcie_hostca_to_devva` | plat | `+0xaefc` | 216 | reverse translation host CA → device VA; used by the ETE ring init |
| `pcie_get_dev_ca` | plat | `+0x6a28` | 108 | returns device CA for a dev index (dev 0: `arg + 0xC0600000`) |
| `ioremap` | plat | UNDEF (import) | — | call sites `oal_pcie_dev_init +0xa2bc`, `+0xa4b4` (each preceded by `__request_region` on `iomem_resource`) |
| `iounmap` | plat | UNDEF (import) | — | 7 sites: `oal_pcie_dev_init` (×2), `oal_pcie_regions_exit`, `oal_pcie_dev_deinit`, `exception_handler`, +2 |
| `get_pci_chip_res` | plat | `+0x6698` | 12 | returns `&g_pci_chip_res`; `g_pci_chip_res` is `.bss+0x3298`, size 120 (runtime-filled resource table) |
| `pcie_read` / `pcie_write` | plat | `+0xbd2c` / `+0xbdc0` | 148 / 156 | indirect register read/write via a function pointer resolved through a dispatch struct; not called from within plat itself (exported) |
| `bal_read` / `bal_write` | plat | `+0x10bc0` / `+0x10c04` | 68 / 72 | indirect bus access through callbacks at `[[g]+0x14]+0x3c`; used only by firmware download (`firmware_file_send`, `firmware_download`, `dev_custom_send`, `firmware_check_version`) |
| `hmac_config_reg_write` | wifi | `+0x15fa88` | 92 | **not MMIO** — wrapper that only calls `oam_error_log*`/`debug` |
| `wal_config_reg_write` | wifi | `+0x12b6b8` | 4 | branch veneer |
| `hal_device_get` | wifi | `+0x3be0c` | 300 | returns shadow-struct pointer `g_pst_hal_board + idx*0x1b0 + 0x10` |
| `hal_get_psd_cfg0_cfg_psd_count`, `hal_set_rx_data_buff_len_cfg_rx_norm_buff_len`, `hal_set_cfg_intr_mask_host_cfg_intr_mask_host`, … | wifi | `0x32174`–`0x328c0` | ≤ 400 | **not MMIO** — read/modify/write the RAM shadow via `ldr/str [r1,#off]` |

> **Important:** `hmac_config_reg_write`, `wal_config_reg_write`, and the entire `hal_get_*`/`hal_set_*` family do **not** touch the device. They read/write the software shadow `g_pst_hal_board` (a `.bss` object; `g_pst_hal_board` at `.data+0x0`, size 4, and `g_pst_hal_board` is loaded as a pointer at `hmac_config_reg_write` etc.). Real silicon access goes through `oal_pcie_devca_to_hostva`/`ioremap`.

---

## 4. Worked examples

### 4.1 PCIe message register `0x40039010` (plat.ko)

Excerpt, `shuangta_pcie_msg_reg_map` at `.text+0x1b1a0`:

```
01b1b0: movw    r1, #0x9010
01b1b4: movt    r1, #0x4003
01b1b8: add     r2, sp, #4
01b1cc: bl      <oal_pcie_inbound_ca_to_va>
01b1d8: ldr     r3, [sp, #4]
01b1e4: str     r3, [r4]            ; out[0]
```

Arithmetic: `movw` sets the low 16 bits `0x9010`; `movt` sets bits 31:16 to `0x4003`. `(0x4003 << 16) | 0x9010 = 0x40039010`. Width 32. The value is a device CA; `oal_pcie_inbound_ca_to_va` translates it to a host VA which is stored as `out[0]`. The same block maps `0x40039014` (`0x1b1e8/0x1b1ec`), `0x400392d4`, `0x40101438`, `0x40101414`, `0x400392f0` into `out[1..5]`.

### 4.2 ETE source-ring register block (plat.ko)

Excerpt, `pcie_ete_sr_reg_init` at `.text+0x14a48` (`r4` = descriptor, `r6` = chip context):

```
14a58: ldr     r5, [r4, #0xdc]      ; r5 = SR register block base (host VA)
14a70: ldr     r3, [r4, #0xec]
14a7c: ldr     r3, [r3]
14a84: lsr     r3, r3, #0x10
14aa0: ldr     r2, [r4, #0xe8]      ; source buffer host address
14aa4: ldr     r1, [r1]
14aa8: ldr     r0, [r1]
14aac: bl      <pcie_hostca_to_devva>
14ab0: str     r0, [r5, #0x10]      ; SR reg +0x10 = ring base device address
14ac0: ldrb    r3, [r3, #4]         ; depth (bytes 4 of a descriptor)
14ac4: ldr     r1, [r2, #0x14]      ; SR reg +0x14
14ac8: sub     r3, r3, #1
14acc: bfi     r1, r3, #0, #0xa     ; depth-1 into bits[9:0]
14ad0: str     r1, [r2, #0x14]      ; write back
14ad8: ldr     r2, [r4, #0xc]
14adc: str     r2, [r3, #0x18]      ; SR reg +0x18
14ae8: ldr     r2, [r3, #8]         ; SR reg +0x08
14af0: bfi     r2, r1, #0, #3
14af4: str     r2, [r3, #8]
```

Register offsets inside the SR block: `+0x08` (3-bit field), `+0x10` (32-bit base), `+0x14` (10-bit depth−1), `+0x18` (32-bit value). Block base = `desc+0xdc`. The destination-ring twin `pcie_ete_dr_reg_init` (`.text+0x1483c`) uses base `desc+0x50` and offsets `+0x30`, `+0x34` (10-bit depth−1), `+0x38`.

### 4.3 MAC register window `0x40108000..0x4010861c` (wifi.ko)

Excerpt, `shuangta_read_all_reg_info` at `.text+0x35600`:

```
35648: mov     r1, #0x8000
3564c: movt    r1, #0x4010          ; r1 = 0x40108000  (window start device CA)
35658: bl      <oal_pcie_devca_to_hostva>
35660: ldreq   r4, [sp, #0x14]      ; r4 = host VA of start
35668: rsb     sb, r4, #0x40000000  ; sb = 0x40000000 - VA
3566c: movw    fp, #0x861c
35670: movt    fp, #0x4010          ; fp = 0x4010861c  (window end device CA)
3567c: add     sb, sb, #0x108000    ; sb = 0x40108000 - VA
...
356a8: ldrb    r0, [r6]
356ac: mov     r1, fp               ; map the end address
356b8: bl      <oal_pcie_devca_to_hostva>
356bc: add     r2, sb, r4           ; devaddr(current) = 0x40108000 + (r4 - VA)
35688: ldr     r3, [sp, #0x14]      ; end VA
3568c: cmp     r4, r3
35690: bhs     <exit>
35694: ldr     r3, [r4], #4         ; read one u32, r4 += 4
```

Arithmetic: start CA = `(0x4010<<16)|0x8000 = 0x40108000`; end CA = `(0x4010<<16)|0x861c = 0x4010861c`; window length = `0x4010861c − 0x40108000 = 0x61c` bytes, dumped as `0x61c/4 = 391` u32 words. The `rsb sb,r4,#0x40000000` + `add sb,sb,#0x108000` pair reconstructs the device CA of the current pointer as `0x40108000 + (ptr − VA_base)`.

### 4.4 MAC hardware block table (wifi.ko)

Excerpt, `shuangta_host_initialize_machw` at `.text+0x3491c`:

```
34940: mov     r3, #0x28
34954: bl      <oal_mem_alloc>       ; 0x28-byte descriptor
34960: str     r0, [r6, #0x14c]      ; store descriptor ptr in chip struct
34968: ldr     ip, [pc, #0x16c]      ; ip = &rodata table (literal @0x34adc)
34978: ldrb    sb, [r6]              ; chip id
34980: ldm     ip!, {r0,r1,r2,r3}    ; copy 10 device-CA words to stack
34984: stm     r5!, {r0,r1,r2,r3}
...  (2 more ldm/stm pairs -> 10 words)
349d0: bl      <helper @0x33fcc>     ; (chip, id, &table) -> fills descriptor
```

The literal at `0x34adc` has `R_ARM_ABS32` → `.rodata+0xb60` (addend `0xb60`, section 11). Dumping that table:

```
$ python - <<'EOF'
from elftools.elf.elffile import ELFFile; import struct
e=ELFFile(open('hi5622v100_wifi.ko','rb')); d=open('hi5622v100_wifi.ko','rb').read()
for i in range(0,0x28,4): print(hex(struct.unpack_from('<I',d,0x168660+i)[0]))
EOF
0x40042000 0x40044000 0x40046000 0x40048000 0x4004a000
0x40040000 0x4004c000 0x40052000 0x40050000 0x40054000
```

The helper at `.text+0x33fcc` (an unnamed function immediately after `shuangta_get_cfg_power0_ref`; no symtab entry) loops over the 10 entries, translates each CA with `oal_pcie_devca_to_hostva`, and stores the results at descriptor offsets `+0x4, +0x8, +0xc, +0x10, +0x14, +0x18, +0x1c, +0x20, +0x24, +0x0`:

```
033ff8: ldr     r1, [r6, #4]!
034000: mov     r0, r7
03400c: bl      <oal_pcie_devca_to_hostva>
034054: str     r2, [r8, #0x24]
03407c: str     r2, [r8, #0x20]
034088: str     r2, [r8, #0x1c]
034090: str     r2, [r8, #0x18]
034098: str     r2, [r8, #0x14]
0340a0: str     r2, [r8, #0x10]
0340a8: str     r2, [r8, #0xc]
0340b0: str     r2, [r8, #8]
0340b8: str     r2, [r8, #4]
0340f0: str     r2, [r8]            ; r8 = [dev+0x14c] descriptor
```

After this, `[dev+0x14c]` holds 10 host VAs of the MAC register blocks, and the `shuangta_*` accessors in §2.2 index into them.

### 4.5 MSDU info-ring table (wifi.ko)

Excerpt, `hmac_get_user_tid_tx_msdu_info_ring_table_addr` at `.text+0x183a0`:

```
183c8: ldr     ip, [r4, #0x92c]      ; ip = table base device CA
183e8: add     r5, r2, r1, lsl #3    ; r5 = r2 + 8*r1
183ec: mov     r1, #0xc
183fc: mla     r5, r1, r5, ip        ; r5 = ip + 12*(r2 + 8*r1)
18404: bl      <oal_pcie_devca_to_hostva>
1840c: bne     <error>
18410: ldr     r2, [sp, #0x1c]       ; out[0] of the translator call
18430: str     r2, [sb]              ; *out = mapped host pointer
```

Arithmetic: entry device CA = `[dev+0x92c] + 12*(r2 + 8*r1)` (r2 and r1 are the two index arguments — tid/user). The table base is written in `hmac_tx_msdu_info_ring_table_addr_init` (`str r1,[r4,#0x92c]` @`.text+0x1851c`) and is itself a runtime value. This is a case where the register/table offset *is* statically known (`12`-byte stride, `+0x92c` field) but the base address is a runtime value, so no absolute register address can be given.

---

## 5. What I could NOT determine

### 5.1 Endpoint-1 and BAR1/BAR2 addresses
- `0x41000000`, `0x41800000`, `0x58000000`, `0x59000000`, `0x59800000` do not appear as materialised immediates in any function of either object (constant-tracking scan, §A.2) and do not appear as any 4-byte-aligned word in `.rodata`/`.data` of either object (aligned scan, §A.2).
- Endpoint-1's register window is therefore derived at run time. The candidate source is `g_pci_chip_res` (a 120-byte `.bss` object, `get_pci_chip_res` returns its address) which `oal_pcie_dev_init` walks to `__request_region` + `ioremap` each resource. Its contents are not present in the file (`.bss`), so the ep-1 base cannot be recovered statically without the runtime value or the device tree.
- The raw-byte hits for `0x58000000` inside `.text` (file offsets `0x95edd`, `0x13eeb5`) are **unaligned** (section offsets `0x95ea5`, `0x13ee7d`, ≡1 mod 4) and are instruction/data fragments, not constants.

### 5.2 Individual register offsets inside the dumped windows
`shuangta_read_soc_to_file` and `shuangta_read_all_reg_info` enumerate *windows* (`[start CA, end CA)`), reading one u32 per iteration. The sections/offsets that the windows are named after (e.g. which register sits at `0x40108004`) are not resolved by these routines; they are byte-blob dumps. Only the window bounds in §2.2 are substantiated.

### 5.3 Runtime-computed offsets from tables
- The MAC block descriptor at `[dev+0x14c]` and the analogous descriptors at `[dev+0x150]` (`shuangta_get_cfg_power0_ref`) are heap objects filled at runtime; the sub-block selection index used by each accessor is data-dependent, so `[dev+0x14c]→+0x14` vs `+0x20` etc. are *runtime-selected* blocks, not fixed offsets.
- The MSDU-info ring table (§4.5) and the TX-MSDU ring descriptors (`hmac_tx_msdu_ring_proc`, `hmac_tx_msdu_ring_post_proc`) compute addresses from fields like `[dev+0x92c]`, `[dev+0x954]`, and descriptor contents. Only the strides (`12` bytes, `8` in the tid index) and container fields are static.
- `g_alg_cfg_lut` / `g_ast_alg_cfg_process_info_table` / the `alg_*_tbl` arrays are algorithm dispatch tables consumed via messages; they are not MMIO.

### 5.4 HAL shadow offsets vs. real silicon offsets
`hal_get_*`/`hal_set_*` (93 functions) read/write the `.bss` shadow rooted at `g_pst_hal_board` (`hal_device_get` returns `g_pst_hal_board + idx*0x1b0 + 0x10`). The shadow field layouts (e.g. `psd_count` at `+0x430` bits[31:16], `rx_data_buff_len` at `+0x1c`) are config-register *shadows*; I found no proof that the shadow offset equals the hardware register offset, and no code path in these two objects that writes the shadow back out through `oal_pcie_devca_to_hostva`/`ioremap` (the shadow appears to be consumed by the firmware-message path). **These offsets must not be treated as MMIO offsets.**

### 5.5 Calibration
No calibration function performs direct MMIO (§2.5). Calibration register addresses are not derivable from `hi5622v100_wifi.ko`/`hi5622v100_plat.ko` alone.

### 5.6 Accessor semantics that remain ambiguous
`pcie_read`/`pcie_write` (`+0xbd2c`/`+0xbdc0`) and `bal_read`/`bal_write` (`+0x10bc0`/`+0x10c04`) dispatch through function pointers stored in runtime structs (`[[g]+0x14]+0x38/0x3c`), so the concrete read/write width and target window are not statically resolvable. No call site inside `hi5622v100_plat.ko` was found for `pcie_read`/`pcie_write`.

---

## Appendix A — reproduction commands

### A.1 Callers of an accessor (relocation-driven)

```
cd build/tmp
python - <<'EOF'
from elftools.elf.elffile import ELFFile
import collections
def callers(ko, target):
    e=ELFFile(open(ko,'rb'))
    funcs=[(s.name,s['st_value'],s['st_size'],s['st_shndx'])
           for s in e.get_section_by_name('.symtab').iter_symbols()
           if s.name and s['st_info']['type']=='STT_FUNC' and s['st_shndx']!='SHN_UNDEF']
    res=collections.Counter()
    for rn in ('.rel.text','.rel.text.unlikely','.rel.init.text'):
        rs=e.get_section_by_name(rn)
        if rs is None: continue
        symtab=e.get_section(rs['sh_link'])
        for r in rs.iter_relocations():
            sym=symtab.get_symbol(r['r_info_sym'])
            if sym.name==target:
                c=[(v,n,sz) for n,v,sz,idx in funcs
                   if idx==rs['sh_info'] and v<=r['r_offset']<v+sz]
                res[max(c)[1] if c else f'<{r["r_offset"]:#x}>']+=1
    return res
for t in ('oal_pcie_devca_to_hostva','ioremap','pcie_read','bal_read'):
    print(t, dict(callers('hi5622v100_plat.ko',t)))
EOF
```

### A.2 Constant-formation scan and aligned table scan

The two scans quoted in §1.4 were run as inline scripts equivalent to:

```
python - <<'EOF'
from elftools.elf.elffile import ELFFile
import capstone, re, struct
def inbar(v):
    return (0x40000000<=v<=0x401FFFFF) or (0x41000000<=v<=0x41FFFFFF) or (0x58000000<=v<=0x5A0FFFFF)
# ... per function: track movw/movt halves, mov #imm, add/sub/rsb #imm,
#     clear a destination register on any other write, record values in `inbar`
EOF
python - <<'EOF'
from elftools.elf.elffile import ELFFile; import struct
for ko in ('hi5622v100_wifi.ko','hi5622v100_plat.ko'):
    e=ELFFile(open(ko,'rb'))
    for sn in ('.rodata','.data','.text'):
        d=e.get_section_by_name(sn).data()
        for off in range(0,len(d)-3,4):
            v=struct.unpack_from('<I',d,off)[0]
            if 0x58000000<=v<=0x59FFFFFF or 0x41000000<=v<=0x4180FFFF:
                print(ko,sn,hex(off),hex(v))
EOF
```

### A.3 Disassembly with relocation annotations

```
python - <<'EOF'
from elftools.elf.elffile import ELFFile
import capstone
e=ELFFile(open('hi5622v100_plat.ko','rb')); secs=list(e.iter_sections())
st=e.get_section_by_name('.symtab'); funcs={}
for s in st.iter_symbols():
    if s.name and s['st_info']['type']=='STT_FUNC' and s['st_shndx']!='SHN_UNDEF':
        funcs.setdefault(s.name,(s['st_value'],s['st_size'],s['st_shndx']))
rels={}
for rn in ('.rel.text','.rel.text.unlikely'):
    rs=e.get_section_by_name(rn)
    if rs is None: continue
    m=rels.setdefault(rs['sh_info'],{}); symtab=e.get_section(rs['sh_link'])
    for r in rs.iter_relocations():
        m.setdefault(r['r_offset'],[]).append(symtab.get_symbol(r['r_info_sym']).name)
v,sz,idx=funcs['shuangta_pcie_msg_reg_map']; d=secs[idx].data()[v:v+sz]
md=capstone.Cs(capstone.CS_ARCH_ARM,capstone.CS_MODE_ARM)
for i in md.disasm(d,v):
    a=rels.get(idx,{}).get(i.address,[])
    print(f'{i.address:06x}: {i.mnemonic:8s} {i.op_str:30s}{"  ; "+" ".join(a) if a else ""}')
EOF
```

### A.4 Symbol/address lookup

```
$ sha256sum hi5622v100_wifi.ko hi5622v100_plat.ko
de78ec07e46e70ce8befa788a2a5d11d17e80cdea7c2789b721b0be5dcfd9521 *hi5622v100_wifi.ko
6f2eac415dbf4d3e8e71c1abd941ea4f9d97b159674d28d5e99d4ecb0e6cd491 *hi5622v100_plat.ko
```
