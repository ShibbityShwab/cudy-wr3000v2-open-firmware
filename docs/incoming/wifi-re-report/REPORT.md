# The Hi5622V100 Wi-Fi / chip protocol: a consolidated reverse-engineering report

**Project:** Cudy WR3000 v2.0 (luofu v2 board) bench reversal - Hi5671YV200 SoC + Hi5622v100 Wi-Fi
endpoint, stock OpenWrt 22.03.6 / kernel 5.10.201, vendor firmware 928,920 B.

**Document:** `wifi-re-report/REPORT.md`, umbrella narrative. **Date:** 2026-11-25.
**State cut-off:** `mem-entries.md` entry 2026-11-25 06:00 (the newest ledger entry at
consolidation time); superseded claims are catalogued in section 8. **Status:** the full duplex
HCC protocol and the operational message lap are hardware-validated; the Wi-Fi interface and data
path are not. Detail sources remain the lane reports listed in section 12; this document is the
publication-grade synthesis, not a replacement.

**Lane / task:** wifi-re-report, shared task `task-109`. Host-only: no device, UART, TFTP or
network contact was made while writing it. **Safety:** nothing in this report authorises a bench
fire; every hardware session cited here was attended and followed the project's recovery-door
rules.

---

## 0. Document control

### 0.1 How to read this report

This report is about **what the chip does and how our clean-room host drives it**, ordered as the
investigation actually happened. It is written so that a reader with the per-lane reports open can
move between the narrative and the evidence in both directions. For every claim there is an
evidence marker and, in [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md), the artifact or ledger entry that
supports it.

Three audiences are served: (a) the project itself, as the reference narrative after a long
multi-lane investigation; (b) reviewers of the public notebook who need to distinguish verified
from speculative; (c) future maintainers who must not repeat the expensive mistakes documented in
section 8.

### 0.2 Companion files

| file | content |
| --- | --- |
| [TABLES.md](TABLES.md) | address maps, ROM map, golden state, protocol tables, frame hex dumps, patch recipes, timing constants |
| [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) | claim -> marker -> artifact/ledger entry, grouped by report section |
| [REPRODUCE.md](REPRODUCE.md) | static reproduction commands and bench-ladder pointers |
| [README.md](README.md) | lane index and ground rules |
| [tools/verify_report.py](tools/verify_report.py) | device-free verifier for this report |

### 0.3 Evidence markers

| marker | meaning | example |
| --- | --- | --- |
| `[V]` | re-verified by the owning lane from a named local artifact (vendor module, firmware blob, ROM dump, image) with a committed tool | "the 12-byte header is built by `hcc_msg_alloc`" [V] |
| `[V*]` | measured on the bench in an attended session by an earlier lane; cited by ledger date | "golden page = 0x86807FEF/7/2/0xABCD" [V*] |
| `[I]` | inference from decoded code/data, not directly measured | "the wifi-custom block is PHY/radio configuration" [I] |
| `[U]` | unknown; needs a device read, ROM/vendor evidence, or is outside the instrument's reach | "the relationship between the two firmware hcc objects" [U] |

A single sentence may carry more than one marker when it mixes measured and inferred parts.
**No claim in this report is stronger than its marker.** In particular, "the protocol works" means
*the two messages we sent were consumed and answered* - not that the whole Wi-Fi stack works.

### 0.4 Provenance and non-goals

The report synthesises committed lane deliverables under `opensource/docs/incoming/`. It did not
re-read the vendor blobs to invent new facts; where a claim needed re-checking, the owning lane's
committed self-testing tool was re-run (section 9.1). The non-goals are the same as the project's:
this is not an upstream OpenWrt submission, not a shippable driver, and not a claim of clean-room
reimplementation of the closed firmware.

---

## 1. Executive summary

### 1.1 The result

We can boot the Hi5622V100 Wi-Fi endpoint from our own clean-room host, bring its closed firmware
to the exact state a working vendor boot reaches, receive its device-to-host protocol frames, and
exchange real host-to-device protocol messages that the firmware processes and answers - all on
our own kernel/driver, using no vendor host code.

The complete chain, each link hardware-validated:

```
SoC GPIO endpoint reset pulse
  -> chip mask ROM S0/S1 runs
  -> ROM <-> image boot handshake completes (0xCEED gate preserved; page +8 = 2)
  -> firmware golden boot (page 0x86807FEF/7/2/0xABCD, state 1, mask 0)
  -> device->host ready handshake decoded (g4 id1 + g4 id2, payload 0x01060850)
  -> host->device context-create (g0 id1, key 0x64, type 1) allocates ctx+0x190
  -> host->device TXQ config sync (g0 id1, cmd 0xD8) consumed with ZERO exceptions
  -> firmware responds with new D2H frames (g1 id3, g1 id0x12)
```

That is the project's Wi-Fi-side core RE goal: **the full duplex HCC protocol works on a
clean-room host**. The next phase is the rest of the vendor startup sequence (calibration, VAP,
scan) and the cfg80211 interface - see section 10.

### 1.2 What is proven, and what is not

| claim | status |
| --- | --- |
| host can reset and boot the chip's mask ROM (S0/S1) | **proven** [V*] |
| ROM<->image handshake contract (0xCEED, page +8 = 2, entry A/B) | **proven** [V]/[V*] |
| golden firmware state achievable repeatedly | **proven** [V*] |
| device->host ready handshake and other D2H frames readable | **proven** [V*] |
| H2D transport (SR ring commit + mailbox announce) | **proven** [V*] |
| firmware processes real H2D messages and answers | **proven** for the two ordered messages [V*] |
| firmware IRQ/thread layer is alive on our boot | **not proven** - bypassed by the PD3/PD4 research patch; without it the mailbox is never serviced [V*] |
| ETE engine A->B DMA copy | **not proven** - firmware-triggered and idle; direct read substitutes for D2H [V*]/[I] |
| Wi-Fi operational interface (VAP, scan, association) | **not started** - next phase |
| data path (hdpp/hmac TX/RX) | **not started** |
| upstreamable clean-room driver | **blocked** by the closed firmware blob (section 11) |

### 1.3 The breakthrough ladder

The investigation reached the result through eight hardware-validated breakthroughs; each is also
a correction of the then-current model (section 8 gives the full register):

| # | date | breakthrough |
| ---: | --- | --- |
| 1 | 2026-11-22 | clean-room transport reproduces the PCIe lifecycle: six iATU viewports, contiguous 928,920 B upload, D2H announce handling [V*] |
| 2 | 2026-11-24 01:15 | the missing host action is a SoC GPIO endpoint reset pulse; with it the mask ROM S0/S1 runs and the ROM handshake can complete - but flakily [V*] |
| 3 | 2026-11-24 09:20 | the chip mask ROM is host-readable; the 16 KiB ROM is dumped and reversed (2,073 B content) [V*]/[V] |
| 4 | 2026-11-24 10:00 | root cause of the S1 stall: **our own 0xCECE gate writes clobber the ROM's 0xCEED handshake**; the boot protocol becomes release-only [V]/[V*] |
| 5 | 2026-11-24 10:40 | **GP8**: firmware boots exactly like the vendor (golden page, state 1, mask 0) [V*] |
| 6 | 2026-11-24 13:10-14:00 | D2H frames are real and readable directly from device rings; the complete ready handshake is received [V*] |
| 7 | 2026-11-24 22:10 | **PD3**: a guarded firmware patch calls the firmware's own H2D dispatcher from the boot-task epilogue, so the firmware mailbox consumer works without the dead IRQ layer [V*] |
| 8 | 2026-11-25 05:10 | **ordered operational loop**: context-create type 1 -> TXQ config sync, both consumed with zero exceptions, firmware responds [V*] |

### 1.4 Key numbers

| quantity | value | evidence |
| --- | ---: | --- |
| firmware image | 928,920 B, sha256 `7fc87e20...ef311b` (pinned local blob) | [V] |
| mask ROM content | 2,073 B of 16,384 B window; sha256 `fcfd464f...8faa4` (local dump) | [V]* |
| BAR0 windows | 6 inbound iATU viewports, 16 MiB total | [V*] |
| handshake page | CPU 0x1BF000 = `0x86807FEF / 0x7 / 0x2 / 0xABCD` | [V*] |
| boot state / exception mask | CPU 0x118B60 = 1; CPU 0x40008 = 0 | [V*] |
| ready-handshake latency | id1 within 2.0 s; id2 within 20 s (observed ~1.2 s total) | [V*] |
| HCC frame header | 12 B, tag `0x5A5A`, total = payload + 12 | [V] |
| H2D receive slots | 60 (group 0: 41 incl. early 0x2F; group 2: 19) | [V] |
| real H2D messages sent+answered | 2 ordered (context-create, TXQ sync); 1 control stub | [V*] |
| H2D announce consumption | `out[0]` 0x8 -> 0 in 0 polls, `h2d_complete done=1` | [V*] |
| firmware responses | g1 id3 (36 B), g1 id0x12 (40 B) | [V*] |

---

## 2. System map: the endpoint, the host, and the layers between

### 2.1 The two sides

The board has two PCIe root complexes; the Wi-Fi endpoint is a separate die (Hi5622V100) behind
one RC, enumerated as vendor:device `59e7:0005`, class `0x028000` [V*]. Inside the endpoint there
is a dual-Cortex-A9-class CPU complex running a **closed 928,920 B firmware**, a 16 KiB mask boot
ROM, a small WRAM, CPU1 TCM windows, PKTRAM for ring nodes, ACP SRAM for frame bodies, and an IO
block containing the boot gate, the mailbox and the "ETE" DMA engine [V]/[V*].

The host side is ours: an OpenWrt 6.18.60 kernel with an original DWC-based `luofu` PCIe
controller driver (candidate patches 013/013a) plus an original bench-instrument module that
implements the transport, the boot protocol and the HCC service. No vendor host module is loaded
in our stack; the vendor host stack is only ever used as a read-only reference (stock pair A) for
golden-state capture and kprobe instrumentation [V*].

### 2.2 Address spaces and translation

Three address spaces appear constantly and confusing them is the most expensive class of mistake
in this project:

1. **Device CA** - the endpoint's internal address space, reachable through the RC windows.
   Device IO CAs are `0x40000000..0x4011FFFF`; the boot gate and mailbox live there.
2. **Firmware CPU address** - the firmware's flat CPU view (region 5), including its own image at
   CPU `0x40000` (= device CA `0x01240000`), the handshake page at CPU `0x1BF000`, and the
   image-header exception area at CPU `0x40000..0x4004B`.
3. **Host BAR0 offset** - what our driver actually writes through the RC outbound window.

The translation rules (full table in [TABLES.md](TABLES.md) T2):

```
device IO CA  -> host BAR0 :  0x3B8000 + (CA - 0x40000000)      (region 3)
firmware CPU  -> host BAR0 :  0x6B8000 + CPU                    (region 5)
host phys     = 0x40000000 + BAR0_offset                        (endpoint 0, bench)
image file    -> CPU       :  CPU = file + 0x40000
mask ROM      -> CPU       :  CPU = ROM dump offset (ROM mapped at CPU 0)
```

The trap: the two formulas look interchangeable but are not. The inherited transport macro
`HI5622_BAR0_IO(ca) = 0x3B8000 + (ca & 0xFFFFF)` masks the CA to 20 bits, which is only correct
for CAs below `0x40100000`; the D2H ack/re-arm registers at `0x40101438/0x40101414` must map to
BAR0 `0x4B9438/0x4B9414`, not `0x3B9438/0x3B9414`. That 1 MiB error silently hid for a long
time because `out[1]` (CA `0x40039014`) is below the boundary (finding F6) [V*].

The second trap: CPU addresses below the IO block (e.g. `0x40008`, `0x4000C`, `0x1BF000`) are
**firmware CPU** addresses, not device CAs. "mask CA 0x40008" in older notes means CPU
`0x40008`, i.e. BAR0 `0x6F8008`, and the "exception record area CA 0x4000C" is CPU
`0x4000C` = BAR0 `0x6F800C` = phys `0x406F800C`. The device IO gate is a different register at
CA `0x4000010C`. This report always writes "CPU" or "device CA" explicitly.

### 2.3 The stack, layer by layer

```
+---------------------------------------------------------------+
| Wi-Fi userspace (stock OpenWrt): netifd, hostapd/wpa_supplicant, iw  |
+---------------------------------------------------------------+
| cfg80211 full-MAC interface (the 39-op vendor surface)             |  <- not implemented yet
+---------------------------------------------------------------+
| HCC message layer: 12-byte header, groups 0/1/2/3/4, mailbox ids  |
|   H2D: SR ring commit -> out[0] mask -> out[2] doorbell -> id3     |
|   D2H: out[1] bitmap -> ack out[3] -> read rings -> re-arm out[4]  |
+---------------------------------------------------------------+
| ETE rings: SR0-2 (host->device) and DR0-3 (device->host)           |
|   node = 8 B {devva, (len<<16)|0x6D2B}; depth 32; packed indices   |
|   engine A->B copy is firmware-triggered and idle on our boot      |
|   (direct read of the device group-A rings is our substitute)      |
+---------------------------------------------------------------+
| PCIe transport: BAR0 six windows, iATU viewports, mailbox regs     |
+---------------------------------------------------------------+
| Chip: mask ROM S0/S1 -> closed firmware (CPU0 image + CPU1 TCM)    |
+---------------------------------------------------------------+
```

### 2.4 Text schematic of the data paths

```
HOST (our 6.18 kernel)                 ENDPOINT (Hi5622V100)
+--------------------------+          +--------------------------------------------+
| luofu-pcie RC driver     |          | mask ROM (16 KiB; S0 CPU0 / S1 CPU1)       |
|  - endpoint reset pulse  |  PCIe    | closed firmware (CPU0 entry 0x46970,       |
|  - six inbound windows   |<-------->|   CPU1 entry 0xC742C, boot task 0x47628)   |
|  - BAR0 MMIO             |          | handshake page CPU 0x1BF000                |
|                          |          | boot gate CA 0x40000108/0x4000010C         |
| hi5622_wifi.ko (ours)    |          | mailbox out[0..5]                          |
|  - transport/boot/HCC    |          | ETE engine (channels at 0x4003A000)        |
|  - direct D2H reader <---+--PKTRAM--+ DR0-3 ring nodes CA 0x0106xxxx              |
|  - H2D sender -----------+---ACP----+ frame bodies CA 0x0200xxxx                  |
|                          |          | IO block CA 0x40000000..0x4011FFFF          |
+--------------------------+          +--------------------------------------------+
```

### 2.5 Device-side address map (the parts that matter)

| device range | content |
| --- | --- |
| CPU `0x00000-0x003FFF` | mask boot ROM (readable 16 KiB window) |
| CPU `0x0040000+` / CA `0x01240000` | firmware image (upload target) |
| CPU `0x010938` | ROM status word |
| CPU `0x040008` / `0x04000C` | exception-seen bitmap / first-fault context frame |
| CPU `0x10C0F4` / `0x10C190` / `0x10F1F0` | firmware message object / dispatcher hcc instance / hcc ring records |
| CPU `0x118B60` | boot-task state |
| CPU `0x17D42C` | boot/FIQ slot (value 0x000C19D1 on a golden boot) |
| CPU `0x1B2800` / `0x1B2860` | platform-custom block / wifi-custom block |
| CPU `0x1BF000` | ROM<->image handshake page |
| CA `0x00400000` / `0x00410000` | CPU1 ITCM / DTCM windows |
| CA `0x01000000+` | PKTRAM (device D2H ring nodes) |
| CA `0x02000000+` | ACP SRAM (device D2H frame bodies) |
| CA `0x40000000` | IO block start (boot gate at +`0x108`, mailbox at +`0x39010`, ETE at +`0x3A000`) |
| CA `0x40100000+` | MAC-side message registers (out[3]/out[4]/D2H doorbell) |

---

## 3. The boot chain

### 3.1 Method and evidence

The boot chain was reconstructed from three independent sources that agree: (a) the 16 KiB mask
ROM dump, disassembled by `wifi-rom-dump/tools/romdis.py` (86/86 checks) [V]; (b) the firmware
image, disassembled by the fw-postboot/rom-flow/ete-engine lanes [V]; (c) bench measurements of
the handshake page, state and exception mask across GP1-GP10 and the PD3/PD4 sessions [V*]. The
ROM is a real mask ROM: it contains 2,073 bytes of code/data and no hidden body - the readable
window **is** the whole relevant ROM, which is what made the root cause findable at all [V].

### 3.2 S0: reset and the CPU0 primary path

At the reset vector the ROM sets up caches/MPU off, per-mode stacks, an ARMv7-R-style MPU region
map (whole-space default, 4 MB IO at `0x40000000` XN/device, 2 GB RAM window), then splits on
`MPIDR & 3` into a primary (CPU0) and a secondary (CPU1) path [V].

The CPU0 path (`0x50C`/`0x54C`) is a cold fallback with one host dependency:

1. clear the stale image header entry-A pointer (store 0 to CPU `0x40000`);
2. write `0xBAAD` to the boot gate CA `0x4000010C`;
3. run the analog/PHY bring-up and wait for the AFE;
4. **poll CA `0x40000108` until it equals `0x00005A5A`** - this is the host release;
5. write `0xCEED` to CA `0x4000010C` (16-bit), set handshake page bit8;
6. re-read the image header entry A (CPU `0x46970`) and `blx` it.

The consequence for the host is absolute: **the firmware must be uploaded before the release
write**, because the ROM re-reads the entry pointer the host firmware provides [V].

### 3.3 S1: CPU1 and the handshake ACK

CPU1 runs the secondary ROM path ROM `0x564`. It clears its own handshake words, arms, then
waits (WFI loop) until **both**:

* handshake page word `+C` (CPU `0x1BF00C`) equals `0x0000ABCD` - written by image entry A, and
* the boot gate CA `0x4000010C` reads `0x0000CEED` - written by the CPU0 ROM path after the
  release.

Only then it executes `strd r1,r0,[r3,#4]` with `r0 = 1 << (MPIDR & 3)`, i.e. **writes
handshake page `+8 = 2`** at ROM `0x5FE`, sets page `+4 |= 4`, and `blx`-es image entry B
(CPU `0xC742C`) [V]. The image never writes 2 - it only consumes the ACK [V]. The golden
handshake page is therefore a three-party artefact: image entry A wrote `0xABCD`, the ROM wrote
`0xCEED` and `2`, and the host's only job was to not disturb the gate.

### 3.4 The handshake page contract

```
CPU 0x1BF000  (BAR0 0x877000, phys 0x40877000)
  +0x00  0x86807FEF   progress bitmap; bit14 set only after +8 == 2
  +0x04  0x00000007   vendor golden value
  +0x08  0x00000002   S1 ACK written by mask ROM CPU1 only (1 << core)
  +0x0C  0x0000ABCD   request written by image entry A
  +0x10  changes       non-contract value (stock read 0xE2F16F38; ours 0xE0F16F38)
```

Additional boot-state observables that form the golden gate: state CPU `0x118B60 == 1` (the boot
task ran) and exception-seen bitmap CPU `0x40008 == 0` (no exception was taken) [V*].

### 3.5 The critical gate: one 16-bit register, two handshakes

This is the project's single most important finding (F1). CA `0x4000010C` is used **twice** with
mutually exclusive values:

* the **ROM boot stage** needs `0xCEED` there to survive until CPU1's S1 check passes;
* the **image service/exception handlers** zero that register when entered and then spin waiting
  for `0x0000CECE` (image handler code at CPU `0xC20F8`/`0xC6E14`).

Our original transport, believing the announce path required it, wrote `0xCECE` to this register
during boot and kept re-writing it. That destroys the ROM's `0xCEED`, so CPU1's S1 check can
never pass, the ACK `+8 = 2` is never written, entry B never runs and the boot task never starts.
The vendor host **never writes `0xCECE`** (independent scan of all 60+ vendor modules: 0 hits)
and its boot has mask 0, so `0xCEED` survives [V]/[V*].

The corrected host protocol is subtractive, which is why it took a ROM dump to find:

1. upload the image and pre-seed the custom blocks;
2. write `0x00005A5A` to CA `0x40000108` (release) - **only** that;
3. wait for handshake page `+8 == 2` (latch once; never re-gate on a live page read, because an
   exception path may mutate the page);
4. only then, if an image handler is observed spinning, service `0xCECE` as a bounded,
   evidence-driven acknowledgement.

Finding F1 has a corollary that is itself a correction: the "S1 is ROM/SoC-internal and not
host-triggerable" conclusion reached at 2026-11-24 08:45 was wrong - the missing ingredient was
removing our own interfering write.

### 3.6 Host-side pre-release obligations

The kprobe capture of the working vendor stack (2026-11-24 07:20) gives the exact pre-release
sequence the host must reproduce [V*]:

```
firmware_download()
  bal_write(dst=CA 0x1240000, data=firmware chunks)      -> our: one contiguous upload + verify
  dev_custom_send / bal_write(dst=CA 0x1B2800)            -> platform-custom block, 24 words
  bal_write(dst=CA 0x1B2860)                              -> wifi-custom block, 618 words
  bal_write(dst=CA 0x40000108, value=0x5A5A)              -> release
```

Also required: ETE rings armed and D2H service running **before** the release (the vendor does
this) so the ready handshake cannot be missed [V]. Our bench ladder does exactly that
([TABLES.md](TABLES.md) T19).

The custom blocks matter: without the platform block entry A cannot copy the CPU1 ITCM/DTCM
payloads (the four section bounds at `+4..+0x10`), and without the TCM payloads CPU1's S1 path
cannot run. GP3-GP5 established this experimentally: with the four bounds only, the TCM loaded but
S1 still did not run; with the full 24-word block the TCM became byte-identical to the vendor
(including the coreword `0x37C8F8D3`) - and S1 *still* did not run, because of F1 [V*].

### 3.7 Post-S1: image execution and the golden state

After S1 the picture is exactly the vendor's:

| observable | value | meaning |
| --- | --- | --- |
| page | `0x86807FEF / 7 / 2 / 0xABCD` | handshake complete |
| state CPU `0x118B60` | 1 | the boot task ran |
| mask CPU `0x40008` | 0 | no exception |
| TCM windows | `0xE59FF018 / 0x40034000 / 0x37C8F8D3` | CPU1 payloads byte-identical to vendor |
| boot/FIQ slot CPU `0x17D42C` | `0x000C19D1` | boot machinery armed |
| `out[1]` | `0x40` initially, then D2H traffic | firmware deposits/stages messages |

The firmware then posts its ready handshake: `g4 id1` (device plat ready) and `g4 id2`
(DEVICE READY) with the 4-byte payload `0x01060850` - a device-RAM address the vendor host stores
in its per-core read-message record [V*]. On our boot both arrive well inside the vendor bounds
(id1 within 2.0 s, id2 within 20 s; observed total ~1.2 s) [V*].

### 3.8 The layer that stays dark, and why we can still work

Even with a golden boot, the firmware's own IRQ/thread layer does **not** become active on our
boot. The evidence: the event-controller enable and scheduler-timer reads, when correctly mapped,
show the scheduler was armed (an earlier "fingerprint absent" reading was an instrument aliasing
bug - see corrections C7), but the firmware's mailbox consumer never acks because **event 0x4C is
never delivered** to the image dispatcher. No H2D message can create that event: the vendor host
sends nothing between release and ready, and no H2D handler reaches the dispatcher (the
`wifi-hcc-link` and `wifi-fw-postboot` lanes closed that negative) [V]/[V*].

Two host-side answers were developed instead of fighting the closed IRQ layer:

* **receive** (D2H): read the device's own group-A rings and frame bodies directly through the
  BAR0 windows - the frames are staged by the firmware regardless of the engine (finding F4);
* **send** (H2D): PD3/PD4, a firmware patch that calls the firmware's own H2D dispatcher wrapper
  from the boot-task epilogue, gated by an `out[0]` poll (finding F3). This is a **research
  instrument**, not a product change.

### 3.9 Boot decision ladder (text form)

```
power-on
  |
  +-- vendor stack (stock boot): GPIO reset pulse -> ROM S0 -> CPU0 cold fallback
  |     -> waits 0x5A5A -> writes 0xCEED -> entry A -> page +C = 0xABCD
  |     -> CPU1 S1: page +C == 0xABCD && gate == 0xCEED -> page +8 = 2, entry B
  |     -> boot task -> ready handshake -> working Wi-Fi stack
  |
  +-- our transport, BEFORE the fix: pulse -> ROM S0 -> upload -> release -> **0xCECE writes**
        -> 0xCEED destroyed -> CPU1 S1 loops forever -> page +8 = 0
        -> (GP1 flaky "successes" were stale vendor WRAM custom blocks from earlier stock boots)
  |
  +-- our transport, AFTER the fix (GP8+): pulse -> ROM S0 -> upload + custom blocks
        -> release 0x5A5A ONLY -> page +8 = 2 -> golden state -> ready pair
        -> [PD3/PD4 patch for the mailbox consumer] -> H2D messages answered
```

---

## 4. Critical findings

Six findings carry the investigation. Each is stated as symptom -> root cause -> evidence -> fix ->
residual risk, so that a future reader can tell what is settled and what is not.

### F1. The `0xCEED` boot handshake was being clobbered by our own `0xCECE` writes

**Symptom.** The chip would reach a "pre-S1" state (page `0x86803FEF / +8 = 0 / 0xABCD`, state 0,
mask 0) or, in other sessions, a RAM-fallback page; the CPU1 ACK `+8 = 2` never appeared, entry B
never ran, the boot task never started. GP1 sometimes worked (2/4 boots), GP2-GP6 never did.

**Root cause.** CA `0x4000010C` is a dual-use 16-bit register (section 3.5). Our transport wrote
`0xCECE` during boot (meant as the image's announce gate), overwriting the ROM's `0xCEED`. CPU1's
S1 WFI loop requires `0xCEED`; it never passes; no ACK is written.

**Evidence.** ROM disassembly at ROM `0x506` (CPU0 writes `0xCEED`) and ROM `0x5FE` (CPU1
writes `1<<core`), image handler code at `0xC20F8`/`0xC6E14` spinning for `0xCECE`, the
subtractive experiment GP8 (release-only) producing the exact vendor golden page, and the
pre-fix GP2-GP6 differential [V]/[V*]. The GP1 flakiness is explained: the successful runs
consumed a stale-but-valid vendor platform block left in chip WRAM by earlier stock boots [I].

**Fix.** Release-only boot: write `0x00005A5A` to CA `0x40000108` and nothing else during boot;
service `0xCECE` only after the handshake is latched, and only in response to observed evidence
(`out[1]` bit2 / mask change). The clean driver encodes this as a latched boot gate and a bounded
evidence-only CECE policy [V]*.

**Residual risk.** The image's real CECE service role (what exactly the vendor's vector handler
expects) is only partly understood; our successful ordered lap wrote no CECE at all, and a later
flow may need the exact vendor ordering.

### F2. The exception-staging overlap: a "safe" firmware code cave was not safe

**Symptom.** The PD3 patch (stub in the image-header exception-record area CPU `0x4000C`) worked
for the first mailbox message but the first-fault record was unreadable - it contained a *later*
exception frame, and the fault looked like "the mailbox consumer died" even though it had worked.

**Root cause.** The image's exception-staging routine writes its 17-word frame **exactly over**
CPU `0x4000C..0x4004B` - the same bytes the PD3 stub occupied. After the first (data-abort)
exception the poller's code was corrupted; executing garbage produced a secondary undef exception;
the staging area keeps only the last frame, so the true first-fault PC was lost.

**Evidence.** PD3 bench (2026-11-24 22:10) consumed the announce; the next session (2026-11-25
00:20) read the exception record after a TXQ-sync fault and found the 17-word frame with
`pc = 0x00171E90` (WRAM data, not code), `sp = 0x001B9FC8`, `lr = 0x00041E4F`, and values
`0x00081039`/`0x00043DF5` pointing into the g0 id1 handler path. PD4 relocated the identical stub
to CPU `0x108240`, inside a 637-byte all-zero static-data alignment gap with zero branch/literal
references, outside both boot memsets and the staging area; after firing, the cave bytes were
still present, proving the relocations [V*].

**Fix.** PD4 stub home; the PD3 cave is deprecated for first-fault analysis (it remains
bench-validated for mailbox consumption). The corrected patch-entry bytes and image hash are in
[TABLES.md](TABLES.md) T15.

**Residual risk.** Even with PD4 the firmware's exception path cascades dabt -> undef, so the
first-fault frame is still not directly readable in the PD4 confirmation session; the current
best evidence for the g0 id1 fault is the static handler analysis plus the pd4 lane's
field-B context proof.

### F3. Bypassing the dead IRQ layer with the firmware's own dispatcher (PD3/PD4)

**Symptom.** With a golden boot, host->device frames reached the device ring but the firmware never
acked the mailbox: `out[0]` stayed `0x8`, no `out[1]` bit2, `h2d_timeout` lane closed. The
firmware's H2D dispatcher (CPU `0xC18AC`, wrapper `0x40294`) is only reached from the image
vector handler `0x86E14` on event 0x4C - and that event is never delivered on our boot [V].

**Root cause.** The firmware's IRQ-unmask/thread layer never runs on our boot (section 3.8). This
is not a host-fixable register: the event controller, GIC and scheduler are firmware/ROM-internal
and the vendor sends nothing pre-ready that could kick them.

**Fix (research).** PD3 redirects the boot-task epilogue (file `0x916A`, CPU `0x4916A`) into a
64-byte guarded stub that polls `out[0]` and, only when pending, calls the firmware's own
dispatcher wrapper `0x40294` with the correct hcc instance context; PD4 moved the stub to the
truly-safe cave. On the bench PD3/PD4 consumed every announce in 0 polls (`h2d_complete done=1`,
`h2d{pub=... done=1}`) without breaking the golden page, and the ordered lap then ran with mask
staying 0 [V*].

**Evidence.** Bench sessions 2026-11-24 22:10 (PD3, announce consumed), 2026-11-25 01:10 (g0 id0
control stub consumed, no exception), 02:10 (PD4 confirmation), 05:10 (ordered lap, both messages
consumed, firmware responds) [V*]. Dispatcher contract from PD2 analysis: `r0` = hcc instance
CPU `0x10C190`; fields `+4` = `out[0]` CA `0x40039010`, `+0xC` = firmware ack CA
`0x400392F0`, `+0x10` = doorbell CA `0x400392D4`, `+0x20` = handler table; preserves
`r3-r7`; no per-core context needed [V].

**Residual risk.** This is a patched vendor image - a bench instrument only. It proves the protocol
and gives us a send path; it is not a route to a shipped product, and it does not revive the rest
of the firmware's thread/scheduler services (the ETE copy trigger and any timer-driven flow remain
dark).

### F4. Direct device-ring read: D2H without the ETE engine

**Symptom.** The device's D2H pipeline stages frames, but the ETE engine that should mirror them
into host DRAM stays idle (`status CA 0x4003A874 = 0`, send words 0, fills static). Earlier work
concluded the D2H service could not be entered without waking the engine.

**Root cause / discovery.** The firmware is not required to use the engine for us to read its
output: it stages 8-byte nodes in the **device** group-A DR rings (PKTRAM, CA `0x01060110` /
`0x01060440`) and the frame bodies in device ACP SRAM (CA `0x02000000+`). Both are inside BAR0
regions the host already owns. The engine's A->B copy is firmware-triggered (the only kick is the
sending trigger CPU `0xC625A` via event 0x2E, itself fed by device-authored status) and no host
write can start it [V]/[I].

**Fix.** A direct-read D2H receive service (`wifi-d2h-direct`): poll the device group-A rings
through BAR0, decode the 12-byte header, validate `total == node len`, catalog by (group,id),
deliver the ready pair, EXCP_INFO, heartbeats, temp-state frames. Consumer advance: the device
`+0x1C` is engine-only; the host-owned `+0x38` republish is the only vendor-analog index write
[V]*.

**Evidence.** Bench 2026-11-24 13:10 (frames decoded from the staged rings) and 14:00 (complete
ready handshake: id1 + id2 payload `0x01060850`, 21 frames, overruns 0) [V*].

**Residual risk.** The direct-read path is a research instrument: it depends on layout constants of
this firmware, and the engine's copy (if it ever runs) would be the vendor's actual mechanism. It
does not resolve why the trigger never fires.

### F5. The context-create precondition and the ordered operational loop

**Symptom.** Sending the vendor's first post-ready frame (TXQ config sync, `g0 id1` cmd
`0xD8`) as the first real H2D message faulted the firmware (`mask 0x12`, exception frame
`pc` in WRAM data). A payload-free `g0 id0` control stub was consumed cleanly, proving the
transport.

**Root cause.** The `g0 id1` key-`0xD8` handler resolves `hdr[3]` as a module index and
dereferences the module's context buffer at `ctx+0x190` (worker CPU `0x81039`/`0x43DF5`).
Only the context-create helper (`CA 0xFE6BC`) writes that field, and on our boot it is NULL:
the boot's own `0xFE6BC` call creates module 18 with type 0 and takes the no-alloc path. The
H2D route to `+0x190` is a **config event with key `0x0064`** (group 0 / id 1), whose data
block selects the module index (`data[4] = 4`) and the create type (`data[0] in {1,2,3}`).
Type 3 took the no-alloc branch on the bench; **type 1 allocates** [V*].

**Fix.** An ordered lap: context-create type 1 -> read-only pack -> TXQ config sync. On the bench
the type-1 create wrote `ctx+0x190 = 0x01011448`, `ctx[0] = 4`, `ctx+0x10 = 0x00010001`,
`ctx+0x434 = 0x0100478C`; the following TXQ sync was consumed in 0 polls with **mask 0 and no
staging frame**, and the firmware answered with `g1 id3` and `g1 id0x12` [V*].

**Residual risk.** The key-0x64 frame is **synthesized from the firmware parser, not captured
from the vendor driver**; the vendor's sender for this key is unproven [I]. The two response frames
are observed but only partially decoded. The type-1/2/3 dispatch is inferred from the shared
allocator analysis [V]/[I].

### F6. The region-3 translation bug (hidden 1 MiB error)

**Symptom.** The host's D2H ack/re-arm writes appeared to have no effect; the firmware's deferred
notify words `g+0xB4`/`g+0xB8` stayed at `0x40` for dozens of sessions; the image's flush never
retired.

**Root cause.** The inherited macro `HI5622_BAR0_IO(ca) = 0x3B8000 + (ca & 0xFFFFF)` truncates
device CAs to 20 bits. For region-3 CAs >= `0x40100000` (out[3] `CA 0x40101438`, out[4]
`CA 0x40101414`) it produces BAR0 `0x3B9438/0x3B9414` instead of the correct
`0x4B9438/0x4B9414` - writes landed in unrelated registers. `out[1]` is below the boundary,
which is why the bug survived.

**Fix.** Real region-3 translation in the clean driver, with compile-time asserts for every
out/mailbox/ETE CA and a selftest mirror [V]*.

**Evidence.** Bench 2026-11-24 21:10: after the fix, `g+0xB4 = 0` and `g+0xB8 = 0` - the
deferred-notify flush retired for the first time; F4 verified on hardware [V*].

**Residual risk.** Other inherited fragments may still use the 20-bit macro; the clean driver is
the single source of truth and the bench instrument modules are deprecated.

---

## 5. Protocol reference (summary)

The full data tables are in [TABLES.md](TABLES.md); this section is the narrative summary needed
to read the operational loop in section 6.

### 5.1 The frame

Every HCC message is a 12-byte header + payload. Total length is `payload + 12` at `+0x04`;
the dispatch id is a u16 at `+0x06`; the transport tag `0x5A5A` is at `+0x0A`; the group is the
low nibble of byte 0. Header byte 1 splits into an alloc nibble (vendor 1) and a resource/core
nibble; `hcc_msg_tx` copies the high nibble to byte 9 [V]. A frame is valid iff the tag is
`0x5A5A`, the total equals the ring-node length, and the dst core matches [V].

### 5.2 Groups and ids

| group | direction | use |
| ---: | --- | --- |
| 0 | host -> device | config/control events; the H2D receive table has 41 slots (40 + early 0x2F) |
| 1 | device -> host | firmware responses/acks (observed: id3 36 B, id0x12 40 B) |
| 2 | host -> device | WAL/scan/MLME class commands; 19 registered H2D slots |
| 3 | device -> host | hdpp/hmac data-path and event frames (e.g. id0x12 temp-state, 16 B) |
| 4 | device -> host | chip boot/heartbeat: id0 OAM log, id1 plat ready, id2 DEVICE READY, id3 exception info, id4 heartbeat |

The two messages of the operational loop are both group 0 id 1 config events distinguished by
their key: **key `0x0064` = context-create**, **key `0x00D8` = TXQ config sync**. Keys live at
payload+8 (`u16`), length at payload+0xA, data at payload+0xC [V].

### 5.3 The H2D mailbox

To send, the host commits a node in an SR ring, then announces mailbox **id 3**: it ORs
`1 << 3` into `out[0]` (CA `0x40039010`) and rings the doorbell (`out[2]`, CA
`0x400392D4`, bit 0). It then polls `out[0]` until the firmware clears the bit. Mailbox id 1 is
a payload-free liveness/PM wake; id 5 is a firmware `BUG()` trap and must never be posted; id 6 is
the firmware's own reply primitive; ids 0,2,4,7,8,9 are unregistered and dropped. The host must
**never write CA `0x400392F0`** - that is the firmware's own dispatch ack pulse [V].

The device's H2D dispatcher is entered through the image vector handler on event 0x4C, which is
why F3's patch is needed on our boot; on a vendor boot the vendor host's service loop keeps the
event path alive.

### 5.4 The D2H service

Vendor order: see `out[1]` bitmap -> write 1 to `out[3]` ack (CA `0x40101438`) -> read/drain
-> write 1 to `out[4]` re-arm (CA `0x40101414`) -> dispatch lowest-id-first. Handler table ids
{1..7}; ids 0/8/9/10 dropped; id > 10 stops the word. On our stack the service is polled (no host
MSI/INTx demux on 6.18 yet) and the direct-read path (F4) supplies the frames.

### 5.5 The ETE rings

SR0-SR2 carry host->device nodes; DR0-DR3 carry device->host nodes. A node is 8 bytes:
`word0` = device-side address of the host-DRAM buffer, `word1 = (len << 16) | 0x6D2B`.
Indices are packed: 10-bit counter plus a `0x400` wrap-phase bit; depth 32 (`0x1F`). The host
must arm the rings and the D2H service **before** release so the ready handshake is not lost [V]/[V*].

### 5.6 The boot registers, in one place

| object | address | host action |
| --- | --- | --- |
| release | device CA `0x40000108` | write `0x00005A5A` once, after upload + seeding |
| boot gate | device CA `0x4000010C` | **do not write during boot**; later, evidence-driven `0xCECE` only |
| handshake page | CPU `0x1BF000` | read-only golden gate (`+8 == 2`) |
| exception mask | CPU `0x40008` | read-only safety gate (must stay 0) |
| mailbox | CAs `0x40039010/14`, `0x400392D4`, `0x40101438/14/34`, never `0x400392F0` | see 5.3/5.4 |

---

## 6. The operational duplex loop

This is the session shape that closed the Wi-Fi-side RE goal (2026-11-25 05:10, ledger). It runs
on the PD4-patched firmware image and the clean-room driver (`hi5622_wifi.ko`, 400,732 B, sha256
`774c4f1f...`), with `h2d_send_budget=2` [V*].

```
S0  fresh boot of the instrument image (single clean endpoint reset pulse)
S1  insmod; prepare six iATU viewports (read back 6/6)
S2  upload 928,920 B firmware; verify target window (diffs = 0)
S3  pre-seed platform block (24 words) + wifi-custom block (618 words); read back
S4  hcc_arm (ETE rings, IRQ mask RMW); hcc_loop started; release 0x5A5A ONLY
S5  golden gate: page 0x86807FEF/7/2/0xABCD, state 1, mask 0  (boot_latched = 1)
S6  ready handshake decoded by the direct reader: g4 id1 then g4 id2 (payload 0x01060850)
S7  PACK BEFORE (read-only): ctx190 = 0, ctx0 = 0, table entry = 0x0011C458, mask = 0
S8  ONE h2d_ctx_create type 1  (g0 id1, key 0x64)
      -> consumed in 0 polls; ctx+0x190 = 0x01011448, ctx[0] = 4, ctx+0x434 = 0x0100478C
      -> mask = 0, staging = 0
S9  ONE h2d_txq_sync72  (g0 id1, cmd 0xD8)
      -> consumed in 0 polls; out0 = 0, out2 = 0, out5 = 0
      -> mask = 0, staging = 0 (NO exception frame)
      -> h2d_status ok=2 timeout=0 refused=0; phase ACTIVE
S10 read the direct D2H catalogue:
      NEW g1 id3  (36 B)  and  g1 id0x12 (40 B)   <- the firmware answered both messages
S11 verify boot remains golden: page/state unchanged; g+0xB4 = g+0xB8 = 0
```

The two response frames are given byte-exactly in [TABLES.md](TABLES.md) T13 (F5/F6). They have
well-formed HCC headers (tag `0x5A5A`, group 1, lengths matching the node lengths) - the firmware
processed our messages and produced protocol replies.

The state machine that encodes this is the committed `hcc_core` (528-check selftest):
`IDLE -> BOOT -> WAIT_PLAT_READY (<= 2.0 s) -> WAIT_DEVICE_READY (<= 20 s) ->
READY_TXQ_SYNC -> ACTIVE`, with the latched boot gate, the vendor-order D2H service, H2D
coalescing, the evidence-only CECE policy, heartbeat and PM as separate, testable steps [V].

---

## 7. Bench evidence chain

Every hardware-verified claim in this report has an attended session behind it. This table is the
chronology; **the newest entry wins** over any older paragraph (project rule), and section 8 lists
the claims that a later entry superseded. Times are ledger timestamps (Asia/Bangkok); "ledger"
means `mem-entries.md` in the superproject. Sessions were attended and recovery-gated; no session
was ever run from this document.

| date / time | session | what was measured (claim) | evidence source |
| --- | --- | --- | --- |
| 2026-11-22 | transport A/B1/B2 | six-viewport iATU, contiguous 928,920 B upload with verify diffs=0, CPU release, polled D2H bitmap, ETE rings armed, H2D smoke | wifi-transport-port SUMMARY/REPORT [V*] |
| 2026-11-23 | recovery + B3 | box recovered to W27b; install-from-stock path validated; B3 cold-boot ready-gate test NEGATIVE (g=0, registry unpopulated) - motivated the firmware-internal investigation | ledger 2026-11-23; wifi-fw-ready-gate [V*] |
| 2026-11-24 01:15 | GP1 | SoC GPIO endpoint reset pulse makes ROM S0/S1 run; handshake page reached the contract values; pulse necessary but flaky (2/4) | ledger 2026-11-24 01:15 [V*] |
| 2026-11-24 02:20 | GP1 re-test | pre-upload chip state after the pulse is identical across boots; post-release page alternates between contract and RAM-fallback - "S1 condition-dependent" | ledger 2026-11-24 02:20 [V*] |
| 2026-11-24 03:10 | GP2 | hog removed, driver-owned pins: mask becomes 0 (no exceptions) but S1 never completes (page +8 = 0) | ledger 2026-11-24 03:10 [V*] |
| 2026-11-24 04:00 | GP3 | seeding the four TCM section bounds makes entry A copy the CPU1 ITCM/DTCM (word0/DTCM exact); S1 still absent | ledger 2026-11-24 04:00 [V*] |
| 2026-11-24 05:00 | stock golden read | working vendor boot: handshake page contract, TCM values, ROM status, **full 24-word platform block captured** (read-only) | ledger 2026-11-24 05:00 [V*] |
| 2026-11-24 05:40 | GP4 | full 24-word vendor platform block: TCM byte-identical incl. coreword 0x37C8F8D3; S1 still absent | ledger 2026-11-24 05:40 [V*] |
| 2026-11-24 06:20 | stock golden read | **618-word wifi-custom block captured** (read-only); host-pointer words noted as a blind-copy hazard | ledger 2026-11-24 06:20 [V*] |
| 2026-11-24 07:20 | stock kprobe | exact vendor pre-release order: firmware chunks -> platform block -> wifi block -> release 0x5A5A | ledger 2026-11-24 07:20 [V*] |
| 2026-11-24 08:00 | GP5 | both vendor custom blocks before release: same no-S1 result | ledger 2026-11-24 08:00 [V*] |
| 2026-11-24 08:45 | GP6 | vendor-matched pulse timing: negative; host-visible phase concluded (later overturned by GP8) | ledger 2026-11-24 08:45 [V*] |
| 2026-11-24 09:20 | ROM dump | mask ROM host-readable; 16 KiB dumped (sha256 fcfd464f...); vectors/IRQ/FIQ targets decoded | ledger 2026-11-24 09:20 [V*] |
| 2026-11-24 10:00 | rom-dump lane | **root cause**: our 0xCECE writes clobber the ROM 0xCEED handshake; release-only protocol defined | ledger 2026-11-24 10:00 [V]/[V*] |
| 2026-11-24 10:40 | **GP8** | **firmware boots exactly like the vendor**: golden page, state 1, mask 0, TCM identical; D2H notify staged | ledger 2026-11-24 10:40 [V*] |
| 2026-11-24 11:10 | GP9 | post-boot service: one D2H notify event drained; IRQ layer still masked; H2D probe not acked | ledger 2026-11-24 11:10 [V*] |
| 2026-11-24 11:40 | GP10 | on-demand CECE + vendor-order service: boot stays golden, no bit2 event, mailbox still dead | ledger 2026-11-24 11:40 [V*] |
| 2026-11-24 12:20 | hcc-service first fire | golden boot; "scheduler fingerprint absent" (later corrected - instrument aliasing) | ledger 2026-11-24 12:20 [V*] |
| 2026-11-24 13:10 | D2H decode | device group-A rings + ACP bodies readable directly; g4 id1 announce decoded | ledger 2026-11-24 13:10 [V*] |
| 2026-11-24 14:00 | d2h-direct fire | **complete ready handshake received** (id1 + id2 payload 0x01060850; 21 frames, 0 overruns) | ledger 2026-11-24 14:00 [V*] |
| 2026-11-24 15:10 | H2D announce | first valid H2D SR frame delivered to the device ring (device consumed the node); firmware mailbox not acked; sysfs grammar requires no-newline write | ledger 2026-11-24 15:10 [V*] |
| 2026-11-24 16:10 | PD fire | image exception/vector path reached for the first time; crude poll patch data-aborts, page clobbered | ledger 2026-11-24 16:10 [V*] |
| 2026-11-24 17:20 | swarm landing | F4 latent transport bug found (region-3 20-bit mask); h2d-frames + hcc-core land; F4 propagated | ledger 2026-11-24 17:20 [V]/[V*] |
| 2026-11-24 18:25 | PD2 analysis | PD data abort root-caused (handshake-retry body before hcc init; nested ABT frames explain the page clobber); clean PD2 design | ledger 2026-11-24 18:25 [V] |
| 2026-11-24 19:00 | PD2-A | PD2 patch breaks the boot: the "dead" config-3 cave is LIVE on our boot; control boot golden | ledger 2026-11-24 19:00 [V*] |
| 2026-11-24 20:00 | driver-clean landing | clean-room instrument driver merges transport+D2H+core; F4 fixed with real region-3 translation; 0 warnings; 133/133 selftest | ledger 2026-11-24 20:00 [V] |
| 2026-11-24 21:10 | clean F4 fire | golden boot + READY_TXQ_SYNC; **g+0xB4/+0xB8 flush retires** (F4 verified on hardware); one announce not yet acked | ledger 2026-11-24 21:10 [V*] |
| 2026-11-24 22:10 | **PD3 fire** | firmware H2D dispatcher alive: one announce **consumed in 0 polls** (h2d_complete done=1), golden page/state/mask intact | ledger 2026-11-24 22:10 [V*] |
| 2026-11-24 22:45 | PD3 record | PD3 cave hardware-validated; out[2]/out[5] reconciled as non-latching pulse observables; pass criterion corrected | ledger 2026-11-24 22:45 [V] |
| 2026-11-24 23:40 | TXQ sync 48 B | first real post-ready message delivered but firmware faults (mask 0x12); 48-vs-72 node length suspected | ledger 2026-11-24 23:40 [V*] |
| 2026-11-25 00:20 | txq72 | fault reproduces; exception record read: pc = WRAM data, evidence in the g0 id1 handler path (0x81039/0x43DF5) | ledger 2026-11-25 00:20 [V*] |
| 2026-11-25 01:10 | control stub | payload-free g0 id0 stub consumed, NO exception: transport proven; fault is handler-specific | ledger 2026-11-25 01:10 [V*] |
| 2026-11-25 02:10 | PD4 confirmation | cave survives (stub present before/after); fault reproduces; exception handler cascades dabt->undef | ledger 2026-11-25 02:10 [V*] |
| 2026-11-25 03:10 | ctx-create RE | key 0x0064 config event identified as the only H2D route to ctx+0x190; frame spec synthesized | ledger 2026-11-25 03:10 [V]/[I] |
| 2026-11-25 04:10 | ordered lap (type 3) | both messages consumed; type 3 takes the no-alloc branch, ctx+0x190 stays 0; TXQ still faults | ledger 2026-11-25 04:10 [V*] |
| 2026-11-25 05:10 | **ordered lap (type 1)** | **context allocated** (ctx+0x190 = 0x01011448), TXQ sync consumed with mask 0 and no staging frame, firmware responds g1 id3 + g1 id0x12 | ledger 2026-11-25 05:10 [V*] |
| 2026-11-25 06:00 | response decode | response frames read byte-exactly from the staged rings: g1 id3 36 B, g1 id0x12 40 B (node lengths match) | ledger 2026-11-25 06:00 [V*] |

End-of-chain bench state (ledger 2026-11-25 06:00): the box is running the successful lap - both
messages consumed, mask 0, page golden, no further writes pending. This report's cut-off is that
entry.

---

## 8. Corrections register

The ledger is append-only and the newest entry wins. This register records the claims this
investigation superseded, why, and when - because repeating one of them costs bench sessions (two
of them cost recovery cycles). "Where it appeared" names the lane/ledger that carried the older
claim.

| # | superseded claim | corrected claim | decisive evidence | date |
| ---: | --- | --- | --- | --- |
| C1 | bench D2H bitmap `0x4` = "DEVICE READY" | the loaded vendor D2H handler table is {1..7}; bitmap bit2 = an EXCP/vector announce; the real ready handshake is HCC `g4 id1` then `g4 id2` (bounded 2 s / 20 s) | vendor host-replica RE + direct-read bench | 2026-11-23/24 |
| C2 | boot needs release `0x5A5A` **and** a `0xCECE` announce | release-only; `0xCECE` must not be written during boot, only evidence-driven after the handshake is latched | ROM dump + GP8 | 2026-11-24 10:00/10:40 |
| C3 | exception mask `0x0A` = svc + data-abort | `0x0A` = undefined + prefetch; id map is undef=1, svc=2, prefetch=3, abort=4 | rom-dump corrections | 2026-11-24 |
| C4 | `0x7386F44F` is a magic; header `0x1F4/0x1F8` are handler slots | `0x7386F44F` is Thumb code; `0x1F4/0x1F8` are stack tops; image header order is {FIQ, data-abort, prefetch-abort, SVC, undefined} | rom-dump | 2026-11-24 |
| C5 | "S1 is ROM/SoC-internal and not host-triggerable" (GP6 conclusion) | wrong: the gate was being destroyed by **our own** `0xCECE` writes; with release-only the ROM S1 path completes deterministically | GP8 | 2026-11-24 08:45 -> 10:40 |
| C6 | the ETE engine must copy D2H frames to host DRAM before we can receive them | the engine copy is firmware-triggered and idle, but the host can read the device group-A ring nodes and ACP frame bodies directly; that is the working D2H path | d2h-direct bench | 2026-11-24 13:10/14:00 |
| C7 | "the image scheduler never armed; GIC/event-controller reads are 0" | instrument bug: the 20-bit transport macro aliased `CA 0x4016xxxx` reads into region 4; corrected reads show the scheduler armed; the blocker is event 0x4C delivery, not scheduling | fw-postboot regmap audit | 2026-11-24 |
| C8 | `out[3]`/`out[4]` map to BAR0 `0x3B9438/0x3B9414` | they map to `0x4B9438/0x4B9414` (region-3 translation); the wrong writes explain the stuck deferred-notify flush | driver-clean F4 + clean fire | 2026-11-24 20:00/21:10 |
| C9 | PD3's image-header-record cave is safe for a polling stub | the exception-staging routine writes a 17-word frame exactly over `0x4000C..0x4004B`, corrupting the stub; PD4 relocated it to CPU `0x108240` | exception record + PD4 cave check | 2026-11-25 00:20/02:10 |
| C10 | PD3 pass criterion includes `out[5]=1` and `out[2]=8` | those are non-latching write-pulse observables; the pass criterion is `out[0]` `0x8 -> 0` + `h2d_complete/hcc_events done=1` with clean page/state/mask | PD3 reconciliation | 2026-11-24 22:45 |
| C11 | the TXQ config sync (`g0 id1` cmd `0xD8`) is the first real H2D message | the `g0 id1` key-0xD8 handler dereferences `ctx+0x190`, so a context-create (`g0 id1` key `0x64`) must be sent first; the ordered lap is create -> sync | ctx-create RE + bench type-3/type-1 laps | 2026-11-25 03:10-05:10 |
| C12 | context-create type 3 ("minimal") is sufficient | type 3 takes the no-alloc branch; type 1 allocates `ctx+0x190` and makes the TXQ sync clean | ordered lap type 3 vs type 1 | 2026-11-25 04:10 -> 05:10 |
| C13 | PD2's cave CPU `0x47432` (config-3 block) is dead code | it is live on our boot: PD2-A breaks the boot; PD1's epilogue hunk alone was fine | PD2-A bench + control boot | 2026-11-24 19:00 |
| C14 | the g0 id1 fault is a transport/PD3 problem | the transport and the PD3 dispatcher are fine: the payload-free `g0 id0` control stub is consumed with no exception; the fault is inside the g0 id1 **handler** (context precondition) | control-stub fire | 2026-11-25 01:10 |
| C15 | the sysfs arm grammar accepts `echo 1 >` | the grammar parser requires exactly `1` with no trailing newline; use `printf 1 >` | first H2D session | 2026-11-24 15:10 |
| C16 | GP1 proves the reset pulse is sufficient | the pulse is necessary but not sufficient: GP1's occasional successes consumed a stale vendor platform block left in WRAM; the deterministic gate is the `0xCEED` handshake | s1-cpu1 + GP8 | 2026-11-24 06:20/10:40 |

---

## 9. Verification and reproduction

### 9.1 What was re-run while consolidating this report

Consolidation did not invent evidence; it re-ran the owning lanes' committed device-free checks to
make sure the facts this report leans on still reproduce from the tree as committed. All checks
are host-only and device-free (no vendor blob is needed for these particular selftests):

| check | result | what it pins |
| --- | --- | --- |
| `wifi-hcc-core/tools/build.sh --no-kernel` | **PASS**, 528 checks, 0 failures | the post-ready state machine: boot latch, ready pair, TXQ sync, mailbox coalescing, CECE policy, ring arithmetic |
| `wifi-h2d-frames/tools/build.sh` | **BUILD OK**, 233 passed, 0 failed | 12-byte header contract, byte-exact builders (incl. TXQ sync), refusal policy, ready decoder |
| `wifi-d2h-direct/tools/d2h_selftest.py` | **PASS**, 92/92 | direct-read service: node/frame decode, catalogue, consumer-advance rules |
| `wifi-ctx-create/tools/ctx_create_re.py --selftest` | **PASS**, 40/40 | key tables, handler/worker/helper chain, read pack |
| `wifi-ctx-create/tools/ctx_create_frame.py --selftest` | **PASS**, 26/26 | the context-create frame builder |
| `wifi-bench-tooling/tools/selftest.py` | **PASS**, 177/177 | session runner, guards, patch builder, audit |
| `wifi-re-report/tools/verify_report.py` (this lane) | **PASS** (see 9.4) | this report's cross-references, markers, claim IDs and key constants |

The remaining lane checks (e.g. `rom_s01.py` 70/70, `romdis.py` 86/86, `fw_pd_builder.py`
56/56, `pd2_re.py` 33/33) were re-run by their owning lanes in the sessions cited in section 7
and read the local vendor blob in place; they are quoted here from those lane records and were not
re-executed in this consolidation pass to avoid concurrent vendor-blob reads while other lanes were
working. That is a bookkeeping distinction only - those checks are committed and runnable per the
lane READMEs.

### 9.2 Reproducing the static claims

All static claim tools read the vendor artifacts **in place** and print derived annotations; none
copies vendor bytes. See [REPRODUCE.md](REPRODUCE.md) for the exact commands. The key ones:

* ROM facts: `wifi-rom-dump/tools/romdis.py selftest` (86) and `info/map/flow/stores/consts/gate`
  against the local 16 KiB ROM dump.
* Image facts: `wifi-rom-flow/tools/fwdis.py`, `wifi-fw-postboot/tools/fwdis.py`,
  `wifi-ete-engine/tools/ete_engine.py selftest` (150), `wifi-h2d-catalog/tools/fw_h2d_tables.py --verify`
  (20 byte-level claims), `wifi-ctx-create/tools/ctx_create_re.py --trace`.
* Protocol constants: `wifi-hcc-core` and `wifi-h2d-frames` selftests (9.1) plus
  `wifi-host-replica/SPEC.md`.
* Firmware patches: `wifi-irq-unmask/tools/fw_pd_builder.py` (56) and
  `wifi-bench-tooling/tools/fw_patch_builder.py` (177-suite) rebuild/verify the PD recipes.

### 9.3 Reproducing the bench result

The bench chain is documented in the lanes' BENCH-PROCEDURE files, in this order: clean boot with
the reset pulse -> prepare/upload/verify -> custom-block seed -> hcc_arm/hcc_loop -> release-only
-> golden gate -> ready pair (direct reader) -> one context-create type 1 -> one TXQ sync ->
direct-read the responses. The generic ladder with pass gates is [TABLES.md](TABLES.md) T19.

**This report does not authorise a fire.** Benches are attended, recovery-gated operations; the
router's UART is read-only and host->router TX is physically dead. See the project's
`BENCH-FACTS.md`, `RUNBOOK.md` and `tools/fire-preflight.sh` rules.

### 9.4 The report verifier

`tools/verify_report.py` is a device-free selftest for the document itself. It checks:

1. all companion files exist and are non-trivial;
2. every lane directory/path this report cites exists relative to `opensource/docs/incoming/`;
3. only the four evidence markers appear (no malformed or invented marker token);
4. every relative markdown link resolves to a companion file, and all numbered report sections
   0-13 exist with their expected titles;
5. the claim IDs in [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md) are unique, gap-free, numbered 1..N, and
   each maps to a real report section with an evidence marker;
6. a curated set of critical constants (addresses, golden words, frame bytes, hashes) appears in
   the tables exactly as in the committed lane sources it cross-checks (e.g. the TXQ-sync wire
   bytes appear in `wifi-h2d-frames`'s committed selftest, the PD3/PD4 image hashes appear in
   `wifi-irq-unmask`'s reports) and the end-state claims also appear in the append-only ledger and
   `STATUS.md`;
7. no forbidden content classes (private keys, secrets paths, oversized hex/base64 blobs that
   would smell of vendor binaries, per-unit MAC addresses) are present.

Run it with the project venv python. It prints one PASS/FAIL line per check group and exits
non-zero on failure.

---

## 10. Limitations and open questions

### 10.1 Limits of the result

1. **The firmware is closed.** Everything here is interoperability research against a binary
   firmware. No upstreamable clean-room Wi-Fi driver follows from it while the blob remains
   closed; the project's own position is that vendor GPL source is the route to a shippable stack.
2. **PD3/PD4 are research instruments.** They patch the vendor firmware image to call its own
   dispatcher. They are bench-validated but must never be presented as product changes, and they
   do not revive the firmware's own thread/scheduler services.
3. **The duplex result is message-level, not link-level.** Two ordered config messages were
   consumed and answered. That proves the protocol path, not a working Wi-Fi interface: no VAP,
   no scan, no association, no data path.
4. **The ETE engine remains idle.** The direct-read D2H path substitutes for the engine's host-DRAM
   copy on D2H; the engine's actual trigger (firmware event 0x2E/sending trigger CPU `0xC625A`)
   never runs on our boot, and no host write can start it [V].
5. **The firmware IRQ/thread layer is still dormant.** The PD3 bypass services the mailbox, but the
   image dispatcher, event controller and scheduler services the vendor stack depends on are not
   exercised; later flows (calibration, scan completion, TX status) may need them.
6. **Some frames are observed but not fully decoded.** The two firmware responses (g1 id3, g1
   id0x12) have clean headers and plausible payload shapes, but field semantics are not proven.
7. **The context-create frame is synthesized**, not captured from the vendor driver; the vendor's
   sender for key `0x0064` is unproven [I]. Type-1/2/3 semantics are inferred from the allocator
   analysis plus one type-3 and one type-1 lap.
8. **Per-unit data stays out.** No MAC/serial/calibration value is in this report by policy.

### 10.2 Ranked open questions

1. What unmasks the firmware's IRQ layer on a real vendor boot, and can the image dispatcher be
   brought up legitimately (without a firmware patch)? This is the difference between a research
   instrument and a usable driver.
2. What is the exact vendor CECE service protocol (order, duration, ack semantics) for the image
   handlers, and does any later flow require it?
3. Why is the ETE engine's A->B copy trigger never reached, and can the D2H path be made
   engine-based instead of direct-read?
4. What is the full post-TXQ-sync startup sequence on our boot (calibration alg set_* -> `g0 id2`,
   VAP create, WAL config), and which handlers have context preconditions like the TXQ sync did?
5. What do the g1 id3 / g1 id0x12 payloads mean field-by-field, and what does the firmware expect
   next?
6. Can the ~39-op cfg80211 full-MAC surface be built over this protocol for the stock userspace,
   and what is the minimum viable subset (the vendor-userspace lane's M0-M6 plan)?
7. What is the data path contract (hdpp TX/RX descriptors, PKTRAM/ACP ownership, PIE rings), and
   what must the host do that the vendor driver does?
8. Is there a host-visible trigger for the firmware's event layer that the vendor host performs
   before the ready exchange, or is it purely ROM/image internal? (Current answer: nothing visible;
   the vendor sends nothing between release and ready - but this is a negative result on the
   modules scanned, not a proof about ROM code we cannot see.)

---

## 11. Publication, provenance, and upstream status

**Artifact policy.** This lane commits text and original code only. No vendor binary, object code,
firmware byte, ROM dump, calibration value, extracted DTS or secret is present. Vendor firmware and
ROM identities appear as SHA-256 digests; the blobs stay at their local paths
(`/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin`,
`/home/shibbityshwab/staging-fi/rom16k.bin`). Wire-frame hex dumps are protocol facts (headers of
messages), not firmware content; the firmware patch recipes contain only the patch hunks (our own
replacement code plus the 4-byte entry words they modify), consistent with the owning lanes'
committed recipes.

**Why it is publishable.** The repository is an intentionally public research notebook: negative
results, dead ends and the ledger are part of the record. This report is written to that standard:
it marks inference as inference, lists the corrections that cost sessions, and does not present
bench instruments as products. The public repos' pre-policy history caveat (an older lab credential
and vendor objects before a policy purge) is documented in `PUBLISH.md` at the project root and
is not repeated here.

**Upstream status.** Unchanged and blocked: upstream OpenWrt submission of a Wi-Fi stack for this
board requires a licensable firmware/driver path. The vendor modules declare GPL-2.0 and a GPL
source request names the exact symbol families/headers (wifi-driver-re SUMMARY). The clean-room
line's ceiling is the message-level protocol proven here; the MAC/MLME and data path remain vendor
code. This report does not claim upstream progress for Wi-Fi.

**Licensing of this lane's own material.** The report text is part of the notebook; the verifier
script is original code (GPL-2.0-compatible, matching the sibling lanes). Nothing here is copied
from vendor source.

---

## 12. Lane documentation map

The detail sources for every section of this report. Each lane report carries its own evidence
table and reproduction commands; where this report compresses, the lane expands.

| lane directory | what it owns in this report |
| --- | --- |
| `wifi-rom-dump` | section 3 ROM map, S0/S1, the 0xCEED gate, corrections C3/C4 |
| `wifi-rom-s01` | handshake page contract, ETE engine-global negative, early S0/S1 reads |
| `wifi-rom-flow` | image boot flow, stage table, missing-stage analysis |
| `wifi-s1-cpu1` | who writes `+8=2`, the CPU0->CPU1 TCM handoff, GP1 flakiness mechanism |
| `wifi-chip-reset` | the vendor power/reset pulse, GPIO pins/timing, GP1 plan |
| `wifi-ete-frames` | 12-byte header contract, ready-frame builders, SR node format |
| `wifi-ete-engine` | ETE window map, writers census, engine wake negative |
| `wifi-ete-init` | ETE send-index init extraction and patch attempt |
| `wifi-hcc-re` | HCC handshake/message contract, early ready model (corrected in C1) |
| `wifi-hcc-link` | vendor-link/event model, "sends nothing pre-ready" negative |
| `wifi-hcc-service` | host HCC service implementation, CECE policy |
| `wifi-hcc-core` | tested post-ready state machine (528 checks) |
| `wifi-h2d-catalog` | H2D dispatch tables, mailbox layer, payload-probe safety |
| `wifi-h2d-frames` | byte-exact builders, catalogue data, validators (233 checks) |
| `wifi-h2d-inject` | injector instrument, early safe probes |
| `wifi-h2d-send` | TXQ-sync send path, ordered context-create ladder, g0 id1 handler analysis |
| `wifi-d2h-direct` | direct device-ring D2H receive service (92 checks) |
| `wifi-host-replica` | definitive vendor host post-release behaviour, mailbox service order |
| `wifi-driver-re` | host-driver interface contract, cfg80211 ops map, D2H catalog counts |
| `wifi-firmware-re` | firmware load interface, memory contract |
| `wifi-transport-port` | transport contract, BAR0 windows, bench A/B1/B2 |
| `wifi-irq-unmask` | PD/PD2/PD3/PD4 analysis, recipes, builders, verifiers |
| `wifi-fw-postboot` | post-handshake startup, scheduler/IRQ audit, instrument correction C7 |
| `wifi-fw-link-event` | firmware link/event bootstrap, event-pump negative |
| `wifi-event-router` | device event-controller registry map |
| `wifi-boot-gate` | boot-task post-send chain, 0x48820 gate |
| `wifi-boot-kick` | vendor post-release boot-kick candidates |
| `wifi-fw-ready-gate` | group-4 ready gate analysis (cold-boot negative) |
| `wifi-vendor-userspace` | userspace surfaces, startup trace/HCC mapping, cfg80211 scope, M0-M6 |
| `wifi-driver-clean` | clean-room instrument driver (the bench baseline) |
| `wifi-openwrt-pkg` | OpenWrt KernelPackage for the instrument driver |
| `wifi-bench-tooling` | one-command bench sessions, guards, patch builder, audit |
| `wifi-ctx-create` | context-create message RE, frame builder, verification pack |

Historical lanes still in the tree but not load-bearing for this report: `wifi-framework-port`,
`wifi-shim-port`, `wifi-source-hunt`, `wifi-wal-glue`, `wifi-algcfg-slice`,
`wifi-datapath-slice`, `wifi-driver-integration`, `wifi-thermal-re`, `wifi-openwrt-pkg`
history beyond the package, and the phase-numbered docs under `opensource/docs/`.

---

## 13. Conventions and glossary

| term | meaning |
| --- | --- |
| **CA** | device-side address in the endpoint's internal space (device CA) - not a host address |
| **CPU address** | firmware CPU view (region 5); image file offset + `0x40000` |
| **BAR0 offset** | host-side offset into the endpoint's 16 MiB BAR0 |
| **H2D / D2H** | host-to-device / device-to-host |
| **HCC** | HiSilicon Communication Channel: the device/ host message protocol (12-byte header, groups/ids) |
| **ETE** | the endpoint DMA ring engine (SR send rings, DR receive rings, node = 8 B) |
| **out[0..5]** | the six mailbox registers shared between host and device |
| **PDn** | the n-th firmware patch iteration (research instrument) |
| **GPn** | the n-th golden-path bench iteration of the boot protocol |
| **golden state** | page `0x86807FEF / 7 / 2 / 0xABCD`, state 1, mask 0: the vendor-equivalent post-boot state |
| **latch** | record a one-time observation (e.g. page `+8 == 2`) instead of re-gating on a live read |
| **field A/B/C** | the three untyped header bytes at `+0x02/+0x03/+0x08` whose meaning is message-specific |
| **pre-S1 state** | page `0x86803FEF / +8 = 0 / 0xABCD`, state 0: the ROM handshake incomplete |
| **RAM-fallback page** | the pre-S1 fallback signature with ROM boot-log values (`0x1BD000/0x1BD0D8`) |

Conventions used in the tables: `??` = header byte not initialised by the firmware builder;
"BAR0 = CPU + 0x6B8000" is the CPU->BAR0 rule; "BAR0 = 0x3B8000 + (CA - 0x40000000)" is the IO-CA
rule; `x` in hashes is an abbreviated digest whose full value is in the owning lane report.




