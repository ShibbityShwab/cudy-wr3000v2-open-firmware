# W23 design: luofu tvsensor v1/hwver-3 correction and gated enable sequence

**Status 2026-11-21:** host-only draft. Nothing in this directory has run on hardware, has been
applied to the release series, or has been built as an ARM target image. This is the design record for
the two kernel patch drafts and the DTS draft next to it; validation/rollback/staging is in
[RELEASE-INTEGRATION.md](RELEASE-INTEGRATION.md).

## 1. Why W23 exists

The release-line thermal driver (patch 012-arm-thermal-luofu-tvsensor.patch) was written before the
vendor hi_tvsensor.ko was decompiled far enough. It does:

    raw = readl(base + 0x24) & 0xffff;                 /* v4 register */
    mC  = ((165 * raw - 32835) / 100) * 1000 - 40000;  /* v4 approximation */

Three independent facts from the vendor module (decoded in [../REPORT.md](../REPORT.md) section 3,
Ghidra 12.2 + objdump) contradict that for this board:

1. The vendor probe chooses the **v4** op table only when the DT hwver is **4**
   (hi_tvsensor_probe+0x45c: cmp hwver,#4; beq v4_register; otherwise
   hi_tvsensor_hw_drv_register = v1 table). This board's DT says hwver = <3>, so the vendor uses the
   **v1** table.
2. The v1 temperature word is **base + 0x04 bits [9:0]**, not base + 0x24. The v4 word is base + 0x24,
   but it is not this board's path.
3. The v4 formulas are ((raw*165 - 23925) / 795) * 1000 - 40000 and ((raw*165 - 32835) / 605) * 1000
   - 40000 (normal/alternate), not /100; and for v1 hwver 3 the formula is
   ((raw*165 - 20625) / 797) * 1000 - 40000, cross-checked against the vendor's own set_th inverse.

In addition, the vendor **probe enables and trims the sensor**; the old driver performed no writes, so
even the correct register would read 0 on our boot (the vendor enables it only when its own module
probes). Reading the wrong register produced raw 0, the wrong formula extrapolated it to -368000 mC,
and the thermal core returned -ENODATA.

## 2. Decoded facts the driver now uses

### 2.1 Variant selection and conversions

| hwver | op table | raw source | conversion (mC) |
| --- | --- | --- | --- |
| 1 | v1 | readl(0x04) & 0x3ff | v=165*raw-26235; q=(((s64)(-0x57f57f57)*v)>>32)+v)>>9; q-=v>>31; mC=q*1000-40000 |
| 2 | v1 | readl(0x04) & 0x3ff | same shape, offset=-23925, magic=-0x5b212ad3 |
| **3 (this board)** | v1 | readl(0x04) & 0x3ff | mC=((165*raw-20625)/797)*1000-40000 |
| 4 | v4 | readl(0x24) | mC=((165*raw-23925)/795)*1000-40000 (normal path) |

The v4 alternate conversion (/605, offset 32835) is selected by the **high half** of the vendor
get_temp argument. The vendor probe passes only the low-nibble hwver value, so with this DT the
alternate path is unreachable; W23 implements the normal v4 formula and documents the alternate as
[U] rather than inventing a second selector. The v4 **enable/trim** sequence (different registers and
a syscon efuse regmap) is also not implemented: this board is hwver 3. If a v4 board appears, the
driver logs a warning and remains read-only for the enable gate.

The v1 hwver-1/2 magic forms are the vendor's compiled signed-multiply sequence (they are not simple
division constants); W23 reproduces them operation-for-operation so the variants are at least
numeric-faithful, marked [U] unvalidated because this board never uses them.

If efuse-trim-en is present, the vendor adds the DT trim value (sign-selected: cell 0 nonzero means
negate cell 1) to the converted millidegrees. On this board trim = <0 0> and the efuse trim path is
selected, so the term is zero; the code implements it because it is a one-line vendor fact.

### 2.2 Enable/trim sequence (from hi_tvsensor_probe and the v1 ops table)

The vendor order, with the W23 faithfulness notes:

| step | vendor operation | W23 |
| --- | --- | --- |
| 1 | set_mode(base, 0) -> +0x04 bit 27 = 0 | same |
| 2 | set_trim(base, efuse_trim_en, trim) | efuse-trim-en: clear +0x04 bit 20 (efuse trim); else set bit 20 and program sign bit 19 + magnitude bits 10..18 |
| 3 | set_en(regmap, base, 1, addition_en) -> +0x00 bit 24 = 1; when addition-en, also +0x400 bit 0 = 1; 10 vendor delay loops | same, delay = usleep_range(1000, 2000) |
| 4 | read the zone trip, set_th(base, trip, hwver), set_int_mask(base, 0, int-base-offset), clr_int | mask stays **1** (see below); optional hisilicon,threshold-mc programs +0x08[9:0]; +0x0c = 1 clears the latch |
| 5 | request threaded IRQ and handle critical work | **not claimed** in W23; the threshold IRQ is left masked |

**Interrupt-mask polarity is inferred, not proven.** The vendor code writes 1 to +0x10 bit 0 before
programming the threshold and 0 after requesting its IRQ, which is consistent with 1 = masked
(masking during setup, unmasking after the handler exists). W23 never claims the IRQ, so it leaves the
bit at 1 (still masked) and does not unmask it. If the polarity is the opposite, the bench would see
an unhandled SPI-0 storm - this is an explicit abort criterion in the bench test, and the gate is
runtime-writable so the sequence can be stopped without a rebuild. The unconfigured block reads
+0x10 = 0x3 in our earlier read-only observation, which is consistent with a masked default but does
not prove the bit meaning.

**Delay units are not decoded.** The vendor calls arm_delay_ops 10 times with a constant that cannot
be mapped to microseconds from the instruction stream alone. W23 uses one millisecond, which is
safely longer than any plausible vendor wait; marked [U].

**Threshold inverse (derived, not guessed).** The vendor v1 set_th for hwver 3 computes, from the trip
temperature in mC and with signed division truncation, the inverse of the read conversion:
raw_th = (797 * (T_mC / 1000) + 31880 + 20625) / 165 = (797 * degC + 52505) / 165, clamped to 10
bits. For the DT helper value 125000 mC this is (797*125 + 52505)/165 = 922 = 0x39a. The property is
a bench-validation helper, not an upstream ABI; it exists so the threshold word can be verified
without claiming the IRQ.

### 2.3 Gate and diagnostics

* enable module parameter: 0 = read-only poll (default), 1 = run the enable/trim sequence. It is a
  module_param_cb with a custom setter so it can be flipped at runtime in an attended session:
  /sys/module/luofu_tvsensor/parameters/enable (the driver/module name is luofu_tvsensor; the lead's
  shorthand 'luofu_thermal=1' becomes this parameter, or the equivalent boot token
  luofu_tvsensor.enable=1 in a temporary bench command line).
* get_temp() returns -ENODATA while the gate is off. This is deliberate: an un-enabled sensor reads
  raw 0 and the correct v1 hwver-3 formula maps 0 to **-65 C**, which is a legal thermal value and
  would look like a measurement. Gating the zone avoids publishing a fake -65 C.
* Two read-only sysfs attributes are attached to the platform device for bench evidence:
  raw - raw=0x... temp_mC=... hwver=... enabled=...; and regs - the live +0x00/+0x04/+0x08/+0x0c/+0x10
  words (and +0x400 when the window covers it).
* The probe dev_info line records hwver, trim, the two booleans, the gate state and the raw word, so
  the UART capture alone is enough to see what the DT supplied.

## 3. DT and binding changes

* The release DTS (see correction below) already has status = okay; the W23 DTS draft keeps it enabled
  and adds the vendor properties the driver consumes: hwver = <3>, trim = <0 0>,
  int-base-offset = <0xc>, addition-en, efuse-trim-en, and the bench helper
  hisilicon,threshold-mc = <125000>.
* reg is widened from <0x14900500 0x100> to <0x14900500 0x408> so the +0x400 addition enable is
  inside the mapped resource. The vendor's 0x14 is far too short even for the v1 registers
  (+0x00..+0x10).
* **Shared-page caveat:** 0x14900800 (PMU-PWM, used for CPU vdd_logic by the vendor) falls inside
  that 0x408 window. The release target currently has no PMU-PWM node, so there is no resource
  conflict today. If PMU-PWM is ever enabled, the sensor node must be split into two resources
  (<0x14900500 0x14> and <0x14900900 0x4>) or moved behind the same syscon; the DTS comment and
  binding say so.
* The binding is expanded from the old read-only description to the properties above and the widened
  window; dt_binding_check passes with dtschema 2026.9 (see RELEASE-INTEGRATION section 0).

## 4. What W23 intentionally does not do

* No interrupt request and no critical-event work item; the threshold IRQ is left masked. A later
  W23b can add the threaded handler once the mask polarity and threshold policy are bench-measured.
* No cooling map / hi_knet_cdev port; the DT zone stays sensor + trips (the vendor cooling device
  needs the LSW CAR model, which is a separate lane).
* No v4 enable/trim sequence and no v4 alternate conversion (unreachable with this DT; [U]).
* No clock/reset model (vendor DT has none).
* No trip-value changes; the vendor 115 C passive / 125 C critical remain vendor policy data, not our
  measurement.
* No release-series or device change: the drafts are under this incoming directory only.

## 5. Corrections/notes found while writing the driver

1. **Release DTS is not disabled.** The lead's brief says 'the DTS node status that we currently have
   disabled'; the release-line staging DTS target-skeleton/dts/luofu-r116.dts has status = okay (and
   so does the 2026-11-21 canonical image's DTB). The status = disabled node is in the **lab** tree
   docs/soc/luofu-r116.dts / lab/luofu-kernel/dts/luofu-r116.dts, which is not what the release image
   builds. W23 therefore does not need an 'enable' hunk in the release DTS; its DTS draft keeps the
   node okay and adds the properties/window. If the staging base ever has the node disabled, set it
   to okay in the same hunk.
2. **Do not report the conversion before enabling.** With the corrected v1/hwver-3 formula, raw 0 is
   -65 C (valid), so the old 'report the math as-is' comment is unsafe for an un-enabled sensor. The
   gate-driven -ENODATA is a behavior change from the old driver but not a user-visible regression:
   the old driver also returned -ENODATA on this board.
3. **Threshold register expectation:** with hisilicon,threshold-mc = <125000>, the bench should read
   +0x08[9:0] = 922 (0x39a). This value is a derived check of the inverse formula; if it is different,
   capture the word before changing anything.
4. **Vendor delay constant** not decodable to time units; one millisecond used, marked [U].
5. **Module parameter name:** the correct runtime path is
   /sys/module/luofu_tvsensor/parameters/enable; 'luofu_thermal' was only the lead's example.
6. **The Kconfig 'performs no register writes' text was replaced** by the gated description; no
   Makefile change is needed (the object name is unchanged).
