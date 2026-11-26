# REPORT - the firmware band-1 beacon-TX gate: names, tables, entry/registration and the one per-band state a register copy cannot reproduce

**Lane:** wifi-5g-tx-scheduler (shared task **task-231**). **HOST-ONLY**:
static decode of the pinned local vendor artifacts; no device contact, no
write, no fire. **The Lead fires.** Companions: [README.md](README.md),
[TABLES.md](TABLES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[tools/fw_tx_scheduler_re.py](tools/fw_tx_scheduler_re.py), evidence/.

Grades: **[V]** re-derived here by the committed tool from the pinned
artifacts; **[V\*]** bench-measured by a committed lane or the Lead;
**[I]** inference (provenance given); **[U]** needs a device read.
Firmware CPU address convention: CA = file offset + 0x40000 of the pinned
FIRMWARE.bin (928,920 B, sha256 7fc87e20...311b). Host read paths:
firmware RAM phys = 0x40000000 + CA + 0x6B8000; device IO phys =
0x40000000 + 0x3B8000 + (CA - 0x40000000).

---

## 0. TL;DR

1. The **45-name blob** is fully enumerated (T1): 13 `smac_msg_proc_*` names +
   32 per-band ISR names (16 x 5g/2g). It is **dead static data** in this
   build: no u32 literal, no movw/movt constant equals any name CA [V].
2. The names still have meaning: they name, in the same order, two static
   pointer tables in the same section - **CA 0x10B058** (32 thumb ISR handler
   pointers; idx14/15 = `smac_tbtt_5g_isr`/`smac_tbtt_2g_isr`) and
   **CA 0x10B0DC** (10 SMAC message-processor pointers, incl. the four beacon
   TX resume/suspend processors and the enable/disable-TRX worker CA 0xE8520)
   [V]. Neither table is referenced by any image code [V]; the live
   registration is done with direct literals by the boot init.
3. **There is no firmware beacon-build/queue task.** The TBTT handlers are
   6/11-instruction leaf stubs with no BL and no device access; they only copy
   per-band timer/queue counters into an event message [V]. The beacon frame is
   staged by the host-key VAP-start path in the per-link object and transmitted
   by hardware once the per-band gate is open [V/I].
4. **The live entry/registration is boot init CA 0xD1B68**: it registers
   **8 IRQ lines per band** with per-band wrapper functions that select the
   band object from CA 0x180814 (+8 = band0, +0x1C = band1) and tail-branch
   into 7 shared bodies [V]. Band0 IRQ 47/49/50/51/52/82/83/59, band1 IRQ
   53/55/56/57/58/84/85/60. The install store is
   `[0x17D398 + 0x98 + 4*irq]` in RAM - host-readable. **Band-1's ISR entry
   is registered identically to band-0's** [V]: registration is not the gap.
5. **The one per-band gate a register-value copy cannot reproduce** (because it
   is message-driven, not value-driven):
   * **0x40101220** - per-band SMAC/TX enable; **bit0 = 5 GHz, bit1 = 2.4 GHz**
     (MAC start CA 0x4AF0C writes 1/2 per band) [V].
   * **0x40101208** (= 0x40101220 - 24) - per-band suspend latch;
     5 GHz bits **0x1|0x4**, 2.4 GHz bits **0x2|0x8** [V bits, I semantics].
   The only writers in the image are four SMAC message processors:
   CA 0xE6A26 resume-5g / 0xE6A70 suspend-5g / 0xE6AB4 resume-2g /
   0xE6B00 suspend-2g [V]. No host g0-id1 table A/B worker reaches them [V].
6. **Ranked cause for the silent 5 GHz beacon (C1):** the band-1 gate is
   closed - `0x40101220` bit0 clear and/or the `0x40101208` bits 0x1/0x4
   set - because the firmware's internal band-start path completed on 2.4 GHz
   but not on 5 GHz. A host copy of every other band-0 value cannot fix that.
   R0 (read-only) decides it in one pass; R1 arms the two gate words directly.
7. **Correction to the TBTT hypothesis:** 0x401015CC is **not** the TBTT
   timer/arm. It is one half of an enable/delay register pair
   (0x401015CC / 0x400024B0) used by the per-band MAC init and by two small
   helper routines (CA 0x897D6 release / 0x8980C assert + save/override)
   [V pair, I clock/power semantics]; the working 2.4 GHz band leaves it 0
   [V\*], so arming it was never going to start TBTT.

---

## 1. Method and inputs

| item | value |
| --- | --- |
| firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...311b`; CPU = file + 0x40000 |
| disassembler | the OpenWrt lab ARM objdump (override `--objdump` / `$BW_OBJDUMP`), `-M force-thumb` |
| committed anchors | ../wifi-fw-beacon-tx (VAP start / per-link beacon state), ../wifi-fw-tbtt-arm (per-band blocks, G hooks), ../wifi-fw-callback-install (G slot model), ../wifi-5g-smac-keys (dispatch tables A/B, chain bases), ../wifi-tbtt-timer, ../wifi-5g-dfs-relax |
| tool | `tools/fw_tx_scheduler_re.py`; `--selftest` = **128/128 PASS** (evidence/selftest.txt) |

The decode is a static, address-anchored read: every table row and every
instruction asserted below is printed by the tool from the pinned image; the
tool stores no vendor byte and commits only derived values.

## 2. The 45-name blob (CA 0x12266C..0x122BA4) [V]

The blob is a contiguous string section, 45 NUL-terminated names, in this
order: **14 ISR-pair names** (7 pairs: `coex_rx_abort_end`, `rx_complete`,
`tx_complete`, `common_timer`, `coex_pta_rx_abort_timeout`,
`coex_tx_abort_end`, `coex_pta_tx_abort_timeout`), then **13 SMAC
message-processor names**, then **18 ISR-pair names** (9 pairs: `tbtt`,
`rx_timeout`, `pre_tx`, `backoff_timeout`, `user_queue_empty`,
`user_ps`, `tx_abort_start`, `lifetime_expire`, `tx_exit`) - 32 ISR
names total (16 x 5g/2g pairs). The full CA list is TABLES T1.

Layout details: every name is a plain ASCII run; the entry before
`smac_pre_tx_5g_isr` carries 3 non-text pad bytes (0x01 0x02 0x03) at
0x122A41..0x122A43, the name starts at 0x122A44 [V]. The blob is preceded by
a table of 0x00/0x01 bytes and followed by two static tables (section 3).

**The names are dead static data in this build.** A full u32-literal search
plus a movw/movt constant reconstruction finds **no reference to any name CA**,
and no code reference to either following table's base [V]. They are the
build's source-order name tables, not runtime-installed objects. (The prior
lane `wifi-fw-beacon-tx` checked only the blob base; this lane checks every
name CA and both tables.)

## 3. The two static handler tables (same section) [V]

**CA 0x10B058 - 32 thumb handler pointers, ISR-name order** (T2).
The 32 entries are 16 adjacent per-band pairs; the pairing is [V] (each pair
is a 5g/2g twin - e.g. idx22/23 call the twin helpers 0xBEB50/0xBEAE8 and
idx18/19 are 5g/2g, and the neighbouring message table's four beacon
processors are likewise 2+2), while *which name* belongs to each entry follows
the blob order [I: the 5g entries are the odd/even half of each pair, the blob
lists 5g first]. Index 14/15 are the TBTT pair:
`smac_tbtt_5g_isr` -> CA **0xE69CA**, `smac_tbtt_2g_isr` -> CA **0xE69A2**.

**CA 0x10B0DC - 10 thumb SMAC message-processor pointers** (T3). The block
contains, in entries 3..6, the four beacon TX resume/suspend processors
(CA 0xE6B00 / 0xE6AB4 / 0xE6A70 / 0xE6A26) and in entry 2 the enable/disable
TRX worker CA **0xE8520**. The other entries (0xE70ED, 0xDB7A1, 0xDB719,
0xE8214, 0xE83AC) are the remaining message processors; the [U] part is which
of the last two is the disable-TRX twin (the blob names `enable_trx_req` and
`disable_trx_req` but only one worker is decoded).

## 4. The live entry/registration: boot init CA 0xD1B68 [V]

The image's boot init block (CA 0xD1B68, called from the pre-HCC boot body -
same family as the G installer CA 0x5B424) contains, after the per-band object
init, **two unconditional registration blocks** selected by a band argument:

* band0 (selector 0) at 0xD1EE8: IRQ **47, 49, 50, 51, 52, 82 (flags 14),
  83, 59**;
* band1 (selector != 0) at 0xD1D2: IRQ **53, 55, 56, 57, 58, 84 (flags 14),
  85, 60**.

Each call is `CA 0xC6DA2(irq, flags, handler)` followed by
`CA 0xC6FF4(irq)` (enable). The handler argument is a literal from the pool
immediately after the block (T4). The handlers are **per-band wrapper
functions**: each is 3 instructions -

```
wrapper_band1:  ldr r3, =0x180814 ; ldr r0, [r3, #0x1C] ; b <shared body>
wrapper_band0:  ldr r3, =0x180814 ; ldr r0, [r3, #0x08] ; b <shared body>
```

- so CA **0x180814 + 8** is the band0 object pointer and **+0x1C** the band1
  object pointer, and the 8 lines per band resolve to only **7 shared
  bodies** (0xAE45C, 0xAEA9C, 0xAEB80, 0xAEC74, 0xB0AB8, 0xB0CE0, 0xB53A4;
  body 0xB0CE0 is registered twice per band). The corresponding band0/band1
  wrappers are exact twins (T4) [V].

The install function CA 0xC74B0 stores the handler at
**`[0x17D398 + 0x98 + 4*irq]`** (base literal CA 0xC7534 = 0x17D398, store
`str.w r8,[r3,#0x98]` at CA 0xC74E8). That RAM table is host-readable, so
the live per-band registration can be verified on device (R0, FIRE-RECIPE).

**Conclusion for the task's item (1):** the per-band ISR entry exists and is
registered identically for both bands; 5 GHz's TBTT line is IRQ 84 in the
same wrapper family as 2.4 GHz's IRQ 82. The registration is not the
band-1-specific failure.

## 5. What the beacon-TX path actually is [V/I]

End-to-end, after the host messages of the committed lanes:

```
host g0-id1 keys -> firmware VAP start (worker CA 0x46394)
  -> per-link beacon state: +0x6B8 frame buffer (280 B),
     +0x6BC aux (translated bus address at aux+0),
     +0x6C0 descriptor (92 B), +0x6C4 = 16, +0x6C6 = 1   [V, beacon-tx lane]
  -> band start CA 0x971D8 -> G hooks (optional) -> 0x5FBC0   [V]
firmware per-band MAC init / MAC start (CA 0x9F2C6 / CA 0x4AF0C)
  -> 0x40101220 = 1 (5 GHz) or 2 (2.4 GHz) enable bit       [V]
hardware TBTT
  -> per-band IRQ (84 / 82) -> wrapper -> shared body
     -> per-band source handlers from CA 0x10B058 family
        - tbtt: 6/11-insn leaf reporting stub, no TX            [V]
  -> beacon frame transmitted by the MAC from the staged per-link
     state, while 0x40101220 bit set and 0x40101208 suspend bits clear  [I]
firmware SMAC internal messages
  -> beacon_tx_resume/suspend processors toggle the gate words  [V]
```

So the "band-1 TX scheduler/beacon task" the bench hypothesis looked for is
**not firmware code**: the firmware has no beacon-build/queue task in this
build. The firmware's per-band control surface is the gate word pair plus the
per-link state staged by the host-key path. The gate words are toggled only by
firmware-internal SMAC messages, and the host cannot post those (T9) [V].

## 6. The per-band gate (the new finding) [V]

Four SMAC message processors are the only writers of the gate pair:

| handler CA | role | effect on the gate words |
| --- | --- | --- |
| 0xE6A26 | resume 5 GHz | `0x40101220 |= 0x1` ; `0x40101208 &= ~(0x1|0x4)` |
| 0xE6A70 | suspend 5 GHz | `0x40101220 &= ~0x1` ; `0x40101208 |= (0x1|0x4)` |
| 0xE6AB4 | resume 2.4 GHz | `0x40101220 |= 0x2` ; `0x40101208 &= ~(0x2|0x8)` |
| 0xE6B00 | suspend 2.4 GHz | `0x40101220 &= ~0x2` ; `0x40101208 |= (0x2|0x8)` |

All four use 16-bit accesses (`ldrh/strh`) to 0x40101220 and to
`[r2,#-24]` = 0x40101208 [V]. They are invoked with an SMAC message in r1
and bail out with status 35629 if `msg[2] != 0` (the "already in this state /
reject" path) [V].

Band-to-bit mapping is fixed by MAC start CA 0x4AF0C: it reads
`u8(perlink+0x11D)`, and writes **1** into 0x40101220 for band 1 / **2** for
band 0 [V] - the same convention as the prior lane. The suspend word is at
0x40101220-24 and uses the doubled bit pair per band; the semantics
"resume clears / suspend sets a latch" is [I] from the four bodies, but the
bit positions and the writes are [V].

Second per-band gate (already known, now placed on the same message surface):
the enable/disable-TRX worker CA 0xE8520 takes the band from `u8(msg+0xA)`
and writes band1 `0x4010E150/0x4010E154/0x4010E158` vs band0
`0x4010820C/0x40108214` [V]. It is message-table entry 2, i.e. also an
internal SMAC message, not a host key [V].

## 7. Why the band-1 beacon can stay silent (ranked)

The bench has already shown that every *host-writable/host-visible element*
equals the working 2.4 GHz element, so a pure register-value difference is
excluded. The gate model adds the missing distinction: the firmware must
*run a message handler* on the band, and a register copy does not run it.

| # | cause | test | grade |
| --- | --- | --- | --- |
| **C1** | the 5 GHz gate is closed: 0x40101220 bit0 clear and/or 0x40101208 bits 0x1/0x4 set, because the firmware's internal band-start path never posted/consumed 5 GHz `beacon_tx_resume` | R0 read 0x40101220 + 0x40101208 (one read each, both bands in the same boot); 2.4 GHz is the reference | [V] bits, cause [I] |
| **C2** | the 5 GHz TRX gate is closed: 0x4010E150/0x4010E154 not at the enable-worker values, because `enable_trx_req` never ran on band 1 | R0 reads those words too | [V] regs, cause [I] |
| **C3** | the per-band hardware beacon/TBTT base was never brought up for band 1 (band-1 SMAC chain base 0x40062000 never live); the host sees all registers equal but the band-1 engine never ran | R0 reads the live IRQ table (registered identically) + gate words; if both gates are open and the beacon is still absent, this is the remaining layer | [U] |
| **C4** | the staged per-link beacon state is not consumed: the 92-byte descriptor (+0x6C0) / translated address (+0x6BC) never reaches a hardware DMA fetch | R0 reads perlink+0x6B8/+0x6BC/+0x6C0/+0x6C6 (prior lane values) plus the gate words | [U] |

The cheapest discriminator is **C1** and it is new: two read-only words answer
it, and R1 is a two-word host fix if C1 holds. If C1 is refuted (gate already
open), the gate model says the failure is downstream in the band-1 engine
(C3/C4) and no host message in the known families can fix it - the stock path
difference is then in the firmware's per-band bring-up, which needs the stock
5 GHz boot capture to diff.

## 8. Correction: 0x401015CC is not the TBTT arm [V pair / I semantics]

The bench attacked the TBTT by writing 0x401015CC; the working 2.4 GHz band
keeps it 0 [V\*]. Decode: 0x401015CC is written only by
(a) the small helpers CA 0x897D6/0x8980C and (b) the per-band MAC init
CA 0x9F2C6 (release at 0x9F436, deassert at 0xA047A) - and in both cases
together with 0x400024B0. The helper pair implements
"assert 0x401015CC=1, save 0x400024B0, set it to 100, later restore and
deassert after a countdown at [ctx+796]" (CA 0x8980C asserts; CA 0x897D6
restores and clears) [V]. That is a clock/power/analog enable + settle-delay
pattern [I], not a TBTT timer: the working band leaves it deasserted, and no
TBTT handler touches it [V]. The TBTT is the hardware MAC's own mechanism; the
firmware-visible knob is the 0x40101220 enable bit + the hardware's own
TBTT/time base programming in the per-band MAC init.

## 9. Negative results [V]

* No u32 literal and no movw/movt constant equals any of the 45 name CAs, the
  ISR table CA 0x10B058, or the message table CA 0x10B0DC: the tables and names
  are not installed by any instruction of this image (the live registration is
  the direct-literal boot init of section 4).
* No g0-id1 table A (217 rows) / table B (88 rows) worker is any of the four
  gate processors, the TRX worker, or any of the 45 handlers: the host cannot
  post any of these SMAC messages through the config-event surface.
* The TBTT handlers contain no BL and no device-register literal (no beacon
  build/queue/arm inside the ISR).
* Neither 0x401015CC nor 0x400024B0 is read/written by any TBTT/beacon handler;
  the 2.4 GHz working band keeps 0x401015CC = 0 [V\*].

## 10. Open items [U]

1. Exact identity of CA 0xE8214 and 0xE83AC in the message table (one is the
   disable-TRX twin; the blob has both `enable_trx_req` and
   `disable_trx_req`).
2. Where the 32-handler source table is copied at run time, if at all (the
   static table is unreferenced; the source dispatch may be code-inline).
3. The producer of the internal `beacon_tx_resume` message (firmware-side
   queue/state machine; not reachable by a host key).
4. Whether the hardware beacon TX needs any further MAC register beyond the
   0x40101220/0x40101208 pair and the per-link staged state (settled by R0/R1).
5. The live values of 0x40101220 / 0x40101208 on the stock 5 GHz boot
   (the reference the R0 pass compares against is the working 2.4 GHz band in
   the same boot).

## 11. Reproduce / verify

    .provision-venv/bin/python tools/fw_tx_scheduler_re.py --selftest
    .provision-venv/bin/python tools/fw_tx_scheduler_re.py --decode
    .provision-venv/bin/python tools/fw_tx_scheduler_re.py --tables
    .provision-venv/bin/python tools/fw_tx_scheduler_re.py --probes

Evidence: [evidence/selftest.txt](evidence/selftest.txt) (128/128 PASS),
[evidence/decode.txt](evidence/decode.txt),
[evidence/tables.txt](evidence/tables.txt),
[evidence/probes.txt](evidence/probes.txt).
Nothing vendor is committed; only addresses, table words, mnemonics, register
constants and probe addresses above.
