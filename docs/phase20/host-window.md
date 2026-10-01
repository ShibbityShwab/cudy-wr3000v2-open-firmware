# host-window: making host memory device-visible and routing the endpoint interrupt (phase 20c, 2026-10-01)

Task `st_01a0f812`. Direct sequel to `docs/phase20/runtime-msg.md`, which stood up the runtime
message context and the ETE SR/DR rings in a takeover boot and proved that the released firmware
then emits a **new** PCIe word (`out[1]` `0 -> 0x40`, bit 6 = `pcie_trigger_ete_sending_handle`)
before the familiar `0x4`, but that **no payload lands** in any DR-ring buffer and **no interrupt is
taken**. That phase named two gaps: the ring base is a host address the device cannot reach (the
runtime window `chip->[4]->[0xc4]` used by `pcie_hostca_to_devva` is absent in a takeover), and the
endpoint's INTx has no routed line. This phase recovers the mechanism behind gap 1 and reproduces it
and characterises gap 2 (the payload still does not land - see Part C).

- **Part A** - the device-visible host window recovered from `hi5622v100_plat.ko`
  (`build/register-dumps/teardown/hi5622v100_plat.ko`, md5 `23660bc285393e678d5cade1c36c194b`)
  **and** from a live vendor boot: the window object, the `hostca -> devva` formula, the *outbound*
  iATU viewport that programs it, and the live register values; and the endpoint INTx route
  (GIC-0 91 -> virq 207), who owns it, and what a takeover boot lacks.
- **Part B** - `lab/hostwin/hostwin.c`: rtmsg + the outbound viewport + the recovered `devva`
  formula + the endpoint config line; one takeover boot.
- **Part C** - whether a payload landed, what the firmware sent, the IRQ behaviour, what remains.

Every claim is marked **[proven]** (a constant/relocation/control-flow in the instruction stream,
or a value the device printed/measured) or **[inferred]**.

**Headline.** The device-visible window is recovered and reproduced. The vendor converts every host
coherent-DMA address with `pcie_hostca_to_devva` @ `0xaefc`, which walks a **0x18-byte per-chiptype
descriptor** at `chip->[4]->[0xc4]` = `.data+0x1fb8`:

```
win+0x00 devva_base  = 0x80000000      win+0x08 devva_end   = 0xffffffff
win+0x10 hostca_base = 0x80000000      (so devva = hostca)
```

and `oal_pcie_set_inbound`'s **outbound block** @ `0x9a38..0x9af4` programs exactly that range into
one endpoint iATU viewport at **BAR2+0x000** (`ctrl1=0`, `ctrl2=enable|bar0`, `base=win[0]`,
`limit=win[8]`, `target=win[0x10]`). A live vendor boot reads that viewport as
`base=0x80000000, limit=0xffffffff, target=0x80000000`; a takeover boot leaves it at reset
(`ctrl2=0`), which is the missing device->host window. `hostwin.c` programs it (byte-for-byte the
vendor's live values) and writes the ETE ring bases through the recovered formula - but **no payload
lands** and **no interrupt is taken**: the firmware still emits only the two mailbox words
(`0x40` then `0x04`). The outbound window was necessary state that rtmsg lacked, but it is not
sufficient on its own: the firmware stops before an ETE transfer, because the host side of the
message service (the ack/re-arm CAs and the real id-6 handler) is still not reproduced. Recovery
restored the vendor stack and both radios.

Artifacts (regenerable):

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko        # md5 23660bc285393e678d5cade1c36c194b
$PY lab/ko_disasm.py $KO pcie_hostca_to_devva pcie_devva_to_hostca oal_pcie_inbound_ca_to_va \
    oal_pcie_devca_to_hostva memmap_get_dev_acp_addr memmap_get_dev_addr_from_acp_addr \
    pcie_if_hostca_to_devva oal_pcie_host_init shuangta_memmap_get_acp_addr \
    shuangta_memmap_get_addr_from_acp_addr                    > build/tmp/hostwin/addr2.txt
$PY lab/ko_disasm.py $KO pcie_irq_enable pcie_irq_disable oal_disable_pcie_irq   # (irq.txt has the rest)
$PY build/tmp/hostwin/raw.py $KO 0x989c 0x9b20               > build/tmp/hostwin/outbound.txt
```

The outbound block lives in an **unnamed local function** (`oal_pcie_set_inbound` @ `0x989c`; the
symbol is absent from `.symtab`), so it is disassembled by raw address range. The six-region
inbound programming at `iatu+0x104+0x200*i` is the subject of phase 18; this phase covers the
**second block at `iatu+0x000..0x018`**.

---

## Part A - the host window and the interrupt route

### A.0 The object model

`oal_pcie_host_init` @ `0xbefc` allocates the **host object** (`kmem_cache_alloc_trace`, 0xc8
bytes) and stores it as `chip->[4]`; the last thing it stores is the window pointer:

```
===== oal_pcie_host_init @ 0xbefc =====
  0x00bf40: ... kmem_cache_alloc_trace(0xc8)         ; host object
  0x00bf84: mla  r6, r1(=0x18), r6(=chiptype), r2    ; r2 = .data table, r6 = &table[0x18*chiptype]
  0x00bf8c: str  r6, [r4, #0xc4]                     ; host->[0xc4] = &window[chiptype]   <-- THE WINDOW
  ...
  0x00c098: ldr  r3, [r4, #0xc4]                     ; (log path reads it back)
  0x00c0a8: ldrd r2, r3, [r3, #0x10]                 ; win+0x10 = the host CA base it logs
```

The literal at `0xc0cc` (loaded by `0xbf70`) is a relocation with addend `0x1fb8` into **`.data`**
(section index 22), i.e. the window table is `&.data[0x1fb8 + 0x18*chiptype]`. `.data+0x1fb8` holds,
for chiptype 0:

| off | value | field |
| --- | ----- | ----- |
| `+0x00` | `0x80000000` | `devva_base` |
| `+0x04` | `0x00000000` | `devva_base_hi` |
| `+0x08` | `0xffffffff` | `devva_end` |
| `+0x0c` | `0x00000000` | `devva_end_hi` |
| `+0x10` | `0x80000000` | `hostca_base` |
| `+0x14` | `0x00000000` | `hostca_base_hi` |

**[proven]** (relocation + `.data` dump; the same six words are what the live iATU outbound viewport
holds, below). The `mla` indexes with the chip type, but the entry actually used is `.data+0x1fb8`
because the live endpoint's outbound register block matches it exactly; the next 0x18-byte slot at
`.data+0x1fd0` is string data, so the driver's chip type on this board is 0. **[measured]**

### A.1 `pcie_hostca_to_devva` and its inverse **[proven]**

```
===== pcie_hostca_to_devva @ 0xaefc =====
  0x00af08: ldr  r1, [r0, #4]                 ; chip->[4]   = host object
  0x00af14: ldr  r1, [r1, #0xc4]              ; host->[0xc4] = window
  0x00af20: ldr  ip, [r1, #0x10]              ; win+0x10 = hostca_base
  0x00af24: cmp  r2, ip                       ; hostca < hostca_base -> error
  0x00af2c: blo  #0xaf5c
  0x00af30: ldr  r0, [r1]                     ; win+0x00 = devva_base
  0x00af34: add  r0, r0, r2                   ; + hostca
  0x00af38: sub  r0, r0, ip                   ; - hostca_base
```

so **`devva = devva_base + hostca - hostca_base`**. With the chiptype-0 window
(`devva_base == hostca_base == 0x80000000`) this is the identity for every host address in
`[0x80000000, 0xffffffff]` - which is the whole DRAM range the coherent allocator hands out. The
inverse `pcie_devva_to_hostca` @ `0xadec` is `hostca = devva + win[0x10] - win[0]` (same identity),
and it first range-checks `devva` against `win[0]`/`win[8]`. `oal_pcie_inbound_ca_to_va` @ `0x8dd8`
and `oal_pcie_devca_to_hostva` @ `0x6914` are the *device*-address translators (the six inbound
regions); `hostwin` does not use them. **[proven]**

The ring/descriptor path uses exactly this conversion: `pcie_ete_sr_reg_init` @ `0x14a48` loads the
ring's DMA address and calls `pcie_hostca_to_devva` before the `base` write (phase 17 A.4:
`0x14aa0: ldr r2,[r4,#0xe8]; 0x14aac: bl pcie_hostca_to_devva; 0x14ab0: str r0,[r5,#0x10]`), and
`shuangta_ete_dr_dscr_fill` writes buffer addresses as device addresses. **[proven]**

### A.2 The outbound viewport: `oal_pcie_set_outbound_by_membar` @ `0x9a38` **[proven]**

The unnamed function at `0x989c` gates on the regions-enabled flag (`host+0x30`) and the revision id
(`host+0x14`); our endpoint reports revision `0x00`, so it takes the membar path. After the six
**inbound** viewports (`iatu+0x104+0x200*i`) and `PCI_COMMAND=7`, the rev==0 branch runs a second
block driven from `host+0xc4` - the **outbound** window:

```
===== oal_pcie_set_inbound @ 0x989c, outbound block (raw disasm) =====
  0x009a38: ldr  r5, [sl, #0x14]              ; host->rev
  0x009a40: bne  #0x9c30                      ; (rev != 0 path)
  0x009a44: ldr  r4, [sl, #0x38]              ; iatu vaddr (BAR2)
  0x009a48: ldr  r6, [sl, #0x10]              ; pci_dev
  0x009a4c: ldr  r7, [sl, #0x20]              ; region descriptor array
  0x009a60: str  r5, [r4]                     ; iatu+0x00 = 0        (ctrl1 = 0)
  0x009a68: ldrb r5, [r3]                     ; desc[0]+0x48 = BAR byte
  0x009a6c: and  r5, r5, #7
  0x009a70: lsl  r5, r5, #8
  0x009a74: orr  r5, r5, #0x80000000
  0x009a80: str  r5, [r4, #4]                 ; iatu+0x04 = enable | bar
  0x009a84: ldr  r3, [sl, #0xc4]              ; <-- the window
  0x009a8c: ldr  r7, [r3]                     ; win+0x00 devva_base
  0x009a94: ldr  r5, [r3, #4]                 ; win+0x04 devva_base_hi
  0x009aa4: str  r7, [r4, #8]                 ; iatu+0x08 = base_lo = devva_base
  0x009ab0: str  r5, [r4, #0xc]               ; iatu+0x0c = base_hi
  0x009ab8: ldr  r1, [r3, #0xc]               ; win+0x0c devva_end_hi
  0x009abc: ldr  r8, [r3, #8]                 ; win+0x08 devva_end
  0x009ad0: str  r8, [r4, #0x10]              ; iatu+0x10 = limit = devva_end
  0x009ad8: ldr  r7, [r3, #0x10]              ; win+0x10 hostca_base
  0x009adc: ldr  r5, [r3, #0x14]              ; win+0x14 hostca_base_hi
  0x009ae8: str  r7, [r4, #0x14]              ; iatu+0x14 = target_lo = hostca_base
  0x009af4: str  r5, [r4, #0x18]              ; iatu+0x18 = target_hi
  0x009b00: mov  r2, #7 ; mov r1,#4 ; ...     ; pci_write_config_word(dev, 4, 7) again
```

The two `dsb`s around each store and the `dsb sy` before `PCI_COMMAND` are the vendor's ordering.
**[proven]** The vendor's own boot log names the same step:
`[oal_pcie_set_outbound_by_membar:642]PCIe outbound bus addr:0x80000000` (phase 18 C.4).

Register map (BAR2), per the same layout as the inbound viewports shifted to 0x000:

| step | offset | value | meaning |
| ---- | ------ | ----- | ------- |
| 1 | `0x000` | `0` | CTRL1 = 0 (memory region) |
| 2 | `0x004` | `(bar&7)<<8 \| 0x80000000` | CTRL2 = enable, BAR0 |
| 3 | `0x008` | `win[0x00]` | base_lo = devva_base |
| 4 | `0x00c` | `win[0x04]` | base_hi |
| 5 | `0x010` | `win[0x08]` | limit = devva_end |
| 6 | `0x014` | `win[0x10]` | target_lo = hostca_base |
| 7 | `0x018` | `win[0x14]` | target_hi |

### A.3 Live vendor boot vs a takeover **[measured]**

Live vendor boot (this task, `build/register-dumps/hostwin/000_baseline_livevendor.txt`), read
through the endpoint's BAR2 (`0x41800000`), all non-zero words:

```
004:0x80000000 008:0x80000000 010:0xFFFFFFFF 014:0x80000000     <- outbound viewport 0
104:0x80000000 108:0x40000000 110:0x401BFFFF                    <- inbound r0 ... r5 (110=b10 etc.)
304:0x80000000 308:0x401C0000 310:0x401D7FFF 314:0x00400000
504:0x80000000 508:0x401D8000 510:0x403B7FFF 514:0x01000000
704:0x80000000 708:0x403B8000 710:0x404D7FFF 714:0x40000000
904:0x80000000 908:0x404D8000 910:0x406B7FFF 914:0x02000000
b04:0x80000000 b08:0x406B8000 b10:0x408CFFFF b14:0x01200000
210/410/610/810/a10/c10/d10/e10/f10:0x00000FFF   (an unmodelled per-viewport register)
```

So the outbound viewport is `ctrl1=0, ctrl2=0x80000000, base=0x80000000, base_hi=0,
limit=0xffffffff, target=0x80000000` - exactly `win[0]`, `win[8]`, `win[0x10]`. The takeover boot
(before hostwin) reads the same block as `[0x000]=0 [0x004]=0 [0x008]=0 [0x010]=0x00000fff
[0x014]=0`, i.e. **reset/disabled**: the endpoint's bus-master translation for host addresses was
never stood up because `oal_pcie_dev_init` (which only runs when the vendor stack binds) is what
calls it. This is the concrete difference between a vendor boot and a takeover.

The DTS confirms the two roles of the SoC's iATU (`docs/soc/luofu-r116.dts`):

```
pcie@0x10160000 {
    iatu_ep = <2 0 0x80000000 0x30000000 0 0x307fffff 0xab000000 0>;   /* endpoint-side window */
    iatu_rc = <0 4 0x80000000 0x50000000 0 0x57ffffff 0 0
               1 0 0x80000000 0x40000000 0 0x47ffffff 0x40000000 0
               2 2 0x80000000 0x48000000 0 0x4fffffff 0x48000000 0>;   /* RC windows */
};
```

The endpoint's **BAR2 is `iatu_bar1`** (`/proc/iomem`: `41800000-41803fff : iatu_bar1`), i.e. the
iATU window we program is the *endpoint's*, not the RC's. The RC's own windows (`iatu_rc`) are
programmed by the `hi_pcie` platform driver (`/sys/bus/platform/devices/10160000.pcie/driver ->
hi_pcie`), which is loaded in both boots, so nothing on the RC side changes between them. **[proven
live + DTS]**

### A.4 The endpoint INTx route **[proven + measured]**

**Request.** The endpoint driver (vendor `rox_pci0` = `hi5622v100_plat.ko`) requests the number out
of `pci_dev->irq` (`struct pci_dev.irq` at `+0x184`) shared, handler `oal_pcie_intx_isr`, and the
action name it appears under in `/proc/interrupts` is `hisi_pci_intx` (the string is present once in
`plat.ko`):

```
===== do_request_irq @ 0x1081c =====
  0x010824: mov  r3, #0x80                 ; IRQF_SHARED
  0x01082c: ldr  r0, [r0, #0x184]          ; r0 = pci_dev->irq
  0x010830: movw r2, .LC0                  ; devname = "hisi_pci_intx"
  0x010838: movw r1, oal_pcie_intx_isr
  0x010840: stm  sp, {r2, ip}              ; stack: devname, dev_id = comm
  0x010848: bl   request_threaded_irq      ; (irq, isr, NULL, IRQF_SHARED, name, comm)
```

Enable is host-side only (`oal_enable_pcie_irq` -> `enable_irq`; no device register), called from
`wlan_power_on` via `bal_irq_enable` right after the firmware download. The ISR
(`oal_pcie_intx_isr` -> `oal_pcie_transfer_done`) clears the PCIe glue status, runs the ETE h2d/d2h
handlers and `pcie_intr_handle`, then returns `IRQ_HANDLED` (phase 20 A.1). **[proven]**

**Route.** The INTx is the SoC PCIe controller's `radm` interrupt. The DTS maps it per controller:

```
pcie@0x10160000 { interrupts = <0 0x3b 4 0 0x45 4>; interrupt-names = "radm" "linkdown"; };
pcie@0x10164000 { interrupts = <0 0x3f 4 0 0x46 4>; interrupt-names = "radm" "linkdown"; };
```

`0x3b` = SPI 59 -> GIC-0 IRQ 91 -> virq 207 (endpoint 0); `0x3f` = SPI 63 -> GIC-0 IRQ 95 -> virq
209 (endpoint 1). The live vendor boot:

```
lspci: Interrupt: pin A routed to IRQ 207   (MSI disabled -> INTx)
config: PCI_COMMAND=0x0006, INTERRUPT_PIN=0x01, INTERRUPT_LINE=0xcf (=207)
sysfs:  0000:00:00.0/irq=207 ; 0001:00:00.0/irq=209
/proc/interrupts:
  207: 0 0 GIC-0  91 Level hisi_pci_intx     <- endpoint 0 (this driver)
  208: 0 0 GIC-0 101 Level pcie_link_down
  209: 9038 0 GIC-0 95 Level hisi_pci_intx   <- endpoint 1 (active)
  210: 0 0 GIC-0 102 Level pcie_link_down
```

**What a takeover lacks.** In the takeover boot (`build/register-dumps/hostwin/040_testboot_state.txt`)
with the vendor modules hidden:

```
config ep0:  PCI_COMMAND=0x0006  INTERRUPT_LINE=0xff  INTERRUPT_PIN=0x01
sysfs irq:   0000:00:00.0/irq=255 ; 0001:00:00.0/irq=255
/proc/interrupts: only 208 and 210 (pcie_link_down) - no hisi_pci_intx line at all
```

`plat.ko` contains **no** config-space store to offset `0x3c`/`0x3d` (whole-module scan for
`#0x3c`/`#0x3d` finds only unrelated struct offsets), so the vendor does not write
`PCI_INTERRUPT_LINE`: the platform writes it (and sets `pci_dev->irq`) when a driver enables the
device. Two consequences, both measured:

1. Without `plat.ko` the endpoint's INTx action does not exist, yet `request_irq(207, IRQF_SHARED)`
   still returns `0` - the virq is a plain DT/GIC mapping, so it can be claimed regardless. But the
   line only fires when the **endpoint asserts INTA**; the released firmware in a takeover never
   gets that far (Part C), so `irq_taken = 0`.
2. `PCI_INTERRUPT_LINE` (config byte, what the kernel wrote for a bound driver) and `pci_dev->irq`
   (the struct field sysfs exposes) are independent: hostwin writes the config byte back to `0xcf`
   but sysfs still reads `255`, because the IRQ was never assigned to the device by an enabling
   driver. The link-down lines (`208`/`210`) remain in both boots - they belong to the RC platform
   driver `hi_pcie`, which is present in both - which is why the GIC 91 route is still wired.

### A.5 Proven vs inferred

| claim | status |
| ----- | ------ |
| host object = `chip->[4]` (0xc8 B), `host->[0xc4]` = `&.data[0x1fb8 + 0x18*chiptype]` | **proven** (`oal_pcie_host_init` 0xbf84/0xbf8c + reloc addend 0x1fb8 in `.data`) |
| window fields: devva_base 0x80000000, devva_end 0xffffffff, hostca_base 0x80000000 | **proven** (`.data+0x1fb8` dump) |
| `devva = devva_base + hostca - hostca_base`; devva end/range checks | **proven** (`pcie_hostca_to_devva`, `pcie_devva_to_hostca`) |
| with chiptype-0 window, `devva == hostca` for host DRAM | **proven** |
| outbound viewport @ BAR2+0x000 programmed from `win[]`, order ctrl1/ctrl2/base/base_hi/limit/target/target_hi | **proven** (`oal_pcie_set_inbound` 0x9a38..0x9af4) |
| live vendor outbound = base 0x80000000, limit 0xffffffff, target 0x80000000; takeover = reset | **measured** |
| endpoint BAR2 = `iatu_bar1`; RC windows (`iatu_rc`) + `hi_pcie` driver unchanged between boots | **measured** (iomem, sysfs) |
| IRQ name = `pci_dev->irq`, `IRQF_SHARED`, handler `oal_pcie_intx_isr`, action `hisi_pci_intx` | **proven** (`do_request_irq`; string + live `/proc/interrupts`) |
| endpoint 0 INTx -> GIC-0 91 -> virq 207; endpoint 1 -> GIC-0 95 -> 209 | **measured** + DTS |
| vendor does not write config 0x3c/0x3d | **proven** (whole-module store scan) |
| takeover: config line 0xff, `pci_dev->irq` 255, no `hisi_pci_intx` action, but `request_irq(207)` works | **measured** |
| what makes the endpoint *assert* INTA (device-side) | **not in `plat.ko`** (a consequence of the message/transfer dialogue) |

---

## Part B - `lab/hostwin/hostwin.c` and the test boot

`hostwin` is `rtmsg` plus the recovered window. In order:

1. **Claim / decode / firmware / release** exactly as `rtmsg` (phase 18/19/20 proven): `pci_enable_device`,
   `pci_request_mem_regions`, BAR0+BAR2 `pci_iomap`, the six inbound viewports at
   `iatu+0x104+0x200*i`, `PCI_COMMAND=7`, `FIRMWARE.bin` -> `BAR0+0x6f8000` (`diffs=0`), the
   `0x5a5a` release to `BAR0+0x3b8108`.
2. **Build the hostca->devva window** from the recovered values
   (`devvabase`/`devvaend`/`hostcabase`, defaults `0x80000000`/`0xffffffff`/`0x80000000`) and log it.
3. **Program the outbound viewport** (`outwin=1`) at BAR2+`0x000..0x018` in the vendor's order, from
   that window - the new step. Read the whole block before and after.
4. **Program the ETE SR/DR rings** as rtmsg, but with every base = `hostca_to_devva(ring dma) + acpoff`
   instead of the raw DMA address (identity with the default window, but now expressed as the
   recovered recipe).
5. **Write `PCI_INTERRUPT_LINE`** to the vendor's observed `0xcf` (=207, `hostirq`) when a takeover
   left it at `0xff` - the **one unproven-but-quoted write** - then `request_irq(207, IRQF_SHARED)`.
6. **Poll** 25 s for the mailbox words, the DR nodes, the payload buffers and a firmware-RAM window.

CI run `36885003644`, artifact `hostwin-ko`, md5 `f3fa795fc83daf9eb28169f521daf802`,
`vermagic=5.10.201 SMP mod_unload ARMv7`, 36052 bytes. Only quoted writes; the single
unproven-but-quoted write is the config line.

### B.1 The module's log (takeover boot, `build/register-dumps/hostwin/030_testboot_dmesg_full.txt`)

```
omo-hostwin: BAR0 base=0x40000000 (config space), BAR2=0x41800000 (iatu_bar1)
omo-hostwin: hostca->devva window: win[0]=0x80000000 win[8]=0xffffffff win[0x10]=0x80000000
omo-hostwin: programming six inbound viewports via BAR2 (vendor membar path)
... all six regions, every iatu write readback match=YES ...
omo-hostwin: programmed 6 viewports
omo-hostwin: cfg[0x004] <= 0x0007 readback=0x0006 MEM|MASTER=set
omo-hostwin: outbound viewport BEFORE: [0x000]=0x00000000 [0x004]=0x00000000 [0x008]=0x00000000 [0x010]=0x00000fff [0x014]=0x00000000
omo-hostwin: outbound viewport0: devva 0x80000000..0xffffffff -> host 0x80000000 (oal_pcie_set_outbound_by_membar @0x9a38)
omo-hostwin:   iatu[0x004] <= 0x80000000 readback=0x80000000 match=YES  (ob ctrl2=ena|bar0)
omo-hostwin:   iatu[0x008] <= 0x80000000 readback=0x80000000 match=YES  (ob base_lo=devva_base)
omo-hostwin:   iatu[0x010] <= 0xffffffff readback=0xffffffff match=YES  (ob limit=devva_end)
omo-hostwin:   iatu[0x014] <= 0x80000000 readback=0x80000000 match=YES  (ob target_lo=hostca_base)
omo-hostwin: outbound viewport AFTER:  [0x000]=0x00000000 [0x004]=0x80000000 [0x008]=0x80000000 [0x010]=0xffffffff [0x014]=0x80000000
omo-hostwin: decode verdict BAR0+0x6f8000: after=0x00000000 decoded=YES
omo-hostwin: SR ch0 nodes=272B dma=0x83eee000 ... DR ch3 dma=0x83db9000 payload dma=0x83f2c000 ... (7 channels)
... all SR/DR program registers readback match=YES ...
omo-hostwin: writability probe BAR0+0x6f8000: wrote 0xdeadbeef read 0xdeadbeef match=YES
omo-hostwin: verify target BAR0+0x6f8000: file=928920 bytes diffs=0 match=YES
omo-hostwin: INTx config before: PCI_INTERRUPT_LINE=255
omo-hostwin: PCI_INTERRUPT_LINE <= 207 (unproven-but-quoted, live vendor value) readback=207
omo-hostwin: request_irq(207, IRQF_SHARED, "omo-hostwin") rc=0 - IRQ path live
omo-hostwin: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
omo-hostwin: release readback = 0x00005a5a
omo-hostwin: [poll +1350ms] MBOX out[1] msg1  CA=0x40039014 0x00000000 -> 0x00000040
omo-hostwin:     bit 6 (id 6 = pcie_trigger_ete_sending_handle)
omo-hostwin: [poll +1900ms] MBOX out[1] msg1  CA=0x40039014 0x00000040 -> 0x00000004
omo-hostwin:     bit 2 (id 2 = unregistered)
omo-hostwin: done (release=1 rings=1 acpoff=0 pollms=500 polldur=25000 irq=207 irq_taken=0 irq_handled=0 msgs=2)
omo-hostwin: freed irq 207 (taken=0 handled=0)
```

The outbound viewport now equals the live vendor boot word for word (`040_testboot_state.txt`), all
six inbound regions and all seven SR/DR channel register sets read back matching, the firmware
verifies `diffs=0`, and the release reads back `0x5a5a`. **No `DR chN ... CHANGED` line and no
`payload+...` line appears anywhere in the 25 s window, and `irq_taken = 0`.**

---

## Part C - outcome

**Did a payload land?** No. The firmware still emits exactly the same two mailbox words as rtmsg -
`out[1] 0 -> 0x40` (bit 6 = `pcie_trigger_ete_sending_handle`) at +1.35 s, then `0x40 -> 0x04`
(bit 2) at +1.90 s - and no DR node or payload buffer we own changes. Programming the outbound
window therefore removed the *address-translation* gap but did not produce a transfer.

**What the firmware sent.** Only the PCIe mailbox pending word, twice, on `out[1]`
(`CA 0x40039014`). Bit 6 dispatches the vendor's `pcie_trigger_ete_sending_handle`, which is a
4-byte tail call: `0x15efc: b pcie_wkup_thread` (**[proven]**) - i.e. "wake the host's HCC receive
thread". So the device is telling the host to go look at its queues; the DR ring deposit that the
thread would read never happened. There is still no id-1 "device plat ready" word and no message
payload.

**IRQ behaviour.** `request_irq(207, IRQF_SHARED)` succeeds (`rc=0`) and is freed cleanly
(`taken=0 handled=0`). The virq exists because the GIC/DT mapping does (the RC's link-down lines
208/210 are present in both boots); the line simply never fires because the endpoint never asserts
INTA - the released firmware stops at the pending-word stage. Writing `PCI_INTERRUPT_LINE=0xcf`
makes the config byte match a vendor boot but does not populate `pci_dev->irq` (sysfs still `255`),
because only an enabling driver's platform hook assigns it. So the interrupt route is *wired* (GIC-0
91 -> 207) but has no source in a takeover.

**What remains, in dependency order:**

1. **The host half of the message service.** The device's pending word must be read, acked, cleared
   and re-armed the way `pcie_msg_handle` does (`*(ctx+0xc)=1`, `*(ctx+4)=0`, `*(ctx+0x10)=1`), then
   the registered handler must actually run. `hostwin`/`rtmsg` register a *stub* for id 6 and never
   perform that ack/re-arm, so the dialogue cannot advance past the first word. **The pending/ack/
   re-arm CAs are still not statically attributable from `plat.ko`** (they live on the per-chip `ctx`
   object the chip layer creates; phase-20a A.6, phase-20b A.1(b)) - recovering them (from
   `hi5622v100_wifi.ko`, which is out of this task's scope) is the next step.
2. **A real id-6 handler + the wake path.** `pcie_trigger_ete_sending_handle` merely calls
   `pcie_wkup_thread`; the actual DR-ring read is `pcie_rx_handle` in the HCC thread, which needs the
   per-chip callbacks (`bal cbs->rx`) the vendor binds outside `plat.ko`.
3. **The interrupt source.** Only once (1) runs and a transfer completes will the endpoint assert
   INTA; then virq 207 (already claimable) and the route (already wired) become useful.

The device-visible window itself is now recovered and reproduced; it is necessary state, and it is
no longer the thing standing between the mailbox word and a payload.

---

## Test record / Recovery

Raw evidence: `build/register-dumps/hostwin/` + `stage/`. **One takeover boot + one recovery
boot.** No panic in the takeover boot (`pstore` unchanged by it).

| file | contents |
| ---- | -------- |
| `000_baseline_livevendor.txt` | live vendor boot (this task), endpoint config/iATU, irq, radios, iomem |
| `002_disasm_outbound_0x989c.txt` | raw disasm of the unnamed `oal_pcie_set_inbound` incl. the outbound block |
| `003_disasm_addr.txt` | `pcie_hostca_to_devva` / inverse / inbound CA / devca / memmap |
| `004_disasm_window.txt` | `oal_pcie_host_init` + the `.data+0x1fb8` window dump |
| `010_staging.txt` | vendor modules hidden, `hostwin.ko` + loader + recovery installed, syntax/md5 |
| `020_testboot_cmd.txt` | the test reboot |
| `030_testboot_dmesg_full.txt` | full takeover dmesg (761 `omo-hostwin` lines) |
| `040_testboot_state.txt` | takeover state: iATU after hostwin, irq, config, pstore |
| `060_recovery_run.txt` | the recovery script run from the device |
| `070_recovery_evidence.txt` | recovered boot: modules/radios/IRQs/power params |
| `stage/` | `hostwin.ko` (md5 `f3fa795fc83daf9eb28169f521daf802`), `omo-hostwin`, `recover-hostwin.sh` |

### Baseline (live, vendor stack loaded, 2026-10-01T14:35Z)

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1
md5 wifi e21629d226ec7de9a860a8955952d311   md5 plat 23660bc285393e678d5cade1c36c194b
0000:00:00.0 irq=207 ; 0001:00:00.0 irq=209 ; both -> rox_pci0 ; /proc/interrupts hisi_pci_intx
config ep0: 0x59e7/0x0005, COMMAND=0x0006, LINE=0xcf, PIN=0x01 ; ep1 LINE=0xd1
phy0+phy1 ; 6 wlan ifaces ; br-lan 192.168.10.1/24 ; chip id:0x34
```

### Takeover boot (md5 `f3fa795fc83daf9eb28169f521daf802`)

Claim + six inbound viewports + **outbound viewport** all `match=YES`; the outbound block goes from
reset (`ctrl2=0`) to `0x80000000/0x80000000/0xffffffff/0x80000000`, byte-for-byte the vendor live
iATU; firmware write `diffs=0`; release `0x5a5a`; `PCI_INTERRUPT_LINE 0xff -> 0xcf`;
`request_irq(207, IRQF_SHARED) rc=0`; two mailbox words (`out[1]=0x40` then `0x04`); no DR/payload
change; `done (... irq=207 irq_taken=0 irq_handled=0 msgs=2)`. No panic, `pstore` unchanged.

### Recovery

`sh /root/recover-hostwin.sh` renamed the modules back and removed the module, loader, symlink and
`/tmp` copy, `sync`, `reboot`. Recovered boot (`070_recovery_evidence.txt`):

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1
md5 wifi e21629d226ec7de9a860a8955952d311   md5 plat 23660bc285393e678d5cade1c36c194b   (baseline)
0000:00:00.0 (irq 207) and 0001:00:00.0 (irq 209) both bound to rox_pci0 ; hisi_pci_intx back
phy0 + phy1 ; 6 wlan ifaces (vap0/1/3/8/9/11) ; br-lan 192.168.10.1/24 up
iwpriv Hisilicon0 get_chipid -> chip id:0x34 version:0x00
iwpriv Hisilicon0 alg get_2g_power_param -> [SUCC]17161605 17161605 ... 0a0606ff   (baseline)
iwpriv Hisilicon0 alg get_5g_power_param -> [SUCC]00000000 0004ff00 ... 0000001a   (baseline)
leftovers (hostwin.ko, .omo-off, loader, symlink): all absent
```

**Recovery verified: the router is healthy with the vendor stack restored and both radios
answering.**

### Hazard note (measured the hard way)

During Part A measurement, one read-only `devmem` probe of the SoC PCIe controller's **`misc`
window** (`0x10161000`) caused an unaligned-access data abort in `devmem`, i.e. a kernel panic and
a reboot (`pstore` blk-1/blk-2, mtime 14:37, `do_alignment_ldrstr` <- `devmem`, addr `0x10161000`).
That window is **not safe to read** from `/dev/mem`. Everything in this report was subsequently
measured through the endpoints' own BAR0/BAR2 (`0x40000000`/`0x41800000`) or from user space, and
the vendor stack was restored. The pre-existing pstore record (blk-0, 10:41) and those two records
(14:37) are the only ones; the takeover boot added none.

### Risk notes

- Writes per takeover boot: six inbound iATU viewports + the one outbound viewport + `PCI_COMMAND=7`
  + the 928,920-byte firmware + the ETE SR/DR program registers (all read back matching) + the
  phase-19b-proven `0x5a5a` release. The one **unproven-but-quoted** write is
  `PCI_INTERRUPT_LINE = 0xcf`. No host->device message reply and no write through the message
  context's `+4`/`+0xc`/`+0x10` pointers.
- The IRQ handler returns `IRQ_NONE` unless a mailbox register changed, and disables the shared
  level line after the first hit; the line was never taken, so no storm.
- Recovery - vendor modules restored and both radios verified after one reboot; `pstore` shows only
  the pre-existing record plus the two from the accidental `devmem` panic above.

### Regenerate

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko
$PY lab/ko_disasm.py $KO pcie_hostca_to_devva pcie_devva_to_hostca oal_pcie_inbound_ca_to_va \
    oal_pcie_devca_to_hostva memmap_get_dev_acp_addr memmap_get_dev_addr_from_acp_addr \
    pcie_if_hostca_to_devva oal_pcie_host_init > build/tmp/hostwin/addr2.txt
$PY build/tmp/hostwin/raw.py $KO 0x989c 0x9b20 > build/tmp/hostwin/outbound.txt
gh run download 36885003644 -n hostwin-ko -D build/tmp/hostwin-ko
```

