# wifi-dp-tuning: device TX-consumer enablement + RX, compared against the vendor

**Lane:** wifi-dp-tuning (shared task task-134), **host-only**, 2026-11-27.
**Base:** opensource/docs/incoming/wifi-dp-air/src (merged provider + data
plane, task-126) + the wifi-ap-start v7 provider patch (the module lineage that
was actually fired as the 242,572 B combined artifact 44c6cc12).
**Artifact:** hi5622_wifi.ko **251,924 B** sha256
**175a4617f208ac7fb67cbd9074cb4eac18876ebf973d905d650f185eed7cc425**
(debug-stripped, path-independent; unstripped 947,432 B sha256 dc35e038...).
**Not fired at the device.** The attended ladder is ladder/LADDER-G-TXQ.md and
the machine form is ladder/bringup-g-txq-consumer.json (33 steps, validated).

Evidence markers: [V] decoded from a named vendor artifact (the local
hi5622v100_wifi.ko 2.4.15 md5 1745ef77 / 2.5.24 md5 4737fcb2, or the pinned
FIRMWARE.bin sha256 7fc87e20); [V*] device-measured by an earlier attended
lane and cited in the project reports; [I] inference; [U] unknown.

---

## 1. Executive summary

The data-plane host stack publishes a frame to the MAC TX MSDU-info ring and
updates the device-RAM table producer index, but the device consumer never
advances rptr (POST-ONLY, first seen 2026-11-27 02:30 and again on the combined
AP module at 10:30). This lane compared the whole host-side TX path against
the vendor driver instead of treating the MAC register program as the only
suspect.

**Finding.** Our 14-register MAC band-0 program is complete and correct
against the vendor init chain - every register the vendor host writes at
data-plane init is one of those 14, in the same order, with the same values
(section 3). The difference is one host-side step the earlier lanes never
implemented: after pushing a ring entry, the vendor also **publishes per-TID
queue state into the 0x2524-byte host-DRAM TXQ table** (timestamp, queued
MSDU count, start sequence number, queued bytes, and a per-user TID-non-empty
bitmap) via hmac_update_user_tid_txq_table. That table is exactly the table
whose device VA the driver hands to the firmware with the g0 id1 key-0x0320
sync; the previous clean-room driver sent the address and left the table
zero - so the firmware's queue view said "nothing queued" no matter how many
ring entries were published.

**Implemented tuning** (5 files, patches/0001, 27,265 B):
1. TXQ-table queue-state publish after every TX post, in the vendor's store
   order and with the vendor's byte layout, including the per-user non-empty
   bitmap (new portable code + kernel glue, host-tested);
2. per-TID packed producer indices (the vendor keeps one wptr per (user,tid);
   the old driver advanced a single wptr for all TIDs);
3. a bounded, once-per-load **dp_tx_complete_poll** that settles
   COMPLETED (MAC) vs POST-ONLY after tx_complete_ms instead of reading the
   completion word immediately after the post;
4. read-only evidence attributes: **dp_txq_table** (table + software state)
   and **dp_mac_ext** (the unprogrammed role-5 tail 0x58..0xac);
5. an opt-in, default-off attended experiment **dp_mac_tail_init** for the
   two vendor-live tail words (0x5c/0x6c = 4) whose writer was not found in
   the shipped host modules - recorded as [U], never used by the ladder.

**Verification.** 16/16 build-script checks PASS: patch round-trips to the
committed src/ files, 37/37 TXQ-layout host checks, 254/254 inherited portable
TX/RX regression, 0 compiler/modpost warnings, vermagic 6.18.60, attribute and
parameter contract present, ladder sequence 16/16 validated. No device was
contacted; a COMPLETED TX and any RX capture remain to be settled by the
attended ladder (section 8).

---

## 2. Method and sources

Local, read-only, in place (nothing vendor is copied into the repository):

| source | identity | used for |
| --- | --- | --- |
| hi5622v100_wifi.ko 2.4.15 (teardown) | md5 1745ef77e728c411cef1921af3dc8731 | symbol+relocation disassembly (capstone, ARM) of the entire host TX/RX data path |
| hi5622v100_wifi.ko 2.5.24 (stock) | md5 4737fcb21a1a2262a96f84d780ad8b35 | cross-build check of every cited function (same CA/size/ops, section 4.5) |
| FIRMWARE.bin | sha256 7fc87e20..., 928,920 B | g0 id1 key tables + workers, Thumb disassembly (CPU = file + 0x40000) |
| opensource/dumps/reg_all_run2.txt | live vendor register dump (25,141 words) | role-5 live values incl. the 0x58..0xac tail |
| opensource/docs/phase43/channel-register-offsets.md | S5 | role-5 offset/accessor map (cross-check) |
| wifi-datapath, wifi-dp-txrx, wifi-dp-air, wifi-datapath-bringup, wifi-ap-start lanes | project reports | what the existing host implementation does and the bench records behind it |

The tools used are throwaway host scripts: pyelftools + capstone for the
vendor modules and the firmware. Only derived constants, offsets and facts are
committed here; no vendor byte, object code or firmware image material is.

---

## 3. Our MAC band-0 program vs the vendor (task item 1a)

### 3.1 What the host writes (all evidence [V] from the vendor accessors)

MAC slot 5 CA 0x40040000 (band 0) is a 0xb0-byte host-interface block. The
vendor host module owns exactly these writes; our dp_setup programs the same
14 words in the same vendor init order (dp_rings.c table):

| off | field | vendor writer (caller) | our program |
| --- | --- | --- | --- |
| +0x00 | tx_ba_info_buff_depth (bits 15:0) = 0x100 | hal_set_tx_ba_info_buf_depth (shuangta_ba_info_res_alloc) | yes |
| +0x04 | tx_ba_info_buf_addr = host-DRAM devVA | hal_set_tx_ba_info_buf_addr (same) | yes |
| +0x10 | msdu_info_ring_ptr_table_base = ACP(ready g4 id2 payload) | hal_set_msdu_info_ring_ptr_table_base (hmac_tx_msdu_info_ring_table_addr_init) | yes |
| +0x1c | rx_norm_len[15:0]=0x63c | small_len[31:16]=0xb0 | two hal_set calls (shuangta_rx_host_init_dscr_queue) | yes (one word) |
| +0x20 | rx_norm free ring addr | hal_set_..._addr | yes |
| +0x24 | rx_small free ring addr | hal_set_..._addr | yes |
| +0x28 | rx_data_cmp ring addr | hal_set_..._addr | yes |
| +0x2c | rx_norm ring size 0x100 | hal_set_..._size | yes |
| +0x30 | rx_small ring size 0x200 | hal_set_..._size | yes |
| +0x34 | rx_data_cmp ring size 0xfff | hal_set_..._size | yes |
| +0x38 | rx_norm free wptr packed (0x8000 after fill) | hal_set_..._wptr | yes |
| +0x3c | rx_small free wptr packed (0x8000) | hal_set_..._wptr | yes |
| +0x40 | rx_data_cmp rptr = 0 | hal_set_..._rptr | yes |
| +0x48 | host MAC interrupt mask = 0xfffffcc1 | hal_set_host_intr_mask (shuangta_host_chip_irq_init) | yes |
| +0x44 | host MAC interrupt status (W1C) | ISR only | not written (read-only evidence) |
| +0x4c | rx_data_cmp wptr | device producer; vendor init writes 0 | not written (0 after reset) |
| +0x50/+0x54 | rx_norm/rx_small free rptr | device consumer; vendor init writes 0 | not written |

**Conclusion:** there is no missing MAC init register in 0x00..0x54. The live
vendor dump values are reproduced by our program for every host-written word
(including +0x38/+0x3c = 0x8000 and +0x48 = 0xfffffcc1), which is also what
the E1 bench read-backs showed [V*].

### 3.2 The unprogrammed tail 0x58..0xac (new evidence)

The role-5 window is 0xb0 bytes long, but the vendor host module has no
accessor or direct writer for 0x58..0xac: enumerating all 60 hal_set_*
accessors gives no store above +0x54, and no shuangta_*/hmac_* function with a
role-5 base writes that range. Yet the live vendor dump has two non-zero words
there [V*]:

    0x40040058 = 0x00000000   0x4004005c = 0x00000004
    0x4004006c = 0x00000004   everything else 0x58..0xac = 0

These two 4s exist on both bands (0x4006005c/6c = 4) while the vendor stock
radios are up. They are therefore firmware/MAC-written state, not a host init
step missing from our program [V*/no host writer] - and the earlier
combined fire (AP-ENABLED and radio up) still did not consume the ring, so a
host write to them is not required for the *host-side* contract. Because they
are visible and the first fire never read back the tail, the lane adds
dp_mac_ext (read-only) to record our tuned module's values on the next
attended boot, plus the opt-in dp_mac_tail_init experiment ([U], default off,
mac_tail_writes=1, writes exactly 4 to both with read-back).

### 3.3 The other suspects named in the task

| candidate | evidence | verdict |
| --- | --- | --- |
| TXQ table producer/consumer config | vendor uses the 12-byte device-RAM table entry {ring devva, wptr, rptr}; our publish writes the same dword1 through the ACP alias with dmb order | already implemented in dp-air; the table *behind* it (TXQ table) was not - see 4 |
| queue doorbell / per-frame MAC write | the vendor push path performs NO MAC register write per frame (only ring entry + device-RAM table dword1 + the TXQ table update); no doorbell register exists in the shipped host path | ruled out as the missing host step [V] |
| BA/aggregation init tx_ba_info 0x800/0x100 | vendor writes only depth 0x100 and the buffer devVA (as ours); the buffer content is device-written completion state | ruled out as a difference [V] |
| MAC +0x40..+0x54 words | completion ring rptr / wptr / free-ring rptrs; all zero at init on both sides, then device-owned | ruled out as a difference [V] |
| port/channel setup | not in the role-5 block; provided by the AP/scan path. The 10:30 combined fire had hostapd AP-ENABLED + a started firmware VAP and still got POST-ONLY | not the host difference; still [V*] evidence that the radio was up |
| firmware-side start message | the firmware SMAC handler-name table contains smac_msg_proc_enable_trx_req / tx_resume_req / clr_hw_fifo_req [V]; no shipped host module sends a trx-enable key (every h2d/hmac key in both builds resolves in firmware table A, and none is a trx enable) | remains an open [U] candidate for a future lane; not reproducible from the shipped host code |
| TXQ-table queue state | see section 4 | **the actionable difference; implemented here** |

---

## 4. The vendor TX enqueue tail: the missing consumer notification

### 4.1 The complete vendor enqueue sequence [V]

Disassembly of the shipped host module (2.4.15, symbol sizes; 2.5.24
byte-identical for every function below):

* hmac_tx_msdu_inqueue / hmac_tx_skb_pop_tid @0x19378 (296 B) - per-TID
  pending-queue pop loop:
    * skb_dequeue from the TID queue, then hmac_tx_msdu_ring_push @0x18f18
      (1,120 B) for each;
    * after the loop it calls **hmac_update_user_tid_txq_table @0x1b294
      (568 B)** - once per popped batch (call site 0x19444).
* hmac_tx_msdu_ring_push @0x18f18 (1,120 B):
    * builds the 8-byte entry {buf_devva, ctl} into the (user,tid) host ring;
    * dmb ish;
    * publishes the next packed producer index into the device-RAM 12-byte
      table entry dword1 (precomputed host VA of that dword, section 5.2);
    * increments the per-(user,tid) counters at slot+0xaec (MSDU count) and
      slot+0xaf0 (bytes), sets the in-ring flag +0xb2a;
    * dmb ish; dsb ishst; sev.
* hmac_update_user_tid_txq_table @0x1b294 (568 B): the queue-state publish
  into the TXQ table (host DRAM, the table address given to the firmware by
  key 0x0320). Store order decoded from the instruction stream:

    table pointer = chip->[0x928]        (set by hmac_txq_table_init)
    user = per-user-object->[0xa] (u16)
    block = table + 0x524 + user*0x40    (movw #0x520 + add #4)
    table[0..1] = u16 jiffies_to_msecs(jiffies)      ; publish clock
    block[tid*2]        = u16 queued_msdu_count      ; per-TID queue length
    block[0x10 + tid*2] = u16 start_sequence_number  ; if pending skb exists
    block[0x20 + tid*4] = u32 queued_bytes
    table[0x10 + user]  = bitmap, bit tid = (queued_msdu_count != 0)
    users > 0x7f use an alternate small block (user*0x40 - 0x1f40 - 0x1c);
    the clean-room host owns user 0 only and refuses the rest [V].

The counts/bytes written here are the same counters the ring push maintains;
in the vendor they are read from the per-(user,tid) slot (push increments
them before the update call). Our tuned code keeps the same counters in
software and publishes them with the vendor layout.

### 4.2 Why this is the consumer notification [I, strongly supported]

* the table is the object whose device VA the driver hands to the firmware by
  the g0 id1 key-0x0320 sync; hmac_txq_table_addr_sync refuses to send it
  until a firmware VAP object exists [V];
* the firmware's key tables decode 0x00d8 -> worker 0x81039 ("TXQ config
  sync": data[0] -> ctx+0x190+0x4c) and 0x0320 -> worker 0x81444 (stores the
  table devVA into firmware RAM at band_base + 0x2524) [V from FIRMWARE.bin];
  the working bring-up ladder sends 0xd8 with data[0]=1 (h2d_txq_sync72) and
  the 0x320 sync, so the consumer context is enabled and has the address;
* the only remaining host-side state a consumer can poll is the table content,
  and the pre-tuning driver left it zero (all bytes, including the bitmap);
* the previous POST-ONLY runs had a started VAP/radio and a published ring
  wptr, so "ring entry missing / not visible" is excluded [V*].
**Still [I]:** no firmware consumer disassembly proves which table fields it
reads; the bench ladder in section 8 is the decisive test.

### 4.3 What the previous lanes did (and why POST-ONLY was the documented outcome)

wifi-dp-txrx / wifi-dp-air implemented: ring entry, dmb, device-RAM table
dword1 publish, read-back, and an immediate completion_pending snapshot. They
did not call any TXQ-table update, did not keep per-TID producer indices, and
evaluated completion microseconds after the post - all three are addressed
here.

### 4.4 Cross-build check

Every cited vendor function (hmac_txq_table_init 0x1b0b4, hmac_txq_table_addr_sync
0x1af00, hmac_tx_msdu_ring_push 0x18f18, hmac_tx_skb_pop_tid 0x19378,
hmac_update_user_tid_txq_table 0x1b294, shuangta_set_tx_msdu_info_ring
0x0389dc, hal_set_* role-5 accessors, shuangta_host_rx_add_buff 0x374d0) has
the same CA, size and instruction sequence in the 2.4.15 teardown build and
the 2.5.24 stock build (md5s above). Both builds call the update after the
push loop (relocation target identical).

### 4.5 RX side (task item 3b)

No host-side RX difference was found in the same comparison:

* free-ring entry = devVA(buffer + 4): shuangta_host_rx_add_buff maps
  (skb_data + 0xC0600004) through pcie_if_hostca_to_devva [V] - matches
  dp_fill_rx_rings (dp_rx_buf_entry_devva = dma + 4);
* norm/small buffer sizes 0x63c/0xb0, depths 256/512, completion depth 4095,
  cmp wptr/rptr ownership - all match the vendor init [V]/[V*];
* the completion-word format and the descriptor field map stay [U]; the
  existing capture-only dp_rx_poll is unchanged and the tuned ladder keeps it
  read-only. If the TX consumer now moves, a same-boot RX capture becomes
  meaningful (ACK/beacon/probe traffic); 0 captures stays NO-RX, recorded.

---

## 5. The tuning: module patch (task item 2)

patches/0001-txq-consumer-tuning.patch (27,265 B sha256 22579b74...) applies
to the combined tree (wifi-dp-air/src + ap-start v7 + ap frames); five files:

| file | change |
| --- | --- |
| dp_txrx.h / dp_txrx.c | new portable section 7/8: TXQ table constants (0x2524, block 0x524+user*0x40, count/sn/bytes/bitmap offsets), dp_txrx_txq_user_block, dp_txrx_txq_flags_set, dp_txrx_txq_publish (byte stores, bounds/refusals, no alignment assumptions) |
| hi5622_dp_txrx.h | bind snapshot carries txba (0x800 B host-DRAM BA-info buffer) for completion evidence; four new handler prototypes |
| hi5622_dp_txrx.c | keeps the txq_table pointer; per-TID packed wptr/count/bytes/sn arrays; publishes the TXQ state after every post (dma_wmb before/after; vendor order: ring entry -> wptr -> TXQ state); new attributes dp_txq_table / dp_tx_complete_poll / dp_mac_ext / dp_mac_tail_init; module params txq_publish (1), tx_complete_ms (2000), mac_tail_writes (0) |
| hi5622_dp.c | passes txq_table + txba in the bind snapshot; registers the four attributes in the dp group |

Behavioural deltas for the next boot:

* dp_tx_frame now also publishes the queue state the vendor's enqueue tail
  publishes (the frame count/bytes per TID and the user-0 non-empty bitmap);
* each TID gets its own producer index (the old single index was only correct
  while every frame used TID 0);
* dp_tx_complete_poll is the new, bounded, once-per-load completion read:
  polls the table entry up to tx_complete_ms, then reports
  COMPLETED (MAC) when device rptr == published wptr, else POST-ONLY, with
  table image + MAC +0x44/+0x4c + first 16 tx_ba_info bytes;
* dp_txq_table records the table image and the software mirror; dp_mac_ext
  records the role-5 tail (vendor comparison 0x5c/0x6c=4);
* everything still fails closed: one setup per load, one 0x320, post budget 8,
  one completion poll, no retry, no writes outside MAC band 0, the opt-in tail
  experiment refuses unless mac_tail_writes=1.

Queue/BA/buffer values compared (task item 2): TX ring 8 x 64 x 8 B, tx_ba_info
0x800 B / depth 0x100, RX norm 256 x (0x63c+4), RX small 512 x (0xb0+4), RX
completion 4095 x 4, TXQ table 0x2524 B - all vendor values, all unchanged;
interrupt/coalescing (hcc_poll_ms 5, allow_frames/frame_gap_ms) unchanged; the
only new timing knob is tx_complete_ms for the bounded poll.

---

## 6. Verification (host-only, no device)

tools/build.sh (16 checks, all PASS on the authoring host):

| check | result |
| --- | --- |
| combined base + ap-start v7 + patch apply | PASS |
| 5 changed files reproduce byte-for-byte from patches/0001 | PASS |
| patch scope exactly 5 files | PASS |
| TXQ-table layout selftest (tools/txq_selftest.c) | 37/37 PASS |
| inherited portable TX/RX regression (wifi-dp-txrx) | 254/254 PASS |
| kernel module build, 0 compiler/modpost warnings | PASS |
| vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, depends=cfg80211 | PASS |
| TXQ-publish + completion-poll symbols present | PASS |
| new attribute surface present (4 attrs) | PASS |
| module parameters recorded | PASS |
| debug strip leaves .text byte-identical | PASS |
| G-TXQ ladder sequence validated (16/16) | PASS |
| no object/binary artifact in the lane | PASS |
| SPDX headers on all C/H sources | PASS |
| no firmware/vendor blob in the lane | PASS |

Host test design: txq_selftest.c pins the table layout with LITERAL offsets
(0x524, 0x40, 0x10, 0x20, u16 count/sn, u32 bytes, bitmap byte) so a macro
regression cannot make the driver and the test agree on a wrong layout; it
also checks the refusal paths leave the buffer untouched, the last legal user
block ends exactly at 0x2524, and the packed-index wrap used for per-TID
producers.

Artifact identity (record with sha256sum/modinfo on the box before insmod):

    hi5622_wifi.ko (debug-stripped, path-independent)
    size   251924 B
    sha256 175a4617f208ac7fb67cbd9074cb4eac18876ebf973d905d650f185eed7cc425
    unstripped 947432 B sha256 dc35e03802aa44a49ff3abb9659b289059262598532876fe50df8b357bb114e5

The .ko is local evidence only (never committed); the inherited combined
module it is based on was 242,572 B sha256 44c6cc12 (task-121).

---

## 7. Ladder G-TXQ (attended; not fired by this lane)

Full procedure: ladder/LADDER-G-TXQ.md. Machine form:
ladder/bringup-g-txq-consumer.json (33 steps, 366 rendered lines, validated).
Prefix = the proven F-AIR flow (boot -> ctx-create t1 -> txq_sync72 -> iface +
one scan as the firmware VAP/channel precondition -> dp_probe -> dp_setup ->
0x0320). New rungs, in order:

| # | action | expected |
| --- | --- | --- |
| G1 | cat dp_mac_ext | read-only role-5 tail; record +0x5c/+0x6c (vendor 4/4) |
| G2 | cat dp_txq_table | publications=0, user0_flags=0x00 before the first TX |
| G3 | echo 1 > dp_tx_frame | posted=1 + dmesg "DP txq: published user=0 tid=0 count=1 ..." |
| G4 | cat dp_tx_status | posted=1, completion_pending (snapshot), TXQ publish publications=1 |
| G5 | cat dp_txq_table | user0_flags=0x01, tid 0 table{count=1 ...} |
| G6 | echo 1 > dp_tx_complete_poll | dmesg COMPLETED (MAC) (rptr advanced) or POST-ONLY; verdict readable in dp_tx_status afterwards |
| G7 | echo 1 > dp_rx_poll; cat dp_rx_capture | read-only capture, 0 captures = NO-RX recorded |
| G8 | post packs | mask 0, staging 0, page 0x86807FEF, state 1 |

Verdicts: COMPLETED (MAC) = the TXQ publish is the consumer notification (or
at least sufficient); POST-ONLY = the hypothesis is falsified - then the
remaining candidates in ranked order are the MAC tail 0x5c/0x6c experiment
(mac_tail_writes=1, one boot), the firmware trx-enable message family [U],
and deeper PIE/EQM port setup [U]. A broadcast probe request has no ACK; an
on-air claim still needs an external observer.

---

## 8. What remains [U] and residual risk

1. **Firmware TX consumer semantics** [U]: no shipped-artifact disassembly
   shows which TXQ-table fields the consumer reads; the ladder decides.
2. **MAC tail 0x5c/0x6c** [U]: vendor-live 4/4 on both bands, no host writer;
   firmware/MAC-owned presumed. dp_mac_tail_init is an opt-in experiment with
   read-back; default off because the meaning is unknown.
3. **SMAC trx-enable message family** [U]: smac_msg_proc_enable_trx_req,
   tx_resume_req, clr_hw_fifo_req exist in the firmware handler-name table but
   no shipped host sender was found; a future lane could map the firmware key
   table to reach them.
4. **RX completion word / descriptor** [U]: unchanged capture-only; needs a
   bench capture once something is received.
5. **Firing risk**: the tuning adds host writes only to host-DRAM structures
   already handed to the firmware (TXQ table) plus one bounded poll; no new
   MMIO write unless mac_tail_writes=1. Fail-closed gates unchanged.

## 9. Reproduction

    cd opensource/docs/incoming/wifi-dp-tuning && ./tools/build.sh

This stages the combined base, applies patches/0001, re-runs the host tests,
builds the module and validates the ladder. Kernel tree: the project 6.18.60
OpenWrt build tree (override with KDIR_OVERRIDE/STAGING_OVERRIDE).

## 10. Publication boundary

Original clean-room code (GPL-2.0 SPDX), original writing, derived constants
and offsets only. No vendor binary, object code, firmware byte, calibration
value or per-unit identity is committed. The local vendor modules/firmware
are read in place by host scripts and are not part of this lane.
