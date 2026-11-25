# PPDU-DESCRIPTOR-NOTES - vendor RX descriptor / PPDU-desc facts for the bit10 follow-up

**Lane:** wifi-rx-capture-plan (task-165 addendum). **Host-only**; no bench
contact. Written in response to the Lead's E-RX-1 result (sticky status
0x182 -> 0x582, bit 10 latched) and the proposed next step ("pre-write the
PPDU descriptor devVA per the vendor add_buff").

Evidence markers: **[V]** decoded from the local vendor host module
hi5622v100_wifi 2.4.15 (not committed; same build as the committed relocatable
disassembly references, symbol addresses match the lane reports);
**[V*]** stock register capture opensource/dumps/reg_all.txt and
reg_all_run2.txt (vendor driver live); **[I]** inference; **[U]** unknown.

---

## 0. TL;DR - four facts that change the next step

1. **The vendor's add_buff zeroes the 36-byte header on every publication.**
   shuangta_host_rx_add_buff 0x374d0 writes the skb pointer at buffer+0, then
   memsets 36 bytes at buffer+4 to 0, then publishes
   entry = devVA(buffer+4) [V]. So header+0x00 is **0 in the vendor's
   published free-ring state** too; it is not a host pre-write.
2. **There is no host PPDU-descriptor pool in the vendor RX data path.**
   shuangta_rx_host_init_dscr_queue 0x37888 allocates exactly the three rings
   (norm 0x400 B/256, small 0x800 B/512, completion 0x3ffc B/4095) and calls
   add_buff for the norm and small rings; no fourth allocation, no extra
   ring-config write [V].
3. **header+0x00 is device-authored metadata.** The only reader is
   shuangta_host_rx_get_ppdu_dscr_addr 0x387b4, which returns
   *(u32 *)(skb->data + 4); its purpose is the vendor debug save/dump path
   (hmac_config_save_ppdu_dscr 0x1612e8, cfg event cmd 0x352,
   "SAVE_TX_ppdu / SAVE_RX_ppdu"; wal_config_dump_rx_ppdu_dscr 0x1616f4 ->
   hdpp_sys_sync_write(3, ...)) [V]. The descriptor itself lives on the
   device/firmware side; the host does not provide it.
4. **bit10 is not a clean smoking gun by itself.** The committed stock vendor
   capture (both runs) has CA 0x40050000 = **0x00000400 = bit10 already
   latched**, while mask CA 0x40040048 = 0xfffffcc1 masks bit10 - the vendor
   never services or clears it [V*]. The 0x182 -> 0x582 transition during the
   client auth is still real evidence that the MAC ran RX
   PPDU/completion-descriptor processing on those frames, but "bit10 clear" is
   not a sufficient success criterion; require completion-producer movement,
   a g1 id13 event, or host delivery.

---

## 1. Vendor RX buffer layout and add_buff (byte-exact) [V]

shuangta_host_rx_add_buff 0x374d0, both ring paths (r1 == 0 norm, r1 == 2
small):

| step | code | meaning |
| --- | --- | --- |
| allocate | hi_net_woc_alloc_skb 0x37600, then align skb->data to 4 (0x37614-0x37634) | per-buffer block of 4 + configured descriptor length |
| host word | str r4,[r0],#4 at 0x37684 (r4 = skb) | buffer+0 = skb pointer (host bookkeeping) |
| header | memset_s(data+4, 36, 0) at 0x37688 | **36-byte device header zeroed on every add** |
| dev VA | pcie_if_hostca_to_devva(chip, hostca) 0x375a0-0x375ac / 0x376cc-0x376d8 | affine host-VA -> device-VA conversion |
| publish | hal_ring_set_entries 0x375b8 / 0x376e4, later hal_ring_set_sw2hw 0x377e8 | ring entry value = devVA(buffer+4) |

Consequences for our driver:

* writing a PPDU-descriptor devVA into header+0x00 before publishing is
  **not vendor behaviour**; the MAC overwrites (or consumes) that word as
  device-authored data. A host value there should not be load-bearing.
* the only host-owned pre-write the vendor performs is buffer+0 (skb pointer);
  our driver leaves it zero. The MAC does not read buffer+0 (the host ISR
  recovers the buffer from the completion entry with hostva - 4). A
  vendor-faithful experiment may write a per-buffer cookie there, but expect
  no effect on the MAC.

## 2. Vendor init allocates exactly three rings [V]

shuangta_rx_host_init_dscr_queue 0x37888:

| ring | alloc call | bytes | entries | config word |
| --- | --- | --- | --- | --- |
| norm free | oal_noncache_alloc 0x378e8 | 0x400 (or 0x1000 for the other chip config) | 256 (or 1024) | +0x20 / +0x2c |
| small free | 0x379e0 | 0x800 (or 0x1000) | 512 (or 1024) | +0x24 / +0x30 |
| completion | 0x37a94 | 0x3ffc | 4095 | +0x28 / +0x34 |

then shuangta_host_rx_add_buff(dev, 0) and (dev, 2) at 0x37b2c/0x37b3c.
No PPDU-descriptor pool, no fourth address/depth pair. This agrees with the
rx-vendor-diff lane's headline negative: the RX enable is the ring fill +
producer publish.

## 3. What the 36-byte header's +0x00 actually is [V]

* shuangta_host_rx_get_ppdu_dscr_addr 0x387b4: r4 = skb->data; returns
  *(u32 *)(r4 + 4) with a NULL/zero guard -> a **device-authored** value.
* no caller inside the shipped wifi module; the symbol is exported for vendor
  debug modules. The save path is a cfg event (cmd 0x352) telling the firmware
  to save TX/RX PPDU descriptors; the dump path is a sys-sync write [V].
* the normal MSDU decoder shuangta_host_rx_get_msdu_info_dscr 0x3843c
  decodes data+8..data+35 (length/flags/status/chain/rate fields); it does not
  require a host value at data+4 [V].

So the field is a device->host pointer for optional debug retrieval of a
device-side PPDU record, not a host-provided buffer address. If the rx-format
lane wants to test a pre-write, the minimal vendor-faithful version is the
buffer+0 host word, not header+0x00.

## 4. bit10 (rx_ppdu_desc error) - decode and caveat

* In the vendor ISR (0x39a1c) bit 10 takes the handler at 0x39c10 and logs
  "rx_ppdu_desc error, dev id, read idx [config+0x40 = completion-ring host
  consumer], write idx [live+0x10 = completion-ring device producer]" [V].
  It is therefore a completion-descriptor write-side validation error, not a
  free-ring entry error.
* mask 0xfffffcc1 has bit10 = **masked**; the ISR only ever sees
  status & ~mask, so the vendor never services bit10. Vendor operation is
  expected to leave it latched.
* the stock capture shows 40050000 = 0x400 (bit10 only) in **both** stock
  runs - i.e. the working vendor software has this bit latched [V*].
* our E-RX-1 observation (0x182 -> 0x582 during 3 client auth frames) is still
  meaningful: it proves the MAC entered the PPDU-descriptor/completion write
  path for those frames. It does not, by itself, prove the drop is caused by
  that error class; the independent drop evidence is cmp wptr staying 0 and no
  g1 id13 event.

## 5. Ranked next checks (cheap, mostly read-only)

1. **Capture the 36-byte header content after a client trigger** (the
   rx-vendor-diff observe-only capture should expose it; otherwise re-run
   E-RX-1 with the header sweep enabled). If every header stays all-zero the
   MAC aborted before any host-memory descriptor write; if header+0x00 holds a
   plausible device CA (0x0xxxxxxx / 0x02xxxxxx / 0x0a5xxxxx) the device wrote
   the PPDU-desc pointer and failed later; a partial header (length/flags set)
   tells how far the write got. This is the most direct discriminator.
2. **Compare machw word 7 (CA 0x40052000) and word 9 (0x40054000) against the
   stock capture.** The vendor stock values are non-zero (word7: +0x00=0x2,
   +0x04=0x0201cc08, +0x0c=0x4, +0x10=0x8000, +0x14=0x8004) and our driver's
   write set never touches these blocks; the rx-linux-parity dp_rx_parity
   probe already dumps them. If they are zero on our side, that is a
   host-writable block outside the 14-write program and the leading "missing
   config" candidate left.
3. **Stock control**: boot the stock vendor image with its AP, run the same
   client auth attempt, and read CA 0x40050000 +0x00 before/after. If the
   vendor AP authenticates the client while bit10 is set or also transitions,
   bit10 is confirmed benign and the fix must be sought on the mgmt-event /
   VAP-state side, not in the descriptor format.
4. **Minimal pre-write rung if still wanted**: host word only
   (buffer+0 <- cookie, dma_wmb, publish entry = devVA(buffer+4)), header
   untouched zero. Success criteria: cmp wptr moves / g1 id13 appears / host
   delivery; bit10 alone is not a criterion.
5. **Word-5/word-8 parity is already clean** (R0): the five device-owned
   live-bank words matched vendor stock, so no missing device state in the
   live bank. The remaining difference class is host-programmed state outside
   the 14-write program (word 7/9 are the only vendor-nonzero candidates).

## 6. Limits

* The local vendor artifact is the 2.4.15 host module; firmware-side descriptor
  storage is not decoded. "Device-authored PPDU descriptor" is inferred from
  the exported debug getter, not from a firmware dump.
* The stock capture is a boot-time register dump, not a traffic log; its bit10
  value cannot be dated to a specific event.
* No hardware was touched by this addendum.

