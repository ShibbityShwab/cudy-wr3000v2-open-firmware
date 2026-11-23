# BENCH-PROCEDURE - host HCC service on the corrected boot (attended only)

**Lane:** wifi-hcc-service (task-87). **Status:** PLAN - this lane is host-only
and never contacted the device. The run below is the Lead's attended session.
It builds on the project hard rules (BENCH-FACTS.md, RUNBOOK.md, LESSONS.md);
the newest dated ledger entry and STATUS.md win over this document.

**Instrument:** hi5622_pciet_hcc.ko from this lane (see BUILD-NOTES.md).
It is its own module (driver hi5622_pciet_hcc); load ONLY it - one driver can
bind the endpoint, so do not load the base transport or the B3 instrument in
the same boot.

**Goal of the run:** put the host side of the corrected boot in place
(vendor-order D2H service + boot-guarded CECE policy + valid-frame ETE SR
announce + DR/ready receive path) and take the read-only discriminators that
decide whether the post-GP8 stall is (a) service-side (fixable here),
(b) image-scheduler/task-queue side, or (c) CPU/ROM IRQ-delivery side
(firmware/ROM-internal). No speculative writes.

---

## 0. Preconditions (all mandatory before step 1)

| gate | how | pass criterion |
| --- | --- | --- |
| G1 recovery door armed | tools/recovery-card.sh | READY, human at the bench |
| G2 bench link | tools/bench-link.sh | PASS |
| G3 UART capture | tools/uart-capture.sh (read-only, absolute path) | alive; **no tty writes, no screen/minicom/picocom** |
| G4 image + module | sha256 of the flashed image per STATUS.md; modinfo | vermagic 6.18.60 SMP mod_unload ARMv7 p2v8 |
| G5 no vendor wifi modules | lsmod | hi5622v100_plat / hi5622v100_wifi absent |
| G6 firmware blob | sha256sum /lib/firmware/hi_wifi/FIRMWARE.bin | 928,920 B (local evidence only) |
| G7 DTS dma-ranges image | STATUS.md | RC inbound window 0x80000000..0x8fffffff; hcc_arm enforces it in code |
| G8 only this module | lsmod after step 1 | hi5622_pciet_hcc present; hi5622_pciet / _b3 absent; **no base D2H poll thread** |

## 1. Hard rules (restated - breaking one has cost a recovery cycle)

- **NEVER write CA 0x400392F0 (out[5])**. The module refuses it at build time
  and at runtime; the procedure only reads it.
- **NEVER write 0xCECE before the handshake page +8 reads 2.** The module
  refuses it by policy (hcc_cece_kick and the automatic path); the legacy
  announce attr is dead in this build. This is the GP8 bug.
- **UART is READ-ONLY.**
- **No device RAM writes** (this service never writes firmware RAM; the
  boot-kick g+0xB4 poke is a different, separately-approved experiment and is
  NOT part of this module).
- **Never read the RC misc window 0x10161000**; the module never does.
- **One idea per session**: the single H2D liveness frame and the single DR
  re-publish are each optional and each is one idea.
- Every device write in this run is one explicit sysfs store, logged to
  dmesg; capture dmesg and UART between steps.
- Do not rmmod after the release; power-cycle is the reset.
- Use endpoint 0000:00:00.0 for every attribute.

## 2. Stages

### S0 - load (no device writes)

    insmod hi5622_pciet_hcc.ko

Expected: two probe lines ("HCC service ready: 3 SR + 4 DR ring mirrors ... no
device writes"), no base poll thread, no oops. Record module sha256.

Gate: no abort/SError/hang; only this module.

### S1 - iATU windows + firmware upload + verify (unchanged transport steps)

    echo 1 > prepare      # 6x iatu vpN readback=OK; windows=1
    echo 1 > upload       # 928920 bytes, 2 chunks
    echo 1 > verify       # diffs=0 match=YES

Gate: 6/6 readback OK and diffs=0, else STOP before release.

### S2 - arm the ETE rings (P0-equivalent, vendor-exact)

    echo 1 > hcc_arm

Expected dmesg:
- one HCC ETE intr line: CA 0x40039508 RMW &= 0xFFE0F8F8, readback OK;
- SR0-2 lines: base in 0x80000000..0x8fffffff, depth-1=0x1f, prod=0,
  ctrl=0, readback OK; send word logged read-only;
- DR0-3 lines: base in window, depth-1=0x1f, pub(+0x38)=0x00000400,
  fill(+0x3c) read-only (reset class 0x10), enable=0x0, readback OK.

Checkpoints: cat hcc_rings (pub=0x400, fills as read); the firmware-owned
enable is still 0 here - pcie_msg_init sets it later.

Gates: 7/7 readback OK; any MISMATCH or out-of-window -> STOP (do not
release). While the CPU is still in reset, rmmod+reload is safe; prefer a
power-cycle and record the failure.

### S3 - start the service BEFORE the release

    echo 1 > hcc_loop

Rationale: the firmware D2H notify/flush path has a bounded out[1]==0 window
(flush CPU 0xC6108 spins for it); draining from before the release keeps that
window open (boot-kick C1e lesson) and is a no-write pass while out[1] is 0.
The loop also starts the boot/IRQ watch.

Expected: "HCC loop: service thread started (cadence 5 ms) ...". No device
writes are visible in dmesg until a D2H word appears.

### S4 - release (0x5a5a ONLY)

    echo 1 > release

Expected: "CPU release: 0x5a5a -> CA 0x40000108". From here rmmod is unsafe.

Gate: no abort/SError/hang. **Do not** write CECE; the module will refuse it
and count it if anyone tries.

### S5 - boot observation (0-1 s)

Wait ~1 s, then:

    cat hcc_status

Expected (GP8 golden signature):
- handshake page CA 0x1bf000 = 0x86807fef / 0x00000007 / 0x00000002 / 0x0000abcd
  (the trailing comment prints "+8==2:1");
- state CA 0x118b60 = 1, mask CA 0x40008 = 0;
- dmesg "HCC boot: handshake page ... state ... mask ..." and "HCC ready:
  starting vendor waits ..." once.

If +8 != 2: STOP the ready wait analysis; read 16-bit CA 0x4000010C
(BAR0 0x877008 is the page word; the gate is at CA 0x4000010C = BAR0
0x3b810c, read-only via devmem if needed) and record. The only host action
for a stuck S1 is the subtractive one (stop writing CECE / release-only);
never fake the +8 word.

### S6 - ready gate (2.0 s / 20.0 s)

While the loop runs, after ~3 s and again after ~25 s:

    cat hcc_ready
    cat hcc_events

Expected on a live ready path:
- g4_id1_seen=1 within 2.0 s (device_plat_ready, 12-byte frame);
- g4_id2_seen=1 within 20.0 s (DEVICE READY, 4-byte payload = device RAM
  address), state READY;
- hcc_events shows bit2 (0x4, startup/vector announce), bit6 (0x40, deposit
  notify) and/or bit3 (0x8, ETE done) counters; CECE writes only if the
  handler-spin evidence appeared, blocked_in_boot is 0 in a clean run;
- hcc_status: DR fills advanced from 0x10 class, g4 frames decoded (hcc RX),
  g+0xB4/+0xB8 (if shown) returned to 0 after the flush retired the notify.

Gate: id1 then id2 = ready path alive. id1 without id2 -> record and STOP
(no writes, no H2D).

### S7 - no-ready decision (read-only)

If the waits expired (hcc_ready prints TIMEOUT) take the discriminator:

    cat hcc_irq_probe
    cat hcc_status
    cat hcc_rings
    cat hcc_events

Decision table (REPORT.md section 7.4):

| hcc_irq_probe shows | meaning | action |
| --- | --- | --- |
| EOI != 0 | IRQ delivery ALIVE | the blocker is the mailbox/DR service; keep hcc_loop running and re-read hcc_ready/hcc_events (record fill trajectory) |
| EOI == 0 and scheduler timer CA 0x40160600/08/0c all 0 | scheduler never ran/unmasked: entry-A post-handshake task-queue path or the FIQ reset slot did not complete | STOP the host side; next lane firmware-side (entry A continuation / FIQ slot CPU 0x17D42C) or a vendor golden trace |
| EOI == 0, timer armed (0x61a800/7/1), raises latched (CA 0x40161188/0x40161288 = 0x5000) | events latched, CPU/ROM IRQ delivery dead | STOP the host side; next lane firmware/ROM IRQ entry decode or vendor golden trace |

Also record from hcc_status: out[0] pending, out[1], g+0xB4/+0xB8, ETE status
CA 0x4003a874, send words 0x4003ac00+4i, DR fills/pubs/enables. The
expected no-ready signature (GP9/GP10 class): out[0]=0x8 if an H2D probe was
sent, g+0xB4 = g+0xB8 = 0x40, ETE status 0, send words 0, fills at the arm
baseline, mask 0.

### S8 - optional: H2D consumer liveness (one frame, one idea)

Only if S7 did not already close the H2D lane, and only once per session:

    echo 1 > hcc_sr_announce

This posts the default safe frame (group 0, id 0, no payload, valid 12-byte
header with tag 0x5a5a and total 0x0c) on SR0 and announces mailbox id 3.

- Consumed: out[0] bit3 clears within ~100 ms, dmesg "HCC H2D: ... notify
  consumed"; the firmware H2D dispatcher (event 0x4C) is alive. Record
  out[2] (=8 on a real consume), read-only out[5] (=1 on a real consume) and
  SR0 consumer.
- Timeout: dmesg "announce NOT consumed (out[0] bit3 stuck)" and
  hcc_events timeout counter. The H2D lane is closed: STOP, do not retry, do
  not send payload frames (some firmware handlers dereference payload words).

Refusals are expected and counted if the module is not post-handshake or
out[0] is busy; they are not failures of the run.

### S9 - optional: S3-only ETE nudge (one write class, one idea)

Only when S7 shows DR fills frozen while the ETE status/send state is the
open question:

    echo 1 > hcc_dr_republish

Re-publishes each DR +0x38 with its current value (host-owned field; the
vendor sending trigger re-publishes it under lock). Re-take hcc_status /
hcc_rings after a few seconds. If fills still frozen and the send words remain
0, the blocker is not the host destination side - record and stop.

### S10 - capture and teardown

- capture dmesg and UART to absolute files under build/;
- save the read-only attrs: hcc_status hcc_ready hcc_irq_probe hcc_events
  hcc_rings hcc_plan and the base status/out1;
- power-cycle (never rmmod after the release);
- tools/bench-teardown.sh --apply; ledger entry with every gate outcome,
  image sha and module sha256.

## 3. Expected observables at a glance

| read | CA / BAR0 | expected on a healthy run | meaning when not |
| --- | --- | --- | --- |
| handshake page +0/+4/+8/+C | CA 0x1bf000 (BAR0 0x877000) | 0x86807fef / 7 / 2 / 0xabcd | boot protocol broken; do not judge the ready path |
| state | CA 0x118b60 (BAR0 0x7d0b60) | 1 | boot task did not run |
| exception mask | CA 0x40008 (BAR0 0x6f8008) | 0 | an exception entry occurred (record; CECE evidence) |
| out[1] bits | CA 0x40039014 | bit2 0x4 and/or bit6 0x40, bit3 0x8, drained | no events: delivery dead (S7) |
| g+0xB4 / g+0xB8 | CA 0x10c1a8 / 0x10c1ac | 0 after service | 0x40/0x40 = flush 0xC6108 never retired the notify |
| ETE status | CA 0x4003a874 | nonzero bits 16-20 when the engine pumps | all zero: engine never kicked (device-side) |
| ETE send words | CA 0x4003ac00+4i | nonzero packed indices when pumping | zero: engine never transferred |
| DR fill +0x3c | ETE block + 0x590/5e0/630/680 + 0x3c | advances from 0x10 class | frozen: no A->B copy |
| g4 ready | DR nodes | id1 then id2 decoded in hcc_ready | no frames: ready pump not reached |
| out[0] H2D pending | CA 0x40039010 | 0 (consumed) after S8 | bit3 stuck: H2D dispatcher dead |

## 4. Stop rules (hard)

1. Any external abort / SError / stall / UART panic -> stop, capture,
   power-cycle.
2. 6/6 iATU readback or verify diffs != 0 -> stop before release.
3. hcc_arm MISMATCH / out-of-window -> stop before release.
4. Any attempt to write CECE before page+8==2 -> it is refused; record and
   stop the session rather than working around it.
5. S7 classification (scheduler absent or delivery dead) -> stop the host
   side; the next lane is firmware-side. Do not add writes.
6. H2D announce timeout -> H2D lane closed; no retry, no payload frame.
7. DR republish: at most once per session; frozen fills afterwards -> stop.
8. Never write out[5], device RAM, H2D id 5, group-4 H2D; never read the RC
   misc window; never type to UART; never rmmod after release.

## 5. After the run

Success (ready pair) moves the project to the post-ready driver-replica
lane: the vendor's first H2D is the group-0 id-1 TXQ config sync (dynamic
payload; not modelled - do not synthesize it). Failure closes the host-side
HCC-service question and hands the blocker to the firmware/ROM lane with the
S7 snapshot as evidence.
