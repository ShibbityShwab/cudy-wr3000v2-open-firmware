# ete-engine: the vendor's BAL/HCC/ETE write engine, and our first safe interaction (phase 17, 2026-10-01)

This document has three parts and the test-boot record:

- **Part A** - the engine's structure, recovered from `hi5622v100_plat.ko` (full `.symtab`,
  `build/register-dumps/teardown/hi5622v100_plat.ko`, md5 `23660bc285393e678d5cade1c36c194b`) and
  `hi5622v100_wifi.ko` (`build/tmp/hi5622v100_wifi.ko`, md5 `e21629d226ec7de9a860a8955952d311`).
  Every field is a disassembly quotation. Steps are marked **[proven]** (a constant/relocation in
  the instruction stream) or **[inferred]** (control flow / table semantics, not a literal).
- **Part B** - `lab/eteprobe/eteprobe.c`: the smallest verifiable engine interaction that has no
  side effects on an unowned chip - claim, coherent DMA buffer + known pattern, and a read-only
  probe of the ETE/glue register block. It deliberately does **not** submit a descriptor.
- **Part C** - the test boot and the recovery boot.

The conclusion that drives Part B: `bal_write(chip, dev_addr, buf, len)` from phase 16 does **not**
end in a register write at all - it tail-calls the BAL bus callback, which hands the buffer to the
HCC queue/thread layer, which finally hands a descriptor to the ETE source/destination DMA engine.
The descriptor *node format* is unambiguous (8 bytes, quoted below), but the *queue* is not
reachable from the unowned state: the ETE ring base, the head/tail program registers and the
doorbell are per-chip runtime resources created by `pcie_ete_init`/`pcie_msg_init` from
`get_pcie_ete_res`, which does not exist when the vendor stack is hidden. Part B therefore stops
at the first step that is genuinely unsafe.

---

## Part A - the engine's structure

### A.0 Method

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko
$PY lab/ko_disasm.py "$KO" <func> ...
```

Full dumps: `build/register-dumps/ete/dump_ete_engine.txt`,
`build/register-dumps/ete/dump_bal_hcc_msg.txt`. `lab/ko_disasm.py` annotates `bl`/`b`/literal
relocations, which is what makes the callback indirections readable.

The engine's symbols (all `plat.ko`): `bal_write`/`bal_read`/`bal_port_start_xfer`,
`hcc_xfer_done`/`hcc_queue_tx_process`/`hcc_process_tx_thread`/`hcc_msg_tx`,
`pcie_ete_init`/`pcie_ete_rings_init`/`pcie_ete_init_src_ring`/`pcie_ete_init_dst_ring`,
`pcie_ete_sr_reg_init`/`pcie_ete_dr_reg_init`, `pcie_ete_sending_trigger`,
`pcie_ete_dr_get_uploadbuf`, `pcie_ete_tx_queue_handle`, `pcie_ete_rcv_buff_check`,
`pcie_ete_h2d_isr_handle`/`pcie_ete_d2h_isr_handle`, `pcie_msg_init`/`pcie_msg_send`/
`pcie_msg_send_irq`/`pcie_msg_handle`, `shuangta_pcie_msg_reg_map`, `get_pcie_ete_res`.

---

### A.1 `bal_write` does not touch a BAR - it dispatches a callback **[proven]**

`bal_write` @ `0x10c04` (`dump_bal_hcc_msg.txt`):

```
  0x010c04: cmp  r0, #0
  0x010c08: bne  #0x10c44                 ; chip != 0 -> -ENOSYS (0x8b2d)
  0x010c0c: movw r0, -> .LANCHOR1
  0x010c18: ldr  ip, [r0, #0x14]          ; ip = BAL bus context
  0x010c1c: ldr  lr, [ip, #0x3c]          ; lr = callback-present flag
  0x010c24: beq  #0x10c3c
  0x010c28: ldr  lr, [ip, #0x38]          ; lr = bus write/xfer callback
  0x010c2c: ldr  r0, [r0, #0x18]          ; r0 = BAL handle
  0x010c38: bx   ip                       ; tail-call callback(handle, ...)
```

The callback is installed by `bal_register_hcc_callbacks` @ `0x10ad8`:

```
  0x010af4: strne r1, [r3, #0x20]         ; ctx->+0x20 = hcc callbacks
  0x010afc: strne r3, [r2, #0x1c]         ; .LANCHOR1+0x1c = ctx
```

and the transfer entry point `bal_port_start_xfer` @ `0x10b78` dispatches `ctx->+0x18`:

```
  0x010ba0: ldr  r2, [r3, #0x14]          ; bus ctx
  0x010ba4: ldr  r2, [r2, #0x18]          ; ->xfer fn
  0x010bb8: ldr  r0, [r3, #0x18]          ; handle
  0x010bbc: bx   r2
```

So the phase-16 `firmware_file_send -> bal_write` call is a bus-abstraction dispatch, not an MMIO
write. The same `bal_write`/`bal_read` pair (and the same callback) is what every message uses.

### A.2 The HCC queue and TX thread **[proven structure]**

`hcc_xfer_done` @ `0x10d08` drains a socket-buffer queue and posts each skb to `hcc_msg_tx`:

```
  0x010d3c: bl   skb_dequeue
  0x010d48: ldr  r2, [r4, #0x118]         ; skb->head?
  0x010d5c: ldrb r3, [r3, #3]             ; port/queue byte of the message
  0x010d60: lsl  r3, r3, #4
  0x010d64: orr  r3, r3, #1
  0x010d68: strb r3, [r2, #1]
  0x010d6c: bl   hcc_msg_tx
```

`hcc_queue_tx_process` @ `0x1128c` pops a queue list under a spinlock and calls the port transfer:

```
  0x0113e8: mov  r0, sp
  0x0113f0: str  r5, [sp, #4]             ; port res
  0x0113fc: bl   bal_port_start_xfer
```

`hcc_process_tx_thread` @ `0x1147c` is the kthread loop: `hcc_thread_condition_check` ->
`hcc_queue_tx_process` per queue, or `schedule()` on a wait-queue.

**[inferred]** The HCC layer is a queue-of-queues: messages are skbs tagged with a port/queue byte;
the TX thread drains each queue and hands the port to `bal_port_start_xfer`, which tail-calls the
BAL bus callback from A.1. That callback is the entry to the ETE path below.

### A.3 `pcie_ete_init` - the engine context and its ring arrays **[proven]**

`pcie_ete_init` @ `0x7820`:

```
  0x007838: mov  r2, #0x88
  0x007844: bl   kmem_cache_alloc_trace    ; context = 0x88 bytes
  0x007860: str  r7, [r4, #0x84]           ; ctx->+0x84 = chip context
  0x007868: str  r5, [r4, #0x80]           ; ctx->+0x80 = chip handle
  0x00786c: bl   get_pcie_ete_res
  0x007878: str  r0, [r4, #0x18]           ; ctx->+0x18 = per-chip ETE resource
  0x007884: mov  r2, #0x33c
  0x00788c: bl   kmem_cache_alloc_trace    ; SR channel array = 0x33c (3 * 0x114)
  0x00789c: mov  r2, #0x21c
  0x0078a4: bl   kmem_cache_alloc_trace    ; DR channel array = 0x21c (4 * 0x6c + hdr)
  0x0078b8: bl   pcie_ete_intr_init        ; install ISR handler tables
  0x0078d4: bl   pcie_ete_rings_init
  0x0078e8: bl   pcie_ete_chn_res          ; per-channel resource enable
```

`pcie_ete_rings_init` @ `0x7680`:

```
  0x0076d0: str  r5, [r4, #0x10]           ; ctx->+0x10 = DR array
  0x0076d4: str  r6, [r4, #0x14]           ; ctx->+0x14 = SR array
  0x0076dc: mov  r2, #3
  0x0076e0: strd r2, r3, [r4, #4]          ; ctx->{+4}=3, {+8}=4  (channel counts)
  0x0076e8: ldr  r1, [r4, #0x18]           ; ETE resource
  0x0076f0: ldr  r1, [r1]                  ; [res] = ETE register-block device CA
  0x0076fc: bl   oal_pcie_inbound_ca_to_va ; map CA -> host VA (r7)
  0x007714: bl   pcie_ete_init_src_ring    ; (ctx, r7)
  0x007728: bl   pcie_ete_init_dst_ring    ; (ctx, r7)
```

`pcie_ete_init_src_ring` iterates **3** channels (0,1,2) with stride `0x114`; `pcie_ete_init_dst_ring`
iterates **4** channels (3,4,5,6) with stride `0x6c` (`dump_ete_engine.txt`). Each channel instance
is initialised by `pcie_ete_sr_init`/`pcie_ete_dr_init`.

### A.4 The ETE program registers (SR/DR) **[proven]**

`pcie_ete_sr_reg_init` @ `0x14a48` (`r4` = SR channel instance, `r5 = [r4+0xdc]` = SR register block
host VA):

```
  0x014a58: ldr  r5, [r4, #0xdc]           ; SR register block base (host VA)
  0x014aa0: ldr  r2, [r4, #0xe8]           ; ring base host address
  0x014aac: bl   pcie_hostca_to_devva
  0x014ab0: str  r0, [r5, #0x10]           ; SR+0x10 = ring base device address
  0x014ab8: ldr  r3, [r4, #8]              ; channel config
  0x014ac0: ldrb r3, [r3, #4]              ; +4 = ring depth
  0x014ac4: ldr  r1, [r2, #0x14]
  0x014ac8: sub  r3, r3, #1
  0x014acc: bfi  r1, r3, #0, #0xa          ; SR+0x14[9:0] = depth-1
  0x014ad0: str  r1, [r2, #0x14]
  0x014ad4: ldr  r3, [r4, #0xdc]
  0x014ad8: ldr  r2, [r4, #0xc]
  0x014adc: str  r2, [r3, #0x18]           ; SR+0x18 = [inst+0xc] (write pointer)
  0x014ae4: ldr  r1, [r4, #8]
  0x014ae8: ldr  r2, [r3, #8]
  0x014aec: ldrb r1, [r1, #5]
  0x014af0: bfi  r2, r1, #0, #3            ; SR+0x08[2:0] = cfg[5]
  0x014af4: str  r2, [r3, #8]
```

`pcie_ete_dr_reg_init` @ `0x1483c` (`r6 = [r1+0x50]` = DR register block host VA):

```
  0x014854: ldr  r6, [r1, #0x50]           ; DR register block base (host VA)
  0x01487c: ldr  r0, [r3]                  ; chip/device
  0x014884: bl   pcie_hostca_to_devva
  0x014888: str  r0, [r6, #0x30]           ; DR+0x30 = ring base device address
  0x014898: ldrb r3, [r3, #4]              ; depth
  0x01489c: ldr  r1, [r2, #0x34]
  0x0148a0: sub  r3, r3, #1
  0x0148a4: bfi  r1, r3, #0, #0xa          ; DR+0x34[9:0] = depth-1
  0x0148a8: str  r1, [r2, #0x34]
  0x0148b0: ldr  r2, [r4, #0x1c]
  0x0148b4: str  r2, [r3, #0x38]           ; DR+0x38 = [inst+0x1c]
```

| register | offset | width | value | evidence |
|---|---|---|---|---|
| SR control | `+0x08` | 3 bits | channel/queue select (`cfg[5]`) | `pcie_ete_sr_reg_init` |
| SR ring base | `+0x10` | 32 | device address (`pcie_hostca_to_devva`) | `pcie_ete_sr_reg_init` |
| SR depth | `+0x14` | 10 bits | depth − 1 | `pcie_ete_sr_reg_init` |
| SR write ptr | `+0x18` | 32 | `[inst+0xc]` | `pcie_ete_sr_reg_init` |
| DR ring base | `+0x30` | 32 | device address | `pcie_ete_dr_reg_init` |
| DR depth | `+0x34` | 10 bits | depth − 1 | `pcie_ete_dr_reg_init` |
| DR pointer | `+0x38` | 32 | `[inst+0x1c]` | `pcie_ete_dr_reg_init` |
| channel res | `+0x2e8` | 32 | read/clear `& 0xfffffc20`, write back | `pcie_ete_chn_res` |

`pcie_ete_chn_res` @ `0x7490` (per channel, clear bits then write back):

```
  0x0074fc: ldr  r8, [r3, #0x2e8]
  0x007500: dsb  sy
  0x007504: and  r8, r8, sb      ; sb = 0xfffffc20
  0x007508: dsb  st
  0x00750c: bl   arm_heavy_mb
  0x007520: str  r8, [r2, #0x2e8]
```

**[inferred]** The block bases `[inst+0xdc]` (SR) and `[inst+0x50]` (DR) are the host VAs of one
per-chip ETE register block, obtained in `pcie_ete_rings_init` from
`oal_pcie_inbound_ca_to_va(chip, [res])` where `res = get_pcie_ete_res()` (A.9). The offsets
`+0x10`/`+0x14` are also the two message-register CAs of `shuangta_pcie_msg_reg_map`
(`0x40039010`/`0x40039014`, A.7) - see A.9 for why the block CA itself is runtime-only.

### A.5 The descriptor node format - 8 bytes, two words **[proven]**

Every ETE descriptor array is an array of 8-byte nodes. Node size, address, length and flags:

```
shuangta_ete_sr_get_nodesize   @0x17654:
  0x017654: mov  r0, #8
  0x017658: bx   lr

shuangta_ete_sr_get_dscr_addr  @0x17670:      ; r0 = instance, r1 = index
  0x017674: ldr  r2, [r0]                     ; r2 = instance->node_array
  0x01767c: ldr  r0, [r2, r3, lsl #3]         ; node[index].word0

shuangta_ete_sr_get_dscr_len   @0x17684:
  0x017684: ldr  r3, [r0]
  0x017688: add  r3, r3, r1, lsl #3
  0x01768c: ldr  r0, [r3, #4]                 ; node[index].word1
  0x017690: lsr  r0, r0, #0x10                ; length = word1 >> 16

shuangta_ete_sr_get_dscr_flag  @0x17698:
  0x01769c: add  r3, r3, r1, lsl #3
  0x0176a0: ldr  r0, [r3, #4]
  0x0176a4: ubfx r0, r0, #0, #0xd             ; flags = word1[12:0]

shuangta_ete_dr_set_sr_dscr_flag @0x176e8:
  0x0176f0: ldr  r3, [r1, #4]
  0x0176f4: bfi  r3, r2, #0, #0xd             ; write flags into word1[12:0]
  0x0176f8: str  r3, [r1, #4]
```

The host-fill path `shuangta_ete_sr_dscr_fill` @ `0x17858` sets the address (arg2) and builds
`word1 = (len << 16) | 0x6000 | 0xd2b` (or `| 0x0d2b`), then advances the ring pointer and
**rings the message doorbell with id 3**:

```
  0x017868: uxth r3, r3                     ; r3 = length (16 bit)
  0x017884: bfi  r1, r3, #0x10, #0x10       ; word1[31:16] = length
  0x01789c: movw r1, #0xd2b                 ; host-fill magic
  0x0178a0: orr  r2, r2, #0x4000            ; owner/valid bit
  0x0178ac: orr  r2, r2, #0x2000            ; owner/valid bit
  0x0178b8: bfi  r2, r1, #0, #0xd           ; word1[12:0] = 0xd2b
  0x0178cc: str  r1(addr), [r3, r2, lsl #3] ; node[index].word0 = data device address
  0x0178e0: str  r2, [r3, #4]               ; node[index].word1 = len<<16 | 0x6d2b
  0x0178ec: bl   pcie_ete_ring_ptr_plus     ; advance producer index
  0x0178f8: bl   pcie_msg_send              ; r1 = #3  -> doorbell
```

And `pcie_ete_sending_trigger` @ `0x13f90` tests a descriptor's flag against that magic:

```
  0x014050: ldr  r3, [r3, #0x3c]            ; cbs->get_dscr_flag
  0x014054: blx  r3
  0x014058: movw r3, #0xd2b
  0x01405c: cmp  r0, r3                     ; host-filled node?
  0x014060: bne  #0x143c4
```

Descriptor node, final form:

| word | bits | field | evidence |
|---|---|---|---|
| node[0] | 31:0 | data buffer **device (ACP) address** | `sr_get_dscr_addr`; `pcie_get_ete_addr`/`memmap_get_dev_acp_addr` in `pcie_ete_sending_trigger` |
| node[1] | 31:16 | **length** in bytes | `sr_get_dscr_len` (`>>16`) |
| node[1] | 14 | owner/valid bit (set on host fill) | `orr #0x4000` in `sr_dscr_fill` |
| node[1] | 13 | owner/valid bit (set on host fill) | `orr #0x2000` in `sr_dscr_fill` |
| node[1] | 12:0 | **flags**; `0xd2b` = host-filled | `sr_get_dscr_flag` (`&0x1fff`), `movw #0xd2b` |

### A.6 Ring head/tail pointer encoding - 11 bits, index + phase **[proven]**

Ring position fields (`[inst+0xc]`, `[inst+0x18]`, `[inst+0x1c]`, `[inst+0x20]`, `[inst+0x24]`,
`[inst+0x28]`, `[inst+0x30]`) are packed as `index[9:0] | phase[10]`. `pcie_ete_ring_ptr_plus`
@ `0x13ef8` increments the index and toggles the phase bit on wrap:

```
  0x013ef8: ldr  r3, [r0]
  0x013efc: add  r2, r3, #1
  0x013f00: bfi  r3, r2, #0, #0xa          ; index = index+1
  0x013f04: ubfx r2, r3, #0, #0xa
  0x013f08: cmp  r2, r1                     ; r1 = depth
  0x013f0c: bfceq r3, #0, #0xa             ; wrap index to 0
  0x013f10: ubfxeq r2, r3, #0xa, #1
  0x013f14: eoreq r2, r2, #1              ; toggle phase
  0x013f18: bfieq r3, r2, #0xa, #1
  0x013f1c: str  r3, [r0]
```

Consumers test the phase bit with `ands ..., #0x400` / `tst ..., #0x400` (bit 10): e.g.
`pcie_ete_rcv_buff_check` @ `0x14dd0`/`0x14e14`, `pcie_ete_dr_get_uploadbuf` @ `0x151d4`,
`pcie_ete_tx_queue_handle` @ `0x15670`.

### A.7 Submit and doorbell: the message registers **[proven]**

The engine's submit primitive is `pcie_msg_send(chip, id)`. `pcie_msg_send` @ `0x160f4` keeps a
global message context (`r6 = [.LANCHOR0+4]`), a pending-mask word at `+0x48` and two mapped
register pointers:

```
  0x016168: ldr  r3, [r6, #0x48]           ; pending bitmask
  0x016170: lsr  r2, r3, r4                ; r4 = message id
  0x016174: tst  r2, #1
  0x016184: orr  r3, r3, r1, lsl r4        ; set bit id
  0x016188: str  r3, [r6, #0x48]
  0x016194: ldr  r2, [r6, #0x2c]           ; msg register 0 (host VA)
  0x01619c: str  r3, [r2]                  ; write the pending mask
  0x0161a4: ldr  r2, [r6, #0x34]           ; msg register 2 (host VA)
  0x0161a8: ldr  r3, [r2]
  0x0161ac: orr  r3, r3, r1
  0x0161b0: str  r3, [r2]                  ; DOORBELL: OR bit 0
```

Those pointers are the six device CAs mapped by `shuangta_pcie_msg_reg_map` @ `0x1b1a0` (stored
into `out[0..5]` = the message context `+0x2c..+0x40`):

| slot | device CA | msg ctx | role (evidence) |
|---|---|---|---|
| out[0] | `0x40039010` | `+0x2c` | H2D pending/message mask (`pcie_msg_send` writes it) |
| out[1] | `0x40039014` | `+0x30` | message register 1 |
| out[2] | `0x400392d4` | `+0x34` | **doorbell / trigger** (`pcie_msg_send`/`_irq` OR bit 0) |
| out[3] | `0x40101438` | `+0x38` | MAC-side message register (past BAR0+0x100000) |
| out[4] | `0x40101414` | `+0x3c` | MAC-side message register (past BAR0+0x100000) |
| out[5] | `0x400392f0` | `+0x40` | message register 5 (`pcie_msg_send_irq` writes 8) |

Registration (from `pcie_msg_init` @ `0xb6e4`): id 1 -> `pcie_dev_ready_msg_handle`, id 3 ->
`pcie_ete_transfer_done_handle`, ids 6/7 -> `pcie_trigger_ete_sending_handle`; `pcie_msg_init` also
zeros `*out[0]`/`*out[1]` (`str r8,[r3]` @ `0xb738`/`0xb740`).

**[proven]** The submit for the SR path is `pcie_msg_send(chip, 3)` (A.5); the RX-reclaim path
`pcie_ete_rcv_buff_check` @ `0x15138` uses `pcie_msg_send(chip, 5)`:

```
  0x015138: ldr  r3, [r4, #0x68]
  0x01513c: mov  r1, #5
  0x015140: ldr  r0, [r3, #0x80]
  0x015144: bl   pcie_msg_send
```

### A.8 Completion **[proven structure]**

The device raises an interrupt; `pcie_thread_handle`/`pcie_msg_handle` dispatch the pending mask to
registered handlers. `pcie_msg_handle` @ `0x171f8`:

```
  0x01725c: ldr  r5, [r3]                  ; r5 = pending mask (read)
  0x017260: str  r2, [r3]                  ; clear
  0x0172b4: rbit r6, r5
  0x0172b8: clz  r6, r6                    ; lowest set bit = message id
  0x0172d8: ldr  r3, [r4, #0x20]           ; handler table
  0x0172dc: ldr  sl, [r3, r6, lsl #3]      ; handler[id].fn
  0x0172f0: blx  sl                        ; handler[id].arg in r0
```

The two interrupt-dispatch tables are walked by `pcie_ete_h2d_isr_handle` @ `0x15a84`
(`[chip+0x20]` first, `[chip+0x28]` second) and `pcie_ete_d2h_isr_handle` @ `0x15c1c`
(`[chip+0x1c]`, `[chip+0x24]`), each iterating set bits with `rbit`/`clz`:

```
  0x015ab8: ldr  r7, [r6, #0x20]
  0x015abc: rbit r5, r4
  0x015ac0: clz  r5, r5
  0x015ad4: blx  r7
```

For a completed TX, `pcie_ete_transfer_done_handle` -> `pcie_ete_tx_done_buff_handle` @ `0x14568`
walks the completed index range and returns the buffers:
`pcie_rls_coherence_addr` / `pcie_ete_dst_buff_check` (@ `0x145f4`/`0x14620`). For RX,
`pcie_ete_rcv_buff_check` @ `0x14d74` scans received nodes (`ldrh r3,[r5,#0xa]`; a magic `0x5a5a`)
and releases descriptors with `pcie_ete_rx_rls_dsc_res_no_unmap`; `pcie_ete_dr_get_uploadbuf`
@ `0x1515c` is the device-fill path (reads DR, advances `[inst+0x24]`). Completion is therefore
signalled by the device writing the message register and raising the interrupt; the host handler
clears the pending bit and recycles the ring.

### A.9 Why the queue is unreachable in the unowned state **[proven + inferred]**

`get_pcie_ete_res` @ `0x1aa7c` returns a per-chip resource from a runtime table:

```
  0x01aa9c: bl   get_chip_type
  0x01aaa8: ldrb r2, [sp, #3]              ; chip type
  0x01aaa8: movw r3, -> .LANCHOR0
  0x01aab4: ldr  r3, [r3, r2, lsl #2]      ; table[chip_type]
  0x01aab8: ldr  r0, [r3, #8]              ; -> ETE resource
```

`.LANCHOR0` here is a runtime (`.bss`/probe-filled) resource table - the same class as
`g_pci_chip_res` (`get_pci_chip_res` @ `0x6698` returns `.bss+0x3298`). Its first word is the ETE
register-block device CA, consumed by `pcie_ete_rings_init` via `oal_pcie_inbound_ca_to_va`. The
two static region descriptors that resemble it are `.data+0x1fa8 = {0x40039000, 0x40039800,
0x40037000, 0x40038000}` and `.data+0x2944 = {0x4003a000, 0x40039508}`; **[inferred]** the ETE
block is one of these PCIe-glue regions (the `+0x10`/`+0x14` SR registers line up with the
`0x40039010`/`0x40039014` message CAs), but which one, and its host mapping, is set up only by
`pcie_ete_init`/`pcie_msg_init` under the vendor stack. **With the vendor stack hidden there is no
resource, no ring base, no depth, no mapped block and no message thread - so a descriptor cannot be
submitted, and Part B does not try.**

### A.10 Full-write chain (summary)

| # | step | layer | evidence |
|---|---|---|---|
| 1 | `firmware_file_send(chip, 0x01240000+off, buf, n)` | plat | phase 16 A.9 |
| 2 | `bal_write` tail-calls `[bus_ctx+0x38](handle, ...)` | BAL | A.1 |
| 3 | HCC TX thread -> `hcc_queue_tx_process` -> `bal_port_start_xfer` -> `[ctx+0x18]` | HCC | A.2 |
| 4 | skb -> ETE node: `{dev_addr, len<<16 | 0x6d2b}` in the 8-byte ring array | ETE | A.5 |
| 5 | `pcie_msg_send(chip, 3)`: pending mask -> `0x40039010`, doorbell -> `0x400392d4` | MSG | A.7 |
| 6 | device DMAs; IRQ -> `pcie_msg_handle` dispatch -> `pcie_ete_transfer_done_handle` | IRQ | A.8 |
| 7 | `pcie_ete_tx_done_buff_handle` / `pcie_ete_rcv_buff_check` recycle the ring | ETE | A.8 |

The per-chip firmware base `0x01240000` (phase 16 A.9) lies inside the `0x01200000-0x01417fff`
device window; the ETE node's address field carries the **device (ACP) address of the host buffer**
the engine reads, while the write target is the HCC/BAL message header **[inferred]**.

---


## Part B - our implementation

`lab/eteprobe/eteprobe.c` (`lab/eteprobe/Makefile`: `obj-m := eteprobe.o`). Built by the existing
GitHub Actions workflow (`.github/workflows/build-load-test-module.yml`, step "build eteprobe
module", artifact `eteprobe-ko`). CI run `36854583613` (commit `76a4dd6`); `eteprobe.ko` md5
`e88b32ba4717a13ddf4fa71e53fc78db`, `vermagic=5.10.201 SMP mod_unload ARMv7`.

What it does, in order, and why each step is safe on an unowned chip:

1. Claim the endpoint exactly as `epinit` stage 1 does: `pci_enable_device`,
   `pci_request_mem_regions`, then the vendor's documented `pci_write_config_word(dev, 4, 7)`,
   read back. It refuses to continue if the regions are busy (i.e. if the vendor stack is loaded).
2. **Coherent DMA buffer + known pattern.** `pci_set_consistent_dma_mask(32)` then
   `dma_alloc_coherent(&dev->dev, dmalen=65536)`, fill with `word[k] = 0xa5a50000 | (k & 0xffff)`
   and a `0xdeadbeef` end marker, then `dma_sync_single_for_device`/`_for_cpu` and a full CPU
   read-back: CRC32 and a per-word match count. This is host memory only - no device register is
   written.
3. **Read-only ETE/glue register probe.** Map `BAR0+0x39000` (0x1000) and `BAR0+0x3a000` (0x1000),
   `ioread32` the SR/DR program registers (`+0x08/0x10/0x14/0x18`, `+0x30/0x34/0x38`), the message
   registers (`+0x10/+0x14`, `+0x2d4`, `+0x2f0`), the channel-res register (`+0x2e8`), the first
   0x40 words of the block and the `+0x2c0` sub-block, and the phase-13 "ETE queue pointer" pairs
   at `+0x840`. The message doorbell register (`+0x2d4`) is read before and after the rest; a read
   has no side effects, so this is the closest thing to a "manual doorbell" interaction that is
   safe. Also logs the ROM vector page (`BAR0+0x0`).
4. **Stop at the boundary.** It never writes BAR0, never writes a descriptor, never starts a CPU,
   never touches `BAR0+0x40000..` or BAR2/BAR4. A `submit=1` module parameter exists only to
   document the boundary: when set it prints the refusal (A.9) and does nothing.

Why not submit: the descriptor *format* is unambiguous (A.5), but the *queue* is not reachable in
the boot-time takeover state. The ETE ring base, the depth/head/tail program registers and the
doorbell are created by `pcie_ete_init`/`pcie_msg_init` from the per-chip `get_pcie_ete_res`
resource (A.9); with the vendor stack hidden that resource, the mapped block, the ISR tables and
the message thread do not exist. Writing a descriptor would mean writing a queue the engine has
never been told about - the same class of mistake as the phase-16 raw `memcpy`. The test boot
confirms it empirically: every SR/DR program register reads back `0x00000000` (section C).

### Recovery command - recorded on the device BEFORE the test reboot

Installed at `/root/recover-eteprobe.sh` (and staged in
`build/register-dumps/ete/stage/recover-eteprobe.sh`) and run as
`sh /root/recover-eteprobe.sh`:

```sh
#!/bin/sh
set -x
cd /lib/modules/5.10.201 || exit 1
[ -f hi5622v100_wifi.ko.omo-off ] && mv -f hi5622v100_wifi.ko.omo-off hi5622v100_wifi.ko
[ -f hi5622v100_plat.ko.omo-off ] && mv -f hi5622v100_plat.ko.omo-off hi5622v100_plat.ko
rm -f /etc/rc.d/S99omo-eteprobe
rm -f /etc/init.d/omo-eteprobe
rm -f /lib/modules/5.10.201/eteprobe.ko
sync
reboot
```

The one-shot loader (`/etc/init.d/omo-eteprobe`, `START=99`) deletes its own rc.d symlink before
`insmod`, the same watchdog safety as phase 16: a hang resets into a reachable boot with the
vendor modules still hidden but no `eteprobe`, so the recovery command can run. Fallback if the
box does not come back: U-Boot slot A (stock).

---

## Part C - test record

Artifacts in `build/register-dumps/ete/` (gitignored): `000_baseline.txt`, `010_staging.txt`,
`015_live_precheck.txt`, `020_testboot_evidence.txt`, `030_recovery_run.txt`,
`040_recovery_evidence.txt`, `050_final_state.txt`, plus `eteprobe.ko`.

### C.1 Live pre-check (normal state, vendor loaded) - `015_live_precheck.txt`

`insmod /tmp/eteprobe.ko` in the normal vendor state loaded the module and refused safely, exactly
as designed:

    md5sum /tmp/eteprobe.ko e88b32ba4717a13ddf4fa71e53fc78db  (== CI artifact)
    [  488.005233] omo-eteprobe: pci_enable_device rc=0 command 0x0006 -> 0x0006
    [  488.020253] omo-eteprobe: pci_request_mem_regions rc=-16 (vendor stack loaded?) - refusing

No DMA allocation, no BAR0 write, module unloaded cleanly. This validates the module's link/ABI
and the region guard without a reboot.

### C.2 Staging (before the reboot) - `010_staging.txt`

    eteprobe.ko md5 e88b32ba4717a13ddf4fa71e53fc78db  (== CI artifact)
    hi5622v100_wifi.ko.omo-off  e21629d226ec7de9a860a8955952d311  (== baseline)
    hi5622v100_plat.ko.omo-off  23660bc285393e678d5cade1c36c194b  (== baseline)
    /etc/rc.d/S99omo-eteprobe -> ../init.d/omo-eteprobe
    sh -n on loader + recovery script: OK

### C.3 Test boot - vendor hidden, `eteprobe` via S99 - `020_testboot_evidence.txt`

    --- (a) vendor stack absent, ours present ---
    [vendor]:  (none: hi5622v100_wifi + hi5622v100_plat + rox_pci0 absent)
    [hi_pcie]: hi_pcie  20480  0
    [eteprobe]: eteprobe 16384  0

    --- (b) claim + coherent DMA buffer ---
    [   38.994627] omo-eteprobe: pci_enable_device rc=0 command 0x0140 -> 0x0142
    [   39.001459] omo-eteprobe: pci_request_mem_regions rc=0 (MEM BARs claimed)
    [   39.008248] omo-eteprobe: cfg[0x004] <= 0x0007 readback=0x0006 MEM|MASTER=set
    [   39.015406] omo-eteprobe: BAR0 base=0x40000000 (config-space read)
    [   39.021674] omo-eteprobe: ROM vector page BAR0+0x0: 00000101 00000110 00000002 00000000 ...
    [   39.034965] omo-eteprobe: pci_set_dma_mask(32) rc=-5
    [   39.040152] omo-eteprobe: pci_set_consistent_dma_mask(32) rc=0
    [   39.057664] omo-eteprobe: dma_alloc_coherent size=65536 virt=c9044000 dma=0x83ac0000 crc32=0x982bd313
    [   39.066905] omo-eteprobe: pattern[0]=0xa5a50000 pattern[1]=0xa5a50001 pattern[last]=0xdeadbeef
    [   39.089090] omo-eteprobe: coherent readback crc32=0x982bd313 match=YES mismatches=0 first=0

    --- (c) read-only ETE register state ---
    [   39.097486] ETE[+0x2d4] msg2/doorbell      = 0x00000000 (BAR0+0x392d4)
    [   39.111117] ETE[+0x008] SR ctrl[2:0]       = 0x00000000 (BAR0+0x39008)
    [   39.118826] ETE[+0x010] SR ring base       = 0x00000000 (BAR0+0x39010)
    [   39.126554] ETE[+0x014] SR depth-1         = 0x00000000 (BAR0+0x39014)
    [   39.134316] ETE[+0x018] SR wptr            = 0x00000000 (BAR0+0x39018)
    [   39.142091] ETE[+0x030] DR ring base       = 0x00000000 (BAR0+0x39030)
    [   39.150007] ETE[+0x034] DR depth-1         = 0x00000000 (BAR0+0x39034)
    [   39.157739] ETE[+0x038] DR wptr            = 0x00000000 (BAR0+0x39038)
    [   39.181355] ETE[+0x2e8] channel res        = 0x000003ff (BAR0+0x392e8)
    [   39.196869] ETE block +0x000 BAR0+0x39000: 0000010b 00000000 ... 00000000
    [   39.214947] ETE block +0x2c0 BAR0+0x392c0: 00000000 00000000 00000000 00000000 00000005
                 00000000 ... 000003ff 00000000 00000000 00000002 00000000 00000000
    [   39.232808] ETE[+0x2d4] msg2/doorbell (after) = 0x00000000
    [   39.240788] omo-eteprobe: doorbell read stable=YES (0x00000000 -> 0x00000000)
    [   39.254544] ETE remap +0x840 BAR0+0x3a840: 00002800 00002800 00028000 00000a00 00028000 00000a00 00000a00 00000a00
    [   39.266665] omo-eteprobe: descriptor format is known ... but the queue is NOT reachable in the unowned state; no descriptor submitted, no BAR0 write
    [   39.283193] omo-eteprobe: done - endpoint claimed, coherent DMA buffer verified, ETE register state logged (read-only)

    --- (d) what binds the endpoints ---
    0000:00:00.0: /sys/bus/pci/devices/0000:00:00.0/driver: No such file or directory  (enable=1, ours)
    0001:00:00.0: /sys/bus/pci/devices/0001:00:00.0/driver: No such file or directory  (enable=0, untouched)
    /sys/bus/pci/drivers/rox_pci0: No such file or directory

    --- (e) reachability / stability ---
    br-lan inet 192.168.10.1/24; no Wiphy, 0 wlan ifaces (wifi down by design)
    S99 rc.d link: removed by the script before insmod (self-delete worked)
    pstore: no new record (blk-0/2/3 mtimes 10:41/10:26/10:34, all pre-test)
    PC ping 192.168.10.1: 2/2, 0% loss; uptime kept climbing (81 s at capture); no panic

Reading:

- **The claim and the DMA buffer are proven.** `dma_alloc_coherent` returned `dma=0x83ac0000`
  (64 KiB), the pattern wrote and read back identically (`crc32=0x982bd313`, `match=YES`,
  `mismatches=0`) across a for-device/for-cpu sync pair.
- **The ETE engine is uninitialized in the takeover state, exactly as A.9 predicts.** Every SR/DR
  program register - ring base, depth, write pointer, control - reads `0x00000000`; only the
  channel-res register `+0x2e8 = 0x000003ff` and the block's `+0x000 = 0x0000010b` (the same
  `0x010b`-class glue word phase 13/`reg_all.txt` records at CA `0x40039000`) are non-zero. There
  is no ring base, no depth and no doorbell state: submitting would be meaningless and unsafe.
  That is the empirical proof for the Part-B stop.
- The `+0x3a840` words (`00002800 00002800 00028000 00000a00 ...`) are the `{pointer,index}`
  shaped "ETE queue pointer" pairs phase 13 ranked first; they are readable and static.
- **Nothing binds the endpoints.** `rox_pci0` does not exist; `0000:00:00.0` has no `driver`
  symlink and `enable=1` (our `pci_enable_device`); `0001:00:00.0` is untouched.
- One panic boundary, one reboot, no hazard: no BAR0 write, no pstore record.

### C.4 Recovery - `030_recovery_run.txt`, `040_recovery_evidence.txt`, `050_final_state.txt`

`sh /root/recover-eteprobe.sh` renamed the modules back, removed the loader, the module and the
rc.d symlink, `sync`, `reboot`. The recovered boot:

    hi5622v100_plat       323584  3 hi5622v100_wifi      (baseline use counts)
    hi5622v100_wifi      3387392  1
    md5 hi5622v100_wifi.ko e21629d226ec7de9a860a8955952d311  (baseline)
    md5 hi5622v100_plat.ko 23660bc285393e678d5cade1c36c194b  (baseline)
    leftovers: eteprobe.ko / *.omo-off / S99omo-eteprobe / init.d/omo-eteprobe all absent
    0000:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
    0001:00:00.0 -> /sys/bus/pci/drivers/rox_pci0
    Wiphy phy0 + phy1; 6 wlan interfaces (vap0 Cudy-1C73, vap8 Cudy-1C73-5G, ...); /tmp/wifi_done present
    softapd + hostapd running; /var/log/hi5622v100.log: "calibration init done."
    br-lan 192.168.10.1/24 up; PC ping 192.168.10.1: 3/3, 0% loss
    pstore: no new record

(The `iwpriv Hisilicon0 alg` private ioctl returns `[FAIL]` with no argument both before the test
and after recovery - it is `set 500 char & get 1000 char`, i.e. it needs an argument; it is not a
radio-health signal here. The radios are verified by the wiphy/interface/SSID inventory, the
running AP daemons and the vendor calibration log.)

**Acceptance state reached: the router is healthy with the vendor stack restored, both radios up,
no leftovers, no new pstore record.**

---

## Limits, stated plainly

- **No descriptor is submitted.** The descriptor node format is unambiguous (A.5), but the ETE
  ring base, depth/head/tail registers and doorbell are `pcie_ete_init`/`pcie_msg_init` runtime
  resources (`get_pcie_ete_res`) that do not exist when the vendor stack is hidden. The test boot
  confirms every SR/DR program register reads 0. `eteprobe` therefore stops at the first unsafe
  step, exactly as the brief allows.
- **The ETE register-block CA is inferred, not proven.** `pcie_ete_rings_init` maps
  `oal_pcie_inbound_ca_to_va(chip, [get_pcie_ete_res()])`; the CA lives in a probe-filled resource
  table, so it is not a file constant. Part B probes the two candidate `BAR0` windows
  (`0x39000`, `0x3a000`) read-only rather than asserting a base.
- **`pci_set_dma_mask(32)` returns `-EIO`** on this unbound endpoint, but
  `pci_set_consistent_dma_mask(32)` succeeds and the coherent allocation works; streaming DMA is
  not exercised (no descriptor is submitted).
- **One boot, Wi-Fi intentionally down.** During the test boot there is no `hi5622v100_wifi`,
  no `hi5622v100_plat`, no `rox_pci0`, no Wiphy; the LAN/SSH path never dropped.
- **One endpoint touched.** `domain=0` (`0000:00:00.0`); `0001:00:00.0` is left unbound.

---

## Next step

With the transport structure documented, the next stage is to reproduce the vendor's engine
*bring-up* rather than probe it: reimplement `pcie_ete_init` (rings, channel arrays, the SR/DR
program registers) and `pcie_msg_init` (the six message registers, the doorbell, the handler
table) on the claimed endpoint, then submit a single descriptor through `pcie_msg_send(chip, 3)`
and watch for `pcie_ete_transfer_done_handle`. The unambiguous parts needed for that - the 8-byte
node, the 11-bit index+phase pointer, the SR/DR register set, the six message CAs and the
`pcie_msg_send` id-3 submit - are all in Part A. The unknown that gates it is the ETE register-block
CA in `get_pcie_ete_res`; the first concrete task is to recover that resource table (from the
vendor's probe path or a live register dump of the vendor stack's mapped window) so the block can
be programmed without guessing.
