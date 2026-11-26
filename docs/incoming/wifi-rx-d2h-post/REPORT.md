# REPORT - the D2H post path of the firmware group-2 id-3, and the host-bound g1 id-13 RX route

**Lane:** `wifi-rx-d2h-post` (task-226). **HOST-ONLY static decode** of the pinned
`FIRMWARE.bin` (928,920 B, sha256 `7fc87e20...c3ef311b`; **CA = file offset + 0x40000**).
Companions: [README.md](README.md), [TABLES.md](TABLES.md), [PROBES.md](PROBES.md),
[FIRE-RECIPE.md](FIRE-RECIPE.md), [tools/](tools/), [evidence/](evidence/).
Tool: `--selftest` **146/146 PASS**; every address/constant below is asserted by it.

**Prior context (cited, not redone):** the F1 of 2026-11-26 measured the SMAC ISR producer
(`band_obj+0xCC0` +105, other-source counter +51, PPDU-list unlink runs; band0 object CA
`0x174BF0`) [V\*]; task-221 (wifi-rx-smac-complete) proved the only g2/id3 builder is the SMAC
body `0xB53A4` and that it posts through `0x102760` [V\*]; task-220 (wifi-rx-mac-delivery)
decoded the consumer `0x10CC18[id3] -> 0xBBC9C -> 0xBA1C8 -> 0xB9780` and the ETE deposit
primitives `0xC61E0/0xC192A/0xC6170` [V\*]. **The open question this lane answers:** why do the
D2H frames actually delivered to the host (`g1 id3`, `g4 id4`; earlier catalogue also
`g4 id1/id2`) reach it, while the `g2 id3` never does?

---

## 0. Verdict (read this first)

1. **`0x102760` is a queue enqueue, not a ring deposit [V].** It resolves the header's
   destination nibble (`hdr[1]>>4`, copied to `hdr[9]`) to a **destination record**
   (`hcc_instance 0x171B6C + 0x448 + 0x18*dest`), resolves the group (`hdr[0]&0xf`) to a
   **group record**, increments the item's stat counter, takes the group record's **queue
   index** (`+8`), and appends the message to that queue's list (`queue+0x40`). It then
   signals the queue's **notifier** (`queue+0xC0`): atomic flag `notifier+0x48=1`, schedule
   `0xC2A54(notifier+4)`. Failure return is `0x8B2D`. **[Q1 answered.]**
2. **The D2H ring deposit is a different, later step [V].** The message layer's transport
   customer drains the queue and calls the ETE deposit op **`0xC61E0`** (ETE ops table CA
   `0x10CEDC` slot **+0x10**): it builds the 8-byte node `{buffer device VA,
   (len<<16) | present-bits | 0x0D2B}`, commits it to the per-channel device source ring
   (**`0xC192A`**: base `[rec]`, packed producer `[rec+0xC]` = index bits 0..9 + phase bit
   10, node at `base + 8*(idx&0x3FF)`), then **`d2h_notify(chip 0, id 6)`** (**`0xC6170`**).
   There is **no per-message-type test in the deposit op or in the notify** [V]: the notify id
   is a **mailbox bitmap bit ≤ 0xA** (`out[1]` CA `0x40039014` plus doorbell bit 0 at CA
   `0x40101434`), not the HCC group/id. Observed ids are **6** (D2H data staged), **5** (SR
   receive drain), **3** (host-interface event tail-call). A deferred-shadow flush (`g+0xB8`
   published by `0xC6108` after `out[1]==0)) covers the busy case. **So the notify path has
   no id filter, the ETE node has no per-type/source field, and the doorbell mask is constant:
   none of those can be the reason g2 id3 is missing. [Q1 per-type answer: ruled out.]**
3. **The discriminator is the per-group queue selection at `0x102760` step 4 [V + I].** The
   host-visible classes are all built with destination nibble **0** (heartbeat: `0xC6BDA`
   `bfc r3,#4,#4`; ready pair: host-side `src core=hdr[1]>>4==0` check [V\*]); the g2 id3
   copies `u8[band_obj]` into that nibble (`0xB5472`). Independently, the firmware registers
   **local handler tables only for groups 0 and 2** (`0x102866` calls: boot task `0x48338`
   g2/19 ids at table `0x10CC18`, `0x48342` g0/40 ids at `0x10CCFC`; hcc init `0x47BC8`
   g0/1 id at `0x10CBEC`) [V]: groups 1/3/4 are not locally consumed. The g2 id3 handler
   `0xBBC9C` is entry id 3 of that local group-2 table [V]. The static HCC message map at CA
   `0x103FB8` (3 queues, 7 groups; `.data`, re-decoded here) carries the per-group queue
   table at CA `0x10CF74` (7 x 12-B `{u16 group; u16 qidx; u32 item_array; u32 max_id}`):
   **g0 -> q1, g1 -> q0, g2 -> q1, g3 -> q0, g4 -> q2, g5 -> q1, g6 -> q0** [V]. The item
   arrays are staged into RAM by the boot copy (writable at runtime; P16 reads them).
   **The host-bound classes therefore take the transport queues (0/2); the g2 id3 takes the
   local-dispatch queue (1), the same queue as the locally registered group 0.**
   [Q1 answered: it is the group->queue selection, not an ETE type/filter.]
4. **The candidate is confirmed: the g2 id3 is an internal firmware notification; the
   host-bound RX copy is the g1 id13 [V].** `0xBBC9C` (local group-2/id-3 handler) -> the
   per-frame datapath `0xBA1C8` -> the CRX builder `0xB9780`: it sets **group 1**
   (`mov.w sb,#1 @0xBA0AA`, applied by `bfi @0xBA14A`), **id 13** (`movs r3,#0xd @0xBA150`),
   total = `frame_len+60` (`0xBA146`), copies the 48-byte RX info + MPDU, and **posts the g1
   id13 through the same `0x102760`** at `0xBA17A`. That message is the RX frame the host
   should receive. The chain is statically complete; what has never been observed on the host is
   the g1 id13 itself. [Q3 answered statically.]
5. **The remaining break is now one of three named gates, not an unknown entry [I].**
   (a) the local dispatch of the g2 id3 does not run (queue 1 notifier/worker not wired or not
   scheduled: the message sits in the queue list); (b) it runs but every frame is dropped by the
   CRX gates (`[chip+0x1a00]`, forward flag, length windows, total <= 0x680) and
   `band+0xCC5` counts the drops; (c) the g1 id13 is built but queued to a transport queue
   whose customer is not wired, so it is never deposited. **The probe pack splits these with
   four counters** (item-stats vs queue list count vs band+0xCC5 vs host catalogue).
6. **Host-side acceptance does not explain the absence [V\*].** The mailbox dispatcher registers
   only D2H ids {1,3,6,7}; ids 2/4/5/8/9/10 are dropped, and the message-family tables on the
   host (plat.ko chip group 4: 5 ids; wifi.ko hmac chip groups 1/2/3; wal chip group 2) are the
   HCC dispatch tables - an unregistered family logs `frame_unknown` but the raw D2H ring
   reader still catalogues the frame. **A missing frame in the catalogue therefore means "never
   deposited", not "host dispatch dropped it".** *(Why our host logs `frame_unknown` for
   g4 id4 while still receiving it is exactly that distinction.)*

---

## 1. `hcc_msg_tx` CA `0x102760` - instruction-level decode [V]

All rows are asserted by `--selftest` (see [TABLES.md](TABLES.md) T1 for the full table).

```
102760  push {r4,r5,r6,lr}; r5 = msg
102762  if msg == NULL -> log(line 0x179), return 0x8B2D
102776  hdr = 0x409EA(msg); if hdr == NULL -> log(0x17F), return 0x8B2D
102782  dest = (u8[hdr+1] >> 4) & 0xF; hdr[9] = dest
10278A  dest_record = hcc_instance(0x171B6C) + 0x448 + 0x18*dest
1027B2  group_rec = 0x102744(dest_record, u8[hdr] & 0xF)        ; group resolver
1027BA  id = u16[hdr+6]
        if id < group_rec->max_id: item = group_rec->item_array + 16*id; item->stats++ 
1027CE  qidx = u16[group_rec+8]
1027D2  queue = 0x1024CE(dest_record, qidx)                     ; queue array, stride 0xE4
1027E2  lock(queue+0xC)
1027E6  list_add_tail(queue+0x40, msg)                          ; 0x102662
1027F6  unlock(queue+0xC)
1027FA  notifier = *(queue+0xC0)
        if notifier: atomic_store(notifier+0x48, 1); 0xC2A54(notifier+4)
        return 0
```

Supporting decodes [V]:

* `0x102744(dest_record, group)`: `map = [dr+4]`; `group_rec = [dr+0x10] + 0xC*group`;
  valid only if `group < [map+0x10]`; returns `0` otherwise (the message then fails with
  `0x8B2D`). So the group set is an init-time property of the *destination*, not of the message.
* `0x1024CE(dest_record, idx)`: `[dr+8]` is the queue-array base, `[map+8]` its count,
  stride `0xE4`. Returns `0` (-> `0x8B2D`) when out of range.
* `0x102662` = intrusive list append (list struct at `queue+0x40`: next/prev/count).
* **Queue object layout** (from the hcc-init constructor at CA `0x47F14`-`0x47F68` [V]):
  `+0x00` = 0, `+0x04` = per-queue config ptr, `+0x08` = state (set 1), `+0x0C` = lock,
  `+0x10` = `0x104294`, `+0x40` = message list head, `+0x80` = second list,
  `+0xC0` = **notifier (initialised NULL)**, `+0xC4` = notifier-set flag,
  `+0xC8` = 0. The notifier is installed later, by the transport/customer registration; if it
  stays NULL, `0x102760` still queues the message and returns success, but nothing is
  scheduled. **This is the exact mechanism that can hold the g2 id3 without a visible failure.**
* The destination/queue/group arrays are all host-readable firmware RAM (layout in
  [TABLES.md](TABLES.md) T4; instance CA `0x171B6C`, dest record 0 CA `0x171FB4`). The
  static map they are initialised from is CA `0x103FB8` (`+8` = 3 queues, `+0x10` = 7
  groups, `+0x18` queue config, `+0x1C` "HCC TX Thread" customer name, `+0x24` group
  table `0x10CF74`, `+0x28` queue records `0x1040A8`); the group table's item-array
  pointers (`0x118F08`, `0x119438`, ...) are staged into RAM by the boot copy, which is why
  P16/P17 read them at runtime [V].
* The message sources relevant here: the SMAC producer body `0xB53A4` calls `0x102760` at
  `0xB5492` [V\*]; the heartbeat builder at `0xC6C0A` [V]; the CRX builder at `0xBA17A` [V].

---

## 2. The transport half - deposit, commit, notify [V]

The ETE ops table CA `0x10CEDC` (word `+0` = `0x4003A000`, `+4` = `0x40039508`,
**`+0x10` = `0xC61E1` = deposit**), stored by `pcie_msg_init` into the ETE context at
`0x10F1F0+0x18`.

```
0xC61E0  deposit(chip, ring_rec, buf_dev_va, len, present)
         node.word0 = buf_dev_va
         node.word1 = (len<<16) | present?(0x4000|0x2000):(present<<13)
                              | 0x0D2B                      ; present=1 -> 0x6D2B
0xC61E0  bl 0xC192A(ring_rec, &node)                        ; ring commit
0xC6220  d2h_notify(*(chip+0x58)=0, 6)                      ; out[1] bit 6 + doorbell
0xC6228  ring_rec->[0x340]++                                ; deposit counter
0xC192A  base = [rec]; idx = [rec+0xC] & 0x3FF
         base[8*idx + 0] = node.word0; base[8*idx + 4] = node.word1
0xC1906  packed producer advance: index bits 0..9, phase bit 10 toggles on wrap
0xC6170  notify(chip, id): reject chip != 0 or id > 0xA; g = *(0x172130); lock g+0xC0
         if g+0xB4 (in-flight latch) == 0: g+0xB4 = mask; *(g+0x9C) = mask   -> out[1]
                                          *(g+0xA4) |= 1                    -> doorbell
         else: g+0xB8 |= mask (deferred shadow, flushed by 0xC6108)
0xC6108  flush: write 1 through g+0xB0, spin while *(g+0x9C) != 0, then publish g+0xB8
```

**No per-message-type branch exists in this half** [V]: the same deposit op, the same ring
commit, the same notify id 6 for every firmware-originated D2H message. The notify "id" is a
bit index in a 11-bit mailbox word (`cmp r1,#0xa` at `0xC6170`), not the header group/id.
Therefore "why does id X reach the host but id Y not" cannot be answered by an ETE node field,
a ring selection, a notify filter or the doorbell mask - those are constant for all classes.
The only class-dependent decision upstream is the queue selection in `0x102760` (section 1).

Note on failure observability: when `0x102760` fails, the SMAC producer logs source lines
`0x257`/`0x275` and bumps `band_obj+0xCCE` (band0 CA `0x1758BE`) before re-arming the PPDU
descriptor [V\*]. That counter is the clean "was the post accepted" probe (P13).

---

## 3. The local consumer and the g1 id13 RX route [V]

The firmware's *own* message dispatcher is `0xFD7E8(reg_tab, msg)`: header via `0x409EA`,
`group = u8[hdr] & 0xf`, `id = u16[hdr+6]`, resolver `0xFD4C8(reg_tab, group, id)` (same
map layout as `0x102744`; `item = [map+0x24...]` family), then `bx [item+4]` - the handler.
It is called from the firmware worker `0x1025F6`-`0x102644` after dequeuing a message from a
queue list [V]. The local handler tables are the ones registered by `0x102866`
(group 2 -> `0x10CC18` with 19 entries, including **id 3 -> `0xBBC9D`** = `0xBBC9C|thumb`;
group 0 -> `0x10CCFC` (40 ids) and `0x10CBEC` (1 id)).

The g2 id3 consumer chain, re-derived [V]:

```
0xBBC9C  r5 = [hdr+0x14] (= band_obj); chip = 0xA4FC8(u8[r5], u8[r5+1]);
         splice the [hdr+0xc] received-frame chain into a local list; 0xBA1C8
0xBA1C8  per received frame: bl 0xB9780 at 0xBA48A
0xB9780  CRX builder: dequeue frame (0x1024EA), hdr=0x409EA, rxinfo=0x40CBA
         frame_len = u16[rxinfo+0x11]  (0xBA0F8)
         gates: [chip+0x1a00] != NULL (0xB97EE); forward flag -> dest table index via 0xBC524
                (0xBA134); frame_len+0x3C <= 0x680 (0xBA110) else band+0xCC5++ (0xBA12C)
         build: group = 1 (sb; 0xBA0AA / bfi 0xBA14A)
                id = 13 (0xBA150; u16 hdr[6] = 0xD)
                total = frame_len + 0x3C (0xBA146)
                copy 48-B RX info + MPDU; post 0x102760 at 0xBA17A
```

So the **only host-bound message derived from a received frame is g1 id13**, built per frame
after the g2 id3 has been locally dispatched. If the host catalogue has no g1 id13 while the
SMAC producer counter moves, the break is between "g2 id3 validated/enqueued by `0x102760`"
and "g1 id13 enqueued by `0x102760`" - i.e. in the local dispatch or the CRX gates, not in
the D2H transport (which demonstrably delivers g1 id3 / g4 id4 in the same sessions).

---

## 4. Why the host never sees the g2 id3 - the ranked answer

| # | candidate | decoded fact | rank |
| ---: | --- | --- | --- |
| C1 | **group->queue routing: g2 id3 is enqueued to the firmware-local dispatch queue, not a transport queue** | `0x102760` picks queue by group record `+8` [V]; local handler tables exist only for groups 0/2 [V]; g2 id3 is entry 3 of the local g2 table with handler `0xBBC9C` [V]; the static HCC map (CA 0x103FB8 + group table 0x10CF74) maps g2->q1, g0->q1, g1/g3->q0, g4->q2 [V] | **primary** |
| C2 | destination nibble: g2 id3 copies `u8[band_obj]` (chip-id byte) while heartbeat clears its nibble to 0 | `0xB5472` vs `0xC6BDA` [V]; the runtime value of `u8[band_obj]` is a bench read (P12) | secondary, probe-decidable |
| C3 | ETE node type/source field, ring selection, notify id filter, doorbell mask | **ruled out** - constant across classes; notify id is a mailbox bit, deposit op has no type branch [V] | ruled out |
| C4 | host-side dispatch tables drop it (`frame_unknown`) | only matters **after** deposit; the raw D2H catalogue reads the ring and would still show the frame; bench has never shown one [V\* + V] | ruled out for "never reaches" |
| C5 | local dispatch scheduled but never runs (queue notifier NULL / worker not draining) | queue notifier initialised NULL and installed by the customer registration [V]; message still queues with success [V] | plausible open gate |
| C6 | local dispatch runs, CRX gates drop every frame | gates at `0xB97EE`/`0xBA134`/`0xBA110`; drops counted at `band+0xCC5` [V] | plausible open gate |
| C7 | g1 id13 built but its queue's transport customer not wired | same mechanism as C5 one level down | plausible open gate |

**The discriminating probes are in [PROBES.md](PROBES.md):** P16 item `g2 id3` stats counter
(accepted posts), P15 queue-1 list count (queued-but-undrained), P17 control queue counts,
P13 producer failure counter, P18 `band+0xCC5` (CRX drops) and P0 host catalogue for g1 id13.
The four deltas give a unique answer for C5/C6/C7 in a single attended read pass.

---

## 5. Host-side D2H decode (context; from committed vendor-module lanes) [V\*]

* mailbox word `out[1]` bit ids: `pcie_msg_init` (plat.ko 0xb6e4) registers handlers for
  **{1,3,6,7}**; id 3 = `pcie_ete_transfer_done_handle` (drain DR), ids 6/7 =
  `pcie_trigger_ete_sending_handle` (wake worker). ids 2/4/5/8/9/10 have no handler.
* The *message family* dispatch on the host is separate: plat.ko chip group 4 table
  (`plat_init_bal_hcc_excp`, 5 ids 0..4), wifi.ko hmac chip groups 1 (28 ids), 2 (12),
  3 (8), wal chip group 2 (19). A received group/id outside the registered post tables is
  logged `frame_unknown`; the frame is still in the DR ring and is catalogued by the raw reader.
* Consequence: **the "host never receives g2 id3" observation is about the ring, not host
  dispatch**, and the "the driver says frame_unknown for g4 id4" observation is about the host
  post table, not about delivery.

---

## 6. What this lane changes / next actions

* The F1's "the producer posts" is refined: the producer **validates and enqueues** the g2 id3;
  whether that queue is then drained by the local worker is a new, named question (P15/P16).
* The host-visible RX target is confirmed to be **g1 id13**, not g2 id3; a bench catalogue with
  a moving item-stats counter for g2 id3 but no g1 id13 points at the local-dispatch queue
  (C5) or the CRX gates (C6), not at the transport.
* One attended read-only pass (FIRE-RECIPE.md) with P0/P12-P19 settles C5 vs C6 vs C7.
* Safety unchanged: read-only loads only; never write CA `0x400392F0`; never read the RC misc
  window; no retries on a failed width; one coherent boot per tuple.
