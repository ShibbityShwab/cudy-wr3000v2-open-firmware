# REPORT - the firmware RX delivery internals: the "RX Thread" is the H2D message thread, the D2H publish path is complete, and the PPDU descriptor list is the probe target

**Lane:** `wifi-rx-f1-thread` (shared task **task-216**).  **HOST-ONLY**: reads the
pinned local vendor firmware image (and the committed stock register dump) in place;
no device contact, no flash, no fire.  The Lead fires.
Companions: [README.md](README.md), [TABLES.md](TABLES.md) (every address/word),
[PROBES.md](PROBES.md) (the read-only probe pack R7), [FIRE-RECIPE.md](FIRE-RECIPE.md)
(the EDCA auth fire), [tools/fw_rx_f1_thread_re.py](tools/fw_rx_f1_thread_re.py)
(stdlib re-derivation tool, `--selftest` 86/86 PASS), [evidence/](evidence/).

Grades: **[V]** re-derived here by the committed tool from the pinned image;
**[V\*]** committed earlier lane or the stock dump; **[I]** inference; **[U]** needs a
device read.  CA = file offset + 0x40000 of `FIRMWARE.bin` (928,920 B, sha256
`7fc87e20...c3ef311b`).

---

## 0. Verdict

1. **The "RX Thread" is the HCC host-message receive thread, not a Wi-Fi MAC RX
   thread.** The firmware's own thread table carries `HCC TX Thread` /
   `HCC RX Thread` / `HCC OAM Thread` (CA 0x10CFC8/0x10CFF8/0x10D028, 0x30-byte
   records); the stored RX entry 0x102552 is a **host-message (H2D) queue
   processor** that dispatches received HCC frames through the group/id dispatch
   CA 0xFD7E8. It cannot be the reason a received 802.11 frame fails to reach the
   host. The f1-capture lane's firmware-internal "RX thread never started"
   wording must therefore be retired: the thread that exists is the *host-message*
   thread, and our host-message path is demonstrably alive (every H2D send in the
   ladder is consumed; the g4 ready pair is received). **[V] data / [I] role.**
2. **The D2H publish path is complete and not the F1 gate.** The notify function
   CA 0xC6170 (id<=10, pending mask, doorbell), the 8-byte ETE node fill
   CA 0xC192A, and the H2D dispatch chain are decoded; all of the delivery
   preconditions named by phase43 (channel enable, `out[1]` drain, notify id<=10,
   gate 0x4000010C==0xCECE) are satisfied on our boot - the ready pair proves the
   D2H deposit surface works end to end. **[V] decode / [V\*] bench proof.**
3. **The PPDU descriptor list is located statically and is the one RX-path
   firmware structure the f1 lane pointed at.** The lock name
   `&pst_device->st_device_alloced_ppdu_dscr_list.st_spin_lock` (CA 0x1060F7) is
   loaded at CA 0xC02BE and stored into the init object at +0x67C; the list head is
   +0x66C/+0x670, the count +0x674, the 0x1a0-byte descriptor allocator is
   CA 0xBF128 (call at 0xC0402), and the init function CA 0xC016C is called from
   the device/VAP start path CA 0xD3BAC. The runtime object address is now a
   *computable probe* (per-chip context, formula in sec. 5/PROBES P4), not an
   unknown. **[V] static decode, [U] runtime address.**
4. **F1 after this decode:** the stock H2D surface carries no RX enable (f1-capture
   [V\*]); the D2H surface is proven; the completion-ring phase, the WOC buffer
   class and the address transform are already excluded ([V\*] sibling lanes).
   The residual is a **firmware-internal RX state / first-PPDU consumer fault**,
   and the only host-message candidate left is the EDCA timeout (fired by the
   recipe in [FIRE-RECIPE.md](FIRE-RECIPE.md)). The probe pack R7 is built to
   distinguish "firmware produced a D2H RX" from "firmware never entered the RX
   consumer" in one attended pass.

---

## 1. Inputs and method

* firmware image `rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin` (2.4.15 ==
  2.5.24 payload here), 928,920 B, sha256 `7fc87e20...c3ef311b`, CPU = file +
  0x40000 **[V]**;
* stock register dump `opensource/dumps/reg_all.txt` for reference words **[V\*]**;
* committed lane findings used as inputs: `wifi-f1-from-capture` (the H2D trace
  negative, the EDCA ranking), `wifi-rx-format` + `wifi-rx-linux-parity` +
  `wifi-rx-completion-phase` (ring/buffer conformance, the fault shape),
  `wifi-d2h-direct` + `wifi-h2d-send/src/hi5622_contract.h` (the direct D2H reader
  and the address transforms / contract constants), `wifi-tbtt-timer` (the
  per-band ISR/wrapper map and the firmware-RAM probe convention);
* disassembly: capstone 5.0.7 Thumb (local venv) cross-checked with the project
  objdump for every call site quoted; the committed tool uses a small near-range
  Thumb-2 BL decoder only (the image is < 4 MB so S:imm10:imm11 determines every
  intra-image BL; each quoted call site is asserted by the selftest).

## 2. Decode A - the thread table and what "HCC RX Thread" is

The thread table is image data at CA 0x10CFC8; three 0x30-byte records
`{char name[16]; u32 id; u32 stack; u32 prio; u32 aff; u32 flag; u32 0xff;
u32 entry; u32 arg}` **[V] layout, [I] field names**:

| # | name | CA | id | stack | prio | entry (+0x28) |
| ---: | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | HCC TX Thread | 0x10CFC8 | 0 | 0x100 | 3 | 0x000C515F (Thumb 0xC515E) |
| 1 | HCC RX Thread | 0x10CFF8 | 1 | 0x100 | 3 | 0x00102553 (Thumb 0x102552) |
| 2 | HCC OAM Thread | 0x10D028 | 2 | 0x100 | 4 | 0x000C515F (shared) |

Only the TX block has a literal xref anywhere in the image (one init/exception
table word at CA 0x103FD4); the RX and OAM name blocks have none, and **no BL in
the image targets either entry function** [V] - they are table-installed RTOS
tasks, which is why a name-string scan cannot map them to code.

**The RX entry body (CA 0x102552) is a host-message processor** [V]:
`push.w {r0,r1,r2,r4..lr}`; it locks the message queue
(`0xFDF8E` lock / `0xFDF50` unlock, `0x1024BC` exclusive store, `0x1024EA`
first-node pop, `0x102530` splice, `0x102514` queue init), then for each
message calls the group/id resolver CA 0xFD4C8 (bl at 0x102612) and the
**dispatch CA 0xFD7E8** (bl at 0x102644), which reads the 12-byte HCC header and
invokes the group/id handler table.  Its per-message callbacks are the
`[msg+0xc8]`/`[msg+0xcc]` slots.  The TX/OAM entry (0xC515E) is the same
queue-service shape on the outgoing side (spins on the TX queue, builds the
dispatch-class message).

**Consequence:** `HCC RX Thread` = the firmware's **H2D** service thread
(host->device control/data messages). It is part of the path that is already
proven working on our boot, not the path that delivers 802.11 RX data. The
firmware-side 802.11 RX path is the SMAC/DMAC completion path (PPDU descriptors,
DR nodes, D2H CRX events), which the rest of this report probes.

## 3. Decode B - the D2H publish path (why "D2H dispatch" is not the gate)

**Decoded functions [V]:**

| function | CA | facts |
| --- | ---: | --- |
| `notify(id)` | 0xC6170 | `cmp r1,#0xa` -> id must be <=10; reads the HCC object pointer from the global word CA 0x172130; takes the object lock; sets pending bit `id`; publishes the pending mask to the out word at `[obj+0x9c]` and ORs bit0 into the doorbell at `[obj+0xa4]` (the committed contract maps those to the D2H bitmap CA 0x40039014 and D2H doorbell CA 0x40101434) |
| notify callers | 0xC6224 / 0xC6998 | publish notify id **6** and **5** respectively |
| ETE node fill | 0xC192A | fills an 8-byte node `{devva, (len<<16)|0x6d2b}`; the two call sites 0xC621C/0xC6254 are the message-send path (the same path used for the ready pair) |
| H2D dispatch | 0xFD7E8 | resolver 0xFD4C8 selects `{count, handler_base}` from the 12-byte group table, then calls `handler_base + id*16`; the RX thread calls it per host message |
| H2D group resolver | 0xFD4C8 | `ldr r3,[r0+4]` count check, `ldr r0,[r0+0x10]` table base, 12-byte records, 16-byte handler slots |

The committed contract constants give the idle-state probe words for the same
object (`HI5622_CPU_G_OBJECT` 0x0010C0F4; +0xB4 in-flight = 0x0010C1A8; +0xB8
deferred = 0x0010C1AC) and the object image words are zero (selftest) **[V]**.

**Why it is not F1:** the v2 transport receives D2H messages end to end - the
g4 id1 "Device plat ready" / g4 id2 "DEVICE READY" pair is received in every good
boot, and the H2D sends are all consumed **[V\*]**.  A missing RX frame would
have to be invisible on a path that demonstrably delivers other D2H ids.  If the
firmware *staged* an RX D2H, our host reader would see it (the same reader logs
the ready pair).  Probes P1/P3 test exactly that staging side.

## 4. Decode C - the PPDU descriptor list (the RX-path firmware structure)

**Static decode [V] (all re-derived by the tool):**

* debug-name literal: `&pst_device->st_device_alloced_ppdu_dscr_list.st_spin_lock`
  at **CA 0x1060F7**; the only literal-word xref is the pool word at **CA 0xC0484**,
  loaded by `ldr r3,[pc,#0x1c4]` at **CA 0xC02BE** and stored with
  `str.w r3,[r4,#0x67c]` at CA 0xC02C2 - i.e. the name pointer is a *field of
  the list object* at +0x67C;
* the same init function **CA 0xC016C** (called once from the device/VAP start
  path at **CA 0xD3BAC**, `bl 0xC016C`; no other caller) resets the list:
  self-pointers at +0x66C (next) and +0x670 (prev) (stores 0xC02B2/0xC02B6),
  count=0 at +0x674 (store 0xC02D0), name pointer at +0x67C;
* list append/count increment sites 0xC043A..0xC0450; the node allocator is the
  **0x1a0-byte pool allocator CA 0xBF128** (`bl 0xBF128` at 0xC0402, size
  `mov.w r3,#0x1a0`) - the descriptor object size is 0x1a0 B;
* the object is the per-chip module context (the same resolver CA 0x961AE used
  everywhere else - see sec. 5): the list is firmware-owned and never
  host-populated.

**What it means for RX:** the f1-capture lane's pointer was correct - this is the
firmware-side descriptor bookkeeping behind the firmware-mediated RX path
described in `wifi-rx-format` sec. 10.4 (MAC PPDU -> firmware consumes PPDU +
allocated descriptor -> writes the host RX buffer header/completion -> D2H CRX).
The R2b fault shape (page golden loss, status bit 10, no host write) is consistent
with this path executing on the first received PPDU and dereferencing state a
host-only boot never built. The probe now tests the object directly (P4).

**Not claimed:** we do not claim the list is uninitialised on our boot - the init
is called from the VAP start path, which our AP ladder runs. The probe answers
that (the +0x67C self-check: if the name pointer is absent, either the formula
is wrong or the init did not run).

## 5. The per-chip context formula (for the probes)

The EDCA handler and most RX-side firmware helpers resolve their context through
the per-chip resolver **CA 0x961AE** `{mov r1,r0; movs r0,#0; b.w 0xFE578}`;
0xFE578 calls 0x943AC(0) (= CA **0x170E28**, the context manager) and returns

    ctx = u32[manager + 4*(0x18C + chip)] + u16[manager + 0x6DC]

with chip 0 for this image, i.e. **ctx = u32[0x171458] + u16[0x171504]** [V
instructions, I formula application].  The probe self-check is
`u32[ctx+0x67C] == 0x1060F7`: if it matches, both the formula and the PPDU-list
init are correct.

The **EDCA handler** (hook CA 0x68C0A, reached via the installed hook slot
G+0x378 at the g0-id29 handler) writes, after the RF-chain gate
(`ldr [ctx+0x4B8]; add 0x1A00; ldr != 0`):
`ctx+0x4AA` (u16 checksum of payload[0..19]), `ctx+0x533` (u8 per-AC flags,
bitfield), `ctx+0x0DB` bit2 **[V]**.  For the stock zero payload (20 x 00 + 40 x
0xff) the predicted checksum is **0x0000** and the flags/bit2 are 0 - so the
EDCA fire's context-write observable is "0x4AA becomes 0x0000"; if it was already
0 pre-fire the rung is outcome-only (the consumed announce + no D2H reply +
golden page), which the recipe states honestly.

## 6. What the firmware expects from the host to deliver frames

| # | host condition | state on our boot | evidence |
| ---: | --- | --- | --- |
| H1 | RX free ring + completion ring programmed (host cfg block CA 0x40040000, host DMA window) | conformant, byte-checked vs stock | [V\*] wifi-rx-format / rx-linux-parity |
| H2 | RX buffers published with the vendor address transform | identity transform proven; buffers conformant | [V\*] wifi-rx-format follow-up 4 |
| H3 | D2H path armed: gate 0x4000010C==0xCECE, channel enable, `out[1]` drained, notify id<=10 | satisfied (ready pair received) | [V] sec. 3 + [V\*] |
| H4 | H2D message surface: everything the stock host sends | matched (only EDCA missing) | [V\*] fw-f1-capture |
| H5 | RX enable / TRX enable / RX-ring reset message | **does not exist as an H2D message** (stock trace negative; the names are internal) | [V\*] fw-f1-capture sec. 4 |
| H6 | firmware-internal RX/PpdU init reached on the first PPDU | **unknown** - this is the residual | [U] probe P4 |

**Conclusion:** no known host precondition is missing.  F1 is either the EDCA
context sync (last host candidate, low probability) or the firmware-internal RX
consumer.  The probe pack and the fire recipe are built around that split.

## 7. Limits

* The name/layout of the thread records is [V] data with [I] field semantics; the
  task-create site is not located (the blocks are referenced only from an init
  table), so the entry-function-to-thread identity is [I] (strong: the RX entry is
  a host-message processor; the TX/OAM entry is the outgoing side).
* The per-chip ctx formula is [V] instructions but its runtime application is [I]
  until probe P4's +0x67C self-check passes.
* No device was touched; nothing in this lane authorises a device write.  Probe
  addresses are read-only; the fire is the Lead's attended run.
* The stock reference words are from the committed 2026-11-08 stock dump; our
  boot legitimately differs in host DMA addresses (our window vs stock) - compare
  structure/enables/indices, not host addresses.
