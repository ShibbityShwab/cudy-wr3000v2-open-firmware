# REPORT - the firmware local-dispatch/CRX chain: the C5/C6/C7 answer and the new counter ladder

**Lane:** rxdisp2 (task-242). **HOST-ONLY static decode** of the pinned
rootfs-2.4.15/lib/firmware/hi_wifi/FIRMWARE.bin (928,920 B, sha256 7fc87e20...c3ef311b;
**CA = file offset + 0x40000**). Companions: [README.md](README.md), [TABLES.md](TABLES.md),
[PROBES.md](PROBES.md), [FIRE-RECIPE.md](FIRE-RECIPE.md), [tools/](tools/),
[evidence/](evidence/). Tool: --selftest **76/76 PASS**; every CA/field/value below is
asserted there.

**Prior context (cited, not redone):** the SMAC ISR producer body 0xB53A4 builds the only
group-2/id-3 RX-complete message and posts it through 0x102760 (task-221); the consumer
0x10CC18[id3] -> 0xBBC9C -> 0xBA1C8 -> 0xB9780 (task-220); 0x102760 is a per-group queue
enqueue that signals queue+0xC0 (task-226); "HCC RX Thread" entry 0x102552 is the H2D
message thread (task-216); the builder gate matrix G1-G10 (task-164). This lane answers the
three open gates **C5 (queue-1 notifier/worker), C6 (CRX builder gates), C7 (g1 id13
deposit)** at instruction level and adds a counter ladder that localises the break.

---

## 0. Verdict

1. **C5 is decoded and is not a NULL-notifier state [V + I].** The q1 notifier is installed
   by the same per-destination init body that creates the **HCC RX Thread** (0x102552),
   which is the thread that services **every host->firmware message this bench uses** (AP
   start, SAE, TWT, rate control - all working). Concretely: q1's notifier must be the
   customer record at **CA 0x171E84** (slot 0x171D14), the customer's task arg is that
   record, and the customer's queue list contains q1 (0x171C54). A NULL q1 notifier cannot
   coexist with working H2D. The message is drained and 0xBBC9C runs. **C5 is ruled out.**
2. **C7 is low [V].** The g1 id13 is built with group 1 and posted to 0x102760 at 0xBA17A;
   its destination nibble is u8[dest+2]&0xF from the forward table (0xBC524). If that
   nibble is 0 it lands in dest0/q0, whose transport customer is the "HCC TX Thread"
   carrying the g1 id3 config replies the host already receives -> proven alive. C7 is
   alive only if the forward table yields a **non-zero** nibble (P22).
3. **The surviving break is the CRX builder's per-frame RX-descriptor gates (C6), or the
   stage immediately before it (C8, new).** The builder's no-counter drops are: RX-ctx
   [chip+0x1A00] (init-time), the class nibble at 0xB9864 (must be 1 or 2),
   0xBC524(u8[rxinfo+6])==0, and the frame_len+0x3C <= 0x680 cap (counter +0xCC5). A **new
   counter ladder** (band+0xCC0/0xCC1/0xCCC/0xCCD/0xCC5/0xCCE) splits C6 from C8 in one
   read pass: +0xCC1 moves iff 0xBA1C8 starts a descriptor, which is the line between
   "queue/dispatch works, builder drops" and "no descriptor ever reaches the builder"
   (the SMAC/hardware RX side).
4. **FIX candidate.** The queue/customer wiring is not the lever. If P24b (+0xCC1) moves,
   the lever is the **received-frame descriptor content** that the MAC/DMAC delivers
   (class nibble, forward flag, frame length) - i.e. the SMAC/DMAC RX configuration /
   descriptor programming, not the firmware. If P24b does not move while +0xCC0 does, the
   break is one stage further upstream in the SMAC completion content (task-221's P5/P6
   rung). No host wire, no H2D message and no firmware byte changes.

---

## 1. The chain, with the corrections this lane adds [V]

    SMAC RX completion (band 0)
      body 0xB53A4: g2/id3, dest nibble = u8[band_obj] (=0 required), hdr[0xC] = frame chain,
                    hdr[0x14] = band_obj, post 0x102760 @0xB5492, post-fail band+0xCCE++ @0xB5442
    0x102760  queue enqueue: dest_record = 0x171B6C+0x448+0x18*dest; group = hdr[0]&0xF;
              item = item_array + 16*id, item.stats(+8)++; qidx = group_rec[+8];
              queue = 0x1024CE(dest_record, qidx) (stride 0xE4); list_add_tail(queue+0x40);
              notifier = queue+0xC0 -> flag notifier+0x48=1, post 0xC2A54(notifier+4)
    q1 = 0x171C54 (the local dispatch queue, dest 0, group 2 -> q1)
    HCC RX Thread (customer 1 = 0x171E84, entry 0x102552)
               pending -> for each bound queue: splice queue+0x40 -> queue+0x80, pop,
               resolver 0xFD4C8(table=[customer]), dispatch 0xFD7E8, free; idle waits 0xC23AC
    0xBBC9C   (g2 id3 handler): band = hdr[0x14]; chip = 0xA4FC8(u8[band], u8[band+1]);
              0xBA1C8(chip, band, frame list); on 0x8B2D re-arms PPDUs via 0xB52F0
    0xBA1C8   per descriptor: band+0xCC1++ (per descriptor started); forwards 0xB2C5C
              (fail -> band+0xCCC++); allocates a g2/id2 report message 0x102822
              (fail -> band+0xCCD++), fills hdr[0x14]=band, hdr[0xC]=descriptor, calls 0xB9780
    0xB9780   CRX builder: G1 [chip+0x1A00]!=0; per received frame: forward flag, class nibble
              in {1,2}; build g1/id13 (total=frame_len+0x3C, dest nibble = u8[dest+2]&0xF
              from 0xBC524), copy MPDU (0x4BDCE), post 0x102760 @0xBA17A
    0x102760  -> dest0/q0 -> customer0 "HCC TX Thread" (0xC515E) -> ETE deposit -> D2H

Corrections to the earlier chain picture [V]:

* 0xB9780 is registered for **g2 ids 2 and 5** (table entries 0 and 3 of 0x10CC18) and is
  called **directly** by 0xBA1C8 at 0xBA48A on a locally built g2/id2 report message; the
  g2/id3 handler 0xBBC9C is the *input* to 0xBA1C8.
* 0xBA1C8 is a per-descriptor decoder, not a per-message passthrough: it increments
  band+0xCC1 at 0xBA240-0xBA248 after the descriptor's class/length fields are non-zero,
  and has its own failure counters band+0xCCC (0xBA3FC, 0xB2C5C fail) and band+0xCCD
  (0xBA41E, report-message alloc fail). The prior probe pack did not read these three,
  which is why C6 and the upstream "no descriptor" case could not be split.
* 0x102760's "notifier" is not a separate object: it is the **customer record** itself
  (+0x48 pending flag, +4 wait object). The worker waits on exactly that object
  (0xC23AC(customer+4)), so the enqueue -> wake path is one structure.

---

## 2. C5 - the local queue, its customer and its worker [V]

### 2.1 Destinations, queues, customers (all static for dest 0)

The per-destination init body (inside the hcc/pcie init at 0x47628, dest loop head 0x47AF4,
body 0x47DF8) computes the dest0 runtime bases from the hcc instance 0x171B6C (immediates
asserted):

| object | CA | derivation |
| --- | ---: | --- |
| queue array | 0x171B70 | instance + 4, stride 0xE4 |
| q0 / q1 / q2 | 0x171B70 / 0x171C54 / 0x171D38 | queue array + k*0xE4 |
| customer array | 0x171E1C | instance + 0x2B0, stride 0x68 |
| customer0/1/2 | 0x171E1C / 0x171E84 / 0x171EEC | customer array + k*0x68 |
| group array | 0x171F54 | instance + 0x3E8, stride 0xC |
| dest0 record | 0x171FB4 | instance + 0x448 |

The static hcc map at 0x103FB8 says **dest start 0, one destination, 3 queues, 3 customers,
7 groups** ([+0]=0, [+4]=1, [+8]=3, [+0xC]=3, [+0x10]=7). The per-destination body runs for
dest0 only; **dest1+ queues are never bound** (this matters for C7: a non-zero g1-id13
dest nibble would land on uninitialized dest records).

Queue construction (0x47F14): +4 = static queue cfg, +0x40/+0x80 list heads, +0xC lock,
**+0xC0 notifier = NULL, +0xC4 flag = 0**, +8 state = 1. Queue cfg records (0x1040C0, 8 B
each) carry the owning customer at **byte +3** and the per-service-round limit u16 at +4:

| queue | cfg CA | customer index | round limit |
| --- | ---: | ---: | ---: |
| q0 | 0x1040C0 | 0 (HCC TX Thread) | 100 |
| q1 | 0x1040C8 | 1 (HCC RX Thread) | 80 |
| q2 | 0x1040D0 | 2 (HCC OAM Thread) | 80 |

### 2.2 Customer creation and queue binding

For each customer index (loop at 0x4802A):
thread record = map[+0x1C] + 0x30*idx = 0x10CFC8+0x30*idx; customer record = customer
array + 0x68*idx; customer+0 = dest record; customer+0x54/+0x58 binding list self-init;
0xC71B8 initialises the wait object at customer+4; customer+0x48 pending = 0 (atomic
0x1024BC); customer+0x4C = thread record; 0xC745C creates the task with the thread-record
entry and **task arg = the customer record**; customer+0x50 = task handle; failure path
logs and aborts the whole init.

Then the bind loop (0x4809E-0x48104) runs over the three dest0 queues:
queue = 0x1024CE(dest0, k); cfg = [queue+4]; custidx = u8[cfg+3];
customer = customer_array + 0x68*custidx; the queue object is appended to the customer list
at customer+0x54, and **queue+0xC0 = customer** (the notifier install). At end of init the
expected notifier values are therefore statically known:

| slot | CA | expected value |
| --- | ---: | ---: |
| q0+0xC0 | 0x171C30 | 0x171E1C (customer0) |
| **q1+0xC0** | **0x171D14** | **0x171E84 (customer1)** |
| q2+0xC0 | 0x171DF8 | 0x171EEC (customer2) |

### 2.3 The worker

"HCC RX Thread" record 0x10CFF8: name "HCC RX Thread", id 1, entry 0x102553, task arg =
customer1. Body 0x102552:

1. if customer+0x48 pending == 1: atomically clear it (0x1024BC), then for every queue in
   customer+0x54: lock queue+0xC (0xFDF8E), **splice queue+0x40 into queue+0x80**
   (0x102530), unlock (0xFDF50);
2. cfg=[queue+4], limit = u16[cfg+4]; pop from queue+0x80 (0x1024EA); if 0x409EA gives a
   header: resolve 0xFD4C8(table=[customer+0], group=u8[hdr]&0xF, id=u16[hdr+6]), bump
   item[+0xC] (the same stats counter 0x102760 bumps), call per-queue callbacks
   (queue+0xC8/0xCC, zero by construction), dispatch 0xFD7E8(dest_record, msg), free
   (0x1024E6);
3. after the round: if anything was processed, rescan; else if pending is still 0, block in
   0xC23AC(customer+4) until the notifier posts it.

### 2.4 Why C5 is not the F1 any more [V + I]

q1 is **the** local dispatch queue and its customer is the H2D service thread. Every host
configuration message this bench relies on (AP/VAP start, key install, HE/TWT/rate config)
is consumed through the dest0 handler tables registered by the boot task into the same map
and dispatched by this thread. The AP beacons, SAE and rate control are on air -> q1 is
being drained -> the g2 id3 enqueued to q1 is drained -> 0xBBC9C runs. The C5 failure mode
("items sit in the queue with the notifier NULL") cannot be the current state; the only C5
corner left is a gross memory/CDS anomaly, which P15/P20 would expose but which no
observation supports.

---

## 3. C6 - the CRX builder gates [V]

All in 0xB9780 (Thumb entry 0xB9781); asserted by the tool:

| gate | CA | condition | on failure |
| --- | ---: | --- | --- |
| G1 | 0xB97EE | [chip+0x1A00] != 0 (per-chip RX ctx) | log 0xA4E, return 0x8B2D, **no counter** |
| G2 | 0xB97AE | chip = 0xA4FC8(u8[band], u8[band+1]) != 0; requires **u8[band]==0** | log 0x361/0xA94 |
| Gsel | 0xB9816-0xB9832 | forward flag = bit6 of u8[0x40CCE(frame)-0x1F]; class nibble = u8[0x40CCE(frame)-0x1D]>>4 | flag=0 -> direct list; flag=1 -> 0xB2CF4 forwarded list (fail: log 0xA5D) |
| Gclass | 0xB9864-0xB98B2 | class nibble must be **1 or 2** | drop the whole list, log 0xA6A, return 0x8B2D, **no counter** |
| G7 | 0xBA0FC/0xBA134 | dest = 0xBC524(u8[rxinfo+6]) with index <= 0x12; dest == 0 -> drop | log 0x49F, **no counter** |
| G8 | 0xBA110-0xBA118 | frame_len+0x3C <= 0x680 | log 0x497, **band+0xCC5++** |
| Gpost | 0xBA17A | post returns 0 | log 0x4B3, free node, **no counter** |

Where:

* chip = 0x15CA08 + 0x1B68*u8[band+1] (band0 chip = 0x15CA08, band1 = 0x15E570). The
  literal is asserted at 0xA4FF4.
* [chip+0x1A00] is installed by the per-band init at 0x48A68-0x48A6C (str.w sl,[r4,#0x1A00])
  with sl = the per-band RX context; so G1 is an **init-time** gate, not per-frame.
* frame_len = u16[rxinfo+0x11]; fwd idx = u8[rxinfo+6]; the RX info is the 48-byte payload
  block the builder assembles at sp+0x30 (0xB98EA-0xB9A56) for the outgoing g1 id13;
  dest = u16[0x170E08+0x6FC] + u32[0x170E08+0x650+4*idx] (0xBC524; the chip-manager object
  is BSS, so its contents are a probe item, P22).
* build: hdr[0] = group 1 (0xBA0AA/0xBA14A), hdr[6] = 13 (0xBA150),
  hdr[4] = frame_len+0x3C (0xBA146/0xBA156), hdr[1]hi = u8[dest+2]&0xF,
  hdr[2] = u8[dest+1], hdr[3] = u8[dest] (0xBA15C-0xBA172), MPDU copy 0x4BDCE,
  **post 0x102760 at 0xBA17A**.

---

## 4. C7 - the g1 id13 deposit and its destination nibble [V]

The g1 id13 is posted through the *same* 0x102760 as the working g1 id3 config replies;
group 1 maps to **q0**, whose customer is the HCC TX Thread (entry 0xC515E). That path
demonstrably delivers (the host's g1 id3 replies arrive), so C7 is only alive if the
**destination nibble** of the g1 id13 is non-zero:

* nibble = u8[dest+2] & 0xF where dest = 0xBC524(u8[rxinfo+6]) (0xBA15C/0xBA162);
* non-zero nibble -> 0x102760 computes dest record 0x171B6C+0x448+0x18*nibble; the init
  only wired dest 0, so dest1+ has a NULL map -> 0x102744 returns 0 -> 0x102760 returns
  0x8B2D -> the builder logs line 0x4B3 and frees the node, **with no counter**. This is
  the only silent C7 failure mode; P22 reads the table and settles it.

---

## 5. C8 - the new upstream candidate and the counter ladder [V]

0xBA1C8 (the per-descriptor decoder between the g2 id3 handler and the builder) bumps three
previously unread **band0 counters**:

| counter | CA / phys | site | meaning |
| --- | --- | ---: | --- |
| band+0xCC0 | 0x1758B0 / 0x4082D8B0 | 0xB53C4 | SMAC producer entries (prior P5) |
| **band+0xCC1** | **0x1758B1 / 0x4082D8B1** | **0xBA248** | 0xBA1C8 started processing a descriptor |
| band+0xCCC | 0x1758BC / 0x4082D8BC | 0xBA402 | 0xB2C5C (descriptor forwarding prep) failed |
| band+0xCCD | 0x1758BD / 0x4082D8BD | 0xBA426 | g2/id2 report-message alloc (0x102822) failed |
| band+0xCC5 | 0x1758B5 / 0x4082D8B5 | 0xBA12E | builder G8 length-cap drop |
| band+0xCCE | 0x1758BE / 0x4082D8BE | 0xB5442 | producer post of the g2 id3 failed (prior P13) |

The ladder answers the question that C5/C6/C7 alone cannot:

    +0xCC0 moves, +0xCC1 still 0   -> the g2 id3 is built+posted+drained but 0xBA1C8 never
                                     starts a descriptor: the frame/descriptor chain content
                                     is empty or its class fields are zero -> SMAC/hardware
                                     RX side (NOT the builder, NOT the queue)   [C8]
    +0xCC1 moves, no g1 id13, +0xCCC/+0xCCD move -> 0xBA1C8 hit its own failures  [C8b]
    +0xCC1 moves, +0xCC5 moves      -> builder ran and dropped on G8            [C6/G8]
    +0xCC1 moves, +0xCC5 stays 0    -> builder ran and dropped on the no-counter gates
                                       G1/Gclass/G7 or the post failed silently    [C6]
    +0xCCE moves                    -> the producer's 0x102760 post was rejected before
                                       any of the above                           [C5-producer]

This ladder is the main new bench deliverable: it localises the F1 to one stage in a single
attended read-only pass, without firmware changes and without host changes.

---

## 6. Re-ranked answer table

| gate | decoded fact | rank after this lane |
| --- | --- | --- |
| C5 local queue notifier / worker | notifier installed by the same init that creates the H2D thread; q1 = 0x171E84 expected; worker body 0x102552 drains q1 | **ruled out** (working H2D proves the drain) |
| C6 CRX builder gates | G1 init-time; Gclass/G7 silent no-counter drops; G8 counted | **primary remaining** |
| C7 g1 id13 deposit | q0/customer0 proven by delivered g1 id3; only a non-zero dest nibble can kill it silently | **low**; P22 settles |
| C8 descriptor stage | 0xBA1C8 never started a descriptor (+0xCC1 static) | **new primary** if +0xCC1 does not move |
| C9 producer post | +0xCCE counter / producer logs 0x257/0x275 (task-221 rung) | **low**; P24f settles |

**FIX candidate (ranked, all host-side/no-firmware-write):**

1. **If P24b (+0xCC1) moves and +0xCC5 stays 0 and +0xCCE stays 0: the fix lever is the
   received-frame descriptor content** - specifically the class nibble the firmware expects
   in {1,2} (0xB9864) and the forward flag/type index that resolves through 0xBC524
   (u8[rxinfo+6], index <= 0x12). These are MAC/DMAC RX descriptor fields; they are
   programmed by the SMAC/DMAC RX configuration, i.e. the same live-bank knobs the
   wifi-rx-woc-class / wifi-rx-smac-complete lanes own. Cross-check the MAC RX descriptor
   class/forward words against the woc-class live bank; do not touch firmware.
2. **If P24b stays 0 while +0xCC0 moves: the break is the SMAC completion content**
   (descriptor chain/class fields empty). That is the task-221 rung (P5/P6 + the SMAC ack
   word); the queue and builder are exonerated.
3. **If P20 shows q1+0xC0 != 0x171E84 or customer1+0x50 == 0:** a CDS/init anomaly (the
   per-dest init aborted) - re-read the D2H ready pair and the init logs; no fix from the
   host side.
4. **If P22 shows any dest nibble != 0 for the forward indices:** the g1 id13 is posted to
   an uninitialized destination and silently dropped. The only possible host-side lever is
   the H2D forward-config message that fills 0x170E08+0x650 (not yet located; do not guess
   it on-air).

---

## 7. What would falsify this report

* P15 q1 list count grows while the host still receives no g1 id13 and the firmware H2D
  commands work -> the q1 drain model is wrong (C5 returns).
* P20 customer1 fields not equal to the static expected values -> init did not run ->
  C5 returns.
* P24b moves and Gclass/G7/G1 all pass, yet no g1 id13 -> the post/nibble path (C7) needs
  the P22 read; if P22 shows nibble 0 on the used index, the model has a gap at the
  descriptor->rx_info mapping and must be re-derived from a firmware-log capture.
* Host receives a g1 id13 after a config change that does not move any P24 counter ->
  contradicts every row above and the whole decode must be re-read.

---

## 8. Safety

Read-only loads only; no writes anywhere; never write CA 0x400392F0; never read the RC misc
window 0x10160000..0x1016FFFF; no retries of a failed width; one coherent boot per tuple;
P0 before the counter pass so a dead delivery channel is not misread as an RX fault. This
lane touched no device.
