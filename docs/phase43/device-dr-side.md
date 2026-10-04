# device-dr-side: the firmware's own D2H transmit path, and the deposit precondition named from the device side (phase 43, 2026-10-04)

Task `st_01a10728`. Static, read-only: `build/tmp/FIRMWARE.bin` plus the phase-13/21/22/25/42 record.
No device access, no writes outside this file.

**Headline, in one paragraph.** The firmware's D2H transmit is **not** a store into host memory and
**not** a write to the DR channel's host-ring registers. It is (a) a **per-channel enable** the
firmware sets itself (`block+0x00 |= 1`), (b) the **device-local ring** program group it writes for
each DR channel (`block+0x10/+0x14/+0x18`), and (c) the **mailbox notify**: a pending mask into
`out[1]` (CA `0x40039014`) plus bit 0 of the D2H doorbell (CA `0x40101434`). The payload deposit -
bytes landing in the host DR buffers and the DR index advancing at `block+0x3c` - is performed by
the **ETE engine**, not by firmware stores: the image contains no store to any host-aperture address
and, after `pcie_msg_init`, no firmware write to any DR-channel register at all. The precondition the
device enforces before it will publish a D2H word is therefore a **mailbox** condition, not a channel
condition: **`out[1]` must read 0** (the host has consumed the previous notification) - file
`0x86120..0x86128`. The DR-channel registers the vendor's device-side code sets that the port's own
ETE programming never sets are the **per-channel enable `block+0x00`** and the **`block+0x10` program
group** (`base/depth/producer`), both written only by the firmware's own init (files `0x9490..0x9496`,
`0x9426/0x9438/0x9444`).

Every claim is tagged **[proven]** (an instruction, a literal, or a table byte at a quoted file
offset) or **[inferred]** / **[unknown]**.

---

## 0. Sources, identity and conventions

| item | value |
| --- | --- |
| firmware image | `build/tmp/FIRMWARE.bin`, 928,920 B, md5 `0e530b976d5a20e87358671f1a577695` |
| runtime convention | **runtime address = file offset + `0x40000`** (record convention, phases 4/6/8/22) |
| disassembler | `pyenv/Scripts/python.exe`, capstone 5.0.7, `CS_ARCH_ARM, CS_MODE_THUMB`, `skipdata=True` |
| register dump | `build/register-dumps/reg_all.txt` (device-side `soc_register` read, format `addr = HEX, value = HEX`) |
| port under test | `opensource/lab/wifidrv1/wifidrv1.c` (read-only) |
| record read | `docs/phase42/{THE-RING-MAP,VERIFICATION,word-semantics,ete-registers-reconciled,block-structure}.md`, `docs/phase25/live-vendor-ring-ground-truth.md`, `docs/phase21/*.md`, `docs/phase13/rings.md`, `docs/HAZARDS.md` |

All addresses below are **file offsets** unless marked `rt` or `CA`. The ETE register block is
**device CA `0x4003a000`**, host-visible through the endpoint's region-3 window at
`BAR0 0x3b8000 + (CA - 0x40000000)` (the port's own rule, `wifidrv1.c:78`, and
`docs/phase42/ete-registers-reconciled.md` A.5).

---

## 1. The channel register file, as the firmware uses it

The ETE block is 7 channel blocks of `0x50` bytes, each carrying **two program groups**. The record's
own field layout (`docs/phase20/rx-loop.md` A.1, re-confirmed against `reg_all.txt`):

| offset | field | who programs it |
| --- | --- | --- |
| `+0x00` | per-channel **enable** (bit 0) | **firmware** (`|= 1`) |
| `+0x08` | control nibble (low 3 bits) | firmware (from `cfg[5]`), host for SR |
| `+0x10` `+0x14` `+0x18` `+0x1c` | group A: base / depth-1 / index / index2 | either (see below) |
| `+0x28` | resource word (reads `0xffff` live) | not found in the image |
| `+0x30` `+0x34` `+0x38` `+0x3c` | group B: base / depth-1 / index / index2 | either (see below) |
| `+0x48` | second enable (reads `1` live on all 7) | **not found in the image** |

Channel block offsets inside the block, and which group is the *host* ring (device CA 0x4003a000
+ offset):

| family | blocks | group A (`+0x10`) | group B (`+0x30`) |
| --- | --- | --- | --- |
| SR (host -> device) | `0x400` `0x450` `0x4a0` | **host DRAM** ring (base `0x844d9000..0x844db000` live) | device RAM ring (`0x01060550..0x01060750` live) |
| DR (device -> host) | `0x590` `0x5e0` `0x630` `0x680` | device RAM ring (`0x01060110..0x01060440` live) | **host DRAM** ring (base `0x844d5000..0x844d8000` live) |

[proven] for the layout and the live values (`reg_all.txt`: `4003a410 = 844db000`,
`4003a460 = 844da000`, `4003a4b0 = 844d9000`, `4003a5c0 = 844d8000`, `4003a610 = 844d7000`,
`4003a660 = 844d6000`, `4003a6b0 = 844d5000`; and `4003a400 = 1`, `4003a590 = 1`, `4003a448 = 1`,
`4003a5d8 = 1`). The host driver programs the **host** groups
(`pcie_ete_sr_reg_init` -> SR `+0x10`, `pcie_ete_dr_reg_init` -> DR `+0x30`); the **firmware**
programs the **device-local** groups (section 3).

---

## 2. The firmware's channel objects and the register-block pointer

The image carries two channel-object tables in its data area, each object starting with the channel's
**register block CA**:

| file offset | value | meaning |
| --- | --- | --- |
| `0xcf5d4` | `0x4003a590` | DR ch0 block CA |
| `0xcf928` | `0x4003a5e0` | DR ch1 block CA |
| `0xcfc7c` | `0x4003a630` | DR ch2 block CA |
| `0xcffd0` | `0x4003a680` | DR ch3 block CA |
| `0xd0324` | `0x4003a6d0` | DR ch4 block CA (no host entry - see section 3) |
| `0xd040c` | `0x4003a400` | SR ch0 block CA |
| `0xd04d8` | `0x4003a450` | SR ch1 block CA |
| `0xd05a4` | `0x4003a4a0` | SR ch2 block CA |
| `0xd0670` | `0x4003a4f0` | SR ch3 block CA (no host entry) |

[proven] values (4-byte little-endian words at those file offsets). Object sizes follow from the
loops in section 3: **`0x354`** for the DR table (`0x4003a590` family) and **`0xcc`** for the SR table
(`0x4003a400` family; e.g. `0xd040c + 0xcc = 0xd04d8`, `+0x198 = 0xd05a4`). Each DR object also
carries a device-local ring base at object `+0x38` (file `0xcf60c = 0x01060330`) and a flag at
`+0x54` (file `0xcf628 = 1`); each SR object carries `+0x08 = 2` and a device-local base at `+0x2c`
(file `0xd0414`, `0xd0438 = 0x01060650`).

The **register-block host VA** is a per-object field, filled at runtime from the CA above:

| family | object field | evidence |
| --- | --- | --- |
| SR (`0xcc` stride) | **`+0xb0`** | file `0x086496`: `ldr.w r3,[r4,#0xb0]` then `ldr r2,[r3,#0x10]` (reads that block's base register); file `0x086110`; file `0x0865c`/`0x086682` |
| DR (`0x354` stride) | **`+0x31c`** | file `0x0866f8`: `ldr.w r3,[fp,#0x31c]` then `ldr r2,[r3,#0x30]` (reads that block's DR base register); file `0x0948a` |

[proven] for the instructions and the register each pointer resolves to (`+0x10` for the `0xcc`
family, `+0x30` for the `0x354` family); the *identity* of the pointer fields with the CA table
entries is **[inferred]** (the CA->VA conversion happens in firmware code I did not need to map).

The firmware also knows the block-id words as data: file `0xccedc = 0x4003a000`, `0xccee0 = 0x40039508`,
`0xcf2a0 = 0x4003a000`, `0xcf2ac = 0x40039000`, `0xcf2b8 = 0x01060440` (the DR ch0 device-local base);
`reg_all.txt` confirms the ids (`4003a000 = 0x10a`, `40039000 = 0x10b`, `40039800 = 0x10c`).

---

## 3. The firmware's channel bring-up - `pcie_msg_init`, file `0x9334`

Two loops walk the two object tables and program the **device-local** group of each channel, then
set the enable. Both loop bodies are quoted instruction-for-instruction below (file offsets, with the
raw encoding, in the appendix).

### 3.1 DR channels - loop A, 4 iterations over the `0x354` table

Head (file `0x9370`): `mov.w sb,#3` / `0x9374 mov.w fp,#0x354` ; the object index is `sb-3` and the
loop exits at `cmp.w sb,#7` (file `0x949c`), so this walks exactly the **4 DR objects** of section 2
(`0x4003a590`, `0x4003a5e0`, `0x4003a630`, `0x4003a680`). The register-block pointer is `[obj+0x31c]`
(file `0x948a`).

```
0x9426  str  r1, [r2, #0x10]      ; DR block +0x10 = base          (r2 = [obj+0x31c])
0x9438  str  r1, [r2, #0x14]      ; DR block +0x14 = depth-1
0x9444  str  r2, [r3, #0x18]      ; DR block +0x18 = producer
0x9454  str  r2, [r3, #8]         ; DR block +0x08 = cfg[5] control
0x948a  ldr.w r3, [r3, #0x31c]    ; the DR channel register block
0x9490  ldr  r2, [r3]
0x9492  orr  r2, r2, #1
0x9496  str  r2, [r3]             ; DR block +0x00 |= 1  <-- PER-CHANNEL ENABLE
```

### 3.2 SR channels - loop B, 3 iterations over the `0xcc` table

Counter `r6`, exiting at `cmp r6,#3` (file `0x956c`); the register-block pointer is `[obj+0xb0]`
(file `0x956e` -> `0x94b6`..`0x9568`). This walks the 3 host-visible SR blocks (`0x400`, `0x450`,
`0x4a0`).

```
0x951c  str  r1, [r3, #0x30]      ; SR block +0x30 = base  (the device-local group)
0x952e  str  r0, [r1, #0x34]      ; SR block +0x34 = depth-1
0x953a  str  r1, [r3, #0x38]      ; SR block +0x38 = producer
0x9562  ldr  r2, [r3]
0x9564  orr  r2, r2, #1
0x9568  str  r2, [r3]             ; SR block +0x00 |= 1
```

**Correction to the record.** `docs/phase22/fw-sr-gate.md` §1.1/1.2 transcribes this as "4 entries with
the `+0x10` group and 3 with the `+0x30` group". The loop bounds are `sb = 3..6` (**4 objects**, the
`0x354` DR table, writing the `+0x10` group) and `r6 = 0..2` (**3 objects**, the `0xcc` SR table,
writing the `+0x30` group) - the counts agree, but the two tables are the **two object families** of
section 2, not a symmetric 7-entry walk. The same function also clears ETE-intr bits 12/29 and applies
`0xe0e0f8f8` (`0x96b0..0x96fc`) and registers its ETE handlers (the record's §1.3/1.4, unchanged).

### 3.3 The mailbox the firmware will transmit through

`pcie_msg_init` stores the six mailbox CAs into its own context (record §1.5, literals at file
`0x97d8 = 0x40101434`, `0x97dc = 0x40039014`, `0x97e0 = 0x40039010`):

```
0x0960 ... 0x0978c/0x0978e  *out[1] = 0 ; *out[0] = 0        (file 0x978c/0x978e)
```

[proven] for the literal values at those file offsets; the register identities are the record's
(unchanged, and re-confirmed by `reg_all.txt`: `40039010 = 0`, `40039014 = 0`).

---

## 4. The firmware's D2H transmit sequence

Three routines, in the order the device uses them.

### 4.1 The notify primitive - file `0x86170` (`d2h_notify(obj, id)`)

```
0x86170  cmp  r1, #0xa              ; id bound
0x86172  push {r3,r4,r5,r6,r7,lr}
0x86174  mov  r7, r1
0x86176  bhi  #0x861d6              ; id > 10 -> drop
0x86178  cbnz r0, #0x861d6          ; obj == NULL -> drop
0x8617a  ldr  r3, [pc, #0x60]       ; lit file 0x861dc = 0x00172130 (the firmware global)
0x8617c  ldr  r5, [r3]              ; r5 = g (the message ctx object)
...
0x8618a  ldr.w r1, [r5, #0xb8]      ; pending shadow
0x861a2  str.w r3, [r5, #0xb8]      ; shadow |= 1<<id
0x861a8  ldr.w r1, [r5, #0x9c]      ; obj+0x9c = out[1]  (CA 0x40039014)
0x861b0  str  r3, [r1]              ; *out[1] = pending mask           <-- THE D2H POST
0x861b2  ldr.w r4, [r5, #0xa4]      ; obj+0xa4 = the D2H doorbell
0x861bc  orr  r3, r3, #1
0x861c0  str  r3, [r4]              ; *doorbell |= 1                   <-- THE RING
```

The context binding `obj+0x9c = out[1]` and `obj+0xa4 = doorbell` is the record's
(`docs/phase22/fw-hostmem.md` §3.1) and is **[proven]** by file `0x9784`/`0x9764` where those two
mailbox CAs were stored into exactly those fields.

The doorbell's address is pinned arithmetically by the sibling routine (section 4.3):
`out[1] = 0x40039014`, `+ 0xc8000 + 0x420 = 0x40101434` -
`0x86f5e add.w r3,r3,#0xc8000` / `0x86f62 add.w r3,r3,#0x420` / `0x86f66 str r2,[r3]`.
So **the firmware's D2H doorbell write is CA `0x40101434`**, matching the record's D2H doorbell and
the port's `OMO_D2H_ACK`/`OMO_D2H_REARM` neighbourhood.

### 4.2 The flush routine - file `0x86108`

```
0x8610c  cbz  r0, #0x8611e          ; obj == NULL -> return
0x8610e  movs r2, #1
0x86110  ldr.w r3, [r0, #0xb0]      ; the channel register block
0x86114  str  r2, [r3]              ; channel +0x00 = 1  <-- RE-ASSERTS THE ENABLE
0x86116  mov.w r3, #0x10000         ; bounded spin (~64 Ki iterations)
0x86120  ldr.w r2, [r4, #0x9c]      ; out[1]
0x86124  ldr  r6, [r2]
0x86126  cmp  r6, #0
0x86128  bne  #0x8611a              ; keep spinning while *out[1] != 0
0x86156  str  r3, [r2]              ; (under obj+0xc0 lock) *out[1] = obj+0xb8
0x86158  ldr.w r2, [r4, #0xa4]      ; the doorbell
0x86162  orr  r3, r3, #1
0x86166  str  r3, [r2]              ; doorbell |= 1
```

**This is the device-side precondition**: before the firmware will publish a D2H word it (a) insists
on the per-channel enable and (b) **waits for `out[1]` to read 0** - i.e. the host must have consumed
the previous `out[1]` word. The `[r0+0xb0]` register pointer makes the enable write a **`0xcc`-family
(SR-side) channel `+0x00`** write [inferred from the `+0xb0` field, section 2].

### 4.3 The ready / hello announce - file `0x86e14` (the `+0x40000`-register writer)

The routine the record calls the "device's ETE bring-up" (`docs/phase37/THE-PROTOCOL-MAP.md` §1.1
step 4). Its full register-touch list, in order:

```
0x86e36  ldr  r2, [pc, #0x158]  -> lit 0x86f90 = 0x40101250
0x86e38  ldr  r3, [r2]
0x86e3a  bic  r3, r3, #0x20
0x86e3e  str  r3, [r2]              ; CA 0x40101250: clear bit 5
0x86e42  str.w r3, [r5, #0xbe0]     ; r5 = 0x0017d398 (device RAM): stage bit 1
0x86e54  str  r2, [r3]              ; CA 0x40101250 = 0x00010002     (lit 0x86f98)
0x86e6c  str  r2, [r3]              ; CA 0x40161f00 = 0x00020002     (lit 0x86f94 = 0x40161f00)
0x86e7a  str  r3, [r2]              ; CA 0x4003022c = 0            (lit 0x86f9c)
0x86e7e  strh r3, [r2]              ; CA 0x4000010c = 0            (lit 0x86fa0)
0x86e80  strh r3, [r2, #-0x4]       ; CA 0x40000108 = 0  (the release register, zeroed)
0x86e96  strh r1, [r3]              ; lit 0x86fa4 = 0x00040008 : OR a halfword bit
0x86e9e..0x86ede                    ; 17 words copied from r0(arg) to CA 0x40000c..0x4004c
0x86ee8/0x86ef4 blx #0xc2ee4        ; cache ops over [0x40000,0x1c0000), [0x1000000,0x1060000)
0x86f04..0x86f20                    ; 3-entry copy table (lit 0x86fa8 = 0x0010f294, dst 0x010db800)
0x86f3e  bl #0x7c0 ; 0x86f44 bl #0x78e   ; r0 = 0x40039000 (lit 0x86fb4)
0x86f4a  bl #0x7c0 ; 0x86f50 bl #0x78e   ; r0 = 0x40039800 (lit 0x86fb8)
0x86f54  movs r2, #4
0x86f56  ldr  r3, [pc, #0x64]       ; lit 0x86fbc = 0x40039014  (out[1])
0x86f5a  str  r2, [r3]              ; *out[1] = 4      (bit 2, "platform ready")
0x86f5e  add.w r3, r3, #0xc8000
0x86f62  add.w r3, r3, #0x420       ; r3 = 0x40039014 + 0xc8420 = 0x40101434
0x86f66  str  r2, [r3]              ; *0x40101434 = 1  <-- THE D2H DOORBELL, pinned
0x86f74  movw r3, #0xcece
0x86f78  ldr  r2, [r1]              ; r1 = lit 0x86fa0 = 0x4000010c
0x86f7a  cmp  r2, r3
0x86f7c  bne  #0x86f78              ; SPIN until *(CA 0x4000010c) == 0xcece
```

**Two cautions, stated plainly.** (1) The spin literal is `0x4000010c`, **not** `0x4003022c`: the
instruction at `0x86f58` is the 2-byte `ldr r1,[pc,#0x44]` (bytes `11 49`), so
`lit = Align(0x86f5c,4) + 0x44 = 0x86fa0 = 0x4000010c`. `0x4003022c` (literal `0x86f9c`) is written
to, earlier, at `0x86e7a`. (2) `reg_all.txt` is a *snapshot of the released, idle vendor stack* and
shows `0x40000108 = 0x5a5a`, `0x4000010c = 0xdeaf` - **not** the values this routine writes - so this
routine is not standing in that state at sample time. Whether it is the firmware-download handshake or
a per-boot announce is **[unknown]** to me; the instructions above are what the image contains.

---

## 5. The deposit commit: what the firmware does *not* write

This is the load-bearing negative, and it is an exhaustive statement about the code I mapped.

1. **No firmware store to the host aperture.** A scan of every 4-byte-aligned word in the image for
   values in `0x40030000..0x4007ffff` (device registers) returns 44 data-table entries and **no code
   literal for any ETE channel CA** (they exist only as object-table data, section 2). A scan of the
   pc-relative literal pools for `0x80000000..0x8fffffff` returns six hits, all of which are constants
   *stored into device-RAM structs* (e.g. `0x49978 str.w r3,[r4,#0x3dc]`), none of them store bases.
   The firmware therefore does not write host DRAM directly.
2. **No runtime write to any DR-channel register.** The only runtime channel-register stores in the
   image are on the `0xcc` (SR) family, at files `0x86678` and `0x86682`:

```
0x8665c  ldr.w r3, [r4, #0xb0]      ; the SR-family channel register block
0x86674  ldr  r2, [r4, #0x1c]       ; ring index A
0x86678  str  r2, [r3, #0x38]       ; block +0x38 = index A
0x8667e  ldr  r2, [r4, #0x28]       ; ring index B
0x86682  str  r2, [r3, #0x18]       ; block +0x18 = index B
```

   The corresponding DR-family site (`0x354` stride, `+0x31c` pointer) only **reads** the DR base:

```
0x866f8  ldr.w r3, [fp, #0x31c]     ; DR channel register block
0x866fc  ldr  r2, [r3, #0x30]       ; DR block +0x30 = the HOST ring base
0x866fe  cmp  r2, #0
0x86702  ldr  r3, [r3, #0x30]
0x86704  str.w r3, [fp, #4]         ; cache it in the object
```

   No `str` to a DR block offset exists anywhere in the image. **[proven]** by the pointer-site scan
   (all `[rX,#0xb0]` / `[rX,#0x31c]` uses, 20 sites, listed and read).
3. Therefore the **deposit commit** - the DR ring's device index advancing at `block+0x3c` and the
   payload landing in the node's buffer - is a **hardware** act of the ETE engine, exactly as the
   record's phase-20 A.6/A.7 concluded from the host side. The firmware's contribution to a D2H
   transfer is the enable, the device-local-group programming, and the mailbox notify.
4. **The `0x4004xxxx` registers are not part of this path.** The task's anchors
   `0x4004a004 = 0x84a90000` / `0x4004a008 = 0x6140` sit in the **2g/5g MAC register blocks**, not in
   an ETE DR channel: the sibling report `docs/phase43/channel-array-map.md` settles the array's
   identity (the `0x40040000`/`0x40060000` blocks carry block id `0x100`, the same inbound region-3
   window as the ETE block's `0x10a`), and the firmware's own copy of that array is built at file
   `0x92044..0x9208e` (base `0x40042000`, lit `0x920f4`, `+0x2000` steps into context `+0x260..+0x284`).
   No firmware store site targets those blocks' `+4`/`+8` fields, so the live `0x84a90000` ring base is
   written by the MAC/its host driver, and it is a *different* D2H surface from the ETE DR rings the
   port posts ([proven] for the absent store sites; block identity per the sibling report).

---

## 6. The precondition the device checks before transmitting

| # | precondition | evidence | note |
| --- | --- | --- | --- |
| P1 | the channel's `+0x00` **enable** must be 1 - the firmware sets it itself before publishing | `0x86110`/`0x86114` (`*(obj->[0xb0]) = 1`), and `0x9490..0x9496` / `0x9562..0x9568` at init | [proven] |
| P2 | **`out[1]` (CA `0x40039014`) must read 0** - the host has consumed the previous notification - before the firmware publishes the next word | `0x86120..0x86128` (bounded spin on `*out[1]`) | [proven] - this is the device-side "keep the mailbox drained" condition |
| P3 | the notify id must be `<= 10` | `0x86170 cmp r1,#0xa` / `0x86176 bhi` | [proven] |
| P4 | the announce path waits for a host-written magic: `*(CA 0x4000010c) == 0xcece` | `0x86f74..0x86f7c` | [proven] instructions; the peer that writes `0xcece` and the meaning of the magic are **[unknown]** |
| - | there is **no** device-side "DR ring armed" check | no firmware read of `DR+0x30` gates anything (`0x866f8..0x86704` only caches it) | [proven negative] |

**Answer to the task's question, stated precisely.** The device-side preconditions for a D2H transmit
are the **mailbox** conditions P1-P3; the DR channel's own host-ring registers (`+0x30/+0x34/+0x38`)
are **programmed by the host** and are only *read* by the firmware. The DR-channel register set that
the **vendor's device-side code sets** and that the **port's ETE programming never sets** is:

| register | CA / BAR0 | firmware writer | port |
| --- | --- | --- | --- |
| **`block+0x00` enable**, DR blocks `0x590/0x5e0/0x630/0x680` | CA `0x4003a590`, `0x4003a5e0`, `0x4003a630`, `0x4003a680` = BAR0 `0x3f2590`, `0x3f25e0`, `0x3f2630`, `0x3f2680` | `0x9490..0x9496` (loop A, 4 DR objects) | **never** - `omo_ete_program` writes only base/depth/wptr for DR (`wifidrv1.c:534..545`), `omo_dr_post` writes only `DR+0x38`; the port's only `+0x00`/`+0x48` writes are on the **SR** blocks (`omo_sr_post`, `wifidrv1.c:1644..1655`) |
| **`block+0x10/+0x14/+0x18`** (the DR channel's device-local program group) | same blocks, `+0x10/+0x14/+0x18` | `0x9426`, `0x9438`, `0x9444` (loop A) | **never** - the port programs only the DR `+0x30` group |

**Important scope limit on that answer.** Both rows are written by the firmware *itself* when
`pcie_msg_init` runs, and `docs/phase20/fw-accept.md` B.1 already measured the takeover's own
per-channel enable writes as no-ops for that reason. So this register set is a real device-side
activation path that the port omits, **but it is not by itself the observed deposit gap**: the port's
`omo_ete_program` runs *before* the firmware is released, and the firmware's own `+0x00 |= 1` lands
later. The residual gap the record already names - the device's message service never being entered
(`docs/phase22/fw-hostmem.md` §4, `docs/phase42/THE-RING-MAP.md` §3) - is **not** a channel register
in this image; the firmware's D2H transmit is only ever triggered by the mailbox/`0x86170` path and
the engine.

---

## 7. Host visibility of each firmware-side register touch

`BAR0 = 0x3b8000 + (CA - 0x40000000)` on the endpoint's region-3 window (the port's own rule). Region 3
decodes CA `0x40000000..0x4011ffff` (`docs/phase42/ete-registers-reconciled.md` A.4/A.5).

| firmware touch | CA | host-visible at | visible? |
| --- | --- | --- | --- |
| `*out[1] = 4` (ready announce), `*out[1] = mask` (`d2h_notify`) | `0x40039014` | `0x3f1014` (the port's `omo_msg + 0x014` / `OMO_MSG1`) | **yes** - the port already polls it |
| D2H doorbell `|= 1` | `0x40101434` | `0x4b8434` (region 3; the port maps this window as `omo_rel`, cf. `omo_rel + 0x101438` = the D2H ack) | **yes**, and the port writes only the ack `0x101438` / re-arm `0x101414` today |
| channel `+0x00 |= 1`, `+0x10/+0x14/+0x18` writes | CA `0x4003a4xx`, `0x4003a5xx`, `0x4003a6xx` | ETE window `0x3f24xx`/`0x3f25xx`/`0x3f26xx` | **yes** - the port already reads/writes those offsets (`omo_ete`) |
| `*0x4000010c = 0`, `*0x40000108 = 0` | `0x4000010c`, `0x40000108` | `0x3b810c`, `0x3b8108` | **yes** - `0x40000108` is the port's own release register (`OMO_RELEASE_OFF 0x3b8108`) |
| `*0x4003022c = 0` | `0x4003022c` | `0x3e822c` | **yes** (inside region 3's decoded range), though the port does not map this offset |
| `*0x40101250`, `*0x40161f00` | `0x40101250`, `0x40161f00` | `0x4b9250`; `0x40161f00` is **outside** region 3's `..0x4011ffff` decode | `0x40101250` yes; `0x40161f00` no (different window) |
| ETE intr block mask `0xe0e0f8f8`; handler registration | `0x40039508` | `0x3f1508` | **yes** - the port already masks this (`OMO_ETE_INTR_OFF 0x508`) |
| device-local group writes, cache ops, `0x010db800` copies | device RAM | - | **no** |

So: **every channel register the firmware writes is host-visible**, and so are both mailbox registers
and the D2H doorbell. The firmware's *internal* state (the `0x354`/`0xcc` channel objects, the
`0x0017d398` message object, the `0x010db800` staging area) is device RAM and is not.

---

## 8. What this does not settle, and the specific missing evidence

1. **Which routine actually emits a steady-state D2H data message is still [unknown].** `0x86170`
   is *a* D2H notify primitive and `0x86e14` is *a* ready announce, but the record's phase-37 gap 9
   stands: there is no static `bl`/BLX or literal naming `0x86170`, so the emitter->id mapping is not
   recoverable from this image alone. **Missing evidence:** a device-side trace, or a second firmware
   image whose call graph is intact.
2. **The emitter of `0xcece` into CA `0x4000010c`** is not in this image (the firmware only *waits*
   for it). **Missing evidence:** the host-side loader/rom code, or a live read of `0x4000010c`
   immediately after the release write.
3. **Whether the DR channel's `+0x10` group must be non-zero for the engine to deposit** - the
   firmware writes it, the port does not; nothing in this image *reads* it as a gate. **Missing
   evidence:** a live vendor read of a DR block with its `+0x10` group zeroed, or a takeover run that
   programs it and observes `DR+0x3c`.
4. **The writer of `block+0x48 (=1 live on all 7)`** - no store site exists in the image. It is
   either the host module or hardware. **Missing evidence:** a host-module channel-init transcription.

---

## 9. Verification - every quoted offset disassembles to the claimed instruction

`cs_disasm(CS_ARCH_ARM, CS_MODE_THUMB)` at each file offset; bytes are the raw encoding.

| file | bytes | instruction | rt |
| --- | --- | --- | --- |
| `0x9426` | `1161` | `str r1, [r2, #0x10]` | `0x49426` |
| `0x9438` | `5161` | `str r1, [r2, #0x14]` | `0x49438` |
| `0x9444` | `9a61` | `str r2, [r3, #0x18]` | `0x49444` |
| `0x9454` | `9a60` | `str r2, [r3, #8]` | `0x49454` |
| `0x948a` | `d3f81c33` | `ldr.w r3, [r3, #0x31c]` | `0x4948a` |
| `0x9490` | `1a68` | `ldr r2, [r3]` | `0x49490` |
| `0x9492` | `42f00102` | `orr r2, r2, #1` | `0x49492` |
| `0x9496` | `1a60` | `str r2, [r3]` | `0x49496` |
| `0x949c` | - | `cmp.w sb, #7` (loop A bound) | `0x4949c` |
| `0x951c` | `1963` | `str r1, [r3, #0x30]` | `0x4951c` |
| `0x952e` | `4863` | `str r0, [r1, #0x34]` | `0x4952e` |
| `0x953a` | `9963` | `str r1, [r3, #0x38]` | `0x4953a` |
| `0x9562` | `1a68` | `ldr r2, [r3]` | `0x49562` |
| `0x9564` | `42f00102` | `orr r2, r2, #1` | `0x49564` |
| `0x9568` | `1a60` | `str r2, [r3]` | `0x49568` |
| `0x956c` | - | `cmp r6, #3` (loop B bound) | `0x4956c` |
| `0x8610c` | `38b1` | `cbz r0, #0x8611e` | `0xc610c` |
| `0x86110` | `d0f8b030` | `ldr.w r3, [r0, #0xb0]` | `0xc6110` |
| `0x86114` | `1a60` | `str r2, [r3]` | `0xc6114` |
| `0x86120` | `d4f89c20` | `ldr.w r2, [r4, #0x9c]` | `0xc6120` |
| `0x86124` | `1668` | `ldr r6, [r2]` | `0xc6124` |
| `0x86126` | `002e` | `cmp r6, #0` | `0xc6126` |
| `0x86128` | `f7d1` | `bne #0x8611a` | `0xc6128` |
| `0x86156` | `1360` | `str r3, [r2]` | `0xc6156` |
| `0x86158` | `d4f8a420` | `ldr.w r2, [r4, #0xa4]` | `0xc6158` |
| `0x86162` | `43f00103` | `orr r3, r3, #1` | `0xc6162` |
| `0x86166` | `1360` | `str r3, [r2]` | `0xc6166` |
| `0x86170` | - | `cmp r1, #0xa` | `0xc6170` |
| `0x86176` | - | `bhi #0x861d6` | `0xc6176` |
| `0x86178` | `68bb` | `cbnz r0, #0x861d6` | `0xc6178` |
| `0x8618a` | `d5f8b810` | `ldr.w r1, [r5, #0xb8]` | `0xc618a` |
| `0x861a8` | `d5f89c10` | `ldr.w r1, [r5, #0x9c]` | `0xc61a8` |
| `0x861b0` | `0b60` | `str r3, [r1]` | `0xc61b0` |
| `0x861b2` | `d5f8a440` | `ldr.w r4, [r5, #0xa4]` | `0xc61b2` |
| `0x861bc` | `43f00103` | `orr r3, r3, #1` | `0xc61bc` |
| `0x861c0` | `2360` | `str r3, [r4]` | `0xc61c0` |
| `0x8665c` | `d4f8b030` | `ldr.w r3, [r4, #0xb0]` | `0xc665c` |
| `0x86674` | `e269` | `ldr r2, [r4, #0x1c]` | `0xc6674` |
| `0x86678` | `9a63` | `str r2, [r3, #0x38]` | `0xc6678` |
| `0x8667e` | `a26a` | `ldr r2, [r4, #0x28]` | `0xc667e` |
| `0x86682` | `9a61` | `str r2, [r3, #0x18]` | `0xc6682` |
| `0x866f8` | `dbf81c33` | `ldr.w r3, [fp, #0x31c]` | `0xc66f8` |
| `0x866fc` | `1a6b` | `ldr r2, [r3, #0x30]` | `0xc66fc` |
| `0x86702` | `1b6b` | `ldr r3, [r3, #0x30]` | `0xc6702` |
| `0x86704` | `cbf80430` | `str.w r3, [fp, #4]` | `0xc6704` |
| `0x86e3a` | `23f02003` | `bic r3, r3, #0x20` | `0xc6e3a` |
| `0x86e3e` | `1360` | `str r3, [r2]` | `0xc6e3e` |
| `0x86e42` | `c5f8e03b` | `str.w r3, [r5, #0xbe0]` | `0xc6e42` |
| `0x86e54` | `1a60` | `str r2, [r3]` | `0xc6e54` |
| `0x86e6c` | `1a60` | `str r2, [r3]` | `0xc6e6c` |
| `0x86e7a` | `1360` | `str r3, [r2]` | `0xc6e7a` |
| `0x86e7e` | `1380` | `strh r3, [r2]` | `0xc6e7e` |
| `0x86e80` | `22f8043c` | `strh r3, [r2, #-0x4]` | `0xc6e80` |
| `0x86e96` | `1980` | `strh r1, [r3]` | `0xc6e96` |
| `0x86e9e` | `5a60` | `str r2, [r3, #4]` | `0xc6e9e` |
| `0x86ede` | `5a64` | `str r2, [r3, #0x44]` | `0xc6ede` |
| `0x86f3e` | `79f73ffc` | `bl #0x7c0` | `0xc6f3e` |
| `0x86f44` | `79f723fc` | `bl #0x78e` | `0xc6f44` |
| `0x86f5a` | `1a60` | `str r2, [r3]` | `0xc6f5a` |
| `0x86f5e` | `03f54823` | `add.w r3, r3, #0xc8000` | `0xc6f5e` |
| `0x86f62` | `03f58463` | `add.w r3, r3, #0x420` | `0xc6f62` |
| `0x86f66` | `1a60` | `str r2, [r3]` | `0xc6f66` |
| `0x86f74` | `4cf6ce63` | `movw r3, #0xcece` | `0xc6f74` |
| `0x86f78` | `0a68` | `ldr r2, [r1]` | `0xc6f78` |
| `0x86f7a` | `9a42` | `cmp r2, r3` | `0xc6f7a` |
| `0x86f7c` | `fcd1` | `bne #0x86f78` | `0xc6f7c` |

Literal pools (data, quoted as literals): file `0x86f88 = 0x0017d398`, `0x86f8c = 0x00105fdc`,
`0x86f90 = 0x40101250`, `0x86f94 = 0x40161f00`, `0x86f98 = 0x00010002`, `0x86f9c = 0x4003022c`,
`0x86fa0 = 0x4000010c`, `0x86fa4 = 0x00040008`, `0x86fa8 = 0x0010f294`, `0x86fac = 0x010db800`,
`0x86fb0 = 0x40000558`, `0x86fb4 = 0x40039000`, `0x86fb8 = 0x40039800`, `0x86fbc = 0x40039014`,
`0x86fc4 = 0x010dffff`.

Channel-object table bytes (data): `0xcf5d4 = 0x4003a590`, `0xcf5e4 = 0x4003ac00`,
`0xcf60c = 0x01060330`, `0xcf628 = 0x00000001`, `0xcf928 = 0x4003a5e0`, `0xcfc7c = 0x4003a630`,
`0xcffd0 = 0x4003a680`, `0xd0324 = 0x4003a6d0`, `0xd040c = 0x4003a400`, `0xd0414 = 0x00000002`,
`0xd0418 = 0x4003ac18`, `0xd0438 = 0x01060650`, `0xd04d8 = 0x4003a450`, `0xd05a4 = 0x4003a4a0`,
`0xd0670 = 0x4003a4f0`, `0xd067c = 0x4003ac24`; the 7-entry cfg table starts `0xc4054 = 0x00000400`.

Live register cross-checks used above (`reg_all.txt`): `4003a000 = 0000010a`, `4003a400 = 00000001`
(SR0 enable), `4003a410 = 844db000`, `4003a590 = 00000001` (DR0 enable), `4003a5c0 = 844d8000`
(DR0 host base), `4003a5c8 = 00000003` / `4003a5cc = 00000003` (DR0 index pair), `4003a448 = 1`
(SR0 `+0x48`), `4003a5d8 = 1` (DR0 `+0x48`), `40039000 = 0000010b`, `40039800 = 0000010c`,
`40039010 = 0`, `40039014 = 0`, `40000108 = 00005a5a`, `4000010c = 0000deaf`,
`40101434 = 0`, `40101410 = 1`, `40101430 = 1`, `40030100 = 0`, `4003022c = 1`.

## Reproduce

```
PY=pyenv/Scripts/python.exe
$PY - <<'EOF'
from capstone import *
d=open('build/tmp/FIRMWARE.bin','rb').read()
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); md.skipdata=True
for a,b,label in [(0x9334,0x9570,'pcie_msg_init channel loops'),
                  (0x86108,0x86240,'flush + d2h_notify'),
                  (0x863c0,0x86800,'channel walk (the only runtime channel stores)'),
                  (0x86e14,0x86f90,'ready/hello announce')]:
    print('==',label,'==')
    for i in md.disasm(d[a:b],a):
        print('  %05x rt%05x  %-8s %s'%(i.address,i.address+0x40000,i.mnemonic,i.op_str))
EOF
```

No file other than this one was written; the device was not touched.
