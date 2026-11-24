# Target, image recipe, config and install/flash status (2026-11-27)

**Scope.** The upstreamable platform side: the OpenWrt target skeleton, the image recipe, the
kernel-config audit, and the three install/flash paths with their exact hardware status.
Host-only; nothing in this file was flashed or changed by this lane.

## 1. Target skeleton - final state

* **L0 patch stack: 67 files** (001-060, 062-067 + committed `013a`).
* **Build-verified 2026-11-24** in an isolated OpenWrt main tree (27663a0, linux-6.18.60,
  OpenWrt GCC 14.4.0, 67 patches installed): `make defconfig` RC=0 with the device selected,
  kernel `prepare`/`compile` RC=0, **full image RC=0**. Command log and hashes:
  [../wifi-upstream-target/BUILD-VERIFICATION.md](../wifi-upstream-target/BUILD-VERIFICATION.md).
* **Fixes committed in the tree (2026-11-24):** `013a` endpoint-reset pulse (now committed;
  load-bearing on hardware), retired DTS `wifi-pcie-hog`, real boot-pair recorder enabled and
  the bench-only duplicate retired, `boot-selector` sysfs path fixed (the scripts had looked
  for `luofu-selector`), never-included profile sketch removed, "schematic/untested" headers
  replaced with the current bench/build status.
* Target files: `target/linux/hisilicon/{Makefile,Kconfig}`, `luofu/target.mk`,
  `luofu/config-6.18`, `image/{Makefile,luofu.mk,luofu-ubinize.cfg}`, single-source board DTS
  `dts/luofu-r116.dts` (702 lines, sha256 `df0d8e59...`, no hog, no `reset-gpios` yet),
  base-files with the A/B install path and running-pair recorder.
* **Not upstream-accepted.** The tree is the build source of the bench-verified line, not a
  public release; the target PR is a separate later artefact (see [NEXT-STEPS.md](NEXT-STEPS.md)).

## 2. Image recipe - final state

The recipe chain is real and host-builds end to end:

* kernel slot = legacy uImage (`-C none`) + FIP trailer, <= 8,650,752 B; gate magic
  `0xd7b7ab1e` at 0x40; trailer generated from **our** board DTB + loader-match overlay (the
  vendor 40,732-byte trailer is never used); load/entry derived from the loader staging base and
  asserted twice (symbol and assembled bytes);
* rootfs slot = squashfs in a static UBI volume `squashfs` plus an **empty** dynamic
  `rootfs_data` volume (the kernel formats/grows it on first read-write mount);
* sysupgrade = tar with CONTROL, kernel (FIP slice), root (rootfs UBI), `fip-manifest.txt`,
  `INSTALL-STATUS.txt`; **schema not frozen**;
* the container carries the provenance marker **"BOOT-VERIFIED RECIPE /
  NOT FLASHABLE-UNTIL-INSTALLED"** and deliberately has no factory/install stanza.

**Bench image identity (rebuilt byte-for-byte 2026-11-26):**

| member | value |
| --- | --- |
| sysupgrade container | 6,533,439 B sha256 `df6a78c3...` |
| kernel slice | 3,372,638 B sha256 `13e81214...`; full-slot hash `1794a531...` |
| rootfs UBI | sha256 `62344e80...` (squashfs `68450ee4...`) |
| rootfs ABI | apk db `V:kernel = 6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1` == tree vermagic == kmod `.apk` kernel= pin |
| rootfs packages | 81 packages (+7 over the previous image): iw 6.17, iwinfo, libiwinfo(+data), wireless-regdb, cfg80211 kmod + clean-room hi5622 driver snapshot; no autoload, no vendor firmware |

The rebuild reproduces the recorded image **byte-for-byte** from the current tree (serialized
world build, config restored byte-identically); from-bytes ABI verifier 25/25 PASS and the host
T0 transaction simulation 68/68 PASS. Artefacts are extracted to `build/abi-image-artifacts/`
(**gitignored local evidence**, never committed): container, kernel slice, rootfs UBI, squashfs,
seven `.apk` files, SHA256SUMS. Details: [../wifi-abi-image/README.md](../wifi-abi-image/README.md)
and `REPORT.md`; the flash procedure is
[../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md).

**Honest caveat:** the box currently reports kernel package `6.18.60~5de0e425...` because the
2026-11-25 install wrote the **kernel slice** but not the rootfs. The kernel slice itself is
unchanged; this is build-provenance bookkeeping in the rootfs apk db, not an ELF-ABI mismatch.
The canonical close is the one **kernel+rootfs-from-one-build** flash (section 4c).

## 3. Kernel config audit - final state

* `luofu/config-6.18` is a **derived OpenWrt delta** against `target/linux/generic/config-6.18`
  (311 delta lines / 567 with provenance comments), regenerated with
  `yes "" | make kernel_oldconfig`; no hand edits. `make defconfig` regenerates cleanly.
* **Bench-only delta:** `CONFIG_CFG80211=m` + resolved symbols and the clean-room driver's
  cfg80211 dependency (added 2026-11-24). This is a bench requirement, **not** an
  upstream-target default; packaging the Wi-Fi stack is an open target-PR decision.
* **A target PR must change** ([../wifi-upstream-target/CONFIG-AUDIT.md](../wifi-upstream-target/CONFIG-AUDIT.md)):
  1. `CONFIG_CMDLINE`/`CONFIG_CMDLINE_FORCE` currently hardcode `ubi.mtd=rootfsb` + every
     attended bring-up gate. An installable image must derive root from the booted slot and
     must not enable lab gates by default (this is also why only pair b can be written today).
  2. `CONFIG_DEVMEM=y` / `CONFIG_STRICT_DEVMEM=n` is a bench instrument; must not ship.
  3. decide the 6.18 default-enabled `ARCH_HISI` symbol set (disable explicitly or tighten
     Kconfig upstream).
  4. keep the bdinfo-nvmem mac-base dependency as an explicit target dependency (OpenWrt
     backport), not an undocumented config delta.
  5. decide `CONFIG_CFG80211_REQUIRE_SIGNED_REGDB` if a minimal target config is preferred.
* No bench diagnostic symbol is forced by the delta today; keep it that way.

## 4. Install / flash paths - status

**(a) T0 zero-flash userspace deploy - HARDWARE-VALIDATED PASS (2026-11-26).**
Five non-kmod packages (iw 6.17, iwinfo, libiwinfo, libiwinfo-data, wireless-regdb) installed
into the running pair-B rootfs via the persistent overlay: 49/49 gates PASS, host apply 5/5,
auto-rollback safe, receipt written, idempotent verify. Native `/usr/sbin/iw` and
`/usr/bin/iwinfo` run from the rootfs afterwards. Evidence: parent-repo ledger 2026-11-26
12:00 and `build/evidence-t0-deploy/`; lane: [../wifi-image-deploy/BENCH-VALIDATION.md](../wifi-image-deploy/BENCH-VALIDATION.md).
The **full-set** (kmod) T0 install correctly failed closed on the kernel-ABI gate - that is
path (c)'s job.

**(b) hostapd package deploy - HOST-VERIFIED, not executed by its lane.**
The AP-start bench needs hostapd, which the flashed image does not carry. The hostapd lane
builds the OpenWrt hostapd-mini v2.12 package set (four `.apk`; no `kernel=` dependency, so the
open kmod-ABI pin is untouched), provides a **T0-style zero-flash apk deploy** (rollback
`apk del`) and the exact AP-start ladder. Host gates: `--check` 88/88 PASS, lane selftest
14/14 PASS, ladder-consistency 107/107 PASS. The AP fires on 2026-11-26/27 also needed a
**manual bench workaround** (pushing the `/usr/share/hostap/*.uc` wifi-scripts files); the real
image fix is `CONFIG_PACKAGE_wifi-scripts=y` in a rebuilt image. Lane:
[../wifi-hostapd-deploy/README.md](../wifi-hostapd-deploy/README.md).

**(c) kernel+rootfs flash from one build - RUNBOOK READY, NOT EXECUTED.**
The canonical P1 close for the kmod ABI: write kernelb + rootfsb from the one rebuilt
container, verify uploads/read-backs/active-pair unchanged, set env `bootflag=b` + selector
cell `0x11` together, reboot, and require the **ABI receipt**
(`apk query --fields version kernel` -> `6.18.60~6f995278...-r1`). Preconditions: human at the
bench, recovery door armed, read-only UART capture, stock pair A as rollback. `/overlay` is
lost (fresh rootfs_data). Runbook: [../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md).

**(d) target `platform_do_upgrade` / release-image install path - UNPROVEN.**
The target's own upgrade path has never run end to end on a device; there is no factory image,
and the bench installs so far used the stock-assisted tooling on pair b (hardware-validated
kernel-slice writes 2026-11-25; the no-broker crafted-backup root channel 2026-11-23 is the
documented fallback). Project rule: the install path must run end to end on a recovered unit
**before** a target PR ships images. The first-install design and fail-closed tooling are
host-side only (parent-repo `tools/first-install-stock.sh`, [../../FIRST-INSTALL.md](../../FIRST-INSTALL.md)).
The container intentionally refuses to be treated as a factory/flashable image until then.

## 5. What remains on the platform image side (ordered)

1. **One flash** of the ABI image (path c) + ABI receipt + T0 full-set verify (fail-closed
   install of cfg80211 + driver kmods).
2. Rebuild the image with hostapd + `wifi-scripts` if the AP bench should be persistent
   (rootfs-only change; kernel pin unchanged).
3. Run the target `platform_do_upgrade` path end to end on a recovered unit (target-PR gate).
4. Target-PR config cleanup (section 3) and the DT `reset-gpios` fold
   ([DTS-0001-FINAL.md](DTS-0001-FINAL.md)).
5. Regenerate the release line to **56** patches (add 013a) and the review form v2
   ([UPSTREAM-SERIES-FINAL.md](UPSTREAM-SERIES-FINAL.md)).

## 6. Evidence

| claim | evidence |
| --- | --- |
| L0 = 67; isolated build RC=0 (defconfig/kernel/world) | [../wifi-upstream-target/BUILD-VERIFICATION.md](../wifi-upstream-target/BUILD-VERIFICATION.md) |
| 013a committed, hog retired, recorder/selector fixes | [../wifi-upstream-target/REPORT.md](../wifi-upstream-target/REPORT.md) 3.1/3.2; tree files |
| image byte-identical rebuild + ABI-correct rootfs | [../wifi-abi-image/REPORT.md](../wifi-abi-image/REPORT.md), `build-evidence/` |
| image identity hashes | [../wifi-image-integration/REPORT.md](../wifi-image-integration/REPORT.md); [../wifi-upstream-target/CFG80211-BENCH-IMAGE.md](../wifi-upstream-target/CFG80211-BENCH-IMAGE.md) |
| T0 userspace PASS 49/49 | parent-repo ledger 2026-11-26 12:00; [../wifi-image-deploy/BENCH-VALIDATION.md](../wifi-image-deploy/BENCH-VALIDATION.md) |
| hostapd deploy host gates 88/88 + 14/14 + 107/107 | [../wifi-hostapd-deploy/README.md](../wifi-hostapd-deploy/README.md); re-run recorded in [PUBLICATION-CHECKS.md](PUBLICATION-CHECKS.md) |
| config audit items | [../wifi-upstream-target/CONFIG-AUDIT.md](../wifi-upstream-target/CONFIG-AUDIT.md) |
| flash runbook, nothing executed | [../wifi-abi-image/FLASH-RUNBOOK.md](../wifi-abi-image/FLASH-RUNBOOK.md) |
| `platform_do_upgrade` unproven | `target-skeleton/target/linux/hisilicon/image/luofu.mk` header; [../wifi-upstream-target/REPORT.md](../wifi-upstream-target/REPORT.md) section 7 |
