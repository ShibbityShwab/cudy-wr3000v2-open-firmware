# The D2H message catalog: every id the vendor host handles, and what it does (phase 37, 2026-10-04)

Static, read-only deliverable. Every offset below was re-disassembled in this session with the
project's `pyenv` capstone (`CS_ARCH_ARM/CS_MODE_THUMB` for `FIRMWARE.bin`, `CS_ARCH_ARM/CS_MODE_ARM`
for the two `.ko` modules); the verification run is in section 7. Claims sourced from the record
are labelled `[record]`; everything else is `[verified]` and carries the offset it was read from.

## 0. Headline

The device->host direction is **two layers**, and the record has been conflating them:

1. **The mailbox notify layer** - the firmware signals "bit *id* is pending" in `out[1]`
   (CA `0x40039014`). The vendor host registers handlers for **four** ids in this space:
   **1, 3, 6, 7** (`pcie_msg_init` @ `0xb6e4`, four `pcie_msg_register` calls). This is the space
   the brief calls "the D2H ids" and it is the id the host's `pcie_msg_handle` @ `0x171f8`
   dispatches on. Ids **0, 2, 4, 5, 8, 9, 10** are *unregistered* in this layer.
2. **The HCC message layer** - the payload of a D2H message is an HCC frame with a header
   `(group = byte[0] & 0xf, id = u16 @ +6)`. The host dispatches it in `hcc_msg_process` @ `0x1204c`
   through per-group tables registered by `hcc_msg_register_tab_chip/_core`. The two
   "device ready" handshake messages live here, in **group 4**.

One correction to the record, quoted byte-for-byte below: the group-4 table's id **1** is
`device_plat_ready_msg_process` and id **2** is `host_ready_msg_process`. The brief's
"id 2 = device_plat_ready_msg_process" is the phase-24 table read one slot too far (it paired
each handler with the *next* entry's id). The mailbox space also has an id 2, but **nothing is
registered there**, so a firmware `out[1] = 0x04` takes the host's "no handler registered" path.

No D2H handler in either layer writes the device: `pcie_msg_send` has exactly four call sites in
`plat.ko` and none of them is a D2H handler; `wifi.ko` never calls it. The D2H direction is
**notify-and-carry**, never request-and-reply.

## 1. Sources, mapping, safety

| item | value |
| --- | --- |
| device image | `build/tmp/FIRMWARE.bin`, 928,920 B, md5 `0e530b976d5a20e87358671f1a577695`; ARM Thumb; runtime = file + `0x40000` |
| host module A | `opensource/build/register-dumps/teardown/hi5622v100_plat.ko`, md5 `23660bc285393e678d5cade1c36c194b` |
| host module B | `opensource/build/tmp/hi5622v100_wifi.ko`, md5 `4737fcb21a1a2262a96f84d780ad8b35` |
| disassembler | `pyenv/Scripts/python.exe` (capstone 5.0.7) |
| record read | `phase19/fw-handshake.md`, `phase20/{message-service,msg-host-half,rx-loop}.md`, `phase22/fw-hostmem.md`, `phase24/{dialogue-endpoints,handler-table,vendor-sr-announce-id}.md`, `phase25/{chip-message-tables-named,full-event-vocabulary,live-vendor-ring-ground-truth}.md`, `phase30/the-gate-located.md`, `phase36/h2d-mailbox-boot-only.md` |

`.ko` offsets are section offsets; the quoted symbol's section is named where it is not `.text`
(only `exception_info_msg_process` is in `.text.unlikely`). `FIRMWARE.bin` offsets are file offsets
with the runtime address (`file + 0x40000`) shown where useful.

Safety: no device access, no writes; the only file written is this document.

## 2. The D2H channel, as the vendor host implements it

`pcie_msg_init` @ `0xb6e4` builds the host context and zeroes the two mailbox words:

```
0x00b738: str     r8, [r3]        ; *out[0] = 0   (r3 = [comm+0x2c])
0x00b740: str     r8, [r3]        ; *out[1] = 0   (r3 = [comm+0x30])
0x00b764: str     r3, [r4, #0x4c] ; comm+0x4c = the D2H handler table (kmalloc'd, 0x58 B)
0x00b774: bl      memset_s        ; table zeroed: 11 entries * 8 B
```

The D2H dispatcher is `pcie_msg_handle` @ `0x171f8` (installed into the chip's op slot 0x68 by
`pcie_msg_init` @ `0xb7e8`, `str sl,[r3,#0x68]`):

```
0x01724c: mov     r1, #1
0x017254: str     r1, [r3]        ; *ack   = 1        (ctx+0xc -> CA 0x40101438)
0x017258: ldr     r3, [r4, #4]
0x01725c: ldr     r5, [r3]        ; read  pending mask (ctx+4 -> out[1] CA 0x40039014)
0x017260: str     r2, [r3]        ; clear pending  (r2 = 0)
0x0172a0: ldr     r3, [r4, #0x10]
0x0172ac: str     r2, [r3]        ; *re-arm = 1     (ctx+0x10 -> CA 0x40101414)
0x0172b4: rbit    r6, r5
0x0172b8: clz     r6, r6          ; r6 = lowest set bit = the D2H id
0x0172d8: ldr     r3, [r4, #0x20] ; the handler table (== comm+0x4c)
0x0172dc: ldr     sl, [r3, r6, lsl #3]   ; handler[ id ]
0x0172e0: add     r3, r3, r6, lsl #3
0x0172e4: cmp     sl, #0
0x0172e8: beq     #0x1739c        ; handler[ id ] == 0 -> "no handler registered" log
0x0172ec: ldr     r0, [r3, #4]    ; arg = entry+4
0x0172f0: blx     sl              ; handler[ id ](arg)
```

So a D2H id is exactly the **bit index** in `out[1]`, the table is indexed by that bit, and
entry layout is `{ handler @ +0, arg @ +4 }`. The registration primitive `pcie_msg_register`
@ `0x15fbc` rejects `id > 0xa` and a NULL handler and stores the pair at `table[id]`:

```
0x015fcc: cmp     r1, #0xa        ; id must be <= 10
0x016004: strne   r2, [r5, r4, lsl #3]   ; entry+0 = r2  (the handler)
0x016008: addne   r4, r5, r4, lsl #3
0x01600c: strne   r3, [r4, #4]           ; entry+4 = r3  (the arg)
```

The firmware's mirror of this channel (section 2.1 of `phase22/fw-hostmem.md` `[record]`, re-read
below `[verified]`) points the same three roles at device registers: `g = *0x172130` holds
`g+0x9c = out[1] (0x40039014)`, `g+0xa4 = the D2H doorbell (0x40101434)`, `g+0xb4/+0xb8 = the
armed-flag and pending shadow`, and the firmware's own H2D dispatcher reads `ctx+0xc` / `ctx+4` /
`ctx+0x10` / `ctx+0x20` at file `0x818ac`.

## 3. The firmware's D2H emitter: `d2h_notify` @ file `0x86170` (rt `0xc6170`)

This is the only function in the blob that writes the `out[1]` pending word and the D2H doorbell
(`[verified]`: a scan of all `ldr/str .w` with imm12 in {`0x9c`,`0xa4`,`0xd0`,`0xd8`} over
`0x86000..0x87000` returns only this function's two stores).

```
0x086170: cmp     r1, #0xa        ; id > 0xa -> reject
0x086172: push    {r3, r4, r5, r6, r7, lr}
0x086174: mov     r7, r1          ; r7 = id
0x086176: bhi     #0x861d6
0x086178: cbnz    r0, #0x861d6    ; must be called with r0 == 0
0x08617a: ldr     r3, [pc, #0x60] ; lit @0x861dc = 0x00172130 (the msg global)
0x08617c: ldr     r5, [r3]        ; r5 = g
0x08617e: cbz     r5, #0x861d6
0x086180: add.w   r6, r5, #0xc0   ; lock
...
0x086198: movs    r3, #1
0x08619a: ldr.w   r2, [r5, #0xb4] ; armed flag
0x08619e: lsls    r3, r7          ; r3 = 1 << id
0x0861a0: orrs    r3, r1          ; |= pending shadow (g+0xb8)
0x0861a2: str.w   r3, [r5, #0xb8] ; shadow
0x0861a8: ldr.w   r1, [r5, #0x9c] ; out[1]  CA 0x40039014
0x0861ac: str.w   r3, [r5, #0xb4]
0x0861b0: str     r3, [r1]        ; out[1] = pending mask
0x0861b2: ldr.w   r4, [r5, #0xa4] ; doorbell CA 0x40101434
0x0861b6: str.w   r2, [r5, #0xb8]
0x0861ba: ldr     r3, [r4]
0x0861bc: orr     r3, r3, #1
0x0861c0: str     r3, [r4]        ; doorbell |= 1
```

The register binding it uses is written by the firmware's `pcie_msg_init` at file `0x9760..0x978e`
(`[verified]`; `lit=0x40101434` / `lit=0x40039014`, and `strd r2,r3,[r5,#0xd0]`):

```
0x009700: str     r2, [r3]        ; *0x172130 = the msg object  (g)
0x009760: ldr     r1, [pc, #0x74] ; lit 0x40101434 (D2H doorbell)
0x009762: ldr     r2, [pc, #0x78] ; lit 0x40039014 (out[1])
0x009764: str.w   r1, [r5, #0xd8]
0x009784: strd    r2, r3, [r5, #0xd0]   ; g(+0x34)=obj: [0]=out[1], [4]=out[0]
0x00978c: str     r7, [r2]        ; *out[1] = 0
0x00978e: str     r7, [r3]        ; *out[0] = 0
```

**Honest limit.** No `BL`/`BLX`, no literal and no `movw/movt` in `FIRMWARE.bin` names `0xc6170`
(the whole-file call/literal scan returns empty), so within this blob `d2h_notify` has no static
caller: the notify is issued by code reached indirectly (the other image / the ctx), and the
record's live captures are the direct evidence that the bits do appear. The *ids* the firmware
stamps are therefore read from the live record (bits 6 and 2, `phase24/handler-table.md`
`[record]`), not from a static D2H call site - that call site is not in this image.

## 4. Catalog, layer A: the mailbox D2H ids the host registers (1, 3, 6, 7)

Registration, `pcie_msg_init` @ `0xb6e4` (`[verified]`; each `movw/movt` pair carries an
`R_ARM_MOVW_ABS_NC`/`R_ARM_MOVT_ABS` reloc to the handler symbol, the `bl` carries
`R_ARM_CALL` to `pcie_msg_register`):

```
0x00b818: movw    r2, #0  ; -> pcie_dev_ready_msg_handle        [R_ARM_MOVW_ABS_NC]
0x00b824: mov     r1, #1                                        ; id = 1
0x00b82c: bl      #0xb82c ; -> pcie_msg_register                [R_ARM_CALL]
0x00b84c: movw    r2, #0  ; -> pcie_trigger_ete_sending_handle  [R_ARM_MOVW_ABS_NC]
0x00b848: mov     r3, r4                                        ; arg = the msg object
0x00b854: mov     r1, #6                                        ; id = 6
0x00b858: bl      #0xb858 ; -> pcie_msg_register                [R_ARM_CALL]
0x00b878: movw    r2, #0  ; -> pcie_trigger_ete_sending_handle  [R_ARM_MOVW_ABS_NC]
0x00b874: mov     r3, r4
0x00b880: mov     r1, #7                                        ; id = 7
0x00b884: bl      #0xb884 ; -> pcie_msg_register                [R_ARM_CALL]
0x00b8a0: movw    r2, #0  ; -> pcie_ete_transfer_done_handle    [R_ARM_MOVW_ABS_NC]
0x00b8ac: mov     r1, #3                                        ; id = 3
0x00b8b8: bl      #0xb8b8 ; -> pcie_msg_register                [R_ARM_CALL]
```

| D2H id | host handler (offset) | arg | what the handler does (`[verified]`) | firmware side | host reply? |
| --- | --- | --- | --- | --- | --- |
| **1** | `pcie_dev_ready_msg_handle` @ `0x8784` (`.text`) | chip obj | `0x008784: bx lr` - an **empty stub**. This mailbox id is a no-op in plat.ko; the real device-ready work is the HCC handler in section 5. | would be `d2h_notify(0, 1)` @ `0x86170` | **no** |
| **3** | `pcie_ete_transfer_done_handle` @ `0x8730` | chip obj | `0x008738: mov r2,#0` / `0x00873c: mov r1,#0x1f` / `0x008740: b pcie_ete_d2h_isr_handle` - walks DR channels 0..4 and drains completed receive buffers | would be `d2h_notify(0, 3)` | **no** (it calls `oal_pcie_transfer_done` -> BAL rx callback; no mailbox write) |
| **6** | `pcie_trigger_ete_sending_handle` @ `0x15efc` | msg obj | `0x015efc: b pcie_wkup_thread` -> `0x01629c` sets `comm+0x28 = 1` and `__wake_up`s the process thread, whose work item is the SR/TX pump (`pcie_thread_handle`) | `d2h_notify(0, 6)` - the one id the record has observed live (`out[1] = 0x40`) | **no** |
| **7** | `pcie_trigger_ete_sending_handle` @ `0x15efc` | msg obj | same as id 6 | would be `d2h_notify(0, 7)` | **no** |

The `d2h_notify` function itself is `[verified]` (section 3); the mapping "this id is emitted for
this event" is `[inferred]` except id 6, which the record has captured live.

**Reply audit (`[verified]`).** All four D2H handlers' `bl`/`b`/`blx` targets are resolved in
section 7; none reaches `pcie_msg_send` or `pcie_msg_send_irq`. The host's *only* H2D senders are
the four known call sites (`phase36/h2d-mailbox-boot-only.md` `[record]`, re-confirmed here):

```
0x0000af8  check_customize_module_exist          (init)
0x002920   hwifi_get_ini_chain_customize_param   (init)
0x015144   pcie_ete_rcv_buff_check               -> pcie_msg_send(chip, 5)
0x0178f8   shuangta_ete_sr_dscr_fill             -> pcie_msg_send(chip, 3)
```
(the `0x1513c: mov r1,#5` / `0x178f4: mov r1,#3` id loads are quoted in section 7). `wifi.ko`
calls neither sender.

**The firmware's two observed bits.** In every takeover the released firmware writes
`out[1] = 0x40` (bit 6) and later `out[1] = 0x04` (bit 2) (`phase20/runtime-msg.md`,
`phase24/handler-table.md` `[record]`). Bit 6 maps to the id-6 handler above; **bit 2 maps to
index 2, where `pcie_msg_init` registers nothing**, so `pcie_msg_handle` @ `0x172e8` takes the
`0x1739c` branch and logs "no handler registered". The mailbox layer has no id 0, 2, 4, 5, 8, 9,
10 handler. (The id-2 *HCC* handler is a different thing; see section 5.)

## 5. Catalog, layer B: the HCC message-table D2H ids

The payload of a D2H message is an HCC frame; the host dispatches it by `(group, id)` read out of
the frame header. `hcc_msg_process` @ `0x1204c` (`[verified]`):

```
0x01204c: mov     r3, r0                  ; r3 = group resource
0x012058: ldr     r2, [r0, #0x118]        ; r2 = the received message (bufr)
0x012084: ldrb    r3, [r2]                ; bufr[0]
0x01208c: and     r3, r3, #0xf            ; group = bufr[0] & 0xf
0x012090: cmp     r3, ip                  ; ip = number of registered groups
0x01209c: ldrh    r2, [r2, #6]            ; id = u16 @ bufr+6
0x0120b8: adds    r3, r3, r2, lsl #4      ; dest = table + id*16
0x0120c0: ldr     r3, [r3, #4]            ; handler = dest+4
0x0120cc: bx      r3                      ; handler(msg)
```

The registered tables are converted from source `{u16 id, u16 pad, u32 handler, u32 pad}`
(stride 12) into the runtime `{id, handler}` (stride 16) by `hcc_msg_register_tab_customise`
@ `0x118c4` (`[verified]`):

```
0x011924: ldr     lr, [r2, #4]            ; the group's runtime table
0x01195c: ldrh    r2, [ip, #-4]           ; source entry id (tab+0)
0x01194c: ldr     r2, [ip], #0xc          ; source entry handler (tab+4), ip += 12
0x011940: add     r4, lr, r2, lsl #4      ; dest = table + id*16
0x011944: str     r2, [lr, r2, lsl #4]    ; dest+0 = id
0x011950: str     r2, [r4, #4]            ; dest+4 = handler
```

### 5.1 Group 4 - the ready/heartbeat/excep table (`.data+0x2660`, 5 entries)

Registered by `plat_init_bal_hcc_excp` @ `0xe380` (`[verified]`):

```
0x00e3a8: mov     r2, #5                  ; count = 5
0x00e3ac: movw    r1, #0  ; -> .LANCHOR0  ; = .data+0x2660  (R_ARM_MOVW_ABS_NC)
0x00e3b4: mov     r0, #4                  ; group = 4
0x00e3b8: bl      #0xe3b8 ; -> hcc_msg_register_tab_chip
```

Raw bytes (`.data`, `[verified]`): each 12-byte entry is `{u16 id @ +0, u16 pad @ +2,
u32 handler @ +4 (relocation-filled; raw 0 in the object), u32 3 @ +8}`. The handler symbol comes
from the `.rel.data` entry at `+4` (`R_ARM_ABS32`):

| id | handler (`.rel.data` @ `.data+`) | offset | semantics (`[verified]` from the handler body) | host reply? |
| --- | --- | --- | --- | --- |
| 0 | `oam_rx_post_action_function` | `+0x2664` -> @ `0xdabc` | `0x00daf0: bl oam_upload_device_log_to_sdt` - forwards a device log blob (`len-13 <= 0x673`) up to SDT/netlink | no device write |
| 1 | `device_plat_ready_msg_process` | `+0x2670` -> @ `0xeb78` | checks source core 0, prints `"Device plat ready! chip id : %d"`, `0x00ebcc: bl complete` (releases wait #1 of `multi_chip_loading`) | **no** |
| 2 | `host_ready_msg_process` | `+0x267c` -> @ `0xebd8` | `0x00ebf8: ldrh r6,[r5,#4]`, `0x00ec00: sub r8,r6,#0xc` (payload = len-12, bounded `len-13 <= 0x62`), `0x00ec7c: bl memcpy_s` into a per-core `0x64` record, sets the ready bit, `0x00ed24: bl complete` (releases wait #2) | **no** (`dmb`/`dsb`/`sev` only) |
| 3 | `exception_info_msg_process` | `+0x2688` -> @ `0x18c0` (`.text.unlikely`) | copies the 0x30-byte message body into the exception record `108*core + 0x58` | no |
| 4 | `heartbeat_msg_process` | `+0x2694` -> @ `0x10214` | `0x010280: bl hrtimer_start_range_ns` - restarts the heartbeat timer for the core | no |

**Correction of the record (`[verified]`, byte-exact).** The brief's
"id 2 = device_plat_ready_msg_process" is wrong. The raw id word at `.data+0x266c` is `1` and its
handler reloc (`+0x2670`) is `device_plat_ready_msg_process`; the id word at `.data+0x2678` is `2`
and its handler (`+0x267c`) is `host_ready_msg_process`. `phase19/fw-handshake.md` `[record]` was
right; `phase24/handler-table.md` `[record]` is off by one entry (it read the id field from
`entry+8`, which is the *next* entry's id). The firmware's own handshake order is
`#1 "Device plat ready"` then `#2 "DEVICE READY"`, and the host's two completions are completed by
ids 1 and 2 respectively.

### 5.2 The other registered D2H tables (groups 0-3)

All these tables live in `wifi.ko`. Registration sites re-verified here (`[verified]`):
`hdpp_main_init` @ `0x12e0` group 3 (5 entries, call @ `0x135c`) and group 0/core (7 entries, call @
`0x136c`); `hmac_main_init` @ `0x3f6c8` group 1 (0x1c = 28 entries, call @ `0x3f778`), group 2
(0xc = 12, call @ `0x3f788`), group 3 (8, call @ `0x3f798`); `wal_main_init` @ `0x123cac` group 2
(0x13 = 19, call @ `0x123ce0`). These are the only `hcc_msg_register_tab*` calls in `wifi.ko`.
`plat.ko` has exactly **one** such call site, the group-4 one at `0xe3b8`; a second same-shaped
source table exists at `.data+0x2750` (16-byte entries, ids 1-4 carrying the same ready/heartbeat
handlers) but no runtime registration of it was found in either module in this pass. The id ->
handler lists for the hmac/wal/hdpp tables are in
`phase25/full-event-vocabulary.md` `[record]`; the two names that matter to this project's goal are
`hmac_d2h_pm_event`, `hmac_d2h_temp_state_update` (the vendor names the direction in the handler),
and `wal_send_mgmt_to_host` / `wal_cfg80211_cac_report`, which are the cfg80211-facing D2H events.

Per-id firmware emitters for this layer are **not** resolved in this pass: the emitters live in the
H2D/HCC command machinery, and the static call graph in this image does not reach the notify site
(section 3). That is the natural next deliverable; the ids and handlers above are what the host
registers.

## 6. Delivery chain and the reply question

A D2H message in the vendor stack:

```
firmware: build HCC frame -> (carrier) -> d2h_notify(0, id) -> out[1] |= 1<<id ; doorbell |= 1
host    : IRQ -> pcie_msg_handle(0x171f8): ack 0x40101438=1, read+clear out[1], re-arm 0x40101414=1,
          handler[id] from comm+0x4c
             id 6/7 -> pcie_trigger_ete_sending_handle -> pcie_wkup_thread  (SR/TX pump)
             id 3   -> pcie_ete_transfer_done_handle -> pcie_ete_d2h_isr_handle -> pcie_rx_handle
                       -> oal_pcie_transfer_done -> BAL rx cb -> hcc queue
                       -> hcc_process_rx_thread -> hcc_queue_msg_process -> hcc_msg_process(0x1204c)
                       -> group/id table -> the id handler (section 5)
             id 1   -> (stub bx lr)
             id 2   -> no handler registered -> logged and dropped
```

**Reply audit.** No handler in either layer issues a host->device message. Concretely: the
mailbox handlers (section 4) contain no `pcie_msg_send`/`pcie_msg_send_irq`; the group-4 handlers
(section 5.1) end in `complete`, `memcpy`/`sev`, a record copy, or `hrtimer_start` - none writes a
device register. So the D2H protocol is one-way: the device notifies, the host consumes. The only
synchronous H2D the vendor's steady state performs from a receive path is
`pcie_msg_send(chip, 5)` in `pcie_ete_rcv_buff_check` (a buffer-reclaim signal) and
`pcie_msg_send(chip, 3)` in `shuangta_ete_sr_dscr_fill` (the SR post announce) - both are
host-initiated, not replies to a D2H id.

## 7. Verification log

Every offset quoted above, re-disassembled with `pyenv/Scripts/python.exe`
(`CS_ARCH_ARM/CS_MODE_THUMB` on `FIRMWARE.bin`, `CS_ARCH_ARM/CS_MODE_ARM` on the modules), the
symbol's own section used for `.ko` offsets:

```
==== PLAT.ko (.text unless noted) ====
0x00b6e4: push {r4, r5, r6, r7, r8, sb, sl, fp, lr}     pcie_msg_init
0x00b738: str r8, [r3]                                   *out[0] = 0
0x00b740: str r8, [r3]                                   *out[1] = 0
0x00b764: str r3, [r4, #0x4c]                            handler table stored
0x00b774: bl  -> memset_s [R_ARM_CALL]                    table zeroed
0x00b79c: movw sl, #0 -> pcie_msg_handle                 [R_ARM_MOVW_ABS_NC]
0x00b7e8: str sl, [r3, #0x68]                            chip op 0x68 = the D2H dispatcher
0x00b818: movw r2, #0 -> pcie_dev_ready_msg_handle       [R_ARM_MOVW_ABS_NC]
0x00b824: mov r1, #1                                     id = 1
0x00b82c: bl  -> pcie_msg_register [R_ARM_CALL]
0x00b84c: movw r2, #0 -> pcie_trigger_ete_sending_handle [R_ARM_MOVW_ABS_NC]
0x00b854: mov r1, #6                                     id = 6
0x00b858: bl  -> pcie_msg_register [R_ARM_CALL]
0x00b878: movw r2, #0 -> pcie_trigger_ete_sending_handle
0x00b880: mov r1, #7                                     id = 7
0x00b884: bl  -> pcie_msg_register [R_ARM_CALL]
0x00b8a0: movw r2, #0 -> pcie_ete_transfer_done_handle
0x00b8ac: mov r1, #3                                     id = 3
0x00b8b8: bl  -> pcie_msg_register [R_ARM_CALL]
0x015fbc: clz ip, r2                                     pcie_msg_register
0x015fcc: cmp r1, #0xa                                   id <= 10
0x016004: strne r2, [r5, r4, lsl #3]                     entry+0 = handler
0x01600c: strne r3, [r4, #4]                             entry+4 = arg
0x0171f8: push {r4, r5, r6, r7, r8, sb, sl, lr}          pcie_msg_handle
0x017254: str r1, [r3]                                   ack = 1
0x01725c: ldr r5, [r3]                                   read pending (out[1])
0x017260: str r2, [r3]                                   clear pending
0x0172ac: str r2, [r3]                                   re-arm = 1
0x0172b4: rbit r6, r5
0x0172b8: clz r6, r6                                     lowest set bit = id
0x0172d8: ldr r3, [r4, #0x20]                            the handler table
0x0172dc: ldr sl, [r3, r6, lsl #3]                       handler[id]
0x0172e4: cmp sl, #0
0x0172e8: beq #0x1739c                                   no-handler path
0x0172ec: ldr r0, [r3, #4]                               arg
0x0172f0: blx sl
0x01739c: bl  -> get_pcie_loglevel [R_ARM_CALL]          "no handler" log path
0x008784: bx lr                                          pcie_dev_ready_msg_handle (stub)
0x008730: cmp r0, #0                                     pcie_ete_transfer_done_handle
0x008738: mov r2, #0
0x00873c: mov r1, #0x1f
0x008740: b   -> pcie_ete_d2h_isr_handle [R_ARM_JUMP24]
0x015efc: b   -> pcie_wkup_thread [R_ARM_JUMP24]         pcie_trigger_ete_sending_handle
0x01629c: push {r4, lr}                                  pcie_wkup_thread
0x0162ac: str r2, [r4, #0x28]                            comm+0x28 = 1
0x00eb78: push {r4, lr}                                  device_plat_ready_msg_process
0x00ebc8: ldr r0, [pc, #4]
0x00ebcc: bl  -> complete [R_ARM_CALL]
0x00ebd8: push {r4, r5, r6, r7, r8, lr}                  host_ready_msg_process
0x00ebf8: ldrh r6, [r5, #4]                              len @ msg+4
0x00ec00: sub r8, r6, #0xc                               payload len = len-12
0x00ec7c: bl  -> memcpy_s [R_ARM_CALL]
0x00ed24: bl  -> complete [R_ARM_CALL]
0x00dabc: push {r4, lr}                                  oam_rx_post_action_function
0x00daf0: bl  -> oam_upload_device_log_to_sdt [R_ARM_CALL]
0x0018c0: movw r2, #0 -> .LC19 [R_ARM_MOVW_ABS_NC]       exception_info_msg_process (.text.unlikely)
0x010214: push {r4, r5, r6, r7, lr}                      heartbeat_msg_process
0x010280: bl  -> hrtimer_start_range_ns [R_ARM_CALL]
0x00e380: push {r4, r5, lr}                              plat_init_bal_hcc_excp
0x00e3a8: mov r2, #5                                     count = 5
0x00e3b4: mov r0, #4                                     group = 4
0x00e3b8: bl  -> hcc_msg_register_tab_chip [R_ARM_CALL]
0x01204c: mov r3, r0                                     hcc_msg_process
0x012084: ldrb r3, [r2]
0x01208c: and r3, r3, #0xf                               group = bufr[0] & 0xf
0x01209c: ldrh r2, [r2, #6]                              id = bufr[6]
0x0120b8: adds r3, r3, r2, lsl #4                        table + id*16
0x0120c0: ldr r3, [r3, #4]                               handler = dest+4
0x0120cc: bx r3
0x0118c4: push {r4, r5, r6, r7, lr}                      hcc_msg_register_tab_customise
0x011924: ldr lr, [r2, #4]
0x011940: add r4, lr, r2, lsl #4
0x011944: str r2, [lr, r2, lsl #4]                       dest+0 = id
0x01194c: ldr r2, [ip], #0xc                             source handler, ip += 12
0x011950: str r2, [r4, #4]                               dest+4 = handler
0x01195c: ldrh r2, [ip, #-4]                             source id (tab+0)
0x01513c: mov r1, #5                                     H2D id 5
0x015144: bl  -> pcie_msg_send [R_ARM_CALL]
0x0178f4: mov r1, #3                                     H2D id 3
0x0178f8: bl  -> pcie_msg_send [R_ARM_CALL]

.data+0x2660 group-4 table (id @ +0, handler reloc @ +4):
  .data+0x2660 id=0  handler .rel.data+0x2664 -> oam_rx_post_action_function
  .data+0x266c id=1  handler .rel.data+0x2670 -> device_plat_ready_msg_process
  .data+0x2678 id=2  handler .rel.data+0x267c -> host_ready_msg_process
  .data+0x2684 id=3  handler .rel.data+0x2688 -> exception_info_msg_process
  .data+0x2690 id=4  handler .rel.data+0x2694 -> heartbeat_msg_process

==== WIFI.ko (registration sites) ====
0x03f768: mov r2, #0x1c                                   hmac group 1: 28 entries
0x03f778: bl  -> hcc_msg_register_tab_chip [R_ARM_CALL]
0x03f780: mov r2, #0xc                                    hmac group 2: 12 entries
0x03f788: bl  -> hcc_msg_register_tab_chip [R_ARM_CALL]
0x03f790: mov r2, #8                                      hmac group 3: 8 entries
0x03f798: bl  -> hcc_msg_register_tab_chip [R_ARM_CALL]
0x123cd0: mov r2, #0x13                                   wal group 2: 19 entries
0x123ce0: bl  -> hcc_msg_register_tab_chip [R_ARM_CALL]
0x001354: mov r2, #5                                      hdpp group 3: 5 entries
0x00135c: bl  -> hcc_msg_register_tab_chip [R_ARM_CALL]
0x001368: mov r2, #7                                      hdpp group 0 (core): 7 entries
0x00136c: bl  -> hcc_msg_register_tab_core [R_ARM_CALL]

==== FIRMWARE.bin (Thumb; file offset, runtime = +0x40000) ====
0x086170: cmp r1, #0xa                                   d2h_notify: id <= 10
0x086174: mov r7, r1
0x08617c: ldr r5, [r3]                                   r5 = *0x172130
0x086198: movs r3, #1
0x08619e: lsls r3, r7                                   1 << id
0x0861a0: orrs r3, r1
0x0861a8: ldr.w r1, [r5, #0x9c]                          out[1] pointer
0x0861b0: str r3, [r1]                                   out[1] = mask
0x0861b2: ldr.w r4, [r5, #0xa4]                          doorbell pointer
0x0861c0: str r3, [r4]                                   doorbell |= 1
0x0861dc: (literal pool word) 0x00172130                 the msg global
0x009706: str r2, [r3]                                   *0x172130 = g
0x009760: ldr r1, [pc, #0x74]  lit 0x40101434            D2H doorbell
0x009762: ldr r2, [pc, #0x78]  lit 0x40039014            out[1]
0x009764: str.w r1, [r5, #0xd8]
0x009784: strd r2, r3, [r5, #0xd0]                       [0]=out[1], [4]=out[0]
0x00978c: str r7, [r2]
0x00978e: str r7, [r3]
0x0818ac: push {r3, r4, r5, r6, r7, lr}                  firmware H2D dispatcher
0x0818b6: ldr r2, [r0, #0xc]                             ctx+0xc = ack
0x0818b8: str r7, [r2]
0x0818bc: ldr r5, [r2]                                   read pending
0x0818be: str r1, [r2]                                   clear pending
0x0818c4: str r1, [r2]                                   re-arm = 8
0x0818da: ldr r3, [r6, #0x20]                            handler table
0x0818e8: blx r3
```

## 8. Proven vs open

| claim | status |
| --- | --- |
| the mailbox D2H id set the host registers is exactly {1, 3, 6, 7} | **verified** (`pcie_msg_init` @ `0xb6e4`, four `pcie_msg_register` sites) |
| table layout `{handler @ +0, arg @ +4}`, id = lowest set bit of `out[1]` | **verified** (`pcie_msg_register` @ `0x15fbc`, `pcie_msg_handle` @ `0x171f8`) |
| handler per id: 1 stub, 3 DR-drain, 6/7 wake the SR pump | **verified** (handler bodies) |
| no D2H handler replies (no `pcie_msg_send` in any handler) | **verified** (4 `pcie_msg_send` sites enumerated) |
| the firmware's D2H emitter is `d2h_notify` @ file `0x86170` | **verified** (only `out[1]`/doorbell writer in the image) |
| group-4 table ids 0..4 and their handlers | **verified** (.data bytes + `.rel.data` relocs + registration) |
| id 1 = `device_plat_ready`, id 2 = `host_ready` (record's "id 2 = device_plat_ready" is off by one) | **verified** |
| the HCC id -> handler lists for hmac/wal/hdpp (groups 0-3) | `[record]` (`phase25`); registration sites re-verified here |
| which firmware id the D2H bit carries for each HCC event | **open** - the D2H notify call site is not in this image (section 3) |
| the transport a D2H payload rides does not affect the id mapping | inferred (the mailbox id and the HCC id are independent layers, section 6) |
