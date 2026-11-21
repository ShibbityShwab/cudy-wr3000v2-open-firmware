# Feature inventory - canonical 2-CPU baseline (Cudy WR3000 v2.0, HiSilicon luofu R116)

**Lane:** feature-inventory (read-only). **Written:** 2026-11-21.
**Capture window:** 2026-11-21 ~05:53-06:03 UTC (host clock), device uptime 3392-3897 s,
device clock 2026-11-17T21:16-21:26Z, device boot_id 5fce39de-4372-4e3e-9c63-1c2e8b2449b3
(unchanged for the whole session - the unit was not rebooted by this lane).
**Image under test:** canonical 2-CPU baseline, OpenWrt SNAPSHOT r0-27663a0
(target hisilicon/luofu, arm_cortex-a9), Linux 6.18.60 #0 SMP Tue Nov 17 20:20:25 2026,
model "Cudy WR3000 v2.0 (HiSilicon luofu R116)", compatible hisilicon,luofu-r116 /
hisilicon,luofu, 2x ARMv7 Cortex-A9 (part 0xc09), MemTotal 112404 kB.
Bench path: host enp195s0f3u1 (192.168.1.88/24) <-> router LAN port eth2 (br-lan member);
all access via SSH root@192.168.1.1.

**Method / constraints.** Every device command was a read (cat, ls, find, readlink, dmesg,
ip, ubinfo). No writes, no reboots, no reflashes, no config changes, no device-tree edits on
the box, no vendor binaries or secrets were read, and /dev/watchdog* was never opened (opening
arms it). MAC addresses are redacted to d4:0d:ab:xx:xx:xx per PUBLISH.md; exact values are
live-read only. The FDT was read once from /sys/firmware/fdt (12 296 B) and decompiled on the
host with dtc; that is our own target DTS image, not a vendor blob.

**Live-state caveat (important).** After the last capture (about 06:04 UTC) the host bench
link flapped and the unit stopped answering 192.168.1.1; the host then learned the same unit
MAC at 192.168.10.1 (stock/vendor LAN). The box therefore left the canonical image during this
session (consistent with another lane starting an attended A/B switch). All inventory data
below was captured *before* that transition. Do not use 192.168.10.1 as canonical-image
evidence, and re-check uname/model before the next device action.

**Legend:** [V] verified with exact output on the box; [U] uncertain / not testable read-only;
[X] missing or not working.

---

## 1. Feature matrix (headline)

| # | Area | Feature | State | Evidence |
|---|------|---------|-------|----------|
| 1 | platform | 2 CPUs (SMP), 2x Cortex-A9 | [V] | /proc/cpuinfo 2x, uname SMP, /sys/devices/system/cpu |
| 2 | platform | SSH + procd userspace, overlayfs root | [V] | SSH session itself, /proc/mounts, df |
| 3 | platform | 128 MB NAND, FMC SPI-NAND + UBI | [V] | 17 MTD, ubi0, /rom + /overlay mounted |
| 4 | LEDs | /sys/class/leds devices | [X] | 0 entries; 0 DT led nodes |
| 5 | LEDs | LED class + triggers built | [V] | modules.builtin: led-class, ledtrig-timer/heartbeat/default-on/netdev |
| 6 | LEDs | gpio-leds driver / DT nodes | [X] | leds-gpio absent from modules.builtin; no leds node |
| 7 | LEDs | LED PWM gate (CRG) | [U] | led_pwm clock present in clk_summary, enable 0, no driver |
| 8 | GPIO | 2 gpiochips, 32+5 = 37 lines, /dev/gpiochip0/1 | [V] | /sys/class/gpio, /sys/kernel/debug/gpio |
| 9 | GPIO | any line claimed / exported | [X] | no consumers, no exports, debug/gpio lists none |
| 10 | GPIO | pinctrl driver, 37 pins | [V] | 14900100.pinctrl bound; 37/37 pins MUX+GPIO UNCLAIMED |
| 11 | keys | input subsystem (/proc/bus/input/devices, /dev/input) | [X] | absent; input core not built |
| 12 | keys | gpio-keys DT node | [X] | no key/button DT nodes |
| 13 | keys | physical RESET/WPS buttons | [V] | RESET used by U-Boot recovery; vendor overlay evidence reset gpio 0x16/wps 0x17 |
| 14 | thermal | thermal zone registered | [V] | thermal_zone0, type cls0-thermal, mode enabled, governor step_wise |
| 15 | thermal | temperature read | [X] | cat temp -> "read error: No data available" (ENODATA) x3 |
| 16 | thermal | cooling devices | [X] | none present |
| 17 | thermal | hwmon exposure | [X] | no /sys/class/hwmon |
| 18 | watchdog | /dev/watchdog0 + driver bound | [V] | luofu-wdt bound; watchdog.ko/luofu-wdt.ko built-in |
| 19 | watchdog | sysfs telemetry (state/timeout/bootstatus) | [X] | only dev/device/subsystem/uevent; CONFIG_WATCHDOG_SYSFS not enabled |
| 20 | watchdog | watchdog reset behaviour | [U] | would require opening /dev/watchdog -> prohibited read-only |
| 21 | RTC | rtc class / /dev/rtc / DT node | [X] | absent everywhere, also absent in vendor lab DTS |
| 22 | power | restart/poweroff path | [V]/[U] | luofu-reboot bound to DT reboot node; reset cmd PROVISIONAL per DTS |
| 23 | power | /sys/power, cpufreq, power_supply devices | [X] | /sys/power absent, no cpufreq, power_supply class empty |
| 24 | I2C | bus/driver/devices | [X] | /sys/bus/i2c absent; DT node disabled; driver not built |
| 25 | SPI | SPI master spi0 + spi-nand | [V] | spi0.1 bound to spi-nand; 17 MTD partitions |
| 26 | SPI | spidev / user SPI | [X] | no /dev/spidev*; FMC is spi-mem/NAND only |
| 27 | USB | host controller/bus/devices | [X] | /sys/bus/usb absent; no USB DT nodes in canonical or vendor DTS |
| 28 | PCIe | root complexes / PCI devices | [X] | both RC nodes disabled; /sys/bus/pci/devices empty; luofu-pcie driver unbound |
| 29 | Wi-Fi | wlan netdev / cfg80211 / userspace | [X] | no wlan, no cfg80211/mac80211, no wireless config, no hostapd/wpa_supplicant/iw |
| 30 | MDIO | mdio bus + 4 PHYs | [V] | 14000000.mdio:02..05, HiSilicon Luofu Genius PHY, phy_id 0x20669a09, polling counters live |
| 31 | net | eth0..eth3 netdevs, driver bound | [V] | 4x luofu-gmac; DT nodes 14380000/14400000/14480000/14500000 |
| 32 | net | link state / carrier | [V]/[U] | eth2 UP carrier 1 only (bench cable); eth0/1/3 DOWN -> [U] untested |
| 33 | net | link speed/duplex reporting | [X] | sysfs speed/duplex -> EINVAL; ethtool not installed |
| 34 | net | unique per-port MAC | [X] | eth0/eth1/eth2 share d4:0d:ab:xx:xx:xx; eth3 = +1 |
| 35 | net | native switch forwarding | [U] | LAN works via CPU-copy mirror cmdline workaround; LSW driver bound, no netdev |
| 36 | storage | MTD partitions, no bad blocks except upgrade | [V] | 17 mtd; mtd16 "upgrade" bad_blocks=1, all others 0 |
| 37 | storage | UBI volumes + overlay free space | [V] | ubi0 on mtd14: squashfs + rootfs_data; 13.2M free on /overlay; 0 bad PEBs |
| 38 | storage | pstore crash/boot log | [X] | /sys/fs/pstore empty |
| 39 | modules | loaded kernel modules | [V] | 36 loaded, all netfilter/ppp; no hardware drivers as modules |
| 40 | modules | built-in luofu driver set | [V] | modules.builtin lists all luofu/* drivers + MDIO/PHY/FMC/NAND/UBI |
| 41 | modules | built-in leds-gpio/input/i2c/rtc/usb/hwmon | [X] | absent from modules.builtin (see section 5) |
| 42 | DT | deferred probes | [V] | /sys/kernel/debug/devices_deferred is empty |
| 43 | DT | boot-time dmesg gap scan | [U] | ring buffer flooded by switch mirror diagnostics; boot log evicted (see 3.16) |
| 44 | clocks | CRG clock/reset provider | [V] | luofu-crg bound; sfc/gemac/mdio/pie gates on; led_pwm/i2c0/pcie0/1 unused |

---

## 2. Exact commands used (reproduce in ~1 minute)

All commands are read-only. SSH prefix used for every device block:

    SSH='ssh -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o BatchMode=yes root@192.168.1.1'

Identity, command line, device tree, boot state:

    $SSH 'uname -a; cat /proc/version; cat /proc/cmdline; cat /etc/openwrt_release;
          cat /tmp/sysinfo/model; cat /proc/cpuinfo; head -5 /proc/meminfo;
          date -u; cat /proc/uptime; cat /proc/sys/kernel/random/boot_id'
    $SSH 'cat /sys/firmware/fdt' > luofu-live.dtb
    dtc -I dtb -O dts -o luofu-live.dts luofu-live.dtb
    $SSH 'ls /sys/firmware/devicetree/base; cat /sys/firmware/devicetree/base/model'

LEDs, GPIO, input/keys:

    $SSH 'ls -l /sys/class/leds; find /sys/firmware/devicetree/base -iname "*led*";
          grep -rl "led" /sys/firmware/devicetree/base | head -40'
    $SSH 'ls -l /sys/class/gpio; ls -l /sys/bus/gpio/devices;
          for d in /sys/class/gpio/gpiochip*; do echo "--- $d";
            cat $d/base $d/ngpio $d/label; done;
          cat /sys/kernel/debug/gpio'
    $SSH 'cat /proc/bus/input/devices; ls -la /dev/input; ls /sys/class/input;
          find /sys/firmware/devicetree/base -iname "*key*" -o -iname "*button*";
          grep -rl "gpio-key" /sys/firmware/devicetree/base'
    $SSH 'ls /sys/kernel/debug/pinctrl; grep -c "MUX UNCLAIMED" /sys/kernel/debug/pinctrl/*/pinmux-pins;
          cat /sys/kernel/debug/pinctrl/pinctrl-handles'

Thermal, watchdog, RTC, power:

    $SSH 'ls -l /sys/class/thermal; for z in /sys/class/thermal/thermal_zone*; do
            echo "--- $z"; cat $z/type $z/temp $z/mode $z/policy; done;
          for c in /sys/class/thermal/cooling_device*; do cat $c/type; done'
    $SSH 'ls -l /sys/class/watchdog; ls -l /dev/watchdog*;
          for a in identity state status timeout timeleft nowayout bootstatus options fw_version;
            do cat /sys/class/watchdog/watchdog0/$a; done'
    $SSH 'ls -l /sys/class/rtc; ls -l /dev/rtc*; ls /sys/power; ls /sys/class/power_supply;
          ls /sys/devices/system/cpu/cpu0/; ls /sys/devices/system/cpu/cpu0/cpufreq'

Buses: I2C, SPI, USB, PCIe, MDIO:

    $SSH 'ls /sys/bus; ls -l /sys/class/i2c-dev; ls -l /sys/bus/i2c/devices; ls -l /dev/i2c*'
    $SSH 'ls -l /sys/class/spi_master; ls -l /sys/bus/spi/devices; ls -l /dev/spidev*;
          for d in /sys/bus/spi/drivers/*; do echo "$(basename $d): $(ls $d)"; done'
    $SSH 'ls -l /sys/bus/usb; ls -l /sys/bus/usb/devices; ls -d /sys/class/usb*'
    $SSH 'ls -l /sys/bus/pci; ls -l /sys/bus/pci/devices; ls -l /sys/class/pci_bus;
          command -v lspci'
    $SSH 'ls -l /sys/class/mdio_bus; ls -l /sys/bus/mdio_bus/devices;
          for p in /sys/class/mdio_bus/14000000.mdio/14000000.mdio:*; do
            echo "--- $p"; cat $p/phy_id $p/phy_interface; readlink -f $p/driver;
            cat $p/statistics/reads $p/statistics/writes $p/statistics/errors; done'

Network:

    $SSH 'ls -l /sys/class/net; for n in /sys/class/net/*; do b=$(basename $n);
            echo "--- $b"; cat $n/address $n/operstate $n/carrier $n/speed $n/duplex $n/mtu;
            readlink -f $n/device; done'
    $SSH 'ip link; ip -d link; ifconfig -a; cat /proc/net/dev; cat /etc/config/network;
          cat /etc/board.json'

MTD / UBI / modules / drivers:

    $SSH 'cat /proc/mtd; cat /proc/partitions; df -h; cat /proc/mounts;
          for m in /sys/class/mtd/mtd*; do cat $m/name $m/size $m/bad_blocks $m/bbt_blocks
            $m/ecc_failures $m/corrected_bits; done'
    $SSH 'ls -l /sys/class/ubi; for u in /sys/class/ubi/ubi*; do cat $u/mtd_num
            $u/volumes_count $u/reserved_for_bad $u/avail_eraseblocks $u/total_eraseblocks
            $u/bad_peb_count $u/eraseblock_size; done; ubinfo -a'
    $SSH 'cat /proc/modules; wc -l /proc/modules; ls -la /lib/modules/6.18.60;
          cat /lib/modules/6.18.60/modules.builtin; cat /proc/sys/kernel/modules_disabled'
    $SSH 'for d in /sys/bus/platform/drivers/*; do echo "$(basename $d):
            $(ls $d | grep -v -E "^(bind|unbind|uevent|module|drivers)$")"; done'

DT-gap auxiliary:

    $SSH 'cat /sys/kernel/debug/devices_deferred; cat /sys/kernel/debug/pinctrl/pinctrl-handles;
          cat /sys/kernel/debug/pinctrl/pinctrl-maps; head -60 /sys/kernel/debug/clk/clk_summary'
    $SSH 'dmesg; logread'   # both flooded by the switch mirror diagnostics; see 3.16

Outputs quoted below are trimmed to the relevant lines; full raw captures were kept on the
host under /tmp/fi/ (ephemeral, not committed).

---

## 3. Detailed evidence

### 3.1 LEDs - [X] nothing exposed

Commands:

    $SSH 'ls -l /sys/class/leds; find /sys/firmware/devicetree/base -iname "*led*"'

Key output:

    === /sys/class/leds ===                    (empty - zero entries)
    === DT dirs matching led ===               (empty - zero nodes)
    leds_count=0    dt_led_nodes=0

The class directory exists but there is not a single LED device. A recursive grep for "led"
matches only status properties containing "disabled"/"enabled", not LED nodes. The target DTS
deliberately has no LED node; see the tracked comment at
opensource/target-skeleton/dts/luofu-r116.dts:1236-1245 ("LEDs / keys: NOT in the lab DTS ...
add standard gpio-keys-polled / gpio-leds nodes only once the GPIO numbering, polarity and
board names are confirmed").

What *is* present:

- LED class and triggers are built in: modules.builtin lists led-class.ko, led-class-multicolor.ko,
  ledtrig-timer.ko, ledtrig-heartbeat.ko, ledtrig-default-on.ko, ledtrig-netdev.ko.
- leds-gpio is **not** built (absent from modules.builtin), so even a gpio-leds DT node would not
  probe on this image without a config change.
- The CRG clock summary has a led_pwm gate (100 MHz, enable count 0, hardware enable N, consumer
  "deviceless"); the vendor LED-PWM block is not ported and is documented dead in
  opensource/docs/MISC-SOC-BLOCKS.md:1161.

Board hardware evidence (no new vendor extraction was done for this report): the vendor board
overlay adds gpio-leds with oem:green:{status,wan,lan,internet,wifi,wifi5g}, i.e. the LEDs are
GPIO-driven; recorded in the tracked target DTS comment above and in
opensource/docs/soc/vendor-dt-notes.md:188-189. So this is a board-modelling + config gap, not
missing hardware. [U] exact GPIO numbers and polarity are not confirmed from schematics.

### 3.2 GPIO chips, lines, pinctrl - [V] 2 banks / 37 lines / 0 consumers

Key output:

    /sys/class/gpio: chip0 -> ../../devices/platform/10106000.gpio/gpio/chip0
                     chip1 -> ../../devices/platform/10107000.gpio/gpio/chip1
                     gpiochip512 -> .../10106000.gpio/gpio/gpiochip512
                     gpiochip544 -> .../10107000.gpio/gpio/gpiochip544
    base=512 ngpio=32 label=10106000.gpio consumer=(empty)
    base=544 ngpio=5  label=10107000.gpio consumer=(empty)
    /sys/kernel/debug/gpio:
      gpiochip0: 32 GPIOs, parent: platform/10106000.gpio, 10106000.gpio:
      gpiochip1: 5 GPIOs, parent: platform/10107000.gpio, 10107000.gpio:
    /dev/gpiochip0 (254,0), /dev/gpiochip1 (254,1) exist (chardev uAPI usable)
    /sys/bus/platform/drivers/gpio-export/ = bind, uevent, unbind (no devices)

DT: gpio@10106000 and gpio@10107000 (snps,dw-apb-gpio, port@0 snps,nr-gpios 32 / 5).

Pinctrl:

    driver luofu-pinctrl bound to 14900100.pinctrl
    37 pins (pin0..pin36): 37/37 "MUX UNCLAIMED (GPIO UNCLAIMED)"
    /sys/kernel/debug/pinctrl/pinctrl-handles: "Requested pin control handlers their pinmux maps:" (none)
    /sys/kernel/debug/pinctrl/pinctrl-maps: "Pinctrl maps:" (none)

So GPIO hardware and both the legacy sysfs interface and the chardev interface are present, but
no consumer/line is claimed anywhere. The target DTS records this as TODO(consumers)
(dts/luofu-r116.dts:588). LED/button bring-up is exactly "claim lines and model consumers".

### 3.3 Input devices / keys / buttons - [X] entire input subsystem absent

Key output:

    /proc/bus/input/devices: NO /proc/bus/input   (file absent)
    /dev/input:              NO /dev/input
    /sys/class/input:        NO /sys/class/input
    DT key/button nodes:     (none)
    DT contains gpio-key:    (none)

modules.builtin contains no input.ko, evdev.ko, keyboard-gpio-polled.ko (or gpio_keys) - the
input core is not built into this image at all. Hardware-wise the RESET button is real (it is
the U-Boot recovery trigger, BENCH-FACTS.md section 4) and the vendor board overlay carries
gpio-keys-polled with reset GPIO 0x16, WPS GPIO 0x17, poll interval 20 ms (tracked target DTS
comment at dts/luofu-r116.dts:1239-1241; opensource/docs/soc/vendor-dt-notes.md:188). Therefore
button support needs a kernel-config change first, then DT nodes - not just DT.

### 3.4 Thermal - zone [V], readable temperature [X], hwmon [X]

Commands and key output:

    /sys/class/thermal/thermal_zone0:
      type=cls0-thermal  mode=enabled  policy=step_wise  available_policies=step_wise
    cat /sys/class/thermal/thermal_zone0/temp   (x3):
      cat: read error: No data available        <- ENODATA every time
    trip_point_0_temp=115000 type=passive
    trip_point_1_temp=125000 type=critical
    cooling devices: (none)
    driver: luofu-tvsensor bound at 14900500.thermal-sensor
            modalias=of:Nthermal-sensorT(null)Chisilicon,luofu-tvsensor

The zone is enabled and the governor exists, but no temperature can be read; 115 C / 125 C trip
values are suspiciously high and are vendor-derived values that should be reviewed [U]. Source
patches-6.18/012-arm-thermal-luofu-tvsensor.patch shows the only MMIO access is a read of the
low 16 bits at sensor base+0x24 with the vendor linear formula; ->get_temp returns 0
unconditionally, so ENODATA comes from the thermal core (no valid cached temperature), not from
a driver error return. Root cause needs a driver-side debug lane; more read-only sysfs reads
will not find it. There is no /sys/class/hwmon in this image. The CRG's efuse_ring_clk has
enable count 0 in clk_summary (possible calibration dependency [U]).

### 3.5 Watchdog - device [V], sysfs telemetry [X], behaviour [U]

    /dev/watchdog   (char 10,130)
    /dev/watchdog0  (char 253,0)
    /sys/class/watchdog/watchdog0 -> .../14880000.system-controller/...:watchdog
    driver = /sys/bus/platform/drivers/luofu-wdt
    DT node: system-controller@14880000/watchdog, compatible hisilicon,luofu-wdt,
             timeout-sec = 30, status = okay
    sysfs attrs present: dev, device, subsystem, uevent (only)
    identity/state/status/timeout/timeleft/nowayout/bootstatus/options/fw_version:
      all "No such file or directory"
    uevent: MAJOR=253 MINOR=0 DEVNAME=watchdog0

watchdog.ko and luofu-wdt.ko are built in. The missing telemetry is expected when
CONFIG_WATCHDOG_SYSFS is not enabled (config-6.18 only has CONFIG_WATCHDOG_CORE=y). Functional
behaviour is [U] because opening /dev/watchdog arms the timer; the read-only rules for this lane
forbid that. A watchdog test must be an attended fire with the recovery card armed.

### 3.6 RTC - [X] absent, probably no hardware

    /sys/class/rtc: NO_RTC_CLASS
    /dev/rtc*:      NO_/dev/rtc
    DT rtc nodes:   (none)
    vendor lab DTS tools/lab-src/arch/arm/boot/dts/luofu-r116.dts: no usb/rtc nodes at all
    modules.builtin: no rtc-* entry

Timekeeping is NTP/soft only (OpenWrt snapshot has NTP enabled in /etc/config/system; no
RTC-backed clock). Because the vendor DTS has no RTC either, treat this as "not present in the
board design", not as a bring-up regression.

### 3.7 Power / reset

    /sys/power: absent (no /sys/power/state file)
    /sys/devices/system/cpu/cpu0/ contains cache, cpu_capacity, hotplug, of_node, subsystem,
      topology, uevent -> no cpufreq, no cpuidle
    /sys/class/power_supply: exists but empty
    DT reboot node, compatible hisilicon,luofu-reboot -> driver luofu-reboot bound
    DT boot-selector, compatible hisilicon,luofu-boot-selector -> driver luofu-selector bound
    luofu-crg reset provider bound; CONFIG_RESET_CONTROLLER=y in config-6.18

Restart/poweroff is via the DT reboot node; the target DTS marks the reset command PROVISIONAL
(TODO(restart) near dts/luofu-r116.dts:400). No poweroff GPIO, regulator, PWM or backlight
class exists. Actual reboot correctness is [U] (read-only lane; a reboot would be a fire).

### 3.8 I2C - [X] no bus in this image

    /sys/class/i2c-dev: NO_I2C_DEV_CLASS
    /sys/bus/i2c:       absent entirely (not just empty)
    /dev/i2c*:          none
    DT i2c@10111000:    status=disabled (snps,designware-i2c, clock-frequency 100 kHz)
    vendor lab DTS:     i2c0 status=disabled as well, with no I2C client nodes
    modules.builtin:    no i2c core, no i2c-designware driver
    clk_summary:        i2c0_clk gate present (100 MHz, enable count 0, hardware enable Y)

The SoC I2C block exists in the clock tree but is disabled and unpopulated in the vendor design
too. Treat as closed unless a board I2C peripheral is discovered [U].

### 3.9 SPI - [V] SPI-NAND only; no user SPI

    /sys/class/spi_master/spi0 -> ../../devices/platform/10a20000.spi/spi_master/spi0
    /sys/bus/spi/devices/spi0.1 -> .../spi0/spi0.1
    driver -> /sys/bus/spi/drivers/spi-nand ; modalias=spi:spi-nand
    DT flash@1: compatible "spi-nand", spi-max-frequency 50 MHz, tx/rx bus width 4
    /dev/spidev*: none
    modules.builtin: spi-luofu-fmc.ko, spinand.ko, nandcore.ko

The FMC controller is an spi-mem NAND controller, not a generic SPI bus, so there is no spidev
and no other SPI peripheral in the tree [X for user SPI]. The NAND flash on it works fully (17
MTD partitions, see 3.14).

### 3.10 USB - [X] absent

    /sys/bus/usb:          NO_USB_BUS
    /sys/bus/usb/devices:  absent
    /sys/class/usb*:       NO_USB_CLASS
    DT usb nodes:          none in the canonical FDT and none in the vendor lab DTS
    modules.builtin:       no usb/xhci/dwc/ehci entries

No USB hardware is modelled anywhere; close as absent by design.

### 3.11 PCIe - [X] both root complexes disabled; Wi-Fi [X]

    /sys/bus/pci:          exists (PCI core built), devices/ = empty, slots/ = empty
    /sys/bus/pci/devices:  empty
    /sys/class/pci_bus:    empty
    lspci:                 MISSING (not installed)
    DT pcie@10160000:      status=disabled, num-lanes=1, child wifi@0,0 pci59e7,0005 disabled
    DT pcie@10164000:      status=disabled, num-lanes=1, child wifi@0,0 pci59e7,0005 disabled
    /sys/bus/platform/drivers/luofu-pcie/: empty (driver built but 0 devices)
    modules.builtin:       pci-host-common.ko, pcie-luofu.ko
    clk_summary:           pcie0_clk / pcie1_clk gates present, enable 0, hardware enable N

The PCIe host driver is built and registered; nothing is enabled in DT. The DTS comment records
num-lanes=1 from a measured Link Cap. HARD RULE: the RC reg includes a misc window at
0x10161000 that must never be read (AGENTS.md); any PCIe bring-up must probe config/DBI only.
Wi-Fi is entirely absent from the image: no /sys/class/ieee80211, no wlan netdev, no
cfg80211/mac80211 modules, no /etc/config/wireless, no hostapd/wpa_supplicant/iw binaries.
The Hi5622V100 driver is vendor-only for 5.10 (RELEASE-NOTES.md: "No Wi-Fi ... [X]").

### 3.12 MDIO / PHYs - [V] bus + four PHYs, live polling

    /sys/class/mdio_bus: 14000000.mdio (real), fixed-0 (virtual)
    /sys/bus/mdio_bus/devices: 14000000.mdio:02, :03, :04, :05
    each device: DRIVER=HiSilicon Luofu Genius PHY, phy_id=0x20669a09, phy_interface=gmii,
                 of compatible ethernet-phy-ieee802.3-c22
    devlinks: 02 -> platform:14380000.ethernet, 03 -> 14400000.ethernet,
              04 -> 14480000.ethernet, 05 -> 14500000.ethernet
    mdio bus drivers: Generic PHY, Generic Clause 45 PHY, HiSilicon Luofu Genius PHY
    polling counters (since boot):
      02: reads 16907, writes  9, transfers 16916, errors 0
      03: reads 16907, writes  9, transfers 16916, errors 0
      04: reads  6796, writes 16, transfers  6812, errors 0
      05: reads 16907, writes  9, transfers 16916, errors 0

MDIO management is definitely live (active PHY state polling, zero MDIO errors). Link speed /
duplex are not exposed: /sys/class/net/eth*/speed and duplex return "read error: Invalid
argument" (the GEMAC driver has no get_link_ksettings), and ethtool is not installed [X]. The
only userspace link signal is carrier/operstate.

### 3.13 Network interfaces / multi-port Ethernet

Key output:

    /sys/class/net: br-lan, eth0, eth1, eth2, eth3, lo   (no wlan*)
    eth0: driver luofu-gmac, address d4:0d:ab:xx:xx:xx, operstate down, carrier 0
    eth1: driver luofu-gmac, address d4:0d:ab:xx:xx:xx, operstate down, carrier 0
    eth2: driver luofu-gmac, address d4:0d:ab:xx:xx:xx, operstate up,   carrier 1, carrier_changes 3
    eth3: driver luofu-gmac, address d4:0d:ab:xx:xx:xx (last octet +1), down, carrier 0
    br-lan: 192.168.1.1/24 up, bridge ports eth0 eth1 eth2 (see /etc/config/network)
    eth3 is the WAN interface (dhcp) in /etc/config/network and /etc/board.json
    DT: 4 enabled gmac nodes (14380000, 14400000, 14480000, 14500000); ethernet@14300000
        (port 8) is disabled by design (vendor lab DTS also disables it)

Findings:

- All four user-facing ports are present as independent netdevs with the driver bound and a
  PHY polling behind each. Only eth2 has ever had a link in this session (the bench cable);
  eth0/eth1/eth3 link-up/throughput is [U] until attended cable tests.
- **MAC plan is a multi-port blocker [X]:** eth0, eth1 and eth2 share one MAC and eth3 is that
  MAC+1. Cause: the DTS gives every GMAC the same bdinfo nvmem cell (mac-base index 0) except
  eth3 (index 1), and /etc/config/network then pins the duplicate MAC onto eth0/eth1/eth2.
  Three bridge ports with the same MAC is not a valid long-term multi-port configuration.
- LAN forwarding in this image is the documented CPU-copy mirror workaround from the canonical
  command line (luofu_switch.cpu_mirror=1 mirror_dst=4, luofu_pie.cpucopy_deliver=1
  cpucopy_port=11); the kernel logs W17_MIRROR/W19_CPU_MIRROR every ~2 s proving the workaround
  is being re-applied. Native CFE-equivalent LSW forwarding is a separate project [U]/[I].
- No DSA netdevs; luofu-switch is bound to 80000000.ethernet-switch but exposes no netdev/sysfs
  beyond generic attrs.
- Userland: busybox ip works; ethtool, iw, iwinfo, lsusb and lspci are not installed; there is
  no opkg package database (/usr/lib/opkg/status absent). Any test tools must be added at image
  build time.

### 3.14 MTD / UBI - [V] healthy

/proc/mtd (17 partitions, NAND writesize 2048, erasesize 131072):

| dev   | size     | name        | notes |
|-------|----------|-------------|-------|
| mtd0  | 0x040000 | esbc        | |
| mtd1  | 0x100000 | uboota      | |
| mtd2  | 0x100000 | ubootb      | |
| mtd3  | 0x040000 | enva        | |
| mtd4  | 0x040000 | envb        | |
| mtd5  | 0x200000 | fac         | |
| mtd6  | 0x040000 | bdinfo      | read-only (flags 0x0); nvmem mac-base source |
| mtd7  | 0x200000 | cfga        | |
| mtd8  | 0x200000 | cfgb        | |
| mtd9  | 0x440000 | log         | |
| mtd10 | 0x040000 | pstore      | |
| mtd11 | 0x840000 | kernela     | |
| mtd12 | 0x840000 | kernelb     | |
| mtd13 | 0x1740000 | rootfsa    | unused by this boot |
| mtd14 | 0x1740000 | rootfsb    | UBI attached here |
| mtd15 | 0x1600000 | rootfs_data | |
| mtd16 | 0x1d80000 | upgrade    | |

Bad-block / ECC state (mtd sysfs, all partitions): bad_blocks=0 everywhere **except mtd16
"upgrade" = 1**; bbt_blocks=0; ecc_failures=0; corrected_bits=0; bitflip_threshold=1.

UBI (ubinfo -a):

    ubi0: attached to mtd14 (rootfsb)
      logical eraseblock size 126976 B (124 KiB)
      total LEBs 186 (23617536 B / 22.5 MiB)
      available LEBs 0 (both volumes take the full device)
      bad physical eraseblocks 0, reserved for bad 20, max EC value 22
      volumes: 0 = squashfs (static, 20 LEB, 2539520 B, data 2510848 B, OK)
               1 = rootfs_data (dynamic, 142 LEB, 18030592 B, OK)
    /dev/ubiblock0_0 exists; /dev/ubi0, /dev/ubi0_0, /dev/ubi0_1, /dev/ubi_ctrl exist

Mounts / free space:

    /dev/root on /rom        squashfs ro           2.5M size, 100% used
    /dev/ubi0_1 on /overlay  ubifs rw             14.0M size, 36.0K used, 13.2M available
    overlayfs:/overlay on /  rw
    /tmp tmpfs 54.9M (260K used); /dev tmpfs 512K; debugfs and pstore mounted

pstore (/sys/fs/pstore) is empty, so no crash record or boot log is preserved there [X].

### 3.15 Kernel modules / built-in drivers / bound devices

/proc/modules: 36 loaded modules, all networking/PPP/crypto helpers, none a hardware driver:

    pppoe ppp_async pppox ppp_generic slhc crc_ccitt
    nf_tables nfnetlink nf_conntrack nf_nat nf_flow_table nf_flow_table_inet
    nft_* helpers (chain_nat, ct, fib, fib_inet, fib_ipv4, fib_ipv6, flow_offload, hash,
      limit, log, masq, nat, numgen, quota, redir, reject, reject_inet, reject_ipv4,
      reject_ipv6), nf_reject_ipv4/6, nf_defrag_ipv4/6, nf_log_syslog
    modules_disabled = 0

/lib/modules/6.18.60/: the 36 loaded .ko files plus modules.builtin (125 entries) and
modules.builtin.modinfo; there is no /proc/config.gz.

Built-in driver set (modules.builtin) includes every luofu driver used by the image:
crg-luofu, pcie-luofu, spi-luofu-fmc, mdio-luofu, luofu-phy, luofu-switch, luofu-gmac,
luofu-pie, luofu-reboot, luofu-selector, luofu_tvsensor, luofu-wdt, nandcore, spinand, ubi,
mtd, led-class + ledtrig-*, pinctrl-luofu, gpio-dwapb, gpio-generic, 8250/8250_dw, bridge,
8021q, phylink, fixed_phy, of_mdio, pstore/ramoops, overlay, squashfs, ubifs.

Not built in (the config-level explanation of the missing features): leds-gpio, input, evdev,
keyboard-gpio-polled / gpio_keys, i2c core + i2c-designware, rtc-*, usb/xhci/dwc, hwmon.

Bound platform drivers (driver -> devices):

    dw-apb-uart: 1010f000.serial        (ttyS0 console)
    gpio-dwapb: 10106000.gpio 10107000.gpio
    luofu-crg: 14880000.system-controller
    luofu-fmc: 10a20000.spi
    luofu-gmac: 14380000.ethernet 14400000.ethernet 14480000.ethernet 14500000.ethernet
    luofu-mdio: 14000000.mdio
    luofu-pie: 10a70000.pie
    luofu-pinctrl: 14900100.pinctrl
    luofu-reboot: reboot
    luofu-selector: boot-selector
    luofu-switch: 80000000.ethernet-switch
    luofu-tvsensor: 14900500.thermal-sensor
    luofu-wdt: 14880000.system-controller:watchdog
    serial8250: serial8250
    luofu-pcie: (no device - driver registered, DT nodes disabled)

SPI drivers: spi-nand -> spi0.1. MDIO drivers: Generic PHY, Generic Clause 45 PHY,
HiSilicon Luofu Genius PHY (4 PHY devices). No i2c, pci, usb, rtc, hwmon or input device is
bound anywhere.

### 3.16 Device-tree gaps: expected vs bound vs disabled

The live FDT (/sys/firmware/fdt, 12 296 B) decompiles to 655 lines and 43 top-level nodes; its
model and compatible strings match the tracked board DTS, i.e. it is our own target DTS build.
No vendor DTS content is reproduced here.

Deferred probes: /sys/kernel/debug/devices_deferred is **empty** (0 bytes) -> no deferred probe
at capture time [V].

Enabled DT nodes and their binding:

| DT node | status | driver / consumer | state |
|---------|--------|-------------------|-------|
| cpus/memory/fixed-clocks/GIC/timers/scu/l2-cache | enabled | core | [V] 2 CPUs boot |
| system-controller@10100000 (sysctrl syscon) | enabled | consumers: reboot, boot-selector | [V] |
| reboot | enabled | luofu-reboot | [V] bound (reset cmd PROVISIONAL) |
| boot-selector | enabled | luofu-selector | [V] bound |
| system-controller@14880000 (CRG) | enabled | luofu-crg | [V] |
| system-controller@14880000/watchdog | enabled | luofu-wdt | [V] bound |
| serial@1010f000 | enabled | dw-apb-uart | [V] ttyS0 console |
| gpio@10106000 / gpio@10107000 | enabled | gpio-dwapb | [V] 2 chips, no consumers |
| pinctrl@14900100 | enabled | luofu-pinctrl | [V] 37 pins, no consumers |
| spi@10a20000 + flash@1 | enabled | luofu-fmc + spi-nand | [V] 17 MTD |
| ethernet-switch@11000000 | enabled | luofu-switch | [V] bound, no netdev |
| pie@10a70000 | enabled | luofu-pie | [V] |
| ethernet@14380000/14400000/14480000/14500000 | enabled | luofu-gmac | [V] eth0..eth3 |
| mdio@14000000 + ethernet-phy@2..5 | enabled | luofu-mdio + Genius PHY | [V] |
| thermal-sensor@14900500 + thermal-zones | enabled | luofu-tvsensor | [V] bound / [X] temp read |
| reserved-memory/flashinfo@80600000 | enabled | loader hand-off contract | [V] boot-critical, no kernel driver |
| serial@1010e000 (uart0 / alias serial1) | disabled | - | [X] not used |
| i2c@10111000 | disabled | - | [X] no bus/driver |
| ethernet@14300000 (5th GMAC) | disabled | - | [X] by design (vendor also disables) |
| woe | disabled | no luofu-woe driver | [X] |
| pcie@10160000 + wifi@0,0 | disabled | luofu-pcie unbound | [X] |
| pcie@10164000 + wifi@0,0 | disabled | luofu-pcie unbound | [X] |

Absent entirely: leds, gpio-keys, rtc, usb, hwmon, pwm/backlight, i2c clients, spidev, sound,
led-pwm.

**Boot-log gap analysis is not possible on this snapshot [U].** The dmesg ring buffer at capture
time was 819 lines covering only ~2777-3058 s of uptime and consisted entirely of luofu-switch
mirror diagnostics (W17_MIRROR / W19_CPU_MIRROR / HB / pie-trace, emitted every ~2 s by the
CPU-copy workaround); the boot messages had been evicted. logread (689 lines) contains only
recent dropbear/odhcpd entries and zero kernel boot lines; pstore is empty. A grep of the
surviving ring for deferred/failed/ENOENT/probe/error returned 0 non-spam matches. Mitigation
evidence that no DT error surface is active: devices_deferred empty, every enabled node bound
(the thermal sensor binds but its read fails), and no consumer claims. A clean boot capture
(read-only UART capture, or dmesg within the first minute of boot) is required to close the
"failed / ENOENT" half of this section.

### 3.17 Config / build gaps behind the missing features

| Feature | Live symptom | Config-level cause (evidence) |
|---------|--------------|-------------------------------|
| buttons/keys | no input class, no /dev/input | no input/evdev/keyboard-gpio-polled in modules.builtin |
| LEDs | 0 LEDs | leds-gpio absent from modules.builtin; LED class/triggers present |
| hwmon | no /sys/class/hwmon | no hwmon in image (only thermal zone) |
| RTC | no class/dev/DT | no rtc driver or DT nodes in canonical or vendor tree |
| USB | no bus/class/DT | no USB core/driver in image; no DT nodes anywhere |
| I2C | no bus | i2c core + designware not built; DT node disabled in both trees |
| watchdog telemetry | no sysfs attrs | CONFIG_WATCHDOG_CORE=y but CONFIG_WATCHDOG_SYSFS not enabled |
| PCIe | 0 PCI devices | CONFIG_PCI/PCIE_DW/PCIE_LUOFU_HOST=y but DT RC nodes disabled |
| thermal | ENODATA | CONFIG_THERMAL/THERMAL_OF/step_wise=y, driver bound; sensor read path fails |
| user SPI | no spidev | FMC is spi-mem NAND controller only; no generic SPI controller/device |
| test tools | no ethtool/evtest/gpioinfo/opkg DB | image is minimal; tools must be built in |

config-6.18 references: target/linux/hisilicon/luofu/config-6.18 line 533
(CONFIG_WATCHDOG_CORE=y), lines 403-426 (CONFIG_PCI/PCIE_DW/PCIE_DW_HOST/PCIE_LUOFU_HOST=y,
CONFIG_PCI_DOMAINS/ECAM/MSI), lines 498-504 (CONFIG_SPI/SPI_MASTER/SPI_MEM=y), lines 511-516
(CONFIG_THERMAL/THERMAL_OF/step_wise), lines 480/452-454 (CONFIG_RESET_CONTROLLER,
CONFIG_POWER_RESET/POWER_RESET_HISI). The file is a delta against OpenWrt generic config-6.18,
so absence of an entry does not by itself prove "not set"; the live modules.builtin and sysfs
classes are the authoritative evidence used in the table above.

---

## 4. Feature-by-feature conclusions (one line each)

- **LEDs:** hardware exists (6 green GPIO LEDs per vendor overlay evidence), image exposes 0
  LEDs; needs gpio-leds DT + CONFIG_LEDS_GPIO + confirmed GPIO/polarity.
- **GPIO:** 2 banks / 37 lines fully functional, zero consumers; ideal substrate for LED/key
  bring-up; no line claimed means no board peripherals currently modelled.
- **Buttons:** RESET/WPS hardware exists, input subsystem entirely absent; needs config + DT.
- **Thermal:** zone/driver/governor present but temperature unreadable (ENODATA); no hwmon, no
  cooling devices; trips 115/125 C need review.
- **Watchdog:** device + driver present, no sysfs telemetry (CONFIG_WATCHDOG_SYSFS off),
  behaviour untested by design of this lane.
- **RTC:** absent, and no vendor DT evidence of hardware -> close.
- **Power/reset:** restart path bound (PROVISIONAL), no /sys/power, no cpufreq, no power supply
  devices.
- **I2C:** absent by DT and config, also unused in vendor DT -> close unless hardware found.
- **SPI:** NAND-only FMC working (17 MTD); no spidev/user SPI.
- **USB:** absent, no vendor DT nodes -> close.
- **PCIe:** both RCs disabled, driver unbound, no devices; Wi-Fi endpoint is vendor-only
  (Hi5622V100) and remains [X].
- **MDIO/PHYs:** 4 PHYs managed and polled with zero errors; link-rate reporting missing.
- **Ethernet:** 4 netdevs with driver+PHY; only eth2 link-verified; duplicate MACs; no
  speed/duplex reporting; native switch forwarding is the separate CPU-copy workaround.
- **Storage:** 17 MTD, UBI squashfs+rootfs_data, overlay rw with 13.2M free, 0 bad PEBs
  (1 bad block on the unused "upgrade" partition).
- **Modules/DT:** no deferred probes; missing features are disabled/absent at DT+config level,
  not failing probes; boot-log evidence is currently lost to switch debug spam.

---

## 5. Prioritized next steps (bring-up order)

### P0 - LEDs (first)
1. Confirm the six GPIO LED mapping and polarity from the existing host-side overlay evidence
   (do not extract vendor blobs from the device); add a standard gpio-leds node with
   oem:green:{status,wan,lan,internet,wifi,wifi5g} and enable CONFIG_LEDS_GPIO (currently not
   built; LED class + triggers are already built in).
2. Build + fire only as an attended bench run with the recovery card armed. Verify each LED one
   at a time from /sys/class/leds/<name>/brightness with the user watching the box; acceptance:
   all six LEDs map to the intended function, no line collisions, no pinctrl conflicts.
3. After the flash, read /sys/kernel/debug/gpio (line claimed?) and
   /sys/kernel/debug/pinctrl/pinctrl-handles to confirm the pins are claimed as expected.

### P0 - Buttons/keys (second)
4. Enable CONFIG_INPUT=y, CONFIG_INPUT_EVDEV=y, CONFIG_KEYBOARD_GPIO_POLLED=y (all currently
   absent from modules.builtin), then add gpio-keys-polled reset GPIO 0x16 / WPS GPIO 0x17,
   poll 20 ms (overlay evidence). Acceptance: /proc/bus/input/devices lists the keys and button
   events can be observed; no write path is exercised by the test.

### P1 - Thermal and watchdog
5. Thermal: capture a clean-boot dmesg and add a driver-side debug print/read for the raw word
   at 0x24 to find why the thermal core has no valid temperature; review the 115/125 C trip
   values (suspiciously high) and add hwmon exposure only if it earns its keep. Do not poke
   registers from the inventory lane.
6. Watchdog: enable CONFIG_WATCHDOG_SYSFS to expose identity/state/timeout/bootstatus, then run
   one attended watchdog test with recovery armed (set timeout, ping, close; confirm reset
   cause after reboot). Never leave a running watchdog in a test image without recovery.

### P2 - Close or defer RTC/I2C/SPI/USB; plan PCIe
7. RTC and USB: close as absent-by-design (no nodes in vendor or canonical DT); record the
   closure in the target docs.
8. I2C: keep closed unless a board peripheral is identified; if it ever is, enable
   CONFIG_I2C + CONFIG_I2C_DESIGNWARE_PLATFORM, set the node okay, and add the client node.
9. SPI: document "FMC NAND only, no spidev"; no bring-up action.
10. PCIe: separate gated plan. Enable one root complex with config/DBI-only probe; NEVER read
    the misc window 0x10161000. Acceptance: /sys/bus/pci/devices enumerates endpoint
    59e7:0005 (x1 link). Wi-Fi is still [X]: vendor-only driver; needs GPL/feasibility work
    before any support promise.

### P3 - Multi-port Ethernet
11. Attended cable tests for eth0, eth1, eth3 (one at a time), recording carrier/operstate and
    switch/PIE behaviour; eth2 is the only port ever link-tested so far.
12. Fix the MAC plan before calling multi-port done: give each port a unique MAC (per-port
    nvmem offsets or per-port config), then re-test bridge and WAN.
13. Add link-rate reporting (phylink/get_link_ksettings or equivalent) so speed/duplex are
    readable, and rebuild with ethtool for diagnostics.
14. Keep native switch forwarding (CFE-equivalent) as a separate project; the current CPU-copy
    mirror workaround is not a multi-port solution.

### Cross-cutting
15. On the next boot, capture the boot log before the mirror spam builds up (read-only UART
    capture, or dmesg within the first minute) to finish the DT-gap section 3.16.
16. Add test tools to the image build (ethtool, evtest/gpioinfo or a tiny static GPIO tool) -
    the image has no package manager and no opkg DB.
17. Any flash/fire is attended with the recovery door armed; none of the above was done on the
    canonical image by this lane.
18. Live-state note: at the end of this session the box left the canonical image and answered
    at 192.168.10.1 (stock/vendor LAN) with the same unit MAC; re-verify uname/model and restore
    the canonical image before continuing this inventory.

---

## Appendix A - relevant device details not covered above

- Console: ttyS0 (serial@1010f000), console=ttyS0,115200 earlycon; UART is READ-ONLY per
  project rules. serial@1010e000 (uart0) disabled.
- /etc/config/network: br-lan = eth0+eth1+eth2 static 192.168.1.1/24; wan = eth3 dhcp;
  explicit macaddr overrides pin the duplicate MACs described in 3.13.
- /etc/config/system: NTP enabled, no RTC-backed clock; no LED/button config sections.
- Device tree nvmem: bdinfo partition (mtd6, read-only) has a fixed-layout mac-base at offset
  0xde00, 6 bytes, exposed as two base cells (index 0 used by eth0-2, index 1 by eth3).
- Boot selector driver exposes the A/B pair through /sys (luofu-selector bound); A/B protocol is
  still [U] per target DTS (values 0x10/0x11 per loader; do not invent writes).
- Image identity: /etc/openwrt_release BUILD_ID r0-27663a0, OPENWRT_BOARD hisilicon/luofu,
  DISTRIB_DESCRIPTION "OpenWrt SNAPSHOT r0-27663a0"; kernel build string Tue Nov 17 20:20:25
  2026. RELEASE-NOTES.md names the canonical image 974e6ca6 (6 226 222 B); that hash cannot be
  re-verified from inside the image [U] - the build string and cmdline match the release notes.
- Redaction: per-unit MACs, ULA prefix and DHCP DUID are intentionally not reproduced here
  (PUBLISH.md); the MAC equality pattern is preserved with xx octets.

## Appendix B - commands that were deliberately NOT run

- No writes to /dev/watchdog* (arms the watchdog), no echo to /dev/mtd*, no ubiattach/ubirmvol,
  no flash/reboot/recovery, no /etc/config changes, no gpio export/unexport, no debugfs or sysfs
  writes, no vendor binary/secret reads, no UART input.
- One read was attempted but is intentionally not relied on: /dev/kmsg is a blocking stream and
  was not used for the boot-log analysis (the ring buffer is already captured by dmesg).

## W22 native forwarding update (2026-11-21)

The wired-LAN item is no longer limited to the CPU-copy mirror workaround: the
driver now initializes the PFE NAPT/flow engine (patch 059) and PIE delivers the
native exception class by default (patch 060).  M1a (native CPU delivery with no
mirror) is hardware-proven; the canonical command line no longer carries
mirror/accepterr/cpucopy tokens, and the mirror patches remain as the documented
fallback.  Per-port delivery and LAN-to-LAN (M1b) are the next bench tests.
