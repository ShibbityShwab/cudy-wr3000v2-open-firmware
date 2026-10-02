# sr-sibling: the sibling's SR rings are instance 0, the device rewrites the SR index on release, and the chip still does not take the frame (phase 21, 2026-10-02)

Task `st_01a0fbb1`. Direct sequel to `docs/phase21/sibling-ep.md`, which claimed the sibling
endpoint `0001:00:00.0` (irq 209) and made the message interrupt fire for the first time
(`irq_taken 0 -> 16864`), but still saw `out[0]` never cleared, no id-1 reply, and the in-thread
SR pump at `pumped=0` with the SR indices reading back desynchronised (`SR+0x18 host commit low,
device SR+0x1c = 0x10`) on a single run.

This phase (A) resolves the sibling's ring/instance mapping from `hi5622v100_plat.ko` and the
live vendor reads, (B) implements the corrected ring programming as the post-release SR
re-sync in `lab/sr2/sr2.c` and boots it **twice**, and (C) records whether the chip takes the
frame.

Every claim is **[proven]** (a constant/relocation/control-flow in the instruction stream, a
captured live value, or a value the device printed) or **[inferred]**.

---

## Headline

**Part A: the sibling's message path and its SR rings are the *same* instance 0 the vendor
services — there is no instance switch.** Both endpoints alias one on-chip register space
(`0x4003a410` reads identically through ep0's `0x403b8000` viewport and ep1's `0x583b8000`
viewport), only instance 0 is initialised (`0x400392e8 = 0x20`, `0x40039508 = 0x3F201818`,
SR0 base `0x84573000`), and instance 1 is raw/zero. What must change on the sibling is the
**host-side decode**, not the device CA: claim ep1, use its BAR0 `0x58000000` region-3 viewport
(ETE `0x4003a000` = BAR0+`0x3f2000`), and program its own BAR2 `0x59800000` iATU — six inbound
viewports plus the one outbound window that lets the device reach the SR/DR host buffers. The SR
ring base is a device VA converted by `pcie_hostca_to_devva` and therefore *must* go through the
sibling's outbound iATU; programming it through ep0's BAR2 would leave the sibling RC undecoded.
**[proven]**

**Part A also resolves the "desynchronised indices": it is not the base, the offset, the packing,
or an instance, but a device-side rewrite of the SR *index* register after release.** On both
boots the module's pre-release commit (`SR+0x18 = 0x400`) read back as `0x400` at program time,
and at service time the same register read `0x4` (ch0) / `0x2` (ch1, ch2) with `SR+0x1c = 0x10`,
while `SR+0x10` base, `SR+0x14` depth and `SR+0x08` ctrl were untouched. The packed outstanding
arithmetic `(producer - consumer)` then goes negative on a u32 (`(4 - 16) & 0x3ff`), `free`
computes 0, and the sibep pump did nothing (`pumped=0`). The fix is to re-assert the quoted
program and re-sync `wptr := rptr` after the device is up. **[proven, reproduced on both boots]**

**Part B: the correction works and is reproducible.** `lab/sr2/sr2.c` is `sibep.c` plus
`omo_ete_resync_sr()`. On both takeover boots the post-release state is byte-for-byte identical
(`base == want`, `wptr ∈ {4,2,2}`, `rptr = 0x10`) and after the re-sync the pump refills **96
nodes** (32 per channel, `commit SR+0x18 <= 0x00000410`) versus sibep's 0. `request_irq(209)`
rc=0, `irq_taken=23813`/`10275`. **[proven]**

**Part C: the chip still does not take the frame.** `out[0]` (CA `0x40039010`) is written only
by our own doorbells and is never cleared by the device (`H2D MASK CLEARED BY DEVICE` count = 0
on both boots); `out[1]` never carries bit 0 (id-1); no `0x5a5a` buffer appears; the DR device
index stays at its idle baseline and `SR+0x1c` never advances past `0x10` after the pump. The
interrupt and the descriptor supply are now both proven live; the remaining blocker is the
device-side **H2D SR consume/accept trigger** (the device re-drives `SR+0x18` at firmware boot,
leaves base/depth/ctrl intact, and never consumes the 32 fresh host descriptors). **[proven]**

---

## Part A — the sibling's register instance, BAR decode, and the desynchronised indices

### A.0 Sources

Static: `lab/ko_disasm.py build/register-dumps/teardown/hi5622v100_plat.ko <func>` — full
`.symtab`, md5 `23660bc285393e678d5cade1c36c194b`. Raw dumps for this phase:
`build/register-dumps/sr2/010_disasm_ring_instance.txt` (ETE init/reg-init/chn_res/intr) and
`011_disasm_endpoint_window.txt` (probe / phy_devid / l1ss / hostca→devva / msg init).
Live (normal vendor boot, read-only): `build/register-dumps/sr2/001_partA_live_vendor.txt`
(dmesg, both endpoints' PCI config, `/proc/interrupts`, and the same CAs read through both
endpoints' BAR0 region-3 viewports).

### A.1 Two functions, selected by `cfg[0xff8] & 0xf`; only the sibling's INTx ever fires

`oal_pcie_get_phy_devid` @`0xbe5c` is a config read (`movw r1,#0xff8` ->
`pci_read_config_dword` -> `ldrb r0,[sp,#8]; and r0,r0,#0xf`), and `oal_pcie_probe` @`0x4d0`
keys its per-function context on it. The live vendor boot prints the mapping and the two INTx
requests:

```
[PCIEL]chip 0, bus 0, probe cnt 1, phy_devid:1.      <- 0000:00:00.0
[PCIEL]chip 0, bus 1, probe cnt 2, phy_devid:0.      <- 0001:00:00.0
[PCIEL]raw irq: 209 ; request pcie intx irq 209 succ
[PCIEL]bus_id_hostview[1], phy_devid[0], is_pcie_cross[1]
[PCIEL]raw irq: 207 ; request pcie intx irq 207 succ
```

and `/proc/interrupts` on the live vendor boot shows which one is the message ISR:

```
207:          0          0     GIC-0  91 Level     hisi_pci_intx
209:      20790          0     GIC-0  95 Level     hisi_pci_intx
```

| endpoint | domain | host BAR0 | BAR2 (iATU) | cfg[0xff8] | phy_devid | INTx |
|---|---|---|---|---|---|---|
| `0000:00:00.0` | 0 | `0x40000000` | `0x41800000` | `0x00011521` | 1 | 207 (0 deliveries) |
| `0001:00:00.0` | 1 | `0x58000000` | `0x59800000` | `0x00011520` | 0 | 209 (the live ISR) |

Both are `59e7:0005` class `028000`. **[proven]**

### A.2 Two register instances `0x800` apart; the sibling uses instance 0

`shuangta_pcie_l1ss_set` @`0x1aeb8` touches the per-device block of *each* probed function
through `oal_pcie_devca_to_hostva`, with the two device CAs exactly `0x800` apart:
`0x40039220`/`0x400392d0` for the first device and `0x40039a20`/`0x40039ad0` for the second.
The live sweep shows only instance 0 carries the vendor's init values:

| CA | instance | live vendor | meaning |
|---|---|---|---|
| `0x40039000` | 0 | `0x10b` | glue block |
| `0x400392e8` | 0 | `0x00000020` | glue chn_res, binding write #2 applied |
| `0x40039508` | 0 | `0x3f201818` | ETE interrupt, binding write #1 applied |
| `0x4003a410` | 0 | `0x84573000` | SR0 host-ring base (live) |
| `0x40039800` | 1 | `0x10c` | glue block, other function |
| `0x40039ae8` | 1 | `0x000003ff` | chn_res raw reset |
| `0x40039d08` | 1 | `0x3f3f1f1f` | ETE intr raw reset |
| `0x4003a808` | 1 | `0x0` | instance-1 ETE block, uninitialised |

`pcie_ete_chn_res` @`0x7490` proves the chn_res base is the *glue* block, not a ring block:
it reads `r3 = [r2]` (the per-device first pointer) and `r8 = [r3, #0x2e8]`, then writes it back
to `[r2+0x2e8]` (CA `0x400392e8`), masking `0xfffffc20`:

```
0x0074fc: ldr  r8, [r3, #0x2e8]   ; glue+0x2e8
0x007504: and  r8, r8, sb         ; sb = 0xfffffc20
0x007520: str  r8, [r2, #0x2e8]
```

and it skips any device whose second-level `[+0x18]` is non-zero, which is why only one
instance is ever written. `pcie_ete_intr_init` @`0x7528` takes the ETE interrupt-block CA from a
per-device descriptor (`[r1+0x18] -> [r1+4]`, resolved by `oal_pcie_inbound_ca_to_va`) and applies
the literal mask `0xffe0f8f8`:

```
0x0075ac: movw r2, #0xf8f8 ; movt r2, #0xffe0   ; 0xffe0f8f8
0x0075b4: and  r2, r2, r1
0x0075b8: str  r2, [r3]
```

The irq-209 ISR is `pcie_intr_handle` @`0x82e4`, which reads its glue status from
`[[ctx+4]]+0x2ec` and on the live irq-209 stack resolved to device CA `0x40039000`
(`sibling-ep.md` A.2), i.e. **instance 0**. There is no instance switch for the sibling: glue,
ETE interrupt and rings are all instance 0. **[proven]**

### A.3 Which BAR / BAR-offset our module must program on the sibling

The device CA does not change (both functions alias one space), but the *host decode* is
per-root-complex. The live vendor dmesg lists the sibling RC's inbound regions:

```
oal_pcie_bar_init_default: bar idx:2, phy start:0x59800000, end:0x59803fff  (iATU)
region idx:0 paddr:0x58000000 ... idx:3 paddr:0x583b8000 size:1179648 (IO) ... idx:5 0x586b8000
```

so on the sibling the module must use:

* **BAR0 = `0x58000000`** for the region-3 register viewport: host `0x583b8000` -> device CA
  `0x40000000`, therefore
  * glue block CA `0x40039000` = BAR0+`0x3f1000`,
  * ETE interrupt CA `0x40039508` = BAR0+`0x3f1508`,
  * ETE ring block CA `0x4003a000` = BAR0+`0x3f2000` (SR ch0 block CA `0x4003a400`, DR ch3
    block CA `0x4003a590`; the module's `{0x400,0x450,0x4a0}` / `{0x590,0x5e0,0x630,0x680}`
    match the live vendor exactly),
* **BAR2 = `0x59800000`** for the iATU: six inbound viewports (`ctrl2` at `0x104 + 0x200*i`)
  and the one outbound window at `0x000..0x018`.

The module does this through `pci_iomap(dev, 0)` / `pci_iomap(dev, 2)` on the claimed sibling, so
the offsets are the same as ep0 but the decode is the sibling's. The capture
`041_boot1_live.txt` confirms it: `BAR0 base=0x58000000`, `BAR2=0x59800000`,
`iatu[0x708] <= 0x583b8000 match=YES`. **[proven]**

### A.4 The SR ring *base* is a device VA that must go through the sibling's outbound iATU

`pcie_hostca_to_devva` @`0xaefc` implements `devva = devva_base + hostca - hostca_base` from the
window descriptor `chip->[4]->[0xc4]` (`win[0]`, `win[8]`, `win[0x10]`); the live vendor window is
`0x80000000 / 0xffffffff / 0x80000000`. `pcie_ete_sr_reg_init` @`0x14a48` converts the node
array's host address before writing the ring base:

```
0x014aa0: ldr  r2, [r4, #0xe8]        ; node array host address
0x014aac: bl   pcie_hostca_to_devva
0x014ab0: str  r0, [r5, #0x10]        ; SR+0x10 = device VA
0x014ad0: str  r1, [r2, #0x14]        ; SR+0x14 = depth-1   (bfi, [2:0] of cfg[5])
0x014adc: str  r2, [r3, #0x18]        ; SR+0x18 = producer  ([r4+0xc])
0x014af4: str  r2, [r3, #8]           ; SR+0x08[2:0] = cfg[5]
```

That device VA is only reachable if the endpoint's **outbound** ATU maps it. On the sibling the
outbound window was at reset (`BEFORE [0x004]=0`, `[0x010]=0xfff`); the module programs it via
BAR2 `0x59800000` (`devva 0x80000000..0xffffffff -> host 0x80000000`), and only then can the
device DMA to the SR/DR buffers. Programming it through ep0's BAR2 `0x41800000` would leave the
sibling RC undecoded and the ring base unreachable. **[proven]**

### A.5 Why the SR indices read desynchronised — resolved

The candidate causes are each tested:

* **Base address — ruled out.** On both boots the post-release readback is
  `base(+0x10) == want` (`0x83a58000`/`0x83dac000` on ch0; the live device never rewrote the
  base), and the module logs `BLOCK REWRITTEN BY DEVICE` count = **0**.
* **Index register offset — ruled out.** The live vendor block (dump `001_partA_live_vendor.txt`)
  has SR ring A at `+0x10` base (host `0x84573000`), `+0x14` depth `0x1f`, `+0x18`/`+0x1c`
  the index pair, and ctrl at `+0x08`; DR ring at `+0x30/+0x34/+0x38/+0x3c`. That is exactly the
  module's `ETE_SR_BASE/DEPTH/WPTR/RPTR/CTRL`, so the register was read where the vendor reads it.
* **Phase/index packing — ruled out.** `pcie_ete_ring_ptr_plus` @`0x13ef8` keeps a 10-bit index
  with the phase at bit 10 (the phase-bit fix from `sr-pump.md`); the module's fresh lap commits
  `0x400` and the post-resync commit is `0x410` (index `0x10`), consistent with the live vendor
  pair (`0x16/0x16` at rest, moving together).
* **Instance mismatch — ruled out.** Reading the same CAs through ep0's and ep1's BAR0 is
  byte-identical (`0x4003a410`, `0x4003a418`, `0x4003a41c`, `0x40039508`, `0x400392e8` all
  match), and instance 1's ring block is zero. There is only one active ETE instance.

**The real cause: the device rewrites the SR index register when its firmware boots on release.**
`sibep`'s pre-release commit read back `0x400`, but at service time the same word read `0x4`
(ch0) / `0x2` (ch1, ch2) with `rptr = 0x10`, while base/depth/ctrl were untouched. The module's
`omo_sr_outstanding()` computes `(prod - cons) mod 2*depth` on the packed pair; with
`prod = 4, cons = 16` the u32 subtraction is negative, so `free = depth - outstanding` clamps to
0 and the pump performs **no** refill — exactly the sibep `pumped=0` and the "commit low / device
0x10" pair recorded in `sibling-ep.md`. Because the base is unchanged, the device and host still
agree on *which* ring; the disagreement is only in the index arithmetic. **[proven, twice]**

### A.6 Does the SR ring have to be programmed through the sibling's BAR2 iATU?

Yes for the *ring base* (A.4): it is a device VA that only the sibling RC's outbound window
decodes. The *register window* itself (base/depth/wptr/ctrl) is programmed through the sibling's
BAR0 region-3 viewport (A.3). Both are the sibling's; neither can be done on ep0's BARs while the
sibling is the claimed function. **[proven]**

---

## Part B — `lab/sr2/sr2.c`, the mandatory self-recovery, and two boots

### B.1 The module and the one correction

`lab/sr2/sr2.c` is `lab/sibep/sibep.c` (sibling endpoint + pump + correct glue/ETE writes) plus
`omo_ete_resync_sr()` and the call to it after the release write. The correction re-asserts the
quoted program per SR channel — `SR+0x10 = hostca_to_devva(node array)`, `SR+0x14 = depth-1`,
`SR+0x18 = producer`, `SR+0x08[2:0] = cfg[5]` (`pcie_ete_sr_reg_init` @`0x14a48`) — and re-syncs
the host producer to the device's current consumer (`wptr := rptr`) so the packed arithmetic starts
from a consistent empty ring. It also logs the base on every change (`omo_svc_log_state`), so a
later device rewrite is visible. Every register written is quoted; `enable=0` (CA `0x400392f0`
never written); the only unproven-but-quoted write remains `PCI_INTERRUPT_LINE = 209`.

Built by CI run **`36983922675`**, branch `omo/phase21-sr2`, commit `e9d9618`:
`sr2.ko` 56072 bytes, md5 **`38131f96297560b74697d90157f43765`**, `vermagic=5.10.201`.

### B.2 The mandatory device-side self-recovery (armed before staging)

1. `/root/recover-sr2.sh` written from `lab/sr2/recover-sr2.sh`
   (md5 `b81383e5bfc4972f3ebf3e901a76221d`): restores both `.omo-off` files, removes the loader,
   its `rc.d` symlink, the staged module, the `/tmp` copies and itself, then `reboot`s.
2. Armed first, before anything was staged:

   ```
   start-stop-daemon -S -b -m -p /tmp/omo-sr2.timer.pid -x /bin/sh -- /root/recover-sr2.sh --watch
   ```

   verified alive in a separate SSH session: pid `14977` `ALIVE /bin/sh /root/recover-sr2.sh --watch`
   (`000_arm_recovery.txt`).
3. It cannot cross the reboot, so it was **re-armed the moment each takeover boot answered**:
   pid `7521` (boot 1), `6784` (the intermediate reboot that skipped the loader), `7400` (boot 2).
4. Cancelled cleanly with `touch /tmp/omo-sr2.done` then `/root/recover-sr2.sh`
   (`060_recovery_run.txt`).

Note: the loader removes its own `/etc/rc.d/S99omo-sr2` symlink on start (one-shot by design), so
a second sr2 boot required re-creating the symlink (`ln -sf ../init.d/omo-sr2 /etc/rc.d/S99omo-sr2`)
before rebooting. The intermediate reboot that consequently ran no module is **not** counted as an
sr2 boot; boots 1 and 2 below are both real module boots.

### B.3 Staging

`030_staging.txt`: `sr2.ko` md5 `38131f96297560b74697d90157f43765` in
`/lib/modules/5.10.201/`, `/etc/init.d/omo-sr2` + `S99omo-sr2`, `/root/recover-sr2.sh`, both
vendor modules hidden as `.omo-off` (364660 B / 3564728 B), loader and recovery `sh -n` OK.

### B.4 Boot 1 — decisive lines (`041_boot1_live.txt`)

```
omo-sr2: endpoint 0001:00:00.0 id 59e7:0005
omo-sr2: BAR0 base=0x58000000 (config space), BAR2=0x59800000 (iatu_bar1)
omo-sr2:   iatu[0x708] <= 0x583b8000 readback=0x583b8000 match=YES  (r3 base_lo)
omo-sr2: PCI_INTERRUPT_LINE <= 209 ... readback=209
omo-sr2: request_irq(209, IRQF_SHARED, "omo-sr2") rc=0 - IRQ path live
omo-sr2: RELEASE write CA 0x40000108 <- 0x00005a5a

omo-sr2: [post0 +790ms] SR ch0 post-release: base(+0x10)=0x83a58000 want=0x83a58000 depth(+0x14)=0x0000001f ctrl(+0x08)=0x00000001 wptr(+0x18)=0x00000004 rptr(+0x1c)=0x00000010
omo-sr2: [post0 +810ms] SR ch0 re-asserted base=0x83a58000 wptr:=rptr=0x00000010 ctrl(+0x08)=0x00000001 (readbacks base=0x83a58000 wptr=0x00000010 ctrl=0x00000001)
omo-sr2: [post0 +820ms] SR ch1 post-release: base(+0x10)=0x82437000 want=0x82437000 depth(+0x14)=0x0000001f ctrl(+0x08)=0x00000001 wptr(+0x18)=0x00000002 rptr(+0x1c)=0x00000010
omo-sr2: [post0 +860ms] SR ch2 post-release: base(+0x10)=0x823ab000 want=0x823ab000 depth(+0x14)=0x0000001f ctrl(+0x08)=0x00000001 wptr(+0x18)=0x00000002 rptr(+0x1c)=0x00000010

omo-sr2: [pump svc +1340ms] SR ch0 refilled 32 node(s) (wptr=0x00000010 rptr=0x00000010 outstanding=0) commit SR+0x18 <= 0x00000410 readback=0x00000410
omo-sr2: [pump svc +1360ms] SR ch1 refilled 32 node(s) (wptr=0x00000010 rptr=0x00000010 outstanding=0) commit SR+0x18 <= 0x00000410 readback=0x00000410
omo-sr2: [pump svc +1380ms] SR ch2 refilled 32 node(s) (wptr=0x00000010 rptr=0x00000010 outstanding=0) commit SR+0x18 <= 0x00000410 readback=0x00000410

omo-sr2: done (... irq=209 irq_taken=23813 irq_handled=2 msgs=4 services=4 sendflag=1
         dr_events=4 sr_events=1 pumped=96)
```

### B.5 Boot 2 — same state, reproduced (`051_boot2_live.txt`)

```
omo-sr2: [post0 +450ms] SR ch0 post-release: base(+0x10)=0x83dac000 want=0x83dac000 depth(+0x14)=0x0000001f ctrl(+0x08)=0x00000001 wptr(+0x18)=0x00000004 rptr(+0x1c)=0x00000010
omo-sr2: [post0 +450ms] SR ch1 post-release: base(+0x10)=0x83f1d000 want=0x83f1d000 depth(+0x14)=0x0000001f ctrl(+0x08)=0x00000001 wptr(+0x18)=0x00000002 rptr(+0x1c)=0x00000010
omo-sr2: [post0 +520ms] SR ch2 post-release: base(+0x10)=0x83db4000 want=0x83db4000 depth(+0x14)=0x0000001f ctrl(+0x08)=0x00000001 wptr(+0x18)=0x00000002 rptr(+0x1c)=0x00000010

omo-sr2: [pump svc +1160ms] SR ch0 refilled 32 node(s) ... commit SR+0x18 <= 0x00000410 readback=0x00000410
omo-sr2: [pump svc +1180ms] SR ch1 refilled 32 node(s) ... commit SR+0x18 <= 0x00000410 readback=0x00000410
omo-sr2: [pump svc +1200ms] SR ch2 refilled 32 node(s) ... commit SR+0x18 <= 0x00000410 readback=0x00000410

omo-sr2: done (... irq=209 irq_taken=10275 irq_handled=2 msgs=4 services=4 sendflag=1
         dr_events=4 sr_events=1 pumped=96)
```

The pre-release commit and the post-release rewrite are **identical on both boots**
(`wptr 0x400 -> {4,2,2}`, `rptr = 0x10`, base/depth/ctrl unchanged, `BLOCK REWRITTEN` = 0). The
desync is therefore deterministic, not a race. **[proven]**

### B.6 What the corrected run shows vs the ep0 srpump run

* The pump now performs the full 32-node lap on every channel (`pumped=96`) where sibep did 0.
  The ep0 `srpump` run's `pumped=64` is the same generator on a ring whose pre-release commit
  survived (ch0 32 + ch1 16 + ch2 16); the sibling's arithmetic is now equally well-posed.
* The interrupt path is unchanged and live (`irq=209`, `irq_taken` in the thousands, `irq_handled=2`).
* Everything else is as in `sibling-ep.md`: the device's D2H side (out[1] id 6 / id 2, dynamic
  glue status `0x8 -> 0x18 -> 0x8 -> 0x9`) works and the id-6 wake reaches the host.

---

## Part C — did the chip take the frame? named next blocker

**No.** With the corrected ring programming, the pump commits a fresh 32-descriptor lap on each
SR channel, and the chip still does not consume or accept it. Over both 25 s service runs:

* `out[0]` (H2D mask, CA `0x40039010`) is written only by the module's own id-3/id-5 doorbells
  (`0x08`/`0x20` alternating) and is **never cleared by the device** —
  `H2D MASK CLEARED BY DEVICE` count = **0** on both boots;
* `out[1]` (CA `0x40039014`) carries only the device's id 6 (`0x40`) and id 2 (`0x04`), acked and
  re-armed each time — **no bit 0, no id-1 "Device plat ready!" reply**; the one `ID-1 READY`
  line in each log is our own posted frame decoded before the re-sync, not a device reply;
* no `0x5a5a` buffer appears in any DR payload, and the DR device index stays at its idle baseline
  (`0x10`); `dr_events = 4` (the initial lap only), `sr_events = 1`;
* after the pump commit `SR+0x18 = 0x410`, `SR+0x1c` stays at `0x10` for the remaining 24 s —
  `SR engine read` events after the pump = **0** on both boots. The device does not consume the
  descriptors.

**Named next blocker: the device-side H2D SR consume/accept trigger.** The interrupt delivery
(`irq_taken` thousands) and the descriptor supply (96 fresh nodes, correct instance-0 base and
packed indices) are both proven, so the failure is specifically that the firmware never reads the
committed SR lap and never takes `out[0]` down. The evidence now narrows it:

* The device rewrites `SR+0x18` (its index) once at firmware boot and then leaves the block
  alone; it never advances `SR+0x1c`. So the ring is address-correct but has no consume trigger.
* The per-channel enable bits are the remaining candidates: the live vendor block has `+0x00 = 1`
  and `+0x48 = 1` (the device sets `+0x00` itself during boot; `+0x48` already reads 1), and
  `pcie_ete_sr_reg_init` derives the ctrl bits from `pcie_ete_get_chn_cfg(...)[5]`. The next
  experiment is the vendor's quoted per-channel H2D enable (`+0x00`/`+0x48`, `get_chn_cfg`
  cfg[5]) and the firmware's own `pcie_ete_h2d_isr_handle` / channel-config path, not the ring
  programming, which is now proven.
* `enable=1` (out[5] `0x400392f0 <= 8`) remains **forbidden** — the phase-20 boot 3 proved it
  hangs the chip, and neither sr2 boot touched it.

---

## Recovery and health

Recovery was cancelled with `/tmp/omo-sr2.done` and run by hand (`060_recovery_run.txt`); the
recovered boot verifies (`080_final_health.txt`):

```
2 wiphys: phy0 (vap0 Cudy-1C73 2g) / phy1 (vap8 Cudy-1C73-5G 5g), 6 interfaces
modules: hi5622v100_wifi 3387392 1 ; hi5622v100_plat 323584 3
md5 wifi e21629d226ec7de9a860a8955952d311 / plat 23660bc285393e678d5cade1c36c194b = baseline
calibration: get_2g_power_param -> [SUCC]17161605... ; get_5g_power_param -> [SUCC]00000000...
chip id: 0x34 version:0x00
leftovers: no *.omo-off, no sr2.ko, no init.d/omo-sr2, no S99omo-sr2, no /root/recover-sr2.sh,
           no /tmp/sr2.ko, no /tmp/omo-sr2*, watchdog gone
pstore: no new record (blk-0 10:41, blk-1/2 14:37 unchanged)
br-lan 192.168.10.1/24 UP
```

Pre-existing (not from this task): the dangling `/etc/rc.d/S99omo-rtmsg` symlink and the
`/root/recover-{epinit,eteinit,fwboot,fwload,hostwin,inbound,msghalf}.sh` scripts left by earlier
phase lanes are still present; they predate this phase and were not touched.

Boots this session: two sr2 takeover boots (both real module boots, both with the module log
captured) plus one intermediate reboot that skipped the loader (S99 self-removed) and one
recovery boot. No panic, no chip hang.

## Writes per takeover boot

Six inbound iATU viewports + one outbound viewport on the **sibling's** RC (host
`0x58000000`/`0x59800000`) + `PCI_COMMAND=7` + the 928,920-byte firmware (read back) + the SR/DR
program registers + the **post-release SR re-assert** (base/depth/wptr/ctrl per channel; quoted) +
the ETE interrupt-block RMW CA `0x40039508` + the glue channel-resource RMW CA `0x400392e8` + the
`0x5a5a` release + the host `out[1]`/`out[3]`/`out[4]` service words + `out[0]`/`out[2]` for the
id-3/id-5 doorbells + the SR producer commits `SR+0x18` (96 nodes across 3 channels) + the single
unproven-but-quoted `PCI_INTERRUPT_LINE <= 209`. The RC misc window `0x10161000` was never
touched; CA `0x400392f0` (`out[5]`) was never written (`enable=0`).

## Artifacts

```
lab/sr2/sr2.c                            the module (sibep + post-release SR re-sync)
lab/sr2/Makefile, omo-sr2                build + one-shot loader
lab/sr2/stage-sr2.sh                     staging (vendor modules -> .omo-off, S99 symlink)
lab/sr2/recover-sr2.sh                   self-recovery + detached watchdog
build/tmp/phase21/sr2-ko/sr2.ko          built module (md5 38131f96297560b74697d90157f43765)
build/register-dumps/sr2/000_arm_recovery.txt     watchdog install (start-stop-daemon)
build/register-dumps/sr2/001_partA_live_vendor.txt  live vendor dmesg/config/alias/blocks
build/register-dumps/sr2/010_disasm_ring_instance.txt  ETE ring/reg-init/chn_res/intr
build/register-dumps/sr2/011_disasm_endpoint_window.txt  probe/phy_devid/l1ss/hostca-msg
build/register-dumps/sr2/020_build.txt            CI run + md5
build/register-dumps/sr2/030_staging.txt          staging log
build/register-dumps/sr2/040_first_contact.txt    re-arm inside the takeover boot
build/register-dumps/sr2/041_boot1_live.txt       boot 1 dmesg | grep omo-sr2
build/register-dumps/sr2/042_boot1_irq_params.txt boot 1 /proc/interrupts + params
build/register-dumps/sr2/043_boot1_full_dmesg.txt boot 1 full dmesg
build/register-dumps/sr2/050_boot2_live.txt       (intermediate reboot that skipped the loader)
build/register-dumps/sr2/051_boot2_live.txt       boot 2 dmesg | grep omo-sr2
build/register-dumps/sr2/052_boot2_irq.txt        boot 2 /proc/interrupts
build/register-dumps/sr2/053_boot2_full_dmesg.txt boot 2 full dmesg
build/register-dumps/sr2/070_decisive.txt         both boots' decisive lines
build/register-dumps/sr2/060_recovery_run.txt     recovery run
build/register-dumps/sr2/080_final_health.txt     recovered health
```
