# Deployment decision: what "latest OpenWrt" can mean on this board (2026-10-02)

This file answers the deployment half of the plan with evidence, and states the one deployment that
is both achievable and safe. Every claim carries its source: **[device]** (read from the live router
this session), **[repo]** (a file in this repo), or **[external]** (a public source, cited).

## 1. The board's identity is not in dispute

| fact | value | source |
| --- | --- | --- |
| device-tree compatible | `hsan-luofu` | **[device]** `/proc/device-tree/compatible` |
| device-tree model | `hsan luofu/R116` | **[device]** `/proc/device-tree/model` |
| vendor distro target | `hisilicon/luofu` | **[device]** `/etc/openwrt_release` |
| vendor base | `OpenWrt 22.03.6 r20265-f85a79bcb4`, revision `2.5.24` | **[device]** |
| arch | `arm_cortex-a9`, CPU part `0xc09` (Cortex-A9) | **[device]** `/proc/cpuinfo` |
| Wi-Fi silicon | Hi5622V100, two PCIe endpoints `59e7:0005` | **[repo]** `docs/DRIVER-BLACKBOX.md`, re-verified live |

Public documentation for the WR3000 **v2** is thin and partly wrong: forum answers describe the v2 as
"Triductor TR6560 + TR5220" [external: OpenWrt forum thread "Cudy WR3000E / Cudy WR3000 determining
version number"], which contradicts this unit's own device tree (`hsan luofu/R116`, HiSilicon) and its
Hi5622V100 PCIe Wi-Fi. The v1 (MediaTek MT7981B, target `mediatek/filogic`) is unrelated hardware that
happens to share the product name.

## 2. No OpenWrt target exists, and none is close

- No `hisilicon/luofu` target, no R116 board profile, and no Hi5622V100 driver exist upstream
  [external: searches over OpenWrt git history and the forum return only the *v1* MediaTek board
  (`mediatek/filogic`, added 2023-05-17 by David Bauer)].
- [repo] `docs/PORT-PLAN.md` reached the same conclusion independently and enumerates what a port
  would require: new clock, pinctrl, PCIe host, NAND, Ethernet MAC and switch drivers plus a device
  tree, and an entirely new Wi-Fi driver and radio firmware.
- The kernel ABI forbids the shortcut: the shipped modules are `vermagic=5.10.201`
  [repo + measured], and Linux has no stable module ABI, so vendor binaries cannot load on a modern
  (6.12) OpenWrt kernel.

**Therefore "deploy the latest OpenWrt from source" is not a deployable outcome on this hardware.**
Anyone who claims otherwise for this SoC has not looked at the device tree.

## 3. What the reversing has established (and what it has not)

The Wi-Fi chip is **releasable and controllable to a defined point** [repo: `mem-entries.md`,
`docs/phase19`-`phase22`, and the phase-22d run committed as `643fa1a`]:

- released by `0x00005a5a` -> device CA `0x40000108`; BSS zeroed; firmware runs [measured];
- the HCC mailbox, the SR/DR rings, the ETE block and the device->host iATU window are all mapped and
  matched to a live vendor boot [measured];
- both endpoints claimable at once; EP0's outbound window programmed to vendor values; the SR
  descriptor fetch **works** (`SR+0x1c = 0x400`) [measured, phase-22d];
- the host half of the message service (ack / clear / re-arm CAs, the id-6 wake) runs against real
  device traffic [measured].

What is **not** solved, and is now known to be device-side rather than a register poke: the firmware's
own H2D dispatcher is never entered, `out[0]` is never cleared by the device, no id-1 reply or
payload ever appears, and the firmware's ready dialogue does not advance
[repo: `docs/phase22/h2d-accept.md`, corroborated by the phase-22d run]. The vendor's ready handshake
contains **no host->device write** to answer with [repo: `docs/phase19/fw-handshake.md`], so this
cannot be closed by inventing a reply; it needs the vendor's message-context/ISR binding reproduced
(its source, or a firmware-level trace).

**Consequence for the goal:** "fully reversed" in the sense of a from-scratch open driver + firmware
is a research programme, not a delivery. Reversing has instead produced something more valuable for
deployment: a complete map of what the vendor firmware does on this board, which is exactly what a
custom firmware layer must not break.

## 4. The deployment that is achievable, and its risks

Deploy a **current, hardened custom firmware image to slot B** on the vendor base, keeping slot A
stock:

1. base = the newest Cudy release available for this board (currently 2.5.24, already running);
2. the proven layer: root SSH (dropbear), package manager with signed feeds, the systime injection
   closed, calibration toolkit, marker file;
3. refresh the layer (this loop): re-verify the injection fix on the live build, confirm the package
   set, take a calibration snapshot, and re-run the flash/verify/rollback drill so the deployment is
   reproducible rather than historical.

Hard rules for that deployment, unchanged [repo: `docs/FLASH-PLAN.md`, `docs/HAZARDS.md`]:
full dumps before any write (held: mtd0-mtd16), **one slot always stock**, kernel slot writes must
write the whole 8,650,752-byte slice, `scp -O` only, UART is the last resort, and the slot selector is
`boot_reg` (`0x10` = A, `0x21` = B) plus **both** env copies.

## 5. What this file commits the loop to

- Continue the **reversing** only where it is cheap and load-bearing for firmware work (the next
  meaningful step is on the firmware side, not another register sweep).
- Deliver the **custom firmware layer deployment** on slot B, verified end to end, with slot A stock
  as the rollback, and no claim that it is mainline OpenWrt - it is a current vendor base with our
  hardening and access layer, which is the "latest version of OpenWrt" that this hardware admits.
