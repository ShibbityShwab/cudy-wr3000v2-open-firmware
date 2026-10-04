# phase47 VERIFICATION - independent re-check of the three reports (2026-10-04)

Task `st_01a10771`. Method: every quoted firmware offset was re-disassembled from the bytes on disk
with the repo venv (`pyenv/Scripts/python.exe`, capstone 5.0.7, `CS_ARCH_ARM`/`CS_MODE_THUMB`); the
sibling report's quotes were re-fetched raw from `raw.githubusercontent.com` at the pinned commit and
compared line-by-line. Nothing was edited except this file.

| item | value |
| --- | --- |
| image | `build/tmp/FIRMWARE.bin` |
| size / md5 | 928,920 B (`0xe2c98`) / `0e530b976d5a20e87358671f1a577695` - **matches all three reports** |
| disassembler | `pyenv/Scripts/python.exe`, capstone 5.0.7, `CS_ARCH_ARM`, `CS_MODE_THUMB` |
| sibling pin | `5-Super-Rookie-5/hongmengkernel` @ `297b33b07b56adb3fa53baf618fdbc16ad9fd447` (re-fetched) |
| whole-image call sweep | capstone linear sweep, 350,275 instructions, `skipdata` |

## Verdicts

| report | verdict |
| --- | --- |
| `fw-source-ids.md` | **PASS** (one wrong peripheral detail, non-load-bearing; named below) |
| `sibling-source-map.md` | **PASS** |
| `reconcile-source-ids.md` | **PASS** |
| note | `reconcile-source-ids.md`'s §4 says "the four register calls are `0x96ca`/`0x96dc`/`0x9800`/`0x980c`" and gives them ids `0x2d/0x2e/0x4c/0x4e` - re-verified correct (§C). Its §1 also names the `0x4C` id as the ctrl-rb H2D line and its caveat that `pcie_msg_init` has no direct caller - both verified. |

---

## A. `fw-source-ids.md` - PASS

### A1. Entry points and their bodies (re-disassembled)

| claim | offset | observed bytes | observed mnemonic | ok |
| --- | --- | --- | --- | --- |
| `register` core prologue | `0x874b0` | `2de9f041` | `push.w {r4,r5,r6,r7,r8,lr}` | yes |
| handler array store | `0x874e8` | `c3f89880` | `str.w r8, [r3, #0x98]` | yes |
| descriptor base literal | `0x874f6` (pool `0x87538`) | `104a` | `ldr r2, [pc, #0x40]` -> `0x00105f8c` | yes |
| target lane (ids > 0x1f) | `0x8750c`/`0x87526` | `2c50` | `ldr r0,[pc,#0x2c]` -> `0x40161800`; `str r4,[r5,r0]` | yes |
| `register` wrapper | `0x86da2`/`0x86da4`/`0x86dae` | `32b1`/`5f28`/`00f07fbb` | `cbz r2`; `cmp r0,#0x5f`; `b.w #0x874b0` | yes |
| `enable` head | `0x86ff4`/`0x86ff6`/`0x86ffa` | `38b5`/`a0f11003`/`4f2b` | `push {r3,r4,r5,lr}`; `sub.w r3,r0,#0x10`; `cmp r3,#0x4f` | yes |
| `enable` bitmap write | `0x8701a`-`0x8702c` | `6209`/`04f01f04`/`03fa04f4`/`074b`/`43f82240` | `lsrs r2,r4,#5`; `and r4,r4,#0x1f`; `lsl.w r4,r3,r4`; `ldr r3,[pc,#0x1c]`; `str.w r4,[r3,r2,lsl #2]` | yes |
| ISENABLER literal | `0x87044` (pool of `0x87026`) | `0x40161100` | unique occurrence in the image (1 hit, whole-image word scan) | yes |
| class helper | `0x81a3c`/`0x81a60`/`0x81a72` | `30b5`/`2b59`/`2851` | `push {r4,r5,lr}`; `ldr r3,[r5,r4]`; `str r0,[r5,r4]`, pool `0x81a88` = `0x40161400` | yes |
| `disable+clear` | `0x86dba`/`0x86de8`/`0x86dea`/`0x86dee`/`0x86df2` | `a0f11003`/`084b`/`43f82240`/`03f58073`/`43f82240` | gate; `ldr r3,[pc,#0x20]` pool `0x86e0c` = `0x40161180`; store; `add.w r3,r3,#0x100`; store (ICPENDR computed, not literal) | yes |

### A2. Call-site counts (whole-image sweep)

* `bl/b.w #0x874b0`: `0x96ca`, `0x96dc`, `0x9800`, `0x980c` (4 direct) + the wrapper tail `0x86dae` - the report's "four direct callers".
* `bl #0x86da2` (wrapper): **24** sites - `0x6f70,0x6f7a,0x7200,0x7212,0x7ce0,0x7ee40,0x821a,0x91ef4,0x91f04,0x91f14,0x91f24,0x91f34,0x91f44,0x91f54,0x91f64,0x921dc,0x921ec,0x921fc,0x9220c,0x9221c,0x9222c,0x9223c,0x9224c,0x93f64` -> 4+24 = **28 register sites** = the report's table count.
* `bl/b.w #0x86ff4` (enable): **29** sites, matching the report's list one for one
  (`0x632 0x18f0 0x7206 0x7218 0x96ea 0x96f0 0x9816 0x981c 0x707aa 0x707b4(b.w) 0x707ba 0x7ee46 0x8708c 0x91efa 0x91f0a 0x91f1a 0x91f2a 0x91f3a 0x91f4a 0x91f5a 0x91f6a 0x921e2 0x921f2 0x92202 0x92212 0x92222 0x92232 0x92242 0x93f6a`).
* `bl #0x86db8` (disable): **17** sites - matches the report's table row count exactly.

### A3. The ctrl-rb id chain (the report's headline)

| claim | evidence re-disassembled | ok |
| --- | --- | --- |
| register `0x4c` at file `0x9800`, `fn = 0x40295` | `0x97f6 movs r0,#0x4c`; `0x97fe ldr r2,[pc,#0x58]` -> pool `0x9858` = `0x00040295`; `0x9800 bl #0x874b0` | yes |
| class `r1 = 5` at `0x979e` | `0x979e movs r1,#5` | yes |
| enable `0x4c` at file `0x9816` -> word 2 bit 12 | `0x9814 movs r0,#0x4c`; `0x9816 bl #0x86ff4`; enable body computes `0x4c>>5=2`, `0x4c&0x1f=12` -> CA `0x40161100+8` = `0x40161108` bit 12 | yes |
| H2D thunk `0x294` -> `0x818ac` | `0x294 ldr r0,[pc,#4]` (pool `0x29c` = `0x0010c190`); `0x296 b.w #0x818ac` | yes |
| dispatcher acks `0x400392f0`=1, consumes `out[0]`, re-arms 8 | `0x818b2 movs r7,#1`; `0x818b8 str r7,[r2]`; `0x818bc ldr r5,[r2]`; `0x818be str r1,[r2]`; `0x818c0 movs r1,#8`; `0x818c4 str r1,[r2]` | yes |
| D2H mirror `0x2a0` -> `0x86108` | `0x2a0 ldr r0,[pc,#4]` (pool `0x2a8` = `0x0010c0f4`); `0x2a2 b.w #0x86108` | yes |
| descriptor record `4c 05 01` at file `0xC6070` | bytes `4c 05 01` at `0xC6070`; `0xC6076` = `4e 05 01` | yes |
| init zeroes the fn array | `0x6e5c mov.w r3,#0x180`; `0x6e66 bl #0x81e0c` | yes |
| source count `(TYPER[4:0]+1)<<5` | `0x6e86/0x6e8a/0x6e8c/0x6e90/0x6e92/0x6e94` (pool `0x7168` = `0x40161004`) | yes |
| distributor CTLR 0/1, CPU interface init | `0x6e98 str r6,[r7]`; `0x6ed4 str.w sl,[r7]`; `0x6eda bl #0x83024`; `0x8302c`..`0x8304a` sweep; `0x83054` PMR pool `0x83098` = `0x40160104`; `0x8305a` BPR; `0x8308e` CTLR `0x40160100` | yes |
| ISR swallow path | `0x82f22 it hi`; `0x82f24 movs r5,#0`; `0x82f48 cbz r5,#0x82f4c`; `0x82f52 str r7,[r3]` | yes |
| `pcie_msg_init` fn-pointer word `0xcf254` = `0x00049335` | `0xcf254` = `0x00049335` | yes |

### A4. One wrong peripheral detail (does not change the verdict)

The report's section 5 item 2 gives the **wrong values for the handler-array pre-writes**. It says:

> `0x6e7c str r3,[r7]` = `0xc2081` -> array[0] (file `0x82081`);
> `0x6e80 str r3,[r7,#4]` = `0xc6c89` -> array[1] (file `0x86c89`);
> `0x6e84 str r3,[r7,#0x74]` = `0x40161004` -> array[0x1d]`.

Re-reading the pool that actually feeds each `str` (each `ldr r3,[pc,#...]` is resolved by
`((pc+4) & ~3) + imm`):

```
0x6e7c <- pool 0x7158 = 0x000c2081  -> array[0]     (report: 0xc2081  - CORRECT)
0x6e80 <- pool 0x715c = 0x40161c00  -> array[1]     (report: 0xc6c89  - WRONG)
0x6e84 <- pool 0x7160 = 0x000c20a3  -> array[0x1d]  (report: 0x40161004 - WRONG)
```

So the third pre-write is **`0x000c20a3`** (a code pointer, file `0x820a3`), not the TYPER register,
and the second is `0x40161c00` (the distributor **ICFGR** base), not `0xc6c89`. The report's
"flagged oddity" (a register address sitting in a handler slot) does not exist; the oddity is
instead that ICFGR is pre-written into `fn_array[1]`. Both surviving facts the report uses are
still true: the array is zeroed first (`0x6e5c`), the `0x1d` slot is pre-written with a code
pointer (consistent with `enable(0x1d)` at `0x8708c`), and the `0x4c`/`0x4e` register+enable pair
is exactly as claimed. PASS stands; this paragraph is the correction.

---

## B. `sibling-source-map.md` - PASS

Every quote below was re-fetched at the pinned commit and compared to the report's quoted text.
Byte/line counts: `pcie_ctrl_rb_regs.h` 64,551 B / 1,883 lines; `pcie_chip_mp17c.c` 25,384 / 646;
`ete_comm.h` 9,893 / 239; `ete_host.c` 87,151 / 2,325; `pcie_firmware_msg.c` 20,609 / 582;
`host_ctrl_rb_regs.h` 159,679 / 4,677; `pcie_linux.c` 131,976 / 3,925; `pcie_pm_mp17c.c` 13,462 /
312; `oal_hardware.h` 1,099 / 39; `frw_event_deploy.c` 22,564 / 622. (The report's byte figures differ
slightly, e.g. `63,273` vs `64,551`, consistent with CRLF-vs-LF counting, not content.)

| quote | location | re-fetched content | ok |
| --- | --- | --- | --- |
| `PCIE_CTRL_RB_BASE (0x04980000)` | `pcie_ctrl_rb_regs.h` L12 | exact | yes |
| `HOST2DEVICE_INTR_SET_OFF 0x2D4`, bits 0-3 | L323-L334 | exact (`host2device_tx_intr_set : 1; /* 0 */`) | yes |
| `RAW_STATUS_OFF 0x2E4`, bits 0-11 | L360-L381 | exact | yes |
| `HOST_INTR_MASK_OFF 0x2E8`, bit0 `host2device_tx_intr_mask`, bit5 `reserved0` | L383-L402 | exact | yes |
| `HOST_INTR_STATUS_OFF 0x2EC` | L406-L425 | exact | yes |
| `HOST_INTR_CLR_OFF 0x2F0`, bit0 `host2device_tx_intr_clr` | L429-L442 | exact | yes |
| `PCIE_MSG_INTR_OFF 0x2A8` | L258-L266 | exact | yes |
| `HOST_CTRL_RB_BASE 0x04985000` | `host_ctrl_rb_regs.h` L12 | exact | yes |
| `ETE_INTR_MASK_OFF 0x86C` (tx_err 0:4, tx_done 8:12, rx_err 16:21, rx_done 24:29) | L2920-L2934 | exact | yes |
| `ETE_INTR_CLR/STS/RAW_STS/CH_DR_EMPTY` = `0x870/0x874/0x878/0x87C` | L2950/2968/2986/2999 | exact | yes |
| `PCIE_CTL_ETE_INTR_*` = `0xF00/0xF04/0xF08/0xF10` | L4330/4348/4366/4384 | exact | yes |
| `HOST_TRIGGER_DEVIE_*` = `0xEB0/0xEB4/0xEB8` | L4280-L4312 | exact | yes |
| `pcie_host_ctl_intr` enum, `HOST2DEVICE_TX_INTR_MASK` first | `ete_comm.h` L42-L60 | exact | yes |
| `g_pcie_ete_intx_callback[PCIE_HOST_CTL_INTR_BUTT]` | `ete_host.c` L36 | exact | yes |
| dispatch loop `for (bit = 0 ...)`, `"unregister host intr:%u"` | L163-L189 | exact | yes |
| `ete_h2d_sr_ctrl`: `dst_intr : 1 /* 13 */`, `src_intr : 1 /* 14 */` | L62-L71 | exact | yes |
| `st_tx_sr_item.ctrl.bits.{src,dst}_intr = 1` | `ete_host.c` L1008-L1020 | exact | yes |
| mask polarity comment `/* mask:1 for mask, 0 for unmask */` | `pcie_chip_mp17c.c` L261 | exact | yes |
| `oal_ete_intr_init` = `/* device intx/msi init */`; `host2device_tx_intr_mask = 0` (L245); `int_event_ignore_mask = (1 << HOST2DEVICE_TX_INTR_MASK)` (L273) | L229-L275 | exact | yes |
| `oal_frw_msg_int_mask_mp17c`: bit=1 (L458) | L447-L464 | exact | yes |
| `oal_frw_msg_int_unmask_mp17c`: bit=0 (L477) | L466-L483 | exact | yes |
| `oal_firmware_msg_download_pre` -> `..._int_unmask(pcie_res)` | `pcie_firmware_msg.c` L486-L502 | exact (L502) | yes |
| resting mask `host_intr_mask.as_dword = 0x0`, bit0 never set | `ete_host.c` L1683-L1701 | exact | yes |
| ISR strips ignore bit: `intr_status &= ~int_event_ignore_mask`; comment L416 "host doesn't report... wait for device" | `ete_host.c` L411-L421 | exact | yes |
| `oal_ete_host2device_tx_intr_cb`: `"host2device intr, ignore by host"` | L2108-L2116 | exact | yes |
| `oal_ete_intr_register(HOST2DEVICE_TX_INTR_MASK, ...)` after mask init | L2135-L2143 | exact | yes |
| host INTx = PCI device IRQ / MSI base+index, `"Read from DTS later"` | `pcie_linux.c` L506-L527 | exact | yes |
| `PCIE_INTX_XFER_PENDING_SEL` PM code | `pcie_pm_mp17c.c` L157-L170 | exact | yes |
| `OAL_IRQ_NUM 5` | `oal_hardware.h` L16 | exact | yes |
| `st_irq_dev.irq = OAL_IRQ_NUM` | `frw_event_deploy.c` L286/L326 | exact | yes |

The report's central negative claim (the device INTID table is **not** in the tree) is consistent
with what the fetched files contain - the tree is host-side only and contains no `0x4016xxxx`
device-window references. Its verdict is scoped and clearly labelled as inference, which is
correct behaviour: it never claims to have derived `0x4C`.

---

## C. `reconcile-source-ids.md` - PASS

### C1. The four register calls (the report's central instruction-level claim)

The report states `pcie_msg_init` registers exactly four ids - `0x2d`, `0x2e`, `0x4c`, `0x4e` - at
file `0x96ca`, `0x96dc`, `0x9800`, `0x980c`, and that these are the *only* four `bl 0x874b0` in the
image. Both halves verified:

* `register` core takes **r0 as the id**: `0x874b4 mov r4, r0` (and `0x874c4 add.w r3,r5,r4,lsl #2`
  indexes the handler array by it), so `movs r0,#0x4c; bl #0x874b0` is a real `register(0x4c, ...)`.
* Whole-image sweep found exactly four `bl #0x874b0` calls, at the four offsets claimed.
* The four id/class/fn triples re-disassembled: `0x96b6/0x96ca` -> id `0x2d`; `0x96da/0x96dc` ->
  `0x2e` (pool `0x97c4` = `0x0004624d`); `0x97f6/0x97fe/0x9800` -> id `0x4c`, class `5` (`0x979e`),
  fn pool `0x9858` = `0x00040295`; `0x980a/0x980c` -> id `0x4e`, fn pool `0x985c` = `0x000402a1`.
* Each `bl` is followed by `mov r7/r4, r0` + `cbnz r0` - the routine aborts (to `0x984c`/`0x983c`)
  if any registration returns non-zero. That is a fail-fast on register's return value, not evidence
  that the call is anything other than `register(id, class, fn)`.

### C2. Enable and the rest of the chain

* `0x9814 movs r0,#0x4c` -> `0x9816 bl #0x86ff4` -> word 2 bit 12 of CA `0x40161108`; the sibling
  pair `0x981a/0x981c` enables `0x4e`. Verified against the enable body (`0x8701a`-`0x8702c`).
* The H2D dispatcher chain `0x294 -> 0x818ac` (ack `0x400392f0`=1, consume `out[0]` at
  `0x40039010`, re-arm doorbell 8 at `0x400392d4`) and the D2H mirror `0x2a0 -> 0x86108` are exactly
  as quoted (re-disassembled; see A3).
* The doorbell/ack address arithmetic at `0x9760`-`0x9776` re-computed: literal `0x40101434`,
  `-0xc8000` -> `0x40039434`, `-0x144` -> `0x400392f0` (ack), `-0x1c` -> `0x400392d4` (doorbell).
* The ETE-page claim for the `0x2d`/`0x2e` handlers: `0x62f8`/`0x624c` load `r5` from pool `0x6388`
  = `0x0010f1f0`, dereference `[r5+0xc]` = the word at file `0xcf1fc` = `0x4003a86c`, test bit 12
  (`lsls r2,r4,#0x13; bpl`) of `[r3,#8]` and clear it at `[r3,#4]` - the ETE block, not the ctrl-rb.
  Verified.
* Literal scans: `0x400392d4/e4/e8/ec/f0` **absent** from the image (0 hits); `0x40161100` exactly
  one hit at `0x87044`. Verified.

### C3. Verdict

Every instruction-level claim in `reconcile-source-ids.md` re-checked against the bytes and matched.
The only limits on it are the ones the report itself states (the id->line binding is an inference,
and `pcie_msg_init` has no direct caller). **PASS.**

### C4. Correction note

An earlier draft of this file marked this report FAIL on the belief that the `0x4c` in
`0x97f6 movs r0,#0x4c` was the call site's file offset rather than an interrupt id. That was the
verifier's error: `0x874b4 mov r4, r0` proves `register`'s first argument is the id, so the call is
`register(0x4c, 5, 0x40295)` exactly as the report says. No claim in the report is wrong.

---

## D. Reproduce

```
pyenv/Scripts/python.exe - <<'PY'
from capstone import *
import struct
d = open("build/tmp/FIRMWARE.bin","rb").read()
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB)
for off in (0x9800, 0x9816, 0x874b0, 0x86ff4, 0x294, 0x818ac, 0x6e5c, 0x6e7c, 0x6e80, 0x6e84):
    for i in md.disasm(d[off:off+4], off):
        print(hex(off), i.mnemonic, i.op_str); break
for a in (0x7158, 0x715c, 0x7160, 0x9858, 0x87044):
    print(hex(a), hex(struct.unpack_from("<I", d, a)[0]))
PY
```
