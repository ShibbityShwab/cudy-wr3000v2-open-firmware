# REPORT - the 5 GHz TBTT/beacon-timer arm: what arms it, what does not, and what to read

**Lane:** `wifi-tbtt-timer` (shared task **task-214**). **HOST-ONLY**: decode of the
pinned local vendor firmware image + the committed stock-capture evidence. No
device contact, no flash, no fire.

Grades: **[V]** re-derived by this lane's committed tool; **[V*]** committed
earlier lane; **[I]** inference; **[U]** needs a device read. CA = file offset +
0x40000 of the pinned FIRMWARE.bin (928,920 B, sha256 7fc87e20...311b).

## 0. Verdict

1. **The TBTT is not a host-armed hardware timer.** The host message surface is
   closed: no g0-id1 key in table A/B maps to a beacon/TBTT/ISR worker, and the
   stock capture's valid key set contains no timer key. The 0x0242 band start
   and the 0x0243 config-notify events only (re)start the band MAC.
2. **The band-start and MAC-start functions do not program a timer.** Their
   complete MMIO footprint is reset/status, SMAC IRQ enables, the MAC-start
   bit and 0x40101220. The two timer words this lane identified are never
   referenced by either function.
3. **The TBTT engine is firmware-internal.** The remaining host-visible work is
   a read-only differential: compare the band-0 (working 2.4G) and band-1
   (silent 5G) SMAC interrupt state and the two timer words in the same boot.
4. **New: the per-band SMAC ISR vectors are decoded at code level.** 8 INTC
   sources per band, 7 common bodies, band dispatch through RAM 0x180814. The
   TBTT ISR is one of those bodies (ranking in sec. 4); the old 45-name blob
   stays name-only but is no longer the only identity we have.

## 1. Host-message negative [V]

The g0-id1 dispatch CA 0x43D58 reads the key as u16 hdr+20, scans table A
(CA 0x10C23C, 217 x {u16 key; u16 pad; u32 worker}) then table B (CA 0x103BF8,
88 rows), and calls [row+4] with (ctx, hdr+24, hdr[22]). The beacon-path keys
are:

| key | worker | role |
| --- | ---: | --- |
| 0x0064 | 0x449E4 | VAP add (band binding) |
| 0x0065 | 0x46394 | VAP start (per-link beacon state) |
| 0x0067 | 0x45F74 | VAP stop/teardown |
| 0x00D8 | 0x81038 | TXQ/AMSDU-MIB config |
| 0x0124 | 0x841B8 | channel / RF programme |
| 0x0127 | 0x84DA4 | set beacon (AP config; beacon template) |
| 0x01F7 | 0xB5B9C | RF mode 3 |
| **0x0242** | **0x4C524** | WAL set_green_ap band start (subcmds 1/2 call 0x4AF0C) |
| **0x0243** | **0x8419C** | config-recently-changed notify (0x4BDCE(bandobj,5,0)) |

* 0x0242 worker 0x4C524: switch on body[0] (0..8, tbb at 0x4C53C); subcmd 1
  (0x4C5E2, body[2]==1) and subcmd 2 (0x4C634) call the MAC start CA 0x4AF0C.
* 0x0243 worker 0x8419C: loads ctx+0x4B8 (band object) and calls
  CA 0x4BDCE(bandobj, 5, 0). 0x4BDCE only calls 0x4AF0C when
  u8(obj+0x14D1) == 3 - a band-state restart helper, not a beacon/timer arm.
* **No table A/B worker is the beacon callback 0x57638, the band start
  0x971D8, the MAC start 0x4AF0C or any of the 7 new common ISR bodies.**

Stock-capture check [V]: the committed `wifi-f1-from-capture` evidence lists
25 distinct valid g0-id1 keys; 0x0127 is present (38 records), **0x0242 and
0x0243 are absent**, and no captured key resolves to a beacon/timer worker.
So no stock boot frame can be the TBTT arm we are missing. The honest
conclusion is: **the TBTT arm is firmware-internal [I]/[V negative on the host
surface]**.

## 2. What the start path actually arms [V]

CA 0x4AF0C (MAC start; complete IO literal set = the 10 values in TABLES T2):

1. reset pulse 0x55 then 0xFF to 0x40114500 / 0x40110500, verify +0x51C/+0x520
   == 0xFF;
2. band1: 0x4010C220 |= 1|8|0x20|0x1000 (0x1029); band0: 0x40108214 |= 0xF;
3. per-band MAC-start bit0: 0x4011080C / 0x4011480C;
4. 0x40101220 |= 1 (band1) / 2 (band0) (the run bit);
5. per-link state: [ctx+0x2B8]+0x14 bit5 clear, [+0x8C4] bits 0..15 clear;
6. arm record: request[1] = 2 (armed), request[0x64] += 1; calls 0xF4504,
   0xF5C54 and 0x96DBC.

CA 0x971D8 (band start) contains **no device-IO literal** and only calls the
runtime hooks G+0x370 (G = 0x1747C4; slot 0 by design, R0 read) and G+0x374
(= 0x57639 Thumb, body 0x57638). The hook installer is the init record at
0x5D13E/0x5D144 (static word 0x00057639 at CA 0x5D280).

**Correction:** 0x57638 is a locked-list teardown - mutex lock at 0x57696,
repeated list-pop (0x77324) + free (0xFDF50) over the lists at obj+0xC, list
re-init, then returns 0/100/1015. It contains no device-IO literal. Calling it
"beacon start" overstates it; the hardware beacon arm is deferred to firmware
task/timer context, which is consistent with the host-message negative.

## 3. The per-band SMAC interrupt machinery (new) [V]

Registration: the per-band init at CA 0xD1B68 registers, for band0
(u8(perband+221)==0) IRQ 47,49,50,51,52,82,83,59 and for band1 IRQ
53,55,56,57,58,84,85,60 via 0xC6DA2(irq, prio, handler) followed by
0xC6FF4(irq). The handler words are a set of 3-instruction wrappers:

| band0 IRQ | band1 IRQ | wrapper (band0 / band1) | common body | first status test |
| --- | --- | --- | --- | --- |
| 47 | 53 | 0xAEC94 / 0xAEC88 | 0xAEC74 | [[obj+672]+232] bit0 -> deferred task event |
| 49 | 55 | 0xB54C8 / 0xB54BC | 0xB53A4 | bit2 -> ring dequeue + 20-B D2H msg |
| 50,83 | 56,85 | 0xB0D50 / 0xB0D44 | 0xB0CE0 | IRQ-disable dispatch + 16-B D2H msg |
| 51 | 57 | 0xAEC68 / 0xAEC5C | 0xAEB80 | bit7 -> 16-bit ring walk + 6-B D2H msg |
| 52 | 58 | 0xAEB74 / 0xAEB68 | 0xAEA9C | bit8 -> counter + 18-B D2H msg |
| 82 | 84 | 0xAE48C / 0xAE480 | 0xAE45C | mask-ack via literal |
| 59 | 60 | 0xB0C44 / 0xB0C38 | 0xB0AB8 | error bits 0/1/2 -> 0xB0930/logs |

Wrapper shape: `ldr r3,=0x180814; ldr r0,[r3,#8|#0x1C]; b.n <body>` - the
per-band SMAC object comes from RAM 0x180814 (+8 = band0, +0x1C = band1).

Dispatch/registration plumbing: the IRQ entry CA 0xC2EFC reads the pending-id
word 0x4016010C, masks it with 0x3FF, bounds-checks 95, loads
handler = [0x17D430 + 4*irq] (0x17D430 = 0x17D398+152), calls it, then writes
the id back to 0x40160110 (ack). 0xC74B0 stores a handler at
[0x17D398+152+4*irq]; 0xC6DB8 sets the enable bit at 0x40161180 + (irq/32)*4
and 0x40161280 + (irq/32)*4. The handler table is cleared at boot (memset of
384 B at 0x17D430, CA 0x46E5C).

**Host visibility limit [V]:** the INTC words are at CA 0x4016xxxx. The host
region-3 window covers CA 0x40000000..0x4011FFFF, so the **host cannot read the
firmware INTC**. The available read-only liveness proxies are the SMAC IRQ
enable/status words and the per-band SMAC objects.

## 4. Which ISR body is the TBTT? [I]

Ranking (all bodies re-derived in TABLES T4/T5):

1. **0xAEC74 (IRQ 47/53) - most likely the TBTT/timer deferred ISR [I].**
   It tests bit0 of the per-band status and, when clear, posts RTOS event 1
   (0xC0550). A software-MAC TBTT ISR that must not run the beacon engine in
   interrupt context would look exactly like this (ISR posts, task runs the
   beacon queue). Its priority in the registration is 5 (same as the data
   paths).
2. 0xB0AB8 (IRQ 59/60): dedicated error-bit handler (bits 0/1/2 of
   [[obj+740]+208] & ~[+48], with logs and 0xB0930) - abort/timeout class,
   not TBTT.
3. 0xB0CE0 (IRQ 50|83 / 56|85): disables its own IRQ, then dispatches a
   16-byte info block to 0xB07C2 (builds a 16-B D2H msg, hdr[6]=0x26) - a
   completion/notify class.
4. 0xAE45C (IRQ 82/84): pure mask-ack with an enable-mask literal.
5. 0xAEA9C (52/58) and 0xAEB80 (51/57) and 0xB53A4 (49/55): completion/ring
   classes that build D2H messages (hdr[6] = 0x23 / 0x09 / 0x12 respectively).

The 45-name blob (CA 0x12266C; `smac_tbtt_5g_isr` CA 0x1229E8,
`smac_common_timer_5g_isr` CA 0x12270C) still has **no code pointer**
[V negative re-checked], but the table above is now the code-level identity of
the per-band SMAC interrupt vectors; the name-to-body mapping stays [U] and can
be closed by reading the per-band status bits in R6 (sec. 6).

## 5. Timer candidate words [V addresses, U semantics]

Two words surface from the arm/init paths and are referenced nowhere else in
this lane's scope:

| CA | width | evidence |
| ---: | ---: | --- |
| 0x401015CC | 4 | SMAC common latch/control: written 1 to "start" (0x89812/0x89816), 0 to clear (0xA0052, 0x897EE), a count in the per-band init (0x9F438). Polled by many init/wait paths. |
| 0x400024B0 | 2 | 16-bit word written **100** by both per-band init paths (0x9F434/0x9F440) and by the timer-sync path (0x89826/0x89828). Reading it in the sync path saves the 16-bit value before reloading 100. |

The literal 100 is the only interval-sized constant in the arm/init code; it is
consistent with a 100-TU default beacon interval but the exact semantics are
**[U]** (could be a tick period). Both are inside the host region-3 window and
are side-effect-free to read (32-bit aligned; the 16-bit word may be read with
a 32-bit read and masked).

## 6. Read-only probe pack R6 (the discriminating measurement)

Full address table in TABLES T7 / `--probes`. Two-step protocol:

1. read the band dispatch word CA 0x180814 (+8 -> band0 object, +0x1C -> band1
   object; firmware RAM rule, host readable);
2. read each object's +672/+700/+740 pointers (firmware RAM or device IO);
   follow them only if the value is in firmware RAM (0x00040000..0x00180000) or
   in the device IO range (0x40000000..0x4011FFFF);
3. read the status/mask pair [obj+672]+232 / +228 twice, ~200 ms apart, in the
   same boot as the working 2.4 GHz AP;
4. read the device-IO pack: 0x4010C200/0x4010C220 (band1 SMAC status/enable),
   0x4010820C/0x40108214 (band0), 0x4010E150/54/58 (band1 TRX), 0x4011080C /
   0x4011480C (MAC start bits), 0x40101220 / 0x40101208, 0x401015CC,
   0x400024B0, and both 0x40110xxx / 0x40114xxx MAC blocks.

Decision table (band0 = working reference, band1 = silent):

| observation | conclusion | next rung |
| --- | --- | --- |
| band1 MAC-start bit0 = 0 or 0x4010C220 lacks 0x1029 | R5 state did not persist | re-run the R5 ladder, re-read |
| band1 SMAC status [obj+672]+232 never changes while band0's does | no SMAC ISR fires on band1 -> **TBTT/timer not armed** | R6b (below) |
| band1 status changes but no beacon | TBTT/ISR does fire -> gap is beacon TX gating (0x40101220/0x40101208 or TRX) | escalate to the beacon-TX lanes |
| 0x400024B0 reads 100 on band0 but 0/other on band1 | timer word not initialised for band1 | R6b |
| 0x401015CC identical on both bands, 0x400024B0 = 100 both | timer words not the discriminator; rely on the ISR status | R6b |

R6b (conditional, still host-side/read-only): re-run the R5 ladder (0x0127 ->
0x0065 -> 0x0242 subcmd 1) and repeat the probe pass; the R5 writes are
idempotent. If band1 SMAC status starts moving, capture and run the 5200 scan.
If nothing moves, the verdict is **firmware-internal only** and the next step
is a firmware patch hypothesis, not another host frame.

## 7. Limits

* The name-blob -> ISR-body mapping and the exact timer semantics stay [U]
  offline; both need the R6 read.
* The firmware INTC is not host-visible, so "which per-band IRQ fired" can only
  be inferred from the per-band SMAC status/mask words.
* No stock reference read of 0x401015CC / 0x400024B0 exists in the committed
  evidence; R6 establishes the first values.
