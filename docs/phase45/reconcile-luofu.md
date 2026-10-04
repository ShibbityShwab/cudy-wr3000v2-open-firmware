# Reconcile-luofu: the sibling mailbox-interrupt enables mapped onto the luofu message block (phase 45, 2026-10-04)

Task `st_01a10740`. Scope: static, read-only (FIRMWARE.bin, the two vendor modules, the phase31-44
record, and the two phase-45 sibling reports). No device access. Deliverable: which host-visible
register in the luofu message block (`CA 0x40039000-0x40039600`, BAR0 `0x3f1000-0x3f1600`) is the
siblings' mailbox-doorbell -> interrupt enable, and the value the port should try.

Sibling reports read: `phase45/hi1105-mailbox-irq.md` (PCIe tree, task `st_01a1073e`) and
`phase45/hi3881-mailbox-irq.md` (SDIO tree, task `st_01a1073f`). This file re-derived every sibling
offset and every luofu-side offset independently and re-disassembled each quoted instruction; the
two sibling reports agree with the independent derivation everywhere they overlap.

## 1. Bottom line

**The luofu counterpart of the sibling enable is `CA 0x400392e8` (BAR0 `0x3f12e8`)** - the register
the port calls "glue chn_res". It is the family's `PCIE_CTRL_RB_HOST_INTR_MASK`: bit 0 =
`host2device_tx_intr_mask` gates the H2D doorbell -> device-interrupt forwarding, bit 3 =
`device2host_rx_intr_mask` gates the reverse direction; **1 = masked, 0 = enabled**, applied by
read-modify-write. **The value to hold there is `0x20`** - bit 0 (and bit 3) cleared, bit 5 set -
which is the vendor's own live value (`reg_all.txt`: `400392e8, value = 20`).

The twist this reconciliation establishes with new evidence: **the port's existing write already
reaches exactly that value.** The takeover reset state of `0x400392e8` is `0x3ff` (the port's own
logs: `glue chn_res pre=0x000003ff`), and `0x3ff & 0xfffffc20 = 0x20` = the vendor's live value.
So on the EP0 block the enable is **not** the missing gate - which is itself a finding the record
can now use: the gate is between the ctrl-rb's interrupt event and line `0x4C`, and the sibling map
names two host-visible instruments that were never read (the raw/post-mask status pair) plus one
family enable register the port has never written (the twin PCIe1 block's mask).

| question | answer |
| --- | --- |
| sibling enable register | `HOST_INTR_MASK`, ctrl-rb offset `0x2E8`, bit 0 (`host2device_tx_intr_mask`), 0 = enable (hi1105 report §1-§3; independently re-derived) |
| its luofu counterpart | **`CA 0x400392e8`**, BAR0 **`0x3f12e8`** |
| value to try (top) | **`0x20`** (bit 0 = 0, bit 3 = 0, bit 5 = 1 - the vendor's exact live value), re-asserted at message-service time |
| second candidate | `CA 0x40039ae8` (twin PCIe1 block mask), BAR0 `0x3f1ae8`, value `old & ~0x9` (= `0x3f6` from reset) - the only family enable register the port has never written |
| what success changes | after a doorbell write, raw status `0x400392e4` bit 0 latches 1 (never measured before); then the dispatcher signature: ack `0x400392f0` pulses 1, out[0] clears, doorbell re-arms to 8 |

## 2. What the two siblings found (one paragraph each)

**hi1105 (PCIe, the layout the luofu follows).** The arm register is `PCIE_CTRL_RB_HOST_INTR_MASK_OFF = 0x2E8`,
bit 0 `host2device_tx_intr_mask`, written 0 to enable (`pcie_chip_mp17c.c` `oal_frw_msg_int_unmask_mp17c`,
L466-483; polarity stated in code as `/* mask:1 for mask, 0 for unmask */`). The doorbell is a separate
register `HOST2DEVICE_INTR_SET_OFF = 0x2D4`: bit 0 = host->device tx ring (`oal_pcie_h2d_int`,
`ete_host.c` L1202-1216), bit 3 = device->host rx ring (`oal_pcie_d2h_int`, L1219-1234). The unmask is
applied when the message service starts (`oal_firmware_msg_download_pre`, `pcie_firmware_msg.c` L486-515),
and the post-mask status `0x2EC` / clear `0x2F0` complete the quartet (`pcie_ctrl_rb_regs.h` L334/L402/
L425/L442).

**hi3881 (SDIO, the structural principle).** The enable is `HISDIO_REG_FUNC1_INT_ENABLE` (function-register
offset `0x09`), armed with the source-class mask `0x07` (DREADY|RERROR|MFARM); there is no per-message-id
enable and the doorbell (a 32-bit message bitmap write to `WRITE_MSG` at `0x24`) is separate from the
enable byte (`oal_sdio.h:39-55`, `oal_sdio_host.c:1099-1118`). Arm order: clear stale status -> write
enable -> open state. Its luofu candidate (`0x40039008/0x40039009`) is **resolved against** in §5.

## 3. Which sibling layout the luofu follows (the discriminating evidence)

The two siblings give different *shapes* (SDIO: status byte at `0x08`, enable byte at `0x09`; PCIe:
doorbell `0x2D4`, raw `0x2E4`, mask `0x2E8`, status `0x2EC`, clr `0x2F0`). The luofu provably follows the
**PCIe ctrl-rb layout**, by four independent luofu-side facts:

1. **Doorbell bit values.** The luofu host rings the doorbell with 1 and the firmware re-arms it with 8;
   these are exactly bit 0 (`host2device_tx_intr_set`) and bit 3 (`device2host_rx_intr_set`) of the
   sibling's `0x2D4` register. Firmware re-arm quote: `0x818c4: str r1, [r2]` with `movs r1, #8` at
   `0x818c0`, `r2 = ctx+0x10 = 0x400392d4` (ctx init `0x9776: str.w r1, [r5, #0xe0]`, r1 = ack-0x1c).
2. **The ack is the interrupt-clear.** The firmware's dispatcher "ack" writes 1 to `0x400392f0` -
   `0x818b8: str r7, [r2]` with `movs r7, #1` at `0x818b2`, `r2 = ctx+0x0c = 0x400392f0` (`0x9770`). In
   the sibling that register is `HOST_INTR_CLR` and its bit 0 is literally named
   `host2device_tx_intr_clr`. The record's "device ack register" is the interrupt-clear of the H2D
   event - the firmware is not acking a protocol message, it is ending its interrupt.
3. **The remaining known registers all sit at the sibling's ctrl-rb offsets.** State `0x40039000`
   (live `0x10b`) = the block's id/status word (`PCIE_CTRL_SYS_CTL_ID` in the sibling; the sibling's
   alive-check reads the same word and compares against `0x101`); out0/out1 `0x40039010/14` = the
   ctrl-rb `GP_REG0/GP_REG1` (`pcie_ctrl_rb_regs.h` L32-45); `0x40039224` (live `0x2008a236`) =
   `PCIE_STATUS0` (`0x224`, mp16 L235); `0x40039508` = `PCIE_CTL_ETE_INTR_MASK` (`0x508`, mp16 L1879,
   with CLR `0x50c` / STS `0x510` / RAW `0x514` next to it); the firmware's boot helpers drive the
   PHY-config port `+0x41c` (addr) / `+0x428` (ctrl) / `+0x424` (rd-data) = the sibling's
   `PHY_CFG_ADDR` / `PHY_CFG1` / `PHY_RD_DATA` (mp16 L1534/L1574/L1558) - see §7 for the helper quotes.
4. **The twin block is the second endpoint's ctrl-rb.** The firmware's boot path runs the same two
   init helpers on **two** bases: `0x86f3e/0x86f44` with base `0x40039000` and `0x86f4a/0x86f50` with
   base `0x40039800` (literals `0x86fb4`/`0x86fb8`). The live dump shows `0x40039800` carrying the same
   file: state `0x10c` at +0, status `0x2000a236` at `+0x224`, cfg `5` at `+0x2d0`, doorbell `0` at
   `+0x2d4`, raw `0` at `+0x2e4`, **mask `0x3ff` at `+0x2e8`**, status `0` at `+0x2ec`, clr `0` at
   `+0x2f0`, ETE intr mask `0x3f3f1f1f` at `+0x508` (phase7 named this window "PCIe1 glue/L1SS").

Conclusion: the luofu message block *is* the family's PCIe ctrl-rb at its own base (`0x40039000`, the
per-chip base pattern: mp16 = `0x40108000`, mp17c = `0x04980000`, mp12 = `0x04903000` - every chip
keeps the internal offsets). The SDIO `0x08/0x09` pair does not map byte-for-byte onto this block; its
contribution is the *principle* (a separate, source-class enable register next to the status word),
which the PCIe layout already satisfies at `0x2e4/0x2e8/0x2ec/0x2f0`.

## 4. The sibling-to-luofu register map (structure, not guesses)

Every row is grounded in a luofu-side reference (firmware instruction / vendor-module instruction /
live dump) named in the rightmost columns; `§7` is the verification ledger for those offsets.

| sibling ctrl-rb register (offset) | luofu CA (base `0x40039000`) | luofu BAR0 | role on the luofu | grounding |
| --- | --- | --- | --- | --- |
| `SYS_CTL_ID` (`0x000`) | `0x40039000` | `0x3f1000` | block state; live `0x10b` = normal op | `reg_all.txt` |
| `GP_REG0` (`0x010`) | `0x40039010` | `0x3f1010` | out[0] (H2D pending word) | firmware literal `0x97e0`, ctx init `0x9784`; vendor reg_map `plat.ko 0x1b1b0` |
| `GP_REG1` (`0x014`) | `0x40039014` | `0x3f1014` | out[1] (D2H pending word) | firmware literals `0x97dc`/`0x86fbc`, ctx init `0x9784`; vendor reg_map `0x1b1e8` |
| `STATUS0` (`0x224`) | `0x40039224` | `0x3f1224` | per-endpoint PCIe status word, live `0x2008a236` | firmware literal `0x86c68`; `reg_all.txt` |
| `PCIE_CFG` (`0x2d0`) | `0x400392d0` | `0x3f12d0` | cfg latches, live `5` | `reg_all.txt`; vendor `0x1ac30/0x1af34` movw/movt |
| `HOST2DEVICE_INTR_SET` (`0x2d4`) | `0x400392d4` | `0x3f12d4` | **the doorbell**; bit 0 = H2D (host writes 1), bit 3 = D2H re-arm (firmware writes 8) | firmware `0x9776`/`0x818c4`; vendor reg_map `0x1b20c`, `pcie_msg_send_irq` |
| `HOST_INTR_RAW_STATUS` (`0x2e4`) | `0x400392e4` | `0x3f12e4` | pre-mask raw event latch; live `0`; **never read by the port** | offset identity; `reg_all.txt` |
| **`HOST_INTR_MASK` (`0x2e8`)** | **`0x400392e8`** | **`0x3f12e8`** | **the enable**; bit 0 `host2device_tx_intr_mask`, bit 3 `device2host_rx_intr_mask`, bit 5 `pcie_msg_irq_mask` (mp12/mp16/mp16c; reserved on mp17c); 1 = masked, 0 = enabled | vendor `pcie_ete_chn_res` `0x074fc/0x07504/0x07520`; port `wifidrv1.c:561-564`; `reg_all.txt` = `0x20` |
| `HOST_INTR_STATUS` (`0x2ec`) | `0x400392ec` | `0x3f12ec` | post-mask status the ISR reads; live `0`; **never read by the port** | offset identity; `reg_all.txt` |
| `HOST_INTR_CLR` (`0x2f0`) | `0x400392f0` | `0x3f12f0` | write-1-to-clear; the firmware's "ack" writes 1 = `host2device_tx_intr_clr`; forbidden to the host | firmware `0x9770`/`0x818b8` |
| `PHY_CFG_ADDR/WR/RD/CFG1` (`0x41c/0x420/0x424/0x428`) | `0x4003941c/20/24/28` | `0x3f141c/20/24/28` | PHY config port driven by firmware boot helpers; live PHY words `0x40039400-0x418` populated | firmware helper `0x7dc/0x7e2/0x7e6/0x7ea`; `reg_all.txt` |
| `ETE_INTR_MASK/CLR/STS/RAW` (`0x508/0x50c/0x510/0x514`) | `0x40039508/0c/10/14` | `0x3f1508/0c/10/14` | ETE interrupt mask (vendor writes `& 0xffe0f8f8` -> live `0x3f201818`) / clr / sts / raw (live `0x09001000`) | vendor `pcie_ete_intr_init` `0x075ac-0x075b8`; firmware literal `0xccee0`; `reg_all.txt` |

Twin block (same layout, base `0x40039800`, the "PCIe1" ctrl-rb; host BAR0 `0x3f1800+`, inside the
port's existing `OMO_IO_WIN` window - no new mapping needed):

| twin CA | BAR0 | live vendor value | who writes it |
| --- | --- | --- | --- |
| `0x40039ad4` (doorbell) | `0x3f1ad4` | `0` | nobody (firmware uses EP0's doorbell) |
| `0x40039ae4` (raw) / `0x40039aec` (status) | `0x3f1ae4` / `0x3f1aec` | `0` / `0` | - |
| **`0x40039ae8` (mask)** | **`0x3f1ae8`** | **`0x3ff` (reset; never written by vendor or port)** | - |
| `0x40039d08` (ETE intr mask) | `0x3f1d08` | `0x3f3f1f1f` (reset; vendor's EP0 pre-write value) | - |

## 5. The enable counterpart and its state in the two boots

**Identity.** `0x400392e8` = the sibling's `HOST_INTR_MASK`. The port's name for it ("glue chn_res")
came from `pcie_ete_chn_res` (`plat.ko .text 0x07490`), the vendor function that reads
`[base+0x2e8]`, ANDs with `0xfffffc20` and writes back (`0x074fc ldr r8,[r3,#0x2e8]` / `0x07504 and
r8,r8,sb` with `sb=0xfffffc20` from `0x074a4/0x074a8` / `0x07520 str r8,[r2,#0x2e8]`). Note the vendor's
reg-map struct (`shuangta_pcie_msg_reg_map`, `0x1b1a0`) maps only
{out0, out1, doorbell, 0x40101438, 0x40101414, ack} - the mask is written by the chn_res path, not the
message path.

**State comparison (new, from the port's own logs + the live dump):**

| | EP0 mask `0x400392e8` | EP0 ETE mask `0x40039508` | twin mask `0x40039ae8` |
| --- | --- | --- | --- |
| vendor live boot (`reg_all.txt`) | `0x20` | `0x3f201818` | `0x3ff` |
| takeover reset (port dmesg, `exp/20261003-*`..`20261004-*`) | `0x3ff` | `0x3f3f1f1f` | (never read) |
| port's post-write value | `0x3ff & 0xfffffc20 = 0x20` | `0x3f3f1f1f & 0xffe0f8f8 = 0x3f201818` | untouched |

So the port's existing binding writes already reproduce the vendor's live values **exactly** on the
EP0 block. Under the sibling polarity that means bit 0 (H2D) is unmasked in both boots, and the
missing gate is downstream of the ctrl-rb (delivery to line `0x4C` - the phase31-35 ledger's known
unreachable block) or in the TLP path. The mask register is therefore *named and cleared* rather
than *missing* - the phase-31 hypothesis "the vendor's interrupt-block init is missing" is not
resurrected by the mask, but the reconciliation does open the instruments in §6.

**Resolution of the hi3881 report's candidate** (`0x40039008/0x40039009` as an SDIO-shaped
status/enable byte pair): the live dump shows `0x40039008 = 0`, `0x4003900c = 0`, and the sibling
PCIe headers place no enable at `+0x108` (that offset is `PCIE_CTRL_RB_PCIE_MEM_SP_RAM_TMODE`, mp16
L80 - a memory-BIST mode word; live `0x40039108 = 0x1a`). The SDIO block is a different transport's
register file; the luofu follows the PCIe one, where the enable lives at `+0x2e8`. The SDIO report's
principle holds: the enable is a separate source-class register, adjacent to the status word - on
the luofu that adjacent pair is `0x400392e4` (raw) / `0x400392e8` (mask) / `0x400392ec` (status),
not a byte at `0x08/0x09`.

## 6. What the port should try (candidates, values, observables)

### 6.1 Top candidate: `CA 0x400392e8` = `0x20`, re-asserted at message-service time

The sibling's unmask is applied **when the message service starts** (`oal_firmware_msg_download_pre`
-> `oal_pcie_firmware_msg_int_unmask`), not at probe. The port's only write is pre-release, inside
`omo_ete_program`. The sibling-faithful placement is therefore: after the release write and after the
firmware's boot init (the helpers at `0x86f3e/0x86f44/0x86f4a/0x86f50`), immediately before the first
doorbell:

```
v = omo_rd(omo_msg, OMO_CHN_RES);            /* 0x400392e8, expect 0x20 already */
omo_wr(omo_msg, OMO_CHN_RES, v & ~0x9u, "H2D/D2H doorbell intr unmask (bits 0,3)");
omo_wr(omo_msg, 0x2e8, 0x20, "host intr mask = vendor live 0x20 (plain form)");  /* optional readback pin */
```

Value forms, in preference order:
1. **`0x20` plain** - the vendor's exact live value (bit 0 = 0 enable H2D, bit 3 = 0 enable D2H,
   bit 5 = 1 as in the vendor's state; bits 10+ are 0 in the vendor's live word too).
2. `v & ~0x9` - the sibling's minimal unmask (clears exactly `host2device_tx` + `device2host_rx`),
   preserving everything else; from the takeover reset `0x3ff` this yields `0x3f6`.
3. The port's existing `v & 0xfffffc20` - already lands on `0x20`; keep it, it is the vendor's own
   chn_res mask.

Expected: the register already reads `0x20` (the pre-release write held - nothing in the firmware
writes `+0x2e8`: the image contains no literal for it, the ctx has no `+0x2e8` field, and the boot
helpers only touch `+0x41c/+0x424/+0x428`). If it does not read `0x20`, that is itself the finding -
something between the port's write and the doorbell re-armed the mask.

**Never** write the adjacent `0x400392f0` (the intr-clear; the record's forbidden ack register - the
sibling arm order's "clear stale status" step is the device's own write, `0x818b8`).

### 6.2 Second candidate: the twin block's mask `CA 0x40039ae8` = `old & ~0x9` (BAR0 `0x3f1ae8`)

The firmware initialises **both** ctrl-rbs at boot (helpers called with base `0x40039800`), and the
twin's mask sits at the family reset `0x3ff` in the vendor boot itself. It is the only family enable
register in the host-visible window the port has never written:

```
v = omo_rd(omo_msg, 0x1ae8);                 /* 0x40039ae8 - twin (PCIe1) host intr mask */
omo_wr(omo_msg, 0x1ae8, v & ~0x9u, "twin doorbell intr unmask (bits 0,3)");   /* 0x3ff -> 0x3f6 */
```

**This one is a labelled guess.** Two conflicting facts bound it: the vendor's boot works while
leaving the twin at `0x3ff` (so under the sibling polarity the twin's H2D bit being 1/masked does
not block the vendor - consistent with the record's verified claim that both endpoint windows decode
the same doorbell register `0x400392d4`), and yet the port's only untried enable is this one. If the
alias holds, the write is a no-op; if any part of the port's TLP path is instead decoded through the
twin's file, clearing its bit 0 is exactly the arm the sibling defines. Use the minimal `& ~0x9`
form so the twin's mac/phy masks (which the vendor deliberately leaves set) are not disturbed.

### 6.3 What a successful write changes (the observables)

In order of how much they prove:

1. **Raw status `0x400392e4` (BAR0 `0x3f12e4`) bit 0 latches 1 after a doorbell write.** Never
   measured on the luofu. Sibling semantics: the doorbell write sets the raw event bit before any
   masking; it stays until the firmware's clear (`0x818b8` -> `+0x2f0`). This is the first host-side
   instrument that separates "doorbell consumed by the mailbox" from "interrupt event raised in the
   ctrl-rb" - it splits the phase31-35 gate in half without any new window.
2. **Post-mask status `0x400392ec` (BAR0 `0x3f12ec`) bit 0 = 1** for the same event when the mask bit
   0 is 0 - confirming the mask polarity reading in §5 on the live part.
3. **The dispatcher signature** (the record's canonical proof, `phase32/THE-GATE-MAP.md` §1): ack
   `0x400392f0` pulses 1 (firmware `0x818b8`), out[0] `0x40039010` clears (`0x818be`), doorbell
   `0x400392d4` re-arms with 8 (`0x818c4`), then D2H announce bits land in out[1] `0x40039014` - and
   downstream, DR deposits appear (`omo_dr_watch` events > 0).

If observation 1 stays 0 after a doorbell that reads back consumed, the event never reaches the
ctrl-rb interrupt stage at all and no mask value can open it - the gate is the TLP/decode path, not
the enable.

## 7. Verification ledger (every luofu-side offset re-disassembled this task)

FIRMWARE.bin md5 `0e530b976d5a20e87358671f1a577695`; runtime = file + `0x40000`. plat.ko md5
`23660bc285393e678d5cade1c36c194b`, file = `.text` + `0x38`. All rows re-derived with capstone 5.0.7
against the files themselves.

| offset | claim | verified disassembly |
| --- | --- | --- |
| `0x86f32` | loads the first helper base | `ldr r0, [pc, #0x80]` -> literal `0x86fb4 = 0x40039000` |
| `0x86f3e` | first mailbox-init call | `bl #0x407c0` (runtime; file `0x7c0`, helper 0x7c0, base `0x40039000`) |
| `0x86f42` / `0x86f44` | second call | `ldr r0, [pc, #0x70]` -> `0x86fb4 = 0x40039000`; `bl #0x4078e` (runtime; file `0x78e`, helper 0x78e) |
| `0x86f48` / `0x86f4a` | third call | `ldr r0, [pc, #0x6c]` -> `0x86fb8 = 0x40039800`; `bl #0x407c0` |
| `0x86f4e` / `0x86f50` | fourth call | `ldr r0, [pc, #0x68]` -> `0x86fb8 = 0x40039800`; `bl #0x4078e` |
| `0x7c0` (helper 0x7c0) | PHY-config write loop, 22 words via `+0x41c/+0x428/+0x424` | `0x7dc str.w r3,[r5,#0x41c]`; `0x7e2 str.w r7,[r5,#0x428]` (r7=1); `0x7e6 str.w r6,[r5,#0x428]` (r6=idx); `0x7ea ldr.w r3,[r5,#0x424]`; `0x7f2 cmp r4,#0x16`; source = literal `0x7fc = 0x00103ba0` (runtime; initial content at file `0xc3ba0`, 22 words, 0x58-byte copy) |
| `0x78e` (helper 0x78e) | status verify loop, 10 reads | `0x7aa ldr.w r2,[sp,r3,lsl #2]`; `0x7b2 ldr r2,[r6,r2]`; `0x7b0 cmp r3,#0xa`; table = literal `0x7bc = 0x00103b78` (runtime; initial content at file `0xc3b78`: `{0x224,0x23c,0x414,0x43c,0x440,0x460,0x464,0x468,0x46c,0x514}`) |
| `0x9770` / `0x9776` | ctx ack / doorbell fields | `str.w r1,[r5,#0xdc]` (r1 = `0x40101434-0xc8144 = 0x400392f0`); `str.w r1,[r5,#0xe0]` (r1-0x1c = `0x400392d4`) |
| `0x9784` | ctx out words | `strd r2,r3,[r5,#0xd0]` (r2 = literal `0x97dc = 0x40039014`, r3 = `0x97e0 = 0x40039010`) |
| `0x818b8` | dispatcher ack | `str r7,[r2]`, r7=1 (`0x818b2`), r2 = ctx+0x0c = `0x400392f0` |
| `0x818c4` | dispatcher doorbell re-arm | `str r1,[r2]`, r1=8 (`0x818c0`), r2 = ctx+0x10 = `0x400392d4` |
| image literal scan | mask/status never firmware-referenced | no little-endian literal for `0x400392e4/0x400392e8/0x400392ec/0x400392f0/0x400392d4` anywhere in the image (doorbell/ack are computed: `0x9776`); literals present: `0x40039000` (0x86fb4/0xcf2ac), `0x40039010` (0x97e0), `0x40039014` (0x97dc/0x86fbc), `0x40039224` (0x86c68), `0x40039508` (0xccee0) |
| plat.ko `0x074a4/0x074a8` | chn_res mask constant | `movw sb,#0xfc20`; `movt sb,#0xffff` |
| plat.ko `0x074fc/0x07504/0x07520` | vendor mask RMW at `+0x2e8` | `ldr r8,[r3,#0x2e8]`; `and r8,r8,sb`; `str r8,[r2,#0x2e8]` (pcie_ete_chn_res, symbol `0x07490` sz 0x98) |
| plat.ko `0x075ac-0x075b8` | ETE intr mask RMW | `movw r2,#0xf8f8`; `movt r2,#0xffe0`; `and r2,r2,r1`; `str r2,[r3]` (pcie_ete_intr_init, symbol `0x07528`; target CA `0x40039508` = rodata file `0x348a8`) |
| plat.ko `0x1b1b0/0x1b1e8/0x1b20c/0x1b278` | vendor mapped regs | movw/movt pairs -> `0x40039010`, `0x40039014`, `0x400392d4`, `0x400392f0` (+ `0x40101438`/`0x40101414`); **no `0x400392e8` in the mapped set** - the mask is chn_res's, not the message path's |
| `reg_all.txt` | vendor live values | `400392e8 = 20`, `400392e4 = 0`, `400392ec = 0`, `400392f0 = 0`, `400392d4 = 0`, `40039508 = 3f201818`, `40039000 = 10b`; twin `40039ae8 = 3ff`, `40039ad4 = 0`, `40039d08 = 3f3f1f1f`, `40039800 = 10c` |
| port dmesg (`build/register-dumps/exp/20261004-*`) | takeover reset of the mask | `glue chn_res pre=0x000003ff` (20261003+; the 20261002 runs read `0x00000000`), `intr pre=0x3f3f1f1f` |

## 8. What is established vs what is a guess

**Established (this task, from the firmware/modules/dumps or the sibling sources):**
- the sibling enable register, its offset `0x2e8`, bit 0/bit 3, and the `1 = masked, 0 = enabled`
  polarity (`hi1105-mailbox-irq.md` §1-§3; independently re-derived from `pcie_chip_mp17c.c` and
  `ete_host.c`);
- the luofu block's identity as the family ctrl-rb at base `0x40039000` (doorbell bits 1/8, ack
  write 1, GP/status/ETE-mask/PHY offsets, dual blocks);
- the state comparison in §5: the port already holds the vendor's exact live mask values on EP0;
- the new host-visible instruments `0x400392e4`/`0x400392ec` (offset identity from the sibling
  headers; their luofu-side values from `reg_all.txt`).

**Guesses (explicit):**
- that the luofu's `0x400392e8` bit 0 gates exactly the sibling's `host2device_tx` event (the
  offset/bits are established; the per-bit wiring on the luofu is inferred from the doorbell/ack bit
  values - a very strong but not proven match);
- that clearing the twin mask `0x40039ae8` bits 0/3 can matter for the port's path at all (the
  record's verified window-alias claim implies it is a no-op; it is offered as the only untried
  family enable register, minimal form only);
- that the raw status latches are host-readable and persistent until the firmware's clear (sibling
  semantics, unmeasured on the luofu).

**Not re-derived:** the gate ledger itself (phases 31-35: doorbell consumed, line `0x4C` silent,
interrupt block PCIe-unreachable), the window/alias claim of `phase32/doorbell-endpoint-path.md`,
and the anti-patterns (never write `0x400392f0`; never read the RC misc window).
