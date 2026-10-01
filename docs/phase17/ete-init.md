# ete-init: the vendor ETE engine's initialisation, reproduced on an unowned endpoint (phase 17, 2026-10-01)

Task `st_01a0f737`. This document has five parts and the test-boot record:

- **Part A** - the ETE **initialisation** recovered from `hi5622v100_plat.ko` (full `.symtab`,
  `build/register-dumps/teardown/hi5622v100_plat.ko`, md5 `23660bc285393e678d5cade1c36c194b`):
  the resource chain, the per-channel ring allocation, the SR/DR program registers, the doorbell and
  the ownership/phase bits. Every field is a disassembly quotation or an ELF data/relocation
  quotation, marked **[proven]** or **[inferred]**.
- **Part B** - `lab/eteinit/eteinit.c`: the proven initialisation performed on the claimed endpoint,
  every write logged with its read-back.
- **Part C** - the test boot (vendor hidden, LAN up, Wi-Fi down by design) and the write log.
- **Part D** - the exact stop point: why no descriptor was filled and no doorbell was rung.
- **Part E** - risk notes, and the recovery command recorded before the reboot, with its evidence.

**Headline correction to `ete-engine.md` A.9.** The earlier report concluded the ETE resource is a
runtime table that "does not exist when the vendor stack is hidden". That is wrong. The resource is
a **static `.data` object** (`shuangta_get_ete_priv_res` returns `.data+0x2944`, whose word 0 is the
ETE register-block CA **`0x4003a000`**; its function-pointer slots are filled by `.rel.data`
relocations). What is genuinely runtime is only the host-CA -> device-VA **inbound window** used to
translate buffer addresses. Part D stops there.

---

## Part A - the initialisation

### A.0 Method

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko
$PY lab/ko_disasm.py "$KO" <func> ...
```

Raw function dumps: `build/tmp/init_path.txt`, `build/tmp/submit.txt`, `build/tmp/nodeinit.txt`,
`build/tmp/addr.txt`, `build/tmp/platget.txt` (reproducible from the two commands above and from
`build/register-dumps/ete/dump_ete_engine.txt` / `dump_bal_hcc_msg.txt`).

### A.1 `get_pcie_ete_res` - the resource chain is static **[proven]**

`get_pcie_ete_res` @ `0x1aa7c` indexes a pointer table in `.bss` and returns the word at `+8`:

```
  0x01aa9c: bl   get_chip_type
  0x01aaa8: ldrb r2, [sp, #3]              ; chip type
  0x01aaac: movw r3, #0 -> .LANCHOR0       ; .bss+0x4c54  (pointer table)
  0x01aab4: ldr  r3, [r3, r2, lsl #2]      ; table[chip_type]
  0x01aab8: ldr  r0, [r3, #8]              ; table[..]->[+8] = ETE resource
```

The table is filled by `plat_res_init` -> `shuangta_get_plat_res` -> `set_plat_res(0, res)`, all
inside the module:

```
plat_res_init @0x1b670:
  0x01b674: bl   shuangta_get_plat_res
  0x01b67c: mov  r1, r0
  0x01b680: mov  r0, #0
  0x01b684: b    set_plat_res              ; set_plat_res(0, res)

set_plat_res @0x1a9cc:
  0x01a9d0: bne  ...                       ; chip_type != 0 -> printk + -1
  0x01a9d4: movw r3, #0 -> .LANCHOR0       ; .bss+0x4c54
  0x01a9dc: str  r1, [r3]                  ; table[0] = res

shuangta_get_plat_res @0x1abfc:
  0x01ac00: movw r4, #0 -> .LANCHOR0       ; a 12-byte .bss struct
  0x01ac08: bl   shuangta_get_soc_priv_res
  0x01ac0c: str  r0, [r4]                  ; +0 = soc  res
  0x01ac10: bl   shuangta_get_pcie_priv_res
  0x01ac14: str  r0, [r4, #4]              ; +4 = pcie res
  0x01ac18: bl   shuangta_get_ete_priv_res
  0x01ac1c: mov  r3, r0
  0x01ac24: str  r3, [r4, #8]              ; +8 = ete  res
  0x01ac28: pop  {r4, pc}                  ; returns r0 = r4

shuangta_get_ete_priv_res @0x17a84:
  0x017a84: movw r0, #0 -> .LANCHOR1       ; .data+0x2944
  0x017a8c: bx   lr
```

`.data+0x2944` is a fully-initialised static object; the zeros in the raw image are filled by
`.rel.data` relocations (resolved via `build/tmp/reldata.py`):

| off | value | source |
|---|---|---|
| `+0x00` | `0x4003a000` | **ETE register-block device CA** (literal) |
| `+0x04` | `0x40039508` | second CA (literal) |
| `+0x08` | `shuangta_ete_sr_node_init_handle` | `R_ARM_ABS32` -> `.text+0x17728` |
| `+0x0c` | `shuangta_ete_dr_node_init_handle` | `.text+0x17970` |
| `+0x10` | `shuangta_ete_sr_dscr_fill` | `.text+0x17858` |
| `+0x14` | `shuangta_ete_dr_dscr_fill` | `.text+0x1765c` |
| `+0x18` | `shuangta_ete_sr_get_nodesize` | `.text+0x17654` |
| `+0x1c`..`+0x40` | the 11 `shuangta_ete_*_get/set_*_dscr_*` accessors | `.rel.data` 0x2960..0x2984 |

**[proven]** The ETE register block sits at device CA `0x4003a000`. BAR0 is `0x40000000` on this
endpoint (config-space read, `020_testboot_evidence.txt`), so it is `BAR0+0x3a000`. This is the block
`shuangta_read_soc_to_file` dumps as window 7 `0x4003a000..0x4003ac34`
(`docs/phase6/register-windows.md` 1.2).

### A.2 `pcie_ete_init` - context, arrays, order **[proven]**

`pcie_ete_init` @ `0x7820` (chip = r0, chip-context = r1):

```
  0x007838: mov  r2, #0x88
  0x007844: bl   kmem_cache_alloc_trace      ; ctx = 0x88 bytes, memset_s 0x88
  0x007860: str  r7, [r4, #0x84]             ; ctx+0x84 = chip context
  0x007868: str  r5, [r4, #0x80]             ; ctx+0x80 = chip handle
  0x00786c: bl   get_pcie_ete_res
  0x007878: str  r0, [r4, #0x18]             ; ctx+0x18 = ETE resource
  0x007884: mov  r2, #0x33c
  0x00788c: bl   kmem_cache_alloc_trace      ; SR array = 0x33c (3 x 0x114)
  0x00789c: mov  r2, #0x21c
  0x0078a4: bl   kmem_cache_alloc_trace      ; DR array = 0x21c
  0x0078b8: bl   pcie_ete_intr_init
  0x0078d4: bl   pcie_ete_rings_init         ; (chip, ctx, SR, DR)
  0x0078e8: bl   pcie_ete_chn_res
```

`pcie_ete_rings_init` @ `0x7680` (chip, ctx, SR, DR):

```
  0x0076b8: bl   memset_s                    ; SR array 0
  0x0076cc: bl   memset_s                    ; DR array 0
  0x0076d0: str  r5, [r4, #0x10]             ; ctx+0x10 = DR array
  0x0076d4: str  r6, [r4, #0x14]             ; ctx+0x14 = SR array
  0x0076dc: mov  r2, #3
  0x0076e0: strd r2, r3, [r4, #4]            ; ctx+4=3 (SR count), ctx+8=4 (DR count)
  0x0076e8: ldr  r1, [r4, #0x18]             ; ETE res
  0x0076f0: ldr  r1, [r1]                    ; res[0] = CA 0x4003a000
  0x0076fc: bl   oal_pcie_inbound_ca_to_va   ; CA -> host VA (v7)
  0x007714: bl   pcie_ete_init_src_ring      ; (ctx, v7)  channels 0..2
  0x007728: bl   pcie_ete_init_dst_ring      ; (ctx, v7)  channels 3..6
```

**[proven]** Three SR channels (0-2, instance stride `0x114`) and four DR channels (3-6, stride
`0x6c`); both ring families are programmed through the **same** mapped block.

### A.3 The channel-config table - offsets and depth **[proven]**

`pcie_ete_init_src_ring` @ `0x7068` iterates `r4 = 0..2`; `pcie_ete_init_dst_ring` @ `0x727c`
iterates `r8 = 3..6`. Both call `pcie_ete_get_chn_cfg(index)`:

```
pcie_ete_get_chn_cfg @0x15f00:
  0x015f00: cmp  r0, #6
  0x015f04: movls r2, #0xc
  0x015f08: ldrls r3, [pc, #8]              ; literal -> .rodata+0x101c  (R_ARM_ABS32)
  0x015f0c: mlals r0, r2, r0, r3            ; entry = 0x101c + 0xc * index
```

`.rodata+0x101c` holds seven 12-byte entries `{u32 block_offset, u8 depth, u8 queue, ...}`:

| idx | block off | depth byte | queue byte | fam |
|---|---|---|---|---|
| 0 | `0x400` | `0x20` (32) | 0 | SR |
| 1 | `0x450` | `0x20` | 0 | SR |
| 2 | `0x4a0` | `0x20` | 0 | SR |
| 3 | `0x590` | `0x20` | 0 | DR |
| 4 | `0x5e0` | `0x20` | 0 | DR |
| 5 | `0x630` | `0x20` | 0 | DR |
| 6 | `0x680` | `0x20` | 0 | DR |

**[proven]** Ring depth is **32** for every channel; the queue-select field is 0.

### A.4 Ring allocation - coherent DMA, sizes **[proven]**

`pcie_ete_init_src_ring` @ `0x7068` (SR):

```
  0x0070cc: bl   pcie_ete_get_chn_cfg        ; r8 = cfg
  0x0070dc: mul  sb, r3=#0x114, r4           ; instance stride 0x114
  0x007108: ldrb r2, [r8, #4]                ; depth = 0x20
  0x007114: add  r6, r2, #2                  ; SR node count = depth + 2 = 34
  0x00711c: ldr  r3, [r3, #8]                ; res+0x08 = sr_node_init_handle
  0x007124: blx  r3                          ; (handle, sr_inst, 34)
  0x007144: str  sl, [r2, #0xec]             ; inst+0xec = block + 0xc18 + 4*i
  0x007150: ldr  r0, [r8]                    ; cfg[0] = block offset
  0x007154: add  r0, r1, r0                  ; block + cfg[0]
  0x007160: str  r0, [r1, #0xdc]             ; inst+0xdc = SR register block VA
  0x00716c: strb r1, [r3, #2]                ; node[i].byte2 = channel index
  0x007178: bl   pcie_ete_sr_init
```

`shuangta_ete_sr_node_init_handle` @ `0x17728` (r0 = handle, r1 = inst, r2 = count):

```
  0x017758: lsl  r6, r2, #3                  ; bytes = count * 8
  0x017794: bl   dma_alloc_attrs             ; (handle, count*8, &dma, 0xa20, 0)
  0x0177a8: str  r3, [r7, #0xe8]             ; inst+0xe8 = DMA (device) address
  0x0177b4: bl   memset_s                    ; zero the node array
  0x0177bc: str  r5, [r7]                    ; inst+0 = host VA
```

`shuangta_ete_dr_node_init_handle` @ `0x17970` is the same but stores the DMA address at
`[inst+0x54]` and the VA at `[inst+0x10]`. In `pcie_ete_init_dst_ring` @ `0x727c` the DR count is
`depth` (32), **not** `depth+2`:

```
  0x007320: ldrb r2, [fp, #4]                ; depth = 32
  0x007328: ldr  r3, [r3, #0xc]              ; res+0x0c = dr_node_init_handle
  0x007354: str  sl, [r2, #0x5c]             ; inst+0x5c = block + 0xc00 + 4*i
  0x007370: str  r0, [r1, #0x50]             ; inst+0x50 = DR register block VA
  0x007388: bl   pcie_ete_dr_init
```

**[proven]** Node arrays are coherent DMA buffers of 8-byte nodes: SR = `(32+2)*8 = 272` bytes per
channel, DR = `32*8 = 256` bytes per channel. `pcie_ete_sr_init` sets `inst+0x14 = node_array +
nodesize*depth` (the array end) via `res+0x18 = shuangta_ete_sr_get_nodesize` (= 8).

### A.5 The SR/DR program registers - offsets, values, order **[proven]**

`pcie_ete_sr_reg_init` @ `0x14a48` (r4 = SR channel instance, `r5 = [r4+0xdc]` = SR block VA):

```
  0x014a98: ldr  r1, [r6, #0x84]             ; chip context
  0x014aa0: ldr  r2, [r4, #0xe8]             ; ring DMA address
  0x014aac: bl   pcie_hostca_to_devva        ; host CA -> device VA
  0x014ab0: str  r0, [r5, #0x10]             ; SR+0x10 = ring base (device VA)
  0x014ac0: ldrb r3, [cfg, #4]               ; depth = 0x20
  0x014ac8: sub  r3, r3, #1                  ; depth-1 = 0x1f
  0x014acc: bfi  r1, r3, #0, #0xa            ; SR+0x14[9:0] = depth-1
  0x014ad0: str  r1, [r2, #0x14]
  0x014ad8: ldr  r2, [r4, #0xc]              ; write pointer (index|phase)
  0x014adc: str  r2, [r3, #0x18]             ; SR+0x18 = write pointer
  0x014aec: ldrb r1, [cfg, #5]               ; queue select = 0
  0x014af0: bfi  r2, r1, #0, #3              ; SR+0x08[2:0] = queue
  0x014af4: str  r2, [r3, #8]                ; SR+0x08 ctrl
```

`pcie_ete_dr_reg_init` @ `0x1483c` (r4 = DR instance, `r6 = [r1+0x50]` = DR block VA):

```
  0x014884: bl   pcie_hostca_to_devva
  0x014888: str  r0, [r6, #0x30]             ; DR+0x30 = ring base (device VA)
  0x014898: ldrb r3, [cfg, #4] ; sub #1
  0x0148a4: bfi  r1, r3, #0, #0xa            ; DR+0x34[9:0] = depth-1
  0x0148b4: str  r2, [r3, #0x38]             ; DR+0x38 = pointer ([inst+0x1c])
```

**Write order is base, depth-1, pointer, ctrl.** Per-channel absolute CAs (block CA + offset +
register offset), all inside `BAR0+0x3a000..`:

| channel | base | ctrl | depth-1 | pointer | chn_res (+0x2e8) |
|---|---|---|---|---|---|
| SR0 | `0x4003a410` | `0x4003a408` | `0x4003a414` | `0x4003a418` | `0x4003a6e8` |
| SR1 | `0x4003a460` | `0x4003a458` | `0x4003a464` | `0x4003a468` | `0x4003a738` |
| SR2 | `0x4003a4b0` | `0x4003a4a8` | `0x4003a4b4` | `0x4003a4b8` | `0x4003a788` |
| DR3 | `0x4003a5c0` | - | `0x4003a5c4` | `0x4003a5c8` | `0x4003a878` |
| DR4 | `0x4003a610` | - | `0x4003a614` | `0x4003a618` | `0x4003a8c8` |
| DR5 | `0x4003a660` | - | `0x4003a664` | `0x4003a668` | `0x4003a918` |
| DR6 | `0x4003a6b0` | - | `0x4003a6b4` | `0x4003a6b8` | `0x4003a968` |

`pcie_ete_chn_res` @ `0x7490` (after the rings are up):

```
  0x0074fc: ldr  r8, [r3, #0x2e8]
  0x007500: dsb  sy
  0x007504: and  r8, r8, sb                  ; sb = 0xfffffc20
  0x007508: dsb  st
  0x00750c: bl   arm_heavy_mb
  0x007520: str  r8, [r2, #0x2e8]
```

**[proven]** Per-channel `+0x2e8` is a read-modify-write: read, AND `0xfffffc20`, write back.

### A.6 The descriptor node and the doorbell **[proven]**

Node = 8 bytes (A.5 of `ete-engine.md`): `word0` = buffer address, `word1` =
`(len << 16) | flags`, flags `0xd2b` = host-filled, bits 13/14 = owner, length in `word1[31:16]`.
`shuangta_ete_sr_dscr_fill` @ `0x17858` writes it, advances the pointer with
`pcie_ete_ring_ptr_plus` @ `0x13ef8` (index `[9:0]` + phase `[10]`, phase toggles on wrap) and
submits with `pcie_msg_send(chip, 3)`:

```
  0x0178a0: orr  r2, r2, #0x4000            ; word1 bit14 = owner/valid
  0x0178ac: orr  r2, r2, #0x2000            ; word1 bit13 = owner/valid
  0x0178b8: bfi  r2, r1=#0xd2b, #0, #0xd
  0x0178cc: str  r1(addr), [r3, r2, lsl #3] ; node[i].word0 = buffer address
  0x0178e0: str  r2, [r3, #4]               ; node[i].word1 = len<<16 | 0x6d2b
  0x0178ec: bl   pcie_ete_ring_ptr_plus
  0x0178f0: ldr  r0, [r5, #0x80]            ; chip handle
  0x0178f4: mov  r1, #3
  0x0178f8: bl   pcie_msg_send              ; doorbell id 3
```

`pcie_msg_send` @ `0x160f4` (chip, id): under a spinlock it ORs bit `id` into a pending mask and,
when the mask was empty, writes the mask to message CA **`0x40039010`** and ORs **bit 0** into
CA **`0x400392d4`** (`[r6+0x2c]` / `[r6+0x34]` are the mapped `shuangta_pcie_msg_reg_map` slots): 

```
  0x016184: orr  r3, r3, r1, lsl r4          ; pending |= 1<<id
  0x01619c: str  r3, [r2]                    ; [r6+0x2c] = 0x40039010 <= mask
  0x0161a4: ldr  r2, [r6, #0x34]             ; 0x400392d4
  0x0161a8: ldr  r3, [r2]
  0x0161ac: orr  r3, r3, r1=#1
  0x0161b0: str  r3, [r2]                    ; doorbell: OR bit 0
```

The six message CAs are the literals inside `shuangta_pcie_msg_reg_map` @ `0x1b1a0`
(`0x40039010, 0x40039014, 0x400392d4, 0x40101438, 0x40101414, 0x400392f0`). Completion is the
device's interrupt -> `pcie_msg_handle` @ `0x171f8` -> `pcie_ete_transfer_done_handle`
(registered for id 3 by `pcie_msg_init` @ `0xb6e4`).

### A.7 Proven vs inferred

| field | value | status |
|---|---|---|
| ETE register-block CA | `0x4003a000` | **proven** (static `.data+0x2944`, `.rel.data` vtable) |
| resource object | static `.data+0x2944`, 16+ function pointers | **proven** |
| SR channels / stride | 3, `0x114` | **proven** |
| DR channels / stride | 4, `0x6c` | **proven** |
| ring depth / setup order | 32; base, depth-1, pointer, ctrl | **proven** (`.rodata+0x101c`) |
| node size | 8 bytes | **proven** |
| ring sizes | SR `(32+2)*8`, DR `32*8` | **proven** |
| node flags / owner bits | `0xd2b`, bits 13/14 | **proven** |
| trigger / doorbell | `pcie_msg_send(chip,3)`: mask->`0x40039010`, OR1->`0x400392d4` | **proven** |
| pointer encoding | `index[9:0] \| phase[10]` | **proven** |
| **ring base value = buffer device VA** | needs `pcie_hostca_to_devva` through the runtime inbound window `chip->[4]->[0xc4]` (`pcie_hostca_to_devva` @ `0xaefc`: `devva = [win] + hostca - [win+0x10]`) | **inferred** on our endpoint |
| **descriptor's transfer target** | node word0 is the device VA of a **host** buffer (`pcie_get_ete_addr` -> `oal_dma_map_single` -> `pcie_hostca_to_devva`); there is no chip address in the node | **proven** (which is why Part D stops) |

---

## Part B - what our module performed

`lab/eteinit/eteinit.c` (`lab/eteinit/Makefile`: `obj-m := eteinit.o`). Built by the existing GitHub
Actions workflow (`.github/workflows/build-load-test-module.yml`, step "build eteinit module",
artifact `eteinit-ko`). CI run `36856067872` (commit `4103e32`); `eteinit.ko` md5
`844a82ca46c2a8e0309f31e6999c8cb5`, `vermagic=5.10.201`.

On the claimed endpoint it performs, with every write read back:

1. **Claim** exactly as `epinit` stage 1: `pci_enable_device`, `pci_request_mem_regions` (refuses if
   the vendor stack holds the regions), `pci_write_config_word(dev, 4, 7)`, BAR0 read.
2. **Ring node arrays** - `dma_alloc_coherent` per channel with the vendor's sizes (272 B x3 SR,
   256 B x4 DR), zeroed (A.4).
3. **The proven register writes** at the A.5 offsets, in the vendor's order, plus the `+0x2e8`
   read-modify-write (A.5). The ring base is the coherent `dma_addr_t`.
4. **Read-only** pre-state of every channel register and post-state of the message registers.
5. **Stop** (Part D): no descriptor, no doorbell.

Module parameters: `domain` (default 0), `wregs` (default 1; 0 = read-only), `submit`
(refused; the parameter documents the boundary).

---

## Part C - test boot and write log

Artifacts in `build/register-dumps/eteinit/` (gitignored): `000_baseline.txt`, `010_staging.txt`,
`020_testboot_evidence.txt`, `030_recovery_run.txt`, `040_recovery_evidence.txt`, plus `stage/`
(the module, the loader and the recovery script). The takeover mechanism is exactly phase 16's:
rename the two vendor `.ko` files, load `eteinit.ko` from a self-deleting `S99` init script.

### C.1 Staging - `010_staging.txt` (before the reboot)

    eteinit.ko md5 844a82ca46c2a8e0309f31e6999c8cb5  (== CI artifact)
    hi5622v100_wifi.ko.omo-off  e21629d226ec7de9a860a8955952d311  (== baseline)
    hi5622v100_plat.ko.omo-off  23660bc285393e678d5cade1c36c194b  (== baseline)
    /etc/rc.d/S99omo-eteinit -> ../init.d/omo-eteinit
    sh -n on loader + recovery script: OK

### C.2 Test boot - `020_testboot_evidence.txt`

(a) vendor stack absent, ours present, endpoints unbound:

    [vendor]: hi5622v100_wifi + hi5622v100_plat absent (vendor stack: 0); hi_pcie loaded
    eteinit 16384 0
    0000:00:00.0 driver: No such file or directory   (enable=1, ours)
    0001:00:00.0 driver: No such file or directory   (untouched)

(b) claim + ring arrays (all seven `dma_alloc_coherent` succeeded; note `0000:00:00.0` is the
    2.4 GHz endpoint):

    38.582 omo-eteinit: pci_enable_device rc=0 command 0x0140 -> 0x0142
    38.589 omo-eteinit: pci_request_mem_regions rc=0 (MEM BARs claimed)
    38.596 omo-eteinit: cfg[0x004] <= 0x0007 readback=0x0006 MEM|MASTER=set
    38.603 omo-eteinit: BAR0 base=0x40000000
    38.620 omo-eteinit: SR ch0 nodes=272B dma=0x83f7c000 (zeroed)
    38.627 omo-eteinit: SR ch1 nodes=272B dma=0x83d10000
    38.635 omo-eteinit: SR ch2 nodes=272B dma=0x83db3000
    38.643 omo-eteinit: DR ch3 nodes=256B dma=0x8363a000
    38.650 omo-eteinit: DR ch4 nodes=256B dma=0x83ea0000
    38.657 omo-eteinit: DR ch5 nodes=256B dma=0x83ea1000
    38.665 omo-eteinit: DR ch6 nodes=256B dma=0x83e82000

(c) **the real SR/DR program registers, at the correct offsets, all read 0x00000000** - this is the
    first measurement against block `0x4003a000` rather than the message block `0x40039000`:

    38.713 omo-eteinit: ---- SR/DR program registers BEFORE (read-only) ----
    38.720   SR ch0 ctrl   [0x408] = 0x00000000
    38.727   SR ch0 base   [0x410] = 0x00000000
    38.734   SR ch0 depth-1[0x414] = 0x00000000
    38.740   SR ch0 wptr   [0x418] = 0x00000000
    ... (SR ch1 0x458/0x460/0x464/0x468, SR ch2 0x4a8/0x4b0/0x4b4/0x4b8)
    38.800   DR ch3 base   [0x5c0] = 0x00000000   ... depth 0x5c4, wptr 0x5c8
    ... (DR ch4 0x610/0x614/0x618, DR ch5 0x660/0x664/0x668, DR ch6 0x6b0/0x6b4/0x6b8)

    The engine is genuinely uninitialised in the unowned boot. (`ete-engine.md` C.3 read
    `BAR0+0x39010`, which is message register 0, not SR ch0's `+0x410`; the conclusion held, the
    evidence was at the wrong offset.)

(d) **the proven writes, every read-back matching** (excerpt; full log in the artifact):

    38.885   SR ch0 base    [0x410] <= 0x83f7c000 readback=0x83f7c000 match=YES
    38.894   SR ch0 depth-1 [0x414] <= 0x0000001f readback=0x0000001f match=YES
    38.903   SR ch0 wptr    [0x418] <= 0x00000000 readback=0x00000000 match=YES
    38.913   SR ch0 ctrl    [0x408] <= 0x00000000 readback=0x00000000 match=YES
    ... (SR ch1, SR ch2 identical shape)
    38.996   DR ch3 base    [0x5c0] <= 0x8363a000 readback=0x8363a000 match=YES
    39.006   DR ch3 depth-1 [0x5c4] <= 0x0000001f readback=0x0000001f match=YES
    39.015   DR ch3 wptr    [0x5c8] <= 0x00000000 readback=0x00000000 match=YES
    ... (DR ch4..ch6 identical shape)
    39.114   SR ch0 chn_res  [0x6e8] <= 0x00000000 readback=0x00000000 match=YES
    ... (all seven chn_res RMW, mask 0xfffffc20, match=YES)

    Post-state (the read-back that matters):

    39.185   SR ch0 ctrl=0x00000000 base=0x83f7c000 depth=0x0000001f wptr=0x00000000
    39.194   SR ch1 ctrl=0x00000000 base=0x83d10000 depth=0x0000001f wptr=0x00000000
    39.203   SR ch2 ctrl=0x00000000 base=0x83db3000 depth=0x0000001f wptr=0x00000000
    39.212   DR ch3 base=0x8363a000 depth=0x0000001f wptr=0x00000000
    39.219   DR ch4 base=0x83ea0000 depth=0x0000001f wptr=0x00000000
    39.227   DR ch5 base=0x83ea1000 depth=0x0000001f wptr=0x00000000
    39.235   DR ch6 base=0x83e82000 depth=0x0000001f wptr=0x00000000

(e) message registers, all zero: `0x40039010=0`, `0x40039014=0`, `0x400392d4=0`, `0x400392f0=0`.

(f) reachability / stability: `br-lan 192.168.10.1/24`, **0 wlan interfaces** (Wi-Fi down by
    design), `S99` symlink self-deleted, no `pstore` record added (blk mtimes unchanged
    10:41/10:26/10:34), PC ping to the router 0% loss, no panic.

**One logging defect, stated plainly.** In `omo_ete_init_block` the three `omo_log_words` calls
pass `win` (not `win + 0x2c0` / `win + 0x840`), so the rows labelled `ETE block +0x2c0` and
`+0x840` re-read the block base. Only the `+0x000 = 0x0000010a` row is valid; the `+0x840` content
is the phase-17 read (`00002800 00002800 00028000 ...`). The channel-register rows were printed by
`omo_rd` with explicit `win + off` and are correct.

---

## Part D - the exact stop point

**Format and ring setup are unambiguous. The transfer operation is not, so no descriptor was filled
and no doorbell was rung.**

The node's `word0` is a **host** buffer's device address. The only producer,
`pcie_ete_sending_trigger` @ `0x13f90`, allocates an skb, copies the payload, and converts the buffer
with the chain `pcie_get_ete_addr` @ `0x6c2c` -> `oal_dma_map_single` -> `pcie_hostca_to_devva`
@ `0xaefc`:

```
  0x014184: bl   pcie_get_ete_addr           ; (chip, skb head, len, dir=2)
  0x014190: bl   memmap_get_dev_acp_addr     ; host buffer -> device (ACP) VA
  0x0141d4: ldr  r3, [r5, #0x18]             ; ETE res
  0x0141e0: ldr  r3, [r3, #0x14]             ; dr_dscr_fill
  0x0141e4: blx  r3                          ; node.word0 = device VA of the host buffer
```

There is therefore **no chip address anywhere in a bare SR/DR node** - it tells the engine where to
read/write *host* memory, and the actual destination inside the chip is determined by the channel's
send path plus the HCC/BAL message header that `pcie_ete_sending_trigger` builds. The brief's benign
operation ("a memory write of a known 64-byte pattern to a scratch location we choose inside the
chip's writable window") cannot be expressed with the recovered node. Reproducing
`pcie_ete_sending_trigger` would mean reconstructing the vendor's skb/message format, its
`pcie_msg_init` handler table and its IRQ plumbing - none of which this endpoint has.

Two further reasons the doorbell was left alone:

- The ring-base value written in C.2(d) is the coherent `dma_addr_t`; the vendor passes that same
  handle through `pcie_hostca_to_devva`, whose window (`chip->[4]->[0xc4]`, read at
  `[win] + hostca - [win+0x10]`) is created at probe time and is not a file constant. So the base is
  the one **inferred** value in the write set.
- `pcie_msg_send`'s doorbell is a read-modify-write OR of bit 0 into PCIe-glue CA `0x400392d4`. On a
  chip whose firmware stack is not loaded, that write's effect is unproven.

Every descriptor node was left zeroed, so even if the engine prefetched a node it would never see
the `0x6d2b` host-filled magic. The module prints this stop (`submit=1` prints the refusal too).

---

## Part E - risk notes and recovery

### E.1 Which writes could disturb the radio, and what was done

- **Config-space `cmd` write (`0x04 = 7`)** - proven safe in phase 16 (reads back `0x0006`;
  MEM|MASTER present). Taken.
- **BAR0 `+0x3a000` SR/DR register writes** - the engine was proven idle first (all seven register
  sets read 0), every write's read-back matched, no doorbell was rung and every node was zeroed.
  The vendor performs exactly these writes on every boot; the class of write that soft-locked the
  bus in phase 16 was a bulk MMIO `memcpy` into `BAR0+0x40000..`, not a config-register write.
  Taken, with the ring base marked inferred.
- **No writes outside the documented windows** - the module touches only config space and
  `BAR0+0x3a000..0x3b000` (mapped read/write) and `BAR0+0x39000..0x3a000` (read-only). It never
  touches `BAR0+0x40000..`, BAR2/BAR4, or the `0x2c0`/`0x840` sub-blocks that phase 13 flagged as
  moving.
- **Doorbell / descriptor** - not taken (Part D).

### E.2 Recovery command - installed on the device BEFORE the test reboot

`/root/recover-eteinit.sh` (staged at `build/register-dumps/eteinit/stage/recover-eteinit.sh`),
run as `sh /root/recover-eteinit.sh`:

```sh
#!/bin/sh
set -x
cd /lib/modules/5.10.201 || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-eteinit
rm -f /etc/init.d/omo-eteinit
rm -f /lib/modules/5.10.201/eteinit.ko
sync
reboot
```

The one-shot loader deletes its own rc.d symlink before `insmod`, so a hang resets into a reachable
boot with the vendor modules still hidden but no `eteinit`. Fallback if the box does not come back:
U-Boot slot A (stock).

### E.3 Recovery evidence - `030_recovery_run.txt`, `040_recovery_evidence.txt`

    sh /root/recover-eteinit.sh: renamed both modules back, removed loader/module/symlink, sync, reboot
    hi5622v100_plat       323584  3 hi5622v100_wifi      (baseline use counts)
    hi5622v100_wifi      3387392  1
    md5 hi5622v100_wifi.ko e21629d226ec7de9a860a8955952d311  (baseline)
    md5 hi5622v100_plat.ko 23660bc285393e678d5cade1c36c194b  (baseline)
    leftovers: eteinit.ko / *.omo-off / S99omo-eteinit / init.d/omo-eteinit all absent
    0000:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
    0001:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
    Wiphy phy0 + phy1; 6 wlan interfaces (vap0 Cudy-1C73, vap8 Cudy-1C73-5G, ...); /tmp/wifi_done
    softapd + hostapd running; /var/log/hi5622v100.log: "calibration init done."
    br-lan 192.168.10.1/24 up; PC ping: 3/3, 0% loss
    pstore: no new record (blk mtimes still 10:41/10:26/10:34)

`dmesg | grep -icE 'panic|soft lockup|oops'` returns 2 only because it matches the boot-time
`pstore_zone: ... kmsg(Oops,panic_write)` registration strings; there is no panic or lockup in
either boot. Only `/root/recover-eteinit.sh` is left on the device (the recovery command itself),
matching the phase-17 `eteprobe` run.

**Acceptance state reached: the router is healthy with the vendor stack restored, both radios up, no
module/loader leftovers, no new pstore record.**

---

## Limits, stated plainly

- **No descriptor was submitted.** The node format, the seven channel register offsets, the depth,
  the doorbell and the ownership/phase bits are all proven, but the node's address is a *host*-buffer
  device VA and the chip-side destination lives in the HCC/BAL message layer that is not present;
  the brief's "write to a chip scratch location" is not expressible. Part D documents the stop.
- **The ring-base value is inferred.** It is the coherent `dma_addr_t`; the vendor applies
  `pcie_hostca_to_devva` through a runtime inbound window. Recovering that window (its base/limit
  live in `chip->[4]->[0xc4]`, set up at probe) is the concrete next step before any descriptor work.
- **One test boot, Wi-Fi intentionally down.** During the test boot there is no `hi5622v100_wifi`,
  no `hi5622v100_plat`, no `rox_pci0`, no Wiphy; the LAN/SSH path never dropped.
- **One endpoint touched.** `domain=0` (`0000:00:00.0`); `0001:00:00.0` is left unbound.
