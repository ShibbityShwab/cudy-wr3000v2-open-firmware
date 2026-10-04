# The "channel-base" array in `hi5622v100_wifi.ko`: what its entries really are, and how they
# relate to the ETE block the port programs

Task `st_01a10725` (parent `01a0fc5c`), 2026-10-04. **Static, read-only**: the two vendor modules,
the record docs, the port source (`opensource/lab/wifidrv1/wifidrv1.c`) and
`build/register-dumps/reg_all.txt`. No device access; nothing outside this file was written.

Sources (md5 as given in the task):

| file | md5 | use |
| --- | --- | --- |
| `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` | the ETE channel table + the SR/DR register-init routines |
| `opensource/build/tmp/hi5622v100_wifi.ko` | `4737fcb21a1a2262a96f84d780ad8b35` | the array, its consumer, and the MAC-block accessors |
| `build/tmp/FIRMWARE.bin` | `0e530b976d5a20e87358671f1a577695` | cross-checks (device-side CA presence) |
| `build/register-dumps/reg_all.txt` | device `soc_register` dump, 25,154 words | every value quoted with its line number |

Method: `pyenv/Scripts/python.exe` (capstone 5.0.7, `CS_ARCH_ARM`/`CS_MODE_ARM`, `skipdata=True`) over
the full `.text` of both modules with the `.rel.text` / `.rel.data` / `.rel.rodata` relocations
applied; ELF section offsets resolved by hand. **Address conventions**: `.ko` code addresses are
`.text` section offsets (`.text` `sh_addr = 0`, file offset = vaddr + `0x38`); `.rodata` offsets are
section offsets (file = `0x167b00` + off in wifi.ko, `0x1fe40` + off in plat.ko). Every instruction
quoted below was re-disassembled from the given binary; §6 is the verification table.

---

## 0. Answers up front (including two corrections to the task's premise)

1. **There is no 12-entry channel-base array.** `.rodata+0xb60` (file `0x168660`) holds **20
   consecutive words**: **two 10-word banks**, band 0 = 2g (`.rodata+0xb60..0xb87`) and band 1 = 5g
   (`.rodata+0xb88..0xbaf`). The task's 12 values are simply the first 12 words of that 20-word run,
   and the "second copy at `0xbc0`" is the 5th word of a *different* table (`.rodata+0xbb0`, the
   2g dump list), not a copy. §1.
2. **The entries are the 2g/5g MAC register blocks**, in wifi.ko's own words (`2g_mac_register` /
   `5g_mac_register`, strings at file `0x1a7b54` / `0x1a7bf4`). They are **not** ETE ring channels:
   no word of the array is a `0x4003xxxx` CA, and the 7 ETE channel register files (SR0..2 at
   `+0x400/+0x450/+0x4a0`, DR0..3 at `+0x590/+0x5e0/+0x630/+0x680` inside CA `0x4003a000`) appear
   nowhere in it. §2, §4.
3. **`0x4003a000` and `0x4004xxxx` are two different hardware blocks inside the same inbound region
   window** (region 3: host CA base `0x40000000`, size `0x120000`). `0x4003a000` = the ETE/remap
   block (block id `0x10a`); `0x40040000`/`0x40060000` = the 2g/5g MAC blocks (block id `0x100`).
   The port programs the former; wifi.ko enumerates the latter. §4.
4. **`0x84a90000` is not a DR ring base.** `reg_all.txt` line 9188 (`addr = 4004a004`) is the **2g
   MAC CSI buffer address register**, written by `shuangta_to_hmac_csi_set_buf_addr`
   (`strne r1,[r0,#4]` @`0x3508c`, with `r0 = [chip+0x14c]+0x10` = CA `0x4004a000`). It is **array
   slot 4 of the 2g bank**; its companion size register is `+0x08` (`0x4004a008 = 0x6140`, line
   9189), and the 5g twin is `0x4006a004 = 0x84a98000` (line 19777). The ETE DR ring bases recorded
   in the *same* dump are `0x844d5000..0x844d8000` — a different block. §5.

---

## 1. The array: where it is, how big it is, and what establishes the index order

### 1.1 The table (verbatim, with file offsets)

`.rodata+0xb60`, file offset `0x168660` (wifi.ko md5 `4737fcb21a1a2262a96f84d780ad8b35`):

| word | .rodata off | file off | value | bank |
| ---: | ---: | ---: | --- | --- |
| 0 | `0xb60` | `0x168660` | `0x40042000` | 2g |
| 1 | `0xb64` | `0x168664` | `0x40044000` | 2g |
| 2 | `0xb68` | `0x168668` | `0x40046000` | 2g |
| 3 | `0xb6c` | `0x16866c` | `0x40048000` | 2g |
| 4 | `0xb70` | `0x168670` | `0x4004a000` | 2g |
| 5 | `0xb74` | `0x168674` | `0x40040000` | 2g |
| 6 | `0xb78` | `0x168678` | `0x4004c000` | 2g |
| 7 | `0xb7c` | `0x16867c` | `0x40052000` | 2g |
| 8 | `0xb80` | `0x168680` | `0x40050000` | 2g |
| 9 | `0xb84` | `0x168684` | `0x40054000` | 2g |
| 10 | `0xb88` | `0x168688` | `0x40062000` | 5g |
| 11 | `0xb8c` | `0x16868c` | `0x40064000` | 5g |
| 12 | `0xb90` | `0x168690` | `0x40066000` | 5g |
| 13 | `0xb94` | `0x168694` | `0x40068000` | 5g |
| 14 | `0xb98` | `0x168698` | `0x4006a000` | 5g |
| 15 | `0xb9c` | `0x16869c` | `0x40060000` | 5g |
| 16 | `0xba0` | `0x1686a0` | `0x4006c000` | 5g |
| 17 | `0xba4` | `0x1686a4` | `0x40072000` | 5g |
| 18 | `0xba8` | `0x1686a8` | `0x40070000` | 5g |
| 19 | `0xbac` | `0x1686ac` | `0x40074000` | 5g |

The word after 19 (`0xbb0`) starts a *different* table (the sorted 2g dump list, §3.2), and the word
before (`0xb5c`) is a small length, so the run of 20 is exactly the two banks. **The two banks are
index-aligned: bank(5g)[k] = bank(2g)[k] + 0x20000 for every k = 0..9** — verified word by word on
the table above.

`.data`/`.text` hold no copies (a full aligned scan of wifi.ko `.text`/`.rodata`/`.data` for values
in `0x4003xxxx` returns **0 words**).

### 1.2 The code that consumes it — index order is table order

`.rel.text` site `0x034adc` is an `R_ARM_ABS32` whose addend is `0x0b60` (section `.rodata`) — the
literal-pool word loaded by

```
0x034968  ldr  ip, [pc, #0x16c]        ; ip = .rodata+0xb60  (literal at .text+0x34adc)
```

inside `shuangta_host_initialize_machw` (`+0x3491c`). Immediately after, the whole run is copied to
the stack as **two 10-word arrays**:

```
0x034974  ldrb r8, [r6, #2]            ; r6 = chip ctx; byte +2 = band (0 or 1)
0x034978  ldrb sb, [r6]                ; byte +0 = chip index
0x03497c  cmp  r8, #0
0x034980  ldm  ip!, {r0, r1, r2, r3}   ; words 0..3
0x034984  stm  r5!, {r0, r1, r2, r3}   ; -> sp+0x14.. (bank A)
0x034988  ldm  ip!, {r0, r1, r2, r3}   ; words 4..7
0x03498c  stm  r5!, {r0, r1, r2, r3}   ; -> bank A
0x034990  add  r4, ip, #8              ; r4 = ip+8
0x034994  ldm  ip,  {r0, r1}           ; words 8,9
0x034998  stm  r5,  {r0, r1}           ; bank A (10 words total: sp+0x14..sp+0x38)
0x03499c  ldm  r4!, {r0, r1, r2, r3}   ; words 10..13
0x0349a0  stm  lr!, {r0, r1, r2, r3}   ; -> bank B (sp+0x3c..)
0x0349a4  ldm  r4!, {r0, r1, r2, r3}   ; words 14..17
0x0349a8  stm  lr!, {r0, r1, r2, r3}
0x0349ac  ldm  r4,  {r0, r1}           ; words 18,19
0x0349b0  stm  lr,  {r0, r1}           ; bank B (10 words total: sp+0x3c..sp+0x60)
0x0349b4  beq  #0x34a04                ; r8 == 0 -> 2g bank
...
0x0349c4  add  r2, sp, #0x3c           ; r8 == 1 -> 5g bank
0x0349d0  bl   #0x33fcc                ; map(bank)
...
0x034a04  add  r2, sp, #0x14           ; r8 == 0
0x034a10  bl   #0x33fcc
```

`[chip+2]` is the band selector: **0 selects the words 0..9 (2g), 1 selects words 10..19 (5g)**; any
other value takes the error path (`0x0349bc cmp r8,#1 / movne r4,#0`). The destination is the
`0x28`-byte allocation stored at `[chip+0x14c]` (`0x034954 bl oal_mem_alloc(#0x28)` -> `0x034960
str r0,[r6,#0x14c]`).

The helper at `+0x33fcc` maps **each CA in order** with `oal_pcie_devca_to_hostva` and stores it at
`dest + 4*(n-1)`:

```
0x033ff0  mov  r5, #1                     ; n = 1..10
0x033ff8  ldr  r1, [r6, #4]!              ; next CA from the stack bank (ascending order)
0x03400c  bl   oal_pcie_devca_to_hostva   ; R_ARM_CALL at this site
0x034024  cmp  r3, #8
0x034028  ldrls pc, [pc, r3, lsl #2]      ; jump table at .text+0x34030
0x0340f0  str  r2, [r8]                   ; n=1 -> slot 0    (fall-through path)
0x0340b8  str  r2, [r8, #4]               ; slot 1
0x0340b0  str  r2, [r8, #8]               ; slot 2
0x0340a8  str  r2, [r8, #0xc]             ; slot 3
0x0340a0  str  r2, [r8, #0x10]            ; slot 4
0x034098  str  r2, [r8, #0x14]            ; slot 5
0x034090  str  r2, [r8, #0x18]            ; slot 6
0x034088  str  r2, [r8, #0x1c]            ; slot 7
0x03407c  str  r2, [r8, #0x20]            ; slot 8
0x034054  str  r2, [r8, #0x24]            ; slot 9
```

with the jump table at `.text+0x34030` holding, in order for slots 1..9,
`0x000340b8, 0x000340b0, 0x000340a8, 0x000340a0, 0x00034098, 0x00034090, 0x00034088, 0x0003407c,
0x00034054` (and the `cmp r5,#0xa` + `str r2,[r8]` at `0x340ec`/`0x340f0` handling slot 0). The loop
runs exactly 10 times (`0x0340ec cmp r5,#0xa; bne 0x34080`).

**Therefore: array slot k (k = 0..9) is CA = table word k of the selected band's bank**, i.e. slot 0
of the 2g bank is `0x40042000` and slot 9 is `0x40054000`; the same indices select
`0x40062000 .. 0x40074000` for 5g.

The access pattern used everywhere by the consumers is `base = [chip+0x14c]; block = base[slot]`
(e.g. `0x034af0 ldr r3,[r0,#0x14c]` then `0x034afc ldr r0,[r3,#0x20]` = slot 8).

---

## 2. Entry -> name

Names below come from **the host code that reads/writes a register inside the block** (each with the
exact instruction), cross-checked against the dump value at that register. `[unknown]` = no host-side
consumer found for that slot (scan described in §6.3); the dump content is stated instead.

### 2.1 Band 0 (2g) — `.rodata+0xb60` words 0..9

| slot | CA (2g) | host consumer (file offset -> register) | name |
| ---: | --- | --- | --- |
| 0 | `0x40042000` | `shuangta_to_hmac_set_fcs_error_inj` `0x034f00 ldr r0,[r0]` -> `0x034f28 str r2,[r0,#0x924]`; `shuangta_disable_tx_err_inj_en` `0x036d08 ldr r0,[r3]` -> `0x036d10 ldrne r3,[r0,#0xa8]` / `0x036d14 bfcne r3,#0,#1` / `0x036d18 strne r3,[r0,#0xa8]`; same slot read by `shuangta_config_psdu_inj_err_param` (`0x036d80`) and `shuangta_set_test_mode_cfg_tx_fcs_err_inj_en_proc` (`0x036e48`) | **MAC test-mode / TX-FCS + PSDU error-injection block** (dump: `0x400420a8 = 6`, line 6014; `0x40042924 = 0`, line 6557) |
| 1 | `0x40044000` | none found | `[unknown]` — dump label "2g MAC 0x40044000"; holds six `0x0106xxxx` and four `0x0206xxxx` device-RAM buffer bases (`0x40044004 = 0x01066a60` .. `0x40044018`, `0x40044024..0x40044030`), plus head/tail-shaped words at `+0x44..+0x50` |
| 2 | `0x40046000` | none found | `[unknown]` — nearly empty (`0x4004600c = 0x27100064`, `0x4004608c` same) |
| 3 | `0x40048000` | `shuangta_set_cca_nav_bypass_cfg_*` (8 functions; e.g. `0x036ee0 ldr r1,[r3,#0xc]` then `0x036ef4 b hal_set_cca_nav_bypass_cfg_pri_20m_cca_bypass_en`); `shuangta_to_hmac_set_txbf_ht_matrix_buffer` `0x034d98 ldr r1,[r4,#0xc]` -> `0x034dac bl hal_set_ht_matrix_buffer_pointer...`, `0x034db8 bl hal_set_ht_matrix_buffer_step...`, `0x034dcc b hal_set_ht_matrix_buffer_num...`; unnamed reader `0x034e50 ldr r0,[r0,#0x10]` / `0x034e5c ldr r1,[r1,#0x14]` / `0x034e68 ldr r2,[r2,#0x18]` | **CCA/nav-bypass + TXBF HT-matrix-buffer block** (`+0xc` shadow, `+0x10` buffer pointer, `+0x14` step, `+0x18` num; dump `0x40048010 = 0x834c4000`, line 8275) |
| 4 | `0x4004a000` | `shuangta_to_hmac_csi_set_param` `0x034f88 ldr r0,[r3,#0x10]` -> `0x034f90 strne r1,[r0]`; `..._csi_set_buf_addr` `0x03508c strne r1,[r0,#4]`; `..._csi_set_buf_size` `0x035214 strne r1,[r0,#8]`; `..._csi_get_buf_size` `0x035290 ldrne r3,[r0,#8]`; `..._csi_get_whitelist` `0x035590 ldr r0,[r3,#0x10]` -> `0x035598 addne r3,r0,#0xc` | **MAC CSI block**: `+0x00` param, **`+0x04` buffer address**, `+0x08` buffer size, `+0x0c..` whitelist |
| 5 | `0x40040000` | `shuangta_get_host_mac_int_mask` `0x034b84 ldr r0,[r3,#0x14]` -> `0x034b8c ldrne r3,[r0,#0x48]`; `shuangta_clear_host_mac_int_status` `0x034c04` -> `0x034c0c strne r1,[r0,#0x44]`; `shuangta_host_mac_irq_mask` `0x034d08` -> RMW `[r0,#0x48]`; `shuangta_host_mac_irq_unmask` `0x034c80` -> RMW `[r0,#0x48]`; `shuangta_rx_host_init_dscr_queue` `0x0378cc ldr r5,[r4,#0x14c]` -> `0x037940 ldr r3,[r5,#0x14]`; `shuangta_ba_info_res_alloc` `0x037dd0`; `shuangta_set_msdu_info_ring_ptr_table_base` `0x038ed8 ldr r1,[r3,#0x14]` | **MAC host-interface block** (block header `+0x00 = 0x100`, line 5928): host-MAC interrupt **mask/clear** `+0x44`/`+0x48` (dump `0x40040048 = 0xfffffcc1`, line 5946) and the RX/MSDU descriptor-queue programming offsets (`+0x20 = 0x844c7000`, `+0x24 = 0x844c8000`, `+0x28 = 0x83734000`, lines 5936-5938) |
| 6 | `0x4004c000` | none found | `[unknown]` — head/tail-shaped words at `+0x04`, `+0x10`, `+0x18` (`0x4004c004 = 0x0204f8f8`, line 10108) |
| 7 | `0x40052000` | two accessors (both unnamed, adjacent to the CSI group): reader `0x035328 ldr r0,[r3,#0x1c]` -> `0x035334 ldr r1,[r0,#0x2ac]` -> `0x035348 bl oal_pcie_devca_to_hostva`; writer `0x03541c ldr r0,[r3,#0x1c]` -> `0x035424 strne r1,[r0,#0x2ac]` | **block whose `+0x2ac` holds a *device address*** (mapped through `oal_pcie_devca_to_hostva`); unset in this dump (`0x400522ac = 0`, line 10880). Otherwise `[unknown]` |
| 8 | `0x40050000` | `shuangta_get_host_mac_int_status` `0x034afc ldr r0,[r3,#0x20]` -> `0x034b04 ldrne r3,[r0]`; used together with slot 5 by `shuangta_rx_host_init_dscr_queue` (`0x037958`) and `shuangta_ba_info_res_alloc` (`0x037ddc`) | **host-MAC interrupt status block** (`+0x00`; dump `0x40050000 = 0x400`, line 10692) |
| 9 | `0x40054000` | none found | `[unknown]` — has the design's MAC address `0xab641c73` replicated at `+0x140..+0x170` and small counters |

### 2.2 Band 1 (5g) — `.rodata+0xb88` words 10..19

The 5g bank is index-aligned with the 2g bank (`+0x20000`), and the *same* consumer functions reach
it through the same slot index (the band byte `[chip+2]` selects which bank was mapped). So:

| slot | CA (5g) | 2g twin | name |
| ---: | --- | --- | --- |
| 0 | `0x40062000` | `0x40042000` | MAC test-mode / TX-FCS + PSDU error-injection block |
| 1 | `0x40064000` | `0x40044000` | `[unknown]` (buffer-base region, `+0x04..0x18` = `0x0106xxxx`, `+0x24..0x30` = `0x0206xxxx`) |
| 2 | `0x40066000` | `0x40046000` | `[unknown]` |
| 3 | `0x40068000` | `0x40048000` | CCA/nav-bypass + TXBF HT-matrix-buffer block |
| 4 | `0x4006a000` | `0x4004a000` | MAC CSI block (`0x4006a004 = 0x84a98000`, line 19777; `0x4006a008 = 0x6140`, line 19778) |
| 5 | `0x40060000` | `0x40040000` | MAC host-interface block (header `0x40060000 = 0x100`, line 16517) |
| 6 | `0x4006c000` | `0x4004c000` | `[unknown]` |
| 7 | `0x40072000` | `0x40052000` | block whose `+0x2ac` holds a device address (`[unknown]` beyond that) |
| 8 | `0x40070000` | `0x40050000` | host-MAC interrupt status block |
| 9 | `0x40074000` | `0x40054000` | `[unknown]` (MAC address `0xab641c75` replicated) |

Every slot therefore has either a named function or an explicit `[unknown]`.

---

## 3. Why the record calls these blocks "2g MAC" / "5g MAC"

### 3.1 The dump labels are wifi.ko's own strings

wifi.ko `.rodata.str1.4` contains, at file offsets `0x1a7b14`, `0x1a7b30`, `0x1a7b54`, `0x1a7b90`,
`0x1a7bac`, `0x1a7bc8`, `0x1a7bf4`:

```
"Reading 2g_soc_register!\n" "Reading 2g_rf_and_abb_register!\n" "Reading 2g_mac_register!\n"
"Reading 2g_phy_register!\n" "Reading 5g_soc_register!\n" "Reading 5g_rf_and_abb_register!\n"
"Reading 5g_mac_register!\n"
```

which is exactly the header set of `reg_all.txt` (`line 5927: Reading 2g_mac_register!`,
`line 16516: Reading 5g_mac_register!`). So "2g MAC" is the module's own name for CA range
`0x4004xxxx` and "5g MAC" for `0x4006xxxx`.

### 3.2 The sorted dump lists (the second, *ordered* pair of 10-CA tables)

`.rel.text`/`.rel.data` type-2 relocations into `.rodata` in the `0xa00..0xc60` range are only four:

| literal site | addend | table content (first words) |
| --- | --- | --- |
| `.text+0x0344d8` | `.rodata+0xae0` | `0x400b0000, 0x400b0800, 0x400b1000, ...` (5g PHY, stride `0x800`) |
| `.text+0x034adc` | `.rodata+0xb60` | the 20-word mapping table of §1 |
| `.text+0x036174` | `.rodata+0xbb0` | `0x40040000, 0x40042000, 0x40044000, 0x40046000, ...` (2g, **sorted**) |
| `.text+0x036178` | `.rodata+0xc00` | `0x40060000, 0x40062000, 0x40064000, 0x40066000, ...` (5g, **sorted**) |

The two sorted tables are loaded with `0x03592c ldr ip,[pc,#0x840]` (-> `.text+0x36174`) and
`0x035d5c ldr ip,[pc,#0x414]` (-> `.text+0x36178`), both inside `shuangta_read_all_reg_info`
(`+0x35600`, the routine that produces the `2g_mac_register` / `5g_mac_register` groups), and the
values immediately after the 2g sorted table at `.rodata+0xbd8..0xbfc` are
`0xb0, 0x92c, 0xac0, 0x1000, 0xe50, 0xe60, 0x924, 0x44, 0x2d4, 0x190` — the ten dump-window lengths
of the 2g MAC group. Note the mapping table (§1) and the dump table are the *same ten CAs in
different orders*: the extra words the task lists as an entry (`0x40050000` at table index 8) sit
between `0x40052000` and `0x40054000` in the sorted dump order.

---

## 4. The relationship between the `0x4003a000` block and the `0x4004xxxx` blocks

### 4.1 Same inbound region, different hardware blocks

Both ranges are decoded through the endpoint's **region 3** inbound viewport (`host 0x403b8000 <->
dev CA 0x40000000`, size `0x120000`; `wifidrv1.c:88-95`, `docs/phase18/inbound-map.md`), so
`BAR0 = 0x3b8000 + (CA - 0x40000000)`: CA `0x4003a000` -> BAR0 `0x3f2000`, CA `0x4004a000` ->
BAR0 `0x402a000` (which is why a host read there bus-errors — the *region* is 0x120000, so CA
`0x4011ffff` is its last address and the MAC blocks are outside every normal-op host viewport).

But they are two different blocks, and each side of the driver knows only one:

* **ETE block, CA `0x4003a000`** — `plat.ko` only. Its resource CA is a literal in `.data`:
  file `0x2944 = 0x4003a000` (+ `0x2948 = 0x40039508` for its interrupt block). Block id
  `0x4003a000 = 0x10a` (reg_all line 1844). It holds the ring-channel register files.
* **MAC blocks, CA `0x4004xxxx`/`0x4006xxxx`** — `wifi.ko` only: the 20-word table of §1 (and the
  two sorted dump tables of §3.2). The 2g "base" block's own header is `0x40040000 = 0x100`
  (line 5928), matching the 5g one `0x40060000 = 0x100` (line 16517).

The separation is complete and verified by aligned scans: **plat.ko contains 0 words in
`0x4004xxxx..0x4007ffff`** (its only CA words are `.data+0x1fa8/0x1fac/0x1fb0/0x1fb4 =
0x40039000/0x40039800/0x40037000/0x40038000` and `.data+0x2944/0x2948 = 0x4003a000/0x40039508`,
plus `.rodata+0xcb8/0xcc0 = 0x40039224/0x40039a24`), and **wifi.ko contains 0 words in
`0x4003xxxx`** — which reproduces and extends the phase-42 note that wifi.ko has no
`0x4003a000`-class constant.

### 4.2 The ETE channel files (what the port programs) are *inside* `0x4003a000`

`plat.ko` `pcie_ete_get_chn_cfg` (`+0x15f00`) returns `.rodata + 0x101c + 12*i`:

```
0x015f00  cmp   r0, #6
0x015f04  movls r2, #0xc
0x015f08  ldrls r3, [pc, #8]         ; -> .rodata (0x101c)
0x015f0c  mlals r0, r2, r0, r3
0x015f10  movhi r0, #0
```

and the table (`.rodata+0x101c`, file `0x20e5c`) holds seven 12-byte records:

| cfg index | channel (record name) | block offset | record |
| ---: | --- | ---: | --- |
| 0 | SR ch0 | `+0x400` | `0x00000400 0x06800020 0x00000001` |
| 1 | SR ch1 | `+0x450` | `0x00000450 0x06800020 0x00000001` |
| 2 | SR ch2 | `+0x4a0` | `0x000004a0 0x06800020 0x00000001` |
| 3 | DR ch0 | `+0x590` | `0x00000590 0x06800020 0x00000001` |
| 4 | DR ch1 | `+0x5e0` | `0x000005e0 0x06800020 0x00000000` |
| 5 | DR ch2 | `+0x630` | `0x00000630 0x06800020 0x00000000` |
| 6 | DR ch3 | `+0x680` | `0x00000680 0x06800020 0x00000000` |

(`cfg[4] = 0x20` is the depth, `cfg[5]` the low control bits.) There are **7 channels, not 12**:
SR0..SR2 and DR0..DR3 (**no DR4**); the port numbers DR0..DR3 as "ch3..ch6" for exactly this reason
(`wifidrv1.c:539`, `ch%u`, `i + 3`).

The field layout is fixed by `pcie_ete_sr_reg_init` (`+0x14a48`) and `pcie_ete_dr_reg_init`
(`+0x1483c`):

```
0x014a58  ldr  r5, [r4, #0xdc]      ; SR block VA
0x014ab0  str  r0, [r5, #0x10]      ; SR +0x10 = base (device VA of the node array)
0x014ad0  str  r1, [r2, #0x14]      ; SR +0x14 = depth-1  (bfi r1,r3,#0,#0xa @0x14acc)
0x014adc  str  r2, [r3, #0x18]      ; SR +0x18 = producer
0x014af4  str  r2, [r3, #8]         ; SR +0x08 = ctrl bits
0x014888  str  r0, [r6, #0x30]      ; DR +0x30 = base
0x0148a8  str  r1, [r2, #0x34]      ; DR +0x34 = depth-1
0x0148b4  str  r2, [r3, #0x38]      ; DR +0x38 = producer
```

which is exactly what the port writes (`wifidrv1.c:519-545`: SR `+0x10/+0x14/+0x18/+0x08`, DR
`+0x30/+0x34/+0x38`, with `ETE_SR0_BASE 0x400`, `ETE_DR0_BASE 0x590`).

### 4.3 What the dump shows for those channels — and the one per-channel register the port never writes

`reg_all.txt`, ETE block (`0x4003a000..0x4003ac30`), quoted with line numbers:

| CA | value | line | meaning |
| --- | --- | ---: | --- |
| `0x4003a400` | `1` | 2100 | SR ch0 `+0x00` = **channel enable** |
| `0x4003a410` | `0x844db000` | 2104 | SR ch0 `+0x10` = node-array base (host DRAM) |
| `0x4003a414` | `0x1f` | 2105 | SR ch0 `+0x14` = depth-1 = 31 |
| `0x4003a418` | `0x1d` | 2106 | SR ch0 `+0x18` = producer |
| `0x4003a41c` | `0x1d` | 2107 | SR ch0 `+0x1c` = consumer |
| `0x4003a430` | `0x1060750` | 2112 | SR ch0 **`+0x30`** = second base (device RAM `0x0106xxxx`) |
| `0x4003a434` | `0x1f` | 2113 | SR ch0 `+0x34` = depth-1 (same 31) |
| `0x4003a438` | `0x1d` | 2114 | SR ch0 `+0x38` = producer (same index) |
| `0x4003a43c` | `0x1d` | 2115 | SR ch0 `+0x3c` = consumer (same index) |
| `0x4003a448` | `1` | 2118 | SR ch0 `+0x48` = second enable (the port's `OMO_SR_EN1`) |
| `0x4003a590` | `1` | 2200 | **DR ch0 `+0x00` = channel enable** |
| `0x4003a5a0` | `0x1060440` | 2204 | DR ch0 `+0x10` = first base (device RAM `0x0106xxxx`) |
| `0x4003a5a4/+0x5a8/+0x5ac` | `0x1f`/`3`/`3` | 2205-2207 | DR ch0 `+0x14/+0x18/+0x1c` = depth-1 / producer / consumer |
| `0x4003a5c0` | `0x844d8000` | 2212 | DR ch0 **`+0x30`** = second base (host DRAM) |
| `0x4003a5c4/+0x5c8/+0x5cc` | `0x1f`/`3`/`3` | 2213-2215 | DR ch0 `+0x34/+0x38/+0x3c` |
| `0x4003a5e0` | `1` | 2220 | DR ch1 `+0x00` enable |
| `0x4003a610` | `0x844d7000` | 2232 | DR ch1 `+0x30` base |
| `0x4003a630` | `1` | 2240 | DR ch2 `+0x00` enable |
| `0x4003a660` | `0x844d6000` | 2252 | DR ch2 `+0x30` base |
| `0x4003a680` | `1` | 2260 | DR ch3 `+0x00` enable |
| `0x4003a6b0` | `0x844d5000` | 2272 | DR ch3 `+0x30` base |

So **each ETE channel carries two base/depth/producer/consumer quadruples** — a host-DRAM one and a
device-RAM (`0x0106xxxx`) one — with the *same* depth and the *same* indices (SR ch0: `0x1f/0x1d/0x1d`
at both `+0x10` and `+0x30`; DR ch0: `0x1f/3/3` at both). For SR the DRAM base is at `+0x10` and the
`0x0106xxxx` base at `+0x30`; for DR it is the other way round. The 5g twin is in the 5g MAC blocks
only in the sense of §2 — the ETE block itself has no band twin; it is shared.

**Deposit-relevant observation** (it falls out of this reconciliation, and is a candidate, not a
proof): the port writes SR `+0x10/+0x14/+0x18/+0x08` and DR `+0x30/+0x34/+0x38`
(`wifidrv1.c:525-544`), plus the two SR enables `+0x00` and `+0x48` (`wifidrv1.c:1648-1655`). It
**never writes the DR channel enable `block+0x00`**, which the vendor has set to `1` on all four DR
channels in this dump (lines 2200/2220/2240/2260), and it never writes either channel's *second*
base quadruple (SR `+0x30`, DR `+0x10`, the `0x0106xxxx` one). Those are the two per-channel
registers that differ between the vendor's live ETE block and the port's program; the first is the
cheapest candidate for a deposit-activation precondition.

---

## 5. Which entry "holds the DR ring whose base is 0x84a90000"? — none; the premise

The value `0x84a90000` occurs exactly once in `reg_all.txt`:

```
line 9188:  addr = 4004a004, value = 84a90000
line 9189:  addr = 4004a008, value = 6140
```

`0x4004a004` is a register of the **2g MAC CSI block**, which is **array slot 4 of the 2g bank**
(table word 4 = `0x4004a000`, §1.1). The register is the CSI **buffer address**, and its neighbour
is the CSI **buffer size** — established from the CSI accessor family, all of which take
`r0 = [chip+0x14c]+0x10` (slot 4):

```
shuangta_to_hmac_csi_set_param       0x034f88 ldr r0,[r3,#0x10] ; 0x034f90 strne r1,[r0]
shuangta_to_hmac_csi_get_param       0x035004 ldr r0,[r3,#0x10] ; 0x03500c ldrne r3,[r0]
shuangta_to_hmac_csi_set_buf_addr    0x035084 ldr r0,[r3,#0x10] ; 0x03508c strne r1,[r0,#4]   <-- 0x4004a004
shuangta_to_hmac_csi_set_buf_size    0x03520c ldr r0,[r3,#0x10] ; 0x035214 strne r1,[r0,#8]   <-- 0x4004a008
shuangta_to_hmac_csi_get_buf_size    0x035288 ldr r0,[r3,#0x10] ; 0x035290 ldrne r3,[r0,#8]
shuangta_to_hmac_csi_get_whitelist   0x035590 ldr r0,[r3,#0x10] ; 0x035598 addne r3,r0,#0xc
```

(one more reader, the unnamed function at `0x035110`, uses the same slot.) The 5g twin is slot 4 of
the 5g bank: `0x4006a004 = 0x84a98000` (line 19777) with the same size `0x6140` (line 19778).

It is **not** an ETE DR ring base, for three independent reasons:

1. It is not in the ETE block: `0x4004a000` is a `0x4004xxxx` MAC CA, and the ETE DR bases in this
   dump are `0x844d5000`/`0x844d6000`/`0x844d7000`/`0x844d8000` (lines 2212/2252/2232/2272).
2. There is no depth/producer/consumer register next to it: the pair is `{address, size}`
   (`+0x04`, `+0x08`), against the ETE channel's `{base, depth-1, producer, consumer}`.
3. The code that writes it is the CSI parameter path, not any ring path.

For completeness, the *other* host-DRAM bases that the MAC blocks in this dump hold (all also **not**
ETE DR rings, but the closest things to DMA-ring bases in the array): slot 5 gives the RX/MSDU
descriptor-queue region `0x844c7000`/`0x844c8000`/`0x83734000` (`0x40040020/24/28`, lines 5936-5938,
written for `shuangta_rx_host_init_dscr_queue` / `shuangta_set_msdu_info_ring_ptr_table_base`), slot
3 gives the TXBF HT-matrix buffer `0x834c4000` (`0x40048010`, line 8275), and slot 1 gives the six
`0x0106xxxx` + four `0x0206xxxx` device-RAM buffer bases (`0x40044004..`, phase-13's "buffer/ring
base" rows). If the deposit target is to be found in host DRAM, the ETE DR `+0x30` base (or the
`0x0106xxxx` second base at DR `+0x10`) is where the vendor's own ring registers point.

---

## 6. Verification

### 6.1 Every quoted instruction re-disassembles (capstone 5.0.7, `CS_MODE_ARM`, `skipdata=True`)

| file offset in wifi.ko | instruction | used in |
| --- | --- | --- |
| `0x034968` | `ldr ip, [pc, #0x16c]` | §1.2 |
| `0x034974`/`0x034978` | `ldrb r8,[r6,#2]` / `ldrb sb,[r6]` | §1.2 |
| `0x034980`/`0x034988`/`0x034994`/`0x034990`/`0x03499c`/`0x0349a4`/`0x0349ac` | `ldm ip!,{r0,r1,r2,r3}` / `ldm ip!,{r0,r1,r2,r3}` / `ldm ip,{r0,r1}` / `add r4,ip,#8` / `ldm r4!,{r0,r1,r2,r3}` / `ldm r4!,{r0,r1,r2,r3}` / `ldm r4,{r0,r1}` | §1.2 |
| `0x0349b4`/`0x0349c4`/`0x0349d0`/`0x034a04`/`0x034a10` | `beq #0x34a04` / `add r2,sp,#0x3c` / `bl #0x33fcc` / `add r2,sp,#0x14` / `bl #0x33fcc` | §1.2 |
| `0x034954`/`0x034960` | `bl #0x34954` (`R_ARM_CALL oal_mem_alloc`) / `str r0,[r6,#0x14c]` | §1.2 |
| `0x033ff0`/`0x033ff8`/`0x03400c`/`0x034024`/`0x034028` | `mov r5,#1` / `ldr r1,[r6,#4]!` / `bl` (`R_ARM_CALL oal_pcie_devca_to_hostva`) / `cmp r3,#8` / `ldrls pc,[pc,r3,lsl #2]` | §1.2 |
| `0x0340f0`/`0x0340b8`/`0x0340b0`/`0x0340a8`/`0x0340a0`/`0x034098`/`0x034090`/`0x034088`/`0x03407c`/`0x034054` | `str r2,[r8]` / `str r2,[r8,#4]` / `str r2,[r8,#8]` / `str r2,[r8,#0xc]` / `str r2,[r8,#0x10]` / `str r2,[r8,#0x14]` / `str r2,[r8,#0x18]` / `str r2,[r8,#0x1c]` / `str r2,[r8,#0x20]` / `str r2,[r8,#0x24]` | §1.2 |
| `.text+0x34030` (9 words) | `0x340b8, 0x340b0, 0x340a8, 0x340a0, 0x34098, 0x34090, 0x34088, 0x3407c, 0x34054` (jump table); `0x0340ec cmp r5,#0xa` | §1.2 |
| `0x034ef4`/`0x034f00`/`0x034f28` | `ldr r0,[r0,#0x14c]` / `ldr r0,[r0]` / `str r2,[r0,#0x924]` | slot 0 |
| `0x036cfc`/`0x036d08`/`0x036d10`/`0x036d14`/`0x036d18` | `ldr r3,[r0,#0x14c]` / `ldr r0,[r3]` / `ldrne r3,[r0,#0xa8]` / `bfcne r3,#0,#1` / `strne r3,[r0,#0xa8]` | slot 0 |
| `0x036ed4`/`0x036ee0`/`0x036ef4` | `ldr r3,[r1,#0x14c]` / `ldr r1,[r3,#0xc]` / `b` (`R_ARM_JUMP24 hal_set_cca_nav_bypass_cfg_pri_20m_cca_bypass_en`) | slot 3 |
| `0x034d88`/`0x034d98`/`0x034dac`/`0x034db8`/`0x034dcc` | `ldr r4,[ip,#0x14c]` / `ldr r1,[r4,#0xc]` / `bl hal_set_ht_matrix_buffer_pointer...` / `bl hal_set_ht_matrix_buffer_step...` / `b hal_set_ht_matrix_buffer_num...` | slot 3 |
| `0x034e44`/`0x034e50`/`0x034e5c`/`0x034e68` | `ldr r0,[ip,#0xc]` / `ldr r0,[r0,#0x10]` / `ldr r1,[r1,#0x14]` / `ldr r2,[r2,#0x18]` | slot 3 |
| `0x034f88`/`0x035084`/`0x03508c`/`0x03520c`/`0x035214`/`0x035288`/`0x035290`/`0x035590`/`0x035598` | `ldr r0,[r3,#0x10]` (x5) / `strne r1,[r0,#4]` / `strne r1,[r0,#8]` / `ldrne r3,[r0,#8]` / `addne r3,r0,#0xc` | slot 4 (§5) |
| `0x034b84`/`0x034b8c`/`0x034c04`/`0x034c0c`/`0x034d08`/`0x034c80` | `ldr r0,[r3,#0x14]` / `ldrne r3,[r0,#0x48]` / `ldr r0,[r3,#0x14]` / `strne r1,[r0,#0x44]` / `ldr r0,[r3,#0x14]` (mask/unmask) | slot 5 |
| `0x0378cc`/`0x037940`/`0x037dd0`/`0x038ed8` | `ldr r5,[r4,#0x14c]` / `ldr r3,[r5,#0x14]` / `ldr r3,[r5,#0x14]` / `ldr r1,[r3,#0x14]` | slot 5 |
| `0x035328`/`0x035334`/`0x035348`/`0x03541c`/`0x035424` | `ldr r0,[r3,#0x1c]` / `ldr r1,[r0,#0x2ac]` / `bl` (`R_ARM_CALL oal_pcie_devca_to_hostva`) / `ldr r0,[r3,#0x1c]` / `strne r1,[r0,#0x2ac]` | slot 7 |
| `0x034af0`/`0x034afc`/`0x034b04` | `ldr r3,[r0,#0x14c]` / `ldr r0,[r3,#0x20]` / `ldrne r3,[r0]` | slot 8 |
| `0x03592c`/`0x035d5c` | `ldr ip,[pc,#0x840]` -> `.text+0x36174`; `ldr ip,[pc,#0x414]` -> `.text+0x36178` | §3.2 |
| `.rel.text` `0x034adc` / `0x036174` / `0x036178` / `0x0344d8` | `R_ARM_ABS32` to `.rodata` with addend `0x0b60` / `0x0bb0` / `0x0c00` / `0x0ae0` | §1.1, §3.2 |

### 6.2 plat.ko

| file offset | instruction | used in |
| --- | --- | --- |
| `0x015f00`/`0x015f04`/`0x015f08`/`0x015f0c`/`0x015f10` | `cmp r0,#6` / `movls r2,#0xc` / `ldrls r3,[pc,#8]` / `mlals r0,r2,r0,r3` / `movhi r0,#0` | §4.2 |
| `0x014a58`/`0x014ab0`/`0x014ad0`/`0x014adc`/`0x014af4` | `ldr r5,[r4,#0xdc]` / `str r0,[r5,#0x10]` / `str r1,[r2,#0x14]` / `str r2,[r3,#0x18]` / `str r2,[r3,#8]` | §4.2 |
| `0x014854`/`0x014888`/`0x0148a8`/`0x0148b4` | `ldr r6,[r1,#0x50]` / `str r0,[r6,#0x30]` / `str r1,[r2,#0x34]` / `str r2,[r3,#0x38]` | §4.2 |
| `.rodata+0x101c` (file `0x20e5c`), 7 x 12 bytes | the channel cfg table of §4.2 | §4.2 |
| `.data+0x2944`/`+0x2948` (file `0x348a4`/`0x348a8`) | `0x4003a000` / `0x40039508` | §4.1 |

### 6.3 The "no consumer" result and the value citations

* Slot coverage was searched over the **whole** `.text` (363,571 instructions, `skipdata=True`): for
  every `ldr rX,[rY,#0x14c]`, every later use of `rX` with an immediate offset in
  `{0,4,8,0xc,0x10,0x14,0x18,0x1c,0x20,0x24}` was recorded. Slots hit: **0, 3, 4, 5, 7, 8**
  (all listed in §2). Slots **1, 2, 6, 9** had no hit anywhere in `hi5622v100_wifi.ko` and are
  reported `[unknown]`.
* Dump values are quoted with the file's own line numbers (format `addr = HEX, value = HEX`); the
  lines used above are 1844, 2100, 2104-2107, 2112-2115, 2118, 2200, 2204-2207, 2212-2215, 2220,
  2232, 2240, 2252, 2260, 2272, 5928, 5933, 5936-5938, 5945-5946, 6014, 6557, 8275, 9187-9189,
  10108, 10692, 10880, 16517, 19777-19778.
* Cross-checks on the firmware: `FIRMWARE.bin` contains `0x4003a000` at file `0xccedc`/`0xcf2a0`,
  `0x4003a400` at `0xd040c`, `0x4003a590` at `0xcf5d4` (the ETE channels), and `0x40040000` at
  `0xc61d8`/`0xe2c08` inside a `{CA, code}` block list `{0x40000000,0x23}, {0x40040000,0x23},
  {0x40080000,0x25}, {0x40100000,0x21}` — i.e. the device side also treats `0x40040000` as a block
  CA, but no firmware word holds `0x40042000`, `0x4004a000`, `0x4004c000`, `0x40050000` or
  `0x84a90000`.

---

## 7. Scope and hazards

* Static, read-only work: no device access, no register write, no `devmem`. The only write is this
  file, under `opensource/docs/phase43/` (directory created for it).
* The two corrections in §0 are the load-bearing findings: **the array is 2 x 10 MAC blocks, not 12
  channels**, and **`0x4004a004 = 0x84a90000` is a CSI buffer address, not a DR ring base**.
  Anything that follows the task's original framing (looking for a DR ring in the `0x4004xxxx`
  range) will look in the wrong block; the vendor's DR ring registers are at ETE `+0x30` (host DRAM)
  and `+0x10` (device RAM `0x0106xxxx`) inside CA `0x4003a000`, with the DR enable at `+0x00` — the
  one register the port's write path does not set (§4.3).
* `0x4004xxxx` is reachable by the host only through a viewport that covers it (region 3 stops at CA
  `0x4011ffff`, and the MAC block offsets at CA `0x402a000` bus-error) — so none of §2 can be
  re-measured through the port's existing windows without a new viewport.
