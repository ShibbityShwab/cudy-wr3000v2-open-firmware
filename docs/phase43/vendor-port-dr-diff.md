# vendor-port-dr-diff: every DR-path channel-register write the vendor performs and the port does not (phase 43, 2026-10-04)

Task `st_01a10727` (parent `01a0fc5c`). **Static, read-only**: the two vendor modules, the firmware
image, the port source, the record's docs and `build/register-dumps/reg_all.txt`. No device access,
no register write, no new artifact except this file.

Question: the port posts correct-format DR nodes in host DRAM and the device never deposits
(`docs/phase40/dr-buffers-stay-zero.md`). The mailbox gate (phases 31-35) is the known
*message-service* cause; this report answers the **deposit-activation** half - *which channel
registers the vendor programs for the DR path that the port's `omo_ete_program` never does*.

Every claim is **[proven]** (an instruction/relocation/constant that re-decodes as quoted),
**[observed]** (a value in a read-only artifact) or **[inferred]**.

---

## 0. Sources, conventions, and how each one is quoted

| source | identity | convention |
| --- | --- | --- |
| `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | md5 `23660bc285393e678d5cade1c36c194b` | ARM (`CS_MODE_ARM`); `.text` `sh_addr = 0`, `sh_offset = 0x38`, so quoted `.text` offsets are vaddrs and file offset = vaddr + `0x38` |
| `opensource/build/tmp/hi5622v100_wifi.ko` | md5 `4737fcb21a1a2262a96f84d780ad8b35` | same, plus `.rodata` (file `0x167b00`) whose offsets are quoted as `rodata 0xNNN` |
| `build/tmp/FIRMWARE.bin` | md5 `0e530b976d5a20e87358671f1a577695`, 928,920 B | Thumb (`CS_MODE_THUMB`), quoted as **file offsets**; runtime = file + `0x40000` |
| `opensource/lab/wifidrv1/wifidrv1.c` | the port | quoted as `wifidrv1.c:LINE` |
| `build/register-dumps/reg_all.txt` | device-side `soc_register` read, lines `addr = HEX, value = HEX` | quoted as `reg_all 0xCA = 0xV` |
| `build/register-dumps/exp/20261004-125119/dmesg.txt` | the phase-40 takeover run of the port | quoted as `exp/20261004-125119` |

Tool: `capstone 5.0.7` via `opensource/lab/ko_disasm.py` (function-level, relocations annotated) and
a raw-window disassembler. §7 re-states every quoted offset with the instruction it decodes to.

Address mapping used throughout (the record's own rule, `docs/phase23/register-windows.md`,
`docs/phase42/ete-registers-reconciled.md` A.1/A.2):

```
ETE block                device CA 0x4003a000   == BAR0 0x3f2000  (the port's OMO_ETE_WIN)
SR channel blocks        ETE + {0x400, 0x450, 0x4a0}          -> CA 0x4003a400/0x450/0x4a0
DR channel blocks        ETE + {0x590, 0x5e0, 0x630, 0x680}   -> CA 0x4003a590/0x5e0/0x630/0x680
field offsets inside a channel block: +0x00 enable | +0x08 ctrl | +0x10 base | +0x14 depth-1
                                      +0x18 producer | +0x1c consumer | +0x30 base | +0x34 depth-1
                                      +0x38 producer | +0x3c consumer | +0x48 second enable
```

---

## 1. What the port programs today (`omo_ete_program`, `omo_dr_post`)

`wifidrv1.c:507` `omo_ete_program()` is the port's whole "first write path". Its ETE writes, in
order and exactly:

| site | register | value |
| --- | --- | --- |
| `wifidrv1.c:517` | message-window `0x508` (CA `0x40039508`, the ETE interrupt block) | `old & 0xffe0f8f8` |
| `wifidrv1.c:525` | SR chN `+0x10` (base) | `devva(node array)` |
| `wifidrv1.c:527` | SR chN `+0x14` (depth-1) | `31` |
| `wifidrv1.c:529` | SR chN `+0x18` (producer) | `0` |
| `wifidrv1.c:531` | SR chN `+0x08` (ctrl) | `0` |
| `wifidrv1.c:540` | **DR chN `+0x30` (base)** | `devva(node array)` |
| `wifidrv1.c:542` | **DR chN `+0x34` (depth-1)** | `31` |
| `wifidrv1.c:544` | **DR chN `+0x38` (producer)** | `0` |
| `wifidrv1.c:550` | glue `0x2e8` (CA `0x400392e8`) | `old & 0xfffffc20` |

and the only DR write outside it is the producer commit

```
wifidrv1.c:1705   iowrite32(idx, omo_ete + b + ETE_DR_WPTR);      /* b = omo_dr_block[i] */
```

whose block list is `wifidrv1.c:363`
`static const unsigned long omo_dr_block[OMO_ETE_DR_N] = { 0x590, 0x5e0, 0x630, 0x680 };`
and whose only DR field constants are `wifidrv1.c:189-192`
`ETE_DR_BASEREG 0x030`, `ETE_DR_DEPTH 0x034`, `ETE_DR_WPTR 0x038`, `ETE_DR_RPTR 0x03c`.
The port's two enable constants are **SR-named only** (`wifidrv1.c:186-187`):
`#define OMO_SR_EN0 0x000` and `#define OMO_SR_EN1 0x048`, and the only two writes of them are in
`omo_sr_post` (`wifidrv1.c:1649` `iowrite32(en | 1U, omo_ete + b + OMO_SR_EN0);` and
`wifidrv1.c:1653` `iowrite32(1U, omo_ete + b + OMO_SR_EN1);`). Enumerating every barrier-write in
the file (`iowrite32` sites 276/381/632-634/910/1036-1044/1101/1108/1207/1211/1328/1363/1398/1643/
1649/1653/1662/1666/1705/1835 and the `omo_wr` sites 517, 525-531, 540-544, 550) confirms:
**there is no write of `b + 0x00` and no write of `b + 0x48` for any DR block, anywhere in the
port.**

---

## 2. What the vendor's *host driver* programs for the DR path - and it is the same set

`pcie_ete_dr_reg_init` @ `0x1483c` (size 204, plat.ko) is the DR register program:

```
0x014854: ldr  r6, [r1, #0x50]          ; r6 = the DR channel block VA (the MMIO base)
0x014874: ldr  r2, [r1, #0x54]          ; the node array (host VA)
0x014884: bl   pcie_hostca_to_devva     ; R_ARM_CALL
0x014888: str  r0, [r6, #0x30]          ; DR+0x30 = base (device VA of the node array)
0x014894: ldr  r2, [r4, #0x50]          ; r2 = the DR block VA again
0x01489c: ldr  r1, [r2, #0x34]          ; read-modify-write of the depth word
0x0148a4: bfi  r1, r3, #0, #0xa         ; index field = cfg depth-1 (cfg[4]=0x20 -> 0x1f)
0x0148a8: str  r1, [r2, #0x34]          ; DR+0x34 = depth-1
0x0148ac: ldr  r3, [r4, #0x50]          ; r3 = the DR block VA
0x0148b0: ldr  r2, [r4, #0x1c]
0x0148b4: str  r2, [r3, #0x38]          ; DR+0x38 = producer (ctx+0x1c)
```

`pcie_ete_dr_init` @ `0x14908` writes only software context fields (`0x14924`-`0x14944`:
`str r6,[r1,#0x1c]`, `+0x20`, `+0x24`, `+0x28`, `+0x2c`, `+0x30`; `0x14958` `str r1,[r3,#4]`;
`0x14960` `strb r2,[r3,#1]`; `0x14968` `bl pcie_ete_dr_reg_init`) and `pcie_ete_init_dst_ring`
@ `0x727c` only walks channels 3..6 (`0x0072b0 mov r8,#3`, `0x00738c cmp r8,#7`,
`0x007384 add r6,r6,#0x6c`, `0x007388 bl pcie_ete_dr_init`). **No other MMIO.**

For contrast, the SR program `pcie_ete_sr_reg_init` @ `0x14a48` writes
`0x014ab0 str r0,[r5,#0x10]` (base), `0x014ad0 str r1,[r2,#0x14]` (depth-1),
`0x014adc str r2,[r3,#0x18]` (producer), `0x014af4 str r2,[r3,#8]` (ctrl = cfg[5]).
The other two vendor MMIO writes around the rings are `pcie_ete_intr_init` @ `0x7528`
(`0x0075ac movw r2,#0xf8f8`, `0x0075b0 movt r2,#0xffe0`, `0x0075b4 and r2,r2,r1`,
`0x0075b8 str r2,[r3]` - the interrupt block) and `pcie_ete_chn_res` @ `0x7490`
(`0x007504 and r8,r8,sb` with `sb=0xfffffc20` from `0x0074a4/0x0074a8`,
`0x007520 str r8,[r2,#0x2e8]` - the glue word). Those two the port already reproduces
(`wifidrv1.c:517`, `:550`).

**Result: the vendor host driver's DR register set is `{+0x30, +0x34, +0x38}` and the port writes
exactly those three (`wifidrv1.c:540/542/544`, `:1705`). Against `omo_ete_program` the host-driver
diff is EMPTY.** A per-function sweep of both modules for an offset-0 channel-enable write
(`orr rX,rY,#1` immediately followed by `str rX,[rZ]`) finds only `pcie_msg_send_irq` `0x1754c`/
`0x17550`, which is the mailbox pending register, not a channel block; wifi.ko has none at all.
**[proven]** - so the missing writes are not the host driver's.

---

## 3. The diff table - the DR path's channel registers

Two other parties program the same blocks: the device **firmware** (`pcie_msg_init`, file `0x9334`)
and nothing else. The vendor's live final state is in `reg_all.txt`. The port side is quoted from
`wifidrv1.c`. Values are for DR ch0 (CA `0x4003a590`, BAR0 `0x3f2590`); the other three blocks are
`+0x50` apart (CA `0x4003a5e0/0x630/0x680`).

| register (CA, ETE offset) | field | vendor write (value) | vendor code site | port write |
| --- | --- | --- | --- | --- |
| **`0x4003a590` (`+0x00`)** | **per-channel enable (bit 0)** | `(old \| 1)` - live `= 0x1` | firmware `pcie_msg_init` loop for the `+0x10` group: `0x0948a ldr.w r3,[r3,#0x31c]`, `0x09490 ldr r2,[r3]`, `0x09492 orr r2,r2,#1`, `0x09496 str r2,[r3]`; loop for the `+0x30` group: `0x0955c ldr.w r3,[r3,#0xb0]`, `0x09562 ldr r2,[r3]`, `0x09564 orr r2,r2,#1`, `0x09568 str r2,[r3]` | **ABSENT** (SR-side equivalent exists: `wifidrv1.c:1649`) |
| `0x4003a598` (`+0x08`) | ctrl (`cfg[5]`) | `cfg[5]` = `0` | firmware `0x09450 bfi r2,r1,#0,#3` / `0x09454 str r2,[r3,#8]` | **ABSENT** for DR (SR-side: `wifidrv1.c:531`) |
| `0x4003a5a0/5a4/5a8/5ac` (`+0x10` group) | base/depth-1/prod/consumer of the **device-internal** ring | base `= 0x01060440`, depth-1 `= 0x1f`, prod `= 3`, cons `= 3` (live) | firmware `0x09426 str r1,[r2,#0x10]`, `0x09434 bfi` / `0x09438 str r1,[r2,#0x14]`, `0x09444 str r2,[r3,#0x18]` | ABSENT (host-driver side too) |
| `0x4003a5b8` (`+0x28`) | unknown (live `0xffff`, same on SR0 at `0x4003a428`) | writer not identified in this pass (neither module's ETE paths nor the firmware's `pcie_msg_init`) | - | ABSENT |
| `0x4003a5c0` (`+0x30`) | ring base (host DRAM) | `devva(node array)`; live `= 0x844d8000` | plat.ko `0x014888 str r0,[r6,#0x30]` (after `0x014884 bl pcie_hostca_to_devva`) | `wifidrv1.c:540` `omo_wr(omo_ete, b + ETE_DR_BASEREG, devva, t)` |
| `0x4003a5c4` (`+0x34`) | depth-1 | `(depth-1) & 0x3ff`; live `= 0x1f` | plat.ko `0x0148a4 bfi r1,r3,#0,#0xa` + `0x0148a8 str r1,[r2,#0x34]` | `wifidrv1.c:542` (`OMO_ETE_DEPTH - 1` = 31) |
| `0x4003a5c8` (`+0x38`) | producer (packed index+phase-10) | `ctx+0x1c`; live `= 3` | plat.ko `0x0148b4 str r2,[r3,#0x38]` | `wifidrv1.c:544` (0, pre-release) and `wifidrv1.c:1705` (committed lap) |
| `0x4003a5cc` (`+0x3c`) | consumer | advanced by the device/firmware | firmware commit twin `0x08192a`/`0x081952` + index advance `0x08190a bfi r3,r2,#0,#0xa` | read only (`wifidrv1.c:1707`, `:1736`, `:1785`) |
| **`0x4003a5d8` (`+0x48`)** | **second enable** | live `= 0x1`; already `1` before the release in the phase-40 run | no writer found in either module or in `pcie_msg_init` (the only host `+0x48` store, `pcie_msg_send` `0x16188 str r3,[r6,#0x48]`, is a software ctx field) | **ABSENT** for DR (SR-side: `wifidrv1.c:1653`) |

`reg_all.txt` confirms the live values (`reg_all 0x4003a590 = 0x1`, `0x4003a5a0 = 0x01060440`,
`0x4003a5a4 = 0x1f`, `0x4003a5a8 = 0x3`, `0x4003a5ac = 0x3`, `0x4003a5c0 = 0x844d8000`,
`0x4003a5c4 = 0x1f`, `0x4003a5c8 = 0x3`, `0x4003a5cc = 0x3`, `0x4003a5d8 = 0x1`) and shows the
same `+0x00 = 1` / `+0x48 = 1` on **all seven** channel blocks (`0x4003a400`, `0x4003a450`,
`0x4003a4a0`, `0x4003a590`, `0x4003a5e0`, `0x4003a630`, `0x4003a680`). **[observed]**

So of the DR path's channel registers, the vendor sets and the port's `omo_ete_program` does not:
**`+0x00` (enable), `+0x48` (second enable)**, and (legitimately - it is device memory) the `+0x10`
group. The port writes the `+0x00`/`+0x48` pair for **SR** channels and never for **DR** - an
asymmetry inside the port's own code, not a deliberate exclusion.

---

## 4. The second candidate family: the `0x4004X000` / `0x4006X000` channel blocks

The record's own fact list names `reg_all 0x4004a004 = 0x84a90000` and `0x4004a008 = 0x6140` as
"channel 5's base+4 = a host-DRAM ring base, base+8 = an index". Both values reproduce
(`reg_all 0x4004a004 = 0x84a90000`, `0x4004a008 = 0x6140`; nothing else in `0x4004a000..0x4004a03c`
is non-zero, and the whole `0x4004a000..0x4004bfff` span holds only three non-zero words).
This family is a **different** register file from the ETE block and is worth a table because it is
the only other place a "DR ring base register" exists:

| register | vendor write (value) | vendor code site | port |
| --- | --- | --- | --- |
| `0x4004a004` / `0x4006a004` (block base `+4`) | live `0x84a90000` / `0x84a98000` (both host DRAM); `0x40040004 = 0x83fd7000`, `0x40060004 = 0x844cd000` | wifi.ko: the 10 CA literals at `rodata 0xb60` and `rodata 0xb88`, copied to the stack by `shuangta_host_initialize_machw` @ `.text 0x3491c` (`0x034968 ldr ip,[pc,#0x16c]`, literal `0x034adc` = `0xb60`, reloc `R_ARM_ABS32`; `ldm ip!`/`stm r5!` block `0x034980`-`0x0349b0`), then translated to host VAs by the helper @ `.text 0x33fcc` (`0x03400c bl oal_pcie_devca_to_hostva`) which stores the ten VAs at `[r8+0]`, `[r8+4]`, `[r8+8]`, `[r8+0xc]`, `[r8+0x10]`, `[r8+0x14]`, `[r8+0x18]`, `[r8+0x1c]`, `[r8+0x20]`, `[r8+0x24]` (`0x0340ec`, `0x0340b8`, `0x0340b0`, `0x0340a8`, `0x0340a0`, `0x034098`, `0x034090`, `0x034088`, `0x03407c`, `0x034054`), the result stored at `[chip+0x14c]` (`0x034960 str r0,[r6,#0x14c]`) | ABSENT (the port never touches this family) |
| `0x4004a008` (block base `+8`) | live `0x6140` (a packed index: low 10 bits `0x140`, phase bit 10 set) | same tables | ABSENT |

This family is **not** the ETE DR ring: `docs/phase6/register-dump.md` (line 255) and
`docs/phase7/dump-semantics.md` (line 58) name `0x4004a000` the **2g MAC** register block
(`phase6`: "*Reading 2g_mac_register!* | `0x4004a000..0x4004ae5c` | ... | static (config/ID)";
`phase7`: "*16 | 2g MAC 0x4004a000 (2g_mac)* | ... | **static config**"), and §3's ETE DR0 block
(`0x4003a590`) is host DRAM too. So the correct DR base register in `reg_all` is `0x4003a5c0 = 0x844d8000`, and
`0x4004a004 = 0x84a90000` is a **MAC ring base**, not the ETE DR ring (see §8).

---

## 5. Ranked deposit-activation candidates

Ranked by how plausible it is that the *missing write* is the precondition for the device's
deposit, given: (a) the port posts correct-format nodes and commits the producer
(`exp/20261004-125119` `DR ch3 posted ... commit DR+0x38 <= 0x00000400 readback=0x00000400`),
(b) the device is alive and signalling (`docs/phase40`: bits 6/2), (c) the port's base/depth/prod
writes all read back (`match=YES`).

### H1 - the DR per-channel enable `+0x00` (CA `0x4003a590`/`0x5e0`/`0x630`/`0x680`) is never set - **highest**

* **[proven]** it is a real per-channel control: phase 22 transcribed the firmware's two loops and
  concluded the *descriptor fetch* is *"the ETE hardware, gated by the channel enable and the
  outbound address translation"* (`docs/phase22/fw-sr-gate.md`, Headline). The instruction sites are
  §3's `0x09490`-`0x09496` and `0x09562`-`0x09568`.
* **[proven]** the vendor ends with it set on all seven channels (`reg_all`: `+0x00 = 1` on
  `0x4003a400/0x450/0x4a0/0x590/0x5e0/0x630/0x680`).
* **[proven]** the port knows the write and applies it to SR only (`wifidrv1.c:1649`), and applies it
  to **no** DR block (§1's full enumeration of the port's writes).
* **[observed]** in the phase-40 run the port itself measured the pre-write value: SR ch0
  `+0x00 = 0x00000000 -> 0x00000001` - i.e. **in a takeover the enabled state does not exist until
  the host writes it** - while no DR channel ever received that write.
* **[observed]** the firmware's loop that would set it had *not* run by the end of that run: at the
  ring-watch baseline (51.7 s, ~9 s after the release readback at 43.0 s) SR ch0 `+0x18` still read
  the port's own committed
  `0x00000400` (`SR ch0 baseline: wptr(+0x18)=0x00000400 rptr(+0x1c)=0x00000400`), whereas the
  firmware's loop writes `+0x18` (`0x09444 str r2,[r3,#0x18]`). So no firmware write had touched the
  program group - and therefore none had set `+0x00` for the four DR channels either.
* Counter-evidence to state honestly: if the firmware's loop *does* complete (past the mailbox gate,
  in a fuller release), it will set `+0x00` for all seven blocks itself, and the write becomes a
  no-op - exactly what `lab/fwaccept` measured in phase 20 (`docs/phase20/fw-accept.md` A.4). The
  experiment below therefore reads first and only writes what is zero.

### H2 - the port never re-asserts the DR program group *after* the release - **high**

The firmware's own loop writes the same fields the port pre-programmed (`+0x10`/`+0x14`/`+0x18`/
`+0x08` for its first group and `+0x30`/`+0x34`/`+0x38` for its second, §3). The port programs its
rings at load time and the release is written later (`exp/20261004-125119`: ring program at
`39.6`-`39.8 s`, release readback at `43.0 s`); after the release it re-reads only the indices
(`wifidrv1.c:1736` `omo_dr_block[i] + ETE_DR_RPTR`, `:1785`/`:1786` `+0x3c`/`+0x38`) and never
`+0x30`/`+0x34` again. If the firmware's loop targets a port-programmed block, the port's base is
overwritten with a firmware-side value and the deposit goes elsewhere. **[inferred]** from the two
loops' field set; the phase-40 run cannot distinguish it because the loop had not run (H1's last
bullet) - which is also why H2 is ranked below H1.

### H3 - the DR second enable `+0x48` - **medium**

Live `= 1` on all seven, and the port writes the SR counterpart only (`wifidrv1.c:1653`).
Ranked below H1 because in the phase-40 run the port read it as **already `1` before the release**
and before any firmware ran (`ENABLE SR ch0 +0x48 0x00000001 -> 0x00000001`), and because that run's
`+0x00` read `0` in the same breath - so no earlier load of the module had reached `omo_sr_post` in
that boot either, making `+0x48 = 1` the block's own default rather than a leftover (both steps
[inferred]). A register that already reads 1 cannot be the missing precondition.

### H4 - the ETE interrupt block's per-channel bits (CA `0x40039508`) - **medium-low**

Both masks clear bits 0-2/8-10/16-20 (`pcie_ete_intr_init` `0x0075b4 and r2,r2,r1` with
`0xffe0f8f8`; the firmware's `0xe0e0f8f8`), and only bits 12/29 (the two group masters) are ever
re-enabled. This is `docs/phase22/fw-sr-gate.md`'s H2, still unexercised. Not a "channel register"
in the ring file's sense, so it is a candidate only via a wider reading of the question.

### H5 - the `0x4004X000` MAC-block ring registers (`base+4`/`base+8`) - **lowest**

Real host-DRAM ring bases in the vendor's live state (`0x4004a004 = 0x84a90000`), programmed by the
wifi.ko MAC init paths (§4), and never touched by the port. Ranked last because (a) those blocks are
the WLAN MAC register file, not the ETE message rings (§4, phase 6/7), (b) the task's own constraint
records that a host read of the range bus-errors, so a write there cannot be verified by readback,
and (c) nothing connects them to the ETE D2H deposit the port is waiting on.

---

## 6. The single most promising register to try next

**`CA 0x4003a590` `+0x00` (BAR0 `0x3f2590`), bit 0 = 1 - the DR ch0 per-channel enable; the same on
`0x4003a5e0`, `0x4003a630`, `0x4003a680` (BAR0 `0x3f25e0`, `0x3f2630`, `0x3f2680`).**

It is the one register in the DR ring's own file that the vendor sets (firmware `0x09492`/`0x09496`,
`0x09564`/`0x09568`; live `reg_all = 1`) and the port never writes, its absence is *measurable in the
port's own logs* for the SR pair, and - unlike H5 - it is already inside `OMO_ETE_WIN`
(`wifidrv1.c:150` `#define OMO_ETE_WIN 0x3f2000UL`), so it needs no new mapping and its write can be
read back immediately.

Experiment shape (future live run, one change):

1. In `omo_ete_program`, for each DR block, `en = omo_rd(b + 0x00); omo_wr(b + 0x00, en | 1, "DR chN +0x00 enable")`
   - exactly the shape of `wifidrv1.c:1648-1650` - and log the pre-value, so a firmware-set 1 is
   never clobbered and the readback proves the write (in the phase-40 run the SR pair's pre-value was
   `0`, so on those blocks the firmware really had not set it).
2. Re-read it *after* the release + mailbox poll (the run that matters), together with `+0x48` and
   the `+0x30`/`+0x34` base/depth: this single read is what separates H1 from H2 - if `+0x30` still
   holds the port's `devva` and `+0x00` reads 1, H1/H2 are cleared and the deposit gate is confirmed
   to be the message service (as `docs/phase40` concluded); if `+0x00` reads 0 or `+0x30` has changed,
   the missing write is the cause.
3. Control: the same two reads on SR ch0 (where the port already sets `+0x00`), to show the write
   lands on the right blocks.

Expected observable: `omo_dr_watch`'s `0 DR deposit events` becomes a non-zero event, or a node word
`word1` becomes `(len<<16)|0x6d2b`-shaped, with the payload no longer zero.

---

## 7. Verification (each quoted offset decodes to the claimed instruction)

`capstone 5.0.7`; plat.ko `CS_MODE_ARM`, `.text` offset = vaddr, file = vaddr + `0x38`;
FIRMWARE.bin `CS_MODE_THUMB`, file offsets.

| quoted | instruction / value |
| --- | --- |
| `0x014854` / `0x014874` / `0x014884` / `0x014888` | `ldr r6,[r1,#0x50]` / `ldr r2,[r1,#0x54]` / `bl pcie_hostca_to_devva` (`R_ARM_CALL`) / `str r0,[r6,#0x30]` |
| `0x01489c` / `0x0148a4` / `0x0148a8` / `0x0148b0` / `0x0148b4` | `ldr r1,[r2,#0x34]` / `bfi r1,r3,#0,#0xa` / `str r1,[r2,#0x34]` / `ldr r2,[r4,#0x1c]` / `str r2,[r3,#0x38]` |
| `0x014ab0` / `0x014ad0` / `0x014adc` / `0x014af4` | `str r0,[r5,#0x10]` / `str r1,[r2,#0x14]` / `str r2,[r3,#0x18]` / `str r2,[r3,#8]` |
| `0x0072b0` / `0x007384` / `0x007388` / `0x00738c` | `mov r8,#3` / `add r6,r6,#0x6c` / `bl pcie_ete_dr_init` / `cmp r8,#7` |
| `0x0075ac` / `0x0075b0` / `0x0075b4` / `0x0075b8` | `movw r2,#0xf8f8` / `movt r2,#0xffe0` / `and r2,r2,r1` / `str r2,[r3]` |
| `0x0074a4` / `0x0074a8` / `0x007504` / `0x007520` | `movw sb,#0xfc20` / `movt sb,#0xffff` / `and r8,r8,sb` / `str r8,[r2,#0x2e8]` |
| `0x016188` | `str r3,[r6,#0x48]` (software ctx, in `pcie_msg_send`) |
| `0x01754c` / `0x017550` | `orr r3,r3,#1` / `str r3,[r2]` (`pcie_msg_send_irq`; the only offset-0 `orr#1;str` in plat.ko) |
| fw `0x09426` / `0x09434` / `0x09438` | `str r1,[r2,#0x10]` / `bfi r1,r3,#0,#0xa` / `str r1,[r2,#0x14]` |
| fw `0x09444` / `0x09450` / `0x09454` | `str r2,[r3,#0x18]` / `bfi r2,r1,#0,#3` / `str r2,[r3,#8]` |
| fw `0x0948a` / `0x09490` / `0x09492` / `0x09496` | `ldr.w r3,[r3,#0x31c]` / `ldr r2,[r3]` / `orr r2,r2,#1` / `str r2,[r3]` |
| fw `0x0951c` / `0x0952a` / `0x0952e` / `0x0953a` | `str r1,[r3,#0x30]` / `bfi r0,r3,#0,#0xa` / `str r0,[r1,#0x34]` / `str r1,[r3,#0x38]` |
| fw `0x0955c` / `0x09562` / `0x09564` / `0x09568` | `ldr.w r3,[r3,#0xb0]` / `ldr r2,[r3]` / `orr r2,r2,#1` / `str r2,[r3]` |
| wifi.ko `0x034968` / literal `0x034adc` | `ldr ip,[pc,#0x16c]` / `= 0xb60` (`R_ARM_ABS32` -> `.rodata`) |
| wifi.ko `0x03400c` / `0x034054` / `0x0340b8` / `0x0340ec` / `0x034960` | `bl oal_pcie_devca_to_hostva` / `str r2,[r8,#0x24]` / `str r2,[r8,#4]` / `str r2,[r8]` / `str r0,[r6,#0x14c]` |
| wifi.ko `.rodata` `0xb60`..`0xb8c`, `0xb88`..`0xbac`, `0xbb0`..`0xbd4`, `0xc00`..`0xc20` | the four 10/9-word CA tables (`0x40042000`, ..., `0x40064000` / `0x40074000`) |

Data (not instructions): `reg_all` values as quoted in §3/§4; phase-40 log lines as quoted in §5
(`build/register-dumps/exp/20261004-125119/dmesg.txt`).

## 8. Corrections / notes this report adds to the record

1. `docs/phase42/VERIFICATION.md`'s *"the corrected DR base is 0x84a90000 (register at device CA
   0x4004a004)"* conflates two register files: `0x4004a004` is the **2g MAC** block's ring base
   (`docs/phase6/register-dump.md` 255, `docs/phase7/dump-semantics.md` 58), while the ETE DR0 base
   register is `0x4003a5c0` and reads `0x844d8000` in the same `reg_all.txt`. Both are host DRAM, but
   only `0x4003a5c0` belongs to the ETE DR path the port posts into.
2. The port's `ETE_SR_STRIDE 0x114` / `ETE_DR_STRIDE 0x6c` (`wifidrv1.c:175-176`) are *host struct*
   strides, not register strides; the phase-40 decode line prints the resulting wrong CAs
   (`SR ch1 CA=0x4003a514`, `DR ch1 CA=0x4003a5fc`, `DR ch3 CA=0x4003a6d4`). The register blocks are
   `+0x50` apart (`{0x400,0x450,0x4a0}` / `{0x590,0x5e0,0x630,0x680}`; same as
   `docs/phase42/ete-registers-reconciled.md` §A.2/§G.1). `omo_ete_program` uses the correct
   `omo_dr_block[]` (`wifidrv1.c:363`); only `omo_decode_regs` (`:1883`) has the wrong stride.
3. `docs/phase20/fw-accept.md` A.4's "the firmware sets `+0x00 = 1`, `+0x48 = 1` for all seven
   channels" is not reproduced for `+0x48` in the phase-40 run, which read `+0x48 = 1` *before* the
   release and before any firmware ran (`ENABLE SR ch0 +0x48 0x00000001 -> 0x00000001`) while `+0x00`
   read `0` (`ENABLE SR ch0 +0x00 0x00000000 -> 0x00000001`). Since a port-side leftover is excluded
   by the `+0x00 = 0`, `+0x48 = 1` is a block default in that run; no writer for it was found in
   either module or in `pcie_msg_init`. If the firmware does set it in a fuller release, the port's
   write is a harmless no-op.
