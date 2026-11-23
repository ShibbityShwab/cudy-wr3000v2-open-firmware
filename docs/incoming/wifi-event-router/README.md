# wifi-event-router - device event controller registry map and the delivery gap (image + vendor-module lane)

Host-only lane "event-router", shared task task-78. It reverse-engineers the Hi5622V100
device event controller / router that the firmware's link/event layer hangs off, from the
vendor firmware image and the vendor kernel modules (read in place; **never copied**), and it
defines the single next bench action.

Read REPORT.md first. Sections 0, 2, 6, 7 and 8 are the answer (TL;DR, register map, why
nothing is delivered, the exact next bench action, evidence table).

Files:

* `REPORT.md` - the lane report. Evidence markers V (verified here), V* (device-measured by a
  prior attended lane), I (inference), U (unknown / needs a device read or ROM dump).
* `tools/devreg.py` - device-register access census for the firmware image. Resolves
  PC-relative literals and movw/movt pairs, follows a bounded straight-line window and reports
  every load/store whose effective address is a device CA; also annotated disassembly with
  resolved literal values (Thumb/ARM), targeted `access`/`defs` queries and call-site census.
* `tools/ko_devca.py` - the same census for the vendor ARM kernel modules (pyelftools +
  capstone, mapping-symbol aware, symbol attribution, .data/.rodata word scan).
* `tools/evctrl_probe.py` - prints the next bench action (BAR0 addresses, expected values,
  observables). It never opens a device.

Quick start:

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=$PWD/tools
    $PY $T/devreg.py dis 0xC2EFC 0xA0       # event dispatcher (CA 0x4016010C -> 0x17D430 -> 0x40160110)
    $PY $T/devreg.py dis 0xC3024 0xA0       # event-controller global init (0x40160100/04/08, banks)
    $PY $T/devreg.py dis 0xC6DB8 0x60       # software raise -> pending banks 0x40161180/0x40161280
    $PY $T/devreg.py dis 0xC704C 0x110      # scheduler: timer 0x40160600 + enable 0x1D + idle cpsie
    $PY $T/devreg.py dis 0x473CC 0x90       # entry A inter-core handshake that gates the scheduler
    $PY $T/ko_devca.py scan --ko /home/shibbityshwab/recovery-extract/lib/hisilicon/ko/hi5622v100_plat.ko
    $PY $T/evctrl_probe.py                  # the next bench action (addresses only)

Constraints: host-only; no device contact in this lane; no builds; no vendor binaries,
firmware bytes or secrets copied into this directory. All artifacts are derived annotations;
the firmware blob and the modules stay at their original local paths.

Top-level answers:

1. **Controller map.** CA 0x40160100 = global enable (=1, writer 0xC3024); 0x40160104 = max
   priority byte (=0xFF); 0x40160108 = config (=3); **0x4016010C = current event vector**
   (low10 = event id, reader dispatcher 0xC2EFC); **0x40160110 = ack/EOI** (dispatcher writes
   the status word back). Per event id 0x10..0x5F: enable 0x40161100+4*(id>>5) (W1S, 0xC6FF4),
   software pending 0x40161180+4*(id>>5) and mirror 0x40161280+4*(id>>5) (W1S, 0xC6DB8),
   priority byte 0x40161400+4*(id>>2), target byte 0x40161800+4*(id>>2) (table CPU 0x105F8C).
   Timer block 0x40160600/04/08/0C raises event 0x1D (handler 0xC6C88); radio pump block
   0x40036000+0x40*n with status 0x40036800/0x40036900 serves handlers 0x3E/0x3F; a second
   controller 0x4018010C/0x40180110 with dispatcher 0x120A0C belongs to CPU1: the image tail at
   CPU 0x120A6C is a complete secondary-core bootstrap (CP15 MMU off->on, its own handler table
   at CPU 0x107B8, its own glue block at CA 0x400330xx) - so the two controllers are per-core.
   The 0x40105000 block is SoC glue, not event routing. The firmware's ETE control write
   is CA 0x4003A86C; the host-owned ETE interrupt word is CA 0x40039508.
2. **First event.** The firmware raises its own events 0x4C and 0x4E from pcie_msg_init
   (CPU 0x49756/0x4975A) before registering/enabling them; they are latched pending during
   init. The first in-image hardware source is the timer event 0x1D, armed only by the
   scheduler 0xC704C. The host-triggerable source is the mailbox announce (out[0] bitmap +
   out[2] bit0 doorbell).
3. **Why none is delivered here.** Events are raised, but the delivery path is never entered:
   the image's only dispatch entry is the ROM-vectored ARM trampoline 0x40064 -> 0xC2EFC, and
   the image only executes `cpsie i` from the dispatcher, deferred path or scheduler idle.
   Entry A is gated before the scheduler by an infinite poll on CPU 0x1BF008 == 2 (inter-core/
   ROM handshake at 0x473CC-0x47618), so in the stalled state the boot task ran from the
   ROM-armed reset slot, the timer was never armed and no context ever unmasked IRQ. The
   handshake partner is most plausibly CPU1 (image tail 0x120A6C); CPU1 never starting would
   keep both the CPU0 scheduler and the CPU1 controller dead.
4. **Host action.** The vendor host driver has no event-controller register access (both
   modules have no 0x4016/0x4018 CA constants); the intended host-to-device event (mailbox
   announce) is measured dead. The one decisive bench action is to replay the firmware's own
   raise for event 0x4C by writing 0x1000 to CA 0x40161188 and 0x40161288 (BAR0 0x519188 /
   0x519288) after a read-only fingerprint (REPORT section 7): success proves the delivery
   path, a latched-but-undispatched bit proves the CPU/ROM delivery gap.
