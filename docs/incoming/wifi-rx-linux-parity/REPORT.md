# Linux RX-enable parity: the canonical open-source RX-enable order vs our Hi5622V100 MAC

**Lane:** wifi-rx-linux-parity (host-only). **Task:** shared task task-166.
**Written:** 2026-11-25. **No bench contact** - nothing in this lane addresses,
resets, flashes or inspects the router. The Lead fires.
**Deliverables:** this report, `patches/0001-rx-linux-parity.patch` (6 files),
`src/` (patch result), `tools/rx_parity_selftest.c` (118 host checks),
`tools/compare_stock.py` (+ 11 unit tests), `tools/build.sh`,
[TEST-LADDER.md](TEST-LADDER.md), [BUILD-NOTES.md](BUILD-NOTES.md).

**Evidence markers:** **[V]** decoded from a named local artifact (vendor module
disassembly / firmware / committed Linux source tree); **[V\*]** measured by an
earlier attended bench lane and quoted from a committed report/ledger entry;
**[L]** live bench observation quoted from the Lead's current-session report;
**[I]** inference; **[U]** unknown.

---

## 0. TL;DR

1. The Linux RX-enable order has **five phases** our driver implements only
   partially: (a) ring memory + DMA program, (b) buffer fill + producer
   publish, **(c) an explicit DMA/ring engine enable word**, **(d) an RX filter
   / address-match word**, (e) interrupt/filter/refill supervision. Our 14-write
   MAC program + free-ring fill + producer publish covers (a) and (b) - the
   vendor-stock comparison in section 6 shows it covers *every* word the vendor
   itself programs in that block - but the host writes **no enable word of any
   kind** and **no filter word**, and we cannot yet prove the firmware programs
   them either.
2. That gap matters because **free-ring prefetch is not RX enable**. The MAC
   consuming 0x20 published free entries at init [L] proves the free-ring *fetch*
   engine runs; Linux separates that from the DMA-enable step (mt76 waits for
   `RX_DMA_BUSY` clear before setting `RX_DMA_EN`; iwlwifi writes the channel
   config register last; ath11k sets `SRNG_ENABLE` in the ring MISC word after
   programming base/size). Nothing about prefetch proves the completion-write
   path is enabled.
3. The two most plausible causes of "captures nothing from the air" are now
   expressed as **testable, read-only-first hypotheses**:
   * **H-enable**: a host-writable RX enable/control word exists that we never
     write (the MAC-config block's four stock-zero, unprogrammed words
     0x08/0x0c/0x14/0x18, or a word in another machw block);
   * **H-filter**: the MAC drops received frames because its address/RX filter
     is not programmed for our BSSID (the host never writes one; Linux drivers
     always do).
   `dp_rx_parity` (new, read-only) dumps every word needed to decide both
   against the committed vendor stock capture; `tools/compare_stock.py` turns the
   dump into the candidate list; `dp_rx_ctl` and `dp_rx_kick` are the matching
   **opt-in, one-shot, fail-closed** experiments for the first "on air" test.
4. The lane also fixes four **write-surface divergences** that are first-class
   suspects for the firmware exception the Lead observed after extended RX
   draining (`mask CA 0x400008 = 0x04000010`, `vector_mask=0x10`,
   `excp_announce=1` [L]):
   * the ring-pointer publish used a 32-bit read-modify-write while the vendor
     stores a zero-extended u16 (now exact-width, refusing unexpected upper
     bits);
   * the status W1C wrote the raw status word instead of `status & ~mask`
     (now served-bits-only);
   * completion entries were consumed without a commit/ownership decision (now
     a bounded commit wait; a still-zero entry is a spurious producer advance,
     not corruption, and is never consumed);
   * `dp_rx_poll` read the config words +0x4c/+0x50/+0x54 that **the vendor never
     reads** (now live-bank-only by default, legacy reads opt-in).
5. Host verification: **118/118** parity-model checks, **11/11**
   compare-stock tests, patch round-trips byte-for-byte, portable files compile
   in kernel context (6.18.60) and the patched glue objects compile **0 errors /
   0 warnings**. `tools/build.sh` reproduces all of it. The full-module link is
   deliberately not attempted (the final association tree is not committed -
   see BUILD-NOTES.md).

---

## 1. The open bug, as measured

Committed context ([L] = Lead's current-session bench report; [V\*] = earlier
attended lanes):

| observation | value | meaning |
| --- | --- | --- |
| client sends 802.11 auth to the AP BSSID | "authentication timed out" 1/3..3/3 | the client transmits; RF path is not the question |
| router dmesg mgmt-RX events (D2H CRX g1 id13) | none | the firmware forwards nothing |
| MAC RX live bank CA 0x40050000 | status **0x000182**, norm rptr **0x0020**, small rptr **0x0020**, cmp wptr **0x0** | the device consumer is stuck at the init prefetch; no completion ever written |
| hostapd | sees nothing | no mgmt frames reach the host at all |
| RX free-ring consumer at init | "MAC consumed 0x20 published free entries" | the free-ring fetch engine runs |
| TX | works (TX COMPLETED via TXQ publish) | MAC/PCIe/DMA/RF transmit path proven |
| RF | -33 dBm; AP beacons observed by clients; laptop TX works on other APs | radio and both directions' RF are proven |
| AP start | 5-message vendor order; WPA2 GTK; AP-ENABLED | control plane and firmware AP state proven |
| after extended RX drain loops | firmware exception appears: page loses golden, mask CA 0x400008 = **0x04000010**, `vector_mask=0x10`, `excp_announce=1`; AP itself stayed clean | a second, RX-adjacent problem |

Two questions follow, and this lane treats them as one write-surface analysis:
**(A)** why does the MAC never write a received frame, and **(B)** which RX-side
host access can fault the firmware? A missing host-writable enable/filter word
and a host write to a register the vendor never touches are two sides of the
same "our host model is incomplete" coin.

---

## 2. Sources, method, licensing

### 2.1 Linux reference tree

All Linux citations are from the project's local, unmodified kernel source tree:

* `build/linux-kbuild/linux-5.10.201/drivers/net/wireless/` (mt76, iwlwifi,
  ath10k, ath11k, brcmfmac) - the stock vendor kernel version of this board.

The Linux sources are GPLv2. **Nothing is copied**: this lane reads them for
architecture facts (what step exists, in what order, with which register
semantics) and re-implements the *rules* as original clean-room code. Short
quotes appear below for analysis/attribution only; our driver keeps its own
structure, names and text.

### 2.2 Project evidence

| artifact | use |
| --- | --- |
| `docs/incoming/wifi-rx-static/REPORT.md` | the 14-write program, live bank decode, 36-B header, ring geometry [V] |
| `opensource/dumps/reg_all.txt` | vendor stock register capture; the probe's reference values [V\*] |
| `docs/incoming/wifi-rx-watermark/REPORT.md` | sustained free-ring refill; current RX source state [V] |
| `docs/incoming/wifi-sibling-gpl/REPORT.md` | Hi3881/Hi1105 siblings: CRX/DRX split, free-ring-as-credit, refill watermark, no SET_RX_FILTER [V] |
| `docs/incoming/wifi-dp-air`, `wifi-dp-tuning`, `wifi-host-replica` | the current driver sources/patches this lane extends [V] |
| Lead's current-session bench report | [L] rows above |

### 2.3 Method

1. Extract, per driver, the functions that make RX actually start - not the
   steady-state data path: ring allocation/programming, buffer fill, the
   explicit enable word, the interrupt/NAPI enable, the RX filter, the producer
   publish/kick, the completion consume/consumer publish, the refill loop, the
   stall watchdog, the teardown order.
2. Distil the canonical order and the *reason* each step exists.
3. Map each step onto our objects (live bank, config block, 14-write program,
   D2H CRX events) and mark done / partial / missing / n/a with evidence.
4. Compare the host's write set against the vendor-stock non-zero write set
   (section 6.3) to see whether the missing step is in the block we program.
5. Implement the safe subset fail-closed (section 7); wrap the experiments in
   explicit module parameters and a one-shot policy.
6. Host-test the rules and the tooling; write the attended fire recipe
   (section 9) for the Lead.

---

## 3. The canonical Linux RX-enable sequences

Quoted paths are relative to `build/linux-kbuild/linux-5.10.201/drivers/net/wireless/`.
Line numbers are from that tree; quotes are short for attribution.

### 3.1 mt76 (PCIe: `mediatek/mt76`)

| step | code | fact |
| --- | --- | --- |
| ring program | `mt76/dma.c:83-116` `mt76_dma_alloc_queue` | clear descriptors to `MT_DMA_CTL_DMA_DONE`, then write `desc_base`, `cpu_idx=0`, `dma_idx=0`, `ring_size` |
| buffer fill + DMA map | `mt76/dma.c:433-467` `mt76_dma_rx_fill` | `page_frag_alloc` -> `dma_map_single(..., DMA_FROM_DEVICE)` -> write descriptor -> kick |
| **kick** | `mt76/dma.c:213-217` `mt76_dma_kick_queue` | `wmb(); writel(q->head, &q->regs->cpu_idx)` - a separate producer-index store *after* the descriptor writes |
| **DMA engine enable** | `mt76/mt7615/dma.c:270-284` | poll `TX_DMA_BUSY|RX_DMA_BUSY` to 0, then `mt76_set(MT_WPDMA_GLO_CFG, TX_DMA_EN|RX_DMA_EN)`, then `mt7615_irq_enable(MT_INT_RX_DONE_ALL|...)` |
| **RX filter** | `mt76/mt7603/main.c:225-229,250-266` | `dev->rxfilter` read-modify-written to `MT_WF_RFCR` (DROP_OTHER_BSS / PROBEREQ / BCAST / ... bits) |
| completion ownership | `mt76/dma.c` dequeue path, `MT_DMA_CTL_DMA_DONE` | a descriptor is only consumed once the device cleared DONE |
| refill | `mt76/dma.c:600,641` | `mt76_dma_rx_fill` after the poll loop / after reset |

### 3.2 iwlwifi (PCIe: `intel/iwlwifi/pcie/rx.c`)

Order from `iwl_pcie_rx_init` (`:1156-1176`): fill the queue (`_iwl_pcie_rx_init`)
-> program the device (`iwl_pcie_rx_hw_init` / `_mq_hw_init`) -> `rxq_restock`
-> `rxq_inc_wr_ptr`.

| step | code | fact |
| --- | --- | --- |
| **stop first** | `:201-220` `iwl_pcie_rx_stop` | disable the RX DMA channel and poll for idle |
| ring + status base | `:903-968` `iwl_pcie_rx_hw_init` | stop DMA; zero/flush pointers; program RBD ring base (`bd_dma >> 8`) and status write-pointer base (`>> 4`); **then** write the channel config register with `CHNL_EN` + IRQ destination + RB size + timeout + RBD count |
| MQ variant | `:972-1058` `iwl_pcie_rx_mq_hw_init` | program per-queue free/used RBD base + status + reset indices; write `RFH_RXF_DMA_CFG` with `RFH_DMA_EN_ENABLE_VAL|rb_size|...`; write `RFH_RXQ_ACTIVE` queue bitmap |
| produce pointers | `:298-360` `rxmq_restock` | write RBDs, then bump the device write pointer; `:223-256` `rxq_inc_wr_ptr` acknowledges and writes it |
| **publish batching** | `:345-401` `rxsq_restock` | "Increment device's write pointer in multiples of 8" (`rxq->write_actual != (rxq->write & ~0x7)`) |
| **stall watchdog** | `:112-140` comment + `:1477+` `iwl_pcie_rx_handle` | "If there are no allocated buffers ... the READ INDEX is not incremented and iwl->status(RX_STALLED) is set"; restock clears it |
| consumer publish | `:1283+` `iwl_pcie_rx_handle_rb` -> restock | after consuming, advance the host read index and restock |

### 3.3 ath10k (PCIe CE rings: `ath/ath10k`)

| step | code | fact |
| --- | --- | --- |
| dest ring program | `ce.c:1463-1500` `ath10k_ce_init_dest_ring` | zero ring memory, read current device indices, set ring base, size, high/low marks, byte swap |
| ring memory | `ce.c:1632-1665` `ath10k_ce_alloc_dest_ring` | `dma_alloc_coherent`, aligned ring, "Legacy platforms that do not support cache coherent DMA are unsupported" |
| **start** | `pci.c:1955-1968` `ath10k_pci_hif_start` | `napi_enable` -> `ath10k_pci_irq_enable` -> `ath10k_pci_rx_post` (fill buffers) |
| fill + retry watchdog | `pci.c:803-833` `ath10k_pci_rx_post_pipe` | post free buffers; on `-ENOSPC` stop, on error arm `rx_post_retry` timer |
| interrupt enable | `ce.c:1405-1425` `ath10k_ce_enable_interrupts` | per-engine interrupt enable after the rings exist |

### 3.4 ath11k (ring HAL: `ath/ath11k`)

| step | code | fact |
| --- | --- | --- |
| ring alloc + params | `dp.c:217-300` `ath11k_dp_srng_setup` | coherent ring; `params{ring_base_paddr, num_entries, intr_batch/timer thresholds, low_threshold}`; then `ath11k_hal_srng_setup` |
| **ring register program + enable** | `hal.c:320-395` `ath11k_hal_srng_dst_hw_init` | write base low/high + size + ring id/entry size + interrupt thresholds + HP address; initialise HP/TP to 0; last write `RING_MISC = ...` with **`HAL_REO1_RING_MISC_SRNG_ENABLE`** (`:394`) |
| buffer replenish | `dp_rx.c:436-460` `ath11k_dp_rxdma_ring_buf_setup`, `dp_rx.c:278-350` `ath11k_dp_rxbufs_replenish` | fill ring with `{paddr|cookie|return-buffer-manager}` descriptors |
| producer kick | `hal.c:832-860` `ath11k_hal_srng_access_end` | publish HP/TP to the register (or shared-memory location the firmware reads) |

### 3.5 brcmfmac (PCIe msg rings: `broadcom/brcm80211/brcmfmac`)

| step | code | fact |
| --- | --- | --- |
| ring config | `commonring.c:31-49` `brcmf_commonring_config` | set depth/item/addr; reset `r_ptr` and `w_ptr`, writing both through the bus callbacks |
| **publish + doorbell** | `commonring.c:181-195` `brcmf_commonring_write_complete` | `f_ptr = w_ptr`; `cr_write_wptr()` (device-visible copy) then `cr_ring_bell()` - the kick is a separate doorbell |
| consumer publish | `commonring.c:225-240` `brcmf_commonring_read_complete` | advance `r_ptr`, write it back |
| transport | `pcie.c:110-139` | mailbox registers and D2H doorbell bits (`BRCMF_PCIE_MB_INT_D2H*`) |

### 3.6 Distilled canonical order

```
 0. (stop/disable the engine first if it was running)      iwlwifi rx_stop
 1. allocate ring memory (coherent)                        all
 2. program base / size / entry-size / id                   all
 3. reset the ring indices (host and device copies)         all
 4. allocate + DMA-map buffers, write ring descriptors      all
 5. publish the producer index (+ a separate kick/doorbell) mt76/brcmfmac/ath11k
 6. ENABLE the engine / ring  <-- a distinct control write   mt76 GLO_CFG, iwlwifi channel config/RFH_DMA_CFG, ath11k SRNG_ENABLE
 7. enable interrupts / NAPI                                mt76/ath10k/iwlwifi
 8. program the RX filter / address match                   mt76/iwlwifi/ath10k/brcmfmac
 9. consume completions with an ownership/commit decision   all
10. publish the consumer index                              iwlwifi/ath11k/brcmfmac
11. refill returned buffers (watermark, retry timer)        all
12. watch for stalls (free-buffer starvation)               iwlwifi RX_STALLED/ath10k timer/Hi1105 free-ring-empty IRQ
```

Our driver implements **1, 2, 3 (partially), 4, 5, 9 (partially), 10, 11**
and now adds bounded-wait/clear rules and stall telemetry. It has **no step 6,
no step 8 and no step 7**; steps 3/9/10 were width- or ownership-imprecise.

---

## 4. Mapping the Linux order onto the Hi5622V100 model

Our objects (committed decode [V], plus [L]):

* **config block** `CA 0x40040000` (0xb0 bytes): 14-write program at
  +0x00/04/10/1c/20/24/28/2c/30/34/38/3c/40/48; status W1C +0x44;
  the vendor never reads +0x4c/+0x50/+0x54.
* **live bank** `CA 0x40050000` (+0x00 status, +0x08/+0x0c free-ring device
  rptrs, +0x10 completion wptr; 0x44 bytes, vendor-dumper span).
* **rings**: norm free 256x4B, small free 512x4B, completion 4095x4B; entries
  are 32-bit device VAs; completion entry = `devVA(buffer+4)`; per-buffer 36-B
  header at buffer+4.
* **mgmt RX** is a D2H CRX event (`g1 id13`), not ring traffic; **data RX**
  should appear as completion-ring entries.
* **interrupts** are not enabled on the host side (poll model); the MAC-interrupt
  mask register is already at the vendor value 0xfffffcc1.

| Linux step | our mechanism | state | evidence |
| --- | --- | --- | --- |
| 1 ring alloc | dp_setup `dma_alloc_coherent` rings + per-buffer pools | **done** | [V] driver source |
| 2 ring program | 14-write MAC program, read-back verified | **done** | [V\*] E1 37/37; stock dump match section 6.3 |
| 3 index reset | producers published as 0x8000, completion consumer 0; host bookkeeping resets on load | **partial**: host side only; no device-side "reset ring" step is decoded | [V] |
| 4 buffer fill + DMA | coherent buffers, `devVA(buffer+4)` entries; no per-buffer dma_map (coherent => not needed) | **done** | [V] |
| 5 producer publish | packed 16-bit publish at +0x38/+0x3c | **done, width corrected here** | [V] route; fixed in patch |
| 6 **engine/ring enable** | none | **MISSING/UNKNOWN** | no decoded host-writable enable; stock comparison shows the 14-write program covers every vendor non-zero word it can own (section 6.3); the rest is [U] |
| 7 interrupt/NAPI | none (deliberate poll model); MAC int mask set | **n/a by design** | [V\*] |
| 8 **RX filter / address match** | none | **MISSING/UNKNOWN** | no filter write in driver; whether the firmware programs it is [U]; Linux always does |
| 9 completion consume | validate entry against pools + 36-B header | **partial -> fixed**: bounded commit wait, clear-after-read, spurious-vs-corruption split | this patch |
| 10 consumer publish | packed write at +0x40 | **done, width corrected here** | this patch |
| 11 refill | rx_drain window + rx-watermark sustained pass | **done** (+ bounded window rule here) | rx-watermark lane |
| 12 stall watch | log-only classification added here (IDLE/PREFETCH_ONLY/PROGRESS/SPURIOUS_WPTR) | **done as telemetry** | this patch |
| 13 teardown | n/a (no module stop path; reload implies power cycle) | **n/a** | driver design |

### 4.1 Why free-ring prefetch is not proof of RX enable

The MAC consumed 0x20 published free entries at init [L]. In the Linux model
that is step 5's device-side effect (the free-ring producer is host -> device;
the device fetching entries is the credit engine), and it stays true even when
step 6 (engine enable) or step 8 (filter) is missing: the ring fetch and the
receive write path are different hardware paths. The vendor stock capture even
shows the same 0x20 prefetch in a working boot [V\*] - `0x40050008 = 0x4005000c
= 0x20` - so 0x20 is the normal prefetch watermark, not evidence that anything
received was forwarded.

### 4.2 The two candidate "missing configuration" hypotheses

**H-enable.** Our host writes no explicit enable word. The vendor-stock
comparison (section 6.3) shows the 14-write program already covers every
non-zero word of the MAC config block **except +0x5c/+0x6c**, which have no
identified writer and are already covered by the existing `mac_tail_writes`
experiment. Therefore, if the enable is a host-visible word in that block, it
must be one of the words that read **zero** in the vendor capture:
**+0x08, +0x0c, +0x14, +0x18** - the four words that are (a) inside the block,
(b) never written by our program, (c) zero in the vendor stock capture. If the
enable lives in another machw block (words 0..4/6/7/9), `dp_rx_parity` prints
the first 16 words of each so the stock diff shows it. **This lane does not
claim one of those words is the enable** - it makes the question answerable by a
read-only capture plus a gated one-shot test.

**H-filter.** Linux always programs an RX filter/address-match before expecting
traffic (mt76 `MT_WF_RFCR`, iwlwifi/mac80211, ath10k). Our host writes none. The
firmware may own the filter in the vendor split, but if it only programs it
after a message/state we never reach, the MAC would silently drop the client's
auth frames while the free-ring and TX paths stay healthy - exactly the
observed asymmetry. H-filter is checked by the same parity dump (candidate
filter-ish words are visible as stock-vs-ours differences) and by the on-air
test in the fire ladder (an associated client data frame would also exercise
the filter).

---

## 5. The crash (B): the RX host write surface, ranked

The Lead's exception (`mask CA 0x400008 = 0x04000010`, `vector_mask=0x10`,
`excp_announce=1`) appeared after extended RX draining while the AP stayed
clean [L]. This lane cannot prove the trigger from the host; it can enumerate
the RX-side accesses and remove the divergences. Ranked candidates:

| # | candidate | why it could fault the firmware/MAC | status in this patch |
| --- | --- | --- | --- |
| T1 | **32-bit RMW publish** of +0x38/+0x3c/+0x40: we wrote `(old & 0xffff0000) | packed` while the vendor's `hal_ring_set_sw2hw` stores a zero-extended u16 [V]. If the upper half ever reads back non-zero, we write ring-pointer bits the vendor never writes. | a MAC ring pointer with unexpected upper bits could make the device fetch/DMA an address it should not; a device-side bus error is a candidate for the exception announcement | **fixed**: exact-width u16 store; refuses (no write) if the upper half is non-zero; read-back verified |
| T2 | **W1C of the raw status word** at +0x44; the vendor ISR W1Cs `status & ~mask` [V]. Our version acknowledged masked-off bits the vendor never touches. | acknowledging foreign/other-subsystem status bits can clear a state the firmware/MAC still needs - a plausible crash trigger in the RX completion path | **fixed**: served-bits-only W1C, mask re-read and validated |
| T3 | **Consuming a completion entry before it is committed**: the device producer pointer can become visible before the entry payload; without an owner/DONE bit we could treat a stale ring word as a valid completion, hand back a buffer still owned by the device, and re-publish it as free. | double-owned buffer -> device writes into memory the host has reused -> corruption; a device error path can announce an exception | **fix-ready**: bounded commit wait (`rx_entry_wait`, default 32), transient zero = spurious (not fatal), clear-after-read; no consume while unstable |
| T4 | **Reading config +0x4c/+0x50/+0x54 in `dp_rx_poll`**: the vendor never reads those words and keeps them 0 [V]. A read-sensitive register there (read-to-clear / FIFO-pop style) would be disturbed by every poll iteration. | repeated reads of a never-read register is a classic way to desynchronise a block; the old poll loops read +0x4c every 50 ms for up to 2 s | **fixed**: default is live-bank-only; the legacy reads are behind `rx_legacy_bank=1` and are counted/reported |
| T5 | **Publishing a producer from a stale consumer snapshot**: the window refill used the drain-start rptr while the watermark pass re-read a fresh one; a stale-window publish can over-post (ring pointer advanced past free slots). | over-posting hands the device entries it may still own (same class as T3) | **fixed**: one bounded window rule (`rxp_refill_plan`) = min(consumed delta, queued, depth-posted, cap); refuses instead of clamping |
| T6 | `dp_rx_ctl` (new) writes one allowlisted candidate word - by design a **deliberate** risk; only an attended experiment. | unknown word semantics | **gated**: `rx_ctl_enable=1` + arm + write gate + one shot + allowlist {0x08,0x0c,0x14,0x18} + read-back; a mismatch closes the tail lane |

What the patch does **not** do: it never writes a register outside MAC band 0, it
never touches the live bank, CA 0x400392f0 or the RC misc window, and every new
write path refuses unless `rx_enable=1`, `dp_rx_arm` passed and the
`dp_setup`+TXQ-sync write gate is met (same policy as the drain).

---

## 6. Vendor stock comparison: what we write vs what the vendor has

### 6.1 Committed stock capture

`opensource/dumps/reg_all.txt` (vendor register-dumper output, stock boot) has
the MAC config block 0x40040000..0x400400ac, the live bank
0x40050000..0x40050040, and every other band-0 block. The values this lane
uses as reference (no per-unit data; the addresses are that boot's own driver
allocation, already published in the capture):

```
40040000=00000100 40040004=83fd7000 40040008=00000000 4004000c=00000000
40040010=02060850 40040014=00000000 40040018=00000000 4004001c=00b0063c
40040020=844c7000 40040024=844c8000 40040028=83734000 4004002c=00000100
40040030=00000200 40040034=00000fff 40040038=00008000 4004003c=00008000
40040040=00000000 40040044=00000000 40040048=fffffcc1 4004004c=00000000
40040050=00000000 40040054=00000000 40040058=00000000 4004005c=00000004
40040060=00000000 40040064=00000000 40040068=00000000 4004006c=00000004
...
40050000=00000400 40050004=00000000 40050008=00000020 4005000c=00000020
40050010=00000000 40050014=00020020 ...
```

### 6.2 Two read facts that matter for the crash analysis

* the vendor **reads** +0x44/+0x48 and the live bank; it **never reads**
  +0x4c/+0x50/+0x54 [V] - our legacy poll did, repeatedly;
* the vendor's own register **dumper** reads every block this lane's probe
  reads (that is where `reg_all.txt` came from), which is the safety argument
  for the read-only probe.

### 6.3 The write-set comparison (the important table)

| MAC config word | vendor stock | our 14-write program | classification |
| --- | ---: | --- | --- |
| +0x00 tx_ba depth | 0x100 | writes 0x100 | match |
| +0x04 tx_ba address | 0x83fd7000 | writes its own coherent txba devVA | programmed, value differs per allocation (expected) |
| +0x10 msdu ring table | 0x02060850 | writes its own ACP alias | programmed, differs per allocation (expected) |
| +0x1c rx buffer lengths | 0x00b0063c | 0x00b0063c | match |
| +0x20/24/28 ring addrs | 0x844c7000/844c8000/83734000 | own coherent ring devVAs | programmed, differs per allocation (expected) |
| +0x2c/30/34 ring sizes | 0x100/0x200/0xfff | 0x100/0x200/0xfff | match |
| +0x38/3c producers | 0x8000/0x8000 | 0x8000/0x8000 | match (filled exactly once) |
| +0x40 completion consumer | 0 | 0 | match |
| +0x44 int status (W1C) | 0 | not written at init (write only opt-in after drain) | n/a |
| +0x48 int mask | 0xfffffcc1 | 0xfffffcc1 | match |
| +0x4c/50/54 | 0 | **never written**; (legacy poll read them - fixed) | vendor never reads them either |
| **+0x5c** | **4** | not written by the 14-write program | **vendor state we never write**; writer not found; existing `mac_tail_writes` experiment |
| **+0x6c** | **4** | not written by the 14-write program | **vendor state we never write**; same experiment |
| **+0x08, +0x0c, +0x14, +0x18** | **0** | not written | **only open host-writable-word candidates for H-enable** |

Conclusion: within the block the driver programs, the 14-write program is a
faithful reproduction of the vendor's write set; the only vendor non-zero words
it does not write are +0x5c/+0x6c (no identified writer). So H-enable either
uses one of the stock-zero words +0x08/0x0c/0x14/0x18, or lives in another
machw block - which is exactly what `dp_rx_parity` + `compare_stock.py` measure
on the live device.

---

## 7. What the patch implements

Files (patch applies to the current RX source state = wifi-rx-static src +
wifi-rx-watermark patch; the integration lane rebases onto the final association
tree):

| file | change |
| --- | --- |
| `hi5622_rx_parity.h/.c` (new) | portable, device-free parity rules: step catalogue, exact-width publish, commit decision, served-bits W1C, bounded refill window, candidate-word allowlist, stall classification, stock-table/classification for the probe |
| `hi5622_dp_txrx.c` | new params/actions; width/ownership/W1C/window fixes; probe, kick, ctl; parity telemetry; legacy-poll gating |
| `hi5622_dp_txrx.h` | declarations for the three new sysfs handlers |
| `hi5622_dp.c` | three new attributes (`dp_rx_parity` 0444, `dp_rx_kick` 0200, `dp_rx_ctl` 0200) |
| `Kbuild` | adds `hi5622_rx_parity.o` |

### 7.1 New module parameters (all fail-closed defaults)

| parameter | default | meaning |
| --- | --- | --- |
| `rx_entry_wait` | 32 | bounded re-reads of a still-zero completion entry (0 = old single-read behaviour) |
| `rx_ring_kick` | 0 | allow the one-shot second producer publish ("kick") experiment |
| `rx_ctl_enable` | 0 | allow the one-shot allowlisted RX-control candidate write |
| `rx_ctl_off` | 0xffffffff | candidate word offset (must be 0x08/0x0c/0x14/0x18) |
| `rx_ctl_set` / `rx_ctl_clr` | 0/0 | bits to set/clear (cannot overlap; not both zero) |
| `rx_legacy_bank` | 0 | allow `dp_rx_poll` to read config +0x4c/+0x50/+0x54 (vendor never reads them) |

### 7.2 New read-only action

**`cat dp_rx_parity`** (one bounded pass, never writes): dumps

* live bank 0x40050000 +0x00..+0x3c (16 words);
* MAC config block 0x40040000 +0x00..+0xac (44 words) with `*` on words that
  are non-zero in the vendor stock capture but never written by our program and
  `!` on programmed words that differ from the capture;
* first 16 words of the other eight band-0 machw blocks
  (0x40042000/44000/46000/48000/4a000/4c000/52000/54000).

`tools/compare_stock.py --probe <file>` diffs it against
`opensource/dumps/reg_all.txt` and lists the missing-write candidates.
This single read-only capture answers H-enable with data, on the real boot,
without a single write.

### 7.3 New opt-in write experiments

**`echo 1 > dp_rx_kick`** (`rx_ring_kick=1`): re-publishes the two free-ring
producer words with the exact zero-extended u16 after a successful arm; one
shot per load, read-back verified. Rationale: the second-publish/kick step mt76
and brcmfmac perform, and the only remaining "publish style" our driver has not
tried.

**`echo 1 > dp_rx_ctl`** (`rx_ctl_enable=1`, `rx_ctl_off=...`, `rx_ctl_set/clr`):
one RMW write to one allowlisted candidate word after arm; one shot per load;
read-back verified (a mismatch closes the tail lane). This is the H-enable
experiment. The allowlist is fixed and tiny: the four stock-zero,
program-unwritten words of the block. Everything else is refused, including all
ring words and +0x5c/+0x6c.

### 7.4 Behavior changes that apply at every load

* ring-pointer publishes (producers + completion consumer) now use the exact
  vendor zero-extended u16 width and refuse an unexpected non-zero upper half;
* the status W1C (only with `rx_clear_status=1`) now writes `status & ~mask`;
* completion entries are consumed only after a bounded commit wait; a
  still-zero entry is a spurious producer advance (log + counter; lane stays
  open up to `RXP_SPURIOUS_MAX=64` consecutive events); a validated prefix is
  committed when the drain stops early; each consumed entry is zeroed
  (clear-after-read);
* refill uses one bounded window rule; a malformed state refuses (closes the
  lane) instead of clamping;
* `dp_rx_poll` defaults to live-bank-only reads;
* `dp_rx_drain`/`dp_rx_capture` telemetry gains the commit/spurious/kick/ctl
  counters and the stall class.

### 7.5 Fail-closed matrix

| condition | result |
| --- | --- |
| `rx_ring_kick=0` (default), `rx_ctl_enable=0` (default) | both new write actions refused EPERM; everything else behaves as before except the divergence fixes |
| not `rx_enable=1` / not armed / RX lane closed / not bound | new actions refused, no write |
| write gate not met (dp_setup + this load's TXQ sync) | refused, no write |
| candidate offset not allowlisted / set & clr overlap / set|clr==0 | refused, no write |
| publish upper half non-zero | refused, no write |
| spurious entries > RXP_SPURIOUS_MAX consecutive | lane closed, no further pointer published, no retry until reload |
| `rx_legacy_bank=0` (default) | config +0x4c/+0x50/+0x54 are never read |

---

## 8. Host verification

`tools/build.sh` (reproducible, host-only):

1. reconstructs the current published RX state from `wifi-rx-static/src` +
   the `wifi-rx-watermark` patch;
2. applies `patches/0001-rx-linux-parity.patch` and verifies `src/`
   byte-for-byte for the six files the patch owns;
3. builds and runs `tools/rx_parity_selftest.c`: **118 checks, 0 failures**
   (exact-width publish, commit wait, spurious cap, served-bits W1C, bounded
   refill window incl. phase/wrap/one-lap, candidate allowlist and RMW,
   stall classes, stock classification, step catalogue, literal decode
   anchors);
4. compiles the portable files in kernel context (6.18.60) and compiles the
   patched glue objects (`hi5622_dp.o`, `hi5622_dp_txrx.o`, `hi5622_rx_live.o`,
   `hi5622_rx_parity.o`, `dp_txrx.o`, `dp_rings.o`): **6/6 objects, 0 errors,
   0 warnings** (modpost unresolved symbols belong to the unlinked full driver,
   expected in the probe);
5. publication checks (no binaries/objects in the lane, SPDX headers).

`tools/test_compare_stock.py`: **11/11** unit tests, including a check that the
committed vendor capture classifies cleanly (0 spurious DIFFs, exactly the two
+0x5c/+0x6c missing-write candidates).

Evidence logs: `evidence/` (host-test, build, patch-verify, kernel-compile,
compare-stock tests/demo).

---

## 9. The attended fire recipe (the Lead fires)

Full rung detail and stop rules: [TEST-LADDER.md](TEST-LADDER.md). Shape:

* **R0 (defaults)**: load with `rx_enable=1` and all new parameters at defaults;
  arm; no new write occurs. PASS: behaviour identical to the current module -
  proves the new code is inert unless opted in.
* **R1 (read-only parity)**: AP-first order -> `dp_setup` -> `dp_rx_arm` ->
  `cat dp_rx_parity` -> host-side `compare_stock.py`. PASS: capture succeeds
  and the four candidate words + other-block diffs are recorded. This is the
  H-enable/H-filter measurement; no writes.
* **R2 (crash watch)**: sample page/mask/`excp_announce` before and after each
  drain; run a few hundred bounded drains with traffic attempts in between.
  PASS: no exception, page golden. This tests T1/T2/T3/T4/T5 fixes against the
  Lead's crash class.
* **R3 (kick)**: `rx_ring_kick=1` -> arm -> kick -> live-bank/parity read.
  PASS: producer read-back correct and live bank unchanged or moving (record).
* **R4 (candidate control)**: `rx_ctl_enable=1` with exactly one candidate word
  and one bit per boot (power-cycle between), `rx_ctl_set=<bit>`. PASS: either
  the live bank starts moving on on-air auth/scan (H-enable confirmed for that
  bit) or a clean negative. **Stop on any exception/page loss.**
* **R5 (on air)**: repeat the client auth/scan/data test and read
  `dp_rx_capture`/`dp_rx_parity`/dmesg for `g1 id13` + completion movement.
  PASS: any of live rptr movement, cmp wptr movement, a D2H g1 id13 event.
* **R6 (legacy read control, optional)**: `rx_legacy_bank=1` only if a
  read-side effect is being investigated; compare crash-watch results against
  R2.

---

## 10. Limits, what this lane does not claim

* **Not fired.** Every bench statement here is a design/reference; the only
  hardware rows are quoted from the Lead and earlier lanes. There is no claim
  that any candidate word is the RX enable.
* The full-module link/build against the final association tree was not
  possible here: that tree is not committed. The patch applies to the
  reconstructed current RX state and the glue objects compile clean against a
  hybrid committed header base; the integration lane must rebase it onto the
  final association tree (BUILD-NOTES.md).
* The probe reads only the first 16 words of the non-config blocks. If the
  missing enable sits deeper in one of them, the probe is a pointer, not a
  proof; extending the span is a one-line change once a candidate block is
  identified.
* `rx_ctl` candidates are selected by the stock-diff heuristic, not by decoded
  semantics; they are [U]. The experiment is deliberately one-shot and
  attended for that reason.
* Steps 7 (interrupts/NAPI) and 13 (teardown) are intentionally not
  implemented; the bench model is poll-driven and module reload is the reset.
* Sub-MSDU chains remain refused (inherited [U]); this lane did not decode the
  chain slot accounting.

---

## 11. Reproduction

```
cd opensource/docs/incoming/wifi-rx-linux-parity
./tools/build.sh                  # host: reconstruct, patch, 118 checks,
                                  # kernel-context compile, publication checks
python3 tools/test_compare_stock.py   # 11 unit tests
# after an attended R1 capture:
tools/compare_stock.py --probe /tmp/dp_rx_parity.txt \
    --dump ../../dumps/reg_all.txt --all
```

Integration with the full association tree (Lead/integration lane):

```
cd <final association tree>
patch -p1 < <lane>/patches/0001-rx-linux-parity.patch
# or: RX_FULL_BASE=<tree> ./tools/build.sh   (adds the full-module build)
```

---

## 12. References

* Linux sources: `build/linux-kbuild/linux-5.10.201/drivers/net/wireless/`
  (mt76 `dma.c`, `mt7615/dma.c`, `mt7603/main.c`; iwlwifi `pcie/rx.c`;
  ath10k `pci.c`, `ce.c`; ath11k `dp.c`, `dp_rx.c`, `hal.c`;
  brcmfmac `commonring.c`, `pcie.c`). GPLv2; analysis-only use.
* Project: `docs/incoming/wifi-rx-static/REPORT.md`,
  `docs/incoming/wifi-rx-watermark/REPORT.md`,
  `docs/incoming/wifi-sibling-gpl/REPORT.md`,
  `opensource/dumps/reg_all.txt`, `docs/incoming/wifi-datapath*`,
  `docs/incoming/wifi-host-replica`.
