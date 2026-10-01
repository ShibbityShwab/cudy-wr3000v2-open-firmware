# fw-download: the vendor's firmware transfer, and our first complete open path from image to silicon (phase 17, 2026-10-01)

Task `st_01a0f746`. This document has four parts and the test-boot record:

- **Part A** - the exact message/transfer structure that carries the firmware chunks, recovered
  from `hi5622v100_plat.ko` (full `.symtab`, `build/register-dumps/teardown/hi5622v100_plat.ko`,
  md5 `23660bc285393e678d5cade1c36c194b`). Every field is a disassembly or ELF-data quotation,
  marked **[proven]** (a constant/relocation in the instruction stream) or **[inferred]**
  (control flow / table semantics / measured hardware behaviour, not a literal).
- **Part B** - `lab/fwload/fwload.c`: claim the endpoint, initialise the ETE engine as `eteinit`
  does, stream `FIRMWARE.bin` through the recovered path in chunks, then read BAR0 back and
  compare with the file.
- **Test record** - the staging, the test boot, the recovery boot.
- **Part C** - the outcome, what works and what does not, and the risk notes for the writes taken.

**Headline correction to `endpoint-init.md` A.9 and `ete-engine.md` A.10.** Those reports
concluded the firmware transfer runs through the BAL/HCC/ETE DMA + message engine (the ETE SR
rings and the `pcie_msg_send(chip,3)` doorbell). It does not. `firmware_file_send`'s `bal_write`
is a **bus-abstraction dispatch to `pcie_write`**, which is a `memcpy_s` into the host VA of the
device CA - a plain chunked memory write. There is **no ETE descriptor, no SR channel and no
doorbell in the firmware data path**. The ETE rings and the doorbell belong to the HCC *message*
path, which `bal_write` never enters. The complete evidence is below.

---

## Part A - the transfer structure

### A.0 Method

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko
$PY lab/ko_disasm.py "$KO" <func> ...        # relocations named at every bl/ldr/movw
```

Raw dumps: `build/tmp/fwload/{bal,hcc,ete,msg,fw,poweron,pcie_wr,translate,inbound}.txt`.
Data/region dumpers: `build/tmp/fwload/structdump.py`, `build/tmp/fwload/xref.py`.

### A.1 The call chain **[proven]** (all `bl` relocations)

```
wlan_power_on                      @0xe46c
  0x00e494: bl -> wlan_bal_init_process
  0x00e4b0: bl -> hwifi_rf_cali_file_load
  0x00e4d0: bl -> firmware_download_function      ; the download

firmware_download_function         @0xf9f8
  0x00fab0: bl -> printk                           ; .LC24 "enter firmware_download_function"
  0x00fab8: bl -> firmware_download
  0x00fb1c: bl -> printk                           ; .LC26 "firmware_download success"

firmware_download                  @0xf834
  0x00f860: bl -> firmware_check_version
  0x00f8dc: bl -> firmware_mem_try_alloc          ; (0x80000, 0x1000, &out)
  0x00f930: bl -> firmware_file_send              ; (desc, i, buf, out)

firmware_file_send                 @0xf56c
  0x00f5e8: bl -> filp_open
  0x00f630: bl -> vfs_llseek                       ; SEEK_END  -> file size
  0x00f64c: bl -> vfs_llseek                       ; SEEK_SET 0
  0x00f6b4: bl -> firmware_kernel_read             ; one chunk
  0x00f724: bl -> bal_write                        ; (chip, target + off, buf, n)

bal_write                          @0x10c04        ->  g_st_pcie_bus_driver[0x38]
pcie_write                         @0xbdc0        ->  pcie_para_check + memcpy_s
```

The prerequisite steps are **no-ops in this module**, which is why the download needs no
power-sequence:

```
dev_power_ctl  @0x107f8: bx lr                          ; [proven] literal stub
pcie_via_gpio_power_cfg @0x107f0: mov r0,#0; bx lr      ; [proven] literal stub
pcie_via_gpio_rst_device @0x107d4: mov r0,#0; bx lr     ; [proven] literal stub
pcie_reinit    @0x7ad4:  mov r0,#0; bx lr               ; [proven] literal stub
```

so `wlan_bal_init_process @0xe308` (`dev_power_ctl(0,1)` + `bal_reinit(0,1)` -> `pcie_reinit`)
does nothing hardware-visible, and `pcie_init_default @0x8128` (`pcie_via_gpio_power_cfg(1)`)
is likewise a no-op. There is no reset/CPU-release in the download path itself. **[proven]**

### A.2 The per-chip descriptor and the file table **[proven]**

`firmware_download_function` @0xf9f8 indexes a per-chip descriptor array by `get_chip_type()`.
For chip type 0 (`type = [sp+0x13]`), with `r5 = .LANCHOR0` and `r4 = .LANCHOR1 =
.data+0x269c`:

```
  0x00fa50: add  r2, r5, r3, lsl #5      ; desc = LANCHOR0 + type*0x20
  0x00fa58: str  r4, [r5, r3, lsl #5]    ; desc+0x00 = chip handle
  0x00fa5c: str  r4, [r2, #4]            ; desc+0x04 = chip handle
  0x00fa68: str  r1=1, [r2, #0xc]        ; desc+0x0c = file count = 1
  0x00fa6c: add  r3, r4, r3, lsl #3      ; file table = LANCHOR1 + type*8
  0x00fa70: str  r6=0, [r2, #8]
  0x00fa74: str  r3, [r2, #0x10]         ; desc+0x10 = file table pointer
  0x00fa78: bl   -> get_firmware_path
  0x00fa80: str  r0, [r4, r7, lsl #3]    ; file table[type].path = path string
  0x00fa90: add  r4, r4, #8 + type*0xc
  0x00fa94: str  r6=0, [r2, #0x1c]
  0x00fa98: str  r4, [r2, #0x18]         ; desc+0x18 = single-shot header descriptor
```

`.LANCHOR1` = `.data+0x269c` (relocation at `0xfa60/0xfa64`), so for chip type 0 the file table
entry is the 8-byte object at `.data+0x269c`:

```
  .data+0x269c: 0x00000000   (path pointer; filled by get_firmware_path -> "/lib/firmware/hi_wifi/FIRMWARE.bin")
  .data+0x26a0: 0x01240000   target device address, chip 0
```

**[proven]** The target is `0x01240000`. It lies inside the region-table entry whose device range
is `0x01200000-0x01417fff` (A.6).

### A.3 `firmware_file_send` - chunking, offsets, sizes **[proven]**

Signature `firmware_file_send(desc=r0, i=r1, buf=r2, buflen=r3)`; `r4=desc`, `r8=i`, `r6=buf`,
`r5=buflen`:

```
  0x00f5d0: ldr  r1, [r2, r1, lsl #3]    ; table[i].path
  0x00f5e4: ldr  r0, [r3, r8, lsl #3]    ; filp_open(table[i].path, O_RDONLY, 0)
  0x00f630: bl   vfs_llseek              ; SEEK_END -> r8 = file size
  0x00f650: cmp  r8, r5 / movlo r5, r8   ; chunk = min(buflen, filesize)
  0x00f680: sub  r0, r8, #1
  0x00f688: add  r0, r0, r5
  0x00f690: bl   __aeabi_uidiv           ; nchunks = ceil(filesize / chunk)
  ...
  0x00f6b4: bl   firmware_kernel_read    ; (filp, &pos, buf, chunk) -> bytes read in r0
  0x00f704: ldr  r2, [sp, #8]            ; i*8
  0x00f708: ldr  r3, [r4, #0x10]         ; table
  0x00f718: ldr  r1, [r3, #4]            ; table[i].target_addr
  0x00f71c: ldr  r3, [sp, #0x10]         ; bytes just read
  0x00f720: add  r1, sl, r1              ; target_addr + running offset
  0x00f724: bl   bal_write               ; bal_write(chip, target+off, buf, n)
  0x00f750: ldr  r3, [sp, #0x10]
  0x00f758: add  sl, sl, r3              ; off += n
```

`firmware_download` supplies the buffer and length:

```
  0x00f8d0: mov  r1, #0x1000
  0x00f8d4: mov  r2, sp
  0x00f8d8: mov  r0, #0x80000
  0x00f8dc: bl   firmware_mem_try_alloc   ; (0x80000, 0x1000, &out)
  0x00f8e8: bic  r1, r1, #7               ; out = align_down(out, 8)
  0x00f928: mov  r1, r6
  0x00f930: bl   firmware_file_send       ; (desc, i, buf, out)
```

and `firmware_mem_try_alloc` @0xf76c requests and returns exactly the requested size:

```
  0x00f7ac/0x00f7b8: r4 = max(size, align)         ; max(0x80000, 0x1000) = 0x80000
  0x00f7cc: bl   __kmalloc                          ; (0x80000, GFP)
  0x00f7d8: str  r4, [r5]                           ; *out = 0x80000
```

**Concrete numbers for this image** (`FIRMWARE.bin`, 928,920 bytes):

| chunk | running off | target device CA | bytes |
| --- | --- | --- | --- |
| 0 | 0 | `0x01240000` | `0x80000` = 524,288 |
| 1 | `0x80000` | `0x012C0000` | 404,632 |

There is **no alignment of the target other than the cumulative byte offset**; the bounce buffer
is 8-byte aligned (`bic #7`). The transfer is a raw byte stream at the file's own layout - the
chunks are not headers or messages, they are the file contents. **[proven]**

### A.4 `bal_write` is a bus dispatch, not the ETE path **[proven]**

`bal_write` @0x10c04:

```
  0x010c0c: movw r0, -> .LANCHOR1        ; .LANCHOR1 = .bss+0x47b8 (BAL global state)
  0x010c18: ldr  ip, [r0, #0x14]         ; ip = port-0 bus context = [.bss+0x47b8+0x14]
  0x010c1c: ldr  lr, [ip, #0x3c]         ; presence flag
  0x010c20: cmp  lr, #0
  0x010c24: beq  0x10c3c                 ; -> -ENOSYS (-0x8b2d) if unregistered
  0x010c28: ldr  lr, [ip, #0x38]         ; lr = bus write callback
  0x010c2c: ldr  r0, [r0, #0x18]         ; r0 = BAL handle
  0x010c30: mov  ip, lr
  0x010c38: bx   ip                       ; tail-call callback(handle, dev_addr, buf, len)
```

The port context is set by `bal_init` @0x109e8:

```
  0x0109ec: movw r6, -> .LANCHOR0        ; .LANCHOR0 = .data+0x289c (BAL bus-cbs table)
  0x010a10: movw r8, -> .LANCHOR1        ; .bss+0x47b8
  0x010a30: ldr  r3, [r6, #0x3c]         ; [.data+0x289c+0x3c] = the registered bus driver
  0x010a38: str  r3, [r4, #0x14]         ; port ctx +0x14 = bus driver
  ...
  0x010a58: ldr  r3, [r4, #0x14]
  0x010a5c: ldr  r3, [r3, #8]            ; bus driver +8 = init fn
  0x010a68: blx  r3
  0x010a70: str  r0, [r4, #0x18]         ; port ctx +0x18 = handle
```

and the bus driver is the PCIe one:

```
  .data+0x28d8: RELOC -> g_st_pcie_bus_driver
  g_st_pcie_bus_driver  @.data+0x2900:
    +0x04 -> pcie_main_init     +0x08 -> pcie_get_res
    +0x18 -> pcie_xfer_data     +0x1c -> pcie_get_status
    +0x20 -> pcie_msg_send      +0x24 -> pcie_msg_register
    +0x28 -> pcie_reinit        +0x2c -> pcie_close
    +0x30 -> pcie_irq_enable    +0x34 -> pcie_irq_disable
    +0x38 -> pcie_write         +0x3c -> pcie_read
```

So **`bal_write` -> `g_st_pcie_bus_driver[0x38] = pcie_write`** (`[proven]`: the `.rel.data`
relocation at `.data+0x2938` names `pcie_write`; `bal_write` reads `[ip+0x38]` with `ip` =
that struct). `pcie_reinit` is a literal `mov r0,#0; bx lr`, so the `bal_reinit` in
`wlan_bal_init_process` also does nothing.

### A.5 `pcie_write` is `memcpy_s` to a translated VA **[proven]**

`pcie_write` @0xbdc0:

```
  0x00bdf4: bl   pcie_para_check          ; (handle, buf, dev_addr, len, &out)
  0x00be00: ldr  r0, [sp, #0xc]           ; dst = out
  0x00be04: mov  r3, r6                   ; len
  0x00be08: mov  r2, r4                   ; buf
  0x00be0c: mov  r1, r6                   ; len
  0x00be10: bl   memcpy_s                 ; memcpy_s(dst, len, buf, len)
  0x00be14: cmp  r0, #0
  0x00be18: mvnne r0, #0xd                ; -EACCES on failure
```

`pcie_para_check` @0xbc04 computes the destination:

```
  0x00bc1c: ldr  r3, [r0]
  0x00bc20: ldr  r5, [r3]
  0x00bc24: ldr  r0, [r5, #4]             ; region context
  0x00bc30: sub  r1, r6, #1               ; len-1
  0x00bc38: add  r1, r1, r2               ; dev_addr + len - 1  (range end)
  0x00bc40: bl   oal_pcie_inbound_ca_to_va ; end address check
  ...
  0x00bc4c: ldr  r0, [r5, #4]
  0x00bc58: bl   oal_pcie_inbound_ca_to_va ; start address -> out
  0x00bc5c: popeq                          ; 0 on success, out = host VA
```

`oal_pcie_inbound_ca_to_va` @0x8dd8 walks the region descriptors (0x50 bytes each) and, for the
one whose `[lo=+0x28, hi=+0x30]` contains the CA, writes:

```
  0x008e6c: subs r4, r4, r1               ; offset = ca - lo
  0x008e74: add  lr, lr, r4               ; out[0] = [desc+0] + offset   (host VA)
  0x008e78: str  lr, [r2]
  0x008e7c: ldr  r1, [ip, #8]
  0x008e80: add  r4, r1, r4               ; out[1] = [desc+8] + offset   (device VA)
  0x008e84: str  r4, [r2, #4]
```

`pcie_write` copies into `out[0]`, i.e. the **host virtual address of the device CA**. No ring,
no descriptor, no doorbell. The entire firmware write is one `memcpy_s` per chunk. **[proven]**

### A.6 The region table, and `0x01240000` -> `BAR0+0x6f8000` **[proven table, inferred host base]**

The region descriptors come from `g_shuangta_region_types` @`.data+0x29d8` (480 bytes = 6 x 0x50;
returned by `shuangta_get_pcie_priv_res` @0x1b5e4, relocation `.rel.text 0x1b5e4/0x1b5e8`). Each
descriptor is `{u64 dev_base(+0x18), u64 dev_limit(+0x20), u64 base(+0x28), u64 limit(+0x30),
... name_ptr(+0x44)}` with `name` in `.rodata.str1.4`:

| # | name | device range (+0x18..+0x20) | size |
| - | ---- | --------------------------- | ---- |
| 0 | `SHUANGTA_REGION_ROM_WRAM` | `0x00000000`-`0x001bffff` | 1.75 MiB |
| 1 | `SHUANGTA_REGION_TCM_NOACP` | `0x00400000`-`0x00417fff` | 96 KiB |
| 2 | `SHUANGTA_REGION_PKTRAM_NOACP` | `0x01000000`-`0x011dffff` | 1.875 MiB |
| 3 | `SHUANGTA_REGION_IO` | `0x40000000`-`0x4011ffff` | 1.125 MiB |
| 4 | `SHUANGTA_REGION_ACP` | `0x02000000`-`0x021dffff` | 1.875 MiB |
| 5 | `SHUANGTA_REGION_ACP` | `0x01200000`-`0x01417fff` | 2.125 MiB |

**[proven]** CA `0x01240000` falls in descriptor 5. **[inferred, measured]** the runtime host base
of each descriptor is the BAR0 sub-region the vendor's own driver requests with that name, read
back from `/proc/iomem` on the healthy router:

```
  40000000-40ffffff : 0000:00:00.0
    40000000-401bffff : SHUANGTA_REGION_ROM_WRAM       <- descriptor 0
    401c0000-401d7fff : SHUANGTA_REGION_TCM_NOACP      <- descriptor 1
    401d8000-403b7fff : SHUANGTA_REGION_PKTRAM_NOACP   <- descriptor 2
    403b8000-404d7fff : SHUANGTA_REGION_IO             <- descriptor 3
    404d8000-406b7fff : SHUANGTA_REGION_ACP            <- descriptor 4
    406b8000-408cffff : SHUANGTA_REGION_ACP            <- descriptor 5
```

so descriptor 5's host base is `BAR0+0x6b8000` and

    device CA 0x01240000  ->  BAR0+0x6b8000 + (0x01240000 - 0x01200000)  =  BAR0+0x6f8000.

**[measured]** the two aliases are the same SRAM. From the loaded 16 MiB BAR0 dump
`build/register-dumps/barmap_ep0_bar0.bin`:

```
  BAR0+0x00000 == BAR0+0x6b8000  : 131072/131072 bytes identical
  BAR0+0x40000 == BAR0+0x6f8000  : 1048465/1048576 bytes identical
  FIRMWARE.bin[0:835788] appears at BAR0+0x40000 AND at BAR0+0x6f8000, byte for byte
```

and `barmap.md` already recorded that `FIRMWARE.bin`'s 835,788-byte image sits at `BAR0+0x40000`.
So the vendor's download target (`0x01240000`) **is** the firmware window (`BAR0+0x40000`), seen
through the ACP alias at `BAR0+0x6f8000`. Writing either changes both.

### A.7 What uses the ETE SR ring and the doorbell - the *message* path **[proven]**

The task brief expected "the submit sequence (which SR channel + doorbell)" to carry the firmware.
The disassembly says otherwise: `bal_write` never reaches the ETE engine. The ETE path is the HCC
**message** path, entered from the other direction:

```
hcc_queue_tx_process @0x1128c
  0x0113fc: bl -> bal_port_start_xfer
bal_port_start_xfer  @0x10b78
  0x010ba0: ldr  r2, [r3, #0x14]         ; port ctx
  0x010ba4: ldr  r2, [r2, #0x18]         ; bus xfer fn
  0x010bbc: bx   r2                       ; = g_st_pcie_bus_driver[0x18] = pcie_xfer_data
pcie_xfer_data       @0x167e4 -> pcie_tx_request_handle @0x166a0
```

The ring submit itself lives in `shuangta_ete_sr_dscr_fill` @0x17858 - `[inst+0x18]` channel,
node `word1 = len<<16 | 0x6d2b`, `pcie_ete_ring_ptr_plus`, then `pcie_msg_send(chip, 3)`:

```
  0x0178e0: str  r2, [r3, #4]            ; node[index].word1 = len<<16 | 0x6d2b
  0x0178ec: bl   pcie_ete_ring_ptr_plus
  0x0178f4: mov  r1, #3
  0x0178f8: bl   pcie_msg_send
```

and `pcie_msg_send(chip,3)` writes the pending mask to `0x40039010` and ORs bit 0 into the
doorbell `0x400392d4`:

```
  0x01619c: str  r3, [r2]                ; msg reg0 (0x40039010) <= pending mask
  0x0161ac: orr  r3, r3, #1
  0x0161b0: str  r3, [r2]                ; doorbell (0x400392d4) |= bit 0
```

This is the **HCC/message** machinery (registered handler id 3 = `pcie_ete_transfer_done_handle`
by `pcie_msg_init`). It is not in the firmware write path. The firmware chunks are not skbs, carry
no HCC header, and are not submitted to any SR channel. **[proven]**

### A.8 Completion **[proven for the host side; inferred for the chip]**

`pcie_write` returns 0 once `memcpy_s` completes; `firmware_file_send` returns 0 after all chunks
and `firmware_download` returns 0 (`0x00f9c0: mov r0,#0`). The host-side completion is therefore
synchronous - there is no ring-completion handshake for the data. The **chip-side** completion
(firmware actually executing) is a separate handshake that this module does not attempt; the
endpoint's CPU is left exactly as the takeover boot found it. **[inferred]**

### A.9 Proven vs inferred

| field | value | status |
| --- | --- | --- |
| file table entry | `{path, 0x01240000}` at `.data+0x269c` | **proven** |
| bounce buffer | `firmware_mem_try_alloc(0x80000, 0x1000)` -> 512 KiB, 8-byte aligned | **proven** |
| chunk size / offsets | `min(0x80000, remaining)`, target = `0x01240000 + cumulative_off` | **proven** |
| chunk count for FIRMWARE.bin | 2 (`0x80000`, then 404,632) | **proven** |
| `bal_write` target | `g_st_pcie_bus_driver[0x38] = pcie_write` | **proven** |
| `pcie_write` body | `pcie_para_check` + `memcpy_s(dst, n, buf, n)` | **proven** |
| destination | host VA of the device CA (region table lookup) | **proven** |
| region table | 6 x 0x50 descriptors, `g_shuangta_region_types` @`.data+0x29d8` | **proven** |
| CA 0x01240000 -> BAR0+0x6f8000 | descriptor 5 (ACP) host base `BAR0+0x6b8000` | **inferred** (iomem + alias match) |
| `BAR0+0x40000` == `BAR0+0x6f8000` (same SRAM) | 1,048,465/1,048,576 bytes identical in the loaded dump | **measured** |
| ETE SR channel / doorbell in the data path | none - the doorbell is the HCC message path | **proven** |
| power/reset prerequisite | none in `plat.ko` (all four stubs are literal no-ops) | **proven** |
| chip-side completion | not attempted | **inferred** |

---

## Part B - our implementation

`lab/fwload/fwload.c` (`lab/fwload/Makefile`: `obj-m := fwload.o`). Built by the existing
`.github/workflows/build-load-test-module.yml` (a `build fwload module` step and a `fwload-ko`
artifact were added). CI run `36857899876` (commit `3e2bb8c`); `fwload.ko` md5
`e06135df586afa4b18ed0eeb4434ee67`, `vermagic=5.10.201 SMP mod_unload ARMv7`.

It does, in order (source comments carry the same recovery citations as Part A):

1. **Claim** exactly as `epinit`/`eteinit`: `pci_get_domain_bus_and_slot`, id check,
   `pci_enable_device`, `pci_request_mem_regions` (refuses when the vendor holds the regions),
   `pci_write_config_word(dev, 4, 7)`, config-space BAR0 read, ROM vector page logged.
2. **ETE engine init as `eteinit`** (`wregs=1`): the seven coherent node arrays (3x272 B SR,
   4x256 B DR) and the proven SR/DR register writes at `BAR0+0x3a000` (base, depth-1, wptr, ctrl)
   plus the `+0x2e8` RMW, every write read back. (Under the ROM/default BAR0 decode the ETE block
   CA `0x4003a000` is `BAR0+0x3a000`; under the vendor's region map it is `BAR0+0x3f2000` - see
   Part C. The module's init is the takeover-state offset and is not used by the download.)
3. **Read the firmware blob** (`fwpath`, default `/lib/firmware/hi_wifi/FIRMWARE.bin`) with
   `filp_open` + `kernel_read`, logging the size (928,920 bytes).
4. **Writability probe** (`probe=1`, default): one `iowrite32(0xdeadbeef)` at the target and an
   `ioread32` back. If it does not take, the module **refuses the bulk write** and reports the
   documented stop (this is the safety gate that fired on the test boot).
5. **Stream the blob** in `chunk`-sized pieces (`chunk=65536`, vendor is 0x80000) to the target
   with `memcpy_toio`, logging `wrote N/total bytes @ BAR0+0x...` per chunk.
6. **Read back** both the target and the verification window (`verify`, default `BAR0+0x40000`)
   and compare 4 bytes at a time with the file, reporting `diffs`, and the first differing offset
   (file byte vs chip byte).

Module parameters: `domain` (0), `fwpath`, `target` (0x6f8000 = CA 0x01240000), `verify`
(0x40000), `chunk` (65536), `maxlen` (0 = whole file), `wregs` (1), `probe` (1), `dowrite` (1).

---

## Test record

Artifacts in `build/register-dumps/fwload/` (gitignored): `000/010/011_staging*.txt`,
`020_testboot_evidence.txt`, `021_probe64k.txt`, `022_devmem_width.txt`, `023_width2.txt`,
`024_testboot_probe0.txt`, `025_write_40000.txt`, `030/031_recovery_run*.txt`,
`040/041_recovery_evidence*.txt`, `050_final_state.txt`, plus `stage/`.

Recovery command (`/root/recover-fwload.sh`, identical in shape to the phase-16/17 scripts) was
installed **before** the test reboot:

```sh
#!/bin/sh
set -x
cd /lib/modules/5.10.201 || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-fwload
rm -f /etc/init.d/omo-fwload
rm -f /lib/modules/5.10.201/fwload.ko
rm -f /tmp/fwload.ko
sync
reboot
```

The one-shot loader deletes its own rc.d symlink before `insmod` (same watchdog safety as
phase 16/17).

### Baseline and staging

Vendor stack loaded, both endpoints bound to `rox_pci0`, 6 wlan interfaces. Vendor md5s
`e21629d226ec7de9a860a8955952d311` (wifi) / `23660bc285393e678d5cade1c36c194b` (plat). `fwload.ko`
staged at `/lib/modules/5.10.201/fwload.ko` with the same md5 as the CI artifact; the two vendor
modules renamed `.omo-off`; `/etc/init.d/omo-fwload` + `S99` symlink and `/root/recover-fwload.sh`
installed; both scripts `sh -n` clean.

### Live pre-check (normal vendor state, `dowrite=0 wregs=0 probe=0`)

```
omo-fwload: pci_enable_device rc=0 command 0x0006 -> 0x0006
rox_pci0 0000:00:00.0: BAR 0: can't reserve [mem 0x40000000-0x40ffffff 64bit]
omo-fwload: pci_request_mem_regions rc=-16 (vendor stack loaded?) - refusing
```

The module refused and did not stay resident; the box kept both radios. This validates the link,
the ABI (vermagic) and the region guard without a reboot.

### Test boot 1 - the documented stop (`020_testboot_evidence.txt`)

Vendor hidden (`hi5622v100_wifi`/`hi5622v100_plat`/`rox_pci0` absent, `hi_pcie` present), `fwload`
loaded from `S99`. Claim and engine init all succeed:

```
omo-fwload: pci_enable_device rc=0 command 0x0140 -> 0x0142
omo-fwload: pci_request_mem_regions rc=0 (MEM BARs claimed)
omo-fwload: cfg[0x004] <= 0x0007 readback=0x0006 MEM|MASTER=set
omo-fwload: BAR0 base=0x40000000 (config-space read)
omo-fwload: ROM vector page BAR0+0x0: 00000101 00000110 00000002 ...
omo-fwload: ETE ring arrays allocated (3x272B SR, 4x256B DR)
... every SR/DR write readback match=YES (14 register rows + 7 chn_res) ...
omo-fwload: ETE SR/DR registers initialised (wregs=1)
omo-fwload: firmware file /lib/firmware/hi_wifi/FIRMWARE.bin size=928920 bytes
omo-fwload: target window BAR0+0x6f8000 and verify window BAR0+0x40000 mapped
omo-fwload: writability probe target BAR0+0x6f8000: wrote 0xdeadbeef read 0xffffffff match=NO
omo-fwload: target window does NOT hold a write - refusing the bulk download (documented stop)
omo-fwload: verify target BAR0+0x6f8000: file=928920 bytes diffs=921139 match=NO  first_diff=0x0 (file 0x71, chip 0xff)
omo-fwload: verify verify BAR0+0x40000: file=928920 bytes diffs=880373 match=NO  first_diff=0x0 (file 0x71, chip 0x20)
```

Vendor absent, `fwload` present, both endpoints unbound (`0000:00:00.0 enable=1`, ours),
`0001:00:00.0 enable=0` untouched; `br-lan 192.168.10.1/24` up, SSH up, **0 wlan interfaces**
(Wi-Fi down by design); **no new pstore record**. No hang.

### Test boot 1 - the width/behaviour measurements (`021`-`023`)

Live in the same takeover boot (`rmmod`, `insmod` with different parameters, `devmem`):

```
probe target=0x40000: wrote 0xdeadbeef read 0x0000beef match=NO        (only the low 16 bits take)
devmem 0x40040000 32 0x11223344  -> 0x00003344                        (upper 16 bits dropped)
devmem 0x40040000 16 0xa1b2      -> 0x0000A1B2                        (low half takes)
devmem 0x40040002 16 0x2222      -> word0 unchanged                   (the +2 half is not writable)
devmem 0x40040000 8  0x5c        -> low byte takes; +1/+2/+3 dropped
devmem 0x406f8000 -> 0xFFFFFFFF      (vendor CA 0x01240000 alias: undecoded)
devmem 0x403b8000 -> 0xFFFFFFFF      (SHUANGTA_REGION_IO window: undecoded)
devmem 0x403f2000 -> 0xFFFFFFFF      (CA 0x4003a000 via the IO window: undecoded)
```

So in the takeover/ROM state `BAR0+0x40000` is a **16-bit-wide register aperture**, not the
firmware SRAM, and every vendor region window is undecoded (`0xffffffff`). The vendor's
region table host bases were confirmed against the **loaded** BAR0 dump
`build/register-dumps/barmap_ep0_bar0.bin`: `BAR0+0x3f2000 = 0x0000010a` is the ETE block under the
vendor map (CA `0x4003a000`), `BAR0+0x3f2200 = 0x00000001` is the remap-enable word, and
`BAR0+0x3a000 = 0xffffffff` there.

### Test boot 2 - the chunked download loop (`024_testboot_probe0.txt`)

Re-run with `probe=0` so the loop executes against the (undecoded) vendor target. It streams the
whole file and never hangs - writes to the undecoded window are posted and dropped:

```
omo-fwload: download 928920 bytes -> BAR0+0x6f8000 (= device CA 0x1240000) in 65536-byte chunks
omo-fwload: wrote 65536/928920 bytes @ BAR0+0x6f8000
omo-fwload: wrote 131072/928920 bytes @ BAR0+0x708000
... (15 chunks, last @ BAR0+0x7d8000) ...
omo-fwload: wrote 928920/928920 bytes @ BAR0+0x7d8000
omo-fwload: download complete (928920 bytes)
omo-fwload: verify target BAR0+0x6f8000: file=928920 bytes diffs=921139 match=NO  first_diff=0x0 (file 0x71, chip 0xff)
omo-fwload: verify verify BAR0+0x40000: file=928920 bytes diffs=880373 match=NO  first_diff=0x0 (file 0x71, chip 0x20)
```

and one bounded write into the decoded aperture (`maxlen=0x20000`, `target=0x40000`,
`025_write_40000.txt`): 131,072 bytes written, no hang, readback `diffs=123668`, first differing
offset `0x2` (file `0x04`, chip `0x00`) - exactly the dropped upper half of word 0.

### Recovery (`030`/`031`, `040`/`041`, `050`)

`sh /root/recover-fwload.sh` renamed the modules back, removed the loader, the module and the
symlink, `sync`, `reboot`. Recovered boot:

```
hi5622v100_plat       323584  3 hi5622v100_wifi     (baseline use counts)
hi5622v100_wifi      3387392  1
md5 hi5622v100_wifi.ko e21629d226ec7de9a860a8955952d311  (baseline)
md5 hi5622v100_plat.ko 23660bc285393e678d5cade1c36c194b  (baseline)
0000:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
0001:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
6 wlan interfaces; /tmp/wifi_done; hostapd + softapd running; "calibration init done."
iwpriv Hisilicon0 get_chipid  -> chip id:0x34 version:0x00
iwpriv Hisilicon0 alg get_2g_power_param -> [SUCC]17161605 17161605 ... 0a0606ff   (baseline values)
br-lan 192.168.10.1/24 up
leftovers (module, .omo-off, loader, symlink, /tmp): all absent
pstore: no new record (blk-0/2/3 mtimes 10:41/10:26/10:34, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored. Acceptance state
reached.** No panic, no bus soft-lock, no pstore record was produced by any run.

---

## Part C - the outcome

### C.1 What was recovered, and what it means

The vendor's firmware download is fully recovered and is **not** an ETE/DMA/message transfer. It
is `firmware_file_send` -> `bal_write` -> `pcie_write` -> `memcpy_s(host VA of device CA
0x01240000 + off, buf, n)`, in `min(0x80000, remaining)` chunks (Part A). The ETE SR rings and the
`pcie_msg_send(chip,3)` doorbell belong to the HCC **message** path and are nowhere in the data
path. The "headers" the brief asked about do not exist: the chunks are raw file bytes at
cumulative offsets; the only structure is the per-chip file-table entry `{path, 0x01240000}`.

The `0x01240000` window resolves to `BAR0+0x6f8000` (the `SHUANGTA_REGION_ACP` descriptor 5, host
base `BAR0+0x6b8000`), which is a mirror of the firmware window `BAR0+0x40000`. Proven three ways:
the region table, `/proc/iomem`'s named sub-regions, and the loaded BAR0 dump (`BAR0+0x6f8000` ==
`BAR0+0x40000` and both == `FIRMWARE.bin[0:835788]`).

### C.2 What the test boot achieved, and where it stopped

`fwload.ko` claimed the endpoint, initialised the ETE engine, read the 928,920-byte file and ran
the chunked write loop **without a single fault**. It did **not** put the firmware in the chip:

- In the takeover/ROM state the vendor's target window is **not decoded**: `BAR0+0x6f8000` reads
  `0xffffffff` and writes are dropped (the whole 928,920-byte stream completed and the read-back
  is unchanged). The module's writability probe caught this and refused the bulk write on test
  boot 1; test boot 2 ran the loop anyway to record the per-chunk log.
- The decoded window `BAR0+0x40000` is a **16-bit register aperture** in the ROM state, not the
  firmware SRAM: a 32-bit write commits only its low 16 bits, the upper half and every byte past
  offset 0 of each word are dropped. So writing the image there cannot reproduce it either.
- The read-back therefore does not match: `BAR0+0x40000` still reads the ROM-state values
  (`0x00000020 ...`), not `FIRMWARE.bin`. The chip does **not** leave ROM state; its CPU is never
  started and its firmware SRAM is never reached.

**The exact step we stopped before (documented stop).** The vendor's driver, at probe time
(`oal_pci_lres_init` @0x7bb0 -> `oal_pcie_dev_init` @0xa090 + `oal_pcie_enable_regions` @0x91e4 /
`oal_pcie_set_inbound_by_viewport` @0x97a8 -> `pcie_inbound_viewport_switch` @0x9264 /
`pcie_inbound_region_cfg` @0x94e0), **programs the endpoint's inbound-region decoders** - the
config-space writes at `0x900`/`0x904`/`0x908`/`0x90c`, and the BAR2 object `/proc/iomem` names
`iatu_bar1`. That programming is what turns `BAR0+0x6b8000` (and the whole named region map) from
`0xffffffff` into the chip's ACP window. Without it, device CA `0x01240000` is unreachable, so the
recovered write path cannot be completed. This is outside the task's stated write scope ("writes
only within the documented engine registers and the DMA the engine owns") - it is the root
complex / endpoint inbound-iATU setup - and its exact descriptor encoding was not unambiguously
recovered, so the module stops there and this document records it. It is the concrete next step.

A second, independent correction that fell out: the ETE register block CA `0x4003a000` is
`BAR0+0x3a000` only under the ROM/default decode (which is why `eteinit` was correct in the
takeover state); under the vendor's region map it is `BAR0+0x3f2000` (the loaded dump reads
`0x0000010a` there and `0xffffffff` at `0x3a000`). Any follow-up that programs the region map must
use `0x3f2000` for the engine registers.

### C.3 What works, what does not

| | status |
| - | ------ |
| recover the exact transfer structure | **done** (Part A, disassembly-quoted) |
| claim the endpoint in the takeover boot | **works** (`pci_enable_device` + `pci_request_mem_regions` rc=0) |
| initialise the ETE SR/DR engine | **works** (all read-backs match; block at CA 0x4003a000) |
| read the firmware image, stream it in chunks, log per chunk | **works** (15 chunks, 928,920 bytes) |
| write the image to the vendor's target in the takeover boot | **does not work** - target CA 0x01240000 -> `BAR0+0x6f8000` is undecoded (`0xffffffff`) |
| write the image to `BAR0+0x40000` in the takeover boot | **does not work** - 16-bit register aperture, not the SRAM |
| `BAR0+0x40000` matches `FIRMWARE.bin` | **no** (ROM-state values) |
| chip leaves ROM state / CPU started | **no** |
| router healthy, vendor stack restored | **yes** (verified twice) |

### C.4 Risk notes for the writes taken

- **Config-space `cmd` write (`0x04 = 7`)** - proven safe in phase 16/17 (reads back `0x0006`;
  MEM|MASTER set). Taken.
- **BAR0 `+0x3a000` SR/DR register writes** - same class as `eteinit` (phase 17), every read-back
  matched, no doorbell rung, all nodes zeroed. Taken.
- **4-byte writability probes** at `BAR0+0x6f8000` and `BAR0+0x40000` - bounded, read back; the
  probe at the vendor target is what prevented a pointless bulk write. Taken.
- **Chunked writes to `BAR0+0x6f8000`** (undecoded): posted and dropped; the bus never stalled; no
  panic, no pstore. Taken (deliberately, to capture the per-chunk log).
- **A bounded 128 KiB write to `BAR0+0x40000`** - completed without a hang. The module's default
  is a 4-byte probe first, and phase 16's 928,920-byte write there soft-locked the bus at
  ~`BAR0+0x100000`, which is why the full write into that aperture was **not** attempted.
- **No writes outside the documented set**: config space, `BAR0+0x3a000..0x3b000`, and the two
  firmware-window offsets. The module never touched the inbound iATU, the RC, the second endpoint,
  or `BAR2`/`BAR4`. No panic occurred in any run, so the pstore recovery path was never needed.

### C.5 Artifacts

`build/register-dumps/fwload/` (gitignored): the staging, test-boot, width-measurement,
chunked-loop, bounded-write, recovery and final-state captures listed in the test record, plus
`stage/` (`fwload.ko`, `omo-fwload`, `recover-fwload.sh`). Disassembly dumps: `build/tmp/fwload/`.
The module source is `lab/fwload/fwload.c`.
