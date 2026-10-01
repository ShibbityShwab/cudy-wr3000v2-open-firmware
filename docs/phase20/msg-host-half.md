# msg-host-half: the host half of the message service (phase 20d, 2026-10-01)

Task `st_01a0f82c`. Direct sequel to `docs/phase20/host-window.md`, which proved and reproduced the
device->host address window (outbound iATU viewport 0 at BAR2+0x000 programmed to
`0x80000000/0xffffffff/0x80000000`, byte-for-byte a live vendor boot) yet still saw the firmware emit
only `out[1] 0x40` (bit 6 = `pcie_trigger_ete_sending_handle`) then `0x04`, with no payload and
`irq_taken = 0`. That report named the remaining blocker as the **host half of the message service**:
the ack (`*(ctx+0xc)=1`) and re-arm (`*(ctx+0x10)=1`) writes and the real id-6 handler. This phase
recovers all three from the instruction stream and implements them.

**Headline.** The ack/re-arm CAs, previously "not statically attributable", **are** attributable.
`pcie_msg_handle` @ `0x171f8` reads its handler table from `*(ctx+0x20)`, and `pcie_msg_init`
stores that table at `comm+0x4c` - so `ctx+0x20 == comm+0x4c` forces **`ctx = comm+0x2c`**, the very
array `shuangta_pcie_msg_reg_map` fills with the six mailbox CAs. Therefore, all **[proven]**:

| ctx field | ctx+off | out slot | CA | BAR0 (region 3) |
| --------- | ------- | -------- | -- | --------------- |
| pending | `+0x04` | `out[1]` | `0x40039014` | `0x3f1014` |
| ack     | `+0x0c` | `out[3]` | `0x40101438` | `0x4b9438` |
| re-arm  | `+0x10` | `out[4]` | `0x40101414` | `0x4b9414` |

The earlier phases bound ack/re-arm to `out[0]`/`out[2]` **as an inference**; that is corrected here.
The id-6 handler `pcie_trigger_ete_sending_handle` @ `0x15efc` is a 4-byte tail call to
`pcie_wkup_thread` @ `0x1629c`, whose complete action list is: set `comm+0x28 = 1` and
`__wake_up(comm+0x18)` - **no device register write at all**; it wakes the HCC receive thread, which
reads the DR ring. The interrupt route (GIC-0 91 -> virq 207, action `hisi_pci_intx`) is proven; the
vendor's ready path arms the line host-side only (`enable_irq`), and `plat.ko` contains no MMIO store
that enables the endpoint's interrupt, so what raises the line is the endpoint's own message/DMA
logic (inferred), re-armed per message by exactly the `out[4]` write implemented here.

- **Part A** (this report): the ack/re-arm CAs, the id-6 action list, and the interrupt source, each
  with disassembly and a proven/inferred mark.
- **Part B**: `lab/msghalf/msghalf.c` - hostwin plus the host half (corrected ack/clear/re-arm, the
  real id-6 handler, DR receive buffers posted), one takeover boot.
- **Part C**: whether the message was handled and answered, and what remains for the full HCC
  dialogue.

Every claim is **[proven]** (a constant/relocation/control-flow in the instruction stream, or a value
the device printed/measured) or **[inferred]**.

Artifacts (regenerable):

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko   # md5 23660bc285393e678d5cade1c36c194b
$PY lab/ko_disasm.py $KO pcie_msg_init pcie_msg_handle pcie_msg_register \
    shuangta_pcie_msg_reg_map pcie_msg_send pcie_msg_send_irq        > build/tmp/msghalf/msgcore.txt
$PY lab/ko_disasm.py $KO pcie_trigger_ete_sending_handle pcie_wkup_thread pcie_thread_init \
    pcie_process_thread pcie_thread_handle pcie_rx_handle            > build/tmp/msghalf/id6.txt
$PY lab/ko_disasm.py $KO do_request_irq oal_pcie_intx_isr oal_pcie_transfer_done \
    pcie_intr_handle oal_enable_pcie_irq pcie_irq_enable bal_irq_enable > build/tmp/msghalf/irq.txt
$PY lab/ko_disasm.py $KO oal_pci_lres_init pci_dev_res_init oal_pcie_host_init \
                                                                     > build/tmp/msghalf/chip.txt
```

---

## Part A - the ack/re-arm CAs, the id-6 handler, and the interrupt source

### A.0 What was already known

Phase-20a A.3 recovered `pcie_msg_handle`'s contract:

```
  0x017248: ldr  r3, [r4, #0xc]      ; ack register pointer
  0x017254: str  r1(=1), [r3]        ; ACK:    *(ctx+0xc) = 1
  0x017258: ldr  r3, [r4, #4]        ; pending register pointer
  0x01725c: ldr  r5, [r3]            ; pending = *(ctx+4)
  0x017260: str  r2(=0), [r3]        ; CLEAR:  *(ctx+4) = 0
  0x0172a0: ldr  r3, [r4, #0x10]     ; re-arm register pointer
  0x0172ac: str  r2(=1), [r3]        ; RE-ARM: *(ctx+0x10) = 1
  0x0172b4: rbit r6, r5 ; clz r6,r6  ; lowest set bit = message id
  0x0172d8: ldr  r3, [r4, #0x20]     ; handler table = *(ctx+0x20)
  0x0172dc: ldr  sl, [r3, r6, lsl #3]; table[id].fn
  0x0172ec: ldr  r0, [r3, #4]        ; table[id].arg
  0x0172f0: blx  sl                  ; fn(arg)
```

and concluded (phase 20a A.6, 20b A.1(b)) that *which* CAs `ctx+4/+0xc/+0x10` point at was "not
statically provable from `plat.ko`". This phase shows that conclusion was wrong: the object is
identifiable, and the ack/re-arm CAs are fixed by it.

### A.1 (a) The ack and re-arm CAs **[proven]**

**(1) `pcie_msg_init` @ `0xb6e4` fills the six-CA array at `comm+0x2c`.** The array base is `sb`, and
the chip op `get_msg_reg_map` (`.data+0x2944`/ops table) is called with it:

```
===== pcie_msg_init @ 0xb6e4 =====
  0x00b6f8: add  sb, r0, #0x2c              ; sb = comm+0x2c: the six-register array
  0x00b70c: mov  r1, sb
  0x00b710: ldr  r3, [r4, #0x70]            ; comm->ops
  0x00b718: ldr  r3, [r3, #8]               ; ops->get_msg_reg_map
  0x00b720: blx  r3                         ; shuangta_pcie_msg_reg_map(dev, comm+0x2c)
```

`shuangta_pcie_msg_reg_map` @ `0x1b1a0` maps exactly six CAs and stores their VAs into that array,
at word offsets `+0x00 .. +0x14`:

```
===== shuangta_pcie_msg_reg_map @ 0x1b1a0 =====
  0x01b1b0: movw r1, #0x9010 ; movt r1, #0x4003   ; 0x40039010 -> *(r4+0x00)  out[0]
  0x01b1cc: bl   oal_pcie_inbound_ca_to_va      ; r3 = VA
  0x01b1e4: str  r3, [r4]                          ;   out[0] = VA
  0x01b1e8: movw r1, #0x9014 ; movt r1, #0x4003   ; 0x40039014 -> *(r4+0x04)  out[1]
  0x01b208: str  r3, [r4, #0x04]
  0x01b20c: movw r1, #0x92d4 ; movt r1, #0x4003   ; 0x400392d4 -> *(r4+0x08)  out[2]
  0x01b22c: str  r3, [r4, #0x08]
  0x01b230: movw r1, #0x1438 ; movt r1, #0x4010   ; 0x40101438 -> *(r4+0x0c)  out[3]
  0x01b250: str  r3, [r4, #0x0c]
  0x01b254: movw r1, #0x1414 ; movt r1, #0x4010   ; 0x40101414 -> *(r4+0x10)  out[4]
  0x01b270: str  r3, [r4, #0x10]
  0x01b278: movw r1, #0x92f0 ; movt r1, #0x4003   ; 0x400392f0 -> *(r4+0x14)  out[5]
  0x01b28c: str  r3, [r4, #0x14]
```

**(2) `pcie_msg_init` stores the handler table at `comm+0x4c`** (kmalloc 0x58, 11 x 8-byte
`{fn,arg}`):

```
  0x00b754: ldr  r0, [r3, #0x1c]            ; kmalloc_caches[...]
  0x00b758: bl   kmem_cache_alloc_trace      ; 0x58 bytes
  0x00b764: str  r3, [r4, #0x4c]            ; comm+0x4c = handler table   <-- THE TABLE
```

**(3) `pcie_msg_handle` reads that table from `ctx+0x20`:** `0x0172d8: ldr r3, [r4, #0x20]`. Since
`comm+0x2c + 0x20 = comm+0x4c`, the only object for which `ctx+0x20` is the table is
**`ctx = comm+0x2c`**. Substituting the array word offsets:

```
  ctx+0x04  = comm+0x30 = out[1] = VA of CA 0x40039014   -> pending
  ctx+0x0c  = comm+0x38 = out[3] = VA of CA 0x40101438   -> ack
  ctx+0x10  = comm+0x3c = out[4] = VA of CA 0x40101414   -> re-arm
  ctx+0x20  = comm+0x4c           = the handler table
```

**(4) Independent cross-check - the *send* side uses the object base.** `pcie_msg_send_irq` @
`0x174a8` is invoked with `ctx = comm` and indexes the same array from 0x2c:

```
  0x0174f8: ldr  r3, [r4, #0x40]     ; comm+0x40 = out[5]  (CA 0x400392f0) -> write 8
  0x017504: str  r2(=8), [r3]
  0x017538: ldr  r2, [r4, #0x2c]     ; comm+0x2c = out[0]  (CA 0x40039010)
  0x01753c: str  r3, [r2]
  0x017544: ldr  r2, [r4, #0x34]     ; comm+0x34 = out[2]  (CA 0x400392d4) doorbell
  0x017550: str  r3, [r2]
```

and `pcie_msg_send` @ `0x160f4` (global comm, `r6 = [.LANCHOR0+4]`) uses `+0x2c` (out[0]) `+0x34`
(out[2]) `+0x44/+0x48` (its shadows). So the vendor has two call conventions on the **same** object:
the send helpers are handed `comm` and index `+0x2c..`; `pcie_msg_handle` is handed `comm+0x2c` and
indexes `+4/+0xc/+0x10`. `pcie_msg_init` binds them adjacently, which is what makes the pairing
self-evident:

```
  0x00b7d0: str  fp, [r3, #0x60]     ; chip+0x60 = pcie_msg_send_irq
  0x00b7dc: str  r4, [r3, #0x64]     ; chip+0x64 = comm            (arg for 0x60)
  0x00b7e8: str  sl, [r3, #0x68]     ; chip+0x68 = pcie_msg_handle
  0x00b7f4: str  sb, [r3, #0x6c]     ; chip+0x6c = comm+0x2c       (arg for 0x68)
```

**Answer.** The ack write goes to **`out[3]`, CA `0x40101438`** (BAR0 `0x4b9438`, region 3); the
re-arm write goes to **`out[4]`, CA `0x40101414`** (BAR0 `0x4b9414`); the pending word read and
cleared is **`out[1]`, CA `0x40039014`** - exactly the register the released firmware writes. The
values are not constants and not computed from the region table: they are the third and fourth
entries of the six-CA array itself. **[proven]**

The live vendor register dump confirms `out[3]`/`out[4]` are momentary (they read `0`
at `0x40101438`/`0x40101414` in `dumps/reg_all.txt` after init), consistent with a write-1 ack/re-arm.

### A.2 (b) The id-6 handler `pcie_trigger_ete_sending_handle` @ `0x15efc` **[proven]**

`pcie_msg_init` registers id 6 (and 7) with **arg = comm**:

```
  0x00b844: ldr  r0, [r4, #0x54]                       ; 0 -> global table
  0x00b848: mov  r3, r4                                ; arg = comm
  0x00b84c: movw r2, pcie_trigger_ete_sending_handle
  0x00b854: mov  r1, #6
  0x00b858: bl   pcie_msg_register
```

The handler is 4 bytes - a tail call with the same argument:

```
===== pcie_trigger_ete_sending_handle @ 0x15efc size=4 =====
  0x015efc: b  pcie_wkup_thread
```

`pcie_wkup_thread` @ `0x1629c` - the complete action list:

```
===== pcie_wkup_thread @ 0x1629c =====
  0x0162a0: subs r4, r0, #0
  0x0162a4: beq  #0x162c4                ; NULL arg -> log only
  0x0162a8: mov  r2, #1
  0x0162ac: str  r2, [r4, #0x28]         ; (1) comm+0x28 = 1   ("ETE send pending")
  0x0162b0: add  r0, r4, #0x18           ; (2) waitqueue head comm+0x18
  0x0162b4: mov  r1, r2                  ; (3) state = TASK_NORMAL (1)
  0x0162bc: mov  r3, #0
  0x0162c0: b    __wake_up               ; (4) __wake_up(comm+0x18, TASK_NORMAL, 0, NULL)
```

So the full action list is: **write no device register**; set the flag at `comm+0x28` to 1 and wake
the task(s) on the waitqueue at `comm+0x18`. That waitqueue is created by `pcie_thread_init`
@ `0x170b8` (`__init_waitqueue_head(comm+0x18)`, `comm+0x28 = 0`), which also starts
`pcie_process_thread` @ `0x16efc` (kthread handle stored at `comm+0x24`):

```
===== pcie_process_thread @ 0x16efc =====
  0x016f1c: add  r6, r7, #0x28           ; r7 = comm; r6 = &comm+0x28 (the flag)
  0x016f34: bl   kthread_should_stop
  0x016f44: bl   pcie_wait_condtion      ; test comm+0x28
  0x016f54: add  r8, r7, #0x18           ; wait on comm+0x18
  0x016f78: bl   prepare_to_wait_event
  0x016f80: mov  r0, r6 ; bl pcie_wait_condtion
  0x016f24: ldr  r3, [r7] ; ldr r3, [r3] ; ldr r0, [r3]
  0x016f30: bl   pcie_thread_handle      ; the receive work
```

`pcie_thread_handle` -> `pcie_rx_handle` @ `0x164d0` reads the DR ring (`ldr r2,[r6,#0x10]` DR base,
`ldr r3,[r6,#8]` depth, stride `0x6c`), takes an upload buffer and hands it to the BAL `cbs->rx`.

**Answer.** The id-6 handler itself does exactly two host-RAM things: `comm+0x28 = 1` and
`__wake_up(comm+0x18)`. It expects the DR ring to hold (or about to hold) a device deposit; the
woken thread is the actual reader. **[proven]** (the handler, the flag, the waitqueue); the
downstream ring read is **[proven structure]** (phase-17 A.5 + this dump).

### A.3 (c) How the interrupt source is armed, and whether we can reach it

**The route [proven + measured].** The endpoint's INTx (pin A, MSI disabled) is the SoC PCIe
controller's `radm` interrupt:

```
docs/soc/luofu-r116.dts:
  pcie@0x10160000 { interrupts = <0x0 0x3b 0x4 0x0 0x45 0x4>;
                    interrupt-names = "radm" "linkdown"; };

live vendor boot:
  lspci: Interrupt: pin A routed to IRQ 207
  /proc/interrupts: 207: 0 0 GIC-0 91 Level hisi_pci_intx
```

`0x3b` = SPI 59 -> GIC-0 IRQ 91 -> virq 207. The action name is `hisi_pci_intx`, registered by the
endpoint driver itself: `do_request_irq` @ `0x1081c` reads the number from `pci_dev->irq`
(`PCI_INTERRUPT_LINE`) and calls `request_threaded_irq(irq, oal_pcie_intx_isr, NULL, IRQF_SHARED,
"hisi_pci_intx", comm)`:

```
  0x010820: mov  ip, r1                 ; dev_id = comm
  0x010824: mov  r3, #0x80              ; IRQF_SHARED
  0x01082c: ldr  r0, [r0, #0x184]       ; pci_dev->irq
  0x010830: movw r2, .LC0               ; "hisi_pci_intx"
  0x010838: movw r1, oal_pcie_intx_isr
  0x010844: mov  r2, #0                 ; thread_fn = NULL
  0x010848: bl   request_threaded_irq
```

**The enable is host-side only [proven].** `oal_enable_pcie_irq` @ `0x7d5c` is `enable_irq(pci_dev->irq)`
and nothing else; `pcie_irq_enable` @ `0x7fb0` walks the registered per-IRQ contexts, and
`bal_irq_enable` is called from `wlan_power_on` right after the firmware download
(`0x00e5f8: bl bal_irq_enable`). No device register is written to enable the line.

**`plat.ko` has no MMIO store that arms the endpoint's interrupt [proven negative].** A whole-module
scan finds the glue status at `+0x2ec` **read** in `pcie_intr_handle` @ `0x82e4`
(`0x008304: ldr r4, [r3, #0x2ec]`), and the only `+0x2ec`/`+0x2e8` *stores* are into software config
structs (stores at `0x1c4c`/`0x1fd4` inside `hwifi_config_init` @ `0x19a0`, and `0x37fc`/`0x3800`
inside `original_value_for_dts_params` @ `0x31e0`), not MMIO. The six message CAs appear **only** in `shuangta_pcie_msg_reg_map`. So there is no
"interrupt-enable register write" in the ready path at all.

**What raises the line [inferred].** Since the host does not program a device-side enable, the assert
must come from the endpoint's own message/DMA logic: the chip raises `radm` when it has a pending
message or completes a host-directed DMA, and the host's per-message **ack (`out[3]=1`)** clears the
source while **re-arm (`out[4]=1`)** re-enables the next one. Those two writes are the only host
writes to the message/glue interface in the entire ready path. This is consistent with the live
counts (endpoint 1, carrying traffic, has ~10-22k interrupts on virq 209; the idle endpoint 0 has 0).

**Reachable from our module?** Yes for the route and the registers:

- `request_irq(207, IRQF_SHARED)` succeeds (`rc=0`) because the DT/GIC mapping exists; in a takeover
  `/proc/interrupts` shows no `207` action because the endpoint driver (which owns `hisi_pci_intx`)
  is hidden. **[measured, phase 20a/20b/20c]**
- The ack/re-arm CAs are the `out[3]`/`out[4]` VAs we already map (region 3, BAR0 `0x4b9438`/
  `0x4b9414`). **[proven]**
- Whether writing them makes the endpoint assert (and thus whether the line's absence in the
  takeover is just "no payload yet" or a missing device-side arm) is exactly what Part B's boot
  measures. **[open]**

### A.4 Proven vs inferred

| claim | status |
| ----- | ------ |
| `pcie_msg_init` fills the six-CA array at `comm+0x2c` via `get_msg_reg_map` | **proven** (`0xb6f8`/`0xb720` + `shuangta_pcie_msg_reg_map`) |
| the handler table (kmalloc 0x58) is stored at `comm+0x4c` | **proven** (`0xb764`) |
| `pcie_msg_handle` reads the table at `*(ctx+0x20)` | **proven** (`0x172d8`) |
| therefore `ctx = comm+0x2c` | **proven** (arithmetic `0x2c+0x20 == 0x4c`) |
| pending = `out[1]` CA `0x40039014`; ack = `out[3]` CA `0x40101438`; re-arm = `out[4]` CA `0x40101414` | **proven** |
| send helpers use `ctx = comm` and index `+0x2c/+0x34/+0x40` (out[0]/out[2]/out[5]) | **proven** (`pcie_msg_send_irq`, `pcie_msg_send`) |
| id-6 handler = `b pcie_wkup_thread`; arg = comm | **proven** (`0x15efc`, `0xb848`) |
| `pcie_wkup_thread`: `comm+0x28 = 1`, `__wake_up(comm+0x18, TASK_NORMAL)`; no device write | **proven** (`0x162ac`..`0x162c0`) |
| the woken thread (`pcie_process_thread`) reads the DR ring | **proven structure** (phase-17 A.5 + `pcie_thread_handle`) |
| INTx pin A -> GIC-0 91 -> virq 207; action `hisi_pci_intx`; `IRQF_SHARED` | **proven + measured** |
| enable is host-side `enable_irq` only | **proven** (`oal_enable_pcie_irq`) |
| `plat.ko` has no MMIO interrupt-enable store | **proven** (whole-module scan) |
| what raises the line is the endpoint's message/DMA logic, cleared by ack and re-enabled by re-arm | **inferred** |
| reachable from the module: request line + write ack/re-arm CAs | **proven** reachable; effect **measured in Part B** |

---

## Part B - `lab/msghalf/msghalf.c` and the test boot

`msghalf` is `hostwin` plus the host half. It keeps every proven step of `hostwin` - claim
(`pci_enable_device` + `pci_request_mem_regions`; refuses if the vendor stack holds the regions),
the six inbound iATU viewports, the outbound (device->host) viewport, `PCI_COMMAND=7`,
`FIRMWARE.bin` -> BAR0 `0x6f8000` with a `diffs=0` read-back, the ETE SR/DR program registers, and the
`0x5a5a` release to CA `0x40000108` - and changes exactly these things:

1. **Corrected ack/re-arm binding.** The message context (`ctx = comm+0x2c`) now binds
   `pending = out[1]`, `ack = out[3]`, `re-arm = out[4]` (Part A.1) instead of hostwin's inferred
   `out[0]`/`out[2]`. This is the module's `omo_msgctx_build`.
2. **The host half proper (`omo_msg_service`).** On the first non-zero `out[1]` - from the ISR *and*
   from the poll loop, because a takeover may never deliver the INTx - it performs the vendor's
   `pcie_msg_handle` sequence with read-backs: `ack` (`out[3] <= 1`), read `out[1]`, clear
   (`out[1] <= 0`), re-arm (`out[4] <= 1`), then dispatch every set bit through the handler table.
3. **A real id-6 handler (`omo_id6_handler`).** Registered for ids 6 and 7, it reproduces
   `pcie_trigger_ete_sending_handle`/`pcie_wkup_thread`: set the send flag (`comm+0x28 = 1`), log the
   `__wake_up(comm+0x18)`, and run the receive path (read the DR ring / payloads).
4. **DR receive buffers posted (`omo_post_dr`).** `shuangta_ete_dr_dscr_fill` @ `0x1765c` semantics:
   each DR node's word0 is set to the payload buffer's device VA (hostca->devva); word1 is left 0 for
   the device. Host memory only, so it does not count as a device write.

The six message registers, the six iATU viewports, the ETE registers, `PCI_COMMAND`, the firmware and
the release are quoted from the disassembly; the only **unproven-but-quoted** write remains
`PCI_INTERRUPT_LINE = 0xcf` (207). No other speculative write exists, and the single boot's writes
are: the six inbound viewports, the outbound viewport, `PCI_COMMAND=7`, the 928,920-byte firmware,
the seven SR/DR program registers (+ the `+0x2e8` RMW), the config line, the `0x5a5a` release, and -
new here - the ack/re-arm pair per message (twice).

CI run `36888723682` (the cancelled+rerun one), artifact `msghalf-ko`, md5
`34e3839e2b364c61c3118c9ae15d06ed`, `vermagic=5.10.201 SMP mod_unload ARMv7`, 38808 bytes.

### B.1 The module's log (takeover boot, `030_testboot_dmesg_full.txt`)

```
[   39.332189] omo-msghalf: msg ctx @ bf94d3a8 (pcie_msg_init @0xb6e4; ctx = comm+0x2c):
[   39.340051] omo-msghalf:   ctx+0x2c = c9771010  CA=0x40039010  out[0] H2D mask
[   39.347400] omo-msghalf:   ctx+0x30 = c9771014  CA=0x40039014  out[1] pending
[   39.354589] omo-msghalf:   ctx+0x34 = c97712d4  CA=0x400392d4  out[2] doorbell
[   39.361848] omo-msghalf:   ctx+0x38 = c9839438  CA=0x40101438  out[3] ack
[   39.368761] omo-msghalf:   ctx+0x3c = c9839414  CA=0x40101414  out[4] re-arm
[   39.375840] omo-msghalf:   ctx+0x40 = c97712f0  CA=0x400392f0  out[5] send irq
[   39.383104] omo-msghalf:   ctx+0x04 pending -> out[1] CA 0x40039014 (PROVEN)
[   39.390319] omo-msghalf:   ctx+0x0c ack     -> out[3] CA 0x40101438 (PROVEN)
[   39.397490] omo-msghalf:   ctx+0x10 re-arm  -> out[4] CA 0x40101414 (PROVEN)
[   39.404532] omo-msghalf:   ctx+0x20 handler table = c1a27e80 (11 x {fn,arg}, kmalloc 0x58; == comm+0x4c)
[   39.414010] omo-msghalf:   id6 handler = omo_id6_handler+0x0/0x54 [msghalf] (pcie_trigger_ete_sending_handle)
...
[   39.951598] omo-msghalf: DR ch3 posted 32 nodes word0=0x83b3f000 (payload dma 0x83b3f000 -> devva; dr_dscr_fill @0x1765c)
[   39.962574] omo-msghalf: DR ch4 posted 32 nodes word0=0x84c4f000 ...
[   39.973556] omo-msghalf: DR ch5 posted 32 nodes word0=0x84c59000 ...
[   39.984535] omo-msghalf: DR ch6 posted 32 nodes word0=0x84c5b000 ...
...
[   41.357048] omo-msghalf: INTx config before: PCI_INTERRUPT_LINE=255 (IRQ pin from cfg[0x3d])
[   41.382241] omo-msghalf: request_irq(207, IRQF_SHARED, "omo-msghalf") rc=0 - IRQ path live
[   41.468703] omo-msghalf: [svc post0 +480ms] pending out[1] CA=0x40039014 = 0x00000040
[   41.476508] omo-msghalf:   pending            = 0x00000040
[   41.476513] omo-msghalf:     bit 6 (id 6 = pcie_trigger_ete_sending_handle)
[   41.488995] omo-msghalf: [svc post0] ACK   out[3] CA=0x40101438 <= 0x00000001 readback=0x00000000
[   41.497835] omo-msghalf: [svc post0] CLEAR out[1] CA=0x40039014 0x00000040 -> 0x00000000 readback=0x00000000
[   41.507709] omo-msghalf: [svc post0] REARM out[4] CA=0x40101414 <= 0x00000001 readback=0x00000000
[   41.516657] omo-msghalf: [svc post0] dispatch id=6 fn=omo_id6_handler+0x0/0x54 [msghalf] arg=00000006
[   41.525930] omo-msghalf: [id6] pcie_trigger_ete_sending_handle @0x15efc -> pcie_wkup_thread @0x1629c
[   41.535113] omo-msghalf: [id6]   comm+0x28 <= 1 (send flag); __wake_up(comm+0x18) HCC rx thread
[   41.543850] omo-msghalf: [id6]   receive path: reading the DR ring (pcie_rx_handle @0x164d0)
...
[   42.448871] omo-msghalf: [svc poll +1460ms] pending out[1] CA=0x40039014 = 0x00000004
[   42.456917] omo-msghalf:     bit 2 (id 2 = unregistered)
[   42.467749] omo-msghalf: [svc poll] ACK   out[3] CA=0x40101438 <= 0x00000001 readback=0x00000000
[   42.476743] omo-msghalf: [svc poll] CLEAR out[1] CA=0x40039014 0x00000004 -> 0x00000000 readback=0x00000000
[   42.486603] omo-msghalf: [svc poll] REARM out[4] CA=0x40101414 <= 0x00000001 readback=0x00000000
[   42.495599] omo-msghalf: [svc poll] id=2 has no handler (unregistered)
[   77.081081] omo-msghalf: done (release=1 rings=1 acpoff=0 pollms=500 polldur=25000 irq=207 irq_taken=0 irq_handled=0 msgs=1 services=2 sendflag=1)
[   77.094532] omo-msghalf: freed irq 207 (taken=0 handled=0)
```

All seven SR/DR channel register sets and the `+0x2e8` RMW read back `match=YES`; the outbound
viewport reads `0x80000000/0x80000000/0xffffffff/0x80000000`; the firmware verifies `diffs=0`; the
release reads `0x5a5a`. **The host half ran twice** (`services=2`): the id-6 word was acked, cleared,
re-armed and dispatched to the real handler (`sendflag=1`), and the id-2 word was acked/cleared/
re-armed with no handler. The acks/re-arms read back `0` (write-1 self-clearing, as the live vendor
dump shows). **No `DR chN node[...] CHANGED` line and no `payload+...` line appears anywhere in the
25 s, and `irq_taken = 0`** - the ISR never fired (the poll loop is what serviced the words).

---

## Part C - outcome

**Was the message handled?** Yes - for the first time the host half executed end to end. The firmware
wrote `out[1] = 0x40` (bit 6 = `pcie_trigger_ete_sending_handle`); the module read it, wrote the ack
(`out[3]` CA `0x40101438 <= 1`), cleared the pending word (`out[1] <= 0`), wrote the re-arm (`out[4]`
CA `0x40101414 <= 1`), and dispatched id 6 to the real handler, which set the send flag and ran the
receive path (`services=2 sendflag=1`). The same sequence serviced the follow-up id-2 word (no
handler). So the ack/re-arm/id-6 gap named by phase 20c is closed: those writes now happen, with the
recovered CAs, on every pending word.

**Was it answered?** No, and nothing in the vendor design says it should be: the id-6 handler is a
wake, not a reply (`pcie_wkup_thread` writes no device register - Part A.2), and the vendor's ready
path contains no host->device write. No reply was fabricated.

**Did the payload land?** No. Zero DR node changes, zero payload-buffer changes, in the whole 25 s;
the DR buffers we posted were never touched. `irq_taken = 0` - the endpoint never asserted INTx. The
firmware's visible output is **unchanged from `hostwin`**: `out[1] = 0x40` then `0x04`, then silence.
The two words and their timing match phase 20c; servicing them did not advance the dialogue.

**This is a negative result for the phase-20c hypothesis that the ack/re-arm was *the* blocker.** The
module performed exactly the vendor's ack / clear / re-arm / dispatch, with the recovered CAs, and the
firmware's observable behaviour did not change at all. So the missing piece is downstream of the
ack/re-arm, not the ack/re-arm itself.

**What remains for the full HCC dialogue, in dependency order:**

1. **The interrupt source.** `request_irq(207, IRQF_SHARED)` succeeds and the route (GIC-0 91 -> 207,
   `hisi_pci_intx`) is wired, but the endpoint never asserts INTA in a takeover, so `irq_taken = 0` in
   every phase-20 lane. This boot narrows it: the two writable message CAs (`out[3]` ack, `out[4]`
   re-arm) do not arm the line either (they self-clear and no interrupt follows). If the assert needs
   a device-side enable, it is a register not yet identified - and it is not written anywhere in
   `plat.ko` (Part A.3). **[inferred]**
2. **The receive machinery behind the id-6 wake.** The vendor handler only wakes `pcie_process_thread`,
   which reads the DR ring through the BAL `cbs->rx` callback and dispatches skbs through
   `hcc_msg_process`/the HCC chip table. A takeover has none of those host structures, so even a
   deposited payload would have no consumer here. **[proven structure, not built]**
3. **The DR producer state.** Our posted DR nodes (word0 = payload buffer dev VA) were never touched.
   The host-side `pcie_ete_dr_reg_init` sets the DR pointer register (`DR+0x38 = [inst+0x1c]`, written
   as `0` here); whether the engine also needs that producer index advanced - or an explicit
   "buffers available" write - before it will DMA a receive is **not determined** by this boot. It is
   the most likely next thing to try, and it is host-ring state, not a device register. **[inferred]**
4. **The full HCC command dialogue.** The id-1 "Device plat ready!" message, the 414-entry command
   table and the SR send path (`pcie_msg_send`: pending -> `out[0]`, doorbell `out[2] |= 1`) are not
   built; without a receive path there is nowhere for a payload to land and no reason to send one.

---

## Test record / Recovery

Raw evidence: `build/register-dumps/msghalf/` + `stage/`. **One takeover boot + one recovery boot.**
No panic in the takeover boot (`pstore` unchanged by it: blk-0/1/2 mtimes `10:41`/`14:37`/`14:37`, all
pre-test).

| file | contents |
| ---- | -------- |
| `000_baseline.txt` | live vendor boot at start: modules/md5, drivers, irq 207/209, `hisi_pci_intx`, config, radios, pstore |
| `010_staging.txt` | vendor modules hidden, `msghalf.ko` + loader + recovery installed, md5, syntax |
| `020_testboot_cmd.txt` | the takeover reboot |
| `030_testboot_dmesg_full.txt` | full takeover dmesg |
| `031_testboot_evidence.txt` | the `omo-msghalf` lines only |
| `040_testboot_state.txt` | takeover state: iATU after msghalf, mailbox regs, config, irq, pstore |
| `060_recovery_run.txt` | the recovery script run from the device |
| `070_recovery_evidence.txt` | recovered boot: modules/radios/IRQs/power params |
| `stage/` | `msghalf.ko` (md5 `34e3839e2b364c61c3118c9ae15d06ed`), `omo-msghalf`, `recover-msghalf.sh` |
| `../tmp/msghalf/*.txt` | the Part-A disassembly dumps |

### Baseline (live, vendor stack loaded, `000_baseline.txt`)

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1
md5 wifi e21629d226ec7de9a860a8955952d311   md5 plat 23660bc285393e678d5cade1c36c194b
0000:00:00.0 irq=207 ; 0001:00:00.0 irq=209 ; both -> rox_pci0 ; /proc/interrupts hisi_pci_intx
config ep0: COMMAND=0x0006, LINE=0xcf, PIN=0x01
phy0+phy1 ; 6 wlan ifaces ; br-lan 192.168.10.1/24 ; chip id:0x34
pstore blk-0/1/2 mtimes 10:41 / 14:37 / 14:37 (all pre-test)
```

### Takeover boot (md5 `34e3839e2b364c61c3118c9ae15d06ed`)

The six inbound viewports and the outbound viewport all `match=YES`; the outbound reads
`0x80000000/0x80000000/0xffffffff/0x80000000`; all seven SR/DR register sets + the `+0x2e8` RMW
`match=YES`; four DR channels posted (32 nodes each); firmware `diffs=0`; release `0x5a5a`;
`PCI_INTERRUPT_LINE 0xff -> 0xcf`; `request_irq(207)` `rc=0`. The host half ran twice
(`services=2 sendflag=1`): id 6 acked/cleared/re-armed/dispatched, id 2 acked/cleared/re-armed. No DR
node/payload change, no ISR, `done (... irq_taken=0 irq_handled=0 msgs=1)`.

Takeover state (`040_testboot_state.txt`): only `msghalf` loaded (no `rox_pci0`), sysfs irq 255,
`/proc/interrupts` shows only the two link-down lines (208/210), iATU outbound and inbound programmed,
all six mailbox regs read `0` after the service, config line `0xcf`, `pstore` unchanged.

### Recovery

`sh /root/recover-msghalf.sh` renamed the modules back and removed the module, loader, symlink and
`/tmp` copy, `sync`, `reboot`. Recovered boot (`070_recovery_evidence.txt`):

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1
md5 wifi e21629d226ec7de9a860a8955952d311   md5 plat 23660bc285393e678d5cade1c36c194b   (baseline)
0000:00:00.0 (irq 207) and 0001:00:00.0 (irq 209) both bound to rox_pci0 ; hisi_pci_intx back
phy0 + phy1 ; 6 wlan ifaces (vap0/1/3/8/9/11) ; br-lan 192.168.10.1/24 up
iwpriv Hisilicon0 get_chipid -> chip id:0x34 version:0x00
iwpriv Hisilicon0 alg get_2g_power_param -> [SUCC]17161605 17161605 ... 0a0606ff   (baseline)
iwpriv Hisilicon0 alg get_5g_power_param -> [SUCC]00000000 0004ff00 ... 0000001a   (baseline)
leftovers (msghalf.ko, .omo-off, loader, symlink, /tmp copy): all absent
pstore: no new record (blk-0/1/2 mtimes 10:41/14:37/14:37, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored and both radios
answering.**

### Hazard note

The phase-20c hazard stands: a read-only `devmem` of the RC `misc` window (`0x10161000`) panicked the
box. Every measurement here was made through the endpoints' own BAR0/BAR2 (`0x403f1010`..., the
region-3 IO window `0x404b94xx`, and BAR2 `0x418000xx`) or from user space. No panic this lane.

### Risk notes

- Writes per takeover boot: six inbound iATU viewports + the one outbound viewport + `PCI_COMMAND=7` +
  the 928,920-byte firmware (all read back) + the seven SR/DR program registers + the phase-19b
  release (`0x5a5a`), plus - new - the **ack/re-arm pair per message** (`out[3] <= 1`, `out[4] <= 1`),
  which are the vendor's own `pcie_msg_handle` writes and read back self-clearing (`0`). The one
  **unproven-but-quoted** write is `PCI_INTERRUPT_LINE = 0xcf`. DR node posting writes only our own
  coherent host memory.
- The IRQ handler now services the message (ack/clear/re-arm) instead of disabling the line; it only
  disables if it keeps firing after servicing (>64 hits), which never happened (`irq_taken=0`).
- Recovery - vendor modules restored and both radios verified after one reboot; `pstore` unchanged.

### Regenerate

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko
# Part A dumps (see the header) land in build/tmp/msghalf/.
gh run download 36888723682 -n msghalf-ko -D build/tmp/msghalf-ko
# stage: scp stage/{msghalf.ko,omo-msghalf,recover-msghalf.sh} to /tmp, then /tmp/stage-msghalf.sh
```
