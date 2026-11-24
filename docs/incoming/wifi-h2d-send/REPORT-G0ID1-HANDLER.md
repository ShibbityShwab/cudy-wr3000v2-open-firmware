# REPORT-G0ID1-HANDLER - what the firmware actually does with group-0 id-1, and why the 48 vs 72 node length is not its parse dimension

**Lane:** wifi-h2d-send (shared task **task-99**, 2026-11-24), host-only static RE.
**Inputs (read in place, no build-time patch):** `REPORT-PD3.md` (PD3 bench
result), `REPORT-PD2.md` (exception-mask/vector contract),
`../wifi-h2d-catalog` (firmware H2D receive tables/registrations),
`../wifi-hcc-link` (registrar call sites), `phase37/fw-ring-message-table.md`
(mailbox dispatcher + message worker), `phase37/sr-message-catalog.md`
(12-byte HCC header), `../wifi-h2d-frames` (frame/build model), `../wifi-boot-gate`
(boot registration chain).
**Firmware blob:** the pinned local vendor `FIRMWARE.bin` (sha256
`7fc87e20...311b`, 928,920 B, never committed). Image convention: CPU = file +
0x40000; Thumb code starts at CA & ~1.

Evidence markers: **[V]** re-derived here from the pinned blob with
`tools/fw_g0id1_handler.py` (38/38 assertions); **[V\*]** device-measured by the
PD3/TXQ-fire sessions; **[I]** inference; **[U]** not decidable from the blob.

---

## 0. One-line result

The group-0 id-1 H2D handler is a **config-event demultiplexer**, not a
length parser: it reads `hdr[3]` as a user/vap field, `hdr+0x14` as a u16 key,
`hdr+0x16` as a u8 sub-key and hands `hdr+0x18` (payload +0xC) to the key's
worker. The captured TXQ frame carries `key 0x00d8`; that key's worker
**unconditionally dereferences the user/vap context** and stores through it -
a precise data-abort candidate when the context is half-initialised. The
handler never reads the declared total (hdr[4]) or any node length, and the
image contains no materialisation of the SR node flag (`0x6d2b`), so **48 vs 72
changes no offset this code parses**; the 72-byte variant is a transport-engine
experiment, not a handler-parse experiment. The bench's mask `0x12` is
`(1<<1)|(1<<4)` = undefined instruction + data abort; the undefined half is
reconciled by the PD3 **cave clobber**: the exception-staging routine writes
its 17-word context frame at CA 0x4000C..0x4004B, which is exactly the PD3
stub's own code - any exception taken after the stub starts destroys the
poller, so the stuck `out[0]` is expected *after* a fault and the 3x
`out[1]=4` are the staging routine's entries.

---

## 1. Registration and the handler body [V]

Registration is in the firmware's **group-0 receive dispatch table** CA
`0x10CCFC` (40 x 12-byte records `{u16 id; u16 pad; u32 fn; u32 aux}`,
registered by the HCC registrar CA `0x102866` from the boot chain at CA
`0x48346`, group 0, count 0x28; the group-2 table CA `0x10CC18` is the other
call at CA `0x4833C`):

    group 0 entry[1]: id 1 -> handler CA 0x00043D59 (code 0x00043D58)
    group 0 entry[0]: id 0 -> handler CA 0x00081695 (payload-free log stub)

The table is populated by the boot chain before the PD3 epilogue (PD3 enters at
the boot-task success epilogue CA 0x4916A); **the g0 id1 slot is therefore
registered in a PD3 boot** - the handler-table precondition is satisfied [V/I].

Handler decode (CA 0x43D58, 0xBA bytes; full listing in `--handler`):

    43D6C  bl 0x409EA                 ; hdr = hcc_msg_get_hdr_validate(msg)
    43D72  ldrb r1,[r0,#3]            ; field B = hdr[3]
    43D76  bl 0xFE578                 ; ctx = lookup_ctx(0, fieldB)
    43D8E  ldrh r2,[r4,#0x14]         ; key = u16 at hdr+0x14 (payload+8)
    43D94..43DA6  search table A (CA 0x10C23C, 0x6C8 B = 217 8-byte entries)
    43DAC..43DBE  search table B (CA 0x103BF8, 0x2C0 B = 88 8-byte entries)
    43DCE  ldr r3,[entry+4]           ; worker fn
    43DEC  add.w r2,r4,#0x18          ; arg2 = hdr+0x18 (payload+0xC)
    43DF0  ldrb r1,[r4,#0x16]         ; arg1 = u8 at hdr+0x16 (payload+0xA)
    43DF2  blx r3                     ; worker(ctx, subkey, data)

Parse summary (the captured frame's values in brackets):

| offset | width | read as | capture |
| --- | --- | --- | --- |
| hdr+0x03 | u8 | field B -> user/vap context via CA 0xFE578 | `0x04` |
| hdr+0x14 | u16 | key (payload +0x8) | `0x00d8` |
| hdr+0x16 | u8 | sub-key (payload +0xA) | `0x14` |
| hdr+0x18 | ptr | data (payload +0xC) | `01 00 00 00 ...` |

**Not read:** hdr+0x04 (declared total), any node word/node length, any
transport length. `hdr` validation (CA 0x409EA) reads hdr[6]/hdr[7]
(pool-slot membership) and the class field `hdr[5]>>1&7 <= 3`, and contains no
halfword/length load at all [V].

The two key tables are **static image data** (both fully populated: 217 and 88
nonzero entries), so the key lookup itself cannot be an uninitialised-table
fault. `key 0x00d8` appears exactly once: table A entry 41 -> worker CA
`0x00081039` [V].

---

## 2. The key-0xd8 worker and the fault candidates [V/I]

The worker (CA 0x81038), in full:

    81038  push {r3,lr}
    8103A  ldrb r1,[r2]                 ; data[0] = payload+0xC[0]
    8103C  ldr.w r3,[r0,#0x190]         ; r0 = user/vap ctx from CA 0xFE578
    81040  strb.w r1,[r3,#0x4c]         ; store data[0] into [ctx+0x190]+0x4c
    81044..81056  log + return 0

The context lookup CA 0xFE578(0, fieldB): rejects `fieldB > 0x12`, computes a
per-id pointer as `*(base + (0x18C+fieldB)*4) + u16[base+0x6DC]`, and returns 0
on the zero-pointer arms. The handler's `cbnz r0` at 0x43D7A sends a 0 result
to the log/return-0x64 path, so a *clean* NULL context does not fault; the
abort arm is a **non-zero but half-initialised record** whose +0x190 field is
not a valid pointer (then the worker loads garbage and stores through it).

Fault candidates for mask bit `1<<4` (=0x10, vector 4 = data abort; the PD2
bench proved this mapping with mask 0x10):

1. **CA 0x81040 `strb.w r1,[r3,#0x4c]`** - r3 from a bad context: precise data
   abort when the store lands in the mask-ROM window. **Likely first fault.
   [I high]**
2. CA 0x8103C `ldr.w r3,[r0,#0x190]` - wild `r0` makes the load itself abort.
   **[I]** (less likely than a half-initialised record arm).

The **undefined-instruction half** (mask bit `1<<1` = 0x02) has a cleaner
explanation than a handler jump: the PD3 cave clobber. The exception staging
routine (CA 0xC6E14) writes the entering vector's 17-word context frame
(`r0-r12, sp, lr, PC, SPSR`) to **CA 0x4000C..0x4004B**, then writes
`out[1]=4`, pulses the D2H doorbell and spins for `0xCECE` (PD2/PD3 reports).
The PD3 stub occupies exactly CA 0x4000C..0x4004B. So:

* exception #1 (wherever it occurs) overwrites the parked PD3 stub's code;
* an exception whose recovery long-jumps back into/near the cave executes
  staged context words as Thumb -> undefined instruction -> exception #2;
* repeat -> the bench saw 3x `out[1]=4` (3 staging entries) and the CECE
  policy wrote 3 times to release the spins; the poller itself is dead, so
  `out[0]=0x8` stays stuck.

This is the PD3 residual risk ("an external exception while the stub is
parked would overwrite the stub code") materialising [I high]. It also means
**`out[0]` stuck is not evidence that the mailbox consumer is dead**: a
post-ready fire that faults anywhere destroys the poller.

**Decisive next read-back (read-only, no write):** after the fire, read the
staged frame at CA 0x0004000C..0x0004004F (region-0 alias, BAR0 = CA) with the
bench read helper: words 0..12 = r0-r12, 13 = sp, 14 = lr, 15 = exception PC,
16 = SPSR mode bits. Word 15 names the last exception's faulting address
(0x81040/0x8103C => the context-store hypothesis; a cave address => the
clobber cascade); SPSR mode 0x17/0x1B identifies ABT vs UND. The first
exception's frame is lost (overwritten by later ones) but the vector mask
(0x40008 bits) plus the last PC still separate the alternatives.

---

## 3. Does 48 vs 72 change the parse? No [V/I]

* The handler reads only header fields and payload offsets +8/+0xA/+0xC; its
  deepest access is hdr+0x18+0x14 = hdr+0x2C, inside the declared 0x30 total.
  Both the 48-byte node and the 72-byte node carry identical bytes at every
  offset it reads. **[V]**
* hcc_msg_get_hdr_validate reads no length (no halfword load in
  0x409EA..0x40A28); the wire `tag 0x5a5a` / declared-total checks live in the
  SR receive path (phase38/wifi-ete-frames) and are satisfied by both
  variants (tag and total are unchanged). **[V]**
* The image contains **no 32-bit literal `0x00006d2b`** and no `movw #0x6d2b`;
  the three halfword matches are accidental instruction bytes inside the
  appended image tail. The SR node flag/length word is the ETE engine's
  descriptor, not something firmware code compares. **[V]**
* Therefore node length can only matter in the transport/engine layer (DMA
  copy size for the node buffer), not in the g0 id1 parse. The 72-byte
  variant is still worth exactly one fire because it isolates that one
  transport variable against the vendor's live capture (node word1
  `(72<<16)|0x6d2b`, 24 x 0xff pad, declared total 0x30). **[I]**

Practical reading of the 72-byte fire: if the fault signature is *identical*
to the 48-byte fire (mask 0x12, 3x out[1]=4, unread out[0]) the node length is
**not** the cause and the remaining lever is the field-B context precondition
(section 2). If it is clean/consumed, node length mattered at the transport
layer and the 48-byte rule was wrong for this firmware.

---

## 4. The receive path around the handler (why a prior message is not required) [V]

The mailbox dispatcher CA 0xC18AC (reached from the event-0x4C wrapper CA
0x40294) is a **mailbox-id** dispatcher: it acks out[5], reads-and-clears
out[0], writes out[2]=8, then dispatches the lowest set bit of the pending word
through `table[id] {fn,arg}`, ids <= 9. It does **not** parse group/id; ids
0/2/4/7/8/9 are unregistered, and the four registered ids are 1/3/5/6
(phase37). The **id-3 handler** (CA 0xC5145) sets `pcie_msg+0x98=1` and runs
the firmware message worker CA 0xC2A54 on `pcie_msg+0x50`; the worker drains
the SR ring node and dispatches through the group/id tables above.

Consequences for the ladder:

* the handler tables are registered by the boot chain **before** the PD3
  epilogue -> no host message is needed to "install" the g0 id1 handler;
* **no prior g0 id0 (or any other) H2D message is a code precondition** of
  the g0 id1 handler: the g0 id0 handler is an independent log stub, and the
  g0 id1 handler's only external dependency is the field-B user/vap context;
* the payload-free g0 id0 frame is therefore the correct **control fire**: it
  exercises ETE node -> mailbox -> worker -> dispatcher with no payload parse
  and no context dereference. If it faults, the fault is transport/mailbox
  level; if it is clean, the g0 id1 fault is payload/handler-level.

The vendor does not send a prior g0 id0: its first post-ready send is this
g0 id1 TXQ config sync (wifi-hcc-link sec 4). Adding a host-synthesised g0 id0
in front would be a ladder change without vendor-model support; it is only a
diagnostic control, and the one-frame-per-idea rule means it is a separate
boot.

---

## 5. Evidence table

| # | claim | method | grade |
| --- | --- | --- | --- |
| E1 | group-0 table CA 0x10CCFC registers id 1 -> CA 0x43D59, id 0 -> CA 0x81695 | table decode | [V] |
| E2 | the g0 id1 handler parses hdr[3] (ctx), key u16 hdr+0x14, subkey u8 hdr+0x16, data hdr+0x18; no total/node-length read | disassembly + assertion | [V] |
| E3 | key tables A (217) / B (88) are static full tables; key 0xd8 -> A[41] -> worker CA 0x81039 | table decode | [V] |
| E4 | worker 0x81039 dereferences ctx+0x190 and stores to +0x4c with no guard; fault candidates CA 0x8103C / 0x81040 | disassembly | [V] |
| E5 | the node flag 0x6d2b is never materialised by the image; the handler parse is node-length independent | word/immediate scan + disassembly | [V] |
| E6 | mailbox dispatcher 0xC18AC is mailbox-id-only (lowest set bit, ids<=9, table[id].fn(arg)) and has no group/id parse | disassembly | [V] |
| E7 | id3 handler 0xC5145 wakes message worker 0xC2A54; group/id dispatch happens in the worker path | phase37 record re-quoted | [V] |
| E8 | handler tables are registered by the boot chain at CA 0x4833C/0x48346 before the PD3 epilogue | bytes + boot-gate chain | [V]/[I] |
| E9 | mask 0x12 = undefined (1<<1) + data abort (1<<4); data abort alone = 0x10 (PD2) | vector contract + bench | [V\*] |
| E10 | the PD3 cave (CA 0x4000C..0x4004B) is the exception-staging frame area, so any exception clobbers the poller (undef cascade, stuck out[0]) | PD3 report + staging routine | [I high] |
| E11 | the 72-byte node variant cannot change the g0 id1 handler parse | E2+E5 | [I high] |

---

## 6. Reproduce (host-only)

    PY=.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-h2d-send/tools
    $PY $T/fw_g0id1_handler.py --selftest      # 38/38 PASS
    $PY $T/fw_g0id1_handler.py --handler       # registration + parse + fault candidates
    $PY $T/fw_g0id1_handler.py --dispatch      # mailbox dispatcher + worker + boot registration
    # optional: $PY $T/fw_g0id1_handler.py --fw /path/to/FIRMWARE.bin

The tool reads the local pinned vendor blob and prints derived annotations
only; it refuses a blob whose sha256 is not the pinned one. No vendor byte is
committed.

---

## 7. Open items

1. **First fault site [U] from host-visible data so far** - settled by the
   read-only staged-frame read (CA 0x4000C..0x4004F) in the next session.
2. **Field-B context state on our boot [U]** - is the field-B user/vap record
   absent (lookup returns 0 -> clean log return, no fault) or half-initialised
   (non-zero record, missing +0x190 -> data abort)? The staged frame's r0
   (worker ctx) / PC decides.
3. **Whether the ETE engine even hands a 48-byte node payload to the worker**
   ([U]; the 72-byte fire is the experiment).
4. **The vendor's own first-post boot state** - the vendor sends this frame at
   hmac_main_init, before any VAP exists; if its field-B context is created by
   firmware-internal boot code on a vendor boot, our host-only boot may lack
   it. That would make the g0 id1 TXQ sync *not* reachable before the
   vendor's firmware-side user/vap bring-up - a different, larger lane.
