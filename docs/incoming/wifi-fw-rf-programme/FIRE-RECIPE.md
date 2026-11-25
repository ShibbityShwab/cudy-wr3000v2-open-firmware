# FIRE-RECIPE - the band-1 RF-programme probe and fix rungs

**Lane:** wifi-fw-rf-programme (task-182). This lane **never fired**. It
decoded the pinned local firmware image only. **The Lead fires**, with the
bench link up and the recovery door armed, per the project rules in
BENCH-FACTS / RUNBOOK. Read-only rung R0 carries no flash risk; R1-R3 are
host-side rungs on the existing bench module.

**The question this recipe answers:** all three 5 GHz RF gates read 1, the
0x0124 ctx store validated, the VAP object is nonzero, the page is golden -
so did the firmware's band-1 RF programme **run**, and if so, what did it
leave behind? See REPORT.md sections 7-8 and TABLES T7-T9.

---

## R0 - the read-only probe pack (do this first, no fire)

The box is already running the CW-0 AP (cudy-ap-test-5g40, ch40, 5200).
Deploy/run the existing committed read helper and read the TABLES T7 list.
bar0read takes **physical** addresses; refuse to run it on anything else.

    phys = 0x40000000 + (CPU + 0x6B8000)

Pass the numbers straight from TABLES T7 column "phys". The helper is
read-only by construction (O_RDONLY + PROT_READ) and refuses unaligned
addresses and the RC misc window 0x10160000..0x1016ffff. Do not write any
address in this rung.

Pass 1 (18 reads, all one-line, in T7 order), then the two pointer steps:

1. reads 1-2  -> ctx0 = u32(slot0) + u16(base); log it.
2. reads 3-5  -> the three gate bytes (expect 2, 1, 1).
3. reads 6-9  -> perlink P, band object, band-1 publish word, +0x1C8.
4. reads 10-12 -> O = *(P+0x2820), mode byte [P+0xF04], branch flag [P+0xF87].
5. read 13 -> R = *(O+0x2D8); then compute the addresses:
   - R+0x3A8, R+0x3AC, R+0x3B4, R+0x3CC, R+0x410, R+0x414 (reads 14-19)
   - descriptor decision: read the channel-table row
     0x117AD4 + 4*idx (idx = channel-table index of the current channel;
     ch36=0, ch40=1, ch100=8 ... ch196=28, TABLES T4). Its byte1 is the
     descriptor slot: 0xFF means the descriptor subsystem is inactive in this
     config, so R+0x410 bit0 = 0 is normal and must not be read as a fault.
     If the slot is not 0xFF, read the pool record 0x1ADC98 + 16*slot + 8.
6. Apply the TABLES T9 decision tree.

**Pass criteria:** the three gate bytes read 2/1/1 (if not, the CW-2
conclusion stands and this recipe stops). **Classification** is T9/C1-C5.

**Abort:** any read that faults or returns all-ones means the BAR0 window is
not where expected - stop, do not retry against a different base.

## R1 - zero-rebuild ordering test (the C1 discriminator)

Purpose: test whether the 0x0124 RF programme was skipped because the gates
were still 0 **when the frame was posted**.

* Same bench module, same ladder, same ch40 AP as CW-0.
* Condition: do **not** reload the vendor wifi module between the first and
  second AP start (a module reload reboots the firmware and resets the gate
  bytes). If the ladder unavoidably reloads it, skip R1 and go to R2.
* Action: start the 5 GHz AP ladder a second time in the same firmware
  session (VAP down/up or a second start_ap pass). On the second pass the gate
  bytes are already 1 in firmware RAM, so the 0x0124 worker reaches
  CA 0x40684.
* Read-back: repeat R0 pass 1-5 after the second start, plus the observer scan
  (iw scan freq 5200 / the committed observer script).
* **If the RF image appears (R+0x3AC = 0x03E6138A) and/or a beacon appears on
  the second start, C1 is confirmed.** The host fix is "post 0x0124 last, or
  re-post it after the five-frame order" - not a driver/firmware rewrite.

## R2 - small host patch (the C1 fix, if R1 is consistent)

* Patch: after the five-frame AP order completes, re-send the **0x0124**
  channel frame with the identical body {band, chip, channel, 0}; optionally
  gate the AP start on the R0 readback (R+0x3AC != 0 or a bounded retry).
* Rebuild is the existing beacon-lane module tree (same patches as
  fw-chan-worker's CW-1), 59-param debug-strip-safe build.
* Fire ch40 first (expect the ch36-64 image + [P+0xF87]=0), then optionally
  ch100 (expect the inline image + [P+0xF87]=1) to prove the variant split.
* **PASS** = R+0x3AC = 0x03E6138A, R+0x410 bit0 = 1, [P+0xF87] = 0, and the
  observer sees the 5 GHz SSID beacon.
* **FAIL, image present, probe 20 slot != 0xFF and bit0 = 0** -> C2: the
  descriptor predicate. Do not patch blind - capture TABLES T7 probes 18/20/21
  and the 0x0124 body byte 1 (field) from the ladder, then report back.
* **FAIL, image present, probe 20 slot == 0xFF** -> the descriptor subsystem is
  inactive by config; bit0 is not the blocker. Capture probes 12-19 and go to
  R3 (C4/C5).
* **FAIL, image absent** -> C1 is not the whole story: [P+0xF04] != 3 or the
  gates were 0 for a different reason; capture reads 10-11 and stop.

## R3 - per-band beacon policy (C4), only if R2 shows a complete RF programme

* Verify the plat module loaded the ini (dmesg lines from
  hi5622v100_plat / the wifi init path) and that
  /lib/firmware/hi_wifi/cfg_hi5622v100_hisi.ini on the box is the shipped
  file with beacon_5g_tx_policy=2.
* Re-fire the same ladder with the ini guaranteed in place and unchanged.
* If the image and bit0 are correct and beacons still do not appear, this is
  C5 (the band-1 TBTT arm): capture the TABLES T8 differential band-object
  dump, VAP+0x13A and VAP+0x58, and report back. Do not start a firmware
  rewrite from this lane's decode.

## Hard rules for every rung

* Never write 0x400392f0; never read 0x10160000..0x1016ffff.
* Never rmmod the vendor modules.
* Page 0x86807fef / state 1 / mask 0 exception -> power-cycle, no retry.
* UART is read-only; no host->router serial input, ever.
* No vendor binary, firmware byte, MAC or secret goes into a committed file:
  the probe output stays in build/ (gitignored), the docs carry addresses and
  expected values only.
