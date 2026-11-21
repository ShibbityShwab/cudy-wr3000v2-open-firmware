# W23 release integration: thermal v1/hwver-3 fix + gated enable sequence

**Status 2026-11-21.** Host-only draft; nothing below has been applied to the release series or the
device by this lane. Draft files are in this directory; the design rationale is in
[W23-DESIGN.md](W23-DESIGN.md). **No hardware validation is claimed.**

## 0. Host validation already done on these drafts

All commands ran on the host only, in a scratch tree extracted from
/home/shibbityshwab/openwrt-lab-thermal/dl/linux-6.18.60.tar.xz:

| check | result |
| --- | --- |
| apply current release patch 012-arm-thermal-luofu-tvsensor.patch to pristine 6.18.60 | clean, no rejects |
| apply 0001-thermal-luofu-tvsensor-w23.patch + 0002-dt-bindings-thermal-luofu-tvsensor-w23.patch after 012 (dry-run and real) | clean, no fuzz/rejects; files match the tested state |
| compile: make defconfig, scripts/config --enable COMPILE_TEST --enable THERMAL --enable THERMAL_OF --enable OF --enable LUOFU_TVSENSOR, make -j16 W=1 drivers/thermal/luofu_tvsensor.o | **RC=0, 0 warnings** in the build log |
| checkpatch (--no-tree --no-signoff --patch) on 0001 | **0 errors, 1 warning** - Missing commit description (expected for a bare release-line patch file; add a description when committing the staged patch) |
| checkpatch on 0002 | **0 errors, 1 warning** - same missing-description warning |
| DTS draft applied to target-skeleton/dts/luofu-r116.dts copy; cpp + dtc with the tree headers (CRG header extracted from patch 004 for the scratch test) | compiles with **0 warnings**; base DTB 12,846 B vs 12,678 B before |
| dt_binding_check DT_SCHEMA_FILES=Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml | **PASS** (dtschema 2026.9; yamllint not installed, skipped) |
| release-line validator / full OpenWrt target build / ARM kernel build / boot | **NOT RUN** (next staging stage) |

Resulting file hashes (patched content, for review):

| file | sha256 |
| --- | --- |
| drivers/thermal/luofu_tvsensor.c | 8de7df6c673b638a7a98ca0206b7fec698e35911a71c350c5c1ee0fc7306917d |
| Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml | 095ad395df0f05d0354e4aca416d0afc6debc9b49cb2db38940965ca1f1243d3 |
| patched target-skeleton/dts/luofu-r116.dts | 3b8727f5cf8fadb8eb8eb5469a9899de9936a86d9c607928aa3e4c98f6e98f04 |

## 1. Draft files and hashes

| file | sha256 | role |
| --- | --- | --- |
| 0001-thermal-luofu-tvsensor-w23.patch (549 lines) | c2a4697559a22fe3c3867b58b85382f95c3dd2b04a987a1c698174696c3253cc | Kconfig help text + full drivers/thermal/luofu_tvsensor.c rewrite; applies **after** patch 012 |
| 0002-dt-bindings-thermal-luofu-tvsensor-w23.patch (114 lines) | 197bb74aad60a4d8caacc22dacc91399a8dc967c28d6b8a5ef98a4727ef7ee9c | binding update (window/vendor properties/threshold helper); applies after 012 |
| 0003-dts-luofu-r116-tvsensor-w23.patch (24 lines) | 3e78b67a4f3dbac7bfcd9ceb7393da89b7339b0379a5d04c06b4c93a93132a38 | release-staging DTS diff (not a kernel patch): node properties + reg = <0x14900500 0x408>; node already status = okay |

The kernel drafts are bare unified diffs in the same style as the release patches; the DTS draft is a
plain diff against target-skeleton/dts/luofu-r116.dts (paths include target-skeleton/...).

## 2. Stage the drafts

From the repository root (inspect all three first). **Patch numbers 066/067 assume no other lane has
taken them - check ls opensource/target-skeleton/target/linux/hisilicon/patches-6.18 | sort | tail and
renumber monotonically after 065 if needed; the patch contents do not reference the number.**

    W=opensource/docs/incoming/wifi-thermal-re/w23
    P=opensource/target-skeleton/target/linux/hisilicon/patches-6.18

    cp $W/0001-thermal-luofu-tvsensor-w23.patch        $P/066-thermal-luofu-tvsensor-w23.patch
    cp $W/0002-dt-bindings-thermal-luofu-tvsensor-w23.patch $P/067-dt-bindings-thermal-luofu-tvsensor-w23.patch
    sha256sum $P/066-thermal-luofu-tvsensor-w23.patch $P/067-dt-bindings-thermal-luofu-tvsensor-w23.patch
    # 066 = c2a46975..., 067 = 197bb74a...

    # board DTS (release staging file, copied into the target by image/luofu.mk)
    cd opensource
    patch -p1 < docs/incoming/wifi-thermal-re/w23/0003-dts-luofu-r116-tvsensor-w23.patch
    cd ..

Nothing in patch 012 itself changes; the two new patches are applied after it in filename order.

## 3. Manifest and release-line bookkeeping

1. opensource/upstream/series-6.18/release-line/manifest.tsv - add after the 060 row:

       include	B	066-thermal-luofu-tvsensor-w23.patch	c2a4697559a22fe3c3867b58b85382f95c3dd2b04a987a1c698174696c3253cc	thermal: correct v1/hwver-3 read path + gated enable/trim sequence (W23)
       include	B	067-dt-bindings-thermal-luofu-tvsensor-w23.patch	197bb74aad60a4d8caacc22dacc91399a8dc967c28d6b8a5ef98a4727ef7ee9c	dt-bindings: luofu tvsensor window and vendor properties (W23)

   Class B = functional, gated bench path. Reclassify 066 to A after the bench pass if the lead wants
   it in the core set; keep 067 with it.
2. release-line/INCLUDED.txt - append the two staged filenames after 060 (L1 order is filename order).
3. opensource/upstream/series-6.18/05-release-line.md:
   * L0 count: 64 -> **66** patch files (001-060 + 062-067); L1 count: 53 -> **55**.
   * Section 7 class counts: currently 27 A + 18 B + 6 C = 51; adding two class-B rows makes
     **27 A + 20 B + 6 C = 53** included.
   * Correct the stale thermal statements while staging: 05-release-line.md:396-397 ('temperature
     register reads 0 and the vendor DT disables the sensor') and target-skeleton/RELEASE-NOTES.md:80,
     147 - replace with the W23 facts and 'bench-pending' wording. Do **not** claim thermal support
     until the bench test below passes.
4. **DTS review series:** the board DTS is single-sourced into
   upstream/series-6.18/dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch by
   tools/gen-dts-series-patch.py. W23 is release-line-only for now; if the DTS change is carried into
   the upstream review series, regenerate that patch with the documented command
   (06-dts-series-dependency.md, 'DTS-0001'), re-run its validator and update the referenced hash. Do
   not hand-edit the generated patch.
5. Config/cmdline: **no canonical change.** CONFIG_LUOFU_TVSENSOR=y already exists. The gate is
   default 0; enable it at runtime during the attended session. A temporary bench image may append
   luofu_tvsensor.enable=1 to CONFIG_CMDLINE (then CONFIG_CMDLINE_FORCE supplies it), but that token
   must not enter the canonical/release command line.

## 4. Staging validation (host, before any boot)

1. Release-line validator on the updated manifest:
   opensource/upstream/series-6.18/tools/check-release-line.sh <linux-6.18.60.tar.xz>
   build/linux-kbuild/waves/2026-11-21-w23-validate - expect every include patch to apply in order
   with RC=0/no fuzz, DTS contract PASS.
2. Re-run checkpatch on the staged 066/067 (same commands as section 0) and dt_binding_check for the
   binding.
3. Full OpenWrt target build (canonical config) with kernel CONFIG_WERROR; confirm zero new warnings
   in luofu_tvsensor.c and the target image builds.
4. Record the image sha256 and keep the previous canonical image (974e6ca6 baseline) for rollback.

## 5. Hardware validation - exact attended bench test

**Preconditions:** human present; recovery card READY (power-cycle + RESET + TFTP); UART capture
running read-only to an absolute path; bench link PASS; stock pair A untouched; previous canonical
image sha256 recorded. The canonical image carries the normal command line and the gate defaults to
0, so the first boot does not write the sensor.

### 5.1 Boot with the gate OFF (read-only baseline)

    # on the box (SSH), all reads
    uname -a
    dmesg | grep -i luofu_tvsensor           # expect: registered: hwver=3 enable=0 trim=0 efuse-trim-en=1 addition-en=1 raw=0x...
    cat /sys/module/luofu_tvsensor/parameters/enable     # 0
    ls /sys/bus/platform/devices | grep -i tvsensor      # expect 14900500.thermal-sensor
    cat /sys/bus/platform/devices/14900500.thermal-sensor/raw
    cat /sys/bus/platform/devices/14900500.thermal-sensor/regs
    cat /sys/class/thermal/thermal_zone0/type            # cls0-thermal
    cat /sys/class/thermal/thermal_zone0/temp            # expect No data available (gate off)

Pass: probe line shows hwver=3, gate 0, no errors; raw is readable; regs is readable; the zone reports
no data (same as the current release behaviour, so no regression). Record the raw baseline and the
full regs dump.

### 5.2 Enable the sensor (attended write step)

    echo 1 > /sys/module/luofu_tvsensor/parameters/enable
    dmesg | tail -5            # expect: enable sequence applied: ctrl=... temp_ctrl=... int_mask=...
    cat /sys/bus/platform/devices/14900500.thermal-sensor/raw
    cat /sys/bus/platform/devices/14900500.thermal-sensor/regs
    cat /sys/class/thermal/thermal_zone0/temp

Expected register readback (record actual values, do not 'fix' them live):

| word | expectation |
| --- | --- |
| +0x00 | bit 24 = 1 |
| +0x04 | bit 27 = 0; bit 20 = 0 (efuse trim); low 10 bits = a plausible raw |
| +0x08 | low 10 bits = **922 (0x39a)** if hisilicon,threshold-mc = <125000> was parsed |
| +0x0c | 1 written by the clear; readback implementation-specific |
| +0x10 | bit 0 = 1 (masked; inferred polarity) |
| +0x400 | bit 0 = 1 (addition-en) |

Pass criteria:

1. raw changes from 0 to a stable non-zero 10-bit value (expected range roughly 0x150-0x300 at room
   temperature; record several readings over at least 10 s - it must not jump between 0 and non-zero).
2. Independently read the same words with the static devmem tool (installed on the canonical image):
   devmem 0x14900500 32, 0x14900504, 0x14900508, 0x1490050c, 0x14900510, 0x14900900 (reads only, no
   writes!). These must match the sysfs regs dump except the inherently write-1-clear word. Stop if
   devmem is unavailable; the sysfs attributes are the primary instrument.
3. /sys/class/thermal/thermal_zone0/temp returns a value in **15000-35000 mC** at idle (room
   temperature; adjust the window to the actual room with a thermometer). It must match the math from
   the raw word: e.g. raw 0x1a0 (416) -> (165*416 - 20625)/797 = 60 -> 20000 mC. Cross-check the
   arithmetic on the captured numbers.
4. **No interrupt storm:** capture cat /proc/interrupts before and after, run dmesg -w (or the UART
   capture) for 120 s, and check for 'nobody cared', 'spurious', 'irq ... disabled', 'BUG: soft
   lockup', Oops/BUG/WARNING. The driver does not request the IRQ, and the mask is left at 1; any
   new/unhandled IRQ line or storm is a **hard fail** (mask-polarity assumption wrong).
5. **Thermal sanity:** idle value stable +/-1-2 C over 60 s; then load the CPUs (yes >/dev/null & twice
   for 60 s), read the value every 5 s: expect a rise of at least ~2 C and no lockup; kill the load and
   expect a fall back toward idle. If the sensor does not move under load, or reads < -20 C / > 60 C at
   idle, fail and capture raw/regs/dmesg.
6. Compare with the vendor baseline if a read-only stock capture is available (same unit, idle,
   /sys/class/thermal/thermal_zone0/temp on the stock boot); record both values. They need not be
   identical (different boot/CPU state) but must be physically plausible and within a few C after the
   sensor has been enabled for a minute.
7. No Oops/panic in UART; SSH/network unchanged; the box stays up for at least 15 minutes of polling.

### 5.3 Reboot/default-image check

Reboot once (warm) on the unchanged image: the driver must come up with gate 0 again (enable reads 0),
the zone must report no data, and no IRQ storm. Record whether the hardware sensor stays enabled
across the reboot (the raw word may be non-zero; that is fine, the gate controls reporting). If a
boot-time gate is wanted for the bench image, build it with luofu_tvsensor.enable=1 in the temporary
command line and repeat 5.2 from boot.

### 5.4 Rollback / fail criteria

* **No write to any other block, no flashes, no module reload in the same session.** All W23 writes
  go through the driver's own sequence; never poke 0x14900500.. by hand.
* echo 0 > /sys/module/luofu_tvsensor/parameters/enable stops temperature reporting immediately (the
  already-issued enable/trim writes are not undone; this is not a hardware revert).
* **Abort immediately** if: any IRQ storm/unhandled-IRQ message; any Oops/BUG/WARNING; the raw word is
  implausible; thermal zone shows values outside physical range; the box hangs or network drops.
  Power-cycle with RESET/TFTP recovery armed; do not attempt an in-place register fix.
* Full rollback: reinstall the previous canonical image (recorded sha256) and/or boot stock pair A;
  the sensor writes are not persistent calibration data and are re-initialised by the stock driver.
* Do not leave the bench image armed for unattended boots if the gate was put in the command line; the
  committed release image keeps the gate default 0.

## 6. What must be bench-verified before this becomes a release claim

1. The three enable/trim writes are accepted and the raw temperature is non-zero and stable.
2. The exact v1/hwver-3 conversion matches a physical reference (room thermometer and/or stock
   thermal-zone value) within the stated window.
3. The register map/readback is as decoded (+0x00 bit 24, +0x04 bits 20/27, +0x400 bit 0, +0x10 bit
   0, +0x08 low 10 bits).
4. No interrupt storm and no spurious IRQ handling.
5. The zone value rises under CPU load and falls afterwards (real sensor, not a stuck register).
6. Boot-to-boot determinism with the gate off, and no side effects on the rest of the image.
7. The release-line validator and full ARM target build pass with the patches staged.

## 7. Known limitations (kept visible)

* Threshold interrupt is masked and no IRQ handler is claimed; the threshold register is a bench
  helper, not an interrupt implementation.
* +0x400 lies inside the PMU-PWM word's page range; the 0x408 window must be split if PMU-PWM is ever
  enabled (see W23-DESIGN section 3).
* Mask polarity is inferred (bench abort criterion above); delay units are [U] (1 ms proxy).
* v4 enable/trim and the v4 alternate conversion are not implemented; not needed for this board.
* All numbers are from host-side decode; no hardware validation is claimed by this directory.
