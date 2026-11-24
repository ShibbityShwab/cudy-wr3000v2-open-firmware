# ETE engine A->B copy: trigger chain, host-owned fields and the wake ladder (ete-wake lane)

**Lane:** `ete-wake` (shared task `task-106`), **host-only**, 2026-11-25. The bench router was
never contacted, no image was built, and no vendor byte/binary/firmware content is committed -
only derived addresses, constants and structure layouts. Local inputs (read in place, never
copied):

| artifact | identity |
| --- | --- |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B, sha256 `7fc87e2051e80b5e3935a9481d666aefb8426efe7e7bcb3ec5ede352c3ef311b` |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko` | 2.5.24, sha256 `6f2eac415dbf4d3e8e71c1abd941ea4f9d97b159674d28d5e99d4ecb0e6cd491` |

Conventions (project-wide, unchanged): firmware **CPU address R = file offset + 0x40000**; host
BAR0 for a firmware RAM object **R = 0x6B8000 + R** (region 5); device IO CA
**BAR0 = 0x3B8000 + (CA - 0x40000000)** (region 3, valid for CA 0x40000000-0x4011FFFF); PKTRAM CA
**BAR0 = 0x1D8000 + (CA - 0x01000000)** (region 2). Vendor-module facts are cited as
`plat 0xNNNN` (`.text`-relative, `sh_addr = 0`). Evidence grades: **[V]** verified in this
lane from the named artifact (fresh disassembly / decoded data), **[V\*]** device-measured by
an earlier attended lane and cited, **[I]** inference, **[U]** unknown.

Read-first companions: `../wifi-ready-emit/REPORT.md` (D2H deposit/notify path),
`../wifi-ete-engine/REPORT.md` (engine writers, ops table, dispatchers - its status-bit
mapping is corrected here), `../wifi-d2h-direct/REPORT.md` (direct reader service),
`../wifi-irq-unmask/REPORT-PD3.md` / `REPORT-PD4.md` (the working patched boot),
`../wifi-driver-clean/REPORT.md` (current clean driver), `../wifi-chip-reset/REPORT.md`
(golden vendor state read pack), and the newest `mem-entries.md` entries.

---

## 0. TL;DR - the five answers

1. **Who reads the status word, and when?** Only two image handlers: the event-0x2E handler
   **CPU 0x4624C** and the event-0x2D handler **CPU 0x462F8**. Both load
   `hcc_instance = CA 0x10F1F0`, take `[hcc+0xC] = CA 0x4003A86C`, read the status word
   `[base+8] = CA 0x4003A874`, ack the upper bits into `[base+4] = CA 0x4003A870`, and
   dispatch. The image has **no polled read and no timer/H2D/thread read** of the status word;
   a third site (CPU 0x499B6) reads it only to log an exception report. If the IRQ-vectored
   event path does not run, the firmware never looks at the status word at all. [V]
2. **What actually starts the A->B copy?** The **sending trigger CPU 0xC625A**, installed at
   `hcc_instance+0x1C` by `pcie_msg_init` (CPU 0x49576, literal `0xC625B`). It is called
   **only** by the event-0x2E handler for status bits **24-28** (one bit per DR channel 0-4),
   with `(hcc_instance, bit_index)`. It reads the **hardware-authored send word**
   `CA 0x4003AC00+4*ch`, takes `target = hi16(send word)`, bumps the per-channel trigger
   counter `record+0x344`, handshake-writes the u16 target into the channel **PKTRAM control
   word** `record+0x14 = staging+0x100` (write + read-back + compare), releases the tagged
   (0x5A5A) nodes between `record+0x10` (progress) and target, counts `record+0x34C`, sets
   `record+0x10 = target` and wakes the message object. That PKTRAM handshake is what commands
   the engine copy. [V]
3. **What makes the firmware read the status word?** A **device hardware event**: the ETE block
   raises status bits 24-28; the device event router reports it as **event 0x2E**, which the
   ROM IRQ vector delivers to the image ARM trampoline **CPU 0x40064 -> 0xC2EFC -> handler
   table CA 0x17D430[0x2E] = 0x4624C**. `pcie_msg_init` registers 0x2E (handler 0x4624D via
   registrar 0xC74B0 at CPU 0x496D6) and enables it (0xC6FF4 at CPU 0x496EE); the 0x2D/0x2E
   handlers have no BL/BLX caller in the whole image. [V]
4. **Do any host-owned fields gate it?** The host owns the ring mirrors, the host-facing ETE
   interrupt word and the mailbox service; **all of them are already vendor-exact in the clean
   driver** (intr RMW `0x40039508 &= 0xFFE0F8F8`, SR group A, DR group B base/depth, pre-posted
   DR free buffers). There is exactly **one host-owned state delta** left: the DR group-B
   **published index +0x38**. The clean driver arms it to **0x400** (32 pre-posted buffers); the
   vendor driver's own init publishes **0** (`pcie_ete_dr_init` clears `inst+0x1C`;
   `pcie_ete_dr_reg_init` writes `+0x38 = inst+0x1C`), and a working vendor boot shows
   **`fill(+0x3C) == published(+0x38)`** on every DR channel (DR0 3/3, DR3 0x404/0x404). Our
   boot shows pub=0x400 vs fill=0x10 - an out-of-phase ring. That is the E1 experiment. [V]/[V\*]
5. **Verdict + ladder.** The start condition is **device-hardware/ROM state**, not a vendor-host
   write; no host-only action is known to author the status word or the send words. But the
   engine is not wholly dead: the **SR (H2D) A->B copy does run** in our boot (SR0
   producer/consumer 1/1, send word 0x00010001 after one announce) [V\*], so only the **DR
   (D2H, device-PKTRAM -> host-DRAM) direction** is gated. The ranked ladder is: **R0**
   read-only snapshot pack; **E1a** DR `+0x38 := current fill`; **E1b** DR `+0x38 := 0`
   (vendor-init control); **E2** read-only golden-vendor ETE window diff (door/global/status/
   send words) to name the remaining device-internal state. If E1 does not wake the engine, the
   verdict is definitive and the direct group-A read remains the supported receive path.

---

## 1. Method and tooling

New tool: `tools/ete_wake.py` (host-only; reads the local blob and module in place and prints
derived annotations; never opens a device):

| command | what it does |
| --- | --- |
| `selftest` | asserts every **[V]** fact of this report from the local artifacts (52 checks) |
| `chain` | prints the full trigger chain with the two dispatcher call graphs ending in 0xC625A |
| `dispatchers` | decodes CPU 0x4624C / 0x462F8 with resolved literal pools |
| `install` | prints the `pcie_msg_init` install/registration/enable sites and their literals |
| `raise` | whole-image census of the event-raise primitive CPU 0xC6DB8 (17 sites, ids) |
| `host` | the vendor-host ETE comparison (initial +0x38=0, re-arm writes, no DR kick) |
| `bench` | the R0 read pack as a CA/BAR0 table |

Whole-image negatives are done as bounds-checked scans over every 16-bit-aligned instruction
slot in Thumb (BL/BLX immediate callers, `ldr rX,[pc,#imm]` literal homes, movw/movt pairs)
plus data-word scans for the CA constants; "no writer" therefore means "no materialisation and
no resolved access", not a grep miss.

---

## 2. The trigger chain (fresh reversal) [V]

### 2.1 hcc_instance (CA 0x10F1F0) - the anchors

| off | static value | meaning |
| ---: | --- | --- |
| +0x04 | 4 | DR channel count (trigger bound) |
| +0x08 | 3 | SR channel count |
| +0x0C | **0x4003A86C** | **ETE status/control base** (the block the dispatchers read) |
| +0x10 | 0x11035C | SR ring-record array (stride 0xCC) |
| +0x14 | 0x10F2B8 | DR ring-record array (stride 0x354, 4 channels) |
| +0x18 | runtime 0x10CEDC | ETE ops table (installed by `pcie_msg_init`) |
| +0x1C | runtime 0xC625A | **sending trigger** |
| +0x20 | runtime 0xC6802 | SR receive/drain callback |
| +0x24 | runtime 0xC1902 | **stub** (`movs r0,#0; bx lr`) |
| +0x28 | runtime 0xC18FE | **stub** (`movs r0,#0; bx lr`) |
| +0x58 | 0 | publish chip id (chip0) |
| +0x5C | runtime 0x10C0F4 | message object **g** |
| +0xB0 | {0x4003A000, 0xC28, 0} | firmware crash-dump descriptor for the ETE window |

DR record fields used by the chain: `+0x00` PKTRAM staging/node base, `+0x08` channel-config
pointer, `+0x0C` packed producer, `+0x10` **progress index**, `+0x14` **PKTRAM control word**
(= staging+0x100), `+0x31C` channel CA, `+0x32C` **send-word CA**, `+0x334` hcc_instance,
`+0x344` **trigger counter**, `+0x34C` released-node counter. [V]

### 2.2 Event-0x2E handler CPU 0x4624C - the only DR path

Decoded body (Thumb; literal pool at CPU 0x462EC = 0x10F1F0):

    r5 = 0x10F1F0 (hcc_instance)
    r3 = [hcc+0xC] = 0x4003A86C ; if 0 -> return
    r4 = [r3+8]  = 0x4003A874  (status word)
    if (status bit29): ack [r3+4] |= 0x20000000; 0x1024BC; 0xC2A54(CA 0x10CAD0/0x10CAD4 lock)
    r4 = status & 0xFFFF0000, with bits 21-23 and 29-31 stripped
    [r3+4] |= r4                      ; ack word CA 0x4003A870
    for each bit b in (status >> 24) & 0x1F:      ; bits 24..28 = DR channels 0..4
        fn = [hcc+0x1C]; if fn: fn(hcc, b)         ; -> sending trigger 0xC625A
    for each bit b in (status >> 16) & 0x1F:      ; bits 16..20
        fn = [hcc+0x24]; if fn: fn(hcc, b)         ; -> stub 0xC1902 (no-op)
    if nothing above: d2h_notify([hcc+0x58], 3)   ; "ETE transfer done"

**Correction to the working premise (and to wifi-ete-engine section 5.2):** the sending trigger
is on status bits **24-28**, not 16-20. Bits 16-20 dispatch into the no-op stub 0xC1902 (its
literal is installed into `hcc+0x24` at CPU 0x49584). [V]

### 2.3 Event-0x2D handler CPU 0x462F8 - the SR path

    r5 = 0x10F1F0; r3 = [hcc+0xC] = 0x4003A86C ; if 0 -> return
    r4 = [r3+8] (status)
    if (status bit12): ack [r3+4] |= 0x1000; 0x1024BC; 0xC2A54(CA 0x10CA08/0x10CA0C lock)
    [r3+4] |= (status & (bits 0..2 | 8..10))       ; ack word
    bits 0-2  -> [hcc+0x20] = 0xC6802(hcc, b)      ; SR receive/drain (0xCC-stride records)
    bits 8-10 -> [hcc+0x28] = 0xC18FE(hcc, b)      ; stub (no-op)

The 0x2D/0x2E handlers **never write** the status word; they only ack [base+4]. [V]

### 2.4 Sending trigger CPU 0xC625A - the copy command

    0xC625A: r6 = hcc; if (!hcc || !hcc[0x5C] || !hcc[0x14]) return
             if (ch >= hcc[4]) return -1
             rec = hcc[0x14] + 0x354*ch
             rec[0x344]++                                   ; trigger counter
             target = lu32(rec[0x32C]) >> 16                ; SEND WORD hi16 (hardware-authored)
             *(u16*)rec[0x14] = target                      ; PKTRAM control word = staging+0x100
             if (*(u16*)rec[0x14] != target) return -1       ; read-back handshake
             while (rec[0x10] != target):
                 node = ops+0x28(rec, rec[0x10] & 0x3FF)     ; tagged-node scan
                 if node[0xA] == 0x5A5A: release/account rec[0x34C]
                 advance packed progress
             rec[0x10] = target ; wake g via 0xC5128

So the *only* firmware command that starts the group A->B copy is the PKTRAM control-word
write, and it is steered by two pieces of **device-hardware state**: the status bits that
select the channel and the send word that supplies the target index. [V]

### 2.5 Registration, enable and the sole entry path

`pcie_msg_init` (CPU 0x49334) does, in order:

| site | effect |
| ---: | --- |
| 0x49576 | `hcc+0x1C = 0xC625B` (sending trigger) |
| 0x4957C | `hcc+0x20 = 0xC6803` (SR receive) |
| 0x49584 | `hcc+0x24 = 0xC1903` (stub) |
| 0x4958C | `hcc+0x28 = 0xC18FF` (stub) |
| 0x496C8 | registrar 0xC74B0(id 0x2D, level 5, handler 0x462F9) -> handler table CA 0x17D430[0x2D] |
| 0x496D6 | registrar 0xC74B0(id 0x2E, level 5, handler 0x4624D) -> handler table CA 0x17D430[0x2E] |
| 0x496E8/0x496EE | enable events 0x2D and 0x2E via 0xC6FF4 |

and the event dispatcher **0xC2EFC** (which reads the current event vector at CA 0x4016010C and
blx-es `0x17D430 + 4*index`) has exactly **one** caller in the image: the ARM `blx` at CPU
0x40094 inside the ROM-vectored exception trampoline at CPU 0x40064. Neither dispatcher has a
BL/BLX caller anywhere. **No H2D message, no host mailbox write, no timer and no firmware
thread reaches them; only a delivered hardware event does.** [V]

---

## 3. What is hardware-authored and what the image never touches [V]

Whole-image scans (all materialisation forms):

| object | CA | image writers | vendor-host writers |
| --- | ---: | --- | --- |
| ETE status word | **0x4003A874** | **none** (read by the 2 dispatchers + the crash logger) | none |
| ETE ack word | 0x4003A870 | dispatchers only (`|=` status bits) | none |
| send words DR0-3 | 0x4003AC00/04/08/0C | **none** | none |
| send words SR0-2 | 0x4003AC18/1C/20 | **none** | none |
| engine global +0x000..+0x3FF | 0x4003A000.. | **none** | none |
| engine doors | 0x4003A7C0 / 0x4003A7D0 | **none** (only data homes CPU 0x10CACC / 0x10CB94) | none |
| engine blocks | 0x4003A540/4/8, 0x4003A720/4/8 | `pcie_msg_init` bit sets only | none |
| ETE status/control base | 0x4003A86C | `pcie_msg_init` (clear bit12/29, `&=0xE0E0F8F8`) | none (host has its own 0x40039508) |
| host-facing ETE intr word | 0x40039508 | **none** (no image read either) | vendor RMW `&=0xFFE0F8F8` |

Consequence: the host cannot author the status word, a send word or the door/global state from
inside the protocol; the only host-writable ETE-adjacent word (0x40039508) is read by no
firmware code at all. [V]

---

## 4. Event-raise census: 0x2D/0x2E are hardware-only [V]

The image's software event-raise primitive is CPU 0xC6DB8 (writes `1<<(id&31)` to the two
per-event banks CA 0x40161180+4*(id>>5) and CA 0x40161280+4*(id>>5)). A whole-image BL/BLX scan
finds **17** call sites. Their arguments (decoded from the surrounding straight-line code):

| sites | event ids raised | source |
| --- | --- | --- |
| 0x49756 / 0x4975C | 0x4C, 0x4E | `pcie_msg_init` (H2D mailbox + D2H flush) |
| 0xB0CF8 / 0xB0CFE / 0xB0D36 / 0xBEE2E | 0x32, 0x53, 0x38, ... | PHY/radio interrupt blocks |
| 0xD4758 / 0xD498A / 0xD4990 / 0xD4996 / 0xD499C / 0xD4A7A / 0xD4A80 / 0xD4A86 | 0x3D, 0x30, 0x31, 0x32, ... | PHY/radio service |
| 0x46946 / 0x40620 / 0xC6E32 | table/exception-derived per-core event | entry A / exception handler |

**No site raises 0x2D or 0x2E.** The ETE completion events are therefore raised only by the
device hardware when it wants the firmware to service a channel - which also means the
"firmware trigger" cannot be exercised by any software action of ours (host or H2D message)
until the hardware asserts it. [V]

---

## 5. The vendor host comparison (what the host owns, and what it does not)

`hi5622v100_plat.ko` 2.5.24, fresh disassembly:

| vendor site | fact |
| --- | --- |
| `pcie_ete_dr_reg_init` plat 0x1483C | group B: `+0x30 = node-array devva`, `+0x34 = depth-1`, **`+0x38 = [inst+0x1C]`** (0x148B0 `ldr r2,[r4,#28]`; 0x148B4 `str r2,[r3,#56]`) |
| `pcie_ete_dr_init` plat 0x14908 | zeroes `inst+0x1C..+0x30` (0x14924 `str r6,[r1,#28]` with r6=0) before `dr_reg_init`; so the vendor's **initial published index is 0** |
| `pcie_ete_sending_trigger` plat 0x13F90 | the D2H receive/re-arm worker: under the `ctx+0x4C` spinlock it writes **`[chan+0x38] = ctx[+0x1C]`** (0x14430) and **`[chan+0x18] = ctx[+0x28]`** (0x1443C) - a *reaction* to a completed transfer, not a start |
| `pcie_trigger_ete_sending_handle` plat 0x15EFC | **not a self-branch bug stub** (correction to wifi-ete-engine section 7): the raw word is the unresolved `b` encoding; the relocated branch is a tail call to `pcie_wkup_thread` (0x1629C), which wakes the ETE worker |
| whole-.text scan | no vendor-host write to the status word, send words, engine blocks/doors/global; the only ETE CAs in data are {0x4003A000, 0x40039508} |

So the vendor host has **no private DR kick**: it programs the destination ring, re-arms it
after processing, and services the mailbox. The start remains the device-side status path. [V]

### 5.1 The one host-owned state delta

| field | our clean driver | vendor driver | working vendor boot |
| --- | --- | --- | --- |
| DR group-B base/depth | node-array devva / 0x1F | same contract | matches |
| DR group-B **published +0x38** | **0x400** (`prepost=32`, packed index 0 phase 1) | **0** at init (`inst+0x1C` zeroed) | **equals fill** (DR0 3 = 3, DR3 0x404 = 0x404) |
| DR group-B fill +0x3C | read-only, reset-class **0x10** | read-only | advanced by the engine |

Our boot therefore presents an **out-of-phase destination ring** (pub=0x400, fill=0x10) that the
engine never consumes, while a working vendor boot keeps `published == fill` on every channel.
This is the only host-owned field that differs in state, so it is the first ladder rung. Whether
it is *sufficient* to wake the engine cannot be decided statically ([I]); the other named
device-internal candidates (engine global/door state, status-bit raising) are not host-writable
and are only separable by the read-only pack. [V]/[V\*]

---

## 6. Engine state: what "idle" means, and the SR asymmetry [V\*]

Observed on our clean-room golden boot (PD3/PD4 patched firmware + clean driver, from the ledger
sessions of 2026-11-24/25):

| observable | our boot | vendor golden boot |
| --- | --- | --- |
| handshake page CA 0x1BF000 | 0x86807FEF / 7 / 2 / 0xABCD | 0x86807FEF / 7 / 2 / 0xABCD |
| state CA 0x118B60 / mask CA 0x40008 | 1 / 0 | 1 / 0 |
| ETE global CA 0x4003A000 | 0x10A | 0x10A (same) |
| engine blocks 0x4003A540 / 0x4003A720 | 1 / 1 | 1 / 1 (same) |
| door CA 0x4003A7C0 | invalid-looking (0x8CAC4ECA pre-GP; 0x6189210A after one GP1 boot) | **0x83FAC000** (4 KB-aligned host-DRAM pointer; 0x83FD4000 in an older dump) |
| door CA 0x4003A7D0 | 0 | 0 |
| ETE status 0x4003A874 / ack 0x4003A870 | 0 / 0 | 0 / 0 (sampled idle) |
| DR send words 0x4003ACxx | 0 | **live** (DR0 0x041E041E, DR3 0x00050005) |
| DR fills +0x3C | 0x10 (reset-class) | **live and equal to published** |
| DR trigger counters record+0x344 | 0 | [U] not captured |
| **SR0 after one H2D announce** | **prod 1 / consumer 1 / send word 0x00010001 -> the engine copied** | live |

Two facts matter: (a) the ETE **engine is capable of running in our boot** - the H2D (SR)
A->B copy demonstrably ran, the SR consumer and send word advanced; (b) only the **D2H (DR)**
direction is gated, and the only observed state differences are the destination-ring
published/fill relationship and the engine door value. The door is not host-authored by the
vendor stack and its per-boot value is a host-DRAM-window pointer, so it is most plausibly
ROM/firmware-initialised engine state ([I]); it must be *read*, not guessed. [V\*]/[I]

---

## 7. Host-owned enablers compared against the task list (all evaluated)

| candidate host enabler | owner | already vendor-exact? | can it wake the DR copy? |
| --- | --- | --- | --- |
| DR channel group-B config (base, depth-1) | host | **yes** (clean driver arm, read-back OK) | no - precondition only; E0/R0 confirms |
| **DR pub/rearm +0x38** | host | **no** (0x400 vs vendor-init 0 / vendor-steady fill) | **testable - the only host-owned delta -> E1a/E1b** |
| ETE interrupt RMW CA 0x40039508 bits | host (vendor-owned word) | **yes** (`&= 0xFFE0F8F8` in the arm sequence) | no - no firmware reader; host-facing status word only |
| engine global CA 0x4003A000..0x3FF | device/ROM | n/a (read-only; global 0x10A matches vendor) | no host writer exists; read in R0/E2 |
| engine doors CA 0x4003A7C0/7D0 | device/ROM | n/a (read-only; **value differs from vendor**) | no vendor-host writer; **do not write speculatively**; name it in E2 |
| SR/DR depth-1 | host | yes (0x1F) | no |
| `pcie_msg_init` registrations (0x2D/0x2E etc.) | firmware | runs in the PD4 golden boot (ready pair decoded) | no host lever; but confirms the handlers exist |
| ETE status word / ack / send words | device | never written by us (correct) | no host action can author them |

Two premises carried into the task are corrected here: (1) the trigger is on status bits
**24-28**, not 16-20 (section 2.2); (2) the vendor's D2H re-arm writes (+0x38/+0x18 in
`pcie_ete_sending_trigger`) are a **host reaction after a completed transfer**, not an engine
start - so "port the vendor's missing kick" is not available.

---

## 8. The experiment ladder (bench; run by the Lead, attended)

Full commands, stop rules and recovery gates are in [BENCH-PROCEDURE.md](BENCH-PROCEDURE.md).
One idea per session; no retries; never write CA 0x400392F0.

### R0 - read-only snapshot (no writes; decides the gap)

On a fresh **PD4 golden boot** (page 0x86807FEF/7/2/0xABCD, state=1, mask=0), run the standard
arm + one ctx-create type 1 + one txq_sync72 lap so the device stages D2H responses, then take:

| # | read | CA | BAR0 | decides |
| --- | --- | ---: | ---: | --- |
| R0.1 | boot page / state / mask | 0x1BF000..C, 0x118B60, 0x40008 | 0x877008..C, 0x7D0B60, 0x6F8008 | boot gate/latch |
| R0.2 | ETE status / ack / ctrl | 0x4003A874 / 0x4003A870 / 0x4003A86C | 0x3F2874 / 70 / 6C | did hardware ever raise status 24-28? |
| R0.3 | engine global + blocks + doors | 0x4003A000.., 0x4003A540/4/8, 0x4003A720/4/8, 0x4003A7C0..CC, 0x4003A7D0.. | 0x3F2000, 0x3F2540, 0x3F2720, 0x3F27C0, 0x3F27D0 | door value vs vendor 0x83FAC000 |
| R0.4 | DR0-3 channel: enable/A base/depth/prod/cons, B base/depth/**pub/fill** | 0x4003A590+0x00..3C, 0x5E0, 0x630, 0x680 | DR0 0x3F2590/5A0/5B8/5C0/5C8/5CC; +0x50 per channel | pub vs fill; A-side staging state |
| R0.5 | send words DR0-3 / SR0-2 | 0x4003AC00..0C / 0x4003AC18..20 | 0x3F2C00..0C / 0x3F2C18..20 | target/progress halves nonzero? |
| R0.6 | DR trigger counters / progress | CPU 0x10F5FC, 0x10F950, 0x10FCA4, 0x10FFF8 / records+0x10 | RAM BAR0 0x7C75FC, 0x7C7950, 0x7C7CA4, 0x7C7FF8 | did 0xC625A ever run? |
| R0.7 | staged D2H nodes in the device source rings | PKTRAM DR0 0x01060540.. word0/word1 | 0x238540.. | work is staged but not copied? |
| R0.8 | g in-flight/shadow | 0x10C1A8 / 0x10C1AC | 0x7C41A8 / AC | notify state |

The existing clean driver already prints rows R0.2-R0.5 via `cat hcc_status`, R0.7/R0.8 via
`rd_status`/`rd_catalog`; rows R0.3 and R0.6 need the (optional) small read-only probe or
are collected with the bench tooling.

**Decision:** D1 = status 0, send words 0, fills 0x10, trigger counters 0, staged nodes present
-> the hardware never raised the DR service status; go to E1. D2 = any status bit 24-28 set but
trigger counters 0 -> event delivery gap (unexpected on PD4; investigate the boot page/latch).
D3 = trigger counters > 0 but fills static -> the trigger ran, the engine did not copy -> E2
becomes the door/global diff, and the ladder skips E1.

### E1a - host-owned `+0x38 := fill` (single write class, one variable)

Preconditions: PD4 golden boot latched (page +8==2 / state 1 / mask 0), rings armed, R0 taken.
Using the bench-only `hcc_dr_pub` attribute (patch in `tools/hcc_dr_pub.patch`, compile-verified:

    insmod hi5622_wifi.ko ...
    ...prepare/upload/verify/seed, hcc_arm, hcc_loop, release 0x5A5A only...
    echo fill > /sys/class/.../hcc_dr_pub    # writes each DR +0x38 = current fill (+0x3C)

One session, one write class (all four channels with the same rule), read-back logged.
Expected if the published/fill relationship is the gate: within the service-poll window the
**status word gains bits 24-28**, `record+0x344` (trigger counters) increments, the send words
become nonzero, the **fills advance past 0x10**, the host DRAM DR nodes get tagged word1 values,
and the core's engine-path rx can deliver frames (and/or `hcc_events` shows the engine path).
Stop rules: any exception/mask change, page mutation, timeout -> stop, power-cycle, do not retry.

### E1b - control `+0x38 := 0` (vendor-init value)

Same session shape, fresh cold boot, single write class. If E1a changed nothing, E1b tests
whether the *vendor's initial value* is load-bearing instead. If both are negative, the
published value is not the gate.

### E2 - read-only golden-vendor ETE window diff (only if E1a/E1b are negative)

Take the same R0.2-R0.6 rows on a **vendor 2.5.24 boot with Wi-Fi up** (read-only pack already
used by wifi-chip-reset: rows C1-C6) and diff. Known already: the door 0x4003A7C0 value differs
(0x83FAC000 vs invalid in our boot) and the send/fill state differs (live vs zero). Any further
differing word narrows the remaining device-internal state. **No writes in this rung.**

### E3 - interpretation / close-out

* E1a positive or E1b positive -> the host-owned destination-ring state was the missing enabler;
  the driver arm is corrected to the vendor relationship (`published = fill` at arm, re-arm from
  the engine-path walk) and the direct reader becomes a fallback.
* E1a/E1b negative -> definitive: the DR direction start condition is engine-internal hardware/
  ROM state (the door/global words no image or vendor-host code writes; the status bits 24-28
  are never raised). The direct group-A read is the supported receive path; no further host
  register work is justified. A device-internal close would need a firmware patch that fakes the
  hardware role (write a send word + call the sending trigger) - out of scope here and NOT
  recommended because the send word is the engine's own protocol state.

---

## 9. The direct-read path: supported alternative and its limits

The direct reader (wifi-d2h-direct / clean driver `hi5622_d2h.c`) reads the **device group-A
rings in PKTRAM** (CA 0x0106xxxx through region 2) and the frame bodies through the ACP window,
following the firmware's record producer. It works today (ready pair, g1 id3/g1 id0x12
responses, heartbeats, all decoded) and is the project's working D2H receive path. Limitations:

1. **Not the vendor ABI.** It depends on internal PKTRAM staging addresses and ring-record fields
   (`hcc_instance+0x14` records, `+0x0C` producer) - stable in this firmware but not part of the
   vendor host contract; a firmware update can move them.
2. **No engine flow control.** The DR source ring is depth 32 and the firmware's deposit path
   blind-wraps it (record producer advances without a hardware consumer), so the host must poll
   and drain faster than the ring wraps; the reader bounds each batch and reports overruns
   (`rd_catalog`), but there is no backpressure equivalent to the engine's fill/published window.
3. **Host-side polling cost**, no DMA to host DRAM, and no per-frame doorbell beyond the
   existing out[1] notify; the destination group-B ring/buffers stay unused.
4. **TX is unaffected:** the H2D SR path and its engine copy work normally, so only RX uses the
   direct read.
5. It is read-only on device state (no speculative ETE writes), which is exactly why it is the
   safe default while the engine wake stays open.

If E1 ever wakes the engine, prefer the engine path (fill/published + host DRAM buffers) and
keep the direct reader as the boot-time/fallback decoder.

---

## 10. Evidence table (new claims of this lane)

| # | claim | method | grade |
| ---: | --- | --- | --- |
| W1 | hcc_instance static layout (+4=4 DR, +8=3 SR, +0xC=0x4003A86C, +0x14=0x10F2B8, +0x58=0, +0xB0 crash-dump desc) | fresh data dump; `layout` selftest | [V] |
| W2 | event-0x2E handler 0x4624C reads [hcc+0xC]+8, acks [base+4], bits 24-28 -> [hcc+0x1C], bits 16-20 -> [hcc+0x24], tail d2h_notify(3) | fresh disasm CPU 0x4624C-0x462E8 with resolved literals | [V] |
| W3 | event-0x2D handler 0x462F8: bits 0-2 -> [hcc+0x20], bits 8-10 -> [hcc+0x28]; ack bits 0-2/8-10 | fresh disasm CPU 0x462F8-0x46386 | [V] |
| W4 | callback install: +0x1C=0xC625A, +0x20=0xC6802, +0x24=0xC1902 (stub), +0x28=0xC18FE (stub) | fresh disasm CPU 0x49576-0x49592 + literal pool 0x497AC-0x497BC | [V] |
| W5 | sending trigger 0xC625A body (target=hi16(send word), PKTRAM control-word handshake, tag 0x5A5A scan, counters +0x344/+0x34C, progress +0x10, wake 0xC5128) | fresh disasm CPU 0xC625A-0xC6354 | [V] |
| W6 | events 0x2D/0x2E registered+enabled by pcie_msg_init (0x496C8/0x496D6/0x496E8) | fresh disasm; registrar 0xC74B0; enable 0xC6FF4 | [V] |
| W7 | dispatchers have no BL/BLX caller; 0xC2EFC has exactly one caller (ARM blx at CPU 0x40094 in the ROM trampoline) | whole-image Thumb BL scan + ARM decode CPU 0x40064 | [V] |
| W8 | status word 0x4003A874 is read only by 0x4624C, 0x462F8 and the crash logger 0x499B6; no polled/timer/H2D read; no literal home/writer | whole-image scans; third-site disasm | [V] |
| W9 | 17-site event-raise census: no site raises 0x2D/0x2E (ids 0x4C/0x4E/0x02/0x30-0x32/0x38/0x3D/0x53/0x55 + per-core exception) | whole-image BL scan to 0xC6DB8 with argument decode | [V] |
| W10 | no image writer of status/send words/doors/global; doors only as data pairs {0x10CACC: 0x4003A7C0}, {0x10CB94: 0x4003A7D0} | whole-image constant scans; data dump | [V] |
| W11 | vendor dr_reg_init writes +0x38 = inst+0x1C and dr_init zeroes inst+0x1C -> vendor initial published = 0 | fresh ARM disasm plat 0x1483C/0x14908 | [V] |
| W12 | vendor D2H re-arm (pcie_ete_sending_trigger 0x14420-0x1443C) writes +0x38=ctx[+0x1C] and +0x18=ctx[+0x28] under lock - a reaction, not a start | fresh ARM disasm plat 0x13F90-0x14568 | [V] |
| W13 | pcie_trigger_ete_sending_handle (0x15EFC) is a tail branch to pcie_wkup_thread, not a self-branch bug | fresh ARM bytes + relocation semantics | [V] |
| W14 | clean driver arms DR +0x38=0x400 (prepost 32) and pre-fills node word0; vendor arms 0 with zeroed node arrays; working vendor boot has published==fill | source read + vendor disasm + golden boot read | [V]/[V*] |
| W15 | SR (H2D) A->B copy does run in our boot (SR0 1/1, send word 0x00010001); only the DR direction is gated | cited bench ledger 2026-11-24 15:10/22:10, 2026-11-25 01:10 | [V*] |
| W16 | our ETE global 0x4003A000=0x10A and blocks=1 match the golden vendor boot; the door differs (invalid vs 0x83FAC000) and send/fill are idle | cited golden diff + task state | [V*] |
| W17 | the only host-owned state delta is DR +0x38; all other host-owned fields are vendor-exact | W11/W12/W14 + clean driver arm source | [V]/[I] |
| W18 | therefore no host-only action is known that wakes the DR copy; E1 tests the one remaining host lever, E2 names the device-internal state | W1-W17 deduction | [I, strong] |
| W19 | bench patch \`tools/hcc_dr_pub.patch\` (RO \`hcc_ete_wake\` pack + WO \`hcc_dr_pub\` fill/zero) compiles clean against the 6.18.60 tree: 411,228 B module, sha256 9351305b...478196, 0 compiler/modpost warnings; \`patch -p1 --dry-run\` clean | host-only build, /tmp/etewake-pub; build-path dependent hash | [V] |

---

## 11. Limitations / negative results

* The device ETE hardware's internal condition for raising status bits 24-28 is not documented
  by the image; this lane proves only that no image/vendor-host code authors it. Its exact
  register semantics (engine global +0x000..+0x3FF, doors +0x7C0/+0x7D0) remain [U].
* The door value's origin is [U]: it is a host-DRAM-window pointer in the vendor boot and
  changes per boot, but no vendor-host code writes it, so it is ROM/firmware-authored (or a
  hardware counter that happens to look like one). E2 is the read-only way to pin it.
* The E1 experiments need a driver attribute that does not exist in the committed trees yet
  (`hcc_dr_pub`, patch provided and compile-verified); the Lead owns the bench fire.
* The SR-asymmetry claim is cited from bench sessions, not re-measured here; R0.4/R0.5 re-reads
  it on the same boot as the DR snapshot.
* Not re-derived here (cited): the ROM IRQ vector installation, event-controller register map,
  and the PD3/PD4 patch behaviour - see wifi-event-router, wifi-fw-link-event and
  wifi-irq-unmask/REPORT-PD3/PD4.

*No vendor binary, firmware byte, calibration value or secret is included in this document; all
addresses are derived annotations. Device untouched.*