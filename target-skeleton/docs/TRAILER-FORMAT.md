# The luofu FIP trailer - decoded format and validation rules

Status: **decoded from local artifacts and stock U-Boot code; generation is
structurally validated, not hardware-proven.** Last analysis: 2026-11-18
(offline; no router, UART, TFTP or fire tool was used).

This document describes the trailer that the stock vendor U-Boot's `bootfip`
command requires after the legacy uImage in the kernel slot:

    bootcmd = mtd read kernel${bootflag} ${loadaddr}; bootfip ${loadaddr}

It supersedes the trailer paragraphs of [TARGET-DESIGN.md](../../docs/TARGET-DESIGN.md)
sections 3.2, 5.2 and 12 item 1 where they say the descriptor layout and the
overlay match rule are unreversed, and it corrects the PHASE 95 ledger claim
that `bootfip` verifies the uImage CRCs (it does not).

Every value below is structural evidence or a measurement; no vendor bytes are
embedded here or in `tools/fip-slice.py`, and the vendor-derived artifacts cited
are local-only (gitignored, not published).

---

## 1. Evidence inventory

| artifact (local path) | sha256 | what it contributes |
| --- | --- | --- |
| `build/linux-kbuild/slice-trailer-40732.bin` | `336b07920e915fac30c3b90d57925b880d430ac7ed94a81963d52af10c102953` (md5 `7e5a83c0dd424006f4b2f3f4235a01a6`) | the fixed 40,732-byte bench-lane trailer; byte-identical to the first 40,732 bytes of the 2.4.15 vendor container |
| `dumps/router-20261108/full/mtd11-kernela.bin` | `6c7c0784fded8c48996f4a20e81a6907511223d47d0f532480abd78d70adb5d8` | stock slot A: 2.4.15 uImage + the 40,828-byte vendor container + a 628-byte slot-only region |
| `dumps/router-20261108/full/mtd12-kernelb.bin` | `dffd16fc8dfb2c5edcbef95a328585267fc4362c95e2c6bc27ec2adfd8fbf904` | slot B at dump time: a lab slice whose trailer is the full 40,828-byte container (independent copy of the same descriptor) |
| `dumps/router-20261108/uboota.bin` (= `full/mtd1-uboota.bin`) | `36d94da9757da5b1fb15101cd457087ca99c0895de1fc64e58ef8210a7433132` | the on-device U-Boot where the gate/applier/match code was decoded; U-Boot 2022.07 (Nov 25 2023 - 18:18:57) for luofu |
| `dumps/router-20261108/ubootb.bin` (= `full/mtd2-ubootb.bin`) | `9736ab57d1bce93cf626048a9e949aad6d4bdbca599e62f058c86128c727db48` | A/B copy; matcher code and table identical to `uboota.bin` (differ only later in the slot) |
| `firmware/WR3000V2-R116-2.4.15-20251030-114751-sysupgrade.bin` | `014d6f6062bd118e35ae7ee2fbc913859e81ccd1bf727d12c96875986a6f0d75` | second independent copy of the 2.4.15 uImage/base DTB/container at file offset 0x80e5c; its embedded bootloader is an older U-Boot link (all vaddrs 0x30 lower) with an instruction-identical applier |
| `firmware/WR3000V2-R116-2.5.24-20260727-122111-sysupgrade.bin` | `ded2d835ed22a4fb26a305c999603631ea846dc5ef8f512aa006d72dd31d2f72` | a **second descriptor variant** (base DTB 31,300 B; overlays 4,604/4,968 B; container total 9,668 B) that confirms field meanings; its first 512 KiB of bootloader are byte-identical to `uboota.bin` |
| vendor base DTB (extracted from the trailer) | `947ec62d7fba2aed624166a585a5001f5385befba962f071f19ed468dfa270e3` | 31,220 B, root compatible `hsan-luofu` |
| vendor overlay board_id=0 (4,592 B) | `fe7df5eaf7a8a6f8d2079896d098cff9de103eea27d9b3d30322b539dfaa2ba7` | reference board overlay (root board_id 0, vendor_id hsan, `hsan-luofu`) |
| vendor overlay board_id=3 (4,920 B) | `17fd44ed9513580a1aea630cd5631fdbfd63d79768035549a0e569eba4cb65cf` | this board (R116) overlay (root board_id 3, vendor_id hsan, `hsan-luofu`) |

Ledger/report cross-references: `mem-entries.md` PHASE 91 (2026-10-11,
descriptor-table discovery), PHASE 92 (2026-10-11, first kernel execution after
the container was restored), PHASE 95/99 (2026-10-11, gate identification),
PHASE 100/108 (2026-10-11, container shape in the boot lane), 2026-11-17 08:05
(3df/3dg/3dh/3dl share the 40,732-byte trailer), 2026-11-18 (target design);
`opensource/docs/phase42-decode/report.md` (2026-10-09 gate/tail analysis);
`opensource/docs/soc/vendor-dt-notes.md` sections 1 and 8 (overlay contents and sizes);
`opensource/docs/TARGET-DESIGN.md` sections 3.2, 5.2, 12.

---

## 2. Byte layout

### 2.1 The 2.4.15 vendor container (40,828 B) - the shape the lab actually boots

Offsets are relative to the start of the trailer (= immediately after the
uImage payload, i.e. slice offset `0x40 + ih_size`).

| trailer offset | size | content | evidence |
| --- | --- | --- | --- |
| 0 | 31,220 | base DTB, FDT magic `d00dfeed`, totalsize `0x79f4` | sha256 above; also embedded in uboota/ubootb |
| 31,220 | 96 | overlay-container descriptor: 32-byte header + 2 x 32-byte entries | decoded from U-Boot, below |
| 31,316 | 4,592 | overlay 0: board_id 0, compatible `hsan-luofu`, vendor_id `hsan` | FDT |
| 35,908 | 4,920 | overlay 1: board_id 3, compatible `hsan-luofu`, vendor_id `hsan` | FDT |
| **40,828** | | **container end** = `base 31,220 + descriptor 96 + 4,592 + 4,920` | descriptor +4 = 0x2588 = 9,608 |

The base DTB is 31,220 B with 1,025 bytes of zero padding after its strings
block; the overlays are padded to 4,592 B (1,026 zero bytes after strings) and
4,920 B (1,024 zero bytes after strings; logical content ends at trailer
offset 39,804).

### 2.2 The fixed 40,732-byte bench artifact

The lab trailer is **byte-identical to the first 40,732 bytes of the 40,828-byte
container** (verified 2026-11-18):

    40,828 - 40,732 = 96 bytes, removed from the end of the last overlay's
    1,024-byte all-zero FDT trailing padding. Nothing that the FDT parser
    visits is removed (its logical content still ends at trailer offset
    39,804, i.e. 928 padding bytes remain inside the 40,732-byte file).

Why the size was chosen is not documented in the repo. The arithmetic is
suggestive and is recorded here as an inference, not a proof: 40,732 =
31,220 + 4,592 + 4,920, i.e. exactly the old pre-descriptor "base DTB + two
overlays" tail size used before PHASE 91, so the historical fixed length was
apparently kept when the descriptor was added (the last 96 padding bytes were
dropped to preserve it). Both 40,828 (2026-10-11 fires) and 40,732
(2026-11-15/18 slices) booted on this bench; see TARGET-DESIGN.md 3.2 and the
2026-11-17 08:05 ledger entry.

Power-on tolerance is explained by the decoded applier (section 4): it never
checks the descriptor's total against the bytes actually present. It copies the
last overlay into a 128 KiB working buffer with `fdt_open_into()` and then
applies it; the only bytes beyond the slot's trailer are erased-partition
`0xFF` (or zero) bytes that lie inside the FDT's unused padding, which no
FDT walk reads.

### 2.3 The 2.5.24 vendor container (40,968 B) - a second data point

| field | 2.4.15 container | 2.5.24 container |
| --- | --- | --- |
| base DTB totalsize | 31,220 (0x79f4) | 31,300 (0x7a44) |
| descriptor header +4 (container total) | 0x2588 = 9,608 | 0x25c4 = 9,668 |
| descriptor header +8 / +12 / +20 | 32 / 32 / 32 | 32 / 32 / 32 |
| descriptor header +16 (entry count) | 2 | 2 |
| entry 0: size / offset | 4,592 / 96 | 4,604 / 96 |
| entry 1: size / offset | 4,920 / 4,688 | 4,968 / 4,700 |
| container total = 32+32*2 + sizes | 9,608 | 9,668 |
| full trailer | 40,828 | 40,968 |

The two independent containers use the same header constants, the same entry
stride and the same `total = 32 + 32*count + sum(sizes)` relation. Their
overlay bytes differ (different DTBO builds), which is why the sizes differ.

### 2.4 The 628-byte region after the container (stock slots only)

The flashed stock slots carry a further 628-byte region immediately after the
container end, before the `0xFF` erased tail: for mtd11 it occupies
`0x42258c..0x422800` (slot offsets) and consists of two-family scrambled
bytes (even offsets resemble `0xcc`, odd offsets resemble `0x66`, each
with 1-3 bits flipped; PHASE 42 `tailblob` found no crc/sum/hash match
across eight decode models).
It is **not part of the trailer**: it is absent from both vendor .bin images
(where the UBI area starts exactly at the container end), and the lab boots
without it (both the 40,828 and 40,732 tails exclude it). Its meaning remains
unknown (section 8).

---

## 3. The overlay-container descriptor, word by word

All words are big-endian 32-bit. The header is 32 bytes; `count` entries of
32 bytes follow, starting at descriptor offset 32. "Loader check" is what the
stock applier (`FUN_800743ec`) actually does; this was read from the
disassembly of `dumps/router-20261108/uboota.bin` (see section 5).

### 3.1 Header

| offset | size | 2.4.15 value | 2.5.24 value | loader check | structural interpretation | confidence |
| --- | --- | --- | --- | --- | --- | --- |
| +0 | 4 | 0xd7b7ab1e | 0xd7b7ab1e | must equal 0xd7b7ab1e (literal compared at applier start) | container magic / gate word | **[V] high** |
| +4 | 4 | 9,608 (0x2588) | 9,668 (0x25c4) | only `<= 0x800000` (8 MiB) | total container size = 32 + 32*count + sum(entry sizes) | value **[V] high**; loader use **[V]**; semantic name **[I]** |
| +8 | 4 | 32 | 32 | must equal 32 | unknown (candidates: header size, table offset, alignment) | value/check **[V]**; meaning **[U]** |
| +12 | 4 | 32 | 32 | must equal 32 | unknown (candidate: entry size) | value/check **[V]**; meaning **[U]** |
| +16 | 4 | 2 | 2 | must be `<= 64`; used as the entry-loop bound | number of overlay entries | **[V] high** |
| +20 | 4 | 32 | 32 | must equal 32 | unknown (candidate: another size/alignment field) | value/check **[V]**; meaning **[U]** |
| +24 | 4 | 0 | 0 | not read | unknown; vendor writes 0 | **[U]** |
| +28 | 4 | 0 | 0 | not read | unknown; vendor writes 0 | **[U]** |

### 3.2 Entries (descriptor offset +32 + i*32, i = 0 .. count-1)

| entry offset | size | 2.4.15 values (entry0 / entry1) | 2.5.24 values | loader check | interpretation | confidence |
| --- | --- | --- | --- | --- | --- | --- |
| +0 | 4 | 4,592 / 4,920 | 4,604 / 4,968 | **not read** | equals that overlay's FDT totalsize | value **[V]** (both variants); bootfip use **[V] absent** |
| +4 | 4 | 96 / 4,688 | 96 / 4,700 | read; overlay FDT is at `descriptor + offset`; its header must validate and its totalsize must be `<= 128 KiB` | offset of the overlay FDT relative to the descriptor start; 96 = 32 + 32*count for the first overlay | **[V] high** |
| +8 | 4 | 0 | 0 | not read | unknown; vendor writes 0 | **[U]** |
| +12 | 4 | 0 | 0 | not read | unknown; vendor writes 0 | **[U]** |
| +16 | 4 | 0 | 0 | not read | unknown; vendor writes 0 | **[U]** |
| +20 | 4 | 0 | 0 | not read | unknown; vendor writes 0 | **[U]** |
| +24 | 4 | 0 | 0 | not read | unknown; vendor writes 0 | **[U]** |
| +28 | 4 | 0 | 0 | not read | unknown; vendor writes 0 | **[U]** |

**Correction to earlier readings.** TARGET-DESIGN.md 3.2 and README 3.1 read
descriptor `+36` as "descriptor length 96". `+36` is entry 0's *offset*
field; it happens to equal 96 because it is the size of the whole table
(32-byte header + 2 x 32-byte entries) and the first overlay follows the
entries. The descriptor length is not stored: it is `32 + 32*count`, derived
from `+16`. Likewise `+68` is entry 1's offset (4,688 = 96 + 4,592), not
an unexplained word.

---

## 4. What the stock bootfip actually validates vs applies

Decoded from the disassembly of the U-Boot in `mtd1-uboota` / `mtd2-ubootb`
(2026-11-08 dumps; U-Boot 2022.07 (Nov 25 2023) for luofu). Addresses below
are virtual addresses of that build; the same code appears in the 2.4.15
firmware's older U-Boot link with every address shifted by 0x30 (instruction
stream identical). `tools/fip-slice.py` validates every item marked "checked".

    bootfip (FUN_80073bb8)
      |  dispatch on the first word at loadaddr:
      |    0xaa640001  -> ARM FIP ToC container path (not used by this slot)
      |    0x4554504f  -> second container header path (format not decoded here)
      |    uImage magic bytes 27 05 19 56 -> uImage path, loader type 2
      v
    loader FUN_800736dc  (uImage path)
      1. uImage magic == 0x27051956                         [checked; else "Bad Magic Number"]
      2. print image info (image_print_contents)            [no CRC verification]
      3. ih_arch (uImage byte +29) == 2 (ARM)               [checked; else "Unsupported Architecture"]
      4. ih_size = be32(header+12); payload = image+0x40
      5. ih_size != 0                                       [checked]
      6. base_dtb = image + 0x40 + ih_size; its FDT header  [checked; else "Could not find a valid device tree"]
      7. base_totalsize = be32(base_dtb+4)
      8. applier(base_dtb, base_dtb + base_totalsize)       [checked; return 0 required]
         - gate word = be32(base_dtb + base_totalsize)
         - gate word == 0xd7b7ab1e                         [checked; else "No valid overlay", return -1]
         - be32(gate+4) <= 8 MiB                           [checked]
         - be32(gate+16) <= 64                             [checked]
         - be32(gate+8) == 32, be32(gate+12) == 32,
           be32(gate+20) == 32                             [each checked]
         - for i in 0..count-1:
              offset = be32(gate+32+i*32 + 4)
              ov = gate + offset
              FDT header at ov valid, totalsize <= 128 KiB [checked; else "Invalid dtbo", return -1]
              match vendor_id against env vendorid          [checked]
              match compatible/board_id against the decoded table and
                env boardid / board strap                    [checked]
              on match: fdt_open_into(base), fdt_open_into(ov),
                fdt_overlay_apply(base, ov)                  [apply; failure => return -1]
              first matching overlay wins; later entries are not applied
         - no entry matched: print "Match dtbo fail, use the default dtb",
           return -1
      9. if applier returned non-zero: print "boot apply overlay fail",
         do not jump to the kernel                             [closed gate]
      10. on success: set the ATAG/DT handover and enter the kernel
          (entry only ever reached when the applier returned 0)

**Not verified anywhere on this path** (explicitly checked in the disassembly
and confirmed by the 2026-10-09 phase42-decode report):

- uImage hcrc (header+4) and dcrc (header+24): the boot path calls only
  `image_print_contents` (`0x8004239c`); the CRC verifier functions referenced by
  the "Bad Header Checksum"/"Bad Data CRC" strings are not called from
  `do_bootfip` or the loader. (This corrects the PHASE 95 ledger claim; the
  bench's wrong-dcrc fixture reached the loader and was refused at the gate -
  it carried a plain FDT magic at the container offset - not by a CRC check.)
- ih_load / ih_ep: stored and printed, never compared by bootfip. The entry
  address is computed by the decompressor from `(pc & 0xf8000000) + TEXT_OFFSET`
  (TARGET-DESIGN.md 3.2), so the lab still asserts it at build time.
- descriptor total (+4) against the bytes actually present: only the <=8 MiB
  cap is checked. This is why the 96-byte-short 40,732 trailer works.
- entry size (+0 of each entry): not read by bootfip at all.
- descriptor +24 / +28 and entry words +8..+28: not read.
- the 628-byte post-container slot region: not referenced by bootfip.

---

## 5. Where the decode came from (so it can be re-derived)

The U-Boot image is `dumps/router-20261108/uboota.bin` (1,048,576 B = mtd1
layout; the first bytes are a `hboot1tag` header, and U-Boot virtual address =
file offset + 0x8003ff00). Reproduce the disassembly with the repo toolchain:

    build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump \
      -D -b binary -m arm --adjust-vma=0x8003ff00 \
      --start-address=0x80073bb8 --stop-address=0x80074690 \
      dumps/router-20261108/uboota.bin

| symbol (by role) | virtual address | file offset in uboota.bin |
| --- | --- | --- |
| bootfip command body | 0x80073bb8 | 0x33cb8 |
| loader (uImage path) | 0x800736dc | 0x337dc |
| be32() helper (rev) | 0x800741ac | 0x342ac |
| gate magic literal (0xd7b7ab1e) | 0x80074668 | 0x34768 |
| overlay applier | 0x800743ec | 0x344ec |
| "No valid overlay"/"Invalid dtbo" strings | 0x8008a2a2 / 0x8008a2b4 | 0x4a3a2 / 0x4a3b4 |
| overlay match helper | 0x800741b4 | 0x342b4 |
| match table (11 rows x 24 B) | 0x800952e8 | 0x553e8 |
| board-strap reader | 0x80074260 | 0x34360 |
| luofu strap callback | 0x80074298 | 0x34398 |
| luofu strap register list | 0x8008039c | 0x4049c |

The 2.4.15 firmware's embedded bootloader carries the same applier instruction
stream at virtual addresses 0x30 lower (for example the applier at 0x800743bc,
the match helper and check sequence byte-identical after the 0x30 shift), and
the 2.5.24 firmware's first 512 KiB of bootloader are byte-identical to
`uboota.bin`. `uboota.bin` and `ubootb.bin` have identical matcher code and
table (they diverge later in the slot).

---

## 6. The overlay match rule

Decoded from the applier (FUN_800743ec), the helper (FUN_800741b4), the
11-row table at 0x800952e8, and the strap callbacks (FUN_80074298 for luofu).
Each row is 24 bytes: {compatible-family token string pointer, inline
vendor tag (8 bytes), reserved, callback pointer}. The same table appears in
both U-Boot slots and (modulo the 0x30 address shift) in the 2.4.15 U-Boot.

| row | family token | vendor tag | callback | note |
| --- | --- | --- | --- | --- |
| 0 | xiling | hsan | 0x80074310 | other SoC family |
| 1 | xiling | vendor2 | 0x800742d0 | |
| 2 | emei | hsan | 0x80074308 | |
| 3 | emei | vendor2 | 0x80074308 | |
| 4 | emei | vendor1 | 0x8007430c | |
| **5** | **luofu** | **hsan** | **0x80074298** | **this board family** |
| 6 | tiangong0 | hsan | 0x80074348 | |
| 7 | tiangong0 | vendor5 | 0x80074348 | |
| 8 | tiangong0 | vendor3 | 0x80074348 | |
| 9 | tiangong1 | hsan | 0x800743c0 | |
| 10 | tiangong2 | hsan | 0x800743e8 | |

The applier's per-overlay decision, in order:

1. Read the overlay's root `vendor_id` property (a string). The stock
   environment carries `vendorid=hsan` (mtd3 dump); if the environment
   variable is set and the overlay's `vendor_id` does not strcmp equal it,
   the overlay is skipped.
2. Read the overlay's root `board_id` property (a 32-bit cell) and its
   root `compatible` property (a string list). The helper walks the 11-row
   table and for each row tests `strstr(compatible, family_token)` and
   `strcmp(vendor_tag, vendor_id)` (the `compatible`/family test is a substring
   match, so `hsan-luofu` matches the `luofu` row).
3. On a row match, board selection is:
   - if the environment has a numeric `boardid` value, it must equal the
     overlay's `board_id`; otherwise
   - if `boardid` is unset (the stock state), the row's callback reads the
     board strap and compares it to the overlay's `board_id`.
   The luofu callback reads four pinctrl "cfg" registers at
   `0x14940000 + 4*{0x0a, 0x0b, 0x12, 0x13}`, takes bit 6 of each, and
   assembles a 4-bit board id from them.
4. The **first** overlay in descriptor order that matches is opened with
   `fdt_open_into()`, the base DTB is opened the same way, and libfdt
   `fdt_overlay_apply()` is run. If that fails the applier returns -1 and
   bootfip does not jump. Overlays after the first match are not considered.
5. If no entry matches, the applier returns -1; bootfip prints
   "boot apply overlay fail" and does not enter the kernel.

Observed vendor data: overlay 0 is board_id 0 (reference `ax3000_lite` board),
overlay 1 is board_id 3 and adds `model = "hsan luofu/R116"`; both carry
`compatible = "hsan-luofu"` and `vendor_id = "hsan"`. The live DT read from this
unit contains the R116 model, so the stock loader selected the board_id=3
overlay, i.e. the strap read 3 on this board [V for the selection, I for the
strap-bit interpretation].

Consequences for a generated trailer:

- at least one overlay must satisfy the match rule or the loader refuses to
  boot (a trailer with only a board_id-0 reference overlay is a dead end on
  this board unless the environment overrides `boardid`);
- every overlay's first `compatible` string must contain the family token
  (`luofu` for this board), because `strstr` stops at the first NUL of the
  property value;
- each overlay's `vendor_id` must equal the environment's `vendorid` value
  (stock: `hsan`);
- the board_id the strap will report is a hardware property and must be
  supplied/asserted when generating (the measured value for this board is 3).

---

## 7. The 40,732 vs 40,828 difference, stated precisely

- The vendor 2.4.15 container is 40,828 B: 31,220 base DTB + 96 descriptor +
  4,592 overlay 0 + 4,920 overlay 1.
- The fixed lab trailer is the first 40,732 bytes of exactly that container:
  byte-for-byte identical over the whole 40,732 B (verified against mtd11 and
  against the uImage tail in the 2.4.15 firmware image).
- The 96 omitted bytes are inside overlay 1's 1,024-byte all-zero trailing
  FDT padding. Overlay 1's struct block ends at trailer offset 35,908 + 3,400 =
  39,308 and its strings block at 35,908 + 3,896 = 39,804; the cut at 40,732
  still leaves 928 zero padding bytes after the last logical byte. No FDT
  structure or property is removed.
- The applier does not compare the descriptor's declared container total
  (9,608) with the number of bytes present, and it never parses the padding;
  the overlays are copied into a 128 KiB work buffer with `fdt_open_into()`
  (which reads the 96 missing bytes from the RAM image of the erased partition,
  i.e. `0xFF`/zero, and only trailing padding is affected).
- Both tail sizes were booted on this bench: 40,828 in the 2026-10-11 fires
  (PHASE 92-102), 40,732 in the 2026-11-15/18 slices. The fixed size is a lab
  convention (fire-preflight compares the last 40,732 bytes), not a loader
  requirement - the loader only requires the structure above.
- **Why** the artifact was cut to 40,732 is **not documented**. The
  arithmetic 31,220 + 4,592 + 4,920 = 40,732 (the old descriptor-less tail
  size) is the likely reason, but that is an inference [I]; no repo evidence
  records the decision.

The 628-byte region after the container end in flashed slots is separate from
both numbers (section 2.4) and is not needed by bootfip.

---

## 8. Remaining unknowns

| # | unknown | status / impact |
| --- | --- | --- |
| 1 | semantic names of descriptor header +8, +12, +20 | values and "must be 32" checks are decoded [V]; their field meanings are [U]. Generation can only hard-code the required constants. |
| 2 | meaning/use of +24, +28 and entry words +8..+28 | not read by the decoded loader [V absent]; vendor writes zero. Safe to emit zero, but semantics [U]. |
| 3 | why the fixed lab artifact is 40,732 B | structural description is exact (section 7); the *decision* is [U]/[I]. |
| 4 | the 628-byte post-container region in stock slots | not decoded (PHASE 42: no digest match across 8 decode models); not required by bootfip (lab boots with containers only) [V]. |
| 5 | exact hardware strap bit meaning | code path decoded [V]; the physical strap source (pinctrl cfg bit 6 of four pins) is [I] and board_id=3 for this unit is inferred from the applied overlay/live DT. |
| 6 | acceptance of a **novel** generated trailer | one attended boot on a recovered unit is still required (TARGET-DESIGN.md 5.2); the generator validates every check the decoded loader performs but that is not a hardware proof. |

---

## 9. Generation verdict and the tool

The evidence is sufficient to generate a trailer that satisfies every check the
stock bootfip performs, and `tools/fip-slice.py` now does so:

    python3 opensource/target-skeleton/tools/fip-slice.py make-trailer \
      --base-dtb  <our compiled board DTB> \
      --overlay   <our reference overlay DTBO> \
      --overlay   <our board overlay DTBO> \
      --output    trailer.bin \
      --overlay-apply require

Inputs the generator requires (all user-supplied; no vendor bytes are
embedded):

| input | why |
| --- | --- |
| base DTB | starts the trailer; its FDT totalsize places the gate; the stock loader passes its own copy of this DT to the kernel |
| one or more overlay DTBOs | must be FDT plugin blobs <= 128 KiB with root `compatible` containing `luofu`, `vendor_id = "hsan"` and a 32-bit `board_id`, and must apply onto the base DTB (libfdt) |
| expected board id | the board strap value the loader will match (measured 3 for R116); the tool refuses if no supplied overlay selects it |
| optional `--overlay-apply auto|require|skip` | runs `fdtoverlay` (the same libfdt apply the loader does) when available |

What the generator derives (all from the decoded format):

- the 32-byte header: magic, computed container total, required constants 32,
  entry count, zero reserved words;
- one 32-byte entry per overlay: FDT totalsize, contiguous offset (table size
  + previous overlay sizes), zero reserved words;
- the trailer bytes: base DTB + descriptor + overlays, contiguous.

Validation performed by the tool is fail-closed and mirrors the decoded loader
(plus the two-vendor-container consistency relation): gate position and magic,
container total, count, required constants, entry count/stride, per-overlay
FDT header and totalsize, contiguous offsets, container total =
32 + 32*count + sizes, match family/vendor/board_id, and optional libfdt
overlay application. A byte-exact check: feeding the 2.4.15 vendor base DTB
and its two overlays through the generator reproduces the 40,828-byte vendor
container **byte for byte** (and the 2.5.24 inputs reproduce its 40,968-byte
container byte for byte) - that is the regression test for the descriptor
encoder. The resulting trailer size is not 40,732: the fixed size is a
bench-lane artifact convention, not a loader requirement.

Also available:

    python3 .../fip-slice.py check-trailer <trailer>   # validate a trailer alone
    python3 .../fip-slice.py check <slice>            # validate uImage + trailer + gate
    python3 .../fip-slice.py assemble --uimage ... --trailer ... --output ...
    python3 .../fip-slice.py manifest --slice ... --output fip-manifest.txt

No generated trailer has been booted yet: the gates in TARGET-DESIGN.md
section 5.2 still require one attended boot on a recovered unit before the
image recipe may rely on it. This document does not claim that a novel trailer
boots; it claims the format and the decoded validation rules are satisfied.

---

## 10. Reproduction (read-only, local artifacts)

    # 1. fixed trailer: decode and print its descriptor/overlay table
    python3 opensource/target-skeleton/tools/fip-slice.py check-trailer \
      build/linux-kbuild/slice-trailer-40732.bin

    # 2. fixed trailer is the first 40,732 bytes of the 2.4.15 vendor container
    python3 - <<'PY'
    from pathlib import Path
    t = Path("build/linux-kbuild/slice-trailer-40732.bin").read_bytes()
    v = Path("dumps/router-20261108/full/mtd11-kernela.bin").read_bytes()
    ih = int.from_bytes(v[12:16], "big")
    print(t == v[0x40+ih:0x40+ih+40732])     # True
    PY

    # 3. re-derive the applier checks (U-Boot binary, file 0x344ec / 0x344bc)
    build/linux-kbuild/toolchain/bin/arm-none-linux-gnueabihf-objdump \
      -D -b binary -m arm --adjust-vma=0x8003ff00 \
      --start-address=0x800743ec --stop-address=0x800746c0 \
      dumps/router-20261108/uboota.bin

    # 4. generator regression against both recorded vendor containers
    #    (extract base/overlays from the local firmware images as in the
    #    tool's README self-checks; the expected outputs are byte-identical)
