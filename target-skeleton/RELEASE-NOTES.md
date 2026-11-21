# Release notes / status - Cudy WR3000 v2.0 (HiSilicon luofu, board R116)

**Date:** 2026-11-21.  **Status: bench-verified 2-CPU baseline; NOT a public or
flashable release.**  This page is the short release status for the target; the
full record is [../upstream/series-6.18/05-release-line.md](../upstream/series-6.18/05-release-line.md)
and [../docs/SMP-RE.md](../docs/SMP-RE.md) section 24.

## Verified baseline

Image **974e6ca6** (6,226,222 B,
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
change is needed for the fix.  The 060/061 sentinel/trampoline probes and
033-042/059 are bench diagnostics only and are **excluded from the release patch
series** (see `release-line/manifest.tsv` in the upstream directory).

## What works (bench baseline)

| capability | state |
| --- | --- |
| 2 CPUs (SMP) | working on the canonical image [V] |
| wired LAN | working via the **documented bench workaround**: switch CPU-copy mirror `cpu_mirror=1 mirror_dst=4` + PIE port-0 copy delivery `cpucopy_deliver=1 cpucopy_port=11` (+ `accepterr=1`).  Native CFE-equivalent L2/VLAN forwarding is a **separate project**, not part of this release [V bench] / [I] |
| SSH / userspace | working (`OpenWrt 6.18.60`, procd) [V] |
| persistent overlay | ubifs `/overlay`, survives reboot [V] |
| DEVMEM / devmem diagnostic image | optional debugging aid; the canonical image carries it, it is not required for normal operation [V] |
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
