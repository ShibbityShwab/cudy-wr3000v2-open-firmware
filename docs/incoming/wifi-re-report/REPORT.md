# The Hi5622V100 Wi-Fi / chip protocol: a consolidated reverse-engineering report

**Project:** Cudy WR3000 v2.0 (luofu v2 board) bench reversal - Hi5671YV200 SoC + Hi5622v100 Wi-Fi
endpoint, stock OpenWrt 22.03.6 / kernel 5.10.201, vendor firmware 928,920 B.

**Document:** `wifi-re-report/REPORT.md`, umbrella narrative, **v2**. **Date:** 2026-11-25.
**State cut-off:** `mem-entries.md` entry 2026-11-25 15:00 (the newest ledger entry at this
revision); superseded claims are catalogued in section 8. **Status:** the full duplex HCC
protocol, the operational message lap, the first calibration exchange (ladder C1) and the first
real Wi-Fi interface on our own image (cfg80211 wiphy registration + VAP netdev lifecycle) are
hardware-validated; the firmware-side VAP/scan path and the MAC data plane are not. Detail
sources remain the lane reports listed in section 12; this document is the publication-grade
synthesis, not a replacement.

**Lane / task:** wifi-re-report, shared task `task-116` (v2; v1 was `task-109`, cut off at
2026-11-25 06:00). Host-only: no device, UART, TFTP or network contact was made while writing it.
**Safety:** nothing in this report authorises a bench fire; every hardware session cited here was
attended and followed the project's recovery-door rules.

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

### 0.5 What changed in v2

v1 stopped at the 2026-11-25 06:00 ledger entry. v2 integrates the later milestones of the same
day; the earlier material is unchanged except where a later result superseded it (section 8):

1. the **cfg80211 bench image** was built, flashed to kernelb (full-slot hash - see T21) and
   booted on the box (section 6.3);
2. the **first real Wi-Fi interface on our own image**: wiphy registration gated on the ready
   handshake, `iw` enumerates phy0, and an nl80211 VAP netdev is created and deleted (M0/M1
   hardware-validated; finding F8, table T22);
3. **ladder C1**: the firmware answered the first calibration message (298 B g0 id2 key 0x0101
   request -> 298 B g1 id3 config-sync answer; finding F7, table T23);
4. the **ETE DR (D2H) A->B copy** host lever was measured negative (E1a/E1b); the start condition
   is device/ROM state and the direct device-ring read is the supported receive path (section
   6.5);
5. the **data-plane architecture** is mapped: a separate MAC host-interface ring block (slot 5),
   the TXQ-table address sync, the per-TID MSDU-info TX entries and the RX free/completion rings
   (finding F9, table T24);
6. the **PD4 research patch** is audited as a minimal, bench-only instrument: byte-identical
   rebuild (two segments, 68 patched bytes), independently re-verified cave proof, the unresolved
   H1/H2 sentinel question, ranked diagnostic variants, and no proven vendor-clean mailbox
   delivery (finding F10, table T25, `wifi-fw-audit/REPORT.md`);
7. the bench evidence chain gains the 2026-11-25 07:40-15:00 sessions (section 7), the
   corrections register gains C17-C26 (section 8), and the verification, limits, publication and
   lane-map sections are updated (sections 9-12).

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
  -> host->device calibration set_rssi_param (g0 id2, key 0x0101, 298 B) consumed
  -> firmware answers with a 298 B g1 id3 config-sync frame (ladder C1)
  -> cfg80211 wiphy registers only after the ready handshake; iw enumerates Wiphy phy0
  -> nl80211 VAP wlan0 created and deleted on our own image (cfg80211 M0/M1)
```

That is the project's Wi-Fi-side core RE goal: **the full duplex HCC protocol works on a
clean-room host**, and the first real Wi-Fi interface now exists on our own image. The remaining
work is the firmware-side VAP/scan/AP control messages and the MAC frame plane (sections 6.2-6.4
and 10).

### 1.2 What is proven, and what is not

| claim | status |
| --- | --- |
| host can reset and boot the chip's mask ROM (S0/S1) | **proven** [V*] |
| ROM<->image handshake contract (0xCEED, page +8 = 2, entry A/B) | **proven** [V]/[V*] |
| golden firmware state achievable repeatedly | **proven** [V*] |
| device->host ready handshake and other D2H frames readable | **proven** [V*] |
| H2D transport (SR ring commit + mailbox announce) | **proven** [V*] |
| firmware processes real H2D messages and answers | **proven** for two ordered messages and the calibration class [V*] |
| calibration exchange (g0 id2 key 0x0101 -> g1 id3 298 B) | **proven** as a protocol exchange; the answer body was not captured (length/count evidence) [V*] |
| cfg80211 management interface (wiphy + VAP netdev) | **proven** on our own image (M0/M1); firmware VAP objects are not created yet [V*] |
| firmware IRQ/thread layer is alive on our boot | **not proven** - bypassed by the PD3/PD4 research patch; without it the mailbox is never serviced [V*] |
| ETE engine DR (D2H) A->B copy | **host lever proven negative** - the only host-owned delta was tested (E1a/E1b); start is device/ROM state; direct read is the supported RX path [V*]/[I] |
| firmware VAP/scan/AP control messages | **not started** - payload structs open (g0 id7/8, g0 id0xC [U]) |
| MAC frame data path (TX/RX frames) | **mapped only** - separate slot-5 ring block; no host implementation, no frame sent/received [V]/[I] |
| upstreamable clean-room driver | **blocked** by the closed firmware blob and the unmatched vendor GPL request (section 11) |
| PD4-patched vendor firmware | **research instrument only** - never a shipping change (section 6.6) |

### 1.3 The breakthrough ladder

The investigation reached the result through twelve hardware-validated breakthroughs; each is
also a correction of the then-current model (section 8 gives the full register):

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
| 9 | 2026-11-25 02:10 | **PD4** moves the dispatcher stub to a genuinely dead static-data cave (the exception-staging area no longer clobbers it), which enables the context-precondition analysis [V*] |
| 10 | 2026-11-25 10:10 | the **ETE DR wake lever is closed**: the only host-owned state delta (published index) was tested negative; the A->B copy start is device/ROM state, so the direct group-A read stays the supported RX path [V*] |
| 11 | 2026-11-25 12:30 | **ladder C1**: the firmware answers the first calibration message (298 B g0 id2 key 0x0101 -> 298 B g1 id3) [V*] |
| 12 | 2026-11-25 15:00 | **first real Wi-Fi interface on our own image**: cfg80211 image flashed (slot 1794a531), wiphy registered after the ready handshake, iw enumerates phy0, VAP wlan0 create/delete via nl80211 (M0/M1) [V*] |

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
| cfg80211 bench image | sysupgrade container 6,267,199 B sha256 `22b72852...`; kernel slice 3,372,638 B sha256 `13e81214...`; full-slot hash `1794a531ed19...` | [V] |
| cfg80211 module / clean driver | cfg80211.ko 2,328,548 B sha256 `949393f9...`; hi5622_wifi.ko 505,076 B sha256 `d2b3cf79...`, depends=cfg80211, 0 warnings | [V]/[V*] |
| wiphy channel plan | 2.4 GHz: 13 channels (1-13, 30 dBm); 5 GHz: 19 LU-plan channels (36-48/52-64/100-140, 23 dBm) | [V*] |
| first VAP | wlan0, ifindex 7, type managed, locally-administered documentation address; created and deleted | [V*] |
| calibration exchange (C1) | 298 B request (g0 id2, key 0x0101, cfg id 3507) -> 298 B answer (g1 id3, syn 0x0101) | [V*] |
| MAC host-interface block | CA 0x40040000 (band 0) / 0x40060000 (band 1), slot 5; ready id2 payload seeds the MSDU-info ring pointer table at `0x02060850` | [V]/[V*] |
| data-plane rings | TX: per-TID 8-byte MSDU-info entries + 0x2524 B TXQ table; RX: 256 x 0x63c + 512 x 0xb0 free rings + 4095-entry completion ring, 0x28 B descriptors | [V] |

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
| cfg80211 full-MAC interface (the 39-op vendor surface)             |
|   M0 wiphy + M1 VAP netdev: on the bench image (hardware-validated)|
|   M2 scan/station stubs: -EOPNOTSUPP; M3+ deferred                 |
+---------------------------------------------------------------+
| HCC message layer: 12-byte header, groups 0/1/2/3/4, mailbox ids  |
|   H2D: SR ring commit -> out[0] mask -> out[2] doorbell -> id3     |
|   D2H: out[1] bitmap -> ack out[3] -> read rings -> re-arm out[4]  |
+---------------------------------------------------------------+
| ETE rings: SR0-2 (host->device) and DR0-3 (device->host)           |
|   node = 8 B {devva, (len<<16)|0x6D2B}; depth 32; packed indices   |
|   engine A->B copy is firmware-triggered; DR direction stays idle  |
|   (direct read of the device group-A rings is our substitute)      |
+---------------------------------------------------------------+
| MAC frame plane: host-interface ring block, slot 5                 |  <- mapped, not implemented
|   TX per-TID MSDU-info rings; RX free + completion rings           |
|   (separate from HCC/ETE; seeded by the ready handshake)           |
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
|  - cfg80211 wiphy/VAP    |          | MAC host-if block CA 0x40040000/0x40060000   |
|  - (data path: NOT impl.)|          |   slot 5: MSDU-info/TXQ/RX rings             |
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
| CA `0x40040000` / `0x40060000` | MAC host-interface ring block, slot 5 of the per-band channel table (band 0 / band 1): MSDU-info ring pointer table (+0x10), RX free/completion rings (+0x20..+0x54), tx_ba_info (+0x00/+0x04) [V] |
| CPU `0x10C23C` / `0x103BF8` | firmware group-0 id-1 config key tables A (217 slots) / B (88 slots), sentinel {0x1451, 0}; the hdpp/alg command namespace [V] |
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

A later experiment closed the remaining host-side question about the receive engine: the DR
(D2H) A->B copy has exactly one host-owned state delta (the group-B published index `+0x38`),
and writing it to the current fill (E1a) or to zero (E1b) on a golden boot moved nothing - no
status bits, no trigger counters, no ring fills (section 6.5). The only start condition is
device/ROM-authored engine state, so the direct device-ring read is not a stop-gap but the
supported D2H receive path for this firmware [V*]/[I].

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

Ten findings carry the investigation. F1-F6 are the boot/protocol findings of v1; F7-F10 are the
management/data-plane findings integrated in v2. Each is stated as symptom -> root cause ->
evidence -> fix -> residual risk, so that a future reader can tell what is settled and what is
not.

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
does not resolve why the trigger never fires. The later ETE-wake ladder (E1a/E1b, section 6.5)
measured the only host-owned candidate negative, so "why" is now classified as device/ROM state
rather than a missing host write.

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

### F7. The first calibration exchange answers on a generic config-sync channel

**Symptom / context.** After the ordered message lap the vendor's next boot step is the factory
calibration: six `alg set_*` config events, HCC group 0 id 2, key `0x0101`, each carrying a
270-byte alg_cfg blob (298 B frame, node length = declared total). The question was whether our
host can drive that family and whether the firmware answers.

**Design (from the firmware parser, not a vendor-driver capture).** The firmware's key-0x0101
handler clears the request's `dev_ret`, reads `need_w4` and answers through the single config-sync
sender with `syn_id 0x0101`, `body_len 270`, the request cfg id echoed and `dev_ret 0`. The six
vendor cfg ids and value layouts were recovered statically: 3502/3503 (2.4 GHz power, 104 B),
3504/3505 (5 GHz power, 70 B), 3506 (xo ppm, u32) and 3507 (rssi, 40 B) [V].

**Bench (ladder C1, 2026-11-25 12:30).** After the type-1 context-create and the TXQ config sync,
one zero-valued `set_rssi_param` (3507, field B = 4) was consumed in 0 polls; the direct D2H
catalogue then showed a second g1 id3 frame with `last_total = 0x012A` (298 B) - exactly the
mirror length of our 298 B request. Boot page and state stayed golden, mask 0, no exception [V*].

**Residual risk.** The answer body was recycled by the driver/firmware before the read-only
capture, so the evidence is count + exact-length, not the decoded body. g1 id3 is **one generic
config-sync channel** dispatched by `syn_id` (`0x0101` alg, `0x0102` cali, `0x07xx` per-VAP), not
a per-message answer type (correction C20). The five remaining calibration messages (C2) are
built but not yet fired as a sequence.

### F8. The first real Wi-Fi interface on our own image (cfg80211 M0/M1)

**Symptom / context.** Our 6.18 image had no `CONFIG_CFG80211`, so no Wi-Fi interface could
exist even with a working HCC protocol.

**Work.** Added `CONFIG_CFG80211=m` to the board kernel config, built `cfg80211.ko` and a
cfg80211-enabled variant of the clean driver (one wiphy per PCI endpoint, RE-derived 2.4/5 GHz
band and channel capability, standard ciphers, AP+STA+monitor), then built the release-line image,
extracted the kernel slice and flashed only **kernelb** from the stock pair (full-slot hash
verified). The wiphy is registered from a workqueue **only after** the ready handshake (`g4 id1`
+ `g4 id2`) has been decoded; the cfg80211 layer performs no MMIO and sends no H2D frame [V].

**Bench (2026-11-25 15:00).** The box booted the new kernel (GP1 in-probe endpoint pulse visible
in dmesg), the golden boot and ready pair were reproduced, then `cfg80211: wiphy registered after
the ready handshake` appeared; `/sys/class/ieee80211/phy0` exists and the stock `iw` enumerates
`Wiphy phy0`. `iw phy phy0 interface add wlan0 type managed` created a real netdev (ifindex 7,
locally-administered documentation address, type managed) which was deleted again - **M0/M1 are
hardware-validated on our own image with the vendor firmware running golden** [V*].

**Residual risk.** This is the *management* interface: no firmware VAP object exists yet (the
user/vap H2D messages `g0 id7/8` are open payloads [U]), scan and station ops are honest
`-EOPNOTSUPP` stubs, and no data path is attached. The cfg80211 kernel config is a **bench-only
addition**, not part of the upstream luofu patch series (section 11).

### F9. The Wi-Fi frame plane is a separate MAC host-interface ring block

**Symptom / context.** The v1 model treated the ETE engine's idle D2H copy as a possible data-path
blocker, because it was not clear whether Wi-Fi frames ride the HCC/ETE message plane.

**Finding.** There are two data planes. The HCC message plane (12-byte header on the ETE SR/DR
rings) carries only control and event messages. User/AP/STA frames use the **MAC host-interface
ring block at device CA `0x40040000` (band 0) / `0x40060000` (band 1)** - slot 5 of the vendor's
per-band channel table - a distinct block from the ETE register file, with no cross-writes between
the two modules [V].

The ready handshake seeds it: the `g4 id2` payload `0x01060850` is the device-RAM address of the
MSDU-info ring pointer table, ACP-mapped to `0x02060850` and written to MAC slot-5 `+0x10` [V]/[V*].
The vendor's own next frame is the **TXQ-table address sync** (`g0 id1`, key `0x0320`, 4-byte
device VA). TX uses per-TID 8-byte MSDU-info entries; RX uses 256 x `0x63c` + 512 x `0xb0` free
rings plus a 4095-entry completion ring with 0x28-byte device descriptors; the 217/88-slot key
tables A/B are the g0 id-1 command namespace [V]. Details in section 6.4 and table T24.

**Residual risk.** The architecture is mapped, not implemented: no host code programs the MAC
block or moves a frame, the frame-buffer address transform and the RX descriptor bit semantics
need bench captures, and the VAP/scan/join payload structs remain [U]. The direct-read constraint
is an ETE-message constraint only and does not block MAC DMA [I, to be tested by ladder B2].

### F10. The PD4 research patch: necessity, minimality and the audit boundary

**Symptom / context.** The firmware's H2D mailbox consumer is reached only through the image's
event-0x4C dispatcher path. Event 0x4C cannot be raised from the host on this board (the
event-controller/GIC window at CA `0x40160000` is not host-visible), and the image never polls
the mailbox itself, so an unpatched vendor firmware cannot service any host frame [V]/[V*].

**Patch.** PD4 redirects the boot-task success epilogue (file `0x0916A`, 4 bytes) into a 64-byte
guarded out[0] poll stub in a genuinely dead static-data gap at CPU `0x108240`; when the
dispatcher's handler table is still the static sentinel, the stub substitutes a read-as-zero
table for a consume-only pass. The patched image differs from the pinned vendor blob in exactly
two windows (928,920 B length unchanged) [V].

**Audit (delivered by the fw-audit lane, shared task `task-115`; `wifi-fw-audit/REPORT.md`,
harness `verify_pd4.py` 31 checks + 14/14 selftest, `pd4_variants.py` 14/14).** The published
image rebuilds byte-identically from the pinned blob with exactly two segments (4-byte branch at
file `0x0916A` + 64-byte stub at file `0x0C8240`): 68 patched bytes, 59 differing byte positions,
length unchanged 928,920 B, no other byte differs [V]. The cave proof was independently re-run: the
637-byte gap CPU `0x108197..0x108413` is all zero and bracketed by two referenced read-only
tables, outside both boot memsets and the exception-staging frame, and an exhaustive Thumb census
finds zero branch/literal/ADR references into it [V]. The stub only reads mailbox out[0]
(CA `0x40039010`) and calls the firmware's own wrapper CPU `0x40294` -> dispatcher CPU `0xC18AC`
(which performs ack `out[5]=1`, clear out[0], doorbell `out[2]=8`, then handler dispatch); every
device write is the firmware's, and the host "never write CA `0x400392F0`" rule is unchanged [V].
On the bench the cave bytes survived, the g0-id1 fault reproduced and the ordered lap consumed
every announce in 0 polls with mask 0 and the golden page intact [V*].

**Residual risk / honest limits.** The audit's one material open question is **H1/H2**: the stub
compares the instance handler-table pointer field (CPU `0x10C1B0`) with the static value
`0x00118D68`, and static evidence says the field keeps that value (the registration helper reads
it and populates the RAM table rather than rewriting the field). If H1 holds, PD4 always takes the
consume-only branch, the mailbox handlers never execute, and the measured duplex lap is driven by
the dispatcher's ack/clear/doorbell side effects. A five-word **read-only** pack (BAR0 `0x7C41B0`,
`0x7D0D68`, `0x2000`; no fire) settles it on the next attended session [V]/[U]. No vendor-clean
(unpatched-firmware) mailbox-delivery path has been demonstrated: the vendor mailbox sequence is
already reproduced, the deferred-send variant's extra `out[5]=8` pre-kick had been tried once in
the takeover era (accepted, no frame accept), ETE 0x2D/0x2E do not deliver, the ROM has no mailbox
service and the image has no other out[0] reader [V]/[V*]. The one host-only candidate is a spare
inbound iATU viewport onto CA `0x40160000` plus a one-shot raise of event 0x4C (CA
`0x40161188 = 0x1000`, mirror `0x40161288`; enable already set at CA `0x40161108` bit 12) - a new
device-write class, never attempted, needing its own attended design and IRQ-storm bounds
[V]/[I]/[U]. Verdict: firmware patching is required for the closed vendor blob **as delivered**
(conditional on that aperture experiment), not fundamental to the hardware; PD4 stays a
research-only artifact that must never ship [I high].

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

### 5.7 The Wi-Fi frame plane in one paragraph

Wi-Fi frames do **not** travel in HCC messages. The HCC/ETE plane carries control and event
messages only; the frame plane is the MAC host-interface ring block at device CA `0x40040000`
(band 0) / `0x40060000` (band 1), slot 5. The ready handshake's `g4 id2` payload is the
MSDU-info ring pointer table (ACP `0x02060850`) and the vendor's next frame after ready is the
TXQ-table address sync (`g0 id1`, key `0x0320`). TX posts 8-byte MSDU-info entries into per-TID
rings; RX free-buffer and completion rings deposit 802.11 MPDUs behind a 0x28-byte device
descriptor. Section 6.4 and [TABLES.md](TABLES.md) T24 give the full geometry; the direct-read
constraint of section 5.4 applies to ETE messages only [V]/[I].

---

## 6. The operational duplex loop and the interface it enabled

### 6.1 The message-level loop (2026-11-25 05:10)

This is the session shape that closed the Wi-Fi-side RE goal. It runs
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

### 6.2 Calibration ladder C1: the firmware answers the first calibration message

The vendor boot sequence after the TXQ config sync is the factory calibration: six `alg set_*`
config events, HCC **group 0 / id 2, key `0x0101`**, each carrying a fixed 270-byte alg_cfg blob
so the frame total is **298 B** and the SR node declares `0x012A6D2B`. The blob layout is
`u16 cfg_id | u8 type(SET=0) | u8 pkt_type | u32 dev_ret | u8 need_w4 | u8 resv | value[260]`;
the value area starts at blob+0x0A and the request's header field B selects the module/vap object
(default 4, the object our type-1 context-create made). The six vendor commands and cfg ids are:

| order | command | cfg id | value bytes |
| ---: | --- | ---: | ---: |
| 1 | set_2g_power_param | 3502 | 104 |
| 2 | set_2g_low_power_param | 3503 | 104 |
| 3 | set_5g_power_param | 3504 | 70 |
| 4 | set_5g_low_power_param | 3505 | 70 |
| 5 | set_xo_ppm_cali_param | 3506 | 4 (u32) |
| 6 | set_rssi_param | 3507 | 40 |

All six are SET/device-processed and expect exactly one answer: an HCC **g1 id3** config-sync
frame with `syn_id 0x0101`, `body_len 270`, the request cfg id echoed and `dev_ret = 0`; no host
reply is sent. g1 id3 is a generic config-sync channel: a backend must dispatch it by `syn_id`
(`0x0101` alg, `0x0102` cali, `0x07xx` per-VAP capability sync), never by frame size [V].

**Bench (ladder C1, 2026-11-25 12:30).** On a fresh PD4 golden boot with the calibration-token
driver (407,996 B, sha256 `2d45e545...`, 301/301 + 251/251 selftests, `h2d_send_budget=3`):
context-create type 1 and TXQ sync consumed in 0 polls, then ONE `set_rssi_param` (3507,
zero-value structural frame) committed as a 298 B frame in a 298 B node and consumed in 0 polls;
the device ring catalogue then showed a **second g1 id3 with `last_total = 0x012A` (298 B)** -
the calibration answer mirroring our request length. Boot page `0x86807FEF/7/2/0xABCD`, state 1,
mask 0 stayed intact; `h2d_status` ok=3 timeout=0; no exception. The answer body was recycled
before the read-only capture, so body semantics (cfg id echo, dev_ret) are expected-by-construction
rather than observed; a future fire can capture it with an immediate post-fire read [V*].

**Next (C2).** The remaining five messages in vendor order with `h2d_send_budget=8`
(create + TXQ + six calibrations is the driver maximum), one new message class per boot until the
sequence is proven. Values are unit-specific: only the zero-value structural frames are used
here; real per-unit calibration values stay local (section 11).

### 6.3 The first real Wi-Fi interface on our own image (2026-11-25 15:00)

This is the milestone that turns the protocol into a user-visible Wi-Fi interface.

**Image build.** The target kernel config gained `CONFIG_CFG80211=m` (plus the resolved
WLAN/RFKILL/signed-regdb symbols). The board image built clean: `cfg80211.ko` 2,328,548 B
sha256 `949393f9...`; sysupgrade container 6,267,199 B sha256 `22b72852...`; the kernel slice
extracted from it is 3,372,638 B sha256 `13e81214...` with full-slot hash `1794a531ed19...`.
The clean driver was rebuilt against that tree (505,076 B sha256 `d2b3cf79...`,
`depends=cfg80211`, 0 warnings) [V].

**Flash pipeline (bench-only, attended).** The box was running our GP1-line image on pair B and
stock on pair A. The new kernel slice was fetched by the stock pair, written to **kernelb** with
`mtd erase/write` and verified with the full-slot hash, the pristine stock files were restored,
the handover used the documented route (`bootflag=b` + `boot_reg=0x21`), and the box rebooted.
Stock pair A kernel+rootfs remained untouched (the write went through stock's own mtd), so the
physical recovery path stayed intact; the recovery door was armed during the flash. Exact
artifacts, hashes and steps: table T21 and `wifi-upstream-target/CFG80211-BENCH-IMAGE.md` [V*].

**Bench result.** The new kernel came up on our image (`uname -r` 6.18.60, rootfsb, selector raw
`0x11`) with the GP1 in-probe endpoint pulse visible in dmesg. `insmod cfg80211.ko` then
`insmod hi5622_wifi.ko` both returned 0; the driver allocated one wiphy per PCI endpoint (2.4 GHz
13 channels / 5 GHz 19 LU-plan channels) with registration **gated on the ready handshake**. After
the standard boot sequence (prepare/upload/verify/seed/arm/loop/release) the golden page and ready
pair were reproduced, dmesg printed `cfg80211: ready handshake decoded ... scheduling wiphy
registration` then `cfg80211: wiphy registered after the ready handshake`, and
`/sys/class/ieee80211/phy0` appeared. The stock `iw` binary (121,952 B, pushed to /tmp with
libnl-tiny) enumerates `Wiphy phy0`; `iw phy phy0 interface add wlan0 type managed` created a
real netdev (ifindex 7, type managed, locally-administered documentation address) and
`iw dev wlan0 del` removed it. **M0 (wiphy+netdev) and M1 (VAP lifecycle) are
hardware-validated on our own image with the vendor firmware running golden** [V*].

**Honest limits.** This is the management interface only: the firmware VAP objects are not
created (the `g0 id7/8` user/vap-add payloads are open [U]), scan/get_station/dump_station are
registered `-EOPNOTSUPP` stubs (M2), every M3+ op is NULL, and no TX/RX data path is attached.
The cfg80211 kernel config is bench-only and is not part of the luofu kernel patch series
(section 11). The wiphy band mapping (domain 0 -> 2.4 GHz) is an inference with a `band_force`
override; the bench saw both bands listed on the two endpoints, not a per-band link test (C26).

### 6.4 The data-plane architecture (mapped, not implemented)

The frame plane is fully mapped statically from the vendor host driver and the firmware, and must
be added to the clean driver; **no frame has been sent or received by our host** (details in
`wifi-datapath/REPORT.md`, tables T24).

**Two planes.** HCC/ETE = control/event messages (working). MAC slot-5 host-interface block =
Wi-Fi frames (not implemented). The blocks are disjoint: `plat.ko` writes only ETE CAs, `wifi.ko`
only MAC CAs. The direct-read workaround is therefore a message-plane workaround; MAC DMA deposits
are expected to work independently once the block is programmed [V]/[I].

**Seeding.** The ready handshake's `g4 id2` payload `0x01060850` is the device-RAM address of the
MSDU-info ring pointer table; the host maps it as ACP `0x02060850` and writes that to MAC slot-5
`+0x10` for both bands. The vendor's next frame is the TXQ-table address sync: `g0 id1`, key
`0x0320`, 4-byte device VA of a 0x2524-byte host-DRAM TXQ table [V].

**TX.** One non-cached ring per (user,TID); each 8-byte entry is
`{u32 buf_devva; u32 ctl}` with `ctl[11:0]` length, `[14:12]` TX path class, `[15]` header flag,
`[19:16]` tid and `[21:20]` vap type (vendor 1/2/3). The host builds a 20-byte DMA descriptor at
`skb->data-0x14`, maps the frame buffer, posts the entry, and publishes the packed producer index
(15-bit index + phase bit 15) with device-visible barriers. Completions arrive through the
256-entry `tx_ba_info` buffer and/or core-g0 TX events; the 16-byte completion record is the
natural first parse target [V].

**RX.** Host pre-fills `rx_norm_data_free_ring` (256 x 0x63c B) and `rx_small_data_free_ring`
(512 x 0xb0 B), advances the packed +0x38/+0x3c write pointers, drains
`rx_data_cmp_ring` (4095 entries) via +0x40/+0x4c, parses the 0x28-byte device descriptor at the
start of each buffer, strips it, handles A-MSDU chains, resolves VAP/user/tid, converts 802.11 to
Ethernet and reposts the buffer. The MAC interrupt status/mask (+0x44/+0x48, mask value
`0xfffffcc1`) is the polling alternative to ETE completion [V].

**Control messages around the data path.** `g0 id7/8` user-add (0xA8 each), `g0 id9` del-user
(0x0C), `g0 id0xC` scan request (0x1FC), `g0 id0xF/0x10/0x11` STA join family (0x40/0x10),
`g0 id0x12` EDCA (0x74), `g2` WAL config events (AP start etc., ids [U]); D2H results are
`g1 id15/16/17`, `g2 id2` and `g2 ids 3-6`. Only the config-event shape is byte-exact today; the
open payload structs are the wall for VAP/scan/AP. The 217/88-entry key tables A/B decode this
namespace; every vendor host key from the shipped modules resolves in them [V]/[U].

**Bring-up order proposed by the lane.** D0 MAC register model -> D1 RX rings + NAPI skeleton ->
D2 TX ring + TXQ address sync -> D3 user/VAP messages -> D4 scan -> D5 STA join, with a
**read-only** first check (ladder B2: single read of CA 0x40040000 and of the ready-seeded +0x10)
before any host write, because an earlier attempt to add an extra iATU viewport rebooted the box
[I]/[V*].

### 6.5 The ETE engine: the host lever is closed, direct read is the supported RX path

**Trigger chain (fully reversed).** The firmware reads the ETE status word `CA 0x4003A874` only
in the event-0x2E/0x2D dispatchers (CPU `0x4624C`/`0x462F8`), which are reachable only through a
delivered device hardware event. Status bits **24-28** call the sending trigger CPU `0xC625A`,
which reads the hardware-authored send word `CA 0x4003AC00+4*ch` (target = high half), handshakes
the target index into the channel's PKTRAM control word, releases tagged nodes and wakes the
message object. Whole-image and vendor-host scans find **no** software writer of the status word,
the send words or the engine doors/global: the start is device hardware/ROM state [V].

**The one host-owned delta and its test.** The DR group-B destination rings have exactly one
host-owned field: the published index `+0x38`. Our clean driver arms it to `0x400` (32 pre-posted
buffers) while the vendor init writes 0 and a working vendor boot shows `published == fill`. Ladder
E1a wrote `+0x38 = fill` on all four channels (read-back OK) and E1b wrote 0 on a fresh boot; on
both, the engine status bits stayed 0, the trigger counters and send words stayed 0 and the fills
stayed `0x10` - the SR/mailbox notification path kept running, the DR copy never started. The engine door
`CA 0x4003A7C0` differed from the vendor value (`0x844C0000` vs vendor `0x83FAC000`) and has no
host writer [V*].

**Verdict.** The DR (D2H) A->B copy is firmware/ROM-internal; no host register work is justified.
The direct device group-A read is the supported RX path (and the only one on this firmware), with
the limits stated in F4: non-vendor ABI layout constants, polled (no DMA to host DRAM), no engine
flow control (bounded batches, overrun reporting), read-only on device state. The SR (H2D) A->B
copy, by contrast, runs normally - that asymmetry is what keeps the H2D send path working [V*]/[I].

### 6.6 The PD4 research patch: audit and boundary

The PD4 patch is the reason the H2D mailbox works on an IRQ-dead boot (sections 3.8, F3, F10).
The audit has now been delivered by the fw-audit lane (shared task `task-115`) as
`wifi-fw-audit/REPORT.md` plus a reproducible harness (`verify_pd4.py` 31 checks + 14/14
selftest; `pd4_variants.py` 14/14). Its verdicts:

* **Identity.** The published image (`cfe24bfb...`) rebuilds byte-identically from the pinned
  vendor blob with exactly two segments: the 4-byte entry branch at file `0x0916A` and the 64-byte
  stub at file `0x0C8240` - 68 patched bytes, 59 differing byte positions, length unchanged,
  nothing else differs [V];
* **Cave safety (independently re-run).** The stub lives in the 637-byte all-zero static-data gap
  CPU `0x108197..0x108413`, outside both boot memsets and the exception-staging frame
  `0x4000C..0x4004B`, between two referenced read-only tables; the exhaustive Thumb census finds
  zero branch targets, literal loads, ADR/ADDW materialisations, aligned 32-bit values or movw/movt
  pairs pointing into the gap [V];
* **What it changes.** The boot-task success epilogue becomes a poll of mailbox out[0]
  (CA `0x40039010`, read-only in the stub) that calls the firmware's own wrapper CPU `0x40294` ->
  dispatcher CPU `0xC18AC`; the dispatcher acks (`out[5]=1`), clears out[0], writes the H2D-done
  doorbell (`out[2]=8`) and dispatches the pending bit through the instance handler table. Every
  device write is the firmware's own; our host "never write CA `0x400392F0`" rule is unchanged
  [V]; the boot page/state/mask and the bench-validated duplex lap are untouched [V*];
* **Open question H1/H2.** The stub's sentinel compare uses the instance handler-table pointer
  field (CPU `0x10C1B0`, static value `0x00118D68`); static evidence says no image writer replaces
  it, so the stub may always take the consume-only ROM-RAZ branch, in which case PD4 services the
  mailbox through the dispatcher's ack/clear/doorbell side effects without executing the handlers.
  A five-word **read-only** pack (BAR0 `0x7C41B0`, `0x7D0D68`, `0x2000`) settles it without a new
  fire [V]/[U];
* **Variants ranked.** `PD4-NOSENT` (force live branch, sha256 `0bc1b3e8...`) and
  `PD4-ALWAYS0` (force consume-only, sha256 `84e9cd95...`) are built and hashed;
  `PD4-MIN` (36-byte delta) and `PD4-LITE` (44-byte) are specified; the one-shot epilogue-call
  variant is rejected (the host posts frames after boot) [V];
* **Vendor-clean alternatives.** No host-only path is proven. The vendor mailbox sequence is
  already reproduced; the vendor's deferred-send additionally writes `out[5]=8` pre-kick and was
  tried once in the takeover era (accepted, no frame accept); ETE 0x2D/0x2E do not deliver, the ROM
  has no mailbox service, the image has no other out[0] reader, and CA `0x40160000` is outside the
  host-visible device-IO region [V]/[V*];
* **The one remaining host-only candidate** is a spare inbound iATU viewport onto device CA
  `0x40160000` plus a one-shot raise of event 0x4C (CA `0x40161188 = 0x1000`, mirror
  `0x40161288`; per-event enable already set at CA `0x40161108` bit 12). This is a new
  device-write class (firmware-owned interrupt controller) and has not been attempted; it needs its
  own attended experiment, recovery card and IRQ-storm/one-shot bounds [V]/[I]/[U].

**Verdict.** Firmware patching is required for the closed vendor blob **as delivered** on this
board; it is conditional only on the untested event-controller aperture experiment and is not a
fundamental property of the hardware. PD4 stays a **research instrument only, never a shipping
change**, and is published as a patch recipe plus verifier, not as vendor firmware bytes (section
11). The audit also corrects two older lane statements (section 8, C27/C28).

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
| 2026-11-25 07:40 | lap reproducibility | the ordered lap is deterministic on a fresh boot: identical ctx addresses/values and a clean mask-0 outcome | ledger 2026-11-25 07:40 [V*] |
| 2026-11-25 08:40 | data-path + bench-auto fire | frame plane identified as the separate MAC slot-5 ring block; first automated protocol-runner fire exposed and fixed host-side transport/capture bugs | ledger 2026-11-25 08:40 [V]/[V*] |
| 2026-11-25 09:10 | cfg80211 lane lands | wiphy/VAP skeleton (M0-M2) compile-clean host-side; CONFIG_CFG80211 blocker handed to the target lane | ledger 2026-11-25 09:10 [V] |
| 2026-11-25 10:10 | ETE-wake E1a/E1b | the only host-owned DR lever is negative: status/send/fills unmoved; DR copy classified device/ROM-internal | ledger 2026-11-25 10:10 [V*] |
| 2026-11-25 11:00 | proto-seq | g1 id3/id0x12 decoded; the six calibration frames built (cfg ids 3502-3507) | ledger 2026-11-25 11:00 [V] |
| 2026-11-25 11:40 | bench-auto rebuild | protocol-mode capture root-caused to the raw-read transport (resource0 is mmap-only); static bar0read helper, readpath self-check, 312/312 | ledger 2026-11-25 11:40 [V] |
| 2026-11-25 12:30 | **ladder C1** | calibration set_rssi_param (298 B, g0 id2 key 0x0101) consumed in 0 polls; firmware answers 298 B g1 id3; page/state/mask clean | ledger 2026-11-25 12:30 [V*] |
| 2026-11-25 13:30 | wave-2 close | all 7 wave-2 lanes delivered; target build verified (image 22b72852..., kernel slice 13e81214..., cfg80211 config in tree) | ledger 2026-11-25 13:30 [V] |
| 2026-11-25 15:00 | **first Wi-Fi interface** | cfg80211 image flashed to kernelb (full-slot 1794a531...); new kernel booted; wiphy registered after the ready handshake; iw enumerates phy0; VAP wlan0 created and deleted (M0/M1) | ledger 2026-11-25 15:00 [V*] |

End-of-chain bench state (ledger 2026-11-25 15:00): the box runs the cfg80211 bench image on
pair B (new kernel on kernelb, rootfsb unchanged, stock pair A untouched and bootable), with the
firmware booted golden, the wiphy registered and the test VAP deleted; no writes are pending and
the recovery path is the stock pair / physical recovery. This report's cut-off is that entry.
The only post-cut-off artefact folded in is the **host-only PD4 audit** (no device session; section
6.6, `wifi-fw-audit/REPORT.md`), delivered after the commit window and integrated here.

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
| C17 | no Wi-Fi interface can exist until the whole stack is built | the cfg80211 management interface is independent: wiphy registration gated on the ready pair and the nl80211 VAP netdev lifecycle were hardware-validated on our own image | image flash + bench | 2026-11-25 15:00 |
| C18 | `CONFIG_CFG80211` unset in the target tree blocks any interface | the target config gained `CONFIG_CFG80211=m`; `cfg80211.ko` (2,328,548 B sha256 `949393f9...`) was built, loaded and exercised on the bench image | BUILD-VERIFICATION sec 5 + bench | 2026-11-25 13:30/15:00 |
| C19 | the ETE DR A->B copy may wake from the host-owned published-index write | E1a (write fill) and E1b (write 0) both left status/send/fills unmoved; the DR start condition is device/ROM state, not a missing host write | ete-wake REPORT sec 8 + bench | 2026-11-25 10:10 |
| C20 | g1 id3 (36 B) is the TXQ-sync answer and g1 id0x12 the context-create answer | g1 id3 is a generic config-sync frame dispatched by `syn_id` (`0x0101` alg / `0x0102` cali / `0x07xx` per-VAP); the 36 B frame is an unsolicited VAP-MIB update (syn `0x07E0`), and g1 id0x12 is the ACS-response family forwarded opaquely | proto-seq REPORT sec 2-3 | 2026-11-25 11:00 |
| C21 | Wi-Fi frame data rides the HCC/ETE path, so the idle ETE engine blocks the data plane | frame data uses a separate MAC host-interface ring block (CA `0x40040000/0x40060000`, slot 5); the direct-read constraint is an ETE-message constraint only | datapath REPORT sec 2/8 | 2026-11-25 08:40 |
| C22 | the first TXQ-sync fault was a 48-vs-72-byte node-length problem | the fault was the missing context-create precondition (handler dereferences `ctx+0x190`); the 72-byte node form is the captured wire/node shape, not the fix | txq72 + ctx-create + ordered lap | 2026-11-25 00:20-05:10 |
| C23 | empty benchmark step captures meant a sequence failure | the capture failed because `dd if=resource0` is not a valid MMIO read on the lab kernel (resource0 is mmap-only); a static arm-musl `bar0read` helper fixed it and empty reads now report device-read-empty/MISSING, never PASS | bench-tooling REPORT sec 8.1 + ledger | 2026-11-25 11:40 |
| C24 | an unconsumed announce after a solo TXQ sync proves the firmware consumer died | it can be the exception cascade: the handler fault corrupts the PD3 poller in the staging area; with the context precondition satisfied and the PD4 cave, announces are consumed in 0 polls | txq72/PD4 + ordered lap | 2026-11-25 01:10-05:10 |
| C25 | cfg80211 belongs to the upstream target/kernel series | the cfg80211 config is a BENCH-ONLY addition; it is not part of the luofu kernel patch series or the proposed submission v2 | CFG80211-BENCH-IMAGE sec 5 + upstream REPORT sec 12 | 2026-11-25 13:30 |
| C26 | the wiphy band mapping "domain 0 = 2.4 GHz" is proven | it is an inference (`[I]`) with a `band_force` override; the bench saw both bands listed on the two endpoints, not a per-band link test | cfg80211 REPORT sec 3/10 R4 + bench dmesg | 2026-11-25 15:00 |
| C27 | the firmware event-raise helper writes event 0x4C to CA `0x401611A0` (PD3B design note) | the helper CPU `0xC6DB8` uses base CA `0x40161180` + word index `id>>5`, so 0x4C targets CA `0x40161188` / mirror `0x40161288` (bit 12 = `0x1000`); `0x401611A0` would be id 0x100, outside the helper range. Matters only if the audit's A3 aperture experiment is ever designed | `wifi-fw-audit/REPORT.md` sec 13 C1 + `verify_pd4.py` | 2026-11-24 (audit) |
| C28 | "twin/ack word" table maps `ctx+0x34` to a separate "twin copy B" register | the six context slots `ctx+0x2c..0x40` are `{out[0], out[1], out[2] doorbell, out[3] D2H ack, out[4] D2H rearm, out[5] ack}`; the vendor's "twin" write is simply the out[2] doorbell OR-1 that the clean transport already reproduces - there is no extra host register to write | `wifi-fw-audit/REPORT.md` sec 13 C2 | 2026-11-24 (audit) |

---

## 9. Verification and reproduction

### 9.1 What was re-run while consolidating this report

Consolidation did not invent evidence; it re-ran the owning lanes' committed device-free checks to
make sure the facts this report leans on still reproduce from the tree as committed. v2 additionally
re-ran every wave-2 lane check whose tools are host-only (the cfg80211 kernel-module build itself was
verified by its owning lane, not re-built here to avoid touching the shared kernel tree). All checks
are host-only and device-free:

| check | result | what it pins |
| --- | --- | --- |
| `wifi-hcc-core/tools/build.sh --no-kernel` | **PASS**, 528 checks, 0 failures | the post-ready state machine: boot latch, ready pair, TXQ sync, mailbox coalescing, CECE policy, ring arithmetic |
| `wifi-h2d-frames/tools/build.sh` | **BUILD OK**, 233 passed, 0 failed | 12-byte header contract, byte-exact builders (incl. TXQ sync), refusal policy, ready decoder |
| `wifi-d2h-direct/tools/d2h_selftest.py` | **PASS**, 92/92 | direct-read service: node/frame decode, catalogue, consumer-advance rules |
| `wifi-ctx-create/tools/ctx_create_re.py --selftest` | **PASS**, 40/40 | key tables, handler/worker/helper chain, read pack |
| `wifi-ctx-create/tools/ctx_create_frame.py --selftest` | **PASS**, 26/26 | the context-create frame builder |
| `wifi-bench-tooling/tools/selftest.py` | **PASS**, 312/312 (was 177 in v1) | session runner incl. the data-driven protocol mode, guards, patch builder, audit |
| `wifi-datapath/tools/build.sh` + `fw_dp_tables.py --selftest` | **PASS**, 77/77 + 35/35 | two-plane model, TX/RX ring builders, key tables A/B |
| `wifi-proto-seq/tools/selftest.sh` | **PASS**, 66/66 + 38/38 + 55/55 | config-sync decode, calibration frame builders, alg command table |
| `wifi-ete-wake/tools/ete_wake.py selftest` | **PASS**, 81/81 | ETE trigger chain, host/vendor comparison, BAR0 map |
| `wifi-cfg80211` host steps (replicated device-free) | **PASS**, 454/454 cfg80211 logic + 242/242 inherited H2D | wiphy/VAP model, registration gate, 39-op map |
| `wifi-cfg80211/tools/selftest.py --kdir <tree>` | **144/145**; the single failure is the known provider-symvers redundancy after the real tree began exporting the six cfg80211 symbols (documented in `wifi-upstream-target/BUILD-VERIFICATION.md` sec 5) | source/contract checks |
| `wifi-fw-audit/tools/verify_pd4.py` + `pd4_variants.py` | **PASS**, 14/14 selftest + 31/31 checks + 14/14 variants | PD4 identity/delta, cave census, H1/H2 observation and the diagnostic variant builds |
| `wifi-re-report/tools/verify_report.py` (this lane) | **PASS** (see 9.4) | this report's cross-references, markers, claim IDs and key constants |

The remaining lane checks (e.g. `rom_s01.py` 70/70, `romdis.py` 86/86, `fw_pd_builder.py`
56/56, `pd2_re.py` 33/33, the target-tree mocked harness 94/94 + 103/103 + 77/77) were re-run by
their owning lanes in the sessions cited in section 7 and read the local vendor blob / target tree
in place; they are quoted here from those lane records and were not re-executed in this
consolidation pass to avoid concurrent vendor-blob or kernel-tree use while other lanes were
working. That is a bookkeeping distinction only - those checks are committed and runnable per the
lane READMEs.

The one re-run that needs a note is the cfg80211 source selftest: its provider-symbol check fails
by design on the current tree because `Module.symvers` now exports the six `wiphy_*/cfg80211_*`
symbols the lane previously declared in a provider file. The owning lane's compile+modpost steps
pass and the upstream-target lane recorded the same reconciliation; this is a lane follow-up (retire
the redundant provider file), not a report defect.

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
  `wifi-bench-tooling/tools/fw_patch_builder.py` (312-suite) rebuild/verify the PD recipes.
* Frame plane and calibration: `wifi-datapath/tools/build.sh` + `fw_dp_tables.py --selftest`
  (77 + 35), `wifi-proto-seq/tools/selftest.sh` (66 + 38 + 55), `wifi-ete-wake/tools/ete_wake.py
  selftest` (81).
* cfg80211 management layer: the lane's host steps (454 + 242), `gen_cfg80211_symvers.py`, and the
  source selftest (144/145, known provider-symvers redundancy).
* Target/bench image: `wifi-upstream-target/BUILD-VERIFICATION.md` has the exact build commands and
  artifact hashes; the flash steps are in `CFG80211-BENCH-IMAGE.md` sec 3 (bench-only, attended).

### 9.3 Reproducing the bench result

The bench chain is documented in the lanes' BENCH-PROCEDURE files, in this order: clean boot with
the reset pulse -> prepare/upload/verify -> custom-block seed -> hcc_arm/hcc_loop -> release-only
-> golden gate -> ready pair (direct reader) -> one context-create type 1 -> one TXQ sync ->
direct-read the responses. Ladder C1 then adds one calibration frame (section 6.2); the cfg80211
observation session adds the module load, ready-gated wiphy registration and the nl80211 VAP
create/delete cycle on the cfg80211 bench image (section 6.3). The generic ladder with pass gates
is [TABLES.md](TABLES.md) T19.

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
   `wifi-irq-unmask`'s reports, the v2 image/slice hashes appear in
   `wifi-upstream-target/BUILD-VERIFICATION.md`, the calibration family in `wifi-proto-seq`'s
   report, the MAC ring block in `wifi-datapath`'s report) and the end-state claims also appear in
   the append-only ledger and `STATUS.md`;
7. the v2 tables T21-T25 exist, the report covers the v2 milestone vocabulary (cfg80211, wiphy,
   calibration, E1a, slot hash) and the new claims stay gap-free in [EVIDENCE-INDEX.md](EVIDENCE-INDEX.md);
8. no forbidden content classes (private keys, secrets paths, oversized hex/base64 blobs that
   would smell of vendor binaries, per-unit MAC addresses) are present.

Run it with the project venv python. It prints one PASS/FAIL line per check group and exits
non-zero on failure.

---

## 10. Limitations and open questions

### 10.1 Limits of the result

1. **The firmware is closed.** Everything here is interoperability research against a binary
   firmware. No upstreamable clean-room Wi-Fi driver follows from it while the blob remains
   closed; the project's own position is that vendor GPL source is the route to a shippable stack.
   The vendor GPL request is unanswered to date.
2. **PD3/PD4 are research instruments.** They patch the vendor firmware image to call its own
   dispatcher. They are bench-validated but must never be presented as product changes. The audit
   (section 6.6) confirms no vendor-clean mailbox-delivery alternative has been demonstrated; a
   shipped driver on this firmware does not exist.
3. **The result is message-level plus management-level, not link-level.** Two ordered config
   messages and one calibration message were consumed and answered; a cfg80211 wiphy + VAP netdev
   lifecycle was hardware-validated. None of that proves a working Wi-Fi link: no firmware VAP
   object exists, no scan returns results, no association happens and no frame is transmitted.
4. **The ETE DR direction is closed, not solved.** The host lever (E1a/E1b) is measured negative,
   the direct device-ring read remains the supported RX path, and it is a research instrument: it
   depends on this firmware's internal PKTRAM layout, it is polled with bounded batches and
   overrun reporting (no engine flow control, no DMA to host DRAM), and it is not the vendor ABI.
5. **The firmware IRQ/thread layer is still dormant.** PD4 services the mailbox, but the image
   dispatcher, event controller and scheduler services the vendor stack depends on are not
   exercised; later flows (scan completion, TX status, PM) may need them.
6. **Some frames are observed but not fully decoded.** The two v1 responses (g1 id3 36 B, g1 id0x12
   40 B) are now decoded structurally (C20); the calibration answer body was counted but not
   captured (length-only evidence), and g1 id0x12's body semantics remain device-side [U].
7. **The context-create frame is synthesized**, not captured from the vendor driver; the vendor's
   sender for key `0x0064` is unproven [I]. Type-1/2/3 semantics are inferred from the allocator
   analysis plus one type-3 and one type-1 lap.
8. **The Wi-Fi interface is M0/M1 only.** The wiphy is registered and VAP netdevs can be created
   and deleted, but no firmware VAP object is created: the user/vap-add H2D payloads (`g0 id7/8`,
   0xA8) and the scan request (`g0 id0xC`, 0x1FC) are open [U]; scan/get_station/dump_station are
   honest `-EOPNOTSUPP` stubs and every M3+ op is NULL.
9. **The MAC data plane is mapped, not implemented.** No host code programs the slot-5 MAC block,
   the TXQ-table address sync has not been fired, no TX ring entry or RX buffer moves a frame, and
   the frame-buffer address transform and RX descriptor semantics still need bench captures
   [V]/[I]/[U].
10. **The cfg80211 bench image is a research instrument, not a release.** It carries a bench-only
    kernel config (not in the luofu patch series), the modules are loaded from `/tmp` (not packaged
    in the rootfs), the image is installed manually from stock, and the install path has never run
    end to end on a device.
11. **Per-unit data stays out.** No MAC/serial/calibration value is in this report by policy; the
    VAP address quoted by the lane is a locally-administered documentation address, not a unit MAC.

### 10.2 Ranked open questions

1. What are the exact user/vap-add H2D payload structs (`g0 id7/8`, 0xA8 each) and the scan request
   (`g0 id0xC`, 0x1FC)? They are the wall between "interface exists" and "firmware VAP/scan works";
   each needs a struct-RE lane or one vendor kprobe capture.
2. Can the MAC data plane be brought up on the bench with the mapped slot-5 block (read-only B2
   check, then RX rings, then TXQ address sync + first TX frame)? This is the first physical test
   of the two-plane model.
3. Can the calibration answer body be captured (immediate post-fire read or a driver logging
   extension), and do the five remaining messages in vendor order behave the same as C1?
4. What unmasks the firmware's IRQ layer on a real vendor boot, and can the image dispatcher be
   brought up legitimately (without a firmware patch)? This is the difference between a research
   instrument and a usable driver. The audit's answer is provisional: no host-only path is proven,
   the H1/H2 read-only pack must be taken, and the only candidate (spare-iATU aperture onto the
   event controller + one-shot raise of event 0x4C) is an untested device-write class (section
   6.6).
5. What is the exact vendor CECE service protocol (order, duration, ack semantics) for the image
   handlers, and does any later flow require it?
6. What are the `g2` WAL config-command ids/payloads for AP start, key install and per-VAP config,
   and the `g0 id0xF/0x10/0x11` STA join shapes?
7. What do the D2H scan/join/PM result frames (g1 15/16/17, g2 2/3/4/5/6, g3 17) mean
   field-by-field, and what ordering do they require?
8. What is the DR A->B copy's device/ROM start condition, and could a future firmware patch or a
   vendor-clean host path make the D2H path engine-based instead of direct-read?
9. Is there a host-visible trigger for the firmware's event layer that the vendor host performs
   before the ready exchange, or is it purely ROM/image internal? (Current answer: nothing visible;
   the vendor sends nothing between release and ready - a negative result on the modules scanned,
   not a proof about ROM code we cannot see.)

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

**The bench image is part of the experiment, not a deliverable.** The cfg80211 kernel is a
bench-only config addition (it is not in the luofu kernel patch series), the Wi-Fi modules are
loaded from `/tmp` rather than packaged in the rootfs, and the image was installed manually from
stock. The board booted it and enumerated a Wi-Fi wiphy + VAP (section 6.3); that is a hardware
milestone for the research line, not a distributable OpenWrt image and not a claim of install-path
readiness.

**Why it is publishable.** The repository is an intentionally public research notebook: negative
results, dead ends and the ledger are part of the record. This report is written to that standard:
it marks inference as inference, lists the corrections that cost sessions, and does not present
bench instruments as products. The public repos' pre-policy history caveat (an older lab credential
and vendor objects before a policy purge) is documented in `PUBLISH.md` at the project root and
is not repeated here.

**Upstream status.** Blocked for Wi-Fi: an upstream OpenWrt submission of a Wi-Fi stack for this
board requires a licensable firmware/driver path. The vendor modules declare GPL-2.0 and a GPL
source request names the exact symbol families/headers (wifi-driver-re SUMMARY); it is unanswered
to date. The clean-room line's ceiling is the message-level protocol plus the M0/M1 management
interface proven here; the firmware remains closed, PD4 must never ship, and the MAC/MLME/data path
remain vendor code. The target side did advance: the board tree builds end to end, the GP1 reset
pulse and the boot-pair recorder fixes are committed, the bench image was built and booted, and
the 67 target patches were classified for submission (26 keep / 17 fold / 24 drop -> proposed
30-patch series + DTS-0001). The install path has still never run end to end on a device, the
submission form has not been regenerated, and the history purge is pending - so this is still not
an upstream submission (see `wifi-upstream-target/REPORT.md` and `docs/ROADMAP-OPENWRT.md`).

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
| `wifi-cfg80211` | section 6.3: cfg80211 M0-M2 lane - wiphy, VAP model, registration gate, 39-op map |
| `wifi-datapath` | section 6.4 / T24: two-plane model, MAC slot-5 rings, TX/RX formats, key tables A/B |
| `wifi-ete-wake` | section 6.5: ETE trigger chain, host-owned field comparison, E1a/E1b ladder |
| `wifi-proto-seq` | sections 5.7/6.2: config-sync decode, calibration family and response contract |
| `wifi-upstream-target` | sections 6.3/11: target tree fixes, bench-image build/flash, submission classification |
| `wifi-fw-audit` | section 6.6 / F10: PD4 minimality/safety audit, H1/H2 open question, ranked variants and the vendor-clean alternative analysis (task-115) |

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
| **cfg80211 / wiphy** | the Linux wireless management layer; a wiphy is one radio instance advertised to nl80211. Our M0/M1 = wiphy registration + VAP netdev lifecycle. |
| **VAP** | virtual access point: an interface on the wiphy and (on the firmware side) a user/vap object; created here via nl80211 `interface add` |
| **M0 / M1 / M2** | cfg80211 milestones: M0 wiphy+netdev model, M1 VAP lifecycle, M2 scan/station stubs |
| **MAC host-interface block** | device MAC block at CA `0x40040000`/`0x40060000` (slot 5) that carries the Wi-Fi frame DMA rings, separate from HCC/ETE |
| **MSDU-info entry** | 8-byte TX ring entry `{u32 buf_devva; u32 ctl}` with len/path/flag/tid/vap bitfields |
| **TXQ table** | 0x2524-byte host-DRAM table whose device VA is synced by the H2D `g0 id1` key `0x0320` message |
| **calibration ladder C1 / C2** | C1 = one calibration message (`set_rssi_param` 3507) after the ordered lap; C2 = the remaining five in vendor order |
| **E1a / E1b** | the ETE DR published-index experiments (write `+0x38` = fill / = 0) that came back negative |
| **image slice / slot hash** | the `kernel.bin` extracted from the sysupgrade container (3,372,638 B sha256 `13e81214...`); its full-slot hash `1794a531...` is what the flash verifies in kernelb |

Conventions used in the tables: `??` = header byte not initialised by the firmware builder;
"BAR0 = CPU + 0x6B8000" is the CPU->BAR0 rule; "BAR0 = 0x3B8000 + (CA - 0x40000000)" is the IO-CA
rule; `x` in hashes is an abbreviated digest whose full value is in the owning lane report.




