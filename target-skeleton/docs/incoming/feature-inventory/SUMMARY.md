# Feature inventory SUMMARY - canonical 2-CPU baseline (Cudy WR3000 v2.0, luofu R116)

**Date:** 2026-11-21 (capture window ~05:53-06:03 UTC).
**Image:** canonical 2-CPU baseline - OpenWrt SNAPSHOT r0-27663a0, Linux 6.18.60 #0 SMP,
board hisilicon/luofu, 2x Cortex-A9, br-lan 192.168.1.1/24.
**Method:** read-only SSH; no writes/reboots/config changes; /dev/watchdog never opened;
MACs redacted to d4:0d:ab:xx:xx:xx.
**Full report:** [REPORT.md](REPORT.md) (feature matrix, exact commands + outputs, DT-gap
analysis, prioritized plan).

## Snapshot facts

- uname: Linux OpenWrt 6.18.60 armv7l; 2x ARMv7 (Cortex-A9 part 0xc09); MemTotal 112404 kB.
- /proc/cmdline: ubi.mtd=rootfsb root=/dev/ubiblock0_0 ... maxcpus=2 nr_cpus=2
  additional_cpus=1 + luofu_switch/pie/gmac gate parameters.
- overlay: /dev/ubi0_1 on /overlay ubifs rw, 13.2M available; /rom squashfs ro.
- boot_id 5fce39de-... unchanged for the whole session (this lane did not reboot the box).

## What works [V]

- 2 CPUs (SMP), SSH/procd userspace, overlayfs root, persistent ubifs overlay.
- FMC SPI-NAND + UBI: 17 MTD partitions, ubi0 on mtd14 (squashfs + rootfs_data), 0 bad PEBs,
  no ECC failures; the only defect is 1 bad block on the unused "upgrade" partition.
- 4x luofu-gmac netdevs (eth0..eth3) each with a PHY; MDIO bus with 4 "HiSilicon Luofu Genius
  PHY" devices (phy_id 0x20669a09, gmii) actively polled, 0 MDIO errors.
- bench LAN on eth2 (carrier 1, br-lan 192.168.1.1) - via the documented CPU-copy mirror
  workaround, not native switch forwarding.
- GPIO: 2 chips / 37 lines (10106000.gpio 32 lines, 10107000.gpio 5 lines), /dev/gpiochip0/1
  present; pinctrl driver bound with 37 pins.
- clocks/resets: luofu-crg bound (sfc, gemac, mdio, pie gates on); restart driver bound.
- Watchdog device + driver (luofu-wdt) present; thermal zone + luofu-tvsensor driver bound;
  MDIO/FMC/PHY/switch/PIE/GMAC drivers all built in and bound.
- No deferred probes (/sys/kernel/debug/devices_deferred empty); every enabled DT node bound.

## What is missing / not working [X] / [U]

- **LEDs: 0.** No /sys/class/leds devices, no DT LED nodes. LED class + triggers are built in,
  but leds-gpio is not built. Hardware exists (6 green GPIO LEDs per vendor overlay evidence).
- **Buttons/keys: none.** No input subsystem at all (no /proc/bus/input/devices, /dev/input,
  /sys/class/input; input/evdev/keyboard-gpio-polled not built). RESET hardware is verified (U-Boot recovery); WPS is vendor-overlay evidence [U].
- **Thermal: unreadable.** Zone registered, mode enabled, driver bound, but
  cat /sys/class/thermal/thermal_zone0/temp -> "No data available" (x3, ENODATA); no hwmon,
  no cooling devices; trips 115 C / 125 C need review.
- **Watchdog: no sysfs telemetry** (CONFIG_WATCHDOG_SYSFS off; only dev/uevent); function
  [U] because opening /dev/watchdog arms it (prohibited read-only).
- **RTC: absent** everywhere (class, /dev/rtc, DT); vendor lab DTS has no RTC either -> no
  hardware evidence, close.
- **I2C: absent** (no /sys/bus/i2c, DT node disabled, driver not built); also disabled in the
  vendor tree -> close unless a board peripheral is found.
- **SPI: NAND-only** (spi0.1 -> spi-nand; 17 MTD works); no /dev/spidev, no user SPI.
- **USB: absent** (no bus/class/DT in canonical or vendor tree) -> close.
- **PCIe: both RCs disabled**, driver built but unbound, /sys/bus/pci/devices empty; no lspci.
  **Wi-Fi: [X]** - no wlan netdev, no cfg80211/mac80211, no wireless config/userspace
  (Hi5622V100 is vendor-module-only).
- **Multi-port Ethernet:** only eth2 ever link-tested; eth0/eth1/eth3 [U]. eth0/eth1/eth2 share
  one MAC (eth3 = +1) -> MAC plan is a multi-port blocker. speed/duplex sysfs reads return
  EINVAL (no get_link_ksettings); ethtool not installed. Native switch forwarding not done.
- **DT-gap scan [U]:** dmesg/logread are flooded by luofu-switch mirror diagnostics ("W17_MIRROR",
  "W19_CPU_MIRROR", "HB", pie-trace every ~2 s); the boot log is evicted and pstore is empty.
  A clean-boot capture is needed to close the "failed/ENOENT at boot" question.

## Condensed matrix

| Area | State | One-line |
|------|-------|----------|
| 2 CPUs / SSH / overlay | [V] | canonical baseline works |
| NAND + UBI | [V] | 17 MTD, squashfs+rootfs_data, 0 bad PEBs |
| GPIO chips/pinctrl | [V] / [X consumers] | 37 lines, 0 claimed |
| LEDs | [X] | 0 exposed; class/triggers built, leds-gpio not built |
| keys/buttons | [X] | input subsystem not built |
| thermal temp / hwmon | [X] / [X] | ENODATA; no hwmon |
| watchdog | [V]/[X telemetry]/[U] | device+driver bound; no sysfs attrs; untested |
| RTC / USB / I2C | [X] | absent by DT+config, no vendor evidence |
| SPI | [V NAND] / [X user] | FMC NAND only, no spidev |
| PCIe / Wi-Fi | [X] / [X] | RCs disabled; vendor-only Wi-Fi |
| MDIO/PHY | [V] | 4 PHYs polled, 0 errors |
| Ethernet link/MAC/speed | [V eth2]/[U rest]/[X] | duplicate MACs, EINVAL speed |
| switch forwarding | [U] | CPU-copy mirror workaround only |

## Top 5 next actions

1. **LEDs first:** confirm the 6 GPIO LED map/polarity from the existing overlay evidence, add a
   gpio-leds node + enable CONFIG_LEDS_GPIO, then attended-bench verify one LED at a time via
   /sys/class/leds/*/brightness (recovery armed).
2. **Buttons second:** enable CONFIG_INPUT/INPUT_EVDEV/KEYBOARD_GPIO_POLLED and add
   gpio-keys-polled (reset gpio 0x16, WPS gpio 0x17, poll 20 ms from overlay evidence); verify via
   /proc/bus/input/devices + events.
3. **Thermal + watchdog:** clean-boot dmesg + driver-side debug of the 0x24 sensor read to fix the
   ENODATA; enable CONFIG_WATCHDOG_SYSFS and run one attended watchdog test with recovery armed.
4. **Close RTC/I2C/SPI/USB as absent/unused, plan PCIe separately:** PCIe bring-up must use
   config/DBI-only probe, NEVER read the misc window 0x10161000; Wi-Fi stays [X] until vendor
   source/feasibility exists.
5. **Multi-port Ethernet:** attended cable tests for eth0/eth1/eth3, fix duplicate per-port MACs,
   add link-rate reporting (phylink/get_link_ksettings + ethtool in image); native switch
   forwarding remains a separate project.

## Constraints / live-state caveat

- Read-only inventory: no writes/reboots/flashes/config changes; any fire is attended with the
  recovery door armed (BENCH-FACTS.md / RUNBOOK.md); UART is read-only; never read RC misc
  window 0x10161000; /dev/watchdog stays closed.
- At ~06:04 UTC, after the last capture, the host bench link flapped and the unit left the
  canonical image: it no longer answers 192.168.1.1 and now answers at **192.168.10.1**
  (stock/vendor LAN, same unit MAC). All data here was captured before that transition.
  Restore/re-verify the canonical image (uname/model) before any further device action.
- No package manager in the image (no opkg DB): ethtool/evtest/gpioinfo must be built in.

## Evidence

- Reproduction commands and key outputs: REPORT.md sections 2-3 (all commands are read-only).
- Raw captures taken on the host during the session under /tmp/fi/ (ephemeral, not committed).
- Related tracked references: dts/luofu-r116.dts (plan/TODOs), RELEASE-NOTES.md (baseline,
  Wi-Fi [X], LAN workaround), docs/soc/vendor-dt-notes.md (board overlay evidence),
  docs/TARGET-DESIGN.md, docs/port-notes/README.md.
