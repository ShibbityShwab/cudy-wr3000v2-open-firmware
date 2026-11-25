# REPORT - the 5 GHz TBTT arm: per-band enable bits, the beacon-state path, and what the firmware does not do

**Lane:** wifi-fw-tbtt-arm (shared task **task-193**), 2026-11-28.
**HOST-ONLY**: static decode of the pinned local artifacts; no device contact,
no write, no fire. **The Lead fires.** Companions: [README.md](README.md),
[TABLES.md](TABLES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[tools/fw_tbtt_arm_re.py](tools/fw_tbtt_arm_re.py), evidence/.

Grades: **[V]** re-derived here by the committed tool from the pinned artifacts;
**[V\*]** bench-measured by a committed lane or the Lead; **[I]** inference;
**[U]** still needs a device read. Firmware CPU address convention:
CA = file offset + 0x40000 of the pinned FIRMWARE.bin (928,920 B, sha256
7fc87e20...311b). Host read paths: firmware RAM phys = 0x40000000 + CA +
0x6B8000; device IO phys = CA + 0x3B8000.

## 0. TL;DR

The "TBTT arm" is **not** a single firmware MMIO write that the image can be
made to perform by another H2D frame. What the image does have is a set of
**per-band enable bits** (start/stop functions CA 0x4AF0C / CA 0xBE294 and the
band-1 TRX-enable worker CA 0xE8520), a **beacon state + frame/queue chain**
built entirely without MMIO by the VAP-start worker CA 0x46394, and four
beacon-named smac message processors that toggle 0x40101220/0x40101208 but are
**not reachable from any g0-id1 key** ([V] negative). The R0 readback
0x548/0x54C = 0x6101 is the fixed band-1 init constant, not the arm bit ([V];
the previous lane listed it as a key state, this lane narrows it). The decisive
next step is therefore a **read-only differential**: the per-band start/enable/
TRX registers plus both per-link beacon objects, band 0 (working) vs band 1
(silent), in the same boot. FIRE-RECIPE R3 lists the ~120 reads (no writes).

## 1. Inputs, method, and the R0 facts this lane builds on

| item | value |
| --- | --- |
| firmware | `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`, 928,920 B, sha256 `7fc87e20...311b`; CPU = file + 0x40000 |
| disassembler | the OpenWrt lab ARM objdump, `-D -b binary -m arm -M force-thumb --adjust-vma=0x40000` |
| R0 probe facts (Lead, 2026-11-28) | band-1 per-link +0x6C4 = 16, +0x6C6 = 1, beacon buffer pointer nonzero; band-1 MAC block 0x548/0x54C = 0x6101; G = 0x1747C4 nonzero, G+0x374 = 0x00057639, G+0x370 = 0 (NULL by design) |
| sibling lanes cited | ../wifi-fw-beacon-tx, ../wifi-fw-callback-install, ../wifi-fw-desc-table, ../wifi-fw-desc-pool, ../wifi-fw-rf-descriptor, ../wifi-fw-dfs-worker |

## 2. The per-band MAC block: complete write inventory, and why 0x548/0x54C is not the arm

Band 0 is device CA **0x40110000**, band 1 **0x40114000** (delta 0x4000) [V].
Each block is initialised from a **23-entry `{u16 offset, u16 value}` table**
at CA **0x105714** (band0) / **0x105770** (band1), applied by the loops at
CA 0x9F3D6/0x9F3DA (band0, base 0x40000000+0x110000) and CA 0x9F91E/0x9F922
(band1, base 0x40004000+0x110000) [V].

* The **complete offset set** of both tables is
  **{0x548, 0x54C, 0x550, 0x554, 0x55C, 0x560, 0x564, 0x568, 0x588, 0x594,
  0x5BC}** - there is no 0x500-0x544 entry and nothing above 0x5BC [V].
* Every value is one of {0x0000, 0x0001, 0x0002, 0x0003, 0x0011, 0x0022,
  0x0024, 0x0033, 0x0035, 0x0101, 0x6000, 0x6001, 0x6101} [V]. **No
  beacon-interval, DTIM, TSF or time value appears anywhere in either table.**
* The band-1 difference is exactly 10 pairs; the final writes are
  0x548 = 0x6101 and 0x54C = 0x6101 (band0: 0x0101). The extra bits are
  **0x6000**, the firmware's own band-1 marker (also present in the
  `0x401145BC = 0` band-id write) [V].
* Other writers of the same block are all fixed-constant: the reset/check
  helpers 0xD968C (band1) / 0xD97BC (band0) write 4,12,0x4C,0xCC,0xCD,0xCF,
  0xDF,0xFF and verify 0x51C/0x520 = 0xFF; the SMAC/MAC0 checks 0xD9604 /
  0xD973C write 16/24/56/0x1038/0x1039 to 0x4010C220 and 4/5/7/0xF to
  0x40108214; the restore CA 0xD9504 writes only 0x1000/0x3000/3/0x55/0xFF
  plus the fixed per-mode list (TABLES T2/T3) [V].

**Consequence.** A "TBTT timer" cannot be identified by reading this block:
whatever the R0 probe sees there (including 0x6101) is the table default.
The remaining candidates are the enable/IRQ/TRX registers below and the
firmware-internal timer/ISR path, which has no MMIO footprint in the image
(§5). That is exactly why R3 is a differential, not a register-name hunt.

## 3. The per-band start/stop functions: the only per-band enable bits

### 3.1 Start CA 0x4AF0C [V]

Takes (band object, per-link-ish request); main path:

~~~
4AF5C  band = u8(perlink+0x11D)                 ; 1 -> band1 branch, else band0
band1 branch:
4AF6A  base = 0x40114500                        ; reset word
4AF72  write 0x55      ; 4AF84 write 0xFF      ; reset pulse
4AF90  check 0x4011451C == 0xFF ; 4AF98/4B06C  check ... 0x40114520 == 0xFF
4AFB0  ; 0x4010C220 |= 0x0001
4AFC0  ; 0x4010C220 |= 0x0008
4AFCA  ; 0x4010C220 |= 0x0020
4AFD4  ; 0x4010C220 |= 0x1000        -> band-1 SMAC IRQ enable = 0x1029
band0 branch (0x4B07C):
4B07C  base = 0x40110500 ; same 0x55/0xFF reset ; check 0x4011051C/0x40110520
4B0C4  ; 0x40108214 |= 0x0001
4B0D6  ; 0x40108214 |= 0x0002
4B0E0  ; 0x40108214 |= 0x0004
4AFD4  ; 0x40108214 |= 0x0008        -> band-0 enable = 0x000F
shared:
4AFF8  ; set bit0 of 0x4011080C (band0) / 0x4011480C (band1)
4B000  band = u8(perlink+0x11D) ; 1 -> value 1, else value 2
4B012  ; 0x40101220 |= that value
~~~

The band-word asymmetry is real: **band 1 arms through the SMAC IRQ block
0x4010C220 (bits 1|8|0x20|0x1000), band 0 through the MAC0 block 0x40108214
(bits 0..3)** [V]. The start also has an early-out when the request byte
perlink-ish+1 is already 2 or +4 is 0xFF, i.e. a second start request for the
same link is a no-op [V].

### 3.2 Stop CA 0xBE294 [V]

Mirror image: clears 0x4010C220 bits 1|8|0x20|0x1000 (band1) or 0x40108214
bits 0..3 (band0), clears bit0 of 0x4011080C/0x4011480C, and writes 0 to the
band reset word. This is the only code that disarms those bits.

### 3.3 The band-1 TRX-enable worker CA 0xE8520 [V]

This is the smac message processor for enable/disable-TRX (the name blob lists
`smac_msg_proc_enable_trx_req` / `disable_trx_req`). Body fields: byte +0xA =
band index (1 = the 5 GHz band), byte +0xE = module/on flag. For band 1 with
the enable flag it writes **0x4010E150 = 0x33, 0x4010E154 = 0xFF,
0x4010E158 = 0xFF**; the disable path zeroes 0x4010E1B8..+0x1C and programs the
SoC INTC 0x40002264/0x40002268/0x4000226C (checks 0x4000006C) [V]. Band 0
counterparts use 0x4010820C/0x40108214 (the same registers the start function
ORs) [V]. **This is the second concrete 5 GHz enable state that can be missing
on a live boot while every previous R0 probe still looks healthy.**

## 4. The beacon state and the frame/queue path: no MMIO anywhere

VAP start CA 0x46394 (the first start of a VAP on a band) [V]:

~~~
464F2  if u8(perlink+0x6C6) == 1: skip allocation   ; already has beacon state
464FA  buf  = alloc(0x118 = 280 B)   -> perlink+0x6B8
46524  desc = alloc(0x5C  =  92 B)   -> perlink+0x6C0
46588  perlink+0x6C4 = 16
46596  perlink+0x6B8 = buf
46590  perlink+0x6C0 = desc
4659A  perlink+0x6C6 = 1
466CE  CA 0xB5290(perlink, beacon buffer, &stack)     ; frame template
466E8  CA 0xB7208(module ctx, 0x310-byte frame, 1, 14)
466F8  CA 0xB3018(perlink, beacon buffer, frame, &stack2) ; build/queue
4670C  CA 0xEE394(buf+32) -> stored in *perlink+0x6BC
464B0  CA 0x971D8(bandobj, ctx)                       ; band start
~~~

The whole function range CA 0x46394..0x467C8 contains **no pc-relative literal
load of any device-IO address** (tool-checked) [V]. The same holds for the band
start CA 0x971D8..0x9744C [V]. So the beacon TX is not switched by an MMIO
write in this path: the firmware stages a 280-byte buffer and a 92-byte
descriptor and hands them to code in the 0xBxxxx region; either the per-band TX
engine consumes them after its enable bits are set, or nothing transmits.

The `0x971D8` band-start tail does read and call the runtime hook table
**G = 0x1747C4** (`G+0x370` = MAC start, `G+0x374` = beacon start) with the
VAP ctx [V]. G+0x370 is NULL by design (../wifi-fw-callback-install [V\*]),
G+0x374 = 0x57639 is installed at boot and was called on the R0 boot (Lead
[V\*]). Decoding 0x57639 shows a locked list walk that ages 184-byte entries -
it is **not** a beacon-start MMIO routine [V], which further supports
"the arm is not in this hook".

## 5. The only "beacon-enable bits" in the image - and why no H2D frame posts them

The image carries exactly four beacon-named handlers, all smac message
processors (name blob ordinals 20/21, `smac_msg_proc_beacon_tx_resume_req` at
CA 0x122904, `_suspend_req` at CA 0x122928) [V]:

| worker | body gate | register effect |
| --- | --- | --- |
| CA 0xE6A26 | requires body[2] == 0 | 0x40101220 |= 0x0001 ; 0x40101208 &= ~(0x1|0x4) |
| CA 0xE6A70 | requires body[2] == 0 | 0x40101220 &= ~0x0001 ; 0x40101208 |= (0x1|0x4) |
| CA 0xE6AB4 | requires body[2] != 0 | 0x40101220 |= 0x0002 ; 0x40101208 &= ~(0x2|0x8) |
| CA 0xE6B00 | requires body[2] != 0 | 0x40101220 &= ~0x0002 ; 0x40101208 |= (0x2|0x8) |

`[r2,#-24]` in each worker is 0x40101220 - 0x18 = **0x40101208** [V]. The two
bits of 0x40101220 are used elsewhere as the per-band "MAC running" bits: start
CA 0x4AF0C ORs 1 for band 1 and 2 for band 0 (§3.1) [V]. Interpretation
(labelled [I] because the mapping of the four workers to band/operation is not
provable from the image alone): the four workers are the resume/suspend pair
for the two bands; a "suspended" band would have its bit cleared in
0x40101220 and its 0x40101208 gate bits set.

**Negative result [V]:** no g0-id1 table-A (217 rows, CA 0x10C23C) or
table-B (88 rows, CA 0x103BF8) key resolves to any of the four workers. The
same holds for the enable-TRX worker 0xE8520. They are smac-internal message
processors, not host g0-id1 config events; **no frame our driver already
builds can post them.**

## 6. Correction: CA 0x7E5CC is not a beacon descriptor

../wifi-fw-beacon-tx [V\*] listed CA 0x7E5CC as a "beacon TX descriptor
refresh" (VAP+0x138 -> desc+0x25E, VAP+0x13A -> desc+0x260). This lane finds
[V]:

* CA 0x7E5CC has exactly **one caller in the image: CA 0x72184**, inside the
  **rate-adaptation function CA 0x70490** (it calls CA 0xD6A1E STA lookup,
  CA 0x6BB60 rate API, CA 0x6C8F8, and consumes rate objects).
* The filled descriptor fields are the rate/retry/protection fields
  desc+0x254..0x272 (from the rate object at r0+44/+45/+40/+62/+66/+132/...)
  plus VAP+0x138/+0x13A at +0x25E/+0x260 and VAP+0xF8 at +0x262. This is the
  TX-info block, **not** the 92-byte per-link beacon descriptor at
  perlink+0x6C0, and it is not an arming/validity gate.
* Therefore the phrase "beacon descriptor validity (+0x25E/+0x260)" from the
  task description does not describe a beacon gate; those two fields are the
  MD/protection fields copied into any TX info by the rate-control path.

## 7. What distinguishes the working 2.4 GHz TBTT from the silent 5 GHz one

At the register level the two bands are enabled through **different registers**
(§3): band 0 -> 0x40108214 bits 0..3, band 1 -> 0x4010C220 bits
1|8|0x20|0x1000, plus the band-1-only TRX triple 0x4010E150/0x4010E154/
0x4010E158 and the shared 0x40101220 bits 1/2 + 0x4011080C/0x4011480C bit0.
Band 0 works, so its chain (start bits + TX engine + firmware timer/ISR) is
proven on this boot. For band 1 the chain can break at any of three stages, and
each stage has a distinct read-only signature (TABLES T7, FIRE-RECIPE R3):

| stage | probe | healthy signature |
| --- | --- | --- |
| MAC start ran | 0x40101220 bit0, 0x4011480C bit0 | both 1 |
| band-1 IRQ enable armed | 0x4010C220 | contains 0x1029 |
| band-1 TRX enabled | 0x4010E150/54/58 | 0x33 / 0xFF / 0xFF |
| beacon state object built | perlink[1]+0x6B8/+0x6C0/+0x6C4/+0x6C6 | nonzero / nonzero / 16 / 1 (R0: yes) |
| beacon descriptor staged | 92 B at *(perlink[1]+0x6C0) vs band 0 | same shape as band 0 |
| beacon TX gate / timer | 0x40101208 bits 0..3 vs band 0 | equal to the working band-0 mask |

Ranked remaining causes for the silent 5 GHz beacon:

| # | cause | read-only discriminator | grade |
| --- | --- | --- | --- |
| C5a | the per-band MAC start/enable was never armed for band 1 (0x4AF0C never ran for its link, or a stop cleared it) | 0x40101220 bit0 / 0x4011480C bit0 / 0x4010C220 | [I] |
| C5b | band-1 TRX enable triple missing (enable-TRX worker never ran for band 1) | 0x4010E150/54/58 | [I] |
| C5c | beacon descriptor not staged/queued on band 1 although the state object exists | 92-byte descriptor diff vs band 0 | [I] |
| C5d | beacon TX suspend gate left set for band 1 | 0x40101208 bits 0..3 diff vs band 0 | [I] |
| C4 | per-band VAP hints (VAP+0x34C.b3, +0x360.b2/b3, +0x58 bit2) wrong for the 5 GHz VAP | VAP flag diff band 0 vs band 1 | [U] (host-side, cheap) |

The order is deliberate: C5a/C5b are one read each, and they are *new* probes
that no earlier pack contained; C5c/C5d need the two-object diff.

## 8. Limits / honest unknowns

* The TBTT ISR entries stay [U]: the 45-name blob CA 0x12266C..0x122BA4 has no
  code pointer and no code reference (re-verified here; ../phase8 and
  ../wifi-fw-desc-pool found the same). The firmware-internal timer that
  triggers TBTT is therefore not addressable offline; the image only shows the
  per-band enable state that gates the engine.
* The four beacon/TRX message processors cannot be posted by the g0-id1 family
  we use [V]; whether they are reachable through the WAL group-2 carrier is
  [U] and was not settled here.
* The internal meaning of 0x40101208 bits 0..3 (per-band beacon-TX vs generic
  TRX gates) is [I]: the four workers are the only beacon-named code that
  writes them, and the image carries no enum.
* The "band 0 is the reference" assumption is safe only while the 2.4 GHz AP is
  actually beaconing on the same boot (it was, per the project state). If R3 is
  fired on a boot where band 0 is not up, the differential loses its reference.
* Nothing here authorises a write. R3 is read-only; any follow-up that writes
  the per-band enable registers would be an unvalidated firmware-RAM/MMIO poke
  and needs its own attended lane.

## 9. Reproduction

~~~
./tools/fw_tbtt_arm_re.py --selftest   # 103/103 PASS, exit 0
./tools/fw_tbtt_arm_re.py --decode     # = evidence/decode.txt
./tools/fw_tbtt_arm_re.py --tables     # = evidence/tables.txt
./tools/fw_tbtt_arm_re.py --probes     # = evidence/probes.txt
~~~

The pinned artifacts are local and uncommitted (`--fw` / `--objdump` / 
`$BW_FIRMWARE` / `$BW_OBJDUMP` override the paths).
