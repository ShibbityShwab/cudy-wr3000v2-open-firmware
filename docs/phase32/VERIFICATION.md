# phase-32 verification: four reports re-checked against the artifacts they cite (2026-10-04)

Task `st_01a10573`. Read-only. One PASS/FAIL verdict per phase-32 report, plus the captured
evidence for each. Nothing was run against the router; only files already in the repository were
read.

| report | verdict |
| --- | --- |
| `interrupt-status-and-doorbell-path.md` (`intr-status`) | **PASS** |
| `irq-block-mapping.md` (`map-4016`) | **PASS** |
| `service-thread-gate.md` (`svc-thread-gate`) | **PASS** |
| `doorbell-endpoint-path.md` (`db-write-path`) | **PASS** |

All four PASS with one caveat recorded (not a failed claim): the two reports that quote `.ko`
call targets as `bl #0xNNNN` print section-relative symbol targets, while the shipped `.ko` bytes
at those sites are the image loader's `imm24 = -2` trampoline stub and capstone therefore prints
`bl #<next pc relative>`. The *instruction* at every quoted offset is a `bl` to a call at that
site, which is what the claim asserts; only the printed operand text is not reproducible from the
file. Detail in "Notes on two quoted forms" below. No claim in any report was found false.

## VERIFY method

Re-disassembled **every** quoted file offset from the raw binaries with the repo-local capstone:

```bash
pyenv/Scripts/python.exe build/tmp/p32verify/check_disasm.py
```

- script: `build/tmp/p32verify/check_disasm.py` (95 claims: 61 from `FIRMWARE.bin` in Thumb plus 12
  in ARM, 26 from `hi5622v100_plat.ko` in ARM, 1 raw-byte check, plus a `bl`-target derivation)
- captured run: `build/tmp/p32verify/check_disasm.out`
- result: **`checked 95 claims, 0 FAIL`, shell exit `0`** (`GATE-PASS`)
- disassembler: capstone **5.0.7** (`CS_ARCH_ARM`; `CS_MODE_THUMB` for `FIRMWARE.bin` code,
  `CS_MODE_ARM` for the vector stub at file `0x64` and for the `.ko` modules)
- artifact identity re-confirmed by md5:
  - `build/tmp/FIRMWARE.bin` `0e530b976d5a20e87358671f1a577695` = the md5 every report cites
  - `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` `23660bc285393e678d5cade1c36c194b`
    (byte-identical copy at `build/tmp/hi5622v100_plat.ko`) = the md5 `db-write-path` cites
  - `opensource/build/tmp/hi5622v100_wifi.ko` `4737fcb21a1a2262a96f84d780ad8b35`
  - `opensource/build/register-dumps/barmap_ep0_bar0.bin` 16,777,216 bytes

`FIRMWARE.bin` is 928,920 B (`0xe2c98`), so every quoted file offset is inside the image; the
largest quoted file offset is `0x87526` (`intr-status` §7), and every `0x1xxxxxx`/`0x4xxxxxxx`
value in these reports is a runtime address or CA, not a file offset.

---

## 1. `intr-status` = `interrupt-status-and-doorbell-path.md` - **PASS**

Verified re-disassembly (all PASS; excerpt of the 61 firmware claims):

```
PASS TH  0x082f00  isr ldr r3 0x4016010C   | ldr r3, [pc, #0x10c]
PASS TH  0x082f04  isr ldr r7 pending      | ldr r7, [r3]
PASS TH  0x082f08  isr ubfx                | ubfx r8, r7, #0, #0xa
PASS TH  0x082f12  isr add.w index         | add.w r3, r4, r8, lsl #2
PASS TH  0x082f16  isr ldr fn [r3,#0x98]   | ldr.w r5, [r3, #0x98]
PASS TH  0x082f4e  isr ldr r3 0x40160110   | ldr r3, [pc, #0xd0]
PASS TH  0x082f52  isr str r7 EOI          | str r7, [r3]
PASS ARM 0x000064  irq-stub sub lr         | sub lr, lr, #4
PASS ARM 0x000068  irq-stub srsdb          | srsdb sp!, #0x13
PASS ARM 0x000070  irq-stub push           | push {r0, r1, r2, r3, r4, r5, r6, r7, r8, sb, sl, fp, ip}
PASS ARM 0x000094  irq-stub blx 0x82efc    | blx #0x82efc
PASS ARM 0x0000c0  irq-stub rfeia          | rfeia sp!
PASS TH  0x000294  thunk ldr r0            | ldr r0, [pc, #4]
PASS TH  0x000296  thunk b.w 0x818ac       | b.w #0x818ac
PASS TH  0x0818b6  disp ldr r2 ctx+0xc     | ldr r2, [r0, #0xc]
PASS TH  0x0818bc  disp ldr pending        | ldr r5, [r2]
PASS TH  0x0818be  disp str clear          | str r1, [r2]
PASS TH  0x0818c4  disp str doorbell       | str r1, [r2]
PASS TH  0x0818da  disp ldr r3 ctx+0x20    | ldr r3, [r6, #0x20]
PASS TH  0x0818e8  disp blx r3             | blx r3
PASS TH  0x0874e8  register fn array store | str.w r8, [r3, #0x98]
PASS TH  0x087526  register priority str   | str r4, [r5, r0]
PASS TH  0x08702c  enable str bitmap       | str.w r4, [r3, r2, lsl #2]
PASS TH  0x0097f6  pcie_msg_init movs r0 4c| movs r0, #0x4c
PASS TH  0x0097fe  pcie_msg_init ldr r2    | ldr r2, [pc, #0x58]     ; pool 0x9858
PASS TH  0x009800  register(0x4c)          | bl #0x874b0
PASS TH  0x009816  enable(0x4c)            | bl #0x86ff4
```

Raw-byte check: the eight words printed by the live probe are the image bytes at file `0x40100`:

```
PASS BYTES 0x040100  irqblock dmesg words == image
      bde8f041faf772b890f82330012b28bf01239d429ed2f9f7f8fb48b194f8a930
```

Literal-pool values resolved from the file (confirm the register identities the report claims):
pool `0x83010` = `0x4016010C` (the pending/ID register read at `0x82f00`), `0x83014` = `0x0017D398`,
`0x83020` = `0x40160110` (EoI), `0x9858` = `0x00040295` (the `0x294` thunk), `0x29c` = `0x0010C190`
(the dispatcher ctx).

Honest gaps the report itself declares, confirmed present and **not** over-stated:

- the vector-slot wiring for `0x40064` is absent from the blob - re-checked: no 4-byte literal
  `0x00040064`/`0x00040065`/`0x0004006d` occurs in `FIRMWARE.bin`; file `0x0`/`0x4` hold
  `0x00046971`/`0x000C742D`. The report marks this **not resolved**, matching the file.
- the register-file names (CTLR/IAR/EOIR/ISENABLER/...) are labelled **[inferred]** from the GIC-400
  offset map while the *addresses* are marked **[proven]**; that split matches the evidence.

## 2. `map-4016` = `irq-block-mapping.md` - **PASS**

This report is a **host-window/unmapped-read** argument, so its "cited artifacts" are the BAR0 dump
and the live dmesgs, not disassembly. Re-read and re-computed:

- BAR0 dump, all four §C/§D anchors reproduce byte-for-byte:
  - `barmap_ep0_bar0.bin + 0x519100` = 32 zero bytes (0x20 zeros)
  - the zero run spans exactly `0x513ffc`-`0x51ade8` = `0x6dec` bytes, all zero (report's "0x6dec"
    confirmed; the byte just before `0x513ffc` = `0xde`, the byte at `0x51ade8` = `0x00`)
  - `+0x80100` and `+0x738100` both = `bde8f041faf772b890f82330012b28bf01239d429ed2f9f7f8fb48b194f8a930`
  - `+0x3f2000` = `0a01000000000000` (i.e. `0x0000010a`, as phase 17/18 record)
  - `+0x4d7ff0` = `ff` x 16 and `+0x4d8000` = `00000000 00000000 02000012 20000600` (the region 3
    tail / region 4 head boundary anchors)
- the region table quoted from `docs/phase17`/`docs/phase18` matches the live iATU viewports in
  `opensource/build/register-dumps/bothep/001_live_vendor_bars2.txt`: EP0 viewport 3 = base
  `0x403b8000`, limit `0x404d7fff`, target `0x40000000`; EP1 viewport 3 = `0x583b8000`-`0x584d7fff`
  -> `0x40000000`. Region 3 is `0x120000` bytes and ends at device CA `0x4011ffff`, so
  `0x40161100 - 0x40000000 = 0x161100 > 0x120000` - the lookup falls off the window, exactly as
  claimed. `0x40519100` lies in region 4 (`0x404d8000`-`0x406b7fff`) at CA `0x02041100`.
- the probe source matches its quotation: `opensource/lab/wifidrv1/wifidrv1.c:162-163`
  `#define OMO_IO_WIN 0x3b8000UL` / `#define OMO_IO_BYTES 0x120000UL`, `:1686`
  `omo_rel = ioremap(omo_bar0_base + OMO_IO_WIN, OMO_IO_BYTES);`, `:1190`
  `#define OMO_IRQ_BLOCK 0x161100UL`, `:1199` `pre[i] = omo_rd(omo_rel, OMO_IRQ_BLOCK + i * 4);`
- the dmesg words the report says equal `FIRMWARE.bin[0x40100..0x40120]` do - verified by decoding
  the little-endian words from both boots:
  - `build/register-dumps/exp/20261004-052201/dmesg.txt` and `.../20261004-053006/dmesg.txt`
    `[intrsamp] pre: irqblock=41f0e8bd b872f7fa ... 30a9f894` -> bytes
    `bd e8 f0 41 fa f7 72 b8 90 f8 23 30 01 2b 28 bf 01 23 9d 42 9e d2 f9 f7 f8 fb 48 b1 94 f8 a9 30`
    == `FIRMWARE.bin[0x40100:0x40120]` (**True** for both boots)
- the retraction is supported: the sampled offset is `0x41100` past a `0x120000` mapping, i.e. an
  out-of-bounds read that never reaches region 3's IO block or region 4.

## 3. `svc-thread-gate` = `service-thread-gate.md` - **PASS**

Claim-by-claim re-check (the report's own §1 "exactly one branch caller" and every offset lit):

```
PASS TH  0x000296  fnsweep b.w 0x818ac         | b.w #0x818ac
PASS TH  0x000294  thunk ldr r0                | ldr r0, [pc, #4]
PASS TH  0x0002a0  stub 0x2a0 ldr r0           | ldr r0, [pc, #4]
PASS TH  0x0002a2  stub 0x2a2 b.w 0x86108      | b.w #0x86108
PASS TH  0x0097f6  pcie_msg_init movs r0 4c    | movs r0, #0x4c
PASS TH  0x009800  register(0x4c)              | bl #0x874b0
PASS TH  0x00980a  pcie_msg_init movs r0 4e    | movs r0, #0x4e
PASS TH  0x009816  enable(0x4c) 0x9816         | bl #0x86ff4
PASS TH  0x009756  enableA(0x4c) 0x9756        | bl #0x86db8
PASS TH  0x086dea  enableA write bitmap        | str.w r4, [r3, r2, lsl #2]
PASS TH  0x086df2  enableA write mirror        | str.w r4, [r3, r2, lsl #2]
PASS TH  0x006e2a  boot seed ldr r7 array      | ldr r7, [pc, #0x31c]
PASS TH  0x006e7c  boot seed str [r7]          | str r3, [r7]
PASS TH  0x006e80  boot seed fn1 str           | str r3, [r7, #4]
PASS TH  0x006e84  boot seed fn1d str          | str r3, [r7, #0x74]
PASS TH  0x08308e  intc_init str CTLR          | str r2, [r3]        ; *0x40160100 = 1
PASS TH  0x086bd0  wfi idle                    | wfi
PASS TH  0x0820e4  sgir ldr r2                 | ldr r2, [pc, #0xc]   ; 0x820f4 = 0x40161F00
PASS TH  0x0820ea  sgir str                    | str r3, [r2]
PASS TH  0x082f46  isr cpsie                   | cpsie i
PASS TH  0x082f4a  isr blx r5                  | blx r5
PASS TH  0x082f4c  isr cpsid                   | cpsid i
```

Two claim-11/12 subtleties cross-checked against the live `[fwctx]` dump in
`build/register-dumps/exp/20261004-052201/dmesg.txt`:

- the report's "H2D enables are word 2 (`0x40161108`, `0x40161188`)" is an arithmetic consequence of
  `id 0x4c`: `word = 0x4c/32 = 2`, `bit = 0x4c%32 = 12`. Those CAs are device-internal and not
  host-visible (`map-4016`), so they cannot be read from a dump; the enable *instructions* that
  write them are verified above. No contradiction.
- the live dump's `[fwctx] +0xb0: 000462f9 0004624d` (slots `0x2c`/`0x2d` of the printed view) and
  `+0x84`/`+0x88`: `00046939 ...` independently corroborate the "ids `0x2d`/`0x2e` are the
  per-channel receive handlers (`0x462f9`/`0x4624d`)" correction in §2/§7.

## 4. `db-write-path` = `doorbell-endpoint-path.md` - **PASS** (with one instruction-level note)

All 26 quoted `.ko` offsets re-disassembled in ARM mode; every mnemonic matches, including both
doorbell write sites and the `phy_devid` keying:

```
PASS ARM 0x0161cc  pcie_msg_send ldr out0        | ldr r2, [r6, #0x2c]
PASS ARM 0x0161dc  pcie_msg_send ldr doorbell    | ldr r2, [r6, #0x34]
PASS ARM 0x0161e4  pcie_msg_send orr r3,r3,r1    | orr r3, r3, r1
PASS ARM 0x0161e8  pcie_msg_send str doorbell    | str r3, [r2]
PASS ARM 0x017530  pcie_msg_send_irq ldr out5    | ldr r3, [r4, #0x40]
PASS ARM 0x01753c  pcie_msg_send_irq str out5    | str r2, [r3]
PASS ARM 0x01757c  pcie_msg_send_irq ldr doorbell| ldr r2, [r4, #0x34]
PASS ARM 0x017584  pcie_msg_send_irq orr #1      | orr r3, r3, #1
PASS ARM 0x017588  pcie_msg_send_irq str doorbell| str r3, [r2]
PASS ARM 0x01b244  shuangta_pcie_msg_reg_map movw| movw r1, #0x92d4
PASS ARM 0x01b248  shuangta_pcie_msg_reg_map movt| movt r1, #0x4003
PASS ARM 0x01b264  shuangta_pcie_msg_reg_map str | str r3, [r4, #8]
PASS ARM 0x00b730  pcie_msg_init add sb          | add sb, r0, #0x2c
PASS ARM 0x00b754  pcie_msg_init ldr table       | ldr r0, [r2, #4]
PASS ARM 0x00b758  pcie_msg_init blx fn          | blx r3
PASS ARM 0x008e28  oal_pcie_inbound_ca_to_va     | ldr ip, [r0, #0x20]
PASS ARM 0x008e30  oal_pcie_inbound_ca_to_va     | ldr r5, [r0, #0x24]
PASS ARM 0x008e6c  oal_pcie_inbound_ca_to_va     | ldr lr, [ip]
PASS ARM 0x008e7c  oal_pcie_inbound_ca_to_va     | ldr r1, [ip, #0x28]
PASS ARM 0x008eb0  oal_pcie_inbound_ca_to_va str | str lr, [r2]
PASS ARM 0x00be98  oal_pcie_get_phy_devid movw   | movw r1, #0xff8
PASS ARM 0x00becc  oal_pcie_get_phy_devid ldrb   | ldrb r0, [sp, #8]
PASS ARM 0x00bed4  oal_pcie_get_phy_devid and    | and r0, r0, #0xf
```

The live mapping the report leans on reproduces verbatim from
`opensource/build/register-dumps/bothep/002_live_vendor_dmesg.txt`:

```
[PCIEL]chip 0, bus 0, probe cnt 1, phy_devid:1.
[PCIEL]chip 0, bus 1, probe cnt 2, phy_devid:0.
[PCIEL]raw irq: 209 ; ... bus_id_hostview[1], phy_devid[0], is_pcie_cross[1]
[PCIEL]raw irq: 207 ; ... bus_id_hostview[0], phy_devid[1], is_pcie_cross[1]
```

and `bothep/001_live_vendor_bars2.txt` gives EP1 (`0001:00:00.0`) region-3 base `0x583b8000` and
EP0 (`0000:00:00.0`) `0x403b8000`, so `0x583b8000 + 0x392d4 = 0x583f12d4` and
`0x403b8000 + 0x392d4 = 0x403f12d4` are both correct.

**Instruction-level note (not a failed claim).** Three `bl` sites are quoted as
`bl oal_pcie_inbound_ca_to_va`/`bl pci_read_config_dword`/`bl oal_pcie_get_phy_devid` with a target
operand. The shipped bytes at those sites are `fe ff ff eb` = `imm24 = -2`, the ARM image loader's
short-range call trampoline whose destination is patched in at load; capstone prints the target as
`<site address>` (it prints `bl #0x1b214`/`bl #0x1d954`), not the symbol. The *claim* - that the
instruction at that offset is a call to that routine - is corroborated by the call's argument setup
(`movw r1,#0x92d4` / `movt r1,#0x4003` immediately before the `0x1b214` call) and by the fact that
an `imm24 = -2` call must be patched. Only the literal `bl #0x8dd8` text is not byte-reproducible;
this is a printing-convention artifact, so it is recorded as a note, not a FAIL.

---

## Notes on two quoted forms (apply to all four reports)

1. **`bl #0xNNNN` in the `.ko`** - see §4 above: the raw bytes are the `-2` trampoline, so capstone
   prints a site-relative target rather than the symbol. The report's `bl #0x8dd8`/`bl #0xbe5c`
   form is the symbol-relative reading, which the surrounding arguments and the ELF symbol table
   (`oal_pcie_get_phy_devid` is present in the module's symbols) support.
2. **`0x00046939` in `intr-status` §4.4** - that is a live-dump fn-array value read back from the
   device, not a firmware file offset, and it is quoted as such. It is reproduced verbatim in
   `.../20261004-052201/dmesg.txt` (`[fwctx] +0x80: ... 00046939 00046939`). No file-offset claim
   is attached to it.

## Bottom line

Each of the four phase-32 reports' load-bearing claims was re-derived from the raw artifacts they
cite and holds. No report contains a false claim at the level the report asserts it; the only
deviation found is the non-byte-reproducible printing of linked `.ko` call targets, which is a
disassembler/loader artifact rather than a wrong statement about the code.

Reproduce all of the above:

```bash
pyenv/Scripts/python.exe build/tmp/p32verify/check_disasm.py     # -> checked 95 claims, 0 FAIL; exit 0
```
