# REPORT - the SMAC PPDU-completion producer: the IRQ handler that builds and posts the group-2 id-3 (RX-complete) message

**Lane:** `wifi-rx-smac-complete` (task-221). **HOST-ONLY** decode of the pinned
`FIRMWARE.bin` (928,920 B, sha256 `7fc87e20...c3ef311b`; **CA = file offset + 0x40000**).
Companions: [README.md](README.md), [TABLES.md](TABLES.md), [PROBES.md](PROBES.md),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [tools/](tools/), [evidence/](evidence/).
Tool: `--selftest` 197/197 PASS. Grades: **[V]** re-derived here, **[V\*]** committed earlier
lane/bench, **[I]** inference, **[U]** needs a device read.

---

## 0. Verdict

1. **The producer is found and is unique: the common SMAC interrupt body `0xB53A4`**,
   registered for **band 0 IRQ 49** (wrapper `0xB54C8`) and **band 1 IRQ 55** (wrapper
   `0xB54BC`) by the per-band init `0xD1B68` (`0xC6DA2(irq, prio, handler)` + `0xC6FF4(irq)`
   enable, 8 sources per band, priority 5). It is the **only** code in the image that builds a
   group-2/id-3 HCC message: all 56 `hcc_msg_tx` (`0x102760`) callers were dataflow-scanned and
   exactly one (`0xB5492`) sets group nibble 2 and the u16 id 3 [V].
2. **The dispatch chain from the hardware IRQ to that body is decoded [V]:**
   ARM exception vector `CA 0x40064` (`sub lr,lr,#4; srsdb; cps; push; blx 0xC2EFC`) ->
   event dispatcher **0xC2EFC**: pending id = `word[0x4016010C] & 0x3FF`, bound `0x5F`,
   handler = `[0x17D398 + 0x98 + 4*id]` (= `0x17D430 + 4*id`), call it, ack by writing the
   pending word back to `0x40160110` -> wrapper (loads the per-band object from RAM `0x180814+8`
   or `+0x1C`) -> body `0xB53A4`.
3. **The body's work is the PPDU completion -> RX-frame chain -> group-2 id-3 message -> post [V]:**
   status gate -> entry counter `band_obj+0xCC0`++ -> dequeue a completion entry
   (`0xB084C(band_obj+0x348, &out, 3)`) -> `0xB0C88` received-frame chain ->
   unlink the PPDU descriptor from `band_obj+0x66C` (`0xB2BD8`) -> SMAC ack `[base+0xe4]=4` ->
   allocate a 20-byte message (`0x102822(0x14)`) -> fill HCC header
   (`hdr[0]&0xf=2`, `u16 hdr[6]=3`, `hdr[2]=u8[band+2]`, `hdr[3]=0x12`, `hdr[0xc]=frame chain`,
   `hdr[0x13]=1`, `hdr[0x14]=band object`, `hdr[0x1d]=0`) -> post via **`0x102760`** ->
   re-arm a fresh 0x198-B PPDU descriptor (`0xB52F0(band_obj, band_obj+0x320, node)`).
4. **Consumer cross-check [V]:** the committed group-2 table CA `0x10CC18` entry id 3 ->
   handler `0x000BBC9D` (`0xBBC9C`), registered by the boot task at `0x48338` (registrar
   `0x102866`, group 2, 19 entries); dispatcher `0xFD7E8` takes group = `u8[hdr]&0xf` and
   id = `u16[hdr+6]`; the handler `0xBBC9C` reads `[hdr+0x14]` (band object) and
   `[hdr+0xc]` (frame chain) - **exactly the two payload fields this producer writes** - and
   runs `0xBA1C8 -> 0xB9780` to build the group-1/id-13 CRX D2H per frame.
5. **Host-visible state:** the INTC/event controller (`0x4016xxxx`) is outside the host
   region-3 window and is **not host-readable**, so "is the RX event enabled at the INTC" cannot
   be read from the host. But the firmware itself enables all 16 registered SMAC IRQs at boot
   (`0xC6FF4`, W1S at `0x40161100 + bank*4`), and the **MAC-level enable/status words
   `0x40108214/0x4010820C` (band 0) and `0x4010C220/0x4010C200` (band 1) ARE host-readable**
   (device-IO window, phys = CA + 0x3B8000), as are the SMAC status/ack words reached through
   `[band_obj+0x2a0]`. The producer writes `4` into the ack word `[base+0xe4]`; that word
   changing is the first host-visible proof that the ISR ran. The mask-bit -> INTC-id mapping
   is **not encoded in the image** ([U]); `0x4010C220 |= 0x1029` (bits 0,3,5,12) and
   `0x40108214 |= 0xF` (bits 0..3) are the only MAC-mask writes, and **whether the RX-complete
   source bit is among them cannot be decided statically** - PROBES P5/P6/P8/P9 settle it.
6. **Why the counters stayed 0:** the bench's "0 before/after" F1 datum covers the *consumer*
   counters (`+0xCC5`, `+0x1AD4`, `+0x1ADA`; committed pack). This lane adds the missing
   producer-side observables and ranks the gates; the F1 is now "which gate in this body fired
   or did not fire", not "where is the producer". Gate list in sec. 5, decision table in
   [FIRE-RECIPE.md](FIRE-RECIPE.md).

---

## 1. IRQ dispatch: from the ARM vector to the per-band SMAC body [V]

| stage | CA | facts |
| --- | ---: | --- |
| IRQ vector stub | **0x40064** | ARM `sub lr,lr,#4` (word `0xE24EE004`), `srsdb`, `cps #0x13`, `push`, then `blx` at `0x40094` -> `0xC2EFC` |
| event dispatcher | **0xC2EFC** | `ldr r7,=word[0x4016010C]`; id = `r7 & 0x3FF` (ubfx at `0xC2F08`); bound `0x5F`; `handler = [0x17D398 + 0x98 + 4*id]` (base literal at `0xC3014`, +0x98 added); call; ack = write pending word to `0x40160110` (literal at `0xC3020`) |
| handler registrar | **0xC74B0** | stores `[0x17D398 + 0x98 + 4*irq] = handler` (literal `0x17D398` at `0xC7534`); wrapper `0xC6DA2` validates irq <= 0x5F then tail-calls it |
| enable | **0xC6FF4** | validates id 0x10..0x5F, sets the W1S bit `1<<(irq&31)` at `0x40161100 + (irq>>5)*4` (+0x100 mirror) - literal at `0xC7044`; called for every registered source from the per-band init |
| per-band registration | **0xD1B68** (caller `0x48314`) | for band 0 (`u8[ctx+0x11d]==0`): IRQ 47,49,50,51,52,82,83,59; band 1: 53,55,56,57,58,84,85,60; each `0xC6DA2(irq,prio,wrapper)` then `0xC6FF4(irq)` |
| wrappers | **0xAEC94 / 0xB54C8 / 0xB0D50 / 0xAEC68 / 0xAEB74 / 0xAE48C / 0xB0C44** (band 0), `+(-0x0C)`-style twins for band 1 | `ldr r3,=0x180814; ldr r0,[r3,#8|#0x1C]; b <body>` - the per-band SMAC object comes from RAM `0x180814+8` (band 0 = 0x174BF0) / `+0x1C` (band 1 = 0x175A18) |

The full IRQ -> wrapper -> body table (8 rows x 2 bands) is in [TABLES.md](TABLES.md) T1; every
row is asserted by `--selftest`.

## 2. The producer body `0xB53A4` (band 0 IRQ 49 / band 1 IRQ 55) [V]

Instruction-level sequence (all rows asserted; CA in TABLES T2):

```
B53A4  push.w {r0..r8,lr}
B53A8  r3 = [band_obj + 0x2a0]          ; SMAC register/context base
B53AE  r3 = [r3 + 0xe8]                 ; SMAC status word
B53B2  lsls r3, r3, #0x1d ; bmi B53FC   ; bit 2 SET -> early return (no counter, no ack)
B53BE  u8[band_obj + 0xcc0]++           ; entry counter (new probe P5)
B53C8  bl 0xBFD4C
B53D0  r0 = band_obj + 0x348 ; r2 = 3
B53D4  bl 0xB084C                       ; dequeue a completion entry; r != 0 -> log 0x2a1, ack 4, return
B53F4  [ [band_obj+0x2a0] + 0xe4 ] = 4  ; SMAC ack for source bit 2 (new probe P6)
B5402  bl 0xB0C88                       ; received-frame chain pointer (r6)
B5406  r7 = r0 - 8                      ; list node
B5410  r0 = band_obj + 0x66C
B5414  bl 0xB2BD8                       ; unlink the PPDU descriptor from the alloced-PPDU list
B5418  [ [band_obj+0x2a0] + 0xe4 ] = 4  ; ack again
B5424  r0 = 0x14 ; bl 0x102822          ; allocate the 20-byte message
B545E  hdr = 0x409EA(msg)
B5462  hdr[0] low nibble = 2            ; group 2
B546E  hdr[6] = 3                       ; id 3  (hdr[7] = 0 so u16 hdr[6] = 3)
B5472  hdr[1] high nibble = u8[band_obj]        ; destination nibble
B547C  hdr[2] = u8[band_obj+2] ; hdr[3] = 0x12
B5488  hdr[0x14] = band_obj             ; band object pointer (consumer reads this)
B548C  hdr[0xc]  = frame chain          ; received-frame chain (consumer reads this)
B5492  bl 0x102760                      ; post the HCC message (hcc_msg_tx)
B5458  bl 0xB52F0                       ; re-arm: reserve + link a fresh 0x198-B PPDU descriptor
```

Failure-path logs use firmware line numbers 0x2A1 (dequeue empty/failed), 0x257/0x275 (message
alloc/post failed) via `0x41CDE/0x41F90`; the failure-path counter is `band_obj+0xCCE`.

**Message destination/semantics note [V decode, I semantic]:** the destination nibble is copied
from `u8[band_obj]` (the same byte the consumer's resolver `0xA4FC8` reads as chip id), and
`hcc_msg_tx` resolves it against the global HCC instance `0x171B6C` (resource map
`+0x448 + 0x18*dest`, group resolver `0x102744`). The RX-complete consumer `0xBBC9C` is the
firmware's group-2/id-3 handler; the payload match above is the link evidence. Whether the
posted message is dispatched in firmware or forwarded to the host through the same HCC layer is
the one remaining direction question - the fire recipe's P0 (D2H catalogue: is a group-2 id-3
frames seen?) plus P5/P6 settle it; the payload-field match is too specific to be accidental
either way.

## 3. Producer uniqueness [V]

Method: full Thumb BL sweep -> 56 callers of `0x102760`; for each caller a 70-instruction
window is scanned for (a) `movs rY,#2` followed by `bfi rZ,rY,#0,#4` (group nibble 2) and
(b) `movs rX,#3` followed within 3 instructions by `strb rX,[hdr,#6]` (id 3). Exactly one
caller matches: **`0xB5492`** (the producer body). The other SMAC bodies build their own
messages: body E (`0xAEA9C`, IRQ 52/58) emits group-2 id `0x23` on its status path, body D
(`0xAEB80`, IRQ 51/57) a 6-B ring-walk D2H, body C (`0xB0CE0`, IRQ 50/83 | 56/85) a 16-B D2H
class - none of them posts group-2/id-3. The per-body identity table is TABLES T1.

## 4. The consumer chain, re-derived from the raw image [V]

| stage | CA | facts |
| --- | ---: | --- |
| group-2 table | **0x10CC18** | 19 x 12-byte `{u32 id; u32 handler; u32 aux}`: id 2 -> 0xB9780 (CRX builder), **id 3 -> 0xBBC9C** |
| registration | **0x48338** | `r2=0x13; r1=0x10CC18; r0=2; bl 0x102866` (the boot-task HCC registrar) |
| dispatcher | **0xFD7E8** | header via `0x409EA`; group = `u8[hdr]&0xf`; id = `u16[hdr+6]`; resolver `0xFD4C8` (12-byte group records -> 16-byte dispatch items); `bx handler` |
| handler | **0xBBC9C** | NULL guard; `r5=[hdr+0x14]`; resolver `0xA4FC8(u8[r5],u8[r5+1])` -> chip; splices the `[hdr+0xc]` chain into a local list; `0xBA1C8` |
| per-frame | **0xBA1C8** | once per received frame: `bl 0xB9780` at `0xBA48A` |
| CRX build | **0xB9780** | pops received-frame nodes (`0x1024EA`/`0x1024E6`), builds the group-1/id-13 D2H (`hdr[6]=0xd` at `0xBA152`), deposits via `0x102760` at `0xBA17A`; gates: per-chip RX ctx `[chip+0x1a00]` (`0xB97EE`), forward flag, total cap 0x680 |

## 5. The gates that decide "why never fires" (ranked)

| # | gate | decode | probe | if it is the blocker |
| ---: | --- | --- | --- | --- |
| G0 | event not delivered: MAC-level mask bit for the RX-complete source not enabled (or INTC side) | INTC enable written at boot for all 16 registered ids incl. 49/55 by `0xC6FF4` [V]; host cannot read it | P1-P3/P8/P9 (mac masks readable; INTC not) | the only host-visible discriminator is the mask word; mapping bit->id is [U]; escalate to image-patch lane if stock-read masks are identical and the ISR still never runs |
| G1 | body entry gate: bit 2 of `[ [band_obj+0x2a0] + 0xe8 ]` is SET -> immediate return before any counter | `0xB53B2/0xB53B4` [V]; polarity/mask-vs-status semantics [U] | P4 before/after the burst | read the word; if bit2 is stuck 1 the ISR can never run -> the blocker is the SMAC/interrupt state feeding it (band context attach, see G1b) |
| G1b | wrong SMAC context base: band 1's `[band_obj+0x2a0]` still holds the boot device base `0x40062000` instead of an attached SMAC RAM context (Lead bench: band 0 +672 = `0x00114F18`) | init writes `0x40042000/0x40062000` at `0xD2046/0xD2094` [V]; attach paths [V\*] smac-start | P2/P3 | 5G chain: the 5G SMAC context is not attached; 2.4G is (the F1 runs on 2.4G, so G1b is not the 2.4G blocker) |
| G2 | completion dequeue empty/failed: `0xB084C(band_obj+0x348, &out, 3)` returns non-zero | `0xB53D4`, log 0x2a1 [V] | P5 (entry counter: increments only when G1 passes), P11 (PPDU list population) | the MAC completed no PPDU into this ring: break is between the MAC PPDU completion write and this ring (before the message build) |
| G3 | message alloc/post failed | `0x102822`, `0x102760` return paths; counter `+0xCCE` | P5/P6 + firmware log via D2H g4 id0 | low-probability; the failure path logs |
| G4 | consumer gates (after a successful post): RX ctx NULL `0xB97EE`, forward flag `0xBA0FC`, cap 0x680 | committed wifi-rx-mac-delivery [V] | P10 (+`+0xCC5/+0x1AD4/+0x1ADA`) | if P5/P6 move and P10 does not: the break is the consumer's own gates - next rung is a firmware-log capture |

**The important new fact for the F1:** the producer's own counter `band_obj+0xCC0` and its SMAC
ack word `[base+0xe4]` are **new, cheap, read-only observables** that the bench did not have.
They split "the ISR never entered" (G0/G1) from "the ISR entered but the consumer/its gates are
the break" (G2-G4) in one pass.

## 6. Host-visible state - direct answer to the task's question

* **INTC:** the event controller is at CA `0x40160000..0x40161FFF`; the host region-3 window
  covers CA `0x40000000..0x4011FFFF`, so the host **cannot read the INTC**. [V\* tbtt lane,
  re-checked: `0x4016xxxx` literals exist only in the firmware's own dispatcher/init code]
* **Is the RX event enabled at the INTC?** The firmware enables every registered source,
  including IRQ 49 (band 0) and IRQ 55 (band 1), at boot through `0xC6FF4` inside the per-band
  init `0xD1B68`; the enable bit is a W1S at `0x40161100 + bank*4`. So the INTC side is
  enabled by construction **if the per-band init ran**. That call chain (0xD1B68 registers and
  enables the 16 IRQs; the same function calls the PPDU-list init 0xC016C at 0xD3BAC) is proven
  to have run on the bench by the committed PPDU-list name-word read (P1 of wifi-ppdu-init),
  so the INTC enable writes executed too. [V decode + V* bench]
* **Does the MAC mask `0x4010C220 |= 0x1029` include the RX-complete IRQ?** Statically
  **undecidable from this image [U]**: the mapping MAC-enable-bit -> INTC event id is hardware.
  The image contains only two MAC-mask write sites: MAC start `0x4AF0C` -> band 1
  `0x4010C220 |= 1|8|0x20|0x1000 = 0x1029` (bits 0,3,5,12) and band 0 `0x40108214 |= 0xF`
  (bits 0..3), plus the stop path that clears bits 0..3. Probe P8/P9 reads both registers live;
  P5/P6 then show whether the RX source (IRQ 49/55) is actually being serviced. If the mask
  equals the expected values and PM P5 stays 0 while P7 moves, the mask-bit hypothesis is the
  single best remaining cause and the next rung is a firmware-side enable/patch decision, not
  another host frame.

## 7. Limits

* The receiver of the posted message (firmware-local dispatch vs host-forwarded HCC frame) is
  the one direction ambiguity; the payload-field match with the firmware's own group-2/id-3
  handler is [V], the transport direction is [I]. PROBES P0 (D2H catalogue) decides it.
* The polarity of the SMAC status word `[base+0xe8]` (bit set = pending vs masked) is [U];
  the instruction-level fact is "bit2 set -> the body returns" [V].
* All runtime values are [U] until the Lead's read-only pass; this lane touches no device.
