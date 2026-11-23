# ETE frames: the vendor's H2D first-send / ready-path frames (Hi5622V100, WR3000 v2.0)

**Lane:** ete-frames (shared task `task-79`), **host-only**, 2026-11-23.
**Status:** static reversal complete from the two vendor module pairs + the local firmware image;
bench sequence written; **no device contact, no image build, no vendor binary / firmware byte
committed.** Companion inputs (read first): `../wifi-hcc-re/REPORT.md` §3/§4/§5,
`../wifi-fw-ready-gate/REPORT.md`, `../wifi-boot-kick/REPORT.md` §18-21,
`../wifi-h2d-catalog/REPORT.md`, `../wifi-h2d-inject/REPORT.md`, `../wifi-hcc-link/REPORT.md`,
`../wifi-transport-port/b3/B3-PLAN.md`.

**Evidence markers:** **[V]** verified here from a named local artifact (fresh disassembly / byte
assertion in this session); **[V\*]** device-measured by an earlier attended lane, cited;
**[I]** inference; **[U]** unknown.

**Inputs (local only, never copied here):**

| artifact | identity |
| --- | --- |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_plat.ko` (2.5.24) | 364,660 B, sha256 `6f2eac41…` |
| `/tmp/rootfs2524b/lib/modules/5.10.201/hi5622v100_wifi.ko` (2.5.24) | 3,564,728 B, sha256 `de78ec07…` |
| `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko` (2.4.15) | 371,708 B, sha256 `27c697918…` |
| `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko` (2.4.15) | 3,581,748 B, sha256 `22fa789a…` |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | 928,920 B, sha256 `7fc87e20…ef311b` |

Conventions: `.ko` offsets are **section-relative vaddrs** (`nm` values, `.text` `sh_addr=0`);
firmware offsets are **file** offsets, **runtime/CPU = file + 0x40000** (bench-verified in
`wifi-h2d-catalog`); device CA ↔ host BAR0: **BAR0 = 0x3B8000 + (CA − 0x40000000)**.

---

## 0. TL;DR - the four answers, and the one correction that matters

1. **There is NO vendor H2D frame in the release→ready window.** After the `0x5a5a` release the
   vendor host sends the device *nothing* until DEVICE READY has completed; the boot dialogue is
   device→host only. This was independently reached by `wifi-hcc-link` §0.3 [V] and is
   re-verified here: the only init-path HCC sender reachable from `hmac_main_init` is the
   post-ready TX-queue config sync `hdpp_config_send_event` (group 0, id 1) [V/I].
2. **The frames the lead asked for as "host sends during the ready exchange" are DEVICE→HOST
   frames and the host only *receives* them** (HCC chip-group 4, ids **1** then **2**). Their
   exact wire form is recovered byte-for-byte from the firmware builders (§2): id1 = 12-byte
   header only; id2 = 16 bytes = 12-byte header + **4-byte payload = a device RAM address**
   (`0x01060850` in this image), *not* a 100-byte struct on the wire (the 100-byte
   `g_dmac_to_hmac_read_msg` is the **host** destination record; only its first dword is
   meaningful - it is the device address of the MSDU-info-ring pointer table).
3. **`out[1]=0x40` (bit 6) is a D2H "I posted an ETE message" notification, not a request for an
   H2D frame.** The firmware's own D2H send helper (file 0x861E0; invoked indirectly - its
   CPU address 0xC61E1 is the ETE ops-table slot at file 0xCCEEC, so a branch scan finds no
   direct callers) builds the descriptor in the DR ring, fills
   `node.word1 = (len<<16)|0x6D2B`, then calls `d2h_notify(obj, 6)` (notify at file 0x86224) [V]. The host handler
   `pcie_trigger_ete_sending_handle` only wakes the PCIe worker, which drains DR. **The correct
   host action on bit 6 is: ack/drain/re-arm `out[1]`, then drain DR0-3 and decode the HCC
   frame; sending an SR frame back is not part of the protocol.**
4. **The 64-byte zero-header frame the bench sent was structurally invalid and could not have
   been dispatched even though the ETE SR engine consumed its node.** The firmware's SR receive
   drain validates the frame: it requires HCC header `hdr[0xA..0xB] == 0x5A5A`
   (firmware file 0x8699E) and a non-zero total length at `hdr[4..5]` (file 0x869B0); the vendor
   transport itself stamps `0x5A5A` right before the SR fill (`pcie_ete_tx_queue_handle`, plat.ko
   0x15760-0x15774) [V]. A zero header has tag 0 and length 0, so the firmware increments its SR
   error counter and drops the payload. This explains "SR consumer=1 but no g4 ready". [V/I]

**One-line bench answer:** do not send any H2D frame; keep the `0xcece` gate served and `out[1]`
drained; drain DR on bit 6/bit 3 with the B3 instrument and read `b3_p3_hcc` - that is where
chip-group 4 id 1 and id 2 appear.

---

## 1. Q1 - "the first ETE SR announce frame the vendor host sends after release"

### 1.1 The SR announce mechanism (the carrier, not a message)

Every H2D HCC frame uses the same carrier; there is **no special "ETE SR announce" message**. The
single choke point is `pcie_ete_tx_queue_handle` (plat.ko **0x155D4**, 868 B), which for each
queued TX message [V]:

1. takes the ring context from the TX chain (`[ctx+0x38]` pending count, `[ctx+0xC]` producer,
   `[ctx+0x10]` depth source, `[r4+8]` config block);
2. **stamps the transport tag**: `hdr[0xA] = 0x5A`, `hdr[0xB] = 0x5A`
   (`movs r3,#0x5a; strb r3,[r8,#0xa]; strb r3,[r8,#0xb]` @0x15760-0x15774) - this is the
   write site for the `0x5A5A` tag that `wifi-h2d-catalog` had left open ("not written by
   either module" is **wrong for this function**; it is written here) [V];
3. takes the **frame total length from the header** `u16 hdr[4..5]` (not the skb length):
   `ldrh fp,[r8,#4]` @0x15798, bounds-check against the ring max `[r2+6]` @0x157A0-0x157A8 [V];
4. translates the buffer to a device VA (`pcie_get_ete_addr` → `memmap_get_dev_acp_addr`) and calls
   the ETE fill callback through the chip ops table `[chip+0x18]+0x10` = `shuangta_ete_sr_dscr_fill`
   (plat.ko **0x17858**) with `(chip, ring_ctx, buffer_devva, len, present=1)` @0x157B8-0x157C4 [V];
5. `shuangta_ete_sr_dscr_fill` then:
   - `node[idx].word0 = buffer_device_VA` (@0x178CC),
   - `node[idx].word1 = (len_u16 << 16) | 0x4000 | 0x2000 | 0x0D2B` = `(len<<16)|0x6D2B`
     (@0x17898-0x178BC for present=1),
   - advances the packed producer index via `pcie_ete_ring_ptr_plus` (0x13EF8; index
     `(idx&0x3FF)|(phase<<10)`, wrap toggles bit 10) @0x178E4-0x178EC,
   - announces `pcie_msg_send(chip, 3)` (@0x178F4 r1=#3 / @0x178F8 call; the primitive writes
     `out[0]=BIT(3)` then `out[2] |= 1`), and bumps the TX counter `[ring+0x100]` [V];
6. the node length used by the DMA is exactly the header total length (`hdr[4..5]`), so **the
   header length and the node length must agree**.

### 1.2 What the vendor host actually sends, and when

* **Boot (release → ready): NONE.** `wlan_power_on` (plat.ko 0xE46C) downloads the image,
  writes `0x5a5a` to CA 0x40000108, calls `bal_irq_enable` (host-only) and returns;
  `multi_chip_loading` (0xF2AC) only waits (see §2.4). `pcie_msg_init` (0xB6E4) registers
  handlers and sends nothing. This is the `wifi-hcc-link` §0.1/§0.3 negative, re-confirmed
  here from the same relocation-resolved call graph [V].
* **First H2D frame in a normal boot: after DEVICE READY**, in the driver's own init:
  `hmac_main_init → hmac_board_init → hdpp_chip_sync_add → hmac_txq_table_init →
  hmac_txq_table_addr_sync → hdpp_config_send_event` → **HCC group 0, id 1** [V chain;
  I for "first"]. The builder is `hdpp_config_send_event` (wifi.ko **0x59C0**): alloc size =
  caller len + 0x10, `hdr[0]=0` (group 0), `hdr[1] = (desc[+4]<<4)|1`, `hdr[2]=desc[+6]`,
  `hdr[3]=desc[+1]`, `hdr[6..7]=0x0001`, `hdr[8]=0`, and two `u16` header-adjacent fields at
  `hdr+0x14`/`hdr+0x16` plus a payload memcpy at `hdr+0xC` [V builder, payload [U]].
  Its payload is the live TXQ-table sync descriptor and is **not modelled here** - do not
  synthesize it.
* No other init-path HCC send exists in either module version [V, wifi-hcc-link §0.1/§0.3].

**So the exact answer to Q1:** the vendor host sends *no* SR announce frame after release, and
the first H2D frame it ever sends is the post-ready group-0 id-1 TXQ config sync (builder
`hdpp_config_send_event`, wifi.ko 0x59C0), whose payload is dynamic and only valid under the
vendor's hmac/hdpp init state. There is nothing to copy for the ready gate.

### 1.3 The 12-byte HCC header the carrier transports (contract, re-verified)

| off | width | field | value in vendor H2D |
| ---: | ---: | --- | --- |
| +0x00 | u8 | group = low nibble | builder |
| +0x01 | u8 | low nibble = alloc state (1); high nibble = **src/dst resource** (copied to +9) | `hcc_msg_alloc` sets low=1; `hcc_msg_tx` copies high→+9 |
| +0x02 | u8 | field A | builder |
| +0x03 | u8 | field B (user/vap/...) | builder |
| +0x04 | u16 | **total length = payload + 12** | `hcc_msg_alloc` (plat 0x11D9C) |
| +0x06 | u16 | **message id** (dispatch key in group) | builder |
| +0x08 | u8 | field C | builder |
| +0x09 | u8 | resource/queue selector = +1 high nibble | `hcc_msg_tx` (0x11BF8) |
| +0x0A | u16 | **transport tag 0x5A5A** | `pcie_ete_tx_queue_handle` 0x15760 (host side) |
| +0x0C | ... | payload | builder |

---

## 2. Q2 - the ready-path frames (HCC chip-group 4, ids 1 and 2) - DEVICE→HOST

### 2.1 Direction and host handlers

The vendor host registers a **receive** table: `plat_init_bal_hcc_excp` (plat.ko 0xE380) does
`hcc_msg_register_tab_chip(group=4, table=.data+0x2660, count=5)` (call @0xE3B8) [V]; ids 1 and 2
are `device_plat_ready_msg_process` (0xEB78) and `host_ready_msg_process` (0xEBD8). Both
handlers read `*(msg+0x118)` = header pointer and only ever call `complete()`/printk/memcpy;
neither sends anything [V].

### 2.2 id1 - "Device plat ready!" (wire form, [V])

Built by the **firmware boot task at file 0x7D28-0x7D64** (CPU 0x47D28):

    movs r1,#6 ; movs r0,#0 ; bl hcc_msg_alloc      @file 0x7D28  (size 0 -> total len 0x0C)
    bl hcc_msg_get_hdr                               @file 0x7D38
    movs r3,#4 ; strb r3,[hdr]                       @file 0x7D3C  -> group = 4
    ldrb r3,[hdr,#1] ; bfc r3,#4,#4 ; strb r3,[hdr,#1] @0x7D40-0x7D48 -> src core 0
    movs r2,#1 ; strb r2,[hdr,#6]                    @file 0x7D4C  -> message id = 1
    movs r3,#0 ; strb r3,[hdr,#3] ; strb r3,[hdr,#7] @file 0x7D4E-0x7D50
    bl hcc_msg_tx                                    @file 0x7D54
    bl 0x102820 (msg-free wrapper -> 0x1024E6)         @file 0x7D60

Wire bytes (host-visible payload; uninitialised header bytes marked `??`):

    04 00 ?? 00 0c 00 01 00 ?? 00 ?? ??      group=4 id=1 total_len=0x0C (payload 0)

`hcc_msg_tx` copies `hdr[1]>>4` to `hdr[9]` (=0 here); the host handler requires
`hdr[1]>>4 == 0` and reads only `hdr[1]`, `u16 hdr[4..5]`, `u16 hdr[6..7]`, `hdr[9]` [V].

### 2.3 id2 - "DEVICE READY" (wire form, [V])

Built by the **firmware boot task at file 0x8FEC-0x9166** (CPU 0x48FEC), first-boot state
`r7 = global[+0x110] == 0`:

    movs r0,#4 ; movs r1,#6 ; bl hcc_msg_alloc      @file 0x9004-0x9008 (size 4 -> total 0x10)
    ldr r3,=0x01060850 ; str r3,[sp,#0x54]          @file 0x9000/0x9006 (payload word)
    bl hcc_msg_get_hdr                               @file 0x90E0
    ldrb r3,[hdr,#1] ; bfi r3,r7,#4,#4 ; strb r3,[hdr,#1] @0x90E4-0x90EC -> src core 0
    movs r5,#4 ; strb r5,[hdr]                       @file 0x90F0  -> group = 4
    movs r3,#2 ; strb r3,[hdr,#6]                    @file 0x90F2  -> message id = 2
    ... memcpy_s(hdr+0xC, 4, &payload, 4)            @file 0x9106
    bl hcc_msg_tx                                    @file 0x9162

Wire bytes:

    04 00 ?? 00 10 00 02 00 ?? 00 ?? ?? 50 08 06 01

    group=4 id=2 total_len=0x10 payload_len=4 payload = 50 08 06 01 (LE dword 0x01060850)

The 4-byte payload is a **device-RAM address** (CPU 0x1060850 = the read/MSDU-info-ring
pointer-table handle in this image) [V: literal at file 0x9150; the host consumes it as
`*(u32*)(g_dmac_to_hmac_read_msg[core]) = payload[0..3]`]. It is **not** a copied 100-byte
struct on the wire.

### 2.4 Host-side handling: the 100-byte `g_dmac_to_hmac_read_msg` and the two waits

* **id1 handler** `device_plat_ready_msg_process` (0xEB78): checks `hdr`, `src core = hdr[1]>>4`
  (must be 0), logs `"Device plat ready! chip id : %d"`, then `complete(LANCHOR0+0x234)` @0xEBCC [V].
* **id2 handler** `host_ready_msg_process` (0xEBD8): checks `hdr`, `hdr[1]&0xF0 == 0`,
  `payload = u16 hdr[4..5] − 0xC ≤ 0x63` (else `"invalid l_msg_len"`), `core = hdr[9]`, and copies
  the payload to the per-core record `g_dmac_to_hmac_read_msg[core]` =
  `LANCHOR0 + 0x1A8 + 0x64*core` (stride 0x64 = 100 B) via `memcpy_s` @0xEC7C; then sets the
  per-core ready flag, ORs core bit into the device-ready bitmap at `LANCHOR0+0x21C` and only
  when the bitmap equals 1 calls `complete(LANCHOR0+0x220)` @0xED24 [V].
* **The 100-byte record's first dword is the only modelled field**: wifi.ko
  `hmac_tx_msdu_info_ring_table_addr_init` (wifi.ko 0x184F0) calls the plat export
  `wlan_get_d2h_read_msg(core)` and does `base = *(u32*)record`, then
  `memmap_get_dev_acp_addr(base)` and `shuangta_set_msdu_info_ring_ptr_table_base(...)`
  (@wifi 0x18510-0x18610). The remaining 96 bytes are not read by any caller found in
  `wifi.ko` (only two call sites, both this same accessor) [V]. For this firmware build only 4
  of the 100 bytes are ever sent, so the rest stays as-is in host RAM [V/I].
* **Wait order/timing** (`multi_chip_loading`, plat.ko **0xF2AC**) [V]:
  1. `wait_for_completion_timeout(LANCHOR0+0x234, 0xC8)` = **200 jiffies = 2.0 s** @0xF324-0xF32C
     (released by g4 id1);
  2. on success mark bit0 in `LANCHOR0+0x218`, `dev_status_check()`;
  3. `wait_for_completion_timeout(LANCHOR0+0x220, 0x7D0)` = **2000 jiffies = 20.0 s** @0xF37C-0xF384
     (released by g4 id2 when the ready bitmap == 1);
  4. on timeout: `"device_ready timeout"` error path (no retry).
  **There are no retries and no host reply** in either step [V].

### 2.5 Which channel carries them, and the host's required receive work

* Prediction from the ETE channel table: **DR0** (the only DR channel whose config flags word is 1;
  hcc-re §4.1) [I]. B3-PLAN P2 decides it on the bench.
* The host must arm SR0-2/DR0-3 and pre-post DR free buffers **before** release (vendor does this
  at probe): DR base `+0x30`, depth−1 `+0x34`, published free index `+0x38`; the node array's
  `word0` must hold each payload buffer's **device VA** and the published value is the packed
  index `(idx&0x3FF)|(phase<<10)` (vendor init writes `DR+0x38 = ctx+0x1C` = 0 at probe;
  B3 publishes `0x400` for 32 buffers - a bench knob, see B3-PLAN §11) [V vendor,
  I convention].
* The firmware's D2H send helper fills `node.word0 = buffer device VA`,
  `node.word1 = (len<<16)|0x6D2B` and advances the fill index, then `d2h_notify(id 6)` [V].
  Host drain: read DR `+0x3C` (device fill index), for each completed node take
  `len = word1>>16`, read the payload at `word0` (translate device VA back to the host buffer),
  decode it as an HCC frame, then return the buffer by advancing `DR+0x38` with the packed index.

---

## 3. Q3 - firmware cross-reference (H2D receive side, ack/gate requirements)

### 3.1 H2D receive tables: group 0 and group 2 only - never group 4 H2D

From the committed, byte-verified `wifi-h2d-catalog`: the firmware's HCC registrar is CA 0x102866
(file 0xC2866) and registers exactly **group 2** (19 entries, CA 0x10CC18), **group 0** (40 entries,
CA 0x10CCFC) and one early **group 0 id 0x2F** (CA 0x10CBEC) [V]. There is **no group-1 and no
group-4 receive table**. Sending chip-group-4 frames H2D would hit no handler.

The mailbox layer is separate: dispatcher file 0x818AC, table CA 0x118D68, ids **1/3/5/6**
(5 = `BUG()` trap: bytes `00 23 1b 60 ff de`); the SR-post announce is always mailbox id **3** [V].

### 3.2 What a valid H2D HCC frame must satisfy (firmware-side checks, [V])

In the firmware's SR-receive drain (function around file 0x866xx-0x869xx):

* header tag `u16 hdr[0xA..0xB] == 0x5A5A` (file **0x8699E**: `movw r0,#0x5a5a; ldrh r1,[r3,#0xa];
  cmp; beq`), otherwise the SR error counter at `[ctx+0xC]` is incremented and the frame is
  skipped (file 0x869A8-0x869AE) [V];
* total length `u16 hdr[4..5]` must be non-zero (file **0x869B0**: `ldrh r1,[r3,#4]; cbz` →
  error path), and the HCC header fetch at CA 0x409EA additionally validates the header class
  field `hdr[5]>>1&7 <= 3` and message-pool membership per `wifi-h2d-catalog` §4.4 [V];
* after dispatch the firmware raises its own D2H notifications (e.g. `d2h_notify(id 5)` at file
  0x86998 on the receive path) - these are device→host, no host reply [V].

**Consequence for the bench:** the previously-sent "64-byte zero-header SR0 frame" failed both
checks (tag 0, total length 0). The ETE engine still consumed the node (SR consumer++ proves the
SR channel enable/engine is alive) but the firmware dropped the payload - which is exactly the
observed "consumer=1, no g4 ready" [V/I].

### 3.3 The `0xcece` gate and `out[1]=0x4` announce (firmware file 0x86E14)

Re-verified instruction bytes [V]:

    file 0x86E76-0x86E80: ldr r2,=0x4003022C ; str 0 ; ldr r2,=0x4000010C
                          strh 0,[0x4000010C]        ; gate zeroed (16-bit)
                          strh 0,[0x40000108]        ; release register zeroed too
    file 0x86F54-0x86F66: out[1] (CA 0x40039014) = 4 ; D2H doorbell CA 0x40101434 = 1
    file 0x86F74-0x86F7C: movw r3,#0xcece ; ldr r2,[CA 0x4000010C] ; cmp r2,r3 ; bne spin

Facts that constrain the host:

* The routine **zeroes the gate and then spins on a 32-bit read == 0x0000CECE**. A halfword write
  is not enough in general (upper half is not cleared by the firmware) - **write the full 32-bit
  `0x0000CECE`** [V].
* The routine runs per vector entry (entry 3 = SVC, entry 1 = data abort, ...); every entry
  re-zeroes the gate. A single post-release write is destroyed when the routine starts
  (bench-proven: `wifi-boot-kick` §18-19) [V]. **Hold the gate live** (sustained write) until the
  boot has advanced (mask bits/state change).
* `out[1]=0x4` is written **directly by this routine** (not a mailbox pending bitmap). It needs
  the normal D2H ack/drain/re-arm service to stay clear: `out[3]=1`, read+write 0 `out[1]`,
  `out[4]=1` [V]. Never write CA 0x400392F0 (out[5]).
* The firmware itself writes CA 0x40000108=0 (release reg) when the routine runs; that is expected
  and is not a host action [V].

---

## 4. Q4 - ready-to-use bench sequence (B3 instrument, no H2D frames)

Preconditions are B3-PLAN §1 (G1 recovery armed, G2 link, G3 UART read-only, G4 image+module,
G5 no vendor modules, G6 firmware blob, G7 dma-ranges image, G8 only this module) and the hard
rules of B3-PLAN §2. **No new register class is written by this sequence.**

| # | action (sysfs / loop) | expected observable | stop condition |
| ---: | --- | --- | --- |
| 0 | `insmod hi5622_pciet_b3.ko poll_ms=0` (record module sha256) | 2 probe lines, 7 ring allocations, no "-EBUSY"; no `D2H poll thread started` | any oops/abort |
| 1 | `echo 1 > prepare` | 6× `iatu vpN … readback=OK`; `windows=1` | mismatch → **STOP** (no release) |
| 2 | `echo 1 > upload` ; `echo 1 > verify` | `928920 bytes`; `diffs=0 match=YES` | diffs≠0 → **STOP** |
| 3 | `echo 1 > b3_p0_ringinit` | 7/7 readback OK; every SR/DR base in `0x80000000..0x8fffffff`; SR0-2 prod=0 ctrl=0; DR0-3 `pub(+0x38)=0x00000400`, `fill(+0x3c)` baselined | MISMATCH/out-of-window → **STOP** |
| 4 | start the **sustained gate service** before release: write `0x0000CECE` → BAR0 **0x3B810C** (tighter than 100 µs if possible), keep running | gate reads `0x0000CECE` while the firmware spins | abort/SError |
| 5 | `echo 1 > release` | CPU release line, `cpu_released=1` | abort/SError |
| 6 | start D2H service: `echo 60 > b3_p1_watch` (blocks; do not interrupt). Optionally `echo 1 > b3_drain` first | dmesg `B3 D2H[...]: out[1]=0x…`; bit2 (`0x4`) announce first; then bit6 (`0x40`) = D2H message staged; bit3 (`0x8`) = ETE done | no bit3/bit6 after 2 windows AND all DR `fill=0` → **STOP** (publish side not reached) |
| 7 | on every bit3/bit6 the instrument auto-drains DR0-3: `b3_p2_result` | `B3 DRn: fill=… last=… -> N completed node(s)`; `B3 RX drN[k]: node len=… hdr{g=4 rg=0 total=12/16 id=1/2 …}` | `implausible completion count` → **hard STOP** |
| 8 | read `cat b3_p3_hcc` | `g4_id1_seen=1` then `g4_id2_seen=1`; id2 `payload=4 B (<=0x63 OK)`; then `NO H2D reply` | id1 without id2 → record, **STOP** (no writes) |
| 9 | `cat b3_p1_events b3_p2_result b3_p4_state b3_rings status`; capture dmesg/UART; power-cycle | out[1] bits histogram (bit2/bit3/bit6), DR fill/pub trajectory, used channel `+0x00=1`, mask/state | — |

**Frame table for this sequence:** steps 0-9 send **no H2D frame**. The expected frames are all
device→host; the only bytes to compare are the decoded headers in step 8:

| frame | direction | wire bytes / fields | host action |
| --- | --- | --- | --- |
| HCC g4 id1 | device→host (DR) | `04 00 ?? 00 0c 00 01 00 ?? 00 ?? ??` | `device_plat_ready_msg_process`: complete wait #1 (2.0 s bound in vendor) |
| HCC g4 id2 | device→host (DR) | `04 00 ?? 00 10 00 02 00 ?? 00 ?? ?? 50 08 06 01` | `host_ready_msg_process`: copy 4 B payload to `g_dmac_to_hmac_read_msg[0]`, complete wait #2 (20.0 s bound in vendor) |
| mailbox id2 | device→host | `out[1] = 0x4` (literal write by 0x86E14) | ack/drain/re-arm only |
| mailbox id6 | device→host | `out[1] = 0x40` | ack/drain/re-arm, then **drain DR0-3** (do not send) |
| mailbox id3 | device→host | `out[1] = 0x8` | ack/drain/re-arm, then drain DR0-3 |

**If (and only if) an H2D consumer-liveness probe is still wanted** (separate idea/session; the
H2D path is not needed for ready):

* Build the combined B3 + injector module (`../wifi-h2d-inject/tools/build.sh --module-only
  --tree b3`), load with `poll_ms=0`; run P0, then `echo 1 > h2d_arm` **before** release
  (single-shot; it re-programs SR0 with the injector's node ring - documented caveat).
* Send exactly one frame, using the **injector** (never the base transport `send`: it writes a
  zero header and the firmware drops it, §3.2):

      echo 'group=0 id=0 dst=0' > h2d_inject
      # identical raw: 000100000c00000000005a5a
      # header: group=0, alloc/dst nibble=0x01, len=0x000C, id=0, tag=0x5A5A at +0xA

  The firmware group-0 id-0 handler (file 0x41695) is a payload-free print-and-return stub
  (`wifi-h2d-catalog` §4.3) [V]. Expected: `out[0]` drains (device clears it), device ack
  `out[5]` reads 1 (read-only checkpoint), SR0 consumer advances by 1, no abort/mask change;
  `h2d_stats` shows the announce/drain read-backs. One injection per session; never post
  mailbox id 5; never write out[5]; never use group 4 H2D (no firmware handler).

**Frames that are WRONG and must not be sent:** the base transport `send` 64-byte zero-header
frame (invalid tag/length, §3.2); any group-4 frame H2D (no receive table); mailbox id 5
(firmware BUG trap); anything from the hmac/hdpp config catalog without its vendor payload
struct (several firmware handlers dereference payload words as pointers).

---

## 5. Confidence summary

| claim | evidence | grade |
| --- | --- | --- |
| No H2D frame in the release→ready window | relocation call graph: `multi_chip_loading`/wlan_power_on/pcie_msg_init send nothing; only init-path sender is post-ready hdpp g0 id1 | [V] |
| First H2D after ready = g0 id1 `hdpp_config_send_event` (TXQ sync) | direct call chain hmac_main_init→hmac_board_init→hdpp_chip_sync_add→hmac_txq_table_init→hmac_txq_table_addr_sync→hdpp_config_send_event; builder disassembly | [V] chain, [I] "first", payload [U] |
| g4 id1 wire bytes | firmware file 0x7D28-0x7D64 + hcc_msg_alloc len setter (file 0xC285A-0xC2862) | [V] |
| g4 id2 wire bytes (payload = 4-byte device address) | firmware file 0x8FEC-0x9166; literal file 0x9150 = 0x01060850; host consumer wifi 0x184F0-0x18610 | [V] |
| 100-byte `g_dmac_to_hmac_read_msg` = host record, only dword0 used | plat 0xEBD8 handler, wifi 0x184F0/0x18510 accessor; 2 call sites only | [V] |
| Waits 200 / 2000 jiffies, no retries, no reply | plat `multi_chip_loading` 0xF2AC (0xC8/0x7D0 immediates) | [V] |
| H2D frames are validated by tag 0x5A5A and non-zero length | firmware file 0x8699E / 0x869B0; tag stamped by plat 0x15760 | [V] |
| H2D receive tables are group 0/2 only (no group 4) | `wifi-h2d-catalog` byte-verified registrar/tables | [V] |
| `0xcece` = 32-bit gate spin, zeroed per vector entry; hold live | firmware file 0x86E74-0x86F7C; `wifi-boot-kick` §18-19 | [V] |
| Which DR channel carries g4 id1/id2 | prediction DR0 (flags word=1) | [I] - B3 P2 decides on the bench |
| `out[1]=0x40` = D2H message staged (not an H2D request) | firmware 0x861E0 helper (ops slot file 0xCCEEC = 0xC61E1): fills DR node, `d2h_notify(id 6)`; host handler = wake worker | [V] |

**Remaining [U]:** the q4 id2 alternate-state builder (file 0x9050 path, taken when the global
config byte `[+0x110] != 0`) was not decoded; the exact DR payload buffer convention the
firmware expects (B3 publishes `0x400` - B3-PLAN §11 fallback is the knob); the dynamic payload
of the first post-ready g0 id1 TXQ sync; which vendor peer writes `0xcece`.

---

## 6. Evidence index / reproduction

| item | file offset / address | how verified |
| --- | --- | --- |
| H2D transport choke point + tag stamp | plat.ko `pcie_ete_tx_queue_handle` @0x155D4, tag @0x15760-0x15774, len @0x15798, fill @0x157B8 | fresh disasm (capstone 5.0.7, ARM) |
| SR node fill + announce id3 | plat.ko `shuangta_ete_sr_dscr_fill` @0x17858; word1 @0x17898-0x178BC; announce @0x178F4/0x178F8 | fresh disasm |
| packed index advance | plat.ko `pcie_ete_ring_ptr_plus` @0x13EF8 | fresh disasm |
| id1 builder | FIRMWARE.bin file 0x7D28-0x7D64 (CPU 0x47D28) | byte dump + disasm |
| id2 builder + payload literal | FIRMWARE.bin file 0x8FEC-0x9166, literal file 0x9150 | byte dump + disasm |
| firmware HCC alloc len field | FIRMWARE.bin file 0xC2822-0xC2864 (len store @0xC285A-0xC2862) | disasm |
| firmware D2H post helper (ops-table slot) | FIRMWARE.bin file 0x861E0-0x86234; pointer 0x000C61E1 at file 0xCCEEC | disasm + literal search |
| firmware ready/vector routine (gate/announce/spin) | FIRMWARE.bin file 0x86E14-0x86F80 | disasm |
| firmware SR receive tag/len checks | FIRMWARE.bin file 0x86998-0x869B4 | disasm |
| host ready handlers + waits | plat.ko 0xEB78 / 0xEBD8 / 0xF2AC | fresh disasm |
| host read-msg consumer | wifi.ko 0x184F0-0x18610; plat export `wlan_get_d2h_read_msg` @0xE76C | fresh disasm + callers |
| B3 DR walk / HCC decode / drain | `../wifi-transport-port/b3/pciet_b3.c` `b3_arm_dr`/`b3_walk_dr`/`b3_hcc_rx`/`b3_d2h_service` | source read |

Host-only reproduction (all local artifacts, nothing vendor is committed):

    PY=<repo>/.provision-venv/bin/python
    # this lane's tools (read the local modules/blob; print derived annotations only)
    $PY tools/verify-frames.py            # asserts the firmware/plat bytes quoted above, prints the two D2H frames
    $PY tools/fw-scan.py 0xC61E0 0xC6170  # CPU-address branch scanner used to locate builders/notifiers
    $PY tools/mk-h2d-frame.py             # emit/validate bench H2D frames (tag/len/dst rules; id5 refused)
    # full-width disassembly used for the quotes:
    $PY ../wifi-h2d-inject/tools/build.sh --selftest-only   # unrelated sanity of the frame model used on the bench
    $PY ../wifi-rom-flow/tools/fwdis.py 0x86E60 0x86F90   # if present; otherwise use any capstone Thumb dump

Cross-references (committed, not re-derived): `wifi-hcc-re` §3.1/§3.3/§4.2/§5.4 (HCC header, SR
node, ETE registers, D2H service order), `wifi-h2d-catalog` §3/§4 (firmware mailbox + HCC receive
tables), `wifi-hcc-link` §0 (no H2D link/event message; only init sender = post-ready g0 id1),
`wifi-boot-kick` §18-21 (gate/publish race, C1e), `wifi-transport-port/b3/B3-PLAN.md` (probe
procedure and stop rules).
