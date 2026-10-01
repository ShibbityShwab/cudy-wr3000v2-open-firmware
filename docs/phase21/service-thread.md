# service-thread: the vendor's PCIe/HCC service worker, and the takeover's minimal loop (phase 21, 2026-10-01)

Task `st_01a0f8c8`. Direct sequel to `docs/phase20/fw-accept.md`, which pinned the missing
device-side accept to "the firmware dispatcher is reached only through the device PCIe glue ISR /
`pcie_thread`". This phase recovers the **host** side of that worker from `hi5622v100_plat.ko`
(`pcie_process_thread` @`0x16efc` / `pcie_thread_handle` @`0x16b00` and everything they call), names
exactly which of its steps our takeover modules have never performed, and implements the minimal
subset as a kthread in `lab/svc/svc.c` to test whether the chip's message service then takes our
frame.

**Headline.** The vendor's worker is a real kernel thread: `pcie_thread_init` @`0x170b8` builds a
`waitqueue` at `comm+0x18` plus a condition flag at `comm+0x28`, creates `pcie_process_thread` with
`kthread_create_on_node`, and every wake runs `pcie_thread_handle`, which (a) releases TX buffers,
(b) pumps the ETE **SR** ring (`pcie_ete_sending_trigger`), (c) consumes the ETE **DR** ring
(`pcie_ete_tx_queue_handle`), and (d) services DMA completions/glue. The host→device mailbox
**dispatcher** `pcie_msg_handle` @`0x171f8` is *not* on that thread - it runs from the ISR path - and
it consumes `out[1]` (CA `0x40039014`), writes the ack `0x40101438` and the re-arm `0x40101414`, then
dispatches the lowest set bit of the pending mask through the handler table at `comm+0x4c`
(ids 1/3/6/7). The ETE glue status is the word at ETE-private `+0x2ec`, masked `0x3d8`, cleared
write-1-to-clear (`oal_pcie_transfer_done` @`0x83e4`; `pcie_intr_handle` @`0x82e4`).

Every claim is **[proven]** (a constant/relocation/control-flow in the instruction stream) or
**[inferred]**.

Artifacts (regenerable):

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko   # md5 23660bc285393e678d5cade1c36c194b
$PY lab/ko_disasm.py $KO pcie_process_thread pcie_thread_handle pcie_thread_init \
    pcie_comm_init pcie_wkup_thread pcie_wait_condtion > build/tmp/phase21/service_core.txt
$PY lab/ko_disasm.py $KO pcie_msg_handle pcie_msg_send pcie_msg_send_irq \
    pcie_msg_wait_for_clr pcie_msg_register pcie_ete_transfer_done_handle \
    oal_pcie_transfer_done > build/tmp/phase21/msg.txt
$PY lab/ko_disasm.py $KO pcie_ete_rcv_buff_check pcie_ete_sending_trigger \
    pcie_ete_h2d_isr_handle pcie_ete_d2h_isr_handle pcie_ete_tx_buf_rls \
    pcie_ete_tx_queue_handle pcie_intr_handle oal_pcie_intx_isr \
    pcie_ete_dr_init pcie_ete_sr_init pcie_ete_intr_init \
    pcie_msg_init shuangta_pcie_msg_reg_map > build/tmp/phase21/{glue,rings,mbox}.txt
```

`plat.ko` is a relocatable ARM object (`.text` at offset 0). All addresses below are **`.text`
offsets** (verified by capstone `CS_ARCH_ARM` with the `.rel.text` map applied, which is how the
`bl` targets and `movw/movt` symbol pairs are named); symbol sizes are from `.symtab`.

---

## Part A - the vendor's service thread in full

### A.0 The thread object `comm` and the wait

The worker's state lives in one structure (`comm`), the same object `pcie_msg_init` @`0xb6e4`
fills. Its relevant fields, proven by the instructions that touch them:

| field | meaning | first seen |
| ----- | ------- | ---------- |
| `comm+0x18` | `wait_queue_head` | `pcie_thread_init` @`0x17118` |
| `comm+0x24` | `task_struct *` (the kthread) | `pcie_thread_init` @`0x17170` |
| `comm+0x28` | condition flag ("work pending") | `pcie_thread_init` @`0x17100` |
| `comm+0x2c..0x40` | the six mailbox register pointers (ctx) | `shuangta_pcie_msg_reg_map`/`pcie_msg_init` |
| `comm+0x4c` | 11-entry `{fn,arg}` message-handler table | `pcie_msg_init` @`0xb764` |

`pcie_comm_init` @`0x171cc` stores the per-chip comm pointer into the global anchor and calls
`pcie_thread_init` then `pcie_msg_init`:

```
  0x0171d0: movw r3, #0        ; -> .LANCHOR0
  0x0171d8: ldr  r2, [r0, #0x54]        ; chip index
  0x0171e0: add  r3, r3, r2, lsl #2
  0x0171e4: str  r0, [r3, #4]           ; g_pcie_priv[chip] = comm
  0x0171e8: bl   pcie_thread_init
  0x0171f4: b    pcie_msg_init
```
**[proven]**

### A.1 `pcie_thread_init` @`0x170b8` - stand up the kthread

```
  0x0170fc: str  r3(=0), [r4, #0x28]    ; cond = 0
  0x017104: mov  r3, #2
  0x01710c: str  r3, [sp, #0x10]        ; sched policy/prio args
  0x017114: str  r3, [sp, #0xc]         ; prio = 0x62 (98)
  0x017118: bl   __init_waitqueue_head  ; waitqueue at r4+0x18
  0x017138: movw r0, #0 -> pcie_process_thread
  0x017140: bl   kthread_create_on_node ; fn=pcie_process_thread, arg=r4
  0x017160: bl   kthread_bind           ; bind to cpu = [sp,#0x14] (= -1 -> skip)
  0x017168: bl   wake_up_process
  0x017170: str  r5, [r4, #0x24]        ; comm+0x24 = task
```
`r4 = comm`. **[proven]**

### A.2 `pcie_process_thread` @`0x16efc` - the loop

```
  0x016efc: push {r4,r5,r6,r7,r8,lr}; sub sp,#0x20
  0x016f00: subs r7, r0, #0            ; r7 = comm (kthread arg)
  0x016f18: beq  #0x17050              ; NULL -> log + return 2
  0x016f1c: add  r6, r7, #0x28         ; r6 = &comm->cond (+0x28)
  0x016f34: bl   kthread_should_stop
  0x016f38: cmp  r0, #0; bne #0x17008  ; -> exit (log, return 0)
  0x016f40: mov  r0, r6
  0x016f44: bl   pcie_wait_condtion    ; returns 1 and clears the flag
  0x016f48: subs r1, r0, #0; bne #0x16f24   ; work pending -> handle
  0x016f50: add  r0, sp, #8
  0x016f54: add  r8, r7, #0x18         ; r8 = &comm->wq (+0x18)
  0x016f58: bl   init_wait_entry
  0x016f6c: ... 
  0x016f78: bl   prepare_to_wait_event  ; wait_event_interruptible on comm+0x18
  0x016f84: bl   pcie_wait_condtion
  0x016f8c: beq  #0x16f60              ; still 0 -> schedule()
  0x016f98: bl   finish_wait
  0x016f9c: b    #0x16f24
  0x016f24: ldr  r3, [r7]              ; comm->[0]
  0x016f28: ldr  r3, [r3]              ; comm->[0]->[0]
  0x016f2c: ldr  r0, [r3]              ; chip handle
  0x016f30: bl   pcie_thread_handle    ; <-- one iteration of real work
  0x016f34: b    #0x16f34              ; loop
```
`0x016fa0..0x16fe4` is the `-ERESTARTSYS` (`r5 == -0x200`) log path; `0x17008..0x17048` the
`kthread_should_stop` log path; `0x17050` the `comm == NULL` path. **[proven]**

So the loop is: **stop check -> condition check (with waitqueue sleep) -> `pcie_thread_handle` ->
repeat**. It has **no hrtimers of its own**; the only timer in this subsystem is the separate
`hcc_timer_*` set and the `oal_workqueue_*` helpers - the thread is purely event-driven.

`pcie_wait_condtion` @`0x13f24` is a 5-line test-and-clear:

```
  0x013f30: ldr  r0, [r4]              ; *cond
  0x013f34: cmp  r0, #1
  0x013f38: moveq r3, #0; streq r3, [r4]   ; consume the flag
  0x013f40: pop  {r4, pc}              ; returns 1 when it fired
```
**[proven]**

The pair that wakes it is `pcie_wkup_thread` @`0x1629c` (also the entire body of the vendor's id-6
handler `pcie_trigger_ete_sending_handle` @`0x15efc`, a 4-byte `b pcie_wkup_thread`):

```
  0x0162a8: mov  r2, #1
  0x0162ac: str  r2, [r4, #0x28]       ; comm+0x28 = 1  ("work pending")
  0x0162b0: add  r0, r4, #0x18         ; comm+0x18
  0x0162b4: mov  r1, r2                ; TASK_NORMAL
  0x0162bc: mov  r3, #0
  0x0162c0: b    __wake_up
```
**[proven]**

### A.3 `pcie_thread_handle` @`0x16b00` - the per-iteration work

```
  0x016b20: ldr  r3, [r4, #4]          ; comm->[4] = channel count
  0x016b34: ldr  r0, [r4, #0x14]       ; comm->[0x14] = channel array
  0x016b40: add  r5, r5, #0x114        ; stride 0x114
  0x016b44: bl   pcie_ete_tx_buf_rls   ; (1) release TX buffers
  ...
  0x016b54: movw sb, #0 -> .LANCHOR0
  0x016b5c: ldr  r5, [sb, #4]          ; g_pcie_priv[0]
  0x016b60: ldr  r3, [r5, #0x14]       ; priv->pending-wake flag
  0x016b64: cmp  r3, #1; beq #0x16df8  ; (2) priv+0x10=1; __wake_up(priv+4); priv+0x14=0
  0x016b6c: ldr  r3, [r4, #8]          ; comm->[8] = tx queue count
  0x016ba4: ldr  r5, [r4, #0x10]       ; comm->[0x10] = tx queue array
  0x016b84: mov  r1, r5; mov r0, r4
  0x016b8c: bl   pcie_ete_sending_trigger ; (3) pump the SR ring for that queue
  ...
  0x016c50: ldr  r5, [r4, #0x80]       ; comm->[0x80] = channel base
  0x016cbc: bl   _raw_spin_lock_irqsave ; chan+0xd8
  0x016cd0: bl   pcie_ete_tx_queue_handle ; (4) consume the DR ring for that channel
  0x016cdc: bl   _raw_spin_unlock_irqrestore
  ...
  0x016e68: ldr  r3, [sl, #0xdc]       ; completion object
  0x016e6c: ldr  r2, [r3, #0x30]
  0x016e84: bl   pcie_dev_addr_to_caller_addr ; (5) map a completion buffer
  0x016e90: streq r3, [sl, #4]
```
Each of steps (1),(3),(4) walks a per-channel/queue array with the block stride `0x114` (SR/TX) or
`0x6c` (DR queue list). **[proven]** for the calls and strides; the exact per-device meaning of
`comm->[4]/[8]/[0x10]/[0x80]` is **[inferred]** from the calls they feed.

### A.4 The ETE glue status handling

The DMA-completion ISR bottom half `oal_pcie_transfer_done` @`0x83e4`:

```
  0x0083f4: ldr  r3, [r4, #4]          ; dma ctx ->[4]
  0x008400: ldr  r3, [r4]              ; dma ctx ->[0]
  0x00840c: ldr  r3, [r3, #0xc]        ; ->[0xc] = the DMA/glue struct S
  0x008418: ldr  r5, [r3, #8]          ; r5 = DMA completion status word
  0x00841c: cmn  r5, #1; beq #0x8500   ; 0xffffffff -> error, cpsid i, return -1
  0x008464: bic  r5, r5, #0xff000000
  0x008468: bic  r5, r5, #0xe00000
  0x00846c: bic  r5, r5, #0xf800
  0x008474: bic  r5, r5, #0xf8         ; keep only 0x001f0707
  0x008478: and  r2, r5, #7            ; h2d sub-index
  0x00847c: ubfx r1, r5, #8, #3        ; h2d channel
  0x008480: ldr  r3, [r0, #4]
  0x008484: orr  r3, r3, r5
  0x008488: str  r3, [r0, #4]          ; W1C: OR the set bits into the glue word
  0x00848c: ldr  r0, [r4]; bl pcie_ete_h2d_isr_handle   ; (r1,r2 as decoded)
  0x008494: ldr  r0, [r4]; lsr r2, r5, #0x10; mov r1,#0
  0x0084a0: bl   pcie_ete_d2h_isr_handle
  0x0084a4: mov  r0, r4; bl pcie_intr_handle            ; dispatch the ETE glue bits
```
**[proven]**

`pcie_intr_handle` @`0x82e4` is the `+0x2ec & 0x3d8` dispatcher:

```
  0x0082f4: ldr  r3, [r6, #4]; cmp r3,#0; beq ret
  0x008300: ldr  r3, [r3]
  0x008304: ldr  r4, [r3, #0x2ec]      ; ETE glue status word
  0x008308: dsb  sy
  0x00830c: ands r4, r4, #0x3d8        ; bits 3,4,6,7,8,9
  0x008310: beq  ret
  0x008328: rbit r5, r4
  0x00832c: clz  r5, r5                ; lowest set bit
  0x008330: cmp  r5, #9; bgt ret
  0x00833c: ldr  r2, [r3, #0x48]       ; handler table at ctx+0x48 (8-byte entries)
  0x008348: ldr  r0, [r3, #0x4c]
  0x00834c: blx  r2                    ; fn(arg)
  0x008354: bics r4, r4, r3, lsl r5    ; clear and loop
```
**[proven]** for the register read, the `dsb`, the `0x3d8` mask, the `rbit/clz` lowest-bit loop and
the `ctx+0x48` table. The base (`[[ctx+4]]` = the ETE private/register block) is **[inferred]**.

The two ISR-bit dispatchers take `(ctx, r1_mask, r2_mask)` and run `ctx+0x20`/`ctx+0x28` handlers:

```
  pcie_ete_h2d_isr_handle @0x15a84:
    0x015ab8: ldr r7, [r6, #0x20]     ; h2d handler
    loop: 0x015ad4 blx r7; 0x015adc bics r4,r4,1<<r5; bne loop
    0x015b3c: ldr sl, [r6, #0x28]     ; d2h handler
    loop: 0x015b50 blx sl; 0x015b58 bics r8,r8,1<<r7; bne loop
```
`pcie_ete_intr_init` @`0x7528` installs the handlers and clears the glue control:

```
  0x007540: str  r0(=pcie_rx_handle),     [r1, #0x1c]
  0x00754c: strd r2(=pcie_tx_done_handle), r3(=pcie_ete_rx_err_handle), [r1, #0x20]
  0x007558: str  r3(=pcie_ete_tx_err_handle), [r1, #0x28]
  0x0075b4: and  r2, r2, r1            ; r2 = 0xffe0f8f8
  0x0075b8: str  r2, [r3]              ; RMW: keep only 0x001f0707 in the glue control
```
`0xffe0f8f8` is exactly the complement of `oal_pcie_transfer_done`'s kept bits `0x001f0707`, so
both functions operate on the same glue word. **[proven]** for the mask/table; base **[inferred]**.

`pcie_ete_transfer_done_handle` @`0x8730` is the message id-3 handler; with a non-NULL arg it
tail-calls the D2H dispatcher for all 31 channels:

```
  0x008738: mov  r2, #0
  0x00873c: mov  r1, #0x1f
  0x008740: b    pcie_ete_d2h_isr_handle
```
**[proven]**

### A.5 The SR/DR ring drive

**SR producer** - `pcie_ete_sending_trigger` @`0x13f90` (called per tx queue from
`pcie_thread_handle`, and as the id-6/7 message handler effect). It walks the ring with a packed
10-bit index + phase bit (`pcie_ete_ring_ptr_plus` @`0x13ef8`):

```
  0x013fd4: ldr  r3, [r4, #0x1c]       ; producer index (packed)
  0x013fd8: ubfx r3, r3, #0, #0xa      ; low 10 bits
  0x013fe0: ubfx r1, r8, #0, #0xa      ; peer/consumer low 10 bits
    ...
  0x014210: ldr  r3, [r4, #0x1c]
  0x014218: add  r1, r3, #1
  0x01421c: bfi  r3, r1, #0, #0xa      ; producer[9:0]++
  0x014220: ldrb r1, [r2, #4]          ; depth
  0x014228: cmp  r0, r1; bfieq r3,#0,#0xa           ; wrap to 0
  0x014230: ubfxeq r1, r3, #0xa, #1; eoreq r1,#1    ; toggle phase
  0x01423c: str  r3, [r4, #0x1c]       ; commit producer
    ...
  0x014408: ldr  r5, [r4, #0x50]       ; ring control object
  0x014430: str  r2, [r3, #0x38]       ; ctrl+0x38 = producer
  0x01443c: str  r2, [r3, #0x18]       ; ctrl+0x18 = producer
```
The descriptor fill `shuangta_ete_sr_dscr_fill` @`0x17858` sets `word1 = (len<<16)|0x6d2b` and then
calls `pcie_msg_send(chip, 3)` (@`0x178f8`) - the SR doorbell. Register init is
`pcie_ete_sr_reg_init` @`0x14a48` (`SR+0x08` ctrl, `+0x10` base, `+0x14` depth-1, `+0x18` wptr;
source `pcie_ete_sr_init` @`0x14b98`). **[proven]**

**DR consumer** - `pcie_ete_tx_queue_handle` @`0x155d4` (called per channel from
`pcie_thread_handle`, under `chan+0xd8`):

```
  0x0155d8: ldr  r5, [r1, #0x38]       ; pending RX count
  0x015644: ldr  r1, [r4, #0x18]       ; node index
  0x015654: ldr  r3, [r6, #0x18]; ldr r3, [r3, #0x24]; blx r3  ; get-dscr callback
  0x01566c: eor  r3, r3, r2; tst r3, #0x400   ; packed phase check
  0x01571c: ldr  r3, [r5, #4]; ldr r2, [r5]   ; pop node from list
  0x015734: ldrb r3, [r4, #0x1c]
  0x015738: ldr  r8, [r5, #0x118]      ; skb
  0x015784: bl   pcie_get_ete_addr
  0x015790: bl   memmap_get_dev_acp_addr
  0x0157b8: ldr  r7, [r3, #0x10]; blx r7     ; dr-dscr-fill callback
  0x0157d8: str  r5, [r4, #0x44]       ; append to done list
  0x0157f4: str  r3, [r4, #0xfc]       ; stat++
```
The buffer consumer `pcie_ete_rcv_buff_check` @`0x14d74` tests the deposited frame
(`ldrh [buf+0xa] == 0x5a5a`, `ldrh [buf+4] != 0`), releases the descriptor
(`pcie_ete_rx_rls_dsc_res_no_unmap` @`0x146dc`), and ends by ringing the **reclaim** doorbell:

```
  0x015060 / 0x0150a4 / 0x0150f0 / 0x01514c: return -0x8b2d (error sentinel)
  0x015138: ldr  r3, [r4, #0x68]
  0x01513c: mov  r1, #5
  0x015140: ldr  r0, [r3, #0x80]
  0x015144: bl   pcie_msg_send        ; rcv_buff_check -> pcie_msg_send(chip, 5)
```
DR init is `pcie_ete_dr_reg_init` @`0x1483c` (source `pcie_ete_dr_init` @`0x14908`; `DR+0x30` base,
`+0x34` depth-1, `+0x38` wptr). **[proven]**

**TX buffer release** - `pcie_ete_tx_buf_rls` @`0x15938` pops completed nodes from
`chan+0x20`/`chan+0xd8` and frees the skb (`hi_net_woc_skb_free`); it maintains
`chan+0x110` (released count). **[proven]**

### A.6 The message dispatch and the handler table

`pcie_msg_handle` @`0x171f8` (host, ctx = `comm+0x2c`; the binding is proven by
`pcie_msg_init` storing the handler table at `comm+0x4c` and `pcie_msg_handle` reading the table at
`ctx+0x20`):

```
  0x017248: ldr  r3, [r4, #0xc]        ; ctx+0xc = ptr to CA 0x40101438
  0x017254: str  r1(=1), [r3]          ; ACK   <= 1
  0x017258: ldr  r3, [r4, #4]          ; ctx+4  = ptr to CA 0x40039014 (out[1])
  0x01725c: ldr  r5, [r3]              ; pending bitmap (device -> host)
  0x017260: str  r2(=0), [r3]          ; CLEAR
  0x0172a0: ldr  r3, [r4, #0x10]       ; ctx+0x10 = ptr to CA 0x40101414
  0x0172ac: str  r2(=1), [r3]          ; RE-ARM <= 1
  0x0172b4: rbit r6, r5; clz r6, r6    ; lowest set bit = message id
  0x0172bc: cmp  r6, #0xa; bgt log
  0x0172d8: ldr  r3, [r4, #0x20]       ; ctx+0x20 = comm+0x4c handler table
  0x0172dc: ldr  sl, [r3, r6, lsl #3]  ; fn
  0x0172ec: ldr  r0, [r3, #4]          ; arg  (r3 advanced by r6*8)
  0x0172f0: blx  sl
  0x0172f8: bics r5, r5, r3, lsl r6; bne loop
```
**[proven]**

`shuangta_pcie_msg_reg_map` @`0x1b1a0` maps the six mailbox CAs through
`oal_pcie_inbound_ca_to_va`, in order (each result stored at `r4+0`, `+4`, `+8`, `+0xc`, `+0x10`,
`+0x14`):

```
  CA 0x40039010 -> reg[0]      CA 0x40101438 -> reg[3]
  CA 0x40039014 -> reg[1]      CA 0x40101414 -> reg[4]
  CA 0x400392d4 -> reg[2]      CA 0x400392f0 -> reg[5]
```
`pcie_msg_init` @`0xb6e4` stores this array at `comm+0x2c`, kmallocs the 0x58-byte handler table at
`comm+0x4c`, binds each chip `chip[i]+0x60 = pcie_msg_send_irq`, `+0x64 = comm`,
`+0x68 = pcie_msg_handle`, `+0x6c = comm+0x2c`, and registers:

| id | handler | source |
| -- | ------- | ------ |
| 1 | `pcie_dev_ready_msg_handle` | `0xb820` |
| 6 | `pcie_trigger_ete_sending_handle` (@`0x15efc` = `b pcie_wkup_thread`) | `0xb84c` |
| 7 | `pcie_trigger_ete_sending_handle` | `0xb878` |
| 3 | `pcie_ete_transfer_done_handle` (`0x8730`) | `0xb8a8` |

**[proven]**

`pcie_msg_send` @`0x160f4` (host->device, async) writes the pending bitmap to `ctx+0` = CA
`0x40039010` (**out[0]**) and sets bit 0 of `ctx+8` = CA `0x400392d4` (**out[2]**, the doorbell):

```
  0x016184: orr  r3, r3, r1, lsl r4    ; pending |= 1<<id
  0x016188: str  r3, [r6, #0x48]       ; acc
  0x016194: ldr  r2, [r6, #0x2c]       ; out[0]
  0x01619c: str  r3, [r2]              ; out[0] = pending
  0x0161a4: ldr  r2, [r6, #0x34]       ; out[2]
  0x0161a8: ldr  r3, [r2]; orr r3,#1; str r3,[r2]   ; doorbell |= 1
```
`pcie_msg_send_irq` @`0x174a8` (host->device, synchronous) first writes `8` to `ctx+0x14` = CA
`0x400392f0` (**out[5]**), then waits for **out[0]** to read back 0
(`pcie_msg_wait_for_clr` @`0x173dc`, `0xfffe` iterations):

```
  0x0174f8: ldr  r3, [r4, #0x40]; mov r2,#8; str r2,[r3]   ; out[5] <= 8
  0x017508: bl   pcie_msg_wait_for_clr                     ; wait out[0]==0
  0x017538: ldr  r2, [r4, #0x2c]; str r3,[r2]              ; out[0] = pending
  0x01754c: orr  r3, r3, #1; str r3, [r2]                  ; out[2] doorbell
```
This is why phase 20's `enable=1` (out[5] <= 8) hung the chip: `0x400392f0` is the **device's**
ack/handshake word (the firmware's own dispatcher stores it at its `ctx+0xc`), and the host's
synchronous sender only writes it immediately before polling for the device's clear. **[proven]**

### A.7 Which of these steps our takeover modules have NOT performed

`fwaccept` (phase 21) already: claims decodes loads releases the chip, programs the six inbound +
one outbound iATU viewport, builds a **host-side copy** of the message context and the handler table
(`omo_msgctx_build`), allocates and programs the ETE SR/DR rings, posts DR buffers, posts the
vendor's 72-byte id-1 SR frame, rings `pcie_msg_send(chip,3)`, requests the IRQ, and runs a
synchronous poll loop that calls the host message service `omo_msg_service` (the `pcie_msg_handle`
equivalent: clear out[1], ack `0x40101438`, re-arm `0x40101414`, dispatch) and scans the rings.

Never performed by any takeover module:

| # | vendor step | where | status |
| - | ----------- | ----- | ------ |
| 1 | create `pcie_process_thread` and its `comm+0x18`/`comm+0x28` condition + wake | `pcie_thread_init` @`0x170b8`, `pcie_wkup_thread` @`0x1629c` | **never** (no kthread exists) |
| 2 | `pcie_thread_handle`: TX-buffer release | `0x16b44` -> `pcie_ete_tx_buf_rls` | **never** |
| 3 | `pcie_thread_handle`: SR-ring pump (`pcie_ete_sending_trigger`) after the thread wake | `0x16b8c` | **never** (only the one-shot post) |
| 4 | `pcie_thread_handle`: DR-ring consume (`pcie_ete_tx_queue_handle`) via the ETE callbacks | `0x16cd0` | **never** (a read-only scan is substituted) |
| 5 | ETE glue status read `+0x2ec & 0x3d8` and W1C clear | `pcie_intr_handle` @`0x82e4`, `oal_pcie_transfer_done` @`0x83e4` | **never**; the fwaccept ISR is deliberately defensive and writes nothing |
| 6 | ETE glue control RMW `AND 0xffe0f8f8` | `pcie_ete_intr_init` @`0x7528` | **never** |
| 7 | DR-buffer reclaim doorbell `pcie_msg_send(chip,5)` | `pcie_ete_rcv_buff_check` @`0x15140` | **never** |
| 8 | periodic SR/DR producer-index re-commit + doorbell re-ring from the service thread | `pcie_ete_sending_trigger` / `pcie_msg_send` | **never** (one ring only) |
| 9 | the host message dispatch `pcie_msg_handle` | `0x171f8` | **done** in fwaccept's poll/ISR (`omo_msg_service`), but not from a service thread and not interleaved with the ring pump |

### A.8 The minimal loop that should make the chip's service take our frame

The vendor's own control flow says the *device-visible* acts a host must supply, in order, are:

1. keep the **message context** armed and dispatch host-side pending (`pcie_msg_handle`: clear
   `out[1]`, ack `0x40101438`, re-arm `0x40101414`) - our `omo_msg_service`, now from the thread;
2. clear the **ETE glue status** (`+0x2ec & 0x3d8`, W1C) each iteration so a completed DMA does not
   mask the next (`pcie_intr_handle` / `oal_pcie_transfer_done`);
3. consume the **DR** ring and re-post buffers, and re-commit the **SR/DR** producer indices, with
   the `pcie_msg_send(chip,3)` / `pcie_msg_send(chip,5)` doorbells
   (`pcie_ete_sending_trigger` / `pcie_ete_tx_queue_handle` / `pcie_ete_rcv_buff_check`);
4. loop until stopped, on the `comm+0x28` condition model (`pcie_process_thread`).

`lab/svc/svc.c` (Part B) implements exactly these four as a kthread. It does **not** touch
`out[5]`/`0x400392f0` (the register that hung the chip in phase 20), and it is the only new device
write surface beyond `fwaccept`'s proven set: an ETE `+0x2ec` write-1-to-clear, and only when the
word is non-zero.

---

## Part B - `lab/svc/svc.c` and the test boots

`lab/svc/svc.c` is `fwaccept.c` (phase 21, kept verbatim except the rename `omo-fwaccept` -> `omo-svc`)
plus the recovered service thread. New module parameters:

| param | default | meaning |
| ----- | ------- | ------- |
| `svc` | 1 | 1 = run the service kthread; 0 = the old synchronous poll loop (control) |
| `svcdur` | 20000 | ms the kthread runs (clamped to >= 20000) |
| `svcms` | 100 | ms between iterations |
| `svcdoorbell` | 10 | iterations between `omo_post_dr()` + the id-3/id-5 doorbells |

`omo_svc_thread` (kernel thread `omo-svc`) runs the four steps of Part A.8 each iteration:
`omo_msg_service` (the `pcie_msg_handle` @`0x171f8` equivalent: clear `out[1]`, ack `0x40101438`,
re-arm `0x40101414`, dispatch), `omo_glue_service` (read ETE `+0x2ec`, `dsb`-equivalent barrier,
mask `0x3d8`, W1C only when non-zero - `pcie_intr_handle` @`0x82e4` / `oal_pcie_transfer_done`
@`0x83e4`), `omo_scan_dr`/`omo_scan_sr` (completion + index scans), and the periodic
`omo_post_dr()` + `pcie_msg_send(chip,3)` / `pcie_msg_send(chip,5)` re-ring. `omo_svc_log_state`
logs every `out[0]`/`out[1]` and SR/DR index change. The one-shot loader `lab/svc/omo-svc` runs
with **`enable=0`** on purpose - phase 20 boot 3 proved `enable=1` (`out[5]` CA `0x400392f0 <= 8`)
hangs the chip, and this phase does not touch that register. The only unproven-but-quoted write
remains `PCI_INTERRUPT_LINE = 0xcf`; the new device write surface is the ETE `+0x2ec` W1C, which
happened zero times because the word read 0.

Build: CI run **36910583771** (branch `omo/phase21-service-thread`), artifact `svc-ko` =
`svc.ko`, 53504 bytes, md5 **6241df9a61f3f47672789a30c08cd0b3**,
`vermagic=5.10.201 SMP mod_unload ARMv7` (matches the vendor kernel).

### B.1 Staging (`build/register-dumps/svc/010_staging.txt`)

```
6241df9a61f3f47672789a30c08cd0b3  /lib/modules/5.10.201/svc.ko
lrwxrwxrwx  /etc/rc.d/S99omo-svc -> ../init.d/omo-svc
-rw-r--r--  /lib/modules/5.10.201/hi5622v100_plat.ko.omo-off   (364660 B, vendor)
-rw-r--r--  /lib/modules/5.10.201/hi5622v100_wifi.ko.omo-off   (3564728 B, vendor)
loader syntax: OK ; recovery syntax: OK
```

### B.2 The test boot (`build/register-dumps/svc/031_testboot_live.txt`, `032_testboot_full.txt`)

The box did **not** hang: the module loaded, the thread ran its full 25 s and the init returned.
The loader deleted its own symlink first, and the pstore record set is unchanged (blk-0/1/2
mtimes still `10:41`/`14:37`/`14:37`).

Takeover + host message service (the chip's first D2H word is id 6, as in phases 19/20):

```
[   43.446526] omo-svc: RELEASE write CA 0x40000108 <- 0x00005a5a (BAR0+0x3b8108)
[   43.521497] omo-svc: [svc post0 +330ms] pending out[1] CA=0x40039014 = 0x00000040
[   43.540653] omo-svc: [svc post0] ACK   out[3] CA=0x40101438 <= 0x00000001 readback=0x00000000
[   43.549137] omo-svc: [svc post0] CLEAR out[1] CA=0x40039014 0x00000040 -> 0x00000000 readback=0x00000000
[   43.558675] omo-svc: [svc post0] REARM out[4] CA=0x40101414 <= 0x00000001 readback=0x00000000
[   43.567285] omo-svc: [svc post0] dispatch id=6 fn=omo_id6_handler+0x0/0x48 [svc] arg=00000006
[   43.795161] omo-svc: [send post0] pcie_msg_send(chip,3): out[0] CA=0x40039010 0x00000000 -> 0x00000008 readback=0x00000008
[   43.808432] omo-svc: [send post0] out[2] CA=0x400392d4 0x00000000 -> 0x00000001 readback=0x00000000 (doorbell |= 1)
[   43.819753] omo-svc: service thread up (pcie_process_thread @0x16efc emulation) dur=25000ms interval=100ms doorbell=10
```

During the 25 s loop the **chip's message service is demonstrably alive in the D2H direction**: it
re-raised id 6 (`out[1] = 0x40`) three times and then id 2 (`0x04`), and every word was consumed and
acked by the thread:

```
[   44.026345] omo-svc: [svc svc] CLEAR out[1] CA=0x40039014 0x00000040 -> 0x00000000 readback=0x00000000
[   44.259289] omo-svc: [svc svc +1070ms] pending out[1] CA=0x40039014 = 0x00000040
[   44.286954] omo-svc: [svc svc] CLEAR out[1] CA=0x40039014 0x00000040 -> 0x00000000 readback=0x00000000
[   44.459353] omo-svc: [svc svc +1270ms] pending out[1] CA=0x40039014 = 0x00000040
[   44.487298] omo-svc: [svc svc] CLEAR out[1] CA=0x40039014 0x00000040 -> 0x00000000 readback=0x00000000
[   44.669371] omo-svc: [svc +1480ms] MBOX out[1] pending CA=0x40039014 0x00000040 -> 0x00000004
[   44.697249] omo-svc: [svc svc +1500ms] pending out[1] CA=0x40039014 = 0x00000004
[   44.723177] omo-svc: [svc svc] CLEAR out[1] CA=0x40039014 0x00000004 -> 0x00000000 readback=0x00000000
```

**`out[0]` (CA `0x40039010`, the H2D mask) is never cleared by the device.** The only changes to it
are the module's own `pcie_msg_send` writes; there is no `H2D MASK CLEARED BY DEVICE` line:

```
[   50.056651] omo-svc: [send svc] pcie_msg_send(chip,3): out[0] CA=0x40039010 0x00000020 -> 0x00000008 readback=0x00000008
[   50.079992] omo-svc: [send svc] pcie_msg_send(chip,5): out[0] CA=0x40039010 0x00000008 -> 0x00000020 readback=0x00000020
```

`glue_clears = 0`: the ETE glue status candidate `+0x2ec` read 0 every iteration, so no glue write was
made. The SR engine still read our descriptors once (`SR ch0 DEVICE INDEX 0x10 -> 0x400`), and the
DR device indices advanced `0 -> 0x10` on all four channels; there was **no** `0x5a5a` buffer, no
HCC message decode and no `ID-1 READY`.

```
[   68.934797] omo-svc: service thread exit after 170 iters (glue_clears=0)
[   68.941988] omo-svc: done (release=1 rings=1 sr_posted=1 acpoff=0 svc=1 iters=170 glue_clears=0 pollms=100 polldur=20000 irq=207 irq_taken=0 irq_handled=0 msgs=4 services=5 sendflag=1 dr_events=4 sr_events=1)
```

Every device write in the run is quoted from the disassembly or a live vendor read (the same set as
`fwaccept`, minus the gate families): the six inbound + one outbound iATU viewports,
`PCI_COMMAND=7`, `FIRMWARE.bin` (read back), the SR/DR program registers + the per-channel `+0x2e8`
RMW, the DR base/depth/wptr, the SR base/depth/wptr/ctrl, the `0x5a5a` release, the host-side
`out[1]`/`out[3]`/`out[4]` service words, and `out[0]`/`out[2]` for the id-3 and id-5 doorbells.
The ETE `+0x2ec` W1C did not fire (value 0). One unproven-but-quoted write: `PCI_INTERRUPT_LINE
= 0xcf`.

---

## Part C - outcome and the named next blocker

**Did the chip process our frame and answer? No.** Across the 25 s service run `out[0]` stayed at the
module's own last write (`0x08` / `0x20`), the endpoint consumed the doorbell (`out[2]` readback 0)
but the chip's firmware dispatcher never cleared `out[0]`, no `out[1]` reply carried an id-1 or an
HCC payload, no `0x5a5a` buffer appeared in the DR ring, and `glue_clears = 0`. The chip's own
receive routine (firmware file `0x818a8`, `docs/phase20/fw-accept.md` A.2) is still never entered for
the H2D direction.

**What the run did prove.** With the vendor service thread stood up as `omo_svc_thread`:

1. the **D2H** message path is fully live end-to-end: the chip re-raises id 6
   (`pcie_trigger_ete_sending_handle`, the request that wakes the host to pump SR) and id 2
   (`host_ready`), and the host consumes/acks/re-arms every word (`services=5`) - this is more
   traffic than phase 20f saw (`out[1]` stayed 0x40/0x04 for the whole 25 s);
2. the thread model itself is safe: 170 iterations, no panic, no chip hang, no new pstore record,
   `enable=0` avoided the `out[5]` hazard;
3. the H2D gate is **not** reached by running the host thread: the chip's service is alive (point 1)
   yet it will not take `out[0]`.

**Named next blocker.** The chip repeatedly asks the host to **pump the SR ring** (id 6) but our
thread only re-rings the `pcie_msg_send` doorbell; it never performs the vendor's
`pcie_ete_sending_trigger` @`0x13f90` descriptor fill + producer commit from the thread, so the SR
engine reads the one descriptor set we posted and then has nothing new to send while the chip keeps
signalling. The next step is to implement that producer side (fill the next SR node and commit
`SR+0x18` from the thread, per `shuangta_ete_sr_dscr_fill` @`0x17858`), and in parallel to pin the
true ETE glue base: `pcie_intr_handle` reads `[[ctx+4]]+0x2ec`, and at both reachable candidates
(`0x4003a2ec`, the channel `+0x2ec`) the word is 0 in this run **and** in the live vendor BAR0 dump,
so the register cannot be located from either side yet - it needs the `[[ctx+4]]` resolution or a
device-side trace.

---

## Recovery

Recovery used the staged `lab/svc/recover-svc.sh` (renames the vendor modules back, removes the
module, loader, symlink, `/tmp` copies and itself, then reboots). Raw evidence:
`build/register-dumps/svc/060_recovery_run.txt`, `070_recovery_evidence.txt`.

Recovered boot:

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1
md5 plat 23660bc285393e678d5cade1c36c194b / wifi e21629d226ec7de9a860a8955952d311  = baseline
0000:00:00.0 -> rox_pci0
radios: vap0 Cudy-1C73 (2g), vap8 Cudy-1C73-5G (5g), ... ; br-lan 192.168.10.1/24 UP
leftovers (svc.ko, *.omo-off, init.d/omo-svc, rc.d/S99omo-svc, /root/recover-svc.sh, /tmp copies): absent
pstore: no new record (blk-0/1/2 mtimes 10:41/14:37/14:37, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored and both radios
answering.**

### Hazard note

No panic and no chip hang this run (`enable=0`, the `out[5]` register untouched). All measurements
were made through endpoint 0's own BAR0; the RC `misc` window (`0x10161000`) was never touched.

### Writes per takeover boot

Boot: the six inbound iATU viewports + one outbound viewport + `PCI_COMMAND=7` + the 928,920-byte
firmware (read back) + the seven SR/DR program registers + the per-channel `+0x2e8` RMW + the DR
base/depth/wptr + the SR base/depth/wptr/ctrl + the `0x5a5a` release + the host `out[1]`/`out[3]`/
`out[4]` service words + `out[0]`/`out[2]` for `pcie_msg_send(3)` and `pcie_msg_send(5)` (repeated
every 10 iterations = 2 s). ETE `+0x2ec` W1C: 0 times (word read 0). The one unproven-but-quoted
write remains `PCI_INTERRUPT_LINE = 0xcf`.
