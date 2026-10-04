# ete-registers-reconciled: the ETE ring registers, the window they live in, and why the port's register offsets can carry frame data (phase 42, 2026-10-04)

Task `st_01a10709`. Static + read-only only: the two vendor modules, the firmware image, the three
2026-10-04 window snapshots, and the record's own register tables. No device access, no writes.

**Headline.** The ETE ring control registers are real and their addresses are settled: the register
block is device CA `0x4003a000` plus a per-channel offset (`SR0..2` `+0x400/0x450/0x4a0`, `DR0..3`
`+0x590/0x5e0/0x630/0x680`), reached on BAR0 as `0x3f2000 + cfg[0]` **only through the vendor's
inbound region mapping**. The 2026-10-04 snapshots are *not* that mapping: their window carries no
block-id word and is populated 4 KiB **past the end of the decoded ETE block** (CA `0x4003b000`, which
every faithful vendor BAR0 dump reads `0xffffffff`). So what the port reads at BAR0 `0x3f2000..` is
whatever the window is *bound to*, and when the binding is the device's D2H staging area it is frame
data, not registers. The port's offsets are correct as *offsets inside the vendor's region-3 window*;
they are **not** self-verifying, and the port never checks the one word that separates the two states.
Every claim below is **[proven]** (an instruction, a relocation, a constant in an image, or a md5'd
dump) or **[inferred]**.

---

## 0. Sources and method

| source | identity |
| --- | --- |
| `build/tmp/hi5622v100_plat.ko` | 364660 B, md5 `23660bc285393e678d5cade1c36c194b` |
| `build/tmp/hi5622v100_wifi.ko` | 3564728 B, md5 `4737fcb21a1a2262a96f84d780ad8b35` (no ETE register CA constant of any kind - 0 hits for `0x40039000/0x4003a000/0x4003a400/0x40039508`) |
| `build/tmp/FIRMWARE.bin` | 928920 B, md5 `0e530b976d5a20e87358671f1a577695`; runtime = file + `0x40000` |
| `build/tmp/msg-normal-op-20261004.txt` | window `0x3f1000..0x3f1fff`, 1024 words, 2026-10-04 |
| `build/tmp/ete-normal-op-20261004.txt` | window `0x3f2000..0x3f2fff`, 1024 words, 2026-10-04 |
| `build/tmp/ete2-normal-op-20261004.txt` | window `0x3f3000..0x3f3fff`, 1024 words, 2026-10-04 |
| `opensource/build/register-dumps/barmap_ep0_bar0.bin` | 16 MiB full BAR0, vendor boot, md5 `40a1da524539c3392d3c04694ffafb4d` |
| `opensource/build/register-dumps/diff_before.bin` / `diff_after.bin` | 16 MiB full BAR0, vendor boot, two instants, md5 `fcf1d71342e76bc5227cdf59eb155d63` / `699389787573b62b5793fd03a43eb17e` |
| `opensource/build/register-dumps/barsnap_ep0_bar0.bin` / `_ep1_` | 1 MiB each (stops *below* the register window, `docs/phase11/barsnap.md`) |

Method: `capstone` `CS_MODE_ARM` over `.text` with the module's `.rel.text` applied, so every quoted
`bl` carries its relocation symbol. In this `.ko` `.text` has `sh_addr = 0` and `sh_offset = 0x38`, so
**file offset = vaddr + 0x38** for `.text`; the offsets quoted below are vaddrs (the convention every
earlier phase uses, e.g. `docs/phase21/both-eps.md` quotes `0x007860: str r7, [r4, #0x84]` for
`pcie_ete_init`). The firmware's CA references are *data* (literal pools/tables), and are labelled as
such, not as instructions.

---

## A. The register map, from the module and its tables

### A.1 The ETE block CA and the interrupt-block CA are literals in the module

`.data` file offset `0x348a4` holds the ETE resource struct, two words:

```
0348a4 = 4003a000     <-- ETE ring/register block CA
0348a8 = 40039508     <-- ETE interrupt block CA
```

It is reached exactly there: `pcie_ete_rings_init` @`0x7680` takes the ring CA from it -

```
0x0076e8: ldr  r1, [r4, #0x18]          ; ete_ctx+0x18 = get_pcie_ete_res() result (the struct above)
0x0076ec: ldr  r3, [r7]                 ; r7 = chip ctx -> [chip] = device array
0x0076f0: ldr  r1, [r1]                 ; res[0x00] = 0x4003a000
0x0076f4: ldr  r3, [r3]                 ; device[0]
0x0076f8: ldr  r0, [r3, #4]             ; device[0]+4 = the per-chip region/viewport table
0x0076fc: bl   oal_pcie_inbound_ca_to_va
0x007708: ldr  r7, [sp, #4]             ; out[0] = the host VA of CA 0x4003a000
0x007714: bl   pcie_ete_init_src_ring
0x007728: bl   pcie_ete_init_dst_ring
```

and `pcie_ete_intr_init` @`0x7528` takes the interrupt CA from the same struct, one word later:

```
0x007568: ldr  r1, [r1, #0x18]          ; ete_ctx+0x18 = the ETE resource struct
0x007580: ldr  r1, [r1, #4]             ; res[0x04] = 0x40039508
0x00758c: bl   oal_pcie_inbound_ca_to_va
0x00759c: str  r3, [r4, #0xc]           ; ete_ctx+0x0c = the mapped interrupt block
0x0075b0: movt r2, #0xffe0
0x0075b4: and  r2, r2, r1               ; & 0xffe0f8f8
0x0075b8: str  r2, [r3]                 ; *intr_block &= 0xffe0f8f8
```

The firmware image names the same pair as data - file `0xccedc` = `0x4003a000`, file `0xccee0` =
`0x40039508` (and again at file `0xcf2a0`) - and the SR0 block CA as data at file `0xd040c`
(`0x4003a400`) and DR0's at file `0xcf5d4` (`0x4003a590`). So host and device agree on the CA.

### A.2 The channel offsets are a 7-entry table in the module's `.rodata`

`pcie_ete_get_chn_cfg` @`0x15f00` returns `.rodata + 0x101c + 0xc*i`:

```
0x015f00: cmp   r0, #6
0x015f04: movls r2, #0xc
0x015f08: ldrls r3, [pc, #8]            ; lit[0x15f18] = 0x0000101c, R_ARM_ABS32 -> .rodata section
0x015f0c: mlals r0, r2, r0, r3
0x015f10: movhi r0, #0
```

`.rodata` starts at file `0x1fe40`, so the table is at file `0x20e5c`:

```
0x020e5c = 00000400  0x020e60 = 06800020  0x020e64 = 00000001   <- SR ch0
0x020e68 = 00000450  0x020e6c = 06800020  0x020e70 = 00000001   <- SR ch1
0x020e74 = 000004a0  0x020e78 = 06800020  0x020e7c = 00000001   <- SR ch2
0x020e80 = 00000590  0x020e84 = 06800020  0x020e88 = 00000001   <- DR ch3
0x020e8c = 000005e0  0x020e90 = 06800020  0x020e94 = 00000000
0x020e98 = 00000630  0x020e9c = 06800020  0x020ea0 = 00000000
0x020ea4 = 00000680  0x020ea8 = 06800020  0x020eac = 00000000
```

`cfg[0]` is the block offset from the ETE CA; `cfg[4]` (byte 4 of the 12-byte record = `0x20`) is the
depth (`32`, used as `depth-1`); `cfg[5]` is the low 3 control bits (`0`). `pcie_ete_init_src_ring`
@`0x7068` builds each channel's register VA as `hostVA(0x4003a000) + cfg[0]`:

```
0x007160: str  r0, [r1, #0xdc]          ; per-channel +0xdc = ETE base VA + cfg[0]  (r0 = arg + cfg[0])
```

### A.3 The field offsets, from the register-init routines

`pcie_ete_sr_reg_init` @`0x14a48` (SR block VA in `[r4+0xdc]`, node array in `[r4+0xe8]`, index in
`[r4+0xc]`, cfg in `[r4+8]`):

```
0x014a58: ldr  r5, [r4, #0xdc]          ; r5 = SR block VA
0x014aa0: ldr  r2, [r4, #0xe8]          ; node array host address
0x014aac: bl   pcie_hostca_to_devva
0x014ab0: str  r0, [r5, #0x10]          ; SR+0x10 = base (device VA)
0x014ac0: ldrb r3, [r3, #4]             ; cfg byte 4 = 32
0x014ad0: str  r1, [r2, #0x14]          ; SR+0x14 = depth-1 (bfi[r1],#0,#0xa)
0x014adc: str  r2, [r3, #0x18]          ; SR+0x18 = producer (wptr)
0x014af4: str  r2, [r3, #8]             ; SR+0x08[2:0] = cfg[5]
```

`pcie_ete_dr_reg_init` @`0x1483c` (DR block VA in `[r1+0x50]`, node array in `[r1+0x54]`):

```
0x014854: ldr  r6, [r1, #0x50]          ; r6 = DR block VA
0x014874: ldr  r2, [r1, #0x54]          ; node array host address
0x014884: bl   pcie_hostca_to_devva
0x014888: str  r0, [r6, #0x30]          ; DR+0x30 = base (device VA)
0x0148a8: str  r1, [r2, #0x34]          ; DR+0x34 = depth-1
0x0148b4: str  r2, [r3, #0x38]          ; DR+0x38 = producer
```

So `SR+0x08 = ctrl/enable`, `+0x10 = base`, `+0x14 = depth-1`, `+0x18 = wptr`, and the pair's second
word `+0x1c` is the consumer (the record's own live pair read, `docs/phase25/live-vendor-ring-ground-truth.md`:
`SR ch0: ctrl=0x00000000 base=0x848F6000 wptr=0x0000041B rptr=0x00000002`, unequal - a real
producer/consumer pair, which is why the two words exist). `DR+0x30 = base`, `+0x34 = depth-1`,
`+0x38 = producer`, `+0x3c` its paired index.

### A.4 The host VA is a *runtime lookup*, never a constant

`oal_pcie_inbound_ca_to_va` @`0x8dd8` walks a per-chip descriptor array (`r0+0x20` = array, `r0+0x24` =
count, stride `0x50`) and returns `va_lo + (CA - ca_base)`:

```
0x008df0: ldr  ip, [r0, #0x20]          ; descriptor array
0x008df8: ldr  r5, [r0, #0x24]          ; count
0x008e34: ldr  lr, [ip]                 ; desc+0x00 = host VA lo
0x008e44: ldr  r1, [ip, #0x28]          ; desc+0x28 = CA base lo
0x008e48: ldr  r0, [ip, #0x2c]          ; desc+0x2c = CA base hi
0x008e58: ldr  r0, [ip, #0x30]          ; desc+0x30 = CA limit lo
0x008e60: ldr  r0, [ip, #0x34]          ; desc+0x34 = CA limit hi
0x008e6c: subs r4, r4, r1               ; ca - ca_base
0x008e74: add  lr, lr, r4
0x008e78: str  lr, [r2]                 ; out[0] = the VA
0x008e90: add  ip, ip, #0x50            ; next descriptor
```

The same descriptors are what the region programming writes - `pcie_inbound_region_cfg` @`0x94e0`
(`docs/phase18/inbound-map.md` A.3) uses `desc+0x10` base, `desc+0x18` target, `desc+0x38` size,
`desc+0x48` the BAR number. Six such descriptors exist (`.data` entries of `0x50` bytes at file
`0x34950/0x349a0/0x349f0/0x34a40/0x34a90/0x34ae0`); the one that contains the ETE CA carries the range
`0x40000000..0x4011ffff` (entry at file `0x34a40`: `[0x00]=0x40000000`, `[0x08]=0x4011ffff`,
`[0x10]=0x40000000`, `[0x18]=0x4011ffff`), which is exactly the live dmesg range `region idx:3
paddr:0x583b8000 size:1179648` (`docs/phase21/sr-sibling.md` A.3). **[inferred]** for the table's
`base/target` roles; [proven] for the lookup instruction sequence and for `0x3b8000 + (CA -
0x40000000)` being the region-3 offset rule (`docs/phase23/register-windows.md`, `docs/phase7/userspace-bar-access.md`).

### A.5 The record's own register table, and the one row that is wrong

`docs/phase23/register-windows.md` is the authoritative table; its ETE row and rule:

```
| ETE rings | `0x4003a000` | `0x3f2000` | SR ch `+0x400/0x450/0x4a0` stride `0x114`; DR ch `+0x590/0x5e0/0x630/0x680` stride `0x6c` |
resource0_offset(CA) = 0x3b8000 + (CA - 0x40000000)
```

The **block offsets are right and the strides are wrong**. `0x114` and `0x6c` are the strides of the
*host-side per-channel software structs*, not of the register blocks:

```
pcie_ete_init_src_ring  0x0070d0: mov  r3, #0x114    ; SR struct-array stride ([chip+0x14] + i*0x114)
pcie_ete_init_dst_ring  0x007384: add  r6, r6, #0x6c ; DR struct-array stride ([chip+0x10] + i*0x6c)
```

The record itself carries the correct pairs in two other places - `docs/phase21/sr-sibling.md` A.3
("*SR ch0 block CA `0x4003a400`, DR ch3 block CA `0x4003a590`; the module's `{0x400,0x450,0x4a0}` /
`{0x590,0x5e0,0x630,0x680}` match the live vendor exactly*") and `docs/phase22/fw-hostmem.md` 2.1
("*SR ch0 base `CA 0x4003a410` = `BAR0+0x3f2410`; ch1 `0x4003a460`, ch2 `0x4003a4b0`*"). The
`0x114/0x6c`-derived CAs in `docs/phase23/wifidrv1-both-blocks.md` ("*3 SR at CA `0x4003a400` stride
`0x114`, 4 DR at CA `0x4003a590` stride `0x6c`*") and in `docs/phase27/vendor-live-ring-shape.md`
(`SR ch1 CA=0x4003a514`, `DR ch1 CA=0x4003a5fc`, `DR2 0x4003a668`, `DR3 0x4003a6d4`) are *not*
register blocks: in all three 16 MiB vendor dumps those words read `0x00000000`.

---

## B. Where the registers actually are in normal op (the vendor's own BAR0)

Three independent full-16 MiB BAR0 dumps, all md5'd above, all with the record's own anchor intact
(`BAR0+0x3b8000 = 0x101 0x110 0x2`, `docs/phase11/barmap.md` on-device log). Read through the vendor's
region-3 window, `BAR0 = 0x3b8000 + (CA - 0x40000000)`:

| BAR0 | CA | field | barmap | diff_before | diff_after |
| --- | --- | --- | --- | --- | --- |
| `0x3f1000` | `0x40039000` | glue block id | `0000010b` | `0000010b` | `0000010b` |
| `0x3f1010` | `0x40039010` | `out[0]` | `00000000` | `00000000` | `00000000` |
| `0x3f12e8` | `0x400392e8` | glue chn_res | `00000020` | `00000020` | `00000020` |
| `0x3f1508` | `0x40039508` | ETE intr block | `3f201818` | `3f201818` | `3f201818` |
| `0x3f2000` | `0x4003a000` | **ETE block id** | `0000010a` | `0000010a` | `0000010a` |
| `0x3f2400` | `0x4003a400` | SR0 `+0x00` enable | `00000001` | `00000001` | `00000001` |
| `0x3f2408` | `0x4003a408` | SR0 `+0x08` ctrl | `00000000` | `00000000` | `00000000` |
| `0x3f2410` | `0x4003a410` | SR0 `+0x10` base | `84908000` | `84908000` | `84908000` |
| `0x3f2414` | `0x4003a414` | SR0 `+0x14` depth-1 | `0000001f` | `0000001f` | `0000001f` |
| `0x3f2418` | `0x4003a418` | SR0 `+0x18` wptr | `00000404` | `00000403` | `00000400` |
| `0x3f241c` | `0x4003a41c` | SR0 `+0x1c` rptr | `00000404` | `00000403` | `00000400` |
| `0x3f2460` | `0x4003a460` | SR1 `+0x10` base | `84909000` | `84909000` | `84909000` |
| `0x3f24b0` | `0x4003a4b0` | SR2 `+0x10` base | `8490a000` | `8490a000` | `8490a000` |
| `0x3f2590` | `0x4003a590` | DR0 `+0x00` enable | `00000001` | `00000001` | `00000001` |
| `0x3f25c0` | `0x4003a5c0` | DR0 `+0x30` base | `8490b000` | `8490b000` | `8490b000` |
| `0x3f25c4` | `0x4003a5c4` | DR0 `+0x34` depth-1 | `0000001f` | `0000001f` | `0000001f` |
| `0x3f25c8` | `0x4003a5c8` | DR0 `+0x38` prod | `00000418` | `00000416` | `0000000f` |
| `0x3f25cc` | `0x4003a5cc` | DR0 `+0x3c` index | `00000418` | `00000416` | `0000000f` |
| `0x3f2610` | `0x4003a610` | DR1 `+0x30` base | `837ca000` | `837ca000` | `837ca000` |
| `0x3f2660` | `0x4003a660` | DR2 `+0x30` base | `83786000` | `83786000` | `83786000` |
| `0x3f26b0` | `0x4003a6b0` | DR3 `+0x30` base | `8376a000` | `8376a000` | `8376a000` |
| `0x3f2514` | `0x4003a514` | record's "SR ch1" | `00000000` | `00000000` | `00000000` |
| `0x3f25fc` | `0x4003a5fc` | record's "DR ch1" | `00000000` | `00000000` | `00000000` |
| `0x3f3000` | `0x4003b000` | past the ETE block | `ffffffff` | `ffffffff` | `ffffffff` |
| `0x3f3ffc` | `0x4003bffc` | past the ETE block | `ffffffff` | `ffffffff` | `ffffffff` |

Four things this settles, all [proven]:

1. **The seven channel register files are exactly where A.2/A.3 put them**, at the `0x50`-stride
   offsets from the `.rodata` cfg table, with the field layout of the register-init routines, and the
   indices *move between two dumps of the same boot* (`SR0 +0x18/+0x1c 0x403 -> 0x400`; `DR0
   +0x38/+0x3c 0x416 -> 0x0f`; `DR3 +0x38 0x15 -> 0x410`).
2. **The per-channel enable word is `block+0x00`, and it is `1` on all seven channels** (the
   firmware's `+0x00 |= 1`, `docs/phase22/fw-sr-gate.md` §1.1). `SR+0x08` ctrl is `0` - the host's
   `cfg[5]`-derived bits, not the enable.
3. **The window ends at CA `0x4003afff`.** CA `0x4003b000` and everything above reads `0xffffffff` in
   all three dumps - the device does not decode past the ETE block. The usable region-3 window is
   `BAR0 0x3b8000..0x3f2fff`.
4. **The DR ring is host DRAM, not window-resident.** `DR0 +0x30 = 0x8490b000` (and `SR0 +0x10 =
   0x84908000`) are device VAs in the outbound window (`0x80000000..0xffffffff -> host 0x80000000`,
   `docs/phase20/host-window.md`), i.e. host DRAM at `0x8490b000`. This contradicts
   `docs/phase41/d2h-window-resident-ring.md`'s "the vendor's DR ring is carved from the message
   window": the vendor's own DR base register says host DRAM, and the descriptors the port's DR ring
   must match are at `0x8490b000`, not in the window. What the window holds is *payload*, not the
   ring's descriptors.

---

## C. What the 2026-10-04 window actually holds

Structure (all three windows `0x3f1000..0x3f3fff`, 3072 words, 2990 non-zero, 82 zeros): repeating
`0x200`-byte slots; each record-starting slot carries a 16-byte head at `+0x000..+0x00f` and payload
from `+0x010`. The head's second word is the marker the task lists - `0xXX016000`, at 17 slots
(`0x3f1004/0x3f1204/0x3f1804/0x3f1a04/0x3f1c04`, `0x3f2004/0x3f2204/0x3f2404/0x3f2804/0x3f2a04/0x3f2c04`,
`0x3f3004/0x3f3204/0x3f3404/0x3f3804/0x3f3a04/0x3f3c04`) - with `+0x000 = 0x00000000` and
`+0x00c = 0x00100000` constant, and payload from `+0x010` (MAC, ethertype `0x0800`/`0x893a`, IPv4
`45 00`, then SSDP/DNS/HTTP text - the traffic `docs/phase41` read).

That marker is the vendor's own **node word1**, not a counter. `shuangta_ete_sr_dscr_fill` @`0x17858`
builds word1 as `(len << 16) | flags`:

```
0x017884: bfi  r1, r3, #0x10, #0x10   ; word1[31:16] = the length
0x01789c: movw r1, #0xd2b
0x0178a0: orr  r2, r2, #0x4000        ; bit 14
0x0178ac: orr  r2, r2, #0x2000        ; bit 13
0x0178b8: bfi  r2, r1, #0, #0xd       ; bits 0..12 = 0xd2b
0x0178cc: str  r1, [r3, r2, lsl #3]   ; node word0 = buffer address
0x0178e0: str  r2, [r3, #4]           ; node word1
```

so the fill's word1 is `(len<<16)|0x6d2b` and the `0x6000` bits are exactly its two present/valid
bits. The observed `0xXX016000` is `(len<<16)|0x6000`: a node word with the low-13 flag sub-field
zero and the length in bits 31:16. It is a *descriptor* word, so its presence at a register offset is
further proof that the window is carrying frame staging, not register state.

[observed] The payloads do not read forward as ASCII: a literal search for the SSDP strings fails in
the capture as stored and in all three 16 MiB vendor dumps, and the strings appear in the capture
only under a reversal of word order *and* byte order. No copy of them exists in the vendor dumps in
forward, byte-reversed or word-order-reversed form. [inferred] The capture's payload layout is
therefore not the vendor dump's, so the capture can be used to show *what the port's offsets return*
when the binding is not the register region, but not to *locate* the registers.

Against section B, the 2026-10-04 window fails every anchor of the register region:

| anchor | register region (B) | 2026-10-04 snapshots |
| --- | --- | --- |
| `0x3f1000` (CA `0x40039000`) | `0000010b` | `00000000` |
| `0x3f2000` (CA `0x4003a000`) | `0000010a` | `00000000` |
| `0x3f1508` (CA `0x40039508`) | `3f201818` | `6d6f6e49` ("Inom") |
| `0x3f2410` (SR0 base) | `84908000` | `0013d40d` |
| `0x3f3000` (past the block) | `ffffffff` | `00000000` + frame data to `0x3f3ffc` |

The last row is decisive: the 2026-10-04 capture is populated at CA `0x4003b000..0x4003bfff`, an
address range the device does not decode (B.3). **A window that is populated past the end of the ETE
block is not a read of the ETE block**, whatever its label says.

---

## D. Reconciliation: why data appears at the register addresses the port uses

**[proven]** The offsets `0x3f1000 / 0x3f1508 / 0x3f2000(+cfg[0])` are the *vendor's region-3* offsets
for CA `0x40039000 / 0x40039508 / 0x4003a000`. They are correct as *offsets*, and in a takeover the
port's own viewport program reproduces them (`docs/phase23/wifidrv1-both-blocks.md`: with the six
viewports programmed, the port decoded `SR ch0 CA=0x4003a400 base=0 depth-1=0 wptr=0 rptr=0` - a
correct, empty register region), and the vendor boot puts the register files at exactly those offsets
(section B).

**[proven]** The 2026-10-04 window is *not* that region (section C): no block id, no channel file, and
4 KiB of payload past the decoded end.

**[inferred]** The cause, in the order the evidence supports it:

1. **The ETE "registers" are a layout inside the device's window memory, not a separate I/O space.**
   The record's own takeover reads are the proof that the window is plain read/write memory: "in a
   TAKEOVER the same windows read all-zero except the six mailbox words" - the port's own writes at CA
   `0x40039010..0x400392f0` persist and read back, and in a takeover nothing else is there. The
   firmware sets `block+0x00 |= 1` (`docs/phase22/fw-sr-gate.md` §1.1) and the host sets the rest; both
   sides are writing words in a memory window. That is why the same BAR0 offsets can hold a register
   file in one state and staged frames in the next, and it is the same effect
   `docs/phase21/sr-sibling.md` A.5 caught as "*the device rewrites the SR index register when its
   firmware boots*" (the port's committed `SR+0x18 = 0x400` came back as `0x4` while base/depth/ctrl
   were untouched - payload written over part of the layout).
2. **What the port reads at those offsets is therefore whatever the window is bound to at that
   moment**, and neither the port's code nor the 2026-10-04 capture checks which. The port's address
   arithmetic is a *constant* (`0x3b8000 + (CA - 0x40000000)`, copied from the vendor boot), while the
   vendor's own resolution is the runtime descriptor lookup of A.4. A constant that was true of one
   binding is not a mapping.
3. **The 2026-10-04 read path is not the vendor dump path** (section C: the payloads are byte-reversed
   relative to any faithful BAR0 read, and the data itself is nowhere in the three 16 MiB vendor dumps
   - no plain, word-reversed or order-reversed copy of the SSDP strings exists in any of them). So the
   capture cannot be used to locate the registers at all; it can only be used to show what the port's
   offsets *return* when the binding is not the register region.

**Consequence.** The phase-41 conclusion that the port's DR ring "must be carved from the message
window at the vendor's layout" does not follow and is contradicted by the vendor's own registers
(B.4: DR base `0x8490b000`, host DRAM). Posting DR nodes inside the window would put them at a CA the
device uses as a register file; that is not the layout the vendor's descriptors describe.

---

## E. What the port must read instead

**E.1 Gate on the block id before any register read.** One word separates the two states:

| read | at | register region | not the register region |
| --- | --- | --- | --- |
| ETE block id | `BAR0 0x3f2000` (CA `0x4003a000`) | `0x0000010a` | `0x00000000` / data / no-decode |
| glue block id | `BAR0 0x3f1000` (CA `0x40039000`) | `0x0000010b` | `0x00000000` / data |
| block end | `BAR0 0x3f3000` (CA `0x4003b000`) | `0xffffffff` | populated |
| ETE intr block | `BAR0 0x3f1508` (CA `0x40039508`) | `0x3f201818` (after `& 0xffe0f8f8`) | frame data |

If the gate fails, the register region is not there and **no** index read at those offsets is
meaningful; derive the host VA from the region descriptor table (A.4) instead of the constant.

**E.2 Then read the indices here** (BAR0 = `0x3f2000`, CA `0x4003a000`):

| channel | block | ctrl/enable | base | depth-1 | producer | consumer |
| --- | --- | --- | --- | --- | --- | --- |
| SR ch0 | `+0x400` | `+0x08` (+`+0x00` bit0 = ch enable) | `+0x10` | `+0x14` | `+0x18` (wptr) | `+0x1c` (rptr) |
| SR ch1 | `+0x450` | `+0x08` | `+0x10` | `+0x14` | `+0x18` | `+0x1c` |
| SR ch2 | `+0x4a0` | `+0x08` | `+0x10` | `+0x14` | `+0x18` | `+0x1c` |
| DR ch0 | `+0x590` | `+0x00` bit0 | `+0x30` | `+0x34` | `+0x38` | `+0x3c` |
| DR ch1 | `+0x5e0` | `+0x00` bit0 | `+0x30` | `+0x34` | `+0x38` | `+0x3c` |
| DR ch2 | `+0x630` | `+0x00` bit0 | `+0x30` | `+0x34` | `+0x38` | `+0x3c` |
| DR ch3 | `+0x680` | `+0x00` bit0 | `+0x30` | `+0x34` | `+0x38` | `+0x3c` |
| (interrupt/enable) | CA `0x40039508` = `0x3f1508` | host mask `0xffe0f8f8`; live `0x3f201818` | | | | |

Absolute addresses a reader can assert: `SR0 base/depth/wptr/rptr = 0x3f2410/0x3f2414/0x3f2418/0x3f241c`;
`SR1 = 0x3f2460/0x3f2464/0x3f2468/0x3f246c`; `SR2 = 0x3f24b0/0x3f24b4/0x3f24b8/0x3f24bc`;
`DR0 = 0x3f25c0/0x3f25c4/0x3f25c8/0x3f25cc`; `DR1 = 0x3f2610/...`; `DR2 = 0x3f2660/...`;
`DR3 = 0x3f26b0/...`.

**E.3 Sanity assertions that separate a register read from a data read** (all four hold in the vendor
dumps, none hold in the 2026-10-04 capture): the block id is `0x10a`; `base` is in the outbound
window (`0x80000000..0xffffffff`, i.e. host DRAM `0x8xxxxxxx`); `depth-1` is `0x1f`; the index pair is
`<= 0x7ff` with the `0x400` phase bit (`pcie_ete_ring_ptr_plus` @`0x13ef8`, `docs/phase21/sr-pump.md`
A.1). A value like `0x0013d40d` or `0x00052039` is payload, not a register.

**E.4 The one read-only experiment that closes the residual question** (what the window is bound to in
the data state): in the *same* session as a capture, also read `BAR0 0x3f2000`, `BAR0 0x3f3000`, and
the endpoint's own iATU viewports / the six region descriptors (`BAR2 + 0x104 + 0x200*i`, and the
`host+0x20` descriptor array the lookup in A.4 walks). If `0x3f2000` is `0x10a` the capture is the
register region and the frames are inside it; if it is `0x00000000` while `0x3f3000` is populated, the
capture is bound to something else and the ETE VA must be computed from the region table, not from
`0x3f2000`.

---

## F. Verification (each quoted offset disassembles to the claimed instruction)

`capstone CS_MODE_ARM`, `plat.ko` md5 `23660bc285393e678d5cade1c36c194b`, `.text` file offset =
vaddr + `0x38`:

| quoted | instruction at that offset |
| --- | --- |
| `0x015f00/0x015f04/0x015f08/0x015f0c/0x015f10` | `cmp r0,#6` / `movls r2,#0xc` / `ldrls r3,[pc,#8]` (`lit[0x15f18]` = `0x101c`, ABS32 to `.rodata`) / `mlals r0,r2,r0,r3` / `movhi r0,#0` |
| `0x014a58` / `0x014ab0` / `0x014ad0` / `0x014adc` / `0x014af4` | `ldr r5,[r4,#0xdc]` / `str r0,[r5,#0x10]` / `str r1,[r2,#0x14]` / `str r2,[r3,#0x18]` / `str r2,[r3,#8]` |
| `0x014aac` / `0x014884` | `bl pcie_hostca_to_devva` (R_ARM_CALL) |
| `0x014854` / `0x014888` / `0x0148a8` / `0x0148b4` | `ldr r6,[r1,#0x50]` / `str r0,[r6,#0x30]` / `str r1,[r2,#0x34]` / `str r2,[r3,#0x38]` |
| `0x0076e8` - `0x007728` | `ldr r1,[r4,#0x18]`; `ldr r3,[r7]`; `ldr r1,[r1]`; `ldr r3,[r3]`; `ldr r0,[r3,#4]`; `bl oal_pcie_inbound_ca_to_va`; `ldr r7,[sp,#4]`; `bl pcie_ete_init_src_ring`; `bl pcie_ete_init_dst_ring` |
| `0x007568` / `0x007580` / `0x00758c` / `0x0075b0` / `0x0075b4` / `0x0075b8` | `ldr r1,[r1,#0x18]` / `ldr r1,[r1,#4]` / `bl oal_pcie_inbound_ca_to_va` / `movt r2,#0xffe0` / `and r2,r2,r1` / `str r2,[r3]` |
| `0x017884`/`0x01789c`/`0x0178a0`/`0x0178ac`/`0x0178b8`/`0x0178cc`/`0x0178e0` | `bfi r1,r3,#0x10,#0x10`; `movw r1,#0xd2b`; `orr r2,r2,#0x4000`; `orr r2,r2,#0x2000`; `bfi r2,r1,#0,#0xd`; `str r1,[r3,r2,lsl #3]`; `str r2,[r3,#4]` |
| `0x0070d0` / `0x007160` | `mov r3,#0x114` / `str r0,[r1,#0xdc]` |
| `0x007384` | `add r6,r6,#0x6c` |
| `0x008df0`/`0x008df8`/`0x008e34`/`0x008e44`/`0x008e48`/`0x008e58`/`0x008e60`/`0x008e6c`/`0x008e74`/`0x008e78`/`0x008e84`/`0x008e90` | `ldr ip,[r0,#0x20]`; `ldr r5,[r0,#0x24]`; `ldr lr,[ip]`; `ldr r1,[ip,#0x28]`; `ldr r0,[ip,#0x2c]`; `ldr r0,[ip,#0x30]`; `ldr r0,[ip,#0x34]`; `subs r4,r4,r1`; `add lr,lr,r4`; `str lr,[r2]`; `str r4,[r2,#4]`; `add ip,ip,#0x50` |

Data (not instructions), quoted as literals: `.data` file `0x348a4`/`0x348a8` = `0x4003a000`/`0x40039508`;
`.rodata` file `0x20e5c..0x20eac` = the 7 x 12-byte channel cfg table; `FIRMWARE.bin` file `0xccedc`/
`0xccee0`/`0xcf2a0`/`0xd040c`/`0xcf5d4` = `0x4003a000`/`0x40039508`/`0x4003a000`/`0x4003a400`/`0x4003a590`;
`hi5622v100_wifi.ko` has none of these constants (0 hits).

## G. Corrections this report makes to the record

1. `docs/phase23/register-windows.md` ETE row: the block offsets and field offsets stand; the strides
   `0x114`/`0x6c` are the host *struct* strides (`0x0070d0`, `0x007384`), not register strides.
   `docs/phase23/wifidrv1-both-blocks.md` and `docs/phase27/vendor-live-ring-shape.md` print channel
   CAs derived from them (`0x4003a514`, `0x4003a5fc`, `0x4003a668`, `0x4003a6d4`); those words read
   `0x00000000` in the vendor's own BAR0.
2. `docs/phase41/d2h-window-resident-ring.md`: the vendor's DR ring is host DRAM (`DR0 +0x30 =
   0x8490b000`, live in three dumps), not carved from the message window; the window holds staged
   payload, and the port's DR nodes must match the DR register's base, not a window layout.
3. The register region ends at CA `0x4003afff`; any capture that is populated at CA `0x4003b000`
   (BAR0 `0x3f3000`) is not reading the ETE block.
