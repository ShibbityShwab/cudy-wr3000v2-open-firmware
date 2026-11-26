# REPORT - lane wifi-smac-start (task-215)

All firmware addresses are CPU addresses (CA = file offset + 0x40000 of the
pinned FIRMWARE.bin, 928,920 B, sha256 7fc87e20...311b). Host read paths:
device IO phys = CA + 0x3B8000; firmware RAM phys = 0x40000000 + CA + 0x6B8000.
Grades: **[V]** re-derived by `tools/fw_smac_start_re.py`, **[V*]** committed
earlier lane/Lead, **[I]** inference, **[U]** needs a device read.

## 1. The bench datum that reframes the problem [V*]

2026-11-26 11:30, 2.4 GHz differential fire (Lead):

| item | working 2.4 GHz AP | silent armed 5 GHz AP |
| --- | --- | --- |
| SMAC wrapper CA 0x180814 +8 / +0x1C | band0 obj 0x174BF0 / band1 obj 0x175A18 | same |
| timer word 0x401015CC | 0 | 0 |
| interval word 0x400024B0 | 0x64 | 0x64 |
| per-band SMAC object +672 | 0x00114F18 (RAM block) | **0x40062000 (device-IO default)** |
| [ctx+672]+228 / +232 | 0x00A00096 / 0 | 0 / 0 |

Consequence: the tbtt-timer lane's "0x401015CC = the beacon timer latch"
hypothesis cannot be the 5 GHz discriminator - the *working* band has the same
0/0x64 words. The measured discriminator is the +672 SMAC-context pointer.
Section 3 decodes what sets it.

## 2. The timer words 0x401015CC / 0x400024B0 - complete writer/reader decode [V]

Literal scans of the whole image (both 16-bit `ldr` and 32-bit `ldr.w`
literal encodings) give the complete reference set:

| CA | site | code |
| --- | --- | --- |
| 0x897EE | 0x897D6 stop helper | strh r3(=0) -> 0x401015CC; save/restore 0x400024B0 from RAM 0x1664C8 |
| 0x89814 | 0x8980C start helper | movs r2,#1; strh r2 -> 0x401015CC; r3 = 0x400024B0; save old interval; strh 100 |
| 0x9F436 | per-link init A | str r5 (32-bit count) -> 0x401015CC; then 0x400024B0 = 100 |
| 0xA0052 | per-link init B | str 0 -> 0x401015CC (clear) |
| 0xA02A0 | per-link init C | str r4 (count) -> 0x401015CC; then 0x400024B0 = 100 |
| 0xA047A | per-link init D | str 0 -> 0x401015CC (clear) |

The start/stop helpers are ref-counted by a byte at per-band-object +796
(start: skip if non-zero, else write 1 and increment; stop: decrement, and on
zero write 0 and restore the saved 16-bit interval). Their direct callers are
**exactly two functions** [V]:

* CA 0xE8670 (per-band MAC/SMAC configure; it also selects the band MAC-start
  register 0x4011080C / 0x4011480C by the object's band byte at +1); start at
  0xE868E, stop at 0xE8804; caller 0xDC74A (function CA 0xDC6FC).
* CA 0xF3B22 (per-link TBTT/timing config; programs the per-band TBTT
  register slots 0x40101584/0x40101588/0x40101594/0x4010159C/0x401015A4/
  0x401015AC with the interval-derived value and selects 2.4 vs 5 GHz by the
  frequency argument, 2400 = 2.4 GHz); start at 0xF3C02, stop at 0xF3D1C; four
  call sites 0x9C974/0x9C9A4/0x9C9B6/0x9C9E6 in the CA 0x9C76C per-link state
  machine.

So 0x401015CC/0x400024B0 are a **TSF/timer latch pair used around TBTT timing
programming**, not the beacon-enable latch. The bench datum proves that both
bands reach the same latch state; nothing on the host side can change it
(no g0-id1 key resolves to these functions - task-214 already proved the
dispatch tables contain no timer worker).

## 3. The per-band SMAC object init - CA 0xD1B68 [V] (the lane's key finding)

CA 0xD1B68 is the boot SMAC/PHY init (single direct caller CA 0x48314). It:

1. memsets 7,272 bytes at **0x174BE0** (the whole band-object region);
2. sets `r4 = 0x00174C30` and loops over the band instances `r7 = 0 .. n-1`
   with `n = byte[0x174BEE]`, stride **0xE28** (`addw r4, r4, #3624`),
   re-entering the body at 0xD1CB6. For band0 r4 = 0x174C30 = **(wrapper
   band0 object 0x174BF0) + 0x40**; for band1 r4 = 0x175A58 = 0x175A18+0x40;
3. per band instance sets (offsets relative to r4):

| field | value band0 | value band1 | note |
| --- | --- | --- | --- |
| +608..+644 (8 words, step 0x2000) | 0x40042000 | **0x40062000** | device-base chain A; +608 stores the base (0xD2046/0xD2096) |
| +648..+708 (8+ words, step 0x800) | 0x40080000 | 0x400B0000 | device-base chain B (0xD20FE) |
| +720 / +724 | 0x4010A000 / 0x40109000 | 0x4010E000 / 0x4010D000 | SMAC sub-bases (0xD1E44 / 0xD2180) |
| +576 / +580 / +584 / +588 / +592 / +596 / +600 | 0x114F18 / 0x114F18+488 / 0x115128 / 0x183438 / 0x1817A0 / 0x180904 / 0x180E68 | same set | **RAM block attach** (0xD2CA2..0xD2CC4), after a 32-byte memset of the same field group (0xD2C9A) |

Note the address relation that makes the Lead's datum meaningful: with
`obj = wrapper+8` and `r4 = obj + 0x40`,
`r4 + 608 = obj + 672` and `r4 + 576 = obj + 640`. The band-1 object's
**+672 therefore holds 0x40062000 by construction** - exactly the silent-band
value - while the working band-0 +672 holds 0x114F18, which is the value the
same init writes to **r4+576 (obj+640)**, i.e. the first RAM-block pointer.
So: the silent band-1 SMAC context still holds the boot device-base default;
the working band-0 SMAC context holds an attached RAM-block pointer. The
residual question - whether the Lead's working-band read of "+672" is in fact
the same field as the firmware's r4+576/608 pair (a 0x20 offset), or whether
band0's +672 is overwritten after boot - is the one item the next read-only
fire must settle (probe both offsets; FIRE-RECIPE R7).

Also [V]: the RAM block at **0x114F18** is a real writable RAM block, not a
constant: the init fills 61 x 4 u16 entries into it from tables 0x1B2A87 /
0x1B2AAC (0xD2BD8..0xD2C50), and later rate/power code reads it as a u16 table
(0x85EF2, 0xA5414, 0xA54D2, 0xA5652, 0xA5770, 0xDA824). Its initial image
content at 0x114F18..0x115100 is the 0x0096 fill; the live +228 word 0xA00096
is consistent with a firmware write at +230. Blocks 0x115128/0x183438/
0x1817A0/0x180904/0x180E68 are the rest of the per-band block set.

### 3b. The status word 0x00A00096 (Lead's [ctx+672]+228 read) [V/I/U]

The common ISR bodies treat the +228/+232 pair as the SMAC mask/ack words, not
as a control structure: body CA 0xAE45C does
`r3 = ([ctx+700] & 0x01000200) & ~[ctx+672]+232` and writes it to
`[ctx+672]+228` (0xAE468/0xAE46A/0xAE470/0xAE474). So +228 is the ack/mask
word and +232 the clear word. The working band's live value 0x00A00096 =
bits 0x800000 | 0x200000 | 0x96 (0x80|0x10|0x04|0x02): the static bit
assignment needs either a live before/after read of an ISR (R7 #10/#11) or a
per-bit experiment, so it stays **[U]**; it is recorded as a liveness
signature, not as a start condition. The image content of the 0x114F18 block
at +228/+232 is 0x00960096, so only the +230 u16 changed (0x0096 -> 0x00A0)
- consistent with a live firmware write, recorded as evidence that the block
is attached and used.

## 4. The band-object accessor [V]

CA 0xF4F48: `movw r0,#3624 ; ldr r3,=0x174BF0 ; mla r0,r0,r1,r3` =
`band_object(band) = 0x174BF0 + 0xE28*band`. Band dispatch word CA 0x180814:
+8 = 0x174BF0 (band0), +0x1C = 0x175A18 (band1). All SMAC ISR wrappers load
this word and pass the object to the common bodies; every common body reads
the context pointer from **[object + 672]** (verified in 0xAE45C, 0xAEB80,
etc.), then uses [+228]/[+232] mask/status. A band whose +672 is a device base
instead of an attached RAM block therefore has its ISR/common-timer/TBTT
context pointing at the wrong place - the exact silent-5G symptom.

## 5. Candidate causes and discriminating probes

C1. **Host step lacking.** The stock boot sends 25 distinct g0-id1 keys
(FAMILY-TABLE); our documented ladder uses 9, so 20 keys are stock-only.
Candidates and workers include 0x01CF->0x84D14, 0x03E4->0x84260,
0x0073->0x84920, 0x0000->0x840B4, 0x0069->0x839B4, 0x0005->0x82878 (all in
the 0x84xxx hmac-config family, the same family as 0x0127 set_beacon and
0x0124 channel). [I] one of these may be the per-band SMAC context
attach/enable our ladder lacks.
C2. **Firmware-internal attach condition.** The attach is inside the same
boot init loop as the device chains; if the loop/attach is re-run or gated for
the active band only, band1 can keep the boot default. The no-host-message
verdict of task-214 is consistent with C2.
C3. **Band-1 start order/side effect.** The band-1 MAC start (0x4AF0C via
0x0242) pulses resets (0x40114500 = 0x55 then 0xFF) but touches no SMAC
context field; C3 is only plausible if a reset clears an attached context and
the attach is not re-done for band1.
C4. **Firmware patch hypothesis.** If R7 shows band1's context stays at the
boot default after a correct 2.4 GHz-style bring-up, the only remaining fix is
a firmware image change (or a host write of the context fields through the
firmware-RAM window - a high-risk path that would need its own safety lane).

## 6. Negative results [V]

* No host g0-id1 key maps to the timer writers, the SMAC init (0xD1B68), the
  band object accessor or the attach code.
* The MAC start 0x4AF0C / band start 0x971D8 do not touch 0x401015CC,
  0x400024B0 or the +672 context field (task-214 + this lane).
* The timer words are identical on the working and silent bands (bench datum).
* The 0x401015xx TBTT block writes in 0xF3B22 do not include 0x401015CC.
* `smac_tbtt_5g_isr` / `smac_tbtt_2g_isr` are named handlers outside the 8
  registered INTC sources per band - the TBTT is delivered by firmware
  timer/task context, not by the host-postable INTC path.
