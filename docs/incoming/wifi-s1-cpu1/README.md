# wifi-s1-cpu1 - who writes handshake +8=2, and the CPU0->CPU1 TCM handoff gate

Host-only lane (**task-85**, 2026-11-23). No device contact, no builds, no vendor
binary/firmware bytes/calibration/secrets committed. Read REPORT.md for the analysis;
this file is the short index.

## Headline results

* **The +8=2 writer is mask-ROM S1, not the image.** The only image store to the
  CA 0x1BF000 page is entry A's strd {0,0xABCD}; the only literal homes of CPU
  0x1BF008 are 0x47510/0x47620; the secondary-core image has no page/doorbell
  reference. The ROM keeps FIQ (vector target 0x7C) and its slot table points into
  the page area, so it owns the reply.
* **New deterministic gate found:** FIRMWARE.bin is a two-image container (CPU0 image
  plus secondary-core ITCM+DTCM images tiling EOF exactly). Entry A itself copies the
  secondary-core ITCM/DTCM into TCM windows CA 0x400000 / 0x410000, using four bounds
  read from the platform-custom block at CPU 0x1B2800. The vendor host writes that
  block (its four section bounds are exactly the device-INI firmware_itcm_*/dtcm_*
  values); our transport never does -> the copies fail -> the secondary core never
  runs -> no +8=2. With an invalid block entry A spins exactly like the GP2 signature.
* **GP1 flakiness mechanism:** GP1 runs the chip ROM twice (early hog release + in-probe
  pulse); the warm second run can skip the image entirely (pre-S1 fallback page with no
  entry-A bits). The early GP1 successes are explained by a stale-but-valid platform
  custom block left in chip WRAM by the previous stock-vendor boots; once consumed,
  every later boot (including GP2) loses S1. GP2 (single clean reset) is the right base.
* **GP3 host action:** keep the GP2 kernel and, before release, (A) write the four
  section bounds to the platform-custom block (BAR0 0x86A804/08/0C/10) and/or
  (B) preload the TCM windows from the local FIRMWARE.bin (BAR0 0x1C0000 / 0x1D0000).
  Expected: handshake 0x86807FEF / 0x3 / 0x00000002 / 0x0000ABCD, TCM word0 0xE59FF018
  and 0x40034000, state 1, mask 0, boot task + scheduler run. Full plan, decision
  matrix and closing-evidence list in REPORT.md section 4 / 6.

## What is here

| file | purpose |
| --- | --- |
| REPORT.md | the S1 identity answer, the TCM handoff gate, GP1/GP2 mechanism, GP3 bench plan, evidence table |
| tools/s1_cpu1.py | self-checking analyzer (selftest 46/46): handshake-page writer census, custom-block/TCM handoff decode, ITCM/DTCM blob layout from the device INI, secondary-core image/ROM ABI, doorbell census, GP3 plan printer |

## Reproduce

    PY=/home/shibbityshwab/Documents/GitHub/cudy-wr3000v2/.provision-venv/bin/python
    T=opensource/docs/incoming/wifi-s1-cpu1/tools/s1_cpu1.py
    $PY $T selftest     # 46/46 PASS
    $PY $T handshake    # page writers + observed GP1/GP2/vendor signatures
    $PY $T tcm          # custom-block contract, ITCM/DTCM blobs, host windows
    $PY $T cpu1         # secondary-core image + ROM ABI
    $PY $T rom          # ROM vectors/slots, doorbell census, writer argument
    $PY $T gp3          # exact GP3 addresses/values/observables

The tool reads HI5622_FW (firmware) and HI5622_INI (device INI, optional cross-check) in
place and prints derived annotations only.

## Constraints honoured

No device contact, no builds, no vendor binaries/firmware bytes/calibration/secrets in this
directory, no superproject commit (the lane's writes stay under
opensource/docs/incoming/wifi-s1-cpu1/).
