# fw-sr-gate: the firmware's own ETE descriptor / message-service path, and the ranked candidates for why the sibling SR engine never fetches (phase 22, 2026-10-02)

Task `st_01a0fbde`. Read-only analysis of the firmware image plus the disassembly artifacts
already on disk; the router was not touched. Direct sequel to `docs/phase21/sr-trigger.md`,
which proved the sibling's registers, pump, interrupt path and firmware bring-up all match the
live vendor, yet the ETE SR engine never advanced its consumer (`SR+0x1c` frozen at `0x10`),
while the same pump on endpoint 0 did fetch (`SR+0x1c` `0x10 -> 0x400`,
`docs/phase20/tx-path.md`).

Every claim is **[proven]** (a constant/relocation/control-flow in the instruction stream, or a
value the device printed) or **[inferred]**.

---

## Headline

**The firmware image contains no host-writable per-channel "fetch kick" and no code that starts
the ETE descriptor fetch.** The firmware's own ETE bring-up (`pcie_msg_init`, file `0x9334`)
programs the channel program registers (`+0x10`/`+0x14`/`+0x18`/`+0x08` and the `+0x30` group),
sets each channel's enable bit `+0x00 |= 1`, configures the ETE interrupt block, and registers
two ETE interrupt handlers — but the descriptor *fetch* itself is the ETE hardware, gated by the
channel enable and the outbound address translation, not by a firmware routine. The one routine
that *consumes* an H2D message (`pcie_msg_handle`, file `0x818a8`) is proven present, correct and
**never entered in either takeover**.

**The decisive negative:** on endpoint 0 the SR engine fetched descriptors while the firmware's
message dispatcher did **not** run (`out[0]` stayed `0x08`, `irq_taken = 0`,
`docs/phase20/tx-path.md` B.1/B.3; `docs/phase20/fw-accept.md` Part B). Therefore the fetch is
**not** initiated by the firmware's message service, and the firmware routines cannot by
themselves explain the endpoint-0-works / sibling-does-not asymmetry. The gate is in the ETE
data path or the per-function/root-complex binding **outside** the host-writable register file —
which is exactly the state the sibling reaches ("every host-writable register matched to the
vendor's live values", `docs/phase21/sr-trigger.md`).

A ranked, testable candidate list is at the end.

---

## 0. Method and sources

| item | value |
| --- | --- |
| firmware image | `build/register-dumps/bringup/FIRMWARE.bin`, 928,920 B, md5 `0e530b976d5a20e87358671f1a577695` |
| runtime address | file offset + `0x40000` (phase 4/6/8) |
| disassembler | `../pyenv/Scripts/python.exe` (capstone 5.0.7 `CS_ARCH_ARM, CS_MODE_THUMB`; pyelftools present but not needed for the raw blob) |
| pre-existing dumps | `build/tmp/fwaccept/fw_disasm.txt` (the four windows quoted in `docs/phase20/fw-accept.md`), host `.ko` dumps in `build/tmp/phase20/*` and `build/tmp/phase21/*` |
| firmware literals read this phase | `build/register-dumps/bringup/FIRMWARE.bin` directly |

All firmware addresses below are **file offsets** unless marked `rt` (`rt = file + 0x40000`).
Host `.ko` quotations are marked "host ko".

---

## 1. The firmware's ETE / message bring-up: `pcie_msg_init` (file `0x9334`)

Entry `push.w {r4-r8,sb,sl,fp,lr}; sub sp,#0x44` at `0x9334`; reached through an ops-table
pointer, not a static `bl` (`0x937e ldr r3,[r7,#0x28]; 0x9382 blx r3`). The tail of the function
is the ETE channel loop, the ETE interrupt configuration, the two interrupt-handler
registrations, the six mailbox CAs, the self-enables and the `pcie_thread` registration.

### 1.1 The ETE `+0x10` program group and the channel enable **[proven]**

The loop `0x937e..0x94a4` walks the ETE channel table at file `0xc4054` (runtime `0x104054`, the
pointer held in the literal at file `0x97e4`) and programs the channel program registers from the
table's `cfg[4]` (depth `0x20`) and `cfg[5]` (control `0`) — the same register group the host's
`pcie_ete_sr_reg_init` writes:

```
  09426 rt49426  str      r1, [r2, #0x10]      ; +0x10 = ring base (device VA)
  09438 rt49438  str      r1, [r2, #0x14]      ; +0x14 = depth-1   (bfi from cfg[4]=0x20)
  09444 rt49444  str      r2, [r3, #0x18]      ; +0x18 = producer index
  09454 rt49454  str      r2, [r3, #8]         ; +0x08[2:0] = cfg[5] = 0
  0948a rt4948a  ldr.w    r3, [r3, #0x31c]     ; channel register block VA
  0948e rt4948e  cbz      r3, #0x9498
  09490 rt49490  ldr      r2, [r3]
  09492 rt49492  orr      r2, r2, #1
  09496 rt49496  str      r2, [r3]             ; +0x00 |= 1   <-- channel enable
```

The table (`file 0xc4054`, 7 populated 12-byte entries `{block, 0x06800020, flag}`):

| entry | block | word1 | flag | cfg[4] depth | cfg[5] ctrl |
| --- | --- | --- | --- | --- | --- |
| 0 | `0x400` | `0x06800020` | 1 | `0x20` | `0` |
| 1 | `0x450` | `0x06800020` | 1 | `0x20` | `0` |
| 2 | `0x4a0` | `0x06800020` | 1 | `0x20` | `0` |
| 3 | `0x590` | `0x06800020` | 1 | `0x20` | `0` |
| 4 | `0x5e0` | `0x06800020` | 0 | `0x20` | `0` |
| 5 | `0x630` | `0x06800020` | 0 | `0x20` | `0` |
| 6 | `0x680` | `0x06800020` | 0 | `0x20` | `0` |

**[proven]** for the table bytes and the `+0x00` enable. The mapping of the two firmware loops to
"SR" vs "DR" channel arrays is **[inferred]** (the firmware's internal struct offsets do not match
the host's `[ctx+0x10]=DR / [ctx+0x14]=SR`; the firmware walks 4 entries with the `+0x10` group
and 3 entries with the `+0x30` group, so the direction names cannot be assigned from the offsets
alone).

### 1.2 The `+0x30` program group and its channel enable **[proven]**

The second loop (`0x94b6..0x956e`) programs the `+0x30` program group and enables those channels
the same way:

```
  0951c rt4951c  str      r1, [r3, #0x30]      ; +0x30 = ring base
  0952e rt4952e  str      r0, [r1, #0x34]      ; +0x34 = depth-1
  0953a rt4953a  str      r1, [r3, #0x38]      ; +0x38 = producer index
  09560 rt49560  cbz      r3, #0x956a
  09562 rt49562  ldr      r2, [r3]
  09564 rt49564  orr      r2, r2, #1
  09568 rt49568  str      r2, [r3]             ; +0x00 |= 1
```

Both loops program the **same** `+0x00` enable bit the live vendor block has set (`+00=1`), which
is why the takeover's per-channel enable writes are no-ops (`docs/phase20/fw-accept.md` B.1,
`docs/phase21/sr-trigger.md` A.3). **[proven]**

### 1.3 The ETE interrupt block: clear bit 12, clear bit 29, mask `0xe0e0f8f8` **[proven]**

Later in the same function the firmware rewrites the ETE interrupt block (`[r4+0xc]`, the field the
host's `pcie_ete_intr_init` stores the CA `0x40039508` VA in):

```
  096b0 rt496b0  ldr      r3, [r4, #0xc]     ; ETE interrupt block pointer
  096b4 rt496b4  ldr      r2, [r3]
  096b8 rt496b8  bfi      r2, r7, #0xc, #1   ; clear bit 12   (r7 = 0)
  096bc rt496bc  str      r2, [r3]
  096c0 rt496c0  ldr      r2, [r3]
  096c2 rt496c2  bfi      r2, r7, #0x1d, #1  ; clear bit 29
  096c6 rt496c6  str      r2, [r3]
  096f6 rt496f6  ldr      r3, [pc, #0xd0]    ; -> file 0x97c8 = 0xe0e0f8f8
  096f8 rt496f8  ldr      r1, [r2]
  096fa rt496fa  ands     r3, r1
  096fc rt496fc  str      r3, [r2]           ; ETE intr &= 0xe0e0f8f8
```

The two literals at file `0x97c8` (`0xe0e0f8f8`) and `0x97c0`/`0x97c4` are the handler thumb
pointers (`0x000462f9` -> file `0x62f8`, `0x0004624d` -> file `0x624c`). The host then applies its
`syntax 0xffe0f8f8` (`pcie_ete_intr_init`, host ko `0x75ac..0x75b8`), so the vendor's last write
leaves `0x3f201818` — the value both the live vendor and the takeover read
(`docs/phase21/sr-trigger.md` A.3/A.4). **[proven]**

### 1.4 The firmware's own IRQ registration for the ETE interrupt **[proven]**

Immediately after clearing bits 12/29 the firmware registers two handlers and enables them:

```
  096c8 rt496c8  ldr      r2, [pc, #0xf4]    ; -> file 0x97c0 = 0x000462f9  (thumb -> file 0x62f8)
  096ca rt496ca  bl       #0x874b0           ; register(id = r0, fn = r2, arg = ...)
  096d6 rt496d6  ldr      r2, [pc, #0xec]    ; -> file 0x97c4 = 0x0004624d  (thumb -> file 0x624c)
  096da rt496da  movs     r0, #0x2e
  096dc rt496dc  bl       #0x874b0
  096e8 rt496e8  movs     r0, #0x2d
  096ea rt496ea  bl       #0x86ff4           ; enable handler 0x2d
  096ee rt496ee  movs     r0, #0x2e
  096f0 rt496f0  bl       #0x86ff4           ; enable handler 0x2e
```

with `r0 = 0x2d` set at `0x96b6 movs r0,#0x2d` and `r1 = 5` at `0x96b2`. `0x874b0` is the
firmware's handler installer — it stores the callback in a per-id table and fails if the slot is
occupied:

```
  874c4 rt874c4  add.w    r3, r5, r4, lsl #2
  874c8 rt874c8  ldr.w    r2, [r3, #0x98]     ; already registered?
  874e8 rt874e8  str.w    r8, [r3, #0x98]     ; table[id] = arg
  874ec rt874ec  bl       #0x81a3c            ; platform register (rc)
  874f4 rt874f4  bne      #0x874d0            ; occupied -> -1
```

and `0x86ff4` is the firmware's per-id enable — a range-checked bitmap set (`r3 = id-0x10;
cmp r3,#0x4f`; `movs r3,#1; lsls r3,r7; ... str` into two bitmaps). **[proven]** for the
instructions and IDs; the name of the framework is **[inferred]** (it is the IRQ/tasklet
controller implied by the adjacent `&g_irq_controller_lock` string at file `0xc419e`).

### 1.5 The message context and the six mailbox CAs **[proven]**

`0x9758..0x978e` builds the firmware's message ctx and zeroes the two host-readable mailboxes:

```
  09760 rt49760  ldr      r1, [pc, #0x74]    ; -> 0x40101434
  09762 rt49762  ldr      r2, [pc, #0x78]    ; -> 0x40039014  (out[1])
  09764 rt49764  str.w    r1, [r5, #0xd8]
  09768 rt49768  sub.w    r1, r1, #0xc8000
  0976c rt4976c  sub.w    r1, r1, #0x144     ; r1 = 0x400392f0
  09770 rt49770  str.w    r1, [r5, #0xdc]
  09774 rt49774  subs     r1, #0x1c         ; r1 = 0x400392d4
  09776 rt49776  str.w    r1, [r5, #0xe0]
  0977a rt4977a  ldr      r3, [pc, #0x64]    ; -> 0x40039010  (out[0])
  09784 rt49784  strd     r2, r3, [r5, #0xd0] ; ctx+0xd0=out[1], ctx+0xd4=out[0]
  0978c rt4978c  str      r7, [r2]           ; *out[1] = 0
  0978e rt4978e  str      r7, [r3]           ; *out[0] = 0      <-- H2D mask zeroed
```

and the function's own enable bits:

```
  0979c rt4979c  ldrh     r3, [r2]
  097a0 rt497a0  orr      r3, r3, #1
  097a4 rt497a4  strh     r3, [r2]           ; 0x40101410 |= 1
  097f4 rt497f4  ldrh     r3, [r2, #0x20]
  097f8 rt497f8  orr      r3, r3, #1
  097fc rt497fc  strh     r3, [r2, #0x20]    ; 0x40101430 |= 1
```

Both enables are set by the firmware itself after release, so the takeover's writes to them are
no-ops (`docs/phase20/fw-accept.md` B.2). **[proven]**

### 1.6 The `pcie_thread` registration **[proven]**

The tail installs the message-service handler into the firmware's handler table (via the
registration helper at file `0x8187a`, which stores `{fn,arg}` at `table[index]`, index `<= 9`):

```
  0982a rt4982a  ldr      r2, [pc, #0x38]    ; -> file 0xc5145 = thumb ptr to file 0x85144
  09830 rt49830  mov      r1, r6             ; index = 3 (the loop counter after the 3-iteration loop)
  09832 rt49832  add.w    r3, r5, #0x34      ; arg
  09838 rt49838  bl       #0x8187a           ; table[3] = file 0x85144
```

`0x85144` (`b 0x85128`) is the firmware's id-3 queue processor: it waits a semaphore and drains a
queue (`0x85128: adds r0,#0x98; bl 0xc24bc; add.w r0,r4,#0x50; b.w 0x82a54`). The adjacent
literal pool holds the thread name `pcie_thread` (file `0xc43de`) and `&pcie_msg->pcie_msg_lock`
(file `0xc43ea`). **[proven]** for the install and target; the index being `3` is **[proven]** by
the loop counter (`r6` reaches 3 at `0x956e` and is not rewritten before `0x9830`).

---

## 2. The firmware's ETE interrupt handlers — the "vector" that is armed

The two handlers the firmware registers for the ETE interrupt are:

* **id `0x2d` -> file `0x62f8`** — reads the interrupt-controller status `[r3+8]`
  (`0x62fe ldr r3,[r5,#0xc]; 0x6302 ldr r4,[r3,#8]`), tests bit 12
  (`0x6304 lsls r2,r4,#0x13; bpl`), and on it re-enables the bit in the controller
  (`0x630c orr r2,r2,#0x1000; 0x6310 str r2,[r3,#4]`), then dispatches the low byte via
  `[r5+0x20]` (`0x6352 ldr r3,[r5,#0x20]; 0x635c blx r3`) and the next byte via `[r5+0x28]`
  (`0x636e ldr r3,[r5,#0x28]`).
* **id `0x2e` -> file `0x624c`** — same shape but tests bit 29
  (`0x625a lsls r2,r4,#2; bpl`; `0x6262 orr r2,r2,#0x20000000; 0x6266 str r2,[r3,#4]`), then
  dispatches bits 16-20 via `[r5+0x24]` (`0x62d0 ldr r3,[r5,#0x24]`) and bits 24-28 via
  `[r5+0x1c]` (`0x62b4 ldr r3,[r5,#0x1c]`).

The four dispatch slots are exactly the four callbacks the host's `pcie_ete_intr_init` installs
(host ko `0x7540/0x754c/0x7558`): `ctx+0x1c = pcie_rx_handle`, `ctx+0x20 = pcie_tx_done_handle`,
`ctx+0x24 = pcie_ete_rx_err_handle`, `ctx+0x28 = pcie_ete_tx_err_handle`. So the firmware's ETE
interrupt path is **structurally identical to the host's**: status word -> bit test -> per-channel
callback table. **[proven]**

The firmware's D2H notify primitive is `0x86170`: it builds a pending mask in `[global+0xb8]`,
writes it to `[global+0x9c]` and ORs bit 0 of `[global+0xa4]` (the doorbell):

```
  8618a rt8618a  ldr.w    r1, [r5, #0xb8]    ; pending bitmap
  861a0 rt861a0  orrs     r3, r1
  861a2 rt861a2  str.w    r3, [r5, #0xb8]
  861a8 rt861a8  ldr.w    r1, [r5, #0x9c]    ; pending register
  861b0 rt861b0  str      r3, [r1]           ; write mask
  861b4 rt861b4  ldr.w    r4, [r5, #0xa4]    ; doorbell register
  861bc rt861bc  orr      r3, r3, #1
  861c0 rt861c0  str      r3, [r4]           ; doorbell |= 1
```

and it is called from the ETE handler `0x624c` tail with message id 3
(`0x62a4 ldr r0,[r5,#0x58]; 0x62aa movs r1,#3; 0x62ac b.w 0x86170`) — i.e. the firmware sends the
id-3 "transfer done" notification to the host from its own ETE interrupt path. **[proven]**

**What the firmware does *not* contain:** any registration for the ETE **descriptor fetch** and
any per-channel "notify"/"kick" register other than these interrupt bits. There is no firmware
write to the `+0x10`/`+0x30` program group after init and no firmware doorbell to trigger a fetch;
the fetch is the ETE hardware reacting to the committed producer index. **[proven]** by the full
`pcie_msg_init` transcription above plus the absence of any other ETE-register writer in the
referenced routines.

---

## 3. The firmware's H2D message consume — `pcie_msg_handle` (file `0x818a8`)

The one routine that would clear `out[0]` is present and correct (quoted from
`build/tmp/fwaccept/fw_disasm.txt`):

```
  818a8 rtc18a8  movs     r1, #0x30
  818aa rtc18aa  movs     r7, r2
  818ac rtc18ac  push     {r3, r4, r5, r6, r7, lr}
  818ae rtc18ae  mov      r6, r0            ; r6 = message ctx
  818b0 rtc18b0  cbz      r0, #0x818c8      ; CHECK 1: ctx must be non-null
  818b2 rtc18b2  movs     r7, #1
  818b4 rtc18b4  movs     r1, #0
  818b6 rtc18b6  ldr      r2, [r0, #0xc]    ; ctx+0xc  = ack register
  818b8 rtc18b8  str      r7, [r2]          ; *ack = 1               -> 0x400392f0
  818ba rtc18ba  ldr      r2, [r0, #4]      ; ctx+4    = pending register
  818bc rtc18bc  ldr      r5, [r2]          ; read pending mask      -> out[0] 0x40039010
  818be rtc18be  str      r1, [r2]          ; *pending = 0           <-- THE H2D MASK CLEAR
  818c0 rtc18c0  movs     r1, #8
  818c2 rtc18c2  ldr      r2, [r0, #0x10]    ; ctx+0x10 = re-arm register
  818c4 rtc18c4  str      r1, [r2]          ; *re-arm = 8            -> 0x400392d4
  818c6 rtc18c6  cbnz     r5, #0x818ca      ; CHECK 2: pending != 0 -> dispatch
  818ca rtc18ca  rsbs     r4, r5, #0
  818cc rtc18cc  ands     r4, r5
  818ce rtc18ce  clz      r4, r4
  818d2 rtc18d2  rsb.w    r4, r4, #0x1f     ; lowest set bit = message id
  818d6 rtc18d6  cmp      r4, #9            ; CHECK 3: id <= 9
  818d8 rtc18d8  bhi      #0x818c8
  818da rtc18da  ldr      r3, [r6, #0x20]    ; ctx+0x20 = handler table {fn,arg}
  818e6 rtc18e6  ldr      r0, [r2, #4]
  818e8 rtc18e8  blx      r3                ; handler(pending)
```

The ctx field binding (`ctx+4 = out[0]`, `ctx+0xc = 0x400392f0`, `ctx+0x10 = 0x400392d4`) is
**[inferred]** from field semantics (the only consistent arrangement); the routine and its three
offsets are **[proven]**. `pcie_msg_handle` is the firmware twin of the host's `pcie_msg_handle`
(host ko `0x171f8`).

**When it runs.** It is entered from the firmware's message thread once the device's PCIe glue
raises the internal H2D interrupt; in both takeovers it never runs: `out[0]` is never cleared by
the device (`H2D MASK CLEARED BY DEVICE` count = 0, `docs/phase21/sr-trigger.md` Part C). **[proven]**

---

## 4. What differs between fetch-works and fetch-fails

The project's two measured states:

| state | endpoint | INTx | firmware `pcie_msg_handle` ran? | ETE SR engine fetched? |
| --- | --- | --- | --- | --- |
| phase 20 (ep0) | `0000:00:00.0` (phy_devid 1, irq 207) | never (`irq_taken=0`) | **no** (`out[0]` stayed `0x08`) | **yes** (`SR+0x1c` `0x10 -> 0x400`) |
| phase 21 (sibling) | `0001:00:00.0` (phy_devid 0, irq 209) | fires (thousands) | **no** (`out[0]` never cleared) | **no** (`SR+0x1c` frozen `0x10`) |

Firmware-grounded conclusions:

1. **The fetch is not initiated by the firmware message service.** ep0 fetched while
   `pcie_msg_handle` did not run; so the id-3 doorbell/consume path is not the fetch trigger.
   **[proven by the two runs]**
2. **The firmware has no per-channel fetch kick.** The only per-channel control in the exposed
   blocks is `+0x00` (firmware-set `|= 1`) and `+0x08` (host-written from `cfg[5]=0`); the
   firmware's ETE interrupt arming is the block `0x40039508` bits (12/29 = the two handler
   groups) plus the per-channel status/enable bits. **[proven]**
3. **The firmware's ETE interrupt mask differs from the host's** (`0xe0e0f8f8` vs `0xffe0f8f8`),
   and the firmware clears bits 12/29 before the host's write; the vendor's last write leaves
   `0x3f201818`. The takeover matches that value, so it is not the *observed* difference —
   but the per-channel bits the host mask clears (bits 0-2, 8-10, 16-20) were never separately
   exercised. **[proven]**
4. **The endpoints are one aliased register space on two root complexes**
   (`docs/phase21/sibling-ep.md` A.4): the same CAs read byte-identically through ep0's
   `0x403b8000` and ep1's `0x583b8000` viewports, and only instance 0 (`0x40039000` /
   `0x40039508` / `0x4003a000`) is initialised. So the endpoint-0/sibling asymmetry is not a
   register-file asymmetry; it is a host-decode / data-path asymmetry. **[proven]**

---

## 5. Ranked hypotheses for the device lane

Each candidate names the exact writes to try and the observable that confirms or kills it.
Ranked by how well it explains ep0-fetch-works / sibling-fetch-fails given that all
host-writable registers already match the vendor.

### H1 (highest) — the ETE engine's descriptor-fetch DMA is translated by the *other* root complex's outbound window; the sibling programmed only its own

The ETE SR engine reads the host descriptor nodes through a device-VA -> host-address outbound
window. On ep0 the module programmed ep0's BAR2 outbound window and the fetch worked; on the
sibling the module programmed only ep1's BAR2 outbound window (`docs/phase21/sr-sibling.md` A.4)
and the fetch failed. A single on-chip engine bound to the *other* function's iATU would read
garbage/fault when only the sibling's window is live.

*Test writes (device lane, while claimed on the sibling, from sr2/srt's `omo_program_outbound`
values):*
- ep1's window (already tried): BAR2 `0x59800000` + `0x000=0`, `+0x004=0x80000000`,
  `+0x008=0x80000000`, `+0x00c=0`, `+0x010=0xffffffff`, `+0x014=0x80000000`, `+0x018=0`.
- **Also program ep0's window**: map/claim ep0's BAR2 `0x41800000` and write the identical
  sequence (`0x000=0`, `0x004=0x80000000`, `0x008=0x80000000`, `0x00c=0`,
  `0x010=0xffffffff`, `0x014=0x80000000`, `0x018=0`). Read each word back and log the match.
- Optionally read back ep1's outbound `+0x004/+0x008/+0x010/+0x014` and compare to the live
  vendor values (`docs/phase21/sr-sibling.md` A.4) — no readback of these was logged before.

*Confirm:* `SR+0x1c` advances past `0x10` (e.g. `0x10 -> 0x400`) after the SR producer commit.
*Kill:* `SR+0x1c` still frozen after both windows verified; or an AER/UR/bus-error appears when
the window is wrong, localising the engine's master.

### H2 — the ETE interrupt block's per-channel enable bits (cleared by the host's `0xffe0f8f8`) gate the SR fetch

The firmware's own mask `0xe0e0f8f8` (file `0x97c8`) and the host's `0xffe0f8f8` both **clear**
the per-channel bits 0-2, 8-10 and 16-20 in CA `0x40039508`, leaving `0x3f201818`. The firmware's
handlers re-enable only bits 12/29 (the two group masters), never the per-channel bits. If the
SR engine's fetch is enabled per channel by these bits, they are off in every takeover.

*Test writes (CA `0x40039508`, the ETE intr block; writes to `+0x10/+0x14` are ignored, but `+0x00`
is writable per `docs/phase21/sr-trigger.md` B.6):*
- `0x40039508 = 0x3f201818` (vendor baseline, already tested — control),
- `0x40039508 = 0x3f201f1f` (OR bits 0-2 and 8-10),
- `0x40039508 = 0x3f201818 | (1<<n)` for each of bits 0..4 and 8..12 individually.
*Confirm:* `SR+0x1c` advances after any of these. *Kill:* no change across the bit sweep.

### H3 — the SR producer commit is missing the edge/phase the engine latches

The firmware itself rewrites `+0x18` at boot (measured `{4,2,2}`, `docs/phase21/sr-sibling.md`
A.5), and the host re-syncs `wptr := rptr` then commits one lap (`0x410` / `0x000`). The ETE
engine consumes on a packed index `index[9:0] | phase[10]` transition
(`pcie_ete_ring_ptr_plus`, host ko `0x13ef8`); a commit that lands on the same packed value the
engine already holds produces no edge.

*Test writes (per SR channel block, after the firmware's rewrite is observed):*
1. `SR+0x18 = rptr` (no-op edge), then
2. `SR+0x18 = rptr + depth` (full-lap, phase toggled),
3. `SR+0x18 = 0x000` then `SR+0x18 = 0x400` (explicit phase toggle),
4. repeat with `ctrl +0x08` low-3 values `0..7` and with `+0x48=1` (vendor) / `+0x48=0`.
*Confirm:* `SR+0x1c` advances on any step. *Kill:* frozen across the whole packed-value sweep.

### H4 — the firmware's H2D channel interrupt is not armed on the sibling, so its message service (and any firmware-side prefetch) never runs

Firmware evidence: the firmware registers ETE handlers id `0x2d`/`0x2e` (files `0x62f8`/`0x624c`)
and enables them with `0x86ff4`; the handlers re-enable ETE-intr bits 12/29 and dispatch the
per-channel callback tables (`[ctx+0x20]`/`[ctx+0x28]` H2D, `[ctx+0x1c]`/`[ctx+0x24]` D2H). If the
sibling's internal glue interrupt never asserts the firmware's H2D status, neither the handlers
nor `pcie_msg_handle` (file `0x818a8`) run.

*Test writes:* after the id-3 doorbell, read the glue status `CA 0x400392ec` (vendor dynamic
`0x8 -> 0x18 -> 0x8 -> 0x9`, `docs/phase21/sibling-ep.md` B.2) and the ETE intr block
`0x40039508`; then write the per-channel/group enable masks (H2 above) and the glue enable word
`CA 0x400392e8 = 0x00000020` (binding write #2, vendor value) and re-poll.
*Confirm:* `out[0]` is cleared by the device (the dispatcher ran) and/or `SR+0x1c` advances.
*Kill:* `out[0]` still never clears and `SR+0x1c` stays frozen with the glue status unchanged
from the takeover baseline.

### H5 — the descriptor base device-VA is not reachable on the sibling RC (wrong `hostca->devva` / ACP offset), so the engine faults silently

The ring base is a device VA produced by `pcie_hostca_to_devva` and must be decoded by the
function's outbound window; the takeover adds an `acpoff` term (`omo_acpoff`,
`lab/sr2/sr2.c`). A wrong base makes the engine's read fail without a visible host-side error.

*Test writes:* read back `SR+0x10` and compare to the computed value with `acpoff=0` vs the
current offset; then write `SR+0x10 = hostca` raw, and `SR+0x10` = `hostca_to_devva(node_array)`
with `acpoff=0`; also try the DR counterpart.
*Confirm:* `SR+0x1c` advances with one of the bases. *Kill:* frozen for all base variants.

---

## 6. Proven vs inferred (summary)

| claim | status |
| --- | --- |
| `pcie_msg_init` (file `0x9334`) programs `+0x10/+0x14/+0x18/+0x08` and the `+0x30` group from the table at file `0xc4054` (`cfg[4]=0x20`, `cfg[5]=0`) | **proven** (`0x9426/0x9438/0x9444/0x9454`, `0x951c/0x952e/0x953a`, table bytes) |
| the firmware sets each channel's enable `+0x00 \|= 1` itself | **proven** (`0x9490..0x9496`, `0x9562..0x9568`) |
| the firmware clears ETE-intr bits 12 and 29 and applies `0xe0e0f8f8` (literal file `0x97c8`) | **proven** (`0x96b0..0x96fc`) |
| the firmware registers ETE handlers id `0x2d` -> file `0x62f8` and `0x2e` -> file `0x624c`, and enables them | **proven** (`0x96c8..0x96f0`, literals `0x97c0`/`0x97c4`, `0x874b0`, `0x86ff4`) |
| the two ETE handlers re-enable status bits 12/29 and dispatch via `[ctx+0x20]`/`[ctx+0x28]` (H2D) and `[ctx+0x1c]`/`[ctx+0x24]` (D2H) | **proven** (`0x6304..0x635c`, `0x625a..0x62dc`) |
| the four dispatch slots equal the host's `pcie_ete_intr_init` callbacks (`ctx+0x1c/+0x20/+0x24/+0x28`) | **proven** (host ko `0x7540/0x754c/0x7558`) |
| the firmware's D2H notify is `0x86170` (mask -> `[global+0x9c]`, doorbell -> `[global+0xa4]`), called with id 3 from `0x624c` | **proven** (`0x86170..0x861c0`, `0x62a4..0x62ac`) |
| `pcie_msg_handle` (file `0x818a8`) acks `0x400392f0`, reads+clears `out[0]`, re-arms `0x400392d4=8`, dispatches lowest bit (`<=9`) via `ctx+0x20` | **proven** (routine); ctx field binding **inferred** |
| the firmware registers its id-3 queue processor (file `0x85144`) as handler index 3 via `0x8187a` | **proven** |
| the fetch is not initiated by the firmware message service | **proven** (ep0 fetched while `pcie_msg_handle` never ran) |
| the firmware image contains no host-writable per-channel fetch kick | **proven** by transcription |
| the endpoint-0/sibling asymmetry is a host-decode/data-path difference, not a register-file difference | **proven** (aliased CAs; only instance 0 initialised) |
| the ranked causes H1-H5 | **inferred** (each has a concrete test and observable) |

---

## Reproduce

```
PY=../pyenv/Scripts/python.exe
FW=build/register-dumps/bringup/FIRMWARE.bin
$PY - <<'EOF'
from capstone import *
d=open('build/register-dumps/bringup/FIRMWARE.bin','rb').read()
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); md.skipdata=True
for a,b,label in [(0x9334,0x9870,'pcie_msg_init'),(0x62f8,0x6390,'ETE hdlr 0x2d'),
                  (0x624c,0x62f8,'ETE hdlr 0x2e'),(0x818a8,0x818f4,'pcie_msg_handle'),
                  (0x86170,0x861dc,'fw msg_send'),(0x874b0,0x87520,'hdlr register')]:
    print('==',label,'==')
    for i in md.disasm(d[a:b],a):
        print('  %05x rt%05x  %-9s %s'%(i.address,i.address+0x40000,i.mnemonic,i.op_str))
EOF
```

Artifacts this phase (new): `docs/phase22/fw-sr-gate.md` (this file). No other file was written;
the router was not touched.
