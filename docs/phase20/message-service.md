# message-service: the interrupt / HCC message path and the first real host (phase 20, 2026-10-01)

Task `st_01a0f7e5`. Direct sequel to `docs/phase19/fw-handshake.md`, which proved the boot-time
takeover can load `FIRMWARE.bin` into the chip, release it with `0x5a5a` (CA `0x40000108`), and
observe exactly one mailbox word (`out[1]` CA `0x40039014` = `0x4` at +1.85 s) - but could not
service it, because the takeover had no IRQ and no message reader.

This phase does two things. **Part A** recovers the vendor's interrupt and message-service path from
`hi5622v100_plat.ko` and answers the specific questions: how the IRQ is requested/enabled, what the
ISR does, how it decides a message is pending, how the six mailbox CAs are used, how the doorbell is
cleared, how a message payload is fetched, and what the host writes to ack or reply. **Part B**
builds a real host (`lab/msgd/msgd.c`) that claims the endpoint, decodes the window, loads the
firmware, releases it, **requests the endpoint INTx line** and polls the mailbox, draining and
logging every word. **Part C** reports what the firmware said and what remains.

**Headline:** the IRQ/message machinery is fully recovered and the host now requests the endpoint's
interrupt line (`request_irq(207, IRQF_SHARED)` succeeds), but in the takeover the released firmware
still emits **exactly one** mailbox word (`out[1]` `0`->`0x4`, bit 2) and **never raises the
interrupt** (0 taken in 25 s). The handshake does not advance: no id-1 message, no second word, no
payload. The reason is proven in Part A.4/A.6 - the vendor's pipeline needs the host-side message
context (six mapped CAs + the ETE SR/DR rings + the HCC queues) that `pcie_ete_init`/`pcie_msg_init`
allocate at runtime, and the endpoint's INTx is **not allocated at all** until the vendor stack's own
IRQ/domain setup runs. A takeover that hides the vendor stack can therefore observe the chip's first
sign but cannot complete the dialogue; the one word is the reachable edge.

- Disassembly artifacts (regenerate below): `build/tmp/phase20/{irq_msg_core,isr2,dispatch,rx_path,ete_path,all}.txt`.
- Module: `lab/msgd/msgd.c` (+ `lab/msgd/Makefile`); CI run `36878628652`, artifact `msgd-ko`,
  md5 `9d954410d3b04c97ad1019f4b41e0724`, vermagic `5.10.201 SMP mod_unload ARMv7`, 23440 bytes.
- Raw evidence: `build/register-dumps/msgd/` (baseline, staging, two test boots, recovery).
- Vendor object: `build/register-dumps/teardown/hi5622v100_plat.ko` md5 `23660bc285393e678d5cade1c36c194b`.

---

## Part A - the interrupt and message-service path (`hi5622v100_plat.ko`)

### A.0 Method

`lab/ko_disasm.py` disassembles ARM functions with relocation annotations. The full per-function
dumps used here are in `build/tmp/phase20/`; the whole-object dump is `all.txt` (583 functions):

```
PY=../pyenv/Scripts/python.exe
$PY lab/ko_disasm.py build/register-dumps/teardown/hi5622v100_plat.ko \
    oal_pcie_probe_irq_init do_request_irq oal_enable_pcie_irq pcie_irq_enable bal_irq_enable \
    oal_pcie_intx_isr oal_pcie_transfer_done pcie_intr_handle pcie_msg_handle pcie_msg_init \
    pcie_msg_send pcie_msg_send_irq pcie_msg_register pcie_msg_wait_for_clr shuangta_pcie_msg_reg_map \
    pcie_ete_intr_init pcie_rx_handle pcie_ete_d2h_isr_handle hcc_msg_process
```

`movw`/`movt` immediates are printed raw (the `.LANCHOR` addend is a linker relocation, so a
`.LANCHOR0` reference without an explicit `add` is not a trustworthy offset); every offset quoted
below is either a literal in the instruction or a `ldr [reg, #imm]` field, so it is exact.

### A.1 How the IRQ is requested and enabled **[proven]**

The endpoint's IRQ is **requested** by the platform driver and **enabled host-side only** - there is
no device register anywhere in the enable path. `oal_pcie_probe_irq_init` @ `0x698`:

```
  0x00069c: mov  r6, r0                 ; r6 = comm (per-IRQ context)
  0x0006a0: mov  r5, r1                 ; r5 = pci_dev
  0x0006ac: ldr  r1, [r1, #0x184]       ; printk: "pci_dev->irq" (struct pci_dev.irq at +0x184)
  0x0006b4: ldr  r7, [r6, #0x1c]        ; already registered?
  0x0006bc: bne  #0x6e8
  0x0006c0: mov  r1, r6
  0x0006c4: mov  r0, r5
  0x0006c8: bl   do_request_irq         ; do_request_irq(pci_dev, comm)
  0x0006cc: subs r4, r0, #0
  0x0006e4: str  r7, [r6, #0x10]        ; [comm+0x1c] was the "registered" flag (0 on success)
```

`do_request_irq` @ `0x1081c`:

```
  0x010820: mov  ip, r1                 ; ip = comm  -> dev_id
  0x010824: mov  r3, #0x80              ; irqflags = IRQF_SHARED
  0x01082c: ldr  r0, [r0, #0x184]       ; r0 = pci_dev->irq
  0x010830: movw r2, .LC0               ; devname
  0x010838: movw r1, oal_pcie_intx_isr  ; primary handler
  0x010840: stm  sp, {r2, ip}           ; stack args: devname, dev_id=comm
  0x010844: mov  r2, #0               ; thread_fn = NULL
  0x010848: bl   request_threaded_irq   ; request_threaded_irq(irq, oal_pcie_intx_isr, NULL,
                                        ;                        IRQF_SHARED, name, comm)
```

So: the number comes from `pci_dev->irq` (which the platform has already written into
PCI config `PCI_INTERRUPT_LINE`, offset 0x3c); it is registered **shared**; there is no register
programmed on the device.

Enabling is the same story. `oal_enable_pcie_irq` @ `0x7d5c`:

```
  0x007da4: ldr  r3, [r4, #0x1c]        ; irq-registered flag
  0x007dac: beq  #0x7dbc
  0x007db4: str  r3(=0), [r4, #0x10]    ; clear the "disabled" state
  0x007df4: ldr  r3, [r4, #0x18]        ; r3 = pci_dev
  0x007df8: ldr  r0, [r3, #0x184]       ; r0 = pci_dev->irq
  0x007dfc: bl   enable_irq             ; <-- host-side only, NO device write
```

`pcie_irq_enable` @ `0x7fb0` walks the registered per-IRQ contexts and calls `oal_enable_pcie_irq`
on each; `bal_irq_enable` @ `0x10c78` is the thin BAL wrapper that tail-calls the registered bus
callback (`0x010c8c: ldr r3,[r3,#0x30]; 0x010c98: bx r3`). Its **only caller in the ready path** is
`wlan_power_on` @ `0xe46c`, at `0x00e5f8: mov r0,#0; bl bal_irq_enable` - i.e. the vendor enables the
IRQ immediately after the firmware download, before waiting for the ready messages. **[proven]**

**Answer to "config space? device register? both?":** the *number* is taken from PCI config space
(`pci_dev->irq` == `PCI_INTERRUPT_LINE`); the *enable* is a host-side `enable_irq` only. No device
register is touched to enable or disable the interrupt. **[proven]**

### A.2 The ISR chain **[proven]**

The registered handler is `oal_pcie_intx_isr` @ `0x854c`. It is deliberately thin - it just calls the
transfer-done worker and maps its result to an irqreturn:

```
  0x008550: mov  r0, r1                 ; r0 = dev_id (comm)
  0x008558: bl   oal_pcie_transfer_done
  0x00855c: cmp  r0, #0
  0x008560: blt  #0x856c                ; < 0 -> error/log/disable/link-state path
  0x008564: mov  r0, #1                 ; return IRQ_HANDLED
```

`oal_pcie_transfer_done` @ `0x83e4` reads the **PCIe glue status** register, clears it, then fans out
to the ETE and message handlers:

```
  0x008404: ldr  r3, [r4]               ; comm->[0] (chip/host object)
  0x00840c: ldr  r3, [r3, #0xc]
  0x008418: ldr  r5, [r3, #8]           ; status dword
  0x008464: bic  r5, r5, #0xff000000    ; mask off, keep the device status bits
  0x008468: bic  r5, r5, #0xe00000
  0x00846c: bic  r5, r5, #0xf800
  0x008474: bic  r5, r5, #0xf8
  0x008480: ldr  r3, [r0, #4]
  0x008484: orr  r3, r3, r5
  0x008488: str  r3, [r0, #4]           ; write-back = clear the bridge status
  0x008490: bl   pcie_ete_h2d_isr_handle
  0x0084a0: bl   pcie_ete_d2h_isr_handle ; RX/tx-done completion dispatch
  0x0084a8: bl   pcie_intr_handle       ; the PCIe message-register dispatch
```

`pcie_intr_handle` @ `0x82e4` is a second, PCIe-level status dispatch: it reads
`[[comm+4]+0] + 0x2ec`, masks to bits `0x3d8` (`{3,4,6,7,8,9}`), and calls `handler[lowest set bit]`
from a table at `comm+0x48` (arg at `comm+0x4c`):

```
  0x008304: ldr  r4, [r3, #0x2ec]       ; status
  0x00830c: ands r4, r4, #0x3d8         ; bits 3,4,6,7,8,9
  0x008328: rbit r5, r4
  0x00832c: clz  r5, r5                 ; lowest set bit
  0x00833c: ldr  r2, [r3, #0x48]        ; handler[bit].fn
  0x008348: ldr  r0, [r3, #0x4c]        ; handler[bit].arg
  0x00834c: blx  r2
```

**How the ISR decides a message is pending [proven]:** two levels. (1) `oal_pcie_transfer_done`
reads the PCIe glue status (`+0x2ec`) and tests the masked bits; (2) the message dispatcher
`pcie_msg_handle` (below) reads a **one-bit-per-message pending mask** and dispatches the lowest set
bit.

### A.3 The message dispatcher `pcie_msg_handle` @ `0x171f8` **[proven]**

```
  0x017248: ldr  r3, [r4, #0xc]         ; ack register pointer
  0x01724c: mov  r1, #1
  0x017254: str  r1, [r3]               ; ACK:          *(ctx+0xc) = 1
  0x017258: ldr  r3, [r4, #4]           ; pending register pointer
  0x01725c: ldr  r5, [r3]               ; pending mask  = *(ctx+4)
  0x017260: str  r2(=0), [r3]           ; CLEAR:        *(ctx+4) = 0
  0x0172a0: ldr  r3, [r4, #0x10]        ; re-arm register pointer
  0x0172ac: str  r2(=1), [r3]           ; RE-ARM:       *(ctx+0x10) = 1
  0x0172b4: rbit r6, r5
  0x0172b8: clz  r6, r6                 ; lowest set bit = message id
  0x0172bc: cmp  r6, #0xa               ; ids 0..10 only
  0x0172d8: ldr  r3, [r4, #0x20]        ; handler table
  0x0172dc: ldr  sl, [r3, r6, lsl #3]   ; handler[id].fn
  0x0172e0: add  r3, r3, r6, lsl #3
  0x0172ec: ldr  r0, [r3, #4]           ; handler[id].arg
  0x0172f0: blx  sl
```

So a message is pending when the word read from `*(ctx+4)` is non-zero; the lowest set bit is the
message id; the host clears that register, writes **1** to the ack register (`ctx+0xc`) and **1** to
the re-arm register (`ctx+0x10`), then runs the registered handler. **[proven]**

`ctx` here is a runtime per-chip object (`pcie_msg_init` stores the handler table/fn pointers into
`chip[i]+0x60..0x6c`). The three register *pointers* (`ctx+4` pending, `ctx+0xc` ack, `ctx+0x10`
re-arm) are host VAs assigned from the chip's resource table at runtime - the module never names
those CAs, so **which mailbox CA is the ack is not statically provable** (see A.6).

### A.4 The six mailbox CAs and how they are used **[proven]**

`shuangta_pcie_msg_reg_map` @ `0x1b1a0` is the *only* place the six CAs appear; it maps each CA to a
host VA and stores the six pointers into a caller array:

```
  0x01b1b0: movw r1, #0x9010 ; movt r1, #0x4003   ; 0x40039010 -> [r4+0x00]
  0x01b1e8: movw r1, #0x9014 ; movt r1, #0x4003   ; 0x40039014 -> [r4+0x04]
  0x01b20c: movw r1, #0x92d4 ; movt r1, #0x4003   ; 0x400392d4 -> [r4+0x08]
  0x01b230: movw r1, #0x1438 ; movt r1, #0x4010   ; 0x40101438 -> [r4+0x0c]
  0x01b254: movw r1, #0x1414 ; movt r1, #0x4010   ; 0x40101414 -> [r4+0x10]
  0x01b278: movw r1, #0x92f0 ; movt r1, #0x4003   ; 0x400392f0 -> [r4+0x14]
```

`pcie_msg_init` passes `ctx+0x2c` as that array, so the message context holds
`out[0..5]` at `+0x2c..+0x40`:

| slot | device CA | msg ctx | BAR0 off | role (proven) |
| ---- | --------- | ------- | -------- | ------------- |
| out[0] | `0x40039010` | `+0x2c` | `0x3f1010` | H2D pending/message mask: `pcie_msg_send` writes the pending bitmap here |
| out[1] | `0x40039014` | `+0x30` | `0x3f1014` | message register 1 - **the register the released chip writes**; zeroed by `pcie_msg_init` |
| out[2] | `0x400392d4` | `+0x34` | `0x3f12d4` | **doorbell / trigger**: `pcie_msg_send`/`_irq` OR bit 0 |
| out[3] | `0x40101438` | `+0x38` | `0x4b9438` | MAC-side message register |
| out[4] | `0x40101414` | `+0x3c` | `0x4b9414` | MAC-side message register |
| out[5] | `0x400392f0` | `+0x40` | `0x3f12f0` | message register 5: `pcie_msg_send_irq` writes `8` |

`pcie_msg_init` @ `0xb6e4` builds the context:

```
  0x00b708: ldr  r2, [r4]               ; ctx->[0] (chip)
  0x00b70c: mov  r1, sb                 ; sb = ctx+0x2c (the reg-pointer array)
  0x00b710: ldr  r3, [r4, #0x70]        ; chip->ops
  0x00b714: ldr  r2, [r2]
  0x00b718: ldr  r3, [r3, #8]           ; chip->get_msg_reg_map
  0x00b71c: ldr  r0, [r2, #4]
  0x00b720: blx  r3                     ; -> shuangta_pcie_msg_reg_map(dev, ctx+0x2c)
  0x00b738: str  r8(=0), [r3]           ; *out[0] = 0
  0x00b740: str  r8(=0), [r3]           ; *out[1] = 0
  0x00b754: kmalloc(0x58) -> [ctx+0x4c] ; handler table (11 x 8-byte {fn,arg})
  0x00b7d0: str  pcie_msg_send_irq, [chip+0x60]
  0x00b7dc: str  ctx,                [chip+0x64]
  0x00b7e8: str  pcie_msg_handle,   [chip+0x68]
  0x00b7f4: str  ctx+0x2c,          [chip+0x6c]
  0x00b820: pcie_msg_register(..., 1, pcie_dev_ready_msg_handle, chip0)
  0x00b854: pcie_msg_register(..., 6, pcie_trigger_ete_sending_handle, ctx)
  0x00b880: pcie_msg_register(..., 7, pcie_trigger_ete_sending_handle, ctx)
  0x00b8b8: pcie_msg_register(..., 3, pcie_ete_transfer_done_handle, chip0)
```

`pcie_msg_register` @ `0x15fbc` writes `table[id].fn` and `table[id].arg`:

```
  0x015ffc: ldr  r5, [r1, #0x4c]        ; handler table
  0x016004: strne r2, [r5, r4, lsl #3]  ; table[id].fn  = fn
  0x01600c: strne r3, [r4, #4]          ; table[id].arg = arg
```

**Host -> device submit** `pcie_msg_send` @ `0x160f4` (pending bitmap -> `out[0]`, doorbell ->
`out[2]`):

```
  0x016184: orr  r3, r3, r1, lsl r4     ; set bit id in the pending word
  0x016194: ldr  r2, [r6, #0x2c]        ; out[0]
  0x01619c: str  r3, [r2]               ; *out[0] = pending bitmap
  0x0161a4: ldr  r2, [r6, #0x34]        ; out[2] doorbell
  0x0161a8: ldr  r3, [r2]
  0x0161ac: orr  r3, r3, r1             ; r1 = 1
  0x0161b0: str  r3, [r2]               ; *out[2] |= 1  (ring the doorbell)
```

`pcie_msg_send_irq` @ `0x174a8` (`out[5] = 8`, then flush):

```
  0x0174f8: ldr  r3, [r4, #0x40]        ; out[5]
  0x0174fc: mov  r2, #8
  0x017504: str  r2, [r3]               ; *out[5] = 8
  0x017508: bl   pcie_msg_wait_for_clr  ; wait for out[0] to be cleared by the device
  0x01753c: str  r3, [r2]               ; *out[0] = pending
  0x017550: str  r3, [r2]               ; *out[2] |= 1  (doorbell)
```

**How the doorbell bit is cleared [proven set / inferred clear]:** the host only ever **sets** it
(`*out[2] |= 1`). It is cleared by the **device** when it consumes the message - the host waits for
the matching `out[0]` to read back 0 in `pcie_msg_wait_for_clr` @ `0x173dc`:

```
  0x017424: ldr  r3, [r4]
  0x01742c: ldr  r5, [r3]               ; read *out[0]
  0x017438: subs r4, r4, #1             ; bounded spin (0xfffe)
  0x017440: ldr  r5, [r3]
  0x017448: bne  ...                    ; until it reads 0
```

The ISR's clear at `*(ctx+4)=0` (A.3) is the *receive* pending word, not the doorbell.

### A.5 How a message payload is fetched - ETE SR/DR rings, not the mailbox **[proven structure]**

The mailbox words are only doorbells/bitmaps; the payload itself is DMA'd into **host-allocated
memory** through the ETE engine's two descriptor rings (`SR` = host->device send, `DR` =
device->host receive). The rings and the message context are created by `pcie_ete_init` @ `0x7820`
from the per-chip resource (`get_pcie_ete_res`) and bound by `pcie_ete_intr_init` @ `0x7528`:

```
  0x007540: str  r0, [r1, #0x1c]                       ; ete->+0x1c = pcie_rx_handle
  0x00754c: strd r2, r3, [r1, #0x20]                   ; +0x20 = pcie_tx_done_handle, +0x24 = rx_err
  0x007558: str  r3, [r1, #0x28]                       ; +0x28 = pcie_ete_tx_err_handle
  0x00758c: bl   oal_pcie_inbound_ca_to_va             ; map the ETE register block
  0x00759c: str  r3, [r4, #0xc]                        ; ete->+0xc = block VA
  0x0075b8: str  r2, [r3]                              ; clear the block's IRQ bits (& 0xffe0f8f8)
```

The device->host receive path `pcie_rx_handle` @ `0x164d0` reads the DR ring (base `ete+0x10`,
depth `ete+8`, per-channel stride `0x6c`), takes an upload buffer, and hands it to the BAL/HCC rx
callback which builds the skb:

```
  0x01651c: ldr  r2, [r6, #0x10]        ; DR ring base (host VA)
  0x016528: ldr  r3, [r6, #8]           ; depth
  0x016544: mul  r3, r3, #0x6c          ; channel stride
  0x01655c: bl   pcie_ete_dr_get_uploadbuf
  0x0165a8: ldr  r3, [r3, #0x40]        ; [LANCHOR2+0x40] = bal cbs
  0x0165ac: ldr  r2, [r3, #4]           ; cbs->rx
  0x0165b0: ldr  r0, [r3, #0x2c]
  0x0165b4: blx  r2                     ; rx(handle, buf) -> skb
  0x0165bc: bl   pcie_wkup_thread       ; wake the HCC rx thread
```

`pcie_ete_d2h_isr_handle` @ `0x15c1c` walks the completion handler sets `[ete+0x1c]`/`[ete+0x24]`
(`rbit`/`clz` over a status mask, `blx` per set bit). The HCC rx thread then dispatches the skb by
its header, `hcc_msg_process` @ `0x1204c`:

```
  0x012058: ldr  r2, [r0, #0x118]       ; skb->data (the HCC message)
  0x012084: ldrb r3, [r2]               ; selector byte 0
  0x01208c: and  r3, r3, #0xf           ; core/group index
  0x01209c: ldrh r2, [r2, #6]           ; message id (u16 @ +6)
  0x0120a8: ldr  r3, [r1, r3]           ; per-core/group handler table
  0x0120b0: ldr  r3, [ip, #4]
  0x0120b8: adds r3, r3, r2, lsl #4     ; record stride 0x10
  0x0120c0: ldr  r3, [r3, #4]
  0x0120cc: bx   r3                     ; handler(msg)
```

The two "ready" handlers live in the **HCC chip table** registered by `plat_init_bal_hcc_excp`
@ `0xe380` -> `hcc_msg_register_tab_chip(chip=4, table=.data+0x2660, count=5)`, with ids 1
(`device_plat_ready_msg_process`, "Device plat ready! chip id : %d") and 2
(`host_ready_msg_process`, "DEVICE READY"). They read the payload from `msg->data` (`ldr
r3,[r0,#0x118]`), check source core `byte[1]>>4 == 0`, copy `len-0xc` (<= 0x62) bytes from `msg+0xc`
into a 0x64-stride record, and `complete()` the two waits in `multi_chip_loading` (phase 19 Part A).

**[proven]** The payload travels on the ETE DR ring into host memory; the mailbox CAs carry only
doorbell/bitmap words. **[proven]** With the vendor stack hidden there is no `get_pcie_ete_res`
context, no ring base and no mapped message block, so there is no device-side message ring for the
takeover to read - only the six mailbox words.

### A.6 What the host writes to ack or reply **[proven: none in the ready path]**

The ready-handshake path contains **no host->device write**. The vendor's acts are: `request_irq`
(A.1), `enable_irq` (A.1), wait #1, read-only `dev_status_check`, wait #2, read-only
`dev_status_check`. The ISR's ack/re-arm writes (`*(ctx+0xc)=1`, `*(ctx+0x10)=1`, clear `*(ctx+4)=0`,
A.3) target the runtime message context; the six CAs are named only in
`shuangta_pcie_msg_reg_map`, and that map fills `ctx+0x2c..0x40`, **not** `ctx+4/+0xc/+0x10`. So the
ack CA is **not statically attributable**. The only writes with a known CA are the ETE *submit*
operations (`pcie_msg_send`: `out[0] = pending`, `out[2] |= 1`; `pcie_msg_send_irq`: `out[5] = 8`),
which are not a reply to the ready handshake.

`msgd` therefore performs **no** host->device write beyond the phase-19b-proven `0x5a5a` release.
An unproven write to one of the six CAs could clobber a live control/status register (the
"one panic = one reboot" hazard) for no proven benefit, so it is deliberately not attempted.

### A.7 Proven vs inferred

| claim | status |
| ----- | ------ |
| IRQ number comes from `pci_dev->irq` (`PCI_INTERRUPT_LINE`), requested `IRQF_SHARED`, `thread_fn=NULL` | **proven** (`do_request_irq`) |
| enable is `enable_irq(pci_dev->irq)`, host-side only, no device register | **proven** (`oal_enable_pcie_irq`) |
| `bal_irq_enable` is called from `wlan_power_on` right after the firmware download | **proven** (`0xe5f8`) |
| ISR = `oal_pcie_intx_isr` -> `oal_pcie_transfer_done` (clear glue status) -> ETE h2d/d2h + `pcie_intr_handle` | **proven** |
| | |
| `pcie_msg_handle` acks `*(ctx+0xc)=1`, reads+clears `*(ctx+4)`, re-arms `*(ctx+0x10)=1`, dispatches lowest set bit | **proven** |
| the six CAs map to `out[0..5]` = msg ctx `+0x2c..+0x40` | **proven** (`shuangta_pcie_msg_reg_map` + `pcie_msg_init`) |
| `pcie_msg_send`: pending -> `out[0]`, doorbell `out[2] |= 1`; `pcie_msg_send_irq`: `out[5] = 8` | **proven** |
| the doorbell is set by the host and cleared by the device (host waits on `out[0]`) | **proven set / inferred clear** |
| payload is DMA'd via the ETE DR ring into host memory, then skb -> `hcc_msg_process` -> HCC chip table | **proven structure** (phase 17 + this dump) |
| ack/pending/re-arm `ctx+0xc/+4/+0x10` map to specific mailbox CAs | **not provable from the module** (runtime ctx) |
| any host->device write in the ready handshake | **none proven** |
| the mailbox word `out[1]=0x4` is the id-2 "DEVICE READY" HCC message | **inferred** (bit 2 = id 2; but the PCIe table registers only ids 1/3/6/7, so id 2 has no PCIe-level handler) |

---

## Part B - `lab/msgd/msgd.c`

The module reuses the phase-18/19 proven path and then adds a host:

1. **Claim** the endpoint (`pci_enable_device`, `pci_request_mem_regions`; refuses if the vendor
   stack is loaded), BAR0 base from config space.
2. **Decode** the six inbound iATU viewports via BAR2 (`0x104 + 0x200*i`), every write read back;
   `PCI_COMMAND = 7`.
3. **Load** `FIRMWARE.bin` to CA `0x01240000` (BAR0 `0x6f8000`), verify read-back (`diffs=0`).
4. **Request the IRQ** - the proven vendor step: read `PCI_INTERRUPT_LINE`, `request_irq(irq,
   omo_irq_handler, IRQF_SHARED, "omo-msgd", dev_id)`. The handler reads all six mailbox CAs and
   returns `IRQ_HANDLED` only for the first observed change (then `disable_irq_nosync`, because the
   line is shared and level - the device source is never cleared, so a storm is possible); otherwise
   `IRQ_NONE`. `useirq=0` or an invalid line falls back to polling.
5. **Release**: `iowrite32(0x5a5a, BAR0+0x3b8108)` (CA `0x40000108`), read back. This is the only
   device write beyond the phase-18 set.
6. **Drain + log** for 25 s (500 ms polls): all six mailbox regs (raw word + per-bit decode with the
   vendor id names) and 12 status regs, plus a 384 KiB firmware-RAM window diffed against the
   pre-release snapshot.
7. **No handshake reply**: A.6 proves none, so none is written.

Parameters: `domain=0 program=1 release=1 useirq=1 irq=0 pollms=500 polldur=25000
scanbase=0x7d8000 scanlen=0x60000`. CI run `36878628652`, artifact `msgd-ko`, md5
`9d954410d3b04c97ad1019f4b41e0724`, 23440 bytes.

### B.1 Boot A - IRQ auto-detect from config space (`irq=0`)

```
[   40.896547] omo-msgd: INTx config: PCI_INTERRUPT_LINE=255 requested irq=255
[   40.903549] omo-msgd: request_irq(255, IRQF_SHARED) rc=-22 - polling only
[   40.910372] omo-msgd: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
[   40.917715] omo-msgd: release readback = 0x00005a5a
...
[   42.369619] omo-msgd: [poll +1840ms] MBOX out[1] msg1   CA=0x40039014 0x00000000 -> 0x00000004
[   42.378643] omo-msgd:     bit 2 (id 2 = unregistered)
...
[   76.225083] omo-msgd: done (... irq=-1 irq_taken=0 irq_handled=0 msgs=1)
```

Config space reports `PCI_INTERRUPT_LINE = 0xff` (sysfs `irq` = 255): in the takeover the endpoint
has **no IRQ line assigned at all**, so `request_irq(255)` is rejected (`-22`, no such irq). Polling
still catches the word.

### B.2 Boot B - the vendor's number passed explicitly (`irq=207`)

`207` is the endpoint's IRQ in the recovered (vendor-loaded) state, from config `PCI_INTERRUPT_LINE`
and `/proc/interrupts`:

```
[   41.096753] omo-msgd: INTx config: PCI_INTERRUPT_LINE=255 requested irq=207
[   41.103781] omo-msgd: request_irq(207, IRQF_SHARED, "omo-msgd") rc=0 - IRQ path live
[   41.111507] omo-msgd: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
[   41.118785] omo-msgd: release readback = 0x00005a5a
...
[   42.559988] omo-msgd: [poll +1840ms] MBOX out[1] msg1   CA=0x40039014 0x00000000 -> 0x00000004
[   42.569008] omo-msgd:     bit 2 (id 2 = unregistered)
...
[   76.323114] omo-msgd: done (... irq=207 irq_taken=0 irq_handled=0 msgs=1)
```

**The IRQ path is now live at the API level** (`request_irq(207, IRQF_SHARED)` returns 0) - the
deliverable's "enable the interrupt path" is met at the host. But the handler takes **zero**
interrupts in 25 s, and `/proc/interrupts` still lists no `207` action: the descriptor accepts the
request, but the endpoint's INTx is not routed to a handler in the takeover. The write to `out[1]` is
the only device-visible event, and it is caught only by polling. **[observed]** That the endpoint's
INTx needs the vendor stack to be hosted/routed (rather than being usable from a clean-boot takeover)
is the **inferred** reading; what is proven is only rc=0 and 0 interrupts.

**Why 207 is not the natural config-space value in the takeover:** the vendor-loaded baseline shows
endpoint `0000:00:00.0` -> irq `207` (`hisi_pci_intx`, GIC-0 91) and `PCI_INTERRUPT_LINE = 0xcf`.
Hiding the two vendor modules removes the `hisi_pci_intx` action from `/proc/interrupts` entirely
and leaves `PCI_INTERRUPT_LINE = 0xff`: **the vendor's IRQ number exists only because the vendor
stack itself requests/hosts it.** `request_irq(207)` succeeds on the (still valid) irq descriptor,
but nothing drives that line for this endpoint in the takeover.

---

## Part C - outcome

**How many messages arrived:** exactly **one**. In both boots `out[1]` (`CA 0x40039014`) went
`0x00000000 -> 0x00000004` at **+1.84 s** after the release write, and never changed again over the
remaining ~23 s. The other five mailbox registers (`0x40039010`, `0x400392d4`, `0x400392f0`,
`0x40101414`, `0x40101438`) stayed `0` throughout. `msgs=1`.

**Decoded meaning:** the raw word is `0x4` on message register 1. Under the vendor's
one-bit-per-message convention (`pcie_msg_send_irq` writes `8` = bit 3 for id 3; `pcie_msg_handle`
dispatches the lowest set bit) it is **bit 2 = PCIe message id 2**. Note the PCIe-level handler table
(`pcie_msg_init`) registers ids 1, 3, 6, 7 only - **id 2 has no handler** ("unregistered"), so even
under the vendor stack this bit would fall through the PCIe dispatcher. The **inferred** reading
(phase 19) is that id 2 is the HCC `host_ready_msg_process` ("DEVICE READY"), but that id is a
*software* message id carried in the payload header on the ETE ring, not the mailbox bit; the two are
different numbering spaces. What is **proven** is only: one HCC mailbox word, `out[1] = 4`.

**Did the handshake advance?** No. There is no id-1 "Device plat ready!" word and no second word, no
payload deposit, and no interrupt. `multi_chip_loading`'s wait #1 (200 jiffies on `.LANCHOR0+0x234`)
would still time out; nothing consumed or acked the single word. The firmware is running (BSS zeroed
98278/98304 words, `dcoldo_vset 0xffffffff -> 0x260d4184`, `pbank_code -> 0x313`, live
`abank_code`/`tcxo_pll_*`) - it emits its first sign and stops, because the host half of the message
service (rings, message context, handler tables, IRQ) is absent.

**What remains for the full HCC dialogue / driver commands:**

1. **A real endpoint IRQ.** The vendor number is only valid because the vendor stack hosts it; a
   clean-boot takeover must either bring up the platform INTx domain itself (what
   `oal_pcie_probe`/`oal_pcie_host_init` do) or find and program the device-side interrupt-enable
   that makes the chip assert INTx. `request_irq(207)` returns 0 but no interrupt is delivered and
   `/proc/interrupts` shows no 207 action, so the word does not reach a handler this way.
2. **The host message context and ETE rings.** `pcie_msg_init` (six CAs + handler table + the
   `ctx+4/+0xc/+0x10` ack/pending/re-arm pointers) and `pcie_ete_init` (SR/DR ring bases, depths and
   the mapped ETE block from `get_pcie_ete_res`) must be stood up, so there is somewhere for a
   payload to land and a reader/acker to run.
3. **The handler tables.** The PCIe table (ids 1/3/6/7) and the HCC chip table (chip 4: id 1 =
   `device_plat_ready_msg_process`, id 2 = `host_ready_msg_process`) must be registered so the
   id-1/id-2 handlers can complete `multi_chip_loading`'s two waits and fill
   `g_dmac_to_hmac_read_msg[]`.
4. **Only then** a host->device reply (and the full ALG/HMAC/WAL command dialogue, the 414-entry
   command table) becomes meaningful.

Until (1)-(3) exist on the host, the mailbox read is the whole reachable handshake - which is what
this phase measured.

---

## Test record / Recovery

Raw evidence: `build/register-dumps/msgd/` (gitignored) + `stage/`.

| file | contents |
| ---- | -------- |
| `000_baseline.txt` | live router, vendor stack loaded, no reboot |
| `010_staging.txt` | vendor modules hidden, `msgd.ko` + loader + recovery installed, syntax/md5 |
| `020_reboot_cmd.txt`, `021_reboot_cmd.txt` | the two test reboots |
| `030_testboot_evidence.txt` | full `omo-msgd` log, boot A (`irq=0`) |
| `031_testboot2_evidence.txt` | full `omo-msgd` log, boot B (`irq=207`) |
| `050_post_test_state.txt`, `051_post_test2_state.txt` | post-test state after each boot |
| `060_recovery_run.txt` | the recovery script run from the device |
| `070_recovery_evidence.txt` | recovered boot: modules/radios/IRQs |
| `080_final_health.txt` | final radios, power params, hostapd |
| `stage/` | `msgd.ko`, `omo-msgd`, `omo-msgd-irq0`, `omo-msgd-irq207`, `recover-msgd.sh` |

**Boots: two test boots + one recovery boot.** The only device write on each test boot was the
phase-19b-proven `0x5a5a` release; Part A.6 proves there is no host answer to write, so none was
attempted. No panic; `/sys/fs/pstore` kept its pre-test mtimes (`10:41`/`10:26`/`10:34`) throughout.

### Baseline (live, vendor stack loaded)

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1
md5 wifi e21629d226ec7de9a860a8955952d311   md5 plat 23660bc285393e678d5cade1c36c194b
0000:00:00.0 irq=207 ; 0001:00:00.0 irq=209 ; both -> rox_pci0 ; /proc/interrupts hisi_pci_intx
phy0+phy1 ; 6 wlan ifaces ; br-lan 192.168.10.1/24
pstore blk-0/2/3 mtimes 10:41 / 10:26 / 10:34 (all pre-test)
```

### Staging

Vendor modules renamed `.ko.omo-off`; `msgd.ko` (md5 `9d954410d3b04c97ad1019f4b41e0724`) installed;
`/etc/init.d/omo-msgd` symlinked `S99` (the loader deletes its own symlink before `insmod`, so a hang
watchdog-reboots into a reachable boot with no `msgd`); `/root/recover-msgd.sh` installed. Both
scripts pass `sh -n`; `md5sum /lib/modules/5.10.201/msgd.ko` matches the artifact.

### Test boot A (`irq=0`)

Claim + six iATU viewports all `match=YES`; `PCI_COMMAND=7` reads back `0x0006`; firmware write
verifies (`diffs=0`); release readback `0x00005a5a`. `PCI_INTERRUPT_LINE=255`, `request_irq(255)`
rc `-22` -> polling. One message (log in B.1). `msgd` stayed loaded (`24576 0`), no panic, pstore
unchanged.

### Test boot B (`irq=207`)

Same claim/decode/write/release. `request_irq(207, IRQF_SHARED)` rc `0` - IRQ path live - but
`irq_taken=0`; one message (log in B.2). No panic, pstore unchanged.

### Recovery

`sh /root/recover-msgd.sh` (staged, run from the device) renamed the modules back and removed the
module, loader, symlink and `/tmp` copy, `sync`, `reboot`. Recovered boot:

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1
md5 wifi e21629d226ec7de9a860a8955952d311   md5 plat 23660bc285393e678d5cade1c36c194b   (baseline)
0000:00:00.0 (irq 207) and 0001:00:00.0 (irq 209) both bound to rox_pci0 ; hisi_pci_intx back
phy0 + phy1 ; 6 wlan ifaces (vap0/1/11/3/8/9) ; hostapd+softapd running
iwpriv Hisilicon0 get_chipid -> chip id:0x34 version:0x00
iwpriv Hisilicon0 alg get_2g_power_param -> [SUCC]17161605 17161605 ... 0a0606ff   (baseline)
iwpriv Hisilicon0 alg get_5g_power_param -> [SUCC]00000000 0004ff00 ... 0000001a   (baseline)
br-lan 192.168.10.1/24 up
leftovers (msgd.ko, .omo-off, loader, symlink, /tmp copy, /root/recover-msgd.sh): all absent
pstore: no new record (blk-0/2/3 mtimes 10:41/10:26/10:34, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored and both radios
answering.**

### Risk notes

- The six iATU viewport writes + `PCI_COMMAND=7` + the 928,920-byte firmware write - the
  phase-18-proven path; every write read back.
- The `0x5a5a -> CA 0x40000108` release - the phase-19b-proven act, taken once per boot (twice in
  total across the two test boots).
- `request_irq` was requested shared and the handler returns `IRQ_HANDLED` only for a mailbox change,
  then disables the line (level/shared, source never cleared) to avoid a storm; otherwise `IRQ_NONE`.
- All handshake work was **reads only** apart from the release: Part A.6 proves no host answer
  exists in the ready path, so none was attempted.
- Recovery - vendor modules restored and both radios verified after one reboot; pstore unchanged.

### Regenerate

```
PY=../pyenv/Scripts/python.exe
$PY lab/ko_disasm.py build/register-dumps/teardown/hi5622v100_plat.ko \
    oal_pcie_probe_irq_init do_request_irq oal_enable_pcie_irq pcie_irq_enable bal_irq_enable \
    oal_pcie_intx_isr oal_pcie_transfer_done pcie_intr_handle pcie_msg_handle pcie_msg_init \
    pcie_msg_send pcie_msg_send_irq pcie_msg_register pcie_msg_wait_for_clr \
    shuangta_pcie_msg_reg_map pcie_ete_intr_init pcie_rx_handle pcie_ete_d2h_isr_handle \
    hcc_msg_process
gh run download 36878628652 -n msgd-ko -D /tmp/msgd-ko
```
