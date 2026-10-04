# spare-viewport-spec: the inbound-viewport encoding, and the exact words for two spare windows (phase 49, 2026-10-04)

Task `st_01a107af` (plan `wifi-forward` task 8, `.omo/plans/wifi-forward.md:170-176`). This is the
**offline** spec: nothing in it has been programmed, no device was touched, and the only file it
writes is this one. It is written to be implemented by the one-shot `iatuview` module (plan task 13,
`.omo/plans/wifi-forward.md:223`), so every field is given as a register offset **and** a word value.

Markers: **[proven]** = a constant/relocation in a disassembly, a vendor log line, or a port source
line; **[measured]** = a value the device or a frozen dump printed; **[derived]** = arithmetic from
sourced values (the arithmetic is machine-checked, §7).

---

## 0. The two questions the two windows must answer

| window | spare index | host window | device CA target | size | what it decides |
| ------ | ----------- | ----------- | ---------------- | ---- | --------------- |
| **A** | 6 | `0x40500000` | `0x40160000` | `0x10000` | can the firmware interrupt-controller block (`0x4016010c` pending/IAR, `0x40161100`/`0x40161108` enables, `0x40161800` priority) be made host-visible? |
| **B** | 7 | `0x40510000` | `0x40039000` | `0x1000` | **positive control**: the ctrl-rb/message block, a target that provably decodes - so A's no-decode can be told apart from a failed program |

Why B is not optional: `0xffffffff` is what an *unprogrammed* iATU window returns (phase17 §C.2,
"the vendor's target window is **not decoded**: `BAR0+0x6f8000` reads `0xffffffff`"; phase18 §A.6
"an unprogrammed window reads `0xffffffff`", `wifidrv1.c:76-80`), and it is *also* what an
unrouted host address returns. A alone therefore proves nothing - only a B that decodes in the same
run makes A's `0xffffffff` a statement about the device. This is the plan's own design
(`.omo/drafts/wifi-forward.md:55`: "expected-negative (no-decode 0xffffffff) ... the parameters
(base/limit/target readbacks + positive control) are the measurement").

---

## 1. The encoding, recovered from two independent implementations

### 1.1 Where the viewport registers live on this silicon

The endpoint implements the iATU **twice** and picks one by PCI revision id (config byte 8):
rev==1 uses the config-space block (`0x900`/`0x908`/`0x90c..0x91c`), rev==0 uses the **BAR2 MMIO
window** and never touches `0x900` (phase18 §A.0-A.3: the gate is at `0x98b8..0x98c8`, the rev==1
path calls `oal_pcie_set_inbound_by_viewport` @`0x97a8`; `phase18/inbound-map.md:29`, `:104-105`).

Our endpoint is **rev 0** (`cfg[0x008] = 0x02800000`, byte 8 = `0x00` **[measured]**
`phase18/inbound-map.md:247`), and its config-space block is dead: `cfg[0x900] = 0xffffffff`
**[measured]** `phase18/inbound-map.md:293`. **So the config-space twin registers must not be used -
the module programs BAR2.** This is the first thing phase 16/17 got wrong.

| item | value | source |
| ---- | ----- | ------ |
| iATU window is BAR2 | `OMO_IATU_BAR = 2` | `wifidrv1.c:72` |
| BAR2 bus address / length | `0x41800000` / `0x4000` (16 KiB, `/proc/iomem` name `iatu_bar1`) | `phase11/barmap.md:63`; `phase18/inbound-map.md:298` |
| BAR0 bus address / length | `0x40000000` / `0x1000000` (16 MiB) | `phase11/barmap.md:63`; `phase18/inbound-map.md:298` |

Host window bases are `BAR0 base + offset` **[proven]**: the port computes `base = bar0_base + r->off`
(`wifidrv1.c:312-313`, spare at `:327`) and takes `bar0_base` from **config space**
`PCI_BASE_ADDRESS_0 & PCI_BASE_ADDRESS_MEM_MASK` (`wifidrv1.c:2047-2049`), because
`pci_resource_start()` returns 0 on this vendor kernel and a first attempt that used it programmed
`base = 0x000000..0x008cffff` and decoded nothing (`phase18/inbound-map.md` §B.3). Expect
`bar0_base = 0x40000000`.

### 1.2 Register layout, per viewport **[proven]**

Inbound viewport `i`'s block starts at `BAR2 + 0x100 + 0x200*i`; the port expresses the same layout
as `c = OMO_IATU_CTRL2 + OMO_IATU_STRIDE * i` (`wifidrv1.c:73-74`, `:311`) and the vendor's order
table (`phase18/inbound-map.md:213-220`) is the cross-check.

| BAR2 offset | name | config-space twin | meaning | source |
| ----------- | ---- | ----------------- | ------- | ------ |
| `0x100 + 0x200*i` | CTRL1 | `0x904` | region type; **the vendor never writes it**, it stays 0 = memory region | `phase18/inbound-map.md:220` |
| `0x104 + 0x200*i` | CTRL2 | `0x908` | control word 2: bit 31 = enable, bits 10:8 = BAR number | `phase18/inbound-map.md:213-214`; `wifidrv1.c:318` |
| `0x108 + 0x200*i` | LOWER_BASE | `0x90c` | host window base, low 32 | `phase18/inbound-map.md:215` |
| `0x10c + 0x200*i` | UPPER_BASE | `0x910` | host window base, high 32 | `phase18/inbound-map.md:216` |
| `0x110 + 0x200*i` | LIMIT | `0x914` | `base + size - 1`, low 32; its high half must equal UPPER_BASE | `phase18/inbound-map.md:217` |
| `0x114 + 0x200*i` | LOWER_TARGET | `0x918` | device CA target, low 32 | `phase18/inbound-map.md:218` |
| `0x118 + 0x200*i` | UPPER_TARGET | `0x91c` | device CA target, high 32 | `phase18/inbound-map.md:219` |

There is **no separate LIMIT_HI register** in the unrolled layout: the vendor writes 7 words per
region (CTRL2 twice, base_lo/hi, limit, target_lo/hi) and separately asserts that the high word of
`base + size - 1` equals UPPER_BASE ("iatu high 32 bits must same!", `phase18/inbound-map.md:133`).
Both our windows are under 2^32, so both high words are `0`.

### 1.3 The write order and the enable word **[proven]**

The vendor's order, reproduced literally by the port (`wifidrv1.c:317-323`, spare at `:334-340`):

```
1. CTRL2 = 0x00000000                       (disable)
2. CTRL2 = 0x80000000                       (enable | BAR0)   = (bar&7)<<8 | 0x80000000
3. LOWER_BASE = base_lo
4. UPPER_BASE = base_hi
5. LIMIT      = base + size - 1
6. LOWER_TARGET = target_lo
7. UPPER_TARGET = target_hi
```

`ctrl2 = 0` and `ctrl2 = 0x80000000` are the port's literals (`wifidrv1.c:317`, `:318`); the enable
word's composition `(bar&7)<<8 | 0x80000000` with BAR0 (bar idx 0) is the vendor's
(`phase18/inbound-map.md:214`, and the vendor's own `bar idx:0` log). `0x904`/CTRL1 is left alone
(`phase18/inbound-map.md:220`).

### 1.4 Which indices are free

The endpoint has **16 inbound viewports** and the vendor programs **6** (`phase32/irq-block-mapping.md:242-243`,
`:243`; `phase32/THE-GATE-MAP.md:120`). The six occupy indices **0..5**
(`phase18/inbound-map.md:231-236`; port table `wifidrv1.c:88-94`):

| idx | host window (absolute) | size | dev CA target | name | source |
| --- | ---------------------- | ---- | ------------- | ---- | ------ |
| 0 | `0x40000000`-`0x401bffff` | `0x1c0000` | `0x00000000` | ROM_WRAM | `wifidrv1.c:89` |
| 1 | `0x401c0000`-`0x401d7fff` | `0x018000` | `0x00400000` | TCM_NOACP | `wifidrv1.c:90` |
| 2 | `0x401d8000`-`0x403b7fff` | `0x1e0000` | `0x01000000` | PKTRAM_NOACP | `wifidrv1.c:91` |
| 3 | `0x403b8000`-`0x404d7fff` | `0x120000` | `0x40000000` | IO | `wifidrv1.c:92` |
| 4 | `0x404d8000`-`0x406b7fff` | `0x1e0000` | `0x02000000` | ACP | `wifidrv1.c:93` |
| 5 | `0x406b8000`-`0x408cffff` | `0x218000` | `0x01200000` | ACP-fw | `wifidrv1.c:94` |

**The spare viewport index is 6** (the first index in 0..15 not used by the six). Corroboration: the
port's phase-34 spare window hard-codes index 6 (`wifidrv1.c:329`,
`c = OMO_IATU_CTRL2 + OMO_IATU_STRIDE * 6`). Two windows need two indices:

| window | index | BAR2 block | CTRL2 |
| ------ | ----- | ---------- | ----- |
| A | **6** | `0xD00`..`0xD18` | `0xD04` |
| B | **7** | `0xF00`..`0xF18` | `0xF04` |

(`0x100 + 0x200*6 = 0xD00`, `0x100 + 0x200*7 = 0xF00` - arithmetic checked in §7.) Both blocks are
inside the 16 KiB BAR2 (`phase11/barmap.md:63`). **Never write the outbound viewport block
(`BAR2+0x000..0x01c`)** - it is a separate vendor step (`phase18/inbound-map.md:387` §C.4) and
the port only touches it in its own outbound path (`wifidrv1.c:1548-1556`).

---

## 2. Window A - viewport 6 - device CA `0x40160000` (the GIC/interrupt-controller block)

Host base and limit **[derived from the sourced window + BAR0 base]**:
`base = 0x40500000` (plan), `limit = 0x40500000 + 0x10000 - 1 = 0x4050ffff`, i.e. BAR0 offset
`0x500000`.

| # | BAR2 offset | register | value | note |
| - | ----------- | -------- | ----- | ---- |
| 1 | `0xD04` | CTRL2 | `0x00000000` | disable before reprogramming (vendor step 1) |
| 2 | `0xD04` | CTRL2 | `0x80000000` | enable \| BAR0 (vendor step 2) |
| 3 | `0xD08` | LOWER_BASE | `0x40500000` | host window base |
| 4 | `0xD0C` | UPPER_BASE | `0x00000000` | base >> 32 |
| 5 | `0xD10` | LIMIT | `0x4050ffff` | base + size - 1 |
| 6 | `0xD14` | LOWER_TARGET | `0x40160000` | device CA target |
| 7 | `0xD18` | UPPER_TARGET | `0x00000000` | target >> 32 |
| - | `0xD00` | CTRL1 | *not written* | read it; assert `0x00000000` (stays memory region) |

**Readback assertions (read each register back through BAR2, compare exactly):**

| register | offset | must read |
| -------- | ------ | --------- |
| CTRL1 | `0xD00` | `0x00000000` |
| CTRL2 | `0xD04` | `0x80000000` |
| LOWER_BASE | `0xD08` | `0x40500000` |
| UPPER_BASE | `0xD0C` | `0x00000000` |
| LIMIT | `0xD10` | `0x4050ffff` |
| LOWER_TARGET | `0xD14` | `0x40160000` |
| UPPER_TARGET | `0xD18` | `0x00000000` |

**Reads through the window (what the run records):**

| device CA | window offset | absolute | expected |
| --------- | ------------- | -------- | -------- |
| `0x4016010c` (interrupt pending / IAR) | `0x10c` | `0x4050010c` | `0xffffffff` (no-decode) |
| `0x40161100` (enable word 0) | `0x1100` | `0x40501100` | `0xffffffff` (no-decode) |
| `0x40161108` (enable word 1, bit 12 = source `0x4c`) | `0x1108` | `0x40501108` | `0xffffffff` (no-decode) |
| `0x40161800` (priority sibling) | `0x1800` | `0x40501800` | `0xffffffff` (no-decode) |

The offsets are `CA - 0x40160000` **[derived]**. The expected class is **no-decode `0xffffffff`**:
phase 32 proved no existing window reaches `0x4016xxxx` (`phase32/irq-block-mapping.md:1`,
`phase32/THE-GATE-MAP.md:120`), a phase-34 spare window on this same target returned the no-decode
signature (`phase35/dispatcher-never-runs.md:31`; `phase47/THE-LAST-LINK.md:157`), and the
firmware's own CPU address map is **not** proof that the PCIe inbound path decodes that range
(`phase32/irq-block-mapping.md:245-246`). A *non*-`0xffffffff` read here is the surprise the run is
looking for, and it is only believable because of window B (§5).

---

## 3. Window B - viewport 7 - device CA `0x40039000` (ctrl-rb positive control)

Host base and limit: `base = 0x40510000` (plan), `limit = 0x40510000 + 0x1000 - 1 = 0x40510fff`,
i.e. BAR0 offset `0x510000`.

| # | BAR2 offset | register | value | note |
| - | ----------- | -------- | ----- | ---- |
| 1 | `0xF04` | CTRL2 | `0x00000000` | disable (vendor step 1) |
| 2 | `0xF04` | CTRL2 | `0x80000000` | enable \| BAR0 (vendor step 2) |
| 3 | `0xF08` | LOWER_BASE | `0x40510000` | host window base |
| 4 | `0xF0C` | UPPER_BASE | `0x00000000` | base >> 32 |
| 5 | `0xF10` | LIMIT | `0x40510fff` | base + size - 1 |
| 6 | `0xF14` | LOWER_TARGET | `0x40039000` | device CA target |
| 7 | `0xF18` | UPPER_TARGET | `0x00000000` | target >> 32 |
| - | `0xF00` | CTRL1 | *not written* | read it; assert `0x00000000` |

**Readback assertions:** identical rule - `0xF00 = 0x00000000`, `0xF04 = 0x80000000`,
`0xF08 = 0x40510000`, `0xF0C = 0x00000000`, `0xF10 = 0x40510fff`, `0xF14 = 0x40039000`,
`0xF18 = 0x00000000`.

**Reads and the one write, through the window** (offsets are `CA - 0x40039000`, and the register map
is the verified sibling map `phase45/hi1105-mailbox-irq.md:35-40`):

| device CA | register | window offset | absolute | access | expected value class |
| --------- | -------- | ------------- | -------- | ------ | -------------------- |
| `0x400392d4` | `HOST2DEVICE_INTR_SET` - the H2D doorbell | `0x2d4` | `0x405102d4` | **write `0x00000001`** | - (ring once) |
| `0x400392e4` | `HOST_INTR_RAW_STATUS` | `0x2e4` | `0x405102e4` | read before + after the doorbell | non-`0xffffffff` (= decode proof), **bit 0 rises 0 -> 1 across the doorbell** |
| `0x400392e8` | `HOST_INTR_MASK` | `0x2e8` | `0x405102e8` | read | record (bit 0 = 0 means the mask is open) |
| `0x400392ec` | `HOST_INTR_STATUS` (post-mask) | `0x2ec` | `0x405102ec` | read | record; bit 0 = 1 iff the mask bit 0 is 0 |
| `0x400392f0` | `HOST_INTR_CLR` - **write-1-to-clear** | `0x2f0` | `0x405102f0` | **READ ONLY, never write** | expected `0x00000000` |

The bit-0 assertion is a measurement, not a guess: the doorbell write of `0x1` set the raw status
from `0x08` to `0x09` (`phase46/intr-fires-at-ctrlrb.md:9-10`; the pre-existing bit 3 is the D2H
latch from the boot dialogue and is never cleared, `phase46/intr-fires-at-ctrlrb.md:16-18`). The
value after the doorbell is `raw_pre + 1` = `0x09` **[derived]** when the pre-value is `0x08`; the
*sharp* assertion is only **bit 0 = 1 after the write**, because a takeover boot's pre-value for
bits other than 0 is not guaranteed.

The doorbell value `0x1` is a documented port write (the port posts `out[2] | 1`, `wifidrv1.c:1143`;
`phase46/intr-fires-at-ctrlrb.md:10`). Writing it *through* window B (rather than through region 3's
`BAR0+0x3f12d4`) is deliberate: it makes the run self-contained - the module needs no other window
programmed - and the translation is the same inbound mechanism. **Never write `0x400392f0`**
(plan guardrail; `phase47/THE-LAST-LINK.md:122-128`).

---

## 4. The run: preconditions and order of operations

**Preconditions (assert, and refuse/abort if violated):**

- **P1** the endpoint is not held by the vendor: `pci_request_mem_regions` succeeds (the `fwload`
  guard, plan task 13).
- **P2** BAR2 is decoded before anything is written: read `BAR2+0x104` (viewport 0 CTRL2) and abort
  if it reads `0xffffffff` - the port's own precondition (`phase18/inbound-map.md:276`); in a
  takeover boot its config-space twin `cfg[0x908]` reads `0x00000000` (`phase18/inbound-map.md:295`).
- **P3 - the load-bearing one: program ONLY the two spare viewports (indices 6 and 7).** The six
  regions must stay **unprogrammed** in this run. Both briefed host windows lie inside region 4's
  measured range, so programming the six in the same run would confound the measurement completely -
  see §6. (This is also the smallest possible change, and it is why the doorbell is rung through B.)

**Order:**

1. claim EP0, map BAR0 and BAR2; read `bar0_base` from **config space** `PCI_BASE_ADDRESS_0`
   (expect `0x40000000`) - never `pci_resource_start()` (§1.1).
2. P2 check on `BAR2+0x104`.
3. program viewport 6 (A) with the seven words of §2, reading each back.
4. program viewport 7 (B) with the seven words of §3, reading each back.
5. print all 14 readbacks + CTRL1 of both blocks; **if any readback mismatches, stop: FAIL, no
   verdict** (§5).
6. read B's `0x2e4` / `0x2e8` / `0x2ec` (pre-doorbell snapshot).
7. write `0x1` to B+`0x2d4` (the doorbell).
8. read B's `0x2e4` / `0x2e8` / `0x2ec` again (post-doorbell snapshot).
9. read A's window at `0x4050010c`, `0x40501100`, `0x40501108`, `0x40501800`.
10. print one machine-greppable verdict line per window and (optionally) `pci_write_config_word(dev,
    4, 7)` to replicate the vendor's trailing `PCI_COMMAND = 7` (writes back `0x0006`; the I/O bit
    is RO0 - `phase18/inbound-map.md:221`, `:310`).

---

## 5. Interpretation - what each outcome means (write this table into the evidence dir)

| A reads (`0x4050010c`) | B reads (`0x405102e4`, after the doorbell) | verdict |
| --- | --- | --- |
| `0xffffffff` | non-`0xffffffff` and bit 0 = 1 | **expected-negative**: the spare viewport decodes and B proves both the program and the host route, yet `0x40160000` does not decode on the PCIe inbound path. The GIC block stays host-invisible; the parameters (base/limit/target readbacks) are the measurement. GATE G5 default branch. |
| non-`0xffffffff` | non-`0xffffffff` and bit 0 = 1 | first host visibility of the interrupt-controller block: GATE G5 follow-on - read `0x40161108` bit 12 and the IAR across a doorbell ring through the new window. |
| any | `0xffffffff`, or bit 0 never rises | **not a GIC verdict**: the programming or the host route failed. Fix per §6 (fallback addresses) and rerun; do not record an A conclusion. |

Rule: an A verdict requires (a) all 14 iATU readbacks matching §2/§3, and (b) B decoding
non-`0xffffffff` with raw-status bit 0 rising in the same run. Anything else is a harness defect.

---

## 6. Host addresses: why these, and the region-4 hazard (the one deviation to know about)

The two host windows in §2/§3 are exactly the plan's (`.omo/plans/wifi-forward.md:223`: "spare
viewport A (dev CA 0x40160000, size 0x10000, host 0x40500000) and spare B (dev CA 0x40039000, size
0x1000, host 0x40510000, positive control)"). They are kept because they are the addresses with the
strongest *routing* evidence available offline: in vendor operation both are decoded by region 4 and
return device data, not the abort signature - the frozen 16 MiB dump reads `0x00000000` at dump
offsets `0x500000` and `0x510000` (`barmap_ep0_bar0.bin`, verified in §7), i.e. the TLP reached EP0
and was translated (region 4 -> device CA `0x02000000 + offset`, `phase18/inbound-map.md:235`;
`phase32/irq-block-mapping.md:127`). A fresh offset above the six (e.g. the port's `0x8d8000`) is
*not* independently proven routable - phase 34's `0xffffffff` there is ambiguous between "unrouted"
and "undecoded".

**The hazard that P3 protects against.** `0x40500000` and `0x40510000` both lie inside region 4's
window, `0x404d8000`-`0x406b7fff` (**[measured]** `phase18/inbound-map.md:235`;
`phase32/irq-block-mapping.md:99`), which translates to device CA `0x02000000`. If the six are
programmed in the same run (region 4 = index 4 < 6/7), then under any overlap-resolution rule the
result is uninterpretable: if the lower index wins, both reads land in ACP SRAM instead of the new
windows; if the higher wins, region 4 has been shadowed for that range. The failure modes are both
silent and both produce a **false** answer, because the shadow content is *not* the abort signature:
the frozen dump reads `0x00000000` at `0x500000`, `0x510000` and `0x519100` (§7,
`phase32/irq-block-mapping.md:127-137`). A shadowed B would therefore satisfy "non-`0xffffffff` =
decode proof" falsely, and a shadowed A would read `0x00000000` and be reported as *reachable* - the
exact class of error phase 32 had to retract for phase 30/31 (`phase32/irq-block-mapping.md:124-137`,
`:96-100`). Hence P3.

**Fallback pair (only if a later run must also program the six, e.g. to reuse region 3's doorbell
path):** move both windows above the six, where nothing can shadow them. Same targets and sizes, same
order, same assertions - only the base/limit words change:

| window | index | BAR2 block | LOWER_BASE | LIMIT | LOWER_TARGET | provenance |
| ------ | ----- | ---------- | ---------- | ----- | ------------ | ---------- |
| A' | 6 | `0xD00`..`0xD18` | `0x408d8000` | `0x408e7fff` | `0x40160000` | base = `bar0_base + 0x8d8000`, the port's phase-34 spare offset `OMO_IRQWIN_OFF` (`wifidrv1.c:299`) |
| B' | 7 | `0xF00`..`0xF18` | `0x408d0000` | `0x408d0fff` | `0x40039000` | base = region 5's limit `0x408cffff` (`phase18/inbound-map.md:236`) + 1 - the first page above the six |

A' is the offset the port already programmed and read back on both endpoints (phase 34,
`wifidrv1.c:299-301`, `:326-341`), so its iATU writes are proven - and the offsets the run reads
through A (`+0x10c`, `+0x1100`, `+0x1108`) are exactly the three the port read on hardware in
phase 34 (`wifidrv1.c:1444-1446`), with `+0x1800` inside the same `0x2000` extent (`wifidrv1.c:300`);
B' is deliberately contiguous with the documented map, the first page above region 5's limit. Both
windows are inside BAR0's 16 MiB (`phase11/barmap.md:63`).

---

## 7. Provenance: every constant above, machine-checked

Each row is `id | value | kind | source | literals`; `literals` are `+`-separated substrings that
must all appear on the cited line, and the row's value must equal the numeric literal in the list
(`0x`-prefixed = hex, bare digits = decimal; the first parseable literal is the one compared -
kind `line`), the arithmetic result (kind `derived`; the cell is the formula, its operands are other row ids, so the chain is
verified in dependency order), or the word in the frozen dump at that offset (kind `measured`). A
missing dump (it is gitignored local state) is reported as SKIP, never as a pass; a mismatch is a
FAIL. Recipes:

```
pyenv/Scripts/python.exe <extracted-check> opensource/docs/phase49/spare-viewport-spec.md .
```

<!-- provenance-check:begin -->
# id | value | kind | source | literals (all must appear on the cited line; first parseable one = the value)
iatu_bar | 0x2 | line | opensource/lab/wifidrv1/wifidrv1.c:72 | OMO_IATU_BAR+2
iatu_ctrl2 | 0x104 | line | opensource/lab/wifidrv1/wifidrv1.c:73 | OMO_IATU_CTRL2+0x104
iatu_stride | 0x200 | line | opensource/lab/wifidrv1/wifidrv1.c:74 | OMO_IATU_STRIDE+0x200
ctrl1_off | 0x100 | line | opensource/docs/phase18/inbound-map.md:220 | CTRL1, stays 0+0x100
ctrl2_disable | 0x0 | line | opensource/lab/wifidrv1/wifidrv1.c:317 | ctrl2=0+0
ctrl2_word | 0x80000000 | line | opensource/lab/wifidrv1/wifidrv1.c:318 | ctrl2=ena+0x80000000
bar0_base | 0x40000000 | line | opensource/docs/phase18/inbound-map.md:298 | 0x40000000+BAR0 base=0x40000000 (config space)+BAR2=0x41800000
bar2_base | 0x41800000 | line | opensource/docs/phase11/barmap.md:63 | 0x41800000+0x4000+16 KiB
bar0_len | 0x1000000 | line | opensource/docs/phase11/barmap.md:63 | 0x1000000+16 MiB
region4_base | 0x404d8000 | line | opensource/docs/phase18/inbound-map.md:235 | 0x404d8000+0x1e0000
region4_span | 0x1e0000 | line | opensource/docs/phase18/inbound-map.md:235 | 0x1e0000+0x02000000
region5_lim | 0x408cffff | line | opensource/docs/phase18/inbound-map.md:236 | 0x408cffff+0x218000
regions_used | 0x6 | line | opensource/lab/wifidrv1/wifidrv1.c:88 | omo_regions[6]+6
port_spare_idx | 0x6 | line | opensource/lab/wifidrv1/wifidrv1.c:329 | OMO_IATU_STRIDE * 6+6
port_spare_off | 0x8d8000 | line | opensource/lab/wifidrv1/wifidrv1.c:299 | OMO_IRQWIN_OFF+0x8d8000
port_spare_tgt | 0x40160000 | line | opensource/lab/wifidrv1/wifidrv1.c:301 | OMO_IRQWIN_TGT+0x40160000
viewport_count | 0x10 | line | opensource/docs/phase32/irq-block-mapping.md:242 | the endpoint has 16 inbound+16
cfg008 | 0x2800000 | line | opensource/docs/phase18/inbound-map.md:247 | revision+0x02800000
cfg900_dead | 0xffffffff | line | opensource/docs/phase18/inbound-map.md:293 | cfg[0x900] = 0xffffffff+0xffffffff
A_base | 0x40500000 | line | .omo/plans/wifi-forward.md:223 | host 0x40500000+0x40500000
A_size | 0x10000 | line | .omo/plans/wifi-forward.md:223 | size 0x10000+0x10000
A_tgt | 0x40160000 | line | .omo/plans/wifi-forward.md:223 | dev CA 0x40160000+0x40160000
B_base | 0x40510000 | line | .omo/plans/wifi-forward.md:223 | host 0x40510000+0x40510000
B_size | 0x1000 | line | .omo/plans/wifi-forward.md:223 | size 0x1000+0x1000
B_tgt | 0x40039000 | line | .omo/plans/wifi-forward.md:223 | dev CA 0x40039000+0x40039000
doorbell_off | 0x2d4 | line | opensource/docs/phase45/hi1105-mailbox-irq.md:35 | 0x2D4+ring the doorbell
raw_off | 0x2e4 | line | opensource/docs/phase45/hi1105-mailbox-irq.md:37 | 0x2E4+RAW_STATUS
mask_off | 0x2e8 | line | opensource/docs/phase45/hi1105-mailbox-irq.md:38 | 0x2E8+the arm register
status_off | 0x2ec | line | opensource/docs/phase45/hi1105-mailbox-irq.md:39 | 0x2EC+post-mask status
clr_off | 0x2f0 | line | opensource/docs/phase45/hi1105-mailbox-irq.md:40 | 0x2F0+write-1-to-clear
doorbell_val | 0x1 | line | opensource/docs/phase46/intr-fires-at-ctrlrb.md:10 | doorbell out[2] <= 0x1+0x1
raw_pre | 0x8 | line | opensource/docs/phase46/intr-fires-at-ctrlrb.md:9 | raw(0x2e4)=0x08+0x08
raw_post | 0x9 | line | opensource/docs/phase46/intr-fires-at-ctrlrb.md:10 | raw 0x08 -> 0x09+0x09
gic_pending_ca | 0x4016010c | line | opensource/docs/phase32/THE-GATE-MAP.md:120 | 0x4016010C+0x40160000
gic_en1_ca | 0x40161108 | line | opensource/docs/phase32/THE-GATE-MAP.md:120 | 0x40161108+host-visible
gic_en0_ca | 0x40161100 | line | opensource/docs/phase32/irq-block-mapping.md:1 | 0x40161100+host-visible
gic_prio_ca | 0x40161800 | line | opensource/docs/phase32/irq-block-mapping.md:4 | 0x40161800+priority sibling
idx6_ctrl2 | 0xd04 | derived | - | iatu_ctrl2 + iatu_stride * 6
idx7_ctrl2 | 0xf04 | derived | - | iatu_ctrl2 + iatu_stride * 7
idx6_block | 0xd00 | derived | - | ctrl1_off + iatu_stride * 6
idx7_block | 0xf00 | derived | - | ctrl1_off + iatu_stride * 7
A_off | 0x500000 | derived | - | A_base - bar0_base
B_off | 0x510000 | derived | - | B_base - bar0_base
A_limit | 0x4050ffff | derived | - | A_base + A_size - 1
B_limit | 0x40510fff | derived | - | B_base + B_size - 1
A_upper_base | 0x0 | derived | - | A_base >> 32
B_upper_base | 0x0 | derived | - | B_base >> 32
A_upper_tgt | 0x0 | derived | - | A_tgt >> 32
B_upper_tgt | 0x0 | derived | - | B_tgt >> 32
A_read_iar | 0x4050010c | derived | - | A_base + gic_pending_ca - A_tgt
A_read_en0 | 0x40501100 | derived | - | A_base + gic_en0_ca - A_tgt
A_read_en1 | 0x40501108 | derived | - | A_base + gic_en1_ca - A_tgt
A_read_prio | 0x40501800 | derived | - | A_base + gic_prio_ca - A_tgt
B_write_doorbell | 0x405102d4 | derived | - | B_base + doorbell_off
B_read_raw | 0x405102e4 | derived | - | B_base + raw_off
B_read_mask | 0x405102e8 | derived | - | B_base + mask_off
B_read_status | 0x405102ec | derived | - | B_base + status_off
B_read_clr | 0x405102f0 | derived | - | B_base + clr_off
raw_expected | 0x9 | derived | - | raw_pre + doorbell_val
fallback_A_base | 0x408d8000 | derived | - | bar0_base + port_spare_off
fallback_A_limit | 0x408e7fff | derived | - | fallback_A_base + A_size - 1
fallback_B_base | 0x408d0000 | derived | - | region5_lim + 1
fallback_B_limit | 0x408d0fff | derived | - | fallback_B_base + B_size - 1
dump_500000 | 0x00000000 | measured | opensource/build/register-dumps/barmap_ep0_bar0.bin@0x500000 | shadow-word-at-A-host-window
dump_510000 | 0x00000000 | measured | opensource/build/register-dumps/barmap_ep0_bar0.bin@0x510000 | shadow-word-at-B-host-window
dump_519100 | 0x00000000 | measured | opensource/build/register-dumps/barmap_ep0_bar0.bin@0x519100 | phase32-section-C-anchor
dump_8d0000 | 0xffffffff | measured | opensource/build/register-dumps/barmap_ep0_bar0.bin@0x8d0000 | fallback-B-page-unmapped-in-vendor-op
dump_8d8000 | 0xffffffff | measured | opensource/build/register-dumps/barmap_ep0_bar0.bin@0x8d8000 | fallback-A-base-unmapped-in-vendor-op
<!-- provenance-check:end -->

### 7.1 The checker (self-contained; extract the block below and run it)

<!-- qa-check:begin -->
```python
import os
import re
import struct
import sys

spec_path = sys.argv[1] if len(sys.argv) > 1 else "opensource/docs/phase49/spare-viewport-spec.md"
root = sys.argv[2] if len(sys.argv) > 2 else os.getcwd()
text = open(spec_path, encoding="utf-8").read()

m = re.search(r"<!-- provenance-check:begin -->(.*?)<!-- provenance-check:end -->", text, re.S)
if not m:
    sys.exit("FAIL: provenance block not found in %s" % spec_path)

rows, order = {}, []
for raw in m.group(1).strip().splitlines():
    raw = raw.strip()
    if not raw or raw.startswith("#"):
        continue
    parts = [p.strip() for p in raw.split("|")]
    if len(parts) != 5:
        sys.exit("FAIL: malformed provenance row: %r" % raw)
    rid, value, kind, source, literals = parts
    rows[rid] = dict(value=int(value, 16), kind=kind, source=source, formula=literals,
                     literals=[l.strip() for l in literals.split("+") if l.strip()])
    order.append(rid)


def as_num(tok):
    tok = tok.strip()
    prefixed = re.fullmatch(r"0[xX]([0-9a-fA-F]+)[uUlL]*", tok)
    if prefixed:
        return int(prefixed.group(1), 16)
    if re.fullmatch(r"[0-9]+", tok):
        return int(tok, 10)
    if re.fullmatch(r"[0-9a-fA-F]+", tok):
        return int(tok, 16)
    return None


seen, fails, skips, checked = {}, [], [], 0
for rid in order:
    r = rows[rid]
    if r["kind"] == "line":
        path, _, lineno = r["source"].rpartition(":")
        full = os.path.join(root, path)
        if not os.path.exists(full):
            skips.append((rid, "source file absent: " + path))
            continue
        lines = open(full, encoding="utf-8", errors="replace").read().splitlines()
        n = int(lineno)
        if not 1 <= n <= len(lines):
            fails.append("%s: %s has only %d lines" % (rid, r["source"], len(lines)))
            continue
        src = lines[n - 1]
        for lit in r["literals"]:
            if lit not in src:
                fails.append("%s: literal %r is NOT on %s" % (rid, lit, r["source"]))
        nums = [v for v in (as_num(l) for l in r["literals"]) if v is not None]
        if not nums:
            fails.append("%s: no numeric literal in %r" % (rid, r["literals"]))
        elif nums[0] != r["value"]:
            fails.append("%s: value 0x%x != literal %s -> 0x%x" % (rid, r["value"], r["literals"], nums[0]))
        else:
            checked += 1
    elif r["kind"] == "measured":
        path, _, off = r["source"].rpartition("@")
        full = os.path.join(root, path)
        if not os.path.exists(full):
            skips.append((rid, "frozen dump absent: " + path))
            continue
        data = open(full, "rb").read()
        o = int(off, 16)
        got = struct.unpack_from("<I", data, o)[0]
        if got != r["value"]:
            fails.append("%s: %s@%s = 0x%08x != 0x%08x" % (rid, path, off, got, r["value"]))
        else:
            checked += 1
    elif r["kind"] == "derived":
        expr = r["formula"]
        if not re.fullmatch(r"[A-Za-z0-9_ xX+\-*/()<>&|^]+", expr):
            fails.append("%s: unsafe formula %r" % (rid, expr))
            continue
        try:
            got = int(eval(expr, {"__builtins__": {}}, dict(seen)))
        except Exception as exc:  # unknown id, syntax, division by zero
            fails.append("%s: formula %r failed: %s" % (rid, expr, exc))
            continue
        if got != r["value"]:
            fails.append("%s: %s = 0x%x but value is 0x%x" % (rid, expr, got, r["value"]))
        else:
            checked += 1
    else:
        fails.append("%s: unknown kind %r" % (rid, r["kind"]))
        continue
    seen[rid] = r["value"]

body = text.replace(m.group(0), "")
body_ok = body_missing = 0
for rid in order:
    r = rows[rid]
    if r["kind"] == "derived" and r["value"] >= 0x1000:
        if re.search(re.escape("0x%x" % r["value"]), body, re.I):
            body_ok += 1
        else:
            body_missing += 1
            fails.append("%s: derived 0x%x does not appear in the document body" % (rid, r["value"]))

print("provenance check: %d rows, %d verified, %d skipped, %d FAILED" % (len(order), checked, len(skips), len(fails)))
print("body cross-check: %d/%d non-trivial derived values present in the body text" % (body_ok, body_ok + body_missing))
for rid, why in skips:
    print("  SKIP  %s: %s" % (rid, why))
for f in fails:
    print("  FAIL  %s" % f)
print("RESULT: %s" % ("PASS" if not fails else "FAIL"))
sys.exit(1 if fails else 0)
```
<!-- qa-check:end -->

---

## 8. Must-nots (each also a plan guardrail)

- **Nothing is programmed by this document.** No device access was used to write it; the module and
  its run are task 13.
- **Never write `0x400392f0`** (`HOST_INTR_CLR`, W1C): `phase45/hi1105-mailbox-irq.md:40`;
  `phase47/THE-LAST-LINK.md:122-128`.
- **Never write `BAR2+0x000..0x01c`** (the outbound viewport): `phase18/inbound-map.md:387`.
- **Never write CTRL1** (`0x100 + 0x200*i`): the vendor leaves it 0 (`phase18/inbound-map.md:220`).
- **Never read the RC misc window `0x10161000`**: `phase47/THE-LAST-LINK.md:152-153`.
- **Never program the six regions in this run** (P3, §6).
- Do not use `pci_resource_start()` for the host base (`phase18/inbound-map.md` §B.3).

## 9. Sources read for this spec

`opensource/lab/wifidrv1/wifidrv1.c` (72-74, 79-83, 88-95, 148, 169, 299-301, 303-341, 1143,
1548-1556, 2047-2049, 2082); `opensource/docs/phase18/inbound-map.md` (§A.0-A.5, §A.6, §A.7, §B.2,
§B.3, §C.2, §C.4); `opensource/docs/phase17/fw-download.md` §C.2 (`:565-598`); `opensource/docs/phase11/barmap.md`
(`:23`, `:63`); `opensource/docs/phase32/THE-GATE-MAP.md` §4 item 5 (`:120`);
`opensource/docs/phase32/irq-block-mapping.md` (`:1`, `:4`, `:81-137`, §F `:242-246`);
`opensource/docs/phase35/dispatcher-never-runs.md` (`:31`); `opensource/docs/phase45/hi1105-mailbox-irq.md`
(`:35-40`); `opensource/docs/phase46/intr-fires-at-ctrlrb.md` (`:9-18`);
`opensource/docs/phase47/THE-LAST-LINK.md` (`:122-128`, `:152-157`);
`opensource/build/register-dumps/barmap_ep0_bar0.bin` (frozen, 16 MiB, phase 11);
`.omo/plans/wifi-forward.md` (task 8 `:170-176`, task 13 `:223`).
