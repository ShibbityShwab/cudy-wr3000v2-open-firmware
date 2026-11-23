# ETE engine start condition: firmware writers, init tables and the ROM-side gap (host-only, definitive)

**Lane:** ete-engine (shared task `task-82`), **host-only**, 2026-11-23. The bench router was
never contacted, no image was built, and no vendor byte/binary/firmware content is committed
here - only derived addresses, constants and structure layouts.

Local inputs (never copied):

| artifact | identity |
| --- | --- |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B, sha256 `7fc87e20...ef311b` |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko` | 2.5.24, 364,660 B (ETE + PCIe platform HAL) |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko` | 2.5.24 (MAC stack) |

Conventions (project-wide, unchanged): firmware **CPU address R = file offset + 0x40000**;
host BAR0 for a RAM object **R = 0x6B8000 + R** (region 5); device IO CA
**0x3B8000 + (CA - 0x40000000)** (region 3); PKTRAM CA **BAR0 = 0x1D8000 + (CA - 0x01000000)**
(region 2). Vendor-module facts are cited as `plat 0xNNNN` (`.text`-relative, `sh_addr = 0`).
Evidence grades: **[V]** verified in this lane from the named artifact (fresh disassembly /
decoded data), **[V\*]** device-measured by an earlier attended lane and cited, **[I]** inference,
**[U]** unknown.

---

## 0. TL;DR - the four answers

1. **The ETE start condition is a firmware act, not a register bit.** The image's only ETE
   "kick" is the **sending trigger `CPU 0xC625A`** (`hcc_instance+0x1C`), and it is invoked
   **only** from the two device-ETE status dispatchers (event handlers `0x4624C` / `0x462F8`).
   For DR channels, status bits 16-19 of the hardware-authored word **CA 0x4003A874** select the
   channel; the trigger reads the channel **send word** `0x4003AC00+4i` high half as a *target
   index* (lo16 = device progress), writes a u16 target into the channel's **PKTRAM control
   word** (`record+0x14 = staging+0x100`) with a read-back handshake, scans the ring for tagged
   (`0x5A5A`) nodes, releases them, updates `record+0x10`, and wakes the message object. **No
   image write and no vendor-host write can author the status/send words**: they are
   device-ETE-hardware state. [V]

2. **No static {CA,value} init table touches the ETE block.** The image contains exactly one
   genuine device-register init-table pair - **31 entries at CPU 0x1058B8 + 31 entries at CPU
   0x1059B0** (CAs 0x401105xx / 0x401145xx, PHY/MAC glue), applied by the loop at
   **CPU 0xA0184** (memcpy 0xF8 to stack, then `*(CA) = value` per pair, twice). The rest of the
   image's "table-looking" runs are ordinary pointer pools. The ETE window
   (0x4003A000-0x4003AFFF) appears in the image only as (a) the ops-table base constant, (b)
   `hcc_instance+0xC = 0x4003A86C`, (c) channel register pointers inside the ring records, and
   (d) the firmware's own **crash-dump descriptor** `{0x4003A000, 0xC28}`. [V]

3. **Every image writer of the ETE block is accounted for** (section 4): `pcie_msg_init`
   (CPU 0x49334) programs DR group A + enable, SR group B + enable, the two engine control
   blocks `+0x540/+0x720`, and masks the status/control base `0x4003A86C`; the two dispatchers
   only write the ack word `0x4003A870` (base+4). Nothing writes status `0x4003A874`, the seven
   send words, the fills `+0x3C`, the engine door words `0x4003A7C0/0x7D0`, or the global area
   `+0x000..+0x3FF`. [V negative, whole-image]

4. **Verdict: no host-visible action can wake the ETE engine.** All host-owned ETE fields are
   already vendor-exact (regions/interrupt mask/ring params), the vendor host driver writes
   nothing else (whole-`.text` scan: zero references to 0x540/0x720/0x7C0/0x7D0/0x86C/0x870/
   0x874/0xC00/0xC18), the image never kicks the engine, and the required device-event delivery
   path is ROM-only and dead in our boot (the entry-A scheduler unmask gate
   `CA 0x1BF008 == 2` never completes, so the image never executes `cpsie i` and the ROM IRQ
   trampoline `CPU 0x40064 -> 0xC2EFC` never dispatches event 0x2D/0x2E). **Next stage to
   reconstruct: ROM stage S0/S1 - the chip mask-ROM pre-entry bring-up and its inter-core/ROM
   partner handshake at CA 0x1BF008/0x1BF00C - plus the ETE engine-global register file
   `0x4003A000+0x000..+0x3FF` / doors `+0x7C0/+0x7D0` that no image or host code writes.**
   Section 9 gives the read-only bench pack (with the firmware's own ETE dump region) and one
   bounded diagnostic write class that proves the negative.

---

## 1. Method and tools

New tools in this lane (they read the local artifacts in place and print derived annotations;
they never open a device):

| file | purpose |
| --- | --- |
| `tools/ete_engine.py` | image + vendor-module recon: `selftest` (150 checks), `tables`, `layout`, `writers`, `chain`, `benchpack` |
| `tools/fwdis.py` | the boot-gate/ready-emit recursive-descent Thumb disassembler, reused read-only for cross-checks |

Method highlights:

* Every "no writer" claim is a **whole-image negative over all constant-materialisation forms**
  (PC-relative literal load, `movw`/`movt` pair, `adr`) plus a decoded-store census through the
  structure fields that hold the ETE pointers - not a grep for one encoding.
* Table census: maximal 8-byte-stride runs of `{w0 in 0x40000000..0x401FFFFF, w1}` over the whole
  image; runs are then classified by CA block and cross-checked against their consumers.
* Vendor module: `.text` scanned with capstone ARM (driving through `.symtab` names) for
  immediates/offsets and for the ETE control-write; `.data` scanned for the CA constants.

Reproduce:

    PY=<repo>/.provision-venv/bin/python
    cd opensource/docs/incoming/wifi-ete-engine/tools
    $PY ete_engine.py selftest     # 150/150 PASS
    $PY ete_engine.py tables       # init-table census + PHY apply loop + crash-dump table
    $PY ete_engine.py layout       # hcc_instance / g / ETE register window
    $PY ete_engine.py writers      # image writers + whole-image negatives
    $PY ete_engine.py chain        # deposit -> commit -> notify -> flush -> trigger
    $PY ete_engine.py benchpack    # read-only BAR0 read list (section 9)

---

## 2. Static {CA,value} init tables in the image (task item a)

### 2.1 Census

Applying the table-run census over the whole image gives three runs with >= 8 entries:

| CPU | entries | content | verdict |
| ---: | ---: | --- | --- |
| 0x9F7A8 | 12 | mixed literal pool of a function (code pointers + a few device constants) | pointer pool, not a table |
| 0xD95C0 | 9 | mixed literal/pointer pool (0x40110548... interleaved with code pointers) | pointer pool, not a table |
| **0x1058B8** | **62** | **31 x {CA 0x401105xx, value} + 31 x {CA 0x401145xx, value}** | **the one genuine register init table** |

The run at 0x1058B8 is two adjacent 31-entry tables: **table1 = 0x1058B8..0x1059B0**,
**table2 = 0x1059B0..0x105AA8**, followed by a small byte/param block (0x105AA8..0x105AC0,
including the 0x800 / 0x3E8 parameters the apply loop consumes). All addresses in the ETE
window are absent from every table entry (asserted by `selftest`). [V]

### 2.2 Consumer and boot timing of the PHY/MAC table

The consumer is the apply loop at **CPU 0xA0184..0xA01C8** (and 0xA026C..0xA0294 for the second
pass), inside the PHY/RF bring-up body that starts near CPU 0x9Fxxx:

    A0186  ldr  r6, =0x1059B0          ; r6 = table2
    A0188  sub.w r7, r6, #0xF8         ; r7 = 0x1058B8 = table1
    A018C  mov  r1, r4 ; movs r0,#0    ; pass 0/1 -> blob_get(0xB1EE4)
    A01A4  mov  r1, r7 ; mov  r0, r8
    A01A8  blx  0x1036AC               ; memcpy 0xF8 bytes of the selected table to stack
    A01B4  ldr  r2,[r3,#4] ; ldr r3,[r4,r8] ; str r2,[r3]   ; *(CA) = value, 8-byte stride
    A01C2  (loop until 0xF8)
    A01CA  bl 0xF3E4C(...,1)           ; commit/unlock

So the image **does** apply static {CA,value} tables where they exist - and it applies none to the
ETE window. Timing: the body is reached from the image's PHY/RF late-init path (the same
0xB1EE4/0xD8FF0 sequencer family used by the hdpp/PHY bring-up); it is not part of entry A,
the boot task, or the ETE bring-up, and it has no bearing on the ETE engine. [V]

### 2.3 The ETE window in the firmware's own data: the crash-dump descriptor

`hcc_instance+0xB0` (= CA 0x10F2A0, repeated as a static table at CPU 0x10F294) is a **crash-dump
descriptor list**:

| region | src CA | length | meaning |
| ---: | ---: | ---: | --- |
| 0 | 0x001BD000 | 0x2000 | firmware log/arena |
| 1 | **0x4003A000** | **0xC28** | **the whole ETE register window up to the send words** |
| 2 | 0x40039000 | 0xD5C | the PCIe glue/mailbox block |

This is useful evidence for the bench pack (section 9): the firmware itself nominates
`0x4003A000..0x4003AC28` as the ETE state worth dumping, and it is host-readable via BAR0
region 3 (`0x3B8000 + (CA-0x40000000)` -> 0x3F2000..0x3F2C28). [V]

---

## 3. hcc_instance, ring records, ops table and the ETE window map (task item b)

### 3.1 hcc_instance CA 0x10F1F0 (size 0xB0; records begin at +0xC8)

| off | static value | meaning |
| ---: | ---: | --- |
| +0x00 | 0x10 | object id/size word |
| +0x04 | 4 | **DR channel count** (upper bound for the trigger) |
| +0x08 | 3 | **SR channel count** |
| +0x0C | **0x4003A86C** | **ETE status/control block base** (image writes through it in `pcie_msg_init`) |
| +0x10 | 0x11035C | SR ring-record array base (3 x 0xCC) |
| +0x14 | 0x10F2B8 | DR ring-record array base (4 x 0x354, plus a 5th record at 0x110008) |
| +0x18 | 0 (runtime 0x10CEDC) | **ETE ops table** pointer, installed by `pcie_msg_init` |
| +0x1C | 0 (runtime 0xC625A) | **sending-trigger** callback (installed by `pcie_msg_init`) |
| +0x20 / +0x24 / +0x28 | 0 (runtime 0xC6802 / 0xC1902 / 0xC18FE) | other ETE status callbacks |
| +0x58 | 0 | publish-chip id (chip0) |
| +0x5C | 0 (runtime 0x10C0F4) | message object **g** |
| +0xB0..+0xC4 | {0x4003A000,0xC28,0}, {0x40039000,0xD5C,0}, 0 | crash-dump descriptor (section 2.3) |

The message object **g = 0x10C0F4** is an interior pointer into the same global object
(`O1 = 0x10C0C0`, `g = O1+0x34`; `*(CA 0x172130) = g`). Its image-static fields are zero;
`pcie_msg_init` installs at runtime:

| g field | pointer slot | installed value |
| --- | --- | --- |
| g+0x9C | out[1] | 0x40039014 |
| g+0xA0 | out[0] | 0x40039010 |
| g+0xA4 | D2H doorbell | 0x40101434 |
| g+0xA8 | out[5] | 0x400392F0 (0x40101434 - 0xC8000 - 0x144) |
| g+0xAC | aux | 0x400392D4 |
| g+0xB0 | D2H flush one-shot | 0x40101418 (0x400392D4 + 0xC8000 + 0x144) |
| g+0xB4 | **in-flight latch** | 0 (deferred-notify state) |
| g+0xB8 | **deferred shadow** | 0 |
| g+0xC0 | lock | O1+0xF4 |

### 3.2 Ring records (channel register pointers live here)

DR record array: base 0x10F2B8, stride 0x354, index 0..3 (+5th record at 0x110008). Selected
fields after `pcie_msg_init`:

| field | content |
| --- | --- |
| +0x00 | PKTRAM staging/node base: DR0 0x01060440, DR1 0x01060330, DR2 0x01060220, DR3 0x01060110 (0x110-byte regions) |
| +0x08 | pointer to the 12-byte channel config entry (0x104078 + 12i) |
| +0x0C | packed producer index (the deposit op advances its ring-context +0xC; identity with this record is the ready-emit lane model [V]/[I]; written to channel +0x18 at init) |
| +0x10 | progress index = low16 of the send word (read once at init, kept current by the trigger) |
| +0x14 | **channel PKTRAM control word = staging + 0x100** (the trigger's u16 handshake target) |
| +0x1C | 1 (flag byte area) |
| +0x31C | channel register CA: 0x4003A590 / 0x4003A5E0 / 0x4003A630 / 0x4003A680 |
| +0x32C | send word CA: 0x4003AC00 / 04 / 08 / 0C |
| +0x330 | per-channel helper object (created by pcie_msg_init) |
| +0x334 | hcc_instance pointer |
| +0x344 | **sending-trigger counter** (incremented once per invocation) |
| +0x34C | released-node counter |

SR record array: base 0x11035C, stride 0xCC, index 0..2, selected fields: +0x10 PKTRAM save
buffer (0x01060750 / ...), +0x18 cfg pointer, +0x1C published index, +0xB0 channel CA
(0x4003A400/0x450/0x4A0), +0xBC send word CA (0x4003AC18/1C/20).

The channel config table (image twin of the vendor `get_chn_cfg` table):

    SR0-2 @ 0x104054: {0x400, 0x06800020, 1} {0x450, ...} {0x4A0, ...}   (depth byte +4 = 0x20, ctrl +5 = 0)
    DR0-3 @ 0x104078: {0x590, 0x06800020, 1} {0x5E0, ..., 0} {0x630, ..., 0} {0x680, ..., 0}

### 3.3 ETE ops table CA 0x10CEDC (installed at hcc_instance+0x18)

| slot | value | role |
| ---: | ---: | --- |
| +0x00 | 0x4003A000 | ETE block base |
| +0x04 | 0x40039508 | host-visible ETE interrupt word (written only by the vendor host, never read by image code [V negative]) |
| +0x08 / +0x0C | 0x000C1875 / 0x000C186F | stubs returning -1 |
| **+0x10** | **0x000C61E1** | **D2H deposit op (CPU 0xC61E0)** |
| +0x14 | 0x000C1859 | node fill (word0 only) |
| +0x18 / +0x1C / +0x20 / +0x24 | 0x000C1867 / 0x000C184F / 0x000C1843 / 0x000C1835 | node size 8 / word0 / len / flag accessors |
| +0x28 / +0x2C | 0x000C182B / 0x000C186B | second-ring word0 / node size |
| +0x30 / +0x34 / +0x38 / +0x3C / +0x40 | 0x000C1821 / 0x000C17F9 / 0x000C17ED / 0x000C1813 / 0x000C1803 | third-ring word0 / addr / len / set-flag / flag accessors |

(All function slots are Thumb pointers: odd = function address + 1.)

### 3.4 ETE register window 0x4003A000-0x4003AFFF (all host-BAR0 readable, region 3)

| offset | role | image writer? | vendor-host writer? |
| --- | --- | --- | --- |
| +0x000..+0x0FF | engine global/config area [U] | **no** | **no** |
| +0x400/+0x450/+0x4A0 | SR0-2 channel blocks | group B + enable | group A |
| +0x540..+0x548 | engine/queue control block 0 | yes: +0 bit0, +4 bit0 set; +8 low3 clear | no |
| +0x590/5E0/630/680 | DR0-3 channel blocks | group A + enable | group B |
| +0x6D0 (+0xC10 send) | 5th DR (control) channel | not configured by `pcie_msg_init`'s DR loop | no |
| +0x720..+0x728 | engine/queue control block 1 | yes: same pattern | no |
| +0x7C0 / +0x7D0 | engine door/status words paired with the two control blocks | **no** | **no** |
| +0x86C | ETE status/control base (image clears bit12, bit29 then `&= 0xE0E0F8F8`) | yes | no |
| +0x870 | ack word (dispatchers write base+4) | yes (ack) | no |
| +0x874 | **status word** (dispatchers read base+8) | **no** | no |
| +0xC00..+0xC0C | DR0-3 send words (hi16 target, lo16 progress) | **no** | **no** |
| +0xC10 | 5th DR send word | **no** | no |
| +0xC18..+0xC24 | SR0-2 send words | **no** | no |

Per-channel register groups (decoded from `pcie_msg_init`): **A = +0x10 base, +0x14 depth-1,
+0x18 producer, +0x1C consumer; B = +0x30 base, +0x34 depth-1, +0x38 published, +0x3C fill.**
The image programs **DR group A** + enable and **SR group B** + enable; the host programs the
mirror side. The engine is the only writer of the fills (+0x3C) and of the send-word halves.

---

## 4. Every image writer of the ETE block (task item a, part 2)

`pcie_msg_init` **CPU 0x49334** (module-init function, called indirectly; it is also entry 1 of
the HCC module table at CPU 0x10F254):

1. DR loop (channel index 3..6 -> DR0-3, records `[hcc+0x14] + 0x354*(i-3)`), sites
   0x493B8..0x49496:
   * `chan+0x10 = record+0x00` (PKTRAM staging/node base)
   * `chan+0x14` = depth-1 via `bfi #0,#0xa` from cfg byte +4 (0x20 -> 0x1F)
   * `chan+0x18 = record+0x0C` (producer = 0)
   * `chan+0x08` = cfg byte +5 & 7 (ctrl = 0) via bfi
   * `chan+0x00 |= 1` (channel enable)
2. SR loop (index 0..2, records `[hcc+0x10] + 0xCC*i`), sites 0x494D6..0x4956E:
   * `chan+0x30 = record+0x10` (PKTRAM save buffer), `chan+0x34` = depth-1,
     `chan+0x38 = record+0x1C` (published), `chan+0x00 |= 1`.
3. Engine control blocks (sites 0x49594-0x495BA and 0x49620-0x4964A):
   * CA 0x4003A548 (block0+8) low 3 bits cleared; 0x4003A544 (block0+4) bit0 set;
     0x4003A540 (block0+0) bit0 set.
   * CA 0x4003A728 / 0x4003A724 / 0x4003A720 (block1) the same pattern.
4. Status/control base CA 0x4003A86C (sites 0x496B0-0x496C6 and 0x496F4-0x496FC):
   clear bit12, clear bit29, then `&= 0xE0E0F8F8` (an *initialisation/mask* RMW; the vendor
   host's separate host-facing word 0x40039508 gets `&= 0xFFE0F8F8`).
5. Mailbox installation (sites 0x496C8-0x4984C): registers ETE status handlers as event ids
   **0x2D** (0x462F8) and **0x2E** (0x4624C), enables them via `0xC6FF4`, installs g's pointer
   slots, and arms the D2H deposit/notify machinery.

The two dispatchers **0x4624C** (event 0x2E) and **0x462F8** (event 0x2D) read status
`[base+8] = 0x4003A874` and write the ack `[base+4] = 0x4003A870`; they never store to the
status word. The startup/vector handshake `0xC6E14` touches only CA 0x40039000/0x40039800/
0x40039014 and CA 0x40000108/0x4000010C/0x4003022C - **not** the ETE window.

**Whole-image negatives** (`ete_engine.py writers`): no code materialises the status word, the
ack word, any channel CA, any send word, the engine doors or the ETE block base (the block base
exists only inside two data tables; 0x4003A86C only at `hcc_instance+0xC`). The ETE registers are
therefore reached exclusively through the structure fields above, and the writers are exactly the
ones listed. [V]

---

## 5. The deposit / notify / flush chain and the receiving trigger (task item b, part 2)

### 5.1 D2H deposit path (ready-frame emission)

```
HCC queue message
  -> ops+0x10 = deposit CPU 0xC61E0(chip_obj, ring_ctx, buffer_dev_va, len, present)
       node.word0 = buffer_dev_va
       node.word1 = (len<<16) | 0x6D2B   (0x6D2B = present|valid|direction tag 0xD2B)
  -> ring commit CPU 0xC192A(ring_ctx, &node)
       node[packed_producer & 0x3FF] at [ctx+0]
       advance ctx+0xC with the depth byte at [[ctx+8]+4]  (CPU 0xC1906 phase-bit helper)
  -> d2h_notify CPU 0xC6170(publish_chip, id=6)
       shadow g+0xB8 |= 1<<6; publish to out[1] only if in-flight g+0xB4 == 0, else defer;
       then g+0xB4 = shadow, g+0xB8 = 0, doorbell CA 0x40101434 bit0
  -> flush CPU 0xC6108 (event 0x4E / direct): write 1 through g+0xB0 (0x40101418),
       spin until out[1] reads 0, then publish the deferred shadow (g+0xB4 = g+0xB8)
```

### 5.2 Device ETE status path (the only ETE kick)

`0x4003A874` bits are dispatched as:

| status bits | handler | callback |
| --- | --- | --- |
| 16-19 | 0x4624C (event 0x2E) | **hcc+0x1C = sending trigger 0xC625A(hcc, ch)** |
| 24-31 | 0x4624C (event 0x2E) | hcc+0x24 = 0xC1902 |
| 3-7 | 0x462F8 (event 0x2D) | hcc+0x20 = 0xC6802 |
| 8-15 | 0x462F8 (event 0x2D) | hcc+0x28 = 0xC18FE |

The 0x2E dispatcher (0x4624C) then tail-calls `d2h_notify(hcc, 3)` ("ETE transfer done"). [V]

### 5.3 Sending trigger CPU 0xC625A(hcc, ch)

```
requires hcc+0x5C != 0, hcc+0x14 != 0, ch < hcc+0x04
r4 = [hcc+0x14] + 0x354*ch                      ; DR record
r4+0x344++                                      ; trigger counter
r5 = (*(r4+0x32C)) >> 16                        ; target index = high half of the send word
*(u16 *)(r4+0x14) = r5 ; read back ; compare    ; handshake write into the channel's
                                                ; PKTRAM control word (record+0x14 = staging+0x100)
loop while r4+0x10 != r5:
    node = ops+0x28(record, r4+0x10 & 0x3FF)    ; tagged-node scan
    if node tag (node+0xA) == 0x5A5A: move/release it, account r4+0x34C
    advance the packed progress index
r4+0x10 = r5 ; wake g via CPU 0xC5128
```

**This is the only ETE kick in the image.** Its inputs are the send word (hardware-authored) and
the DR record's progress index; its command channel is a PKTRAM control word, not a register.

---

## 6. Exactly what starts the group A->B transfer (task item b, part 3)

Reconstructed start condition (evidence: sections 3-5, all [V]):

1. The ETE **hardware** owns the send words (0x4003AC00+4i / 0x4003AC18+4i) and the fills
   (+0x3C). They encode packed ring indices (low 10 bits index, bit 10 wrap phase; hi16 = target
   that the firmware must reach, lo16 = device progress). No image code and no vendor-host code
   writes them.
2. When the device ETE logic has channel work/credit, it raises bits in status word 0x4003A874
   (bits 16-19 = DR channels). The image cannot set that word; it only acks `+0x870` and reads
   `+0x874`.
3. The status bits reach the image only through the **ROM-vectored event path**:
   image header +0x64 ARM trampoline `CPU 0x40064` -> event dispatcher `0xC2EFC` -> current
   vector CA 0x4016010C -> handler table CA 0x17D430 -> event 0x2E handler `0x4624C`. The
   trampoline's BLX at CPU 0x40094 is the **only** caller of `0xC2EFC` in the image [V].
4. The dispatcher then (and only then) invokes the sending trigger, which commands the channel
   through the PKTRAM control word; the engine copies ring entries A->B and advances the fills.

Consequences:

* There is **no host register** in the loop. The host only owns the mirror ring fields
  (DR group B base/depth/published; SR group A), the host-facing interrupt mask, and the mailbox
  drain service - all already vendor-exact in the current B3 instrument (the ete-init patch made
  the intr RMW and bfi-faithful depth/ctrl writes exact). [V]/[V\*]
* The first status word must therefore be authored by the device ETE engine from a state that the
  image does not create. The only ETE-window state outside image/vendor-host writes is the
  engine **global/config area +0x000..+0x3FF** and the engine **door words +0x7C0/+0x7D0**
  (an image write exists only to the +0x540/+0x720 control words and the +0x86C mask). Whether
  those are ROM-initialised, hardware-reset defaults, or mask/credit registers is [U]; they are
  the exact gap. [I, strong]
* Independently, the event delivery itself is dead in our boot: entry A (S2) blocks on the
  **ROM/inter-core handshake `CA 0x1BF008 == 2`** before entering the scheduler; the image only
  executes `cpsie i` inside the dispatcher/deferred path/scheduler idle, so no context ever
  unmasks IRQ, and the ROM trampoline never runs `0xC2EFC`. The pending software events (0x4C/
  0x4E) and the timer (0x1D) stay undelivered (event-router lane [V\*]). Even a hypothetical
  hardware status word would never reach the dispatcher in this state.

---

## 7. Vendor-module comparison (task item c)

`hi5622v100_plat.ko` 2.5.24 (identical init functions to 2.4.15 per the ete-init lane):

| vendor site | ETE host write |
| --- | --- |
| `pcie_ete_intr_init` plat 0x7528 | maps CA 0x40039508 and **RMWs the host-facing intr word** `&= 0xFFE0F8F8` (the only ETE global write) |
| `pcie_ete_sr_reg_init` plat 0x14A48 | SR group A: +0x10 base (host-DRAM dev VA), +0x14 depth-1 (bfi #0,#0xa), +0x18 producer, +0x08 ctrl (bfi #0,#3); reads the SR send word hi16 as an "already sending" warning |
| `pcie_ete_dr_reg_init` plat 0x1483C | DR group B: +0x30 base, +0x34 depth-1, +0x38 published |
| `pcie_ete_sending_trigger` plat 0x13F90 | runtime re-publish of DR +0x38 / +0x18 through the channel VA - the vendor's only "kick-like" host write, and B3's P0 pre-post covers the initial value |

Whole-`.text` scan of both modules: **zero** immediate/offset references to the device-internal
registers 0x540, 0x720, 0x7C0, 0x7D0, 0x86C, 0x870, 0x874, 0xC00, 0xC18; the only CA pair in
`.data` is {0x4003A000, 0x40039508} (returned by `get_pcie_ete_res`). The vendor host stack
therefore has **no private engine-start write** that B3 could copy - its role is exactly the ring
mirror + intr mask + mailbox service. [V]

---

## 8. Current bench reality (local evidence only; no device contact by this lane)

Read-only UART evidence already on disk, consistent with the task context:

| marker | observed | source |
| --- | --- | --- |
| P0 pre-release | DR0-3 fill(+0x3C) = 0x10 (reset value), enable(+0x00) = 0 | `build/uart-logs/20261123T*.log` |
| post-release P2 | DR0-3 enable = 0x00000001, ctrl = 0x00, fill = 0x10, published(+0x38) = 0x400 (host B3 value persisted) | same |
| P1 watch | event bit2 only (platform-up out[1]=4); bit6/bit3 absent in that run; the Lead's later state has g+0xB4 = g+0xB8 = 0x40 (deferred id6) | same + task context [V\*] |
| host ring nodes | DR node word1 = 0, no 12-byte HCC payload landed in host DRAM | P2 dump |
| ETE status/send/fills | all zero / 0x10, never advanced | task context [V\*] |

Interpretation: the firmware's side of the contract ran (`pcie_msg_init` configured DR group A and
set enable=1; the boot task ran; the platform-up announce was published), and the host's side is
complete, yet the hardware-authored ETE state never changes - exactly what "engine never kicked"
predicts. The host-owned published value survived the firmware's channel enable, so there is also
no ordering/latching loss to blame on the pre-release P0 arming.

---

## 9. Conclusion, next stage, and the bench action (task item d)

### 9.1 Definitive answer

**No host-visible action can wake the ETE engine from the reachable state.** The ETE transfer is
started by a firmware command that is itself gated on a hardware-authored status word, and that
delivery path is ROM-only and dead in this boot. The host has no register in that loop; the
vendor stack has no such write either, so "port the vendor's missing write" is not a possible fix.
This is a **device/ROM-internal blocker**, not a host ETE-init gap.

### 9.2 The exact ROM/init stage to reconstruct next

Reconstruct **ROM stage S0/S1 of the boot flow** (rom-flow stage table: S0 = ROM load + vector
table, S1 = header-entry dispatch) and specifically the **ROM/first-stage ETE engine bring-up and
the entry-A inter-core/ROM partner handshake**:

1. **CA 0x1BF008 / 0x1BF00C handshake**: entry A writes {0, 0xABCD} to 0x1BF008/0x1BF00C, wakes
   the partner via CA 0x40161F00 = 0x20000, then spins until `0x1BF008 == 2`. This is the gate
   that keeps the scheduler (and thus IRQ unmask and event delivery, S13) from ever running.
   Partner = ROM or the chip's second core; the image cannot write 2 [V: writer scan; V\*:
   event-router spin]. The ROM's counter-write must be reconstructed (or replicated only if it is
   a host-writable register, which it is not in the vendor stack).
2. **ETE engine-global register file CA 0x4003A000+0x000..+0x3FF and door words
   +0x7C0/+0x7D0**: no image or host code writes them; their reset/ROM values are the missing
   environmental state for the ETE engine. The firmware's own crash-dump descriptor
   `{0x4003A000, 0xC28}` gives the exact snapshot region.
3. **Event delivery S13** (ROM IRQ trampoline at CPU 0x40064 -> 0xC2EFC -> handler table
   CA 0x17D430): prove whether the ROM's vector/mask contract is what our release path skips.

**Evidence that would prove it** (all read-only unless marked):

* **Golden vendor-boot trace** (attended vendor stack): after release, read CA 0x1BF008 (expect
  2 once entry A proceeds), watch whether 0xC2EFC executes (instrument the trampoline or count
  handler calls through the handler table CA 0x17D430), and snapshot the ETE region
  `{0x4003A000, 0xC28}` plus the two doors when the vendor's first D2H traffic appears. Diff
  against our boot's identical snapshot: every word that differs in the ETE window / 0x1BF008 is
  a candidate for the ROM stage.
* **Read-only bench pack P-ETE** below: decides whether the engine is cold (all zero/0x10) or
  whether hardware status exists but delivery is dead (status nonzero, trigger counters 0).

### 9.3 The bench action

**(A) READ-ONLY snapshot pack - the decisive, no-write action.** Run the standard B3 sequence to
the platform-up state (release + 0xcece service + D2H drain), then read the rows in
`ete_engine.py benchpack` (all values/addresses are derived annotations; nothing below opens a
device):

| read | CA | BAR0 | decides |
| --- | ---: | ---: | --- |
| ETE status word | 0x4003A874 | 0x3F2874 | did the device engine ever raise a status? |
| ETE ack word | 0x4003A870 | 0x3F2870 | were dispatcher acks ever written? |
| DR0-3 send words | 0x4003AC00/04/08/0C | 0x3F2C00/04/08/0C | target/progress halves advancing? |
| DR0-3 fill + channel | 0x4003A590/5E0/630/680 | 0x3F2590/25E0/2630/2680 | did the A->B copy run? |
| engine blocks +0/+4/+8 | 0x4003A540 / 0x4003A720 | 0x3F2540 / 0x3F2720 | engine control state |
| engine doors | 0x4003A7C0 / 0x4003A7D0 | 0x3F27C0 / 0x3F27D0 | never-written engine state |
| ETE status/control base | 0x4003A86C | 0x3F286C | mask value |
| host intr word | 0x40039508 | 0x3F1508 | vendor mask applied? |
| trigger counters DR0-3 | 0x10F5FC/50/CA4/FFF8 | 0x7C75FC/7950/7CA4/7FF8 | did the firmware trigger ever run? |
| g in-flight / shadow | 0x10C1A8 / 0x10C1AC | 0x7C41A8 / 0x7C41AC | notify state |
| entry-A handshake | 0x1BF008 / 0x1BF00C | 0x877008 / 0x87700C | is entry A still spinning? |
| event vector / pending | 0x4016010C / 0x40161184 / 0x40161284 | 0x51810C / 0x519184 / 0x519284 | delivery state |
| ETE crash-dump region | 0x4003A000, 0xC28 bytes | 0x3F2000 | golden-diff payload |

Decision matrix:

* **D0** - status 0x4003A874 nonzero **and** fills/send words advancing: the engine/pump is alive;
  the remaining gap is event delivery - check 0x1BF008 == 2 and trigger counters; escalate to the
  ROM/event lane (diagnostic write B).
* **D1 (expected)** - status 0, send words 0, fills 0x10, trigger counters 0, 0x1BF008 != 2:
  the engine was never kicked and the scheduler path never ran - **definitive device/ROM-internal
  negative**; the next work is section 9.2 (golden trace + ROM stage reconstruction), not more
  host writes.
* **D2** - ETE doors (0x4003A7C0/0x7D0) or the global area are nonzero in a golden boot but zero
  here: the diff names the ROM-initialised ETE state directly.

**(B) One bounded diagnostic write class (optional, only if the Lead wants the negative proven in
one session).** The ETE status bits themselves cannot be authored from the host; the only
host-writable lever is the **device event controller's software pending-set**, replaying the
image's own raise primitive (`0xC6DB8`) for the ETE completion events **0x2D / 0x2E** (the
event-router lane proposed the same write class for 0x4C):

    fingerprint (read): CA 0x40161184, 0x40161284, 0x4016010C, 0x4003A870,
                        0x10C1A8, 0x10C1AC, 0x10F5FC.. trigger counters
    write: 0x00004000 -> CA 0x40161184 (BAR0 0x519184) and CA 0x40161284 (BAR0 0x519284)
           [event 0x2E = D2H status dispatcher 0x4624C; 0x4000 = 1<<(0x2E & 31)]
    optional second run: 0x00002000 for event 0x2D (dispatcher 0x462F8)

Expected if the CPU can deliver events at all: the dispatcher runs, so **g+0xB8 gains bit3**
(local `|0x08`; it is currently 0x40), or out[1] bit3 is published (then drain it normally); the
ack write to 0x4003A870 happens (value unchanged when status is 0). Expected if the CPU IRQ path
is masked: the pending bit stays latched, g+0xB8 unchanged, counters unchanged - the definitive
ROM/event-delivery proof. **This cannot start the ETE transfer**: the trigger would still see
status bits 16-19 = 0 (hardware-authored), so fills/send words stay 0. Do not write any ETE
register.

---

## 10. Evidence table (new claims of this lane)

| # | claim | method | grade |
| ---: | --- | --- | --- |
| E1 | hcc_instance layout (+4=4 DR, +8=3 SR, +0xC=0x4003A86C, +0x14=0x10F2B8, +0xB0 crash-dump table) | `layout`; data dump | [V] |
| E2 | g = 0x10C0F4 = O1+0x34; pointer slots installed by pcie_msg_init (0x40039014/10, 0x40101434, 0x400392F0, 0x400392D4, 0x40101418, in-flight +0xB4, shadow +0xB8) | `layout`; pcie_msg_init disasm 0x496FE-0x497F4 | [V] |
| E3 | ETE ops table 0x10CEDC: base 0x4003A000, intr 0x40039508, deposit +0x10 = 0xC61E0, accessors | `layout` | [V] |
| E4 | image writers: pcie_msg_init (DR group A+enable, SR group B+enable, +0x540/+0x720, +0x86C mask), dispatchers (ack +0x870) only | disasm 0x493B8-0x496FC; `writers` | [V] |
| E5 | NO image writer of status 0x4003A874, send words, fills, +0x7C0/+0x7D0, global +0x000..+0x3FF; no materialisation of those CAs anywhere | whole-image scan (literal/movw+movt), `writers`; 150-check selftest | [V] |
| E6 | one genuine static {CA,value} init table pair: 31 + 31 entries at 0x1058B8/0x1059B0 (CAs 0x401105xx/0x401145xx), apply loop 0xA0184 | `tables`; data dump; apply-loop disasm | [V] |
| E7 | no static table addresses the ETE window; ETE window is in the firmware's own crash-dump list {0x4003A000,0xC28} | `tables`; selftest | [V] |
| E8 | sending trigger 0xC625A: target=hi16(send word), u16 handshake into record+0x14 (=staging+0x100), counters +0x344/+0x34C, tag 0x5A5A scan, wakes g | `chain`; fresh disasm | [V] |
| E9 | dispatchers 0x4624C/0x462F8: read base+8 status, write base+4 ack, bits 16-19 -> trigger, tail-notify(3); registered/enabled as events 0x2E/0x2D | fresh disasm; pcie_msg_init sites 0x496C8-0x496F0 | [V] |
| E10 | the event dispatcher 0xC2EFC has exactly one caller in the image: the ROM-vectored ARM trampoline BLX at CPU 0x40094; trampoline ends rfeia | ARM disasm of header +0x64; selftest | [V] |
| E11 | vendor host: only ETE global write is the intr-word RMW 0xFFE0F8F8; no immediates/offsets 0x540/0x720/0x7C0/0x7D0/0x86C/0x870/0x874/0xC00/0xC18; no engine-block CA words | plat.ko capstone scan; `selftest` | [V] |
| E12 | host-side ETE arming in B3 is vendor-exact; the published index written pre-release persisted after the firmware's channel enable | prior lane + UART logs; task context | [V\*] |
| E13 | entry A never reaches the scheduler (ROM handshake 0x1BF008==2 missing) so no context unmasks IRQ and no event dispatches | event-router lane [V\*]; rom-flow stage table; this lane's read pack | [V\*] |
| E14 | therefore the ETE pump cannot be started by any host action; the missing state is the ROM/first-stage ETE enable + ROM event delivery | E4-E13 + bench state | [I, strong] |
| E15 | the exact next reconstruction is ROM S0/S1 (0x1BF008 partner + ETE global/config +0x000..+0x3FF / doors +0x7C0/+0x7D0), proven by a golden-vs-ours dump of {0x4003A000,0xC28} and 0x1BF008 | deduction + firmware crash-dump table | [I] |

---

## 11. Limitations / negative results

* The ETE engine's internal register semantics are not documented by the image (no image access to
  +0x000..+0x3FF, +0x7C0/+0x7D0). Their role is [U]; this lane can only prove that *no image or
  vendor-host code writes them*, which is what makes them the named gap.
* The rom-flow/event-router conclusions about ROM/IRQ delivery are cited, not re-derived here.
* The event-replay diagnostic (B) writes the device event controller (host-writable through BAR0
  per the event-router map); it is a diagnostic, not an engine wake-up, and it is not required if
  the Lead accepts the read-only pack.
* The record+0x14 = staging+0x100 derivation is from the `pcie_msg_init` address arithmetic
  (node size 8 x depth 32 + PKTRAM staging word); the handshake semantics (who reads the PKTRAM
  word) remain [U] - the ETE hardware is the only plausible consumer.

*No vendor binary, firmware byte, calibration value or secret is included in this document; all
contents are derived addresses, constants and structure layouts. Device untouched by this lane.*
