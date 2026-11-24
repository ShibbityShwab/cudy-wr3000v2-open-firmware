# PD4: a genuinely-safe stub cave (static-data alignment gap) + the g0 id1 field-B context precondition

**Lane:** `wifi-irq-unmask`, shared task **task-100 (PD4)**, **host-only static RE**, 2026-11-24.
Bench evidence is cited from the Lead's PD3 / txq_sync72 fires (attended; see
`../wifi-h2d-send/REPORT-G0ID1-HANDLER.md` and `REPORT-PD3.md`); all new analysis in this
report is host-only. The vendor firmware blob and the mask-ROM dump stay **local evidence
only**; nothing vendor-derived is committed. Address conventions unchanged: image
**CPU = file + 0x40000**; host BAR0 of firmware RAM = CPU + 0x6b8000 (window 5, size 0x218000);
device CA IO window 0x40000000..0x4011FFFF only.

Evidence markers: **[V]** verified in this lane with `tools/pd4_re.py` (42/42) and the extended
`tools/fw_pd_builder.py` (81/81); **[V\*]** device-measured by earlier fires; **[I]** inference;
**[U]** not decidable without a device read.

---

## 0. TL;DR

1. **PD2's failure** (stub in entry A's config-3 block) proved config-conditional blocks are
   live on our boots. **PD3's cave** (image-header exception-record area CPU 0x4000C) booted
   and dispatched the mailbox, but the txq_sync72 fire showed the fatal residual risk: the
   image's exception-staging routine writes its 17-word frame **exactly over the parked
   PD3 poller**, so the first exception destroys the poller, follow-up undefined instructions
   overwrite the record, and the true first-fault frame is lost (mask 0x12 = UND + DABT
   cascade, 3x out[1]=4).
2. **PD4 relocates the identical guarded poll stub to CPU 0x108240 (file 0xC8240)**, inside
   the **637-byte all-zero static-data alignment gap CPU 0x108197..0x108413**. The gap:
   * is file-backed (image) static read-only data, not code, not a header word, not a config
     block;
   * lies **outside both boot memsets** (0x117C00..0x1ADDE0 and 0x1000000..0x10E0000);
   * is **outside the exception-staging area** 0x4000C..0x4004B;
   * is reached by **no branch target, no PC-relative literal load, no 32-bit word / movw /
     movt / adr address materialisation** anywhere in the image [V];
   * is bracketed by **two referenced read-only tables** (0x108054 read at 0xDE79C and
     0x108414 read at 0xDE864, both consumed with positive offsets from their base), i.e. it
     is pure inter-table alignment filler: no consumer window covers it.
   The only entry is the PD4 patch of the boot-task success epilogue 0x4916A.
3. **PD4 patch (exact):**
   * file **0x0916A**, 4 B: `00 20 21 B0` -> **`BF F0 69 B8`** (`b.w 0x108240`);
   * file **0x0C8240**, 64 B: guarded out[0] poll stub (same semantics as PD2/PD3: wait for
     the hcc instance, poll mailbox **out[0] CA 0x40039010**, call wrapper **0x40294**, and
     substitute the mask-ROM RAZ 0x2000 for the static handler-table sentinel 0x118D68,
     restoring it after the call).
   Patched image **FIRMWARE.PD4.bin** sha256
   **`cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1`**, length unchanged
   928,920 B; only the two segment windows differ from the pinned vendor blob (sha256
   `7fc87e20…311b`). `PD4.recipe.json` carries the machine-readable recipe.
4. **After one txq_sync72 fire, the TRUE first exception frame survives** at firmware
   CPU 0x4000C..0x4004F = **BAR0 0x6F800C (host phys 0x406F800C)**: read its 17 words
   (r0-r12, sp, lr, PC, SPSR). Expected PC **0x8103C/0x81040** confirms the predicted
   field-B context fault (section 8); any other PC refutes it. Expected SPSR mode 0x17
   (ABT); expected mask CA 0x40008 = **0x10 only** (the PD3 cascade's UND half disappears).
   Read-only cave check: **BAR0 0x7C0240 (host phys 0x407C0240)** must still hold the
   patched stub, proving no runtime writer touched the cave.
5. **g0 id1 field-B context precondition (new, [V]):** handler group-0 id-1 calls
   CA **0xFE578(0, hdr[3])**. That function is a singleton-gated lookup into the
   **boot-populated 19-entry user context table** of the global user-manager object
   **G = CPU 0x170E28** (BSS): `ctx = *(u32)(G + (0x18C+fieldB)*4) + u16(G+0x6DC)`, and
   `(0x18C+id)*4 = 0x630 + id*4`, i.e. the table is **G+0x630+id*4**. The BSS user-manager
   init block **CPU 0x49204..0x49264** memsets G (0x700 B) and then fills **all 19** context
   pointers with **0x119758 + k*0xB40** (pool end 0x126D18 = 0x119758 + 19*0xB40), zeroing
   every context body. The per-context field **+0x190 is written only by the context-create
   helper CA 0xFE6BC** (`str.w r0,[ctx,#0x190]` at 0xFE7E4) - i.e. by a user/vap-add style
   message, **not by the boot**. Therefore on our host-only boot the lookup returns a
   **non-NULL but zeroed** context, and the key 0xd8 worker at CA 0x81038 does
   `ldr r3,[ctx+0x190]` (=0) then `strb r1,[r3,#0x4c]` => **precise data abort at
   0x81040, faulting address 0x4C**. Our canned `hdr[3] = 0x04` is vendor-wire-exact
   (72-byte capture) but its **precondition - an existing/created user-vap context 4 - is
   absent**. A payload tweak cannot fix that: the first real message must be the
   context-create/init message, and the TXQ sync afterwards.
6. **Single next bench action:** fire **FIRMWARE.PD4.bin** (sha256 `cfe24bfb…037b1`) once on
   a fresh golden boot with the standard GP8/txq_sync72 procedure, then take the read-only
   read of CA 0x4000C.. (first frame) + BAR0 0x7C0240 (cave integrity). One boot, one frame,
   no retries; recovery armed first.

---

## 1. Why the PD3 cave cannot be used for the first-fault question

PD3's own risk note said it: "an external exception while the stub is parked would overwrite
the stub code (staged frame) and the CPU would resume in corrupted code". The txq_sync72 fire
turned that risk into the observed signature:

* the exception-staging routine (CPU 0xC6E14) writes its entering-vector frame
  (`r0-r12, sp, lr, PC, SPSR`, 17 words) at **CPU 0x4000C..0x4004B** - the literal
  `0x40008` sits at CPU 0xC6FA4 [V];
* the PD3 poller occupied exactly that range, so exception #1 zeroed/replaced its code; the
  ABT-mode spin/recovery path then executed staged context words as Thumb -> UND; every
  later exception overwrote the frame again. The record therefore only kept the **last**
  frame (pc = 0x171E90 = WRAM data), and the mask accumulated **0x12 = (1<<1)|(1<<4)**
  = UND + DABT with 3x out[1] bit2 announces (3 staging entries) [V\*].
* Even a correct first DABT cannot be diagnosed from that: the frame that names the fault
  (the worker/context store) is overwritten.

So PD4's requirement is purely: **a cave the exception staging routine does not write**, and
ideally one nothing else writes either. The header record area is now excluded by its own
writer; the remaining candidate classes were re-scanned over the whole blob.

---

## 2. Cave search over the whole image blob

Method (all in `tools/pd4_re.py --cave`): the pinned 928,920-byte blob is scanned for all
all-zero runs; every candidate is then subjected to a conservative whole-image census
(every even file offset decoded as **Thumb and ARM**, deliberately over-approximating, so an
empty result is a strong negative), plus the known runtime writers, plus the boot memsets.

### 2.1 Candidate classes and verdicts

| class | example | verdict |
| --- | --- | --- |
| image header exception-record area (PD3) | CPU 0x4000C..0x4004B | **rejected**: exception-staging writer (this fire) |
| entry A config-3 block (PD2) | CPU 0x47432 | **rejected**: executed on our boots (PD2-A fire) |
| image-header live words | 0x0..0x64 | **rejected**: direct boot/handler contract |
| appended bytes beyond image end | > 0x122C98 | **rejected**: entry A memsets CPU 0x117C00..0x1ADDE0 |
| the file-tail "code-looking" region | 0x117C00..0x122C98 | **rejected**: inside the same memset; it is uninitialised-BSS garbage preserved in the vendor image (the boot zeroes it) |
| zero runs inside live PHY/config tables | e.g. 0x106530 (74 B) | **rejected**: the whole 0x105000..0x106FFF table region is referenced by many code literal pools; the run is interior table data |
| zero run overlapping hcc globals | 0x10C0F7 (185 B) | **rejected**: contains the hcc instance fields 0x10C190..0x10C1AF written by `pcie_msg_init` |
| ETE/ring descriptor table gaps | 0x10CA07 / 0x10CAD0 | **rejected**: table of device-CA + code-pointer descriptors (0x4003A5xx, 0x0C17C5…) |
| D2H record interiors | 0x10F2D5.. (767 B x5) | **rejected**: runtime-maintained per-channel records (cfg/prod/progress are written by the D2H engine) |
| init-sequence table gaps | 0x10B353, 0x11610F, 0x11615F | **rejected**: PHY/register init sequences read by init code |
| dead code in the code segment | - | **rejected**: there is no all-zero gap >= 24 B anywhere in CPU 0x40000..0x102FFF; "unreachable code" cannot be proven to the same standard because dispatch is indirect through runtime-populated tables |
| **static-data alignment gap** | **CPU 0x108197..0x108413 (637 B)** | **CHOSEN**: zero references, outside every writer, between two referenced read-only tables |

### 2.2 The chosen gap, CPU 0x108197..0x108413 (file 0xC8197..0xC8413)

* all 637 bytes are zero in the vendor image (`sha16 695f7ca8fe3dd652`) [V];
* the byte after the gap (CPU 0x108414) is the first byte of a **referenced** table: the
  literal pool **CPU 0xDE890** = {0x107F3C, 0x1087D4, **0x108054**, **0x108414**} [V];
* consumer of 0x108054: `ldr r3,[pc,#0xF8]` at CPU 0xDE79C -> `add.w r7,r3,r8; add r7,sb`,
  then reads `r7[0], r7[2], r7[4]` (positive offsets, 0xDE7BC..0xDE7C6) [V];
* consumer of 0x108414: `ldr r1,[pc,#0x34]` at CPU 0xDE864 -> `add r1,r8; add r1,sb;
  adds r0,r1,r2`, then `ldrh r3,[r0,#2]`, `ldrh ip,[r0,#4]`, `ldrh r2,[r1,r2]`
  (positive offsets, 0xDE866..0xDE87A) [V];
* i.e. the gap sits **between** two static tables and is never inside either consumer's
  positive-offset window; there is no address materialisation of any byte in
  [0x108197,0x108414) in the whole image (table in section 3) [V].
* **Runtime writers excluded:** the only bulk writers found in the image are the two boot
  memsets (CPU 0x117C00..0x1ADDE0, 0x961E0 B, literals 0x46DEC/0x46DF0; and
  CPU 0x1000000..0x10E0000, 0xE0000 B, immediates 0x46D94..0x46DA0). The cave is outside
  both, and it is far below the BSS start that the first memset covers. The exception
  staging routine writes 0x4000C..0x4004B only. With no reference into the gap, no
  computed pointer/offset writer remains [V/I-high].

### 2.3 Stub placement and size

The chosen stub home is **CPU 0x108240** (word-aligned, 169 B after the preceding table data
and 404 B before the next table base, i.e. with margins on both sides):

* LOOP window file 0xC8240..0xC827F = 64 B, all zero in the vendor image [V];
* the stub's own literal pool occupies the last 16 B (base+0x30): `0x10C19C, 0x40039010,
  0x118D68, 0x2000` [V];
* `b.w` from 0x4916A to 0x108240 is +0xBF0D2, comfortably inside the Thumb-2 B.W range [V].

---

## 3. PD4 exact patch specification

### 3.1 Segments (file = CPU - 0x40000)

| id | CPU | file | len | original window bytes | patched bytes | decode |
| --- | ---: | ---: | ---: | --- | --- | --- |
| **PD4-ENTRY** | 0x4916A | **0x0916A** | 4 | `00 20 21 B0` | **`BF F0 69 B8`** | `b.w 0x108240` |
| **PD4-LOOP** | 0x108240 | **0x0C8240** | 64 | 64 zero bytes (static-data gap) | see 3.2 | guarded out[0] poll stub |

Source-window sha16: ENTRY **`c5975cf4bd702f88`** (same window as PD3's epilogue hunk);
LOOP **`f5a5fd42d16a2030`** (= sha256 of 64 zero bytes). Patched image **FIRMWARE.PD4.bin**
sha256 **`cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1`**, length
unchanged 928,920 B; only bytes inside the two segment windows differ. `PD4.recipe.json`
in this directory carries the machine-readable recipe (schema
`hi5622-fw-patch-recipe/1`, `based_on: null`).

PD4-LOOP patched bytes (hex):

    0b4f81b03b68002bfcd00a4a1368002bf8d07b69084ca34202d038f71bf8f1e7064c7c6138f716f87b61ebe700bf00bf9cc1100010900340688d110000200000

### 3.2 Stub decode (CPU 0x108240)

    0x108240  ldr  r7,=0x10C19C      ; hcc+0xC (dispatcher's first deref; init guard)
    0x108242  sub  sp,#4             ; once: each bl pushes LR -> 8-byte aligned public call
    0x108244  loop: ldr r3,[r7]      ; instance initialised?
    0x108246  cmp  r3,#0
    0x108248  beq  loop              ; no -> wait (safe, never calls)
    0x10824A  ldr  r2,=0x40039010    ; mailbox out[0] (H2D pending read/clear)
    0x10824C  ldr  r3,[r2]
    0x10824E  cmp  r3,#0
    0x108250  beq  loop
    0x108252  ldr  r3,[r7,#0x14]     ; [hcc+0x20] handler table
    0x108254  ldr  r4,=0x118D68      ; static/uninitialised sentinel (image code address)
    0x108256  cmp  r3,r4
    0x108258  beq  zero
    0x10825A  bl   0x40294           ; live table: real dispatch
    0x10825E  b    loop
    0x108260  zero: ldr r4,=0x2000
    0x108262  str  r4,[r7,#0x14]     ; consume-only table (mask-ROM RAZ reads 0)
    0x108264  bl   0x40294           ; real dispatcher: ack out[5]=1, clear out[0], out[2]=8
    0x108268  str  r3,[r7,#0x14]     ; restore (dispatcher preserves r3/r7)
    0x10826A  b    loop
    0x10826C  nop ; nop
    0x108270  .word 0x0010C19C, 0x40039010, 0x00118D68, 0x00002000

The boot-task epilogue at 0x4916A (`movs r0,#0; add sp,#0x84; pop {pc}`) is replaced by the
branch, exactly as in PD3; everything before it (the full boot/init chain, including
`pcie_msg_init`) is unchanged, so the hcc instance and the group-0/id-1 handler are
registered before the stub runs [V/I-high].

### 3.3 Why this cave is safe where PD3's is not

* **Control flow:** whole-image census into [0x108197,0x108414): branch targets **0**,
  PC-relative literal loads **0**, 32-bit word values **0**, movw/movt immediates **0**,
  adr/addw/subw targets **0**. The stub is entered only by the PD4 ENTRY patch [V].
* **Writer:** outside both boot memsets and outside the exception-staging area; no reference
  produces an address in the gap, so no computed store can reach it [V/I-high].
* **Reader:** being a read-only static-data filler, even a hypothetical reader cannot exist
  without an address; no consumer's positive-offset window from the two bracketing table
  bases covers it [V].
* **The exception staging routine does not touch it**, so the first exception's frame at
  CPU 0x4000C survives; this is exactly the PD4 experiment.

Residual risk: a device-DMA writer whose descriptor targets this RAM region is not provable
from the image alone; the bench read-back of the cave after boot (BAR0 0x7C0240, expected to
hold the patched stub) is the direct check, and the boot itself would hang if the stub were
wiped before the epilogue. Bench-attended experiment only.

---

## 4. Expected bench observables (PD4)

Standard GP8 golden-boot procedure with `fw_path=hi_wifi/FIRMWARE.PD4.bin` (sha256
`cfe24bfb…037b1`), then ONE `h2d_txq_sync72` fire (task-99 ladder B), then read-only capture.

| # | observable | pass / meaning |
| --- | --- | --- |
| 1 | boot page CA 0x1BF000 | `0x86807FEF / 7 / 2 / 0xABCD`, state CA 0x118B60 = 1, mask CA 0x40008 = 0 before the fire (same golden gate as PD3) |
| 2 | **cave integrity** BAR0 **0x7C0240** (CPU 0x108240) | reads the patched stub `0b4f81b03b68002b…`; a mismatch/zers means a runtime writer - stop and report |
| 3 | S9 store + announce | same completion contract as PD3: out[0] 0x8 -> 0 and `h2d_complete data=0x8` while the firmware path runs; then the worker fault happens inside the firmware |
| 4 | mask CA 0x40008 after the fire | **expected 0x10 only** (DABT, no UND): the PD3 cascade's undefined half came from the clobbered PD3 cave. A mask containing 0x02 again = the cascade is not the PD3 cave only (report it) |
| 5 | **exception record CPU 0x4000C..0x4004F** = BAR0 **0x6F800C** (host phys 0x406F800C) | **the primary PD4 deliverable**: 17 words = r0-r12, sp, lr, PC, SPSR of the TRUE first frame. **Expected PC 0x8103C/0x81040** (field-B context worker) with **r3 = 0**; SPSR mode 0x17 (ABT). Any other PC refutes the context model and is itself the new finding |
| 6 | CECE / out[1] staging count | PD3 saw 3 staging entries; PD4 should see **1** (the poller is not destroyed, but the faulting worker never returns; the ABT spin path may still re-enter - record the count) |
| 7 | page/state after the fire | golden page intact, state 1 (PD3's fire kept them; PD4 must not regress) |

Read-only rows 2/5 are the experiment; do **not** write CA 0x4000C.. or the cave.

Negative cases: row 1 fails => the boot did not reach the epilogue (capture page/mask);
row 2 fails => the cave is live after all (the static proof was wrong - report and fall back
to a different gap); row 5 PC names something outside 0x8103C/0x81040 => the predicted fault
is refuted and the frame itself is the deliverable.

---

## 5. PD4 build / reproduce (host-only)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-irq-unmask/tools
    $PY $T/pd4_re.py --selftest          # 42/42 PASS
    $PY $T/pd4_re.py --cave              # cave proof + census + patch segments
    $PY $T/pd4_re.py --ctx               # field-B context precondition analysis
    $PY $T/fw_pd_builder.py --selftest   # 81/81 PASS (PD2/PD3/PD4/PD3B)
    $PY $T/fw_pd_builder.py --build PD4 --out /tmp/hi5622-pd4
    # -> /tmp/hi5622-pd4/FIRMWARE.PD4.bin sha256
    #    cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1

Both tools read the pinned local blob in place, refuse a wrong blob and never write vendor
bytes into the repository.

---

## 6. The g0 id1 field-B context precondition (task part 2)

### 6.1 What CA 0xFE578 does [V]

`ctx = 0xFE578(0, hdr[3])`, called by the handler at CPU 0x43D76 as
`movs r0,#0; mov r1,r4=fieldB; bl 0xFE578` (handler prologue at 0x43D58; `ldrb r1,[r0,#3]`
at 0x43D72).

    0xFE578  push {r4,r5,r6,lr}; mov r5,r0; mov r4,r1
    0xFE57E  bl 0xFE558              ; r6 = getter(0)  -> literal at 0xFE574 = 0x001ADB80
    0xFE584  mov r0,r5
    0xFE586  bl 0x943AC              ; r0 = getter(0)  -> literal at 0x943C8 = 0x00170E28 = G
    0xFE58A  cbz r0, log_return_0 ; cbnz r6, ok
    0xFE59C  cmp r4,#0x12            ; fieldB bounds 0..18 -> 0xFE5A8
    0xFE5A8  add.w r4,r4,#0x18C      ; index = 0x18C + fieldB
    0xFE5AC  ldrh.w r3,[r0,#0x6DC]   ; common offset u16
    0xFE5B0  ldr.w r0,[r0,r4,lsl #2] ; per-fieldB pointer
    0xFE5B4  add r0,r3               ; ctx = pointer + offset
    0xFE5B6  b 0xFE59A               ; return ctx

Key identity: **(0x18C + id)*4 = 0x630 + id*4** - the lookup table is the
**G+0x630+id*4** table. The two `getter` functions are "return the singleton if the id
argument is 0, else log an error and return 0"; the handler passes 0, so both succeed.

The handler tolerates **ctx == 0**: `cbnz r0,#0x43D8E` at 0x43D7A - a clean zero falls to a
log + return 0x64 path and never faults.

### 6.2 Who populates the table [V]

**CPU 0x49204..0x49264** (literal pool CPU 0x49300: G = 0x170E28 at 0x49310, pool base
0x119758 at 0x49330, pool end 0x126D18 at 0x4931C):

    0x49204  mov.w r3,#0x700
    0x49208  ldr r4,=0x170E28      ; G
    0x4920C  mov r1,r3 ; mov r0,r4
    0x49210  bl  memset-ish        ; *G = 0 for 0x700 bytes (covers +0x18C..+0x6DF)
    0x49214  G+0x624 = G+0x628 ; G+0x620 = 0x100 ; list-init(G+0x620)
    0x49236  strb r6,[G+0x62C] ; strh r6,[G+0x6DC]   ; u16 G+0x6DC = r6 (0 on the boot path [I])
    0x49242  loop: r3=0xB40 ; r0 = r5 + sb(=0x119758)
    0x4924C  str r0,[r8,#4]!      ; G+0x630 + 4*k = context_k
    0x49252  memset(context_k, 0, 0xB40)
    0x4925A  r5 += 0xB40 ; cmp r5,#0xD5C0 ; strb r6,[r7,#1]! ; bne loop

So the init **memsets G itself** (which clears the lookup table) and then fills all
**19 = 0xD5C0/0xB40** context pointers with *valid BSS pointers* 0x119758 + k*0xB40
(pool end 0x126D18 = base + 19*0xB40), and **zeroes each 0xB40-byte context body**.
After this block: **table[4] is non-NULL** (0x119758 + 4*0xB40 = 0x11C458) but the body,
including **+0x190, is zero**.

### 6.3 When does ctx+0x190 become valid? [V]

The only writer of `[ctx,#0x190]` found in the image is the context-create helper
**CA 0xFE6BC**: `str.w r0,[r4,#0x190]` at **0xFE7E4** (field writes at +0x18C..+0x1D4 of a
freshly added context; its caller side also clears the field at 0x44ADE). It is reached from
the user/vap configuration message handlers - i.e. **only after a context-create message**,
never by the boot init. Therefore the per-context pointer that the key-0xd8 worker dereferences
(`ctx+0x190`) is **NULL on a host-only boot that has not created that context**.

### 6.4 Fault model (why the bench saw a data abort) [V/I-high]

    handler group-0 id-1 (0x43D58) with hdr[3] = 4
      -> 0xFE578(0,4): ctx = table[4] + u16(G+0x6DC)
         = 0x11C458 + 0   (NON-NULL -> clean-NULL arm NOT taken)
      -> key u16 hdr+0x14 = 0x00d8 -> table A entry 41 -> worker CA 0x81039
      -> 0x8103C  ldr.w r3,[r0,#0x190]      ; r3 = 0 (body zeroed at boot)
      -> 0x81040  strb.w r1,[r3,#0x4c]      ; store to address 0x4C
      -> PRECISE DATA ABORT (vector 4, mask bit 0x10) at PC 0x81040

This explains both the data-abort half of the PD3-fire mask (0x10) and why the failure is
**payload-related, not transport-related**: the frame reached the handler and the handler
dereferenced the (boot-created, not yet configured) context. It also predicts the PD4 first
frame: **pc 0x8103C/0x81040, r3 = 0, SPSR mode 0x17**.

### 6.5 Is our canned hdr[3] = 0x04 valid? [V/I]

* **As a wire value: yes** - the vendor 72-byte capture frame carries `hdr[3] = 0x04`
  (task-99 capture), and our sender reproduces the frame byte-exactly.
* **As a precondition: no, on our boot** - field B selects an *existing, created* user/vap
  context. The vendor host creates its contexts with the earlier H2D configuration messages
  (its bring-up sequence) before/around the TXQ sync; our host sends the TXQ sync as the
  first real message. A payload context-id change alone cannot help: **every** index is
  boot-populated and every body is zero, so any field B hits the same NULL +0x190 unless a
  context-create message has run first.
* **Vendor-boot nuance [I]:** on a vendor boot the vendor userspace may have created context
  4 before the sync; the static evidence cannot prove which message created it (the create
  helper is reached through the group-0 user/vap-add handlers). Bench question resolved by
  the PD4 first frame (r3=0 confirms the model; a non-zero r3 would point at a different
  context state).

### 6.6 What the next real message should be [I, ranked]

1. **Read the PD4 first frame** (section 4 row 5) - confirms/refutes the NULL-+0x190 model
   and gives the true first-fault PC. This is the primary experiment.
2. If confirmed, the first real H2D message must be a **context-create/init** message for
   field B = 4 (a user/vap add or the vendor's earliest config event that reaches
   CA 0xFE6BC), with the TXQ config sync only afterwards. Payload-only changes to the sync
   are not sufficient.
3. **Control fire:** the payload-free **group-0 id-0** frame (handler CA 0x81695, log stub,
   reads no payload/context) exercises the identical ETE/mailbox/dispatch path with no
   context dereference. If it is clean, the remaining defect is exactly the missing
   per-context state, and the transport/dispatcher path is fully exonerated.

---

## 7. Ranked PD4 delivery

| rank | item | status |
| --- | --- | --- |
| 1 | PD4 stub in the 0x108197 static-data gap (image `cfe24bfb…037b1`) + one txq_sync72 fire + first-frame read | **recommended next bench action** |
| 2 | same fire on a TFTP-booted copy so the read-back uses only read windows | not needed: BAR0 read of 0x6F800C / 0x7C0240 is inside the mapped region-5 window |
| 3 | if the cave is proven live on the bench (row 2 mismatch), relocate to the next zero gap with the same census (tool ranks them automatically) | fallback |

---

## 8. Evidence table

| # | claim | method | grade |
| --- | --- | --- | --- |
| E1 | the image's exception staging routine writes its 17-word frame at CPU 0x4000C..0x4004B (the PD3 cave), so PD3's poller is destroyed by the first exception | staging routine disasm + literal 0xC6FA4 = 0x40008 + PD3/txq72 mask/out[1] evidence | [V]/[V\*] |
| E2 | the 637-byte gap CPU 0x108197..0x108413 is all zero in the vendor image | blob read | [V] |
| E3 | no branch/literal/word/movw/adr reference into the gap anywhere in the image | whole-image Thumb+ARM conservative census | [V] |
| E4 | the gap is bracketed by two referenced read-only tables (0x108054 / 0x108414) read with positive offsets from their base | literal pool 0xDE890 + consumers 0xDE79C / 0xDE864 | [V] |
| E5 | the gap is outside both boot memsets and the exception-staging area | literal/immediate bounds + staging literal | [V] |
| E6 | PD4 patch = 4-byte epilogue `b.w 0x108240` + 64-byte guarded poll stub; image sha256 `cfe24bfb…037b1`, only the segment windows differ | builder selftest (81/81) | [V] |
| E7 | after one txq_sync72 fire the true first exception frame survives at BAR0 0x6F800C; expected pc 0x8103C/0x81040, r3=0, SPSR 0x17 | PD4 design + PD3 session shape | [I high] |
| E8 | CA 0xFE578 = singleton-gated lookup into G+0x630+fieldB*4 + u16(G+0x6DC), G = 0x170E28 | disasm + literal reads | [V] |
| E9 | the boot init CPU 0x49204..0x49264 memsets G (0x700 B) and fills all 19 context pointers (0x119758 + k*0xB40, end 0x126D18), zeroing the bodies | disasm + literal pool 0x49300 | [V] |
| E10 | ctx+0x190 is written only by the context-create helper CA 0xFE6BC (0xFE7E4); +0x190 is NULL on a boot with no created context -> worker's store to 0x4C data-aborts at 0x81040 | disasm + BSS init proof | [V]/[I high] |
| E11 | our hdr[3]=0x04 is vendor-wire-exact but its field-B precondition (created context 4) is missing on our boot; a context-create message must precede the TXQ sync | E8-E10 + task-99 capture | [I high] |

---

## 9. Open items ([U])

1. **O1 - the true first frame** (the whole point of PD4): bench read of CPU 0x4000C after the
   fire settles pc/r3/SPSR and therefore the context model.
2. **O2 - mask after PD4**: does the UND half disappear (expected 0x10 only)? The count of
   staging entries (out[1] bit2) also quantifies the cascade.
3. **O3 - which exact H2D message creates context 4** on the vendor boot: not decidable from
   the image alone; the create helper CA 0xFE6BC call graph through the group-0 user/vap
   handlers is the next static trace if PD4 confirms the model.
4. **O4 - device-DMA writers into FW RAM** below 0x117C00 are not provable from the image;
   bench row 2 (cave read-back) is the direct check.

---

*Lane wifi-irq-unmask, 2026-11-24, shared task task-100 (PD4). Host-only static reversal and
patch design; the PD2-A/PD3/txq_sync72 bench evidence is cited, the vendor firmware blob and
mask-ROM dump remain local evidence only; this directory keeps addresses, constants,
instruction encodings and hashes, never vendor bytes.*
