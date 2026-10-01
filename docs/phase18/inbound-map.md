# inbound-map: the vendor's inbound-region (iATU) programming, and the firmware write that lands (phase 18, 2026-10-01)

Task `st_01a0f759`. This document has three parts and the test-boot record:

- **Part A** - the inbound-region programming recovered from `hi5622v100_plat.ko` (full
  `.symtab`, `build/register-dumps/teardown/hi5622v100_plat.ko`, md5
  `23660bc285393e678d5cade1c36c194b`): which registers are written, their values, the order, what
  each decoded window means, and the resolution of the `0x900` conflict.
- **Part B** - `lab/inbound/inbound.c`: the takeover boot that claims the endpoint, programs the
  six inbound viewports, and probes the result.
- **Part C** - the separate boot that writes `FIRMWARE.bin` and verifies the read-back, the risk
  notes, and the recovery evidence.

Every claim is marked **[proven]** (a constant/relocation/control-flow in the instruction stream,
or a value the device itself printed) or **[inferred]** (semantics derived from structure and
measured hardware behaviour, not a literal). Raw disassembly: `build/tmp/phase18/`
(`lres.txt`, `hostinit.txt`, `devinit.txt`, `inbound2.txt`, `set_inbound_membar_0x989c.txt`).

---

## Part A - the inbound programming

### A.0 The headline: the `0x900` conflict is a **revision gate**, and our device takes the other branch

`docs/phase17/fw-download.md` stopped before "the config-space writes at `0x900`/`0x904`/`0x908`/
`0x90c` and the BAR2 object `/proc/iomem` names `iatu_bar1`", because on our device
`cfg[0x900] = 0xffffffff` and those registers appeared unimplemented. The disassembly resolves it:

**The `0x900` block is one of two mutually exclusive implementations, selected by the endpoint's
PCI revision id (config-space byte `0x08`).** The vendor writes the config-space block **only when
that byte is 1**; when it is 0 the vendor programs the iATU through the **BAR2 MMIO window**
(`iatu_bar1`) and never touches `0x900`. Our endpoint - and the vendor's own endpoints in the boot
dmesg we captured - report **revision 0** (`cfg[0x008] = 0x02800000`, byte 8 = `0x00`
**[proven, measured]**), so the vendor takes the membar branch on this silicon, and `0x900` is
simply dead config space that reads all-ones. That is why phase 16 saw `0xffffffff` there.

### A.1 The call chain **[proven]** (all `bl`/relocation citations)

```
oal_pci_lres_init                          @0x7bb0
  0x7be0: bl -> pcie_get_bus_id_host_view
  0x7c30: bl -> pcie_get_chip_id
  0x7c54: bl -> get_chip_type(0, &chiptype)          ; [sp+2]
  0x7c5c: bl -> pci_enable_device(dev)
  0x7c94: bl -> pci_read_config_byte(dev, 8, &rev)   ; [sp+3] = revision id
  0x7cc8: bl -> oal_pcie_host_init(dev, rev, chiptype)
  0x7ce8: bl -> oal_pcie_dev_init(host, bus_id)

oal_pcie_host_init                         @0xbefc
  0xbf1c: movw r1, #0xff8 ; bl pci_read_config_dword(dev, 0xff8, &v)
  0xbf14: mov  r7, r1                                ; r7 = rev (2nd arg)
  0xbf80: str  r7, [r4, #0x14]                       ; host->rev = cfg byte 8  <-- THE GATE
  0xbf88: str  r6, [r4, #0xc4]                       ; host->[0xc4] = chip resource table

oal_pcie_dev_init                          @0xa090
  0xa0e8: bl -> get_pci_chip_res ; r8 = [r0+0x70]    ; run-time region descriptors
  0xa11c: bl -> memcpy_s(per-chip copy, 0x50*count, [r8])
  0xa188: str sb, [host+0x20]                        ; region array
  0xa1a4: str r2, [host+0x24]                        ; region count = 6
  0xa4a0: bl -> __request_region(iomem_resource, region paddr, size, name)
  0xa4b4: bl -> ioremap(region paddr, size)          ; each region's host VA
  0xa4dc: bl -> oal_pcie_enable_regions              ; sets host+0x30 = 1
  0xa4fc: bl -> 0x989c                               ; <inlined oal_pcie_set_inbound>

oal_pcie_enable_regions                    @0x91e4    ; ONLY sets a flag
  0x91fc: str r1=1, [r3, #0x30]                      ; "regions enabled"
  0x920c: mov r0, #0 ; pop                           ; no IATU write at all
```

So "enabling regions" is a software flag, not hardware; the hardware programming is entirely in the
unnamed `oal_pcie_set_inbound` at `0x989c` (the compiler inlined
`oal_pcie_region_inbound_cfg` / `oal_pcie_set_inbound_by_membar` / `..._by_viewport` /
`oal_pcie_set_outbound_by_membar` / `oal_pcie_bar_init_default` into it, which is why those names
survive only as `__func__` strings - e.g. `0x7f54: "iatu_bar1"`, `0x7c88: "...pci write iatu config
failed ret=%d"`). **[proven]**

### A.2 The gate, verbatim **[proven]**

`oal_pcie_set_inbound` @ `0x989c` (disasm `set_inbound_membar_0x989c.txt`):

```
  0x98ac: ldr  r3, [sl, #0x30]        ; regions-enabled flag
  0x98b4: beq  #0x9bec                ; 0 -> print "pcie regions is disabled, iatu config failed"
  0x98b8: ldr  r3, [sl, #0x14]        ; host->rev  (= config-space byte 8)
  0x98c0: beq  #0x9b3c                ; rev == 1 -> oal_pcie_set_inbound_by_viewport  (cfg 0x900 block)
  0x98c4: cmp  r3, #0
  0x98c8: bne  #0x9bb0                ; rev != 0 and != 1 -> -ENODEV
  0x98cc: ...                         ; rev == 0 -> the membar loop below
```

and at `0x9b3c` the rev==1 path calls the config-space function:

```
  0x9b3c: movw fp, #0 ; movt fp, #0
  0x9b44: bl -> oal_pcie_set_inbound_by_viewport     ; then rejoins at 0x9a38
```

**What gates the `0x900` block:** (1) the regions-enabled flag `host+0x30` set by
`oal_pcie_enable_regions`, and (2) the revision id. On our device the flag is set but the revision
id is 0, so the config-space block is never reached.

### A.3 rev==1: the config-space viewport path (what the brief quoted) **[proven]**

`oal_pcie_set_inbound_by_viewport` @ `0x97a8` loops over the six regions and for each calls
`pcie_inbound_viewport_switch` @ `0x9264` then `pcie_inbound_region_cfg` @ `0x94e0`; after the loop
it writes `PCI_COMMAND = 7`:

```
  0x9814: mov r2, #7 ; 0x9818: mov r1, #4 ; bl pci_write_config_word(dev, 4, 7)
```

`pcie_inbound_viewport_switch(dev, i, desc)`:

```
  0x9274: orr  r7, r1, #0x80000000          ; 0x80000000 | region index i
  0x928c: movw r1, #0x900
  0x929c: bl -> pci_write_config_dword(dev, 0x900, r7)     ; iATU_VIEWPORT = inbound | index
  0x92b4: bl -> pci_read_config_dword(dev, 0x900, &v)      ; read-back check (fails -> -4)
  0x92d8: bl -> pci_write_config_dword(dev, 0x908, 0)      ; CTRL2 = 0 (disable)
  0x92f4: and  r2, r2, #7                                  ; r2 = byte[desc+0x48][0] & 7
  0x92f8: lsl  r2, r2, #8                                  ; -> BAR number in bits 8..10
  0x92fc: orr  r2, r2, #0x80000000                         ; -> enable, bit 31
  0x9300: bl -> pci_write_config_dword(dev, 0x908, r2)     ; CTRL2 = enable | bar
```

`pcie_inbound_region_cfg(dev, i, desc)` (desc = 0x50-byte run-time region descriptor):

```
  0x9518: pci_write_config_dword(dev, 0x90c, [desc+0x10])  ; base_lo  (host PCI bus address)
  0x9534: pci_write_config_dword(dev, 0x910, [desc+0x14])  ; base_hi
  0x9548: r2 = [desc+0x38]                                 ; size
  0x954c: adds r2, r2, fp (base-1) ; 0x9550: adc sb, sb, #0 ; r2:sb = base + size - 1
  0x9554: cmp r8 ([desc+0x14]), sb ; bne -> log "iatu high 32 bits must same!"
  0x955c: pci_write_config_dword(dev, 0x914, r2)          ; limit_lo
  0x9580: pci_write_config_dword(dev, 0x918, [desc+0x18])  ; target_lo (device CA)
  0x9598: pci_write_config_dword(dev, 0x91c, [desc+0x1c])  ; target_hi
```

Note `0x904` (**CTRL1**, the region type) is **never written** by either path: it stays at its reset
value 0, i.e. "memory region". The brief's `0x904` is the one register of the five that the vendor
leaves alone. **[proven]**

### A.4 rev==0: the membar path (what our device actually runs) **[proven]**

Same function, the other branch - the register file at the iATU MMIO window instead of config
space:

```
  0x98e4: ldr r3, [sl, #0x24]        ; region count (6)
  0x98e8: ldr r5, [sl, #0x20]        ; region descriptor array (stride 0x50)
  0x9918: ldr r3, [sp, #0x18]        ; iatu vaddr  (= [sl+0x38] = ioremap(BAR2))
  0x9924: add r4, r3, #0x104         ; viewport 0 register block begins at +0x104
  0x9928: mov r8, #0x10              ; 16 possible viewports
 loop (0x9930):
  0x993c: str r3=0,  [r4]            ;   ctrl2 = 0            (disable)
  0x9940: r3 = [desc+0x48] ; r7 = byte[r3] & 7
  0x994c: r7 <<= 8 ; 0x9950: r7 |= 0x80000000 ; 0x9954: r7 &= 0x80000700
  0x9960: str r7, [r4]               ;   ctrl2 = enable | BAR number
  0x9984: str sb=[desc+0x10], [r4+4] ;   base_lo
  0x9990: str r7=[desc+0x14], [r4+8] ;   base_hi
  0x99b8: str sl=[limit],    [r4+12];   limit_lo  (= base + size - 1; hi checked equal)
  0x99cc: str sb=[desc+0x18], [r4+16];   target_lo (device CA)
  0x99d8: str r7=[desc+0x1c], [r4+20];   target_hi
  0x99e0: add r5, r5, #0x50          ;   next region descriptor
  0x99ec: add r4, r4, #0x200         ;   next viewport (stride 0x200)
  0x99f0: subs r8, r8, #1 ; bne loop
  0x9a08: ldr r3, [iatu, #0x104] ; dsb sy
```

and then, after the loop, the same tail as the rev==1 path:

```
  0x9a14: pci_write_config_word(dev, 4, 7)          ; PCI_COMMAND = IO|MEM|MASTER
```

The `(byte & 7) << 8 | 0x80000000` in ctrl2 is the **BAR number** in bits 8-10 plus the enable bit;
the vendor's own boot log identifies the bar for every one of the six regions as `bar idx:0`
(`[oal_pcie_regions_get_bar_res:916]bar idx:0, region idx:0..5`) **[proven, device log]**, so ctrl2
= `0x80000000` for all six, which is what our module writes.

There is a **second, separate block** in the same rev==0 branch at `iatu+0x00..0x18`
(`0x9a58..0x9af4`), driven from `host+0xc4`: that is the **outbound** window
(`oal_pcie_set_outbound_by_membar`, target `0x80000000`, the vendor log
`[oal_pcie_set_outbound_by_membar:642]PCIe outbound bus addr:0x80000000`). It is not part of the
inbound map and our module does not reproduce it (see C.4). **[proven]**

### A.5 Register map and the correct order **[proven]**

The iATU register file is 16 KiB at **BAR2**, which the live `/proc/iomem` names `iatu_bar1`:

```
  40000000-40ffffff : 0000:00:00.0
    40000000-401bffff : SHUANGTA_REGION_ROM_WRAM
    401c0000-401d7fff : SHUANGTA_REGION_TCM_NOACP
    401d8000-403b7fff : SHUANGTA_REGION_PKTRAM_NOACP
    403b8000-404d7fff : SHUANGTA_REGION_IO
    404d8000-406b7fff : SHUANGTA_REGION_ACP
    406b8000-408cffff : SHUANGTA_REGION_ACP
  41000000-417fffff : 0000:00:00.0
  41800000-41803fff : 0000:00:00.0
    41800000-41803fff : iatu_bar1          <-- the iATU window (BAR2, 16 KiB)
```

**[measured, live router, vendor stack loaded]**. The membar offsets are the config-space iATU
offsets shifted by `-0x804` (0x908->0x104, 0x90c->0x108, ...), i.e. the config-space DBI file is
the same registers; the config path additionally selects the viewport through `0x900`, which the
per-viewport MMIO window does not need.

Per region index `i` (0..5), written in exactly this order (the vendor's):

| step | membar offset | config-space twin | value | meaning |
| ---- | ------------- | ----------------- | ----- | ------- |
| 1 | `0x104 + 0x200*i` | `0x908` | `0` | CTRL2 = disable |
| 2 | `0x104 + 0x200*i` | `0x908` | `(bar&7)<<8 \| 0x80000000` | CTRL2 = enable, BAR0 |
| 3 | `0x108 + 0x200*i` | `0x90c` | `[desc+0x10]` | base_lo (host PCI bus address) |
| 4 | `0x10c + 0x200*i` | `0x910` | `[desc+0x14]` | base_hi |
| 5 | `0x110 + 0x200*i` | `0x914` | `base + size - 1` | limit |
| 6 | `0x114 + 0x200*i` | `0x918` | `[desc+0x18]` | target_lo (device CA) |
| 7 | `0x118 + 0x200*i` | `0x91c` | `[desc+0x1c]` | target_hi |
| - | `0x100 + 0x200*i` | `0x904` | *(not written)* | CTRL1, stays 0 = memory region |
| after | `PCI_COMMAND` | `0x004` | `7` | IO\|MEM\|MASTER |

### A.6 The six regions and what each decoded window means **[proven values]**

The run-time descriptors come from `g_shuangta_region_types` (`.data+0x29d8`, 6 x 0x50) through
`get_pci_chip_res`, and the addresses agree three ways: the table's device ranges, the vendor boot
log's `region paddr`/`region_size` (from the pstore), and the live `/proc/iomem` names.

| idx | host BAR0 window | size | device CA target | /proc/iomem name | what it decodes |
| --- | ---------------- | ---- | ---------------- | ---------------- | --------------- |
| 0 | `0x40000000`-`0x401bffff` | `0x1c0000` | `0x00000000` | `SHUANGTA_REGION_ROM_WRAM` | ROM/RAM code (the ROM vector/data the takeover boot reads at BAR0+0x0) |
| 1 | `0x401c0000`-`0x401d7fff` | `0x018000` | `0x00400000` | `SHUANGTA_REGION_TCM_NOACP` | device TCM |
| 2 | `0x401d8000`-`0x403b7fff` | `0x1e0000` | `0x01000000` | `SHUANGTA_REGION_PKTRAM_NOACP` | packet RAM |
| 3 | `0x403b8000`-`0x404d7fff` | `0x120000` | `0x40000000` | `SHUANGTA_REGION_IO` | the register/IO block (ETE engine at CA `0x4003a000` appears at BAR0+0x3f2000) |
| 4 | `0x404d8000`-`0x406b7fff` | `0x1e0000` | `0x02000000` | `SHUANGTA_REGION_ACP` | ACP SRAM |
| 5 | `0x406b8000`-`0x408cffff` | `0x218000` | `0x01200000` | `SHUANGTA_REGION_ACP` | ACP SRAM; **holds the firmware** at CA `0x01240000` = BAR0+`0x6f8000` |

**[proven]** (`g_shuangta_region_types` + `[oal_pcie_regions_get_bar_res:916]` log lines 228-234/258-264
of the pstore). So the vendor firmware target, device CA `0x01240000`, resolves through region 5 to
`BAR0+0x6f8000` - the same route phase 17 found, now with the decoder actually programmed.

### A.7 Proven vs inferred

| field | value | status |
| --- | --- | --- |
| gate | endpoint PCI revision id == 1 -> cfg path; == 0 -> membar path | **proven** (`0x98b8..0x98c8`) |
| our endpoint revision | `0x00` (`cfg[0x008]=0x02800000`) | **proven, measured** |
| regions-enabled flag | `host+0x30`, set by `oal_pcie_enable_regions` | **proven** |
| config-space path regs | `0x900`(sel), `0x908`(ctrl2), `0x90c..0x91c`(base/limit/target); `0x904` unwritten | **proven** |
| membar path regs | `iatu_bar1 + 0x104 + 0x200*i` (+0, +4, +8, +0xc, +0x10, +0x14) | **proven** |
| membar window | BAR2 = `0x41800000`, 16 KiB, `/proc/iomem` name `iatu_bar1` | **proven, measured** |
| ctrl2 value | `0x80000000` (enable + BAR0) | **proven** (code + `bar idx:0` log) |
| limit | `base + size - 1` (high halves must match) | **proven** |
| six region values | table in A.6 | **proven** (table + pstore + iomem) |
| outbound block at `iatu+0x0..0x18` | separate, not reproduced | **proven** (present in code) |

---

## Part B - `lab/inbound/inbound.c` and the decode probe

`lab/inbound/inbound.c` (`lab/inbound/Makefile`: `obj-m := inbound.o`). Built by the existing
GitHub Actions workflow (a `build inbound module` step and an `inbound-ko` artifact were added).
CI run `36860545708` (commit `f32f494`); `inbound.ko` md5
`92bec491631b7df17d6eca514b0bb27d`, `vermagic=5.10.201 SMP mod_unload ARMv7`.

In order (every iATU write logged with its read-back):

1. **Claim** as `epinit`/`eteinit`: `pci_get_domain_bus_and_slot`, id check,
   `pci_enable_device`, `pci_request_mem_regions`, config-space BAR read.
2. **Read-only config dump** including `0x900/0x904/0x908/0x90c` and the revision byte, and log
   which vendor path that byte selects.
3. **Map BAR0 and BAR2** (`pci_iomap`). BAR0 base is taken from **config space**, not
   `pci_resource_start()` - on this vendor kernel the inlined resource macro reads the wrong
   struct offsets and returned 0 (section B.3); `pci_iomap` itself, being in-kernel, is correct.
4. **Baseline**: `BAR0+0x40000`, `BAR0+0x6f8000`, the ROM vector page, and iATU viewport 0.
5. If `program=1`: refuse if `iatu_bar1+0x104` reads `0xffffffff` (window not decoded); otherwise
   write the six viewports in the A.5 order, each read back.
6. `pci_write_config_word(dev, 4, 7)`.
7. **Probe** `BAR0+0x6f8000` and `BAR0+0x40000` before/after and report decode.
8. `mode=2` additionally writes `FIRMWARE.bin` (Part C).

Parameters: `domain` (0), `program` (1), `mode` (1 = probe, 2 = firmware), `fwpath`
(`/lib/firmware/hi_wifi/FIRMWARE.bin`), `target` (0x6f8000 = CA 0x01240000), `verify` (0x40000),
`chunk` (0x10000; the vendor uses 0x80000), `maxlen` (0), `dowrite` (1).

### Test boot 1 - the decode probe (`build/register-dumps/inbound/060_testboot1_evidence2.txt`)

Vendor hidden (`hi5622v100_wifi`/`plat`/`rox_pci0` absent), `hi_pcie` present, `inbound` loaded from
`S99`. Config space and the gate, then the programming (all six viewports, all read-backs `YES`):

```
omo-inbound: cfg[0x008] = 0x02800000  revision[8]/class[9..b]
omo-inbound: cfg[0x900] = 0xffffffff  iATU viewport 0x900
omo-inbound: cfg[0x904] = 0x00000000  iATU ctrl1 0x904
omo-inbound: cfg[0x908] = 0x00000000  iATU ctrl2 0x908
omo-inbound: cfg[0x90c] = 0x00000000  iATU base_lo 0x90c
omo-inbound: PCI revision id = 0x00 -> vendor membar (BAR2 iATU window) path
omo-inbound: BAR0 base=0x40000000 (config space), BAR2=0x41800000 (iatu_bar1)
omo-inbound: ROM vector page BAR0+0x0 [0x0]: 00000101 00000110 00000002 ...
omo-inbound: before BAR0+0x40000 [0x40000]: 00000020 00000000 ... 00b40640
omo-inbound: before BAR0+0x6f8000 [0x6f8000]: ffffffff ffffffff ffffffff ...
omo-inbound: before iatu viewport0 [0x100]: 00000000 ... 00000fff ...
omo-inbound: region 5 ACP-fw: host 0x406b8000..0x408cffff -> dev 0x1200000 size 0x218000
omo-inbound:   iatu[0xb04] <= 0x80000000 readback=0x80000000 match=YES  (r5 ctrl2=ena)
omo-inbound:   iatu[0xb08] <= 0x406b8000 readback=0x406b8000 match=YES  (r5 base_lo)
omo-inbound:   iatu[0xb10] <= 0x408cffff readback=0x408cffff match=YES  (r5 limit)
omo-inbound:   iatu[0xb14] <= 0x01200000 readback=0x01200000 match=YES  (r5 target_lo)
... (regions 0..4 likewise, every match=YES) ...
omo-inbound: programmed 6 viewports
omo-inbound: cfg[0x004] <= 0x0007 readback=0x0006 MEM|MASTER=set
omo-inbound: after  BAR0+0x6f8000 [0x6f8000]: 00000000 00000000 4e2c733a 001a7c31 ...
omo-inbound: after  BAR0+0x40000 [0x40000]: 00000000 00000000 4e2c733a 001a7c31 ...
omo-inbound: decode verdict BAR0+0x6f8000: before=0/8 after=8/8 decoded=YES
omo-inbound: decode verdict BAR0+0x40000: before=8/8 after=8/8 decoded=YES
```

and the live `devmem` probes after the program:

```
0x40000000 0xE59FF018     (dev 0x0        = ROM/RAM code)
0x40040000 0x00000000     (dev 0x40000)
0x406b8000 0xE59FF018     (dev 0x1200000  = alias of dev 0x0)
0x406f8000 0x00000000     (dev 0x1240000  = firmware target)
0x403b8000 0x00000101     (dev 0x40000000 = IO ROM vector)
0x403f2000 0x0000010A     (dev 0x4003a000 = ETE block; == the loaded vendor dump)
```

**Decode proven.** Three independent confirmations: the target window changes from all-`0xff` to
real data; `BAR0+0x3f2000` now reads `0x0000010a`, exactly the value in the loaded vendor BAR0 dump
(`build/register-dumps/barmap_ep0_bar0.bin`) at that offset; and `BAR0+0x0` changes to the device's
ROM/RAM code (`0xE59FF018`, the loaded dump's value at `BAR0+0x0`). The two firmware aliases
`BAR0+0x40000` and `BAR0+0x6f8000` read identically, as the vendor's own map has them. No hang, no
new pstore record, box reachable.

### B.3 The first attempt, and the ABI delta it exposed

The first boot-1 run programmed the wrong host bases because `pci_resource_start()` returned 0 on
this vendor kernel (the same `struct pci_dev`/`struct resource` offset delta phase 16 flagged), so
the iATU got `base = 0x000000..0x008cffff`. All six writes still took and read back, but
`BAR0+0x6f8000` stayed `0xffffffff` - the host base was simply the wrong address. The fix (take the
base from config-space BAR0) is commit `f32f494`; the regenerated boot is the one above. The wrong
programming was harmless and was undone by the recovery reboot. **[measured]**

---

## Part C - the firmware write, verified

Separate boot (`mode=2`), same recovery script staged first
(`build/register-dumps/inbound/090_testboot2_evidence.txt`):

```
omo-inbound: region 5 ACP-fw: host 0x406b8000..0x408cffff -> dev 0x1200000 size 0x218000
... all six viewports programmed, every read-back match=YES ...
omo-inbound: cfg[0x004] <= 0x0007 readback=0x0006 MEM|MASTER=set
omo-inbound: decode verdict BAR0+0x6f8000: before=0/8 after=8/8 decoded=YES
omo-inbound: firmware file /lib/firmware/hi_wifi/FIRMWARE.bin size=928920 bytes
omo-inbound: writability probe BAR0+0x6f8000: wrote 0xdeadbeef read 0xdeadbeef match=YES
omo-inbound: download 928920 bytes -> BAR0+0x6f8000 (device CA 0x01240000) in 524288-byte chunks
omo-inbound: wrote 524288/928920 bytes @ BAR0+0x6f8000
omo-inbound: wrote 928920/928920 bytes @ BAR0+0x778000
omo-inbound: verify target BAR0+0x6f8000: file=928920 bytes diffs=0 match=YES
omo-inbound: done (mode=2 program=1)
```

The read-back comparison is byte-for-byte over the whole 928,920-byte file: **`diffs=0`**. The
command sequence and chunking are the vendor's (`firmware_file_send` -> chunks of `0x80000` to
device CA `0x01240000 + off`), so this is the vendor's own download, performed by us:

```
file:   71 69 04 00 2d 74 0c 00 ...        (FIRMWARE.bin, 0x00046971 0x000c742d)
window: 0x406f8000 0x00046971  0x406f8004 0x000C742D  0x406f8008 0x00000000 ...
```

**This is the first time the firmware image has been put in the chip by an open path** - the phase-17
write could not land because the window was undecoded; with the inbound region programming
recovered, it does. The chip's own CPU is still not started (that is the vendor's separate
hand-off, out of scope); we only place the image. No hang, no new pstore record, box reachable.

### C.4 Risk notes for the writes taken

- **BAR2 (`iatu_bar1`) iATU register writes** - six viewports, in the vendor's exact order and with
  the vendor's exact values (proven from the disassembly and the vendor's own boot log). Every write
  read back. This is the vendor's own probe-time programming, replayed. Taken.
- **ctrl2 is enabled before base/limit/target**, as the vendor does; each region briefly points at
  its reset window (base 0, limit 0) until the address is written. That is the vendor's order and
  was reproduced literally rather than "hardened", so the evidence matches the vendor. Taken.
- **The outbound block (`iatu+0x0..0x18`) is not reproduced.** It is a separate vendor step
  (`oal_pcie_set_outbound_by_membar`, host `0x80000000`) and is not needed for host->device inbound
  access; leaving it alone is the smaller change. Not taken.
- **`PCI_COMMAND = 7`** - proven safe in phases 16/17/18 (reads back `0x0006`; I/O bit is RO0, no
  I/O BAR). Taken.
- **The full 928,920-byte firmware write to BAR0+0x6f8000** - this is exactly the vendor's own
  download target through the now-decoded region 5 window, so it is the vendor's own write; it
  completed without a stall. (Phase 16's hang was a write into the *undecoded* window past
  BAR0+0x100000; the decoded region-5 window is the vendor's firmware SRAM and does not stall.)
  Taken, once, in a dedicated boot.
- **Nothing outside the documented set**: config space, the six inbound viewport registers at
  `iatu_bar1`, and the firmware window. No writes to the second endpoint, the RC, the outbound
  iATU, or the ETE engine. No panic occurred in any run, so the pstore path was never needed.

### C.5 Recovery - recorded before the reboots, verified after

Installed on the device **before** the test reboots at `/root/recover-inbound.sh` (shipped as
`build/register-dumps/inbound/stage/recover-inbound.sh`, `sh -n` clean):

```sh
#!/bin/sh
set -x
cd /lib/modules/5.10.201 || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-inbound
rm -f /etc/init.d/omo-inbound
rm -f /lib/modules/5.10.201/inbound.ko
rm -f /tmp/inbound.ko
sync
reboot
```

The one-shot loader deletes its own rc.d symlink before `insmod` (watchdog safety, as in phases
16/17): a hang resets into a reachable boot with the vendor still hidden and no `inbound`, so the
recovery script can run. Fallback if the box does not come back at all: U-Boot slot A (stock).

Recovered boot (`build/register-dumps/inbound/120_recovery_evidence.txt`):

```
hi5622v100_plat       323584  3 hi5622v100_wifi
hi5622v100_wifi      3387392  1
md5 hi5622v100_wifi.ko e21629d226ec7de9a860a8955952d311  (baseline)
md5 hi5622v100_plat.ko 23660bc285393e678d5cade1c36c194b  (baseline)
0000:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
0001:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
Wiphy phy0 + phy1, 6 wlan interfaces
iwpriv Hisilicon0 get_chipid -> chip id:0x34 version:0x00
iwpriv Hisilicon0 alg get_2g_power_param -> [SUCC]17161605 17161605 ...
br-lan 192.168.10.1/24 up
leftovers (module, .omo-off, loader, symlink): all absent  (/root/recover-inbound.sh retained)
pstore: no new record (blk-0/2/3 mtimes 10:41/10:26/10:34, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored. Acceptance reached.**

---

## Test record

Artifacts in `build/register-dumps/inbound/` (gitignored): `000_baseline.txt`,
`010_staging.txt`, `015_live_precheck.txt` (module ABI + refuse-when-vendor-loaded, no reboot),
`020_testboot1_stage.txt`, `030_testboot1_evidence.txt` (first attempt, wrong host base),
`040_recovery_run.txt`, `045_recovery_early.txt`, `050_staging2.txt`, `060_testboot1_evidence2.txt`
(**the decode proof**), `070_testboot2_evidence.txt` and `075/076_testboot2_*` (the missed-symlink
boot), `078_recovery_between.txt`, `080_staging_boot2.txt`,
`090_testboot2_evidence.txt` (**the firmware write + verify**), `100_fw_compare.txt`,
`110_recovery_run.txt`, `120_recovery_evidence.txt`, plus `stage/` (`inbound.ko`, `omo-inbound`,
`recover-inbound.sh`). Disassembly: `build/tmp/phase18/`. The module source is
`lab/inbound/inbound.c`.

Sequence: live pre-check (no reboot) -> test boot 1 (decode probe) -> recovery -> test boot 2
(firmware write) -> recovery. Two test boots, two recoveries; the intervening recovery was added
because the boot-2 loader's symlink had not been re-created on the first attempt (a host reboot
resets the endpoint's iATU, so each boot must program it - which the module does, and which is why
the clean boot-2 rerun is the evidence above).
