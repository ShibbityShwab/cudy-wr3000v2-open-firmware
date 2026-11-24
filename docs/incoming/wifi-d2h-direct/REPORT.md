# Direct-read D2H receive service: the firmware's staged HCC frames read straight out of the device rings, the consumer-advance answer, and the engine-idle conclusion

**Lane:** wifi-d2h-direct (shared task **task-89**), **host-only**, 2026-11-24.
The bench router was never contacted, no image was built, no device was opened,
and no vendor firmware byte, ROM byte, object code or secret is copied into
this directory.  Every address/constant below is a derived annotation of local
artifacts read in place; the firmware image stays at its local path.

**Status:** design + compile-ready implementation + build verification + 92/92
device-free selftest done.  The bench run is the Lead's attended session; the
procedure is in `BENCH-PROCEDURE.md`.

**Deliverables (all in this directory):**

| path | contents |
| --- | --- |
| REPORT.md | this document: the five answers, the design, the evidence, the corrections |
| BENCH-PROCEDURE.md | attended bench ladder: stages, expected observables, stop rules |
| BUILD-NOTES.md | build result, toolchain, reproducibility, module metadata |
| src/hi5622_d2h_direct.h | contract header: windows, node/frame/record layout, catalogue, state |
| src/pciet_d2h_direct.c | the direct-read receive service (poll / decode / deliver / republish / census) |
| src/hcc-d2h-direct.patch | the exact integration hunks into the wifi-hcc-service module |
| tools/mkpatch.py | regenerates the patch from the committed hcc-service sources (read-only) |
| tools/build.sh | assembles + builds ONE out-of-tree module; no image build, no device contact |
| tools/d2h_selftest.py | 92 device-free checks (windows, index arithmetic, node/frame decode, captured batch, write census, firmware record table) + raw-hex CLI decoder |

**Read-first inputs:** `../wifi-ete-frames/REPORT.md` (12-byte HCC header, node
word1, ready wire forms), `../wifi-ready-emit/REPORT.md` (D2H deposit op,
d2h_notify, two rings, status path), `../wifi-hcc-re/REPORT.md` (D2H handler
catalogue, mailbox order), `../wifi-fw-postboot/REPORT.md` (corrected
post-handshake state: the scheduler/GIC is armed; the missing transition is
downstream), `../wifi-hcc-service/` (the host service this lane extends),
`../wifi-host-replica/REPORT.md`, `../wifi-ete-init/REPORT.md`,
`../wifi-ete-engine/REPORT.md`, `../wifi-transport-port/REPORT.md` (six
inbound regions), `docs/phase42/ete-registers-reconciled.md` (register fields,
vendor live values), ledger 2026-11-24 10:40 (GP8 golden boot), 12:20 (hcc
bench) and 13:10 (the D2H frame discovery).

Evidence markers follow the project convention: **[V]** verified here from a
named local artifact (fresh firmware disassembly / byte assertion), **[V\*]**
device-measured by an earlier attended lane and cited, **[I]** inference,
**[U]** unknown.

Address conventions (project-wide, unchanged): firmware CPU address -> host
BAR0 = **0x6B8000 + CPU** (region 5); device IO CA -> **0x3B8000 + (CA -
0x40000000)** (region 3, valid only for CA 0x40000000..0x4011FFFF); PKTRAM CA
-> **0x1D8000 + (CA - 0x01000000)** (region 2); ACP CA -> **0x4D8000 + (CA -
0x02000000)** (region 4).

---

## 0. TL;DR - the five answers

1. **Receive service (direct read).** The firmware stages every D2H HCC frame
   as an 8-byte node in the DEVICE group-A DR ring and the frame body in the
   device's PKTRAM message pool (node.word0 = its ACP alias).  The host reads
   both directly: nodes at the **channel +0x10 base** in PKTRAM, bodies at
   **node.word0** in the ACP window, and the producer count from the
   firmware's packed **producer shadow at record+0xC** in image RAM.  A
   keep-up poll decodes the 12-byte header (tag 0x5A5A, total at +4, id at +6),
   validates total == node length, and delivers g4 id1/id2 (the ready pair),
   g4 id3 EXCP_INFO and every other group/id into a kernel-side catalogue +
   bounded hexdump ring (sysfs `rd_status`/`rd_decode`/`rd_catalog`).  Wrap
   and overrun are detected with the vendor packed-index arithmetic; a slot
   that is not committed yet is retried, not skipped.  Implemented in
   `src/pciet_d2h_direct.c` and wired into the existing
   `hi5622_pciet_hcc` module (compile-verified, see section 7).
2. **Consumer advance.** The device group-A consumer (+0x1C) is **engine-
   authored only**: `pcie_msg_init` never writes it, the deposit/commit path
   never writes it, the firmware never reads it, and the vendor host never
   writes it [V].  The only host-owned DR index is the group-B **published
   free index +0x38** (vendor `pcie_ete_dr_reg_init` and sending-trigger
   re-publish) [V].  The firmware does **not** gate on ring space: the commit
   helper blindly stores `node[producer & 0x3FF]` and advances the packed
   producer shadow [V], so the direct-reader needs no consumer write to let
   the firmware continue - it only has to keep up with a depth-32 ring.  The
   service therefore performs **no device writes in the receive path**;
   `rd_advance=pub` republishes the host-owned +0x38 (vendor analog) and the
   experimental +0x1C write exists only behind the default-OFF `rd_ab_write`
   parameter, as a documented fallback.
3. **Engine idle.** The ETE A->B copy is not autonomous: it is started by the
   firmware's **sending trigger CPU 0xC625A**, invoked only from the D2H/L2
   dispatcher CPU 0x4624C on status word CA 0x4003A874 bits 16-20; the
   trigger writes the u16 target index into the **PKTRAM control word at
   node-array base + 0x100** and re-reads it [V].  In our boot the trigger
   never ran (R+0x344 = 0, R+0x34C = 0, send words 0x4003AC00+4i = 0, fill
   +0x3C static [V\*]), while the deposit path did run (nodes staged) - so the
   stall is exactly "copy not started", not "device did not send".  Nothing
   the host writes feeds that device-internal chain (the vendor host's entire
   ETE surface is the intr-word RMW + the group-B mirror fields, which our
   transport already provides; `wifi-ete-engine` whole-image negative).  The
   `wifi-fw-postboot` lane's correction is adopted: the "scheduler never
   armed" probe was an instrument artifact (`HI5622_BAR0_IO` wraps CA
   >= 0x40120000), so the scheduler is **not** the gate; the copy-start chain
   is below the host-visible surface.  Direct read is the working receive
   path, independent of the engine.
4. **D2H catalogue for our boot.** The complete handler catalogue is known
   (chip group 4: id0 OAM dev log, id1 device_plat_ready, id2 DEVICE READY
   with a 4-byte payload, id3 exception_info, id4 heartbeat; chip group 3:
   hdpp/hmac event ids including **id 0x12 = 18 = hmac_d2h_temp_state_update**)
   and it cleanly decodes the 13:10 capture: DR3 node0 = g4 id1 (12 B, the
   ready announce), node1 = g4 id3 (73 B, "DEVICE EXCP_INFO"), and the four
   16-B DR0 frames are g3 id18 and/or **g4 id2** - the single open decode that
   the next bench re-read settles (section 5).  A 592-B DR3 frame matches the
   g4 id0 OAM log-blob class by length; the remaining ids must be re-read
   (they are staged in the ring and the service decodes them automatically).
5. **hcc-service correction (cross-check).** That lane's DR receive path is
   structurally wrong for this boot, in three specific ways: (a) it walks the
   **host** group-B node array and reads +0x3C as the fill - the device never
   wrote that array, because the engine (which would mirror the descriptors
   into it) is idle; (b) it returns buffers by writing +0x38, which cannot
   free the **device** group-A ring, the ring that actually fills; (c) the
   +0x3C = 0x10 / +0x38 = 0x400 values it logged are destination-side fields
   of an idle engine, not a frame count - the "0 nodes / no ready frames"
   verdict was an artifact of reading the wrong ring.  The service keeps the
   vendor-order mailbox path (that part is correct) and gains the direct-read
   path; `hcc_ready` now also prints the direct-path result.

---

## 1. Method and the new evidence taken for this lane

Everything previously published by the cited lanes is used as-is.  The new
evidence below was taken **this session** from the local vendor image
(read in place) to answer the two questions the lane had to decide
(nodes-vs-records layout and consumer advance).  Reproduction:
`tools/d2h_selftest.py` asserts the record table; the disassembly windows are
listed in section 10.

### 1.1 The DR channel init: what the firmware writes (and never writes)

`pcie_msg_init` (CPU 0x49334) programs each of the four DR channel register
blocks (CA 0x4003A590/0x5E0/0x630/0x680) in the loop CPU 0x4937E..0x494A4 [V]:

| write site | effect |
| --- | --- |
| 0x493BA | record+0xC = 0 (producer shadow; the live producer, see 1.2) |
| 0x493BC | record+8 = the per-channel cfg entry (depth at cfg+4, ctrl at cfg+5) |
| 0x493C0 | record+0x10 = 0 (progress / send-register lo16 copy) |
| 0x493C2 | record+0x18 = 0 (send target high-watermark) |
| 0x493C4 | record+0x1C = 1 (byte) |
| 0x493E2..0x493F0 | **record+0x14 = node-size(8) * depth(32) + record[0] = node array + 0x100** (the PKTRAM control word) |
| 0x493F8 | record+0x334 = hcc_instance |
| 0x493FC..0x49426 | **channel +0x10 = record[0] = the node-array CA** (0x01060440/0x01060330/0x01060220/0x01060110 for DR0-3) |
| 0x49428..0x49438 | channel +0x14 = depth-1 (bfi into the low 10 bits; 0x1F) |
| 0x4943A..0x49444 | channel +0x18 = record+0xC (producer copy, 0 at init) |
| 0x4944A..0x49454 | channel +0x08 ctrl = bfi(cfg[5], 0..2) |
| 0x49490..0x49496 | channel +0x00 |= 1 (enable) |

**It never writes the +0x1C consumer.**  The whole loop and its helper table
were read for this lane; the only other DR-field writes found on the D2H path
are the send register (device-authored) and the record RAM fields.

### 1.2 The deposit and commit path: where the nodes live and who counts them

The D2H send path (CPU ~0xC67A4..0xC67F8) [V]: it stamps tag 0x5A at
hdr+0xA/+0xB, resolves the message buffer through CPU 0xC1D88 (a PKTRAM CA
gets +0x1000000 -> the ACP alias), calls the deposit op through ops+0x10, then
bumps record+0x33C.  The deposit op CPU 0xC61E0 builds node
{word0 = the resolved buffer, word1 = (len << 16) | 0x6D2B}, commits it via
CPU 0xC192A, then `d2h_notify(...,6)` and record+0x340++ [V].

The commit helper CPU 0xC192A [V]:

    ldr  r2,[ctx+0xC] ; node base = [ctx+0]
    ubfx r2,r2,#0,#0xA                       ; packed producer slot
    str  r4,[node_base, r2, lsl #3]          ; node.word0 = buffer (ACP alias)
    str  r2w,[node_base, r2, lsl #3 + 4]     ; node.word1 = len<<16 | 0x6D2B
    ...  -> packed index-plus CPU 0xC1906 on the dword at [ctx+0xC]

with `ctx` = the per-channel DR record (its [ctx+0] is the node-array CA and
[ctx+8] is the cfg with depth at +4) [V].  So:

* **node array = record[0] = channel +0x10 register** (NOT +0x100);
* **producer = the packed dword at record+0xC** in firmware RAM;
* the +0x100 word after the node array is the ETE channel control/handshake
  word (the sending trigger writes the u16 target there at CPU 0xC6298-0xC62A4
  and re-reads it) - **the host must never write it** [V].

This is also why the 13:10 capture found exactly **12 nodes in DR3 and 4 in
DR0**: those are the committed producer counts of the boot-time deposits.

### 1.3 The firmware record table (static image data, self-checked at runtime)

| idx | record CPU | node-array CA (word0) | channel CA (+0x31C) | send CA (+0x32C) |
| ---: | ---: | ---: | ---: | ---: |
| DR0 | 0x10F2B8 | 0x01060440 | 0x4003A590 | 0x4003AC00 |
| DR1 | 0x10F60C | 0x01060330 | 0x4003A5E0 | 0x4003AC04 |
| DR2 | 0x10F960 | 0x01060220 | 0x4003A630 | 0x4003AC08 |
| DR3 | 0x10FCB4 | 0x01060110 | 0x4003A680 | 0x4003AC0C |

stride 0x354; hcc_instance+0x14 points at the first record [V, asserted by
the selftest against the local blob].  `record+0xC` is the producer shadow,
`+0x340` the tx counter, `+0x34C` the released counter, `+0x344` the
sending-trigger counter [V]/[V\*].

---

## 2. The direct-read receive service

### 2.1 Window map (what the service reads, and where)

| object | device address | host access | evidence |
| --- | --- | --- | --- |
| DR node array | channel +0x10 CA, PKTRAM 0x01060440/0x01060330/0x01060220/0x01060110 | BAR0 0x1D8000 + (CA - 0x01000000) (region 2) | [V]/[V\*] |
| frame body | node.word0, ACP alias 0x02xxxxxx | BAR0 0x4D8000 + (CA - 0x02000000) (region 4) | [V\*] 13:10 |
| producer shadow | record+0xC, CPU 0x10F2C4 + 0x354*ch | BAR0 0x6B8000 + CPU (region 5) | [V] |
| channel registers | CA 0x4003A590+0x50*ch | BAR0 0x3B8000 + (CA - 0x40000000) (region 3) | [V] |
| PKTRAM alias (fallback) | node.word0 - 0x1000000 | region 2 formula | [V] CPU 0xC1D88 |
| host-DRAM buffers (fallback) | 0x80000000..0x8FFFFFFF | identity DMA window, phys_to_virt | [V] |

The service bounds-checks every address against these ranges; an unresolvable
node pointer is logged and skipped, never dereferenced.

### 2.2 Poll algorithm (one pass, read-only)

    gate: windows programmed && handshake page +8 == 2 (golden boot gate)
    for each DR channel 0..3:
        read +0x00 enable, +0x10 base, +0x14 depth-1, +0x18 producer,
             +0x1C consumer, +0x38 published, +0x3C fill
        skip unless enable bit0 and base in PKTRAM; depth <= 32
        layout self-check: record+0x31C == channel CA &&
                           record+0x32C == send CA && cfg ptr sane &&
                           cfg depth 1..32
        producer = record+0xC (packed, sanity-checked)      [authority]
                 | channel +0x18                             [fallback]
        n = idx_count(last, producer, depth)     (vendor packed arithmetic)
        if n > depth: overrun -> warn, read the newest depth, count the loss
        for each new node:
            word1 == 0 / flags != 0x6D2B -> not committed yet: stop, retry
                                            next poll (stall watchdog: 200)
            len = word1 >> 16; require 12 <= len <= 0x680
            resolve word0 (ACP | PKTRAM | host DMA), copy min(len,96) B
            decode 12-byte header, validate tag 0x5A5A and total == len,
            deliver to catalogue/log, count
        last = producer                              (or the stuck slot)
    no device writes in this pass

Properties that matter for the bench:

* The boot batch stays in the ring until the host reads it (the engine is not
  consuming), so a manual `rd_poll` after the boot sees the same 16 nodes a
  tight loop would; the loop only adds timing.  The service starts from
  last = 0 and the firmware initialises the producer to 0 during boot, so
  nothing staged between release and the first poll is missed.
* A node that is not committed yet stops the walk without advancing the
  baseline, so a producer/descriptor visibility race is retried instead of
  skipping a frame; a slot that stays invalid for 200 polls is logged and
  skipped (bad_node counter) so the reader cannot wedge.
* Depth 32 per channel is the only back-pressure: a host that falls > 32
  deposits behind loses the overwritten nodes (hard overrun warning).  The
  service counts this; the next bench is read-only and far from it (16 nodes
  total).

### 2.3 Delivery and catalogue

Every decoded frame updates: global counters; a per-(group,id) `seen` table
with counts + last total/len; a 16-entry bounded frame log (header fields,
node pointer/len, up to 48 payload bytes, tag/length check flags, timestamp);
and the specific handlers: g4 id1 sets `id1_seen` (vendor wait #1 analog),
g4 id2 sets `id2_seen` + the 4-byte payload dword and copies it into
`ctx->read_msg[0]` (the vendor 100-byte per-core record's only used field),
g4 id3 records the EXCP_INFO body and flags the "DEVICE EXCP_INFO" prefix,
g4 id4 is logged as heartbeat.  Unknown (group,id) pairs are still catalogued
with their raw header/payload prefix - that is how the remaining 13:10 frames
get identified on the next bench.

### 2.4 sysfs surface (on the endpoint device, 0000:00:00.0)

| attribute | mode | purpose |
| --- | --- | --- |
| rd_poll | WO | one manual read-only poll; logs the node count |
| rd_status | RO | per-channel base/depth/enable/producer sources/cons/pub/fill/last/consumed/overruns + global counters + gate state |
| rd_decode | RO | the last 16 decoded frames: header fields, tag/len checks, payload hex + ASCII |
| rd_catalog | RO | per-(group,id) catalogue counts, ready flags, g4 id2 payload, the reference catalogue |
| rd_advance | WO | 'pub' = republish the host-owned DR +0x38 free index (vendor analog); 'ab' = EXPERIMENTAL device group-A +0x1C consumer write, refused unless `rd_ab_write=1` |
| hcc_ready | RO | unchanged engine-path ready state + a new DIRECT line with the direct-path ready result |

Module parameter: `rd_ab_write` (uint, default **0**).  All other module
parameters are the inherited hcc-service ones (`hcc_poll_ms`, `hcc_cece_mode`,
base transport params).

---

## 3. Consumer advance - the exact answer

### 3.1 The register map, with ownership

| field | offset | writer(s) | meaning |
| --- | --- | --- | --- |
| enable | +0x00 | firmware (`|= 1` in pcie_msg_init) | channel enabled |
| ctrl | +0x08 | firmware (cfg[5]&7) | control bits |
| group A base | +0x10 | firmware (= record[0]) | device source ring node array (PKTRAM) |
| group A depth-1 | +0x14 | firmware (0x1F) | depth 32 |
| group A producer | +0x18 | firmware, init only (copied from record+0xC at init) | stale copy; the live producer is the RAM shadow |
| **group A consumer** | **+0x1C** | **device ETE engine (copy completion) only** | frees source-ring slots |
| group B base | +0x30 | **host** (vendor pcie_ete_dr_reg_init) | host destination ring (host DRAM) |
| group B depth-1 | +0x34 | host | depth 32 |
| **group B published free index** | **+0x38** | **host** (vendor init + sending-trigger re-publish) | the only host-owned DR index |
| group B fill | +0x3C | device/engine | destination-side fill |

[V] from the pcie_msg_init disassembly plus phase42 A.3 and the vendor init
paths; [V\*] for the live values.

### 3.2 Which index may the host write, and in what order

* **The device ring consumer is +0x1C.**  It is advanced by the device's ETE
  copy when the copy runs; no firmware writer and no vendor-host writer exists
  for it [V].  A host write would not make the firmware proceed because the
  firmware never reads it - the deposit path is a blind producer ring: commit
  stores `node[producer & 0x3FF]` and bumps the packed RAM producer [V].  So
  there is **no host-writable device-ring consumer** in the vendor protocol,
  and the direct-read path does not need one.
* **The only host-owned DR index is +0x38.**  The vendor host writes it at
  probe (`pcie_ete_dr_reg_init`: `+0x38 = ctx[+0x1C]`) and re-publishes it in
  its sending trigger under the channel lock [V].  This is the destination
  ring's free index; it gates the engine's destination buffers, not the device
  source ring.
* **Sequence (vendor analog, implemented as `rd_advance=pub`):** for each
  channel whose host consumption advanced, compute the packed index by
  advancing +0x38 once per consumed node (`idx_plus(pub, depth)`), `wmb()`,
  single 32-bit write to `channel+0x38`, read it back and log.  It requires
  handshake page +8 == 2 and performs no write when nothing was consumed.
* **Experimental fallback (not part of the vendor contract; default OFF):**
  `insmod ... rd_ab_write=1` and `echo ab > rd_advance` writes the packed
  consumer value = the host's consumed producer to `channel+0x1C`, once per
  channel per load, only on the full golden signature (page +8 == 2, +C ==
  0xABCD, state CA 0x118B60 == 1, mask CA 0x40008 == 0), only when the host
  has walked up to the producer.  Rationale for shipping it disabled: if a
  future bench ever sees the producer stall at depth 32 with no engine copy,
  freeing the device ring is the only lever left, and the write is otherwise
  harmless (the firmware does not read the word).  Rationale for not using it
  on the next bench: it is an unverified write class; a read-only pass already
  answers the open questions.

### 3.3 Why "free ring space so the firmware continues" is not the current blocker

The firmware's send pump computes `pending = target(record+0x18) -
producer(record+0xC)` and deposits at most that many [V at CPU 0xC66A4-0xC66B6
and 0xC6746-0xC6758]; it never consults the consumer or ring occupancy.  The
ring wraps silently at depth 32.  Only a host slower than 32 deposits per
channel loses frames - a keep-up problem, not a consumer-register problem.

---

## 4. Engine idle - the conclusion (and what would falsify it)

### 4.1 The start condition, re-verified

The A->B copy is commanded by the firmware, not by autonomous hardware:

    ETE status block CA 0x4003A86C, status word +8 = 0x4003A874, bits 16-20
      -> D2H/L2 dispatcher CPU 0x4624C
      -> sending trigger CPU 0xC625A:
           target   = high16(*(record+0x32C) == CA 0x4003AC00+4i)   [V]
           write u16 target to the PKTRAM control word
                  (record+0x14 = node-array base + 0x100), re-read it [V]
           pump: read node word0 from the ring, check tag 0x5A5A, advance
                 progress record+0x10, released counter record+0x34C [V]
           wake the owning task CPU 0xC5128

The status word/send registers are device-authored; the whole-image negatives
of `wifi-ete-engine` stand: no host register or H2D message feeds this chain,
and no static init table touches the ETE window.

### 4.2 What the bench measured, and what it means

| observable | our boot (GP9/GP10/12:20) | meaning |
| --- | --- | --- |
| nodes staged in group A (13:10) | DR3 12, DR0 4 | **the deposit path works** |
| out[1] bit6 (d2h_notify 6) | seen | deposit + notify ran |
| trigger counter R+0x344 | 0 | sending trigger never ran |
| released counter R+0x34C | 0 | no copy progress reported |
| send words 0x4003AC00+4i | 0 | no target/progress pair |
| fill +0x3C | 0x10 static | no destination-side copy |
| ETE status 0x4003A874 | 0 | quiet (also quiet in the vendor golden read) |

Conclusion: the device's **deposit** half of the D2H pipeline is alive and
complete; the **copy** half never started, because the firmware-side trigger
that starts it never ran.  The trigger is invoked by the device event layer
from device status bits; the host's entire ETE surface is already vendor-exact
(intr-word RMW, group-B mirror, pre-posted free index - `wifi-ete-init` and
`wifi-ete-engine` negatives).  **No host action can wake it**; the direct read
is the workaround, and it delivers the very frames the copy would have
mirrored.

### 4.3 Adopting the fw-postboot correction

The earlier "scheduler never armed" story (hcc_irq_probe) is retracted by
`wifi-fw-postboot`: the probe's CA 0x4016xxxx reads use a 20-bit-masking
macro and decode to unrelated CA 0x4006xxxx registers, the GIC is not
reachable through the six vendor regions at all, and the ROM S1 + entry A +
entry B code order proves the timer was armed and event 0x1D enabled on the
boot whose golden page/mask were measured.  This lane does not repeat that
mistake: the only new register reads here are group-A/B/record fields inside
proven-mapped windows, and every one is bounds-checked by region (section 2.1).
The consequence for this lane: the engine-idle gap is **below** the
scheduler/IRQ layer (device D2H copy-start path), which is exactly the class
the direct read bypasses.

---

## 4a. One minor-correction note for the bench-reads table

`wifi-ready-emit/tools/bench-reads.py` lists the record counters with host
BAR0 addresses 0x7C35FC / 0x7C3950 / 0x7C3CA4 for CPU 0x10F5FC / 0x10F950 /
0x10FCA4.  With the project rule BAR0 = 0x6B8000 + CPU those are
**0x7C75FC / 0x7C7950 / 0x7C7CA4** (digit slip, consistently 0x4000 low);
the same table's g+0xB4/+0xB8 rows (0x7C41A8/0x7C41AC) are correct.  This lane
uses the formula, not those literals, and its selftest pins the arithmetic.

---

## 5. D2H message catalogue for our boot

### 5.1 Handler catalogue (vendor host tables, hcc-re sec 5.5) [V]

| space | group | id | handler / meaning |
| --- | ---: | ---: | --- |
| chip | 4 | 0 | oam_rx_post_action_function - device log blob to SDT/netlink |
| chip | 4 | **1** | device_plat_ready_msg_process - "Device plat ready!"; completes vendor wait #1 (2.0 s) |
| chip | 4 | **2** | host_ready_msg_process - "DEVICE READY"; payload = 4-byte device RAM address copied to g_dmac_to_hmac_read_msg[core]; completes wait #2 (20.0 s) |
| chip | 4 | **3** | exception_info_msg_process - copies a 0x30-byte exception body |
| chip | 4 | 4 | heartbeat_msg_process - restarts the host heartbeat hrtimer |
| chip | 3 | 0,4,5,7,8 | hdpp: voice aggr, WoW data report, rx schedule req, tx/rx ppdu record |
| chip | 3 | 9,10,15,16,17,18,19,20 | hmac: smps cap, pfm, bsd update, multiap 11v, d2h PM, **temp state (id 18)**, beacon report, beacon frame |
| chip | 1/2 | ... | hmac/wal event tables (not seen yet on this boot) |
| core | 0 | 0-4,7,8 | hmac tx/rx completion + exception events |

No D2H id requires a host reply on the boot path; the ready pair is
device->host only (ete-frames/host-replica negatives).

### 5.2 The 13:10 captured batch (device group-A rings)

Read live by the earlier lane; the direct service reproduces this decode and
records the ids of the frames that were not fully decoded then:

| DR3 node | len | decode now | note |
| ---: | ---: | --- | --- |
| 0 | 12 | **g4 id1 device_plat_ready** (tag 0x5A5A) | the ready handshake's first frame [V\*] |
| 1 | 73 | **g4 id3 exception_info**, payload starts "DEVICE EXCP_INFO" | [V\*] |
| 2 | 592 (0x250) | g4 id0 OAM dev-log class by length [I] | id to be re-read |
| 3 | 41 | unknown id | re-read |
| 4 | 536 | g4 large message (id to be re-read) | |
| 5 | 74 | unknown id | re-read |
| 6 | 41 | unknown id | re-read |
| 7 | 500 | g4 large message | |
| 8 | 70 | unknown id | re-read |
| 9 | 128 | unknown id | re-read |
| 10 | 70 | unknown id | re-read |
| 11 | 99 | unknown id | re-read |

| DR0 node | len | decode now | note |
| ---: | ---: | --- | --- |
| 0..3 | 16 each | at least node0 = **g3 id18 (0x12) hmac_d2h_temp_state_update** [V\*]; the other three are 16-B frames of the same total as **g4 id2 DEVICE READY** | **top next-bench re-read**: if one of them has group=4/id=2 the ready pair is complete in this boot |

Ring inventory: 16 nodes total (DR3 12, DR0 4, DR1/DR2 empty) - well inside
the depth-32 ring, so the next read-only pass cannot overrun.

### 5.3 What each frame means for the bring-up

* g4 id1 alone = the device reached "device plat ready"; the vendor host would
  be inside wait #1.
* g4 id2 = the second half ("DEVICE READY") with the read-message address;
  its presence closes the ready exchange for our host.  The firmware builder
  is the boot task's id2 phase, gated on internal `[sp+0x24] == 2` and
  `[0x1ADB80+0x110] == 0` (fw-postboot / ready-gate) - firmware-internal, no
  host write can supply it.  If the re-read shows only g3 id18 in DR0, then
  the id2 phase gate is the next firmware-side question, and the direct read
  has proven everything the host can do.
* g4 id3 EXCP_INFO is data for the vendor host's exception logger; on our boot
  it is evidence the image's exception/startup path produced a report - record
  the body, do not act on it from the host.
* g4 id0/id4 are device logging/heartbeat traffic: they prove the firmware's
  post-boot application layer is running.

---

## 6. Cross-check and correction of the hcc-service receive path

The `wifi-hcc-service` lane delivered the correct vendor-order mailbox
service but its DR receive path assumed something that is false in our boot:

| hcc-service assumption | reality (this lane) | consequence |
| --- | --- | --- |
| the device fills the **host** group-B node array (host DRAM) and advances +0x3C; the host reads +0x3C as the fill | the device fills the **device** group-A ring in PKTRAM; the host group-B array is only the host's own pre-posted free buffers. +0x3C stays 0x10 and the host-array slots stay word1=0 | `hcc_walk_dr` finds 0 completed nodes over and over ("64 DR nodes drained all len=0" in the 12:20 session was exactly this) |
| returning buffers by writing +0x38 lets the device ring proceed | +0x38 is the destination free index; only the engine would consume it, and it is idle. The device source ring is freed by +0x1C, which the vendor host never writes and the firmware never reads | the +0x38 write is harmless but cannot unblock anything |
| "fill=0x10 / pub=0x410" is a frame count | they are destination-side registers of an idle engine (0x10 = the reset-class value; pub = the host's own pre-posted 0x400 + walk bookkeeping) | the negative ready verdict was an artifact |
| ready frames can be decoded from the engine-copied host rings | the ready pair is sitting in the device group-A ring, fully readable | the ready pair was found by the Lead's 13:10 direct read, and this lane makes it a service |

What stays correct and is reused unchanged: the vendor-order `out[1]`
ack/drain/re-arm path, the boot-guarded CECE policy, the H2D announce rules,
the boot/ready wait state machine, the dev_info logging and the safety gates.
The new service only **adds** the device-ring read path and reports it in
`hcc_ready`.

---

## 7. Implementation summary

* New files: `src/hi5622_d2h_direct.h` (contract) and
  `src/pciet_d2h_direct.c` (the service).  Integration into the existing
  module via `src/hcc-d2h-direct.patch`: include, embedded state field,
  probe init, `pciet_hcc_rd()` accessor, poll call in `hcc_service_pass`,
  attribute creation/removal, one line in `hcc_ready`, and the Kbuild object.
* The module keeps its identity `hi5622_pciet_hcc` (one driver can bind the
  endpoint; load only this module on the bench), vermagic
  `6.18.60 SMP mod_unload ARMv7 p2v8`, GPL, no new module dependencies.
* Safety encoded: no device writes in the receive path; the two possible
  writes are the vendor-owned +0x38 republish (explicit store) and the
  default-OFF +0x1C experiment (full golden-boot gate + once-per-channel +
  readback); `CA 0x400392F0` never appears as a write target (source census in
  the selftest); no firmware/device RAM store, no PKTRAM control-word write,
  no UART, no RC misc window.
* Build (host-only): see `BUILD-NOTES.md`; zero warnings, module sha256
  `60810cf23c019ed9f46d43771cc30d47450d2c5ba75bf75b2bb7eb64630ad783`,
  262,352 B.  Device-free selftest: **92/92 PASS**.

---

## 8. Bench summary (full detail in BENCH-PROCEDURE.md)

Read-only pass on a fresh golden boot: insmod the module, prepare/upload/
verify, `hcc_arm`, start `hcc_loop`, release 0x5A5A **only**, then read
`rd_status` / `rd_decode` / `rd_catalog` (and `hcc_status`/`hcc_ready`).
Expected: `layout=OK`, producer source shadow, DR3 producer 0x0c (12 nodes),
DR0 0x04 (4 nodes), 16 frames decoded, **g4 id1 seen**, EXCP_INFO seen, and
`rd_catalog` settling whether g4 id2 is among the four DR0 16-B frames.  No
device writes; the +0x38 republish and the experimental +0x1C are separate,
optional, one-idea sessions.  Stop rules: no out[5] write, no CECE before
page +8 == 2, no H2D frame in this session, power-cycle instead of rmmod after
release, any abort/SError/hang -> capture and stop.

---

## 9. Proven vs assumed, and open items

Proven by construction / host-side evidence:
* the ring, node, record and window layout (fresh disassembly + selftest
  against the local image);
* the consumer/host-index ownership and the blind-producer behaviour;
* the frame decode/catalogue rules and the captured-batch model;
* the module compiles clean and exposes the documented surface;
* the direct-read path's bounds/safety census.

Open items [U]/[I]:
* the ids of the 13:10 frames not yet decoded (the service will print them on
  the next bench; lengths suggest g4 id0 for the 592-B frame);
* whether **g4 id2** is in the four DR0 16-B frames (the decisive next read);
* the vendor-golden values of the ETE engine global/doors (CA 0x4003A000 /
  0x4003A7C0 / 0x4003A7D0) - not host-visible through the six regions; they
  would only tell us what a *working* engine looks like, and no host write can
  produce it anyway;
* whether any host action wakes the copy (all evidence says no; the direct
  read makes it moot for the ready pair).

---

## 10. Reproduction (host-only, local artifacts only)

    PY=<repo>/.provision-venv/bin/python

    # 1) regenerate the integration patch from the committed hcc-service src
    $PY tools/mkpatch.py --check          # or without --check to rewrite
    # 2) build the module (kernel-module only, no image, no device)
    ./tools/build.sh
    # 3) device-free selftest (92 checks; asserts the local firmware record
    #    table in place and the service source write census)
    $PY tools/d2h_selftest.py
    # 4) optional raw bench-capture decode
    $PY tools/d2h_selftest.py --node 0x02048c48 0x000c6d2b
    $PY tools/d2h_selftest.py --frame 040000000c00010000005a5a

Firmware disassembly windows used for the new evidence (all local, nothing
committed):

    $PY ../wifi-ready-emit/tools/fwdis.py dis 0x49334 110   # pcie_msg_init DR loop
    $PY ../wifi-ready-emit/tools/fwdis.py dis 0xC192A 40    # ring commit helper
    $PY ../wifi-ready-emit/tools/fwdis.py dis 0xC1906 36    # packed index-plus
    $PY ../wifi-ready-emit/tools/fwdis.py dis 0xC61E0 70    # deposit op
    $PY ../wifi-ready-emit/tools/fwdis.py dis 0xC625A 120   # sending trigger
    $PY ../wifi-ready-emit/tools/fwdis.py dis 0xC66A4 60    # send pump pending
    $PY ../wifi-ready-emit/tools/fwdis.py dis 0xC1D88 60    # device-VA resolver

---

## 11. Corrections carried forward

* The hcc-service DR receive path cannot see the boot's D2H frames: the
  device writes the **device** group-A ring, not the host group-B ring, and
  only the idle engine would mirror them (section 6).
* `+0x3C` is not a fill the host can wait on in our boot (0x10 static); the
  authoritative producer is the firmware RAM shadow at record+0xC [V].
* The device group-A node array is at the **+0x10 base**; base + 0x100 is the
  PKTRAM control/handshake word (never write it) [V] - the earlier
  "node array = staging + 0x100" reading of the record field is superseded:
  record+0x14 is the control-word pointer, record[0] is the node-array base.
* The consumer that frees the device ring is +0x1C and is not host-writable
  in the vendor protocol; +0x38 is the host-owned destination index and its
  republish cannot free the device ring.  The firmware does not gate on ring
  space at all (section 3.3).
* "The scheduler never armed" is an instrument artifact (adopted from
  wifi-fw-postboot): do not read CA >= 0x40120000 through
  `HI5622_BAR0_IO`, and do not draw conclusions from the 12:20 hcc_irq_probe
  words.
* The ready pair does not need the ETE engine: it is readable directly from
  the device group-A ring, which is why the 13:10 read succeeded where the
  engine-path receive never could.