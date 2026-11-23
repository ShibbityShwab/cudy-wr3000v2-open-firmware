# wifi-fw-link-event - firmware-internal link/event bring-up (image-only lane)

Host-only reconstruction of the Hi5622V100 firmware's firmware-internal link/event
bootstrap: the consumer of the boot-region data at CPU 0x474B8, the device event pump
0x46848, the boot task's per-link +0x4B0 creation chain, the five event-thread post/wake
sites, and the ROM-driven IRQ trampoline -> event dispatcher 0xC2EFC chain that explains why
the boot never dispatches its registered link/event services.

Read REPORT.md first (sections 0, 6, 7, 8 and 9 are the answer; sections 10-11 are the
evidence table and what to check first).

Files:

* REPORT.md - the lane report (evidence markers V / V* / I / U).
* tools/fw_cfg.py - recursive-descent Thumb CFG/literal analyzer for the local firmware blob:
  dump, func, reach, back, lits, calls, findlit, movimm, **pool** (literal-target consumer
  census) and **regs** (event-handler registration census). Reads the blob in place; never
  copies or commits it.
* tools/live_reads.py - READ-ONLY bench read plan for the event/link layer (addresses and
  expected values only; it never opens a device).

Quick start:

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=$PWD/tools
    $PY $T/fw_cfg.py pool 0x474B8 0x47528     # 30 literal loads, all inside entry A
    $PY $T/fw_cfg.py regs                      # event id -> handler -> registrar census
    $PY $T/fw_cfg.py dump 0xC2EFC 0xA0         # event dispatcher (index = CA 0x4016010C)
    $PY $T/fw_cfg.py --lo 0x47600 --hi 0x49E00 reach 0x49188 0x48820 --compact
    $PY $T/live_reads.py                       # the one next bench action (read-only)

Constraints: no device contact, no builds, no vendor binaries / firmware bytes / secrets in
this directory. The firmware blob stays outside the repository
(/home/shibbityshwab/recovery-extract/lib/firmware/hi_wifi/FIRMWARE.bin) and is only read in
place.

Top-level answers:

1. CPU 0x474B8 is entry A's literal pool, not a dispatch table; its only consumer is entry A
   (0x471C0..0x4744A), which registers CPU 0x46848 as hardware-event handler 0x3E/0x3F and
   wires the boot-global event pump callbacks.
2. CPU 0x46848 is the device-event pump, not a link-create worker; the per-link +0x4B0 bind
   (0x48840) lives in the boot task 0x47628's post-send init chain.
3. The five "event-thread loop entries" are bl 0xC5128 post/wake call sites downstream of
   the event layer.
4. The image's event dispatch entry is the ARM IRQ/FIQ trampoline at CPU 0x40064 (calls
   0xC2EFC; its only caller is the ROM). Every firmware-internal link/event service is a
   handler in CA 0x17D430. Our boot has the table populated but has never dispatched an
   event (H2D out[0] never acked = event 0x4C never dispatched), so the missing input is a
   delivered hardware event - definitively firmware/ROM-internal, with one read-only bench
   verification pack left (REPORT sections 8-9).
