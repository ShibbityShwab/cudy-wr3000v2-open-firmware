# Where the port actually stands (corrected 2026-10-02, after re-reading the record)

This file corrects a framing error made in the deployment decision earlier today. That decision
(`DEPLOYMENT-DECISION.md`) said "no port is close and a from-source build is not startable." The
research record says otherwise: **the port is already under construction**, with artifacts on disk
and, in two cases, proven on hardware. This file is the accurate state of play.

## 1. What already exists (all in this repo)

| artifact | what it is | state |
| --- | --- | --- |
| `docs/soc/luofu-r116.dts` (1,581 lines, 40 KB) | a full reconstructed device tree for this board: clocks (142 refs), pinctrl (20), GPIO (75), GEMAC/Ethernet (29+20), MDIO, GIC, timers, FMC/NAND, PCIe, LSW switch (22) | written; produced by `lab/dt2dts.py` from the extracted DT |
| `docs/phase4/mmio-map.md` (35 KB) | Wi-Fi chip MMIO register-map skeleton recovered from the driver ELF | done |
| `docs/phase4/firmware-disasm.md` (30 KB) | FIRMWARE.bin disassembled: architecture settled (Thumb + localized A32, ARMv7-A/R), section map, function boundaries, `smac`/`hcc` handler table name->address | done |
| `docs/phase3/firmware-forensics.md` (27 KB) | firmware structure: boot header, veneer clusters, MMIO register table, jump/const/exponential tables | done |
| `docs/phase3/alg-commands.md` (53 KB) | the full 414-entry `alg` command table with on-device probe results | done |
| `docs/phase3/wire-capture.md` | driver<->firmware boundary captured with kprobes | done |
| `docs/phase6`-`docs/phase10` | message fields, register windows, firmware callgraph, calibration store, KV record semantics, RSSI/spectral controls | done |
| `docs/phase14/abi-match.md` | **the struct-ABI delta to the vendor kernel is solved**: it is the `#ifdef CONFIG_PM` wowlan pointer pair; the CI build recipe carries the config | done |
| `docs/phase14/wifidrv0.md` | **our own cfg80211 driver registers a wiphy (`omo-drv0`) AND creates a real netdev (`omowl0`)** on the live device; `iw dev` lists it, up/down works, nl80211 add/del works, `rmmod` cleans up, vendor radios untouched | **proven on hardware** |
| `lab/wifidrv0/` (370-line .c) + `lab/wifiskel/` (162-line .c) | the driver skeleton and its predecessor | in repo |
| `docs/phase11`-`docs/phase18` | BAR maps, module-load gating, calibration write path, ring/ringwatch, ETE engine + init + firmware download, inbound iATU map | done |
| `docs/phase19`-`docs/phase22` | release, handshake, message service, runtime context, rings, RC routing, H2D gate, harness | done |

Two facts that were previously mis-stated by me and are now corrected:

1. **A self-built wireless driver already binds and creates a netdev on this device** (`omowl0`),
   with the struct-ABI problem solved. That is the single hardest "is a port even startable"
   question, and it is answered yes.
2. The vendor stack cannot be `rmmod`'d (phase 15: first step panics deterministically, watchdog
   recovers). So a port must **coexist** with the vendor modules rather than replace them in place -
   that is a design constraint, not a blocker, and it is already the working assumption of the
   bring-up modules.

## 2. What is genuinely not done

- The **radio firmware interface** (driver<->firmware message protocol) is mapped but not fully
  decoded; the H2D accept gate is device-side and unarmed (phase 22, measured, twice).
- The port has **no upstream target to land in**: this SoC is not in OpenWrt mainline. A "custom
  OpenWrt build" therefore means *this repo's own device tree + our own driver + the vendor
  kernel's constraints*, not `make menuconfig` on an official target.
- Nothing about the radio's PHY/calibration algorithms is public (documentation hunt: no datasheets,
  no vendor SDK); that is why the GPL request matters and why the `alg` table + calibration parser
  work exists.

## 3. The next steps the record implies (not a restart)

1. Continue from `lab/wifidrv0` - it is the live thread: extend the skeleton from "wiphy + netdev
   that opens/closes" toward actual TX/RX against the endpoint, using the phase-4 MMIO map, the
   phase-17 ETE engine work and the phase-6/phase-7 telemetry.
2. Use the recovered `alg` table + calibration parser (phases 3, 8) as the configuration surface a
   real driver must implement - it is the vendor's own control API, already extracted.
3. Keep the DTS current: it is the board description any build needs, and it is machine-produced by
   `lab/dt2dts.py` from the extracted DT, so it can be regenerated when needed.
4. Stop treating "no upstream target" as "cannot start." The correct statement is: no upstream
   target, and a local port sustained by this repo's own DTS, driver and tooling.
