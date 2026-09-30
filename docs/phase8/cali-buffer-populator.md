# Calibration buffer: who fills it (phase 8, 2026-10-01)

Scope: local, read-only. Inputs: `build/tmp/hi5622v100_plat.ko`, `build/tmp/hi5622v100_wifi.ko`
(ARM32 ET_REL, full `.symtab`). Tooling: `pyenv/Scripts/python.exe` + pyelftools 0.33 + capstone 5.0.7.
All addresses are link-time offsets (section vaddrs are 0, so symbol value == address). Relocation
types: 43 = `R_ARM_MOVW_ABS_NC`, 44 = `R_ARM_MOVT_ABS`, 28 = `R_ARM_CALL`, 29 = `R_ARM_JUMP24`,
2 = `R_ARM_ABS32`.

**Headline.** The buffer the save routines write is a heap block (`oal_noncache_alloc`) whose
**pointer** lives in `.bss` of `plat.ko` at offset `0x120` (5g) / `0x124` (2g). That block is filled
**only** by `hwifi_rf_cali_file_load_5g` / `_2g`, via `memset_s` + **`kernel_read` of the whole file**
(0x22d0 / 0x920 bytes) — a bulk byte copy, not a field-by-field packer. There is **no `str`/`strh`/
`strb` of any calibration field at a constant offset into this buffer anywhere in either module**
(scan in §3). Therefore the file's field ORDER is not defined by these binaries; the driver is a
transparent round-tripper and the order is whatever the `.kv` producer wrote. §5 states exactly what
this does and does not settle.

---

## 1. The global the writers read

The writers (in `wifi.ko`) do not touch a symbol directly; they call the imported accessors
`hmac_rf_cali_host_addr` / `_2g`, which live in `plat.ko`:

```
===== plat.ko
000014d4: 003000e3 movw  r3, #0        ; RELOC .LANCHOR0 (st_value=0x120, shndx=30=.bss)
000014d8: 003040e3 movt  r3, #0        ; RELOC .LANCHOR0 (st_value=0x120, shndx=30=.bss)
000014dc: 000093e5 ldr   r0, [r3]      ; return *(void**)(.bss+0x120)
000014e0: 1eff2fe1 bx    lr
--- hmac_rf_cali_host_addr_2g @0x14e4 (16 B)
000014e4: 003000e3 movw  r3, #0        ; RELOC .LANCHOR0 (0x120, .bss)
000014e8: 003040e3 movt  r3, #0        ; RELOC .LANCHOR0 (0x120, .bss)
000014ec: 040093e5 ldr   r0, [r3, #4]  ; return *(void**)(.bss+0x124)
```
(`hmac_rf_cali_data_dma_addr` @0x150c reads `[r3,#8]` = `.bss+0x128`; `_2g` @0x151c reads
`[r3,#0xc]` = `.bss+0x12c`.)

Resolution:

| item | value |
|---|---|
| symbol | `.LANCHOR0`, `STT_NOTYPE`, `st_shndx = 30` (`.bss`) |
| address of the anchor | section-relative `0x120` in `.bss` |
| `plat.ko` `.bss` | vaddr `0x0`, `sh_size = 0x4ee4`, file offset `0x364c8` |
| slot read by the 5g writer | `.bss+0x120` — 4-byte pointer, **unnamed** (no `STT_OBJECT` symbol) |
| slot read by the 2g writer | `.bss+0x124` — 4-byte pointer, unnamed |
| DMA-addr slots | `.bss+0x128` (5g), `.bss+0x12c` (2g) |
| `.bss+0x130` | `g_st_cust_country_code_ignore_flag` (size 4) — **not** cali |
| `.bss+0x134` | `g_al_host_init_params` (size 816) — **not** cali |

`.LANCHOR0` is a shared GCC anchor: many functions reference it at large offsets for unrelated
statics. Only offsets `0x0/0x4/0x8/0xc` are the calibration pointer slots (and `+0x10` is the country
flag at `.bss+0x130`). The pointed-to buffer is **heap**, not `.bss`: it is allocated at runtime (see
§3), size `0x22d0` (5g) / `0x920` (2g).

`wifi.ko` cannot reference this anchor at all — it imports the accessor functions as undefined:
`hmac_rf_cali_host_addr` (`SHN_UNDEF`, `v=0`) and `_2g` likewise, called at `0xc3e5c` / `0xc3f54`.

---

## 2. Cross-references to `.bss+0x120` in both modules

Method: enumerate every `.rel.*` section whose target is `.text`, keep relocations whose symbol is
named `.LANCHOR0` and whose `st_value == 0x120`, and map each `r_offset` to its enclosing `STT_FUNC`.
Command (excerpt of the script that produced the tables):

```python
for sec in secs:                      # secs = ELFFile.iter_sections()
  if not sec.name.startswith('.rel') or idx2name.get(sec['sh_info']) != '.text': continue
  for r in sec.iter_relocations():
    sy = symtab.get_symbol(r['r_info_sym'])
    if sy.name == '.LANCHOR0' and sy['st_value'] == 0x120:
        ...  # (address, type, owner-function)
```

### 2.1 `hi5622v100_plat.ko` — direct refs (MOVW/MOVT pair → base register)

| `.text` addr | type | function | what it does |
|---|---|---|---|
| `0x14d4`, `0x14d8` | 43/44 | `hmac_rf_cali_host_addr` | returns `*(void**)(.bss+0x120)` |
| `0x14e4`, `0x14e8` | 43/44 | `hmac_rf_cali_host_addr_2g` | returns `*(void**)(.bss+0x124)` |
| `0x150c`, `0x1510` | 43/44 | `hmac_rf_cali_data_dma_addr` | returns `*(void**)(.bss+0x128)` |
| `0x151c`, `0x1520` | 43/44 | `hmac_rf_cali_data_dma_addr_2g` | returns `*(void**)(.bss+0x12c)` |
| `0x152c`, `0x1530` | 43/44 | `hwfi_set_country_code_ingore_hipriv_flag` | `strb r0,[r3,#0x10]` → `.bss+0x130` (country flag, same anchor) |
| `0x2c98`, `0x2c9c` | 43/44 | `hwifi_rf_cali_file_load_2g` | **stores the 2g buffer pointer** (§3) |
| `0x2e48`, `0x2e4c` | 43/44 | `hwifi_rf_cali_file_load_5g` | **stores the 5g buffer pointer** (§3) |

The same `.LANCHOR0`(0x120) symbol is additionally referenced at larger offsets by unrelated
functions sharing the anchor (e.g. `hwifi_config_init`, `oal_stat_dump_help`,
`hwifi_get_plat_tag_from_country_code`, `hwifi_get_ini_*`). Those accesses are at offsets `>= 0x14`
and hit other statics, not the cali slots; they are listed here only so the anchor reference is not
mistaken for a cali access.

### 2.2 `hi5622v100_wifi.ko` — no direct ref; accessor call sites only

```
.text+0xc3e5c -> hmac_rf_cali_host_addr    (UNDEF) in FUNC hmac_save_cali_data_to_file_5g
.text+0xc3f54 -> hmac_rf_cali_host_addr_2g (UNDEF) in FUNC hmac_save_cali_data_to_file_2g
```
No relocation in `wifi.ko` targets `.bss+0x120` itself: that section belongs to a different module.

---

## 3. The functions that store into the buffer — and the decisive negative

### 3.1 The only writers of the slot: the two file loaders

`hwifi_rf_cali_file_load_5g` @`0x2e44` (444 B), relevant stores ("r4 = .bss+0x120"):

```
00002e48: 004000e3 movw  r4, #0            ; RELOC .LANCHOR0 (0x120, .bss)
00002e4c: 004040e3 movt  r4, #0            ; RELOC .LANCHOR0 (0x120, .bss)
00002e5c: 98319fe5 ldr   r3, [pc, #0x198]  ; stack canary
00002e60: 0d10a0e1 mov   r1, sp            ; out-param for the DMA/physical address
00002e64: d00202e3 movw  r0, #0x22d0       ; size 8912
00002e68: 003093e5 ldr   r3, [r3]
00002e6c: 04308de5 str   r3, [sp, #4]
00002e70: 0030a0e3 mov   r3, #0
00002e74: 0050a0e3 mov   r5, #0
00002e80: 00508de5 str   r5, [sp]         ; [sp]=0 before call
00002e8c: feffffeb bl    oal_noncache_alloc
00002e90: 006050e2 subs  r6, r0, #0       ; r6 = buffer VA
00002e9c: d03202e3 movw  r3, #0x22d0
00002ea0: 0310a0e1 mov   r1, r3
00002ea4: feffffeb bl    memset_s           ; memset(buffer, 0x22d0, 0)
00002eac: 00309de5 ldr   r3, [sp]           ; r3 = allocator's out-param (DMA/physical addr; inferred)
00002eb4: 000000e3 movw  r0, #0            ; RELOC .LANCHOR1 (0xac,.data) = "/usr/local/factory/wifi_cali_data.kv"
00002eb8: 000040e3 movt  r0, #0
00002ec4: 006084e5 str   r6, [r4]          ; *** .bss+0x120 = buffer VA             ***
00002ec8: 083084e5 str   r3, [r4, #8]      ; *** .bss+0x128 = DMA/phys addr         ***
00002ef4: 0610a0e1 mov   r1, r6
00002ef8: d02202e3 movw  r2, #0x22d0
00002efc: feffffeb bl    kernel_read        ; * bulk fill: read 0x22d0 bytes into buffer *
00002f0c..0x2f38: ldr [r4]; check buffer[0]==0x5a5a5a5a and buffer[0x22cc]==0xa5a5a5a5
00002fd4: f434c8e5 strb  r3, [r8, #0x4f4]  ; r8=r4+0x1000 -> .bss+0x1614: "5g loaded" flag
```

`hwifi_rf_cali_file_load_2g` @`0x2c94` (432 B) is the twin:

```
00002ca4: 018a84e2 add   r8, r4, #0x1000
00002cb4: 920ea0e3 mov   r0, #0x920        ; size 2336
00002cdc: feffffeb bl    oal_noncache_alloc
00002cf4: feffffeb bl    memset_s
00002d10: 046084e5 str   r6, [r4, #4]      ; *** .bss+0x124 = 2g buffer VA          ***
00002d14: 0c3084e5 str   r3, [r4, #0xc]    ; *** .bss+0x12c = 2g DMA/phys addr      ***
00002d48: feffffeb bl    kernel_read        ; * bulk fill: read 0x920 bytes into buffer *
00002d58..0x2d7c: check buffer[0]==0x5a5a5a5a and buffer[0x91c]==0xa5a5a5a5
00002e14: f534c8e5 strb  r3, [r8, #0x4f5]  ; .bss+0x1615: "2g loaded" flag
```

So the *only* stores that ever target the buffer include exactly these four slot writes; the buffer
CONTENTS arrive through `kernel_read`. Note the loader also stores a second copy of the physical
address/zero pair at `.bss+0x1618`..`0x1624` (via `r7 = r4+0x1500`), which is an unrelated static
block and is **not** part of the calibration global.

`hwifi_rf_cali_file_load` @`0x3000` (68 B) just calls both loaders with an argument; `alg_eqment`'s
`hmac_save_cali_data_to_file` dispatcher re-saves both bands.

### 3.2 Negative: no per-field stores exist

To test the "populator with constant-offset stores" hypothesis directly, every function in both `.ko`
was disassembled with a per-function register taint (capstone detail mode):

* mark a register when `movw/movt` relocates to `.LANCHOR0`(0x120) → `anchor`;
* propagate through `mov/add/sub`;
* `ldr rX,[anchor]` → `buf5g`, `ldr rX,[anchor,#4]` → `buf2g`; also `r0` after a call to
  `hmac_rf_cali_host_addr` / `_2g`;
* report every `str/strh/strb/strd` whose base register carries `buf5g`/`buf2g`.

Result over `hi5622v100_plat.ko`: the **only** hits at all in the module are the four loader slot
stores above (plus, at `.bss+0x120`-relative small offsets, the accessor `ldr`s). Over
`hi5622v100_wifi.ko`: **zero** hits. A second, taint-free scan for struct-initializer signatures
(≥25 constant-offset stores through one base register) surfaced only unrelated builders
(`original_value_for_dts_params`, `hwifi_get_dts_txpow_adj_customize_param`,
`hwifi_get_ini_customize_param`, `sdt_drv_netlink_recv`, …) — none of them reaches the cali buffer.

Complementary call-site scans (all `.rel.*` relocations) confirm the buffer is only ever
allocated/read by the loaders, and only ever read by the save routines:

```
plat.ko : oal_noncache_alloc  -> 0x2cdc (hwifi_rf_cali_file_load_2g), 0x2e8c (hwifi_rf_cali_file_load_5g)   [no other callers]
plat.ko : kernel_read         -> 0x2d48 (_2g), 0x2efc (_5g)  [+ firmware_kernel_read, oal_print_hex_dump, ini_readline — unrelated]
wifi.ko : hmac_rf_cali_host_addr    -> called only from hmac_save_cali_data_to_file_5g (0xc3e5c)
wifi.ko : hmac_rf_cali_host_addr_2g -> called only from hmac_save_cali_data_to_file_2g (0xc3f54)
```

**Conclusion of §3: the buffer's field order is not defined by any code in these two modules.** The
loader copies the file verbatim (validating only offset `+0` and `+len-4`), and the saver writes that
same image back.

---

## 4. Callers of the save routines (who decides to save, at what event)

`wifi.ko`:

| save routine | call site | caller | trigger |
|---|---|---|---|
| `hmac_save_cali_data_to_file` @`0xc4044` | `0x3f86c` | `hmac_main_init` @`0x3f6c8` | interface bring-up: called right after `hmac_board_init` returns 0 and `hcc_set_state(4)` — saves **both** bands once per boot |
| `hmac_save_cali_data_to_file_5g` @`0xc3e54` | `0x87bc4` | `alg_eqment_config_param_output_entry` @`0x87b38` | command id `0xdc4` |
| `hmac_save_cali_data_to_file_2g` @`0xc3f4c` | `0x87d00` | `alg_eqment_config_param_output_entry` | command id `0xdc3` |

`hmac_main_init` tail:

```
0003f850: bl  hmac_fsm_init
0003f854: ldr r0, [r5]                 ; g_pst_mac_board
0003f858: bl  hmac_board_init
0003f85c: subs r6, r0, #0
0003f860: bne ...                      ; board init failed -> skip
0003f864: mov r0, #4
0003f868: bl  hcc_set_state
0003f86c: bl  hmac_save_cali_data_to_file   ; <-- save both bands
```

`alg_eqment_config_param_output_entry` dispatch (id in `ip`):

```
00087b9c: movw r3, #0xdc2 ; cmp ip,r3 ; beq -> alg_cfg_process_eqment_check_output
00087ba8: movw r3, #0xdc3 ; cmp ip,r3 ; beq 0x87cfc -> hmac_save_cali_data_to_file_2g
00087bb4: movw r3, #0xdc4 ; cmp ip,r3 ; bne 0x87c90 (error)
00087bc0: ldrb r0, [r0, #2]                  ; chip id argument
00087bc4: bl   hmac_save_cali_data_to_file_5g
```

`alg_eqment_config_param_output_entry` is **not called directly** — it is a function pointer in a
dispatch table: `R_ARM_ABS32` to it at `.data+0xa40`. Its user/host entry points are also table
entries: `wal_wlan_cfg_cali_process_entry` @`0x14b6ec` at `.data+0x1d6c`, and
`wal_config_cali_cfg_param_host_entry` @`0xfbac0` at `.rodata+0x8628`/`.rodata+0x862c`. The host
entry parses the message and forwards it with `hmac_config_alg_send_event(0x102, …)`:

```
000fbbac: ldrh r2, [r1, #0xa]
000fbb6c: movw r1, #0x102
000fbb78: bl   hmac_config_alg_send_event
```

So the "who": either the driver itself at interface init, or a userspace/host **equipment ("eqment")
calibration output** command (`0xdc3`/`0xdc4`) routed through the netlink/alg message channel. The
`alg_cfg_process_eqment_check_output` path (`0xdc2`) only pre-renders a text summary (it `memcpy_s`es
a `snprintf_s` buffer); it does not touch the cali buffer.

---

## 5. What this establishes, and what remains open

**Established (all with the excerpts above):**

1. The writers read a heap pointer held in `plat.ko` `.bss+0x120` (5g) / `.bss+0x124` (2g), via the
   accessors `hmac_rf_cali_host_addr` / `_2g` (`.LANCHOR0` @ `.bss+0x120`, `.bss` size `0x4ee4`).
2. That pointer is created and stored **only** by `hwifi_rf_cali_file_load_5g` (`0x2e44`) /
   `_2g` (`0x2c94`): `oal_noncache_alloc(0x22d0|0x920)` → `memset_s` → `kernel_read` of the entire
   `.kv` file → store VA at `.bss+0x120`/`+0x124` and phys addr at `+0x128`/`+0x12c`.
3. There is **no field-by-field store** (`str/strh/strb` with the buffer base + constant offset)
   anywhere in either module; the only stores at the buffer base are the four slot writes in (2).
4. The save routines are invoked (a) once per boot from `hmac_main_init`, and (b) on equipment
   commands `0xdc3`/`0xdc4` from `alg_eqment_config_param_output_entry` (a `.data` dispatch-table
   entry reached from the cali netlink/host handler).

**Consequence for the `.kv` layout.** The on-disk field order is the RAM buffer's order, and the RAM
buffer is a byte-for-byte copy of the file — the driver neither packs nor reorders it. The order was
therefore fixed by whoever first produced `/usr/local/factory/wifi_cali_data*.kv` (factory calibration
host tooling, or firmware writing through the DMA address at `.bss+0x128`/`+0x12c`), **not** by any
function in these two `.ko` files. The `ZZZZ` / `a5a5a5a5` container comes from the `verify_*` stampers
(`wifi.ko` `0xc3c20`/`0xc3d3c`) and the trailer offsets are `0x22cc` / `0x91c`, matching the loader's
own validation.

**Still open (explicit):**

* The per-field semantics of the payload. `kv-format.md` §2.1 already reports only periodicity
  (`wifi.cal` text dump shows live `alg get_*` values, but not this struct).
* Whether the buffer is ever mutated between load and save by the firmware over the `+0x128`/`+0x12c`
  DMA/phys address (the DMA slots are populated by the loader; no ARM code in these two modules reads
  them, so any mutation happens outside this disassembly).
* Which producer wrote the file: not answerable from these two binaries. The next source would be the
  firmware image or the factory/host calibration tool — neither is in these modules.
* The `cali-buffer-populator` premise itself is falsified as stated: there is no in-driver populator
  symbol; the loaders are the fillers, and they are bulk copiers.

---

## Verification commands

* Symbol/global resolution:
  `ELFFile('.../hi5622v100_plat.ko')` → `.symtab`; the `.LANCHOR0` entry with `st_value == 0x120`
  has `st_shndx == 30` (`.bss`, `sh_size 0x4ee4`).
* x-ref scan: iterate every `.rel.*` with `sh_info == .text`, filter `sym.name == '.LANCHOR0' and
  sym['st_value'] == 0x120`; map `r_offset` to the containing `STT_FUNC`.
* Fill scan: per-`STT_FUNC` linear capstone A32 disassembly with register taint
  (`movw/movt`→anchor, `mov/add/sub` propagate, `ldr rX,[anchor(+4)]`→buffer), reporting store
  mnemonics in `{str,strh,strb,strd}` whose base register is buffer-tainted. Zero buffer hits in
  `wifi.ko`; only the four loader slot writes in `plat.ko`.
* Call-site scans: all `.rel.*` relocations to `oal_noncache_alloc`, `kernel_read`,
  `hmac_rf_cali_host_addr(_2g)`, `alg_eqment_config_param_output_entry`; plus `R_ARM_ABS32` scan of
  `.data`/`.rodata` for the dispatch-table pointers.
