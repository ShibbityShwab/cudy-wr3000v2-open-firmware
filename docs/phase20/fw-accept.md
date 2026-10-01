# fw-accept: the chip's own HCC receive path and the device-side accept gate (phase 21, 2026-10-01)

Task `st_01a0f8b1`. Direct sequel to `docs/phase20/tx-path.md`, which proved the host→device SR path
is correct and live (the endpoint's SR engine consumed all 32 posted descriptors, and the H2D mask
`out[0]` CA `0x40039010` read back the sent bit `0x08` after `pcie_msg_send(chip,3)`) - yet the
firmware never cleared `out[0]` and never answered. That report named the blocker as a
device-side gate downstream of the descriptor fill. This phase goes into the **firmware image
itself** (`FIRMWARE.bin`, the ARMv7/Thumb-2 blob the driver loads into the chip), finds the device's
own HCC message dispatcher, and tests what has to be true for it to take the first frame.

**Headline.** The device's H2D receive path is recovered and quoted: the firmware builds its own
message context (mapping the six mailbox CAs, zeroing `out[0]`/`out[1]`) and contains a
`pcie_msg_handle`-equivalent at firmware file `0x818a8` that **reads and clears `out[0]`**, acks
`0x400392f0`, re-arms the doorbell `0x400392d4` with `8`, and dispatches the lowest set bit. That
routine exists and is correct - but in the takeover it is never invoked, so `out[0]` stays `0x08`.
The firmware's message-service init *does* run after the release (it sets its own enable bits
`0x40101410 = 1`, `0x40101430 = 1` and the channel control), so those are **not** the gate. Three
candidate enables were exercised on the takeover:

| boot | `enable` | write | result |
| ---- | -------- | ----- | ------ |
| 1 | 2 | per-channel ETE `+0x00 \|= 1`, `+0x48 = 1` (all 7) | **no-op** (the firmware already sets `+0x00`/`+0x48` itself); `out[0]` stays `0x08` |
| 2 | 4 | `0x40101410 \|= 1`, `0x40101430 \|= 1` (the firmware's own init bits) | **no-op** (the firmware sets them after release); `out[0]` stays `0x08` |
| 3 | 1 | `out[5]` CA `0x400392f0 <= 8` (`pcie_msg_send_irq @0x174a8`) | **the box hung and the 30 s watchdog rebooted it** - `0x400392f0` is a live handshake register, not a passive enable |

**The chip did not accept the frame.** No `out[0]` clear, no id-1/id-2 answer, no payload, no SR/DR
movement beyond the SR engine reading the descriptors. Part C names the remaining blocker and why it
is not a register in the windows the endpoint exposes.

Every claim is **[proven]** (a constant/relocation/control-flow in the instruction stream, or a value
the device printed/measured) or **[inferred]**.

Artifacts (regenerable):

```
PY=../pyenv/Scripts/python.exe
# firmware image (identical to rootfs-2.4.15; md5 0e530b976d5a20e87358671f1a577695, 928920 B)
FW=../build/tmp/FIRMWARE.bin
# Part-A disassembly dumps (file offsets; runtime = file + 0x40000)
$PY lab/fwaccept_disasm.py > build/tmp/fwaccept/fw_disasm.txt   # see the Reproduce section
# host-side transmit path (already recovered in phase 20f)
KO=build/register-dumps/teardown/hi5622v100_plat.ko    # md5 23660bc285393e678d5cade1c36c194b
$PY lab/ko_disasm.py $KO pcie_msg_send pcie_msg_send_irq pcie_msg_wait_for_clr \
    pcie_msg_init shuangta_pcie_msg_reg_map pcie_ete_sr_reg_init > build/tmp/fwaccept/host_msg.txt
```

Live vendor comparison: `build/register-dumps/barmap_ep0_bar0.bin` (16 MiB raw BAR0 of a live vendor
boot, read-only; phase 11) decoded as `CA = 0x40000000 + (BAR0 - 0x3b8000)`.

---

## Part A - the firmware-side H2D receive path

### A.0 Method and address convention

`FIRMWARE.bin` is the uncompressed ARMv7 Thumb-2 image the driver writes to device CA `0x01240000`.
Its **runtime address = file offset + `0x40000`** for code and data pointers (phase 4/6/8). All
addresses below are **file offsets** unless marked `rt`. The firmware is the code that runs on the
chip; the driver modules (`hi5622v100_plat.ko`, `hi5622v100_wifi.ko`) are the host side and are
quoted only for cross-reference. Disassembly is capstone 5.0.7 `CS_ARCH_ARM, CS_MODE_THUMB`.

### A.1 The firmware builds its own message context **[proven]**

The firmware's PCIe message init is the large function at file `0x9334` (entry
`push.w {r4-r8,sb,sl,fp,lr}; sub sp,#0x44`; it is reached through an ops-table pointer, so there is
no static `bl` to it - the registered entry flows through `[r7+0x28]` at `0x937e`). Its tail is the
register map and the enable writes:

```
  ; file 0x9758  (runtime 0x49758) - the six mailbox CAs into the ctx at +0xd0..+0xe4
  0x09760: ldr  r1, [pc, #0x74]      ; -> 0x40101434
  0x09762: ldr  r2, [pc, #0x78]      ; -> 0x40039014  (out[1])
  0x09764: str.w r1, [r5, #0xd8]                  ; ctx+0xd8 = 0x40101434
  0x09768: sub.w r1, r1, #0xc8000
  0x0976c: sub.w r1, r1, #0x144
  0x09770: str.w r1, [r5, #0xdc]                  ; ctx+0xdc = 0x400392f0  (out[5])
  0x09774: subs r1, #0x1c
  0x09776: str.w r1, [r5, #0xe0]                  ; ctx+0xe0 = 0x400392d4  (out[2] doorbell)
  0x0977a: ldr  r3, [pc, #0x64]      ; -> 0x40039010  (out[0])
  0x0977c: add.w r1, r1, #0xc8000
  0x09780: add.w r1, r1, #0x144
  0x09784: strd r2, r3, [r5, #0xd0]               ; ctx+0xd0 = out[1], ctx+0xd4 = out[0]
  0x09788: str.w r1, [r5, #0xe4]                  ; ctx+0xe4 = 0x40101418
  0x0978c: str  r7, [r2]                          ; *out[1] = 0
  0x0978e: str  r7, [r3]                          ; *out[0] = 0        <-- the H2D mask
  ; the message-service enable bits the firmware sets for itself
  0x09794: add.w r2, r2, #0xc8000
  0x09798: add.w r2, r2, #0x3fc                   ; r2 = 0x40101410
  0x0979c: ldrh r3, [r2]
  0x097a0: orr  r3, r3, #1
  0x097a4: strh r3, [r2]                          ; 0x40101410 |= 1
  0x097f4: ldrh r3, [r2, #0x20]                   ; 0x40101430
  0x097f8: orr  r3, r3, #1
  0x097fc: strh r3, [r2, #0x20]                   ; 0x40101430 |= 1
```

So the firmware's ctx array (base `r5+0xd0`) is, in order,
`{out[1]=0x40039014, out[0]=0x40039010, 0x40101434, 0x400392f0, 0x400392d4, 0x40101418}`, and the
init zeroes `out[0]`/`out[1]` exactly as the host's `pcie_msg_init @0xb6e4` zeroes its own copy
(`docs/phase20/message-service.md` A.4). The same function's literal pool (file `0x97d8..0x97f0`)
holds `0x40101434`, `0x40039014`, `0x40039010` and the runtime address `0x00104054` of the ETE
channel table at file `0xc4054` (`{block, 0x06800020, flag}`), so this is the firmware's
`pcie_msg_init` + ETE ring init in one routine. **[proven]**

### A.2 Where the firmware clears `out[0]` - its own dispatcher **[proven]**

The firmware's `pcie_msg_handle`-equivalent is at file `0x818a8` (runtime `0xc18a8`):

```
  ; file 0x818a8  (runtime 0xc18a8)
  0x818ae: mov  r6, r0                 ; r6 = the message ctx
  0x818b0: cbz  r0, #0x818c8
  0x818b6: ldr  r2, [r0, #0xc]         ; ctx+0xc  = ack register
  0x818b8: str  r7(=1), [r2]           ; *ack = 1                    -> CA 0x400392f0
  0x818ba: ldr  r2, [r0, #4]           ; ctx+4    = pending register
  0x818bc: ldr  r5, [r2]               ; r5 = pending mask           -> *out[0] 0x40039010
  0x818be: str  r1(=0), [r2]           ; *pending = 0   <-- THE H2D MASK CLEAR
  0x818c0: movs r1, #8
  0x818c2: ldr  r2, [r0, #0x10]        ; ctx+0x10 = re-arm register
  0x818c4: str  r1, [r2]               ; *re-arm = 8                 -> CA 0x400392d4
  0x818c6: cbnz r5, #0x818ca           ; pending != 0 -> dispatch
  0x818c8: pop  {r3,r4,r5,r6,r7,pc}
  0x818ca: rsbs r4, r5, #0
  0x818cc: ands r4, r5
  0x818ce: clz  r4, r4
  0x818d2: rsb.w r4, r4, #0x1f         ; lowest set bit = message id
  0x818d6: cmp  r4, #9
  0x818d8: bhi  #0x818c8
  0x818da: ldr  r3, [r6, #0x20]        ; ctx+0x20 = handler table {fn,arg}
  0x818dc: add.w r2, r3, r4, lsl #3
  0x818e0: ldr.w r3, [r3, r4, lsl #3]
  0x818e4: cbz  r3, #0x818ea
  0x818e6: ldr  r0, [r2, #4]
  0x818e8: blx  r3                     ; handler(pending)
```

Bound to the firmware ctx of A.1 (`ctx = r5+0xd0`), the three word offsets resolve to
`ctx+4 = out[0]`, `ctx+0xc = 0x400392f0`, `ctx+0x10 = 0x400392d4`: the firmware, when it takes an
H2D message, **writes 1 to `0x400392f0`, reads and clears `out[0]`, and writes 8 to the doorbell
`0x400392d4`** before dispatching the lowest pending bit. The ctx-base binding is **[inferred]** from
the field semantics (it is the only arrangement in which the firmware's own map yields pending =
`out[0]`); the routine and its three word offsets are **[proven]**.

The handlers are installed by the registration helper at file `0x8187a`
(`{table[index] = fn; [table+4] = arg; index <= 9}`), which the same init calls:

```
  ; file 0x9818 (runtime 0xc9818)
  0x09828: ldr  r3, [pc, #0x34]        ; -> 0x001043ea = "&pcie_msg->pcie_msg_lock"
  0x0982a: ldr  r2, [pc, #0x38]        ; -> 0x000c5145 = thumb ptr to file 0x85144
  0x09838: bl   #0x8187a               ; register handler fn=0x85144
```

and the same region references the thread name `pcie_thread` (literal `0x001043de` at file `0x97d0`,
loaded at `0x9732`). So the firmware runs a `pcie_thread` whose message handler table includes the
queue processor at file `0x85144`. **[proven]**

### A.3 The host side, cross-referenced **[proven]**

The host's `pcie_msg_send @0x160f4` writes the pending bitmap to `out[0]` and ORs bit 0 of `out[2]`;
its synchronous twin `pcie_msg_send_irq @0x174a8` additionally writes `out[5] = 8` first:

```
  0x0174f8: ldr  r3, [r4, #0x40]       ; out[5] CA 0x400392f0
  0x0174fc: mov  r2, #8
  0x017504: str  r2, [r3]              ; *out[5] = 8
  0x017508: bl   pcie_msg_wait_for_clr ; wait for the device to clear out[0]
```

`0x400392f0` is exactly the firmware's **ack** word in A.2, i.e. it is a live bidirectional
handshake register, not a passive control bit. **[proven]**

### A.4 What the firmware does after release - and what it checks before accepting

The takeover's own gate-window dump (Part B) shows the firmware's init **does** run: after the
release the firmware sets the per-channel ETE control (`+0x00 = 1`, `+0x48 = 1` for all seven
channels) and the two message-service enable bits `0x40101410 = 1`, `0x40101430 = 1`. A live vendor
boot (`barmap_ep0_bar0.bin`) reads the same ETE block `+0x000 = 0x10a`, the same mailbox window, the
same `0x40101410/0x40101430 = 1`, and `0x40039000 = 0x10b`; the only persistent difference in the
mapped windows is `0x400392e8` (vendor `0x20`, takeover `0x3ff`) - a status/idle word, not an enable.
**[measured]**

Therefore the gate is **not** a missing enable bit in the mapped windows: the firmware's own
receive context and dispatcher are built and armed, but the dispatcher is never entered when the
host rings the doorbell. `out[2]` self-clears on write (`readback = 0`), so the endpoint hardware
consumes the trigger, yet the firmware's message handler does not run and `out[0]` is never cleared.
That is consistent with the firmware's dispatcher being reached only from the device's PCIe glue
ISR, which in a vendor boot is fed by the runtime message context and ETE binding the takeover does
not reproduce (the same conclusion `docs/phase20/rx-loop.md`/`tx-path.md` reached). **[inferred]**

### A.5 Proven vs inferred

| claim | status |
| ----- | ------ |
| firmware builds a message ctx mapping the six mailbox CAs at `r5+0xd0..+0xe4`, zeroing `out[0]`/`out[1]` | **proven** (file `0x9758..0x978e`) |
| firmware sets `0x40101410 \|= 1` and `0x40101430 \|= 1` in the same init | **proven** (file `0x979c..0x97fc`) |
| firmware dispatcher at file `0x818a8` acks `ctx+0xc`, reads+clears `ctx+4`, writes `8` to `ctx+0x10`, dispatches lowest set bit | **proven** |
| with the firmware ctx, `ctx+4 = out[0]`, `ctx+0xc = 0x400392f0`, `ctx+0x10 = 0x400392d4` | **inferred** (field semantics; the only consistent binding) |
| firmware registers handlers via `0x8187a` and runs a `pcie_thread` | **proven** (file `0x9818..0x983a`, pool `0x9860/0x9864`, string pool) |
| host `pcie_msg_send_irq` writes `out[5] = 8` before flushing | **proven** (`0x174f8..0x17504`) |
| firmware sets its own channel/enable bits after release | **measured** (Part B dump vs live vendor) |
| the takeover leaves `0x400392e8` at `0x3ff` where the vendor reads `0x20` | **measured** |
| the firmware dispatcher is entered only via the device PCIe glue ISR/thread | **inferred** |

---

## Part B - `lab/fwaccept/fwaccept.c` and the test boots

`fwaccept` is `txpath` (phase 20f, kept verbatim) plus:

1. a read-only gate-window dump (`omo_dump_gate_regs`) of the ETE block `+0x000..+0x03f`, every
   channel's `+0x00/+0x08/+0x48`, and the mailbox words `0x40039000/10/14`, `0x40039108/0c`,
   `0x40039220/24`, `0x400392d0/d4/e8/f0/f4`, `0x40101410/30/34`;
2. `omo_set_enable(mode)` with the three quoted gate families (mode 0 control, 1 = `out[5]=8`,
   2 = channel `+0x00/+0x48`, 4 = `0x40101410/0x40101430`).

It keeps every proven phase-20f step: claim, the six inbound + one outbound iATU viewports,
`PCI_COMMAND=7`, `FIRMWARE.bin` → BAR0 `0x6f8000` (`diffs=0`), the ETE SR/DR program registers and
the `+0x2e8` RMW, the DR post + producer commit, the SR post (the vendor's 72-byte id-1 frame in
slot 0, the alg frame in slot 1) + producer commit, the `0x5a5a` release, the ack/clear/re-arm
service, and `pcie_msg_send(chip,3)`. Every device write is quoted; the one unproven-but-quoted
write is `PCI_INTERRUPT_LINE = 0xcf`.

Module: `lab/fwaccept/` (`Makefile`, `omo-fwaccept` loader, `stage-fwaccept.sh`,
`recover-fwaccept.sh`); CI runs `36907019235` (boot 1, `fwaccept.ko` md5
`2e6914f6cf398491a72685da2275e7fd`) and `36907688150` (boots 2-3, md5
`55165b75c6ac0e4b3e23b02a25574b46`), both `vermagic=5.10.201 SMP mod_unload ARMv7`.

### B.1 Boot 1 - `enable=2` (per-channel ETE control)

Pre-release (before the chip is released) the channel `+0x00` reads 0; after release the firmware
sets it, and the module's own writes are then no-ops:

```
DUMP pre  SR0 CA 0x4003a400: +00=00000000 +08=00000001 +48=00000001
DUMP pre  MBOX ... 39010=00000000 392d4=00000000 392e8=000003ff 392f4=00000002 101410=00000000 101430=00000000
GATE pre  SR0 CA 0x4003a400: +00=00000001 +08=00000001 +48=00000001     (firmware set it after release)
ENABLE SR ch0 +0x00 0x00000001 -> 0x00000001 readback=0x00000001
ENABLE SR ch0 +0x48 0x00000001 -> 0x00000001 readback=0x00000001
[post0 +430ms] SR ch0 DEVICE INDEX 0x00000010 -> 0x00000400 = SR engine read
[send post0] pcie_msg_send(chip,3): out[0] CA=0x40039010 0x00000000 -> 0x00000008 readback=0x00000008
[poll +910ms] MBOX out[0] H2D mask = 0x00000008   bit 3 (id 3 = pcie_ete_transfer_done_handle)
[poll +930ms] MBOX out[1] pending  = 0x00000040   bit 6 (id 6 = pcie_trigger_ete_sending_handle)
[poll +1410ms] MBOX out[1] pending = 0x00000004   bit 2 (id 2)
[poll +1620ms] MBOX out[1] pending = 0x00000000
done (release=1 rings=1 sr_posted=1 ... irq_taken=0 irq_handled=0 msgs=3 services=4 sendflag=1 dr_events=4 sr_events=1)
```

`out[0]` never clears, `out[1]` shows only id 6 and id 2, no payload, `irq_taken = 0`. **No change
from phase 20f.** Evidence: `build/register-dumps/fwaccept/{030,031}_*`.

### B.2 Boot 2 - `enable=4` (the firmware's own message-service enable bits)

Identical pre-state to boot 1; the firmware sets `0x40101410/0x40101430` itself after release, so
the module's writes are no-ops:

```
DUMP pre  MBOX ... 39010=00000000 392e8=000003ff 101410=00000000 101430=00000000
GATE pre  ... GATE pre out[5] CA=0x400392f0 = 0x00000000
ENABLE 0x40101410 0x00000001 -> 0x00000001 readback=0x00000001 (fw pcie_msg_init 0x097a0)
ENABLE 0x40101430 0x00000001 -> 0x00000001 readback=0x00000001 (fw pcie_msg_init 0x097f8)
DUMP gate MBOX ... 101410=00000001 101430=00000001 101434=00000000
[send post0] pcie_msg_send(chip,3): out[0] 0x00000000 -> 0x00000008 readback=0x00000008
[poll +800ms] MBOX out[0] H2D mask = 0x00000008
[poll +820ms] MBOX out[1] pending  = 0x00000040
[poll +1300ms] MBOX out[1] pending = 0x00000004
done (... irq_taken=0 irq_handled=0 msgs=3 services=4 sendflag=1 dr_events=4 sr_events=1)
```

Again no `out[0]` clear, no answer. Evidence: `build/register-dumps/fwaccept/{032,033}_*`.

### B.3 Boot 3 - `enable=1` (`out[5] = 8`) - HAZARD

Writing `out[5]` CA `0x400392f0 <= 8` **hung the endpoint**; the 30 s hardware watchdog rebooted the
box. The one-shot loader deletes its own `/etc/rc.d` symlink before `insmod`, so the watchdog boot
came up with `fwaccept` absent and reachable:

```
post-test3: fwaccept loaded? (none); /etc/rc.d/S99omo-fwaccept: No such file or directory
            /lib/modules/5.10.201/fwaccept.ko present (50156 B); dmesg | grep -c fwaccept = 0
            pstore: no new record (blk-0/1/2 mtimes 10:41/14:37/14:37, all pre-test)
```

Because `0x400392f0` is the firmware's live ack/handshake word (A.2/A.3), it is not a safe passive
enable; the run was not repeated. Evidence: `build/register-dumps/fwaccept/040_post_test3_state.txt`.

---

## Part C - outcome and the named next blocker

**Did the chip accept the frame and answer?** No. Across all three takeover boots the firmware
produced only `out[1] = 0x40` (id 6) then `0x04` (id 2), the H2D mask `out[0]` stayed `0x08`, there
were zero payload/HCC decode events, and `irq_taken = 0`. The host half is proven live (the SR
engine consumed the descriptors; the doorbell was written and observed), and the firmware's own
receive dispatcher is present and correct (A.2) - but it is never invoked.

**What is now proven about the gate.** It is not a missing bit in the register windows the endpoint
exposes:

- the per-channel ETE control (`+0x00`, `+0x48`) is set by the firmware itself after release
  (boot 1) - writing it changes nothing;
- the firmware's message-service enables `0x40101410/0x40101430` are set by the firmware itself
  after release (boot 2) - writing them changes nothing;
- `out[5]` (`0x400392f0`) is the firmware's live ack/handshake word; writing it hangs the chip.

The only measured difference from a live vendor boot in these windows is `0x400392e8`
(vendor `0x20`, takeover `0x3ff`), a status/idle word rather than an enable.

**Named next blocker.** The firmware's H2D dispatcher is reached only through the device's PCIe
glue ISR / `pcie_thread`, and that path is never entered by a raw takeover: the trigger (`out[2]`)
is consumed by the endpoint hardware (`readback = 0`) yet the firmware's message handler does not
run. Reproducing vendor behaviour needs the **device-side binding the firmware expects before it
services H2D** - the message-context/ETE binding and the ISR that the vendor host stands up
(`pcie_msg_init`/`pcie_ete_init`/`hcc_init`) - or a **device-side trace** (JTAG/ROM-monitor or a
firmware-side probe) to see which check gate `0x818a8` is behind. This is the same conclusion
`docs/phase20/rx-loop.md`/`tx-path.md` reached from the host side, now pinned to the exact firmware
routine that would have to run.

---

## Test record / Recovery

Raw evidence: `build/register-dumps/fwaccept/` + `build/tmp/fwaccept/`.

| file | contents |
| ---- | -------- |
| `000_baseline.txt` | live vendor boot before staging: modules/md5, drivers, irq 207/209, chip, radios |
| `010_staging.txt`, `011_staging2.txt` | vendor modules hidden, `fwaccept.ko` + loader + recovery installed, md5, syntax |
| `030_testboot1_dmesg.txt`, `031_testboot1_evidence.txt` | boot 1 (`enable=2`, md5 `2e6914f6...`), full module log |
| `032_testboot2_dmesg.txt`, `033_testboot2_evidence.txt` | boot 2 (`enable=4`, md5 `55165b75...`), full module log |
| `040_post_test3_state.txt` | boot 3 (`enable=1`) hang + watchdog reboot state |
| `060_recovery_run.txt`, `070_recovery_evidence.txt` | recovery script + verified healthy state |
| `../tmp/fwaccept/fw_disasm.txt` | the Part-A firmware disassembly |
| `../tmp/fwaccept/host_msg.txt` | the host-side `pcie_msg_send*` disassembly |

### Baseline (live vendor)

```
hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3 ; md5 wifi e21629d2... plat 23660bc2...
0000:00:00.0 and 0001:00:00.0 both -> rox_pci0 ; irq 207/209 hisi_pci_intx
chip id:0x34 version:0x00 ; 6 wlan ifaces ; br-lan 192.168.10.1/24
```

### Takeover boots

Boot 1 (`enable=2`) and boot 2 (`enable=4`): six inbound + one outbound viewport `match=YES`; SR/DR
program registers + `+0x2e8` RMW `match=YES`; firmware `diffs=0`; release `0x5a5a`; `request_irq(207)`
`rc=0`. Both: `out[0]` stays `0x08`, `out[1]` id 6 then id 2, `irq_taken=0`. Boot 3 (`enable=1`):
the `out[5]=8` write hung the chip; the watchdog rebooted into a reachable boot with `fwaccept`
absent. No new pstore record in any boot.

### Recovery (`070_recovery_evidence.txt`)

`sh /root/recover-fwaccept.sh` renamed the modules back and removed the module, loader, symlink,
`/tmp` copy and itself, `sync`, `reboot`. Recovered boot:

```
hi5622v100_plat 323584 3 hi5622v100_wifi ; hi5622v100_wifi 3387392 1 (md5 e21629d2.../23660bc2... = baseline)
both endpoints bound to rox_pci0 ; irq 207/209 hisi_pci_intx ; chip id:0x34 ; 6 wlan ifaces
2g/5g power params match baseline ; br-lan 192.168.10.1/24 UP
leftovers (module, .omo-off, loader, symlink, /tmp copy, /root script, enable file): absent
pstore: no new record (blk-0/1/2 mtimes 10:41/14:37/14:37, all pre-test)
```

**Recovery verified: the router is healthy with the vendor stack restored and both radios
answering.**

### Hazard note

Boot 3 hit the documented hazard: an `out[5]` write hung the chip and the hardware watchdog rebooted
the box. No panic record was written (`pstore` unchanged) and the recovery boot came up healthy.
All measurements were made through endpoint 0's own BAR0 (the ETE window at BAR0 `0x3f2000`) or from
the module; the RC `misc` window (`0x10161000`) was never touched.

### Writes per takeover boot

Boots 1-2: the six inbound iATU viewports + one outbound viewport + `PCI_COMMAND=7` + the
928,920-byte firmware (read back) + the seven SR/DR program registers + the `+0x2e8` RMW + the DR
base/depth/write registers + the SR base/depth/ctrl/write registers + one gate family (boot 1:
per-channel `+0x00 \|= 1` / `+0x48 = 1`; boot 2: `0x40101410 \|= 1` / `0x40101430 \|= 1`) + the
`0x5a5a` release + the ack/clear/re-arm pair + the H2D mask/doorbell pair. All quoted; the one
unproven-but-quoted write remains `PCI_INTERRUPT_LINE = 0xcf`. Boot 3: the same set plus
`out[5] <= 8` (quoted from `pcie_msg_send_irq @0x174a8`), which hung the chip.

### Reproduce

```
PY=../pyenv/Scripts/python.exe
FW=../build/tmp/FIRMWARE.bin
$PY - <<'EOF'
from capstone import *
d=open('../build/tmp/FIRMWARE.bin','rb').read()
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB)
for a,b in [(0x9334,0x9390),(0x9758,0x97a8),(0x97f4,0x9800),(0x818a8,0x818f4)]:
    for i in md.disasm(d[a:b],a): print("  %05x rt%05x  %-8s %s"%(i.address,i.address+0x40000,i.mnemonic,i.op_str))
EOF
# CI artifact
gh run download 36907688150 -n fwaccept-ko -D build/tmp/fwaccept-ko2
# stage: scp build/tmp/fwaccept-ko2/fwaccept.ko + lab/fwaccept/{omo-fwaccept,recover-fwaccept.sh,stage-fwaccept.sh}
#        to /tmp, echo <mode> > /etc/omo-fwaccept.enable, sh /tmp/stage-fwaccept.sh, reboot
# recovery: sh /root/recover-fwaccept.sh
```
