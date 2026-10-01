# runtime-msg: the runtime message context, the ETE rings and the interrupt route (phase 20b, 2026-10-01)

Task `st_01a0f7ff`. Direct sequel to `docs/phase20/message-service.md`, which proved the whole
interrupt/message path in `hi5622v100_plat.ko` but stopped at the reachable edge: in a clean-boot
takeover the released firmware writes exactly one mailbox word (`out[1]`, CA `0x40039014`,
`0 -> 4`) and never raises an interrupt, because the host half of the message service - the runtime
message context that `pcie_msg_init` builds, the ETE SR/DR rings that `pcie_ete_init` builds, and a
routed INTx - does not exist.

This phase answers the three gaps the parent task names:

- **Part A** (this document): what the runtime message context *is* (where it is allocated, its
  fields, the `+4`/`+0xc`/`+0x10` pending/ack/re-arm pointers, and how it is bound to the six
  mailbox CAs); what `pcie_msg_init`/`pcie_ete_init` set up (rings, descriptors, handler tables,
  interrupts); how the endpoint's interrupt reaches the host in a vendor boot (and why the takeover
  has none); and the DR ring descriptor layout that carries a payload to host memory. Every claim is
  disassembly or a live-vendor read, marked **[proven]** or **[inferred]**.
- **Part B**: `lab/rtmsg/rtmsg.c` - claim, decode, build the message context and rings, request the
  IRQ, load and release the firmware, and log the pending word, the ISR, the DR-ring buffers and the
  decoded message.
- **Part C**: what arrived, what the IRQ did, whether the handshake advanced, and what remains.

Artifacts used here (regenerable; `lab/ko_disasm.py`):

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko   # md5 23660bc285393e678d5cade1c36c194b
$PY lab/ko_disasm.py $KO pcie_msg_init pcie_msg_register pcie_msg_send pcie_msg_send_irq \
    pcie_msg_wait_for_clr pcie_msg_handle shuangta_pcie_msg_reg_map        > build/tmp/rtmsg/msg.txt
$PY lab/ko_disasm.py $KO pcie_ete_init pcie_ete_rings_init pcie_ete_intr_init \
    pcie_ete_init_src_ring pcie_ete_init_dst_ring pcie_ete_chn_res \
    pcie_get_ete_addr get_pcie_ete_res pcie_ete_deinit                     > build/tmp/rtmsg/ete_init.txt
$PY lab/ko_disasm.py $KO pcie_ete_dr_reg_init pcie_ete_dr_init pcie_ete_sr_reg_init \
    pcie_ete_sr_init pcie_ete_dr_get_uploadbuf pcie_ete_rcv_buff_check \
    pcie_ete_d2h_isr_handle pcie_ete_h2d_isr_handle pcie_rx_handle \
    pcie_tx_done_handle                                                    > build/tmp/rtmsg/ete_ring.txt
$PY lab/ko_disasm.py $KO shuangta_ete_sr_get_nodesize shuangta_ete_dr_dscr_fill \
    shuangta_ete_sr_get_dscr_addr shuangta_ete_sr_get_dscr_len \
    shuangta_ete_sr_get_dscr_flag shuangta_ete_sr_get_dr_dscr_addr \
    shuangta_ete_dr_get_dscr_addr shuangta_ete_dr_get_sr_dscr_flag \
    shuangta_ete_dr_set_sr_dscr_flag shuangta_ete_dr_get_sr_dscr_addr \
    shuangta_ete_dr_get_sr_dscr_len shuangta_ete_sr_node_init_handle \
    shuangta_ete_dr_get_sr_nodesize shuangta_ete_sr_dscr_fill \
    shuangta_ete_dr_node_init_handle shuangta_get_ete_priv_res \
    pcie_ete_get_chn_cfg                                                   > build/tmp/rtmsg/ete_desc.txt
$PY lab/ko_disasm.py $KO oal_pcie_probe oal_pcie_host_init oal_pcie_dev_init pcie_main_init \
    pcie_init_static_res oal_pcie_probe_irq_init do_request_irq oal_enable_pcie_irq \
    oal_pcie_transfer_done oal_pcie_intx_isr pcie_intr_handle bal_irq_enable \
    wlan_power_on                                                          > build/tmp/rtmsg/irq.txt
$PY lab/ko_disasm.py $KO pcie_hostca_to_devva pcie_devva_to_hostca \
    memmap_get_dev_addr_from_acp_addr oal_pcie_inbound_ca_to_va \
    oal_pcie_devca_to_hostva pcie_dev_addr_to_caller_addr                  > build/tmp/rtmsg/addr.txt
$PY lab/ko_disasm.py $KO pcie_thread_init pci_dev_res_init pcie_init_static_res \
    pcie_init_default get_pci_chip_res                                     > build/tmp/rtmsg/comm.txt
```

The live vendor boot (`0000:00:00.0` = 2.4 GHz endpoint, `0001:00:00.0` = 5 GHz) supplied the config
space, `/proc/interrupts`, `lspci -vv` and `sysfs` values quoted below.

---

## Part A - the context, the rings and the interrupt route

### A.0 The object model in one paragraph

`pcie_main_init` @ `0x704` runs once per PCIe controller. Its per-controller context is a **static
`.bss` object**, not a kmalloc: `r5 = .LANCHOR0 + 0x78 * controller` (quote below). On it, in order:
`pcie_ete_init(0, comm)` builds the ETE engine and its rings, then `pcie_comm_init(comm)` registers
the context in a global table and tail-calls `pcie_msg_init(comm)`, which builds the message
context: the six mailbox CAs and an 11-entry handler table. The **six CAs are proven**; the
per-message `+4`/`+0xc`/`+0x10` register pointers live on a *per-chip* object that the chip layer
creates and that `pcie_msg_init` only receives as `comm->[0][i]`, so their CA binding is **not
statically attributable** from `plat.ko` alone (the same conclusion as phase-20a A.6).

```
===== pcie_main_init @ 0x704 =====
  0x00071c: ldr  r0, [pc, #0x300]            ; __stack_chk_guard
  0x000724: mov  r3, #0x78
  0x00073c: mla  r5, r3, r4, r5              ; r5 = .LANCHOR0 + 0x78 * controller  <-- the context
  0x0007a8: ... __pci_register_driver          ; rox_pci0
  0x000830: bl   get_pcie_res                  ; -> comm+0x70 (ops table)
  0x000838: str  r0, [r5, #0x70]
  0x000864: mov  r0, r5
  0x000868: bl   pci_dev_res_init              ; walks comm->[0][i] = the per-chip objects
  0x0008e4: mov  r0, #0
  0x0008e8: bl   pcie_ete_init                 ; pcie_ete_init(0, comm)
  0x000940: str  r6(=0), [r5, #0x54]
  0x000944: bl   pcie_comm_init                ; registers comm, makes the msg context
  0x000960: blx  r3                            ; comm->ops->?... (probe tail)
```

```
===== pcie_comm_init @ 0x171cc =====
  0x0171d8: ldr  r2, [r0, #0x54]
  0x0171e0: add  r3, r3, r2, lsl #2            ; r3 = .LANCHOR0 + 4 + 0
  0x0171e4: str  r0, [r3, #4]                  ; global message-context table[0] = comm
  0x0171e8: bl   pcie_thread_init              ; kthread: pcie_process_thread
  0x0171ec: mov  r0, r4
  0x0171f0: pop  {r4, lr}
  0x0171f4: b    pcie_msg_init                 ; tail-call pcie_msg_init(comm)
```

The per-chip objects (`comm->[0][i]`, 0x9c bytes) are allocated in `oal_pcie_probe` @ `0x4d0`:

```
  0x0005ac: mov  r2, #0x9c
  0x0005b8: bl   kmem_cache_alloc_trace        ; chip object, 0x9c bytes, memset 0x9c
  0x0005f4: str  r7, [r5, #0x18]               ; chip+0x18 = pci_dev
  0x000650: str  r5, [r2, r0, lsl #2]          ; comm->[0][phy_devid] = chip
```

`pci_dev_res_init` @ `0x821c` walks that array and calls `oal_pci_lres_init(chip)` after setting
`chip->[0x18]->0xb0 = chip`.

### A.1 The runtime message context (`pcie_msg_init` @ `0xb6e4`) **[proven]**

`pcie_msg_init(comm)` builds the context on the per-controller object. The full body (from
`build/tmp/rtmsg/msg.txt`):

```
===== pcie_msg_init @ 0xb6e4 =====
  0x00b6f8: add  sb, r0, #0x2c               ; sb = comm+0x2c: the six-register array
  0x00b708: ldr  r2, [r4]                    ; comm->[0] = per-chip array
  0x00b70c: mov  r1, sb
  0x00b710: ldr  r3, [r4, #0x70]             ; comm->ops
  0x00b714: ldr  r2, [r2]
  0x00b718: ldr  r3, [r3, #8]                ; ops->get_msg_reg_map
  0x00b71c: ldr  r0, [r2, #4]                ; pci dev
  0x00b720: blx  r3                          ; shuangta_pcie_msg_reg_map(dev, comm+0x2c)
  0x00b738: str  r8(=0), [r3]                ; *out[0] = 0   (CA 0x40039010)
  0x00b740: str  r8(=0), [r3]                ; *out[1] = 0   (CA 0x40039014)
  0x00b74c: str  r8(=0), [r4, #0x48]         ; comm+0x48 = pending shadow = 0
  0x00b750: str  r8(=0), [r4, #0x44]         ; comm+0x44 = send flag = 0
  0x00b754: ldr  r0, [r3, #0x1c]             ; kmalloc_caches[...]
  0x00b758: bl   kmem_cache_alloc_trace      ; r2=0x58 -> the 11-entry handler table
  0x00b764: str  r3, [r4, #0x4c]             ; comm+0x4c = handler table (11 x 8 bytes)
  0x00b774: bl   memset_s                     ; 0x58 bytes
  ; --- bind the per-chip vtable (loop over comm->[0x58] chips) ---
  0x00b7b4: ldr  r3, [r1, r5, lsl #2]        ; chip = comm->[0][i]
  0x00b7d0: str  fp, [r3, #0x60]             ; chip+0x60 = pcie_msg_send_irq
  0x00b7dc: str  r4, [r3, #0x64]             ; chip+0x64 = comm
  0x00b7e8: str  sl, [r3, #0x68]             ; chip+0x68 = pcie_msg_handle
  0x00b7f4: str  sb, [r3, #0x6c]             ; chip+0x6c = comm+0x2c (the six CAs)
  0x00b810: str  r3(=0), [r4, #0x50]         ; comm+0x50 = 0 (before spin_lock_init, below)
  ; --- register the PCIe-level handlers (pcie_msg_register @0x15fbc) ---
  0x00b814: ldr  r0, [r4, #0x54]             ; 0 -> "use the global table"
  0x00b820: bl   pcie_msg_register           ; id 1, pcie_dev_ready_msg_handle,     chip[0]
  0x00b858: bl   pcie_msg_register           ; id 6, pcie_trigger_ete_sending_handle, comm
  0x00b884: bl   pcie_msg_register           ; id 7, pcie_trigger_ete_sending_handle, comm
  0x00b8b8: bl   pcie_msg_register           ; id 3, pcie_ete_transfer_done_handle,  chip[0]
```

Context layout (offsets quoted from the instructions above; all **[proven]** except where noted):

| off | field | source |
| --- | ----- | ------ |
| `+0x00` | pointer to the per-chip object array (`comm->[0][i]`, 0x9c-byte objects) | `pcie_main_init`/`oal_pcie_probe` |
| `+0x2c..+0x40` | `out[0..5]`: the six mailbox CA host VAs | `shuangta_pcie_msg_reg_map` |
| `+0x44` | send-in-progress flag / last pending shadow | `pcie_msg_send` |
| `+0x48` | pending bitmask shadow | `pcie_msg_send` |
| `+0x4c` | pointer to the `kmalloc(0x58)` 11-entry `{fn,arg}` handler table | `pcie_msg_init`, `pcie_msg_register` |
| `+0x50` | spinlock (used by `pcie_msg_send`) | `pcie_msg_send` |
| `+0x54` | 0 (set by `pcie_main_init` right before `pcie_comm_init`) | `0x940` |
| `+0x58` | chip count (`pcie_bus_getnum()`) | `pcie_main_init` |
| `+0x70` | ops table (`get_pcie_res`) | `pcie_main_init` |

**The six mailbox CAs** (`shuangta_pcie_msg_reg_map` @ `0x1b1a0`, the only place the literals
appear; each `movw`/`movt` pair is an `oal_pcie_inbound_ca_to_va` map into `out[i]`):

```
  0x01b1b0: movw r1, #0x9010 ; movt r1, #0x4003   ; 0x40039010 -> out[0] (comm+0x2c)
  0x01b1e8: movw r1, #0x9014 ; movt r1, #0x4003   ; 0x40039014 -> out[1] (comm+0x30)
  0x01b20c: movw r1, #0x92d4 ; movt r1, #0x4003   ; 0x400392d4 -> out[2] (comm+0x34)
  0x01b230: movw r1, #0x1438 ; movt r1, #0x4010   ; 0x40101438 -> out[3] (comm+0x38)
  0x01b254: movw r1, #0x1414 ; movt r1, #0x4010   ; 0x40101414 -> out[4] (comm+0x3c)
  0x01b278: movw r1, #0x92f0 ; movt r1, #0x4003   ; 0x400392f0 -> out[5] (comm+0x40)
```

| slot | device CA | BAR0 off | role (proven) |
| ---- | --------- | -------- | ------------- |
| `out[0]` | `0x40039010` | `0x3f1010` | H2D pending/message mask: `pcie_msg_send` writes the bitmap here |
| `out[1]` | `0x40039014` | `0x3f1014` | message register 1 - **the register the released chip writes** (`0 -> 4`) |
| `out[2]` | `0x400392d4` | `0x3f12d4` | doorbell: `pcie_msg_send`/`_irq` OR bit 0 |
| `out[3]` | `0x40101438` | `0x4b9438` | MAC-side message register |
| `out[4]` | `0x40101414` | `0x4b9414` | MAC-side message register |
| `out[5]` | `0x400392f0` | `0x3f12f0` | message register 5: `pcie_msg_send_irq` writes `8` |

The handler table is written by `pcie_msg_register` @ `0x15fbc`; its table pointer comes from the
global comm when `r0 == 0`:

```
  0x015ff0: ldr  r1, [r1, #4]                ; [.LANCHOR0+4] = the registered comm
  0x015ffc: ldr  r5, [r1, #0x4c]             ; table = comm+0x4c
  0x016004: strne r2, [r5, r4, lsl #3]       ; table[id].fn  = fn
  0x01600c: strne r3, [r4, #4]               ; table[id].arg = arg  (r4 = &table[id])
```

**(a) The pending/ack/re-arm contract.** `pcie_msg_handle` @ `0x171f8` is the dispatcher; its
argument is the same-shaped "runtime context" the parent task names (`ctx`):

```
===== pcie_msg_handle @ 0x171f8 =====
  0x017248: ldr  r3, [r4, #0xc]              ; ack register pointer
  0x017254: str  r1(=1), [r3]                ; ACK:     *(ctx+0xc) = 1
  0x017258: ldr  r3, [r4, #4]                ; pending register pointer
  0x01725c: ldr  r5, [r3]                    ; pending  = *(ctx+4)
  0x017260: str  r2(=0), [r3]                ; CLEAR:   *(ctx+4) = 0
  0x0172a0: ldr  r3, [r4, #0x10]             ; re-arm register pointer
  0x0172ac: str  r2(=1), [r3]                ; RE-ARM:  *(ctx+0x10) = 1
  0x0172b4: rbit r6, r5
  0x0172b8: clz  r6, r6                      ; r6 = lowest set bit in the pending mask
  0x0172bc: cmp  r6, #0xa                    ; ids 0..10 only
  0x0172d8: ldr  r3, [r4, #0x20]             ; ctx+0x20 = handler table
  0x0172dc: ldr  sl, [r3, r6, lsl #3]        ; table[id].fn
  0x0172ec: ldr  r0, [r3, #4]                ; table[id].arg
  0x0172f0: blx  sl
```

So **a message is pending when `*(ctx+4) != 0`**; the lowest set bit is the id; the handler writes
**1 to `*(ctx+0xc)` (ack)**, clears `*(ctx+4)`, writes **1 to `*(ctx+0x10)` (re-arm)**, and calls
`table[id].fn(table[id].arg)`. **[proven]**

**(b) What `ctx` is, and why the CA binding is not provable.** `ctx` is not the 0x78-byte
per-controller object: that object's `+0x4c` is the handler table pointer and `+0x48` a pending
shadow, whereas `pcie_msg_handle` reads its table at `+0x20` and its registers at `+4`/`+0xc`/`+0x10`.
The object `pcie_msg_handle` receives is the **per-chip object** (`comm->[0][i]`), which
`pcie_msg_init` binds as `chip+0x68 = pcie_msg_handle` with `chip+0x64 = comm`; `chip+0x6c` holds the
six-CA array. The `+4`/`+0xc`/`+0x10` pointers and `+0x20` table are created by the chip layer
(`oal_pci_lres_init` / the Wi-Fi driver), not by `plat.ko`, so **which mailbox CAs are the
pending/ack/re-arm registers cannot be read out of `plat.ko`**. **[inferred]**, same limit as
phase-20a A.6.

The natural reading - and the one `lab/rtmsg` builds, without acting on it - is that the receive
pending word is the register the released chip actually wrote (`out[1]`, CA `0x40039014`), with the
H2D mask (`out[0]`) and doorbell (`out[2]`) as ack/re-arm. That is **inferred**, not proven.

### A.2 What `pcie_ete_init` sets up: the SR/DR rings and the ETE registers **[proven]**

`pcie_ete_init` @ `0x7820` (controller 0, `comm`) allocates the engine context and its ring arrays,
installs the completion handlers, maps the ETE register block and programs the per-channel rings:

```
===== pcie_ete_init @ 0x7820 =====
  0x007838: mov  r2, #0x88
  0x007844: bl   kmem_cache_alloc_trace        ; ete = 0x88 bytes, memset 0
  0x007860: str  r7, [r4, #0x84]               ; ete+0x84 = comm
  0x007868: str  r5, [r4, #0x80]               ; ete+0x80 = channel (0)
  0x00786c: bl   get_pcie_ete_res
  0x007878: str  r0, [r4, #0x18]               ; ete+0x18 = per-chip ETE resource
  0x007884: mov  r2, #0x33c
  0x00788c: bl   kmem_cache_alloc_trace        ; SR channel array = 0x33c (3 x 0x114)
  0x00789c: mov  r2, #0x21c
  0x0078a4: bl   kmem_cache_alloc_trace        ; DR channel array = 0x21c (4 x 0x6c + hdr)
  0x0078b8: bl   pcie_ete_intr_init            ; completion handlers + ETE status block
  0x0078d4: bl   pcie_ete_rings_init           ; ring arrays + program registers
  0x0078e8: bl   pcie_ete_chn_res              ; per-channel resource enable
```

`pcie_ete_intr_init` @ `0x7528` installs the four completion entry points and maps the ETE
**status** block (the resource's second CA, `0x40039508`), clearing its latched IRQ bits:

```
  0x007540: str  r0, [r1, #0x1c]               ; ete+0x1c = pcie_rx_handle
  0x00754c: strd r2, r3, [r1, #0x20]           ; ete+0x20 = pcie_tx_done_handle, +0x24 = rx_err
  0x007558: str  r3, [r1, #0x28]               ; ete+0x28 = pcie_ete_tx_err_handle
  0x007568: ldr  r1, [r1, #0x18]               ; ETE resource
  0x007580: ldr  r1, [r1, #4]                  ; res+4 = CA 0x40039508 (status block)
  0x00758c: bl   oal_pcie_inbound_ca_to_va
  0x00759c: str  r3, [r4, #0xc]                ; ete+0xc = status-block VA
  0x0075a8: ldr  r1, [r3]
  0x0075b4: and  r2, r2, r1                    ; r2 = 0xffe0f8f8
  0x0075b8: str  r2, [r3]                      ; clear the block's IRQ bits
```

`pcie_ete_rings_init` @ `0x7680` stores the arrays, maps the ETE **register** block (the resource's
first CA, `0x4003a000`) and programs the two ring families through it:

```
  0x0076d0: str  r5, [r4, #0x10]               ; ete+0x10 = DR channel array
  0x0076d4: str  r6, [r4, #0x14]               ; ete+0x14 = SR channel array
  0x0076dc: mov  r2, #3
  0x0076e0: strd r2, r3, [r4, #4]              ; ete+4 = 3 (SR count), ete+8 = 4 (DR count)
  0x0076e8: ldr  r1, [r4, #0x18]
  0x0076f0: ldr  r1, [r1]                      ; res[0] = CA 0x4003a000
  0x0076fc: bl   oal_pcie_inbound_ca_to_va     ; -> r7 = ETE register-block VA
  0x007714: bl   pcie_ete_init_src_ring        ; (ete, r7): channels 0..2, stride 0x114
  0x007728: bl   pcie_ete_init_dst_ring        ; (ete, r7): channels 3..6, stride 0x6c
```

The resource object is the static `.data+0x2944` (phase-17 A.1): `+0x00 = 0x4003a000` (ETE
register block), `+0x04 = 0x40039508` (status block), `+0x08 = sr_node_init_handle`,
`+0x0c = dr_node_init_handle`, `+0x10 = sr_dscr_fill`, `+0x14 = dr_dscr_fill`, `+0x18..` the node
size / descriptor accessors.

> **Offset correction (measured, phase 20b).** `0x4003a000` is a *device* CA, not a BAR0 offset.
> The host reaches it through the region-3 viewport (`host 0x403b8000 -> dev CA 0x40000000`), so
> the BAR0 offset of the ETE register block is `0x3b8000 + (0x4003a000 - 0x40000000) = 0x3f2000`
> (host `0x403f2000`). The phase-17 text's "BAR0+0x3a000" conflated the two; a read at BAR0
> `+0x3a000` lands in region 0 (ROM_WRAM, dev CA `0x3a000`) and reads `0xffffffff`. The message CAs
> are handled correctly elsewhere (`0x40039010 -> BAR0+0x3f1010`), which is why only the ETE block
> was mis-addressed. The channel config table is `.rodata+0x101c`, seven 12-byte entries;
`pcie_ete_get_chn_cfg` @ `0x15f00` indexes it:

| idx | block off | depth | queue | family |
| --- | --------- | ----- | ----- | ------ |
| 0..2 | `0x400/0x450/0x4a0` | `0x20` (32) | 0 | SR |
| 3..6 | `0x590/0x5e0/0x630/0x680` | `0x20` (32) | 0 | DR |

**Ring allocation** (phase-17 A.4, `shuangta_ete_{sr,dr}_node_init_handle` @ `0x17728`/`0x17970`):
`dma_alloc_attrs(count*8, 0xa20, 0)`; SR count = `depth+2` = 34 nodes = **272 bytes**, DR count =
`depth` = 32 nodes = **256 bytes**; the handle is stored (`inst+0xe8` for SR, `inst+0x54` for DR)
and the host VA at `inst+0` (SR) / `inst+0x10` (DR).

**Program registers** (all 32-bit; SR block base + these offsets, DR likewise):

| register | offset | value written | source |
| -------- | ------ | ------------- | ------ |
| SR control | `+0x08` | `[2:0] = queue` (0) | `pcie_ete_sr_reg_init` |
| SR ring base | `+0x10` | `pcie_hostca_to_devva(ring dma)` | `pcie_ete_sr_reg_init` |
| SR depth | `+0x14` | `[9:0] = depth-1` (31) | `pcie_ete_sr_reg_init` |
| SR write ptr | `+0x18` | `[inst+0xc]` (index[9:0]\|phase[10]) | `pcie_ete_sr_reg_init` |
| DR ring base | `+0x30` | `pcie_hostca_to_devva(ring dma)` | `pcie_ete_dr_reg_init` |
| DR depth | `+0x34` | `[9:0] = depth-1` (31) | `pcie_ete_dr_reg_init` |
| DR pointer | `+0x38` | `[inst+0x1c]` | `pcie_ete_dr_reg_init` |
| channel res | `+0x2e8` | read, `& 0xfffffc20`, write back | `pcie_ete_chn_res` |

Write order is base, depth-1, pointer, control (`pcie_ete_sr_reg_init` @ `0x14a48`):

```
  0x014aa0: ldr  r2, [r4, #0xe8]             ; ring DMA address
  0x014aac: bl   pcie_hostca_to_devva
  0x014ab0: str  r0, [r5, #0x10]             ; SR+0x10 = ring base device VA
  0x014ac0: ldrb r3, [cfg, #4]               ; depth = 0x20
  0x014ac8: sub  r3, r3, #1                  ; 0x1f
  0x014acc: bfi  r1, r3, #0, #0xa            ; SR+0x14[9:0] = depth-1
  0x014ad0: str  r1, [r2, #0x14]
  0x014ad8: ldr  r2, [r4, #0xc]              ; write pointer
  0x014adc: str  r2, [r3, #0x18]             ; SR+0x18 = write pointer
  0x014aec: ldrb r1, [cfg, #5]               ; queue select = 0
  0x014af0: bfi  r2, r1, #0, #3             ; SR+0x08[2:0] = queue
  0x014af4: str  r2, [r3, #8]
```

`pcie_ete_dr_reg_init` @ `0x1483c` is the DR twin (base `+0x30`, depth `+0x34`, pointer `+0x38`).
`pcie_ete_chn_res` @ `0x7490` does the `+0x2e8` read-modify-write with mask `0xfffffc20`.

**The host-CA -> device-VA window.** The ring base (and every descriptor address) must be a *device*
address, so the vendor converts the coherent DMA address with `pcie_hostca_to_devva` @ `0xaefc`:

```
  0x00af08: ldr  r1, [r0, #4]                ; chip+4 -> host object
  0x00af14: ldr  r1, [r1, #0xc4]             ; -> the runtime inbound window
  0x00af20: ldr  ip, [r1, #0x10]             ; win+0x10 = host-CA base
  0x00af24: cmp  r2, ip                      ; hostca < base -> error
  0x00af30: ldr  r0, [r1]                    ; win+0 = device-VA base
  0x00af34: add  r0, r0, r2
  0x00af38: sub  r0, r0, ip                  ; devva = devva_base + hostca - hostca_base
```

`pcie_devva_to_hostca` @ `0xadec` is the inverse. The window object lives at `chip->[4]->[0xc4]`
and is built at runtime - the SoC device tree describes the mapping (see A.3). **[proven formula,
inferred values]**.

### A.3 How the endpoint's interrupt reaches the host **[proven structure, live-measured]**

**(1) Request.** `oal_pcie_probe_irq_init` @ `0x698` reads the number from `pci_dev->irq` (which is
`PCI_INTERRUPT_LINE`), and `do_request_irq` @ `0x1081c` registers `oal_pcie_intx_isr` **shared** with
`thread_fn = NULL`:

```
  0x010820: mov  ip, r1                      ; dev_id = comm
  0x010824: mov  r3, #0x80                   ; IRQF_SHARED
  0x01082c: ldr  r0, [r0, #0x184]            ; pci_dev->irq
  0x010838: movw r1, oal_pcie_intx_isr
  0x010844: mov  r2, #0
  0x010848: bl   request_threaded_irq        ; (irq, isr, NULL, IRQF_SHARED, name, comm)
```

**(2) Enable.** `oal_enable_pcie_irq` @ `0x7d5c` is `enable_irq(pci_dev->irq)` - host-side only,
no device register. `bal_irq_enable` @ `0x10c78` dispatches the same callback and its only ready-path
caller is `wlan_power_on` @ `0xe46c` (`0xe5f8: bl bal_irq_enable`), i.e. right after the firmware
download.

**(3) Service.** `oal_pcie_intx_isr` @ `0x854c` -> `oal_pcie_transfer_done` @ `0x83e4`, which reads
the PCIe **glue status**, clears it by write-back, then fans out to ETE and message dispatch:

```
===== oal_pcie_transfer_done @ 0x83e4 =====
  0x008404: ldr  r3, [r4]                    ; comm->[0]
  0x00840c: ldr  r3, [r3, #0xc]
  0x008418: ldr  r5, [r3, #8]                ; glue status dword
  0x008464: bic  r5, r5, #0xff000000
  0x008468: bic  r5, r5, #0xe00000
  0x00846c: bic  r5, r5, #0xf800
  0x008474: bic  r5, r5, #0xf8
  0x008480: ldr  r3, [r0, #4]
  0x008484: orr  r3, r3, r5
  0x008488: str  r3, [r0, #4]                ; write-back = clear the glue status
  0x008490: bl   pcie_ete_h2d_isr_handle
  0x0084a0: bl   pcie_ete_d2h_isr_handle
  0x0084a8: bl   pcie_intr_handle            ; PCIe-level message-register dispatch
```

`pcie_intr_handle` @ `0x82e4` is the second dispatcher: it reads `[[comm+4]] + 0x2ec`, masks to bits
`0x3d8` (`{3,4,6,7,8,9}`) and calls `handler[lowest bit]` from an inline table at
`comm + 0x48 + bit*8`:

```
  0x0082f4: ldr  r3, [r6, #4]                ; comm+4 = host object
  0x008300: ldr  r3, [r3]
  0x008304: ldr  r4, [r3, #0x2ec]            ; glue/controller status
  0x00830c: ands r4, r4, #0x3d8              ; bits 3,4,6,7,8,9
  0x008328: rbit r5, r4
  0x00832c: clz  r5, r5                      ; lowest set bit
  0x00833c: ldr  r2, [r3, #0x48]             ; handler[bit].fn
  0x008348: ldr  r0, [r3, #0x4c]             ; handler[bit].arg
  0x00834c: blx  r2
```

**(4) Why the takeover has no line.** The number is whatever the PCI core / host bridge wrote into
`PCI_INTERRUPT_LINE`. On the live vendor boot the endpoint is bound to `rox_pci0` (this driver,
`plat.ko`) and that stack requests and hosts the line:

```
vendor boot, live:
  lspci: 0000:00:00.0 Network controller [59e7:0005]
         Interrupt: pin A routed to IRQ 207
         Capabilities: [50] MSI: Enable- Count=1/1 Maskable+ 64bit+   (MSI disabled -> INTx)
  config: PCI_COMMAND=0x0006, PCI_INTERRUPT_PIN=0x01, PCI_INTERRUPT_LINE=0xcf (=207)
  sysfs:  /sys/bus/pci/devices/0000:00:00.0/irq = 207 ; driver -> rox_pci0
  /proc/interrupts:
     207: 0 0 GIC-0 91 Level hisi_pci_intx      <- endpoint 0
     209: 20789 0 GIC-0 95 Level hisi_pci_intx  <- endpoint 1 (5 GHz, active)
     208: 0 0 GIC-0 101 Level pcie_link_down
     210: 0 0 GIC-0 102 Level pcie_link_down
```

The line is the SoC PCIe controller's **radm** interrupt, wired in the device tree:

```
docs/soc/luofu-r116.dts:
  pcie@0x10160000 { interrupts = <0 0x3b 4 0 0x45 4>;
                    interrupt-names = "radm" "linkdown";
                    compatible = "hsan,pcie" "hsan,acp-pcie0";
                    iatu_ep = <2 0 0x80000000 0x30000000 0 0x307fffff 0xab000000 0>; ... }
```

`0x3b` = SPI 59 -> GIC-0 IRQ 91 -> virq 207. `0x45` = SPI 69 -> GIC-0 IRQ 101 -> virq 208. The
`iatu_ep` property is the endpoint's inbound window (host PCI `0x30000000..0x307fffff` -> device ACP
`0xab000000`); this is the mapping `pcie_hostca_to_devva` walks at runtime. **[proven]**

In the takeover (vendor modules renamed), no driver claims the endpoint, so `PCI_INTERRUPT_LINE`
reads `0xff` (sysfs irq `255`) and `request_irq(207)` - if forced - succeeds at the API level on a
descriptor nothing drives for this endpoint: `irq_taken = 0`. **[observed, phase-20a B.2]** The
interrupt route is created by the vendor stack's own probe/request; a clean-boot takeover must stand
up that route (request the line *and* have a source that asserts it) rather than inherit it.

### A.4 The DR ring descriptor - the payload carrier **[proven]**

Every ring node is 8 bytes. The descriptor helpers (`build/tmp/rtmsg/ete_desc.txt`) fix the layout:

```
shuangta_ete_dr_dscr_fill @0x1765c:            ; r0 = channel instance, r1 = buffer address
  0x01765c: ldr  r3, [r0, #0x1c]               ; r3 = current DR index (u10)
  0x017660: ldr  r2, [r0, #0x10]               ; r2 = DR node array base
  0x017664: ubfx r3, r3, #0, #0xa
  0x017668: str  r1, [r2, r3, lsl #3]          ; node[index].word0 = buffer address

shuangta_ete_dr_get_dscr_addr @0x176c0:         ; r0 = instance, r1 = index
  0x0176c4: ldr  r2, [r0, #0x10]               ; node array
  0x0176cc: ldr  r0, [r2, r3, lsl #3]          ; node[index].word0

shuangta_ete_sr_get_dscr_len @0x17684:
  0x01768c: ldr  r0, [r3, #4]                  ; node[index].word1
  0x017690: lsr  r0, r0, #0x10                 ; length = word1 >> 16

shuangta_ete_sr_get_dscr_flag @0x17698:
  0x0176a0: ldr  r0, [r3, #4]
  0x0176a4: ubfx r0, r0, #0, #0xd              ; flags = word1[12:0]
```

The host fill path `shuangta_ete_sr_dscr_fill` @ `0x17858` builds the word:

```
  0x017884: bfi  r1, r3, #0x10, #0x10          ; word1[31:16] = length
  0x0178a0: orr  r2, r2, #0x4000               ; owner/valid bit 14
  0x0178ac: orr  r2, r2, #0x2000               ; owner/valid bit 13
  0x0178b8: bfi  r2, r1(=0xd2b), #0, #0xd      ; word1[12:0] = host-fill magic 0xd2b
  0x0178cc: str  addr, [node_array, idx, lsl #3]  ; node.word0 = buffer device address
  0x0178e0: str  word1, [node_array, idx*8 + 4]   ; node.word1 = len<<16 | 0x6d2b
```

| word | bits | field |
| ---- | ---- | ----- |
| `word0` | 31:0 | data buffer **device (ACP) address** |
| `word1` | 31:16 | **length** (bytes) |
| `word1` | 14,13 | owner/valid bits (set by the filler) |
| `word1` | 12:0 | flags; `0xd2b` = host-filled |

Ring position fields are packed `index[9:0] | phase[10]` and advanced by `pcie_ete_ring_ptr_plus`
@ `0x13ef8`. The device->host receive path reads the DR ring and hands the buffer to the HCC layer:

```
===== pcie_rx_handle @ 0x164d0 =====
  0x01651c: ldr  r2, [r6, #0x10]               ; DR ring base
  0x016528: ldr  r3, [r6, #8]                  ; depth
  0x016544: mul  r3, r3, #0x6c                 ; per-channel stride
  0x01655c: bl   pcie_ete_dr_get_uploadbuf
  0x0165ac: ldr  r2, [r3, #4]                  ; bal cbs->rx
  0x0165b4: blx  r2                            ; rx(handle, buf) -> skb
  0x0165bc: bl   pcie_wkup_thread              ; wake the HCC rx thread
```

`hcc_msg_process` @ `0x1204c` then dispatches the skb by its header (selector byte `[0] & 0xf`,
message id `u16` at `+6`) through the HCC chip table (chip 4: id 1 `device_plat_ready_msg_process`,
id 2 `host_ready_msg_process`). **[proven structure]**

### A.5 Proven vs inferred

| claim | status |
| ----- | ------ |
| per-controller context is a static `.bss` object, `pcie_main_init` selects it with `.LANCHOR0 + 0x78*ctrl` | **proven** (`0x73c`) |
| `pcie_comm_init` registers it and tail-calls `pcie_msg_init` | **proven** (`0x171e8..0x171f4`) |
| `pcie_msg_init` maps the six CAs into `comm+0x2c..+0x40`, kmallocs a 0x58 handler table at `comm+0x4c`, zeroes `out[0]`/`out[1]`, binds `chip+0x60..0x6c` and registers ids 1/3/6/7 | **proven** (`pcie_msg_init`, `pcie_msg_register`) |
| the six CAs and their roles (`0x40039010` mask, `0x40039014` msg1, `0x400392d4` doorbell, `0x40101438`/`0x40101414` MAC, `0x400392f0` msg5) | **proven** (`shuangta_pcie_msg_reg_map`, `pcie_msg_send`, `pcie_msg_send_irq`) |
| `pcie_msg_handle`: pending `*(ctx+4)`, ack `*(ctx+0xc)=1`, clear `*(ctx+4)=0`, re-arm `*(ctx+0x10)=1`, dispatch lowest set bit via `table = *(ctx+0x20)` | **proven** (`pcie_msg_handle`) |
| `ctx` is the per-chip object (`comm->[0][i]`), not the 0x78 context; its `+4`/`+0xc`/`+0x10` register pointers are created outside `plat.ko` | **inferred** |
| which mailbox CAs are pending/ack/re-arm | **not provable from the module** |
| ETE ring block CA `0x4003a000`, status block CA `0x40039508`, 3 SR (stride `0x114`) + 4 DR (stride `0x6c`) channels, depth 32, node 8 bytes, SR `(32+2)*8` / DR `32*8`, program order base/depth-1/ptr/ctrl + `+0x2e8 & 0xfffffc20` | **proven** (`.data+0x2944`, `.rodata+0x101c`, `pcie_ete_*`) |
| ring base = `pcie_hostca_to_devva(ring dma)` through `chip->[4]->[0xc4]`; `devva = devva_base + hostca - hostca_base` | **proven formula**, window values **inferred** (the DTS `iatu_ep` gives host PCI `0x30000000` -> ACP `0xab000000`) |
| IRQ number = `pci_dev->irq` = `PCI_INTERRUPT_LINE`, `IRQF_SHARED`, `thread_fn=NULL`; enable = host-side `enable_irq` | **proven** (`do_request_irq`, `oal_enable_pcie_irq`) |
| ISR = `oal_pcie_intx_isr` -> `oal_pcie_transfer_done` (clear glue status) -> ETE h2d/d2h + `pcie_intr_handle` (status `+0x2ec & 0x3d8`) | **proven** |
| vendor boot: INTx pin A -> IRQ 207 (GIC-0 91 radm `hisi_pci_intx`), MSI disabled; takeover: `PCI_INTERRUPT_LINE=0xff`/irq 255 | **measured** (live) |
| DR node = `{u32 address; u32 (len<<16)|flags}`, flags `0xd2b`, owner bits 13/14 | **proven** |
| payload travels on the ETE DR ring into host memory, then skb -> HCC chip table | **proven structure** (phase-17 + this dump) |

---

## Part B - `lab/rtmsg/rtmsg.c`

*(filled in after the test boot; see the test record below)*

## Part C - outcome

*(filled in after the test boot)*

## Test record / Recovery

*(filled in after the test boot)*
