# Host HCC protocol core: the complete post-ready state machine (compile-ready + tests)

**Lane:** wifi-hcc-core (shared task **task-92**), **host-only**, 2026-11-24.
**Status:** design + portable implementation + kernel compile check + 528/528
deterministic host selftest.  The bench router was never contacted, no image was
built, and no vendor binary, firmware byte, object code, calibration value or
secret is reproduced here - only derived addresses, constants, decoded
structure and original code.

**Deliverables (all in this directory):**

| path | contents |
| --- | --- |
| REPORT.md | this document: protocol model, the policy fix list, verification, limits |
| INTEGRATION.md | exact integration map into the hi5622_pciet_hcc module + the code-level fix list |
| BUILD-NOTES.md | build result, toolchain, hashes, reproducibility |
| src/hcc_core.h | contract header: address maps, mailbox/boot/frame/ready/PM constants, all types, API |
| src/hcc_core.c | the state machine (portable C99; no kernel dependency, no device contact) |
| src/hcc_core_kernel.h/.c | reference kernel adapter (readl/writel ops, jiffies clock, dev_info events) |
| src/kmod/Kbuild, src/kmod/hcc_core_check.c | device-free compile-check kernel module (compile verification only) |
| tests/hcc_core_test.c | deterministic fake-BAR0 selftest: ready pair, timeouts, coalescing, ring wrap, CECE policy, write census |
| tools/build.sh | host-only build driver: host selftest + 6.18.60 kernel compile check |

**Read-first inputs (all committed under opensource/docs/incoming/):**
wifi-hcc-service (the service this core generalises; its REPORT sec 3-6 and
src/hi5622_hcc_service.h constants), wifi-d2h-direct (the working direct
device group-A receive path and the engine-path correction), wifi-host-replica
(vendor pcie_msg_handle / pcie_msg_send semantics, post-release write list,
ready waits, PM + heartbeat), wifi-h2d-catalog (H2D receive tables: groups 0/2
only, id 5 BUG trap, group-0 id-1 TXQ config sync), wifi-ete-frames (12-byte
header/tag/node contract), wifi-rom-dump (corrected release-only boot protocol),
wifi-hcc-link (nothing is sent between release and ready), wifi-fw-postboot
(corrected post-handshake state), plus the ledgers 2026-11-24 10:40 (GP8),
11:10/11:40 (GP9/GP10), 12:20 (hcc bench), 14:00 (direct-read ready pair) and
16:10 (PD patch page overwrite).

Evidence markers follow the project convention: **[V]** verified from a named
local artifact or committed lane report, **[V*]** device-measured by an
earlier attended lane and cited, **[I]** inference, **[U]** unknown.

Address conventions (project-wide, and implemented in the core):
- device IO CA -> host BAR0 = **0x3b8000 + (CA - 0x40000000)** [region 3];
- firmware CPU -> host BAR0 = **0x6b8000 + CPU** [region 5].

---

## 0. TL;DR

1. **One state machine owns the whole post-release protocol.**  Release ->
   BOOT (handshake page CA 0x1bf000 +8 == 2, latched once) ->
   WAIT_PLAT_READY (chip-group-4 id1, <= 2.0 s) -> WAIT_DEVICE_READY
   (chip-group-4 id2, <= 20.0 s, payload copied into the vendor 100-byte
   per-core record) -> READY_TXQ_SYNC (first H2D: group-0 id-1 TXQ config
   sync) -> ACTIVE (D2H/mailbox, DR service, PM state, heartbeat).  Every
   timeout, coalescing rule and ordering constraint in the decoded contract is
   implemented and unit-tested; the library is device-free (time and IO are
   injected) so the tests are fully deterministic.
2. **The three policy fixes requested by the task are in the code, not in a
   comment:** (a) the handshake gate is latched when first observed, so the
   firmware overwriting the page later (PD patched boot) cannot lose the gate;
   (b) the ready pair is accepted from the direct-read catalogue (the working
   path) as well as the legacy engine-copied host-DR path, deduped at the first
   transition; (c) CECE (CA 0x4000010c = 0x0000cece) is written only after the
   latch and only on observed handler-spin evidence, one write per event, with
   an optional bounded manual kick and a mode that disables all writes for
   negative-control sessions.
3. **A static mapping bug in the current service is identified and fixed here:**
   the inherited transport macro maps device IO CAs with a 20-bit mask, so
   out[3]/out[4] (CA 0x40101438/0x40101414) resolve to BAR0 +0x3b9438/+0x3b9414
   instead of +0x4b9438/+0x4b9414.  The core uses the range-checked region
   formula and the tests pin both addresses (INTEGRATION.md fix F4).
4. **The H2D path is the vendor pcie_msg_send/pcie_msg_send_irq algorithm:**
   pending/in-flight bitmaps, publish to out[0], doorbell RMW out[2] |= 1,
   bounded polled wait for out[0] == 0, coalescing of announcements, hard
   refusals (id 5, ids outside {1,3}, group 4, oversized payload) and no
   write to out[5] ever.  The first post-ready H2D (group-0 id-1 TXQ config
   sync) is modelled as a state transition whose dynamic payload the caller
   must supply - the core never synthesises it [V].
5. **Verification:** host build zero warnings under -std=c99 -Wall -Wextra
   -Werror -pedantic; **528/528 selftest checks pass**; the same core compiles
   clean as part of a kernel module against the project's **6.18.60** tree
   (vermagic 6.18.60 SMP mod_unload ARMv7 p2v8, GPL, zero warnings) - i.e. it
   is ready to drop into the in-tree hcc service module (INTEGRATION.md).

---

## 1. Method and what is new in this lane

This is a synthesis + hardening lane.  The protocol facts were established by
the reversal lanes listed above; what was missing was a single, portable,
tested implementation that owns the sequence and encodes the post-GP8 policy
corrections, instead of a collection of service-pass fragments that each
re-derive the state.  New content:

- the **latched boot gate** and the page-overwrite observability (the PD patched
  image overwrites CA 0x1bf000 minutes after S1; a live re-read would refuse
  every later H2D/announce - the latch makes the gate sticky) [V*, ledger
  2026-11-24 16:10];
- the **unified ready state machine** fed by either receive path, with the
  vendor's exact validation rules (id1: 12-byte total, header chip id 0;
  id2: total 13..111, payload copy into read_msg, ready_map bit, single-core
  completion) [V];
- the **H2D mailbox coalescing state machine** as a first-class object with
  deterministic tests (the current service does the out[0]/out[2] writes
  inline in the sysfs handler and cannot be tested device-free);
- the **write-allowlist and safety census** (six writable CAs; out[5] and the
  transport release word are unreachable by construction, every refused write
  is counted, and the selftest asserts a zero bad-write count);
- the **out[3]/out[4] BAR0 mapping correction** (INTEGRATION F4);
- the **kernel-compile proof**: a compile-check module builds the same
  hcc_core.c under the tree's -Werror and CONFIG_MODULE_STRIPPED=y kernel
  build, so "compile-ready" is demonstrated, not asserted.

No part of this lane touches the device; the bench run of the integrated
module is the Lead's attended session (INTEGRATION.md section 6).

---

## 2. The protocol model

### 2.1 Phases, entry conditions and deadlines

| phase | entered when | deadline | exit |
| --- | --- | --- | --- |
| IDLE | init | - | hcc_core_note_release() (transport wrote 0x5a5a to CA 0x40000108) |
| BOOT | release noted | 5000 ms | handshake page +8 == 2 seen (latch); optional one-shot CEED repair |
| WAIT_PLAT_READY | boot latch | 2000 ms from latch | chip-group-4 id1 (total 12, chip id 0) |
| WAIT_DEVICE_READY | id1 | 20000 ms from id1 | chip-group-4 id2 (total 13..111) |
| READY_TXQ_SYNC | id2 | none (vendor first H2D; payload caller-supplied) | group-0 id-1 SR commit -> ACTIVE |
| ACTIVE | TXQ sync committed | per-message timers | steady state; H2D allowed; failures keep servicing |
| FAILED | any timeout | - | D2H mailbox service continues (vendor S9); H2D refused |

Timings are the vendor literals [V]: wait_for_completion_timeout(...200 jiffies)
and 2000 jiffies with HZ=100 in both trees -> 2.0 s / 20.0 s; the PM sleep-ack
wait is 50 jiffies -> 500 ms; the heartbeat is a 10 s one-shot hrtimer.  The
core takes a caller-supplied millisecond clock, so the timeouts do not depend on
HZ and the tests can hit them exactly.

### 2.2 Corrected boot protocol (encoded as the latch)

One 16-bit register CA 0x4000010c carries two mutually exclusive handshakes [V]:
the mask ROM's CPU0 cold path writes **0xCEED** after the release and CPU1's S1
accepts only that value, while the image service/exception handler CPU 0xC6E14
zeros the word and spins for 32-bit **0x0000CECE**.  GP8 (ledger 2026-11-24
10:40) proved the successful sequence is: release 0x5a5a only, no CECE during
the boot stage, then the golden handshake page 0x86807fef / 7 / 2 / abcd,
state CA 0x118b60 = 1, exception mask 0.  The core:

- never writes CA 0x40000108 (the release is the transport's job) and never
  writes CECE before the latch;
- latches on page +8 == 2 (not on the golden words; the PD image boots with
  +0 = 0x86803FEF / +4 = 0x17 and must still work) [V*];
- reports whether the latch matched the golden page (handshake_golden) and
  counts post-latch page-overwrite observations (page_lost_after_latch)
  without ever un-latching;
- offers an **opt-in** one-shot 0xCEED re-assert for a boot where +8 stays 0
  and the gate has been clobbered (default off; GP8 needs no repair) [I].

### 2.3 D2H mailbox service (vendor pcie_msg_handle) [V]

Per observed word, exactly:

    probe-read out[1] (CA 0x40039014); if zero -> NO device writes this pass
    out[3] = 1                (CA 0x40101438; ack FIRST)  [correct BAR0 +0x4b9438]
    bitmap  = re-read out[1]  (merge a word published in between)
    out[1] = 0                (drain; device precondition P2)
    out[4] = 1                (CA 0x40101414; re-arm)     [correct BAR0 +0x4b9414]
    for id = lowest set bit first, ids 0..10; stop at the first id > 10:
        id 1  pcie_dev_ready_msg_handle        empty stub
        id 2  exception_panic_msg_process      startup/vector announce: capture
                                                mask CA 0x40008 + the 17-word
                                                context frame CA 0x40000c, never
                                                send H2D id 5
        id 3  pcie_ete_transfer_done_handle    drain DR channels (mask 0x1f)
        id 4  wlan_pm_slp_disallow_callback    PM state: device refuses sleep
        id 5  wlan_pm_wkup_dev_ok_callback     PM state: wake-up OK
        id 6  pcie_trigger_ete_sending_handle  deposit notify -> drain DR; NO CECE evidence
        id 7  pcie_trigger_ete_sending_handle  sending variant -> drain DR; CECE evidence
        ids 0, 8, 9, 10                        no handler -> counted/dropped

The pass is serialised and returns the serviced-bitmap; after a failure the
loop keeps servicing (vendor SPEC S9).  The CECE evidence classes are exactly:
out[1] bit2, a registered handler other than id6, or a change (first nonzero)
in the exception-seen mask CA 0x40008.  One write per evidence event.

### 2.4 Ready exchange (device->host, no host reply) [V]

Wire forms (byte-for-byte from the captured device rings):

    g4 id1: 04 00 ?? 00 0c 00 01 00 ?? 00 ?? ??              (12 B, no payload)
    g4 id2: 04 00 ?? 00 10 00 02 00 ?? 00 ?? ?? 50 08 06 01  (16 B, 4 B payload)

id1 completes wait #1 only when total == 12 and header chip id (buf[1] >> 4) is
0; id2 completes wait #2 when total is 13..111, copies the payload into the
100-byte per-core read_msg record (only dword0 is consumed by the vendor
driver) and sets ready_map bit 0.  Duplicates are counted but cannot re-trigger
a transition.  Both receive paths feed the same handler:

- **HCC_SRC_DIRECT** - the direct device group-A ring reader (the working path;
  the ETE engine A->B copy is firmware-triggered and idle) [V*];
- **HCC_SRC_ENGINE** - the legacy engine-copied host group-B DR path (kept for
  completeness; it never sees frames while the engine is idle) [V].

### 2.5 H2D send path [V]

The core owns the protocol half; the transport owns the DMA buffer and the
actual node store:

    build a valid 12-byte header (group in [0,2]; id 5 refused; total = 12+payload
      <= 0x680; tag 0x5a5a; allocated nibble 0x1; dst in both +1 and +9)
    sr_commit(): node word1 = (total << 16) | 0x6d2b; packed producer advance
      (low 10 bits + 0x400 phase, depth 32); then ONE mailbox id-3 announce
      per coalesced batch
    mbox_send(id): pending |= BIT(id); if in-flight == 0 publish:
      out[0] = pending; out[2] |= 1 (RMW); in-flight = pending, pending = 0
      else coalesce into pending
    mbox_tick(): poll out[0] (the device clears it when it consumes the mask;
      bounded by cfg.mbox_clear_timeout_ms); on zero: complete the mask and
      publish the coalesced pending batch; on timeout: close the H2D lane
      (counted, one event, no further writes)

The core refuses H2D before the boot latch and in the FAILED phase, ids outside
{1,3} (id 1 only when PM is enabled), id 5 always, and payload > 0x674.  The
first post-ready H2D is the vendor's group-0 id-1 TXQ table sync; its payload is
dynamic and not reconstructed, so the API builds the frame from a caller-
supplied payload and the READY_TXQ_SYNC -> ACTIVE transition happens when that
frame is committed (hcc_core_txq_sync_build + hcc_core_sr_commit) - the core
never invents config data.

### 2.6 PM and heartbeat [V]

- PM state machine: chip-group-4 id4 -> device refuses sleep; id5 -> wake-up OK;
  a host sleep request is mailbox **id 1** (vendor: 50-jiffy sleep-ack wait)
  and is disabled by default because the vendor PM layer is not on the boot
  path.  When enabled, the request is gated on ACTIVE, one at a time, with the
  500 ms ack deadline; id4 aborts a pending request; expiry counts a timeout.
- Heartbeat: a 10 s one-shot, restartable by chip-group-4 id 4 **only while
  armed**; the vendor boot path never arms it (heartbeat_start has no caller in
  plat.ko; only the debug modules import it), so the core defaults it off.  When
  armed, each missed period is counted and surfaced as an event (the vendor
  submits EXCP_HEART on expiry).

### 2.7 Frame contract [V]

    12-byte header, little-endian:
      [0] group (low nibble)          [1] high nibble = chip/core, low = alloc (vendor 0x1)
      [2] vap                         [3] chip id
      [4..5] u16 total = payload+12   [6..7] u16 message id
      [8] field C                     [9] dst core
      [10..11] 0x5a5a transport tag   [12..] payload (<= 0x674)
    device ring node: word0 = buffer device VA, word1 = (len<<16) | 0x6d2b

Validation used for both directions: tag must be 0x5a5a, total must equal the
wire/node length, total in [12, 0x680].  The firmware's own SR receive check
compares exactly the tag and the length at file 0x8699e/0x869b0 [V], which is
why the old "64-byte zero header" smoke was dropped [V*].

---

## 3. Policy fix list (what the integrated module must change)

The code in this lane is the reference for these fixes; INTEGRATION.md maps each
one to the current module function.

| # | fix | why it matters | evidence |
| ---: | --- | --- | --- |
| F1 | **Latch** handshake page +8 == 2 on first observation; never re-gate on a live re-read; count page-overwrite observations | the PD patched image overwrites CA 0x1bf000 minutes after S1; a live check then refuses every announce/H2D | [V*] ledger 2026-11-24 16:10 |
| F2 | Feed the **direct-read catalogue** id1/id2 into the ready state machine, and keep the engine path as a second source (dedupe at first transition) | the ETE engine A->B copy is firmware-internal and idle; the ready pair is only visible on the device group-A ring | [V*] wifi-d2h-direct sec 0/2/6 |
| F3 | CECE: **post-latch only**, evidence-driven (bit2 / handler / mask change), one write per event, bounded kick <= 3, mode to disable writes | writing CECE during the ROM stage clobbers 0xCEED and stalls CPU1/S1 (the GP8 root cause); blind writes after boot change device state | [V] wifi-rom-dump; [V*] GP8 |
| F4 | Use the **21-bit region-3 formula** for device IO CAs (BAR0 = 0x3b8000 + CA - 0x40000000); the inherited HI5622_BAR0_IO 20-bit mask mis-maps out[3]/out[4] to +0x3b9438/+0x3b9414 instead of +0x4b9438/+0x4b9414 | the ack/re-arm registers are above 0x40100000; a wrong address means the intended ack/re-arm never lands (the out[1] drain still worked, which is why the bug stayed invisible) | [V] transport REPORT address table + host-replica sec 4.1 |
| F5 | H2D announce: pending/in-flight **coalescing**, out[0] publish, out[2] |= 1 doorbell, polled wait on **out[0]** (not out[2]), bounded timeout + lane close, never out[5], never id 5 | pcie_msg_send/send_irq semantics; the pre-GP8 transport waited on the wrong register; out[5] is a project hard rule and id 5 is a firmware BUG trap | [V] host-replica sec 3.2; h2d-catalog sec 3-4 |
| F6 | SR commit: node word1 = (total<<16)\|0x6d2b, packed producer mirror, one id-3 announce per batch; TXQ sync is the first post-ready H2D and its payload is **never synthesised** | firmware SR receive validates tag+length; the ready->active transition is the vendor's TXQ table sync, whose payload is dynamic | [V] ete-frames sec 1.3/3.2; hcc-link sec 0; h2d-catalog sec 6.1 |
| F7 | Enforce a write allowlist (out[0..4] + gate); make the release word and out[5] unreachable; no firmware RAM stores; keep a bad-write census | defence in depth for a public research notebook; the selftest asserts the census stays zero | [V] project hard rules |
| F8 | Deadlines: boot 5 s, plat 2 s, dev 20 s (from the latch), PM 500 ms, heartbeat 10 s; keep servicing D2H after FAILED | vendor waits are 200/2000 jiffies at HZ=100; SPEC S9 keeps the mailbox alive after a failed load | [V] host-replica sec 2/4.4/7 |
| F9 | Frame validation on both paths: tag 0x5a5a, total == wire length, max total 0x680, H2D groups {0,2}, group 4 device->host only; id1 total 12 / id2 total 13..111 | the firmware drops frames that fail its own tag/length check; a synthetic group-4 H2D has no receive table | [V] ete-frames; h2d-catalog sec 4 |

---

## 4. Implementation summary

### 4.1 Layout

The core is one translation unit and one header with **no kernel dependency**
(plain C99; bool and hcc_u* types selected by __KERNEL__).  It performs no
allocation, no sleeping, no timers and no logging: time is an argument and IO
goes through two function pointers.  That is what makes the tests deterministic
and the kernel integration trivial.

    struct hcc_core {
        io ops (read32/write32) + config + event callback
        boot:    released, boot_latched, handshake[4], golden, page_lost, ceed_repairs
        ready:   phase, fail_reason, deadline_ms, ready_map, id2 payload/read_msg,
                 id1/id2 validation counters, txq_sync state
        d2h:     word/bitmap counters, per-id counters, hist ring, vector capture,
                 need_dr_drain / wake counters, excp_announce
        cece:    mode, pending, evidence classes, writes/refused counters
        h2d:     pending/in-flight masks, publish/completion/coalesce/timeout counters,
                 lane-closed flag, SR channel mirrors (producer/depth/node encoding)
        hb/pm:   armed/deadline/counters, PM flags/pending/deadline/timeouts
        safety:  writes, bad_writes, bad_reads, last write CA/value
    };

### 4.2 Public API (src/hcc_core.h)

    hcc_core_config_default / hcc_core_init
    hcc_core_note_release(core, now)          record the transport's 0x5a5a write
    hcc_core_tick(core, now)                  one service pass (recommended every 5 ms)
    hcc_core_boot_check / exception_watch      individual stages
    hcc_core_d2h_poll / mbox_tick / cece_service
    hcc_core_deliver_frame(src, frame, len)   one decoded device->host frame
    hcc_core_deliver_hdr(src, hdr, payload, avail, wire_len)  snapshot form
    hcc_core_frame_build / frame_decode / txq_sync_build
    hcc_core_sr_commit(chan, devva, group, id, total)  node encoding + id-3 announce
    hcc_core_mbox_send(id)                    PM id 1 / announce id 3 (guarded)
    hcc_core_cece_kick()                      bounded manual kick (mode 2)
    hcc_core_heartbeat_arm / disarm
    hcc_core_pm_request_sleep(now)
    hcc_core_status(buf, len)                 one-line observability for sysfs/dmesg
    pure helpers: hcc_bar0_io/fw, hcc_idx_plus/count/slot, hcc_sr_node_word1,
                  hcc_ca_writable, hcc_phase_name, hcc_event_name

### 4.3 Determinism and observability

Every wait is a caller-supplied deadline; every device access is counted; every
event (boot latch, timeout, D2H word/dispatch, CECE write/refusal, ready
transitions, TXQ sync, heartbeat, PM, H2D publish/complete/timeout, invalid or
unknown frames) is emitted through one callback.  The status line exposes the
post-GP8 stall observables the earlier lanes used (out[1] bitmap history, id
vector capture, exception mask, ready map/payload, CECE counters, H2D
coalescing/timeouts, heartbeat, PM), so the next attended session can be read
from one cat.

---

## 5. Verification

### 5.1 Host selftest (deterministic, device-free)

    ./tools/build.sh --no-kernel
    -> host build: zero warnings (-std=c99 -Wall -Wextra -Werror -pedantic)
    -> wifi-hcc-core selftest: 528 checks, 0 failure(s); RESULT: PASS

The fake device is a BAR0 with the real region-3/region-5 windows, a firmware
RAM image, a write recorder with CA+value, and a coalescing hook for out[1].
Coverage:

- **address maps**: every CA used by the core, including the corrected
  out[3]/out[4] BAR0 addresses and the out-of-range rejections;
- **ring arithmetic**: plus/count/slot, pre-post 0x400, 40-node walk on depth
  32 (slot sequence 0..31 twice, final packed index 0x408);
- **frame contract**: build/validate the captured stub/id1/id2 wire forms,
  refusals (tag, length, short, oversize, group 4 H2D, id 5, payload bound);
- **boot latch**: no latch before +8 == 2; latch on golden page; latch on the
  PD-style non-golden page; page overwrite after latch leaves the latch intact;
  boot timeout and late-latch recovery; no writes before the latch;
- **CECE policy**: each evidence class produces exactly one 0x0000CECE write;
  no new evidence -> no write; pre-latch evidence refused/counted; mode OFF
  disables writes; bounded kick (<= 3) and refusals; opt-in CEED repair;
- **D2H service**: ack -> drain -> re-arm order checked against the write log;
  zero bitmap writes nothing; ids 1..7 dispatched, 0/8/9/10 dropped, bit > 10
  stops; coalesced word merged; id2 vector capture (mask + 17-word frame);
- **ready pair**: id1 ENGINE then id2 DIRECT completes READY -> TXQ; payload
  0x01060850 lands in id2_payload and read_msg; duplicate/wrong-core/wrong-total
  handling; 2.0 s and 20.0 s timeouts at the exact deadline; mailbox keeps
  being serviced after FAILED;
- **H2D**: coalescing of id 3 / id 1 batches, exactly two writes per publish
  (out[0], out[2] RMW), completion + next-batch publication, bounded timeout +
  lane close + refusal after close, id 5 / ids outside {1,3} / pre-latch
  refusals;
- **SR**: node word1 encoding, 40 commits on depth 32 (wrap + phase), one
  publish for 40 coalesced announces, DMA-window/channel/group refusals;
- **heartbeat / PM**: armed/disarmed heartbeat, id4 restart, exact 10 s expiry;
  PM disabled default, ACTIVE-only requests, id5 wake-ok, id4 abort, 500 ms
  timeout;
- **write safety census**: every write in every test is in the allowlist, no
  firmware-RAM store, no release-word write, no out[5] write, and the core's
  bad-write/bad-read counters stay zero.

### 5.2 Kernel compile check (6.18.60)

    ./tools/build.sh
    -> host selftest PASS
    -> CC [M] hcc_core.o / hcc_core_kernel.o / hcc_core_check.o
    -> hcc_core_kmod.ko (85,404 B, GPL, vermagic 6.18.60 SMP mod_unload ARMv7 p2v8)
    -> zero compiler warnings, modpost clean
    -> insmod-time (device-free) smoke: frame contract + address map assertions

This proves the library and the reference adapter compile under the project's
kernel build (including its -Werror and CONFIG_MODULE_STRIPPED=y), which is the
"compile-ready" bar for the in-tree hcc service module.  The compile-check
module is **not** part of the service module and never touches a device (its IO
ops are NULL).

### 5.3 Safety invariants (tested, not just documented)

- only six device CAs are writable: out[0] 0x40039010, out[1] 0x40039014,
  out[2] 0x400392d4, out[3] 0x40101438, out[4] 0x40101414, gate 0x4000010c;
- out[5] CA 0x400392f0 and the CPU release word CA 0x40000108 are not in the
  allowlist; the write helper refuses them and counts the attempt;
- no firmware/device RAM store exists in the core (no writer at all);
- CECE is unreachable before the latch and bounded afterwards;
- H2D id 5, group-4 H2D, ids outside {1,3}, payload > 0x674 and DMA addresses
  outside 0x80000000..0x8fffffff are refused;
- the poller writes nothing on a zero D2H bitmap (probe-read first).

---

## 6. Integration (details in INTEGRATION.md)

The integrated module is the wifi-hcc-service + wifi-d2h-direct module
(hi5622_pciet_hcc).  The core replaces the protocol fragments; the transport
(probe/iATU/upload/release) and the direct-read ring walk stay where they are:

- probe: embed struct hcc_core in the service state, initialise it with the
  kernel adapter (hcc_kernel_init(ctx->bar0, &cfg)), keep the current sysfs
  surface and add the core status line;
- hcc_loop / hcc_service: call hcc_core_tick() each pass instead of the
  hand-rolled mailbox/CECE fragments; call hcc_core_note_release() right
  after the transport's release write;
- d2h-direct: after decoding a frame, feed it to
  hcc_core_deliver_hdr(HCC_SRC_DIRECT, ...); the legacy engine DR walk, when
  it produces a frame, feeds the same handler with HCC_SRC_ENGINE;
- hcc_sr_announce: build with hcc_core_frame_build() into the SR buffer and
  commit with hcc_core_sr_commit(); the core does the announce/coalescing;
- hcc_ready / hcc_status: render hcc_core_status(); the old boot-page live
  check and the CECE policy are removed (F1/F3);
- fix F4 in the transport macro (or use hcc_bar0_io()) before the next
  session.

---

## 7. Limits and open items (honest)

- **Not device-tested by this lane.**  No module is committed here and the bench
  router was not contacted; the attended run of the integrated module is the
  Lead's session.  The protocol facts are cited from committed lane reports and
  the GP8/GP9/GP10/direct-read ledgers.
- **The TXQ-sync payload is not reconstructed** [U]: the vendor's first
  post-ready H2D (group-0 id-1) carries dynamic config data (TXQ table address
  sync).  The core models the transition and builds a valid frame from a
  caller-supplied payload, but sending a synthetic one would be exactly the
  kind of speculative frame the h2d-catalog lane warns against.
- **PM id-1 ack semantics are partly [U]**: the vendor waits 50 jiffies for a
  sleep-ack completion whose exact completion source is not decoded; the core
  models the wait and clears it on the observable id4/id5 PM events and on
  timeout.  The PM layer stays disabled by default.
- **Heartbeat is instrumentation, not boot-path**: the vendor boot never arms
  it [V]; the core keeps it available for debug sessions.
- **The out[3]/out[4] mapping finding (F4) is static**; it explains why the
  ack/re-arm writes may not have landed where intended even though the out[1]
  drain (correctly mapped) kept the service working.  Whether that contributed
  to the observed g+0xB4/+0xB8 deferred-notify state needs the attended session
  [I] - the core is correct either way.
- The engine-path receive is kept as a source for completeness; the engine A->B
  copy remains firmware-internal and idle (wifi-ete-engine/ready-emit
  negatives), so the direct reader is the operative path.

---

## 8. Reproduction (host-only)

    # host selftest (no kernel tree needed)
    ./tools/build.sh --no-kernel

    # host selftest + 6.18.60 kernel compile check
    ./tools/build.sh

    # individual steps
    gcc -std=c99 -O2 -Wall -Wextra -Werror -pedantic -I src \
        src/hcc_core.c tests/hcc_core_test.c -o /tmp/hcc_core_test
    /tmp/hcc_core_test          # -> 528 checks, 0 failure(s)

All paths are derived from the commit history of the referenced lanes; no
vendor artifact is read at build or test time (the selftest is 100 % synthetic,
and the kernel check does not link any vendor binary).

---

## 9. Corrections carried forward (so they are not repeated)

- Writing 0xCECE during the ROM boot stage is the GP8 bug, not a fix: the same
  register carries the ROM's 0xCEED handshake.  The gate is latched + evidence-
  driven here.
- The old engine-path DR receive is structurally wrong for this boot (the
  engine is idle; the device group-A ring is the real source) - the core
  accepts the direct path and does not depend on +0x3C/+0x38 engine fields.
- "out[1] bit2 = DEVICE READY" is wrong: it is the startup/vector announce;
  real ready = chip-group-4 id1/id2 over the device ring.
- A 64-byte zero header H2D smoke is invalid (tag/length check); the builder
  always writes tag 0x5a5a and the true total.
- The "scheduler never armed" IRQ probe result is an instrument artifact
  (fw-postboot correction); the core does not encode it.
- The inherited 20-bit device-CA mask is wrong for out[3]/out[4]; use the
  region formula (F4).
