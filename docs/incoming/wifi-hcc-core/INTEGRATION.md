# INTEGRATION: wiring hcc_core into the hi5622_pciet_hcc module

**Audience:** the lane that merges the protocol core into the in-tree Wi-Fi hcc
service module (opensource/docs/incoming/wifi-hcc-service + the d2h-direct
extension).  Host-only; no device contact in this document.

**Target module:** hi5622_pciet_hcc (driver name hi5622_pciet_hcc; the current
service sources are wifi-hcc-service/src/, extended by
wifi-d2h-direct/src/hcc-d2h-direct.patch).  The transport (probe, iATU
prepare, firmware upload/release, ETE arm) and the direct-read ring walk are
NOT replaced by this lane - only the protocol state fragments are.

The core is portable C99 with no kernel dependency (src/hcc_core.h/.c).  The
kernel-only glue lives in src/hcc_core_kernel.h/.c (readl/writel ops, jiffies
clock, dev_info event sink); it is compile-verified against the 6.18.60 tree.

---

## 1. What to add, and where

1. Copy src/hcc_core.c, src/hcc_core.h, src/hcc_core_kernel.c and
   src/hcc_core_kernel.h into the module tree; add hcc_core.o and
   hcc_core_kernel.o to the module object list (Kbuild), e.g.

       hi5622_pciet_hcc-y += pciet_main.o pciet_ete.o pciet_hcc_service.o \
                             pciet_d2h_direct.o hcc_core.o hcc_core_kernel.o

2. Embed the adapter (and the core inside it) in the service state:

       struct hi5622_hcc_service {
           ...
           struct hcc_kernel hcc;      /* core + IO ops + dev + log limit */
       };

   Initialise in pciet_hcc_probe() after the windows are programmed, or at
   probe time with the BAR0 mapping (the core performs no device access until
   hcc_core_note_release):

       struct hcc_core_config cfg;
       hcc_core_config_default(&cfg);
       cfg.pm_enabled = false;            /* vendor PM is not on the boot path */
       cfg.heartbeat_armed = false;       /* vendor boot never arms it */
       cfg.allow_ceed_repair = false;     /* GP8 needs no repair */
       cfg.mbox_clear_timeout_ms = 1000;  /* polled out[0] wait */
       hcc_kernel_init(&h->hcc, dev, ctx->bar0, &cfg);

   Note: struct hcc_core is about 1.1 KiB; keep it in the service state
   (kmalloc'd or part of the ctx), not on the stack (the kernel's
   frame-larger-than check will catch a stack copy).

3. Right after the transport performs the release write (0x5a5a to
   CA 0x40000108), call

       hcc_kernel_release(&h->hcc);

   The core never writes the release word itself.

4. In the service loop thread (hcc_loop) replace the mailbox/CECE/watch body
   with one tick per pass:

       hcc_kernel_tick(&h->hcc);          /* boot latch + exception watch +
                                             D2H mailbox + ready deadlines +
                                             H2D mailbox + heartbeat + PM +
                                             evidence-driven CECE */

   Keep the direct-read poll and the DR-fill watchdog in the same loop; they
   are the receive path, not the protocol core.

5. In pciet_d2h_poll() deliver every decoded device->host frame to the core,
   from the stored snapshot form (hdr[12] + payload snapshot + wire length):

       hcc_core_deliver_hdr(&h->hcc.core, HCC_SRC_DIRECT,
                            frame->hdr, frame->payload,
                            min(frame->payload_len,
                                HI5622_D2H_PAYLOAD_SNAP),
                            frame->total, hcc_kernel_now_ms());

   For the ready pair this is sufficient: g4 id2 carries 4 payload bytes.  If
   the legacy engine DR walk ever decodes a frame, deliver it the same way
   with HCC_SRC_ENGINE; the first valid transition wins and duplicates are
   counted.

6. Rewrite hcc_sr_announce to use the core (the sysfs grammar can stay):

       rc = hcc_core_frame_build(&h->hcc.core, sr_buf, sr_buf_len,
                                 group, id, dst, payload, plen, &total);
       if (rc) return rc;                      /* refusals come from the core */
       /* transport: copy sr_buf into the SR DMA buffer, get its dev VA */
       rc = hcc_core_sr_commit(&h->hcc.core, 0, devva, group, id, total,
                               hcc_kernel_now_ms());

   hcc_core_sr_commit encodes the node word1, advances the packed producer
   mirror and issues the coalesced mailbox id-3 announce via out[0]/out[2].
   The old manual out[0] write, out[2] RMW and out[0] drain poll are deleted.

7. For the first post-ready H2D (vendor group-0 id-1 TXQ table sync) use
   hcc_core_txq_sync_build() with the payload the driver-replica lane supplies;
   the READY_TXQ_SYNC -> ACTIVE transition happens automatically when that
   frame is committed.  Do NOT synthesise this payload.

8. In hcc_ready_show()/hcc_status_show() render hcc_core_status(); keep the
   existing sysfs names.  Recommended additional read-only attributes:
   hcc_phase (phase + fail reason) and the core counters the status line
   already contains.

9. Remove or neutralize the replaced fragments:
   hcc_boot_complete() live-page read, hcc_g4_id1()/hcc_g4_id2() ready struct,
   hcc_cece_policy(), the manual mailbox part of hcc_service_pass(), and
   hcc_build_sr_frame's hand-made header (keep only the sysfs grammar
   parsing).  hcc_irq_watch()/hcc_irq_probe can stay as a separate
   diagnostic, but do not encode the retracted "scheduler never armed"
   conclusion (fw-postboot correction).

---

## 2. Replacement map (current module -> core)

| current symbol (file) | replace with | notes |
| --- | --- | --- |
| hcc_boot_complete() (pciet_hcc_service.c) | hcc_core_boot_check() inside hcc_core_tick() | LATCHED gate (F1); golden flag + page_lost observability |
| hcc_service_pass() mailbox block | hcc_core_d2h_poll() via hcc_core_tick() | vendor order + dispatch table + bitmap coalescing |
| hcc_cece_policy() | hcc_core_cece_service() via tick | post-latch evidence classes, one write per event (F3) |
| hcc_g4_id1() / hcc_g4_id2() / enum hcc_ready_state | hcc_core_deliver_hdr() | both receive sources; vendor validation/caps |
| hcc_build_sr_frame() header part | hcc_core_frame_build() / txq_sync_build() | tag/length always correct; id5/group4 refusals |
| hcc_sr_announce_store() announce part | hcc_core_sr_commit() | node word1 + producer + coalesced id-3 |
| pciet_msg_send_irq / pciet_msg_wait_for_clr (pciet_main.c) | hcc_core_mbox_send()/mbox_tick() | pending/in-flight; out[0] wait; doorbell RMW (F5) |
| hcc_watch_dr() fill watchdog | unchanged (d2h direct) | receive path, not protocol |
| hcc_irq_watch()/hcc_irq_probe | unchanged diagnostic only | see fw-postboot correction |
| hcc_ready_show()/hcc_status_show() ready section | hcc_core_status() | keep sysfs attribute names |
| hcc_boot_watch() mask/state reads | hcc_core_exception_watch() + status | mask change is CECE evidence |

---

## 3. The code-level fix list (apply with the merge)

**F1 - Latch the boot handshake.**  The current hcc_boot_complete() re-reads
CA 0x1bf000 +8 on every use.  The PD patched boot overwrites that page minutes
after S1 (0x0010C190 / 0 / 0 / 0x1BEE50 observed), so the live check returns
false and every later announce is refused.  Use the latched gate; keep
page_lost_after_latch as an observability counter.

**F2 - Feed the direct-read catalogue into the ready state machine.**  The
ready pair arrives on the device group-A ring and is decoded by
pciet_d2h_poll(); deliver every decoded frame to hcc_core_deliver_hdr() with
HCC_SRC_DIRECT.  The engine-copied host group-B path is idle in this boot and
must not be the only source.

**F3 - CECE policy.**  Never before the latch.  After the latch only on
evidence: out[1] bit2 (startup/vector announce), a registered D2H handler
other than the pure id6 deposit notify, or a change (first nonzero) of the
exception-seen mask CA 0x40008.  One 32-bit 0x0000CECE write per event.  Keep
the manual kick bounded (<= 3/load, mode 2 only) and keep mode 1 (no writes)
for negative-control sessions.

**F4 - Fix the device-CA BAR0 mapping.**  The inherited macro

    #define HI5622_BAR0_IO(ca) (0x3b8000 + ((ca) & 0xfffff))

is only correct for CAs below 0x40100000.  Region 3 covers CA
0x40000000..0x4011FFFF, so the correct mapping is

    BAR0 = 0x3b8000 + (CA - 0x40000000)          [21-bit region offset]

which puts out[3] CA 0x40101438 at BAR0 +0x4b9438 and out[4] CA 0x40101414 at
+0x4b9414 (the current macro produces +0x3b9438/+0x3b9414 - the wrong
registers).  Either use hcc_bar0_io() from the core or replace the macro with
the region formula plus a range check and a BUILD_BUG_ON for
HI5622_CA_NEVER_WRITE.  The out[1] drain CA 0x40039014 maps correctly under
both formulas, which is why the D2H service still appeared to work.

**F5 - H2D coalescing/announce.**  pcie_msg_send semantics: pending |= BIT(id);
if in-flight == 0 then in-flight = pending, pending = 0, out[0] = in-flight,
out[2] |= 1.  The device clears out[0] on consumption; poll out[0] (bounded),
complete the mask and publish any coalesced pending batch.  Never wait on
out[2] (the host's own doorbell), never write out[5], never send id 5.

**F6 - SR commit + TXQ sync.**  Node word1 = (total << 16) | 0x6d2b, packed
producer (low 10 bits + 0x400 phase, depth 32), then one id-3 announce per
coalesced batch.  The first post-ready H2D is group-0 id-1; its payload is
dynamic and must come from the driver-replica work - never synthesise it.

**F7 - Write allowlist.**  Only out[0] 0x40039010, out[1] 0x40039014, out[2]
0x400392d4, out[3] 0x40101438, out[4] 0x40101414 and the gate 0x4000010c are
writable.  The release word 0x40000108 is the transport's and out[5]
0x400392f0 is forbidden.  Keep a refused-write counter and surface it.

**F8 - Deadlines + failure semantics.**  Boot 5 s, plat-ready 2 s, device-ready
20 s (from the latch), PM sleep-ack 500 ms, heartbeat 10 s.  On a ready
timeout enter FAILED but keep servicing the D2H mailbox (vendor SPEC S9);
refuse H2D after failure.

**F9 - Frame validation.**  Tag 0x5a5a, total == node/wire length, total in
[12, 0x680]; H2D groups 0/2 only; group 4 is device->host only; g4 id1 total
must be 12 with chip id 0; g4 id2 total 13..111.  The firmware's own receive
check compares tag and length, so an invalid frame is silently dropped.

---

## 4. Bench wiring (condensed; the attended procedure stays the Lead's)

    S0  recovery door armed, bench link PASS, UART read-only, image+module
        hashes recorded
    S1  insmod hi5622_pciet_hcc.ko (core initialised, PM/heartbeat off)
    S2  prepare -> upload -> verify (diffs=0)
    S3  hcc_arm (rings armed; no CECE writes)
    S4  hcc_loop start, then release 0x5A5A only; core note_release
    S5  watch: boot latch (page +8==2), state CA 0x118b60=1, mask 0
    S6  ready pair: g4 id1 <= 2.0 s then g4 id2 <= 20.0 s via the DIRECT
        catalogue; verify id2 payload dword (0x01060850 on the current boot)
    S7  TXQ sync: only with a reconstructed payload; expect READY_TXQ_SYNC ->
        ACTIVE and the first coalesced id-3 announce if an SR frame is posted
    S8  capture hcc_status (core line) + dmesg; power-cycle instead of rmmod
        after release

Expected core status line on a healthy boot (fields abbreviated):

    phase=WAIT_PLAT_READY ... boot=latched hs=86807fef/7/2/abcd golden=1
    page_lost=0 h2d[... closed=0] ... bad=0

Then after the ready pair:

    phase=ACTIVE ready[id1=1 id2=1 map=1 id2plen=4] txq=1 ... bad=0

Stop rules (unchanged): no out[5] write, no device RAM write, no H2D id 5,
no CECE before page +8==2, one idea per session, capture-and-stop on abort/
SError/hang, no retries after an H2D announce timeout.

---

## 5. What NOT to replace

- the transport skeleton: probe, iATU prepare, firmware upload/verify, release,
  and the BAR0 mapping itself (only F4's CA formula changes);
- the ETE arm sequence (hcc_arm: intr RMW first, SR/DR bfd-faithful init);
- the direct-read service internals (device group-A node walk, window bounds,
  producer shadow authority, ring-space answer): the core consumes its
  decoded frames, it does not read rings itself;
- the sysfs attribute names used by the bench procedure; only their content
  moves to hcc_core_status();
- pciet_hcc_probe/remove life-cycle structure (just add the core init/status).

---

## 6. Compile + merge verification (host-only)

    cd opensource/docs/incoming/wifi-hcc-core
    ./tools/build.sh                 # host 528/528 PASS + 6.18.60 module build
    # then, in the integrated module tree:
    make -C KDIR M=... modules       # zero warnings under the tree's CONFIG_WERROR

The core files are self-contained; the only integration-specific code is the
event sink and the two IO callbacks in src/hcc_core_kernel.c (about 70 lines).
If the module prefers its own logging, replace hcc_kernel_event() with a
dev_info wrapper and keep the rest.
