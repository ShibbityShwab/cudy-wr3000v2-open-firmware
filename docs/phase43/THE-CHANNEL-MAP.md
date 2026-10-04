# THE-CHANNEL-MAP: the deposit-activation recipe (phase 43, 2026-10-04)

Merge of the four verified phase-43 lane reports into one recipe. Merge task `st_01a10734` (parent
`01a0fc5c`). All four source lanes passed independent verification (`VERIFICATION.md`, task
`st_01a10731`, 491/491 checks) before this merge; **no new measurement was taken here** - every
claim below is quoted from a lane report, and the lane attribution sits at the end of each section.

| lane report | what it settles | verdict |
| --- | --- | --- |
| `channel-array-map.md` (task `st_01a10725`) | what the 20-word `0x4004xxxx`/`0x4006xxxx` CA array is, and why it is not the ETE ring file | PASS |
| `channel-register-offsets.md` (task `st_01a10726`) | what one `0x4004a000`-style block's words mean (CSI block), and where the real ring registers live | PASS |
| `vendor-port-dr-diff.md` (task `st_01a10727`) | the register-by-register vendor-vs-port diff on the DR path, and the ranked candidates | PASS |
| `device-dr-side.md` (task `st_01a10728`) | the device-side precondition the firmware enforces before it publishes a D2H word | PASS |

Where two lanes overlap the earlier reading in the record is corrected: the "12-entry channel-base
array" (`channel-array-map.md` §0.1, `channel-register-offsets.md` §1) and the
"`0x4004a004` = DR ring base / `+0x08` = an index" note (`vendor-port-dr-diff.md` §8.1,
`channel-register-offsets.md` §6.1). Both corrections are load-bearing for the experiment below.

---

## 1. The named channel array and the per-channel register map

**Attribution: `channel-array-map.md` §1-§4, `channel-register-offsets.md` §1-§5.**

### 1.1 The array is two 10-word banks, not 12 channels

`hi5622v100_wifi.ko` md5 `4737fcb21a1a2262a96f84d780ad8b35`, `.rodata+0xb60` (file `0x168660`)
holds **20 consecutive words = two 10-word banks**; band 0 = 2g (`.rodata+0xb60..0xb87`), band 1 =
5g (`.rodata+0xb88..0xbaf`), with `bank(5g)[k] = bank(2g)[k] + 0x20000` for every `k = 0..9`
(verified word-by-word). The record's "12 values" are the first 12 words of that 20-word run; the
"second copy at `0xbc0`" is the 5th word of a *different* table (the sorted 2g dump list at
`0xbb0`). Consumed only by `shuangta_host_initialize_machw` @`.text 0x3491c` (`0x034968 ldr
ip,[pc,#0x16c]`, literal `0x034adc` = `0xb60`, `R_ARM_ABS32`), which copies the selected 10-word
bank to the stack, converts each CA with `oal_pcie_devca_to_hostva` (helper @`.text 0x33fcc`) and
stores the 10 host VAs at `[chip+0x14c]` (`0x034960 str r0,[r6,#0x14c]`). The band selector is
`[chip+2]`: 0 -> words 0..9 (2g), 1 -> words 10..19 (5g). **Array slot k = CA = table word k of the
selected band's bank**; every consumer uses `base = [chip+0x14c]; block = base[slot]`.

| slot | CA, band 0 (2g) | CA, band 1 (5g) | role (vendor symbol / dump) |
| ---: | --- | --- | --- |
| 0 | `0x40042000` | `0x40062000` | MAC test-mode / TX-FCS + PSDU error-injection block (`+0x924` inj word, `+0xa8` test-mode bits) |
| 1 | `0x40044000` | `0x40064000` | `[unknown]` - device-RAM buffer bases `0x0106xxxx`/`0x0206xxxx` |
| 2 | `0x40046000` | `0x40066000` | `[unknown]` - nearly empty |
| 3 | `0x40048000` | `0x40068000` | CCA/nav-bypass + TXBF HT-matrix buffer (`+0x10/+0x14/+0x18`, bypass `+0x618`) |
| 4 | `0x4004a000` | `0x4006a000` | **MAC CSI block** - `+0x00` param, `+0x04` buffer addr, `+0x08` buffer size, `+0x0c..` whitelist |
| 5 | `0x40040000` | `0x40060000` | **MAC host-interface / ring block** - RX rings `+0x20..+0x54`, `tx_ba_info` `+0x00/+0x04`, msdu ptr table `+0x10`, int status `+0x44`, int mask `+0x48` |
| 6 | `0x4004c000` | `0x4006c000` | `[unknown]` - head/tail-shaped words |
| 7 | `0x40052000` | `0x40072000` | block whose `+0x2ac` holds a *device address*; otherwise `[unknown]` |
| 8 | `0x40050000` | `0x40070000` | host-MAC interrupt *status* block (`+0x00` read) |
| 9 | `0x40054000` | `0x40074000` | `[unknown]` - design MAC address replicated at `+0x140..+0x170` |

Slots 1, 2, 6, 9 have no host-side consumer in either module (whole-`.text` scan,
`channel-array-map.md` §6.3). The dump labels ("2g MAC" / "5g MAC") are wifi.ko's own strings
(`Reading 2g_mac_register!` file `0x1a7b54`), and `reg_all.txt`'s captured run lengths reproduce the
module's own window-length table at `.rodata+0xbd8` byte-for-byte, which fixes the block boundaries.

### 1.2 The real ring registers live in slot 5, not in slot 4

`channel-register-offsets.md` §5: word 5 (CA `0x40040000` / `0x40060000`) is the block that carries
base / depth / wptr / rptr. The port's own "channel 5" framing is right about *where* the rings are
and wrong about *which block* - `0x4004a000` (slot 4) is the CSI block, not a ring.

| offset | field | writer |
| --- | --- | --- |
| `+0x00` | `tx_ba_info_buff_depth` | `hal_set_tx_ba_info_buf_depth_cfg_...` |
| `+0x04` | `tx_ba_info_buf_addr` (device VA of a host buffer) | `hal_set_tx_ba_info_buf_addr_cfg_...` |
| `+0x10` | `msdu_info_ring_ptr_table_base` | `hal_set_msdu_info_ring_ptr_table_base_cfg_...` |
| `+0x1c` | `rx_norm_buff_len` [15:0] \| `rx_small_buff_len` [31:16] | `hal_set_rx_data_buff_len_cfg_*` |
| `+0x20` / `+0x24` / `+0x28` | `rx_norm_data_free_ring_addr` / `rx_small_data_free_ring_addr` / `rx_data_cmp_ring_addr` | `..._cfg_*_addr` (`pcie_if_hostca_to_devva` output) |
| `+0x2c` / `+0x30` / `+0x34` | the three ring **sizes** (bits 11:0) | `..._cfg_*_size` |
| `+0x38` / `+0x3c` / `+0x40` / `+0x4c` / `+0x50` / `+0x54` | the six wptr / rptr words (index [14:0] + phase bit 15, RMW) | `..._cfg_*_wptr` / `_rptr` |
| `+0x44` | host MAC interrupt **status** (write 1 to clear) | `shuangta_clear_host_mac_int_status` |
| `+0x48` | host MAC interrupt **mask** | `shuangta_host_mac_irq_mask` / `_unmask`, `hal_set_host_intr_mask_cfg_...` |

Cross-check that pins the mapping: `.rodata+0xbd8` = the ten window lengths
`0xb0, 0x92c, 0xac0, 0x1000, 0xe50, 0xe60, 0x924, 0x44, 0x2d4, 0x190` (dump matches), and the
`.rodata+0xbb0`/`0xc00` sorted lists are the same ten CAs as the mapping table in a different
order. The live `0x40040048 = 0xfffffcc1` equals the module's own `movw #0xfcc1 / movt #0xffff`
(`0x3a7bc`) - a one-word confirmation that slot 5 is CA `0x40040000`.

### 1.3 Where the ETE channels actually are

`channel-array-map.md` §4.2: the port's ETE block is **device CA `0x4003a000`** (block id `0x10a`,
`reg_all` line 1844), a *different hardware block* inside the same inbound region-3 window
(`host 0x403b8000 <-> dev CA 0x40000000`, size `0x120000`; `wifidrv1.c:88-95`). Its register file is
**7 channels of `0x50` bytes - not 12**: SR0..SR2 at `+0x400/+0x450/+0x4a0` and DR0..DR3 at
`+0x590/+0x5e0/+0x630/+0x680` (**no DR4**), which is why the port numbers DR0..DR3 as "ch3..ch6"
(`wifidrv1.c:539`, `i + 3`). The separation is complete: **plat.ko contains 0 words in
`0x4004xxxx..0x4007ffff`, and wifi.ko contains 0 words in `0x4003xxxx`.**

Per-channel register map inside an ETE channel block (both families, from
`pcie_ete_sr_reg_init` `+0x14a48` / `pcie_ete_dr_reg_init` `+0x1483c` and the port's constants):

| offset | field |
| --- | --- |
| `+0x00` | per-channel **enable** (bit 0) - set to 1 by all parties |
| `+0x08` | control nibble (`cfg[5]`, low 3 bits) |
| `+0x10` `+0x14` `+0x18` `+0x1c` | program group A: base / depth-1 / producer / consumer |
| `+0x28` | resource word (reads `0xffff` live; writer unidentified) |
| `+0x30` `+0x34` `+0x38` `+0x3c` | program group B: base / depth-1 / producer / consumer |
| `+0x48` | second enable (reads `1` live on all 7) |

Each channel carries **two** program groups - a host-DRAM one and a device-RAM (`0x0106xxxx`) one,
with the same depth and the same indices. Which group is the host ring differs by family:

| family | blocks | group A (`+0x10`) | group B (`+0x30`) |
| --- | --- | --- | --- |
| SR (host -> device) | `0x4003a400` `0x450` `0x4a0` | **host DRAM** ring (live `0x844d9000..0x844db000`) | device RAM ring (`0x01060550..0x01060750`) |
| DR (device -> host) | `0x4003a590` `0x5e0` `0x630` `0x680` | device RAM ring (`0x01060110..0x01060440`) | **host DRAM** ring (live `0x844d5000..0x844d8000`) |

The host driver programs the **host** groups (`pcie_ete_sr_reg_init` -> SR `+0x10`;
`pcie_ete_dr_reg_init` -> DR `+0x30`); the device firmware programs the **device-local** groups
(`device-dr-side.md` §3). `reg_all.txt` confirms the live layout: `4003a410 = 844db000`,
`4003a460 = 844da000`, `4003a4b0 = 844d9000`, `4003a5c0 = 844d8000`, `4003a610 = 844d7000`,
`4003a660 = 844d6000`, `4003a6b0 = 844d5000`, and `+0x00 = 1` on all seven blocks
(`4003a400/450/4a0/590/5e0/630/680`, lines 2100/2220/2240/2260).

---

## 2. The vendor-vs-port diff, and the top candidate register

**Attribution: `vendor-port-dr-diff.md` §1-§6 (with `device-dr-side.md` §3 as its device-side
counterpart).**

### 2.1 What the port programs today

`omo_ete_program` (`wifidrv1.c:507`) writes, and nothing else on the DR path does:

| site | register | value |
| --- | --- | --- |
| `wifidrv1.c:517` | ETE intr block `0x508` (CA `0x40039508`) | `old & 0xffe0f8f8` |
| `wifidrv1.c:525/527/529/531` | SR chN `+0x10` / `+0x14` / `+0x18` / `+0x08` | `devva` / `31` / `0` / `0` |
| `wifidrv1.c:540/542/544` | **DR chN `+0x30` / `+0x34` / `+0x38`** | `devva` / `31` / `0` |
| `wifidrv1.c:550` | glue `0x2e8` (CA `0x400392e8`) | `old & 0xfffffc20` |
| `wifidrv1.c:1705` | DR chN `+0x38` (producer commit) | the committed lap index |

DR block list `wifidrv1.c:363` `{ 0x590, 0x5e0, 0x630, 0x680 }`; DR field constants
`wifidrv1.c:189-192` `ETE_DR_BASEREG/DEPTH/WPTR/RPTR = 0x030/0x034/0x038/0x03c`. The port's two
enable constants are **SR-named only** (`wifidrv1.c:186-187` `OMO_SR_EN0 0x000`, `OMO_SR_EN1 0x048`)
and are written only in `omo_sr_post` (`wifidrv1.c:1649`, `:1653`). A full enumeration of every
`iowrite32` / `omo_wr` in the file confirms: **there is no write of `b + 0x00` and no write of
`b + 0x48` for any DR block, anywhere in the port.**

### 2.2 The host-driver diff is empty

`pcie_ete_dr_reg_init` @plat.ko `0x1483c` writes exactly `DR+0x30`, `DR+0x34`, `DR+0x38`
(`0x014888`, `0x0148a8`, `0x0148b4`, all quoted); the SR counterpart `0x14a48` writes
`SR+0x10/+0x14/+0x18/+0x08`. `pcie_ete_init_dst_ring` walks channels 3..6 only. **The vendor host
driver's DR register set is `{+0x30, +0x34, +0x38}`, and the port writes exactly those three.** A
per-function sweep of both modules for an offset-0 channel-enable write (`orr rX,rY,#1` followed by
`str rX,[rZ]`) finds only `pcie_msg_send_irq` `0x1754c`/`0x17550` - the mailbox pending register,
not a channel block. So the missing writes are not the host driver's.

### 2.3 The diff table - the DR path's channel registers

Values are DR ch0, CA `0x4003a590` / BAR0 `0x3f2590`; the other three blocks are `+0x50` apart.

| register (CA, offset) | field | vendor | port |
| --- | --- | --- | --- |
| **`0x4003a590` (`+0x00`)** | **per-channel enable (bit 0)** | `(old \| 1)`, live `= 1` - firmware `pcie_msg_init` `0x09490-096` (via `[obj+0x31c]`) and `0x09562-568` (via `[obj+0xb0]`) | **ABSENT** (SR-side equivalent exists: `wifidrv1.c:1649`) |
| `0x4003a598` (`+0x08`) | ctrl (`cfg[5]` = 0) | firmware `0x09450`/`0x09454` | ABSENT for DR (SR-side: `wifidrv1.c:531`) |
| `0x4003a5a0`..`+0x1c` (`+0x10` group) | base/depth-1/prod/cons of the device-internal ring | base `0x01060440`, depth-1 `0x1f`, prod/cons `3` - firmware `0x09426`/`0x09438`/`0x09444` | ABSENT (host-driver side too) |
| `0x4003a5b8` (`+0x28`) | unknown (live `0xffff`) | writer unidentified | ABSENT |
| `0x4003a5c0` (`+0x30`) | ring base (host DRAM) | `devva(node array)`, live `0x844d8000` | `wifidrv1.c:540` |
| `0x4003a5c4` (`+0x34`) | depth-1 | `0x1f` | `wifidrv1.c:542` |
| `0x4003a5c8` (`+0x38`) | producer (index + phase) | `ctx+0x1c`, live `3` | `wifidrv1.c:544` (0) and `:1705` |
| `0x4003a5cc` (`+0x3c`) | consumer | advanced by the device/firmware | read only (`:1707`, `:1736`, `:1785`) |
| **`0x4003a5d8` (`+0x48`)** | **second enable** | live `= 1` on all 7; **no writer found** in either module or `pcie_msg_init` | **ABSENT** for DR (SR-side: `wifidrv1.c:1653`) |

**Result:** the vendor sets and the port's `omo_ete_program` does not: **`+0x00` (enable)**,
**`+0x48` (second enable)**, and (legitimately device memory) the `+0x10` group. The port writes the
`+0x00`/`+0x48` pair for **SR** channels and never for **DR** - an asymmetry inside the port's own
code, not a deliberate exclusion.

### 2.4 Ranked candidates

| # | candidate | rank | why |
| --- | --- | --- | --- |
| H1 | **DR per-channel enable `+0x00`** (CA `0x4003a590`/`5e0`/`630`/`680`) is never set | **highest** | it is a real per-channel control (phase-22: the descriptor fetch is "gated by the channel enable"); the vendor ends with it `= 1` on all seven; the port knows the write and applies it to SR only; in the phase-40 run the port measured SR ch0 `+0x00 = 0 -> 1`, so **in a takeover the enabled state does not exist until the host writes it** |
| H2 | the port never re-asserts the DR program group *after* the release | high | the firmware's loop writes the same `+0x10`/`+0x14`/`+0x18`/`+0x08` fields the port pre-programmed; if it targets a port-programmed block the port's base is overwritten |
| H3 | DR second enable `+0x48` | medium | live `= 1` on all seven, but the phase-40 run read it as **already `1` before the release** and before any firmware ran - a register that already reads 1 cannot be the missing precondition |
| H4 | ETE interrupt block per-channel bits (CA `0x40039508`) | medium-low | both masks clear bits 0-2/8-10/16-20, only bits 12/29 re-enabled; not a "channel register" in the ring file's sense |
| H5 | `0x4004X000` MAC-block ring registers (`base+4`/`base+8`) | **lowest** | real host-DRAM ring bases (`0x4004a004 = 0x84a90000`) but a **different register file** (phase 6/7: the 2g/5g MAC register file, §1.2 above), the host read of that range bus-errors, and nothing connects them to the ETE D2H deposit |

### 2.5 The top candidate register

**`CA 0x4003a590` `+0x00` (BAR0 `0x3f2590`), bit 0 = 1 - the DR ch0 per-channel enable; the same on
`0x4003a5e0` / `0x4003a630` / `0x4003a680` (BAR0 `0x3f25e0` / `0x3f2630` / `0x3f2680`).**

It is the one register in the DR ring's own file that the vendor sets (firmware `0x09492`/`0x09496`,
`0x09564`/`0x09568`; live `reg_all = 1`) and the port never writes; its absence is **measurable in
the port's own logs** for the SR pair; and - unlike H5 - it is already inside `OMO_ETE_WIN`
(`wifidrv1.c:150` `#define OMO_ETE_WIN 0x3f2000UL`), so it needs no new mapping and its write can be
read back immediately.

**Why the `0x4004xxxx` family is not the answer** (`channel-array-map.md` §5,
`channel-register-offsets.md` §2): `0x84a90000` occurs exactly once in `reg_all.txt` (line 9188,
`addr = 4004a004`); it is the **2g MAC CSI buffer address** (slot 4), and its neighbour
`0x4004a008 = 0x6140` is the CSI **buffer size** (the module's own `movw r7,#0x6140`), not a ring
index. The ETE DR ring bases recorded in the same dump are `0x844d5000..0x844d8000` (a different
block), and `0x4004a000`'s pair is `{address, size}`, not `{base, depth, producer, consumer}`. The
phase-42 note that "the corrected DR base is `0x84a90000` (register at CA `0x4004a004`)" conflates
two register files (`vendor-port-dr-diff.md` §8.1).

---

## 3. The device-side precondition

**Attribution: `device-dr-side.md` §3-§6 (firmware `pcie_msg_init` @file `0x9334`, `d2h_notify`
@`0x86170`, flush @`0x86108`).**

The firmware's D2H transmit is **not** a store into host memory and **not** a write to the DR
channel's host-ring registers. It is: (a) a per-channel enable the firmware sets itself
(`block+0x00 |= 1`, `0x09490-096` / `0x09562-568`), (b) the device-local ring program group it
writes for each DR channel (`+0x10/+0x14/+0x18`, `0x09426`/`0x09438`/`0x09444`), and (c) the
**mailbox notify**: pending mask -> `out[1]` (CA `0x40039014`) plus bit 0 of the D2H doorbell
(CA `0x40101434`). The payload deposit itself - bytes landing in host DR and the DR index advancing
at `block+0x3c` - is performed by the **ETE engine**, not firmware stores (the image contains no
store to any host-aperture address and, after `pcie_msg_init`, no firmware write to any DR-channel
register at all).

| # | precondition the device checks | evidence |
| --- | --- | --- |
| P1 | the channel's `+0x00` **enable** must be 1 - the firmware sets it itself before publishing | `0x86110`/`0x86114` (`*(obj->[0xb0]) = 1`), `0x09490-096` / `0x09562-568` - [proven] |
| P2 | **`out[1]` (CA `0x40039014`) must read 0** - the host has consumed the previous notification - before the firmware publishes the next word | `0x86120..0x86128` (bounded spin on `*out[1]`) - [proven], the "keep the mailbox drained" condition |
| P3 | the notify id must be `<= 10` | `0x86170 cmp r1,#0xa` / `0x86176 bhi` - [proven] |
| P4 | the announce path waits for a host-written magic: `*(CA 0x4000010c) == 0xcece` | `0x86f74..0x86f7c` - [proven] instructions; the peer that writes `0xcece` is [unknown] |
| - | there is **no** device-side "DR ring armed" check | no firmware read of `DR+0x30` gates anything (`0x866f8..0x86704` only caches it) - [proven negative] |

So the device-side precondition on the **deposit** is a **mailbox** condition (P1-P3), not a channel
condition, and the DR channel's own host-ring registers (`+0x30/+0x34/+0x38`) are **programmed by
the host** and only *read* by the firmware.

**Important scope limit** (`device-dr-side.md` §6): both the `+0x00` enable and the `+0x10` program
group are written by the firmware *itself* when `pcie_msg_init` runs, and `docs/phase20/fw-accept.md`
B.1 already measured the takeover's own per-channel enable writes as no-ops for that reason. This
device-side register set is a real activation path the port omits, **but it is not by itself the
observed deposit gap**: the port's `omo_ete_program` runs *before* the firmware is released, and the
firmware's own `+0x00 |= 1` lands later. The residual gap the record already names - the device's
message service never being entered (`docs/phase22/fw-hostmem.md` §4,
`docs/phase42/THE-RING-MAP.md` §3) - is not a channel register in this image.

Every channel register the firmware writes is host-visible (region-3 rule `BAR0 = 0x3b8000 + (CA -
0x40000000)`): `out[1] = 0x40039014` -> `0x3f1014` (the port's `OMO_MSG1` already polls it), the
doorbell `0x40101434` -> `0x4b8434`, the channel blocks `0x4003a4xx`/`0x5xx`/`0x6xx` -> the ETE
window, and the release register `0x40000108` -> `0x3b8108` (`OMO_RELEASE_OFF`).

---

## 4. The concrete next live experiment

**Attribution: experiment shape from `vendor-port-dr-diff.md` §5-§6; cycle ordering from
`device-dr-side.md` §3-§6 and the phase-40 run log `exp/20261004-125119`.**

### 4.1 Which register, which value

Write **`CA 0x4003a590` `+0x00` (BAR0 `0x3f2590`), bit 0 = 1** - the DR ch0 per-channel enable -
and the same on `0x4003a5e0` / `0x4003a630` / `0x4003a680` (BAR0 `0x3f25e0` / `0x3f2630` /
`0x3f2680`). Read first, OR in bit 0, so a firmware-set 1 is never clobbered:

```
en = omo_rd(b + 0x00);
omo_wr(b + 0x00, en | 1, "DR chN +0x00 enable");   /* exactly the shape of wifidrv1.c:1648-1650 */
```

The port already has the write primitive and the block list (`omo_dr_block[]`, `wifidrv1.c:363`);
the register is inside `OMO_ETE_WIN` (`wifidrv1.c:150`), so no new mapping is needed.

### 4.2 Order relative to the existing cycle

In `omo_ete_program` - i.e. **at load time, in the same pre-release program step that already writes
DR `+0x30`/`+0x34`/`+0x38`** (`wifidrv1.c:540-544`) and clears the glue word (`:550`) - and
**before** the release register `0x40000108` is written (`OMO_RELEASE_OFF`, `wifidrv1.c:592`).
Phase-40 ordering reference: ring program at `39.6`-`39.8 s`, release readback at `43.0 s`. Then,
**after** the release + mailbox poll, **re-read** `+0x00`, `+0x48`, `+0x30`, `+0x34` - that one
read separates H1 from H2 (this is `vendor-port-dr-diff.md` §6 step 2).

### 4.3 Safety constraints that apply

- Never write CA `0x400392f0`; never read the RC misc window `0x10161000` (`reg_all` shows it panicked
  the box). Measure only through the endpoints' BAR0/BAR2.
- Never `rmmod` the vendor modules; the firmware's `pcie_msg_init` writes the same fields, so a write
  made after release is a no-op at best and a clobber at worst.
- Read-before-write on every channel register (log the pre-value) so a firmware-owned state is never
  clobbered and the readback proves the write - in the phase-40 run the SR pair's pre-value was `0`,
  so on those blocks the firmware really had not set it.
- Keep the DR program ordering: enable (`+0x00`) inside `omo_ete_program`, then the release write,
  then the producer commit `DR+0x38` (`wifidrv1.c:1705`) - never the release before the program.
- Run the experiment detached with the device watchdog armed first; snapshot Wi-Fi calibration before
  and verify `MANIFEST.sha256` before restoring.
- Host reads of the `0x4004xxxx` MAC range bus-error; do not add a viewport for H5 in the same run -
  keep the single change to H1 so the two candidates stay separable.
- Control: perform the same read/write pair on SR ch0 (where the port already sets `+0x00`) to show
  the write lands on the right blocks.

### 4.4 Expected observable

`omo_dr_watch`'s `0 DR deposit events` becomes non-zero, or a node `word1` becomes
`(len<<16)|0x6d2b`-shaped with a non-zero payload. If instead `+0x00` reads 1 and `+0x30` still holds
the port's `devva`, H1/H2 are cleared and the gate is confirmed to be the message service (as
`docs/phase40` concluded).

---

## 5. Verification and erratum

`VERIFICATION.md` (task `st_01a10731`) re-disassembled every quoted offset and re-read every
`reg_all.txt` value and every cited data word for all four reports: **491/491 exact matches**, all
four verdicts PASS, binary md5s recomputed and matching. The one recorded erratum is in
`channel-array-map.md` §6.3: it states `FIRMWARE.bin` holds "no firmware word ... `0x40042000`",
but the image does hold `0x40042000` at file offset `0x920f4` (as `device-dr-side.md` §5.4 says).
The sentence is not a quote, is contradicted by its own sibling lane, and touches no headline
finding; the array size/identity, the CA-to-block separation, and the `0x4004a004` interpretation
all remain independently confirmed. Nothing in this merged recipe depends on that sentence.
