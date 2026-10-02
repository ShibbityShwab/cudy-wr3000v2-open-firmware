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

## 1b. What phase 23 added (2026-10-02, same session, all on hardware)

| layer | state | evidence |
| --- | --- | --- |
| wiphy + netdev (`wifidrv1`) | registers, opens/closes, clean `rmmod`; vendor radios untouched | `docs/phase23/wifidrv1-endpoint.md` |
| endpoint claim | **only in a takeover boot**; under the vendor stack `pci_request_mem_regions` returns `EBUSY` (-16) by design | same |
| register windows | **message `BAR0+0x3f1000`** (out[0] at `+0x010` = `0x3f1010`, confirmed against the vendor boot), ETE `BAR0+0x3f2000`, region-3 IO carrying the release, the CPU-start signature and the ack/re-arm | `docs/phase24/address-verification.md` |
| inbound viewports | **programmed and byte-identical to the live vendor boot**, all six | `docs/phase23/write-path-executed.md` |
| ETE ring decode | 3 SR (`0x4003a400`/`514`/`628`) + 4 DR (`0x590`/`5fc`/`668`/`6d4`), geometry matches phase 20 | `docs/phase23/wifidrv1-both-blocks.md` |
| **ring ownership** | **3 SR + 4 DR rings + both binding writes written, readback match, 0 failures** | `docs/phase23/write-path-executed.md` |
| **firmware load + release** | FIRMWARE.bin written to `BAR0+0x6f8000`, `diffs=0`; `0x5a5a` release readback match; **CPU-start signature 9/9 changed** (`dcoldo 0xffffffff -> 0x260d4184`, BSS zeroed) | `docs/phase23/firmware-speaks.md` |
| **first firmware word** | **`out[1] 0 -> 0x04` (bit 2) read at the corrected address** | same |
| **host half** | **dispatch + ack (`out[3]` self-clearing) + clear (`out[1]` takes and holds) + re-arm (`out[4]` self-clearing) all executed on the real registers** | `docs/phase24/host-half-executed.md` |
| H2D send | host write lands (`out[0]` bitmap readback match), doorbell consumed by hardware - **device does not accept**, reproducing phase 20 on verified-correct registers | `docs/phase24/h2d-send-result.md` |
| data path | not started. The device-side H2D accept gate holds; closing it needs device-side state or a firmware trace, not host register writes | `docs/phase22/h2d-accept.md` |

**Three bugs were found by measurement on the way, all mine, all now covered by checks:** the message
window was read at `0x39000` instead of `0x3f1000` (symptom: everything looked silent); the window was
`ioremap`ed `0x1000` bytes while the write path touches `+0x1508` (symptom: kernel paging oops); and
the mailbox service keyed on a *transition* rather than the pending *state*, so it serviced nothing
while a word sat pending. Two rules came out of them and are applied before every build now: an
offset-fits-its-window sweep, and *derive each register address from the documented CA plus the
translation rule and confirm the register behaves as documented* - because the window bug survived
several phases precisely by producing plausible values at a plausible-looking address.

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
