# Phase 45 verification (2026-10-04)

Task `st_01a10757`. Scope: verify the three phase-45 reports against the sources and binaries they
cite, and record one verdict per report. Read-only against the device; nothing was written on the
router and no register was touched.

**Result**

| report | verdict |
| --- | --- |
| [hi1105-mailbox-irq.md](hi1105-mailbox-irq.md) | **PASS** |
| [hi3881-mailbox-irq.md](hi3881-mailbox-irq.md) | **PASS** |
| [reconcile-luofu.md](reconcile-luofu.md) | **FAIL** - one wrong claim: the `plat.ko` "file = `.text` + `0x38`" label. The quoted instructions are byte-identical, but they sit at the bare symbol offset, not at that offset plus `0x38` (row-by-row evidence in §3). |

Everything else in `reconcile-luofu.md` that this task re-derived is correct: all 22 firmware
instructions, all six firmware literals, the helper tables, the literal scan, the `reg_all.txt` and
dmesg values, and the substantive `plat.ko` claims once disassembled the repo's own way.

## 1. hi1105-mailbox-irq.md - PASS

Method: re-fetched the pinned commit's raw files and string-matched each quoted line, then
independently re-derived the arm register, its bit, and the polarity.

| cited URL (pinned `297b33b07b56adb3fa53baf618fdbc16ad9fd447`) | HTTP | quote checked |
| --- | --- | --- |
| `.../platform/inc/oal/mp17c/pcie_ctrl_rb_regs.h` | 200, 63,273 B | `#define PCIE_CTRL_RB_BASE    (0x04980000)` at L12; `#define PCIE_CTRL_RB_HOST2DEVICE_INTR_SET_OFF    0x2D4` at L334; `#define PCIE_CTRL_RB_HOST_INTR_MASK_OFF    0x2E8` at L402; `unsigned int host2device_tx_intr_mask : 1; /* 0 */` at L386; `..._RAW_STATUS_OFF 0x2E4` L379; `..._STATUS_OFF 0x2EC` L425; `..._CLR_OFF 0x2F0` L442; `host2device_tx_intr_clr : 1;` L432 - all present |
| `.../platform/oal/pcie/chip/mp17c/pcie_chip_mp17c.c` | 200, 25,178 B | `oal_frw_msg_int_unmask_mp17c` at L466 with `host2device_tx_intr_mask = 0;` L477 and `device2host_rx_intr_mask = 0;` L478; `/* mask:1 for mask, 0 for unmask */` at L261; `oal_ete_intr_init` L230; `reg->h2d_intr_addr = ... + PCIE_CTRL_RB_HOST2DEVICE_INTR_SET_OFF;` L298 - all present |
| `.../platform/oal/pcie/ete/ete_host.c` | 200 | `oal_pcie_h2d_int` L1202 with `host2device_tx_intr_set = 1` L1213; `oal_pcie_d2h_int` L1219 with `device2host_rx_intr_set = 1` L1231; `oal_pcie_frw_int_func_register` L1665 mapping `g_pcie_ete_intx_callback[DEVICE2HOST_RX_INTR_MASK]` L1672; no-op `oal_ete_host2device_tx_intr_cb` L2113 registered at L2139; `oal_writel(0xffffffff, ...host_intr_mask_addr)` L1347 - all present |
| `.../platform/oal/pcie/pcie_firmware_msg.c` | 200 | `oal_pcie_firmware_tx_memcpy_trigger` L221; D2H ISR L386 and its registration L393-401; `oal_pcie_firmware_msg_int_unmask` L442-447; `oal_firmware_msg_download_pre` L486 with the unmask at L502 - all present |
| `.../platform/inc/oal/ete/ete_comm.h` | 200 | enum `_PCIE_HOST_CTL_INTR_` at L42 with `HOST2DEVICE_TX_INTR_MASK` as element 0 - present |

Substantive check (independent re-derivation): the H2D doorbell is a separate register from the
enable. `oal_pcie_h2d_int` writes bit 0 of `0x2D4` (`host2device_tx_intr_set`), and the enable is
bit 0 of `0x2E8` cleared to 0 by `oal_frw_msg_int_unmask_mp17c` / `oal_ete_intr_init`, with the
mask polarity stated in code at L261. **Correct.** The report's own flagged caveat (the eDMA
header's different bit numbering) is also correct and correctly labelled. Its quote of
`oal_firmware_msg_download_pre` at "L486-L515" is accurate; the internal line numbers it gives for
`oal_frw_msg_int_unmask_mp17c` as a range (`L466-L483`) are the function's full extent, accurate.

## 2. hi3881-mailbox-irq.md - PASS

Method: re-fetched the four load-bearing raw files and compared quoted lines and line numbers.

| cited URL (`openharmony/device_soc_hisilicon`, `master`) | HTTP | quote checked |
| --- | --- | --- |
| `.../driver/oal/oal_sdio.h` | 200, 3,334 B | `HISDIO_REG_FUNC1_INT_ENABLE  0x09` L40; `HISDIO_REG_FUNC1_WRITE_MSG 0x24` L42; `HISDIO_FUNC1_INT_MFARM (1 << 2)` L52; `HISDIO_FUNC1_INT_MASK (DREADY | RERROR | MFARM)` L55 - all present, mask = `0x07` |
| `.../driver/oal/oal_sdio_host.c` | 200, 73,695 B | `oal_sdio_dev_init` L1079; status clear `data = HISDIO_FUNC1_INT_MASK;` L1099 + write to `0x08` L1100; enable write to `HISDIO_REG_FUNC1_INT_ENABLE` L1118 preceded by `data = HISDIO_FUNC1_INT_MASK;` L1117; `oal_enable_sdio_state(bus, OAL_SDIO_ALL)` L1124; de-init writes 0 to `0x09` L1146; `hi_sdio->func1_int_mask = HISDIO_FUNC1_INT_MASK;` L2139; `oal_sdio_send_msg` L147 with `1 << val` L175 and the `WRITE_MSG` write L176; MFARM branch in the ISR L821; `oal_sdio_message_register` L316 - all present at those exact lines |

Substantive check: the enable register is `0x09` written with source-class mask `0x07`, the doorbell
is the message-bitmap write `1 << msg_id` to `0x24`, and per-id routing is software. **Correct.**
Nothing quoted depends on wording that this verification could not reproduce byte-for-byte.

## 3. reconcile-luofu.md - FAIL

### 3.1 Binaries verified

| binary | md5 (this task) | report md5 |
| --- | --- | --- |
| `build/versions/2.4.15/FIRMWARE.bin` (identical to `rootfs-2.4.15/.../lib/firmware/hi_wifi/FIRMWARE.bin` and 7 other copies) | `0e530b976d5a20e87358671f1a577695` | matches |
| `build/tmp/hi5622v100_plat.ko` | `23660bc285393e678d5cade1c36c194b` | matches |

capstone 5.0.7 against those bytes, both Thumb and ARM mode, as required. All checks below are in
`opensource/build/verify_p45.py` (re-runnable, exit 0).

### 3.2 Firmware rows - PASS (22/22 instructions, 6/6 literals, both tables)

Disassembled as Thumb at the report's offset, read as a **file** offset.

| offset | report claim | re-disassembly |
| --- | --- | --- |
| `0x86f32` | `ldr r0, [pc, #0x80]` -> literal `0x86fb4 = 0x40039000` | matches; literal `0x40039000` |
| `0x86f3e` | `bl` to helper `0x7c0` | `bl #0x7c0` |
| `0x86f42` / `0x86f44` | `ldr r0, [pc, #0x70]` -> `0x40039000`; `bl #0x78e` | both matches |
| `0x86f48` / `0x86f4a` | `ldr r0, [pc, #0x6c]` -> `0x40039800`; `bl #0x7c0` | both matches; literal `0x40039800` |
| `0x86f4e` / `0x86f50` | `ldr r0, [pc, #0x68]` -> `0x40039800`; `bl #0x78e` | both matches |
| helper `0x7c0` | `0x7dc str.w r3,[r5,#0x41c]`; `0x7e2 str.w r7,[r5,#0x428]`; `0x7e6 str.w r6,[r5,#0x428]`; `0x7ea ldr.w r3,[r5,#0x424]`; `0x7f2 cmp r4,#0x16` | all five match verbatim |
| helper `0x7c0` source | literal `0x7fc = 0x00103ba0` | `0x103ba0` |
| helper `0x78e` | `0x7aa ldr.w r2,[sp,r3,lsl #2]`; `0x7b2 ldr r2,[r6,r2]`; `0x7b0 cmp r3,#0xa` | all three match |
| helper `0x78e` table | literal `0x7bc = 0x00103b78`, table = `{0x224,0x23c,0x414,0x43c,0x440,0x460,0x464,0x468,0x46c,0x514}` | literal `0x103b78`; table reads back exactly `[0x224,0x23c,0x414,0x43c,0x440,0x460,0x464,0x468,0x46c,0x514]` |
| `0x9770` / `0x9776` | `str.w r1,[r5,#0xdc]`; `str.w r1,[r5,#0xe0]` | both match |
| `0x9784` | `strd r2,r3,[r5,#0xd0]` | matches; literals `0x97dc = 0x40039014`, `0x97e0 = 0x40039010` |
| `0x818b2` / `0x818b8` | `movs r7,#1`; `str r7,[r2]` (ack) | both match |
| `0x818c0` / `0x818c4` | `movs r1,#8`; `str r1,[r2]` (doorbell re-arm) | both match |
| literal scan | no LE literal for `0x400392e4/0x400392e8/0x400392ec/0x400392f0/0x400392d4`; literals present for `0x40039000` (0x86fb4, 0xcf2ac), `0x40039010` (0x97e0), `0x40039014` (0x97dc, 0x86fbc), `0x40039224` (0x86c68), `0x40039508` (0xccee0) | exact match on all 12 addresses |

### 3.3 The wrong claim - the `plat.ko` "file = `.text` + `0x38`" label

The report states, at `reconcile-luofu.md:228-229` (verbatim):

> FIRMWARE.bin md5 `0e530b976d5a20e87358671f1a577695`; runtime = file + `0x40000`. plat.ko md5
> `23660bc285393e678d5cade1c36c194b`, file = `.text` + `0x38`.

The quoted instruction **bytes** are correct, and the offsets as *symbol* offsets are correct, but
the `+0x38` mapping is not: the bytes it points at are not the quoted instructions. Disassembling
`plat.ko` in ARM at file offset `off - 0x38` (the report's label) and at `off`:

| report offset | quoted instruction | at its own offset `off - 0x38` (report's label) | at bare offset `off` (the symbol offset the module actually uses) |
| --- | --- | --- | --- |
| `0x074a4` | `movw sb, #0xfc20` | `lsls r4, r3, #0x14` | `movw sb, #0xfc20` |
| `0x074a8` | `movt sb, #0xffff` | `movs r0, r0` | `movt sb, #0xffff` |
| `0x074fc` | `ldr r8, [r3, #0x2e8]` | `movs r4, r0` (or no decode) | `ldr r8, [r3, #0x2e8]` |
| `0x07504` | `and r8, r8, sb` | `movs r1, #4` | `and r8, r8, sb` |
| `0x07520` | `str r8, [r2, #0x2e8]` | `movs r0, #4` | `str r8, [r2, #0x2e8]` |
| `0x075ac` | `movw r2, #0xf8f8` | `adds r0, #0` | `movw r2, #0xf8f8` |
| `0x075b0` | `movt r2, #0xffe0` | `adds r0, #0` | `movt r2, #0xffe0` |

Confirming byte evidence: byte `0x74a4` is `72 31 00 e3`, the ARM encoding of `movw r3,#0x172` -
not `movw sb,#0xfc20`. The encoded `movw sb,#0xfc20` is `20 9c 0f e3`, at file offset `0x74a4` of
the module **as the report itself reads it** (so the offsets it prints are the ones it used).

Root cause, checked: `readelf` shows `plat.ko` `.text` at `sh_addr = 0x0`, `sh_offset = 0x38`. The
`0x38` is the ELF section header table's position in the file, not a load bias. Because `sh_addr`
is 0 there is no `+0x38`; the file offset equals the symbol offset (`pcie_ete_chn_res` = `0x7490`,
`pcie_ete_intr_init` = `0x7528`). The repo's own tool
(`opensource/lab/ko_disasm.py`: `sec.data()[val:val+size]`, no bias, ARM mode) agrees, and it
reproduces every quoted `plat.ko` instruction verbatim:

```
===== pcie_ete_chn_res @ 0x7490 size=152 (.text) =====
  0x0074a4: 209c0fe3 movw     sb, #0xfc20
  0x0074a8: ff9f4fe3 movt     sb, #0xffff
  0x0074fc: e88293e5 ldr      r8, [r3, #0x2e8]
  0x007504: 098008e0 and      r8, r8, sb
  0x007520: e88282e5 str      r8, [r2, #0x2e8]
===== pcie_ete_intr_init @ 0x7528 size=344 (.text) =====
  0x0075ac: f8280fe3 movw     r2, #0xf8f8
  0x0075b0: e02f4fe3 movt     r2, #0xffe0
  0x0075b4: 012002e0 and      r2, r2, r1
  0x0075b8: 002083e5 str      r2, [r3]
```

So all seven `plat.ko` offsets are correct **as symbol offsets**, and the quoted mnemonics are exact.
The sole defect is the report's mapping label, which says `file = .text + 0x38` and therefore points
an `+0x38` reader at unrelated instructions.

Effect: the finding stands; the mapping statement is false. The mask RMW "at `+0x2e8` with constant
`0xfffffc20`" is real, but a reader who follows the report's own rule and disassembles file
`0x74a4 + 0x38 = 0x74dc` will read `str r7, [r2]` and may wrongly conclude the claim was fabricated.
The same corruption would hit the port's own copy of the reader code.
The **firmware** rows are not affected - they were computed with a *different* rule
("runtime = file + `0x40000`") and their quoted instructions do sit at the printed file offset
(`0x86f3e bl #0x7c0` at file `0x86f3e`; at file `0x86f3e - 0x40000` there is no call at all). That
rule is an internal inconsistency, not a wrong instruction: the printed firmware offsets are
correct as file offsets, but the stated `+0x40000` would move every one of them.

### 3.4 Other re-derived values - PASS

| claim | check | result |
| --- | --- | --- |
| vendor live values (`reg_all.txt`) | `400392e8 = 20` (L2811), `400392e4 = 0` (L2810), `400392ec = 0` (L2812), `400392f0 = 0` (L2813), `400392d4 = 0` (L2806), `40039508 = 3f201818` (L2947), `40039000 = 10b` (L2625) | all match |
| twin block | `40039ae8 = 3ff` (L3154), `40039ad4 = 0` (L3149), `40039d08 = 3f3f1f1f` (L3290), `40039800 = 10c` (L2968) | all match |
| `40039008 = 0`, `4003900c = 0` (SDIO-shape candidate resolution) | reg_all L2627/L2628 | match; `0x40039108 = 0x1a` also matches |
| vendor cfg `0x2d0 = 5` on both blocks | reg_all L2805, L3148 | match |
| takeover reset of the mask | dmesg `glue chn_res pre=0x000003ff mask=0xfffffc20` in all `exp/20261004-*` runs (e.g. `20261004-041246/dmesg.txt:689`) | match |
| `intr pre=0x3f3f1f1f` | same file line 663 | match |
| "the 20261002 runs read `0x00000000`" | `0x00000000` in `20261002-150844/162146/172042/172556/182028`, `0x000003ff` from `20261002-214710` on | **imprecise**: true for the five listed early runs, but `20261002-214710`, `-222521`, `-223208`, `-233007` already read `0x3ff`. Does not change the conclusion (the port's `0x3ff & 0xfffffc20 = 0x20` reproduces the vendor value) |
| port source `wifidrv1.c` | `OMO_CHN_RES 0x2e8` L170, `OMO_GLUE_CHN_RES_MASK 0xfffffc20U` L357, RMW at L562-564 | match |

## 4. Reproduction

```bash
cd C:/Users/ShibbityShwab/Documents/GitHub/cudy-wr3000v2
./pyenv/Scripts/python.exe opensource/build/verify_p45.py   # exit 0; 45 PASS / 7 FAIL, all FAILs = the 7 rows of §3.3
./pyenv/Scripts/python.exe opensource/lab/ko_disasm.py build/tmp/hi5622v100_plat.ko pcie_ete_chn_res pcie_ete_intr_init
```

The 7 FAILs are the deliberate negative check: they are exactly the `plat.ko` rows disassembled
under the report's own `+0x38` label, which is the wrong claim §3.3 names.
