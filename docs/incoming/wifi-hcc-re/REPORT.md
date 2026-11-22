# HCC post-transport-boot contract — Hi5622V100 (WR3000 v2.0), host-only reversal

**Lane:** hcc-re (task-63), host-only. **Written:** 2026-11-22. **Status:** static reversal complete
for the requested contract items; no device contact, no vendor binary/blob committed. Companion:
`SUMMARY.md`. This is the contract for everything **after** the transport stages that already pass
on our 6.18 image (parent ledger 2026-11-22 18:20 B1, 18:35 B2).

**Builds on (does not redo):** `wifi-driver-re/REPORT.md` §3–4, `wifi-firmware-re/REPORT.md`
§2–3, `incoming/wifi-transport-port/REPORT.md` + `BENCH-PLAN.md`, `phase37/THE-PROTOCOL-MAP.md`,
`phase42/word-semantics.md` + `ete-registers-reconciled.md`, `phase43/device-dr-side.md`,
`incoming/wifi-datapath-slice/`, `wifi-algcfg-slice/`, `wifi-wal-glue/`. Fresh work this lane:
re-disassembly of the exact local 2.4.15 modules to re-verify the D2H/H2D catalogues, HCC
format/queue routing, ETE channel table and init order, the ready-completion handshake, and the
hdpp/alg attachment tables.

**Evidence markers:** **[V]** verified this lane from a named local artifact (fresh disassembly of
the files below); **[V\*]** device-measured by an earlier attended lane (cited doc); **[I]**
inference; **[U]** unknown.

| artifact | md5 / sha256 | note |
| --- | --- | --- |
| `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko` | `f688618b4daa52473be41577f992f7d4` | pcie/bal/hcc/ete/plat layer |
| `/home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_wifi.ko` | `1745ef77e728c411cef1921af3dc8731` | hal/hmac/wal/hdpp/alg layer |
| `/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin` | sha256 `7fc87e20…ef311b`, 928,920 B | firmware; file offset, runtime = file + 0x40000 |

Conventions: `.ko` offsets are **section-relative vaddrs** (`nm`/`readelf` values;
`.text` `sh_addr=0`). Firmware = **file** offsets, Thumb. Device CA ↔ host BAR0:
**BAR0 = 0x3b8000 + (CA − 0x40000000)** (region 3) [V].

> Module identity: phase37/42 quoted plat.ko `23660bc2…` / wifi.ko `4737fcb2…`; the files above
> are the 2.4.15 set, different md5 but the **same code layout** — every address here reproduces on
> them and every table below was re-decoded from them.

---

## 0. TL;DR — the contract after "DEVICE READY"

1. **The `out[1]=0x4` already measured is a platform-up announce, not the ready handshake.**
   Firmware's ETE bring-up writes `out[1]=4` and then spins on the host magic `0xcece`; the vendor
   host PCIe layer has **no handler for mailbox id 2** (dropped). The real handshake is two **HCC
   payload messages** — chip group 4 ids **1** and **2** — which arrive over the ETE **DR ring** and
   complete the vendor's 200/2000-jiffy init waits. **No H2D reply to either.**
2. Host work after `out[1]=0x4`: **arm ETE rings (SR0–2 + DR0–3), pre-post DR free buffers, keep
   `out[1]` drained**, then service D2H and dispatch HCC messages. Nothing is sent to make ready
   happen.
3. **H2D message = one SR node + one mailbox announce id 3.** Node = 8 B
   `{word0 = buffer device VA, word1 = (len<<16)|0x6d2b}`; buffer = **12-byte HCC header +
   payload**; `len = payload + 12`; producer index = `(idx&0x3ff) | phase<<10`. Announce = write
   pending bitmap to `out[0]` (CA 0x40039010) and OR bit0 into doorbell `out[2]` (CA 0x400392d4)
   — vendor `pcie_msg_send(chip, 3)`.
4. **H2D ids ≠ message ids**: the HCC message id is u16 at header `+6`; the mailbox id is always 3
   for data. Only two H2D sends exist in the vendor boot path: id 3 (SR announce) and id 5
   (receive-buffer check on a failure branch — the firmware id-5 handler is a `BUG()` trap;
   **never post 5 on the success path**).
5. **D2H mailbox dispatch**: lowest set bit of `out[1]` (ids 0..10), ack `out[3]=1`, drain
   `out[1]=0`, re-arm `out[4]=1`, then call the 8-byte table entry. Vendor registers only ids
   **{1,3,6,7}**; **2,4,5,8,9,10 are dropped**. HCC handoff only via DR payloads (id 3 = ETE done ⇒
   drain DR; id 6/7 = data staged ⇒ wake/inspect DR).
6. **HCC tables to reproduce** (§5.5, all re-decoded): plat boot group 4 (ids 0–4); hdpp chip group 3
   (0,4,5,7,8) + core group 0 (0,1,2,3,4,7,8); hmac chip groups 1 (28 ids), 2 (12), 3 (8); wal chip
   group 2 (19).
7. **ETE**: SR0–2 blocks `+0x400/+0x450/+0x4a0`, DR0–3 `+0x590/+0x5e0/+0x630/+0x680` (block CA
   0x4003a000). SR host writes `+0x10 base, +0x14 depth−1, +0x18 producer, +0x08 ctrl`; DR host
   writes `+0x30 base, +0x34 depth−1, +0x38 producer(free buffers)`. Channel `+0x00 enable` is
   set **by firmware**, not host. Depth 32 (`depth−1=0x1f`). Intr/mask block CA 0x40039508.
8. **hdpp/alg attach**: register hcc queue customers for **chip queue 2** and **core queue 0**;
   register the D2H tables; implement `hmac_event_config_syn` (chip group1 id3) as the
   config/alg/cali response dispatcher; init order `hdpp_chip_init → hdpp_var_init →
   hdpp_hisi_thread_init → register → user/vap`.
9. **Post-ready init**: `multi_chip_loading` waits; then `hal_main_init → hmac_main_init`
   (tables, board/fsm init, cali file save, hcc state←4) `→ wal_main_init` (tables, cfg80211,
   state←5). The cali "to device" log is from the **file-save verify functions**, not a message.
10. **Next bench steps** §9 (B3 probes P0–P5; never write `0x400392f0`).

---

## 1. Layering and the exact points where the transport stops

[V] from current modules:

    plat.ko init_module (0x1a75c)
      plat_res_init, oal_main_init, sdt_drv_main_init, low_power_init, plat_hcc_init (0x1a690)
        plat_custom_init, plat_main_init, pcie_init_static_res, plat_debug_sysfs_init, bal_init,
        hcc_init (0x10df8), plat_exception_init
      pcie_main_init (unlikely .text 0x704) at PCI probe:
        pcie_init_default, pcie_bus_getnum, __pci_register_driver, get_pcie_res, pci_dev_res_init,
        oal_pcie_enable_remap, pcie_ete_init (0x7820), pcie_comm_init (0x171cc)
          pcie_ete_init: pcie_ete_intr_init (0x7528), pcie_ete_rings_init (0x7680), pcie_ete_chn_res
          pcie_comm_init: pcie_thread_init; tail-call pcie_msg_init (0xb6e4)  [arms, sends nothing]
      hcc_init: memset, hcc_get_group_type, hcc_queue_module_init, hcc_msg_module_init,
                hcc_thread_init, hcc_timer_module_init, bal_register_hcc_callbacks (0x1101c),
                bal_port_init
      hcc_start (0x11184): thread+timer module init, then hcc state ← 1
    wifi.ko host_module_init (0x3e154; called from init_module 0x3e220)
      oal_net_dev_res_init, oam_wifi_init, multi_chip_loading (imported from plat.ko),
      hal_main_init (0x31d98), hmac_main_init (0x3f6b8), wal_main_init (0x12386c)

HCC **state byte** (single byte via `hcc_get_instance`) [V]: only in-plat writer is `hcc_start`
(←1); wifi.ko sets **4** (hmac_main_init 0x3f854) and **5** (wal_main_init 0x12395c); exits set 6.
Gates: `hmac_main_init` bails when state ∈ {0,6}; `wal_main_init` bails when state ≤3 or ==6.
Boot order: 1 → 4 → 5.

B1+B2 already proved [V\*]: six viewports; 928,920 B upload diffs=0; release 0x5a5a; gate 0xcece;
polled D2H bitmap `0x4`; SR0 block +0x400 base 0x8360c000 depth−1 0x1f; DR0 block +0x590 base
0x83634000 depth−1 0x1f; 64 B H2D smoke on SR0 with doorbell mask 8, no abort.

---

## 2. The post-DEVICE-READY handshake

### 2.1 Three distinct event channels (do not conflate)

| channel | content | evidence |
| --- | --- | --- |
| **mailbox words** (`out[0]/out[2]` H2D, `out[1]` + doorbell 0x40101434 D2H) | 11-bit bitmaps of **PCIe message ids 0..10**, no payload | [V] §5.1/§5.4 |
| **ETE rings** (SR host→device, DR device→host; 8-B nodes + payload buffers in host DRAM) | the **HCC frames** (12-B header + payload) | [V] §3/§4 |
| **HCC tables** (host RAM) | group/id → handler | [V] §5.5 |

### 2.2 Device side: ready announce and the `0xcece` gate (firmware, [V])

Ready routine file 0x86e14 (Thumb) tail, re-disassembled:

    0x86f32 ldr r0,[pc,#0x80]   ; 0x40039000 (glue block)
    0x86f3e bl  #0x7c0 ; 0x86f44 bl #0x78e        ; helper pair on 0x40039000
    0x86f48 ldr r0,[pc,#0x6c]   ; 0x40039800 (the "twin"/ack block)
    0x86f4a bl  #0x7c0 ; 0x86f50 bl #0x78e
    0x86f54 movs r2,#4
    0x86f56 ldr  r3,[pc,#0x64]  ; 0x40039014 = out[1]
    0x86f5a str  r2,[r3]        ; *out[1] = 0x4   <-- the word B1 measured
    0x86f66 str  r2,[r3+0xc8420]; *0x40101434 = 1 (D2H doorbell)
    0x86f74 movw r3,#0xcece
    0x86f78 ldr  r2,[r1]        ; r1 = 0x4000010c (literal file 0x86fa0)
    0x86f7a cmp  r2,r3
    0x86f7c bne  #0x86f78       ; SPIN until host writes 0xcece

So **bit 2 = "platform ETE up"**, and the device then blocks on the host magic `0xcece` @ CA
0x4000010c before publishing further D2H words. [V] instructions. The peer that writes `0xcece`
is **[U]** — no vendor module writes it or CA 0x4000010c (this lane's u32 scan over all 60+ vendor
`.ko`; phase43 reached the same negative). Our B1 writes it by hand and the device proceeds
[V\*], so **keep the B1.5 announce step** until the peer is identified.

### 2.3 Host side: bit 2 has no handler — it obliges nothing but a drain

`pcie_msg_init` (0xb6e4) registers exactly four D2H ids [V] (call sites 0xb818–0xb8b8):

| D2H mailbox id | handler | body |
| ---: | --- | --- |
| 1 | `pcie_dev_ready_msg_handle` 0x8784 | **4-byte `bx lr`** empty stub |
| 3 | `pcie_ete_transfer_done_handle` 0x8730 | `b pcie_ete_d2h_isr_handle(chip,0,0x1f)` ⇒ drain DR |
| 6 | `pcie_trigger_ete_sending_handle` 0x15efc | `b pcie_wkup_thread` ⇒ wake worker |
| 7 | same as 6 | wake worker |

`pcie_msg_handle` drops any other set bit through the "no handler registered" path (0x1739c).
None of these handlers writes the device (call lists: wake/complete/printk only). **Bit 2 therefore
only needs draining** (`out[1]=0`, `out[3]=1`, `out[4]=1`; §5.4) so the device's P2 wait clears.

### 2.4 The real handshake: HCC chip group 4 ids 1 and 2 ([V])

The vendor's waits live in `multi_chip_loading` (plat 0xf2ac), which **wifi.ko's
`host_module_init` calls directly** (imported symbol, call 0x3e168) [V]:

    completion #1 at plat_ctx+0x234: wait_for_completion_timeout(..., 0xc8 = 200 jiffies)   0xf324
      success: plat_ctx+0x218 |= 1; dev_status_check()                                       0xf358
    completion #2 at plat_ctx+0x220: wait_for_completion_timeout(..., 0x7d0 = 2000 jiffies)  0xf37c
      success: dev_status_check()                                                            0xf390

Completers: `plat_init_bal_hcc_excp` (0xe380) registers **chip group 4, table `.data+0x2660`,
count 5** (`hcc_msg_register_tab_chip` call 0xe3b8) [V]. Table re-decoded from current plat.ko:

| g4 id | handler | semantics | reply |
| ---: | --- | --- | --- |
| 0 | `oam_rx_post_action_function` 0xdabc | device log blob to SDT/netlink | none |
| **1** | `device_plat_ready_msg_process` 0xeb78 | src-core check (`hdr[1]>>4==0`), log "Device plat ready!", **complete() = wait #1** | **none** |
| **2** | `host_ready_msg_process` 0xebd8 | payload `len=u16@hdr+4`; copies `len−0xc` (≤0x63) into per-core 0x64 record at `plat_ctx+0x1a8+0x64*core`; sets ready mask; **complete() = wait #2** when core mask==1 | **none** |
| 3 | `exception_info_msg_process` 0x18c0 | copy 0x30-byte exception body | none |
| 4 | `heartbeat_msg_process` 0x10214 | `hrtimer_start_range_ns` heartbeat restart | none |

Both handlers receive the **HCC message object** in r0 and read `*(msg+0x118)` = header pointer;
their call lists contain only complete/memcpy/spinlock/dmb-dsb-sev. **DEVICE READY is completed by
the device posting HCC group-4 id 1 then id 2 over a DR ring; no host message is required.**

> **Correction to BENCH-PLAN (important for the driver):** BENCH-PLAN equates mailbox bitmap
> `0x2/0x4` with ready ids 1/2. Those bits are mailbox ids; id 2 is unregistered/dropped. The HCC
> ids 1/2 are a different id space (header group/id) and are not observable as bitmap bits. On the
> bench "ready2=1 + bitmap 0x4" proves the platform-up announce, **not** that the vendor's
> 2000-jiffy wait was released. The real gate is receiving group-4 id 1/2 payloads from DR.

### 2.5 What must be armed for those payloads to arrive

The vendor arms the ETE at **PCI probe** (`pcie_ete_init` inside `pcie_main_init`, before the
firmware is uploaded): `pcie_ete_intr_init → pcie_ete_rings_init → pcie_ete_chn_res`;
`rings_init` allocates 3 SR + 4 DR software contexts, maps the ETE block and calls
`pcie_ete_init_src_ring` (0x7068) / `pcie_ete_init_dst_ring` (0x727c); each channel's
`sr_init`/`dr_init` programs base/depth/index and SR ctrl [V]. Initial DR free buffers are placed
at probe by the vendor (dr_dscr_fill writes node word0; `pcie_ete_sending_trigger` publishes the
host index — 0x14430 `str r2,[r3,#0x38]`) [I/V]. **Our B2 armed only SR0+DR0 after release; B3
must arm all SR0–2 and DR0–3 (§9 P0).**

Device D2H publish preconditions ([V] firmware + phase43): **P1** channel `+0x00` enable = 1
(firmware sets it: file 0x86110–0x86114); **P2 `out[1]` must read 0** before the next publish
(file 0x86120–0x86128, bounded spin); **P3** notify id ≤ 10 (0x86170 `cmp r1,#0xa`); **P4**
`*(0x4000010c)==0xcece` before the announce path continues (0x86f74–0x86f7c).

---

## 3. H2D message format and the vendor send path

### 3.1 Message object + 12-byte HCC header

The HCC message object **is an skb**; its payload area holds a 12-byte header + data; header pointer
at object **+0x118** [V]. `hcc_msg_alloc(size, clear)` (plat 0x11d9c) [V]:

    max size  : movw r3,#0x674 ; size > 0x674 -> NULL                   0x11d9c/0x11da8
    alloc     : __netdev_alloc_skb(NULL, size+0xf, 0xa20)               0x11dc4
    hdr       : r2 = *(skb+0x118)                                       0x11dd0
    len field : *(u16*)(hdr+4) = size+0xc  (total = payload+12)         0x11db4/0x11e18
    alloc bit : hdr[1] low nibble |= 1 (allocated)                      0x11e0c–0x11e24
    clear     : if clear: memset_s(hdr, 0, size+0xf)                    0x11e50–0x11e60
    skb_put   : size+12 bytes                                           0x11e28

Header layout (verified across `hcc_msg_alloc`, `hcc_msg_tx`, `hcc_msg_process`,
`hmac_chan_initiate_switch_to_new_channel`) [V]; little-endian:

| off | width | field | meaning |
| ---: | ---: | --- | --- |
| +0x00 | u8 | **group/type** | low nibble = group index (0..15) selecting the group record in the HCC msg map (dispatch masks `&0xf`) |
| +0x01 | u8 | low nibble = alloc state (1 = allocated); **high nibble = resource-group / thread index** used for queue routing | |
| +0x02 | u8 | per-message field A | |
| +0x03 | u8 | per-message field B (e.g. chip id / vap id) | |
| +0x04 | u16 | **total length = payload + 12** | |
| +0x06 | u16 | **message id** (dispatch key inside the group) | |
| +0x08 | u8 | per-message field C (wal carries a sequence counter) | |
| +0x09 | u8 | **dst = hdr[1]>>4** (resource/thread selector, set by `hcc_msg_tx`) | |
| +0x0a | u16 | transport tag **0x5a5a** — SR/ETE-layer constant, not set via the HCC API | |
| +0x0c | … | **payload** | |
| end | — | padding to 4 B (builders allocate payload+0x10) | |

Evidence: `hcc_msg_alloc` 0x11d9c–0x11e30; `hcc_msg_tx` 0x11bf8 (`ldrb hdr+1; ubfx 4,4;
strb hdr+9`); `hcc_msg_process` reads `hdr[0]&0xf` and `u16 hdr+6` [V]. The `0x5a5a` tag
is what `pcie_ete_rcv_buff_check` (0x14de8/0x14dec) and `pcie_ete_tx_queue_handle` (0x15798)
check, and what the cali verify functions use as "valid" magic [V].

**Queue routing** [V]: `hcc_msg_tx` → `hcc_queue_get_by_msg(msg)` (0x12a00) →
`hcc_queue_add_msg(queue,msg)` (0x12930); returns 0, or **0x8b2d (OAL_FAIL)** if the queue is not
found. Queue selection uses the resource-group index (`msg[9]` = header byte+1 high nibble) + the
msg map entry for `msg[0]&0xf` + u16 id at `msg+6`. `hcc_msg_tx_to_core(msg, core)` (0x11b2c)
retries into the other core's resource group.

### 3.2 Dispatch and table registration (host side)

`hcc_msg_process(reg_tab, msg)` (0x1204c) [V]:

    hdr = *(msg+0x118); if !hdr or !reg_tab return
    map = *(reg_tab+4); n_groups = *(map+0x10)
    group = hdr[0] & 0xf; if group >= n_groups return
    rec   = *(reg_tab+0x10) + group*12          ; {u32 max_id; u32 item_array}
    id    = *(u16*)(hdr+6); if id >= rec[0] return
    fn    = *(u32*)(rec[1] + id*16 + 4)         ; 16-byte item {u32 id; u32 fn; u32 aux; u32 stats}
    if fn: fn(msg)                              ; r0 = the HCC message object

Registration (plat 0x117e4–0x11a94) [V]:

    hcc_msg_register_tab_chip(group, table, count) -> customise(type 0, ...)
    hcc_msg_register_tab_core(group, table, count) -> customise(type 1, ...) + customise(type 2, ...)
    customise(type, group, table, count):
      res = hcc_get_group_res(type) = hcc_instance + 0x18*type + 0x394
      map = *(res+4); if group >= *(map+0x10) -> fail
      rec = *(res+0x10) + group*12; itemarr = *(rec+4); if !itemarr -> fail
      for each input record {u16 id; u16 pad; u32 func; u32 aux} (stride 12):
          if id >= rec[0] -> fail
          itemarr[id].id = id; itemarr[id].func = func

(type 0 = chip map; types 1/2 = the two core maps, so `register_tab_core` covers both cores.)

### 3.3 SR ring node + the announce (id 3)

`shuangta_ete_sr_dscr_fill` (0x17858) [V]:

    word1 = (u16(len) << 16)
    if 5th arg != 0: word1 |= 0x4000 | 0x2000 | 0xd2b   -> (len<<16)|0x6d2b  "present"
    else:            word1 |= (arg bit14, bit13) | 0xd2b
    idx = (*(u32*)(chan_ctx+0xc)) & 0x3ff               ; packed producer index
    node[idx].word0 = buffer device VA
    node[idx].word1 = word1
    pcie_ete_ring_ptr_plus(chan_ctx+0xc, depth)         ; commit (0x13ef8)
    pcie_msg_send(chip, 3)                              ; the mailbox announce
    chan_ctx[0x100]++                                   ; tx counter

`pcie_ete_ring_ptr_plus` (0x13ef8) [V]: `idx=(idx+1)&0x3ff; if ((idx&0x3ff)==depth){idx&=~0x3ff;
idx^=0x400;} store`. Buffer payload = HCC message; the node length is the **buffer total**
(`u16 [buf+4]` is used as the DMA size by `pcie_ete_tx_queue_handle` 0x15798 [V]).

`pcie_msg_send(chip, id)` (0x160f4) [V] — the announce primitive:

    arg0 must be 0 (chip 0); id = arg1, must be <= 9                      0x16138
    spin_lock(chip+0x50)
    if !(pending & (1<<id)):
        pending |= (1<<id)
        if chip[+0x44] == 0:                    ; no message in flight
            *[chip+0x2c] = pending              ; out[0] CA 0x40039010 = pending bitmap
            chip[+0x44]  = pending
            *[chip+0x34] |= 1                   ; out[2] CA 0x400392d4 doorbell |= bit0
    return id (or 0x8b2d on bad args)

So the H2D mailbox sequence for any normal message: **write pending bitmap to CA 0x40039010, then OR
1 into CA 0x400392d4.** Do not write CA 0x400392f0.

`pcie_msg_send_irq` (0x174a8; registered as per-channel async send callback in `pcie_msg_init`
struct+0x60) is the **deferred/interrupt variant** [V]:

    *[ctx+0x40] = 8                     ; phase37 identifies ctx+0x40 as out[5] CA 0x400392f0 [I]
    pcie_msg_wait_for_clr(ctx+0x2c)     ; spin (~0xfffe) until *out[0] reads 0
    spin_lock; pending = ctx[+0x48]; ctx[+0x44] = pending
    if pending: *out[0] = pending; ctx[+0x48] = 0; *out[2] |= 1 (doorbell bit0)

`pcie_msg_wait_for_clr` (0x173dc) returns 0 on clear or 0x8b2d on timeout [V]. Our transport
skeleton's **twin_enabled=0 default is correct**: the out[5] word is only written on this callback
path and the project rule forbids CA 0x400392f0; B2's smoke omitted it and completed cleanly [V\*].

### 3.4 Full vendor H2D chain

    hmac_send_msg (wifi.ko 0x46eb4) / hmac_hcc_msg_tx_send (0x40320)
      -> hcc_msg_tx (plat 0x11bf8)                 set dst, find queue, enqueue
      -> hcc_queue_add_msg (0x12930)               queue list
      -> hcc_thread_process_tx (0x11418)           TX worker drains queues
      -> hcc_queue_tx_process (0x1128c)            port = queue's bal port; permit = min(
                                                   queue pending, bal_get_port_info free desc);
                                                   move msgs; bal_port_start_xfer
      -> bal_port_start_xfer (0x10b78)             indirect call into the PCIe/bal port cb
      -> pcie_xfer_data (0x167e4) -> pcie_tx_request_handle (0x166a0)
             under lock append descriptors to chip tx list; pcie_wkup_thread
      -> pcie TX worker -> pcie_ete_tx_queue_handle -> shuangta_ete_sr_dscr_fill (0x17858)
             -> node + producer commit + pcie_msg_send(chip, 3)

Back-pressure [V]: `hcc_queue_get_tx_buf_num(queue, port_res)` (0x111ec) = `min(u16 queue[+4],
u16 bal_get_port_info.info[+2])`; `hcc_queue_tx_process` returns 1 when more remain and the TX
thread loops. Driver-visible: `hcc_msg_tx` may fail with **0x8b2d**, `pcie_msg_send_irq` may time
out with **0x8b2d** waiting for `out[0]==0` — treat both as retryable "device busy", not protocol
errors. `hcc_msg_free` 0x11b1c; `hcc_msg_clone` 0x11e84 for multi-core broadcast.

### 3.5 Device side of H2D

Firmware dispatcher file 0x818ac ([V], phase37/43): on the doorbell it writes ack
`*(0x400392f0)=1`, reads+clears `out[0]`, writes `0x400392d4 = 8`, then dispatches the lowest
set bit (ids ≤ 9) through its 8-byte table at device RAM 0x118d68 (ids **1,3,5,6** populated).
Handlers: id1 = 0x510 (post global msg object), id3 = 0x85144 (SR announce consumer,
arg 0x0010C0F4), **id5 = 0x819dc = BUG() trap** (bytes `00 23 1b 60 ff de`), id6 = 0x4cce4.
**Never post id 5 on a success path.**

---

## 4. ETE engine contract (SR/DR)

### 4.1 Block, channels, resources

| item | value | evidence |
| --- | --- | --- |
| ETE register block CA | **0x4003a000** (BAR0+0x3f2000); window ends CA 0x4003afff (0x4003b000 reads 0xffffffff) | [V] phase42/43; `get_pcie_ete_res` used by `pcie_ete_rings_init` 0x76e8–0x7728 |
| ETE interrupt/mask block CA | **0x40039508** (BAR0+0x3f1508) | [V] phase42 A.1; `pcie_ete_intr_init` 0x7528 |
| channels | 7 × 0x50 B: **SR0–2 `+0x400/+0x450/+0x4a0`, DR0–3 `+0x590/+0x5e0/+0x630/+0x680`** | [V] fresh decode of the channel table |
| per-channel enable | `block+0x00` bit0, firmware sets it itself; no host caller of `pcie_ete_chn_enable` (0x147dc) in plat.ko | [V] phase43 + caller scan |
| ctrl | `block+0x08` low 3 bits = channel cfg byte5 | [V] `pcie_ete_sr_reg_init` 0x14ae0–0x14af4 |
| resource / second enable | `block+0x28` / `block+0x48` exist; semantics [U] | [V] phase42 |

Channel config table re-decoded from current plat.ko via `pcie_ete_get_chn_cfg` (0x15f00; literal
`.rodata+0x101c`, 7 × 12 B) [V]:

    ch0 SR0: {block_off 0x400, depth/ctrl 0x06800020, flags 1}   depth byte(+4)=0x20 -> 32
    ch1 SR1: {0x450, 0x06800020, 1}                               ctrl byte(+5)=0x00
    ch2 SR2: {0x4a0, 0x06800020, 1}
    ch3 DR0: {0x590, 0x06800020, 1}   <-- flags=1 only for SR0-2 and DR0
    ch4 DR1: {0x5e0, 0x06800020, 0}
    ch5 DR2: {0x630, 0x06800020, 0}
    ch6 DR3: {0x680, 0x06800020, 0}

**[I]** the third word marks **HCC/control channels** (SR0–2, DR0) vs data channels (DR1–3):
three SR ports for chip+2 cores and DR0 for host-bound control/message traffic. **Bench-pin in B3.**

### 4.2 Register semantics (groups A and B)

Each channel has two 4-word program groups: **A = +0x10/+0x14/+0x18/+0x1c**, **B =
+0x30/+0x34/+0x38/+0x3c** = {base, depth−1, producer, consumer/index}.

| direction | host writes | device writes | meaning |
| --- | --- | --- | --- |
| **SR** (host→device) | group A: `+0x10` = device VA of the 8-B node array, `+0x14` = depth−1 (low 10 bits), `+0x18` = producer index, `+0x08` = ctrl bits | `+0x1c` = consumer index | host produces descriptors; device consumes |
| **DR** (device→host) | group B: `+0x30` = base, `+0x34` = depth−1, `+0x38` = host-published free-buffer index | `+0x3c` = service/fill index | host publishes free buffers; device deposits payloads+node words and advances |

[V] `pcie_ete_sr_reg_init` (0x14a48: `SR+0x10=pcie_hostca_to_devva(node array)`,
`SR+0x14=cfg[4]−1` via `bfi #0,#0xa`, `SR+0x18=ctx[+0xc]`, `SR+0x08=ctrl`) and
`pcie_ete_dr_reg_init` (0x1483c: `DR+0x30=devva`, `DR+0x34=cfg[4]−1`, `DR+0x38=ctx[+0x1c]`);
DR re-publish write in `pcie_ete_sending_trigger` (0x14430 `str r2,[r3,#0x38]`) [V].
Device-measured shape [V\*]: depth−1=**0x1f** (32), index ≤0x7ff with the **0x400 wrap-phase bit**,
base in **0x80000000..0xffffffff**.

**Index encoding** (`pcie_ete_ring_ptr_plus` 0x13ef8) [V]: low 10 bits node index, bit 10 = wrap
phase; advance idx+1; when `(idx&0x3ff)==depth` reset low bits to 0 and toggle bit 10. The firmware
twin is file 0x81906 (phase42 §2.3(c)).

### 4.3 Node format

8-byte node = `{u32 word0 = buffer device VA; u32 word1 = (len<<16) | flags}` [V]:
`shuangta_ete_sr_dscr_fill` 0x178cc/0x178e0 builds exactly that; DR twin `shuangta_ete_dr_dscr_fill`
(0x1765c); firmware twin file 0x861dc/0x8192a. Flags: low 13 bits = direction tag **0xd2b**, bits
13/14 = **present/valid** (both ⇒ 0x6000; full word `(len<<16)|0x6d2b`). Accessors confirm the
split: len = `word1>>16`, flag = `word1&0x1fff` (`shuangta_ete_sr_get_dscr_len` 0x17684,
`shuangta_ete_sr_get_dscr_flag` 0x17698) [V].

### 4.4 Host init order (vendor, at probe) [V]

    pcie_main_init: ... pcie_ete_init (0x7820)
      pcie_ete_init:
        alloc 3 contexts; get_pcie_ete_res() -> ctx[+0x18] (res[0]=0x4003a000, res[1]=0x40039508)
        pcie_ete_intr_init (0x7528): map intr CA 0x40039508 -> VA; *VA &= 0xffe0f8f8;
          installs chip[+0x1c]=pcie_rx_handle, +0x20=pcie_tx_done_handle,
          +0x24=pcie_ete_rx_err_handle, +0x28=pcie_ete_tx_err_handle
        pcie_ete_rings_init (0x7680): memset contexts; map ETE block VA;
          pcie_ete_init_src_ring (0x7068) and pcie_ete_init_dst_ring (0x727c)
        pcie_ete_chn_res (0x7490): channel resource/A-B copy glue (0x2e8 word RMW) [I]
    then pcie_comm_init arms the mailbox (§5)

Our B2 programmed SR0 base 0x8360c000 / DR0 base 0x83634000, depth−1 0x1f after release [V\*] — same
contract; B3 should extend to all channels.

### 4.5 DMA window

Engine-side addresses for node arrays and buffers are host DRAM in **0x80000000..0xffffffff**,
identity to host phys 0x80000000 on luofu; allocate with **dma_alloc_coherent** and publish the DMA
handle (device VA). The RC inbound iATU (6.18 DWC framework from DTS `dma-ranges`; B2-verified vp0
0x80000000..0x8fffffff) makes it reachable [V\*/I]. Buffers the device writes must be coherent; the
vendor used `pcie_rls_coherence_addr` (0x6d10) in its release path [V].

### 4.6 Completions and DR consume protocol

* **Mailbox id 3 = ETE transfer done** → `pcie_ete_transfer_done_handle` (0x8730) →
  `pcie_ete_d2h_isr_handle` (0x15c1c) with (chip, 0, 0x1f) ⇒ walks DR channels' completed ranges [V].
* **Mailbox id 6/7 = data staged / wake** → `pcie_wkup_thread` (0x1629c); the RX worker
  (`pcie_rx_handle` 0x164d0) drains DR via `pcie_ete_dr_get_uploadbuf` (0x1515c) /
  `pcie_ete_rcv_buff_check` (0x14d74) [V call chain, phase37 §1.1].
* **Host consume**: for each completed node (from last-seen service index to device `DR+0x3c`):
  read `word1>>16` = payload length, `word0` = buffer device VA; dispatch the payload as an HCC
  message; return the buffer and advance **`DR+0x38`** so the device can reuse it; refresh the
  cached index. `pcie_ete_get_rls_num` (0x14734) computes newly completed nodes as
  `depth*phase_delta + (idx−last)` with the 0x400 phase bit (modulo-2*depth counter) [V arithmetic,
  [I] channel binding].
* **Firmware never writes DR registers**: phase43's exhaustive scan found no `str` to any DR block
  offset; the firmware only reads `DR+0x30` and sets the channel enable. Deposit index/payload
  placement are ETE hardware acts [V phase43 negative + [I]].
* Repeats: **P2** the device will not publish the next D2H word until `out[1]` reads 0 [V]; **P1**
  it re-asserts its own channel `+0x00=1` first [V].

---

## 5. D2H dispatch: mailbox ids 0..10 and handler catalogues

### 5.1 The six message registers (authoritative order) [V]

`shuangta_pcie_msg_reg_map` (plat 0x1b1a0) resolves six CAs in this order (literals 0x1b1b0,
0x1b1e8, 0x1b20c, 0x1b230, 0x1b254, 0x1b278):

| idx | CA | BAR0 | role |
| ---: | --- | --- | --- |
| 0 | **0x40039010** | 0x3f1010 | **H2D pending bitmap** (host writes; device reads+clears) |
| 1 | **0x40039014** | 0x3f1014 | **D2H pending bitmap** (device writes; host reads, drains) |
| 2 | **0x400392d4** | 0x3f12d4 | **H2D doorbell** (host ORs bit0; device writes 8) |
| 3 | **0x40101438** | 0x4b9438 | **D2H ack** (host writes 1) |
| 4 | **0x40101414** | 0x4b9414 | **D2H re-arm** (host writes 1) |
| 5 | **0x400392f0** | 0x3f12f0 | H2D ack/"copy A" — device writes 1 after consuming; **project rule: never write from host** |

Device D2H notify primitive (firmware 0x86170, [V] phase43): id bound ≤10; pending shadow
`ctx+0xb8`; writes the pending word to **out[1]** and ORs bit0 into **D2H doorbell 0x40101434**
(pinned by 0x86f5e–0x86f66: 0x40039014 + 0xc8000 + 0x420).

### 5.2 Host receive algorithm — `pcie_msg_handle` (0x171f8) [V]

    *[ctx+0x0c] = 1                    ; ack   (CA 0x40101438)
    bitmap = *[ctx+0x04]               ; read D2H bitmap (CA 0x40039014)
    *[ctx+0x04] = 0                    ; drain it (device precondition P2)
    *[ctx+0x10] = 1                    ; re-arm (CA 0x40101414)
    while bitmap:
        id = ctz(bitmap)               ; lowest set bit (rbit+clz)
        if id > 10: break              ; cmp r6,#0xa
        fn = table[id].fn; arg = table[id].arg    ; table at [ctx+0x20], 8-byte entries
        if fn: fn(arg)
        clear bit id

`pcie_msg_register(handle, id, fn, arg)` (0x15fbc) [V]: rejects **id > 10** or fn==0; writes
`{fn, arg}` at `table[id*8]` (11 × 8 B; unregistered ids NULL and dropped).

### 5.3 Registered ids and actions [V]

| id | handler | action | host reply |
| ---: | --- | --- | --- |
| 1 | `pcie_dev_ready_msg_handle` 0x8784 | `bx lr` empty | none |
| 2 | — | **not registered** → dropped | none (the `0x4` B1 saw) |
| 3 | `pcie_ete_transfer_done_handle` 0x8730 → `pcie_ete_d2h_isr_handle` | drain DR → HCC dispatch | none |
| 6 | `pcie_trigger_ete_sending_handle` 0x15efc → `b pcie_wkup_thread` | wake worker | none |
| 7 | same | wake worker | none |
| 0,4,5,8,9,10 | — | dropped | none |

### 5.4 Required host write sequence per received D2H word

1. `out[3]=1` (CA 0x40101438) — ack.
2. read `out[1]` (CA 0x40039014), then write 0 — drain (mandatory, P2).
3. `out[4]=1` (CA 0x40101414) — re-arm.
4. dispatch per lowest set bit (save the word before clearing).
5. for id 3/6/7: run the DR drain; per node dispatch the HCC message (group/id); return buffers and
   advance `DR+0x38`.

### 5.5 HCC D2H catalogues (re-decoded from current modules)

**plat.ko chip group 4 — boot/exception** (`plat_init_bal_hcc_excp` 0xe380:
`hcc_msg_register_tab_chip(4, .data+0x2660, 5)`) [V]: see §2.4.

**wifi.ko hdpp — chip group 3 (5) and core group 0 (7)** (`hdpp_main_init` 0x12e0; calls 0x135c /
0x136c; tables `.data+4` / `.data+0x40`) [V]:

| space/g | id | handler |
| --- | ---: | --- |
| chip g3 | 0 | `hmac_voice_aggr_event` |
| chip g3 | 4 | `hmac_device_wow_data_report` |
| chip g3 | 5 | `hmac_rx_schedule_req` |
| chip g3 | 7 | `hdpp_stat_save_tx_ppdu_record_process` |
| chip g3 | 8 | `hdpp_stat_save_rx_ppdu_record_process` |
| core g0 | 0 | `hmac_tx_complete_event_handle` |
| core g0 | 1 | `hmac_tx_event_process` |
| core g0 | 2 | `hmac_rx_process_data_event` |
| core g0 | 3 | `hmac_rx_process_data_event` |
| core g0 | 4 | `hmac_tx_complete_notify_other_core_event_handle` |
| core g0 | 7 | `hmac_mac_exception_proc` |
| core g0 | 8 | `hmac_ba_timeout_proc` |

**wifi.ko hmac — chip groups 1 (28), 2 (12), 3 (8)** (`hmac_main_init` 0x3f6b8; calls 0x3f768,
0x3f778, 0x3f788; tables `.data+0x584/0x6d4/0x764`) [V]:

    g1: 0 hmac_sdt_up_reg_val           1 hmac_create_ba_event          2 hmac_del_ba_event
        3 hmac_event_config_syn         5 hmac_syn_info_event           9 hmac_bandwidth_info_syn_event
        10 hmac_protection_info_syn_event 11 hmac_ch_status_info_syn_event
        13 hmac_rx_process_mgmt_event   14 hmac_mgmt_rx_delba_event      15 hmac_scan_proc_scanned_bss
        16 hmac_scan_proc_scan_comp_event 17 hmac_scan_process_chan_result_event
        18 hmac_event_acs_response       20 hmac_mgmt_send_disasoc_deauth_event
        21 hmac_mgmt_send_disasoc_deauth_event 22 hmac_chan_switch_to_new_chan_complete
        24 hmac_mgmt_tbtt_event          25 hmac_dfs_radar_detect_event   26 hmac_proc_disasoc_misc_event
        29 hmac_acs_process_rescan_event 31 hmac_csi_complete            34 hmac_sr_bss_color_change_event
        35 hmac_sr_syn_parameter_event   36 hmac_chr_report_msg           37 hmac_pfm_detect_process_event
        38 hmac_config_set_security_port_event 39 hmac_update_fbt_scan_result
    g2: 22 (NULL) 23 (NULL) 24 hmac_spectral_scan_complete 25 hmac_phy_event_complete
        26 hmac_phy_event_complete 27 hmac_mgmt_deauth_msg_process 28 hmac_ba_action_msg_process
        29 hcc_timer_process 33 hmac_mgmt_read_error_msg_process
        34 hmac_mgmt_send_deauth_frame_process 35 hmac_del_user_msg_process
        36 hmac_rx_data_send_disasoc_frame_process
    g3: 9 hmac_smps_update_device_capbility 10 hmac_pfm_hiex_rx_local_msg 15 hmac_bsd_update_msg
        16 hmac_multiap_report_11v_event 17 hmac_d2h_pm_event 18 hmac_d2h_temp_state_update
        19 hmac_receive_beacon_probrsp_to_reprot 20 hmac_report_beacon_frame

**wifi.ko wal — chip group 2 (19)** (`wal_main_init` 0x12386c; call 0x1238a0; table
`.data+0x1c74`) [V] — merges into the same chip group 2 as hmac's 12 entries:

    0 wal_config_process_pkt  1 wal_acs_netlink_recv_handle 2 wal_scan_comp_proc_sta
    3 wal_asoc_comp_proc_sta  4 wal_disasoc_comp_proc_sta    5 wal_connect_new_sta_proc_ap
    6 wal_disconnect_sta_proc_ap 7 wal_mic_failure_proc      8 wal_acs_response_event_handler
    9 wal_send_mgmt_to_host  12 wal_cfg80211_init_evt_handle 13 wal_cfg80211_mgmt_tx_status
    20 wal_cfg80211_cac_report 21 wal_receive_all_sta_rssi_proc 30 wal_report_external_auth_req
    31 wal_process_packet_xmit 32 wal_dfr_power_down_dev     37 wal_multiap_report_proc
    38 wal_hiwifi_report_proc

**No D2H id requires a reply.** The only host→device message on a receive path in the vendor is
`pcie_msg_send(chip,5)` inside `pcie_ete_rcv_buff_check` (call 0x15144), on the **failure
branch**; the device's id-5 handler is the `BUG()` trap. **Do not post id 5 on the success path.**

### 5.6 Device-side H2D dispatch (reference)

Firmware table at device RAM 0x118d68 (8-byte {fn,arg}), dispatched on lowest set bit of `out[0]`;
ids 1,3,5,6 populated (files 0x510 / 0x85144 / 0x819dc BUG / 0x4cce4) [V phase37/43]. The H2D
mailbox is a small fixed vocabulary; real traffic is SR-ring HCC frames.

---

## 6. hdpp / alg attachment points

### 6.1 hcc queue customers (TX port hookup) [V]

`hcc_queue_register_customer_for_chip(queue_id, lock_fn, unlock_fn)` (0x124fc) and
`..._for_core` (0x12544) store the callbacks at queue`+0x38/+0x3c` (queue struct stride 0x54).
hdpp registers (`hdpp_main_init` 0x12e0):

    hdpp_chip_init(*(.data+0)); hdpp_var_init(); hdpp_hisi_thread_init()      ; worker thread
    hcc_queue_register_customer_for_chip(2, hdpp_hcc_sync_read_lock, hdpp_hcc_sync_read_unlock)
    hcc_queue_register_customer_for_core(0, ...same...)
    hcc_msg_register_tab_chip(3, .data+0x04, 5)     ; §5.5 chip g3
    hcc_msg_register_tab_core(0, .data+0x40, 7)     ; §5.5 core g0

So the driver's data TX path is **hcc queue 2 (chip) / queue 0 (core)**; the callbacks are the
read-lock hooks around queue access.

### 6.2 Config/alg/cali request-response surface [V]

* **H2D config events** (generic builders): `hmac_config_send_event` (wifi.ko 0x57210; calls
  `hcc_msg_tx`; chip group 0 id 1 helper) and `hmac_config_alg_send_event` (0x60c44; group 0 id 2
  helper). They carry the wal/hmac config command blobs (the alg_cfg codec slice documents the
  414-entry command table and 176-B LUT).
* **D2H responses**: `hmac_event_config_syn` (wifi.ko 0x6b0e0, **chip group 1 id 3**) is the
  dispatcher that routes to `hmac_sync_dmac_alg_cfg_rsp_entry` (0x87b58),
  `hmac_sync_dmac_cali_cfg_rsp_entry` (0x87f8c) and `hmac_chan_tx_cali_sync` (0x46d9c). The
  response carries the request-id word (vendor convention 0x0d01xxxx, 2g/5g variants) pairing it
  with the pending request; the wal glue slice documents
  `wal_msg_request_add_queue / wal_set_dev_msg_response_by_addr / wal_check_and_release_msg_resp`
  (0x126808 / 0x126908 / 0x109370) [V from driver-re §3.7 + algcfg/wal slices].
* **hdpp D2H handlers call into the hdpp object model** [V call lists]: `hmac_voice_aggr_event` →
  hdpp_vap_get_by_id; `hmac_rx_schedule_req` → hdpp_vap_get_by_id, hdpp_band_get_by_id;
  `hmac_device_wow_data_report` → __netdev_alloc_skb, memcpy_s, mac_rx_cb_get_from_msg,
  hmac_rx_ring_entry_proc, hi_net_woc_skb_free (WOW/offload frame delivery into the network stack);
  the hdpp stat handlers only update `g_st_hdpp_statics` (ppdu tx/rx records).
* **Attachment summary**: (a) register the §5.5 D2H tables via the HCC registration API; (b) register
  queue customers for chip queue 2 / core queue 0; (c) implement the config-sync response decoder at
  chip group1 id3; (d) implement hdpp lifecycle (`hdpp_chip_init → hdpp_var_init →
  hdpp_hisi_thread_init → register → hdpp_user/vap init`) before data traffic; (e) alg codec and
  wal glue sit above this contract unchanged.

### 6.3 Core (per-band) vs chip addressing

HCC dispatch has three register spaces: type 0 = chip map (groups 1/2/3/4 registrations) and types
1/2 = the two core maps (only hdpp registers there, core group 0). The message header's **byte+1
high nibble** selects the resource-group/queue (`hcc_msg_tx` copies it to hdr[9]); the D2H core
messages carry the core in the same nibble (the ready handler reads `hdr[1]>>4` as source core;
`host_ready_msg_process` indexes its per-core record by it) — that is the 2.4G/5G separation [V].
Group space selection at dispatch is done by the queue/bal layer (chip → type 0, per-core → type 1/2)
[V from `hcc_get_group_res(type)` and the dual registration].

---

## 7. Other init between DEVICE READY and first traffic

The vendor sends **no H2D message** between the ready handshake and normal driver operation [V]:

    wifi.ko host_module_init (0x3e154):
      oal_net_dev_res_init; oam_wifi_init
      multi_chip_loading()                    <-- the two ready waits (§2.4)
      hal_main_init()                         (0x31d98)
      hmac_main_init()                        (0x3f6b8)
        state gate; waitqueue; hmac_res_vap_dev_init; hmac_res_init
        hcc_msg_register_tab_chip(1,.data+0x584,28); (2,.data+0x6d4,12); (3,.data+0x764,8)
        hdpp_main_init(); hmac_acs_init(); hmac_psta_mgr_init(); hmac_phy_event_init();
        hmac_spectral_init(); ... ; hmac_fsm_init(); hmac_board_init()
        hcc_set_state(4)
        hmac_save_cali_data_to_file()         <-- cali RAM structs -> file
      wal_main_init()                         (0x12386c)
        wal_msg_queue_init; state gate (>3, !=6)
        hcc_msg_register_tab_chip(2,.data+0x1c74,19)
        wal_hipriv_create_proc; wal_cfg80211_init (wiphy_new_nm/wiphy_register);
        oal_cfg80211_genel_netlink_init; hcc_set_state(5);
        wal_drv_cfg_func_hook_init; oam_wal_func_fook_register; wal_acs_init; wal_dfx_init;
        wal_set_custom_process_func

**Calibration** (the stock log line "2g/5g the cali result to device succ" is a misnomer) [V]:
strings at wifi.ko `.rodata` 0x5a080/0x5a0b4/0x5a0dc/0x5a118/0x5a140 belong to
`hmac_save_cali_data_to_file_5g` (0xc3c4c) / `_2g` (0xc3d44) →
`hmac_save_cali_data_verify_*` (0xc3a18/0xc3b34), which normalize RAM magic
`0xfeedfeed → 0x5a5a5a5a / 0xa5a5a5a5` (offsets 0 and 0x91c for 2g; 0x2000+0x2cc for 5g) and
write the container to `hmac_rf_cali_data_file_name*` via filp_open/oal_kernel_file_write/
vfs_fsync/filp_close. plat.ko owns the cali buffers and exports their host/device addresses:
`hmac_rf_cali_host_addr` (0x14d4) / `_2g` (0x14e4) = LANCHOR0[0]/[1];
`hmac_rf_cali_data_dma_addr` (0x150c) / `_2g` (0x151c) = LANCHOR0[2]/[3] [V]. The actual
**device delivery mechanism is [U]**: likely a shared host-DRAM cali arena visible through the
outbound window (loaded by `hwifi_rf_cali_file_load` in `wlan_power_on`), not an HCC message; the
first steady-state H2D messages are config events (§6.2) when the driver configures the radio.

**Heartbeat / PM**: chip group4 id4 restarts the host heartbeat hrtimer (`hrtimer_start_range_ns`
0x10280) — no host action beyond dispatch [V]; the D2H GPIO/PM callback
`wlan_pm_d2h_gpio_irq_callback` (plat 0xe734) is a separate PM line [V].

---

## 8. Driver-facing contract in one page (what the Lead implements next)

Preconditions (true after B2): link up, BAR0 windows, fw released, gate set, D2H mailbox drained,
ETE block reachable in the 0x8xxxxxxx DMA window.

1. **Arm ETE** (vendor does it at probe; can be done now): program SR0–2 group A and DR0–3 group B
   with `dma_alloc_coherent` node arrays (8-B nodes, depth 32, index
   `(idx&0x3ff)|(phase<<10)`), SR ctrl=0; leave `+0x00` enable to firmware. Mask the ETE interrupt
   block (`0x40039508 &= 0xffe0f8f8`) or use the polled path.
2. **Pre-post DR free buffers**: fill `node.word0` = payload buffer device VA for the first depth
   slots and publish `DR+0x38`; keep node array and payload buffers coherent.
3. **D2H poll loop**: read `out[1]`; per §5.4 ack → drain → re-arm → dispatch; on id 3/6/7 drain
   DR, decode each node's payload as an HCC frame, dispatch via the §5.5 tables, return buffers +
   advance `DR+0x38`.
4. **Ready completion**: expect **chip group4 id 1** (`device_plat_ready_msg_process`) then **id 2**
   (`host_ready_msg_process`, ≤0x63-byte payload) from the DR path — the two vendor init waits.
   Send nothing.
5. **H2D**: build `{12-B header, payload}` in a coherent buffer, post an SR node
   (`len = payload+12`, flags 0x6d2b), advance the SR producer, then `out[0] = 1<<3;
   out[2] |= 1` (or the drain-poll variant with twin disabled). Wait for `out[0]` back to 0 before
   reusing the slot. Never post mailbox id 5; never write 0x400392f0.
6. **Layer bring-up**: register the D2H tables (§5.5), queue customers (chip 2/core 0), then
   hal → hmac → wal order (§7) with the config-sync decoder at chip group1 id3.

---

## 9. Bench probes for the next attended session (B3; extends BENCH-PLAN B2)

All are reads or the §5.4 ack-drain-rearm writes already proven; no new writes beyond the mailbox
protocol. Recovery armed, UART read-only, one idea per session, power-cycle is the only reset after
release.

| # | probe | expected / decision value |
| ---: | --- | --- |
| P0 | arm all rings: SR0–2 + DR0–3 depth 32, pre-post DR nodes on all four channels; report each base (0x8xxxxxxx) and depth−1=0x1f | confirms vendor ring shape; prerequisite for any D2H payload |
| P1 | keep polling after draining `0x4`; log every subsequent `out[1]` for 30 s | expect **bit6 (0x40)** (firmware `d2h_notify(obj,6)` after a DR deposit) and/or **bit3 (0x8)** (ETE done). If only 0x4 ever appears → publish side not reached (P4) |
| P2 | on id 3/6 snapshot all four DR blocks' `+0x3c/+0x38` and the first 12 B of up to 8 completed nodes; decode header group/type/id/len | pins **which DR channel carries chip-group messages** (prediction [I]: DR0, flags word=1) and whether group-4 id1/2 arrive |
| P3 | when a group-4 id1/2 payload is seen, log payload len + first bytes; check id2 payload ≤0x63 and record the bytes | the real handshake gate; no H2D reply |
| P4 | read `out[0]` (H2D pending), `out[5]` **read-only**, DR `+0x00` enables after release; confirm firmware re-asserted each DR `+0x00=1` and `out[0]=0` | tests P1/P2 preconditions if no payload appears |
| P5 | if id3 drains but nodes are empty/zero: dump node word1 + first 16 B per buffer | distinguishes "no deposit" from "wrong node indexing" |

Safety: **never write CA 0x400392f0**; never read the RC misc window 0x10161000; UART read-only.

---

## 10. Evidence ledger

| claim | artifact / method | grade |
| --- | --- | --- |
| ready announce writes `out[1]=4`, D2H doorbell, then spins on `0x4000010c==0xcece` | FIRMWARE.bin file 0x86f32–0x86f7c, literals 0x86fa0/0x86fb4/0x86fb8/0x86fbc Thumb | [V] |
| no vendor module writes `0xcece` / CA 0x4000010c | u32 scan over all vendor `.ko` in recovery-extract | [V] negative |
| mailbox ids {1,3,6,7}; id>10 rejected; id2 dropped | plat `pcie_msg_init` 0xb814–0xb8b8; `pcie_msg_register` 0x15fcc; `pcie_msg_handle` 0x172bc | [V] |
| D2H ack/drain/re-arm + lowest-bit-first | plat `pcie_msg_handle` 0x17248–0x17308 | [V] |
| six message CAs/order | plat `shuangta_pcie_msg_reg_map` 0x1b1b0–0x1b280 | [V] |
| ready waits 200/2000 j; completers g4 id1/id2 | plat `multi_chip_loading` 0xf2ac–0xf3c8; `plat_init_bal_hcc_excp` 0xe380–0xe3b8; .data+0x2660; handlers 0xeb78/0xebd8 | [V] |
| HCC header/alloc/queue/dispatch | plat `hcc_msg_alloc` 0x11d9c, `hcc_msg_tx` 0x11bf8, `hcc_msg_process` 0x1204c, `hcc_msg_register_tab*` 0x117e4–0x11a94, `hcc_queue_get_by_msg` 0x12a00 | [V] |
| SR node/announce; producer packed index | plat `shuangta_ete_sr_dscr_fill` 0x17858, `pcie_ete_ring_ptr_plus` 0x13ef8, `pcie_msg_send` 0x160f4, `pcie_msg_send_irq` 0x174a8, `pcie_msg_wait_for_clr` 0x173dc | [V] |
| ETE channel table (7 × 0x50, depth 32) + fields | plat `pcie_ete_get_chn_cfg` 0x15f00 + .rodata+0x101c; `pcie_ete_sr_reg_init` 0x14a48; `pcie_ete_dr_reg_init` 0x1483c; `pcie_ete_intr_init` 0x7528; `pcie_ete_rings_init` 0x7680 | [V] |
| DMA window 0x8xxxxxxx + RC inbound iATU | phase42 E.3 [V\*]; B2 ledger 2026-11-22 18:35 | [V\*] |
| D2H catalogues (g4, hdpp g3/core g0, hmac g1/2/3, wal g2) | registration call sites + .data/.rel.data tables of current modules | [V] |
| device H2D ids 1/3/5(BUG)/6; D2H notify; P1–P4 | FIRMWARE.bin 0x818ac, 0x86108, 0x86170, 0x86e14 + phase43 | [V] |
| cali "to device" log is file-save verify; cali buffers exported by plat | wifi.ko 0xc3a18/0xc3b34/0xc3c4c/0xc3d44; plat 0x14d4–0x1528 | [V] |
| hdpp/alg attach tables, queue customers, config-sync dispatcher | wifi `hdpp_main_init` 0x12e0, `hmac_main_init` 0x3f6b8, `wal_main_init` 0x12386c, `hmac_event_config_syn` 0x6b0e0; plat `hcc_queue_register_customer*` 0x124b0–0x12600 | [V] |
| ready waits are HCC payloads, not mailbox bits | contract chain §2; no id2 handler + handler call lists | [V] / [I] for bench interpretation |

**Corrections carried by this report:**
1. BENCH-PLAN's "ready1 = bitmap 0x2 / ready2 = bitmap 0x4" maps the mailbox id space onto the HCC
   ready ids. Mailbox id 2 has **no host handler**; the vendor waits are released by HCC chip group4
   ids 1/2 delivered over DR. Keep bitmap 0x4 as the platform-up announce, not as handshake proof.
2. phase37 "D2H ids 0..9": the host bound is **≤10** (`cmp #0xa`); firmware side is ≤9.
3. `hdpp_main_init` chip group-3 ids are {0,4,5,7,8}; core group-0 ids {0,1,2,3,4,7,8} (table
   order in .data is 0,1,2,4,3,7,8 — the 4/3 pair is swapped).
4. The cali "2g/5g the cali result to device succ" line comes from the cali **file-save/verify**
   functions, not from an H2D message (no device send in that path).

**Remaining [U]:** which firmware peer writes `0xcece`; which ETE channel carries the group-4 ready
messages and each HCC port (prediction DR0, §4.1 [I]); the cali-to-device delivery mechanism;
twin/out[5] semantics on the interrupt send path; the DR payload buffer pool depth the firmware
expects.

---

## Appendix A — quick address reference

    device CAs   ETE block 0x4003a000, intr 0x40039508, SR0-2 +0x400/450/4a0, DR0-3 +0x590/5e0/630/680
    mailbox      out[0] 0x40039010 H2D pend | out[1] 0x40039014 D2H pend | out[2] 0x400392d4 H2D doorbell
                 out[3] 0x40101438 D2H ack | out[4] 0x40101414 D2H rearm | out[5] 0x400392f0 (do not write)
                 D2H doorbell 0x40101434 | gate 0x4000010c (0xcece) | release 0x40000108 (0x5a5a)
    plat.ko      pcie_ete_init 0x7820 | pcie_ete_intr_init 0x7528 | pcie_ete_rings_init 0x7680
                 pcie_ete_sr_reg_init 0x14a48 | pcie_ete_dr_reg_init 0x1483c | pcie_ete_chn_enable 0x147dc
                 pcie_ete_sending_trigger 0x13f90 | pcie_ete_d2h_isr_handle 0x15c1c
                 pcie_msg_init 0xb6e4 | pcie_msg_handle 0x171f8 | pcie_msg_send 0x160f4
                 pcie_msg_send_irq 0x174a8 | pcie_msg_wait_for_clr 0x173dc | pcie_msg_register 0x15fbc
                 shuangta_ete_sr_dscr_fill 0x17858 | shuangta_ete_dr_dscr_fill 0x1765c
                 hcc_init 0x10df8 | hcc_msg_alloc 0x11d9c | hcc_msg_tx 0x11bf8 | hcc_msg_process 0x1204c
                 hcc_msg_register_tab_chip 0x11a3c | _core 0x11a94 | hcc_queue_register_customer_for_chip 0x124fc
                 multi_chip_loading 0xf2ac | plat_init_bal_hcc_excp 0xe380 | wlan_power_on 0xe46c
                 device_plat_ready_msg_process 0xeb78 | host_ready_msg_process 0xebd8
    wifi.ko      host_module_init 0x3e154 | hal_main_init 0x31d98 | hmac_main_init 0x3f6b8
                 wal_main_init 0x12386c | hdpp_main_init 0x12e0 | hmac_board_init 0x94b64
                 hmac_event_config_syn 0x6b0e0 | hmac_sync_dmac_alg_cfg_rsp_entry 0x87b58
                 hmac_sync_dmac_cali_cfg_rsp_entry 0x87f8c | hmac_config_send_event 0x57210
                 hmac_config_alg_send_event 0x60c44 | hmac_send_msg 0x46eb4
                 hmac_save_cali_data_to_file 0xc3e3c | _2g/_5g 0xc3d44/0xc3c4c
    FIRMWARE.bin dispatcher 0x818ac (H2D) | d2h notify 0x86170 | d2h flush 0x86108 | ready announce 0x86e14

## Appendix B — regeneration (local-only inputs; nothing vendor committed)

    PY=<repo>/.provision-venv/bin/python          # capstone 5.0.7 + pyelftools 0.33
    # function disassembly with relocations resolved:
    $PY opensource/lab/ko_disasm.py <ko> <func> [func ...]
    # call sites / callers / literal-pool + data-table decode (this lane's throwaway tools):
    #   cg.py calls|callers|show <ko> <funcs...>; brief.py; dumptables.py; scan.py; findref.py
    # D2H tables: registration call sites in hdpp_main_init/hmac_main_init/wal_main_init/
    #   plat_init_bal_hcc_excp plus .data/.rel.data (12-byte input records
    #   {u16 id; u16 pad; u32 fn; u32 aux})
    # firmware: capstone Thumb over FIRMWARE.bin at the quoted file offsets
