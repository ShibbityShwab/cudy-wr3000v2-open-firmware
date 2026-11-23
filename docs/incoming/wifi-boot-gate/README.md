# wifi-boot-gate - boot-task post-send chain and the 0x48820 gate (image-only lane)

Host-only reversal of the boot task 0x47628 post-send chain up to the per-link bind
(0x4881E / 0x48820 / 0x48840) and of the exact condition that decides whether the bind runs.
Answers, in REPORT.md:

* the chain 0x48160 -> 0x47D28 -> 0x49188 -> 0x49204 -> 0x48336 -> 0x4845C.. -> 0x4881E,
  with the exact arguments/objects at each step;
* the gate: 0x48820 is a structurally-passing singleton assertion (0x9438C on
  byte[CA 0x170E29]); the data-dependent decision is the next lookup 0x4882E =
  0xFE578(0,0x12) = word[CA 0x1714A0] + u16[CA 0x171504], written only by the boot task's
  own 0x49204 init chain (0x4923A / 0x4924C).  It does NOT depend on the event layer
  (CA 0x4016010C / 0x17D430 / 0xC2EFC), the D2H/ETE path or a hardware event;
* the bind value CA 0x169E98 has no writer in the image -> the 0x48840 bind stores NULL even
  on a completed pass; the real per-link context family is 0xB1EE4's CA 0x174BF0/0x175A18,
  published at CA 0x15CA08+0x1A00 and bound by the 0x44BBC family;
* the exact read-only formula to inspect the gate state on a parked chip (BAR0 = CPU +
  0x6B8000), with expected open/closed values and a three-way decision matrix;
* conclusion: no host write/H2D/DMA/timing can satisfy the gate; the one remaining bench
  action is the read-only pack.

Files:

* REPORT.md - the lane report (evidence markers V / V* / I / U, writer census, gate formula,
  decision matrix, reproduction commands).
* tools/bootgate.py - recursive-descent Thumb CFG/literal analyzer for the local firmware
  blob: dis, func, block, trace, callers, xref, writers, calls, bytes, bar.  Reads the blob
  in place; never copies or commits it.
* tools/gate_reads.py - the READ-ONLY paused-chip read plan (addresses, expected values,
  decision matrix).  It never opens a device.

Quick start:

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=$PWD/tools
    $PY $T/bootgate.py dis 0x48818 0x40            # gate A / gate B / bind
    $PY $T/bootgate.py dis 0x49204 0x70            # gate-state writer chain
    $PY $T/bootgate.py dis 0xFE578 0x44            # the lookup 0x961AE tail-calls
    $PY $T/bootgate.py callers 0x102760            # message-layer send census
    $PY $T/gate_reads.py                           # the one next bench action (read-only)

Constraints: no device contact, no builds, no vendor binaries / firmware bytes / secrets in
this directory.  The firmware blob stays outside the repository
(/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin) and is only read in
place.
