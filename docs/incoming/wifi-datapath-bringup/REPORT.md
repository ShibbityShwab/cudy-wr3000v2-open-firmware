# wifi-datapath-bringup - MAC data-plane host side: TXQ address sync + TX/RX rings

**Lane:** wifi-datapath-bringup (shared task task-111). **Host-only implementation,
compile-verified; NOT fired at the device.** 2026-11-25.

**Deliverables:** this file, FRAME-SPECS.md, BENCH-LADDER.md, BUILD-NOTES.md,
src/ (the clean driver copy + the new data-plane extension), tools/ (build.sh,
dp_logic_selftest.c, dp_static_decode.py, validate_sequence.py), sequences/
(ladder-d-datapath.json). Everything is inside `opensource/docs/incoming/wifi-datapath-bringup/`.

**Read-first inputs (committed):** ../wifi-datapath/REPORT.md + FRAME-SPECS.md +
BENCH-LADDER.md (the static RE lane this builds on), ../wifi-h2d-send/REPORT.md
(the working H2D control plane and the exact-frame carrier), ../wifi-ctx-create/
REPORT.md, ../../phase43/channel-register-offsets.md (slot-5 register map),
STATUS.md 3h-3k, and the newest ledger entries (2026-11-25 12:30/13:30/15:00).

**Evidence markers:** [V] decoded here from a named local artifact (vendor .ko /
FIRMWARE.bin; both read in place, never committed); [V*] device-measured by an
earlier attended lane and cited; [I] inference; [U] undecided.

**One-line summary:** the clean-room driver can now allocate and hand the MAC
host-interface data plane (host-DRAM MSDU-info rings, RX free rings, completion
ring, tx_ba_info, 0x2524-byte TXQ table) to the vendor firmware: a new one-shot
`dp_setup` programs MAC slot-5 band 0 with read-back verification and writes the
8 device-RAM MSDU-info table entries, then a new one-shot `dp_txq_sync` sends the
vendor's own g0 id1 key-0x0320 TXQ-table address sync frame through the already
working HCC H2D carrier.

---

## 0. TL;DR

1. **The four open [U] facts of the data-plane lane are now decoded from the
   vendor module, not guessed.** (a) The RX free rings are arrays of **4-byte
   device VAs**, one per buffer, and the entry is `buffer_dma + 4` (the vendor
   keeps a 4-byte host word before the DMA area); the rings are allocated
   0x400 B (256), 0x800 B (512) and 0x3ffc B (4095) [V]. (b) The per-TID TX
   MSDU-info ring depth default is **0x40 = 64** entries of 8 B [V]. (c) The
   device-RAM MSDU-info ring-pointer table has **12-byte entries** at
   `base + 12*(tid + user*8)`, user <= 0x91, and each entry is
   **{ring device VA, 0, 0}** at ring-init time [V]. (d) The firmware's
   key-0x0320 worker CA **0x81445** stores the 4-byte payload into firmware RAM
   at `(band array) + 0x252c*band + 0x2524` [V FW disasm].
2. **The ready g4 id2 payload semantic is implemented, not just cited.** The
   payload dword0 (bench: 0x01060850) is the PKTRAM device CA of that table;
   MAC +0x10 must receive the ACP alias (`+0x01000000` = 0x02060850, the live
   vendor value), and the host writes the per-ring entries through the same ACP
   alias [V]/[V*]. Every value is range-checked (PKTRAM 0x01000000..0x011dffff,
   ACP 0x02000000..0x021dffff, host DMA 0x80000000..0x8fffffff).
3. **The data plane is a separate DMA path from the ETE engine**, so the
   direct-read workaround for D2H messages does not apply to it: MAC rings are
   host-DRAM buffers the MAC/hdpp engine DMA-reads/writes; the host only
   publishes ring entries and programs slot 5 [I, unchanged from the static
   lane's working hypothesis].
4. **Implementation status:** the extension compiles clean against the project
   6.18.60 tree with **0 compiler/modpost warnings** and 13 new T symbols; host
   selftests are **69/69** (new data-plane logic) and **308/308** (inherited H2D
   control plane); the static-decode selftest re-derives **34/34** facts from
   the local vendor artifacts; the bench protocol sequence validates and renders
   **10/10** through the bench tooling's own validator. Module: 462,952 B sha256
   8fc693ad4c3c9338baa1089ecb2acd420be78eb059760bfc0fa356ade205cec9.
5. **Nothing was fired at the device by this lane.** The ladder (BENCH-LADDER.md)
   is staged: D0 boot golden -> D1 ctx-create -> D2 ONE read-only MAC probe
   (session A) -> D3 dp_setup (session B) -> D4 ONE key-0x0320 frame -> packs.
   Hard stop rules are unchanged.

---

## 1. Scope, method, inputs

| input | what was used |
| --- | --- |
| hi5622v100_wifi.ko 2.4.15 md5 1745ef77... (local) | symbol boundaries, call graph, constants of the TX/RX data-path init path (tools/dp_static_decode.py) |
| FIRMWARE.bin sha256 7fc87e20...311b, 928,920 B (local) | group-0 id-1 key-table workers CA 0x81445 (0x0320) and 0x81039 (0x00d8) |
| committed lane reports | wifi-datapath (ring/register/TXQ facts), wifi-h2d-send (carrier, exact builders, gates), phase43 channel-register-offsets |
| working bench state | STATUS 3h-3k + ledger 2026-11-25: golden boot, ready pair, ctx-create, TXQ sync 0xd8, calibration answer g1 id3 |

Method: pyelftools + capstone on the local ELF module (ARM) and the firmware
image (Thumb), relocations resolved to call targets; then the decoded constants
are re-asserted by tools/dp_static_decode.py (--selftest, 34 checks). No vendor
byte, image content, calibration value or per-unit identity is committed; the
decode tool prints booleans/counts only.

Reproduce:

    cd opensource/docs/incoming/wifi-datapath-bringup
    ./tools/build.sh                       # host tests + kernel build + static decode + sequence validation
    /path/to/.provision-venv/bin/python tools/dp_static_decode.py --selftest

---

## 2. What was decoded (the facts the implementation needs)

### 2.1 RX free rings are 4-byte device-VA arrays [V]

`shuangta_rx_host_init_dscr_queue` (wifi.ko 0x37888):

| ring | alloc | depth field | entry | register |
| --- | ---: | ---: | ---: | --- |
| rx_norm_data_free_ring | 0x400 B | 0x100 (256) | 4 B | +0x20 addr / +0x2c size |
| rx_small_data_free_ring | 0x800 B | 0x200 (512) | 4 B | +0x24 / +0x30 |
| rx_data_cmp_ring | 0x3ffc B | 0xfff (4095) | 4 B | +0x28 / +0x34 |

Buffer lengths +0x1c = norm[15:0] 0x63c / small[31:16] 0xb0. The ring is filled
by `shuangta_host_rx_add_buff`: per buffer it allocates an skb, aligns the data
pointer, writes its own skb pointer at buffer+0, zeroes the next 0x24 B, maps
the buffer and pushes `pcie_if_hostca_to_devva(data + 0x4)` into the ring; every
fill ends with dmb/dsb+sev and the wptr advance. So:

    ring entry (4 B) = device VA of the DMA area = buffer + 4
    buffer DMA area  = 0x63c (norm) / 0xb0 (small) bytes starting at buffer+4
    host owns bytes 0..3 (bookkeeping word) of every buffer

After a full fill of each ring the packed producer index is index 0 / phase 1
(0x8000), exactly the vendor live values [V*].

### 2.2 TX MSDU-info ring depth = 64 [V]

`hmac_tx_msdu_dscr_init` (0x18654) zeroes all per-TID ring slots
(dev+0xadc+tid*0x58) and stores 0x40 into slot+0x38 (the depth default).
`hmac_tx_msdu_ring_init` (0x18d7c) then allocates `depth*8` bytes per TID via
`shuangta_tx_msdu_ring_buf_alloc`, zeroes it and records {host ptr, DMA addr,
published index, in-ring flag}. Ring index = packed 15-bit index + phase bit 15
(`hmac_tx_msdu_ring_rw_index_check`) and is published with dmb/dsb+sev [V].

### 2.3 The device-RAM MSDU-info ring-pointer table [V]

`hmac_tx_msdu_info_ring_table_addr_init` (0x184f0) is the vendor chain behind
MAC +0x10: `wlan_get_d2h_read_msg(chip)` returns the host record whose dword0 is
the g4 id2 payload (bench 0x01060850), it converts that device CA with
`memmap_get_dev_acp_addr` and writes the ACP alias into
`shuangta_set_msdu_info_ring_ptr_table_base` for band 0 then band 1.

`hmac_get_user_tid_tx_msdu_info_ring_table_addr` (0x183a0) computes
`entry = table_base + 0xc * (tid + user*8)` (user <= 0x91), resolves it with
`oal_pcie_devca_to_hostva` and `shuangta_tx_msdu_ring_buf_alloc` writes the
12-byte entry `{ring device VA, 0, 0}` at ring-init time. This is the missing
piece: the data rings are handed to the firmware by writing this table, not by
an HCC message.

### 2.4 The TXQ table and its address sync [V]

`hmac_txq_table_init` (0x1b0b4) allocates a zeroed 0x2524-byte non-cached host
table, calls `shuangta_ba_info_res_alloc` (tx_ba_info: 0x800 B, depth 0x100,
MAC +0x00/+0x04), then the MSDU-table-base init, then
`hmac_txq_table_addr_sync` (0x1af00) = `hdpp_config_send_event(vap, 0x320, 4,
&dev->txq_table_dma)`. The frame is group 0 id 1 key 0x0320, 4-byte payload; the
firmware worker CA 0x81445 (Thumb, FIRMWARE.bin CPU 0x81444) does
`*(u32 *)(band_base + 0x252c*band + 0x2524) = payload[0]` [V FW]. The per-user
blocks of the table (hmac_update_user_tid_txq_table, 0x1b294) live at
+0x524 + user*0x40: 8x u16 queue length, 8x u16 start sn, 8x u32 bytes; the
table is zero at first sync, which is what the vendor sends too.

### 2.5 The MAC slot-5 program (unchanged values) [V]/[V*]

From phase43 S5 + the vendor accessors: +0x10 MSDU table ACP, +0x1c buffer
lengths, +0x20/+0x24/+0x28 ring addresses, +0x2c/+0x30/+0x34 sizes (bits 11:0),
+0x00/+0x04 tx_ba depth/addr, +0x48 mask 0xfffffcc1, +0x38/+0x3c/+0x40 packed
indices (0x8000/0x8000/0 after a full fill), +0x4c/+0x50/+0x54 device-side
counters. Band 0 CA 0x40040000; band 1 CA 0x40060000 is NOT touched by this lane.

---

## 3. What was implemented

### 3.1 Portable logic (`src/dp_rings.h` / `src/dp_rings.c`)

* ring geometry + buffer layout constants (all `2.x` facts);
* `dp_mac_program()` - the 14-write MAC program in vendor order with RMW masks,
  value range checks (depths, lengths, host-DMA/ACP windows) and refusal codes;
* `dp_ready_payload_ptrtable()` - validates the g4 id2 payload and computes the
  ACP alias;
* `dp_msdu_table_entry_ca()/`dp_msdu_table_entry_encode()` - the 12-byte,
  `tid + user*8` table arithmetic and entry image;
* packed MAC-ring index arithmetic (15-bit index + phase bit), `dp_tx_ring_devva()`
  and `dp_rx_buf_entry_devva()`.

### 3.2 Kernel extension (`src/hi5622_dp.c` + `hi5622_dp.h`)

New sysfs group `dp`, registered from main.c, state allocated at probe (no
device access, no rings):

| attr | mode | action |
| --- | --- | --- |
| dp_probe | RO | ONE read-only word read of MAC band0 +0x00 (the B2a reachability probe). Sets probe_ok on success; no write. |
| dp_setup | WO (write "1") | allocate rings/tables (coherent DMA, host window checked), fill RX free rings completely, write the 8 device-RAM MSDU-info entries through ACP with read-back, then the 14-register MAC program with per-write read-back. One shot; a failure marks the lane failed (no retry). |
| dp_txq_sync | WO (write "1") | ONE g0 id1 key-0x0320 frame with the TXQ-table device VA through the inherited exact H2D carrier (frame build/validate -> gate -> SR node -> mailbox id-3 announce -> bounded out[0] poll). |
| h2d_txq_addr_sync | WO (write "1") | bench-tooling compatibility alias of dp_txq_sync (the bench lane already allowlists this name). |
| dp_status | RO | counters, gates, device VAs, ready payload, MAC read-back snapshot, stop-rule reminder. |
| dp_rings | RO | live MAC band-0 register snapshot + device-RAM table entries + ring indices (reads only). |

### 3.3 The exact sequence and its gates (as coded)

    0. probe (no device write)
    1. prepare / upload / verify / seed / hcc_arm / hcc_loop / release   [existing]
    2. ready pair g4 id1 + g4 id2 (firmware -> host)                    [existing]
    3. h2d_ctx_create (g0 id1 key 0x64)                                 [existing]
    4. dp_probe   : ONE read of CA 0x40040000 (must not bus-error)
    5. dp_setup   : gate = windows + armed + released + latched +
                    phase READY_TXQ_SYNC/ACTIVE + this load's ctx_create +
                    probe_ok + not failed/setup.  Then:
                    a) resolve ready id2 payload -> PKTRAM CA/ACP
                    b) dma_alloc_coherent: TXQ table 0x2524, 8x64x8 TX rings,
                       tx_ba_info 0x800, RX rings 0x400/0x800/0x3ffc, and
                       256+512 coherent RX buffers
                    c) write 8 MSDU-table entries {ring devVA,0,0} via ACP,
                       read back each
                    d) fill both RX free rings (entry = buf dma + 4), dma_wmb
                    e) execute the 14 MAC writes in order, RMW where masked,
                       read back and compare every masked field (first mismatch
                       stops the program and closes the lane)
    6. dp_txq_sync: gate = setup_done + not failed + not sent + h2d_send_budget
                    + this load's ctx_create.  ONE 32-byte frame, node length 32,
                    out[0] must be consumed within the bounded poll.

Safety invariants carried over: probe (and dp_setup) never run before the SR
rings are armed and the CPU released; only MAC band 0 and one device-RAM table
are written; CA 0x400392f0, the RC misc window, the release word and the gate
word are untouched; a register read-back mismatch or a DMA allocation failure
closes the lane with no retry until reload.

---

## 4. What remains [U] / risks (honest list)

1. **MAC slot-5 reachability through the region-3 window is still unmeasured.**
   A previous lane recorded a bus error reading BAR0 0x402a004 while the declared
   window covers it; this is why dp_probe exists as a separate one-shot session
   and why dp_setup refuses without probe_ok. If the probe bus-errors, stop - do
   not add an iATU viewport.
2. **Table entries dwords 1/2 are written as 0** (matching the vendor's
   ring-init values [V]); their later use (likely sync indices) is not decoded.
3. **user index 0 is assumed for the first rings.** The vendor's per-band user
   id (`band+0xa`) is set by the user-add path (g0 id7/8), which is not part of
   this lane. With no user/VAP added, the firmware is not expected to consume
   the rings or deliver RX completions in this first ladder - the ladder gates
   on "no exception + registers verified + announcement consumed", not on
   traffic.
4. **TXQ table content is zero at the first sync** (vendor does the same at
   hmac_txq_table_init); the per-user counters are only updated when a user
   queues frames (`hmac_update_user_tid_txq_table`), which requires a user.
5. **Completion-ring entry format and the MAC interrupt path remain [U].**
   The ring is allocated/programmed (4095 x 4 B) but not consumed by this lane;
   the MAC interrupt mask is written to the vendor value while no IRQ line is
   assumed (poll via dp_status/dp_rings).
6. **Band 1 is not programmed** (one band per ladder, stop rules).
7. **No frame has ever traversed these rings** - the TX 8-byte entry encoder and
   the 20-byte DMA descriptor are still the static lane's spec; this lane only
   hands the empty rings over.

---

## 5. Expected bench observables (summary; details in BENCH-LADDER.md)

| step | expected | negative / stop |
| --- | --- | --- |
| dp_probe | one word read, attr prints value (vendor boot 0x100; 0/other recorded) | bus error/hang/reboot -> stop, no writes, report exposure |
| dp_setup | dp_status: `setup_done=1 failed=0 mac_regs=14 table_entries=8`; dp_rings: `+0x38/+0x3c = 0x8000`, `+0x48 = 0xfffffcc1`; mask CA 0x400008 still 0, staging CA 0x40000c still 0 | any read-back mismatch, exception mask change, reboot -> stop lane for the boot |
| dp_txq_sync | h2d_status `txq_addr_sent=1 ok=2 timeout=0`; out[0] 0x40039010 consumed -> 0; page/state golden, mask 0 | announce not consumed or new exception -> no retry |
| catalogue after | D2H catalogue delta captured (the 0x320 worker has no known D2H answer) | unexpected exception info frame -> record, power-cycle |

---

## 6. Evidence index

| claim | evidence |
| --- | --- |
| ready id2 payload = device-RAM MSDU-info table pointer; MAC +0x10 = ACP alias | wifi-datapath REPORT 2.1/3; hmac_tx_msdu_info_ring_table_addr_init disassembly; live 0x40040010 = 0x02060850 [V]/[V*]; tools/dp_static_decode.py |
| 12-byte table entries at base+12*(tid+user*8), {devva,0,0} | hmac_get_user_tid_tx_msdu_info_ring_table_addr + shuangta_tx_msdu_ring_buf_alloc [V] (static decode checks) |
| RX free rings 256/512 x 4 B, completion 4095 x 4 B, buffer 0x63c/0xb0, entry = buffer+4 | shuangta_rx_host_init_dscr_queue + shuangta_host_rx_add_buff [V] (static decode checks) |
| TX MSDU ring depth 64, 8 B entries, packed index | hmac_tx_msdu_dscr_init / ring_init / rw_index_check [V]; wifi-datapath 6.2/6.3 |
| TXQ table 0x2524 + key-0x0320 4-byte devVA sync | hmac_txq_table_init / hmac_txq_table_addr_sync [V]; wifi-datapath 5.1 |
| firmware 0x0320 worker CA 0x81445 stores payload at fw RAM +0x2524+0x252c*band | FIRMWARE.bin disassembly, checked by dp_static_decode.py [V] |
| one-shot H2D carrier/gate/budget semantics | ../wifi-h2d-send REPORT + 308/308 host selftest (this lane's copy) |
| register program order/values/masks | phase43 channel-register-offsets S5; vendor hal_set_* accessors [V]/[V*]; dp_rings selftest 69/69 |

---

## 7. License / publication

GPL-2.0 (kernel-linking license) for all C code; docs are original writing. No
vendor binaries, firmware bytes, calibration data, object code or per-unit
values are committed: the vendor artifacts are read in place by the local
decode tool, which prints only derived constants/booleans. This lane is a
bench-reversal instrument, not an upstream submission artifact; the Wi-Fi
firmware remains closed (see STATUS.md section 5).
