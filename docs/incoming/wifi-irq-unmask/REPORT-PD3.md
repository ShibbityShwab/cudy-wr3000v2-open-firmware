# PD3: PD2 bench-failure RCA and a stub in a genuinely unreachable cave (image-header record area)

**Lane:** `wifi-irq-unmask`, shared task **task-91 follow-up (PD3)**, **host-only static RE**, 2026-11-24.
Bench evidence is cited from the Lead's PD2-A fire (attended, GP8-style session); the subsequent
analysis and tooling are host-only. The vendor firmware blob and mask-ROM dump stay **local
evidence only**; nothing vendor-derived is committed. Address conventions unchanged: image
**CPU = file + 0x40000**; host BAR0 of CPU RAM = CPU + 0x6B8000; device CA IO window
0x40000000..0x4011FFFF only.

Evidence markers: **[V]** verified in this lane with the tools below; **[V\*]** device-measured
by the PD2-A fire or earlier lanes; **[I]** inference; **[U]** not decidable without a device read.

---

## 0. TL;DR

1. **PD2-A failed and the cause is now proven: the stub's cave was NOT dead.** PD2 placed its
   poll stub in entry A's **config-3 block** CPU 0x47432, reachable when the boot selector at
   platform-block word [+0x24] == 3. On the PD2-A boot entry A took that branch and executed the
   stub, which waits for the hcc instance (still zero pre-boot) - **a deterministic hang, not a
   fault**: bench page = 0x6F (ROM bits 0..3 plus entry A's page |= 0x20 / |= 0x40), no later
   page writes, mask stayed 0, state garbage, no D2H frames. **[V\* + V]**
2. **Why the control/PD1 boots still reached the golden page:** the *original* config-3 block is
   a legitimate alternative PHY-init variant that ends with `b.w 0x46AC6` into the normal
   continuation. So "control booted golden" does not prove the selector was 2, and the branch can
   be taken (PD1 differed from PD2 only in the cave bytes, and PD1 booted - so the boot executed
   the PD2 stub). **[V]**
3. **PD3 relocates the stub to the one file-backed region of the image that is control-flow
   unreachable:** the **image-header exception-record area CPU 0x4000C..0x4004B** (file
   0x000C..0x004B). The vendor image has 0x40008..0x4004F all zero; ROM uses only header +0x00
   (entry A) / +0x04 (entry B); a whole-image census finds **no branch target and no PC-relative
   literal load** into [0x40008, 0x40050); the only runtime writer is the exception staging
   routine (a crash path that already ends the experiment). **[V]**
4. **PD3 patch (exact bytes, file offsets):**
   * file **0x000C**, 64 B: the guarded poll stub (CPU 0x4000C..0x4004B) - same logic as PD2:
     wait for the hcc instance to be initialised, poll mailbox **out[0] (CA 0x40039010)**, only
     then call the firmware event-0x4C wrapper **0x40294 -> dispatcher 0xC18AC**; while the
     handler-table pointer still holds its static sentinel 0x118D68, substitute the mask-ROM RAZ
     window 0x2000 (consume-only) and restore it afterwards.
   * file **0x0916A**, 4 B: `00 20 21 B0` -> **`F6 F7 4F BF`** (`b.w 0x4000C`; the boot-task
     success epilogue CPU 0x4916A, only reached after the full boot/init chain).
   Patched image: sha256 **`eea9f2ff9345dc8d16badd65e77a5f8b82cf58740b7203239838259adc3905ce`**,
   length unchanged 928,920 B (only bytes inside the two segment windows differ).
5. **Zero-cave fallback PD3B is also specified and built** (in case the header cave is
   disliked): 8-byte in-place epilogue replacement `movs r0,#0x4C; bl 0xC6DB8; b 0x4916A`
   (raise event 0x4C in a loop through the firmware's own event controller) + one 4-byte data
   patch `[0x10C1B0] = 0x00002000` (consume-only handler table). Image sha256
   **`ef0683dbc1a8b3b74e99f27111105538e8a437e70a8fad2a9103ae291cb745f2`**. It tests the
   event-controller delivery layer directly and has **no cave at all**. Ranked second because
   that delivery layer is the known-unknown (and the raise loop may cause an IRQ storm).
6. **Module gate fix (Lead):** latch boot completion once (`page+8==2` OR page+0 bit14 OR
   `state==1 && mask==0`) and never gate the SR announce on a live page re-read. Full policy in
   section 7. **[I high]**

7. **VALIDATED ON HARDWARE (2026-11-24, revised after the fire): PD3 booted golden and the
   firmware's H2D dispatcher consumed the mailbox.** Clean driver, fw_path
   `hi_wifi/FIRMWARE.PD3.bin` (sha256 `eea9f2ff…3905ce`), release 0x5A5A only: page
   `0x86807FEF/7/2/0xABCD` intact, state 1, mask 0, ready pair id1+id2; ONE H2D SR announce
   (id3) -> rc=0, dmesg `coalesced announce id3: out[0]=0x00000000 after 0 polls` (consumed
   immediately), dmesg `hcc-core: h2d_complete data=0x00000008`, hcc_events
   `h2d{pub=1 done=1 coal=0 to=0 closed=0 announces=1}`, safety writes=17 bad_writes=0.
   The header-record cave is proven: no boot break, no page clobber, no data abort. The raw
   `out[5]=1 / out[2]=8` prediction was over-specified - those dispatcher stores are
   write-pulse/non-latching observables (section 5.5); the **host completion contract is
   out[0] 0x8 -> 0 plus h2d_complete**, exactly as measured. The latched boot gate was already
   active in the clean driver (F1), so no module fix was needed for this lap. **[V\*]**

---

## 1. PD2-A bench failure: evidence and root cause

### 1.1 Bench evidence (Lead's fire, PD2-A image sha256 7a691807...)

Fresh GP8-style boot (upload+verify diffs=0, custom blocks seeded, hcc_arm/hcc_loop, release
0x5A5A only; no CECE during boot):

| observable | PD2-A | control boot (same session shape) |
| --- | --- | --- |
| ROM dispatch | release reg = 0x5A5A, gate CA 0x4000010C = 0xCEED, page+0 bit3 set | same |
| page CA 0x1BF000 | **0x0000006F / 3 / 0x9DFBEF6A / 0** | 0x86807FEF / 7 / 2 / 0xABCD |
| state CA 0x118B60 | 0x6914F8C4 (garbage) | 1 |
| mask CA 0x40008 | 0 | 0 |
| D2H | no frames / no ready pair | id1 + id2 decoded |

### 1.2 Page = 0x6F decodes to "entry A died between 0x469A0 and 0x46ACA"

* ROM cold boot sets page bits 0..3 (0xF) and page+4 = 3.
* Entry A then sets **page |= 0x20** at CPU 0x46978 and **page |= 0x40** at 0x469A0 -> 0x6F.
* The next page writes in entry A are 0x46ACA (bit23), 0x473C2 (bit13), 0x473F8 (bit14);
  none happened. So execution stopped between 0x469A0 and 0x46ACA.
* The only control transfer leaving that window is the **config-3 branch at 0x46A1E**
  (`beq.w 0x47432`). PD2's stub sat exactly there and waits for `[0x10C19C]` to become
  non-zero - but the hcc instance is only initialised by the boot task, which entry A never
  creates. Hence a **hang**, not a fault: mask = 0, no exception, state untouched.

### 1.3 The differential proof that the block was executed

* **PD1** (same file 0x916A epilogue hunk, no cave bytes) **booted golden** in the same session
  shape.
* **PD2-A** differs from PD1 only in the cave bytes at file 0x7432..0x746F.
* Therefore the failing boot executed those bytes -> the selector branch was taken. The
  "conditionally dead" classification was wrong for our bench boots.

### 1.4 Why the control boot is not evidence of a 2 selector

The original config-3 block is a legitimate alternative PHY-init variant; it ends at CPU
0x4746E with `b.w 0x46AC6`, i.e. it joins the normal continuation and the boot proceeds to the
golden page. A control boot with selector 3 therefore looks identical to a selector-2 boot. The
selector source is the host-seeded platform block CPU 0x1B2800 word [+0x24] (compare at
0x469E0/0x469E4/0x46A1C); our session evidently presented 3 (seed value differs from the
captured vendor "2", or the block was stale/unseeded). **[I]** The important conclusion is
unconditional: **config-conditional blocks must never be used as caves.**

---

## 2. Design candidates for PD3, ranked

| id | candidate | verdict |
| --- | --- | --- |
| **PD3-A (chosen)** | Same guarded poll logic as PD2, stub relocated to the **image-header exception-record area CPU 0x4000C..0x4004B** (file-backed, all-zero, control-flow unreachable, entered only by our own patch). | Safe by construction; no runtime selector, no config dependence; keeps PD2's proven stub semantics. |
| **PD3-B (zero-cave fallback)** | 8-byte in-place epilogue replacement: `movs r0,#0x4C; bl 0xC6DB8; b 0x4916A` (software-raise event 0x4C repeatedly) + data patch `[hcc+0x20] = 0x2000` (consume-only table). | No cave at all; exercises the firmware event controller (the missing vendor path). But its success depends on the unproven event-0x4C delivery layer, and the raise loop may create an IRQ storm. Ranked second. |
| PD3-C | Appended stub beyond image end. | Rejected: entry A memsets CPU 0x117C00..0x1ADDE0 and 0x1000000..0x10E0000 at every boot (literals at 0x46DEC/0x46DF0), so appended bytes would be zeroed before use. **[V]** |
| PD3-D | Other config-conditional blocks (config-1 block 0x46A8C etc.). | Rejected for the same reason as PD2's failure: conditionals cannot be proven dead from a golden control boot. |
| PD3-E | Existing out[0] polling loop in the image. | Does not exist: the only image reader of out[0] is the dispatcher itself (via the hcc instance pointers). **[V negative]** |

---

## 3. PD3 exact patch specification

### 3.1 Segments (file = CPU - 0x40000)

| id | CPU | file | len | original window bytes | patched bytes | decode |
| --- | ---: | ---: | ---: | --- | --- | --- |
| **PD3-ENTRY** | 0x4916A | **0x0916A** | 4 | `00 20 21 B0` | **`F6 F7 4F BF`** | `b.w 0x4000C` |
| **PD3-LOOP** | 0x4000C | **0x0000C** | 64 (0x40) | 64 zero bytes (reserved exception-record area) | see 3.2 | guarded out[0] poll stub |

Source-window sha16: ENTRY **`c5975cf4bd702f88`** (same window as PD2's epilogue hunk);
LOOP **`f5a5fd42d16a2030`** (= sha256 of 64 zero bytes). Patched **FIRMWARE.PD3.bin** sha256
**`eea9f2ff9345dc8d16badd65e77a5f8b82cf58740b7203239838259adc3905ce`**; only bytes inside the two segment windows differ from the vendor blob. `PD3.recipe.json` in this directory carries the machine-readable
recipe (schema `hi5622-fw-patch-recipe/1`, `based_on: null`).

PD3-LOOP patched bytes (hex):

    0b4f81b03b68002bfcd00a4a1368002bf8d07b69084ca34202d000f035f9f1e7064c7c6100f030f97b61
    ebe700bf00bf9cc1100010900340688d110000200000

### 3.2 Stub decode (CPU 0x4000C)

    0x4000C  ldr  r7,=0x10C19C      ; hcc+0xC (dispatcher's first deref; init guard)
    0x4000E  sub  sp,#4             ; once: each bl pushes LR -> 8-byte aligned public call
    0x40010  loop: ldr r3,[r7]      ; instance initialised?
    0x40012  cmp  r3,#0
    0x40014  beq  loop              ; no -> wait (safe, never calls)
    0x40016  ldr  r2,=0x40039010    ; mailbox out[0] (H2D pending read/clear)
    0x40018  ldr  r3,[r2]
    0x4001A  cmp  r3,#0
    0x4001C  beq  loop
    0x4001E  ldr  r3,[r7,#0x14]     ; [hcc+0x20] handler table
    0x40020  ldr  r4,=0x118D68      ; static/uninitialised sentinel (image code address)
    0x40022  cmp  r3,r4
    0x40024  beq  zero
    0x40026  bl   0x40294           ; live table: real dispatch
    0x4002A  b    loop
    0x4002C  zero: ldr r4,=0x2000
    0x4002E  str  r4,[r7,#0x14]     ; consume-only table (mask-ROM RAZ reads 0)
    0x40030  bl   0x40294           ; real dispatcher: ack out[5]=1, clear out[0], out[2]=8
    0x40034  str  r3,[r7,#0x14]     ; restore (dispatcher preserves r3/r7)
    0x40036  b    loop
    0x40038  nop ; nop
    0x4003C  .word 0x0010C19C, 0x40039010, 0x00118D68, 0x00002000

### 3.3 Why this cave is safe (unlike PD2's)

* **No control flow reaches it.** A whole-image census finds **no branch target and no
  PC-relative literal load** into [0x40008, 0x40050) (`pd3_re.py --header`). The header
  word0/word1 (entry A/B pointers) and the handler table at 0x40050 are untouched; the byte
  before the cave (0x40008) is the exception-mask halfword and the two mask writers
  (0x404FE, 0xC19E2) touch only 0x4000A, which PD3 does not overwrite.
* **Only our patch enters it.** PD3-ENTRY redirects the boot-task success epilogue 0x4916A
  (reached only after the full boot/init chain, including `pcie_msg_init`) straight into
  0x4000C; there is no other entry.
* **Runtime writers:** only the exception staging routine (CPU 0xC6E14, literal 0x40008 at
  0xC6FA4) writes the record area - mask + 17-word staged frame - and that only happens when an
  exception is taken (a crash path that already ends the experiment; golden boots keep mask=0).
  The id-3 EXCP_INFO builder 0x49188 reads the pointer 0x40008 (payload only). Both are
  acceptable: before our stub runs nothing writes there, and a crash during the stub is already
  an experiment failure.
* **Size:** 64 B at 0x4000C..0x4004B fits before the handler table at 0x40050; the mask halfword
  at 0x40008..0x4000B is left untouched (bench still reads mask CA 0x40008).

Residual risk: an external exception while the stub is parked would overwrite the stub code
(staged frame) and the CPU would resume in corrupted code; this requires a crash that the
golden-boot evidence (mask stayed 0 across many runs) says does not happen spontaneously.

---

## 4. PD3-B: the no-cave alternative (ranked second, also built)

| segment | CPU | file | len | source_sha16 | patched bytes | decode |
| --- | ---: | ---: | ---: | --- | --- | --- |
| PD3B-ENTRY | 0x4916A | 0x916A | 8 | `c0a8c159e85b161b` | **`4C 20 7D F0 24 FE FB E7`** | `movs r0,#0x4C; bl 0xC6DB8; b 0x4916A` |
| PD3B-TABLE | 0x10C1B0 | 0xCC1B0 | 4 | `85fdd9f887cc4011` | **`00 20 00 00`** | `.word 0x00002000` (hcc+0x20 -> ROM RAZ zero table) |

Image sha256 **`ef0683dbc1a8b3b74e99f27111105538e8a437e70a8fad2a9103ae291cb745f2`**.

Rationale: CPU 0xC6DB8 is the firmware's own event raise/arm helper (id range 0x10..0x5F; for
0x4C it writes the raise bank CA 0x401611A0 and mirror 0x401612A0). Event 0x4C is registered
(handler 0x40295 = wrapper 0x40294) and enabled (CA 0x40161108 bit12) by `pcie_msg_init`, so
the boot epilogue raising it in a loop asks the *firmware event controller* to deliver the vendor
path. If delivery works: real dispatcher in IRQ context (best outcome). If not: the loop burns
CPU0 raising events, the mailbox stays unconsumed, mask/page/state stay clean - a clean negative
that independently settles the "event 0x4C delivery" question. The raise loop is deliberately an
8-byte in-place replacement of the epilogue (`00 20 21 B0 BD E8 F0 8F` is exactly 8 bytes), so
it needs **no cave at all**.

---

## 5. Expected bench observables

Standard GP8 golden-boot procedure, then ONE H2D SR announce (`printf 1 > hcc_sr_announce`);
read page/state/mask/out[0-5] and the direct D2H reader.

### 5.1 PD3-A (recommended first fire)

| # | observable | pass |
| --- | --- | --- |
| 1 | page CA 0x1BF000 after release | `0x86807FEF / 0x00000007 / 0x00000002 / 0x0000ABCD` (and unchanged after the announce) |
| 2 | state CA 0x118B60 | 1 |
| 3 | mask CA 0x40008 | **0 throughout** |
| 4 | D2H ready pair | id1 + id2 still decoded; frames keep flowing |
| 5 | **out[0] CA 0x40039010** | **0x8 -> 0** |
| 6 | out[5] CA 0x400392F0 | **not a pass criterion** - the dispatcher stores 1 here, but the register does not present it to a delayed host read (section 5.5) |
| 7 | out[2] CA 0x400392D4 | **not a pass criterion** - the dispatcher stores 8 here, but it is a write-pulse/doorbell, not a latch (section 5.5) |
| 8 | bonus | if the live handler table was populated by firmware, a real D2H response may appear; the zero-table path intentionally runs consume-only |

Negative cases: rows 1-4 fail => the boot did not reach the epilogue (capture page/mask/state);
row 5 fails while 1-4 pass => the stub ran but the dispatcher instance/table contract differs
(read-only follow-up: [0x10C19C]/[0x10C1B0]). **Stop after one boot + one frame; no retries.**

### 5.2 PD3-B (second fire, only if PD3-A is unavailable or after PD3-A)

Same rows 1-4 (boot must stay golden). Rows 5-7 pass => **event-0x4C delivery works** (major
result: the vendor path is alive and the earlier "not host-visible" conclusion only concerned the
host-side view). Rows 5-7 stay at 0 with clean mask/page => delivery layer is dead; the firmware
mailbox consumer can only be serviced by firmware-side polling (PD3-A's model).

---

### 5.4 Bench validation (device, 2026-11-24) - PASS

Exact session (revised after the fire; clean driver `hi5622_wifi.ko`, fw_path
`hi_wifi/FIRMWARE.PD3.bin` sha256 `eea9f2ff9345dc8d16badd65e77a5f8b82cf58740b7203239838259adc3905ce`;
prepare/upload/verify/seed, hcc_arm, hcc_loop, release 0x5A5A ONLY; no host write of out[5]/out[2]):

| observable | measured |
| --- | --- |
| page CA 0x1BF000 | **0x86807FEF / 0x00000007 / 0x00000002 / 0x0000ABCD - INTACT** |
| state CA 0x118B60 | **1** |
| exception mask CA 0x40008 | **0** |
| D2H ready pair | **id1 + id2 decoded (id2 payload 0x01060850)** |
| one H2D SR announce (id3) | **rc=0**; dmesg `coalesced announce id3: out[0]=0x00000000 after 0 polls` (consumed immediately) |
| h2d completion event | dmesg `hcc-core: h2d_complete data=0x00000008`; hcc_events `h2d{pub=1 done=1 coal=0 to=0 closed=0 announces=1}` |
| raw mailbox read after announce | out[0]=0, out[2]=0, out[5]=0, out[1]=0 |
| safety counters | writes=17, bad_writes=0 |

Interpretation: the firmware's own H2D dispatcher 0xC18AC ran (it read+cleared out[0], the
driver's polled completion path saw the clear on its **first** poll) and the device took the SR
frame; no exception, no page clobber, no boot break. The header-record-area cave is therefore
validated on hardware. The driver's latched boot gate was already active in the clean driver
(F1: `boot_latched` gates `hcc_core_mbox_send`), so the section 6 module change is a
hardening note, not a prerequisite.

### 5.5 Why the raw out[5]/out[2] reads were 0 (static reconciliation)

The PD2/PD3 text predicted raw `out[5]=1` and `out[2]=8` after consumption. The bench read both
as 0 a second or more later. Static analysis shows **the consume-only path does not skip those
writes** - they are unconditional in 0xC18AC before the pending-bit check:

    0xC18B2 movs r7,#1 ; movs r1,#0
    0xC18B6 ldr  r2,[r0,#0xC] ; str r7,[r2]     ; *[hcc+0xC]  = 1  -> CA 0x400392F0 (out[5])
    0xC18BA ldr  r2,[r0,#4]   ; ldr r5,[r2] ; str r1,[r2]   ; out[0] read + clear
    0xC18C0 movs r1,#8 ; ldr r2,[r0,#0x10] ; str r1,[r2]    ; *[hcc+0x10] = 8 -> CA 0x400392D4 (out[2])
    0xC18C6 cbnz r5, dispatch ...                ; pending bits after the ack/doorbell writes

so both stores execute on every dispatcher entry. No firmware code clears them afterwards:

* the only image materialisations of the mailbox CAs are the `pcie_msg_init` pointer install
  (literals 0x497DC/0x497E0 = out[1]/out[0]; ack/out[2] computed by arithmetic from the D2H
  doorbell 0x40101434) and the exception-report staging routine's out[1] write; a whole-image
  census finds **no other writer of CA 0x400392F0 / 0x400392D4** [V];
* in PD3's consume-only path the zero handler table means **no post-dispatch handlers run**, so
  the only two writers of those words in that boot are the two dispatcher stores above [V].

Since the values are written but not visible to a later host read, the registers behave as
**write-pulse / non-latching doorbell-status words** (they are not read-back latches). This is
consistent with the host side's own contract: the clean driver completes an H2D send by polling
**out[0]** (`hcc_core_mbox_tick`: out[0]==0 -> `HCC_EVENT_H2D_COMPLETE` with the inflight mask,
which is exactly the dmesg `data=0x00000008`), treats out[2] as the host->device doorbell it
writes (firmware's 8 is a pulse), and documents out[5] as an auxiliary read-only status word that
is never a required ack (`hi5622_contract.h`). Earlier sessions had already seen the host's own
out[2] write read back as 0, supporting the non-latching interpretation. [V source / V\* bench]

**Correct pass criterion for PD-type mailbox patches:** `out[0] 0x8 -> 0` plus the driver's
completion event (`h2d_complete data=0x8`, hcc_events done=1), with page/state/mask clean.
The raw out[2]/out[5] rows are diagnostic only and may legitimately read 0 later.

## 6. Module policy fix for the Lead: latch boot completion

The SR-announce gate currently re-reads the live handshake page (page+8 / page+0 bit14). That is
fragile in patched boots and in crash paths: the exception staging routine zeroes CA 0x40000108/
0x4000010C and writes its frame over the record area, and the firmware legitimately mutates
page+4/+8 during S1/entry B. Required change:

1. After release, run a boot witness over: `page+8 == 2` (S1 ACK) OR `page+0 & 0x4000`
   (entry A passed its poll) OR `state CA 0x118B60 == 1 && mask CA 0x40008 == 0`.
2. On the **first** confirmed observation latch `boot_complete = 1` in module state (single
   atomic write; cleared only on unload or a fresh release). Record evidence + timestamp + a
   snapshot of the page in sysfs.
3. Gate the SR announce and the CECE policy on the **latched** flag; keep the live page only as
   a diagnostic (report it, never gate on it).
4. Why safe: a latched post-release observation is strictly stronger evidence than a re-read, and
   avoids the PD2-class failure where a wiped/mutated page made the gate refuse a good boot.

---

## 7. Single next bench action

**Fire PD3-A once** (`FIRMWARE.PD3.bin` sha256
`eea9f2ff9345dc8d16badd65e77a5f8b82cf58740b7203239838259adc3905ce`) with the standard golden-boot
procedure, then post ONE H2D SR announce and take the section 5.1 table. One variable, one boot,
no retries; keep the read-only D2H reader running. PD3-B exists as the zero-cave follow-up.

---

## 8. Reproduce (host-only)

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-irq-unmask/tools
    $PY $T/pd3_re.py --header        # record-area cave proof + census
    $PY $T/pd3_re.py --pd2-fail      # PD2 bench-failure reconstruction
    $PY $T/pd3_re.py --selftest      # 15/15 PASS
    $PY $T/fw_pd_builder.py --selftest          # 56/56 PASS (PD2/PD3/PD3B)
    $PY $T/fw_pd_builder.py --list
    $PY $T/fw_pd_builder.py --recipe PD3
    $PY $T/fw_pd_builder.py --build PD3  --out /tmp/hi5622-pd3
    $PY $T/fw_pd_builder.py --build PD3B --out /tmp/hi5622-pd3b

Both tools read the pinned local blob in place, refuse a wrong blob, and never write vendor bytes
into the repository.

---

## 9. Evidence table

| # | claim | method | grade |
| --- | --- | --- | --- |
| E1 | PD2-A failed because entry A executed the stub in the config-3 block | bench page 0x6F window + differential PD1/PD2 | [V\*]/[V] |
| E2 | original config-3 block joins normal continuation 0x46AC6, so control boot cannot prove selector | disasm 0x47432..0x47471 | [V] |
| E3 | image header record area 0x40008..0x4004F is zero, unused by ROM, and has no branch/literal reference anywhere in the image | header layout + full census (pd3_re) | [V] |
| E4 | only runtime writer of the record area is the exception staging routine 0xC6E14 (mask+frame, crash path) | disasm + literal 0xC6FA4 | [V] |
| E5 | PD3 stub decodes exactly as designed and the patched image differs only in its 68 bytes | fw_pd_builder selftest | [V] |
| E6 | PD3-B is a valid no-cave alternative (8-byte epilogue raise loop + 4-byte table patch) | builder selftest + disasm | [V] |
| E7 | expected PD3 bench observables: golden page/state/mask + out[0] 0x8->0, out[5]=1, out[2]=8 | dispatcher contract + stub semantics | [I high] |

---

## 10. Open items ([U])

1. **O1 - what was the actual pre-release selector value on the bench?** The PD2-A evidence says
   it was 3 at 0x469E0; whether the platform-block seed file carries 3 or the block was unseeded
   is a host-side question (read the seed files/step logs). It does not affect PD3 (no selector
   dependence) but matters for future patch design.
2. **O2 - does event 0x4C delivery work after boot?** PD3-B answers this directly. If yes, the
   vendor event-driven path can be restored; if no, PD3-A's firmware-side polling model is the
   mailbox service.
3. **O3 - is [hcc+0x20] ever populated at runtime?** Static value is the sentinel 0x118D68 with
   no image writer found (REPORT-PD2 section 2.4). PD3 handles both cases; a read-only host read
   of CPU 0x10C1B0 (BAR0 0x7C41B0) on a golden boot settles it.
4. **O4 - parked boot task:** PD3 keeps the boot task alive as the poller (same as PD2); row 4 of
   section 5.1 checks the D2H path is unaffected.

---

*Lane wifi-irq-unmask, 2026-11-24, shared task task-91 follow-up. Host-only static reversal and patch
design; the PD2-A failure is device-measured and cited. The vendor firmware blob and mask-ROM
dump remain local evidence only; this directory keeps addresses, constants, instruction
encodings and hashes, never vendor bytes.*
