# wifi-upstream-target - lane report (task-107), 2026-11-24

**Lane:** upstream-target (OpenWrt target + kernel series readiness).
**Scope:** host-only. No router, bench link, UART, flash, TFTP or vendor binary was
touched. **Nothing here is upstream acceptance.**
**Deliverables:** this report, BUILD-VERIFICATION.md, PATCH-CLASSIFICATION.md,
submission-series.md, CONFIG-AUDIT.md, dts-endpoint-proposal.dtsi; real-tree fixes in
opensource/target-skeleton/ and an updated docs/ROADMAP-OPENWRT.md.

## 1. Result in one paragraph

The target was reviewed against the current hardware record, its stale "schematic /
untested" caveats were replaced with the verified status, and the host-fixable defects
found in review were fixed. The two important fixes are (a) the GP1 endpoint-reset
patch (013a) and its DTS hog retirement, which existed only in the working tree, are now
committed, and (b) the boot-pair recorder was never actually enabled on the unit: a
bench-only duplicate was doing the job and the real recorder + platform.sh looked for
the wrong sysfs name (luofu-selector instead of the hardware boot-selector). After the
fix the real recorder is enabled, the duplicate is gone, both scripts resolve the
hardware path, and the mocked harness passes 94/94. The final target tree was then
build-verified in an isolated OpenWrt main tree (make defconfig, kernel
prepare/compile, full image build - see BUILD-VERIFICATION.md), and all 67 target
patches were classified for submission: 26 keep (7 regenerated with folded hunks),
17 fold, 24 drop -> a proposed **30-patch kernel series + DTS-0001** with commit-message
drafts and acceptance criteria. Remaining blockers are unchanged and are stated in
section 9: Wi-Fi has no upstreamable driver and its firmware is closed, the PD4 firmware
patch is research-only, the install path has not run end to end on a device, and the
opensource history purge is still pending.

## 2. Starting point (already true before this lane)

- Parent STATUS.md 3b-3j: bench line on the 6.18 target; GP1 endpoint reset pulse
  hardware-validated; full duplex HCC protocol working on the clean-room host
  (golden boot -> ready handshake -> context-create -> TXQ config sync -> firmware
  responses g1 id3/g1 id0x12), using the research-only PD4 firmware patch.
- target-skeleton: 66 tracked patches + 013a untracked in the working tree; board DTS
  702 lines; image chain with uImage/FIP/UBI/sysupgrade stages; base-files with the
  A/B install path and the pair recorder; upstream review form frozen at 26 patches
  (series-6.18) + DTS-0001; L1 release-line manifest 55 patches.
- Uncommitted working-tree delta from the GP1 session: the 013a patch itself and the
  DTS removal of wifi-pcie-hog (both were in the tree the GP1 image was built from).
- Known caveats in the docs: the target was still described as "schematic/untested"
  in several file headers and in the roadmap top status; the roadmap predated the
  Wi-Fi protocol result.

## 3. Fixes made in the real tree (target-skeleton)

### 3.1 GP1 delta committed (013a + DTS hog retirement)
- Added target/linux/hisilicon/patches-6.18/013a-arm-pci-luofu-endpoint-reset-pulse.patch
  (untracked before; sha256 d4b544a398099e23793e39e4cebc9b81533b749bea1498cae9ef3d5f4636386b):
  at RC0 probe, before the RC clock is enabled, drive SoC GPIO0 pins 20/21/28 LOW for
  20 ms then HIGH for 100 ms (RMW on 0x10106000 DATA/+0x04 DDR). This is the GP1
  sequence recorded in parent STATUS.md 3c: without it the chip's mask ROM never runs
  S0/S1; with it the handshake page reaches 0x86807FEF/2/0xABCD and both RC links train.
- Board DTS: removed the wifi-pcie-hog (the driver owns the pulse now; a hog releasing
  the line before the driver's assert defeats the pulse). DTS header updated with the
  boot-tested baselines (6e9c54d0 loader hand-off + the 2026-11-24 GP1 line).
- Evidence: GP1 bench record (parent STATUS.md 3c, dmesg "endpoint reset pulse" on
  every boot); the GP1 build tree's DTS/config are byte-identical to this working tree
  (verified by diff against openwrt-lab-selectorfix-img before the commit).
- Upstream note: the hardcoded GPIO base in 013a is bench-acceptable but not upstream
  form; the fold into review-form 0013 must move it to DT data (reset-gpios). The DTS
  change itself is an active-DTS change and was boot-tested by GP1.

### 3.2 Boot-pair recorder wiring and selector path (real bug found)
- **Symptom/root cause 1:** the image shipped two recorders. The validated
  base-files/etc/init.d/luofu_record_pair (START=96, two-witness, read-back verified)
  had **no rc.d symlink**, so it never ran; the bench-only
  base-files/etc/init.d/luofu-pair-record (hardcoded slot=b, no validation) was enabled
  as S01 and did the recording. RELEASE-NOTES/RELEASE history credited the validated
  recorder, but the running code was the hack. Fixed: duplicate recorder and its
  S01/K99 links removed; S96luofu_record_pair shipped; the validated recorder's header
  now matches reality.
- **Symptom/root cause 2:** both the recorder and base-files/lib/upgrade/platform.sh
  looked for /sys/devices/platform/**luofu-selector**/current_pair. The platform device
  is named from the devicetree node, which is **boot-selector**; the bench tools and the
  parent ledger use /sys/devices/platform/boot-selector/... (evidence: tools/
  bench-push-kernel.sh, tools/bench-install.sh, ledger 2026-11-23 11:10/18:40). So even
  when the real recorder ran, it could not find the selector and left the boot cell
  untouched. Fixed with _selector_find()/_luofu_selector_dir() discovery: boot-selector
  primary (hardware truth), luofu-selector fallback (older fixtures/drafts only).
- Tests: tools/test-platform-abinstall.sh fixture moved to the hardware path; recorder
  and platform tests pass 94/94. test-mac-provisioning.sh 103/103 and
  test-mac-nvmem.sh 77/77 still pass. **One attended reboot is still required** to
  hardware-confirm that the recorder writes the pair on the unit (host-only lane).
- Note for the next review: patch 017's sysfs ABI text and the driver platform name
  should be aligned with the boot-selector device path during the submission cleanup.

### 3.3 Documentation truth
- target/linux/hisilicon/Makefile, Kconfig, luofu/target.mk, image/Makefile and
  image/luofu.mk: the "SCHEMATIC / UNTESTED" headers replaced with the 2026-11-24
  status (build source of the bench-verified line; NOT upstream-accepted, NOT a
  flashable release; image chain boots; install path unproven). Kconfig explicitly
  marked "review stub, not used by the build; submission recommendation: delete".
- Removed the never-included profile sketch luofu/profiles/cudy_wr3000-v2.mk.unused
  (modern OpenWrt builds from the Device/ stanza in image/Makefile).
- target-skeleton/README.md + RELEASE-NOTES.md: new 2026-11-24 status blocks (history
  kept); docs/ROADMAP-OPENWRT.md: new 2026-11-24 section and a corrected top status.
- Not changed: docs/TARGET-DESIGN.md, docs/upstream/SUBMISSION-STATUS.md (owned by
  earlier lanes; the new lane docs point to them).

### 3.4 DTS audit against the task checklist
| checklist item | state in dts/luofu-r116.dts | action |
| --- | --- | --- |
| serial/UART | uart0 (disabled) + uart1 (console), aliases serial0/serial1/console, chosen stdout-path | present, no change |
| ethernet + internal switch | gmac0-4, mdio0 + PHY2-5, lsw + 4 ethernet-ports, pie | present, no change |
| NAND/FMC | fmc SPI-MEM + spi-nand child + 17 fixed partitions incl bdinfo read-only | present, no change |
| PCIe RC | pcie0 + pcie1, ranges/dma-ranges, clocks, resets, acp-gate | present, no change (GP1 delta retired the hog) |
| Hi5622 endpoint children | **absent on purpose**: no binding and no driver; PCI endpoints bind by ID | documented + proposal only (dts-endpoint-proposal.dtsi) |
| pinctrl/thermal/CRG/watchdog | pinctrl, tvsensor + cls0-thermal, crg syscon+gate/clock tree, wdt child | present, no change |
| boot-selector/sysctrl/reboot | sysctrl syscon, reboot, boot-selector | present; scripts fixed to its real device name |

Adding disabled endpoint nodes to the active DTS would be an active-DTS change with no
driver to use it and no binding to validate it; that is why it is a proposal file, not
a change. The real upstream dependency is the Wi-Fi driver decision (section 9).

## 4. Build verification (isolated tree)

See BUILD-VERIFICATION.md for the full command log, artifact list and hashes. Summary:

- OpenWrt main commit 27663a0, linux-6.18.60, OpenWrt GCC 14.4.0; tree copied from the
  GP1 build tree with reflink (isolated, .config untouched), target/dts/tools replaced
  with this commit's files, 67 patches installed.
- **make defconfig: RC=0**, no out-of-sync warning, target/device selections present.
- **make target/linux/prepare: RC=0; make target/linux/compile: RC=0**, full
  vmlinux/zImage/System.map produced; 013a present in the compiled source.
- **make -j16 (world/image): RC=0**, sysupgrade image produced and the image stages
  (FIP slice, rootfs UBI, sysupgrade container) re-validated from their own bytes.
- The rebuilt rootfs carries S96luofu_record_pair and no bench-only recorder.

## 5. Patch series classification for submission

Full table with per-patch verdicts and reasons: PATCH-CLASSIFICATION.md. Counts of the
67 L0 files: **26 KEEP** (001-026 review-form base; 0009, 0012, 0013, 0019, 0020, 0022,
0024 are regenerated with folds), **17 FOLD** (013a, 027, 031, 032, 036, 038, 041, 043,
044, 046, 048, 051, 053, 059, 060, 066, 067), **24 DROP** (bench-only SMP/PIE/LSW
diagnostics, probes and CPU-copy workarounds; they stay in the L0 lab stack, some stay
in L1 for the bench image).

Proposed v2 submission: **30 kernel patches + DTS-0001**, ordered with commit-message
drafts and acceptance criteria: submission-series.md. The frozen review form
(upstream/series-6.18/, 26 patches + DTS-0001) is unchanged by this lane: regenerating
it requires applying the fold decisions to the target patches and re-running the
validation pipeline.

Required release-line delta (do not forget): **013a must be added to L1** (55 -> 56)
in release-line/INCLUDED.txt + manifest.tsv and the validator re-run; DTS-0001 must be
regenerated because the active DTS changed with GP1 (the hog is gone).

## 6. Defconfig / config audit

make defconfig is clean and the board selections regenerate; the release-line config is
a derived delta (311 lines) and needs no change for the image to build. A submission
target PR must remove the bench configuration from CONFIG_CMDLINE/CMDLINE_FORCE
(ubi.mtd=rootfsb + all luofu_* gates), CONFIG_DEVMEM/STRICT_DEVMEM=n, and decide the
"default ARCH_HISI" symbol set. Details and reproduction: CONFIG-AUDIT.md.

## 7. Recipe

The image chain (image/luofu.mk + image/Makefile + tools/{fip-slice,luofu-image,
pad-dtb}.py + scripts/ mapping) is real and was rebuilt end to end in this session.
The container is intentionally marked NOT FLASHABLE-UNTIL-INSTALLED: there is no
factory image, platform_do_upgrade has never run end to end on a device, and the
hardcoded ubi.mtd=rootfsb cmdline is why only pair b can be written today. This is a
target-PR blocker, separate from the kernel series.

## 8. Changes for the real tree vs the review form

- **Real tree (this lane's commits):** target-skeleton/* (DTS, 013a patch, base-files
  recorder/selector, rc.d, tests, headers, README/RELEASE-NOTES), docs/ROADMAP-OPENWRT.md,
  docs/SKELETON-SMOKE.md session record, and this lane directory.
- **Review form (upstream/series-6.18/): NOT modified.** Required follow-ups listed in
  submission-series.md: folds into 0009/0012/0013/0019/0020/0022/0024, four new RFC
  entries 0027-0030, DTS-0001 regeneration, L1 013a addition, full re-validation.
- **Release line:** unchanged artifacts; only the working-tree GP1 delta was committed
  (it was already in the GP1 build tree, so no new image claim).

## 9. Remaining blockers and honest boundaries

1. **Wi-Fi is not upstreamable.** The clean-room transport + full duplex HCC protocol
   work on the bench with a research-only patched firmware (PD4); the firmware blob is
   closed, the GPL source request is unanswered, and the PD4 patch must never ship.
   A mainline Wi-Fi driver needs the mac80211/data-path work in the Wi-Fi lanes; the
   host protocol is readiness evidence, not a driver.
2. **Install path.** platform_do_upgrade/first-install never ran end to end on a device;
   the image is installed manually from stock. Required before a target PR ships images.
3. **Hardware confirmation of this session's base-files change** (recorder/selector) -
   one attended reboot with the recovery door armed.
4. **Submission form stale** at v1 (26 patches); the v2 plan exists but is not generated.
5. **Publication NOT READY** (history purge pending); do not present the opensource
   submodule as a submission-ready repository.

## 10. Evidence index

| evidence | location |
| --- | --- |
| build logs + hashes, artifact list | BUILD-VERIFICATION.md (this lane) |
| patch table + submission series | PATCH-CLASSIFICATION.md, submission-series.md |
| defconfig evidence | CONFIG-AUDIT.md |
| GP1 record | parent STATUS.md 3c (+ ledger 2026-11-24) |
| full duplex Wi-Fi protocol record | parent STATUS.md 3j (+ ledger 2026-11-25 05:10) |
| selector device-name evidence | parent ledger 2026-11-23 11:10/18:40, tools/bench-push-kernel.sh, tools/bench-install.sh |
| 013a hash | d4b544a398099e23793e39e4cebc9b81533b749bea1498cae9ef3d5f4636386b |
| target selftests | 94/94 + 103/103 + 77/77 (commands in section 3.2) |
| roadmap update | ../../ROADMAP-OPENWRT.md "Where the plan stands on 2026-11-24" |
| session build record | ../../SKELETON-SMOKE.md "2026-11-24 upstream-target session" |

## 11. Limitations of this report

- All hardware statements are quoted from earlier dated records (parent STATUS.md and
  the ledger); this lane did not touch the device.
- The image hashes recorded in BUILD-VERIFICATION.md are host-build artifacts, not
  hardware-validated images; the last hardware-validated baselines remain 974e6ca6 /
  7f270b8c / 6e9c54d0 and the 2026-11-24 GP1/PD4 line.
- The fold decisions are engineering judgement for review; they are not upstream
  approval and no patch was sent anywhere.

## 12. Handoff executed: cfg80211 bench kernel (wifi-cfg80211 task-104)

Added after the main report was written; see CFG80211-BENCH-IMAGE.md for the exact
rebuild/deploy/flash procedure and BUILD-VERIFICATION.md section 5 for evidence.

- The cfg80211 config fragment from ../../wifi-cfg80211/cfg80211.config is now in the
  target kernel config (CONFIG_CFG80211=m and the resolved symbols). The config file
  was regenerated with the canonical command ('yes "" | make kernel_oldconfig') and
  then rebuilt from scratch to prove the delta is prompt-free (prepare+compile RC=0).
- Verified: net/wireless/cfg80211.ko builds (2,328,548 B sha256 949393f9..., vermagic
  6.18.60), the kernel Module.symvers exports the six driver imports, and a full image
  builds (container 6,267,199 B sha256 22b72852..., kernel slice sha256 13e81214...,
  slot hash 1794a531...). The clean-room hi5622_wifi.ko rebuilt against this tree
  cleanly (505,076 B sha256 d2b3cf79..., depends=cfg80211, 0 warnings); its lane's
  redundant cfg80211-provider.symvers check now reports the real exports as
  duplicates and must be retired (their follow-up).
- Bench flow (attended, recovery armed): tools/bench-push-kernel.sh with the extracted
  kernel slice --slot a (box runs pair b; rootfs b is untouched and the fixed pair
  recorder returns the box to the old kernelb on a later reset), then insmod
  /tmp/cfg80211.ko + /tmp/hi5622_wifi.ko. Nothing was flashed or booted by this lane.
- Tree/concurrency: the world builds were run in the shared GP1 release tree; that
  disrupted the cfg80211 lane's mid-run module check (transient missing
  vmlinux/Module.symvers). The check was re-run at the end against the final tree
  (see BUILD-VERIFICATION section 5/6 and the recommendation to serialize world
  builds). The validated tree state is recorded there.
- This is a BENCH config addition: it is not part of the upstream kernel series and
  the target-PR config decision (cfg80211 full-MAC + package dependency or backports)
  is still open (CONFIG-AUDIT.md).
