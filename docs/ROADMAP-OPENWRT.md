# Roadmap: from a working box to a complete open-source OpenWrt submission

> **Status 2026-11-17: aspirational plan, not current state.** This document was written
> 2026-11-14 around a hybrid bench image and has not been executed. Do not read any milestone
> as met without a receipt, and do not treat bench gates as upstream progress. The live state
> is the parent repository's `STATUS.md`; publication blockers are in
> `docs/PUBLISH-READINESS.md`.

**Bench state at the time of writing (2026-11-14, superseded):** the vendor userspace and its
binary-only driver stack (via patched modules, see `VENDOR-STACK-ON-MAINLINE.md`) ran on a
vanilla 5.10.201 kernel, Ethernet up at `eth2`, `br-lan` 192.168.10.1, ping 3/3. As of
2026-11-16 the box is back on vendor stock and the slice-delivery lane is blocked (parent
`STATUS.md` section 3); the Wi-Fi is **Hi5622V100**, not MT76 (an earlier line here was wrong).

That hybrid is not an open-source port. This document is the concrete plan to close the gap,
block by block, and land it in OpenWrt. Each phase states the deliverable, the method, and the
evidence that closes it.

---

## Phase 0 - freeze the working state (done, 2026-11-14)

Deliverables, all in this repository:

- `lab/vendor-stack/init-vendorroot.c` - the initramfs init: mounts the vendor rootfs_data, writes the
  overlay shadows, arms the watchdog keeper, loads the 22-module vendor stack in dependency order.
- `tools/patch-vendor-modules.sh` - reproducible `.plt` patching for a module directory.
- `docs/VENDOR-STACK-ON-MAINLINE.md` - the loader recipe and the two blockers.
- The kernel tree, DTS and build lane in the private half (`tools/lab-src`, the parent `RUNBOOK.md`; the old Windows `tools\local-build.bat` is archived).

Evidence: G4 green, ping 3/3, `Hi-GEMAC ... eth2` and `br-lan port 1(eth2)` in dmesg.

## Phase 1 - observation platform (the reversal multiplier)

The vendor stack now runs on our kernel, which turns every vendor driver into a live oracle. The goal
of this phase is to make that oracle easy to query.

- **1.1** A register snapshot harness: a patched vendor module or a small kernel helper that dumps the
  register windows of `hi_kmdio`, `hi_lsw_pfe`, `hi_kphy`, `hi_kmac` while they run, on every boot,
  into the overlay. We already hold the offsets from the disassembly (`hi_kmdio`'s bus-op map is
  `0x20/0x2c/0x30/0x38`; frame format `0x8000|(reg<<8)|phy`).
- **1.2** A trace of a PHY transaction: the vendor's own MDIO reads/writes during probe, logged in
  order. This is the complete PHY init sequence, which we do not have to guess.
- **1.3** A trace of the switch bring-up: `hi_lsw_pfe`'s register writes during init, in order.

Deliverable: `dumps/live/<boot>/` register and transaction traces, replayed by our drivers.

## Phase 2 - clean-room drivers, block by block

Each block is closed by running our driver with the vendor modules **removed** and the box still
passing its smoke test. Order is by dependency, which is also the order the vendor loads them.

| block | current | method | closes when |
| --- | --- | --- | --- |
| FMC/NAND | 95% | finish BCH ECC and bad-block handling from `hi_flash`'s tables | the vendor rootfs mounts with our controller only, read and write verified |
| CRG/clocks | 90% | enumerate remaining gates from the live clock tree | every consumer resolves with no vendor module |
| WDT | 100% | done | - |
| pinctrl | 10% | replay `hi_kpinctrl`'s group table from Phase 1 dumps into pinctrl-mmio | UART/ETH/MDIO pins configure from our driver |
| MDIO | 60% | implement the four-phase handshake against the traced transactions | `mdio-tool` read/write works with `hi_kmdio` unloaded |
| KPHY | 0% | the Genius PHY init sequence from the 1.2 trace | our phy driver binds `14000000.mdio0:04` |
| GEMAC | 70% | already probes; attach to our PHY once MDIO/KPHY are ours | `eth2` up with no vendor module |
| LSW switch | 15% | `hi_lsw_pfe`/`hi_lsw_dp` disassembly (capstone, both are ~120 KB) plus the 1.3 trace: VLAN table, MAC table, port config | the LAN ports forward through our switch driver |
| PCIe | 40% | host controller init from the phase 19-22 work, plus the vendor's link sequence | the WiFi endpoint enumerates under our host driver |
| WiFi | 30% | see the decision below | a wiphy exists without the vendor's Wi-Fi modules |

The switch and the WiFi are the two large efforts. Everything above them is weeks, not quarters.

## Phase 3 - the WiFi decision (a real fork in the road)

The Wi-Fi stack is `hi5622v100_plat`, `hi5622v100_wifi` and a **928,920-byte firmware blob**. The
driver side is tractable: five modules build with matching vermagic, the HCC service map is mapped, and
the mailbox protocol is partially reversed. The firmware is not: without source it cannot be made open,
and no amount of driver work changes that.

Three options, in order of preference:

1. **Request the GPL'd source from Cudy.** The kernel modules are GPL-derived works and the vendor must
   provide corresponding source. This is the only path to a genuinely complete open-source submission,
   and it is a legal request, not a technical one. Prepare the request with the specific modules listed
   and the license evidence.
2. **Ship the blob, documented.** A port that loads the vendor Wi-Fi firmware is not open-source in the
   strict sense, but it is what most dual-licence router ports do. The device page states it plainly.
3. **Replace the stack.** A softmac rewrite on the Hi5622 MAC is a multi-quarter project with no
   guarantee.

Do not let the Wi-Fi decision block Phases 1, 2, 4 and 5. The wired box is fully portable without it.

## Phase 4 - userspace

- Replace the vendor's `hsan_*` rc scripts and `hi_appenv`/`hi_*env` blobs with device tree data and
  standard OpenWrt init.
- Reimplement the vendor LuCI pages (`patched/*.lua`) as standard packages; the RPC injection surface
  documented in the writeup disappears with them.
- Port the LED and button maps, the dual-image/sysupgrade logic, and the MAC address handling.

Deliverable: a rootfs that boots with no vendor userspace component.

## Phase 5 - the OpenWrt submission

- **Target**: `target/linux/luofu` (Hi5671, dual Cortex-A9, R116). **None exists yet** - not in
  this repository and not upstream. The private build tree has a working `luofu-r116.dts`, but it
  is a stage-1 skeleton with vendor `hsan,*` compatibles, a `lab` partition, and no bindings; it
  is not a submittable DTS. The kernel config is in the private build lane.
- **Drivers**: send the clean-room drivers upstream first, as separate patch series, with devicetree
  bindings: FMC/NAND, CRG, WDT, pinctrl, MDIO, PHY, GEMAC, LSW, PCIe. Upstream review is the gate;
  land them in `drivers/` before the target.
- **Target and image recipe**: PR to `openwrt/openwrt` with the target, the image recipe, and
  the device profile. The recipe must encode the measured boot constraints, not just
  "uImage + dual-image": the loader (`bootcmd=mtd read kernel${bootflag}; bootfip`) accepts
  only a FIP package, the 8,650,752-byte kernel slot holds `uImage + the fixed 40,732-byte
  FIP/DTBO trailer`, a failed boot leaves a status word that makes `bootfip` reject the slot,
  and the trailer is vendor-derived and cannot currently be regenerated from published
  material. Solving that is part of the submission, not an afterthought.
- **Compliance**: the GPL request from Phase 3, plus `NOTICE.md` in this repository as the evidence
  trail. Track the vendor's response as part of the submission, not as an afterthought.
- **Device page**: the OpenWrt wiki entry with the hardware identity warning from
  `HARDWARE-IDENTITY.md` (the enclosure label says v1; the PCB is the v2/luofu design).

## Milestones

| # | milestone | gate |
| --- | --- | --- |
| M1 | hybrid image reproducible from a clean checkout | `tools/lab-src` + the parent RUNBOOK build lane + the patch script + the smoke test |
| M2 | `eth2` up with **no vendor network modules** | our MDIO + PHY + GEMAC, vendor stack removed - **NOT MET**: the bound PHY driver is the vendor's `hi_kphy` |
| M3 | LAN ports forward through our switch driver | our LSW, `hi_lsw_*` removed |
| M4 | WiFi decision executed | source received, or the blob documented, or the rewrite scoped |
| M5 | rootfs with no vendor userspace | standard OpenWrt init + packages |
| M6 | drivers accepted upstream | patch series merged in `drivers/` |
| M7 | target accepted in OpenWrt | PR merged, device page live |

M2 is the next real target: it removes the vendor's largest modules from the critical path and proves
the clean-room method end to end on the network, which is the part users care about.
