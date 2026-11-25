# MAC RX ring entry + 36-byte device-header format: exact contract, host-driver conformance, and the fail-closed hardening patch

**Lane:** wifi-rx-format (host-only clean-room). **Task:** shared task-163.
**Written:** 2026-11-25. **No bench contact, no image, no vendor bytes.**
**Deliverables:** this report, `patches/0001-rx-format-hardening.patch` (2 new
files + 4 edited files, applies to the final association + mgmt-carrier +
watermark tree), `src/hi5622_rx_format.h/.c` (portable model),
`tools/rx_format_selftest.c` (**132 checks, 0 failures**),
`tools/run-host-tests.sh`, `tools/build-patched-module.sh` (host build,
**0 warnings**, 57 params), `tools/route-rx-probe.sh` (router-side READ-ONLY
witness script), [FIRE-RECIPE.md](FIRE-RECIPE.md),
[TEST-LADDER.md](TEST-LADDER.md), [BUILD-NOTES.md](BUILD-NOTES.md),
[evidence/](evidence/).

Evidence markers: **[V]** decoded from the local vendor host module
(hi5622v100_wifi 2.4.15/2.5.24, not committed; committed relocatable
disassembly `../wifi-shim-port/raw/hi5622v100_wifi.dis`, symbol+offset cited);
**[V*]** measured by an earlier attended bench lane / the committed stock
register dump `opensource/dumps/reg_all.txt`; **[I]** inference; **[U]**
unknown.

---

## 0. TL;DR

The existing clean-room model of the RX ring entry and of the per-buffer
36-byte header (lane wifi-rx-static, task-137) is **format-correct**: every
field, offset, geometry constant and pointer rule in it is confirmed below
against the vendor's own decoder and against the vendor stock register dump.
The contract the MAC expects is:

1. **Ring entry = 4-byte little-endian device VA of buffer+4** - not the
   buffer base, not a physical address, not a length/flag word.  The vendor
   pushes exactly this value into the free ring and the device echoes the same
   value into the completion ring; the ISR converts it back and reads the host
   word at hostva-4 [V `shuangta_host_rx_add_buff` 0x374d0,
   `shuangta_irq_host_mac_isr` 0x39a1c].
2. **Buffer = [0..3] host-owned word, [4..0x27] 36-byte device-written
   header, [0x28..] MPDU payload.**  Capacity is 4 + configured length:
   `0x640` (norm) / `0xb4` (small); the configured device-writable lengths are
   `0x63c` / `0xb0` (config block +0x1c).  The host pre-writes **only** the
   host word and a zeroed 36-byte header; everything in the header is written
   by the device.
3. Ring geometry: norm free ring **256 x 4 B**, small free ring **512 x 4 B**,
   completion ring **4095 x 4 B**; packed pointers are **15 index bits + phase
   bit 15**; a full free-ring fill publishes `0x8000` (index 0, phase 1).
4. The only two **real divergences** our driver had from that vendor contract
   are fixed by this lane's patch:
   * **D1 - recycled buffers were not re-scrubbed before re-publishing.**
     The vendor zeroes the 36-byte header on *every* add_buff
     (`str r4,[data]; memset_s(data+4,36,0)`), so a stale `header+0x1a` length
     or `header+0x20` sub-MSDU chain pointer can never survive a recycle.  Our
     refill wrote the entry only.  A stale chain pointer would make the drain
     refuse the next frame permanently; a stale length could be misread.  Now
     every re-published buffer is prepared (scrub + host-word cookie) before
     its entry becomes visible.
   * **D2 - no fail-closed reset guard in the free-ring refill.**  A live
     consumer-pointer jump (host-MAC block reset / desync) was interpreted as
     "the device consumed N slots" and could re-publish buffers the host still
     believes are in flight (double-hand) or publish on top of slots never
     posted.  The patch enforces `posted + delta <= depth` and closes the lane
     instead.
   It also adds the **decisive read-only bench instrument** for the open
   "MAC RX captures nothing" bug: `dp_rx_probe` reports config-vs-vendor
   mirror mismatches, free-ring entries, completion-ring entries that the
   live wptr never reported, and the **device-write witness** per buffer
   (host-word cookie intact + 36-byte header nonzero = the MAC *did* DMA a
   received frame into host memory even though no completion/wptr/D2H event
   surfaced).  See section 7 for the verdict each outcome implies.

**Honest scope:** this lane proves the format and fixes the two real
divergences; it cannot itself explain "no mgmt RX event" (that is the
D2H CRX path, not these rings) nor the firmware exception (section 7.2 shows
that with `cmp_wptr == 0` the drain performs **zero writes**, so the observed
E2-era crash is not caused by drain writes).

---

## 1. Inputs and method

| input | use |
| --- | --- |
| local vendor host module hi5622v100_wifi (2.4.15 / 2.5.24, not committed) | relocatable disassembly with symbols; every function/offset below is a symbol+offset in it |
| committed disassembly reference `../wifi-shim-port/raw/hi5622v100_wifi.dis` | line/address citations (RX functions 0x374d0-0x39a1c) |
| committed stock register dump `opensource/dumps/reg_all.txt` (vendor driver running) | the [V*] live values that pin every config word |
| ours: final association tree + mgmt-carrier + watermark module (`d2c608e7` artifact, 347,388 B) | the driver under test; its source tree is reproduced byte-for-byte by `../wifi-rx-watermark/tools/build-final-module.sh` (.text `7403aaff...`, see [evidence/chain.txt](evidence/chain.txt)) |
| prior lane reports | wifi-rx-static (live bank + 36-B header model), wifi-sibling-gpl (CRX vs ring split), wifi-re-report |

Method: decode the vendor's RX producer path (`shuangta_host_rx_add_buff`,
`shuangta_rx_host_init_dscr_queue`), its HAL ring core
(`hal_ring_get_hw2sw`, `hal_ring_set_sw2hw`, `hal_ring_get_entry_count`,
`hal_ring_get_entries`, `hal_ring_set_entries`), and its header decoder
(`shuangta_host_rx_get_msdu_info_dscr` + the `hmac_rx_ctl` accessors); pin the
result against the stock dump; then compare with our driver and diff the
pre-write/write-back sequences.  Every constant is a derived fact; no vendor
code bytes are reproduced.

---

## 2. The RX ring entry (4 bytes, device VA of buffer+4)

### 2.1 Vendor producer path [V]

`shuangta_host_rx_add_buff(dev, ring_type)` 0x374d0; `ring_type == 0` = norm
free ring, `2` = small free ring.  Norm path (0x376f8-0x376e4):

    skb = hi_net_woc_alloc_skb(..., len = buflen + 3)     /* buflen 0x63c norm */
    align skb->data to 4 bytes
    data = skb->data
    *(u32 *)data = (u32)skb                                /* host word at +0    */
    memset_s(data + 4, 36, 0)                              /* 36-B header zeroed */
    ...
    entry = pcie_if_hostca_to_devva(0, data + 4 + <hostca transform>)
    hal_ring_set_entries(norm_ring, entry)                /* 4-byte store       */

`hal_ring_set_entries` 0x322f4 only accepts a **type-1 (SW2HW producer)**
ring, computes `slot = producer_index * 4`, stores `entry` there, then
increments the local packed producer (wrap at depth toggles the phase bit) -
so the ring is an array of **4-byte** values and the value is exactly the
device-visible VA of the buffer's header start.

`shuangta_irq_host_mac_isr` 0x39a1c (status bit 2 branch) consumes
`hal_ring_get_entries(cmp_ring, &entry, 1)` and converts the entry with
`oal_pcie_devca_to_hostva`, then reads `*(u32 *)(hostva - 4)` = the host word.
That is only correct if **completion entry == free-ring entry == devVA
(buffer+4)**; both directions confirm each other.

### 2.2 Consequences the host must respect

* The entry is **4-byte aligned** by construction (the buffer DMA address must
  be 4-byte aligned; `buffer+4` is then 4-aligned and nonzero).  Our model
  refuses an unaligned/zero entry (`hif_rx_entry_encode`).
* The device writes **from buffer+4 onward only**; the host word at buffer+0
  is host-private (the vendor stores the skb pointer there; this driver stores
  a cookie).  The DMA area the device may write is exactly the configured
  buffer length, i.e. buffer+4 .. buffer+4+len-1.
* The ring entries are **not** a descriptor/control word: no length, flags,
  ownership or next-address is pre-written by the host.  All of that arrives
  in the per-buffer 36-byte header (section 3) after the device wrote it.

---

## 3. The 36-byte device-written header (at buffer+4)

The header base is `H = buffer+4`; the payload starts at `H+0x24 =
buffer+0x28`.  The vendor report path `shuangta_report_rx_msdu_dscr` 0x38d20
copies exactly **36 bytes from `skb->data+4`** via `oam_report_dscr`, and
`shuangta_host_rx_get_msdu_info_dscr` 0x3843c decodes it at the offsets
below.  That function then does `skb_put(len)` and advances
`skb->data += 0x28`, i.e. payload at buffer+0x28 [V].

### 3.1 Field table (offsets within H)

| off | width | vendor decode (destination in the 48-B hmac_rx_ctl) | meaning |
| --- | --- | --- | --- |
| 0x00 | u32 | `shuangta_host_rx_get_ppdu_dscr_addr` 0x387b4 reads `*(u32 *)(data+4)` | device VA of the PPDU info descriptor (rate/RSSI metadata; decoded elsewhere by the vendor) [V]/[I] |
| 0x04 | u16, bits11:0 | -> ctl+0x19 | 12-bit field [V]; semantic [U] |
| 0x05 | u8 | bit4 -> ctl+0x08.b0; bit6 -> ctl+0x09.b0; bit7 -> ctl+0x09.b1 | rx flags [V]; semantics [I] |
| 0x06 | u8 | `shuangta_host_get_rx_sub_msdu_num` 0x383f0 reads `skb->data[10]` | number of sub-MSDUs in this buffer [V] |
| 0x07 | u8 | bits7:4 -> status nibble (`shuangta_host_get_msdu_rx_status` 0x3838c: `skb->data[0xb] >> 4`); bit1 -> ctl+0x05.b4 | receive status nibble (the vendor ISR drops status 0 and 3) [V] |
| 0x08 | u16 | bits2:0 -> ctl+0x0e bits4:2; bits15:4 -> ctl+0x1d (12-bit) | flags + 12-bit field [V]; semantics [U] |
| 0x0a | u16, bits11:0 | -> ctl+0x1f | 12-bit field [V]; [U] |
| 0x0c | u32 | -> ctl+0x21 (32-bit load/store) | 32-bit value [V]; timestamp/TSF [I] |
| 0x10 | u16 | -> ctl+0x25 | [V]; [U] |
| 0x12 | u16 | -> ctl+0x15; then `hmac_rx_get_mac_header_len` parses it as the frame-control word | **copy of the MPDU frame-control field** [V] |
| 0x14 | u16, bits11:0 | -> ctl+0x13 | 12-bit field [V]; [U] |
| 0x15 | u8, bits5:4 | -> ctl+0x10 bits3:0 | [V]; [U] |
| 0x16 | u8 | -> ctl+0x04 | [V]; [U] |
| 0x17 | u8 | low nibble -> ctl+0x0b; high nibble -> ctl+0x0f | [V]; [U] |
| 0x18 | u16 | bits15:4 -> ctl+0x17; low nibble tested on the status==3 sub-MSDU path | [V]; [U] |
| **0x1a** | **u16** | **-> ctl+0x11; then `skb_put(len)`; bound check `len + 0x28 <= allocation`** | **MPDU payload length in bytes** [V] |
| 0x1c | u16, bits11:0 | -> ctl+0x1b | 12-bit field [V]; [U] |
| 0x1e | u8 | -> ctl+0x0d | [V]; [U] |
| 0x1f | u8 | low nibble -> ctl+0x09 bits5:2; bit7 -> ctl+0x0c | [V]; [U] |
| **0x20** | **u32** | **`shuangta_rx_get_next_sub_msdu` 0x38814 reads `*(u32 *)(data+0x24)`, converts devCA->hostca and reads the next skb's host word at hostva-4** | **device VA of the next sub-MSDU buffer+4; 0 = last buffer** [V] |
| 0x24..0x27 | - | outside the header (the header is exactly 36 bytes) | payload starts here (= buffer+0x28) |

The vendor's exact payload bound is `len + 0x28 <= 0x640` for band 0 (and
0x1064 in its alternate buffer-size configuration), where 0x640 is the whole
allocation [V 0x38630-0x38650].  Our model states it as
`len <= dma_len - 0x24` with `dma_len = 0x63c`; both are the same inequality.
A **nonzero** next pointer always means "another buffer follows"; the drain
still refuses a chain (fail-closed, unchanged), and a stale nonzero chain
pointer in a recycled buffer is a real hazard (fixed by D1).

### 3.2 What the host must pre-write into the buffer

| bytes | writer | content |
| --- | --- | --- |
| buffer+0..3 | **host** | host bookkeeping word (vendor: `(u32)skb`; this driver: cookie `0x5258_xxxx`) - the device never reads or writes it |
| buffer+4..0x27 | **host, zeroed** | 36-byte header area must be zero before the entry is published; the device overwrites it when it writes a received frame |
| buffer+0x28.. | device | MPDU payload, `len` bytes (header+0x1a) |

No ctl/status/ownership word is pre-written by the host.  The only config
values that influence how the device writes are in the MAC config block
(section 4: buffer lengths at +0x1c).  The vendor repeats the host word +
36-byte scrub on **every** add_buff, i.e. the scrub is a per-publish
obligation, not a one-time allocation initialisation - this is D1.

---

## 4. Ring geometry and config-block programming

### 4.1 Geometry [V `shuangta_rx_host_init_dscr_queue` 0x37888 + stock dump]

| ring | entries | entry size | ring bytes | config addr | config depth | producer word | consumer word |
| --- | --- | --- | --- | --- | --- | --- | --- |
| norm free (type 1) | 256 | 4 B | 0x400 | +0x20 | +0x2c (bits11:0) | +0x38 (host) | live CA 0x40050000 +0x08 (device) |
| small free (type 1) | 512 | 4 B | 0x800 | +0x24 | +0x30 (bits11:0) | +0x3c (host) | live +0x0c (device) |
| completion (type 2) | 4095 | 4 B | 0x3ffc | +0x28 | +0x34 (bits11:0) | live +0x10 (device) | +0x40 (host) |

Buffer lengths share one word: **config +0x1c = norm (bits15:0) | small
(bits31:16) = 0x00b0_063c** (device-writable lengths; allocation = 4 + that).
Vendor stock dump [V* `opensource/dumps/reg_all.txt`]:

    40040000=00000100  40040004=83fd7000  40040010=02060850  4004001c=00b0063c
    40040020=844c7000  40040024=844c8000  40040028=83734000  4004002c=00000100
    40040030=00000200  40040034=00000fff  40040038=00008000  4004003c=00008000
    40040040=00000000  40040044=00000000  40040048=fffffcc1  4004004c=00000000
    40040050=00000000  40040054=00000000
    40050000=00000400  40050008=00000020  4005000c=00000020  40050010=00000000

Note what that dump already proves: at idle the vendor's own free-ring
consumers are **0x20** (the MAC's 32-buffer prefetch watermark) and the
completion producer is **0**.  "norm/small rptr = 0x20, cmp wptr = 0" is the
vendor stock mirror, not an anomaly [V*].

### 4.2 Packed pointer format [V `hal_ring_set_sw2hw` 0x31f1c, `hal_ring_get_entry_count` 0x31f80, `hal_ring_get_hw2sw` 0x31e1c]

* 16-bit values: **bits14:0 = index, bit15 = phase** (wrap flag).  Ring
  depths are <= 4095 so the index always fits 15 bits.
* Free-ring producer publishing is a **plain 16-bit store** to config +0x38 /
  +0x3c by `hal_ring_set_sw2hw` (type 1: writes the local packed producer);
  the completion consumer is a plain 16-bit store to +0x40 (type 2).
* A fill of exactly depth entries ends at index 0 with **phase toggled**:
  the 256-entry fill publishes **0x8000**, the 512-entry fill publishes
  **0x8000** (both stock-dump values above).
* `hal_ring_get_entry_count` (the vendor's "how many slots can I fill / how
  many completions are pending" arithmetic) is phase-aware and resolves the
  full/empty ambiguity by index+phase comparison; the free-ring variant
  returns free slots, the completion variant returns pending entries.  Our
  `hi5622_rx_ring_pending/consumed/idx_advance` implement the same rules
  (host-tested, including one-lap/phase cases).
* The vendor's *setter* path (`shuangta_set_rx_*_rptr/wptr` ->
  `hal_set_*_cfg_*`) writes the same 15+phase value through a 32-bit RMW; its
  extra "phase XOR old bit15" step is a toggle semantic of the setter, not of
  the publish path.  A 32-bit RMW that preserves the upper 16 bits is safe
  (our `dptx_publish16` does exactly that and read-back checks the low half).

### 4.3 Write-back / status semantics [V]

* The device **echoes** the free-ring entry into the completion ring
  (`hal_ring_get_entries` returns the 4-byte value; the ISR decodes it as a
  devVA).  It is not necessary for the host to pre-initialise completion
  entries, but they must be **zeroed** after being consumed: the vendor's
  `hal_ring_get_entries` 0x32164 memsets each consumed entry to 0 (0x321f0 /
  0x322bc) - the patch now does the same (clear-after-read).
* The host-MAC interrupt status word is the live bank CA 0x40050000 +0x00;
  **RX-completion is status bit 2**; the mask is config +0x48 (vendor value
  `0xfffffcc1` = bits 1..5 enabled); status is cleared with a **W1C** write to
  config +0x44.  Our driver polls instead of taking the interrupt; it never
  writes +0x44 unless `rx_clear_status=1`, which is feature-gated and matches
  the vendor clear path when enabled.
* Free-ring refill ("add buffers") is driven by the free-ring rptr the device
  publishes in the live bank (+0x08/+0x0c).  The vendor fills
  `min(free_slots, queued)` and publishes once.  "Publishing the free-buffer
  producer is the RX enable" holds: there is no extra enable bit or message
  (confirmed again here; the vendor's *whole* RX start is geometry + add_buff
  + publish, plus IRQ unmask bits 3/4 that only affect host interrupts).

---

## 5. Our driver: conformance check and the two real divergences

Base: final association + mgmt-carrier + watermark tree (`d2c608e7` module,
347,388 B, .text `7403aaff...`; regenerated and .text-verified for this lane,
[evidence/chain.txt](evidence/chain.txt)).

| # | contract | our code | verdict |
| --- | --- | --- | --- |
| C1 | norm 256x4, small 512x4, cmp 4095x4 | `dp_rings.h` DP_RX_* constants; `dp_alloc_rings` allocates rx_norm_ring 0x400 / rx_small_ring 0x800 / rx_cmp_ring 0x3ffc via dma_alloc_coherent | MATCH |
| C2 | entry = devVA(buffer+4) | `dp_fill_rx_rings` uses `dp_rx_buf_entry_devva(buf_dma) = buf_dma+4`; `dptx_rx_find_buffer` compares against `dma[i]+4` | MATCH |
| C3 | buffer alloc = 4 + 0x63c / 4 + 0xb0; header zeroed | `DP_RX_NORM_ALLOC`/`DP_RX_SMALL_ALLOC` + `memset` in `dp_dma_alloc` | MATCH at allocation; **D1** on recycle |
| C4 | config +0x1c/+0x20/+0x24/+0x28/+0x2c/+0x30/+0x34/+0x38/+0x3c/+0x40 with vendor values | `dp_mac_program` writes exactly these (RMW where masked) and read-back verifies; the new bind-time self-check also compares the readbacks against the vendor stock mirror | MATCH (and now enforced) |
| C5 | 15-bit index + phase bit15, full fill publishes 0x8000 | `dp_ring_idx_pack/next`; bind tracks rx_*_prod = 0x8000 | MATCH |
| C6 | completion entries cleared after read; consumer published after the refills | **was missing**; patch adds clear-after-read + `dma_wmb` before the consumer publish | FIXED |
| C7 | 36-byte header decode: len header+0x1a, payload buffer+0x28, next header+0x20 | `hi5622_rx_hdr_parse` (rx-static) uses exactly these; the patch restates them in `hif_rx_hdr_parse` with a literal-offset host selftest | MATCH |
| C8 | device-write proof independent of pointer movement | absent | **added** `dp_rx_probe` (host-word cookie + zero-header baseline witness) |
| C9 | free-ring refill must not publish on an unexplained rptr jump | absent (a reset looked like a big consumed delta) | **FIXED**: `hif_rx_refill_budget` guard |
| C10 | recycled buffer re-scrub (host word + 36-byte header) before publish | **was missing** | **FIXED** |

D1/D2 are the only format-level divergences found; both are fixed by
`patches/0001-rx-format-hardening.patch`.

### 5.1 Patch contents

| file | change |
| --- | --- |
| `hi5622_rx_format.h/.c` (new, portable) | entry encode/decode/classify, host-word cookie, buffer prepare/witness, 36-byte header parse with literal offsets, fail-closed refill budget, vendor stock-mirror config table |
| `hi5622_dp_txrx.c` | bind-time format self-check (pools prepared with cookies + zeroed headers, all 256+512 just-filled ring entries verified against their buffers, 8 config words verified against the stock mirror - failure fails `dp_setup` closed); both refill paths re-publish through `dptx_rx_publish_one` (scrub -> entry -> advance); reset guard in the window refill; drain verifies the host-word cookie, zeroes each consumed completion entry and issues `dma_wmb` before the consumer publish; new read-only `dp_rx_probe` |
| `hi5622_dp_txrx.h` | `hi5622_dp_rx_probe_show` prototype |
| `hi5622_dp.c` | `dp_rx_probe` attribute (0444, read-only) |
| `Kbuild` | adds `hi5622_rx_format.o` |

No module parameter is added (still 57), no register constant is changed, no
enable semantics changed; every added check only *refuses*.

---

## 6. Verification performed (this lane)

* host model test: **132 checks, 0 failures**, `gcc -std=c99 -Wall -Wextra
  -Werror` ([evidence/host-test.txt](evidence/host-test.txt)) - literal
  offsets for every header field, entry encoding, cookie/witness rules,
  stale-header recycle, reset-guard boundary cases, vendor-mirror config words.
* patch applies cleanly (`patch -p1 --dry-run` + apply, 0 conflict markers)
  to the exact base tree whose .text is byte-identical to the fired
  `d2c608e7` module ([evidence/patch-verify.txt](evidence/patch-verify.txt),
  [evidence/chain.txt](evidence/chain.txt)).
* patched module builds against the project 6.18.60 tree with
  **0 warnings / 0 errors / 0 undefined symbols**; stripped **356,424 B**,
  sha256 `0eadba35...` (build path dependent), 57 params, required
  symbols/strings present ([evidence/build.txt](evidence/build.txt),
  [evidence/module-identity.txt](evidence/module-identity.txt)).
  Not fired: the Lead fires; a final-image lane bakes.
* router-side probe script dry-run verified (prints actions only, no writes):
  [evidence/router-dryrun.txt](evidence/router-dryrun.txt).

Reproduce: `./tools/run-host-tests.sh` then
`REBUILD_CHAIN=1 ./tools/build-patched-module.sh`.

---

## 7. What this means for the open bug ("the MAC RX captures nothing")

### 7.1 The ring/entry/header format is not the cause of "no ring capture"

Three independent facts:

1. The entry/header contract is exactly what our driver already implements
   (sections 2-5); and the hardware demonstrably accepts the ring: the live
   bank reads norm/small rptr = **0x20**, exactly the vendor stock mirror - the
   MAC consumed the published free entries up to its prefetch watermark [V*].
2. Management frames do not normally use these buffers at all.  The sibling
   GPL analysis (task-151) decoded the two-family split: AP auth/assoc/probe
   management frames arrive as **D2H CRX events (g1 id 13 ->
   hmac_rx_process_mgmt_event)**, while the MAC ring path carries **data**
   MPDUs (`hmac_rx_ring_entry_proc` never dispatches management frames)
   [V sibling GPL + local .data dispatch table].  A laptop sending auth frames
   therefore need not move the free-ring rptr or the completion wptr; the
   correct first datum for those is the D2H mgmt event counter, not a ring
   capture.
3. The new `dp_rx_probe` settles the empirical question without interpretation:
   * **DEVICE-WROTE-FRAMES** -> a received frame *was* DMA'd into host memory;
     the capture gap is then the completion/report pointer path (wptr,
     consumer publish, ISR/poll) - inspect `cmp_ring nonzero_entries` and the
     live wptr in the same line.
   * **MAC-WROTE-NOTHING** -> rings are armed and configured; no received frame
     reached host memory in this load.  For mgmt-only traffic that is the
     *expected* architecture; with an associated data source it would point at
     the firmware/VAP forwarding path, not the entry/header format.
   * **FORMAT-SETUP-MISMATCH** -> a config/ring word does not mirror the
     vendor stock dump (on the patched module the bind self-check would
     already have failed `dp_setup` closed).

### 7.2 The firmware exception: which RX-side writes could fault the MAC

RX-side writes in our driver, in order of fault capability:

| write | can it fault the firmware/MAC? |
| --- | --- |
| CPU writes to ring memory (entries) | **not directly**; only dangerous if the published producer then lets the MAC read an entry whose devVA is not a live host buffer |
| free-ring producer publish (+0x38/+0x3c) | **the only dangerous RX write**: the MAC prefetches the entries the producer exposes and DMAs received frames to their devVAs.  A producer advanced past entries the host did not write (or over buffers already in flight) can make the MAC DMA to a stale/foreign devVA -> bus error -> firmware exception class.  The patch's reset guard + per-entry prepare close the known ways to do that |
| completion consumer publish (+0x40) | no (host-owned pointer; affects only which completions the host may re-read) |
| W1C status +0x44 (only `rx_clear_status=1`) | no (matches the vendor clear path) |

Important negative result from the code path: with **cmp_wptr == 0** (the
observed bench state) the drain's `pending == 0` branch only reads the live
bank and the interrupt mask and returns - it executes **zero device writes**
(`dptx_rx_wm_refill` also performs no publish while the refill queue is
empty).  So the observed exception after "extended drain loops" cannot have
been caused by drain writes in that state; the RX-side writes reachable in
such a session are the initial `dp_setup` program (same writes as the vendor,
verified against the stock mirror, now also self-checked at bind) and the
free-ring publish *only if* a completion ever appears.  Treat the exception as
its own finding (mgmt/HCC or firmware side) - the capture plan's crash
detection (page/mask/excp_announce sampling) is the right instrument.

---

## 8. Open items / honest limits

* [U] The semantic names of most 36-byte header fields (only length, chain
  pointer, sub-MSDU count, status nibble, frame-control copy and PPDU
  descriptor address are pinned).  The raw offsets + widths above are exact;
  the data-path/netdev hand-off will need the hmac_rx_ctl field names for
  rate/BA/DA-index reporting, which this lane mapped only structurally
  (destination offsets in the 48-byte ctl).
* [U] Whether the hi5622 firmware forwards *management* frames on the D2H CRX
  path in our host-SME configuration (the sibling design says yes; the bench
  currently observes none).  `dp_rx_probe` / the mgmt ladder decide.
* [U] Sub-MSDU chain slot accounting: the header's next pointer is decoded but
  the drain still refuses a non-empty chain (fail-closed, unchanged).
* [I] Status nibble values other than "0/3 dropped by the vendor ISR" are not
  named; the base capture path does not need them.
* This lane is host-only: the patched module is **not fired**; the Lead owns
  the bench and the final-image bake.

---

## 9. Reproduction

    cd opensource/docs/incoming/wifi-rx-format
    ./tools/run-host-tests.sh                        # 132 checks, 0 failures
    REBUILD_CHAIN=1 ./tools/build-patched-module.sh  # base tree + patch + build
    # base tree only (step 0) is reproduced by:
    #   ( cd ../wifi-rx-watermark && REBUILD_ASSOC=1 ./tools/build-final-module.sh )

Bench (the Lead fires; read-only): `tools/route-rx-probe.sh --all` on the
router after the association ladder reached `dp_rx_arm`; interpretation table
in [FIRE-RECIPE.md](FIRE-RECIPE.md).

---

## 10. Follow-up (2026-11-25): the R2b fault result, why the PPDU pre-write is not the fix, and the RX-fault analysis

### 10.1 What the bench established (and the plan correction)

R0/R2 (rx-vendor-diff module, `rx_drain_write=0 rx_status_service=0`) gave the
first positive MAC-receive evidence and then the decisive fault result:

* R0 (write-free parity): the device-side RX/MAC state matches the vendor
  stock dump (live +0x14/+0x24/+0x28/+0x2c/+0x30 all match; config 21/21
  match); the one deviation is device-owned live +0x04 = 0x8000 vs stock 0
  (recorded, meaning [U]).
* R2 (observe-only, zero writes, laptop sent 3 auth frames): observe
  `actions=1 frames=0 bad=0 chains=0` -> **not a single byte was written into
  any host RX buffer**, the 36-byte header stayed all-zero, and the sticky
  host-MAC status went 0x00000182 -> 0x00000582 (+ bit 10, rx_ppdu_desc
  error). **The firmware handshake page went golden:0 during the auth
  attempts** -> the RX path faults the device/firmware DMAC (data abort),
  not just drops frames.

The earlier plan of this lane was to pre-write a host PPDU-descriptor devVA at
buffer+4+0x00. That plan was **checked, built and then abandoned before
commit** at the Lead's direction, because the static evidence refutes its
premise (this lane had already flagged the first point in its review):

1. the vendor producer `shuangta_host_rx_add_buff` 0x374d0 **zeroes the whole
   36-byte header on every publication** (`str r4,[r0],#4` stores the skb at
   buffer+0; `memset_s(r0, 36, 0)` at 0x37688) - a published vendor free
   buffer's header+0x00 is zero, so the vendor does not pre-write it either;
2. no host PPDU-descriptor pool exists: `shuangta_rx_host_init_dscr_queue`
   0x37888 allocates only the norm/small free rings and the completion ring;
3. header+0x00 is device-authored (the vendor's
   `shuangta_host_rx_get_ppdu_dscr_addr` 0x387b4 has no caller in the shipped
   host module - only the debug dump path uses the field);
4. **status bit 10 is already latched and masked in the committed vendor stock
   dump** (`opensource/dumps/reg_all.txt`: CA 0x40050000 = 0x400 = bit 10;
   mask +0x48 = 0xfffffcc1 has bit 10 = 1, i.e. masked).  "bit 10 clear" is
   therefore **not** a sufficient success criterion; it is a masked,
   informational status the vendor itself shows in a healthy state.

Consequence: the fault is **earlier than the per-buffer header write** and is
device/firmware-internal; the next productive work is a candidate analysis of
the device-side RX sequence, not another host buffer pre-write.

### 10.2 (a) Completion-ring descriptor write side

The host-visible completion-ring contract is fully conformant with the vendor
stock dump (checked here against the same dump the parity action uses):

| item | vendor stock | ours | conclusion |
| --- | --- | --- | --- |
| ring base register (+0x28) | device VA of a host-DRAM ring | dp_setup-programmed host DRAM devVA, read-back verified | conform |
| depth (+0x34) | 0x00000fff (4095 x 4 B) | same | conform |
| host consumer (+0x40) | 0 | 0 (only a drain with `rx_drain_write=1` would move it) | conform |
| device producer (live +0x10) | 0 | 0 | conform |
| entry format | 4-byte device VA; the MAC echoes the free-ring entry (buffer+4 devVA), ISR converts devCA->hostVA and reads the host word at hostva-4 | our observe path classifies the same values | format conformant; no host pre-initialisation is required (entries are written by the device only) |
| status bits | 0x400 (bit 10 latched, masked) | 0x182 idle -> 0x582 on the auth attempt | bit 10 is not a discriminator |

The bit-10 handler's own log reads the **completion-ring** indices: host
consumer (config +0x40) and device producer (live +0x10) - i.e. the error is
raised inside the device when it tries to process a completion descriptor,
before/independent of any host-visible entry. Since our completion-ring
registers are byte-identical to the vendor stock (and the only device-owned
live difference is +0x04), there is **no known host-side completion-ring
format or ownership violation to fix**. What remains is the device-internal
target of that descriptor write (see 10.4).

### 10.3 (b) RX buffer DMA path: what could abort

Host-controlled buffer properties are all within the verified contract:
allocation 0x640 (norm) / 0xb4 (small), entry = devVA(buffer+4), 4-byte
aligned, header zeroed, payload bound `len + 0x28 <= 0x640`, device-writes
area 0x63c/0xb0.  Two things a host can still get wrong on this path, ranked:

1. **Address space of the write master vs the read master.**  Our only positive
   cross-master evidence is TX: the device *reads* our host-DRAM at raw
   dma_alloc_coherent bus addresses (frame on air, TX COMPLETED).  RX is a
   *write* to the same window; if the RX DMA master validated addresses in a
   different (vendor "host CA") encoding, a write could abort while reads work.
   The vendor publishes RX buffers through a different address transform than
   ours: `shuangta_host_rx_add_buff` computes the entry from
   `skb->data + 0xc0000004 + 0x600000` through
   `pcie_if_hostca_to_devva` (a host-CA transform), while our entries are raw
   coherent DMA addresses.  Both are in 0x8xxxxxxx and the same window as the
   rings, so this is a low-probability but *testable* hypothesis: one attended
   rung could publish buffers whose entries are transformed exactly like the
   vendor's (requires the vendor WOC host-CA relation, still [U]), or the
   stock-control run can be used to compare.
2. **Device-RAM/PPDU-descriptor target** (see 10.4): if the device fetches a
   descriptor pointer from an area that is not initialised, the abort shows up
   as a firmware data abort with no host write at all - exactly the R2b shape.

The 0x20 free-rptr prefetch is the vendor stock mirror, i.e. normal; it does
not fault by itself.  The "complete entry phase bit" (15 index bits + phase
bit 15) is only a *host* publishing convention; the device writes completion
entries as plain 4-byte values, so a phase mismatch cannot cause a device
fault.

### 10.4 (c) Between receiving a PPDU and writing the buffer: the device-side path

Decisive observation from the firmware image (local artifact, strings only):
the RX path is **firmware-mediated**, not a pure hardware DMA:

* `smac_rx_complete_5g_isr` / `smac_rx_complete_2g_isr` (firmware RX
  completion ISRs), `HCC RX Thread`, `smac_rx_timeout_*_isr`;
* `st_device_alloced_ppdu_dscr_list` - the firmware owns a **list of allocated
  PPDU descriptors** (this is why header+0x00 is device-authored: the device
  writes a device-RAM descriptor address into it);
* `smac_msg_proc_enable_trx_req` / `..._disable_trx_req` and
  `smac_msg_proc_rx_ring_reset_req` - firmware message handlers that (re)arm
  the receive path on a host request.

So the sequence on a received PPDU is: MAC hardware -> firmware/DMAC consumes
the PPDU + PPDU descriptor (device RAM, firmware-owned list) -> firmware
writes the host RX buffer and completion entry (and, for management, builds
the D2H CRX event).  The R2b page golden-loss is a **firmware data abort in
this path**, and it happens before the firmware's first host-buffer write.
The strongest candidate is therefore that the firmware-side RX ring/PPDU
descriptor state was **never (re)initialised in our session**, because the
clean-room host never sends the vendor's pre-RX configuration sequence - our
driver sends only the minimal AP/data-plane message set, and the vendor's
`enable_trx` / `rx_ring_reset` requests are **not built by either vendor host
module we have statically decoded** (no sender strings in
hi5622v100_wifi/plat), i.e. they come from another vendor host component (or
another message name/route) not yet in our artifact set.

Candidate differences to emulate/verify (ranked):

| # | candidate | evidence | cheap test |
| --- | --- | --- | --- |
| F1 | firmware RX ring/PPDU descriptor state uninitialised -> firmware dereferences an unarmed list on the first PPDU (data abort before host write) | firmware strings `st_device_alloced_ppdu_dscr_list`, `smac_msg_proc_rx_ring_reset_req`, `smac_msg_proc_enable_trx_req`; R2b observe frames=0 + page golden:0 | **stock-control run**: vendor image, vendor AP, same auth frames -> if no page loss, capture the vendor's pre-RX H2D sequence (the vendor host component that sends the rx-ring reset is not in our modules; hunt the full vendor rootfs module set) |
| F2 | RX-write address encoding differs from the TX-read encoding (vendor host-CA transform at buffer publication) | vendor add_buff uses `pcie_if_hostca_to_devva` on a WOC host CA; our entries are raw coherent DMA VAs | a later rung that publishes entries transformed exactly like the vendor (needs the WOC host-CA relation) - or a vendor-image memory comparison of published free-ring entries |
| F3 | device-owned live +0x04 = 0x8000 (ours) vs 0 (vendor stock) is a firmware RX state word | R0 parity was the only deviation | record live +0x04 before/after the trigger and in the stock-control run; if stock vendor shows 0 while RX works, ask what wrote 0x8000 in our boot (phase-bit pattern suggests a ring state) |
| F4 | status bits 1/8 (asserted in our 0x182) are vendor-"forward to firmware core" classes that our driver never forwards | vendor ISR forward helpers 0x39558 (bit 1) / 0x3920c (bit 8); vendor-diff decode | compare with a stock-control dmesg; if the vendor forwards them, a host-side status-forward implementation is a candidate (needs the firmware message decode) |

The bit-10 status the R2 run latched is *not* a sufficient criterion (10.1
item 4); it is evidence that a PPDU-descriptor error was counted, but the
fatal event is the firmware page loss.  The decisive discriminator remains
the **stock-control run**: if the vendor image can receive the same auth
frames without losing the golden page, the difference is host-side session
configuration (F1/F4), not the buffer format; if the vendor image also loses
the page while *not* AP-associated, the fault class is upstream.

### 10.5 What was NOT shipped

The follow-up PPDU-descriptor pre-write patch (host-DRAM descriptor pool,
`rx_ppdu_desc`/`rx_ppdu_desc_stride` params, `dp_rx_ppdu` survey) was
implemented and host-tested (86/86 checks) and then **not committed** at the
Lead's direction once the R2b data showed the header write never happens; the
plan-correction evidence above (10.1) supersedes it.  Nothing in this
follow-up changes the shipped lane artifacts: the committed deliverables are
still the 0001 patch, the model/tests and the contract in sections 1-9.
