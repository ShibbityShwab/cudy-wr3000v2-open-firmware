# MAC RX data path - static RE, the live-bank correction and the fail-closed RX enable

**Lane:** wifi-rx-static (host-only clean-room). **Task:** shared task-137.
**Written:** 2026-11-28. **No bench contact, no image build, no vendor bytes.**
**Base tree:** the final combined clean-room driver source (wifi-stability lane
final combined module, unstripped 990,760 B sha256 64ece213..., opensource 0bc0e24).
**Deliverables:** `patches/0001-rx-live-enable.patch` (6 files), `src/`,
`tools/rx_selftest.c` (51 checks, all pass), `tools/build.sh`, `TEST-LADDER.md`,
`BUILD-NOTES.md`.

Evidence markers: **[V]** decoded from the named local vendor artifact
(hi5622v100_wifi.ko 2.4.15 / 2.5.24, not committed: `/tmp/rootfs2524b/...`,
`/home/shibbityshwab/recovery-extract/...`); **[V*]** measured by an earlier
attended bench lane and quoted from the committed reports; **[I]** inference;
**[U]** unknown.

---

## 0. TL;DR

The RX completion ring never moved **because the driver was reading the wrong
register bank, not because the MAC RX consumer was disabled**.

1. The vendor driver keeps its live device-side ring pointers in a *separate*
   mapped block: machw array word 8, **CA 0x40050000** (band 0) [V]. Offsets:
   **+0x00** host-MAC interrupt status, **+0x08** norm free-ring rptr,
   **+0x0c** small free-ring rptr, **+0x10** completion-ring wptr.
2. The block the 14-write program touches, **CA 0x40040000**, carries the
   *config* words (+0x20/24/28 addresses, +0x2c/30/34 sizes, +0x38/3c/40
   wptr/rptr, +0x44 W1C status, +0x48 mask 0xfffffcc1). The config words
   **+0x4c/+0x50/+0x54 are never read by the vendor** - the stock register
   dump keeps them 0 while the live block carries 0x40050008 = 0x4005000c =
   **0x20** (the MAC already prefetched 32 free buffers) [V*].
3. `dp_rx_poll` polled config +0x4c/+0x50/+0x54 and so observed zeros even
   when the MAC was consuming buffers. This also invalidated the inherited
   "no RX" conclusion: the capture decoder itself was additionally wrong by
   4 bytes (see section 5).
4. The vendor's **12-write free-ring/completion-ring program is the whole
   enable** - there is no extra MAC RX enable bit or host->firmware RX
   queue-state message in the shipped host module. What the host still owes is
   the *consumer* half: read the live bank, drain 4-byte completion entries
   (each is the device VA of buffer+4), decode the 36-byte per-buffer header,
   refill the free rings up to the live rptr delta and publish the completion
   ring consumer index. That is what the patch implements, behind
   `rx_enable=1` (default 0, fail-closed).

**Predicted bench proof (L1 ladder rung, read-only):** `echo 1 > dp_rx_arm`
prints the live bank; a healthy AP boot should show **norm_rptr/small_rptr =
0x20** (vendor stock mirror) and mask `0xfffffcc1`. That single line is the
RX-direction mirror of the TXQ-publish proof.

---

## 1. Inputs and method

Local evidence only (paths are references, never committed):

| artifact | use |
| --- | --- |
| vendor wifi.ko 2.4.15 / 2.5.24, ELF32 ARM, not stripped | symbol/address decode with the lane tools (`tools/annotate.py` from wifi-host-replica; objdump text kept in the committed wifi-shim-port raw dumps) |
| committed disassembly references `docs/incoming/wifi-shim-port/raw/hi5622v100_wifi.dis` + `-data.dis` | every address below is a symbol+offset in that artifact |
| committed phase43 register dumps `docs/phase43/channel-register-offsets.md` | the stock live values (0x4004xxxx and 0x4005xxxx) quoted in section 3 |
| prior lane reports | wifi-datapath / -bringup / -txrx (the 14-write program and TXQ publish), wifi-host-replica (machw struct), wifi-dp-air (rings already allocated and filled) |

Method: follow the machw object fill, then every caller that reads or writes
a ring pointer, then the MAC ISR that consumes the completion ring, then the
per-buffer header decoder. Every constant below is a **derived fact**: offsets
and values, no vendor code bytes are reproduced.

---

## 2. The machw register-bank map (what CA is what) [V]

`shuangta_host_initialize_machw` 0x3490c allocates a 40-byte object and calls
the static filler at **0x33fbc**, which walks the .rodata device-CA table at
**.rodata+0xb60** (band 0) / +0xb88 (band 1) and converts each device CA to a
host mapping with `oal_pcie_devca_to_hostva`, storing word i at object+i*4.
Band-0 table (10 words):

| word | CA | role (from the accessors) |
| --- | --- | --- |
| 0 | 0x40042000 | [U] (has the +0xa8 / +0x924 fields used by test-mode helpers) |
| 1 | 0x40044000 | [U] |
| 2 | 0x40046000 | [U] |
| 3 | 0x40048000 | [U] (phase43 dump has +0x10..+0x18) |
| 4 | 0x4004a000 | CSI block (buffer address/size, whitelist) |
| **5** | **0x40040000** | **MAC host-interface config** - the 14-write program's block |
| 6 | 0x4004c000 | [U] |
| 7 | 0x40052000 | [U] (cfg/lmi/psd style fields live in one of words 7/9) |
| **8** | **0x40050000** | **live host-MAC interrupt status + ring pointers** |
| 9 | 0x40054000 | [U] |

The map is reachable from our driver: inbound region 3 covers CA
0x40000000..0x4011ffff, so band-0 word 8 is at BAR0 offset
`0x3b8000 + 0x50000` through the existing range-checked translation.

---

## 3. The live bank (CA 0x40050000) vs the config block (0x40040000) [V]/[V*]

Vendor accessors that prove the split:

| vendor function | address | what it does |
| --- | --- | --- |
| `shuangta_get_host_mac_int_status` | 0x34ad0 | machw=*[dev+0x14c]; r0=[machw+0x20]; **status = *r0** -> CA 0x40050000 +0x00 |
| `shuangta_get_host_mac_int_mask` | 0x34b50 | machw=*[dev+0x14c]; r0=[machw+0x14]; **value = *[r0+0x48]** -> CA 0x40040000 +0x48 |
| `shuangta_clear_host_mac_int_status` | 0x34bd8 | **W1C write to *[[machw+0x14]+0x44]** |
| `shuangta_host_mac_irq_unmask / _mask` | 0x34c54 / 0x34cdc | RMW **[[machw+0x14]+0x48]**, BIC/OR bit (0xfffffcc1 source at 0x3a7bc) |
| `shuangta_rx_host_init_dscr_queue` | 0x37888 | binds the RX rings and stores, per ring, the pointers to its producer/consumer words |
| `shuangta_host_rx_add_buff` | 0x374d0 | refill path: alloc buffer, write free-ring entry, set_sw2hw publish, irq_unmask |

Ring binder detail (0x37888, the proof for the live offsets):

* norm free ring struct fields (relative to the ring object at dev+0x90):
  +0x10 = **[machw+0x14]+0x38** (producer, config), +0x14 = **[machw+0x20]+0x08**
  (consumer, **live**), +0x18/0x1c = size/address config words.
* small free ring: producer **[machw+0x14]+0x3c**, consumer **[machw+0x20]+0x0c**.
* completion ring: producer **[machw+0x20]+0x10** (**live**), consumer
  **[machw+0x14]+0x40** (config, host-owned).

Stock register dump (phase43 `reg_all.txt`, vendor driver running, band 0):

```
4004001c=00b0063c  40040020=844c7000  40040024=844c8000  40040028=83734000
4004002c=00000100  40040030=00000200  40040034=00000fff
40040038=00008000  4004003c=00008000  40040040=00000000
40040048=fffffcc1  4004004c=00000000  40040050=00000000  40040054=00000000
40050000=00000400  40050004=00000000  40050008=00000020  4005000c=00000020
40050010=00000000
```

Read this carefully: the free rings are published (producers 0x8000) and the
live consumers are at **0x20** - the MAC fetched 32 entries from each ring -
while the config-block words +0x4c/+0x50/+0x54 stay **0**. That is the
corrected diagnosis of "RX captures 0": the telemetry was dead, not
necessarily the data path. (The same dump's +0x00 = 0x400 is a status bit
left from another subsystem; with mask 0xfffffcc1 the RX bit is bit 2.)

---

## 4. Vendor RX init sequence (the enable) [V]

`shuangta_rx_host_init_dscr_queue` 0x37888, in vendor order:

1. norm free ring: 256 entries x 4 B (0x400 B, non-cached host DRAM), zeroed;
   ring type 1 (SW2HW producer), depth 256 at struct+0x04, entry size 4 at +0x06,
   bound producer/consumer words; `hal_set_rx_data_buff_len_cfg_rx_norm_buff_len`
   writes **0x63c** into config +0x1c bits 15:0; the ring address (buffer device
   VA) into config +0x20; the depth into config +0x2c bits 11:0.
2. small free ring: 512 entries x 4 B (0x800 B); buff len **0xb0** into +0x1c
   bits 31:16; address +0x24; depth +0x30.
3. completion ring: 4095 x 4 B (0x3ffc B); type 2 (HW2SW consumer), depth 4095;
   address +0x28; depth +0x34.
4. Fill both free rings by `shuangta_host_rx_add_buff` 0x374d0:
   allocate an skb, 4-align skb->data, store the skb pointer in data[0], zero the
   36-byte device-header area at data+4, and push **devVA(data+4)** as the ring
   entry; after the batch, `hal_ring_set_sw2hw` publishes the zero-extended
   packed wptr (index|phase<<15) and `shuangta_host_mac_irq_unmask(dev, 3)` /
   `(dev, 4)` unmasks the norm/small MAC interrupt bits in config +0x48.
   After a full fill the published producer is **0x8000** (index 0, phase 1) -
   exactly what our dp_setup writes [V*].

**There is no additional RX enable**: grep over the shipped host module finds no
other writer of the RX ring words, no RX-enable H2D message (the only data-plane
H2D message is the TXQ-table address sync key 0x0320), and the stock dump shows
the free rings armed purely by the sequence above. The MAC prefetch (0x20
consumed) is the hardware consumer running on our own 14-write program.

---

## 5. The completion path and the 36-byte per-buffer header [V]

MAC ISR `shuangta_irq_host_mac_isr` 0x39a1c:

1. read status = *[[machw+0x20]+0x00] (live), mask = *[[machw+0x14]+0x48],
   `status &= ~mask`; walk set bits.
2. **status bit 2 (0x04)** enters the RX branch (0x39c9c): if the RX MPDU queue is
   not full, refill both free rings; then take
   `hal_ring_get_entry_count` of the completion ring (struct at dev+0xb8) and
   consume each entry with `hal_ring_get_entries(cmp, &entry, 1)`.
3. **A completion entry is the device VA of buffer+4** (the same value the host
   pushed into the free ring): the ISR converts it with
   `oal_pcie_devca_to_hostva` and reads the skb pointer at `hostva-4`.
4. After the loop it calls `hal_ring_set_sw2hw(cmp)`, which writes the
   host-side packed consumer index to **config +0x40**, then clears the served
   status bits with a W1C to **config +0x44**.

Per-buffer header decode, `shuangta_host_rx_get_msdu_info_dscr` 0x3843c with
r4 = skb->data (the buffer start D):

| field | address | meaning |
| --- | --- | --- |
| payload length | **D+0x1e** (header+0x1a), u16 | vendor `skb_put(len)`; check is `len + 0x28 <= 0x640` (band 0) |
| next sub-MSDU | **D+0x24** (header+0x20), u32 | device VA of the next chained buffer+4, 0 = end; walked by `shuangta_rx_get_next_sub_msdu` 0x38814 |
| flags | D+9 (header+0x5) | bit7 = "report/not-first" observed by `shuangta_report_rx_msdu_dscr` 0x38d20; bit0 = sub-MSDU continuation |
| status | D+0xb (header+0x7) | status nibble in bits 7:4 (`shuangta_host_get_msdu_rx_status` 0x3838c) |
| payload | **D+0x28** | vendor does `skb_put(len)` then `skb->data += 0x28` |

The device writes 36 bytes at D+4 (the vendor report path copies exactly 36
bytes from `skb->data+4`). **Correction to the inherited model:** the
wifi-dp-txrx capture decoder treated the descriptor as starting at the buffer
base and read the length at `desc+0x1e`; that is 4 bytes high - the descriptor
base is buffer+4, the length is at descriptor+0x1a, the payload starts at
descriptor+0x24 (= buffer+0x28). The patch carries the corrected model and the
host test pins it with literal offsets.

Ring index format (both directions): 15 index bits + bit 15 phase [V]. The
vendor setters `shuangta_set_rx_*_{w,r}ptr` split bit 15..22 as the phase
(`ubfx #15,#8`) and the low 15 bits as the index; `hal_ring_get_entry_count`
0x31f80 resolves the full/empty ambiguity by comparing index and phase:
equal index + equal phase = full (0 free), equal index + different phase =
empty (depth free), otherwise the index delta adjusted by one depth.

---

## 6. What the patch implements

Lane deliverables on the final combined tree (patch applies with `patch -p1`):

| file | change |
| --- | --- |
| `hi5622_rx_live.h/.c` (new) | portable, device-free model: live-bank CAs/offsets, ring pending/consumed arithmetic, packed index advance/publish, 36-byte header parse (length/chain/alignment, fail-closed), completion-entry pool classification, status-bit decode |
| `hi5622_dp_txrx.h/.c` | module params `rx_enable` (default **0**), `rx_clear_status` (default **0**), `rx_drain_max` (default 64); the live-bank read helpers; two sysfs actions `dp_rx_arm` (read-only validation) and `dp_rx_drain` (the only pointer-moving RX action); live-bank telemetry in `dp_rx_poll` when armed and in `dp_rx_capture`; corrected capture payload offset |
| `hi5622_dp.c` | registers the two new attributes |
| `Kbuild` | adds `hi5622_rx_live.o` |

`dp_rx_arm` (read-only; the ladder's L1 proof): refuses unless `rx_enable=1`,
bound, transport windows/armed, and the earlier setup passed. Reads live
status/norm_rptr/small_rptr/cmp_wptr plus config norm/small producer, cmp
consumer and mask; refuses on any 0xffffffff read, mask != 0xfffffcc1, bad
packed index or impossible pending count. Logs the live bank + the expected
"vendor stock mirror" note. **No register write.**

`dp_rx_drain` (only pointer mover): requires `rx_enable=1`, a successful arm
and the write gate (dp_setup + this load's TXQ sync). Reads the live bank,
computes pending completions (`cmp_wptr - cmp_cons`), consumes a bounded
prefix (<= rx_drain_max), and for every entry:

* validates the entry is inside the norm/small RX pool at the expected
  `dma+HIL_RX_SKB_WORD` value (linear search; misaligned/unknown -> refuse);
* parses the 36-byte header at buffer+4: length 1..dma_cap-0x24, no sub-MSDU
  chain (chain decode still [U] -> refuse), readable payload;
* captures up to 16 headers/payload prefixes into `dp_rx_capture`;
* queues the freed buffer for refill.

Only after **all** entries validate does it commit: refill the free rings from
the completed queues bounded by the live consumer-rptr delta (so a buffer still
in flight can never be re-published), publish the free-ring producers and the
completion-ring consumer with read-back checks, and (only if
`rx_clear_status=1`) W1C the observed status word to config +0x44.

Writes performed by the whole RX path: config +0x38, +0x3c (free-ring
producers), +0x40 (completion consumer), and +0x44 only behind
`rx_clear_status=1`. The live bank is read-only. No IRQ is enabled (poll
driven), no MAC word outside band 0 is touched, no firmware message is sent.

### Fail-closed matrix

| condition | result |
| --- | --- |
| `rx_enable=0` (default) | arm/drain refused EPERM; `dp_rx_poll`/`dp_rx_capture` behave as before (observe-only) |
| not bound / transport not armed | arm/drain refused EBUSY |
| arm mask != 0xfffffcc1, any 0xffffffff read, bad packed index | arm refused, no write |
| drain before arm | refused EINVAL |
| write gate (dp_setup/TXQ sync) not met | drain refused, no write |
| unknown/misaligned completion entry | lane closed (`rx_failed`), no further pointer published, no retry |
| header length 0 / payload overrun / chain present | lane closed before any pointer publish |
| publish read-back mismatch | lane closed |
| new module load | all state resets (the lane never silently retries) |

The patch is deliberately capture-only: it does not deliver frames to mac80211
yet (the netdev/msdu hand-off is a separate step), so the ladder's PASS bounds
are about *proof of the device path*, not throughput.

---

## 7. Host test + build verification (this lane)

* `tools/rx_selftest.c`: **51 checks, 0 failures** - literal vendor offsets,
  ring pending/consumed arithmetic (including phase-wrap, one-lap and
  consumer-ahead refusals), packed index advance/publish, header parse
  (length bounds, chain refusal, misaligned chain), entry classification,
  status bit decode, and a synthetic end-to-end "published 256, consumed 32,
  one frame on slot 0" case.
* module build against the project 6.18.60 tree: **RC=0, 0 warnings**
  (`hi5622_rx_live.o` compiled as part of `hi5622_wifi.ko`).
  Unstripped 1,021,976 B sha256 `3649f589e3a428575e6ee4a917a823b7b3ce434add0988ed71bda5f2b7830e12`;
  stripped 280,736 B sha256 `5b43d0a2ca526f84fabd744377213024c9dde15ce22a3c18e826180944d1f7be`.
* patch round-trip: `patch -p1` on a pristine copy of the final combined tree
  recreates all six files byte-for-byte.

---

## 8. Still unknown / next steps

* **[U] sub-MSDU chains**: the next-pointer at header+0x20 chains buffers, and
  each chained buffer was also consumed from a free ring; the slot accounting
  for chains is not decoded, so the patch refuses (`HIL_RX_ECHAIN`) and the
  lane stops. Needed before A-MPDU/busy RX.
* **[U] which frames the firmware forwards to the host rings** (mgmt vs data;
  AP-mode probe/auth handled internally or not). The ladder's L4 decides this.
* **[U] cfg/lmi bank identity** (machw words 7/9): the vendor writes
  `cfg_intr_mask_host = 0x1f` (setter 0x328a4, register +0x1b0) in the chip IRQ
  init. Polling does not need it; interrupt-driven RX later will.
* The inherited `dp_rx_poll` config-bank snapshot fields remain for
  compatibility; the live bank line is authoritative when `rx_enable=1`.
* Netdev/msdu delivery (needs mac80211 hand-off, rate/status mapping) is not in
  this patch.

---

## 9. Reproduction

```sh
# base = the final combined clean-room driver source tree (wifi-stability lane)
tools/build.sh            # uses RX_BASE (see script); runs selftest + kernel build
```

The patch is self-contained; no vendor artifact is needed to build or test it.
