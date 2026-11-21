# Release notes / status - Cudy WR3000 v2.0 (HiSilicon luofu, board R116)

**Date:** 2026-11-21.  **Status: bench-verified 2-CPU baseline; NOT a public or
flashable release.**  This page is the short release status for the target; the
full record is [../upstream/series-6.18/05-release-line.md](../upstream/series-6.18/05-release-line.md)
and [../docs/SMP-RE.md](../docs/SMP-RE.md) section 24.

## Verified baselines

**Board-feature baseline 7f270b8c (2026-11-21):** same 2-CPU/network/SSH/overlay
line plus the stock board_id=3 overlay features - six `oem:green:*` LEDs
writable (status/wan/lan/internet/wifi/wifi5g), `event0` =
gpio-keys-polled (reset GPIO22 / WPS GPIO23, active-low), `watchdog0`
exposed through sysfs (CONFIG_WATCHDOG_SYSFS).  **Thermal remains unsupported:**
the temperature register reads 0 and the vendor DT disables the sensor - no
thermal claim.  This image required the base-DTB pad step (below), because the
new DTS compiles to an 11,702-byte DTB with an unaligned totalsize and the stock
U-Boot bootfip gate-word load data-aborted on it.  [V]

**2-CPU/SMP baseline 974e6ca6** (6,226,222 B,
sha256 `974e6ca63f6e86418761bc0f135e438f07d0e6ef909c61bb0339f00c5596c508`):
patches 001-061 + the loader hand-off DTS fix + canonical command line + DEVMEM/
devmem/pair-recorder, no probe tokens.  Installed manually from the stock system
and verified on the unit [V]:

- 2 CPUs: `smp: Brought up 1 node, 2 CPUs`, CPU1 mpidr `80000001`,
  3987.86 BogoMIPS, `/proc/cpuinfo` = 2, online 0-1, SCU CONFIG bit 5 set;
- network: `br-lan` 192.168.1.1/24 up, host ping 20/20 0% loss (and repeated 0%
  loss runs), SSH login;
- persistent overlay: `/dev/ubi0_1 on /overlay type ubifs (rw)`, files survive a
  reboot;
- stability: 15-minute continuous-ping soak, 0 failures; no Oops/panic/BUG/
  WARNING (the only `panic` text is the literal `panic=0` cmdline);
- persistent across reboot: fresh boot, rootfsb, pair b recorded, 2 CPUs again.

## The SMP fix is DTS-only (release-critical)

The stock U-Boot `bootfip` hand-off staging is gated by two devicetree nodes the
stock base DTB carries.  They are now in the board DTS
([dts/luofu-r116.dts](dts/luofu-r116.dts), submodule commits `5a1b5b3`/`9e17d5c`):

    aliases { serial0 = &uart1; serial1 = &uart0; console = &uart1; };

    reserved-memory {
        #address-cells = <1>; #size-cells = <1>; ranges;
        flashinfo@80600000 {
            compatible = "hsan,flashinfo_reserved";
            reg = <0x80600000 0x4000>;
            atag-offset = <0x1000>;
        };
    };

Without them U-Boot printed `alias console not found`, skipped staging, and CPU1
never polled the kernel pen; every kernel release had no consumer.  The kernel
release implementation (patch 018) was correct all along - no SMP kernel code
change is needed for the fix.  **Base-DTB padding is also required:** the LED/
DTS compiles to an 11,702-byte DTB whose totalsize is not 8-byte aligned;
the image build runs `Build/luofu-pad-dtb` (`tools/pad-dtb.py`) to fold
in the padding (11,704 B) before `append-dtb`, otherwise the stock U-Boot
bootfip gate-word load is unaligned and the loader falls back to stock
(top-level `docs/TRAILER-FORMAT.md` section 11).  The DTS contract ships as the explicit
DTS-series entry **DTS-0001**
(`../upstream/series-6.18/dts-series/0001-arm-dts-hisilicon-luofu-r116-loader-handoff.patch`;
documented in `../upstream/series-6.18/06-dts-series-dependency.md`) and must
be posted with kernel patch 0018.  The 064/065 sentinel/trampoline probes and
033-039/062/063 are bench diagnostics only and are **excluded from the release
patch series** (see `release-line/manifest.tsv` in the upstream directory).

## What works (bench baseline)

| capability | state |
| --- | --- |
| 2 CPUs (SMP) | working on the canonical image [V] |
| wired LAN | working via the **documented bench workaround**: switch CPU-copy mirror `cpu_mirror=1 mirror_dst=4` + PIE port-0 copy delivery `cpucopy_deliver=1 cpucopy_port=11` (+ `accepterr=1`).  Native CFE-equivalent L2/VLAN forwarding is a **separate project**, not part of this release [V bench] / [I] |
| SSH / userspace | working (`OpenWrt 6.18.60`, procd) [V] |
| persistent overlay | ubifs `/overlay`, survives reboot [V] |
| DEVMEM / devmem diagnostic image | optional debugging aid; the canonical image carries it, it is not required for normal operation [V] |
| board buttons/LEDs | six `oem:green:*` LEDs writable from userspace; `event0` = gpio-keys-polled (reset/WPS), active-low, 20 ms poll [V] |
| watchdog | `watchdog0` exposed through sysfs (CONFIG_WATCHDOG_SYSFS); driver bind already verified [V] |
| thermal | **unsupported on this board**: temperature register reads 0, vendor DT disables the sensor - not a release claim [U]/[X] |
| storage | FMC SPI-NAND + UBI rootfs mounted, 17 partitions [V] |
| everything else | drivers compile/bind; behaviour not necessarily exercised on hardware - see TARGET-DESIGN.md [U] |

## Canonical boot configuration

Built-in cmdline (`CONFIG_CMDLINE_FORCE`, `luofu/config-6.18`):

    ubi.mtd=rootfsb ubi.block=0,0 root=/dev/ubiblock0_0 rootfstype=squashfs
    rootwait console=ttyS0,115200 earlycon noinitrd cma=0 maxcpus=2 nr_cpus=2
    additional_cpus=1

Canonical attended module gates (default off; no `luofu_smp_*` tokens - those are
investigation-only): `luofu_switch.arm=1 egress=1 qidmap=1 eps=1 qminit=1 pfe=1
ifc=1 ext=1`, `luofu_pie.arm=1 rxcfg=1 accepterr=1 cpucopy_deliver=1
cpucopy_port=11`, `luofu_gmac.macregs=1`, `luofu_switch.cpu_mirror=1
mirror_dst=4`.  The diagnostic readout gates (`rxdiag`, `rxstroke`, `rxtrace`,
`dump`, `scan`, `pagedump`) are optional and not canonical.

## Retraction: the "2-CPU hang" was a host bench-link outage

The 2026-11-20 report of a silent 2-CPU hang after ~23 minutes is **retracted**
[V].  The host USB-Ethernet adapter lost carrier/IPs overnight; both management
IPs and the ended UART capture made the box look dead.  The UART showed it alive
with no Oops/panic/BUG/WARNING, and after the host link was restored the same
canonical image completed the verification matrix and the 15-minute soak with 0
failures.  There is no SMP stability bug.  The maxcpus=1 (`d101f96b`) and
lockdep (`18b3cb1d`) images built for the false hang are diagnostics only.

## Not included / not claimed

- **No flashable/public release**: the image was installed manually from stock;
  `platform_do_upgrade` and the first-install path have never run on a device;
  the container is not installable.
- **No Wi-Fi**: Hi5622V100 is vendor-module-only [X].
- **No native switch forwarding**: the LAN path is the CPU-copy workaround
  described above; CFE-equivalent forwarding is separate work [U]/[I].
- **No upstream review**: the kernel series is a review form of drafts/RFCs;
  bench verification of the target image does not make the drivers upstream-ready.
- Remaining [U] items (install path, bootloader env re-stamp, trailer A/B
  per-slot, pinctrl consumer states, PCIe enumeration, etc.) are listed in
  [../docs/TARGET-DESIGN.md](../docs/TARGET-DESIGN.md), [../docs/SMP-RE.md](../docs/SMP-RE.md)
  and [docs/port-notes/](docs/port-notes/) (lane records index).

## Evidence

- root cause / SMP record: [../docs/SMP-RE.md](../docs/SMP-RE.md) sections 21-24;
- canonical boot: parent-repo `build/evidence-bench-gates/2026-11-20-smp-2cpu.log`,
  `2026-11-20-smp-reboot.log`, `2026-11-21-soak-2cpu.log`,
  `2026-11-21-final-reboot-2cpu.log` (local, gitignored);
- release line + host validation:
  [../upstream/series-6.18/05-release-line.md](../upstream/series-6.18/05-release-line.md)
  and `validation/release-line/`;
- target patch stack: [target/linux/hisilicon/patches-6.18/](target/linux/hisilicon/patches-6.18/)
  (L0, 62 files; the release classification manifest is in the upstream directory).

## Next phase: board feature bring-up

The 2-CPU baseline is the platform; the remaining work is feature bring-up and hardening:

| feature | current state | next step |
| --- | --- | --- |
| 2 CPUs | [V] working, soak + reboot verified | keep as the baseline |
| wired LAN (one port, bench workaround) | [V] CPU-copy mirror + port-0 delivery | native CFE-equivalent forwarding is a separate project; multi-port/WAN needs attended cable tests |
| storage (FMC SPI-NAND + UBI) | [V] rootfs/overlay mounted, 17 partitions | exercise/verify NAND under load and the install path |
| SSH / userspace / overlay | [V] working | install/upgrade path end-to-end (A/B): manual stock-mediated path verified repeatedly; the automated first-install tool is blocked because stock has no SSH/dropbear/opkg |
| LEDs / buttons | [V] 6 oem:green:* LEDs + gpio-keys-polled (reset/WPS) exposed; brightness writable; physical light/button press pending user confirmation | stock overlay board_id=3 map (GPIO 24/25/3/2/0/1 LEDs, 22/23 keys, active-low) |
| thermal / watchdog / RTC | thermal ENODATA [X]; watchdog [V] sysfs timeout/identity/state visible (untested arm); RTC absent | debug the tvsensor read; one attended watchdog test with recovery armed |
| USB / PCIe / I2C / SPI | [U] drivers ported, devices unverified | inventory devices/buses, add board nodes, test per feature |
| Wi-Fi (Hi5622) | [X] vendor driver only for 5.10 | feasibility/port plan first; do not promise support yet |
| native switch forwarding | [I] documented workaround in use | independent LSW/PIE hardware-bridge project |
| upstream review | not submitted | finish DTS bindings cleanup, rerun validator/checkpatch |

Sequencing: (1) remote feature inventory and vendor-DT extraction, one feature at a time with
build/install/verify; (2) attended multi-port/LED/button/install-path tests with the recovery card
armed and UART captured; (3) Wi-Fi feasibility then optional port; (4) upstream consolidation.


## Verified 2026-11-21 (image 7f270b8c)

- LEDs: six oem:green:{status,wan,lan,internet,wifi,wifi5g} class devices exist;
  /sys/class/leds/*/brightness writes and reads back (active-low GPIO map from the
  stock board_id=3 overlay: 24/25/3/2/0/1).  Physical light state still needs one
  human confirmation.
- Buttons: gpio-keys-polled registered as /dev/input/event0 (reset GPIO 22
  KEY_RESTART, WPS GPIO 23 KEY_WPS_BUTTON, 20 ms poll); one press test pending.
- Watchdog: watchdog0 with identity HiSilicon luofu watchdog, timeout 30 s,
  state active (not armed by us); CONFIG_WATCHDOG_SYSFS enabled.
- DTB alignment fix [V]: the new DTS pushed the base DTB to an unaligned totalsize
  (11,702 B); U-Boot bootfip data-aborted on the unaligned gate-word load and fell
  back to stock.  scripts/pad-dtb.py now pads the DTB to 11,704 B / 8-byte
  totalsize, and the image boots (2 CPUs, ping 0% loss).

## Install path status (2026-11-21)

Manual stock-mediated install is verified repeatedly on the bench (write kernelb from the stock
telnet shell, ubiformat rootfsb, selector cell 0x11, reboot): latest run wrote image 7421bb7a
(kernelb sha 5632ccdb..., rootfs part sha 52977160...) and booted with 2 CPUs/6 LEDs/event0.
The automated first-install tool is host-side untested on the unit because it needs an SSH
transport and the vendor image has no dropbear and no opkg; a target dropbear pushed to /tmp also
failed host-key generation (Bad buf_getptr). Adding a telnet+base64 transport (binary-safe
readback) or provisioning a working SSH server on stock are the two ways to close the automated
install gate. Also: writing next_pair a from OUR image is effective (cell reads 0x10) but the
boot-pair recorder re-records the running pair on the next boot, so a warm reboot can return to
pair B; the reliable stock switch is the stock-side cell write + stock reboot.
