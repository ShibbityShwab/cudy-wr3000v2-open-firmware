# Chip mask boot ROM reversed: S0/S1 boot flow, the exact +8=2 writer, the 0xCEED gate and the host action

**Lane:** wifi-rom-dump (shared task `task-86`), **host-only**, 2026-11-23. The bench router
was never contacted, no image was built, no device was opened, and **no ROM or firmware byte is
committed here** - only derived addresses, constants, decoded structure and a bench plan. The
ROM dump itself is a DEVICE ROM kept as local evidence only.

Inputs (read in place; local evidence only):

| artifact | identity |
| --- | --- |
| `/home/shibbityshwab/staging-fi/rom16k.bin` (mask boot ROM) | 16,384 B, sha256 `fcfd464fd2bdfc1ab8b3f731ce19c7037885699f21796753654ae85c2198faa4` |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B, sha256 `7fc87e20...ef311b` |
| prior lanes: `wifi-rom-s01`, `wifi-rom-flow`, `wifi-s1-cpu1`, `wifi-fw-link-event`, `wifi-ete-engine`, `wifi-chip-reset` | cited, not repeated |

Evidence grades: **[V]** verified here from the local artifacts by `tools/romdis.py` (86/86
selftest); **[V\*]** measured on the bench by an earlier attended lane (cited); **[I]**
inference from code/data; **[U]** unknown.

Address conventions: **ROM cpu address == dump file offset** (ROM is mapped at CPU 0; host
identity window phys = `0x40000000 + offset`). Image **cpu address = file + 0x40000**.
Host BAR0 for IO = `0x3B8000 + (CA - 0x40000000)`; the `CA 0x1BF000` handshake page is
readable at BAR0 `0x877000` (phys `0x40877000`). The host upload lands at CA `0x01240000`,
i.e. image cpu base `0x40000`.

---

## 0. TL;DR - the five answers

**(a) The readable window is the whole relevant ROM.** The 16 KiB dump contains 2,073 bytes of
real mask ROM (`0x000-0x81B`): ARM vectors + stubs `0x000-0x08B`, the ARM reset/boot path
`0x08C-0x1DF`, a 7-word handler/stack table `0x1E0-0x1F8`, Thumb-2 boot/S1 code
`0x1FC-0x60B`, two ARM dispatch words `0x60C/0x614`, two 16-byte-record field-merge tables
`0x618-0x7F7`, and the build banner at `0x7F8`. `0x81C-0x3FFF` reads all-zero (RAZ, no
code); the window boundary is phys `0x40003FFF` [V]. There is **no hidden ROM body** behind
the S1 logic: the complete CPU0 and CPU1 boot paths are in this dump [V].

**(b) Both image header entries are ROM-dispatched, on different cores.** CPU0's primary ROM
path jumps to **entry A** (`header+0x00 = 0x46970`) `blx` at ROM `0x55E` after the host
release; CPU1's secondary ROM path (the S1 path, ROM `0x564`) writes the handshake ACK and
then loads **entry B** (`header+0x04 = 0xC742C`, read from CPU `0x40004`) and `blx`-es it
at ROM `0x604`. Both entry pointers are **cleared by the ROM on the cold/non-0xCEED path**
(CPU0 stores 0 to `0x40000` at `0x53A`; CPU1 stores 0 to `0x40004` at `0x5AA`) and must be
re-populated by the host firmware upload before the release write [V].

**(c) The +8=2 writer is CPU1 running the mask ROM, at ROM `0x5FE`.** CPU1 executes the
secondary path `0x564`: it clears its own handshake words, arms, then waits (WFI loop) until
BOTH `[0x1BF00C] == 0x0000ABCD` (the image's handshake request) and
`[0x4000010C] & 0xFFFF == 0x0000CEED` (the ROM boot-stage gate). Only then it executes
`strd r1,r0,[r3,#4]` with `r0 = 1 << (MPIDR & 3)` - `[0x1BF008] = 2` on CPU1 - sets
`[0x1BF004] |= 4`, and dispatches entry B. **The image never writes +8=2**; it only consumes
the ACK [V].

**(d) The exact gating condition is a 16-bit register the host is currently clobbering.** CPU1's
S1 completes iff (i) CPU1 runs the ROM, (ii) the image posts the `0xABCD` magic and rings
the inter-core doorbell (`CA 0x40161F00 <- 0x00020000`), (iii) CPU1 is woken, and (iv)
`CA 0x4000010C == 0xCEED` at the check. The ROM writes `0xCEED` there (CPU0, `0x506`,
after the host `0x5A5A` release). **Our transport writes `0xCECE` to that same 16-bit
register** as its "D2H announce gate" and continuously re-writes it - overwriting `0xCEED`,
so CPU1's loop can never pass. The vendor host never writes `0xCECE` and its boot has
mask 0 (the image-side `0xCECE` consumer never runs), so `0xCEED` survives and S1
completes. The two values are mutually exclusive uses of one register [V code / I high].

**(e) A host action exists, and it is a subtraction, not an addition.** Release with
`0x5A5A -> CA 0x40000108` as today, keep the GP4-grade pre-seeding (platform-custom block /
TCM), and **do not write `0xCECE` to `CA 0x4000010C` during boot**. Expected on the next
attended boot: page `0x1BF000 = 0x86807FEF`, `+4 = 0x00000007`, `+8 = 0x00000002`,
`+C = 0x0000ABCD`; then state `CA 0x118B60 = 1` and the boot task/scheduler run. The bench
already reached `state=1` in earlier **no-gate-service** sessions without reading the page -
that is consistent with S1 having completed then. A host write of `0x0000CEED` to
`CA 0x4000010C` is available as a direct diagnostic/repair of condition (iv) [V code / V\*
bench / I].

---

## 1. Method and reproduction

`tools/romdis.py` (new, host-only, capstone-based) reads the local ROM dump and the local
vendor firmware in place and prints derived facts only. Its `selftest` validates **every**
claim in this report (86 checks) against the bytes, and fails if the local ROM dump is not the
expected device ROM.

```
PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
T=opensource/docs/incoming/wifi-rom-dump/tools/romdis.py

$PY $T info                 # identity, sha256, 0x81C-0x3FFF classification
$PY $T map                  # vectors, stubs, slot table, code/data regions
$PY $T flow                 # S0 reset path, primary path, secondary S1 path
$PY $T stores               # absolute-address store census (S1 evidence)
$PY $T consts               # materialised 32-bit constant census
$PY $T tables               # the two field-merge tables decoded
$PY $T search 0xcece        # materialisation search for any constant
$PY $T gate                 # the S1 gating analysis + host-action checklist
$PY $T image header         # image header + ROM slot cross-check
$PY $T image handlers       # 5 handlers, ARM->Thumb veneers, r1 ids (corrected)
$PY $T image dispatchers    # IRQ trampoline 0x40064 / 0xC2EFC and FIQ 0x103140 / 0xC2EEC
$PY $T image census         # 0x4000010C / 0xCECE / 0x40000108 census in the image
$PY $T selftest             # 86/86 PASS
```

The tool never copies the ROM or the firmware; `ROM_DUMP` / `HI5622_FW` override the local
paths.

---

## 2. The ROM dump: region classification

| ROM cpu range | content | grade |
| --- | --- | --- |
| `0x000-0x01F` | ARM vector table: 5x `ldr pc,[pc,#0x18]` + NOP + 2x `ldr pc,[pc,#0x14]` | [V] |
| `0x020-0x03B` | 7 vector target words: `0x8C,0x3C,0x4C,0x5C,0x6C,0x40064,0x7C` | [V] |
| `0x03C-0x08B` | 5 ARM exception stubs x16 B: `push {r0}; ldr r0,=slot; ldr r0,[r0]; bx r0` | [V] |
| `0x08C-0x1DF` | ARM reset/boot path: caches/MPU off -> per-mode stacks -> MPU regions -> on -> MPIDR dispatch | [V] |
| `0x1E0-0x1F8` | 7-word table: 5 handler slots (`0x40060,0x4005C,0x40058,0x40054,0x40050`) then 2 stack tops (`0x1BF110, 0x1BEF00`) | [V] |
| `0x1FC-0x60B` | Thumb-2 boot/S1 code (helpers, S0 primary path, S1 secondary path) | [V] |
| `0x60C-0x617` | `0x54D` / `0x565` (primary/secondary Thumb entries) + 2 ARM `ldr pc,[pc,#-4]` dispatch words | [V] |
| `0x618-0x717` | field-merge table A (16 x 16 B) | [V] |
| `0x718-0x7F7` | field-merge table B (14 x 16 B) | [V] |
| `0x7F8-0x81B` | build banner: identifies a **Hi1152V100**-family "CS" ROM build dated 2022-09-21 16:21:44 (identification metadata only) | [V] |
| `0x81C-0x3FFF` | all zero; reads as RAZ - **no ROM content** | [V] |

The effective ROM is therefore 2,073 bytes; the last nonzero byte is `0x818`. Reads at
phys `0x40004000+` return `0xFFFFFFFF` (unmapped) [V\*].

**Function/entry map of the Thumb region** (all verified by disassembly):

| entry | role |
| --- | --- |
| `0x1FC` | read 16-bit `CA 0x4000010C` (boot-stage gate) |
| `0x208` | write `0xBAAD` to `CA 0x4000010C` |
| `0x218` | write `0xCEED` to `CA 0x4000010C` |
| `0x228` | AFE/analog-ready wait: sets handshake-page `+0` bit1 when the status bits in `0x4003943C`/`0x40039C3C` (bit11) clear |
| `0x278` | delay helper |
| `0x298` | bitfield merge helper: `((src>>a)&((1<<w)-1))<<s` into `[dst+off]` |
| `0x2E8` | apply table A (16 entries) + table B (14 entries) of `0x298` merges |
| `0x310/0x32C` | small analog-config helpers |
| `0x344` | analog/PHY bring-up + AFE wait + status reads (writes `0x400395xx/0x40039Dxx/0x40101264`) |
| `0x480` | return 0 iff 16-bit `CA 0x40000108 == 0x5A5A` (release-word predicate) |
| `0x498` | analog init + set page `+0` bit2 + poll `[0x40000108]==0x5A5A` |
| `0x4C4` | full cold-boot fallback: `0xBAAD` -> `0x344` -> page bit2 -> poll release -> `0xCEED` |
| `0x50C` | **primary (CPU0) boot decision**: page=0, read gate, 0xCEED -> 0x18, else fallback |
| `0x54C` | **primary entry**: call `0x50C`, re-read `[0x40000]`, `blx` entry A, spin |
| `0x564` | **secondary (CPU1) path = S1**: 0xCEED gate, 0xABCD magic, WFI, +8=2, entry B |

The two tables decode as a repeated 16-byte record `{src_shift, width, dst_offset,
dst_shift}`; e.g. table A entry 7 copies source bits 12-15 to `[dst+0x28]` bits 0-3. They are
used only by fn `0x2E8` inside the analog bring-up [V].

---

## 3. S0: the reset path

At ROM `0x8C` (reset vector target):

* `0x0C4` read SCTLR; `0x0C8/0x0CC/0x0D0/0x0D4` clear bits 12 (I-cache), 11 (Z), 2
  (D-cache), 0 (MMU/MPU enable); `0x0D8` write SCTLR; `0x0DC` read MPIDR, `& 3` [V].
* per-mode stacks, core-indexed (mode CPSR values `0xD2/0xDB/0xD7/0xD1/0xD3`):
  IRQ/UND/ABT/FIQ SP = `0x1BF110 - 0x80*core`; SVC SP = `0x1BEF00 - 0x1000*core` [V].
  Note these are the very RAM words the image later nominates as crash-dump region 0
  (`0x1BD000,0x2000`), and `0x1BF110` is inside the ROM<->image page's stack area.
* `0x15C-0x1B8` CP15 **c6/c2/c1 {0,2,4}** sequence: select region index (c6,c2,{0}), then
  base (c6,c1,{0}), size (c6,c1,{2}), attributes (c6,c1,{4}) for
  **region0 base 0 size 0x3F attr 0x30C**, **region1 base 0x40000000 size 0x2B attr
  0x1300**, **region2 base 0x80000000 size 0x3D attr 0x1300** [V]. The size fields decode as
  `2^(size+1)` -> 4 GB / 4 MB / 2 GB with the enable bit set, i.e. an ARMv7-R-style MPU
  (whole-space default, 4 MB IO at 0x40000000 marked XN/device, 2 GB RAM window) [I].
  The page/stacks therefore run with a normal-memory, non-cacheable default region - which is
  why the cross-core handshake works without cache maintenance.
* `0x1BC` clear-then-set SCTLR.M with a DSB, then `0x1CC` MPIDR dispatch:
  `MPIDR&3 == 0` -> `0x608 ldr pc -> 0x54D` (primary/CPU0); otherwise
  `0x610 ldr pc -> 0x565` (secondary/CPU1) [V].

---

## 4. S0b: the primary (CPU0) boot path and how the image is started

`0x54C -> 0x50C`:

1. `[0x1BF000] = 0`; read 16-bit `[0x4000010C]`.
2. If it is already `0xCEED`: store `0x18` (page bits 3,4) and go to the image. This is the
   warm path (the register persisted from a previous ROM pass) [V code / U persistence].
3. Otherwise (cold path, what all bench boots take):
   * `0x53A`: `[0x40000] = 0` - **clear the stale image header entry-A pointer**;
   * `[0x1BF000] = 1` (bit0 = "primary boot begun");
   * call `0x4C4`: `[0x4000010C] = 0xBAAD`; call `0x344` (analog/PHY init; inside it
     fn `0x228` sets page bit1 once the AFE status bits clear); `[0x1BF000] |= 4` (bit2);
     **poll 16-bit `[0x40000108]` until it equals `0x5A5A`** (the host release write);
     then `[0x4000010C] = 0xCEED`;
   * back in `0x50C`: `[0x1BF000] |= 8` (bit3 = "ROM gate passed") and return.
4. `0x54C`: **re-read `[0x40000]` after the release poll**. If it is nonzero, `blx` it -
   this is image entry A, CPU `0x46970` (Thumb) - then spin forever at `0x560`.

**Host consequence:** the upload must finish before the `0x5A5A` write; the ROM clears the
header at the start of the cold path and reloads the pointer only after the host unblocks the
poll. This matches the vendor kprobe order (chunks to CA `0x01240000` -> custom blocks ->
`0x40000108 = 0x5A5A`) [V\*].

---

## 5. S1: the secondary (CPU1) path - the exact +8=2 writer and its gate

ROM `0x564` (detailed listing in `romdis.py flow`):

1. `[0x1BF004] = 0`; `[0x1BF00C] = 0` (clear its own handshake status/magic words).
2. `0x592/0x598`: `r2 = 0x00040000` (**image header base**, cpu address `0x40000`).
3. Read 16-bit `[0x4000010C]`; record the result in page `+4`:
   `!= 0xCEED -> +4 |= 1`; `== 0xCEED -> +4 |= 8`.
4. If it was not `0xCEED`: `[0x40004] = 0` - **clear the stale image header entry-B
   pointer** (symmetric to CPU0 clearing entry A).
5. `[0x40160104] = 0xFF`, `[0x40160100] = 1` (CPU1 event-controller words); `+4 |= 2`
   ("CPU1 armed").
6. **WFI loop** (`0x5E2-0x5EE`): pass only when BOTH
   `[0x1BF00C] == 0x0000ABCD` and `[0x4000010C] & 0xFFFF == 0x0000CEED`.
7. On success:
   `[0x1BF004] |= 4`; **`strd {flags, 1<<core} -> [0x1BF004],[0x1BF008]`** at ROM
   `0x5FE`, i.e. **`[0x1BF008] = 2` on CPU1** - this is S1;
   then `r2 = [0x40004]` (entry-B pointer) and `blx r2` if nonzero - **CPU1 enters image
   entry B `0xC742C`**; otherwise spin forever at `0x606`.

The wake event for the WFI is the image's own **inter-core doorbell** `CA 0x40161F00 <-
0x00020000` (target core 1), rung by entry A immediately after it posts `0xABCD` [V code;
V\* golden contract]. The optional hook at `0x40000004` sometimes extrapolated from earlier
notes does **not** exist: the value is `0x00040000` (header base), not `0x40000000` [V].

### 5.1 Handshake page bit maps (all writers verified in ROM/image disassembly)

| page word | bit | writer | meaning | grade |
| --- | --- | --- | --- | --- |
| `+0x000` | 0 | ROM `0x53C` (val 1) | primary cold path begun | [V] |
| | 1 | ROM `0x228` (`0x270`) | AFE/analog ready | [V] |
| | 2 | ROM `0x498/0x4C4` (`0x4B8/0x4F2`) | analog init done | [V] |
| | 3 | ROM `0x542/0x548` or `0x18` path | ROM gate passed | [V] |
| | 4 | ROM `0x52C` (`0x18` path only) | gate was 0xCEED at first check | [V] |
| | 5,6 | image entry A `0x4697A/0x469A4` | image first acts | [V] |
| | 13 | image entry A `0x473C2` | handshake requested | [V] |
| | 14 | image entry A `0x473FC` | **handshake complete (+8==2 seen)** | [V] |
| | >16 | image init/helper bits (incl. 23,25,26,31) | image progress markers | [V\*] |
| `+0x004` | 0 | ROM `0x59E` | CPU1 first gate check was not 0xCEED | [V] |
| | 1 | ROM `0x5D2` | CPU1 armed / entering wait | [V] |
| | 2 | ROM `0x5FA` | **CPU1 handshake completed (+8 written)** | [V] |
| | 3 | ROM `0x5A2` | CPU1 first gate check was 0xCEED | [V] |
| | 4 | image entry B `0xC7432` | image-side flag | [V\*] |
| `+0x008` | - | ROM `0x5FE` only | `1<<core` = S1 ACK | [V] |
| `+0x00C` | - | image entry A `0x473D4` (0xABCD), ROM CPU1 clears | handshake magic | [V] |

### 5.2 Cross-validation against the bench (this is the strongest evidence)

The ROM decode **predicts** the previously unexplained pre-upload page that the GP4/GP5/GP6
sessions read after the endpoint pulse and before the release:

* `+0x000 = 0x00000007` = bit0 (boot begun) | bit1 (AFE ready) | bit2 (analog init done) -
  exactly the ROM state while CPU0 is parked in the `0x4C4` poll waiting for `0x5A5A` [V];
* `+0x004 = 0x00000003` = bit0 (first gate check != 0xCEED) | bit1 (armed) - exactly the ROM
  state while CPU1 is parked in its WFI loop [V];
* `+0x008` random, `+0x00C = 0` (CPU1's clear) - as observed [V\*].

After the release, GP2/GP6 read `+0 = 0x86803FEF` (bit14 clear) and `+4 = 3` - i.e. entry A
ran and spun at `[+8]==2`, with **CPU1 bit2 never set**. The vendor golden boot reads
`+0 = 0x86807FEF` (bit14 set) and `+4 = 7` (bit2 set) [V\*] - the same ROM path, completed.
The missing condition is therefore inside CPU1's loop, and the only value that can differ is
the gate at `0x4000010C`.

---

## 6. The gate register `CA 0x4000010C`: two incompatible handshakes

| consumer | value it needs | where |
| --- | --- | --- |
| ROM boot stage (CPU0 -> CPU1) | `0xCEED` | written by CPU0 ROM `0x4C4` at ROM `0x506`; read by primary `0x526` and secondary `0x58C/0x5EA` |
| image service/exception handshake | `0xCECE` | image `0xC20F8` (event-id 0x02 per-core service) spins until `[0x4000010C]==0xCECE`; the common exception handler `0xC6E14` zeroes `0x4000010C` and `0x40000108` then spins for `0xCECE`; the transport calls this the "D2H announce gate" |

The host transport's `pciet_d2h_gate_announce()` writes `0x0000CECE` to
`CA 0x4000010C` (BAR0 `0x3B810C`), and the bench service loops write it continuously
before and after the `0x5A5A` release [V code + V\* ledger]. Once that write lands,
CPU1's loop condition (iv) is false forever. The vendor host never writes `0xCECE`
(host-replica constant/behaviour census) and on the vendor boot the image takes no exception
(mask 0), so the image-side `0xCECE` consumer never executes and `0xCEED` survives [V\*].

**Consequence for prior lanes:** this also explains the earlier bench observation that
"no-gate-service" sessions reached `state=1`, `g valid`, `out[1]=0x40 (READY posted)`
while every gate-serviced session (GP3-GP6) stayed `state=0, mask=0` [V\*]: without the
`0xCECE` writes S1 completes, entry A leaves its spin and queues the boot task; with them it
never does. The page was simply never read in the no-gate runs.

---

## 7. The host action: definitive answer

**Yes, a host action exists, and it is subtractive.** Exact sequence (attended, recovery
armed):

1. **Pre-release (unchanged from GP4/GP5 best practice):** upload the firmware to
   CA `0x01240000` and seed the platform-custom block (CPU `0x1B2800`, BAR0
   `0x86A800`) / TCM windows (`0x1C0000/0x1D0000`) exactly as the GP4 session did. These
   feed entry A's CPU1-image handoff and are not the S1 gate itself.
2. **Release only:** write `0x00005A5A` to `CA 0x40000108` (BAR0 `0x3B8108`). This
   completes the ROM cold-path poll, which writes `0xCEED` and jumps to entry A.
3. **Do NOT write `0xCECE` to `CA 0x4000010C` during boot** (not before the release, not
   after it, not in a service loop). Keeping `0xCEED` is the whole action. If an
   image-side service/exception handshake is genuinely needed, write `0xCECE` only **after**
   S1 is confirmed complete (`page +8 == 2` or `page +0` bit14 set): CPU1 no longer reads
   the register after S1, so the write is then harmless.
4. **Optional direct repair:** if `+8` stays 0 while `+4` has bits 0/1, read 16-bit
   `CA 0x4000010C`; if it is not `0xCEED`, a host write of `0x0000CEED` to
   `CA 0x4000010C` (BAR0 `0x3B810C`) restores gate condition (iv). This is a diagnostic
   repair, not the primary plan.
5. **Do not fake the ACK** by writing 2 to `+8`: that only releases entry A's spin; CPU1
   has not run, entry B never executes, CPU1's TCM image never starts, and the progress word
   `+4` will lack bit2 (a cheap discriminator).

**Expected observables on success (read-only):**

| read | location | expected |
| --- | --- | --- |
| handshake page `+0/+4/+8/+C` | CA `0x1BF000` (BAR0 `0x877000`, phys `0x40877000`) | `0x86807FEF / 0x00000007 / 0x00000002 / 0x0000ABCD` |
| state | CA `0x118B60` | 1 (boot task ran) |
| exception mask | CA `0x40008` | 0 |
| TCM ITCM/DTCM word0 | CA `0x400000/0x410000` | `0xE59FF018 / 0x40034000` |
| pre-release page (sanity) | same page | `0x00000007 / 0x00000003 / random / 0` |

**What is not an action / must not be done:** writing `0xCEED` before the release (it makes
CPU0 take the warm path and can skip the `0x4C4` fallback with a stale header); writing
`0xCECE` continuously "to keep the D2H gate open"; writing the page `+8`; relying on a
ROM-internal CPU1 release register - there is none in the readable ROM, CPU1 comes out of
reset with the chip.

---

## 8. Image side: the wake/FIQ path (task item 4)

The ROM routes **IRQ directly to image `0x40064`**, and **FIQ through its own stub `0x7C`
into image header +0x50 = `0x103140`** [V]:

* `0x40064` (ARM) = `sub lr; srsdb; cps #0x13; push {...}; blx 0xC2EFC; ...; rfeia` - the
  IRQ second-level trampoline; its only callee is the **event dispatcher `0xC2EFC`** (Thumb:
  reads event-status word `CA 0x4016010C`, takes its low 10 bits as index into the handler
  table at `CA 0x17D398+0x98 = 0x17D430`, acks via `CA 0x40160110`). This is the layer all
  link/mailbox/H2D services are registered in.
* `0x103140` = the **FIQ handler** (not a "reset handler" as earlier notes had it) - the ARM
  srsdb/rfeia trampoline that `blx`-es the **other** dispatcher `0xC2EEC` (Thumb: loads the
  slot `CA 0x17D398+0x94` and `bx`-es it: the boot-task dispatch slot).
* The five header words are, in ROM-slot order, **{FIQ, data-abort, prefetch-abort, SVC,
  undefined}** = `{0x103140, 0x103114, 0x1030E8, 0x1030BC, 0x103090}`; the ROM slot table
  `0x1E0-0x1F0` was cross-checked against the image words [V].
* The ARM stubs all build a 17-word frame and branch through veneers `0x103B40/48/68/70` to
  Thumb thunks that set **r1 = 1 undefined, 2 SVC, 3 prefetch-abort, 4 data-abort**; the
  common handler `0xC6E14` sets exception-seen mask bit `1<<r1` at `CA 0x40008`
  [V] (see corrections).

**Does anything in the image complete the handshake? No.** Static census of the whole image:
no store to `+8/+C` other than entry A's single `strd {0,0xABCD}`; no writer of the value 2;
the image's only relationship to `CA 0x4000010C` is the `0xCECE` service spin; and it never
writes the host-only release register `0x40000108` (no aligned literal). The image *consumes*
the ROM's S1 ACK; CPU1 is then sent to entry B. The image reads CA `0x40000004` once as a
status word (function CPU `0x4587C`), unrelated to the ROM gate [V].

---

## 9. Corrections to prior lanes (contradictions resolved)

| prior claim | correction | evidence |
| --- | --- | --- |
| ROM word at `0x1FC` = magic `0x7386F44F` (data) | it is the **first Thumb instruction word** (`movw r3,#0x10C`) of the gate-read helper; the real table ends at `0x1F8` | [V] |
| handler-slot table includes `0x1F4/0x1F8` as exception slots | those are the **stack tops** `0x1BF110/0x1BEF00`; the 5 slots are `0x1E0-0x1F0` | [V] |
| header +0x50..+0x60 = reset/undef/svc/prefetch/abort | actual: **FIQ / data-abort / prefetch-abort / SVC / undefined** (proved by ROM vectors -> stubs -> slots -> header) | [V] |
| r1 ids: 1 = data abort, 2 = prefetch, 3 = svc, 4 = undef | **1 = undefined, 2 = SVC, 3 = prefetch abort, 4 = data abort** | [V] |
| bench mask `0x0A` = SVC + data abort ("store to ROM raised a data abort") | mask bit = `1<<r1`: `0x0A = 0x02|0x08` = **undefined + prefetch abort**; a data abort would set bit `0x10`. The abort/DR narrative should be re-examined with this mapping | [V] mapping / [U] consequence |
| IRQ vector target never read; "if it points into ROM the image dispatcher can never run" | **IRQ target = `0x40064`** (image trampoline), so the event wiring exists; FIQ = `0x7C` ROM stub -> `0x103140` | [V] |
| "reset handler 0x103140" | it is the **FIQ handler**; the ROM reset path never enters it | [V] |
| image header entries "consumed by the ROM/loader" (mechanism unknown) | mechanism: **entry A by CPU0 path `0x55E`; entry B by CPU1 path `0x604` after S1**; both header pointers are cleared on the cold path and re-populated by the upload | [V] |
| "ROM arms the reset slot `CA 0x17D42C` with `0x47629`" [U] | that slot is reached from the **FIQ** dispatcher `0xC2EEC`; what arms it is still [U], but it is not part of S1 | [U] |
| "host cannot complete S1 / S1 is ROM-internal, not host-triggerable" | **corrected:** S1 is host-unblockable - the host only has to stop writing `0xCECE` over the ROM's `0xCEED`; the no-gate bench sessions already show `state=1` | [V code + V\*] |
| S1 writer "mask-ROM code after entry A's wake doorbell" [I] | confirmed, and localised to **CPU1 ROM `0x5FE`**; the ACK value is `1<<core`, not a stage constant | [V] |
| pre-image page `{0x1BD000,0x1BD0D8}` / later `{7,3}` unexplained | explained by the ROM cold path + CPU1 arming bits (section 5.2); `{base,cursor}` was a different (pre-ROM-init) RAM state | [V] |

---

## 10. Bench verification plan (attended, read-only except the release)

One fresh GP2-style boot (single clean pulse), standard pre-release seeding (GP4/GP5 best:
platform block + TCM; the wifi-custom block is not required for S1), then:

1. Read pre-release page +0/+4/+8/+C (expect `7/3/random/0`) - confirms the ROM cold path.
2. Write `0x5A5A` to CA `0x40000108` (**the only write**). **Do not write `0xCECE`.**
3. Read the page at ~0.2/1/5/20 s: success = `0x86807FEF / 7 / 2 / 0xABCD`; then read
   `CA 0x118B60` (expect 1) and `CA 0x40008` (expect 0).
4. If `+8` is still 0: read 16-bit `CA 0x4000010C` (BAR0 `0x3B810C`). If it is
   `0xCECE` or anything else, the transport/service loop is still clobbering it; if the page
   `+4` has bit2 already set but `+8` is not 2, the ACK store was skipped for another
   reason and the ROM is vindicated.
5. Only after success, if the image-side D2H/exception gate is needed, write `0xCECE` to
   `CA 0x4000010C` and continue the existing transport/watch flow.

Safety: one 32-bit write to `CA 0x40000108` (BAR0 `0x3B8108`), plus reads. Do not touch
`CA 0x400392F0`, `CA 0x40160110`, or write the page/handshake words.

---

## 11. Negative results / do-not-chase

* There is **no ROM code that releases CPU1 through a host-visible register**: the only ROM
  writes outside the page/stacks are the analog block (`0x40039xxx`), `0x40101264`, and the
  CPU1 event words `0x40160100/104`; none is a CPU-release control and none is host-gated.
* There is **no hidden ROM body** in `0x81C-0x3FFF` (all zero) and the S1 logic is fully
  decoded; a second ROM dump is not needed for S1.
* The "continuation hook" at `CA 0x40000004` does not exist (it is image header +0x04 =
  entry B).
* `CA 0x1BF008 = 2` cannot be produced by any image store (single-writer census) and faking
  it does not release CPU1.
* The 0xCECE gate is not a boot prerequisite of the firmware: the vendor boot never writes it
  and has mask 0. It is an image-side exception/service handshake only.
* Do not re-run the "gate service during boot" pattern; that is the bug this lane identifies.

---

## 12. Evidence table (new claims of this lane)

| # | claim | method | grade |
| --- | --- | --- | --- |
| R1 | ROM effective size 0x81B, `0x81C-0x3FFF` zero/RAZ, banner `Hi1152V100CS 2022-09-21` | full dump scan + `romdis.py info/selftest` | [V] |
| R2 | vectors: reset `0x8C`, IRQ -> `0x40064` (image), FIQ -> `0x7C` -> header +0x50 | vector/slot/header cross-check | [V] |
| R3 | handler-slot map `0x1E0-0x1F0` -> header offsets `0x60/5C/58/54/50`; `0x1F4/0x1F8` = stacks | disassembly + image header | [V] |
| R4 | S0 reset: SCTLR off, per-mode stacks `0x1BF110/0x1BEF00`, CP15 c6 region config (4GB/4MB/2GB), SCTLR.M, MPIDR split | instruction decode | [V] |
| R5 | primary path: clear `[0x40000]`, `0xBAAD`->PHY->page bit2->poll `[0x40000108]==0x5A5A`->`0xCEED`->page bit3->re-read header->`blx` entry A | instruction decode | [V] |
| R6 | secondary path: clear `+4/+C`, clear `[0x40004]` on non-0xCEED, event words, arm, WFI on `0xABCD & 0xCEED`, `+4|=4`, `+8=1<<core`, `blx` entry B | instruction decode | [V] |
| R7 | the only ROM page writers are the sites in `romdis.py stores`; no ROM write to `0x40000108` | constant-tracked store census | [V] |
| R8 | pre-upload bench page `{7,3,random,0}` is exactly the ROM cold+armed state | ROM arithmetic + [V\*] data | [V]/[V\*] |
| R9 | golden page `{0x86807FEF,7,2,0xABCD}` is the same ROM path completed | ROM arithmetic + [V\*] | [V]/[V\*] |
| R10 | `CA 0x4000010C` is a dual-use 16-bit gate: ROM `0xCEED` vs image `0xCECE` | ROM disassembly + image `0xC20F8`/`0xC6E14` + transport source | [V] |
| R11 | transport writes `0xCECE` during boot (bench service loops and `pciet_d2h_gate_announce`) | transport source + ledger [V\*] | [V] |
| R12 | the host action = release-only, no `0xCECE` during boot (plus optional `0xCEED` repair) | R8-R11 + no-gate bench state=1 observation | [I high] / bench-testable |
| R13 | image header handler order and r1 ids corrected (undef=1, svc=2, prefetch=3, abort=4) | ROM vectors -> stubs -> header -> veneers -> thunks | [V] |
| R14 | bench mask `0x0A` = undefined+prefetch (not svc+data-abort) | R13 + mask store at image `0xC6E90` | [V] |
| R15 | FIQ path = `0x103140` -> `0xC2EEC` (boot-task slot); IRQ path = `0x40064` -> `0xC2EFC` (handler table `0x17D430`) | image disassembly | [V] |
| R16 | image never writes +8=2 / `0x40000108`; it consumes the ACK and reads `CA 0x40000004` status | image census + ROM decode | [V] |
| R17 | CPU1 is dispatched into image entry B after S1 (not into a `0x40000004` hook) | ROM `0x592/0x598` header-base arithmetic + `0x5F4/0x604` | [V] |

---

## 13. Open items ([U])

1. Persistence semantics of `CA 0x4000010C` across chip resets (AON register? RAM?): the
   warm path `0x52C` (`0x18` page state) was never observed; it decides whether a bare
   second reset can skip the cold fallback.
2. Exact WFI wake semantics: the loop is re-entered by the image's doorbell; whether a host
   write to the event controller could also wake CPU1 is unknown and unnecessary.
3. How the CPU1 entry-B stage relates to the TCM ITCM/DTCM image (entry A copies the blobs
   into `0x400000/0x410000` before the handshake; entry B presumably configures/moves on).
4. Banner chip naming ("Hi1152V100CS") vs the board's Hi5622V100 module naming - [U].
5. The core profile (ARMv7-R MPU reading of CP15 c6) is an inference [I]; the S1 logic does
   not depend on it.
6. Whether the mask `0x0A` undef+prefetch events change the abort-RCA conclusion of the
   earlier lanes - flagged for that lane, not re-analysed here.

---

*Lane: wifi-rom-dump, 2026-11-23. All contents are derived addresses, constants, structure
layouts and decoded instructions. The device ROM dump and the vendor firmware remain local
evidence only; nothing vendor is committed. Device untouched by this lane.*
