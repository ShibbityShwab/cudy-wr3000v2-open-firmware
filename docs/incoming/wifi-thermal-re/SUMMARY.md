# Wi-Fi + thermal SUMMARY — Cudy WR3000 v2.0 (HiSilicon luofu R116)

**Lane:** `wifi-thermal-re` (host-only, 2026-11-21). **Full report:** [REPORT.md](REPORT.md).
**No device was touched, no vendor binary/blob is committed, per-unit data is redacted.**
**Markers:** [V] re-verified by this lane from a named local artifact; [V*] device-measured by an
earlier attended lane (cited doc); [I] inference; [U] unknown; [X] absent/blocked.

## One-paragraph verdict

The radio is a **Hi5622V100 (ShuangtaV100)** behind **two PCIe endpoints** (`59e7:0005`, one per
band) driven by two GPL-declared vendor modules — `hi5622v100_plat.ko` (371,708 B; PCIe/BAL/HCC/
INI/firmware) and `hi5622v100_wifi.ko` (3,581,748 B; cfg80211 full-MAC with **25 cfg80211/wiphy**
imports and **1 mac80211 helper**) — plus a **closed 928,920-byte `FIRMWARE.bin`**, two INIs and a
per-unit calibration store in the `fac` MTD partition. The firmware load interface is decoded: a
contiguous `memcpy_s` of the whole blob to device CA `0x01240000` (BAR0+0x6f8000) via the six
pre-programmed inbound windows, then a **`0x5a5a` write to CA `0x40000108` that releases the chip
CPU**, an IRQ enable, and two firmware HCC "ready" messages. **No rings/doorbell are used for the
firmware transfer.** A full 6.18 port requires vendor GPL source (10-16 engineer-weeks after source)
and cannot be upstreamed (closed firmware, vendor userspace ABI); without source there is **no
realistic partial AP/client path on 6.18** — only the vendor 5.10 kernel stack works, as a hybrid
fallback.

The **thermal sensor** `tvsensor@14900500` is **enabled in the vendor DT** (the circulating "vendor
DT disables it" claim is false — the disabled property is in our own reconstructed DTS). The vendor
driver (9,840 B) selects the **v1** op table for `hwver=3`, reads temperature from **`+0x04[9:0]`**,
uses `T = ((165·raw − 20625)/797)·1000 − 40000` m°C, and **enables/trims the sensor at probe**
(writes `+0x00.bit24`, `+0x04` mode/trim, and `+0x400.bit0` when `addition-en`). Our 6.18 driver
instead reads `+0x24` with a wrong v4/"/100" formula and performs no enable writes, so it sees raw 0
and the thermal core returns ENODATA. This looks **fixable with ~2-4 host days + one or two attended
bench sessions**; today it is still [X].

Other named blocks: **USB absent by design** (no DT node, no modules; dead init-script refs),
**RTC absent**, **NFC absent**, **I2C present in DT with DW driver modules available but unused and
never loaded**, **PWM** = PMU-PWM used for the CPU `vdd_logic` regulator (optional port) + a dead
LED-PWM node with no driver/consumer. No further missed hardware blocks were found.

## Key numbers

| item | value |
| --- | --- |
| Wi-Fi endpoints | 2 × `[59e7:0005]`, class `0x028000`; BAR0 16 MiB / BAR2 16 KiB / BAR4 8 MiB each |
| BAR assignment (domain 0 / domain 1) | `0x40000000`/`0x41000000`/`0x41800000` and `0x58000000`/`0x59000000`/`0x59800000`; cross-wired (phy devid 1/0) |
| firmware | 928,920 B, sha256 `7fc87e20…ef311b`, md5 `0e530b976d5a20e87358671f1a577695`; header `0x00046971`/`0x000C742D`; banner `ChenTangV100R001C20T13` |
| firmware load | target CA `0x01240000` → BAR0+0x6f8000 (alias BAR0+0x40000); 0x80000 B bounce buffer; chunks 524,288 + 404,632 B; raw `memcpy_s` |
| CPU release | `bal_write(…, 0x40000108, 0x00005a5a, 4)` — executed after the chunk loop (vendor descriptor `.data+0x26a4`) |
| post-boot handshake | IRQ enable → HCC msg id 1 ("Device plat ready", 200 jiffies) → id 2 (device ready, 2000 jiffies) |
| driver surface | 704 / 4,247 functions; 119,588 / 1,452,124 B `.text`; 126 / 308 imports; cfg80211 0/25; mac80211 0/1; `import_ns=HW_RTOS_NS` tags 46/211 |
| calibration store | `fac` MTD5 (2 MiB) mounted `/usr/local/factory`; `wifi_cali_data.kv` 8,912 B (`ZZZZ` … `a5a5a5a5`, trailer `0x22cc`), `_2g.kv` 2,336 B (trailer `0x91c`); no EFS exists |
| thermal sensor | base `0x14900500`; v1/hwver 3: temp = `base+0x04` bits[9:0]; `T_mC = ((165·raw − 20625)/797)·1000 − 40000`; enable `+0x00` bit24; addition-en `+0x400` bit0 |
| vendor thermal DT | node **enabled** (no `status`), `hwver=3`, `trim=<0 0>`, `int-base-offset=0xc`, `addition-en`, `efuse-trim-en`; zone trips 115 °C passive / 125 °C critical |
| 6.18 thermal state | node enabled, driver reads `+0x24` & wrong formula & no enable ⇒ raw 0 ⇒ ENODATA |

## Effort / verdict table

| block | today | route / effort |
| --- | --- | --- |
| **Wi-Fi on 6.18** | **[X] no driver** | vendor GPL source + 10-16 eng-wks (PCIe host 2-4, kernel API 2-4, cfg80211 1-2, fw/cal/userspace 1-2, bench 2-4); blocked by closed firmware/protocol; upstream impossible |
| Wi-Fi without vendor source | no 6.18 path | clean-room = years; no partial AP/STA milestone shown; H2D message-service gate unresolved |
| Wi-Fi today | vendor 5.10 hybrid works | keep as fallback only (1-2 days per release maintenance) |
| PCIe host (enabling) | draft patch 013/014, never booted | 2-4 eng-wks attended |
| **Thermal** | **[X] ENODATA today, but fixable** | vendor driver enables/drives it; our 6.18 driver is wrong (variant/register/formula + no enable); fix = 2-4 host days + 1-2 attended sessions; no vendor source needed |
| USB | absent | none (not on board) |
| RTC | absent | none |
| I2C | present, unused | optional 1-2 days if a consumer appears |
| PWM | PMU regulator used; LED-PWM dead | optional CPU-regulator port 2-3 days; LED-PWM do not port |
| NFC | absent | none |

## Corrections to prior docs (details + evidence in REPORT §6)

1. Vendor DT **does not** disable the thermal sensor; our reconstructed DTS does.
2. The 6.18 thermal driver is wrong: hwver 3 uses the **v1** table, register **+0x04**, formula
   **/797** (not v4/+0x24/"/100"), and needs the vendor enable/trim writes.
3. The vendor **does** execute the `0x5a5a` CPU-release write after the firmware chunk loop
   (phase19's "untaken branch" is wrong).
4. `hi5622v100_wifi.ko` imports **1** mac80211 helper (`ieee80211_get_channel_khz`), not 0.
5. The "211 namespaced symbols" figure is a modinfo-tag count, not a symbol count.
6. Thermal register window must cover `+0x00..+0x10` **and `+0x400`**, not just `+0x24`.
7. There is no EFS in this image; the per-unit store is the `fac` partition + `ZZZZ` blobs + INI.


## W23 implementation draft (follow-up, 2026-11-21, host-only)

The thermal correction is drafted as release-line **W23** under
[w23/](w23/): `0001-thermal-luofu-tvsensor-w23.patch`
(sha256 `c2a46975…53253cc`; replaces the wrong v4/+0x24/"/100" path with the vendor v1/hwver-3
`+0x04[9:0]` path and the gated enable/trim sequence), `0002-dt-bindings-thermal-luofu-tvsensor-w23.patch`
(`197bb74a…7f7ee9c`; 0x408 window + vendor properties), and
`0003-dts-luofu-r116-tvsensor-w23.patch` (`3e78b67a…3132a38`; release DTS node properties/window —
the release DTS was already `status = "okay"`; the disabled node is the lab tree). Design:
[w23/W23-DESIGN.md](w23/W23-DESIGN.md); staging and the exact attended bench test:
[w23/RELEASE-INTEGRATION.md](w23/RELEASE-INTEGRATION.md).

Host validation only: drafts apply after release patch 012 on pristine 6.18.60; driver compiles
`W=1` with **0 warnings** (host x86_64 COMPILE_TEST), checkpatch **0 errors** (one expected
missing-description warning per bare patch), DTS compiles with 0 warnings, and `dt_binding_check` on
the updated binding **PASSES** (dtschema 2026.9). **No ARM target build, no release-line validator, no
boot, no hardware validation** is claimed. The driver keeps a default-off `enable` gate
(`/sys/module/luofu_tvsensor/parameters/enable`), exposes read-only `raw`/`regs` sysfs attributes for
the bench, leaves the threshold IRQ masked (no IRQ handler in W23), and returns ENODATA while gated so
an undriven raw 0 is never published as −65 °C. The bench test in RELEASE-INTEGRATION §5 is the gate
for changing any release thermal claim.

## Next evidence needed

1. **Thermal (highest value):** one attended 6.18 boot with the corrected read-only conversion
   (`+0x04`, v1/hwver-3) compared against the vendor boot's thermal-zone value; then a second
   attended run with the vendor enable/trim sequence. This converts an [X] into a validated driver.
2. **Wi-Fi (only if source arrives):** reproduce the shipped SDK on 5.10 (module sizes/symbols), then
   port plat → wifi → calibration/userspace, bench-gated.
3. **Without source:** validate PCIe enumeration on 6.18 as independent work; do not ship Wi-Fi.
4. Optional read-only vendor-boot capture: `/sys/class/thermal/thermal_zone0/temp`, governor/trip
   state, `/sys/class/hwmon` (if any) — confirms the vendor thermal policy before touching trips.
