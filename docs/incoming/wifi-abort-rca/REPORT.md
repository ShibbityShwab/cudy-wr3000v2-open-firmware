# Wi-Fi firmware DATA ABORT: descriptor-fill chain root cause (abort-rca lane)

Host-only reversal lane, 2026-11-23. Inputs are local artifacts only: the vendor firmware
blob `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 B,
never copied or committed), the committed transport sources under
`opensource/docs/incoming/wifi-transport-port/build/`, and the prior lane reports. No bench
device was contacted; nothing here is a device write. No vendor bytes are reproduced except
short disassembly annotations of the kind the prior lanes already use.

Evidence markers: **[V]** re-verified here from the local artifact; **[V\*]** device-measured
by an earlier lane and cited; **[I]** inference; **[U]** unknown. Prior lanes' verified results
are cited, not repeated.

Address conventions (used throughout this lane; they reconcile the prior reports):

| name | rule |
| --- | --- |
| CPU / "CA" in image reports | image is loaded at CPU `0x40000`, so **CPU = file offset + 0x40000** |
| host BAR0 of a RAM object | **BAR0 = CPU + 0x6B8000** (region-5 window, base BAR0 `0x6b8000`) |
| host BAR0 of a chip-IO CA | **BAR0 = 0x3B8000 + (CA & 0xFFFFF)** (region-3 window) |
| device CA of a RAM object | device CA = CPU + 0x1200000 |

Consequence used below: the staged exception frame is at CPU `0x40008..0x4004c`, i.e. host
BAR0 `0x6f8008..0x6f804c`; the g object is CPU `0x10c0f4`, i.e. BAR0 `0x7c40f4`.

---

## 1. TL;DR — answers to (a)-(d) and to the three follow-up questions

**(a) Fault hypothesis.** The abort is taken inside the DR/ETE descriptor-state writer
`0x87D64`, called from `0x88AFC` at CPU `0x88bb8` (Thumb `bl`; return address/`lr`
`0x88bbd`). The callee does `dr = *(obj + 0x2CC)` and then stores, for `i = 0..0x3f`, a
31-bit value + bit-31-valid word into `dr + 0x2B8 + 4i`, and (when its direction argument is
non-zero) also into `dr + 0x3CC + 4i` [V]. **The invalid item is the pointer `dr` (the
`*(obj+0x2CC)` slot of the per-core context passed to the chain).** If `dr` is 0 the two
store sequences target CPU `0x2B8..0x3B8` and CPU `0x3CC..0x4CC`, i.e. the chip's ROM region
below the image base — a precise data abort on the first store [I]. If `dr` is a stale/near
pointer the abort lands similarly. The exception handler that runs recovers the fault (mask
bit 1) and finishes the announce contract; this is why the boot chain otherwise looks healthy.

**(b) The init stage that should have populated it and did not.** `dr` is a per-core
field of the DR/ETE descriptor-record bring-up. The only firmware code that writes that
per-core family is `0x84664` (store at `0x846b8`: `str r5,[([ctx+0x190]) + 0x1c*idx + 0x2cc]`,
idx 0..3, gated by `0x961ae`) [V]. Its **only** callers are the two sites `0x651e6`/`0x65268`
inside the four-iteration per-core loop of function `0x65148` [V]. `0x65148` itself is called
only from five firmware flow sites: `0x67048`, `0x6994c`, `0x69aa8`, `0x69dc2`, `0x69e88` [V].
All 61 unique host-reachable firmware handler entry points catalogued in
`wifi-h2d-catalog/REPORT.md` (group-0/group-2 HCC handlers §4.3, early id `0x2f`, and the
5 mailbox ids 1/3/5/6 §3) were checked: **none reaches
`0x65148` or `0x84664` within two BL levels** [V]. So the per-core descriptor-record stage is
firmware-init-only, and on the observed cold boot it never ran to set the descriptor pointer
for the core that executed `0x88AFC`. The concrete reason it did not run is the known
announce/deadlock state: the entry-3/svc announce handler is parked on `0x4000010c == 0xcece`
with the gate wiped by its own preamble (`0x86e7e`/`0x86e80`), so the firmware's task/init
flow that contains the `0x65148` call sites does not proceed [V\* + I].
Note this refines, does not confirm, the ROM-flow lane's `+0x2CC == 0` premise: see §4.4.

**(c) Free host reads that confirm on a parked chip** — host-only, read-only, all in the
region-5 window (`BAR0 = CPU + 0x6B8000`), see §6 and `tools/abort_readplan.py`. The single
decisive pair: read `SPSR` of the staged frame at CPU `0x4004c` / **BAR0 `0x6f804c`** (settles
vector identity and fault mode), and read the candidate context's `+0x2CC` word (see §6 for
the candidate list; the g object's is **BAR0 `0x7c43c0`**).

**(d) Firmware-implied required host action.** There is no host register that starts the
per-core stage — it is not reachable from any host message handler (§5). The firmware-implied
host action is the announce-contract service that lets the firmware's own task/init flow pass
the parked announce handler: keep `0x4000010c == 0x0000cece` while the handler is in its spin,
keep `out[1]` drained/acked/re-armed so the D2H publishes, and do not park the firmware in the
entry-3 handler (the C1e procedure). Once that handler returns, the flow that contains the
`0x65148` per-core loop is the first code that can repair the descriptor state; a true cold
boot with the service loop held before release is the only clean test.

**Follow-up Q1 (which object is `obj`)** — the chain is proven, the exact CA is not yet
pinned by static analysis: `obj` = **arg0 of `0x88afc`** = arg0 of its containing function
`0xda6dc` (call sites `0xda8c8`, `0xe9b18`); `0xda6dc` is called with `r0 = r4` from
`0x72ebe`, `0xa564c`, `0xdc47c`, `0xdc61a`, `0xdc882`, `0xdca60`, and the other chain
`0x88afc`/`0x888cc` caller is `0xe9a7c` called from `0x7473e` (`r0 = r4`) and `0xea10e`
(`r0 = r7`) [V]. In the data-path branch that reaches it, `r4 = *(parent_ctx + 0x4B0)`
(proved at `0x745e4`, `0x746a0` and `0x846c2`) [V], i.e. `obj` is a **per-link/per-core
sub-context object obtained from the parent context at `+0x4B0`**, not the boot task object
at CA `0x117c00` and not the g message context at CA `0x10c0f4`.
**Follow-up Q2 (CA + +0x2B8/+0x2CC/+0x3CC reads)** — candidate list and exact BAR0 addresses
in §6. The boot object's already-read `+0x2CC = 0x00117ec8` is **non-zero**, and its `+0x2B8`
word is a self-referential list head, so it is not the faulting object; the faulting object's
`+0x2CC` must be read at runtime (the per-link contexts are pool objects, not statically
addressed).
**Follow-up Q3 (is the `0x651xx` per-core loop host-reachable?)** — **no** for the catalogued
host paths: no HCC/mailbox handler reaches it within two BL hops [V]; so it is firmware-init
only (detail in §5).

---

## 2. The exception-report mechanism, decoded [V]

The image carries its own exception entry table at CPU `0x40050` (file `0x50`):
`{0x103140, 0x103114, 0x1030e8, 0x1030bc, 0x103090}` and per-vector entry thunks at
`0xc6fc8` (id 4, `-8`), `0xc6fd6` (id 3, `-4`), `0xc6fe0` (id 2, `-4`), `0xc6fea` (id 1, `-4`).
The ARM stubs (`0x103090`, `0x1030bc`, `0x1030e8`, `0x103114`) are identical apart from the
id and the branch to their thunk; each does exactly this [V]:

1. `pop {r0}` -- restores the interrupted `r0` that the ROM dispatcher saved before
   transferring control (the ROM's entry path pushes `r0`, see the ROM glimpse in the
   boot-kick lane §15/§17);
2. `sub sp,#0x44; stm sp,{r0..r12}` -- 13 words of interrupted state;
3. save `lr` (hardware exception return address) and `spsr`;
4. `cps #0x13` and save the SVC-banked `sp` and `lr`;
5. thunk adjusts one word and enters the common staging routine `0xc6e14`.

So the live record at CPU `0x40008` is **not** "mask + a frame whose lr is saved twice"
(the §20.1 reading). The exact layout is:

| word | CA | content |
| --- | --- | --- |
| - | `0x40008` | halfword exception-seen mask `1<<r1` (OR-accumulated; `0x0a` = ids 1 and 3 seen) |
| 0..12 | `0x4000c+4i` | `r0..r12` of the faulted context |
| 13 | `0x40040` | SVC-banked SP |
| 14 | `0x40044` | SVC-banked LR |
| 15 | `0x40048` | LR at handler entry **minus 4** (ids 1/2/3) or **minus 8** (id 4) |
| 16 | `0x4004c` | SPSR of the faulted context (CPSR snapshot) |

Therefore: `word15 + 4 = 0x00088bbd` = the hardware exception return address captured at the
abort, and it equals `word14` (the SVC-banked LR) **only if the data abort was taken from SVC
mode** -- which is exactly the case if SVC code was executing the callee `0x87d64` whose
`lr` is still the `0x88bb8` BL return address (`0x88bbd` with the Thumb bit) [V + I].
The observed frame (prior lane, host reads) is:
`0x0a | 0 | 0 | 0x001bc000 | 0x001bcc48 | 0x3 | 0x40114564 | 0x3 | 0x40114568 | 0x3 |
0x40114588 | 0x33 | 0x40114588 | 0 | 0x001bce54 | 0x00088bbd | 0x00088bb9` [V\*].

**Caveat that must be settled before trusting the register words (open item O1).** Under the
layout above, words 4..11 of the faulted context are `r4..r11` = `3, 0x40114564, 3,
0x40114568, 3, 0x40114588, 0x33, 0x40114588`, which are the value/address pairs of the
firmware's own PCIe/PHY `{CA,value}` table at CPU `0x1059c0` (pairs 7..10), **not** the
registers of `0x87d64` (which would show `r0 = ~3`, `r5 = *(obj+0x2cc)`, `r3 = loop index`).
Two readings are possible and are discriminated by one read:
* **R1 (in-band)**: the abort really is in `0x87d64` (lr says so) and `r4..r11` were clobbered
  by the ROM dispatcher before the image stub saved them (the stub only restores `r0`); then
  the staged register words are unreliable and lr/SP/SPSR are the only usable fields.
* **R2 (PHY applier)**: the frame was overwritten by a later exception staged from the PHY
  table-applier code (the `(value,address)` register cascade), and the descriptor-path lr is
  coincidental for the currently parked exception record.
*Decisive read:* SPSR at CPU `0x4004c` / BAR0 `0x6f804c`. Mode bits `0x13` (SVC) support R1,
`0x12` (IRQ) supports R2 (the prior lane's earlier staged SVC frame had SPSR `0x60000192`,
i.e. IRQ mode [V\*], so an IRQ-mode stage is not hypothetical).

---

## 3. The descriptor-fill chain, fully decoded [V]

All addresses are CPU (file = CPU - 0x40000).

### 3.1 Caller `0x88afc` (file `0x48afc`)

`push.w {r4..r8,sb,sl,fp,lr}; sub sp,#0x224`; `memset(sp+0x20, 0, 0x200)` via ROM
`0x1031a4`; `base = 0x87fb0(obj, obj[1], 0)` (per-CPU table selector: reads `obj+0x12d`, a CPU
id and returns one of CPU `0x10d5c4/0x10d724/0x10db44/0x10de04`); then for a 0x160-byte
stride-0x20 loop calls `0x88828` to assemble per-record words; then for descriptor index
`i = 0..0x3f` it builds a 32-bit word from a stride-0x14 source record array `*(arg1)` and
stores it at `buf + arg2*0x100 + 4i` (`0x88c7c..0x88cf2`, `str r3,[r8,r7,lsl #2]`); finally:
`0x87d64(obj, buf + arg2*0x100, arg2, 0, 0x40)` at **`0x88bb8`** (return `0x88bbd`) [V].
The sibling direction function `0x888cc` calls the same writer at `0x88a90` with
`(obj, buf, dir, 0x40, 4)` [V].

### 3.2 Callee `0x87d64` (file `0x47d64`) -- the fault site

```
0x87d64 push {r4,r5,r6,lr}
0x87d66 r5 = [r0 + 0x2cc]          ; dr = *(obj+0x2CC)  <-- the pointer at issue
0x87d6a r0 = ~3
0x87d6e r4 = byte[sp+0x10]         ; count (0x40 or 4)
0x87d72 r0 = r1 - 4*r3             ; buf - 4*start
0x87d76 r4 = r4 + r3               ; end index
0x87d78 if (r2 == 0) goto 0x87dac  ; r2 = direction
0x87d7a loop_a (writes dr+0x3CC array):
0x87d7e   r1 = r5 + (r3<<2)
0x87d82   r6 = [r0 + (r3<<2)]      ; word from descriptor buffer
0x87d86   r2 = [r1 + 0x3cc]
0x87d8c   bfi r2,r6,#0,#0x1f       ; low 31 bits = value, bit31 preserved
0x87d90   [r1 + 0x3cc] = r2        ; STORE
0x87d96 loop_b (writes dr+0x2B8 array): same with [r1+0x2b8] / 0x87da8 [V]
```

So the two arrays are `dr + 0x2B8 + 4i` and `dr + 0x3CC + 4i`, 64 entries each, each entry a
**31-bit value with a separate bit-31 flag** (typical ring/descriptor base register shape)
[V]. The reader in the same module, `0x88fa4`, confirms it reads the same two arrays
(`[r0+0x2cc]` then `0xae+idx`/`0xf3+idx` indices) and decodes bitfields at 0..9,
10..11, 12..14, 15..23 and 24..29 into an outbound descriptor header [V].

### 3.3 Object layout used by the chain (fields verified in code)

| field | type | used by | meaning [I unless noted] |
| --- | --- | --- | --- |
| `obj+0x01` | u8 | `0x88afc`, `0x888cc` | descriptor-set count/mode selector [V] |
| `obj+0x12d` | u8 | `0x87fb0` | CPU/table selector for the per-CPU source table [V] |
| `obj+0x280` | ptr | `0x888cc` (s16 array), written as literal `0x1813cc` in `0xda6dc` | per-CPU table pointer [V] |
| `obj+0x284/+0x288` | ptr | `0x888cc`, `0xda6dc` | s16 tables (offsets/limits) [V] |
| `obj+0x294/+0x298` | ptr | `0xda6dc` | further per-CPU tables [V] |
| `obj+0x2cc` | ptr | `0x87d64`, `0x88fa4` | **descriptor-state block `dr`** (64+64 31-bit slots) [V] |
| `obj+0x2d8` | ptr | `0x88024` | register block programmed by `0x88024` (`+0x350 |= 0x10000000`, `+0x158` bfi 0x28) [V] |
| `obj+0xe0..0x104` | u32 x? | `0x87db2` | four 10-bit base/end pairs per queue (values `0x3cc,0x3bc,0x3a8,0x390,0x380,0x360,0x200` written as bfi at bit 0 and bit 16) [V] |
| `obj+0x4b0` | ptr | `0x745e4/0x746a0/0x846c2` | per-link sub-context pointer [V] |
| `obj+0x190` | ptr | `0x84664`, `0xfea..` | per-core record array base (records `0x1c` apart) [V] |

`dr` block itself: 64 words at `dr+0x2b8` and 64 words at `dr+0x3cc` [V].

---

## 4. Where `obj` comes from and what writes `*(obj+0x2cc)`

### 4.1 Proven caller chain

* `0x88afc` is called only from `0xda8c8` (inside `0xda6dc`) and `0xe9b18` (inside `0xe9a7c`)
  [V]. Both pass their own arg0 unchanged.
* `0xda6dc` (`push.w {r4..lr}; mov r4,r0; ...`) is called from `0x72ebe`, `0xa564c`,
  `0xdc47c`, `0xdc61a`, `0xdc882`, `0xdca60`, all with `r0 = r4` of their own function [V].
* `0xe9a7c` is called from `0x7473e` (`r0 = r4`) and `0xea10e` (`r0 = r7`) [V].
* In the data-path branch, those functions obtain the object as `r4 = [parent+0x4b0]`
  (`0x745e4`, `0x746a0`; and the init-side `0x846c2` uses the same `+0x4b0` sub-context
  pattern) [V]. => `obj` is a **per-link/per-core context**, a pool object, not a static
  image object address.

### 4.2 Why `obj` is not the two live-readable objects already dumped

* **Boot task object CA `0x117c00`**: live `+0x2cc = 0x00117ec8` (self+0x2c8) and live
  `+0x2b8..+0x2c8` are self-referential list heads (`0x117eb8/0x117ec0/0x117ec8`) [V\*], which
  cannot be the 64-entry 31-bit slot arrays of §3.2. Also `+0x280` reads `0x4188d`, not a
  pointer [V\*]. => not the faulting object.
* **g message context CA `0x10c0f4`**: the image-resident template of g at file `0xcc0f4`
  has `g+0x190 = 0x52`, `g+0x2cc = 0x0008558d`, `g+0x2d8 = 0x6e` [V] -- not the 64-slot block shape of §3.2 (and if used as a pointer it lands in RAM, so it would
  not produce the observed ROM-target fault); runtime code does overwrite
  `g+0x9c/+0xb0/+0xb4/+0xb8/+0xc0`, so the static words are not final proof, and no writer of
  `g+0x2cc` was found. => probably not the faulting object; the §6 reads settle it.

### 4.3 The `+0x2cc` writer sites (corrects a premise of the ROM-flow lane)

A raw scan for `str.w rX,[rY,#0x2cc]` finds exactly three sites, but decoding each shows they
write three **different object families** [V]:

1. `0x5c26c` (in the ops/callback-table constructor around `0x5bdd8`): writes the literal
   CPU `0x0007b725` (Thumb code pointer) to `[obj+0x2cc]` and `[obj+0x2e4]`, in a run that
   also fills `+0x1bc/+0x220/+0x84/+0x28/+0x54` with code pointers. This is an **ops table**,
   not the descriptor context.
2. `0x846b8` inside `0x84664`: writes `r5` into `[([ctx+0x190]) + 0x1c*idx + 0x2cc]` with
   `idx = 0..3`; `r5` is the small integer parameter handed in by the per-core loop
   (`0,0,0xbc0,0x5e0` are the static source-record values at CA `0x117a14+0x10i+0xc`) [V].
   So this fills a **per-core parameter array**, not the 64-slot descriptor block pointer.
3. `0xfeaf6` (in the `0xfea..` per-core block init): writes table-sourced words into the
   same `[ctx+0x190] + 0x1c*i + 0x2bc..0x2cc` record array (`i=0..3`) [V].

Consequence: **no direct-store writer of the descriptor context's `+0x2cc` is present in the
image**; it must be set by a structure copy/loop or an encoding the imm12 scan misses [U].
So the ROM-flow lane's hypothesis `obj+0x2cc == 0 -> ROM-space store` remains plausible but
**unproven by writer analysis**; it must be confirmed by the live `+0x2cc` reads in §6, and
the per-core parameter array of item 2/3 is a separate field from the descriptor-block
pointer (do not conflate them).

---

## 5. Reachability of the per-core bring-up (follow-up Q3)

* `0x84664` (with the `+0x2cc`-family store at `0x846b8`) is called only from `0x651e6` and
  `0x65268`, both inside function `0x65148` [V].
* `0x65148` (a four-iteration, `r4 += 0x10` per-core loop with log ids `0xb31/0xb3a/0xb42`)
  is called from five sites: `0x67048`, `0x6994c`, `0x69aa8`, `0x69dc2`, `0x69e88` [V].
* Host-reachability test: all 61 unique host-reachable handler entry points of the firmware's
  own HCC H2D dispatch tables (group-0/group-2 entries of
  `opensource/docs/incoming/wifi-h2d-catalog/REPORT.md` §4.3, early id `0x2f`) plus the 5
  mailbox handlers (files `0x510`, `0x85128`, `0x85144`, `0x819dc`, `0x4cce4`, same report §3) were
  disassembled and their BL targets followed one level deep. **None** reaches `0x65148`,
  `0x84664` or the loop sites [V]. Host H2D traffic therefore cannot start the stage; it is
  firmware-init/task-flow only.
* Reaching it from the host must go through the firmware's own flow after the announce
  handler returns, i.e. the C1e contract (see §1(d)).

---

## 6. Read-only confirmation plan (parked chip, host BAR0)

`tools/abort_readplan.py` prints the whole list; the decisive entries:

| # | CA | BAR0 | expected if hypothesis holds |
| --- | --- | --- | --- |
| 1 | `0x4004c` (SPSR) | `0x6f804c` | `0x13` (SVC) => in-band R1; `0x12`/other => open item O1 flips |
| 2 | `0x40048` (word15) | `0x6f8048` | `0x88bb9` (known) |
| 3 | g`+0x2cc` = `0x10c3c0` | `0x7c43c0` | 0/invalid => g is a faulting-context candidate; valid RAM pointer => g is not the faulting context |
| 4 | g`+0x2b8/+0x3cc` | `0x7c43ac`/`0x7c44c0` | 64-word arrays (should look like ring bases, not list heads) |
| 5 | g`+0x190` (block ptr) | `0x7c4284` | valid RAM ptr; then `[block+0x2cc+0x1c*i]` (`BAR0 = 0x6b8000+block+0x2cc+0x1c*i`) |
| 6 | boot`+0x190/+0x2cc` | `0x7cfd90`/`0x7cfecc` | boot object is not the faulting object (known `+0x2cc=0x117ec8`) |
| 7 | `0x171b70+0x2cc`, `0x171e1c+0x2cc`, `0x171f54+0x2cc`, `0x171fa8+0x2cc` | `0x829e3c`,`0x82a0e8`,`0x82a220`,`0x82a274` | registry destination objects; 0 in the right one would mark the missing per-core stage |
| 8 | `[ctx+0x4b0]` of the readable contexts then `+0x2cc` of the result | via g/boot `+0x4b0` (`0x7c45a4`/`0x7d00b0`) | identifies the actual per-link `obj` and its `dr` |

Pass 1 and 2 first (they cost one read each and decide O1); then the `+0x2cc` sweep.
**Result of this sweep on a fresh POR (2026-11-23):** rows 3 and 5 were invalid/static for g
and the boot object (`g+0x2cc` = image template `0x0008558d`, `g+0x4b0` = image template
`0x000001cf`); see section 10 - g is neither the obj nor the parent. The updated, still
read-only follow-up list is in section 10.4. If a
`dr` value is read, also read its two arrays at `BAR0 = 0x6b8000 + dr + 0x2b8` and `+0x3cc`:
those addresses must be inside the CPU RAM extent `0x00000..0x1bffff`; anything else (e.g.
`0x2b8`/`0x3cc` themselves, or a code address) is the invalid-pointer signature.

Device-CA checks for the IO-vector alternative R2 (read-only): `0x40114568`/`0x40114564`/
`0x40114588` at BAR0 `0x4add68`/`0x4add64`/`0x4add88`, plus the final-value comparison
already done in the boot-kick lane §21.

---

## 7. Firmware-implied host action and test

1. On a true cold boot, run the C1e service loop **before** release: serve `0x0000cece` to
   `0x4000010c`, drain `out[1]` (`0x40039014`) to 0, ack/`re-arm` `0x40101438`/`0x40101414`,
   keep `g+0xb4` (armed) at 0, cadence < 100 us. Do not leave the entry-3 handler parked.
2. Release `0x5a5a` to `0x40000108` and keep serving for >= 90 s.
3. Observe (read-only): mask `0x40008` must gain bit 1 **only if** the descriptor path still
   faults; if the per-core stage now runs, read `[ctx+0x190] + 0x1c*i + 0x2cc` (should become
   a valid RAM pointer) and the `+0x2b8/+0x3cc` arrays; g+`0xb8` pending must clear and
   `out[1]` must take the published bit [V\* baseline].
4. If the mask still gains bit 1 with a valid `dr`, the fault is not the missing per-core
   stage and the read set in §6 should be taken on the parked chip before any further write.

No RAM/IO poke is justified by the evidence; in particular do not write `g+0xb4` as a
descriptor repair (it only unblocks the D2H publish) and do not touch `0x40114568` again.

---

## 8. Open items

* **O1**: whether the staged `r4..r11` are the faulted context's or a later overwrite (SPSR
  read at BAR0 `0x6f804c` decides; see §2). Until then, the frame's lr/sp/SPSR are the only
  fields used by this report.
* **O2**: the writer of the descriptor context's `+0x2cc` is not identified statically
  (§4.3); a live `dr` read or a wider writer scan (structure-copy/loops) is needed.
* **O3**: the exact CA of the per-link `obj` (§4.1) - obtainable from `[ctx+0x4b0]` at runtime
  once a readable parent context is known (§6 rows 5/8).
* **O4**: which core ran `0x88afc` (the chain has no core-index argument; `0x87fb0` selects
  per-CPU tables from `obj[0x12d]`).

---

## 9. Reproduction (host-only)

```sh
# all commands read the local blob only; no device contact
V=.provision-venv/bin/python
$V opensource/docs/incoming/wifi-abort-rca/tools/fw_abort_re.py dis --cpu 0x87d64 --len 0xc0
$V opensource/docs/incoming/wifi-abort-rca/tools/fw_abort_re.py dis --cpu 0x88afc --len 0x2d0
$V opensource/docs/incoming/wifi-abort-rca/tools/fw_abort_re.py frame
$V opensource/docs/incoming/wifi-abort-rca/tools/fw_abort_re.py callers 0x88afc 0x87d64 0xda6dc 0xe9a7c
$V opensource/docs/incoming/wifi-abort-rca/tools/fw_abort_re.py fields 0x2b8 0x2cc 0x3cc
$V opensource/docs/incoming/wifi-abort-rca/tools/abort_readplan.py            # BAR0 read list
$V opensource/docs/incoming/wifi-abort-rca/tools/abort_readplan.py --block <read value>
```

Inputs are local evidence only (`HI5622_FW` may override the blob path). Nothing in this
directory is built from or contains vendor binaries; the two tools are plain text scanners.

---

## 10. Follow-up session (fresh POR, 2026-11-23) - reads, parent-context trace, next step

Session [V\*]: fresh POR -> combined B3+injector, prepare/upload/verify/b3_p0_ringinit/h2d_arm all rc 0 ->
release `0x5a5a` -> service loop 8 s (gate `0xcece` held + `b3_drain` + `g+0xb4` kept 0).
Result: mask `0x0A`, state 1, g `0x0010C0F4`, `out[1]=0`, `g+0xb4` kept 0. Same abort signature as the
cold boot; the announce contract was served, so the remaining anomalies are not the 0xcece race.

### 10.1 What the new reads settle

1. **SPSR = `0x000000B3`** at BAR0 `0x6f804c` [V\*]. Decode: `I=1`, `T=1`, mode bits `0x13` = **SVC**.
   So the staged exception context is firmware SVC code -> the "later IRQ/PHY-applier overwrite"
   reading (R2 of section 2) is excluded. The frame is the in-band data-abort context (R1).
   (`0x0A` mask still says both id 1 and id 3 ran; an SVC-from-SVC frame would also show `0x13`,
   so open item O1 - the r4..r11 PHY-table anomaly - remains open but is not an IRQ-mode event.)
2. **g is not the obj** [V\*]: `g+0x2cc` live = `0x0008558D`, exactly the image template value at
   CPU `0x10c3c0` (file `0xcc3c0`) [V]. Predicted in section 4.2; confirmed.
3. **g is not the parent of the +0x4B0 deref either** [V/U]: `g+0x4b0` live = `0x000001CF` and the
   *unmodified image template* at CPU `0x10c5a4` (file `0xcc5a4`) is also `0x000001CF` [V].
   So `g+0x4b0` is a static data word (0x1cf = 463), not a runtime pointer; the lead's
   "invalid pointer" reading is really "g is not the object this code reads".
4. **boot object is not the parent** [V/I]: `[boot+0x4b0]` live = 0, but the live boot-object area
   (CPU `0x117c00`+) is *runtime-initialised memory*: the image file at CPU `0x117c00` (file
   `0xd7c00`) is Thumb code, while the live reads there are runtime list heads/pointers [V].
   Consequence: **for objects in the image tail (CPU >=~0x110000) the file cannot be used as a
   template**; only live reads are meaningful, and a null there simply means "not the link object".
5. **Registry record `+0x2cc` is not a descriptor-block pointer** [V\*/I]: record 0's value is the
   field's own address (`0x171b70+0x2cc = 0x171e3c`, value `0x171e3c` = self list head);
   record 2's value is self/near-self (`0x17221c` vs field `0x172220`; exact self if the field is
   `+0x2c8`); record 1 = 0, record 3 = `0x30003`. The records are heterogeneous routing objects
   with a list head at that offset - not `dr` candidates. Do not read further there for `dr`.

### 10.2 Which object supplies +0x4B0 (follow-up Q1) [V]

The two chain entry points are **tag-dispatched firmware callbacks**, not ordinary callees:

* the callback table at CPU **`0x104cb8`** has 8-byte records `{u32 tag, u32 fn}` with tags
  `0xdad..0xdc9`; entries `0xdbc -> 0x7469d` and `0xdbd -> 0x745e1` [V];
* the dispatcher at CPU **`0x7243c`** (`push {r0,r1,r2,r4..lr}; mov r4,r1; mov r5,r0; ...
  ldrh r2,[r1]; subw r3,r2,#0xdad; cmp r3,#0x1c; ... bx r3`) calls the handler as
  **`fn(ctx = r0, msg = r1)`** [V].

So in the three traced sites the parent is supplied by that callback context, not by a global:

| site | function | parent used for `*(+0x4B0)` |
| --- | --- | --- |
| `0x745e4` | `0x745e0` (tag `0xdbd`) | r0 = dispatcher ctx |
| `0x746a0` | `0x7469c` (tag `0xdbc`) | r0 = dispatcher ctx |
| `0x846c2` | `0x84664` | `r0 = 0x961ae(byte[arg0])` (object lookup via `0xfe578` pool), then `*(r0+0x4b0)` |

The per-link/channel object is created/initialised by the early-init region: `0x48840`
(`o = 0x961ae(byte[r5]); o+0x4b0 = [sb+8]`, `sb = 0x9438c(...)`) and `0x44bbc` (same family,
also `+0x4b4/+0x4b8` and `+0x190`). Entry A (`0x46970`) is just before this region, and entry A is
proven to have run, so the per-link `+0x4b0` bind is part of the observed early boot; but the object
that the chain actually reads `+0x4b0` from is the *event/link callback context*, which is created
later by the HCC link/event layer. That is the missing stage: **HCC link/event context bring-up**
(host-facing), and the H2D probe result (`out[0]` stays `0x8`, `ack=0`, `last_result=-110`) agrees:
the firmware message/event core is not consuming yet.

### 10.3 Writers of +0x4B0 in the image (follow-up Q2) [V]

Exactly five direct `str ... [#0x4b0]` sites exist:

1. `0x48840` - link/channel create: `obj+0x4b0 = [sb+8]`, `obj+0x4b8 = 0xa4fc8(...)`; `sb = 0x9438c(...)`.
   Early-init region (right after entry A `0x46970`); this is the per-link sub-context bind.
2. `0x44bbc` - same object family create/init: writes `+0x4b0/+0x4b4/+0x4b8`, then touches `+0x190`.
3. `0x4490e` - teardown/reset: zeroes `+0x4ac/+0x4b0/+0x4b4` before releasing the object.
4. `0x59f50`, `0x5cff0` - **different object family**: ops/vtable initialisers that store a literal
   Thumb code pointer into `+0x4b0` (and into `+0x80/+0x114/+0x14/...`). Do not mix these with the
   per-link pointer family.

So `[parent+0x4b0]` is written by the early per-link bind (`0x48840`/`0x44bbc`) and cleared by the
teardown (`0x4490e`); the chain's parent is the event-layer context, so a zero/invalid value there
means "the event-layer context was never bound", not "a random field is corrupted".

### 10.4 Is a targeted write justified? (follow-up Q3) - **NO**

* The two +0x4B0 words read so far belong to objects the chain never uses (g proven by exact
  template match, boot object by runtime-init argument); there is no evidence of a corrupted
  parent field yet, so a write would be treating an unproven hypothesis.
* A fabricated `[ctx+0x4b0]` is not a passive pointer: the callback handlers dereference many
  sub-fields of it (`+0xde0/+0xcde/+0xd70/+0x4b8/+0x190` in `0x7469c`/`0x745e0`/`0x84664`), so a
  plausible-looking but non-object address produces a precise external abort on a parked chip; and
  the value must be a live kernel object (with locks/lists), not an address that merely looks sane.
* Correct next step is read-only, then the link/event lane:
  * HCC instance context = `g+0x9c` = CPU **`0x10c190`** (BAR0 `0x7c4190`): proven by the image
    wrapper CPU `0x40294` (`ldr r0,=0x10c190; b 0xc18ac`) = mailbox dispatcher entry [V].
    Read instance`+0x4b0` -> CPU `0x10c640` (BAR0 **`0x7c4640`**), instance`+0x2cc` ->
    `0x10c45c` (`0x7c445c`), instance`+0x190` -> `0x10c320` (`0x7c4320`),
    instance`+0x2b8` -> `0x10c448` (`0x7c4448`).
  * per-link `+0x4b0` of the four registry-record targets: CPU `0x172020` (`0x82a020`),
    `0x1722cc` (`0x82a2cc`), `0x172404` (`0x82a404`), `0x172458` (`0x82a458`).
  * registry record `+0x2cc` re-reads with exact field addresses to confirm list heads:
    `0x171e3c` (`0x829e3c`), `0x1720e8` (`0x82a0e8`), `0x172220` (`0x82a220`),
    `0x172274` (`0x82a274`).
  * the tag-dispatcher registration table at CPU `0x106e40` (`{u32 count, u32 fn, u16 lo, u16 hi}`;
    row at `0x106e70` = `{0xF, 0x7243d, 0x0ed8, 0x0edb}`) is static and has no literal/movw/movt
    reference in the image [V] - a static registration descriptor; no live read needed.
  * `tools/abort_readplan.py` is updated with this session's list.
* If those reads stay null, the next lane is not another firmware poke: it is the **HCC
  link/event-layer bring-up** (host counterpart) - the vendor host performs the link/instance
  handshake that makes the firmware event layer build these contexts (see `wifi-host-replica` and
  `wifi-h2d-catalog`). The H2D probe failing to be consumed (`out[0]` stuck at 0x8, last_result
  -110) says the firmware is stalled before that layer, so the host-side handshake is the missing
  input, not a missing RAM word.
