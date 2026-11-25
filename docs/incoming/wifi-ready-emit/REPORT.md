# Ready-emission path: why the firmware never emits HCC g4 id1/id2 - and what host action (if any) gates it

**Lane:** ready-emit (shared task `task-80`), **host-only**, 2026-11-23.
**Status:** static reversal complete from the local firmware image + the vendor 2.5.24 module
pair; one read-only bench decision pack written; **no device contact, no image build, no vendor
binary/firmware byte committed**. Inputs (local only, never copied):

| artifact | identity |
| --- | --- |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B, sha256 `7fc87e20...ef311b` |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko` (2.5.24) | 364,660 B |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko` (2.5.24) | 3,564,728 B |

Conventions (unchanged): firmware **CPU runtime R = file offset + 0x40000**; host BAR0 for a
RAM object **R = 0x6B8000 + R** (region-5 window); device IO CA **0x40000000..: BAR0 =
0x3B8000 + (CA - 0x40000000)** (region-3); PKTRAM CA region-2: **BAR0 = 0x1D8000 + (CA -
0x01000000)**. Evidence markers: **[V]** verified this session in the named artifact (fresh
disassembly / decoded structure); **[V\*]** bench-measured by an earlier attended lane, cited;
**[I]** inference; **[U]** unknown.

Read-first companions: `../wifi-ete-frames/REPORT.md` (ready frame wire form, bit6 correction),
`../wifi-boot-gate/REPORT.md` (boot-task post-send chain, gate B), `../wifi-hcc-re/REPORT.md`
(ETE register map, D2H service order), `../wifi-fw-ready-gate/REPORT.md` (0x86E14 decode,
bench history), `../wifi-fw-link-event/REPORT.md` (event dispatcher, ROM IRQ), and
`../wifi-transport-port/b3/` (instrument).

---

## 0. TL;DR - the five answers

1. **Who calls `d2h_notify`, and under what conditions.** The image calls it from exactly two
   sites **[V]**: the **D2H deposit op** (CPU 0xC61E0, ops-table slot +0x10) passes **id 6**
   after it has built and committed an 8-byte node, and the **SR receive drain** passes **id 5**
   after consuming an H2D frame. The primitive (CPU 0xC6170) itself requires: chip arg 0, id
   ≤ 0x0A, a live `g = *(CA 0x172130)`, and "not already pending"; it publishes to `out[1]`
   only when the in-flight latch `g+0xB4` is 0, otherwise the bit is deferred in the shadow
   `g+0xB8` and later flushed by CPU 0xC6108 after `out[1]` reads 0 **[V]**.
2. **What actually emits the ready frames.** The boot task builds g4 id1/id2 and hands them to
   the message layer (`0x102760`, enqueue at CPU 0xC2662) **[V]** (prior lanes). A queued HCC
   message becomes a **device-source-ring node** via the ETE ops slot +0x10 = CPU **0xC61E1**
   (file 0x861E0): it builds `{word0 = buffer device VA, word1 = (len<<16)|0x6D2B}`, commits it
   into the firmware's D2H source ring (CPU 0xC192A), then `d2h_notify(chip0, 6)` **[V]**. The
   bench's `out[1]` bit6 at ~0.4 s can only be produced by this path **[V/I]**, so a ready-path
   message **was** staged into the device-side D2H source ring.
3. **The publish semantics that matter.** Each ETE channel has two rings: **group A (device
   source ring)** = firmware-owned, base in PKTRAM (`0x01060440+i`, nodes at `+0x100`), written
   by the deposit op; **group B (host destination ring)** = host-owned base/depth/published
   (+0x30/+0x34/+0x38), whose **fill (+0x3C) is advanced by the ETE engine**, never by the
   firmware. The device drives that source→host copy from its own ETE status/interrupt layer:
   status base CA **0x4003A86C**, status word **+8 = 0x4003A874**, bits 16-20 dispatched to the
   firmware "sending trigger" CPU **0xC625A** (installed at `hcc_instance+0x1C`) **[V]**. No
   host register feeds that layer; the host only supplies the group-B destination ring and the
   pre-posted free buffers.
4. **The 0x86E14 contract.** It is **not** an H2D-arrival handler: it is the non-reset
   startup/vector handshake (entered through the four thunks/veneers, in our case as a normal
   call - saved return address is CPU 0x88BBD **[V\*]**). It stages the vector context into
   `CA 0x40008..0x4004C`, masks its ETE status register, **zeroes halfwords CA 0x4000010C and
   0x40000108** (why a pre-placed 0xcece is wiped), sets flag bits at rt 0x17DF78, stores
   `out[1] = 4` and pulses D2H doorbell CA 0x40101434, then spins for the 32-bit value
   `0x0000CECE` and returns through the lock at rt 0x17DF44 **[V]**. The `out[0]` bit3 ack is
   the separate **H2D dispatcher** CPU 0xC18AC (`out[5]=1`, `out[0]=0`, `out[2]=8`), which
   never ran in our state **[V]/[V\*]**.
5. **The bench answer.** There is **no host-visible action that gates the ready frames.** All
   host-owned preconditions (SR0-2 + DR0-3 rings, pre-posted free buffers, `0xcece` service,
   `out[1]` ack/drain/re-arm) are already provided, and every step from "ready message built"
   to "`d2h_notify(6)`" runs in our boot. The stall is below that, in the device-side ETE
   source→host copy / status path, which the host cannot write. The only untested host-visible
   delta against the vendor probe is the host-facing ETE interrupt-mask write at CA 0x40039508
   (vendor ANDs 0xFFE0F8F8; B3 never writes it) - and it is not read by any firmware code **[V
   negative]**, so it is not expected to be the gate. **Single next bench action:** the
   read-only snapshot pack in section 7 (`tools/bench-reads.py`), which decides stage S1/S2/S3
   in one shot.

---

## 1. Method and tooling

* New tools in this lane read the firmware blob in place and print derived annotations only:
  `tools/readyemit.py` (`chain`, `records`, `ops`, `bar0`, `selftest` - the selftest asserts
  every fact quoted here) and `tools/bench-reads.py` (the read-only bench pack with BAR0
  mapping). `tools/fwdis.py` is the boot-gate lane's recursive-descent disassembler, reused
  unchanged (copied read-only for reproducibility).
* Vendor module facts come from fresh `objdump -d` of the local 2.5.24 `hi5622v100_plat.ko`
  (`.text`-relative addresses `sh_addr=0`, same convention as every prior lane) and are cited
  as `plat 0xNNNN`.
* Nothing below reproduces a firmware or module byte; only addresses, mnemonics, decoded fields
  and derived structure layouts.

---

## 2. The verified ready/deposit/notify chain (fresh reversal)

### 2.1 Boot task: state, id1 send, gate B (re-linked, not re-derived)

Prior lanes established [V\*]: the boot task CPU 0x47628 runs; its state byte CA 0x118B60
advances; the id1 builder CPU 0x47D28 queues its message through CPU 0x102760; the post-send
chain writes the gate-B words (CA 0x1714A0 = 0x001261D8). The Lead's current state has **gate B
open**, which by the boot-gate lane's own decision table means the pass reached CPU 0x49204 -
i.e. the id1 send returned 0 and everything up to the per-record body ran **[V\*** +
`../wifi-boot-gate/REPORT.md` §2/§6]. This lane's job starts downstream of that.

### 2.2 The firmware ETE ops table (fresh [V])

Firmware data at **CPU 0x10CEDC** is the per-chip0 ETE ops table installed into
`hcc_instance+0x18` by `pcie_msg_init` (CPU 0x49334) at CPU 0x49356 **[V]**:

| slot | value | role |
| --- | --- | --- |
| +0x00 | `0x4003A000` | ETE register-block base CA |
| +0x04 | `0x40039508` | ETE host-facing interrupt word CA |
| +0x08 / +0x0C | `0xC1875` / `0xC186F` | stubs returning -1 |
| **+0x10** | **`0x000C61E1`** | **D2H deposit op (CPU 0xC61E0)** |
| +0x14 | `0xC1859` | "return buffer" node fill (writes word0 only) |
| +0x18 / +0x1C | `0xC1867` / `0xC184F` | node size = 8 / get node word0 |
| +0x20 +0x24 | `0xC1843` / `0xC1835` | get node len / node flag |
| +0x28 | `0xC182B` | get word0 from second ring pointer |
| +0x2C +0x30 | `0xC186B` / `0xC1821` | node size / get word0 from third ring pointer |
| +0x34..+0x40 | `0xC17F9`/`0xC17ED`/`0xC1813`/`0xC1803` | addr / len / set flag accessors |

So the D2H "send one frame" operation is an **indirect call through ops+0x10** - which is why a
plain BL caller scan finds no callers of CPU 0xC61E0 **[V negative]**, and why the call site
cannot be pinned without resolving the tx-port dispatch [U].

### 2.3 The deposit op CPU 0xC61E0 (fresh [V])

Signature: `(chip_obj, ring_ctx, buffer_dev_va, len, present)`; `present` is on the stack.

1. builds the 8-byte node on its own stack: `word0 = buffer_dev_va`, `word1 = (u16 len << 16)
   | flags`, where `present != 0` ORs bits 14|13|0x0D2B (= `0x6D2B`) else bits 14/13 from the
   argument (CPU 0xC61E6-0xC621A) **[V]**;
2. calls **CPU 0xC192A(ring_ctx, &node)** - the ring commit helper:
   `node[producer&0x3FF]` at `[ctx+0]`, then advance the packed index at `*[ctx+8]` using the
   depth byte at `[ctx+8]+4` via the phase-bit helper CPU 0xC1906 **[V]**;
3. calls **CPU 0xC6170(publish_chip, 6)** where `publish_chip = [chip_obj+0x58]` = 0 for chip0
   **[V]**;
4. bumps the per-ctx tx counter `[ring_ctx+0x340]` **[V]**.

### 2.4 `d2h_notify` CPU 0xC6170 - the exact contract (fresh [V])

```
  if id > 0x0A -> return 0x8B2D
  if chip_arg != 0 -> return 0x8B2D
  g = *(CA 0x00172130); if !g -> return 0x8B2D
  lock g+0xC0
    if (g+0xB8 shadow >> id) & 1 -> return 0x8B2D        (already pending)
    shadow = g+0xB8 | (1<<id); store g+0xB8 = shadow
    inflight = g+0xB4
    if inflight != 0:                                    (a word is in flight)
        unlock; return 0                                 (stays deferred in the shadow)
    else:
        *(g+0x9C) = shadow        ; out[1] CA 0x40039014 = shadow   PUBLISH
        g+0xB4    = shadow        ; in-flight latch
        g+0xB8    = 0             ; shadow cleared
        *(g+0xA4) |= 1            ; D2H doorbell CA 0x40101434 bit0
        unlock; return 0
```

`g` is the message object reached through CA 0x172130 (`0x0010C0F4` in this image) **[V\*]**;
its mailbox pointer slots are installed by `pcie_msg_init` (CPU 0x496FE..0x49788 in the same
function: `out[1]` 0x40039014, `out[0]` 0x40039010, D2H doorbell 0x40101434, `out[5]`
0x400392F0) **[V]**.

### 2.5 The flush routine CPU 0xC6108 (fresh [V])

Called through a function pointer when a D2H word must be retired:

1. writes 1 through the pointer at `[g+0xB0]` (visible in the bench value list as the only
   "one-shot" write in this path) and busy-waits 0xFFFF iterations;
2. **spins until `*out[1] == 0`** - this is the P2 drain precondition that `../wifi-hcc-re`
   §2.5 documented at file level;
3. takes `g+0xC0`, copies `g+0xB4 = g+0xB8`, and if the shadow is non-zero publishes it to
   `out[1]` + doorbell and clears the shadow.

### 2.6 The decisive bench inference (why bit6 is already a big step)

`out[1]` bit6 can only be produced by a `d2h_notify(...,6)` call, and the only in-image caller
that passes 6 is the deposit op's tail **[V]**. The Lead's bench saw bit6 at ~0.4 s after release
**[V\*]**. Therefore: **the ready-path message reached the ETE deposit op, the 8-byte node was
committed to the device source ring, and the host was correctly notified.** Everything that
remains missing is the device-side source→host copy (section 3).

---

## 3. ETE TX/DR publish semantics (fresh [V] + prior [V])

### 3.1 Two rings per channel

| ring | owner | registers | direction |
| --- | --- | --- | --- |
| **group A** | **firmware** | +0x10 base, +0x14 depth-1, +0x18 producer, +0x1C consumer | device source ring (in PKTRAM) |
| **group B** | **host** | +0x30 base, +0x34 depth-1, +0x38 published free index; **+0x3C fill = device** | host destination ring (host DRAM) |

The vendor module's `pcie_ete_dr_reg_init` (plat 0x1483C) writes only group B **[V]**:
`+0x30 = devva(node array)`, `+0x34 = depth-1 (bfi 0..9)`, `+0x38 = ctx[+0x1C]`. The
firmware's `pcie_msg_init` writes **group A** on the four channels 0x590/0x5E0/0x630/0x680:
`+0x10 = word[R]` (device staging buffer), `+0x14 = depth-1`, `+0x18 = 0`, `+0x08 = cfg[5]&7`,
then sets the channel enable `+0x00 |= 1` (CPU 0x4941A..0x49496) **[V]**. The two sides
therefore never write the same ring; the engine copies A→B.

### 3.2 Firmware D2H channel records (fresh [V])

`hcc_instance+0x14 = CA 0x0010F2B8` is the first of four records, stride **0x354**, index 0..3
= channels 0x590/0x5E0/0x630/0x680:

| idx | record | word[0] staging | node array (R+0x14, runtime = word0+0x100) | chan CA (R+0x31C) | send CA (R+0x32C) |
| ---: | --- | --- | --- | --- | --- |
| 0 | 0x10F2B8 | 0x01060440 | 0x01060540 | 0x4003A590 | 0x4003AC00 |
| 1 | 0x10F60C | 0x01060330 | 0x01060430 | 0x4003A5E0 | 0x4003AC04 |
| 2 | 0x10F960 | 0x01060220 | 0x01060320 | 0x4003A630 | 0x4003AC08 |
| 3 | 0x10FCB4 | 0x01060110 | 0x01060210 | 0x4003A680 | 0x4003AC0C |

The node array is device PKTRAM, not host DRAM **[V]**. `pcie_msg_init` also initialises
`R+0x10` (progress index) from the **low 16 bits** of the send register at R+0x32C, and stores
`hcc_instance` at `R+0x334` **[V]**.

### 3.3 The ETE +0xC00 send/flag registers (fresh [V])

The vendor host knows four per-DR register addresses `0x4003AC00+4i` (`pcie_ete_init_dst_ring`
stores them in its DR context at ctx+0x5C; plat 0x72d0/0x7354) **[V]**. The firmware reads them
as follows:

* `pcie_msg_init`: `R+0x10 = u16(*(R+0x32C))` (initial progress index) **[V]**;
* sending trigger CPU 0xC625A: `target = (*(R+0x32C)) >> 16` and then it pumps nodes/queues
  between `R+0x10` and `target`, writing `u16 target` to `[R+0x14]` as a handshake and
  re-reading it (CPU 0xC6292-0xC62A4) **[V]**.

Neither module writes those registers; they are device/hardware-authored state ("sending
count/flag"), and **the host cannot and does not need to write them**.

### 3.4 The ETE status word and the sending trigger (fresh [V])

`hcc_instance+0x0C = CA 0x4003A86C` (ETE status base; the firmware masks it with
`0xE0E0F8F8` in `pcie_msg_init`, CPU 0x496B0-0x496FC) **[V]**. The D2H/L2 dispatcher CPU
0x4624C takes `[hcc_instance+0xC]`, reads `[base+8] = CA 0x4003A874`, and for each set bit:

* bits 16-20 -> calls `[hcc_instance+0x1C]` = the **sending trigger 0xC625A** (channel index);
* bits 24-31 -> calls `[hcc_instance+0x24]`;

and at the end posts a `d2h_notify(...,3)` ("ETE transfer done") through the same object
**[V]**. `pcie_msg_init` registers both D2H chip callbacks through 0xC74B0 (CPU 0x496C8-
0x496F0): slot **0x2D = CPU 0x462F8**, slot **0x2E = CPU 0x4624C**; separately it registers the
event codes **0x4C -> wrapper CPU 0x40294** (loads CA 0x10C190 = the g+0x9C sub-object and tail-
calls the H2D dispatcher 0xC18AC) and **0x4E -> wrapper CPU 0x402A0** (loads g = 0x10C0F4 and
tail-calls the D2H flush 0xC6108) **[V wrappers; event-code mapping from
`../wifi-fw-link-event` §6]**. The trigger's tail wakes the owning task via CPU 0xC5128 **[V]**.

**Consequence:** the D2H source→host copy is started by a **device-internal status/interrupt
event**, not by a host register write. The host-visible side of the contract is only: keep the
group-B ring armed and free buffers published, and service `out[1]` so the P2 wait does not
stall. Both are satisfied in the Lead's runs.

### 3.5 What the host can read back (the new observables this lane adds)

* `R+0x344` per record: **sending-trigger invocation counter** (incremented at CPU 0xC628A
  before the pump). 0 on all four = the trigger never ran **[V]**.
* `R+0x34C`: released-node counter (CPU 0xC62E8-0xC62F2) **[V]**.
* `ETE 0x4003A874`: the device D2H status word itself **[V]**.
* `ETE 0x4003AC00+4i`: target/progress halves of the send registers **[V]**.

These four reads are the crux of the bench pack in section 7 and have not been taken on this
bench before.

---

## 4. The 0x86E14 contract, corrected

### 4.1 What it is

Four stubs (file 0xC3114/0xC30E8/0xC30BC/0xC3090) enter through four tail thunks
(file 0x86FC8/0x86FD6/0x86FE0/0x86FEA) that adjust the staged return PC and call the common body
at CPU 0xC6E14 with a core index; the stubs are targets of the image arm/vector table
**[V\*, `../wifi-fw-ready-gate` §10.1]**. In the bench's reachable state the entry is a
**normal call** (saved lr = CPU 0x88BBD, stale SVC SPSR) **[V\*]** - i.e. it can be reached from
firmware boot code, not only from a real exception.

Verified body actions (fresh byte checks this session plus the prior lane's ordered decode):

1. ORs `1<<core` into the halfword at CA 0x40008 (vector-seen mask; bench = 0x08) and stages
   the 17-word context at CA 0x4000C..0x4004C **[V\*]**;
2. masks/reprograms its ETE status block and PCIe glue, sets the flag bits in rt 0x17DF78
   (bench = 0x0F) **[V\*]**;
3. **zeroes CA 0x4003022C, halfword CA 0x4000010C and halfword CA 0x40000108** (CPU
   0xC6E76-0xC6E80) **[V]** - this is why a pre-written `0xcece` or release word is wiped;
4. stores `out[1] = 4` (a **store**, not an OR) and pulses D2H doorbell CA 0x40101434 (CPU
   0xC6F54-0xC6F66) **[V]**;
5. spins until `*CA 0x4000010C == 0x0000CECE` (32-bit compare, CPU 0xC6F74-0xC6F7C) **[V]**;
6. returns through the lock at rt 0x17DF44 (tail call CPU 0xFDF72) **[V\*, fresh byte cross-check]**.

### 4.2 What it is not

* It is **not the H2D-arrival handler** and it does not ack `out[0]`; the H2D ack is CPU
  0xC18AC (section 6). The `out[1]=4` word is the platform-up/startup announce, not a response
  to any host message.
* It does **not** send the ready frames. Those are built by the boot task (section 2) and are
  emitted over the D2H data path.

### 4.3 Host obligations that remain valid

Serve 32-bit `0x0000CECE` at CA 0x4000010C **post-release and while the entry is spinning**
(every entry zeroes it; a single pre-placed write is destroyed), and keep the normal
`out[1]` ack/drain/re-arm service running (`out[3]=1`, read+`out[1]=0`, `out[4]=1`; never
write out[5]). The Lead's current service does this **[V\*]**.

---

## 5. Vendor ETE TX queue handling (fresh disassembly, plat 2.5.24)

| vendor symbol | address | contract relevant here |
| --- | --- | --- |
| `pcie_ete_init_src_ring` | 0x7068 | maps 3 SR channels; stores `ctx+0xEC = ETE+0xC10+4i` (SR send/flag regs) and `ctx+0xDC = channel reg VA`; allocates node buffers; `pcie_ete_sr_init` |
| `pcie_ete_sr_reg_init` | 0x14A48 | requires `ctx+0xDC` and `ctx+0xEC`; **reads high16 of the +0xC10 register and rejects the init as "already sending" if non-zero**; writes SR +0x10 base, +0x14 depth-1, +0x18 producer, +0x08 ctrl |
| `pcie_ete_init_dst_ring` | 0x727C | for channels 3..6: allocates a buffer, stores `ctx+0x50 = channel reg VA` and `ctx+0x5C = ETE+0xC00+4i`, then `pcie_ete_dr_init` -> `dr_reg_init` group B writes |
| `pcie_ete_dr_reg_init` | 0x1483C | group B: +0x30 base (hostca->devva), +0x34 depth-1, +0x38 publish = ctx[+0x1C] |
| `pcie_ete_tx_queue_handle` | 0x155D4 | the H2D choke point; stamps tag 0x5A5A at 0x15760-0x15774, uses header total length, calls SR fill + announce id 3 |
| `pcie_ete_sending_trigger` | 0x13F90 | builds/stages H2D HCC skbs, sets tag/len, then under a spinlock at 0x14420-0x1443C writes **+0x38 = ctx[+0x1C] and +0x18 = ctx[+0x28]** through the ctx+0x50 channel-register pointer (publish/kick pair) |
| `pcie_ete_dr_get_uploadbuf` | 0x1515C | host D2H receive: requires `ctx+0x50` and `ctx+0x5C` non-NULL, reads the fill u16 through a ctx pointer and computes released nodes with the 0x400 phase bit |
| `pcie_ete_intr_init` | 0x7528 | host-facing ETE interrupt word: maps CA 0x40039508 and ANDs it with **0xFFE0F8F8**; installs chip callbacks |

The vendor TX path's own notion of "send" is: fill SR nodes (H2D) or republish the DR indices
(+0x38/+0x18) under lock; it never touches the device's +0xC00 registers and never pokes the
device status word. In other words, **the vendor host does not have a private "kick" that B3 is
missing** for the D2H copy - the copy is still a device-side act, driven from 0x4003A874.

---

## 6. B3 P0 vs the firmware/vendor expectation

Comparison of `../wifi-transport-port/b3/pciet_b3.c` (`b3_arm_sr` / `b3_arm_dr`,
lines 375-416) with the contracts above **[V source read]**:

| item | firmware expects | B3 P0 does | verdict |
| --- | --- | --- | --- |
| SR0-2 group A base/depth/producer/ctrl | host-owned | writes base=node DMA, depth-1, prod=0, ctrl=0 | OK for the ring; **ctrl is 0** whereas vendor writes cfg[5]&7 (vendor SR ctrl byte). D2H path unaffected **[I]** |
| DR0-3 group B base/depth/published | host-owned | writes base=node DMA (0x8xxxxxxx window enforced), depth-1, pub=0x400/32 pre-posted | matches vendor group-B contract **[V]/[I]** |
| DR pre-posted node word0 = buffer device VA, word1=0 | host-owned | yes, then wmb before publishing | matches |
| ETE +0xC00/+0xC10 send/flag registers | device-owned, read by firmware | **never touched** | correct - host must not write |
| ETE +0x00 enable / DR +0x08 ctrl / +0x1C / +0x3C | firmware-owned | read-only | correct |
| ETE host-facing interrupt word CA 0x40039508 | vendor ANDs 0xFFE0F8F8 at probe | **B3 never writes it** (live value 0x3F201818) | the one untested host-visible delta; not read by firmware code, so not expected to gate anything **[V negative]** |
| D2H ack/drain/re-arm (`out[3]/out[1]/out[4]`) | host must clear P2 window | yes, per event | matches |

**Conclusion of the comparison:** B3 replicates every host-owned field the firmware actually
consumes; the only difference is the host-facing interrupt mask, which is a host-receive setting.

---

## 7. The bench decision pack and the single next action

### 7.1 Sequence (READ-ONLY; one fresh cold boot; no new writes)

Run the standard B3 sequence to the first events (cold boot -> prepare -> upload -> verify ->
P0 -> release with the sustained `0xcece` service -> D2H service with ack/drain/re-arm). When
`out[1]` first shows **0x40 (bit6)** and has been drained, take the pack:

1. `tools/bench-reads.py` snapshot at t ~= 2 s after the first bit6 (through the existing
   sysfs/physdump observability path; **read-only, no writes**);
2. `cat b3_p1_events b3_p2_result b3_p3_hcc b3_rings b3_p4_state status` at the same time;
3. the same pack again ~10 s later (steady state), then power-cycle.

The pack's rows are printed with BAR0 offsets by `tools/bench-reads.py`; the decisive rows:

| read | CA | BAR0 | decides |
| --- | --- | --- | --- |
| DR0..DR3 record `+0x344` | 0x10F5FC / 0x10F950 / 0x10FCA4 / 0x10FFF8 | 0x7C35FC / 0x7C3950 / 0x7C3CA4 / 0x7C7FF8 | has the firmware **sending trigger** ever run? |
| ETE status word | 0x4003A874 | 0x3F1874 | did the device's D2H status/interrupt layer fire? |
| ETE send regs | 0x4003AC00/04/08/0C | 0x3F2C00/04/08/0C | target vs progress index per channel |
| DR0..3 fill +0x3C | 0x4003A5CC/61C/66C/6BC | 0x3F25CC/261C/266C/26BC | did the source->host copy happen? |
| g in-flight/deferred | 0x10C1A8 / 0x10C1AC | 0x7C41A8 / 0x7C41AC | did P2/flush keep up with notify? |
| DR0 source node word1 (PKTRAM) | 0x01060544 | 0x238544 | was a node actually staged in the device source ring? |

### 7.2 Decision matrix

* **S0** - any aperture/mapping row wrong: fix the reads first (no conclusion).
* **S1** - all four `R+0x344 == 0` **and** ETE 0x4003A874 bits 16-20 == 0: the D2H sending
  trigger never ran; the device ETE status/interrupt dispatch is idle. No host writable input
  exists in that path - this is the definitive negative; the next work is the off-image
  ROM/init-table stage (and the vendor golden-boot register diff), not more host writes.
* **S2** - some `R+0x344 != 0` but no staged source node and `R+0x10 == 0`: the trigger ran but
  had nothing eligible; the break is upstream in the firmware HCC TX queue path; compare
  `g+0xB4/g+0xB8` and the `out[1]` history.
* **S3** - a staged source node is present (word1 low bits 0x6D2B) or `DR+0x10` advanced, while
  all host fill (+0x3C) are static: nodes are staged but the ETE source->host copy did not run.
  Only in this branch is the host-owned destination side the suspect; then the one benign write
  class to try in a *later* session is a **single re-publish of DR0..DR3 +0x38 with their current
  value** (host-owned field; vendored "sending trigger" does exactly this under lock), followed
  by this same pack.

### 7.3 The single next bench action

**Fresh cold boot + the standard B3 sequence, then the read-only pack above (section 7.1) at
first bit6 and at +10 s.** Expected decisive observables: `DR0..3 +0x344` (trigger count),
ETE 0x4003A874, and the per-channel fill/send-register pairs. If S1: the ready-frames negative
is definitive and the next lane is ROM/init-table + golden-boot diff, not a bench write.

---

## 8. What would close it

1. **The read-only pack (section 7)** - decides S1 vs S2 vs S3; that is the immediate close.
2. **Golden vendor-boot register diff** - the Lead already has a working 2.5.24 pair-A trace
   (chip mask 0, ready waits complete). Snapshot the *same* nine reads on the vendor boot
   (DR+0x38/+0x3C, +0xC00 registers, 0x4003A874, R+0x344, g+0xB4/+0xB8) and diff: if S1, the
   vendor boot must have non-zero 0x4003A874 activity while ours is silent, which pins the
   missing stage to the off-image ROM/init-table (event/IRQ delivery) - the only remaining
   conclusion consistent with `../wifi-fw-link-event` (image IRQ trampoline at CPU 0x40064 has
   no in-image caller; the dispatcher 0xC2EFC is ROM-driven).
3. **S3 only:** the single re-publish experiment (section 7.2) - one attended session, one write
   class, then the same pack.

---

## 9. Corrections to premises carried into this task

1. **"0x86E14 is the H2D-arrival handler that posts out[1]|=4 and spins for 0xcece"**: it is the
   non-reset startup/vector handshake; it **stores** out[1]=4 (platform-up), zeroes gate/release
   CAs, and returns after the spin. The H2D ack path is CPU 0xC18AC **[V]**.
2. **"firmware never writes DR registers"** (prior negative) is too broad in one respect: the
   firmware *does* write the **group A** device-source fields (+0x10/+0x14/+0x18/+0x08) and the
   enable, on channels 0x590-0x680, in `pcie_msg_init`; it never writes group B (+0x30/0x34/0x38).
   This lane's "two rings per channel" model reconciles the bench facts **[V]**.
3. **"out[1] bit6 = a request for H2D frames"** (one late ledger reading): instruction-level, the
   only in-image caller passing id 6 is the D2H deposit op **[V]**. The vendor handler name
   (`pcie_trigger_ete_sending_handle`) describes the host's reaction (wake worker), not a
   required H2D reply. The ledger's following entry already reaches the same correction.
4. **"out[0] not acked because the announce is wrong"**: the acker is CPU 0xC18AC, reached only
   through the firmware's event/interrupt dispatch; its absence is the same class of idle
   device event layer as the ready-frame stall, not a missing host mailbox write **[V]/[I]**.
5. **"out[1] bit2 might be panic"** (interim ledger hypothesis): the store site is a literal
   `out[1] = 4` in the startup handshake (CPU 0xC6F54-0xC6F5A) **[V]**, and no vendor handler
   is registered for D2H id 2 (`../wifi-hcc-re` §5.3); treat it as the platform-up announce.

---

## 10. Evidence table (new claims of this lane)

| # | claim | artifact / method | grade |
| ---: | --- | --- | --- |
| E1 | firmware ETE ops table at CPU 0x10CEDC: `0x4003A000`, `0x40039508`, ..., +0x10 = 0xC61E1 | `readyemit.py chain/ops`; fresh disasm + data dump | [V] |
| E2 | D2H deposit op CPU 0xC61E0: node `{word0, (len<<16)|0x6D2B}`, commit via 0xC192A, notify(0,6) | fresh disasm 0xC61E0-0xC6254 | [V] |
| E3 | `d2h_notify` has exactly two callers: 0xC6224 (id6 deposit) and 0xC6998 (id5 SR drain) | whole-image BL scan (`readyemit.py chain`) | [V] |
| E4 | `d2h_notify` contract: id<=10, chip0, g=*(0x172130), shadow g+0xB8, in-flight g+0xB4, publish to g+0x9C out[1] + g+0xA4 doorbell | fresh disasm 0xC6170-0xC61DC | [V] |
| E5 | flush CPU 0xC6108: one-shot write through g+0xB0, spin `out[1]==0`, publish deferred shadow | fresh disasm 0xC6108-0xC616C | [V] |
| E6 | four DR channel records at 0x10F2B8 stride 0x354: staging 0x01060440/330/220/110, node arrays +0x100, chan CAs 0x590/5E0/630/680, send CAs 0x4003AC00+4i | `readyemit.py records`; fresh data dump | [V] |
| E7 | `pcie_msg_init` (CPU 0x49334) installs hcc_instance fields (+0x18 ops, +0x1C trigger 0xC625B, +0x14 records, +0x5C=g, +0x0C ETE status) and programs DR group A + enable | fresh disasm; see also mem-consistency | [V] |
| E8 | sending trigger CPU 0xC625A: `target = high16(*R+0x32C)`, pump queues, counters R+0x344/R+0x34C, tag 0x5A5A check, wake via 0xC5128 | fresh disasm 0xC625A-0xC6354 | [V] |
| E9 | status dispatcher CPU 0x4624C: reads ETE 0x4003A86C+8, bits16-20 -> hcc_instance+0x1C (trigger), bits24-31 -> +0x24, posts notify(3) | fresh disasm 0x46248-0xC62A4 region | [V] |
| E10 | startup handshake 0x86E14: zeroes CA 0x4003022C and halfwords CA 0x4000010C/0x40000108, `out[1]=4` store, doorbell 0x40101434, 32-bit 0xcece spin | fresh disasm 0xC6E74/0xC6F54-0xC6F7C | [V] |
| E11 | H2D dispatcher 0xC18AC: out[5]=1, read+clear out[0], out[2]=8, dispatch ids<=9; wrappers CPU 0x40294 (H2D, arg CA 0x10C190) and 0x402A0 (D2H flush, arg g); never ran in our state | fresh disasm + Lead out[0]=0x8/out[5]=0 [V*] | [V]/[V*] |
| E12 | bench bit6 at ~0.4 s implies the deposit op executed and staged a source-ring node | E3+E4 + Lead timeline; deductive | [V]/[I] |
| E13 | B3 P0 replicates every host-owned field consumed by the firmware; only delta = host-facing intr mask 0x40039508 (never written by B3) | source read + fresh vendor disasm | [V] |
| E14 | no host register/mailbox write feeds the D2H source->host copy; it is driven from ETE status 0x4003A874 | E8+E9, whole-path search | [I], strong |
| E15 | ready frames therefore cannot be gated by any host action we can see; the open stage is the device ETE copy/status path | E12+E14 | [I], strong |
| E16 | which ring ctx is passed to ops+0x10 (the deposit call site) and the identity of its ctx[0]/ctx[8] | indirect call, no BL site; not resolved | [U] |

---

## 11. Reproduction

    PY=<repo>/.provision-venv/bin/python
    cd opensource/docs/incoming/wifi-ready-emit/tools
    $PY readyemit.py selftest        # asserts every [V] fact quoted above (51 checks)
    $PY readyemit.py chain           # ops table, notify callers, g/hcc_instance anchors
    $PY readyemit.py records         # DR source-ring records + H2D staging contexts + ETE map
    $PY readyemit.py bar0 0x10F2B8 0x4003AC00   # host BAR0 mapping helper
    $PY bench-reads.py --markdown    # the read-only bench pack printed as a table

Vendor disassembly used above (local artifacts, nothing committed):

    OD=<toolchain>/arm-openwrt-linux-muslgnueabi-objdump
    $OD -d --start-address=0x1483c --stop-address=0x14908 /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko
    $OD -d --start-address=0x727c  --stop-address=0x7490  /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko
    $OD -d --start-address=0x13f90 --stop-address=0x14568 /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko
    $OD -d --start-address=0x1515c --stop-address=0x155d4 /tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko

*No vendor binary, firmware byte, calibration value or secret is included in this document; all
addresses are derived annotations. Device untouched.*
