# rx-loop: the vendor's PCIe receive loop and the first payload test (phase 20e, 2026-10-01)

Task `st_01a0f882`. Direct sequel to `docs/phase20/msg-host-half.md`, which proved the host half of
the message service (ack / clear / re-arm / id-6) runs but changes nothing: `out[1] = 0x40` then
`0x04`, `irq_taken = 0`, and zero DR node changes. That report named the remaining blockers as
**(1)** the id-6 wake has no consumer and **(2)** the DR producer state is not advanced. This phase
recovers the vendor's own consumer - `pcie_process_thread` / `pcie_ete_transfer_done_handle` /
`pcie_ete_rcv_buff_check` / `pcie_ete_dr_get_uploadbuf` - from `plat.ko`, reimplements it, and runs
it on one takeover boot.

**Headline.** The DR receive ring is recovered end to end. Its node format is *not* the SR format:
the **host** fills every 8-byte DR node with `word0 = payload buffer device address` and leaves
`word1 = 0`, and the **device never stamps the node** - it deposits the payload into the buffer and
advances the channel's read index (`DR+0x3c`). A live vendor boot while the radio is idle
(`build/register-dumps/rxloop/000_live_dr_crosscheck.txt`) shows exactly this: every DR node is
`{word0 = buffer CA, word1 = 0}`, `DR+0x30 = ring base`, `DR+0x34 = 0x1f` (depth-1) and
`DR+0x38 == DR+0x3c` (a running packed index). The one thing `msghalf` did **not** do is commit the
host producer index: it wrote `DR+0x38 = 0`. `rxloop` fills all 32 nodes **and** commits the packed
producer index (`0x400` = index 0, phase 1 after one 32-entry lap) to `DR+0x38`, and adds the real
id-3 consumer (`pcie_ete_transfer_done_handle` -> d2h ISR -> `pcie_rx_handle`).

**Part B/C result.** The producer-index commit (`DR+0x38 = 0x400`) is the concrete difference from
phase 20d, and the endpoint reacted to it - `DR+0x3c` moved `0 -> 0x10` on all four DR channels,
**before the firmware release** - but the firmware still deposited nothing: no node stamp, no payload
byte, no `0x5a5a` header, no id-3 transfer-done word, `irq_taken = 0`. The receive loop is now
proven complete and waiting; the payload gap is upstream in the host SR/HCC command dialogue (Part C).

Everything is **[proven]** (a constant/relocation/control-flow in the instruction stream, or a value
the device printed/measured) or **[inferred]**.

Artifacts (regenerable):

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko   # md5 23660bc285393e678d5cade1c36c194b
$PY lab/ko_disasm.py $KO pcie_process_thread pcie_thread_init pcie_wkup_thread \
    pcie_wait_condtion pcie_thread_handle pcie_rx_handle pcie_ete_rcv_buff_check \
    pcie_ete_dr_get_uploadbuf pcie_ete_transfer_done_handle oal_pcie_transfer_done \
    pcie_ete_d2h_isr_handle pcie_ete_rx_rls_dsc_res pcie_ete_get_rls_num > build/tmp/rxloop/loop.txt
$PY lab/ko_disasm.py $KO pcie_ete_dr_init pcie_ete_dr_reg_init pcie_ete_rings_init \
    pcie_ete_init pcie_ete_init_dst_ring pcie_ete_chn_res pcie_ete_intr_init \
    shuangta_ete_dr_dscr_fill shuangta_ete_dr_node_init_handle shuangta_ete_sr_dscr_fill \
    shuangta_ete_dr_get_dscr_addr pcie_ete_ring_ptr_plus > build/tmp/rxloop/dr.txt
```

Live cross-check regenerator: `devmem` (read-only) at `0x403f2000+off` for the ETE register blocks
and at the `DR+0x30` value (CA == physical) for the node arrays; full transcript in
`build/register-dumps/rxloop/000_live_dr_crosscheck.txt`.

---

## Part A - the vendor's receive loop

### A.0 The pieces, in dependency order

`pcie_ete_init` @ `0x7820` builds the ETE engine; `pcie_ete_rings_init` @ `0x7680` allocates the
channel arrays and maps the ETE register block; `pcie_ete_init_dst_ring` @ `0x727c` walks the four
DR channels (3..6, stride `0x6c`); each channel is initialised by `pcie_ete_dr_init` @ `0x14908`
which allocates the 8-byte node array (`shuangta_ete_dr_node_init_handle` @ `0x17970`) and programs
the channel registers (`pcie_ete_dr_reg_init` @ `0x1483c`). At runtime the device completion arrives
as message id 3 -> `pcie_ete_transfer_done_handle` @ `0x8730` -> `pcie_ete_d2h_isr_handle`
@ `0x15c1c` -> `pcie_rx_handle` @ `0x164d0` -> `pcie_ete_dr_get_uploadbuf` @ `0x1515c` ->
`pcie_ete_rcv_buff_check` @ `0x14d74`. The id-6 wake (`pcie_wkup_thread` @ `0x1629c`) wakes the
separate SR-pump thread `pcie_process_thread` @ `0x16efc`.

### A.1 The channel table and the register blocks

`pcie_ete_get_chn_cfg` @ `0x15f00` returns `.rodata+0x101c + 0xc*chn` (7 entries, stride `0xc`); the
first word is the block offset inside the ETE window, the byte at `+4` is the ring depth (0x20) and
the byte at `+5` is the SR control nibble (`dump_chn_cfg_table.txt`):

```
  chn  block   cfg[0]  cfg[4]  cfg[5]
   0   0x400   0x400   0x20    1     SR
   1   0x450   0x450   0x20    1     SR
   2   0x4a0   0x4a0   0x20    1     SR
   3   0x590   0x590   0x20    1     DR
   4   0x5e0   0x5e0   0x20    0     DR
   5   0x630   0x630   0x20    0     DR
   6   0x680   0x680   0x20    0     DR
```

Each block is `0x50` bytes and carries both register groups: **SR** at `+0x10/+0x14/+0x18` (base,
depth-1, write pointer) and **DR** at `+0x30/+0x34/+0x38` (base, depth-1, write pointer); the second
index register of each pair is at `+0x1c` (SR) and `+0x3c` (DR). `pcie_ete_dr_reg_init` @ `0x1483c`
programs a DR channel (`r4` = channel instance, `r6 = [r4+0x50]` = block VA):

```
  0x014854: ldr  r6, [r1, #0x50]       ; DR register block host VA
  ...
  0x014884: bl   pcie_hostca_to_devva
  0x014888: str  r0, [r6, #0x30]       ; DR+0x30 = ring base device address
  0x014894: ldr  r2, [r4, #0x50]
  0x014898: ldrb r3, [r3, #4]          ; cfg[4] = depth (0x20)
  0x01489c: ldr  r1, [r2, #0x34]
  0x0148a0: sub  r3, r3, #1
  0x0148a4: bfi  r1, r3, #0, #0xa      ; DR+0x34[9:0] = depth-1
  0x0148a8: str  r1, [r2, #0x34]
  0x0148b0: ldr  r2, [r4, #0x1c]       ; the channel's committed producer index
  0x0148b4: str  r2, [r3, #0x38]       ; DR+0x38 = [inst+0x1c]     <-- the commit
```

`pcie_ete_chn_res` @ `0x7490` then read-modify-writes `+0x2e8` with `0xfffffc20` for every channel.
**[proven]** (the vendor's own live boot matches: `+0x30` = a RAM address, `+0x34` = `0x1f`).

### A.2 DR node allocation - 8-byte nodes, coherent

`pcie_ete_init_dst_ring` @ `0x727c` iterates channels 3..6, builds the block VA
(`arg1 + cfg[0]`), and calls `shuangta_ete_dr_node_init_handle(inst, chanctx, depth)`:

```
===== shuangta_ete_dr_node_init_handle @ 0x17970 =====
  0x0179a0: lsl  r1, r2, #3            ; size = depth * 8
  0x0179d8: bl   dma_alloc_attrs       ; coherent node array
  0x0179e4: ldr  r3, [sp, #8]          ; dma_handle
  0x0179ec: str  r3, [r6, #0x54]       ; chanctx+0x54 = node array DMA address
  0x0179f0: str  r4, [r6, #0x10]       ; chanctx+0x10 = node array host VA
```

So a DR channel's node array is `depth` (32) nodes of 8 bytes, coherent DMA, and its host VA is
`chanctx+0x10` / DMA address is `chanctx+0x54`. **[proven]**

### A.3 DR node fill and post - `{word0 = buffer, word1 = 0}`

`shuangta_ete_dr_dscr_fill` @ `0x1765c` is the whole fill primitive - note it writes **only word0**
and never touches word1 (unlike the SR fill, A.4):

```
===== shuangta_ete_dr_dscr_fill @ 0x1765c =====
  0x01765c: ldr  r3, [r0, #0x1c]       ; chanctx+0x1c = packed producer index
  0x017660: ldr  r2, [r0, #0x10]       ; chanctx+0x10 = node array
  0x017664: ubfx r3, r3, #0, #0xa      ; slot = index & 0x3ff
  0x017668: str  r1, [r2, r3, lsl #3]  ; node[slot].word0 = buffer device address
```

The index is advanced by the caller through the ring-pointer helper (A.4). A posted-but-unconsumed
node is therefore `{word0 = buffer device (ACP) address, word1 = 0}`. **[proven]**

### A.4 The node format, the owner bits 13/14 and the 0xd2b magic (SR), and the packed index

The 8-byte node is shared by both directions; the **SR** (host->device) fill
`shuangta_ete_sr_dscr_fill` @ `0x17858` is where the owner/valid bits and the magic live:

```
  0x017868: uxth r3, r3
  0x017884: bfi  r1, r3, #0x10, #0x10     ; word1[31:16] = length
  0x01789c: movw r1, #0xd2b               ; host-fill magic
  0x0178a0: orr  r2, r2, #0x4000          ; owner/valid bit 14
  0x0178ac: orr  r2, r2, #0x2000          ; owner/valid bit 13
  0x0178b8: bfi  r2, r1, #0, #0xd         ; word1[12:0] = 0xd2b
  0x0178cc: str  addr, [r3, idx, lsl #3]  ; word0 = data device address
  0x0178e0: str  r2, [r3, #4]             ; word1 = (len<<16) | 0x6000 | 0xd2b
  0x0178ec: bl   pcie_ete_ring_ptr_plus
  0x0178f8: bl   pcie_msg_send            ; doorbell id 3
```

The control (non-data) branch at `0x17910` clears both owner bits and writes `0xd2b` only. So for
the SR direction the device tests `word1[12:0] == 0xd2b` (`pcie_ete_sending_trigger` @ `0x14058`) to
recognise a host-filled node. **For the DR direction there is no such marker** - the live vendor boot
shows all 32 DR nodes with `word1 = 0` even while the channel is live and the device has consumed
them (A.8). **[proven]**

The ring position is a packed 11-bit value, `index[9:0] | phase[10]`; `pcie_ete_ring_ptr_plus`
@ `0x13ef8` increments the index and, on reaching `depth`, wraps to 0 and toggles phase:

```
  0x013ef8: ldr  r3, [r0]
  0x013efc: add  r2, r3, #1
  0x013f00: bfi  r3, r2, #0, #0xa         ; index = index+1
  0x013f04: ubfx r2, r3, #0, #0xa
  0x013f08: cmp  r2, r1                    ; r1 = depth
  0x013f0c: bfceq r3, #0, #0xa            ; wrap index to 0
  0x013f14: eoreq r2, r2, #1              ; toggle phase
  0x013f18: bfieq r3, r2, #0xa, #1
  0x013f1c: str  r3, [r0]
```

Consumers test the phase bit as `tst ..., #0x400` (`pcie_ete_rcv_buff_check` @ `0x14dd0`,
`pcie_ete_dr_get_uploadbuf` @ `0x151d4`). **[proven]**

### A.5 The receive loop and what it waits on

`pcie_thread_init` @ `0x170b8` initialises the waitqueue at `comm+0x18`, zeroes the flag
`comm+0x28`, and starts `pcie_process_thread` @ `0x16efc` (`kthread` at `comm+0x24`).
`pcie_wait_condtion` @ `0x13f24` is a test-and-clear: it returns `*cond` and sets `*cond = 0` when it
was 1. `pcie_process_thread`:

```
  0x016f1c: add  r6, r7, #0x28            ; &comm+0x28
  0x016f34: bl   kthread_should_stop
  0x016f40: mov  r0, r6 ; bl pcie_wait_condtion
  0x016f4c: bne  #0x16f24                 ; cond was set -> do work
  0x016f24: ldr  r3, [r7] ; ldr r3, [r3] ; ldr r0, [r3]
  0x016f30: bl   pcie_thread_handle       ; the SR pump (tx_queue_handle / sending_trigger)
  0x016f50: ... prepare_to_wait_event(comm+0x18) ; schedule()   ; the waitqueue
```

So the thread is woken either by the `comm+0x28` flag (set by `pcie_wkup_thread` @ `0x1629c`, itself
reached from the id-6 handler `pcie_trigger_ete_sending_handle` @ `0x15efc` = `b pcie_wkup_thread`)
or by `__wake_up(comm+0x18)`; it then runs `pcie_thread_handle` - the **SR/TX** pump. The **DR/RX**
consumer is a separate path: message id 3 -> `pcie_ete_transfer_done_handle` -> d2h ISR. **[proven]**

### A.6 The DR consumer

`pcie_ete_transfer_done_handle` @ `0x8730` (registered as id 3 by `pcie_msg_init` @ `0xb6e4`):

```
  0x008738: mov  r2, #0
  0x00873c: mov  r1, #0x1f
  0x008740: b    pcie_ete_d2h_isr_handle    ; r0 = ete, mask 0x1f
```

`pcie_ete_d2h_isr_handle` @ `0x15c1c` walks the set bits of the mask and calls
`[ete+0x1c](ete, bit)`:
```
  0x015c50: ldr  r7, [r6, #0x1c]       ; fn installed by pcie_ete_intr_init
  0x015c54: rbit r5, r4 ; clz r5, r5   ; lowest set bit = channel
  0x015c6c: blx  r7                    ; fn(ete, chn)
```
`pcie_ete_intr_init` @ `0x7528` installs `[ete+0x1c] = pcie_rx_handle`, `[ete+0x20] =
pcie_tx_done_handle`, `[ete+0x24] = pcie_ete_rx_err_handle`, `[ete+0x28] =
pcie_ete_tx_err_handle`. So mask `0x1f` calls `pcie_rx_handle(ete, 0..4)` - DR channel indices.
**[proven]**

`pcie_rx_handle` @ `0x164d0` picks the channel and runs the upload-buffer drain:

```
  0x01651c: ldr  r2, [r6, #0x10]        ; ete->[0x10] = DR channel array
  0x016528: ldr  r3, [r6, #8]           ; DR channel count (4)
  0x016530: cmp  r3, r1 ; bls error     ; chn must be < 4
  0x016540: mov  r3, #0x6c
  0x016544: mul  r3, r3, r1             ; chn * sizeof(drchan)
  0x01653c: str  (sp+0x14), [sp, #0x10] ; out+8 = &out[0xc] (the result list)
  0x01654c: add  r0, r2, r3             ; drchan
  0x01655c: bl   pcie_ete_dr_get_uploadbuf
  0x01656c: ldr  r3, [r3, #8]           ; out->payload != 0 ?
  0x01659c: ... ldr r3,[cbs+0x40] ; ldr r2,[r3+4] ; ldr r0,[r3+0x2c] ; blx r2
                                        ; BAL cbs->rx(cbs->arg, out)
  0x0165b8: bl   pcie_wkup_thread       ; wake the process thread again
```

`pcie_ete_dr_get_uploadbuf` @ `0x1515c` is the actual DR ring drain. It reads the device's index
through the pointer the vendor keeps at `chanctx+0x34` (`ldrh [r3]` @ `0x151a4`), stores it at
`chanctx+0x20`, compares it with the host's consumed index `chanctx+0x24`, and uses the phase bit to
detect a wrap:

```
  0x0151a0: ldr  r3, [r4, #0x34]        ; pointer to the device index register
  0x0151a4: ldrh r3, [r3]               ; hw index (16 bit)
  0x0151ac: str  r3, [r4, #0x20]        ; chanctx+0x20 = hw index
  0x0151b0: ldr  r2, [r4, #0x24]        ; host consumed index
  0x0151b8: cmp  r2, r3 ; beq nothing-new
  0x0151c0: ldr  r6, [r4, #0x1c]        ; packed reference index
  0x0151c8: eor  r3, r3, r6
  0x0151d0: tst  r3, #0x400             ; phase mismatch?
  0x0151e0: cmp  r3(low10), r2(low10) ; bls  -> log "index back"
```

For each completed slot it walks the buffer list at `chanctx+0x3c`, gets the posted buffer address
through the per-chip op `[res+0x30]` (the ETE op table's `shuangta_ete_dr_get_dscr_addr` @ `0x176c0`
returns `node[idx].word0`), releases the descriptor (`pcie_ete_rx_rls_dsc_res` @ `0x14684`) and
updates the consumed index with phase handling:

```
  0x015324: ldr  r3, [r2, #8] ; add #1 ; str   ; list count++
  0x015330: ldr  r3, [r4, #0x24]               ; consumed index
  0x015338: add  r1, r3, #1
  0x01533c: bfi  r3, r1, #0, #0xa
  0x01534c: bfieq r3, r0, #0, #0xa             ; wrap
  0x01535c: str  r3, [r4, #0x24]               ; commit consumed index
```

`pcie_ete_rcv_buff_check` @ `0x14d74` is the buffer-level test - it scans the completed-buffer list
`[chanctx+0x3c]`, and the completion marker is a **buffer header**, not a node field:

```
  0x014db0: ldr  r5, [r7, #0x118]       ; the rx buffer object
  0x014dd4: movw fp, #0x5a5a
  0x014de8: ldrh r3, [r5, #0xa]         ; buffer+0xa
  0x014dec: cmp  r3, fp                 ; == 0x5a5a ?
  0x014df4: ldrh r3, [r5, #4]
  0x014dfc: beq  process                ; magic && len != 0
  0x014e20: bl   pcie_ete_rx_rls_dsc_res_no_unmap
  0x015138: ldr  r3, [r4, #0x68] ; ldr r0, [r3, #0x80]
  0x01513c: mov  r1, #5
  0x015140: bl   pcie_msg_send          ; id 5 = "RX buffers reclaimed"
```

**How a completion is recognised [proven]:** the device advances the DR read index register
(`DR+0x3c`, read through `[chanctx+0x34]`); the host compares it to its consumed index (phase-aware)
and, for each new slot, takes `node[slot].word0` as the buffer address, releases the slot, and tests
the buffer header (`ldrh [buf+0xa] == 0x5a5a`, `ldrh [buf+4] != 0`). **Payload length/address:** the
address is `node[slot].word0`; the length is the 16-bit field at `buffer+4` / the SR `word1>>16`
convention. **What is re-posted:** the descriptor is released (`pcie_ete_rx_rls_dsc_res`) and the
host signals the device with message id 5. **[structure proven; the exact buffer header is the BAL
`cbs->rx` contract]**

### A.7 What the device needs written to start producing

There is **no separate "start" bit**: the DR engine is armed by programming base/depth at
`DR+0x30/0x34` (`pcie_ete_dr_reg_init`) and by the per-channel resource RMW of `+0x2e8`
(`pcie_ete_chn_res`). The buffers become visible to the device through the **committed producer
index** written to `DR+0x38` (`= [inst+0x1c]`, the host's post index, packed as `index[9:0] |
phase[10]`). `msghalf` filled the nodes but wrote `DR+0x38 = 0` - i.e. zero buffers committed - which
is the one producer-state difference this module changes. **[proven register; the "commit gates
production" reading is the experiment in Part B]**

### A.8 Live cross-check on the healthy vendor boot

Read-only, via `devmem` through endpoint BAR0 `0x403f2000+{block}` (and CA == physical for the node
arrays). Full transcript `build/register-dumps/rxloop/000_live_dr_crosscheck.txt`. Idle, vendor
stack bound (`rox_pci0`), radios up, `irq 207` at 0:

```
SR ch0 block 0x400: +0x10=0x84905000 +0x14=0x1f +0x18=5      +0x30=0x01060750
DR ch3 block 0x590: +0x10=0x01060440 +0x30=0x82F92000 +0x34=0x1f +0x38=1     +0x3c=1
DR ch4 block 0x5e0: +0x10=0x01060330 +0x30=0x834E0000 +0x34=0x1f +0x38=0     +0x3c=0
DR ch5 block 0x630: +0x10=0x01060220 +0x30=0x83706000 +0x34=0x1f +0x38=0     +0x3c=0
DR ch6 block 0x680: +0x10=0x01060110 +0x30=0x830F8000 +0x34=0x1f +0x38=0x0d  +0x3c=0x0d
```

Every DR node of ch3 (base `0x82F92000`) and ch6 (base `0x830F8000`) reads
`{word0 = 0x85xxxxxx/0x82xxxxxx buffer CA, word1 = 0}` for all 32 slots; ch4/ch5 read all-zero (base
programmed, nothing posted). So, while the device has consumed everything the host committed
(`+0x38 == +0x3c`) the nodes still show `word1 = 0`: **the DR node is never stamped**; the payload
goes to the buffer and completion is the index advance. This is the "posted-but-unconsumed"
(`+0x38 > +0x3c`, node `{buf,0}`) vs "completed" (`+0x3c` advanced, node still `{buf,0}`) distinction
asked for. **[measured]**

### A.9 Proven vs inferred

| claim | status |
| ----- | ------ |
| DR blocks are `{0x590,0x5e0,0x630,0x680}`, depth 32, stride 0x6c | **proven** (`get_chn_cfg` / `.rodata+0x101c`; live) |
| DR+0x30 base, +0x34 depth-1, +0x38 producer index | **proven** (`pcie_ete_dr_reg_init`) |
| DR node array = `depth*8` coherent, VA `chanctx+0x10`, DMA `+0x54` | **proven** (`shuangta_ete_dr_node_init_handle`) |
| DR fill writes only `word0 = buffer device address`, word1 untouched | **proven** (`shuangta_ete_dr_dscr_fill`; live `word1=0`) |
| SR owner bits 13/14 and `word1[12:0] = 0xd2b`; index/phase packing | **proven** (`shuangta_ete_sr_dscr_fill`, `pcie_ete_ring_ptr_plus`) |
| the DR node is never stamped by the device | **measured** (live idle, all 32 nodes `word1=0`) |
| completion = device advances the DR index register; phase-aware compare | **proven** (`pcie_ete_dr_get_uploadbuf`, `pcie_rx_handle`) |
| buffer header test `ldrh[buf+0xa]==0x5a5a` and `ldrh[buf+4]!=0` | **proven** (`pcie_ete_rcv_buff_check`) |
| id-3 handler -> d2h ISR -> `pcie_rx_handle` for DR channels | **proven** (`pcie_ete_transfer_done_handle`, `pcie_ete_d2h_isr_handle`, `pcie_ete_intr_init`) |
| `pcie_process_thread` is the SR-pump consumer of the id-6 wake (not the DR reader) | **proven** control flow |
| `[res+0x30]` is the per-chip `dr_get_dscr_addr` returning `node[idx].word0` | **inferred** (op-table relocations `.data+0x294c..0x2984`) |
| DR re-post/reclaim signal is `pcie_msg_send(chip,5)` | **proven** call; "reclaim" semantics inferred |
| the `DR+0x38` commit gates device production | **inferred** - the Part B experiment |

---

## Part B - `lab/rxloop/rxloop.c` and the test boots

`rxloop` is `msghalf` plus the receive loop. It keeps every proven step of `msghalf` (claim via
`pci_enable_device` + `pci_request_mem_regions`; the six inbound iATU viewports; the outbound
(device->host) viewport; `PCI_COMMAND=7`; `FIRMWARE.bin` -> BAR0 `0x6f8000` with a `diffs=0`
read-back; the ETE SR/DR rings; the six mailbox CAs and the corrected ack/re-arm binding; the real
id-6 handler; the `0x5a5a` release; the config line write) and changes exactly:

1. **The DR post commits the producer index.** `omo_post_dr` fills all 32 DR nodes with
   `word0 = payload devva` / `word1 = 0` exactly as `shuangta_ete_dr_dscr_fill` @0x1765c, computes
   the packed index with `omo_ring_ptr_plus` (mirroring `pcie_ete_ring_ptr_plus` @0x13ef8: 32 fills
   from 0 -> `0x400` = index 0, phase 1) and writes it to `DR+0x38` exactly as
   `pcie_ete_dr_reg_init` @0x1483c does (`str [regblock+0x38] = [inst+0x1c]`). This is the one
   producer-state difference from `msghalf`, which left `DR+0x38 = 0`.
2. **The real id-3 consumer.** The id-3 entry (`pcie_ete_transfer_done_handle` @0x8730) is no longer
   a stub: `omo_rx_handler` logs the `d2h ISR -> pcie_rx_handle` route and runs the DR scan.
3. **The DR consumer/log.** `omo_scan_dr` reads each channel's `+0x38/+0x3c`, logs index changes,
   every node word0/word1 change (with owner/len/flag decodes), every payload word change, and the
   exact `pcie_ete_rcv_buff_check` test (`ldrh [buf+0xa]==0x5a5a`, `ldrh [buf+4]!=0`).

Every device write is quoted; the only unproven-but-quoted write remains `PCI_INTERRUPT_LINE=0xcf`
(207), the measured live vendor value. CI runs `36900732659` (module md5
`51e5779fdec948b6dc09170ca26cab48`) and `36901655380` (module md5
`2db44f285e442719de5baf90aa03519e`), both `vermagic=5.10.201 SMP mod_unload ARMv7`; the second adds a
pre-release device-index read in `omo_post_dr` and an `omo_scan_dr("postdr")` baseline. Two takeover
boots, each followed by a recovery boot (`010/011_staging*.txt`, `020/021_testboot*_cmd.txt`).

### B.1 The module log (clean boot 2, `033_testboot2_evidence.txt`)

The DR post and its pre-release baseline:

```
[   39.703018] omo-rxloop: DR ch3 baseline after commit: wptr(+0x38)=0x00000400 rptr(+0x3c)=0x00000010 (device index, before release)
[   39.736831] omo-rxloop: DR ch4 baseline after commit: wptr(+0x38)=0x00000400 rptr(+0x3c)=0x00000010 (device index, before release)
[   39.770641] omo-rxloop: DR ch5 baseline after commit: wptr(+0x38)=0x00000400 rptr(+0x3c)=0x00000010 (device index, before release)
[   39.804459] omo-rxloop: DR ch6 baseline after commit: wptr(+0x38)=0x00000400 rptr(+0x3c)=0x00000010 (device index, before release)
```

No node or payload byte changed over the whole run: every `node[n] CHANGED` line is the module's own
post being observed (`ctl=0x00000000 ... owner13=0 owner14=0`), and there is not one `payload+...` or
`BUFFER 0x5a5a MAGIC` line. The mailbox service and the end:

```
[   68.430961] omo-rxloop: done (release=1 rings=1 acpoff=0 pollms=100 polldur=20000 irq=207 irq_taken=0 irq_handled=0 msgs=3 services=5 sendflag=1 dr_events=4)
```

`services=5` = the id-6 wake twice plus the id-2 (no-handler) word etc.; `irq_taken=0` - the endpoint
never asserted INTx, so the whole dialogue was serviced by the poll loop, exactly as in phase 20d.
`dr_events=4` = the four channel index reads; there were zero payload/magic events. (The first boot
reproduced this: `done (... irq_taken=0 irq_handled=0 msgs=1 services=3 sendflag=1 dr_events=4)`; its
firmware scan overflowed the ring buffer and evicted its early lines, which is why this clean second
boot exists.)

### B.2 The live state after the test (`041_testboot2_state.txt`)

```
DR ch3 block 0x590: +0x30=0x83CC7000 +0x34=0x1f +0x38=0x00000400 +0x3c=0x00000010
DR ch4 block 0x5e0: +0x30=0x83E2D000 +0x34=0x1f +0x38=0x00000400 +0x3c=0x00000010
DR ch5 block 0x630: +0x30=0x83F08000 +0x34=0x1f +0x38=0x00000400 +0x3c=0x00000010
DR ch6 block 0x680: +0x30=0x83F5D000 +0x34=0x1f +0x38=0x00000400 +0x3c=0x00000010
DR ch3 node[0..31] = {0x83CC7000, word1=0}   (all four channels, every slot unchanged)
mailbox regs after the service: all 0
```

So the endpoint's DR engine **did** react to the commit - `+0x3c` moved off 0 - but the reaction is
`0x10` (16), it happens **before the firmware release**, and it produces no data. **[measured]**

---

## Part C - outcome

**Did the firmware's payload arrive?** No. Over the whole 20 s (both boots) - polling the DR index
registers, all 32 nodes of all four channels, and 2 KiB of payload per channel every 100 ms, with the
id-6 wake serviced and the id-3 consumer wired - not one payload byte, not one node stamp, not one
`0x5a5a` header and not one id-3 transfer-done word appeared. The firmware's visible output is again
only `out[1] = 0x40` (id 6) and `out[1] = 0x04` (id 2), then silence.

**What the node state shows.** A posted DR node is exactly `{word0 = payload buffer device address,
word1 = 0}` - the device never stamps it (confirmed both on the takeover boot and live on the idle
vendor boot). All 32 nodes stayed that way and the payload buffers stayed zero. The only device-side
movement was the channel read index `DR+0x3c`: `0 -> 0x10` on all four channels, and the pre-release
baseline shows it was already `0x10` **before the chip was released** - i.e. it is the DR engine's
reaction to the producer-index commit (`DR+0x38 = 0x400`), not firmware receive activity. This is the
concrete difference from `msghalf` (which wrote `DR+0x38 = 0` and saw no index movement at all), and
it settles the producer-state hypothesis: the commit is necessary but not sufficient.

**Named next blocker.** The firmware still never reaches the host-facing D2H step. The missing piece
is upstream of the receive loop, in the message dialogue: the host never answers the device's
handshake, so the firmware never sends a payload. The firmware writes id 6
(`pcie_trigger_ete_sending_handle`) and id 2 (device-ready) but never id 3
(`pcie_ete_transfer_done_handle`) - the only id that drives `pcie_rx_handle`. With the DR engine armed
and 32 buffers committed, no completion is ever raised. What is still unbuilt is the **host SR/HCC
command path** (`pcie_msg_send` into the 414-entry command table) and the **id-1 "Device plat
ready!"** completion the vendor's HCC init sends; without the host sending a command into the SR ring
there is nothing for the firmware to answer and so nothing for the DR ring to deliver. (The INTx line
is also still never asserted - `irq_taken=0` in every lane and both boots - but the DR consumer here
is polled, so delivery is not blocked by that.)

---

## Test record / Recovery

Raw evidence: `build/register-dumps/rxloop/` + `stage/`.

| file | contents |
| ---- | -------- |
| `000_baseline.txt` | live vendor boot at start: modules/md5, drivers, irq 207/209, config, radios, pstore |
| `000_live_dr_crosscheck.txt` | read-only live vendor ETE/DR registers + all four DR node arrays (idle, radios up) |
| `010_staging.txt`, `011_staging2.txt` | vendor modules hidden, `rxloop.ko` + loader + recovery installed, md5, syntax |
| `020_testboot_cmd.txt`, `021_testboot2_cmd.txt` | the takeover reboots |
| `030_testboot_dmesg_full.txt`, `031_testboot_evidence.txt` | boot 1 (md5 `51e5779f...`; the firmware scan overflowed the ring - early lines evicted) |
| `032_testboot2_dmesg_full.txt`, `033_testboot2_evidence.txt` | boot 2 clean (md5 `2db44f28...`), full module log |
| `040_testboot_state.txt`, `041_testboot2_state.txt` | takeover state after each test: DR regs (wptr/rptr), node arrays, mailbox, config, pstore |
| `060_recovery_run.txt`, `061_recovery2_run.txt` | the recovery scripts run from the device |
| `070_recovery_evidence.txt`, `071_recovery2_evidence.txt` | recovered boots: modules/radios/IRQs/power params/web, leftovers, pstore |
| `stage/` | `rxloop.ko` (md5 `2db44f285e442719de5baf90aa03519e`), `omo-rxloop`, `recover-rxloop.sh` |
| `../tmp/rxloop/*.txt` | the Part-A disassembly dumps |

### Baseline (live vendor, `000_baseline.txt`)

```
hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3 ; md5 wifi e21629d2... plat 23660bc2...
0000:00:00.0 irq=207 -> rox_pci0 ; 0001:00:00.0 irq=209 -> rox_pci0 ; /proc/interrupts hisi_pci_intx
config ep0: COMMAND=0x0006, LINE=0xcf, PIN=0x01
chip id 0x34 ; 6 wlan ifaces ; br-lan 192.168.10.1/24 ; pstore mtimes 10:41/14:37
DR ch3 base 0x82F92000 w=r=1 ; ch4 0 ; ch5 0 ; ch6 0x830F8000 w=r=0xd ; all nodes {buf,0}
```

### Takeover boot 2 (`032_...`, `033_...`, `041_...`)

Six inbound + one outbound viewport `match=YES`; all seven SR/DR program registers and the `+0x2e8`
RMW `match=YES`; four DR channels posted (32 nodes each) and `DR+0x38 <= 0x400` read back; firmware
`diffs=0`; release `0x5a5a`; config line `0xff -> 0xcf`; `request_irq(207)` `rc=0`. `done (...
irq_taken=0 irq_handled=0 msgs=3 services=5 sendflag=1 dr_events=4)`; DR `+0x38=0x400`,
`+0x3c=0x10` on all four channels; every node `{posted buffer, 0}`; payloads zero; pstore unchanged
(blk-0/1/2 mtimes `10:41`/`14:37`/`14:37`, all pre-test).

### Recovery (both boots)

`sh /root/recover-rxloop.sh` renamed the modules back and removed the module, loader, symlink, `/tmp`
copy and itself, `sync`, `reboot`. Recovered boot (`071_recovery2_evidence.txt`):

```
hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3 (md5 e21629d2.../23660bc2... = baseline)
both endpoints bound to rox_pci0 ; irq 207/209 hisi_pci_intx ; config LINE=0xcf, COMMAND=0x0006
chip id:0x34 ; 6 wlan ifaces ; br-lan 192.168.10.1/24 ; web UI HTTP OK
2g/5g power params match baseline ; leftovers (module, .omo-off, loader, symlink, /tmp, /root script): absent
pstore: no new record (blk-0/1/2 mtimes 10:41/14:37/14:37, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored and both radios
answering.**

### Hazard note

No panic in either takeover boot or either recovery boot; `pstore` gained no record. Every
measurement was made through endpoint 0's own BAR0 (the ETE window at `0x403f2000`) or from the
module; the RC `misc` window (`0x10161000`) was never touched.

### Writes per takeover boot

Six inbound iATU viewports + the one outbound viewport + `PCI_COMMAND=7` + the 928,920-byte firmware
(all read back) + the seven SR/DR program registers + the `+0x2e8` RMW + the four DR
base/depth/**write-pointer** registers (the commit) + the phase-19b `0x5a5a` release + the ack/re-arm
pair per message. All are quoted; the one unproven-but-quoted write remains `PCI_INTERRUPT_LINE =
0xcf`.

### Regenerate

```
PY=../pyenv/Scripts/python.exe
KO=build/register-dumps/teardown/hi5622v100_plat.ko
$PY lab/ko_disasm.py $KO ... > build/tmp/rxloop/{loop,dr}.txt
gh run download 36901655380 -n rxloop-ko -D build/tmp/rxloop-ko2
# stage: scp build/register-dumps/rxloop/stage/{rxloop.ko,omo-rxloop,recover-rxloop.sh} to /tmp,
#        then sh /tmp/stage-rxloop.sh; recovery: sh /root/recover-rxloop.sh
```
