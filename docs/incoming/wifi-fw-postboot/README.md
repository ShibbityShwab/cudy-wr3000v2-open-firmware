# wifi-fw-postboot - firmware post-handshake/startup continuation and the scheduler "fingerprint" audit (image lane)

Host-only lane "fw-postboot", shared task **task-88**, 2026-11-23. It statically reverses what
the Hi5622V100 vendor Wi-Fi firmware does after the inter-core boot handshake completes, and it
audits the instrument that was used to conclude that "the image scheduler never arms". Inputs
are read in place and **never copied**: the vendor `FIRMWARE.bin` and the local mask-ROM dump.

Read REPORT.md first. Section 0 is the one-page answer; sections 2-3 are the decisive evidence
(instrument mapping bug + static proof the GIC was enabled), section 4 is the startup state
machine map, sections 5-6 are the event-controller and FIQ/wake path, section 7 is the
corrected conclusion and the one useful next host action.

Headline: the "scheduler fingerprint absent" readings are not device reads. The hcc service
reads CA 0x4016xxxx through `HI5622_BAR0_IO(ca) = 0x3B8000 + (ca & 0xFFFFF)`; the GIC block
is past the end of inbound region 3 (CA 0x40000000..0x4011FFFF), so the macro wraps every such
CA by -0x100000 and the words read are CA 0x4006xxxx instead (13/13 audited CAs, see
`tools/regmap_probe.py`). Meanwhile the measured golden handshake page (+4 bit2, +8=2) proves
ROM S1 executed its GIC enable (ROM 0x5C8 writes CA 0x40160100 = 1 before the WFI), and entry
A's post-spin path has no gate before it arms the scheduler timer at 0xC7072-0xC708C, with the
exception mask CA 0x40008 = 0 proving the two BUG gates did not fire. So the scheduler was
armed; the remaining gap is the boot state machine's pass 1 -> ready-pair progression and the
D2H deposit/notify decode, not the IRQ layer.

Files:

* `REPORT.md` - the lane report. Evidence markers V (verified here), V* (device-measured by a
  prior attended lane), I (inference), U (unknown / needs a device read).
* `tools/fwdis.py` - capstone-based image disassembly helper used for every address in the
  report: Thumb/ARM windows with resolved PC-literals, whole-image u32 search, "code sites that
  load a literal equal to X" (`lrefs`), literal-pool dump, call targets.
* `tools/regmap_probe.py` - the instrument audit: computes `HI5622_BAR0_IO(ca)`, finds the
  real inbound region, decodes the real target CA and flags every hcc IRQ-probe CA that does
  not decode to itself. Prints only addresses/constants; never opens a device.
* `regen.sh` - the exact commands that reproduce every claim.

Quick start:

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=$PWD/tools
    $PY $T/regmap_probe.py                 # the headline: 13/13 probe CAs decode elsewhere
    $PY $T/fwdis.py dis 0x473CC 0xE0       # entry A post-handshake continuation
    $PY $T/fwdis.py dis 0xC745C 0x60       # task create (boot task)
    $PY $T/fwdis.py dis 0xC704C 0x180      # scheduler start: timer arm + event 0x1D + idle
    $PY $T/fwdis.py dis 0xC3024 0xA0       # event-controller init (0x40160100=1)
    $PY $T/fwdis.py dis 0xC2EEC 0x20       # FIQ dispatcher -> the 0x17D42C slot
    $PY $T/fwdis.py lrefs 0x0017D42C       # who references the FIQ slot
    $PY $T/fwdis.py dis 0x47CE0 0x60       # boot task: one-shot slot stamps + state check
    $PY $T/fwdis.py dis 0x48140 0x160      # setup branch (state 0 -> 1)

Constraints: host-only (no device contact in this lane); no builds; no vendor binaries,
firmware/ROM bytes or secrets copied into this directory. All artifacts are derived
annotations; the firmware blob and the ROM dump stay at their original local paths.

Top-level answers:

1. **Why the scheduler "never arms": it is an instrument artifact.** The hcc IRQ probe reads
   the wrong CA for all 0x4016xxxx words; the GIC is not host-visible through the six vendor
   inbound regions. Correct readout needs a 7th iATU viewport to target CA 0x40160000
   (read-only diagnostic), not `HI5622_BAR0_IO`.
2. **What the boot state machine does after the +8=2 spin:** page bit14, bounded memset,
   descriptor {name 0x104243, fn 0x47625, kind 7}, task create 0xC745C, scheduler start
   0xC704C; the scheduler arms timer CA 0x40160600=0x0061A800 / 0x40160604=0x0061A800 /
   0x40160608=7 / 0x4016060C=1, enables event 0x1D (CA 0x40161100 bit13), then dispatches the
   boot task or idles with `cpsie i` (0xC2B7A).
3. **The boot-slot/FIQ slot CA 0x17D42C** is the FIQ dispatcher's one-shot entry hook
   (ROM FIQ -> 0x7C -> header+0x50 = 0x103140 -> 0xC2EEC -> bx [0x17D398+0x94]). Its value
   0x000C19D1 is a BUG trap; the only writer is the boot task's one-shot disarm at 0x47D08.
   It is not the IRQ/FIQ unmask or scheduler kick path; FIQ is never unmasked by the image
   (six `cpsie i`, zero `cpsie f`).
4. **Event-controller init:** 0xC3024 (called unconditionally by entry A 0x46EDA and entry B
   0xC7440) writes CA 0x40160100 = 1, 0x40160104 = 0xFF, 0x40160108 = 3 and priority ids
   0/1/2/0x1D; ROM S1 additionally writes CA 0x40160104/0x40160100 before the WFI.
5. **state = 1** is the boot state machine's pass 1: the setup branch advanced 0 -> 1 at
   0x48174, created the per-state worker and registered the H2D id-1/event-0x46 handlers, then
   built and sent g4 id-1 through 0xC2760. The branch does not wait for host input; no static
   branch returns to the state check. The measured notify bit6 shows the D2H deposit path ran;
   the missing transition is the host DR/HCC receive decode and/or the id-2 phase gate, i.e.
   downstream of the scheduler.
6. **No host action can supply the scheduler arm.** The only useful host action is the
   read-only 7th-viewport GIC readout (REPORT section 7.2) to close the timing question; then
   the lane is the DR/HCC receive contract, not more firmware pokes.

Corrects prior-lane claims: the `hcc_irq_probe` fingerprint, `evctrl_probe.py`'s IO
addresses (and event-router REPORT section 7's BAR0 0x519188 write plan), the "scheduler never
armed" conclusion, and the "ROM-armed reset/FIQ slot 0x17D42C = 0x47629" guess.
