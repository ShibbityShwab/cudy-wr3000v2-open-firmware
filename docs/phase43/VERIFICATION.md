# Phase-43 report verification

Independent verification of the four phase-43 reports in this directory, performed 2026-10-04 by
task `st_01a10731`. **Method**: every quoted code offset was re-disassembled from the cited binary
with `pyenv/Scripts/python.exe` (capstone 5.0.7, `CS_ARCH_ARM / CS_MODE_ARM` for the `.ko` files,
`CS_ARCH_ARM / CS_MODE_THUMB` for `FIRMWARE.bin`), comparing mnemonic **and** operand string; every
`reg_all.txt` value was re-read from the file by parsing `addr = HEX, value = HEX`; every cited
`.rodata`/`.data` word and every cited file-offset data word was re-read from the binary; the two
report scans (region separation, port write set) were re-run. **Nothing was written except this
file.** All hashes matched the reports' own identity tables.

Binary identities (recomputed, all match the reports):

| file | md5 (recomputed) | matches reports |
| --- | --- | --- |
| `opensource/build/tmp/hi5622v100_wifi.ko` | `4737fcb21a1a2262a96f84d780ad8b35` | yes |
| `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` | yes |
| `build/tmp/FIRMWARE.bin` (928,920 B) | `0e530b976d5a20e87358671f1a577695` | yes |

Section geometry re-derived and matching: wifi.ko `.text` `sh_addr = 0`, `sh_offset = 0x38`;
`.rodata` `sh_offset = 0x167b00`; plat.ko `.text` `sh_offset = 0x38`, `.rodata` `sh_offset = 0x1fe40`.

---

## Verdicts

| report | verdict |
| --- | --- |
| `channel-array-map.md` | **PASS** (with one non-load-bearing §6.3 cross-check error noted below) |
| `channel-register-offsets.md` | **PASS** |
| `vendor-port-dr-diff.md` | **PASS** |
| `device-dr-side.md` | **PASS** |

Totals: **491** targeted checks (instruction re-disassembly, data-word reads, `reg_all` values,
relocation addends) - **491 exact matches**. One report contains a factually wrong sentence in an
aside; it is not a quote, is not load-bearing, and is called out explicitly. No report's headline
claim was contradicted.

---

## `channel-array-map.md` - PASS

### Evidence

- **Every quoted instruction re-disassembles exactly** (mnemonic + operands), covering the §1.2
  array-copy block (`0x034968`-`0x034a10`), the §1.2 converter loop (`0x033ff0`-`0x034054`,
  including the ten `str r2,[r8,#N]` slot stores and `0x0340ec cmp r5,#0xa`), the six slot
  consumers (§2: slots 0, 3, 4, 5, 7, 8), the §3.2 sorted-table loaders (`0x03592c`/`0x035d5c`),
  and the §4.2 plat.ko ETE routines (`0x015f00`-`0x015f10`, `0x014a58`/`0x014ab0`/`0x014ad0`/
  `0x014adc`/`0x014af4`, `0x014888`/`0x0148a8`/`0x0148b4`). All match.
- **The 20-word array and its two banks re-read word-by-word**: `.rodata+0xb60..0xbac` =
  `40042000 40044000 40046000 40048000 4004a000 40040000 4004c000 40052000 40050000 40054000`
  (2g) then `40062000 40064000 40066000 40068000 4006a000 40060000 4006c000 40072000 40070000
  40074000` (5g). `bank(5g)[k] = bank(2g)[k] + 0x20000` verified for all `k = 0..9` (**True**). The
  word before the run is `0x3fc` (a length) and the word after (`+0xbb0`) begins the differently
  ordered 2g dump list (`0x40040000`...), so the "20 consecutive words = two 10-word banks" claim
  is exact, and the "12-entry array" premise is correctly corrected.
- **Relocation addends**: `.text+0x034adc`, `0x036174`, `0x036178`, `0x0344d8` are `R_ARM_ABS32`
  (reloc type 2, empty symbol - REL form) whose addend is the literal word at the site: `0xb60`,
  `0xbb0`, `0xc00`, `0xae0`. All four match §1.1/§3.2.
- **Region-separation scan reproduced**: `hi5622v100_wifi.ko` `.text`/`.data` contain **0** words in
  `0x4003xxxx`; `hi5622v100_plat.ko` `.text` contains **0** words in `0x4004xxxx..0x4007ffff`. The
  eight plat.ko `0x4003xxxx` words are exactly `.rodata+0xcb8/0xcc0 = 0x40039224/0x40039a24` and
  `.data+0x1fa8/0x1fac/0x1fb0/0x1fb4 = 0x40039000/0x40039800/0x40037000/0x40038000` plus
  `.data+0x2944/0x2948 = 0x4003a000/0x40039508` (file `0x348a4`/`0x348a8`) - identical to §4.1 and
  §6.2.
- **`0x84a90000` occurs exactly once** in `reg_all.txt`: line 9188, `addr = 4004a004`. Its neighbour
  `0x4004a008 = 0x6140` is line 9189; the 5g twin `0x4006a004 = 0x84a98000` (line 19777) with size
  `0x6140` (line 19778). The ETE DR bases quoted (`0x844d8000/0x844d7000/0x844d6000/0x844d5000` at
  lines 2212/2232/2252/2272) and the ETE channel values (`0x4003a400 = 1` line 2100,
  `0x4003a410 = 0x844db000` line 2104, `0x4003a590 = 1` line 2200, etc.) all re-read exactly.
- **ETE channel cfg table** (`.rodata+0x101c`, file `0x20e5c`) re-read: seven 12-byte records -
  SR `+0x400/+0x450/+0x4a0` and DR `+0x590/+0x5e0/+0x630/+0x680`, depth `0x20`, control
  `1,1,1,0,0,0`. Matches §4.2 ("7 channels, not 12; no DR4").
- **Dump cross-checks**: `0x40040000 = 0x100` (line 5928), `0x40060000 = 0x100` (line 16517),
  `0x4003a000 = 0x10a` (line 1844), `0x40050000 = 0x400` (line 10692) - all match.

### The one error (non-load-bearing)

§6.3 ("The 'no consumer' result and the value citations") states, as a firmware cross-check, that
`FIRMWARE.bin` "... has `0x40040000` at `0xc61d8`/`0xe2c08` ... but **no firmware word holds
`0x40042000`, `0x4004a000`, `0x4004c000`, `0x40050000` or `0x84a90000`**."

**This is wrong for `0x40042000`**: `FIRMWARE.bin` contains `0x40042000` at file offset `0x920f4`
(and `0x40062000` at `0x920f8`) - the device's own copy of the array base, exactly as the sibling
report `device-dr-side.md` §5.4 states ("the firmware's own copy of that array is built at file
`0x92044..0x9208e` (base `0x40042000`, lit `0x920f4`)"). The other four values in that list
(`0x4004a000`, `0x4004c000`, `0x40050000`, `0x84a90000`) genuinely do not appear in the image, so
only the `0x40042000` part of the sentence is false. It appears in a closing "cross-checks" bullet,
is contradicted by the report's own sibling, and does **not** touch any headline finding (the array
size/identity, the CA-to-block separation, or the `0x4004a004` interpretation), all of which are
independently confirmed above. Verdict stays PASS; the erratum is recorded here.

---

## `channel-register-offsets.md` - PASS

### Evidence

- **Every quoted instruction re-disassembles exactly**, covering: the §1 base-array anchors
  (`0x034960`, `0x034968`, `0x0349c4`, `0x0349d0`, `0x034a04`, `0x033ff8`, `0x03592c`); the §2 CSI
  accessor family (`0x034f7c`/`0x034f90` set_param, `0x034ff8`/`0x035004`/`0x03500c` get_param,
  `0x035078`/`0x035084`/`0x03508c` set_buf_addr, `0x035104`/`0x035110`/`0x03512c` getter,
  `0x035200`/`0x03520c`/`0x035214` set_buf_size, `0x03527c`/`0x035288`/`0x035290` get_buf_size,
  `0x035498`/`0x0354b8`/`0x0354cc`/`0x0354d0` set_whitelist, `0x035584`/`0x035590`/`0x035598`
  get_whitelist); the §4 other-array-word accessors (`0x034ef4`/`0x034f00`/`0x034f28` slot 0,
  `0x032454`/`0x032460`/`0x032464` fcs test-mode, `0x0324ec`/`0x03254c`/`0x032494`/`0x0324dc`
  ht-matrix/cca, `0x036ed4` slot 3, `0x035328`/`0x035334`/`0x03541c`/`0x035424` slot 7,
  `0x034af0`/`0x034afc`/`0x034b04` slot 8); and the §5 ring-register accessors
  (`0x0326b8`-`0x03272c` `hal_set_*`, `0x0325c0`-`0x0326a4` wptr/rptr, `0x034b8c`/`0x034c0c`/
  `0x034c98` host-MAC int), plus the §5.2 callers (`0x037968`/`0x03796c`, `0x037dc4`-`0x038ee8`,
  `0x03a784`/`0x03a7a0`/`0x03a7a4`/`0x03a7bc`). All match, including the two `movw r7,#0x6140`
  (`0x0bae38`) and the `hmac_csi_init` call sites.
- **Array/table data re-read**: `.rodata+0xb60`/`+0xb88` ten-word banks; the ascending dump list
  `.rodata+0xbb0` = `0x40040000...`; the ten window-length words `.rodata+0xbd8..0xbfc` =
  `0xb0, 0x92c, 0xac0, 0x1000, 0xe50, 0xe60, 0x924, 0x44, 0x2d4, 0x190`; `.rodata+0xc00` =
  `0x40060000`. All match §1/§3.
- **Every `reg_all.txt` value in Appendix B re-read exactly**, including the whole `0x40040000`
  ring block (`+0x00 = 0x100`, `+0x1c = 0x00b0063c`, `+0x20 = 0x844c7000`, `+0x24 = 0x844c8000`,
  `+0x28 = 0x83734000`, `+0x2c = 0x100`, `+0x30 = 0x200`, `+0x34 = 0xfff`, `+0x38/+0x3c = 0x8000`,
  `+0x48 = 0xfffffcc1`, the `+0x40/+0x44/+0x4c/+0x50/+0x54 = 0` tail), the `0x40048010/14/18` and
  `0x40048618` ht-matrix values, the `0x400420a8=6`/`0x40042924=0`/`0x40042928=0x64` fcs values, the
  full `0x4004a000` CSI window (`+0x00=0`, `+0x04=0x84a90000`, `+0x08=0x6140`, `+0x0c/+0x10/+0x14=0`,
  `+0x414=0x2710`), and the `0x40050000` block. All match.
- **The CSI-block zero-span claim is exact**: re-scanning `reg_all.txt`, the entire
  `0x4004a000..0x4004bfff` span holds exactly three non-zero words - `+0x04`, `+0x08`, `+0x414` -
  as §2.1/§2.3 state.
- **The `+0x08`-is-a-size correction is grounded**: `hmac_csi_init`'s `movw r7,#0x6140` (`0xbae38`)
  feeds `shuangta_to_hmac_csi_set_buf_size` (`0xbae54`), and `+0x08` re-reads as `0x6140` - so §6.1's
  correction of the phase-42 "base+8 = an index" note (and of `vendor-port-dr-diff.md` §4's "packed
  index" aside) is evidence-backed.

No inaccuracies found.

---

## `vendor-port-dr-diff.md` - PASS

### Evidence

- **Every quoted instruction re-disassembles exactly** across §2/§7: the plat.ko host-driver DR set
  (`0x014854`/`0x014874`/`0x014888`/`0x01489c`/`0x0148a4`/`0x0148a8`/`0x0148b0`/`0x0148b4`), the SR
  set (`0x014ab0`/`0x014ad0`/`0x014adc`/`0x014af4`), the channel-walk (`0x0072b0`/`0x007384`/
  `0x00738c`), the interrupt block (`0x0075ac`/`0x0075b0`/`0x0075b4`/`0x0075b8`), the glue word
  (`0x0074a4`/`0x0074a8`/`0x007504`/`0x007520`), the only offset-0 enable candidate
  (`0x016188`, `0x01754c`/`0x017550`); the firmware loops (`0x9426`/`0x9434`/`0x9438`/`0x9444`/
  `0x9450`/`0x9454`, `0x948a`/`0x9490`/`0x9492`/`0x9496`, `0x951c`/`0x952a`/`0x952e`/`0x953a`,
  `0x9562`/`0x9564`/`0x9568`); and the wifi.ko array sites (`0x34968`, `0x3400c`, `0x34054`,
  `0x340b8`, `0x340ec`, `0x34960`). All match.
- **The port-side "diff is empty" claim is grounded**: `wifidrv1.c` defines
  `ETE_DR_BASEREG/DEPTH/WPTR/RPTR = 0x030/0x034/0x038/0x03c` (lines 189-192) and
  `omo_dr_block[] = { 0x590, 0x5e0, 0x630, 0x680 }` (line 363); `omo_ete_program`'s DR loop
  (lines 537-545) writes only `+0x30`/`+0x34`/`+0x38`; the only DR write outside it is the producer
  commit `iowrite32(idx, omo_ete + b + ETE_DR_WPTR)` (line 1705). A full grep of the file for
  `iowrite32`/`omo_wr` at `b + 0x00` or `b + 0x48` returns **nothing** - the only `+0x00`/`+0x48`
  enables are the SR-side `OMO_SR_EN0 0x000` / `OMO_SR_EN1 0x048` writes at lines 1649 and 1653
  (`omo_sr_post`). So "the vendor host driver's DR register set is {+0x30,+0x34,+0x38} and the port
  writes exactly those three; the missing writes are not the host driver's" (§2) is exactly correct.
- **§3 dump values re-read exactly**: `0x4003a590=1`, `0x4003a5a0=0x01060440`, `0x4003a5a4=0x1f`,
  `0x4003a5a8=3`, `0x4003a5ac=3`, `0x4003a5c0=0x844d8000`, `0x4003a5c4=0x1f`, `0x4003a5c8=3`,
  `0x4003a5cc=3`, `0x4003a5d8=1`, and `+0x00 = 1` / `+0x48 = 1` on all seven channel blocks
  (`0x4003a400/450/4a0/590/5e0/630/680`).
- **§4 values re-read exactly**: `0x4004a004=0x84a90000`, `0x4004a008=0x6140`,
  `0x4006a004=0x84a98000`, `0x40040004=0x83fd7000`, `0x40060004=0x844cd000`; the `.rodata` tables
  at `0xb60/b88/bb0/c00` re-read as quoted. The "CSI block, not ETE DR ring" conclusion matches
  `channel-register-offsets.md` and the source (`0x4004a004` written by the CSI accessor).
- **The `0x84a90000` uniqueness and line number** match. **The port/CA reachability rule**
  (`OMO_ETE_WIN 0x3f2000`, line 150) re-read.

No inaccuracies found. (Note: §4's parenthetical reading of `+0x08` as "a packed index" is the
report's own *quoted* framing of the phase-42 note that its §8 and the sibling report correct; the
report does not endorse it - it says the correct DR base is `0x4003a5c0` and defers the CSI
identification to the sibling. Verified consistent.)

---

## `device-dr-side.md` - PASS

### Evidence

- **Every quoted instruction re-disassembles exactly (Thumb)**, with matching raw bytes, across the
  §9 verification table and the §3/§4.1/§4.2/§4.3/§5 bodies: `pcie_msg_init` loops
  (`0x9426`/`0x9438`/`0x9444`/`0x9454`/`0x948a`/`0x9490`/`0x9492`/`0x9496`, `0x951c`/`0x952e`/
  `0x953a`/`0x9562`/`0x9564`/`0x9568`); `d2h_notify` (`0x86170`-`0x861c0`); the flush routine
  (`0x8610c`-`0x86166`); the only runtime channel stores (`0x8665c`/`0x86674`/`0x86678`/`0x8667e`/
  `0x86682`) and the DR-family read-only site (`0x866f8`/`0x866fc`/`0x86702`/`0x86704`); the
  ready/hello announce (`0x86e3a`-`0x86f7c`). Byte-for-byte matches.
- **The `0x86f58` literal correction is exactly right**: the instruction at `0x86f58` is the 2-byte
  `ldr r1, [pc, #0x44]` (bytes `11 49`), so the spin literal resolves to
  `Align(0x86f5c,4) + 0x44 = 0x86fa0 = 0x4000010c`; `0x4003022c` is a *separate* literal at
  `0x86f9c`, written earlier at `0x86e7a`. Re-decoded and confirmed.
- **Object-table data words re-read exactly**: `0xcf5d4=0x4003a590`, `0xcf928=0x4003a5e0`,
  `0xcfc7c=0x4003a630`, `0xcffd0=0x4003a680`, `0xd0324=0x4003a6d0`, `0xd040c=0x4003a400`,
  `0xd04d8=0x4003a450`, `0xd05a4=0x4003a4a0`, `0xd0670=0x4003a4f0`, `0xcf60c=0x01060330`,
  `0xcf628=1`, `0xd0414=2`, `0xd0438=0x01060650`; literal pools `0x86f90=0x40101250`,
  `0x86f94=0x40161f00`, `0x86f9c=0x4003022c`, `0x86fa0=0x4000010c`, `0x86fa4=0x00040008`,
  `0x86fb4=0x40039000`, `0x86fb8=0x40039800`, `0x86fbc=0x40039014`; the block-id words
  `0xccedc=0x4003a000`, `0xcf2a0=0x4003a000`, `0xcf2ac=0x40039000`, `0xcf2b8=0x01060440`. All match.
- **The "no firmware store to a DR block" negative is consistent**: the only runtime channel-register
  stores in the image are on the `0xcc`-family (SR) blocks (`0x86678`/`0x86682`), and the `0x354`-
  family (`+0x31c`) site only **reads** the DR base (`0x866f8`-`0x86704`). The firmware's own array
  copy is present at `0x920f4 = 0x40042000` / `0x920f8 = 0x40062000`, as §5.4 states.
- **Live cross-checks re-read exactly**: `0x4003a000=0x10a`, `0x4003a400=1`, `0x4003a410=0x844db000`,
  `0x4003a590=1`, `0x4003a5c0=0x844d8000`, `0x4003a5c8=3`, `0x4003a5cc=3`, `0x4003a448=1`,
  `0x4003a5d8=1`, `0x40039000=0x10b`, `0x40039800=0x10c`, `0x40039010=0`, `0x40039014=0`,
  `0x40000108=0x5a5a`, `0x4000010c=0xdeaf`, `0x40101434=0`, `0x40101410=1`, `0x40101430=1`,
  `0x40030100=0`, `0x4003022c=1`, and the SR/DR host-ring bases
  (`0x844da000`/`0x844d9000`/`0x844d7000`/`0x844d6000`/`0x844d5000`).
- **The port-side claims re-read**: `OMO_RELEASE_OFF 0x3b8108` (line 592), `OMO_D2H_ACK/REARM`
  (lines 620-621), `OMO_MSG1 0x014` (line 168) - matching §7; and the "port writes only SR `+0x00`/
  `+0x48`" statement matches the port source (see the vendor-port-dr-diff section above).

No inaccuracies found. (The one analytical count in §5.3 - "44 data-table entries" - is a
data-table classification, not a quoted offset; a naive aligned scan finds more candidate words, but
the classification is outside the falsifiable per-offset claims and does not affect any verdict.)

---

## How to reproduce

```bash
PY=pyenv/Scripts/python.exe
# instruction checks (ARM: the two .ko; THUMB: FIRMWARE.bin) - compare mnemonic+operands
#   at every offset listed in each report's verification table, then re-read:
#   - .rodata/.data words at the cited offsets (rodata file base 0x167b00 / plat 0x1fe40)
#   - reg_all.txt values by parsing "addr = HEX, value = HEX"
#   - the four R_ARM_ABS32 literal-word addends at .text+0x34adc/0x36174/0x36178/0x344d8
```

The verification harness used here was a throwaway script under
`opensource/docs/phase43/_verify_tmp.py`, removed after the run; only this file was added.
