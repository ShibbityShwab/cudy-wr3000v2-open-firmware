# Release notes / status - Cudy WR3000 v2.0 (HiSilicon luofu, board R116)

**Date:** 2026-11-21.  **Status: bench-verified 2-CPU baseline; NOT a public or
flashable release.**  This page is the short release status for the target; the
full record is [../upstream/series-6.18/05-release-line.md](../upstream/series-6.18/05-release-line.md)
and [../docs/SMP-RE.md](../docs/SMP-RE.md) section 24.

## 2026-11-22 addendum: PCIe and Wi-Fi transport proof points

Two bench results from 2026-11-22 supersede the stale rows below.  They are proof
points, not a release change (no new image is claimed here):

- **PCIe works on the 6.18 kernel [V].**  The link never trained because GPIO0
  pins 20/21/28 - the Wi-Fi endpoint's board power/reset controls - were left as
  inputs.  They must be outputs driven HIGH before training; with that fix in the
  `pcie-luofu` `host_init` the link reaches L0 and both Hi5622 endpoints
  (59e7:0005) enumerate.  The submission form is the `wifi-pcie-hog` now in
  [dts/luofu-r116.dts](dts/luofu-r116.dts); the hardcoded driver workaround must be
  removed after one boot test proves the hog alone.
- **Wi-Fi transport bring-up works [V]; there is still no Wi-Fi driver.**  The
  clean-room `hi5622_pciet` module boots the chip (firmware upload verify
  diffs=0, CPU released, D2H DEVICE READY id 2) and stage B2 passed on image
  **W26a**: the RC inbound window is programmed from the DTS `dma-ranges`, the
  ETE rings are programmed and the H2D smoke doorbell completes cleanly.  The
  vendor-only radio stack and the missing mac80211/data path are why the "Wi-Fi:
  vendor driver only [X]" row is unchanged for *support* purposes - the transport
  is readiness evidence, not a driver.

The definitive per-item upstream checklist (2 DONE / 20 PARTIAL / 5 BLOCKED) and the
proposed target+DTS-first submission split are in
[../docs/upstream/SUBMISSION-STATUS.md](../docs/upstream/SUBMISSION-STATUS.md).
The generated review-form artefacts (`series/0013`, MANIFEST, validation logs,
DTS-0001) are stale after the 2026-11-22 patch-013/DTS edits and must be regenerated
before anything is posted.

## Verified baselines

**Board-feature baseline 7f270b8c (2026-11-21):** same 2-CPU/network/SSH/overlay
line plus the stock board_id=3 overlay features - six `oem:green:*` LEDs
writable (status/wan/lan/internet/wifi/wifi5g), `event0` =
gpio-keys-polled (reset GPIO22 / WPS GPIO23, active-low), `watchdog0`
exposed through sysfs (CONFIG_WATCHDOG_SYSFS).  **Thermal remains unsupported:**
the temperature register reads 0 and the vendor DT disables the sensor - no
thermal claim.  This image required the base-DTB pad step (below), because the
new DTS compiles to a 13,041-byte DTB with an unaligned totalsize and the stock
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
DTS compiles to a 13,041-byte DTB whose totalsize is not 8-byte aligned;
the image build runs `Build/luofu-pad-dtb` (`tools/pad-dtb.py`) to fold
in the padding (13,048 B) before `append-dtb`, otherwise the stock U-Boot
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
| wired LAN | working **natively on all four jacks** via the W22 kernel NAPT/flow-engine init: bench-verified per-port CPU delivery on eth0/port9, eth1/port10, eth2/port11 and eth3/port12 (WAN; link+DHCP-class frames), and LAN-to-LAN forwarding through the software bridge proven in both directions on eth2<->eth0 AND eth2<->eth1 with a second host adapter (broadcast + learned unicast) - no CPU-copy mirror, no hardware L2 offload required. WAN eth3 is a separate zone but its L3 path is also verified: DHCP lease from a host server, default route, and ICMP both directions [V bench M1a/M1b] |
| SSH / userspace | working (`OpenWrt 6.18.60`, procd) [V] |
| persistent overlay | ubifs `/overlay`, survives reboot [V] |
| DEVMEM / devmem diagnostic image | optional debugging aid; the canonical image carries it, it is not required for normal operation [V] |
| board buttons/LEDs | six `oem:green:*` LEDs writable from userspace; `event0` = gpio-keys-polled (reset/WPS), active-low, 20 ms poll [V] |
| watchdog | `watchdog0` exposed through sysfs (CONFIG_WATCHDOG_SYSFS); driver bind already verified [V] |
| thermal | **working via W23** (bench-proven): hwver-3/v1 path, gated enable; gate off ENODATA, enable -> 59 C idle / 63 C under load, registers verified, no IRQ [V bench] |
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
| LEDs / buttons | [V] 6 oem:green:* LEDs visually confirmed lit; gpio-keys-polled RESET (KEY_RESTART) and WPS (KEY_WPS_BUTTON) presses captured; brightness writable | stock overlay board_id=3 map (GPIO 24/25/3/2/0/1 LEDs, 22/23 keys, active-low) |
| thermal / watchdog / RTC | thermal [V] W23 bench-proven 59 C idle / 63 C load; watchdog [V] sysfs timeout/identity/state visible (untested arm); RTC absent | debug the tvsensor read; one attended watchdog test with recovery armed |
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
  KEY_RESTART, WPS GPIO 23 KEY_WPS_BUTTON, 20 ms poll); both presses captured on hardware.
- Watchdog: watchdog0 with identity HiSilicon luofu watchdog, timeout 30 s,
  state active (not armed by us); CONFIG_WATCHDOG_SYSFS enabled.
- DTB alignment fix [V]: the new DTS pushed the base DTB to an unaligned totalsize
  (13,041 B); U-Boot bootfip data-aborted on the unaligned gate-word load and fell
  back to stock.  scripts/pad-dtb.py now pads the DTB to 13,048 B / 8-byte
  totalsize, and the image boots (2 CPUs, ping 0% loss).
  **Active-DTS warning [V]:** the first active-DTS cleanup (image 6f19c6d6)
  did not boot - MTD scan stalled and the U-Boot watchdog reset the unit.
  The DTS is back to the known-good active content (compiled DTB
  byte-identical to the 7421bb7a baseline); after any active-DTS change the
  rebuilt image must be boot-tested before it is trusted.

## Install path status (2026-11-21, corrected after the mixed-pair diagnosis)

The verified install transport is the **stock telnet root shell** (`tools/stock-telnet-install.sh`
+ `tools/telnet-exec.py`; fixed in commit `6e69aa3`): read-only inspect by default; `--apply
--attended --recovery-armed` writes only pair **b** (kernelb mtd12 + rootfsb mtd14), verifies
uploads/read-backs/active-pair-unchanged, sets and reads back the U-Boot env `bootflag`, moves
the selector cell, reboots, and requires the booted cmdline to contain `ubi.mtd=rootfsb` and
the project marker.  Verified end-to-end 2026-11-21 (flashinfo-test image 6e9c54d0 PASS).

Install/recovery rules learned on the unit:
- **Set the U-Boot env `bootflag` together with the selector cell.** The loader reads the
  kernel slot named by the env bootflag; a cell-only handover after a RESET-hold recovery can pair
  our kernel with the stock rootfs.  The installer refuses when env and cell disagree.
- **Our image can only be written to pair b.** Its baked cmdline uses `ubi.mtd=rootfsb`, so
  writing pair a after a recovery produces the mixed our-kernel/stock-rootfs state.
- **The broker channel (CVE-2026-71961) only works on a consistent vendor pair.** The vendor
  bdinfo checkuuid reads `/proc/sys/dev/flash_uuid`; in the mixed state neither the vendor web
  wizard nor cmagent works.  Recovery is physical RESET-hold + TFTP stock image to restore a
  consistent pair, then the env-bootflag/selector handover above.
- The older first-install design (`first-install-stock.sh` + `FIRST-INSTALL.md`) needs SSH
  and was never hardware-run; the vendor image has no dropbear/opkg.  The telnet installer is the
  bench path.


## Hardware validation status of the cleaned DTS (2026-11-21)

The upstream-bindings cleanup that turned the board DTS into its cleaned 712-line form (commit
538377c) is HOST-VALIDATED ONLY and HARDWARE-REGRESSED: an image built from it (6f19c6d6) boots
the kernel but stalls in MTD partition registration and is reset by the U-Boot watchdog, then
falls back to stock. The known-good hardware baseline remains image **7421bb7a** with the
pre-clean DTS. Any active-DTS change must be boot-tested on the unit; host-side patch/dtc/
contract checks are necessary but not sufficient. A conservative cleanup (prose/comment removal
without active-node renames or removals) is in place (submodule commit `7ae18f2`):
only comments were removed, the compiled DTB is byte-identical to the known-good
7421bb7a DTB (13,041 B sha256 `49fe5c47...`), and a boot-test of the rebuilt
image is still required before treating it as hardware-validated.

Verified 2026-11-21 (lead): the conservative comment-only cleanup (submodule 7ae18f2) compiles to
a DTB byte-identical to the known-good DTB (13041 B sha256 49fe5c47..., padded 13048 B sha256
402e4f71...) and the rebuilt image reproduces the verified image 7421bb7a byte-for-byte
(sha256 7421bb7ace41e2e03613a0314874fabfcd16aeef60dbdd713ebc6c2b735cd4b3); release-line validator
PASS with DTS-0001 072d0b8b... byte-identical and the loader contract intact. No new boot
behaviour is introduced. The 14 deferred active deltas (switch unit address/reg order, node
renames, status removals, thermal/WiFi/sp804/WOE removals, model/compatible change, macro
rendering) are documented in upstream/series-6.18/07-dts-upstream-bindings.md section 3 and each
needs its own hardware boot-test; driver-facing deltas must never ride along with prose cleanup.

## Multi-port Ethernet status (2026-11-21)

The verified baseline serves one jack (eth2) through the CPU-copy workaround: switch
ingress mirror of eth2/port 11 to CPU destination port 4, and PIE delivers the copies
to the eth2 netdev. The other jacks do not work through this path because the CPU-copy
completion reports port 0 and carries no confirmed source-port identifier; delivering
all mirrored frames to one netdev would be incorrect bridge behaviour. The far-right
bench jack was verified to be a LAN port (box stayed alive and its LAN bridge sent IPv6
RAs, while host-to-box frames were not delivered). The agreed next step is the attended
source-port experiment in opensource/docs/MULTI-PORT-PLAN.md: if the RX completion has a
source-port field, extend mirror mask + per-port delivery; if not, multi-port LAN/WAN
forwarding is the native L2/VLAN hardware bridge project (LSW-SPEC), not a CPU-copy hack.

## Multi-port source-port experiment: negative (2026-11-21)

The CPU-copy path was tested with the extended mirror mask (ports 9-12) and full RX descriptor
capture on the unit. Frames from the far-right jack are delivered exactly like eth2: completion
port=0, vport=0, only descriptor words 0/1 populated, no source-port field. The mirror mask does
cover the other jacks, but the CPU cannot tell jacks apart. Multi-port LAN/WAN therefore requires
the native L2/VLAN bridge (LSW-SPEC, separate project); the single-port bench workaround stands.


## Physical feature checks (2026-11-21)

- LEDs [V]: with all six oem:green:* devices driven to brightness=1 the user visually confirmed
  all six LEDs lit (active-low GPIO map 24/25/3/2/0/1); set back to off afterwards.
- Buttons [V]: RESET and WPS presses both captured on /dev/input/event0 as EV_KEY press+release pairs
  (RESET = KEY_RESTART 0x198, WPS = KEY_WPS_BUTTON 0x211).


## TWD timer interrupt flags: measured and boot-tested (2026-11-21)

Upstream blocker #6 is closed. The target DTS now uses the standard Cortex-A9 TWD encoding
<GIC_PPI 13 (GIC_CPU_MASK_SIMPLE(2) | IRQ_TYPE_LEVEL_HIGH)> (0x304). Hardware facts: the GIC-400
PPI configuration is read-only/SoC-fixed and the measured GICD_ICFGR1 field for PPI13/ID29 is 0b11,
so the DT flag cannot reprogram hardware; the standard LEVEL_HIGH form is what upstream platforms
use. Boot test PASS on image f6685b80 (sha256 f6685b80...): /proc/interrupts reports
"GIC-0 29 Level twd", both CPUs tick at ~100 Hz, ping 0% loss, no Oops/RCU stalls. DTS-0001 and the
release validation will be refreshed from the new single-source DTS; the previously verified
baseline was 7421bb7a (same userspace).

## W22: native CPU delivery (2026-11-21)

The PFE NAPT/flow engine is now initialized by the driver (patch 059, gate
luofu_switch.naptinit, default on, vendor hi_pfe_napt_init sequence) and PIE
delivers the native exception/CPU-delivery class by default (patch 060, retires
luofu_pie.accepterr).  Bench result: canonical image 50023446 boots with no
mirror tokens, the NAPT init verifies itself in dmesg, host ping/SSH work, and a
reboot reproduces it.  The CPU-copy mirror workaround is no longer needed for
eth2; its patches (054-057) stay in the line as the documented fallback
(naptinit=0 + mirror tokens).  Multi-port (per-port delivery, LAN-to-LAN) is the
next experiment (M1b); it is not claimed here.

## W23: thermal sensor fix (2026-11-22)

The old driver read the wrong register/variant and never enabled the sensor, so the temperature always looked invalid.  W23 (patches 066/067) implements the vendor hwver-3/v1 path: raw = base+0x04[9:0], mC = ((165*raw-20625)/797)*1000-40000, the vendor enable/trim sequence, a 0x408 register window and a module gate (default off).  Bench: gate off -> ENODATA; enable -> raw 0x25e = 59000 mC, registers match, no IRQ, no warnings; CPU load moves it to 63 C with cooling afterwards.  The canonical image ships it with the gate default off.
