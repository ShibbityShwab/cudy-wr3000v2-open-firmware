# ROM stage S0/S1 reconstructed: the CA 0x1BF000 inter-core boot handshake, the ETE engine-global gap and the discriminating read pack

**Lane:** wifi-rom-s01 (shared task `task-83`), **host-only**, 2026-11-23. The bench router was
never contacted, no image was built, no device was opened, and no vendor byte/binary/firmware
content is committed here - only derived addresses, constants, structure layouts and a
read-only bench plan.

Local inputs (never copied):

| artifact | identity |
| --- | --- |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B, sha256 `7fc87e20...ef311b` |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko` | 2.5.24 (364,660 B) |
| 265 vendor modules in `/tmp/rootfs2524b/lib/modules/5.10.201/` + `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/` | ETE constant scan |
| prior lanes: `wifi-rom-flow`, `wifi-event-router`, `wifi-ete-engine`, `wifi-fw-link-event`, `wifi-boot-kick`, `wifi-boot-gate`, `wifi-ete-init`, `wifi-ready-emit`, `wifi-hcc-re`, `wifi-host-replica` | cited, not repeated |

Evidence grades: **[V]** verified here from the local artifact with `tools/rom_s01.py` (70/70
selftest), **[V\*]** measured on the bench by an earlier attended lane (cited), **[I]** inference,
**[U]** unknown.

Conventions: firmware **CPU = file + 0x40000**; host **BAR0 (CPU/ROM view) = CPU + 0x6B8000**
(region 5, ACP alias) or **= CPU for CPU < 0x1BFFFF** (region-0 identity); IO CA
**BAR0 = 0x3B8000 + (CA - 0x40000000)** for CA 0x40000000..0x4011FFFF (region 3); PKTRAM CA
**BAR0 = 0x1D8000 + (CA - 0x01000000)** (region 2); **phys = 0x40000000 + BAR0**.

---

## 0. TL;DR - the five answers

**(a) The CA 0x1BF000 page is a ROM<->image boot contract page, not a plain flag.** Layout:
`+0x000` boot-progress bitmap, `+0x004` second flags word, `+0x008` inter-core handshake
**status**, `+0x00C` handshake **magic 0xABCD**, `+0x010` cache-range end. The image (entry A,
header +0x00 = CPU 0x46970) writes `{0, 0xABCD}` to +8/+C at CPU 0x473D4, cleans/invalidates that
range, rings the inter-core doorbell **CA 0x40161F00 <- 0x00020000**, then spins until
`[0x1BF008] == 2` (wait loop calls ARM cache maintenance 0x102E68). **The only image writer of
+8/+C is that one store; no image site writes 2, and no image site can materialise 0x1BD000 /
0x1BD0D8. The partner is therefore mask-ROM code** (the ROM secondary-core/IPC service), i.e. ROM
stage S1 - not image code and not the image-tail CPU1 bootstrap (which is a reset-time, MMU-off
blob that never references 0x1BF000). Golden vendor boot reads `{2, 0xABCD}` [V\*], confirming the
contract end-to-end. **Why our boot leaves 0x1BD000:** those two words are exactly
`{boot-log-arena base 0x1BD000, write cursor 0x1BD000+0xD8}` - the arena the image itself
nominates as crash-dump region 0 - and both the image `0` and the magic `0xABCD` are absent. So
in the D1 boot **entry A never executed its handshake store at all** (the page still holds
pre-image ROM state); the boot is stuck *before* S1 completion, not in a different protocol.
**(b) The ETE engine-global (+0x000..+0x3FF) and doors (+0x7C0/+0x7D0) are authored by nobody
visible: no image constant materialisation of any of those registers, and no vendor module among
265 contains a door/global CA.** New here: the image *does* hold the doors as static CA fields
paired with the two control blocks `{CPU 0x10CAC8: 0x4003A540, 0x10CACC: 0x4003A7C0}` and
`{0x10CB90: 0x4003A720, 0x10CB94: 0x4003A7D0}` (so door0/door1 are the companion registers of the
two engines), but no code ever loads those fields. Door 0x7C0 read **0x8CAC4ECA**, door 0x7D0 = 0;
0x8CAC4ECA is not a valid ring index under the send-word packing and is not an aligned DMA
pointer, so a "sane value" cannot be derived from the image - only the stock-boot golden diff
names it. **Decisive free read:** the firmware's own exception handler copies the entire window
`{0x4003A000, 0xC28}` to PKTRAM CA 0x010DD800 (host BAR0 0x2B5800) - an ETE-window snapshot taken
at exception time, already present on any boot that took an exception (ours: mask 0x0A).
**(c) The readable ROM vector window proves a two-level ROM->image exception ABI.** Vectors:
CA 0x0..0x10 = `ldr pc,[pc,#0x18]` (targets at CA 0x20..0x30), CA 0x14 = NOP (reserved vector 5),
CA 0x18/0x1C = `ldr pc,[pc,#0x14]` (IRQ target at CA **0x34**, FIQ at CA **0x38**). The word at
CA **0x1E4 = 0x0004005C** points into the image header handler table: ROM code at CA 0x50 does
`ldr r0,=[0x1E4]; ldr r0,[r0]; bx r0` = branch to image header +0x5C = 0x001030BC (prefetch-abort
stub). The mask evidence (0x0A = data-abort bit1 + SVC bit3) shows this ABI is live. **The words at
CA 0x20..0x38 - in particular IRQ 0x34/FIQ 0x38 - have never been read and are host-readable in
the identity window; they decide whether IRQ is vectored into the image trampoline CPU 0x40064
or stays ROM-internal. That, plus the unfinished S1 handshake, is the stage our boot misses.**
**(d) The discriminating pack is read-only and stays inside the six host-readable inbound
regions** - see section 6: handshake-page time series, 0x1BD000 boot-log, entry-A state words,
the firmware's crash-dump ETE snapshot in PKTRAM, the live ETE window, and the ROM vector target
words (CA 0x20..0x3C) plus handler-slot pointers (CA 0x1E0..0x1FF) - each with the same set on a
stock vendor boot as the golden diff. **Correction:** the event-controller CAs 0x4016xxxx /
0x4018xxxx are outside all six inbound regions and are NOT host-readable; the D1 pack's
`ev_status/ev_ack/ev_raiseA/ev_raiseB` reads were region-4 aliases of ACP CA 0x0204010C etc. and
must not be repeated.
**(e) Host-side conclusion: the host cannot wake the ETE engine and cannot complete S1.** What
remains is read-only: (i) sample the 0x1BF000 contract page over time and (ii) diff the ETE window
(live + the firmware crash-dump snapshot) against a stock vendor boot, then (iii) read the ROM IRQ
vector target. Those three decide "S1 never completed" vs "ROM IRQ delivery is the missing stage"
without another host write; no further host register work can progress the ETE path.

---

## 1. Method

`tools/rom_s01.py` (new, host-only) reads the local image/modules in place and prints derived
facts: `selftest` (70 checks), `handshake`, `rom`, `engine`, `pack`. The check set covers the
whole-image constant census, literal-load sites, materialisation set (literal loads + movw/movt),
disassembly of the named sites, descriptor-table decode, host-readability model and the vendor-
module negatives. Prior-lane tools were reused read-only for cross-checks
(`wifi-ete-engine/tools/ete_engine.py`, `wifi-rom-flow/tools/fwdis.py`).

New evidence recovered from the project session history (all bench measurements by earlier
attended lanes; no device contact by this lane):

* **golden_v2 trace** on the stock 5.10 vendor stack: `cpu1_hs (phys 0x40877008) = 0x00000002`,
  `cpu1_hs2 (phys 0x4087700c) = 0x0000ABCD`, mask 0x40008 = 0, state 0x118B60 = 1, boot slot
  0x17D42C = 0x000C19D1, g = 0x0010C0F4, ETE intr 0x40039508 = 0x3F201818, out[1] = 0.
* **D1 confirmation pack** (hybrid, up=1904 s): the ETE values in the task context plus
  CA 0x1BF008 = 0x001BD000, 0x1BF00C = 0x001BD0D8.
* **earlier hybrid-state read** (2026-11-23 ~08:03Z, mask 0x0A, state 1): the same pair read
  `2 / 0xABCD` once - provenance (hybrid vs stock stack) not recorded, re-check in the pack.

---

## 2. The CA 0x1BF000 ROM<->image boot handshake page

### 2.1 The page

The page starts immediately after the 0x2000-byte arena at CA 0x1BD000..0x1BF000 that the
firmware itself nominates as crash-dump region 0. It is the top boundary of low RAM in entry A's
own range check (CPU 0x46D4C compares a boot-copy length sum against 0x1BF000).

| offset | role | writer (CPU) | bit / value |
| ---: | --- | --- | --- |
| +0x000 | boot-progress bitmap | entry A 0x4697A / 0x469A4 / 0x473C2 / 0x473FC; fn 0x410C6; fn 0x41268; helper 0x40140 | bits 5, 6, 13, 14, 25, 20, 31 |
| +0x004 | second flags word | entry B 0xC7432 | bit 4 (0x10) |
| +0x008 | inter-core handshake **status** | image 0x473D4 (writes 0); partner writes 2 | 0 -> 2 |
| +0x00C | handshake **magic** | image 0x473D4 (writes 0xABCD) | 0xABCD |
| +0x010 | end of cache-clean range | (not a word; r1 = 0x1BF010) | - |

### 2.2 The handshake code (entry A, CPU 0x46970) [V]

```
0x46974  ldr  r5,=0x001BF000            ; the ROM<->image page
0x46978  [r5] |= 0x20                   ; bit5: first image act
0x469A0  [r5] |= 0x40                   ; bit6: after early clock/console writes
...
0x473BE  [r5] |= 0x2000                 ; bit13: handshake requested
0x473C8  movw r3,#0xabcd
0x473CC  ldr  r4,=0x001BF008
0x473CE  add  r1,r4,#8 ; mov r0,r4 ; strd r2,r3,[r4]   ; [status]=0, [magic]=0xABCD
0x473D8  blx  0x102EE4                  ; ARM cache clean/invalidate [r0,r1)
0x473DC  dmb  ishst
0x473E0  mov  r2,#0x20000
0x473E4  r3 = [0x40161F00] ; [r3] = r2   ; inter-core wake: (1<<1)<<16, no SWI
0x473E8  isb
0x473F0  ldr  r3,[r4] ; cmp r3,#2        ; poll the status word
0x473F4  bne  0x47610                    ; wait path: cache maintenance 0x102E68, retry
0x473F8  [r5] |= 0x4000                 ; bit14: handshake complete
0x47402.. queue boot task (0xC745C) + call scheduler (0xC704C)
```

The exception handler uses the same inter-core doorbell with bit0 set (0x10002 = core0,
0x20002 = core1, CPU 0xC6E50/0xC6E6A), which fixes the project convention
`CA 0x40161F00 = (1<<core)<<16 | 1`; entry A writes 0x20000 = "wake core 1 only".

### 2.3 Who writes what - the partner is ROM code [V]

* **Image writer census:** the only store to 0x1BF008/0x1BF00C in the whole image is `strd` at
  CPU 0x473D4; 0x001BF008 is materialised by exactly two literals (0x47510, 0x47620). No image
  code writes the value 2 to the status word, and 0x001BD000 / 0x001BD0D8 are not materialisable
  constants anywhere in the image (0x1BD000 occurs once, only as the crash-dump table's first
  source pointer; 0x1BD0D8 occurs nowhere).
* **CPU1 image tail (CPU 0x120A6C) is not the partner:** it is a reset-time bootstrap (CP15 MMU
  off -> TLB/cache maintenance -> MMU on at 0x120A9A/0x120B04) that memsets/loads its own WRAM
  tables (0x107B8..0x14408), writes the coreword at CPU 0x10938 and initialises the CPU1 event
  controller 0x4018xxxx; it never references 0x1BF000. The image never materialises its entry
  address either - the ROM owns secondary-core release. [V, [U] observation]
* **Vendor host:** no module among 265 contains any 0x1BFxxx address; the host is not involved.
* => **writer of 2 = mask-ROM code** (the ROM's secondary-core/IPC service), i.e. **ROM stage S1**.
  How the ROM knows the address is [U] (the page is a ROM-defined contract; the CPU1 boot code that
  ROM releases may be ROM-internal, not the image tail).

### 2.4 Golden vendor values [V\*]

Stock 5.10 vendor boot, vendor stack up: **0x1BF008 = 0x00000002, 0x1BF00C = 0x0000ABCD** (with
mask = 0, state = 1, boot slot disarmed, g valid, out[1] = 0). The contract as reconstructed above
is therefore exactly what a fully working boot shows.

### 2.5 Our boot: two observed states and the 0x1BD000 anomaly

| state | 0x1BF000 | 0x1BF004 | 0x1BF008 | 0x1BF00C | meaning |
| --- | --- | --- | --- | --- | --- |
| ROM pre-image | (ROM bits) | (ROM bits) | arena base `0x001BD000` | arena cursor `0x001BD0D8` | S1 handshake never executed |
| image spinning | bit5/6/13 set | bit4 set | 0 | 0xABCD | partner has not answered yet |
| complete (golden) | bits up to 14 set | bit4 set | 2 | 0xABCD | S1 passed; scheduler path entered |

**The D1 pack read `0x001BD000 / 0x001BD0D8`, i.e. the ROM pre-image state - not a different
handshake dialect.** Both words are exactly `{0x1BD000, 0x1BD000+0xD8}`: the base of the
0x2000-byte boot-log arena (crash-dump region 0) and a 216-byte write cursor. They cannot be
produced by image code, and the image's own `0` and `0xABCD` are both absent, so in that boot
entry A never reached CPU 0x473D4. The D1 ETE negative was thus measured on a boot whose **S1
stage is incomplete**. An earlier hybrid read saw `2 / 0xABCD`, so completion is boot/timing
dependent; a single co-sampled read set (handshake + 0x1BF000 bits + arena + entry-A state) on one
boot classifies the outcome without a write.

---

## 3. ETE engine-global and doors

### 3.1 Readability and the firmware's own snapshot

CA 0x4003A000..0x4003AC28 is host-readable at BAR0 0x3F2000..0x3F2C28 (region 3). The firmware
itself nominates exactly `{0x4003A000, 0xC28}` as crash-dump region 1. The exception handler
(CPU 0xC6E14) walks the descriptor table at CPU 0x10F294 and copies, in order, to CA 0x010DB800
with end limit 0x010DFFFF:

| region | source CA | len | destination CA | host BAR0 (PKTRAM) | content |
| ---: | ---: | ---: | ---: | ---: | --- |
| 0 | 0x001BD000 | 0x2000 | 0x010DB800 | 0x2B3800 | boot-log arena copy |
| 1 | **0x4003A000** | **0xC28** | **0x010DD800** | **0x2B5800** | **ETE window at exception time** |
| 2 | 0x40039000 | 0xD5C | 0x010DE428 | 0x2B6428 | PCIe glue block copy |

The descriptor cursor words CPU 0x10F29C / 0x10F2A8 / 0x10F2B4 (host BAR0 0x7C729C/A8/B4) are
written after each successful memcpy, so a nonzero cursor proves that copy ran. On any boot that
took an exception (ours: mask 0x0A) the ETE engine-global + doors are therefore already preserved
in host-readable PKTRAM - **the whole of section 3.2/3.3 below is recoverable with zero writes**.

### 3.2 Writer census - nobody visible authors the global/doors [V]

* Image: `pcie_msg_init` (CPU 0x49334) writes only DR group A + enable, SR group B + enable, the
  two control blocks 0x4003A540/+4/+8 and 0x4003A720/+4/+8, and status/control base 0x4003A86C;
  the two dispatchers write only the ack 0x4003A870. Engine global +0x000..+0x3FF, doors
  0x4003A7C0/+0x7D0, status 0x4003A874, send words 0x4003ACxx and fills +0x3C have **no image
  materialisation at all** (independently confirmed by the ete-engine lane).
* Vendor host: 265 modules scanned; only `hi5622v100_plat.ko` contains ETE CAs and only
  `{0x4003A000, 0x40039508}`; no module contains a door/global/send CA. `pcie_ete_sending_trigger`
  is exported but has no direct BL caller and no importing module; the registered
  `pcie_trigger_ete_sending_handle` symbol is a 4-byte self-branch BUG stub at plat 0x15EFC. There
  is no hidden host engine-start write to copy.
* **New data finding:** the doors appear in image static data as CA fields paired with the two
  control blocks: `{CPU 0x10CAC8: 0x4003A540, 0x10CACC: 0x4003A7C0}` and
  `{0x10CB90: 0x4003A720, 0x10CB94: 0x4003A7D0}`. So door0/door1 are the per-engine companion
  registers of blocks 0/1. The containing objects are used by the ETE dispatchers and
  `pcie_msg_init` only through their lock/queue words 0x10CAD0/0x10CAD4; no code loads the door
  CA fields themselves. The pairing is evidence for the door semantics, not for a writer. [V]
* => the global/door state is ROM- or hardware-authored, i.e. ROM stage S0/S1 environment.

### 3.3 The door value

D1 read: CA 0x4003A7C0 = **0x8CAC4ECA**, CA 0x4003A7D0 = 0 [V\*]. Decode attempt:

* not a ring index under the send-word packing (hi16 0x8CAC = 36012 >> depth 32; lo16 0x4ECA =
  20170);
* inside the device->host DMA window range 0x80000000..0x8FFFFFFF but not buffer-aligned, so not a
  convincing DMA pointer;
* contains the 0xCA byte pattern used elsewhere as a hardware token, but no image/vendor code
  references it (0 hits in image, 0 in 265 modules).

Conclusion: a **sane value cannot be derived from the image**; the door is either a hardware token
that is already correct (engine-global hypothesis falsified) or ROM-programmed state we do not
have. Only the stock-boot golden diff (section 6 row C) can name it. [U]

### 3.4 What would wake the ETE status layer

The status word 0x4003A874 is hardware-authored; the image's only ETE kick (sending trigger
0xC625A) is invoked only by the ETE status dispatchers 0x4624C/0x462F8, which are reached only
through the ROM-vectored event path (image trampoline CPU 0x40064 -> 0xC2EFC -> handler table
CA 0x17D430). Nothing the host writes appears in that loop. The two device-side possibilities are:

1. the ETE engine-global/door state was never established (ROM/first-stage bring-up), so the
   hardware never raises status bits 16-19;
2. the state is fine but the ROM IRQ vector never enters the image trampoline, so status bits are
   never delivered - and independently the S1 handshake page shows entry A may not have run its
   scheduler path in the D1 boot.

Both are decided by the pack in section 6 (rows A, B, C, D) and by the golden diff.

---

## 4. The readable ROM vectors and what they imply

### 4.1 Decode [V arithmetic over V\* live words]

| CA | live word | decode | target word |
| ---: | ---: | --- | ---: |
| 0x00,04,08,0C,10 | 0xE59FF018 | `ldr pc,[pc,#0x18]` | CA 0x20, 0x24, 0x28, 0x2C, 0x30 |
| 0x14 | 0xE320F000 | NOP (ARM reserved vector 5) | - |
| 0x18 | 0xE59FF014 | `ldr pc,[pc,#0x14]` | CA **0x34 (IRQ)** |
| 0x1C | 0xE59FF014 | `ldr pc,[pc,#0x14]` | CA **0x38 (FIQ)** |
| 0x1E4 | 0x0004005C | ROM handler-slot pointer | -> image header +0x5C |

ROM code at CA 0x50 is `ldr r0,[pc,#0x18C]` (r0 = [0x1E4] = 0x0004005C), `ldr r0,[r0]`
(r0 = [0x4005C] = 0x001030BC = the image prefetch-abort stub), `bx r0`, then
`str r1,[sp,#-4]!` to hand the caller's r1 to the image stub. This is a **two-level ROM->image
exception dispatch ABI**: the ROM keeps handler slots in its own WRAM/RAM table and points them at
the image header's five handler words (header +0x50..+0x60 = reset/undef/svc/prefetch/abort =
0x103140/114/0E8/0BC/090).

### 4.2 What the ABI implies

* The ROM does not merely load and branch into header entries A/B; it also **inherits the image's
  exception handlers**. Mask evidence (ours 0x0A = data-abort bit1 + SVC bit3; golden vendor 0)
  proves the SVC and data-abort half of this ABI is live in our boot: the image's common handler
  0xC6E14 ran.
* IRQ/FIQ are the exception classes that matter for the ETE path: the image's ARM trampoline at
  CPU 0x40064 has exactly one caller - the ROM IRQ vector - and the event dispatcher 0xC2EFC has
  exactly one caller - that trampoline's BLX at 0x40094 [V]. So **if CA 0x34 points into the image
  (0x40064/0x40065) the delivery wiring exists and the gap is event assertion; if it points into
  ROM, the image dispatcher can never run.** The word has never been read; it is a free read.
* The second unknown is who starts CPU1 and who answers the handshake. The image contains its own
  CPU1 reset bootstrap (tail 0x120A6C) but never materialises its entry; the ROM's secondary-core
  release code is in mask ROM and unreadable except the identity window. [U]

### 4.3 Which stage our boot misses

The image-side ABI is intact (header entries present, both consumed by ROM only; exception ABI
proven live). What our boot misses is one of two ROM-side stages:

1. **S1 completion** - the ROM dispatching entry A far enough that the handshake store runs and
   the ROM partner answers 2. The D1 boot is stuck before this (page holds the ROM arena
   descriptor). If this is the transport's canonical outcome, the missing stage is ROM/first-stage,
   not ETE.
2. **S13 event delivery** - if S1 completes (page shows 2/0xABCD) the remaining question is the
   ROM IRQ vector target (CA 0x34/0x38). All event-controller registers (0x4016xxxx) are outside
   the inbound regions, so this can only be proven by the ROM vector words, not by reading the
   controller.

---

## 5. ROM contract map (deliverable)

| id | stage | owner | trigger / entry | expected state (read-only markers) | grade |
| --- | --- | --- | --- | --- | --- |
| S0a | power-on vectors | mask ROM | CA 0x0..0x1C -> targets CA 0x20..0x38 | vector words as section 4.1 | [V\*] |
| S0b | ROM boot-log arena init | mask ROM | before image start | CA 0x1BD000 arena + CA 0x1BF008/0x1BF00C = {base, cursor} | [I] |
| S0c | image load + header dispatch | mask ROM | header +0x00 (A) / +0x04 (B) | entries A/B run; system object CA 0x17D398 = 0x60, handler table CA 0x17D430 populated | [V] |
| S0d | secondary-core release | mask ROM | ROM-owned (no image reference to tail 0x120A6C) | CPU1 runs tail; CPU1 controller CA 0x4018xxxx initialised; [U] observable | [I] |
| S1 | handshake + scheduler start | image entry A + ROM partner | CPU 0x473D4 store, CA 0x40161F00 = 0x20000, poll == 2 | CA 0x1BF000 bits 5,6,13 -> 14; CA 0x1BF008 = 2; CA 0x1BF00C = 0xABCD; then boot task queued + scheduler 0xC704C | [V] code / [V\*] golden |
| S2 | boot task first pass | image (ROM-armed reset slot CA 0x17D42C) | reset dispatcher 0xC2EEC | state CA 0x118B60 = 1; registry CA 0x171B6C populated; g = 0x0010C0F4; slot disarmed 0xC19D1 | [V\*] |
| S3 | exception ABI (ROM->image) | mask ROM + image stubs | vectors -> header +0x50..+0x60 | mask CA 0x40008 bits; staged frame CA 0x40008..; PKTRAM crash dump; flag CA 0x17DF78 = 0x0F | [V] code / [V\*] mask 0x0A |
| S4 | IRQ/FIQ delivery | mask ROM | ROM vector targets CA 0x34/0x38 -> image 0x40064? | [U] - read CA 0x20..0x3C; if 0x40064/65 the path is wired | [U] |
| S5 | ETE engine bring-up environment | mask ROM / hardware | engine global +0x000..+0x3FF, doors +0x7C0/+0x7D0 | [U] - recover from the firmware crash dump (BAR0 0x2B5800) or stock-boot read (BAR0 0x3F2000) | [U] |

---

## 6. The discriminating read-only pack (deliverable)

All rows are reads; no writes, no builds, no firmware poke. `BAR0` = endpoint BAR0 offset,
`phys = 0x40000000 + BAR0`. PKTRAM rows take BAR0 = 0x1D8000 + (CA - 0x01000000).

### A. Hybrid boot - handshake page time series (one fresh boot)

| # | CA | rule | BAR0 | phys | samples |
| --- | ---: | --- | ---: | ---: | --- |
| A1 | 0x1BF000 | CPU+0x6B8000 | 0x877000 | 0x40877000 | t0, then ~1 s, 5 s, 20 s |
| A2 | 0x1BF004 | CPU+0x6B8000 | 0x877004 | 0x40877004 | same |
| A3 | 0x1BF008 | CPU+0x6B8000 | 0x877008 | 0x40877008 | same |
| A4 | 0x1BF00C | CPU+0x6B8000 | 0x87700C | 0x4087700C | same |
| A5 | 0x1BD000..0x1BD0FF | CPU+0x6B8000 | 0x7D3000 | 0x407D3000 | once (64 words) |
| A6 | 0x17D398 +0x00/+0x74 | CPU+0x6B8000 | 0x835398 / 0x83540C | 0x40835398 / 0x4083540C | once |
| A7 | 0x17D430[0x1D,0x3E,0x3F,0x40,0x42] | CPU+0x6B8000 | 0x8354D4 / 0x835528 / 0x83552C / 0x835530 / 0x835538 | +0x40000000 | once |
| A8 | 0x117C00+0xEE0..0xEEC | CPU+0x6B8000 | 0x7D0AE0..0x7D0AEC | 0x407D0AE0.. | once |
| A9 | 0x118B60 / 0x17D42C / 0x40008 | CPU+0x6B8000 / region 3 | 0x7D0B60 / 0x83542C / 0x6F8008 | 0x407D0B60 / 0x4083542C / 0x406F8008 | once |
| A10 | 0x17DF78 | CPU+0x6B8000 | 0x835F78 | 0x40835F78 | once |

Decision: `{x,y}` at A3/A4 = S1 never executed (ROM pre-image values, expected y =
base+0xD8); `{0,0xABCD}` = entry A spinning (A1 has bit13, not bit14); `{2,0xABCD}` = S1
complete. A6/A7/A9 confirm how far entry A ran on the same boot. Rows A5 names the boot-log
content (confirms the arena descriptor interpretation).

### B. Firmware crash-dump snapshot (PKTRAM, zero writes)

| # | read | CA | len | BAR0 | note |
| --- | --- | ---: | ---: | ---: | --- |
| B1 | descriptor cursors | 0x10F29C / 0x10F2A8 / 0x10F2B4 | 3 words | 0x7C729C / 0x7C72A8 / 0x7C72B4 | nonzero == that memcpy ran |
| B2 | **ETE window snapshot** | 0x010DD800 | 0xC28 | **0x2B5800** | global + doors + status + send words at exception time |
| B3 | boot-log copy | 0x010DB800 | 0x2000 | 0x2B3800 | earlier stage evidence |
| B4 | PCIe glue copy | 0x010DE428 | 0xD5C | 0x2B6428 | PCIe state at exception time |

### C. ETE window live (region 3) and the golden diff

| # | read | CA | BAR0 | D1 value [V\*] |
| --- | --- | ---: | ---: | --- |
| C1 | full window | 0x4003A000 len 0xC28 | 0x3F2000 | snapshot payload |
| C2 | engine global | 0x4003A000..0x4003A3FC | 0x3F2000..0x3F23FC | all zero in D1 |
| C3 | doors | 0x4003A7C0 / 0x4003A7D0 | 0x3F27C0 / 0x3F27D0 | 0x8CAC4ECA / 0 |
| C4 | status + ack | 0x4003A874 / 0x4003A870 | 0x3F2874 / 0x3F2870 | 0 / 0 |
| C5 | control blocks | 0x4003A540/44/48, 0x4003A720/24/28 | 0x3F2540/44/48, 0x3F2720/24/28 | +0 = 1 |
| C6 | channels/send/fills/mask | DR 0x4003A590/+38/+3C, SR 0x4003A400/450/4A0, send 0x4003AC00..0x4003AC20, mask 0x4003A86C | region 3 | fills 0x10, sends 0 |

**GOLDEN:** run C1-C6 on a stock 5.10 vendor boot (vendor stack up, Wi-Fi working) and diff.
If doors/global match ours, the engine-global hypothesis is falsified and the gap is event
delivery; every differing global/door word is a ROM-authored ETE state candidate. Also run B1
there: mask = 0 means no exception, so the dump should be absent (cursors 0) - a differential datum.

### D. ROM vector window (region-0 identity: BAR0 == CPU, phys = 0x40000000 + CA)

| # | read | CA | expected / discriminator |
| --- | --- | ---: | --- |
| D1 | ROM vector targets | 0x20, 0x24, 0x28, 0x2C, 0x30, **0x34 (IRQ)**, **0x38 (FIQ)**, 0x3C | 0x40064/0x40065 => IRQ vectored into the image trampoline; < 0x40000 => ROM-internal. Expect one entry == 0x50 (the double-indirect stub already known via 0x1E4) |
| D2 | ROM handler-slot pointers | 0x1E0, 0x1E4, 0x1E8, 0x1EC, 0x1F0, 0x1F4, 0x1F8, 0x1FC | 0x1E4 = 0x0004005C known; entries like 0x40050/54/58/5C/60 would prove all exception classes map onto the image header table |
| D3 | record | 0x0..0x1C, 0x1E4 | known values (section 4.1) |

### E. Golden stock-boot reference set

E1 handshake page A1-A4 (golden 2 / 0xABCD); E2 A5, A6, A7, A9; E3 C1-C6; E4 D1/D2; E5 B1
(expected absent). This is the only way to obtain the engine-global/door reference values and the
ROM IRQ target.

### F. Cannot-read / do-not-repeat corrections

* CA 0x4016xxxx and 0x4018xxxx are **outside all six inbound regions** (region 3 ends at CA
  0x4011FFFF). BAR0 0x51810C falls in region 4 and decodes ACP CA **0x0204010C**, so the D1
  pack's `ev_status=0 ev_ack=0 ev_raiseA=0 ev_raiseB=0` were aliases, not event registers. Do not
  repeat them; the event-controller fingerprint is not executable from the host (also proven by the
  earlier vp4 remap attempt: device returns UR 0xFFFFFFFF for 0x4016xxxx).
* CA 0x40039508 (host ETE intr word, BAR0 0x3F1508) is readable but host-owned, not ROM contract.
* Writing 2 to CA 0x1BF008 (faking the partner) remains a Lead-approved diagnostic only, not part
  of this pack.

---

## 7. Definitive host-side conclusion

1. **No host-side action can wake the ETE engine.** The ETE start condition is a firmware command
   gated on a hardware-authored status word; the image does not author the engine-global/door
   state; the vendor stack has no such write (265 modules, whole-image and whole-vendor negatives);
   and the only vendor "kick-like" helper is dead code / a BUG stub. This lane adds no host lever.
2. **The ROM contract is now readable enough to decide the remaining alternatives without any
   write.** The 0x1BF000 page states are exact and discriminating; the IRQ/FIQ vector target words
   are readable in the identity window; the firmware itself has already snapshotted the ETE
   window (PKTRAM) on any boot that took an exception.
3. **The next discriminator is a stock-vendor golden boot run of pack rows A/C/D** (plus the
   hybrid crash-dump B2 on ours). It decides: engine-global mismatch (ROM ETE bring-up missing) vs
   engine-global match (the gap is ROM IRQ delivery / S1 completion). Both outcomes are
   ROM/firmware-internal; neither is host-fixable.
4. **Static work that can still progress:** finish the decode of the image-tail CPU1 bootstrap and
   its relation to the ROM release (who supplies its entry address), and the two engine-context
   objects at CPU 0x10CA00..0x10CC00 that pair the control blocks with the doors. These are
   image-only and can be done without the bench, but cannot prove the ROM side.

---

## 8. Negative results / do-not-chase

* No image writer of the handshake status value 2, of 0x1BD000/0x1BD0D8, of the ETE engine
  global, of the doors, the status word, the send words or the fills [V].
* The CPU1 image tail is not the handshake partner and is not the ETE bring-up [V].
* No vendor module writes the engine global/doors [V] (265 modules).
* The event controllers 0x4016xxxx/0x4018xxxx are not host-readable; their previous "0" reads were
  region-4 aliases [V arithmetic + V\* bench remap].
* Another exception-vector poke, another boot-task re-kick, another D2H drain variant, another
  ETE ring-mirror pre-post: already proven negative or irrelevant in prior lanes; do not repeat.

---

## 9. Reproduction (host-only; local blob read in place; nothing vendor committed)

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
T=opensource/docs/incoming/wifi-rom-s01/tools/rom_s01.py

$PY $T selftest      # 70/70 PASS - every fact in this report
$PY $T handshake     # page layout, writers, handshake code, golden/D1 states
$PY $T rom           # ROM vector window decode + ABI inference
$PY $T engine        # engine-global/doors writer census + crash-dump snapshot route
$PY $T pack          # the read-only discriminating bench pack
```

The tool reads `HI5622_FW` / `HI5622_PLAT_KO` in place (env-overridable) and prints derived
annotations only.

---

## 10. Evidence table (new claims of this lane)

| # | claim | method | grade |
| ---: | --- | --- | --- |
| R1 | CA 0x1BF000 page layout: +0 bitmap, +4 flags, +8 status, +C magic, +10 cache end; bits 5/6/13/14/20/25/31 writers, +4 bit4 by entry B | disassembly at the 8 writer sites; register preservation of r5 across entry A | [V] |
| R2 | handshake sequence {0,0xABCD} -> cache clean [0x1BF008,0x1BF010) -> CA 0x40161F00=0x20000 -> poll ==2; success path queues boot task 0xC745C and calls scheduler 0xC704C | disassembly CPU 0x473C8..0x47428 | [V] |
| R3 | only image writer of +8/+C is CPU 0x473D4; no image writer of 2; 0x1BD000 materialised once only as crash-table src0; 0x1BD0D8 never | whole-image census + materialisation scan + literal refs | [V] |
| R4 | CPU1 tail (0x120A6C) is a reset-time MMU-off bootstrap with its own WRAM tables (0x107B8..0x14408), 0x10938 coreword, 0x4018 controller; never references 0x1BF000; entry never materialised by the image | disassembly; literal scan; full-image value scan | [V] |
| R5 | golden vendor boot reads CA 0x1BF008 = 2 / 0x1BF00C = 0xABCD; mask=0, state=1, g valid | prior session golden_v2 trace (bench) | [V\*] |
| R6 | D1 hybrid boot reads 0x001BD000 / 0x001BD0D8 = ROM {arena base, cursor} pre-image state | D1 confirmation pack + image census | [V\*] + [I] |
| R7 | ETE crash-dump layout: {0x1BD000,0x2000}->0x010DB800, {0x4003A000,0xC28}->0x010DD800, {0x40039000,0xD5C}->0x010DE428, end 0x010DFFFF; PKTRAM BAR0 0x2B3800 / 0x2B5800 / 0x2B6428; cursors at 0x10F29C/A8/B4 | handler disassembly 0xC6F00..0xC6F22 + descriptor table | [V] |
| R8 | doors appear only as static CA fields paired with blocks 0/1 at CPU 0x10CAC8/CC and 0x10CB90/94; no code loads those fields | data dump + literal scan | [V] |
| R9 | no host product writes the engine global/doors; plat.ko has no door/global CA; the bit6 handler symbol is a BUG stub | 265-module scan; plat.ko .text/.data scan | [V] |
| R10 | ROM vector decode: targets CA 0x20..0x30, IRQ 0x34, FIQ 0x38; [0x1E4]=0x4005C double-indirects into image header +0x5C = 0x001030BC (prefetch stub) | live vector words [V\*] + arithmetic + image header | [V]/[V\*] |
| R11 | event-controller CAs 0x4016xxxx/0x4018xxxx are outside all inbound regions; BAR0 0x51810C aliases ACP CA 0x0204010C | region table + iATU program + vp4 remap result | [V] |
| R12 | the D1 ETE negative was measured on a boot whose S1 handshake had not executed; whether S1 completion changes the ETE/event state is the open discriminator | R6 + prior lane state reads | [I] |

---

## 11. Limitations / open items

1. **Mask ROM body is unreadable** except the identity window; `[U]`: which ROM code writes the
   handshake reply, whether the CPU1 boot code is ROM-internal or the image tail, and the ROM IRQ
   target (readable via CA 0x34/0x38 - pack D1).
2. `[U]`: whether CA 0x1E4 is a fixed slot or re-pointed per exception class (pack D2).
3. `[U]`: door 0x7C0 semantics and the sane/expected value (only the stock-boot golden diff).
4. `[U]`: the provenance of the earlier "2 / 0xABCD" hybrid read (stock vs hybrid); re-sample in
   the same run as the pack.
5. The pack is read-only and must be run by an attended bench lane; this lane did not contact the
   device.

*No vendor binary, firmware byte, calibration value or secret is included in this document; all
contents are derived addresses, constants and structure layouts. Device untouched by this lane.*
