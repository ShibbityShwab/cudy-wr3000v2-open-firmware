# doorbell-endpoint-path: the vendor rings the H2D doorbell through the phy_devid-0 function (EP1); the port rings it through EP0 (phase 32, 2026-10-04)

Task `st_01a1056e`. Read-only static analysis of the two vendor modules plus the record. One
question: **through which endpoint's BAR viewport does the vendor's host driver write the H2D
doorbell (device CA `0x400392d4`) at runtime, and does the port write it through the same one?**

This is a *static* answer about the host decode, not a new device experiment. Every offset quoted
below disassembles to the instruction claimed; the reproduction script is at the end.

---

## Verdict (one paragraph)

The vendor's H2D doorbell is written by **`pcie_msg_send`** and **`pcie_msg_send_irq`** in
`hi5622v100_plat.ko` (an OR of bit 0 into the word the message context holds at `+0x34`). That
word is built once, at `pcie_msg_init`, by **`shuangta_pcie_msg_reg_map`**, which resolves the
device CA **`0x400392d4`** through **`oal_pcie_inbound_ca_to_va(table, CA)`** with
**`table = chip->dev[0]->[4]`**. The per-function device array is keyed on the **`phy_devid`**
that `oal_pcie_probe` reads from **config space `cfg[0xff8] & 0xf`**; the live vendor boot shows
`0000:00:00.0` has `phy_devid 1` and **`0001:00:00.0` has `phy_devid 0`**, so
**`chip->dev[0]` is `0001:00:00.0` (EP1, BAR0 `0x58000000`, region-3 window `0x583b8000`, irq 209)**.
The vendor therefore rings the doorbell through **EP1's inbound viewport**: host VA
`0x583b8000 + 0x392d4 = 0x583f12d4`.

**The port is not on the same function.** `opensource/lab/wifidrv1` claims `0000:00:00.0` (EP0;
`omo_domain` default 0, live boots print `BAR0 base=0x40000000`), so it rings the doorbell at
`BAR0 + 0x3f12d4 = 0x403f12d4`. The two windows decode the **same device register** (CA
`0x400392d4` - the record proved the endpoint register space is aliased), so the register-level
effect of the two writes is identical; the difference is **which root complex carries the
host->device TLP** (RC1 for the vendor, RC0 for the port). Whether that per-RC path is what stops
the doorbell latching (phase 31) is a hypothesis, not something this static pass proves - but the
endpoint asymmetry is real and now stated.

This also **corrects `docs/phase21/both-eps.md` A.2**, which read the device array as
probe-ordered ("index 0 = first-probed = 0000:00:00.0"). It is not; it is keyed on `phy_devid`.
`docs/phase21/sibling-ep.md` A.1 and `docs/phase22/rc-routing.md` A.1 already carry the correct
`phy_devid` mapping.

---

## Sources and method

| item | value | use |
| --- | --- | --- |
| `opensource/build/register-dumps/teardown/hi5622v100_plat.ko` | md5 `23660bc285393e678d5cade1c36c194b` | the host driver - holds the doorbell write |
| `opensource/build/tmp/hi5622v100_wifi.ko` | md5 `e21629d226ec7de9a860a8955952d311` | no doorbell write; `hcc_msg_tx` is undefined (imports plat.ko's) |
| `pyenv/Scripts/python.exe`, capstone `CS_ARCH_ARM, CS_MODE_ARM` | | the `.ko` modules are **ARM mode** |
| live vendor dmesg | `opensource/build/register-dumps/bothep/002_live_vendor_dmesg.txt` | the `phy_devid`/bus/RC mapping |
| live vendor BAR2 | `opensource/build/register-dumps/bothep/001_live_vendor_bars2.txt` | both endpoints' region-3 host bases |
| live port boots | `build/register-dumps/exp/20261004-053006/dmesg.txt` etc. | `omo-drv1: BAR0 base=0x40000000` |

Addressing convention: offsets below are **section-relative** symbol offsets (what a disassembler
prints when the section is loaded at its own base) - the same convention the phase 16..31 docs
use. To reach a file offset: a `.text` symbol is at `V + 0x38`; a `.text.unlikely` symbol (only
`oal_pcie_probe` here) is at `V + 0x1d35c`. Where the file offset matters it is given inline.

---

## Part A - the two doorbell write sites (module `hi5622v100_plat.ko`)

The message context is the `0x78`-byte per-controller object `.LANCHOR0 + 0x78*controller`
(`pcie_main_init` @text `0x704`), and the six mapped message words live at `+0x2c .. +0x40`:

```
comm+0x2c = out[0]  CA 0x40039010   (pending bitmap)
comm+0x30 = out[1]  CA 0x40039014
comm+0x34 = out[2]  CA 0x400392d4   <-- THE H2D DOORBELL
comm+0x38 =         CA 0x40101438
comm+0x3c =         CA 0x40101414
comm+0x40 = out[5]  CA 0x400392f0   (the sync "send irq" word, never written by the port)
```

### A.1 `pcie_msg_send` @text `0x160f4` (file `0x1612c`)

The H2D submit path (called by `shuangta_ete_sr_dscr_fill` @text `0x17858`, reloc at text
`0x178f8`, and by `pcie_ete_rcv_buff_check` @text `0x14d74`, reloc at text `0x15144`):

```
  0x16194: ldr  r2, [r6, #0x2c]        ; r2 = out[0] VA        (CA 0x40039010)
  0x1619c: str  r3, [r2]               ; *out[0] = pending bitmap
  0x161a4: ldr  r2, [r6, #0x34]        ; r2 = out[2] VA        (CA 0x400392d4)  <-- doorbell
  0x161a8: ldr  r3, [r2]
  0x161ac: orr  r3, r3, r1             ; r1 = 1
  0x161b0: str  r3, [r2]               ; *out[2] |= 1  (ring the doorbell)
```

### A.2 `pcie_msg_send_irq` @text `0x174a8` (file `0x174e0`)

The synchronous form; installed as `dev[i]->0x60` for every function in `pcie_msg_init` (relocs
at text `0xb794`/`0xb798`). It writes the sync word `out[5]` too (which the port never touches):

```
  0x174f8: ldr  r3, [r4, #0x40]        ; r3 = out[5] VA        (CA 0x400392f0)
  0x174fc: mov  r2, #8
  0x17504: str  r2, [r3]               ; *out[5] = 8  (the synchronous arm)
  0x17538: ldr  r2, [r4, #0x2c]        ; out[0]
  0x1753c: str  r3, [r2]               ; *out[0] = pending
  0x17544: ldr  r2, [r4, #0x34]        ; r2 = out[2] VA        (CA 0x400392d4)  <-- doorbell
  0x17548: ldr  r3, [r2]
  0x1754c: orr  r3, r3, #1
  0x17550: str  r3, [r2]               ; *out[2] |= 1  (ring the doorbell)
```

Both write sites then reach **`comm+0x34`**, i.e. the VA of device CA `0x400392d4`.

---

## Part B - how that VA is built: `shuangta_pcie_msg_reg_map`

`pcie_msg_init` @text `0xb6e4` (file `0xb71c`) builds the message context and calls the chip's
message-register mapper with the comm block as its argument:

```
  0xb6f8: add  sb, r0, #0x2c           ; sb = &comm[0x2c]  (the six message words)
  0xb708: ldr  r2, [r4]                ; r2 = chip->[0]  = the per-function device array base
  0xb710: ldr  r3, [r4, #0x70]         ; r3 = chip ops/res table
  0xb714: ldr  r2, [r2]                ; r2 = dev[0]
  0xb718: ldr  r3, [r3, #8]            ; r3 = fn  = shuangta_pcie_msg_reg_map
  0xb71c: ldr  r0, [r2, #4]            ; r0 = dev[0]->[4]  = the CA->VA viewport table
  0xb720: blx  r3                      ; shuangta_pcie_msg_reg_map(table = dev[0]->[4], &comm[0x2c])
```

`shuangta_pcie_msg_reg_map` @text `0x1b1a0` (file `0x1b1d8`) maps six device CAs, in order, into
`comm+0x00/0x04/0x08/0x0c/0x10/0x14` (= `chip_ctx+0x2c/0x30/0x34/0x38/0x3c/0x40`). The third is the
doorbell:

```
  0x1b1a4: mov  r4, r1                 ; r4 = &comm[0x2c]
  0x1b1bc: mov  r5, r0                 ; r5 = the viewport table (passed straight through)
  0x1b20c: movw r1, #0x92d4
  0x1b210: movt r1, #0x4003            ; r1 = 0x400392d4   <-- the H2D doorbell CA
  0x1b214: bl   oal_pcie_inbound_ca_to_va        ; (table, CA, &sp[4])
  0x1b220: ldr  r3, [sp, #4]           ; r3 = resolved host VA
  0x1b22c: str  r3, [r4, #8]           ; comm[0x08] = doorbell VA  ( == chip_ctx+0x34 )
```

The other five, for completeness: `0x1b1b0` CA `0x40039010` -> `comm[0x00]`; `0x1b1e8` CA
`0x40039014` -> `comm[0x04]`; `0x1b230` CA `0x40101438` -> `comm[0x0c]`; `0x1b254` CA `0x40101414`
-> `comm[0x10]`; `0x1b278` CA `0x400392f0` -> `comm[0x14]`.

`oal_pcie_inbound_ca_to_va` @text `0x8dd8` (file `0x8e10`) is the CA->VA translator: it walks the
per-region descriptor entries (stride `0x50`) in the table handed in, matches the device CA and
returns `entry_va + (CA - devca_base)`:

```
  0x8df0: ldr  ip, [r0, #0x20]         ; ip = the descriptor entry array
  0x8df8: ldr  r5, [r0, #0x24]         ; r5 = region count
  0x8e34: ldr  lr, [ip]                ; lr = entry->[0] = the mapped host VA
  0x8e44: ldr  r1, [ip, #0x28]         ; entry devca_base_lo
  0x8e6c: subs r4, r4, r1              ; offset = CA - devca_base
  0x8e74: add  lr, lr, r4              ; VA = entry_va + offset
  0x8e78: str  lr, [r2]                ; *out_va = VA
```

So the doorbell VA is **the region-3 (IO) entry's host base plus `0x392d4`** - and the endpoint
question becomes: which function's table is `dev[0]->[4]`?

---

## Part C - which physical function is `chip->dev[0]`?

### C.1 The array is keyed on `phy_devid`, not probe order

`oal_pcie_probe` @text `0x4d0` (`.text.unlikely`, file `0x1d82c`) allocates a per-function context and stores it at
index **`phy_devid`**:

```
  0x05f8: bl   oal_pcie_get_phy_devid
  0x0624: ldr  r2, [r6]                ; r6 = .LANCHOR0 = the chip struct; r2 = chip->[0] = dev array
  0x062c: ldr  r3, [r2, r0, lsl #2]    ; dev[phy_devid]
  0x0650: str  r5, [r2, r0, lsl #2]    ; dev[phy_devid] = the new context
  0x0674: str  r3, [r6, #0x6c]         ; probe count++
```

`oal_pcie_get_phy_devid` @text `0xbe5c` (file `0xbe94`) is a **config-space** read:

```
  0xbe60: movw r1, #0xff8
  0xbe7c: bl   pci_read_config_dword   ; read cfg[0xff8] into sp+8
  0xbe94: ldrb r0, [sp, #8]            ; LSB of cfg[0xff8]
  0xbe9c: and  r0, r0, #0xf            ; phy_devid = cfg[0xff8] & 0xf
```

(`pci_dev_res_init` @text `0x821c` later loops `chip->dev[0..probe_count-1]`, so it is the *index*,
not the probe order, that names a function.)

### C.2 The live vendor boot fixes the mapping

`opensource/build/register-dumps/bothep/002_live_vendor_dmesg.txt` (`bothep/002`), verbatim:

```
[PCIEL]chip 0, bus 0, probe cnt 1, phy_devid:1.
[PCIEL]chip 0, bus 1, probe cnt 2, phy_devid:0.
[PCIEL]raw irq: 209 ; request pcie intx irq 209 succ
[PCIEL]bus_id_hostview[1], phy_devid[0], is_pcie_cross[1]
[PCIEL]raw irq: 207 ; request pcie intx irq 207 succ
[PCIEL]bus_id_hostview[0], phy_devid[1], is_pcie_cross[1]
```

with the region sets printed immediately after each irq request (`bothep/002`): bus 1 / phy_devid
0 gets `region paddr:0x58000000 ... region idx:3 paddr:0x583b8000`; bus 0 / phy_devid 1 gets
`region paddr:0x40000000 ... region idx:3 paddr:0x403b8000`. The config words are
`cfg[0xff8]=0x00011521` (-> `&0xf=1`) on `0000:00:00.0` and `0x00011520` (-> `0`) on
`0001:00:00.0` (`docs/phase21/sibling-ep.md` A.1, `docs/phase22/rc-routing.md` A.1, live
`sr2/001_partA_live_vendor.txt`). So:

| endpoint | domain | cfg[0xff8] | phy_devid | chip->dev[] | BAR0 | region-3 host base | INTx |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0000:00:00.0` | 0 | `0x00011521` | **1** | `dev[1]` | `0x40000000` | `0x403b8000` | 207 (0 deliveries) |
| `0001:00:00.0` | 1 | `0x00011520` | **0** | **`dev[0]`** | `0x58000000` | `0x583b8000` | 209 (the live message ISR) |

**`chip->dev[0]` = `0001:00:00.0` (EP1).** The two region-3 windows are confirmed live in
`bothep/001_live_vendor_bars2.txt`: inbound viewport 3 base_lo `0x403b8000` on EP0 and
`0x583b8000` on EP1, both targeting device CA `0x40000000`.

### C.3 The resolved doorbell address

```
vendor doorbell VA = EP1 region-3 host base + (0x400392d4 - 0x40000000)
                   = 0x583b8000 + 0x392d4
                   = 0x583f12d4
```

i.e. the vendor rings `out[2]` through **EP1's (`0001:00:00.0`, RC1) inbound viewport**.

---

## Part D - the port's endpoint

`opensource/lab/wifidrv1/wifidrv1.c` is the current port. It claims exactly one function,
`pci_get_domain_bus_and_slot(omo_domain, 0, OMO_PCI_DEV)` (`:1636`), with `omo_domain` defaulting
to **0** (`:220-222`), i.e. **`0000:00:00.0` = EP0**. It maps BAR0 (`pci_iomap(dev, 0)`), programs
its own six inbound viewports with `base = bar0_base + r->off` (`:289-309`, IO entry `off =
0x3b8000` at `:92`), and rings the doorbell with

```
  omo_msg = ioremap(BAR0 + OMO_MSG_WIN)        ; OMO_MSG_WIN = 0x3f1000   (:148)
  iowrite32(m2 | 1U, omo_msg + OMO_MSG_DOORBELL); OMO_MSG_DOORBELL = 0x2d4 (:968, :997)
```

so the port's doorbell VA is `BAR0 + 0x3f12d4`. The live boots confirm `BAR0 base=0x40000000`
(`build/register-dumps/exp/20261004-053006/dmesg.txt`, `.../20261004-052201/`, `.../044442/`),
hence **`0x403f12d4` - EP0's window, not the vendor's EP1 window.**

| | endpoint | RC | region-3 host base | doorbell VA | device register reached |
| --- | --- | --- | --- | --- | --- |
| vendor `pcie_msg_send`/`_irq` | `0001:00:00.0` (dev[0]) | RC1 | `0x583b8000` | **`0x583f12d4`** | CA `0x400392d4` |
| port `wifidrv1` | `0000:00:00.0` (dev[1]) | RC0 | `0x403b8000` | **`0x403f12d4`** | CA `0x400392d4` |

They are **not the same function**. They *are* the same device register: `docs/phase21/sibling-ep.md`
A.4 read the same CAs through both endpoints' region-3 windows and got byte-identical values, and
CA `0x400392d4` is a plain register in the shared IO block. So the doorbell write itself has the
same register-level effect on either path; the only difference is the root complex that carries
the TLP. (The vendor programs both RCs at probe - `bothep/001_live_vendor_bars2.txt` - so its
choice of dev[0] is not a constraint on the register effect.)

---

## Part E - the call chain, in one line per hop

```
plat_ko:pcie_msg_send_irq @0x174a8 / pcie_msg_send @0x160f4   (hi5622v100_plat.ko)
   -> OR bit 0 into comm[+0x34]                              (0x17544..0x17550 / 0x161a4..0x161b0)
   -> comm[+0x34] = VA built by shuangta_pcie_msg_reg_map @0x1b1a0
        (CA 0x400392d4 -> comm[+0x08], stored at 0x1b22c)
   -> oal_pcie_inbound_ca_to_va @0x8dd8 (table, CA)          (0x1b214)
   -> table = chip->dev[0]->[4]                              (pcie_msg_init @0xb6e4, 0xb708/0xb714/0xb71c)
   -> chip->dev[0] = the phy_devid-0 function = 0001:00:00.0 (oal_pcie_probe @0x4d0 + oal_pcie_get_phy_devid @0xbe5c)
   -> 0001:00:00.0 region-3 host base = 0x583b8000           (live: bothep/001_live_vendor_bars2.txt)
   -> doorbell VA = 0x583f12d4  (device CA 0x400392d4)
```

Module attribution: the doorbell write and its map are **only** in `hi5622v100_plat.ko`.
`hi5622v100_wifi.ko` has no `pcie_msg_send`/`shuangta_pcie_msg_reg_map`/`pcie_msg_init` reference;
its `hcc_msg_tx` is an undefined import resolved in plat.ko. So wifi.ko's H2D traffic reaches the
doorbell only through plat.ko's exported HCC/BAL layer, which bottoms out at `pcie_msg_send`.

---

## Part F - proven vs inferred

| claim | status |
| --- | --- |
| `pcie_msg_send_irq` @0x174a8 and `pcie_msg_send` @0x160f4 OR bit 0 into the word at `comm+0x34` | **proven** (instructions 0x17544..0x17550, 0x161a4..0x161b0) |
| `comm+0x34` is the VA of device CA `0x400392d4` | **proven** (`shuangta_pcie_msg_reg_map` 0x1b20c..0x1b22c; `pcie_msg_init` passes `&comm[0x2c]`) |
| that VA is produced by `oal_pcie_inbound_ca_to_va(dev[0]->[4], CA)` | **proven** (`pcie_msg_init` 0xb708/0xb714/0xb71c; `shuangta_pcie_msg_reg_map` 0x1b1bc/0x1b214) |
| the device array is keyed on `cfg[0xff8] & 0xf` | **proven** (`oal_pcie_get_phy_devid` 0xbe94/0xbe9c; `oal_pcie_probe` 0x62c/0x650) |
| `0001:00:00.0` has `phy_devid 0` and `0000:00:00.0` has `phy_devid 1`, so `chip->dev[0]` is `0001:00:00.0` | **proven** (live dmesg `bothep/002`; config words 0x11520/0x11521) |
| EP1's region-3 host base is `0x583b8000` (EP0's is `0x403b8000`) | **proven** (`bothep/001_live_vendor_bars2.txt`) |
| so the vendor doorbell VA is `0x583f12d4` | **proven** (arithmetic over the two above) |
| the port (`wifidrv1`) owns EP0 and writes the doorbell at `0x403f12d4` | **proven** (source `:220-222/:148/:968/:997/:1636`; live `BAR0 base=0x40000000`) |
| both windows reach the same device register, so the register effect is identical | **proven** (`docs/phase21/sibling-ep.md` A.4 alias read) |
| the differing RC path is *why* the port's doorbell does not deliver (phase 31) | **inferred** - not shown here; the alias means a register write cannot distinguish the two, so any effect must be on the TLP/interrupt routing (`is_pcie_cross[1]`) |

### Correction carried here

`docs/phase21/both-eps.md` A.2 says "`chip->dev[0]` is the first-probed function = `0000:00:00.0`".
The array index is `phy_devid`, not probe order; `dev[0]` is the `phy_devid`-0 function =
`0001:00:00.0`. That document's *experiment* (bothep boot 2) still holds, but its attribution of
the fetch to "the ETE programming via ep0's BAR0" is the outbound-window effect described in
`docs/phase22/rc-routing.md` (the fetch egresses EP0; the register windows are aliased), not an
index-0 claim. `docs/phase21/sibling-ep.md` A.1 and `docs/phase22/rc-routing.md` A.1 have the
correct mapping.

---

## Verification

Every quoted offset, from the module named:

```
PY=opensource/../pyenv/Scripts/python.exe     # repo-local venv, capstone
$PY - <<'EOF'
import struct
from capstone import *
d = open('opensource/build/register-dumps/teardown/hi5622v100_plat.ko','rb').read()
# .text file offset = 0x38; symbol .text offsets used below
md = Cs(CS_ARCH_ARM, CS_MODE_ARM)
SEC = {'.text': 0x38, '.text.unlikely': 0x1d35c}
sites = {                    # symbol: (section, section-relative offset)
  'pcie_msg_send':           ('.text', 0x160f4),
  'pcie_msg_send_irq':       ('.text', 0x174a8),
  'shuangta_pcie_msg_reg_map': ('.text', 0x1b1a0),
  'pcie_msg_init':           ('.text', 0xb6e4),
  'oal_pcie_inbound_ca_to_va': ('.text', 0x8dd8),
  'oal_pcie_get_phy_devid':  ('.text', 0xbe5c),
  'oal_pcie_probe':          ('.text.unlikely', 0x4d0),
}
for name,(sec,v) in sites.items():
    fo = SEC[sec] + v
    print('== %s @%s 0x%x (file 0x%x)' % (name, sec, v, fo))
    for i in md.disasm(d[fo: fo+0x60], v):
        print('   %05x  %-8s %s' % (i.address, i.mnemonic, i.op_str))
EOF
```

Expect the doorbell OR sequence at `0x17544`/`0x17550` and `0x161a4`/`0x161b0`, the
`movw r1,#0x92d4 ; movt r1,#0x4003` at `0x1b20c`/`0x1b210`, the `ldr r0,[r2,#4]` at `0xb71c`, the
`ldrb/and` at `0xbe94`/`0xbe9c`, and the `table->[0x20] / entry + devca_base` walk at
`0x8df0`/`0x8e34`/`0x8e44`.

Read-only artifacts used: `bothep/001_live_vendor_bars2.txt`, `bothep/002_live_vendor_dmesg.txt`
(under `opensource/build/register-dumps/`), and the live port dmesg under
`build/register-dumps/exp/20261004-*`. No device was touched for this document.
