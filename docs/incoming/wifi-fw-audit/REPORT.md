# PD4 audit: minimality, safety, and the vendor-clean alternatives for the H2D mailbox delivery

**Lane:** `wifi-fw-audit` (shared task **task-115**), **host-only static audit**, 2026-11-24.
The bench router was never contacted; no image was built on a device; no flash and no
UART/U-Boot action was taken. The vendor firmware blob and the mask-ROM dump remain **local
evidence only**: this directory carries patch recipes (offsets, source-window hashes, OUR
patch bytes), analysis prose and tools - never vendor bytes.

Deliverables: this REPORT.md; `recipes/PD4.recipe.json` (machine-readable recipe, extended
`hi5622-fw-patch-recipe/1` schema, compatible with the owning lane's builders);
`tools/verify_pd4.py` (independent verification harness: identity, delta, entry/loop decode,
callee contract, writer ban, whole-image reference census, stub safety invariants, 31 checks +
14 selftest checks); `tools/pd4_variants.py` (behavioural variants that make the H1/H2 branch
explicit at 2-4 bytes, the spec-only smaller/safer shapes, the rejected one-shot design);
`README.md` (index + reproduce commands).

Evidence markers: **[V]** verified host-side in this lane against the pinned artifacts;
**[V*]** device/bench-measured by an earlier attended lane and cited; **[I]** inference;
**[U]** not decidable without a device read. The PD4 design and its device validation were
produced by the owning lane `wifi-irq-unmask` (task-100) and the Lead's fires; this lane
re-derives, audits and formalises - it does not claim the design.

---

## 0. TL;DR - the verdicts

1. **PD4 is reproducible and minimal in shape.** The published PD4 image (sha256
   `cfe24bfb...037b1`) rebuilds byte-identically from the pinned vendor blob (sha256
   `7fc87e20...311b`) with exactly **two segments**: a 4-byte branch at file `0x0916A` and a
   64-byte guarded poll stub at file `0x0C8240`. 68 patched bytes, 59 differing byte positions,
   length unchanged, no other byte differs. [V]
2. **The cave choice is sound and independently re-verified.** The stub lives in the 637-byte
   all-zero static-data alignment gap CPU `0x108197..0x108413`; it is outside both boot memsets
   (`0x117C00..0x1ADDE0` and memset(0x01000000,0,0xE0000)), outside the exception-staging frame
   (`0x4000C..0x4004B`), outside the live header words, between two referenced read-only tables;
   the exhaustive Thumb census finds **zero** branch targets, PC-relative literal loads,
   ADR/ADDW materialisations, word-aligned 32-bit values or movw/movt pairs pointing into the
   gap, and the only ARM-state candidates are data-decode artifacts outside the reached ARM
   code islands. [V]
3. **The ENTRY hunk is the minimum possible redirect.** The 4-byte `B.W` replaces the boot-task
   success epilogue prologue (`movs r0,#0; add sp,#0x84`) and lands in the cave (+0xBF0D2,
   inside Thumb-2 B.W range). The stub's `sub sp,#4` is not padding: skipping the `add sp,#0x84`
   plus 8-register pop leaves SP at 4 (mod 8), so the sub restores AAPCS 8-byte alignment for
   the public call. [V]
4. **What the stub changes is one thing: the firmware's H2D mailbox consumer becomes a firmware
   thread poll instead of an event-0x4C IRQ dispatch.** When out[0] is pending the stub calls the
   firmware's own wrapper CPU `0x40294` (loads the hcc instance `0x0010C190`, tail-calls the
   dispatcher CPU `0xC18AC`), which acknowledges (writes 1 to out[5] CA `0x400392F0`), reads+clears
   out[0] CA `0x40039010`, writes the H2D-done doorbell 8 to CA `0x400392D4`, then dispatches the
   pending bit through the instance's 8-byte `{fn,arg}` handler table. The stub itself never writes
   a device CA; the only CA it reads is out[0]. It bypasses - not repairs - the missing
   mailbox-to-event-0x4C assertion. The boot/ready handshake, handshake page, state word and
   exception mask are untouched (device-validated). [V] static + [V*] bench.
5. **One material question is unresolved: the stub's own sentinel check (H1/H2).** The stub
   compares the instance handler-table pointer field (CPU `0x10C1B0`) with the static value
   `0x00118D68`; equal => substitute the ROM RAZ window `0x2000` for the call (consume-only), else
   use the live table. Static evidence says the field **keeps** its static value `0x00118D68` (it
   is the RAM address of the table; the mailbox registration helper reads the field and
   populates that RAM table, it does not rewrite the field) - contradicting the design
   assumption that a runtime layer replaces it. If H1 holds, PD4 always takes the consume-only
   branch and the mailbox handlers never execute; the measured duplex lap would then be driven
   by the dispatcher's ack/clear/doorbell side effects, not by handler execution. One read-only
   pack (5 words, section 5) settles it on the next bench session. [U]
6. **Smaller/safer variants exist and are ranked.** `PD4-NOSENT` (4-byte diagnostic build: force
   the live branch) and `PD4-ALWAYS0` (2-byte diagnostic build: force the consume-only branch) are
   built and hashed by the tool; `PD4-MIN` (drop the sentinel branch: 36-byte delta instead of 68)
   and `PD4-LITE` (no firmware-code call: 44-byte delta) are specified; the one-shot epilogue-call
   variant is **rejected as functionally useless** (the host posts H2D frames after boot). The
   PD2-era locations and the PD3 header cave remain rejected by earlier bench evidence. [V]
   static / [V*] prior fires.
7. **No vendor-clean (unpatched-firmware) path is proven.** The vendor's mailbox writes are
   already reproduced exactly; the vendor's deferred-send variant additionally writes 8 to out[5]
   CA `0x400392F0` before the kick (`pcie_msg_send_irq`), and that arm was fired once in the takeover
   era: accepted, no accept of the frame. ETE SR completion events `0x2D/0x2E` do not reach the
   firmware (engine wake is firmware/ROM-internal), the ROM has no mailbox service, and the
   image has no other reader of out[0]. [V]/[V*]
8. **One host-only lever could still make the vendor path live without patching: a spare inbound
   iATU viewport onto device CA `0x40160000` and a one-shot raise of event 0x4C.** The endpoint
   has 16 inbound viewports and the vendor programs only 6; the firmware's own raise helper CPU
   `0xC6DB8` computes `1<<(id&31)` and writes it to CA `0x40161180+4*(id>>5)` and its +0x100 mirror -
   for id `0x4C` that is CA `0x40161188 = 0x1000` / mirror `0x40161288 = 0x1000`; the per-event
   enable is already set (CA `0x40161108` bit 12). This is a new device-write class (firmware-owned
   interrupt controller) and needs its own attended experiment, recovery card and stop rules -
   it has not been attempted. [V] static semantics / [I] feasibility / [U] outcome.
9. **Verdict on "is firmware patching fundamentally required?": for the closed vendor blob as
   delivered on this board, yes - no host-only protocol path is proven; conditionally no if the
   event-controller aperture of verdict 8 works. It is not a fundamental property of the
   hardware: the vendor's own boot raises event 0x4C; the failure is in the device-side
   assertion/delivery layer whose state is outside every host-visible region (CA
   `0x40000000..0x4011FFFF` is all the host sees of device IO). PD4 remains the
   hardware-validated research path for the current line and must stay labelled research-only.
   [I high]

---

## 1. Inputs, conventions, method

| artifact | identity | use |
| --- | --- | --- |
| local vendor firmware in the local `rootfs-2.4.15`/`rootfs-2.5.24` trees or `recovery-extract` | 928,920 B, sha256 `7fc87e20...311b` | read in place; never copied |
| local 16 KiB mask-ROM dump | sha256 `fcfd464f...8faa4`; effective ROM 0x0..0x81B, 0x81C..0x3FFF reads zero | RAZ-window check |
| `wifi-irq-unmask` lane (REPORT.md, REPORT-PD2/PD3/PD4.md, recipes, tools) | committed | the design this audit covers |
| `wifi-bench-tooling` (`fw_patch_builder.py`, `bench_session.py`, `bar0read.c`) | committed | cross-builder + read-only bench read path |
| `wifi-h2d-catalog`, `wifi-hcc-re`, `wifi-datapath`, `wifi-re-report` | committed | handler table ids, dispatcher semantics, instance fields |
| `wifi-fw-postboot`, `wifi-event-router`, `wifi-fw-link-event`, `wifi-ete-wake` | committed | event/IRQ layer analysis, gate results |
| Lead bench ledger (`mem-entries.md` 2026-11-24/25) | committed | PD4 fires, duplex lap, cfg80211 milestone |
| prior-era docs `phase21/22/23/24/30/32/37/49` | committed, pre-policy history | takeover-era out[5] experiment, event-controller mapping, iATU facts |

Conventions: image **CPU = file + `0x40000`**; host **BAR0 (firmware-RAM region 5) = CPU +
`0x6B8000`**; host BAR0 (device-IO region 3) = `0x3B8000 + (CA - 0x40000000)`, CA
`0x40000000..0x4011FFFF` only; the region-0 identity window reads ROM/WRAM at BAR0 offset == CPU.
Every quoted claim is checked by `tools/verify_pd4.py` (31 checks), `tools/pd4_variants.py` or the
owning lanes' tools; no vendor-byte analysis is claimed without a [V] tool check.

Method: rebuild the artifact from the pinned blob and hash it; full-byte diff; capstone-decode
every patched instruction and every callee; independent whole-image reference census (Thumb
exhaustive at every second byte; ARM at every fourth byte filtered to the instructions reachable
from the known ARM entry points; 32-bit word scan at every byte offset; movw/movt pair scan;
ADR/ADDW); re-derive the writer-exclusion bounds from their own literal sites; re-read the vendor
host module (hi5622v100_plat.ko, read in place) for the mailbox write sequences; rank
alternatives against measured evidence.

---

## 2. PD4 identity and reproduction

### 2.1 Hash chain

| step | value |
| --- | --- |
| pinned vendor blob | 928,920 B, sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b` |
| ENTRY source window (file 0x0916A, 4 B) | sha16 `c5975cf4bd702f88` |
| LOOP source window (file 0x0C8240, 64 B) | sha16 `f5a5fd42d16a2030` (= sha256 of 64 zero bytes) |
| patched image | sha256 `cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1`, length unchanged 928,920 B |
| delta | exactly 2 segments, 68 patched bytes, 59 differing byte positions, 8 differing runs (holes where a patch byte equals the source), nothing outside the segments |

The harness reproduces the image in memory and refuses to write it into the repository:
`tools/verify_pd4.py --fw <local blob>` prints PASS A1 A2 A3 B1 B2 B3 and the patched hash; any
output path must be outside `/opensource/`. Cross-check: the owning lane's builder
(`wifi-bench-tooling/tools/fw_patch_builder.py`, given a renamed copy of this recipe because a
built-in PD4 already exists) rebuilds the identical image sha256 `cfe24bfb...037b1` from the
same segments - the recipe is schema-compatible, not just similar [V].

### 2.2 The two segments

| id | CPU | file | len | source | patched | decode |
| --- | ---: | ---: | ---: | --- | --- | --- |
| PD4-ENTRY | `0x4916A` | `0x0916A` | 4 | `00 20 21 B0` (`movs r0,#0; add sp,#0x84`) | `BF F0 69 B8` | `b.w 0x108240` |
| PD4-LOOP | `0x108240` | `0x0C8240` | 64 | 64 zero bytes (cave) | recipe `patched_hex` | guarded out[0] poll stub |

Stub decode (CPU address: instruction; the literal pool is the last 16 B):

    0x108240  ldr  r7, =0x10C19C        ; hcc instance + 0xC (ack-pointer field) - init guard
    0x108242  sub  sp, #4              ; skipped epilogue leaves SP at 4 (mod 8): restore AAPCS
    0x108244  loop: ldr r3, [r7]       ; instance initialised?
    0x108246  cmp  r3, #0
    0x108248  beq  loop                ; no -> wait (never dereferences a CA)
    0x10824A  ldr  r2, =0x40039010     ; mailbox out[0]
    0x10824C  ldr  r3, [r2]
    0x10824E  cmp  r3, #0
    0x108250  beq  loop                ; no pending H2D -> wait
    0x108252  ldr  r3, [r7, #0x14]     ; instance+0x20 = handler-table pointer
    0x108254  ldr  r4, =0x118D68       ; static value (the object of the H1/H2 question)
    0x108256  cmp  r3, r4
    0x108258  beq  zero
    0x10825A  bl   0x40294             ; live table: firmware dispatcher
    0x10825E  b    loop
    0x108260  zero: ldr r4, =0x2000    ; ROM RAZ window (reads zero)
    0x108262  str  r4, [r7, #0x14]
    0x108264  bl   0x40294             ; consume-only dispatch (NULL handler entries -> cbz skip)
    0x108268  str  r3, [r7, #0x14]     ; restore the field (dispatcher preserves r3)
    0x10826A  b    loop
    0x10826C  nop; nop
    0x108270  .word 0x0010C19C, 0x40039010, 0x00118D68, 0x00002000

### 2.3 The callee the stub changes

`0x40294`: `ldr r0, [pc,#4]` (literal `0x0010C190`); `b.w 0xC18AC` (pinned bytes `0148 81f009bb`;
literal at CPU `0x4029C` = `0x0010C190`). `0xC18AC`, the H2D mailbox dispatcher: push {r3-r7,lr};
r6 = r0; if (r0 == 0) return; r2 = [r0+0xC]; *r2 = 1 (ack CA `0x400392F0` = out[5]); r2 = [r0+4];
r5 = *r2; *r2 = 0 (read+clear CA `0x40039010` = out[0]); r2 = [r0+0x10]; *r2 = 8 (doorbell
CA `0x400392D4` = out[2]); if (r5 == 0) return; then per pending bit id = ctz(r5); if id > 9 return;
table = [r6+0x20]; entry = table + id*8; fn = *entry; arg = *(entry+4); if (fn) blx fn(arg); clear the
bit; repeat. Instance fields are set by the firmware's own `pcie_msg_init`: +0x04 out[0] pointer,
+0x0C ack/out[5] pointer (CA `0x400392F0`), +0x10 doorbell pointer (CA `0x400392D4`), +0x20
handler-table pointer = static `0x00118D68`. The stub reads only out[0] and calls the wrapper; every
device-CA write is performed by the firmware's own dispatcher code (the project rule "never write
CA 0x400392F0" is a *host* rule and the firmware owns that register - the stub does not add a host
write). [V]

---

## 3. Part A - minimality audit

### 3.1 Patch footprint

* **Segment count 2** is the minimum for "redirect an existing path + park a new poller": one hunk
  must change control flow at the only post-init point reachable on every boot, the other must
  hold the poller code. No single-window patch can do both (the epilogue has no spare bytes and no
  other all-zero code cave is reachable from it). [V]
* **ENTRY hunk 4 B** is the minimum redirect: a Thumb-2 `B.W` is the only instruction that can
  reach `0x108240` from `0x4916A` (+0xBF0D2, inside +-16 MB). A 2-byte `B.N` cannot reach; a
  literal+`bx` would be 6+ B and would clobber a register the epilogue still needs; a `BL` variant
  cannot both call and return without an island. [V]
* **LOOP window 64 B** = 44 B code + 4 B `nop` padding + 16 B literal pool. The padding is cosmetic:
  the code ends exactly at a 4-byte boundary (+0x2C), so a 60-byte stub with the pool at +0x2C is
  possible with re-based PC-relative offsets. That is a 4-byte saving with zero semantic value and
  a re-encoding risk - not worth a new artifact, but the current stub is 4 B over the absolute
  minimum for its semantics. [V]
* **The sentinel branch is ~30 B of the stub** (live path 8 B + zero path 14 B + 2 literals 8 B).
  Dropping it gives the `PD4-MIN` shape: 32-byte stub, 36-byte total delta [V] (computed from the
  decode; not built here because it needs a fresh encoder - see `tools/pd4_variants.py`).

### 3.2 Why each piece exists (and what can/cannot be removed)

| piece | purpose | removable? |
| --- | --- | --- |
| ENTRY `b.w` | reach the poller from the boot-task success epilogue | no |
| instance-init guard | never touch out[0] or the table before `pcie_msg_init` has set the instance pointers; on the PD4 entry path it is already satisfied, but it keeps the stub safe if any other path ever reaches the epilogue | semantically yes (dead on the designed path), safety no |
| out[0] poll | the trigger for the mailbox consumer | no |
| sentinel branch + ROM-RAZ substitution | designed to avoid dispatching through the static handler-table pointer before the firmware populated a live table | **questionable** - the field appears never to change (H1/H2, section 5); dead code (H1) or load-bearing (H2) |
| `sub sp,#4` | AAPCS alignment (skipped `add sp,#0x84` + 8-word pop) | no (a misaligned public call is UB) |
| `b loop` after the call | permanent poller; the boot task never returns | no (single-shot is useless, 3.4) |
| literal pool | constants for the branch/compare/CA | could be 2 words in `PD4-MIN` |

### 3.3 Variants ranked (built/spec-only/rejected)

Tool: `tools/pd4_variants.py` (selftest 14/14; `--build /tmp/pd4-variants` writes the two
diagnostic images outside the repo with a JSON manifest).

| rank | variant | delta vs PD4 | changed B | what it changes | risk |
| ---: | --- | --- | ---: | --- | --- |
| 0 | **PD4 (reference)** | - | 68 patched B (2 segs) | the validated artifact | sentinel-branch ambiguity (H1/H2); CPU0 spin |
| 1 | **PD4-NOSENT** (built, sha256 `0bc1b3e8...2820`) | file `0xC8256` 4 B `a34202d0` -> `c046c046` | 4 | cmp/beq become two nops: the dispatcher always sees the real table `0x118D68` (whose RAM content is the boot-populated {id1,3,5,6} table) | executes the real vendor mailbox handlers; id3 = SR announce consumer (desired), id5 = BUG() trap (only reachable if the host posts bit5 - forbidden) |
| 2 | **PD4-ALWAYS0** (built, sha256 `84e9cd95...0210`) | file `0xC8258` 2 B `02d0` -> `02e0` | 1 (2-B hunk) | `beq` becomes unconditional `b`: deterministic consume-only (ack/clear/doorbell only) | if the duplex lap still works under it, mailbox handler execution was never needed (H1); if it breaks, H1 is refuted |
| 3 | **PD4-MIN** (spec-only) | fresh 32-B stub | ~36 total | drop the sentinel branch and 2 literals: same guard, same wrapper call, real table always; no ROM-RAZ dependency | needs a fresh stub encoding; the right long-term shape *if* the live table is wanted |
| 4 | **PD4-LITE** (spec-only) | fresh 40-B stub | ~44 total | never call firmware code; perform the dispatcher's three writes directly (ack=1 CA 0x400392F0, clear out[0], doorbell=8) | only valid if handler execution is proven unnecessary; loses the vendor handler dispatch; still a firmware patch; writes out[5] from firmware code (the dispatcher does the same) |
| 5 | **PD-ONESHOT** | rejected | 8 | `bl 0x40294` + branch to a spare epilogue island; no loop | functionally useless: the host posts H2D frames after boot, so a one-shot call at boot sees an empty mailbox |
| R1 | PD2-era config-3 block CPU `0x47432` | rejected by fire | - | - | executed on our boot (PD2-A fire broke the boot) |
| R2 | PD3 header exception-record area CPU `0x4000C` | rejected by fire | - | - | exception staging writes its 17-word frame exactly there; the first fault destroys the poller |
| R3 | live header words `0x0..0x64` | rejected | - | - | direct boot/handler contract |
| R4 | image tail `0x117C00..0x122C98` | rejected | - | - | inside the first boot memset |
| R5 | zero runs inside live PHY/config tables, hcc globals, ETE/DR descriptors, init sequences | rejected | - | - | referenced / runtime-written (full census in REPORT-PD4 section 2.1) |

### 3.4 The one-shot idea, evaluated and rejected

Replacing the epilogue with a single `bl 0x40294` looks attractive (4-8 bytes, no cave) but is inert:
the dispatcher's input is out[0] CA `0x40039010`, and when the boot task reaches its epilogue the
host has not yet posted anything (the host posts after the ready handshake, and the ready handshake
is sent by the same boot task). A one-shot call at that instant must see `out[0] == 0` and returns
immediately; the missing consumer remains missing. A periodic variant that hooks an existing
firmware callback (scheduler timer event 0x1D handler `0xC6C88`, or the idle helper `0xC2B7A`) would be
smaller than a stub but runs in IRQ/idle context: the dispatcher may call handlers written for
the boot-task context, and hot-path reentrancy with the event layer cannot be excluded statically.
Ranked below the dedicated-thread poller. [I high]

---

## 4. Part B - safety audit

### 4.1 The cave proof (independent re-run)

* gap `0x108197..0x108413` (637 B) is all zero in the vendor image [V];
* the stub window `0x108240..0x10827F` is inside the gap with 169 B below and 404 B above; all 64
  source bytes are zero [V];
* the gap is bracketed by two referenced read-only tables: literal pool CPU `0xDE890` =
  {`0x107F3C, 0x1087D4, 0x108054, 0x108414`}; consumers load `0x108054` and `0x108414` and read with
  positive offsets from those bases, so neither consumer window covers the gap [V];
* **writer exclusion** re-derived from the writers' own literals: boot memset #1
  `0x117C00..0x1ADDE0` (literals at CPU `0x46DEC/0x46DF0`) [V]; boot memset #2
  `memset(0x01000000, 0, 0xE0000)` (`mov.w` immediates at CPU `0x46D94/0x46D9C`, call to the memset
  helper `0xC1E0C`) [V]; exception staging writes its 17-word frame at `0x4000C..0x4004B`
  (literal CPU `0xC6FA4 = 0x40008`) [V] and is far away; the stub is below the BSS/memset floor
  (`0x108280 < 0x117C00`) and below the image content end (`< 0x122C98`), so it is file-backed static
  data the boot does not clear and the appended tail does not overlap [V];
* **reference census** (whole image, conservative over-approximation): Thumb, every second byte:
  **0** branch targets, **0** PC-relative literal addresses, **0** ADR/ADDW materialisations into
  the gap [V]; ARM, every fourth byte: 9 candidate sites, **all** data-decode artifacts outside the
  77 instructions reachable from the known ARM entry points
  (`0x40064/0x102C04/0x102E68/0x103140/0x103B50/0x103B60`) - a Thumb `blx` or literal table decoded as ARM,
  not code [V]; 32-bit word scan at every byte offset: 1 hit at CPU `0xE278D` (offset = 1 mod 4):
  not a loadable aligned literal, 0 word-aligned hits [V]; `movw`/`movt` pair scan: 2,943 distinct movw
  lows and 46 distinct movt highs, **0** pairs materialise a gap address [V].

Residual risk: a device-DMA writer targeting this RAM region is not provable from the image alone;
the bench read-back of the cave (BAR0 `0x7C0240`) after boot is the direct check and was part of the
PD4 fire plan; the boot itself would hang if the cave were wiped before the epilogue. Device
evidence so far: the stub is present before and after fires and boots stay golden [V*]. Bench
attended only.

### 4.2 Entry timing

The source at the entry site is the boot-task success epilogue prologue:
`0x49160 mov r0,r4 ; 0x49162 bl 0x102760 ; 0x49166 cmp r0,#0 ; 0x49168 bne 0x4910C ; 0x4916A movs r0,#0 ; 0x4916C add sp,#0x84 ; 0x4916E pop.w {r4-r8,sb,sl,fp,pc}`.
The `bne` makes it a retry-like loop; the fall-through at `0x4916A` is reached only when the call
returns 0, i.e. after the boot task's ready-frame/post-send init chain - including the
`pcie_msg_init` call that programs the mailbox instance fields and the ready handshake frames - has
run. This is what makes the stub's `ldr [instance+0xC]` guard expected-true on the designed path and
what removes the PD3 failure mode (entry before init). [V] static; [V*] the PD4 fires show golden
page/state/mask and a consumed announce.

### 4.3 Stub safety invariants (checked by the harness)

* the stub itself references exactly **one** device CA (CA `0x40039010`, a read) - PASS I1;
* no `0xCECE`, no boot-gate CA `0x40000108`/`0x4000010C`, no literal `0x400392F0` in the stub - PASS I2;
* no store to a device CA from the stub; all device writes are the firmware dispatcher's own (ack
  `0x400392F0 = 1`, out[0] clear, doorbell `0x400392D4 = 8`) - the host's hard rule "never write CA
  `0x400392F0`" is not weakened: the host still never writes it - PASS I3;
* all internal branch targets stay inside the stub and the only call is the wrapper `0x40294` - PASS E3;
* `sub sp,#4` restores 8-byte alignment for the public call (skipped `add sp,#0x84` + 8-register pop
  puts SP at 4 mod 8) - PASS E1.

### 4.4 Residual risks (honest list)

1. **H1/H2 sentinel ambiguity** (section 5): the stub may be permanently consume-only, or the live
   branch may be the one taken; both are safe for the register protocol, but what they *mean* for
   handler execution and driver assumptions differs. One read-only pack settles it.
2. **CPU0 is pinned by the poll loop and never yields.** The boot task becomes an infinite
   thread-context loop on the boot core. The measured image keeps producing D2H frames after PD4
   boots, so the D2H/ETE producer path does not depend on CPU0 scheduling [V*], but any future
   firmware feature that wants a CPU0 thread would be starved. A real functional change; state it.
3. **Exception behaviour is not improved.** If a mailbox handler faults while the stub is parked,
   the firmware's own exception path runs (staging frame at `0x4000C`, cascade possible); the cave
   survives and the first frame is readable at BAR0 `0x6F800C`, which is PD4's diagnostic value.
4. **Loop hazards:** the stub has no watchdog, timeout or exit; if a handler ever fails to return,
   the mailbox consumer is dead for that boot. Keep the one-frame stop rule and the read-only fault
   frame check in the bench procedure.
5. **No mitigation of the underlying event-layer gap:** PD4 is a bypass. The vendor path (event
   0x4C) remains broken/unobservable on this board; PD4 does not repair it and a patched image is
   a research instrument, not a product.

---

## 5. The open question: H1/H2 and the decisive read-only pack

### 5.1 The two hypotheses

The stub branches on `[instance+0x20]` (CPU `0x10C1B0`) vs static `0x00118D68`:

* **H1 - static pointer (favoured by static analysis).** CPU `0x10C1B0` is initialised data holding
  `0x00118D68`; the whole image contains that value exactly once (the field itself); the only writer
  path found is the mailbox *registration* helper CPU `0xC187A`: it loads the global pointer slot
  `0x172130` (written `0x0010C0F4` by `pcie_msg_init` at CPU `0x49706`), then `ldr r4,[r4,#0xBC]`
  **reads** CPU `0x10C1B0` (since `0x10C0F4+0xBC = 0x10C1B0`) and writes the entry `{fn,arg}` at
  `0x118D68 + id*8`. So the pointer field stays `0x118D68` and the *table* at RAM `0x118D68` is
  zeroed by the boot memset and then populated with ids 1/3/5/6. Under H1 the stub's compare is
  always equal, the ROM-RAZ substitution is always taken, and the dispatcher skips all handlers
  (its `cbz` NULL check makes the ROM zeros safe). The measured duplex lap would then be explained
  by the dispatcher's ack/clear/doorbell side effects (and/or another path), not by handler
  execution. Supporting: the PD2 report's own "no image code writes [0x10C1B0]" [U] note; my
  independent scan (value occurs once; no store with offset 0xBC/0xF0 to that object found);
  `0x118D68` is inside the first memset range, so a static image table there would be wiped anyway;
  `wifi-hcc-re`/`wifi-h2d-catalog` describe the table as "RAM, populated ids 1/3/5/6".
* **H2 - runtime pointer.** Some unmapped firmware layer rewrites `[0x10C1B0]` (or the stub is entered
  with a different instance object) before the compare. Then the live branch is taken and PD4
  dispatches through the real vendor handler table. The strongest argument for H2 is *indirect*: the
  successful ordered lap showed the firmware writing `ctx+0x190 = 0x01011448` after the host's key-0x64
  create frame - i.e. the firmware's H2D message handler demonstrably ran, which the success path
  attributes to the mailbox dispatch. That is contradictory to H1 unless the handler path is woken
  by another mechanism (the dispatcher's ack/doorbell writes, or the ETE/SR path).

The audit does not pick a side: neither hypothesis is proven, and the public reports currently
describe the live branch as if H2 held. The contradiction is flagged because it changes what
downstream driver work may assume about firmware handler execution. [U]

### 5.2 The read-only pack (no writes, needs no new fire)

All values are boot state; read them with the existing read-only `bar0read` helper on the next attended
session with any PD4 golden boot (no frame needed; no device mutation):

| word | address | H1 predicts | H2 predicts |
| --- | --- | --- | --- |
| instance +0x20 (table pointer) | BAR0 `0x7C41B0` (CPU `0x10C1B0`) | `0x00118D68` | != `0x00118D68` |
| table entry id1 | BAR0 `0x7D0D68` (CPU `0x118D68`) | fn = `0x00040511` | per live table |
| table entry id3 | BAR0 `0x7D0D78` (`0x118D68+0x10`) | fn = `0x000C5145`, arg = `0x0010C0F4` | per live table |
| table entry id5 | BAR0 `0x7D0D88` (+0x20) | fn = `0x000C19DD` (BUG() trap) | per live table |
| ROM RAZ window | BAR0 `0x2000` (region-0 identity) | zero | zero (RAZ confirmed from the dump) |

Interpretation: field = static **and** table populated => H1 (PD4 is consume-only; then run
`PD4-NOSENT` as the next diagnostic to see whether handler execution changes anything); field !=
static => H2 (PD4 is the designed live dispatch; make `PD4-ALWAYS0` the control). The pack changes
nothing on the device and is safe with the recovery card armed. [I high]

---

## 6. What functionality the stub changes (vs the vendor path)

| aspect | vendor golden boot | PD4 boot |
| --- | --- | --- |
| H2D mailbox trigger | device mailbox asserts event `0x4C`; the image IRQ trampoline `0x40064` -> event dispatcher `0xC2EFC` -> handler table `0x17D430[0x4C]` -> wrapper `0x40294` -> dispatcher `0xC18AC` | boot task falls into the stub and calls the wrapper directly whenever out[0] is pending; the event layer is bypassed |
| context | IRQ context (event dispatcher saves/restores the interrupted context) | thread context (boot task), permanent loop, no scheduler yield on CPU0 |
| mailbox acknowledgement | dispatcher writes ack=1 (out[5]), clears out[0], writes doorbell 8 | identical - the same firmware dispatcher code runs |
| handler dispatch | per pending bit through the live 8-byte table | the same dispatcher call; whether handlers execute is exactly the H1/H2 question (section 5) |
| boot state | handshake page `0x86807FEF/7/2/0xABCD`, state 1, mask 0 | unchanged on every measured PD4 boot [V*] |
| ready handshake | g4 id1/id2 sent before the epilogue | identical (the stub is only entered after them) |
| other firmware tasks | scheduler continues | CPU0 stays in the poll loop; CPU1 services the remaining firmware work (D2H frames measured after PD4 boots) |
| exception diagnostics | staging frame is transient | cave survives exceptions; the first staged frame at CA `0x4000C` (BAR0 `0x6F800C`) is readable - the reason PD4 was built |

Net: PD4 does not "fix" the firmware's event layer; it replaces the missing delivery with a
firmware-side poll, and its only behavioural cost is the permanently pinned CPU0 thread. [V/I]

---

## 7. Part C - vendor-clean alternatives (can the host raise event 0x4C?)

Ranked by (a) does it avoid patching the firmware, (b) evidence that the required mechanism exists,
(c) risk/effort. None has been executed in this lane.

### A1. Mailbox doorbell bits (the vendor's own sequence) - already reproduced, insufficient

The vendor host's normal send (`pcie_msg_send`, plat.ko `0x160f4`) writes the pending bitmap to out[0]
CA `0x40039010`, then ORs bit0 into out[2] CA `0x400392D4`; the six mailbox register pointers are filled
by `shuangta_pcie_msg_reg_map` (`0x1b1a0`) in the order {`0x40039010, 0x40039014, 0x400392d4,
0x40101438, 0x40101414, 0x400392f0`} = `ctx+0x2c..ctx+0x40`. Our clean-room transport already performs
exactly this sequence, and the firmware still never takes event `0x4C`. [V] static + [V*] bench
(out[0] 0x8 never consumed on unpatched firmware across many sessions). The vendor's *deferred*
variant `pcie_msg_send_irq` (`0x174a8`) additionally writes **8 to out[5] CA `0x400392F0` first**
(`ldr r3,[ctx+0x40]; *r3 = 8`), waits for out[0] to clear, then posts; that is exactly the register
the project's absolute rule says the host must never write. It *was* tried once in the takeover era
(`phase22/h2d-accept.md` BOOT B): the write was accepted by the register file, did not hang that
dual-RC boot, and did not produce an accept (out[0] stayed 0x8, no id-1, glue unchanged). The
never-write rule comes from a different (EP0-only takeover) hang, so the corpus has a no-hang
datapoint for the write but no success datapoint. Keep the rule; do not rank this as a solution.
[V]/[V*]

### A2. ETE SR completion events 0x2D/0x2E - not a delivery path

`pcie_msg_init` registers handlers `0x462F9` (id `0x2D`) and `0x4624D` (id `0x2E`) for ETE ring
completion. The host *can* commit SR nodes and the ETE engine consumes them (bench: SR0
producer/consumer advanced, sendword `0x00010001`), but no firmware dispatch is observed, and the
ETE/DR engine wake itself is firmware/ROM-internal (the `ete-wake` E1a/E1b experiment was negative:
the only host-writable fill lever does not wake the A->B copy). Those event handlers are ring-copy
services, not the H2D dispatcher. [V]/[V*]

### A3. Event controller via a different aperture (spare iATU viewport + one-shot raise) - the only host-only candidate

The device event controller block CA `0x40160000` is outside the host-visible IO region 3
(`0x40000000..0x4011FFFF`): the host cannot read or write it today, and the old "BAR0 0x519188" probe
was retracted as a wrong-window alias (it resolved into region-4 ACP SRAM). The endpoint has **16
inbound iATU viewports and the vendor programs 6**; the `wifi-fw-postboot` lane proposed a read-only
7th viewport onto CA `0x40160000` (BAR0 `0x8D0000..0x8DFFFF`), and the `phase32` analysis lists the same
as the highest-information diagnostic. The firmware's own semantics for the raise are fully
decoded:

* **raise helper** CPU `0xC6DB8(id)`: requires `0x10 <= id <= 0x5F`; computes `1 << (id & 31)` and
  writes it to CA `0x40161180 + 4*(id >> 5)`, then the same word to CA `0x40161280 + 4*(id >> 5)`
  (the +0x100 mirror). For id `0x4C`: CA `0x40161188 = 0x00001000` and mirror `0x40161288 = 0x00001000`
  (**not** `0x401611A0`, see the corrections register). [V]
* **per-event enable** already set by `pcie_msg_init`: `0xC6FF4(0x4C)` writes CA
  `0x40161100 + 4*(0x4C>>5) = 0x40161108`, bit 12 = `0x1000`; the controller global enable/priority/config
  (`0x40160100/04/08`) are set by `0xC3024` in entry A (and ROM S1); handler `0x4C` = wrapper `0x40295` is
  registered by `pcie_msg_init` (`0xC74B0(0x4C, 0x40295)`). So a host that could reach CA `0x40161188`
  could raise exactly the event the vendor stack raises to deliver to the dispatcher - the real
  vendor path, no firmware patch. [V] static.

Constraints and risks: (1) it needs a **spare iATU viewport programmed at runtime** by the host
(reversible; must not disturb the six named windows or calibration - the phase32/irq-block
analysis is explicit); (2) it is a **new class of device write** - firmware-owned interrupt
controller state - so it needs its own gate, a one-shot/bounded rule (assert once after posting a
frame, never loop), a recovery card and an IRQ-storm stop rule (a 207-line IRQ storm already
panicked the box once in the phase49 arc); (3) the block may itself be write-protected from the
PCIe inbound path - unknown until tried; (4) if the hardware mailbox never asserts the event, this
is the *only* way to test whether the rest of the vendor path (dispatcher + handlers) is healthy.
**It is the one vendor-clean experiment worth designing before accepting "patching is fundamental".**
[I high]/[U]

### A4. The ROM's own handlers - no path

The mask ROM's IRQ vector[6] literal points into the image trampoline (ROM `0x34` -> CPU `0x40064`); the
ROM has no mailbox service, only the S0/S1 handshake and cache maintenance, and no host-writable ROM
register raises the H2D mailbox event. [V]

### A5. Other host-visible wake paths - none

Whole-image census: the only reader of out[0] CA `0x40039010` is the dispatcher `0xC18AC` (single literal in
`pcie_msg_init`); the firmware has **no poller** of its own. The other host-owned mailbox words are
D2H flow-control (out[1] bitmap is device-written; `0x40101438` ack / `0x40101414` re-arm are the host's D2H
consumption handshake; writing them cannot wake the H2D consumer). ETE doorbells are already
exercised. So no existing host-writable register provides an alternative wake. [V]

### A6. Vendor trace / JTAG / ROM monitor - decisive but not host-only

A vendor golden-boot trace of handler table `0x17D430[0x4C]` firing (or a ROM-monitor/JTAG watch of the
event controller) would settle the assertion question and the H1/H2 question directly. This is out
of scope for a host-only lane and needs hardware access; it is listed so the corpus records the
decisive instrument that does not exist yet. [U]

### A7. Verdict

* **No vendor-clean (unpatched-firmware) path is proven on this board.** The vendor's mailbox write
  sequence is fully reproduced and insufficient; the deferred-send out[5] arm was tried and
  insufficient; ETE events do not deliver; the ROM has no service; there is no firmware poller.
* **Patching is required for the closed vendor blob as delivered** - unless the A3 aperture
  experiment makes the event controller reachable and writable, in which case the vendor IRQ path
  could be made live from the host (still not "clean" in the sense of zero device side effects - it
  is a host-side, non-persistent intervention).
* It is **not** a fundamental property of the hardware: the vendor shipping stack raises event
  `0x4C` on its own boot. The blocker is the device-internal assertion/delivery chain whose state is
  outside `0x40000000..0x4011FFFF`, not a missing host message format. [I high]
* For the upstream/public frame: PD4 is a research instrument. A from-scratch driver cannot depend
  on it; the honest upstream statement remains "Wi-Fi firmware interface closed, no upstreamable
  driver path yet".

---

## 8. Formalised research artifact

The patch is published as a **recipe**, never as a patched blob:

* schema `hi5622-fw-patch-recipe/1` (same as the owning lanes; compatible with
  `wifi-bench-tooling/tools/fw_patch_builder.py`), extended with `image_sha256`, `cpu_file_base`,
  `semantics` and `safety` blocks: pinned `source` sha256+length (provenance); per segment `file`,
  `len`, `source_sha16` (window hash of the *source* bytes), `patched_hex` (our bytes), `isa`,
  `expect` (semantics), `cpu`, `role`; `image_sha256` (result identity) and the semantic/safety
  descriptions.
* **verification harness** `tools/verify_pd4.py`: 31 checks in 8 groups (identity, delta, entry,
  loop/cave, stub decode, callee contract, writer ban/reference census, stub safety) plus 14
  selftest checks; `--json` for machine consumption; refuses to write anything inside the
  repository; reads the vendor blob in place. Runtime < 10 s including the census.
* **variant harness** `tools/pd4_variants.py`: builds the two diagnostic variants outside the repo with a
  JSON manifest, prints the spec-only smaller/safer shapes and the rejected one-shot design.
* **hash chain to a deployed image:** vendor blob sha256 -> per-window sha16 -> patched image
  sha256 -> deployed name (e.g. `/lib/firmware/hi_wifi/FIRMWARE.PD4.bin`; the firmware-loader name is
  relative to `/lib/firmware`). The bench-session runner's manifest binds the deployed file to the
  recipe; the verification harness re-derives it from scratch.

Reproduce (host-only):

    PY=.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-fw-audit/tools
    $PY $T/verify_pd4.py --selftest                  # 14/14 PASS, no vendor blob needed
    $PY $T/verify_pd4.py                             # 31 checks + census, PASS
    $PY $T/verify_pd4.py --json | jq .               # machine-readable
    $PY $T/pd4_variants.py --selftest                # 14/14 PASS
    $PY $T/pd4_variants.py --table                   # ranked option table
    $PY $T/pd4_variants.py --build /tmp/pd4-variants # diagnostic images + manifest (outside repo)

Deployment/bench rules that remain mandatory for any PD4 (or variant) fire: standard GP8
golden-boot procedure, release `0x5A5A` only and **never `0xCECE` during boot**, custom blocks
pre-seeded, `hcc_arm`/`hcc_loop`, the latched boot gate, one frame budget, host never writes CA
`0x400392F0`, never reads `0x10161000`, recovery card armed, and for PD4 a read-only cave-integrity check
(BAR0 `0x7C0240`) plus, after any fault, the staging frame (BAR0 `0x6F800C`).

---

## 9. Publication and provenance rationale

* **What is published here:** offsets, source-window hashes, our 68 patch bytes, instruction
  encodings, semantic descriptions, the harnesses (MIT), and the audit prose. This is the same form
  the owning lanes already publish (PD2/PD3/PD4 recipes in `wifi-irq-unmask`).
* **What is not published:** the vendor firmware blob, the patched image, the mask-ROM dump, or any
  extracted vendor code/data. The harness reads the local blob in place; the patched image is
  produced locally by the reader from their own device/rootfs and never committed. The repository's
  rule ("no vendor binaries or vendor-derived blobs in tracked text; a recipe produces them locally
  from the reader's own device", `PUBLISH.md`) is met by construction, and the tools refuse an output
  directory inside the repository.
* **Provenance:** reverse engineering of the vendor's GPL-2.0 kernel modules and a closed firmware
  blob (static disassembly) plus black-box device experiments, not a clean-room implementation; the
  GPLv2 section-3 source request is open (no reply). The recipe is a functional specification of a
  *delta* we wrote; the displaced instructions are quoted only as the 4-byte encoding needed to
  apply the delta. License intent: prose/tools MIT, any derived code GPL-2.0, consistent with
  `PUBLISH.md`. This is a research instrument, not a release or an upstream artifact; bench gates are
  not upstream progress.
* **Safety framing for readers:** the recipe is not a bootable image and should not be applied by
  anyone who does not have the recovery door and the bench procedure; the sending-side hard rules
  and the one-frame stop rule are part of the artifact.

---

## 10. Recommendations (ranked)

1. **Keep PD4 as the validated research artifact** for the current clean-room line; do not promote
   it to a product path or an upstream claim. Fix its documentation to state the H1/H2 open
   question (section 5) instead of assuming live handler dispatch.
2. **Run the 5-word read-only pack (section 5.2) on the next attended session.** It needs no new
   fire and settles H1/H2; it also confirms the cave content and the table ids. Owner: the
   bench/Lead lane; `wifi-bench-tooling/tools/bar0read.c` already provides the read path.
3. **Then run exactly one diagnostic image:** `PD4-NOSENT` if H1 (to see whether handler execution is
   needed at all), or `PD4-ALWAYS0` if H2 (consume-only control). One boot, one frame, one variable;
   compare the duplex lap end-to-end. Hashes in the variant manifest.
4. **Only if a vendor-clean path is required for publication/legal framing:** design the A3 aperture
   experiment (spare iATU viewport -> CA `0x40160000`; read-only first, then a single raise write of
   `0x1000` to CA `0x40161188` / `0x40161288` after a mailbox post) as its own task with a recovery
   card, an IRQ-storm stop rule and a no-loop write budget. Do not attempt it as part of a protocol
   ladder.
5. **If the line moves to the vendor handler path**, build `PD4-MIN` (smaller, no ROM-RAZ dependency)
   with a fresh encoder rather than carrying the sentinel branch forever.
6. **Publication:** reference this report from the umbrella `wifi-re-report` (the H1/H2 open item and
   the A3 alternative are the two corrections to the current narrative); keep the recipe/tool hashes
   in the evidence index.

---

## 11. Evidence table

| # | claim | method | grade |
| --- | --- | --- | --- |
| E1 | PD4 image sha256 `cfe24bfb...037b1` rebuilds from the pinned blob with unchanged length | verify_pd4 A1/A2/B1/B2 | [V] |
| E2 | the delta is exactly 68 bytes in 2 segments (59 differing byte positions); nothing else differs | full-byte diff, B3 | [V] |
| E3 | the 637-byte gap `0x108197..0x108413` is all zero and bracketed by two referenced tables | gap scan + literal pool `0xDE890` | [V] |
| E4 | the stub is outside both boot memsets and the exception-staging frame; the bounds are re-derived from their own literals/immediates | G1-G7 | [V] |
| E5 | no Thumb/decode reference, aligned literal, or movw/movt pair points into the gap; ARM candidates are data-decode artifacts outside the reachable ARM islands | H1 + census | [V] |
| E6 | the entry site is the boot-task success epilogue (post-init) and the 4-byte B.W reaches the cave | C1-C3 + source decode | [V] |
| E7 | the stub decodes to the audited sequence; `sub sp,#4` fixes AAPCS alignment; only one CA is referenced (out[0], read); no boot magic/forbidden CA | E1-E3, I1-I3 | [V] |
| E8 | wrapper `0x40294` passes the hcc instance `0x0010C190`; dispatcher `0xC18AC` performs ack(out[5]=1)/clear(out[0])/doorbell(=8)/handler dispatch with a NULL-handler skip | F1-F3 + pinned encodings | [V] |
| E9 | PD4 is device-validated: golden boot page/state/mask, ready handshake, consumed announce, full duplex lap and the deterministic re-run | cited Lead fires (2026-11-24/25) | [V*] |
| E10 | H1/H2 is unresolved: the handler-table pointer field keeps static `0x00118D68`; no image writer found; the registration helper reads the field and populates the RAM table | J1 + whole-image scans + hcc-re/h2d-catalog | [V]/[U] |
| E11 | the vendor's deferred send writes `out[5] CA 0x400392F0 = 8` before the kick; the normal send does not; the takeover-era trial was accepted but did not produce an accept | pl.ko 0x174a8/0x160f4/0x1b1a0 disassembly + phase22/h2d-accept.md | [V]/[V*] |
| E12 | the event controller's raise/enable semantics are decoded: raise CA `0x40161180+4*(id>>5)` (+0x100 mirror), enable CA `0x40161100+4*(id>>5)`; for `0x4C`: `0x40161188 = 0x1000` and enable `0x40161108` bit 12 | firmware helpers `0xC6DB8`/`0xC6FF4` + `pcie_msg_init` | [V] |
| E13 | CA `0x40160000` is outside region 3 (`0x40000000..0x4011FFFF`); the endpoint has 16 inbound viewports and the vendor programs 6 | wifi-fw-postboot sec.7.2, phase32/irq-block-mapping §F | [V]/[I] |
| E14 | no firmware poller of out[0] exists other than the dispatcher; ETE/ROM paths cannot raise `0x4C` | whole-image literal census + wifi-irq-unmask/wifi-fw-link-event | [V] |
| E15 | the two diagnostic variants are built, hashed and decode-checked; the smaller/safer shapes are specified | pd4_variants selftest/build | [V] |
| E16 | this directory's recipe is accepted by the owning lane's builder and rebuilds the identical PD4 image sha256 (renamed copy only because the owning builder has a built-in PD4) | fw_patch_builder.py cross-run | [V] |

---

## 12. Open items ([U])

1. **H1 vs H2** (section 5): one read-only pack; no device mutation.
2. Whether the real mailbox handlers ever run under PD4 - follows from item 1 plus the
   `PD4-NOSENT`/`PD4-ALWAYS0` single-variable fires.
3. Whether CA `0x40161188` is writable from a PCIe inbound aperture (A3) and whether the event controller
   then delivers `0x4C` to the CPU; an IRQ-storm bound is mandatory.
4. The exact writer (if any) of CPU `0x10C1B0` - H2 would need one; the static scan cannot find it, so H2
   also implies a DMA/device-side writer or a code path not covered by the census.
5. The provenance/licensing of the firmware blob itself (GPL request open); until answered, keep the
   recipe-only publication stance and never redistribute the blob.

---

## 13. Corrections register (this lane)

* **C1.** `wifi-irq-unmask` REPORT-PD3 (PD3-B section) says the event-raise helper "for `0x4C` writes the
  raise bank CA `0x401611A0` and mirror `0x401612A0`". Re-derived here: CPU `0xC6DB8` uses base
  `0x40161180` with word index `id>>5`, so id `0x4C` targets word index 2: CA `0x40161188` / mirror
  `0x40161288` (bit 12 = `0x1000`). `0x401611A0` would be id `0x100`, outside the helper's `0x10..0x5F`
  range. The corrected pair matters only if the A3 aperture experiment is ever designed; no
  historical result depends on it.
* **C2.** The `wifi-driver-re` "twin/ack word" table maps `ctx+0x34` to a "twin copy B `0x40039800`".
  Re-derived from `shuangta_pcie_msg_reg_map`: the six context slots are
  `ctx+0x2c..ctx+0x40 = {out[0], out[1], out[2] doorbell, out[3] D2H ack, out[4] D2H rearm, out[5] ack}`;
  so the vendor's "twin" write is simply the out[2] doorbell OR-1, already reproduced by the clean
  transport. This removes an old open question (the "twin semantics [U]") from the H2D path: there
  is no extra host register to write.
* **C3.** The `wifi-hcc-re` section 3.3 note "[I] phase37 identifies `ctx+0x40` as out[5]" is confirmed [V]
  by the `shuangta_pcie_msg_reg_map` disassembly; the deferred send's first write is therefore a write to
  CA `0x400392F0`, matching the project's never-write rule concern.

---

*Lane `wifi-fw-audit`, 2026-11-24, shared task task-115. Host-only audit: the vendor firmware and mask-ROM
dump were read in place and are not redistributed; every quoted value is an address, constant,
instruction encoding or hash; the tools' outputs are reproducible with `tools/verify_pd4.py` and
`tools/pd4_variants.py`.*