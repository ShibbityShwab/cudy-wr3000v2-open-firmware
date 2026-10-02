# fw-hostmem: what the device reads out of host memory, and whether a shared memory structure arms the H2D accept gate (phase 22, 2026-10-02)

Task `st_01a0fc02`. Read-only analysis of the on-disk artifacts. Direct sequel to
`docs/phase22/h2d-accept.md`, which proved that **every host-writable register in the exposed
windows** is a dead end for the H2D accept gate (20 batched hypotheses: glue arm, ETE-intr enable
sweep, SR producer-commit edge/phase variants, plus the CA `0x400392f0 <= 8` synchronous arm), while
the descriptor fetch **and** the completion interrupt both work live (`docs/phase21/both-eps.md`).

That phase named one remaining explanation class: the firmware may be waiting on something in
**memory** rather than a register. In particular the runtime message context the host driver builds
is a host-memory object (`comm = .LANCHOR0 + 0x78*controller`, `docs/phase20/runtime-msg.md` A.1);
if the firmware polls or reads that structure, no register poke would ever arm it. This document
enumerates every host-memory structure the driver and the device share, names the writer and the
address source of each, says whether the device **polls** it or is **interrupted**, and answers the
decisive question for the firmware dispatcher at file `0x818a8`/`0x818ac`.

Every claim is **[proven]** (a constant/relocation/control-flow in an instruction stream, or a
value the device printed) or **[inferred]**.

---

## Headline

1. **The device reads exactly two host-memory structures: the ETE SR node ring and the data buffers
   its nodes point at.** Both are coherent DMA in host physical `0x80000000..0xffffffff` - the
   range the endpoint outbound iATU window maps (`docs/phase20/host-window.md` A.1). Their addresses
   are published by **registers** (`SR+0x10`, and each node's `word0`), not by a descriptor the
   firmware fetches.
2. **The runtime message context is *not* shared memory.** The firmware builds its own message
   context in **device** RAM (file `0x9334`, `pcie_msg_init`; ctx fields at file
   `0x9758..0x9788`) whose fields are *pointers to device registers*. `pcie_msg_init` stores the
   object pointer into a firmware global (`*0x172130`, file `0x9700..0x9706`). No firmware code
   references any host address; there is no host pointer anywhere in the blob.
3. **The H2D dispatcher (`file 0x818ac`) reads no host memory.** It reads only the ctx it is handed
   (`ctx+4` -> `out[0]` CA `0x40039010`), writes the ack `ctx+0xc` -> `0x400392f0`, the re-arm
   `ctx+0x10` -> `0x400392d4`, and dispatches via the device table `ctx+0x20`. **Therefore the
   "firmware polls the host context" hypothesis as stated is killed.** The gate is the device-side
   *caller* of the dispatcher (an internal wake), not a host-memory population.
4. **The one shared host-memory structure that *can* gate the H2D direction is the SR ring**: the
   mirrored dispatchers show the H2D mailbox is fed by the host directly, but the firmware's message
   service is entered from its own ETE receive path, whose input is the host SR ring. The ranked
   hypotheses at the end turn that into concrete writes with confirm/kill observables.
5. **The exact mirrored contract is now pinned on both sides** (all offsets from instructions):

   | direction | pending | doorbell | ack | clear | re-arm | dispatcher |
   | --- | --- | --- | --- | --- | --- | --- |
   | H2D (host -> fw) | `out[0]` `0x40039010` | `out[2]` `0x400392d4` | `0x400392f0` | `*(ctx+4)=0` | `0x400392d4 = 8` | firmware file `0x818ac` |
   | D2H (fw -> host) | `out[1]` `0x40039014` | `0x40101434` | `0x40101438` | `*(ctx+4)=0` | `0x40101414 = 1` | host `pcie_msg_handle` `0x171f8` |

---

## 0. Method and sources

| item | value | use |
| --- | --- | --- |
| `build/tmp/FIRMWARE.bin` | 928,920 B, md5 `0e530b976d5a20e87358671f1a577695` | the device image |
| runtime convention | file offset + `0x40000` (phase 4/6/8) | pointer literals in the blob are runtime addresses |
| `build/register-dumps/teardown/hi5622v100_plat.ko` | md5 `23660bc285393e678d5cade1c36c194b` | host driver |
| `pyenv/Scripts/python.exe`, capstone 5.0.7 `CS_ARCH_ARM, CS_MODE_THUMB` | | disassembly (this phase) |
| prior dumps | `build/tmp/fwaccept/fw_disasm.txt`, `build/tmp/fwload/*.txt`, `docs/phase20/*`, `docs/phase21/*` | cross-checks |

Reproduce the new firmware windows:

```
PY=../pyenv/Scripts/python.exe
$PY - <<'EOF'
from capstone import *
d=open('../build/tmp/FIRMWARE.bin','rb').read()
md=Cs(CS_ARCH_ARM,CS_MODE_THUMB); md.skipdata=True
for a,b in [(0x9334,0x9770),(0x8187a,0x818f4),(0x86170,0x86230),(0x82a54,0x82b60)]:
    print("== 0x%x =="%a)
    for i in md.disasm(d[a:b],a):
        print("  %05x rt%05x  %-8s %s"%(i.address,i.address+0x40000,i.mnemonic,i.op_str))
EOF
```

All firmware addresses below are **file offsets** unless marked `rt`; host `.ko` addresses are
`.text` offsets.

---

## 1. The two address spaces and the one window

**Host DRAM reachable by the device is exactly `0x80000000..0xffffffff`.** The endpoint's outbound
iATU viewport is programmed by `oal_pcie_set_outbound_by_membar` (host ko `0x9a38..0x9af4`) from the
window descriptor `.data+0x1fb8`:

```
win+0x00 devva_base  = 0x80000000     win+0x08 devva_end   = 0xffffffff
win+0x10 hostca_base = 0x80000000
devva = devva_base + hostca - hostca_base                (pcie_hostca_to_devva @0xaefc)
```

With chiptype 0 this is the identity, so **`devva == hostca`** for the whole range the coherent
allocator hands out (`docs/phase20/host-window.md` A.0/A.1). The firmware blob contains a
device-side region/attribute table at file `0xc6180` whose entry index `0x0e` is
`{0x1110, 0x0e, 0x80000000, 0x3d}` (file `0xc6210..0xc621f`) - a device region based at
`0x80000000`, i.e. the host aperture. **[proven** bytes**; aperture reading inferred**] This is the
only 4-byte literal `0x80000000` in the blob (the other two hits at file `0xc631e`/`0xd6106` are
2-byte-aligned coincidences inside bitmask/constant tables).

**Consequence.** Host memory outside `0x80000000..0xffffffff` is invisible to the device. The
host's static `.bss` (where `comm = .LANCHOR0 + 0x78*ctrl` and the `g_dmac_to_hmac_read_msg[]`
records live) is *not* in the coherent-DMA window, so even if the firmware had a pointer to it, it
could not read it. This alone is fatal to the "firmware reads the host message context" story unless
a host address were explicitly converted with `pcie_hostca_to_devva` and published - and no such
publish site exists (section 2.5).

---

## 2. Host-memory structures the driver and the device share

Legend for "address source": the mechanism by which the **device** learns the host address. "polls?"
asks whether the device reads the structure in a loop or is told about it by an interrupt.

### 2.1 ETE SR node ring (H2D descriptor ring) - SHARED, host-written, device-read

| property | value | source |
| --- | --- | --- |
| what | 3 channels, each `(depth+2)*8 = 272` bytes = 34 nodes of 8 B | `pcie_ete_init_src_ring` @`0x7068`, `shuangta_ete_sr_node_init_handle` @`0x17728` (`dma_alloc_attrs`) |
| who writes | **host driver** at probe (`pcie_ete_sr_reg_init` @`0x14a48`, `shuangta_ete_sr_dscr_fill` @`0x17858`); the firmware also re-programs the channel registers in `pcie_msg_init` (file `0x9426..0x9454`) | host ko; firmware file `0x9334` |
| host | `dma_alloc_coherent`, host physical `0x83xxxxxx` | live logs (`SR ch0 base 0x83a1f000`, `0x8370b000`, ...) |
| address source | **SR+0x10 program register** = `pcie_hostca_to_devva(ring dma)`; host ko `0x14aa0: ldr r2,[r4,#0xe8]; bl pcie_hostca_to_devva; 0x14ab0: str r0,[r5,#0x10]`. The firmware writes the same register from its channel object: firmware `0x9426: str r1,[r2,#0x10]` | proven |
| CA / BAR0 | SR ch0 base `CA 0x4003a410` = `BAR0+0x3f2410`; ch1 `0x4003a460`, ch2 `0x4003a4b0` | ETE block `CA 0x4003a000` (`docs/phase20/runtime-msg.md` A.2) |
| node format | `word0 = buffer device address`; `word1 = (len<<16) | 0x6d2b` (bits 14,13 owner/valid + `0xd2b` host-fill magic) | `shuangta_ete_sr_dscr_fill` @`0x17858`; `docs/phase20/tx-path.md` A.2 |
| device access | the **ETE hardware** DMAs the node list through the outbound window; the payload goes to the device | proven live: `SR ch0 DEVICE INDEX 0x10 -> 0x400` |
| polled? | no - engine-driven on a committed producer index (`SR+0x18`), completion via interrupt | `docs/phase21/both-eps.md` |

### 2.2 ETE SR data buffers (H2D message payloads) - SHARED, host-written, device-read

The buffer address in each node's `word0` must be a device VA (`pcie_hostca_to_devva(buf dma)`).
Host ko `shuangta_ete_dr_dscr_fill`/`shuangta_ete_sr_dscr_fill` store it raw; the ring base
conversion plus identity window make it the host physical address. The vendor posts a 72-byte id-1
HCC frame in SR slot 0 (`docs/phase20/tx-path.md` B.1, `docs/phase20/fw-accept.md` B.1). Address
source: **node.word0** (host memory). Device reads it. Not polled.

### 2.3 ETE DR node ring (D2H descriptor ring) - SHARED, host-posts, device-writes

| property | value | source |
| --- | --- | --- |
| what | 4 channels, each `depth*8 = 256` bytes = 32 nodes | `pcie_ete_init_dst_ring` @`0x727c`, `shuangta_ete_dr_node_init_handle` @`0x17970` |
| who writes | host posts the node array; **device fills** `word0/word1` on completion | `docs/phase20/rx-loop.md` A.2 |
| address source | **DR+0x30 program register** (base) and **DR+0x38** (producer); host ko `pcie_ete_dr_reg_init` @`0x1483c` | proven |
| CA / BAR0 | DR ch3 base `CA 0x4003a590`; ch4 `0x4003a5e0`, ch5 `0x4003a630`, ch6 `0x4003a680` | `docs/phase20/runtime-msg.md` A.2 |
| device access | ETE hardware writes completion nodes back into host memory through the outbound window | proven structure |

### 2.4 ETE DR receive buffers - SHARED, host-allocated, device-writes

Host `dma_alloc_coherent` payload buffers (e.g. 4 x 2048 B per `lab/rtmsg`), pointed at by the DR
nodes. Device writes the received data there. Address source: **node.word0** (host fills for DR, or
device fills from the posted buffer list). Not polled; completion interrupt.

### 2.5 The runtime message context (`comm`) - HOST-ONLY, **not** shared

| field | meaning | source |
| --- | --- | --- |
| `.LANCHOR0 + 0x78*ctrl` | per-controller `comm` (static `.bss`) | `pcie_main_init` @`0x704` (`mla r5,r3,#0x78,r5`) |
| `comm+0x00` | per-chip object array | `oal_pcie_probe` @`0x4d0` |
| `comm+0x18` / `+0x24` / `+0x28` | waitqueue / kthread / cond flag | `pcie_thread_init` @`0x170b8` |
| `comm+0x2c..+0x40` | `out[0..5]` host VAs of the six mailbox CAs | `shuangta_pcie_msg_reg_map` @`0x1b1a0`, `pcie_msg_init` @`0xb6e4` |
| `comm+0x44` / `+0x48` | send flag / pending shadow | `pcie_msg_send` @`0x160f4` |
| `comm+0x4c` | 11-entry `{fn,arg}` handler table (kmalloc 0x58) | `pcie_msg_init` @`0xb764` |
| `comm+0x50` | spinlock | `pcie_msg_send` |
| `comm+0x58` / `+0x70` | chip count / ops table | `pcie_main_init` |

**The device never learns this address.** No `str` of `comm` (or of a `pcie_hostca_to_devva(comm)`
value) to any device register exists in `plat.ko`; `pcie_msg_init` only *writes through* `comm`'s
CA pointers, never publishes `comm`. The firmware has no literal or `movw/movt`/`ADR` that can name
it (section 3/4). **[proven negative]**

### 2.6 `g_dmac_to_hmac_read_msg[]` and the ready handshake - HOST-ONLY

| symbol | offset | writer | reader |
| --- | --- | --- | --- |
| `g_dmac_to_hmac_read_msg[core]`, stride `0x64` | `.LANCHOR0 + 0x1a8` | host `host_ready_msg_process` @`0xebd8` (`memcpy_s` @`0xec7c`) | host `hmac_main_init` |
| device-ready bitmap | `.LANCHOR0 + 0x21c` | host `host_ready_msg_process` @`0xece4` | host |
| completion #1 / #2 | `.LANCHOR0 + 0x234` / `+0x220` | host id-1 / id-2 handlers (`complete`) | `multi_chip_loading` @`0xf2ac` |
| `multi_chip_loading` state | `.LANCHOR0 + 0x218` | host @`0xf358` | host |

Source: `docs/phase19/fw-handshake.md` A.2. These are filled by the **host** when the device sends
the id-1/id-2 messages; the device does not read them. **[proven]** They are the *destination* of
the H2D dialogue, not a gate on it.

### 2.7 Handler and config tables

| table | location | type | shared? |
| --- | --- | --- | --- |
| PCIe message handler table (host) | `comm+0x4c`, 11 x 8 B | host `.bss`/kmalloc | no |
| HCC chip message table (host) | `.data+0x2660`, 5 x 12 B, group 4, ids 1/2 | host `.data` | no (`plat_init_bal_hcc_excp` @`0xe380`) |
| ETE channel config (host) | `.rodata+0x101c`, 7 x 12 B | host `.rodata` | no |
| ETE channel config (firmware) | file `0xc4054`, 7 x 12 B `{block,0x06800020,flag}` | device `.rodata` | no |
| firmware message handler table | `[*0x172130]+0xbc`, index <= 9 | device RAM | no - device register pointers only |
| device region/attribute table | file `0xc6180`, entry `0x0e` base `0x80000000` | device `.rodata` | config only |

### 2.8 The mailbox register file - the actual shared surface

The six CAs in region 3 (`CA 0x40039xxx` / `0x40101xxx`) are the only bidirectional shared state:
host-writable and firmware-readable. They are device IO, not host memory. `pcie_msg_init` zeroes
`out[0]`/`out[1]` (host ko `0xb738/0xb740`; firmware file `0x978c/0x978e`). **No host memory is
involved in the H2D mailbox handshake.**

---

## 3. The device's own copy: the firmware object and the mirrored dispatchers

### 3.1 `pcie_msg_init` builds the firmware's message context (file `0x9334`)

The routine stores its object pointer into a firmware global and maps the six mailbox CAs
(firmware `0x9758..0x9788`, cross-checked this phase):

```
09700: ldr  r3, [pc,#0xc8]     ; lit@0x97cc = 0x00172130      <-- the firmware global (device RAM)
09702: str.w r7, [r5,#0x128]
09706: str  r2, [r3]           ; *0x172130 = [sp,#4]           (object pointer)
...
09764: str.w r1, [r5,#0xd8]    ; obj+0xd8 = 0x40101434         (D2H doorbell)
09770: str.w r1, [r5,#0xdc]    ; obj+0xdc = 0x400392f0         (H2D ack)
09776: str.w r1, [r5,#0xe0]    ; obj+0xe0 = 0x400392d4         (H2D re-arm / doorbell)
09784: strd  r2, r3, [r5,#0xd0]; obj+0xd0 = 0x40039014 (out[1]), obj+0xd4 = 0x40039010 (out[0])
0978c: str   r7, [r2]          ; *out[1] = 0
0978e: str   r7, [r3]          ; *out[0] = 0
```

`*0x172130` is set at file `0x9706` to `0x10cedc - 0xde8 = 0x10c0f4` (obj `0x10c0c0` + `0x34`).
The **firmware message context base is therefore `g = *0x172130`** and its fields are
`g+0x9c..`; the handler registration helper at file `0x8187a` stores into `[g+0xbc + idx*8]` and the
dispatcher reads `ctx+0x20`, so `ctx = g+0x9c` - all consistent:

| ctx | field | value / meaning | quote |
| --- | --- | --- | --- |
| base | `g+0x9c` (= obj `+0xd0`) | message ctx | derived |
| `ctx+0` | `g+0x9c` | `out[1]` `0x40039014` (D2H pending) | file `0x9784` |
| `ctx+4` | `g+0xa0` | `out[0]` `0x40039010` (**H2D pending**) | file `0x9784` |
| `ctx+8` | `g+0xa4` | `0x40101434` (D2H doorbell) | file `0x9764` |
| `ctx+0xc` | `g+0xa8` | `0x400392f0` (H2D ack) | file `0x9770` |
| `ctx+0x10` | `g+0xac` | `0x400392d4` (H2D re-arm/doorbell) | file `0x9776` |
| `ctx+0x14` | `g+0xb0` | `0x40101418` | file `0x9788` |
| `ctx+0x18` / `0x1c` | `g+0xb4` / `+0xb8` | written-flag / pending shadow | `d2h_notify` |
| `ctx+0x20` | `g+0xbc` | handler table (11 x 8 B) | file `0x8187a` |
| `ctx+0x24` | `g+0xc0` | lock | `d2h_notify` |

### 3.2 The firmware D2H sender `0x86170` **[proven]**

```
08617a: ldr  r3, [pc,#0x60]   ; lit@0x861dc = 0x00172130   (the global)
08617c: ldr  r5, [r3]         ; r5 = g
086180: add.w r6, r5, #0xc0   ; lock
08618a: ldr.w r1, [r5,#0xb8]  ; pending shadow
086198: movs r3,#1 ; lsls r3,r7
08619a: ldr.w r2, [r5,#0xb4]  ; already-armed flag
0861a0: orrs r3, r1
0861a2: str.w r3, [r5,#0xb8]  ; shadow |= bit
0861a8: ldr.w r1, [r5,#0x9c]  ; out[1] pointer           -> CA 0x40039014
0861b0: str  r3, [r1]         ; out[1] = pending mask
0861b2: ldr.w r4, [r5,#0xa4]  ; 0x40101434 doorbell
0861bc: orr  r3, r3, #1
0861c0: str  r3, [r4]         ; doorbell |= 1
```

This is exactly the two mailbox words the released firmware emits (`out[1] 0x40` then `0x4`,
`docs/phase21/both-eps.md`): the D2H pending word comes from the **firmware's** `out[1]`, and the
host's `pcie_msg_handle` (`0x171f8`) consumes it. **[proven]**

### 3.3 The firmware H2D consumer at file `0x818ac` **[proven]**

The literal pool word `0x00172130` sits at file `0x818a8` (the previous function's `ldr r4,[pc,#0x20]`
at `0x81884` resolves there), so the dispatcher **entry is `0x818ac`**, not `0x818a8` as the earlier
phase quoted (which decoded the literal word as two `movs`):

```
0818ac: push  {r3,r4,r5,r6,r7,lr}
0818ae: mov   r6, r0              ; r6 = ctx
0818b0: cbz   r0, 0x818c8
0818b2: movs  r7, #1
0818b4: movs  r1, #0
0818b6: ldr   r2, [r0, #0xc]      ; ctx+0xc = ack
0818b8: str   r7, [r2]            ; *ack = 1                -> 0x400392f0
0818ba: ldr   r2, [r0, #4]        ; ctx+4 = pending
0818bc: ldr   r5, [r2]            ; read pending            -> out[0] 0x40039010
0818be: str   r1, [r2]            ; *pending = 0            <-- H2D MASK CLEAR
0818c0: movs  r1, #8
0818c2: ldr   r2, [r0, #0x10]     ; ctx+0x10 = re-arm
0818c4: str   r1, [r2]            ; *re-arm = 8             -> 0x400392d4
0818c6: cbnz  r5, 0x818ca
0818da: ldr   r3, [r6, #0x20]     ; ctx+0x20 = handler table
0818e8: blx   r3                  ; handler(pending)
```

With `ctx = g+0x9c` (section 3.1) the offsets resolve to `out[0]`, `0x400392f0`, `0x400392d4` and
the device table - **the dispatcher touches no host memory at all**. Its argument is a device-RAM
pointer; the caller must supply it. **[proven]**

### 3.4 Who calls `0x818ac`? - open, and it is *not* a host-populated pointer

Every static-reference scan over the blob found **nothing**:

| reference kind | result |
| --- | --- |
| `BL`/`BLX #imm` targeting `0x818a8`/`0x818ac` | 0 sites (full Thumb BL/BLX decode) |
| 4-byte literal `0x000818a8/9` or `0x000c18a8/9` | 0 sites (whole file, 2-byte aligned) |
| `movw`/`movt` pair for `0xc18a8`/`0xc18ac` | 0 sites |
| `ADR` / `add rX,pc,#imm` to `0x818xx` | 0 sites |

So the dispatcher's function pointer is installed **at runtime** into a device structure (most
plausibly by the firmware's IRQ/task framework, whose installer is `0x874b0` and enable is
`0x86ff4`, used by `pcie_msg_init` at file `0x96c8..0x96f0`). The pointer is *device* state; the
host cannot write it. **[proven** absence; runtime installation inferred**]**

Note also: the `pcie_msg_init` handler registration at file `0x9838` installs `0x85144` (the queue
processor) at table index 3 - that is the *id-3 handler*, not the dispatcher. The dispatcher is the
framework's message consumer.

---

## 4. Decisive question

> What, if anything, must a host populate **in memory** for the firmware dispatcher (`file 0x818ac`)
> to be entered, and how does the device learn that address?

**Answer: nothing in host memory. There is no host-memory publish site for it, and the dispatcher
reads no host address.** The evidence:

1. **The dispatcher's only inputs are device registers.** `ctx = g+0x9c` where `g = *0x172130` is a
   *device RAM* global set at file `0x9706`; `ctx+4/+0xc/+0x10` are pointers to device registers
   `0x40039010`/`0x400392f0`/`0x400392d4`; `ctx+0x20` is the device handler table (file
   `0x9758..0x9788`, `0x8187a`). No load of any host address occurs.
2. **The host never publishes `comm` (or any host structure) to the device.** `pcie_msg_init` only
   writes *through* the CA pointers; there is no store of `comm` or of
   `pcie_hostca_to_devva(comm)` to a register or device memory (`plat.ko` whole-module store scan
   idiom from `docs/phase20/host-window.md` A.4).
3. **Host memory outside `0x80000000..0xffffffff` is unreachable anyway** (section 1). The static
   `comm`/`g_dmac_to_hmac_read_msg[]` objects are not coherent DMA.
4. **The only host memory the device reads is the ETE SR ring and its buffers** (section 2.1/2.2),
   addressed by `SR+0x10` and `node.word0` - and the descriptor fetch is already proven live in the
   takeover (`SR ch0 DEVICE INDEX 0x10 -> 0x400`), so that address channel is *not* the missing
   piece either.

**Conclusion.** The "shared memory structure arms the H2D gate" hypothesis is **killed as stated**:
the firmware does not read the host runtime message context, and no host-memory population can make
the dispatcher run. The gate is the device-side **caller** of `0x818ac` (the firmware's internal
wake). This converges with `docs/phase22/h2d-accept.md` (every exposed register is dead) and narrows
the remaining work to the wake path *inside* the firmware, plus the one shared host-memory structure
that feeds the firmware's ETE receive path (the SR ring), which the ranked hypotheses below target.

**What the host *can* still change about shared memory** (and therefore the only memory-side
experiments that remain): the SR node ring contents and producer commit, the DR ring contents, and
the two ring base registers. Everything else in section 2 is host-only or already live.

---

## 5. Proven vs inferred

| claim | status |
| --- | --- |
| device-reachable host memory is exactly `0x80000000..0xffffffff` (outbound window identity) | **proven** (`oal_pcie_set_outbound_by_membar` @`0x9a38`, `pcie_hostca_to_devva` @`0xaefc`, live iATU) |
| firmware has a device region based at `0x80000000` (file `0xc6218`) | **proven** bytes; aperture reading **inferred** |
| SR node ring / DR node ring / DR buffers are host coherent DMA, addressed by `SR+0x10` / `DR+0x30` / `node.word0` | **proven** (`pcie_ete_*_reg_init`, `shuangta_ete_*_dscr_fill`, firmware `0x9426/0x951c`) |
| the host runtime message context `comm` is never published to the device | **proven negative** (no publish site; not in the window) |
| `g_dmac_to_hmac_read_msg[]` / ready bitmap / completions are host-only | **proven** (`docs/phase19/fw-handshake.md` A.2) |
| firmware message ctx base `g+0x9c`, fields `out[1]/out[0]/0x40101434/0x400392f0/0x400392d4/0x40101418`, table `g+0xbc`, global `*0x172130` | **proven** (files `0x9700..0x9788`, `0x8187a`, `0x86170`) |
| H2D dispatcher entry is `0x818ac` (the `0x818a8` quote decoded the literal word) | **proven** (file `0x818a8` holds `0x00172130`, the previous function's literal) |
| dispatcher reads only device registers / device table, no host memory | **proven** (file `0x818ac..0x818f2`) |
| the dispatcher has no static reference (BL/literal/movw/ADR) and is installed at runtime | **proven** absence; installation **inferred** |
| the firmware's message service is entered from its ETE receive path | **inferred** (mirror of the host's `oal_pcie_transfer_done` fan-out; `docs/phase21/service-thread.md` A.4) |

---

## 6. Ranked hypotheses for the device lane

Each hypothesis names the exact writes to perform and the observable that confirms or kills it.
"candidate write" = the host-side act; all are within the already-proven takeover write set unless
marked. Never write CA `0x400392f0` except as the dispatcher's ack (see H2); never read the RC misc
window `0x10161000`.

### H1 (highest) - the H2D message must arrive as an ETE **SR ring** delivery, not (only) as a mailbox register write; the firmware dispatcher is downstream of the ETE receive path

**Rationale.** The only host memory the device reads is the SR ring (section 2.1); the H2D mailbox
register alone never entered `0x818ac` across 20 hypotheses (`h2d-accept.md`). The vendor's id-1 frame
is a 72-byte SR payload (`tx-path.md` B.1), suggesting the real H2D carrier is the ring, with the
mailbox as a secondary notify.

**Candidate writes** (host, after release, in the proven ep0-primary/both-RC config):
- Fill SR ch0 node 0 with `word0 = msg_buf_device_va` (coherent DMA address, no offset under the
  identity window), `word1 = (72<<16) | 0x6d2b` exactly as `shuangta_ete_sr_dscr_fill` @`0x17858`
  (`bfi r2,r1,#0,d,#0xd` @`0x178b8`, `orr #0x4000`/`#0x2000` @`0x178a0/0x178ac`).
- Re-assert `SR ch0 +0x10 <= base`, `+0x14 <= 0x1f`, `+0x08 <= ctrl`, then commit
  `SR+0x18 <= 0x410` (one lap), then `SR+0x18 <= 0x000` (second lap, edge).
- Do **not** write `out[0]` on this arm (isolate the ring from the mailbox).

**Confirm:** `out[0]` CA `0x40039010` transitions nonzero -> zero with **no host write**, or an id-1
bit appears in `out[1]` (bit 1), or the firmware writes `0x400392f0 = 1` (read it back).
**Kill:** `SR+0x1c` advances (engine consumed) but `out[0]` stays `0` and no id-1/ack - i.e. the
ring alone is insufficient (the phase-20/21 result, now with the full-CA audit). This is the
decisive A/B.

### H2 - the device wake is the firmware's ETE interrupt, whose per-channel enables the host must leave intact; the host should let the firmware's own interrupt path run and only *observe* the ack

**Rationale.** `pcie_msg_init` clears ETE-intr bits 12/29 and masks `0xe0e0f8f8`
(file `0x96b0..0x96fc`), then registers the handlers `0x62f8`/`0x624c` at ids `0x2d`/`0x2e` and
enables them (file `0x96c8..0x96f0`). The host's `pcie_ete_intr_init` then ANDs `0xffe0f8f8`
(host ko `0x75b8`), leaving the vendor value `0x3f201818`. The host must not re-clear the
per-channel bits the firmware's handlers re-arm.

**Candidate writes** (CA `0x40039508`, the ETE interrupt block; writes to `+0x10/+0x14` are
ignored, `+0x00` is writable per `sr-trigger.md` B.6):
- `0x40039508 <= 0x3f201818` (vendor baseline, control), then selectively
  `0x3f201818 | (1<<n)` for n in 0/1/2/8/9/10 (SR per-channel groups), **one entry per boot entry**.
- After each, ring the H2D mailbox (`out[0] <= 0x08`, `out[2] |= 1`) and read the ack
  `0x400392f0`.

**Confirm:** `0x400392f0` reads back `1` (the firmware dispatcher's ack) or `out[0]` clears.
**Kill:** `0x400392f0` stays `0` and `out[0]` stays `0x08` across all bits (already the result of
the phase-22 ETE-intr sweep for the *enable* value; this arm differs by also reading the ack, which
was never sampled).

### H3 - the firmware services H2D only after its D2H queue drains; the host must post the DR ring on the channel the firmware's D2H notify names

**Rationale.** `d2h_notify` (file `0x86170`) maintains a pending shadow and only writes the register
when `+0xb4` is clear; the firmware's D2H path is a queue. If the H2D dispatcher is interleaved with
the D2H queue service, a stuck D2H queue could block it.

**Candidate writes:**
- Post DR nodes on all four channels (`word0 = payload device VA`, `word1 = (2048<<16)|0x6d2b`-style
  from `shuangta_ete_dr_dscr_fill`/host ko `0x1765c`), commit `DR+0x38` per the host's
  `pcie_ete_dr_reg_init` @`0x1483c`.
- Ring `out[1]`-side service: have the host consume any `out[1]` mask (clear it, ack `0x40101438`,
  re-arm `0x40101414`) and then ring H2D (`out[0] <= 0x08`, `out[2] |= 1`).

**Confirm:** DR node `word0/word1` change (device wrote a completion) **and** `out[0]` clears.
**Kill:** DR nodes move but `out[0]` never clears (the queues are independent), or neither moves.

### H4 - the firmware's `pcie_msg_init` re-programs `SR+0x10`/`DR+0x30` from its own channel object; the host must verify and, if needed, re-assert the ring base after the firmware's init

**Rationale.** The firmware writes these registers at boot (file `0x9426`, `0x951c`) from its
channel object; the host writes them at probe. If the firmware's value ever wins, the engine reads a
different host address than the host's ring.

**Candidate writes:**
- Read back `SR ch0 +0x10`, `+0x14`, `DR ch3 +0x30`, `+0x34` post-release. If
  `base != pcie_hostca_to_devva(host ring dma)` re-write exactly the vendor values (`base = ring
  devva`, `depth-1 = 0x1f`) and re-commit `SR+0x18`.
- Additionally program ep0's outbound viewport (`BAR2 0x41800000+0x000..0x018`) to
  `{0x000=0,0x004=0x80000000,0x008=0x80000000,0x010=0xffffffff,0x014=0x80000000,0x018=0}` on **both**
  RCs (the queued H1 of `NEXT-EXPERIMENT.md`).

**Confirm:** a fetch that was failing starts (`SR+0x1c` advances), then any H2D observable.
**Kill:** base already matches and the fetch already works (measured: it did in `sr2`/`sr-sibling`),
so this cannot be the accept gate - it only protects the other arms from a stale base.

### H5 (ranked last; killed by static analysis) - the firmware polls/reads the host runtime message context in host memory

**Rationale from the task brief.** `comm = .LANCHOR0 + 0x78*ctrl` is host memory; if the firmware
read it, no register would arm the gate.

**Falsified here.** The firmware's context is device RAM (`*0x172130 = 0x10c0f4`, file `0x9706`), its
fields are device register pointers, the dispatcher reads no host address, no host pointer is
published, and host `.bss` is outside the reachable window. A residual check, if the device lane
wants it: allocate one coherent page with a plausible ctx image at a known `0x8xxxxxxx` host
physical address and watch for any device read (only observable with JTAG/ROM-monitor, or
indirectly by seeing whether the firmware's `out[0]`/ack behavior changes at all).

**Confirm:** impossible from the host side; would require a device-side trace.
**Kill:** the static facts above (no reference, no publish, out-of-window). Treat this as closed
unless a firmware trace contradicts it.

---

## 7. One-line answer

The device does **not** read the host's message context; the firmware builds its own ctx in device
RAM pointing at the mailbox registers, and its H2D dispatcher (`file 0x818ac`) reads only those
registers and a device handler table. The only shared host memory is the ETE ring data path, whose
address is published by `SR+0x10`/`DR+0x30` and node `word0`. The accept gate is therefore the
firmware's internal wake of that dispatcher, not a host-memory population - so the device lane's
memory-side levers are exactly H1-H4 above, all on the SR/DR ring and the ETE interrupt/glue state.
