# Host-side HCC service counterpart on the corrected boot protocol

**Lane:** wifi-hcc-service (shared task task-87), **host-only**, 2026-11-24.
**Status:** design + compile-ready implementation + build verification done; host-only.
The bench router was never contacted, no image was built, and no vendor binary,
firmware byte, calibration value or secret is reproduced here - only derived
addresses, constants, decoded structure and original code.

**Deliverables (all in this directory):**

| path | contents |
| --- | --- |
| REPORT.md | this document: the five answers, the design, the evidence ledger |
| BENCH-PROCEDURE.md | attended bench procedure: stages, stop rules, expected observables |
| BUILD-NOTES.md | build result, toolchain, reproducibility, module metadata |
| src/hi5622_pciet.h, src/pciet_main.c, src/pciet_ete.c | verbatim copies of the project transport skeleton (wifi-transport-port/build) with the marked HCC integration hunks |
| src/hi5622_hcc_service.h | contract header: mailbox/boot/event-layer/ETE/HCC constants + entry points |
| src/pciet_hcc_service.c | the service: vendor-order D2H loop, corrected CECE policy, ETE SR announce, DR/ready receive path, IRQ-layer discriminators, sysfs surface |
| src/Kbuild, src/Makefile | module build (driver name hi5622_pciet_hcc, own module - cannot collide with hi5622_pciet.ko / hi5622_pciet_b3.ko) |
| tools/build.sh | host-only build driver: copies src to a scratch dir, builds one out-of-tree module, prints sha256/vermagic/attrs |
| tools/frame_selftest.py | device-free self-test of the HCC frame model + packed ring-index arithmetic (83 checks PASS) |

**Read-first inputs (cross-referenced, committed under opensource/docs/incoming/):**
wifi-host-replica (pcie_msg_handle order, D2H handler table, ready waits),
wifi-ete-frames (header/tag/length, ready-frame wire form, no H2D in the ready
window), wifi-ready-emit (d2h_notify / in-flight / flush contract, ETE status
path), wifi-ete-init (vendor ETE init order + intr RMW), wifi-ete-engine
(engine start condition), wifi-event-router (event controller + IRQ-unmask
analysis), wifi-rom-dump (mask ROM decode + corrected boot protocol),
wifi-hcc-link and wifi-h2d-catalog (H2D negatives/catalogue), the transport
skeleton under wifi-transport-port. Bench state cited from the ledger entries
2026-11-24 10:40 (GP8), 11:10 (GP9), 11:40 (GP10) and STATUS.md sections 3b/3c.

Evidence markers follow the project convention: **[V]** verified from a named
local artifact or committed lane report, **[V\*]** device-measured by an
earlier attended lane and cited, **[I]** inference, **[U]** unknown.

Address conventions (project-wide, unchanged):
- firmware-internal CPU address -> host BAR0 = CPU + 0x6B8000;
- device IO CA -> host BAR0 = 0x3B8000 + (CA - 0x40000000);
- image file offset -> CPU address = file + 0x40000.

---

## 0. TL;DR - the five answers

1. **D2H service.** The host counterpart is the vendor pcie_msg_handle
   sequence, exactly: read out[1] (CA 0x40039014); if nonzero, **ack
   out[3] (CA 0x40101438) = 1 first**, re-read and **drain out[1] = 0**,
   **re-arm out[4] (CA 0x40101414) = 1**, then dispatch the saved bitmap
   lowest bit first (ids 0..10; id>10 stop) with the vendor handler
   semantics {1 stub, 2 exception/startup announce, 3 ETE done -> drain DR
   channels 0x1f, 4/5 PM, 6/7 wake the ETE TX/service thread}; id 3/6/7 also
   drain DR0-3 and decode the HCC frames. Probe-read first so a pass with no
   pending word performs no device writes. This is implemented in
   src/pciet_hcc_service.c::hcc_service_pass() and run by either a single
   manual pass (hcc_service) or the service kthread (hcc_loop, 5 ms default).

2. **CECE policy (the load-bearing new rule).** CA 0x4000010C is a dual-use
   gate: the ROM boot stage needs 0xCEED to survive until the handshake page
   CA 0x1BF000 + 0x8 reads 2 (GP8), while the image service/exception
   handler (CPU 0xC6E14) zeroes the word and spins for 32-bit 0x0000CECE.
   The corrected policy encoded here is: **never write CECE before page+8
   == 2**, and after boot **only on observed handler-spin evidence** - out[1]
   bit2 (0x4 startup/vector announce), a nonzero/changed exception-seen mask
   (CA 0x400008), or a D2H handler posting (any id other than the pure
   deposit notify bit6). Each evidence event allows at most one 32-bit write;
   a manual kick (hcc_cece_kick) is bounded to 3 per load and refuses
   pre-handshake. The legacy transport "announce" attr is hard-refused in
   this build, so the GP8 bug cannot be reintroduced through it.

3. **ETE TX side / engine wake.** The first H2D frame carrier is implemented
   with a **valid** 12-byte header (group low nibble, alloc nibble 0x1 |
   dst<<4 at +1, u16 total = payload+12 at +4, u16 id at +6, dst at +9, tag
   0x5A5A at +0xA), the SR0 node {buffer dev VA, (total<<16)|0x6D2B}, packed
   producer commit, then the vendor mailbox id 3 order: out[0] = BIT(3),
   out[2] |= 1 (RMW), bounded out[0] drain poll. Refusals: mailbox id 5
   (firmware BUG()), HCC group 4 H2D (no receive table), group other than
   0/2, payload > 0x674. The **ETE engine wake requirement is a negative**:
   rings armed before release (P0-equivalent, now including the vendor intr
   RMW &= 0xFFE0F8F8 and bfi-faithful depth/ctrl), host group-B publish of
   pre-posted free buffers, and the DR +0x38 re-publish are all host-owned
   fields this module programs, but the group A->B copy is started by the
   **device-authored** ETE status word CA 0x4003A874 (bits 16-20 -> firmware
   sending trigger 0xC625A), which the image reaches only through its IRQ
   event path. **No host register or H2D message feeds that loop** (whole
   vendor-module scan negative; firmware-writer census negative). What the
   host sequencing can do is keep the device-side path from stalling once
   delivery works: keep out[1] drained so the firmware flush 0xC6108 spin
   (out[1]==0) can retire, and serve CECE only if a handler is spinning.

4. **Ready exchange / IRQ-unmask.** The device emits HCC chip-group-4 id1
   (12-byte header, total 0x0C) then id2 (16 bytes, 4-byte payload = a device
   RAM address) over DR; the host only receives, copies id2's payload into
   its 100-byte per-core record and completes two waits (200 jiffies = 2.0 s,
   2000 jiffies = 20.0 s). No H2D reply exists or is needed. The image IRQ
   layer's only unmask sites are inside the IRQ dispatcher and the scheduler
   idle (cpsie i); the scheduler is entered by image entry A after the
   +8==2 handshake and after the boot task is queued. **GP8 satisfied the
   handshake gate (page +8 = 2, state 1, mask 0), so the previously suspected
   blocker is gone - yet GP9/GP10 still observe delivery-dead symptoms
   (out[0] not consumed, g+0xB4 = g+0xB8 = 0x40, ETE status/send/fills
   frozen).** Static evidence says **no host mailbox/ETE/HCC message can
   unblock the image IRQ layer**: the vendor sends nothing between release
   and the first device HCC message, no vendor host write touches the
   0x4016/0x4018 event controllers, and no firmware H2D handler reaches the
   event dispatcher. The host service's only real levers are (a) the
   corrected boot (release-only, CECE out of the boot stage), (b) serving
   CECE if/when a handler spins, (c) keeping out[1] drained. The decisive
   classification is therefore the read-only hcc_irq_probe (scheduler timer
   fingerprint CA 0x40160600/08/0c, EOI CA 0x40160110, latched raises
   CA 0x40161188/0x40161288): scheduler fingerprint absent => entry A did not
   reach its scheduler (task-queue/FIQ path, firmware/ROM-internal); timer
   armed but EOI 0 with raises latched => CPU/ROM IRQ delivery gap
   (firmware/ROM-internal). Either way the next lane is firmware-side
   (ROM IRQ entry / entry-A post-handshake path or a vendor golden trace),
   not more host writes. The single bounded H2D liveness probe (valid SR
   frame + id 3) stays available to prove the H2D dispatcher (event 0x4C) is
   alive once delivery works; a timeout closes the H2D lane (consistently
   with the wifi-hcc-link negative).

5. **Deliverable status.** Compile-ready and build-verified: module
   hi5622_pciet_hcc.ko, 201,140 B, sha256
   8fd9085431ee6b064f435f9550d9dfef5d74b0023020983682361af7746a48db,
   vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, zero compiler warnings under
   the tree's CONFIG_WERROR, modpost clean; 13 hcc_* sysfs attributes; 83/83
   frame/ring self-test checks pass on the host. Full procedure and stop
   rules: BENCH-PROCEDURE.md.

---

## 1. Method, inputs and what is new here

This lane is a synthesis lane: the protocol facts were established by the
reversal lanes cited above (all committed), the corrected boot by the ROM
dump lane and GP8, and the post-boot observations by GP9/GP10. What was
missing was the host-side service that runs the vendor's post-release
algorithm under the corrected protocol constraints, plus the read-only
discriminators that decide the next blocker. That is the code delivered here.

New design content of this lane (not merely re-stating prior reports):

- the **CECE policy** as an executable rule set (boot guard + evidence
  classes + bounded manual kick) instead of a one-shot write;
- a **vendor-ordered D2H service** that combines ack/drain/re-arm, per-bit
  dispatch, the DR drain on ids 3/6/7, the DR fill watchdog, and the ready
  state machine in one serialized pass;
- the **valid-frame ETE SR announce** built from the decoded carrier checks
  (tag/length) with a hard-refusal matrix, and the vendor mailbox id 3 order
  with the correct out[0] pending write (the pre-GP8 transport waited on the
  wrong register and posted zero-header frames);
- the **IRQ-layer discriminator pack** that turns the open IRQ-unmask
  question into a three-way bench decision;
- the server-side observability for the D2H notify stall (g+0xB4/g+0xB8) and
  the ETE status/send/fill state in one read-only snapshot.

---

## 2. The corrected boot protocol encoded in the service

Root cause (ledger 2026-11-24 10:00/10:40, ROM-dump lane): one 16-bit
register CA 0x4000010C carries two mutually exclusive handshakes.

- ROM stage: CPU0's ROM cold path writes 0xCEED after the host release; CPU1's
  S1 (mask ROM 0x5FE) passes only when 0x1BF00C == 0xABCD and
  0x4000010C & 0xFFFF == 0xCEED, then writes 0x1BF008 = 2 and dispatches
  image entry B. The vendor never writes CECE and its boot completes.
- Image stage: the service/exception handler CPU 0xC6E14 zeroes the halfword
  CA 0x4000010C (and 0x40000108), stores out[1] = 4, and spins until the
  32-bit word reads 0x0000CECE; image CPU 0xC20F8 is a second CECE consumer.

GP8 verified that release-only brings the firmware to the vendor golden state:
handshake page 0x86807FEF / 0x00000007 / 0x00000002 / 0x0000ABCD, state
CA 0x118B60 = 1, exception mask CA 0x400008 = 0, TCM byte-identical, both
custom blocks pre-seeded [V\*]. GP9/GP10 then showed the post-boot stall
details [V\*]. The service therefore enforces:

- the transport's own release write (0x5A5A -> CA 0x40000108) is unchanged;
- the base transport announce attr is REFUSED in this build (its one job was
  the CECE write, which is now policy-owned) - the GP8 bug cannot be
  reintroduced through the standard sysfs surface;
- the only CECE writer is hcc_cece_policy(), gated by hcc_boot_complete()
  (handshake page + 0x8 == 2) and by observed handler-spin evidence, with
  counters for every refused pre-boot attempt.

---

## 3. The vendor-exact D2H service loop

Algorithm (host-replica REPORT sec 4.2, re-verified across both vendor
versions [V]):

    probe-read out[1] (CA 0x40039014); if zero -> no device writes this pass
    out[3] = 1                 (CA 0x40101438; ack FIRST)
    bitmap |= re-read out[1]
    out[1] = 0                 (drain; device precondition P2)
    out[4] = 1                 (CA 0x40101414; re-arm)
    for id = 0..10, lowest set bit first; stop above 10:
        id1 pcie_dev_ready_msg_handle        - empty stub
        id2 exception_panic_msg_process      - startup/vector announce (0x4)
        id3 pcie_ete_transfer_done_handle    - drain DR channels mask 0x1f
        id4 wlan_pm_slp_disallow_callback    - PM state only
        id5 wlan_pm_wkup_dev_ok_callback     - PM state only
        id6/7 pcie_trigger_ete_sending_handle - wake the chip TX/service thread
    (id 3/6/7 also drain DR0-3 and decode HCC frames here)

Implementation details worth noting:

- The ack is written before the read that captures the bitmap, and the
  re-read after the ack catches a word the device deposited in between (the
  device is allowed to publish between ack and drain).
- A zero-bitmap pass writes nothing, so the loop can safely run BEFORE the
  release (needed for the boot-kick C1e lesson: the firmware flush 0xC6108
  spins for out[1]==0, so a tight drain keeps that window open). The thread
  also runs the read-only boot/IRQ watch in that phase.
- The loop is serialized by one mutex; the device-visible order is always
  ack -> drain -> re-arm and only then handler work (the same order a vendor
  IRQ thread produces).
- The DR drain uses the vendor get_rls_num arithmetic: from = last +0x3C
  baseline, fill = live +0x3C, n = depth*phase_delta + (low_to - low_from);
  each completed node is decoded and the buffer is returned by advancing the
  host-owned +0x38 free index with the packed index. The service never
  writes +0x3C, +0x00, +0x08, +0x1C or the send words.
- A fill watchdog drains a DR channel whose fill advanced without a mailbox
  word (deposit-without-notify finding), and logs it as such.
- The D2H notify stall is made observable: g+0xB4 (in-flight) and g+0xB8
  (deferred shadow) at CA 0x10C1A8/0x10C1AC are read back; both 0x40 means the
  deposit op published bit6 and the firmware flush 0xC6108 has not retired it
  (a delivery-dead signature, see section 7).

---

## 4. The corrected CECE policy

Rule set encoded in hcc_cece_policy():

    boot = (windows_programmed && handshake_page[+8] == 2)
    evidence_this_pass =
         out[1] bit2 (0x4 startup/vector announce)
      || exception-seen mask CA 0x400008 changed since last look, or first nonzero
      || a D2H handler posted (any id except the pure deposit notify bit6)
    if !boot:
        if evidence -> count as blocked, log refusal, DO NOT WRITE
        return
    if evidence: cece_pending = true
    if cece_pending: write 32-bit 0x0000CECE -> CA 0x4000010C once; clear pending

Why these classes: the image zeroes the gate per vector entry, so one write
per observed entry is the correct event-driven behaviour; the startup routine
stores out[1]=4 so bit2 is the direct "a handler is in its spin" marker; the
mask covers an exception entry whose announce was missed; a handler posting
proves an image handler ran in a context that may spin on CECE (CPU 0xC20F8
spins without announcing). The write is 32-bit because the routine compares
the full 32-bit word (a halfword write is not sufficient in general [V]).

Safety: the policy never runs before the handshake, has no timer/periodic
component, and is bounded per event. hcc_cece_mode = 1 disables all CECE
writes for a negative-control session; mode 2 additionally allows the bounded
manual hcc_cece_kick (<= 3 per load). Counters hcc_events reports
cece_writes / blocked_in_boot / evidence_events.

What this policy does NOT do: it cannot create IRQ delivery or start the
image scheduler (section 7). It only lets an observed spinning handler return,
which is what the vendor's missing CECE peer would have done.

---

## 5. ETE TX side: the valid SR announce and the engine-wake answer

### 5.1 The carrier and the first H2D id3 announce

Every H2D HCC frame uses one carrier (ete-frames sec 1.1 [V]): build a
12-byte header + payload; commit an SR node {buffer device VA,
(total<<16)|0x6D2B} with the packed producer; announce mailbox id 3 via
out[0] = BIT(3) and out[2] |= 1. The vendor host stamps 0x5A5A at +0xA and
takes the length from the header (+4), so the firmware SR-receive drain
checks exactly those two fields (tag at file 0x8699E, length at file 0x869B0)
and drops a zero header. The pre-GP8 transport "send" smoke posted a 64-byte
zero header, which fails both checks - the firmware consumed the node and
dropped the payload (the observed "SR consumer advanced, no ready" signature).

Implemented (hcc_sr_announce): request grammar "1" (safe default) or
group= / id= / dst= / payload=<hex>; the header is always rebuilt so tag and
length are correct; refusals are hard: id 5 (firmware BUG), group 4 (no H2D
receive table - group 4 is device->host only), any group other than 0/2 (the
only H2D tables), dst > 0xf, payload > 0x674 (hcc_msg_alloc bound), odd hex,
and any state that is not (windows armed, CPU released, handshake page +8=2,
out[0]==0). The default liveness frame is the vendor payload-free group-0
id-0 stub frame 00 01 00 00 0c 00 00 00 00 00 5a 5a (byte-identical to the
h2d-inject validated frame [V]). After posting, the current out[0] bit3,
out[2], read-only out[5] and SR0 consumer are recorded; a stuck out[0] bit3
means the firmware H2D dispatcher is dead and the H2D lane is closed (no
retry in-session, no speculative payloads - several firmware handlers
dereference payload words).

### 5.2 What the host can and cannot do for the engine

The ETE engine's group A -> group B copy start condition (ready-emit sec 3-5,
ete-engine sec 6, all [V]):

- the device ETE engine raises status word CA 0x4003A874 bits 16-20 for the
  DR channels; the image's D2H/L2 dispatcher CPU 0x4624C maps those bits to
  the firmware sending trigger CPU 0xC625A, which then drives the copy and
  the +0x3C fill advance;
- the send words 0x4003AC00+4i / 0x4003AC18+4i are device-authored
  (hi16 = target packed index, lo16 = progress); the host must never write
  them;
- the host-owned inputs are the DR group-B mirror (base +0x30, depth-1 +0x34,
  pre-posted free index +0x38) and the host-facing ETE interrupt word
  CA 0x40039508 (vendor RMW &= 0xFFE0F8F8). The latter is a host receive
  mask, not read by any firmware code - it is vendor parity, not the wake;
- the vendor's only kick-like write is the sending-trigger re-publish of
  DR +0x38 / SR +0x18 under lock; P0's pre-post covers the initial value.

This module implements the host-owned part vendor-exactly (hcc_arm: intr RMW
FIRST, then SR0-2 group A with bfi-faithful depth/ctrl, then DR0-3 group B
with 32 pre-posted buffers and published free index; all read back; DMA
window enforced 0x80000000..0x8fffffff), and offers the S3-only one-shot
hcc_dr_republish (re-write each DR +0x38 with its current value; changes no
state if the engine is idle). Conclusion: **there is no host action that
wakes the ETE engine from the reachable post-boot state**; the missing part
is the device-side event delivery described in section 7. The report records
this as a negative so the next lane does not repeat the search.

---

## 6. The ready exchange and the exact expected observables

The release->ready dialogue is device->host only (ete-frames sec 0/2,
hcc-link sec 0, host-replica sec 2) [V]: the vendor host sends nothing after
the release until DEVICE READY has completed.

Device->host ready frames (wire form recovered byte-for-byte [V]):

    g4 id1: 04 00 ?? 00 0c 00 01 00 ?? 00 ?? ??               (12 B, no payload)
    g4 id2: 04 00 ?? 00 10 00 02 00 ?? 00 ?? ?? 50 08 06 01   (16 B, 4-byte payload)

- id1 = device_plat_ready_msg_process: completes the 200-jiffy (2.0 s) wait.
- id2 = host_ready_msg_process: total-12 payload must be <= 0x63 B, copied
  into the host's 100-byte per-core record (only dword0 is used by the
  vendor consumer: it is the device address of the MSDU-info-ring pointer
  table); completes the 2000-jiffy (20.0 s) wait when the ready map == 1.
- No retries and no host reply in either wait; on timeout the vendor returns
  OAL_FAIL 0x8b2d after printing the device status words.

Host duties implemented: keep the DR0-3 group-B mirror armed with pre-posted
free buffers BEFORE the release; track the device fill +0x3C; on id 3/6/7 (or
the fill watchdog) decode each completed node, read the payload from the node
word0 buffer, decode the 12-byte header, dispatch chip-group-4 ids 1/2, and
return the buffer by advancing +0x38 with the packed index. The service also
logs the first H2D send the vendor makes after ready (group-0 id-1 TXQ config
sync with a dynamic payload) and never synthesizes it.

Expected observables on success (also in BENCH-PROCEDURE.md):

    handshake page      0x86807fef / 7 / 2 / 0xabcd
    state               1
    exception mask      0
    out[1]              bit2 0x4 (startup) and/or bit6 0x40 (deposit notify),
                        bit3 0x8 (ETE done); drained by the service
    DR0-3 fill +0x3c    advances from the 0x10 reset class; host pub +0x38
                        follows; g4 frames decoded
    hcc_ready           g4_id1_seen=1 then g4_id2_seen=1 with 4-byte payload,
                        state READY before the 2.0 s / 20.0 s bounds
    g+0xB4/+0xB8        return to 0 (flush 0xC6108 retired the notify)
    ETE status/send     nonzero status bits and non-zero send words when the
                        engine pumps

---

## 7. The IRQ-unmask question - determination

### 7.1 What the static evidence says

The image only ever executes cpsie i inside (a) the IRQ dispatcher 0xC2EFC,
(b) the deferred-dispatch path from an exception return, (c) the scheduler
idle 0xC2B7A reached from 0xC704C, (d) late thread code (event-router sec 6.2
[V]). Entry A, entry B, pcie_msg_init and the boot task contain no unmask.
Entry A reaches the scheduler only after the ROM/CPU1 handshake (it posts
{0,0xABCD} to 0x1BF008/0x1BF00C, rings CA 0x40161F00 <- 0x00020000, and spins
until 0x1BF008 == 2); then it queues the boot task and calls the scheduler,
which arms the timer CA 0x40160600 (0x61A800 / 7 / 1) and enters the
IRQ-enabled idle. The ROM's IRQ vector targets the image trampoline 0x40064
(rom-dump sec 8 [V]), so the wiring exists.

### 7.2 What GP8 changed and what it did not change

GP8 made page +8 == 2 - the handshake gate the event-router lane flagged as
the blocker is PASSED, and the boot task ran (state 1). But GP9/GP10 still
show the delivery-dead signature: out[0] bit3 not consumed by the H2D
dispatcher (event 0x4C), g+0xB4 = g+0xB8 = 0x40 (flush 0xC6108 / event 0x4E
not retired), ETE status 0 and send words 0 (event 0x2D/0x2E dispatcher not
run), no g4 ready frames (device D2H pump not reached). The exception-seen
mask stays 0, so no handler ran - the CECE consumer was never entered either.

### 7.3 Is the host service (mailbox/ETE/HCC messages) what unblocks it?

**No, per the static evidence.** (i) The vendor host sends nothing between
the release and the first device HCC message (host-replica sec 2.1,
ete-frames sec 1.2, hcc-link sec 0 - independent [V]); (ii) no vendor host
register write touches the 0x4016/0x4018 event controllers (event-router sec
3 [V]); (iii) no firmware H2D receive handler reaches the event dispatcher,
its per-link callbacks or the link-create worker (hcc-link sec 5.2-5.4 [V]).
There is therefore no message or order to send. The host's only interactions
with the image exception path are the boot protocol and the CECE gate: if a
handler is spinning, CECE lets it return; writing CECE does not create
delivery, and writing it during boot is the GP8 bug.

### 7.4 The three-way discriminator (hcc_irq_probe)

One read-only snapshot decides the next blocker:

| observation | meaning | next lane |
| --- | --- | --- |
| EOI CA 0x40160110 nonzero | image dispatcher 0xC2EFC ran: delivery ALIVE | service the mailbox/DR; expect the ready pair - no host unblock needed |
| scheduler timer CA 0x40160600/08/0c all 0 (and enables CA 0x40161108 missing 0x5000) | scheduler 0xC704C never ran / never unmasked: entry A's post-handshake task-queue+scheduler path or the FIQ-armed reset slot did not complete. Firmware/ROM-internal. | firmware-side: entry-A post-handshake path / FIQ slot arms (CPU 0x17D42C); or a vendor golden trace |
| timer armed, EOI 0, raises latched (CA 0x40161188/0x40161288 == 0x5000) | events 0x4C/0x4E latched but CPU/ROM IRQ delivery never vectored: mask still set or ROM vector contract. Firmware/ROM-internal. | firmware/ROM IRQ entry decode or vendor golden trace |

This is the honest determination: the host counterpart cannot fix this class
of blocker, and the report says so instead of adding speculative writes.
The H2D valid-frame announce remains useful only as a liveness probe once the
H2D dispatcher (event 0x4C) is alive; a timeout is a confirmed negative.

---

## 8. Implementation summary

Module hi5622_pciet_hcc.ko (driver name hi5622_pciet_hcc, own module; load
ONLY this one - one driver can bind the endpoint). The transport skeleton
files are verbatim copies of wifi-transport-port/build with four marked HCC
hunks in pciet_main.c (driver name+poll default, HCC probe/remove hooks, and
hard refusals of the incompatible legacy announce/ringinit/send/poll attrs)
and the state pointer/prototypes in hi5622_pciet.h.

Sysfs surface (endpoint device dir, e.g. /sys/bus/pci/devices/0000:00:00.0):

| attribute | mode | purpose |
| --- | --- | --- |
| hcc_arm | WO | one-shot vendor-exact ETE init (intr RMW, SR0-2 group A, DR0-3 group B, pre-post 32) + readback; refuses outside the DMA window / second arm |
| hcc_loop | WO | start/stop the service kthread ("1"/"0"); cadence module param hcc_poll_ms (default 5) |
| hcc_service | WO | one manual service pass (vendor order + CECE policy + DR drain + IRQ watch) |
| hcc_sr_announce | WO | post one VALID SR frame + mailbox id 3 announce (default safe group-0 id-0 stub); hard refusal matrix |
| hcc_cece_kick | WO | bounded manual CECE (<=3/load, post-handshake only, refused in mode 1) |
| hcc_dr_republish | WO | S3-only: one re-publish of DR0-3 +0x38 with the current value |
| hcc_clear | WO | reset telemetry counters (rings/ready state kept) |
| hcc_status | RO | boot page/state/mask/boot slot, mailbox words incl. read-only out[5], g+0xB4/+0xB8, ETE intr/status/send/fill/enables |
| hcc_ready | RO | ready state machine, g4 id1/id2 decode + payload, timeout verdict |
| hcc_irq_probe | RO | the three-way IRQ-layer discriminator (controller, EOI, vector, enables, latched raises, scheduler timer) + verdict |
| hcc_events | RO | D2H bitmap counters/history, CECE counters, H2D announce telemetry |
| hcc_rings | RO | all 7 channel registers + device-authored send words, read-only |
| hcc_plan | RO | the bench ladder and stop rules as text (operator convenience) |

Safety encoded (defence in depth):
- single gated write helpers; CA 0x400392F0 (out[5]) never written
  (BUILD_BUG_ON + runtime WARN_ON gate); only read;
- never write the firmware-owned ETE fields (+0x00, +0x08, +0x1C, +0x3C) or
  the device-authored send words (+0xC00/+0xC18); never write device RAM;
  never read the RC misc window 0x10161000; UART untouched;
- CECE writes gated by handshake + evidence (section 4);
- H2D: no id 5, no group-4, no payload > 0x674, one frame per request, no
  retry after timeout; announce only after handshake and with out[0]==0;
- probe() performs no device writes; only explicit sysfs stores write.

Build: see BUILD-NOTES.md. Module 201,140 B, sha256
8fd9085431ee6b064f435f9550d9dfef5d74b0023020983682361af7746a48db, vermagic
6.18.60 SMP mod_unload ARMv7 p2v8, zero warnings under CONFIG_WERROR, modpost
clean; 13 attributes present; frame/ring self-test 83/83 PASS.

---

## 9. Bench procedure (summary; full detail in BENCH-PROCEDURE.md)

    S0 preconditions: recovery door armed, bench link PASS, UART read-only,
       image + module hashes recorded, no vendor wifi modules loaded.
    S1 insmod hi5622_pciet_hcc.ko   (base D2H poll defaults OFF in this build)
    S2 prepare -> upload -> verify (diffs=0)
    S3 hcc_arm (7/7 readback OK; DR pub=0x400; NEVER write CECE here)
    S4 start hcc_loop BEFORE the release, then echo 1 > release (0x5a5a ONLY)
    S5 watch the boot: handshake page +8=2, state=1, mask=0 (expected GP8
       signature); CECE is refused pre-handshake by design
    S6 ready gate: hcc_ready expects g4 id1 <= 2.0 s, g4 id2 <= 20.0 s
    S7 if no ready: hcc_irq_probe decides the three-way table of section 7.4
    S8 optional one-shot H2D liveness: hcc_sr_announce (valid frame + id3)
    S9 optional S3-only ETE nudge: hcc_dr_republish (one per session)
    S10 capture dmesg/UART + all hcc_* attrs; power-cycle (no rmmod after
        release); ledger entry.

Stop rules: no out[5] write; no device RAM write; no H2D id 5; no group-4
H2D; no CECE before page+8==2; one idea per session; any abort/SError/hang ->
capture and stop; no retries after an H2D announce timeout.

---

## 10. What is proven vs assumed, and open items

Proven by construction/host-side evidence:
- the vendor-ordered service, CECE policy, frame validation and ready decode
  match the committed decoded contract (every constant carries a source);
- the module compiles clean and exposes the documented surface;
- the frame model/ring arithmetic pass 83 host checks.

Not proven by this lane (host-only): no part of the module has been loaded on
the device. The bench run is the Lead's attended session.

Open items [U]:
- which firmware/ROM code completes event delivery once the scheduler runs -
  the next bench snapshot (section 7.4) narrows it to one of two firmware
  locations;
- the exact DR channel that carries g4 id1/id2 (prediction DR0 from the
  channel flags; the service decodes all four, so the bench will pin it);
- the intent of the first post-ready group-0 id-1 TXQ config sync payload
  (dynamic, not reconstructed - do not synthesize);
- whether the image ever enters the CECE spin on a healthy boot (vendor mask
  stays 0, so probably not) - the policy observes and reacts rather than
  assuming.

---

## 11. Evidence ledger / reproduction

    # module build (host-only, module only)
    ./tools/build.sh

    # frame model + ring arithmetic self-test (no device)
    python3 tools/frame_selftest.py        # -> 83 checks PASS

The constants in src/hi5622_hcc_service.h cite the lane and section each one
came from (host-replica sec 4.2, ete-frames sec 1.3/2/3.2, ready-emit sec
2.4/3.4/4.1, ete-init sec 2, event-router sec 2, rom-dump sec 5/7). The
bench-state quotes are the ledger entries 2026-11-24 10:40 (GP8), 11:10
(GP9), 11:40 (GP10) and STATUS.md sections 3b/3c.

No vendor binary, object code, firmware byte, calibration value or secret is
included in this directory. Derived addresses/constants are used exactly as
the prior lanes' reports publish them.

---

## 12. Corrections carried forward (so they are not repeated)

- The pre-GP8 plan "write 0xCECE on every out[1] bit2 announce and hold it
  live during boot" is WRONG: the boot handshake lives in the same register
  and needs 0xCEED until page+8==2 (GP8; rom-dump sec 6-7). This service
  refuses pre-handshake CECE and keeps the legacy announce attr dead.
- The pre-GP8 H2D "smoke" that posts a 64-byte zero header is invalid and was
  dropped by the firmware tag/length check (ete-frames sec 3.2). This service
  builds a valid header and refuses zero/invalid frames.
- "The host counter to the firmware vector handler is a CECE ack per announce
  and that alone unblocks the boot" is half true: the CECE ack releases an
  observed handler, but the IRQ/event layer activation is image/ROM-owned and
  is not host-message-triggerable (section 7).
- "out[1] bit2 = DEVICE READY" is wrong (it is the startup/vector announce
  0x4); the real ready pair is device->host HCC group-4 id1/id2 on DR.
