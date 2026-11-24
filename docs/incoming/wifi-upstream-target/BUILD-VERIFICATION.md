# Build verification - target tree checkpoint + cfg80211 bench kernel

Host-only, 2026-11-24. Tree: OpenWrt main commit 27663a0, linux-6.18.60, OpenWrt GCC
14.4.0. Two tree states were used, both derived from the release-line build tree
/home/shibbityshwab/openwrt-lab-selectorfix-img (written below as OWRT):

- A: isolated reflink copy of OWRT, target replaced with this commit's tree, no
  cfg80211 change - used for the defconfig + kernel prepare/compile proof.
- B: OWRT itself, target replaced with this commit's tree - used for the full image
  builds (a copied prebuilt tree cannot rebuild its host tools: their cached build
  stamps bake absolute paths; see section 6).

Full command logs are in /home/shibbityshwab/upstream-target-*.log (local evidence,
not committed). This file records the commands, results and hashes.

## 1. Target installation / metadata

- Installed: target/linux/hisilicon/* (67 patch files = 001-060 + 062-067 + 013a),
  board DTS in target/linux/hisilicon/dts/, tools to scripts/.
- make defconfig (state A): **RC=0**, no target warnings, and the selections are
  regenerated:
  CONFIG_TARGET_hisilicon=y, CONFIG_TARGET_hisilicon_luofu=y,
  CONFIG_TARGET_hisilicon_luofu_DEVICE_cudy_wr3000-v2=y,
  CONFIG_TARGET_BOARD="hisilicon", CONFIG_TARGET_SUBTARGET="luofu",
  CONFIG_TARGET_PROFILE="DEVICE_cudy_wr3000-v2".
- The earlier "configuration is out of sync" warning in the copy disappeared after the
  defconfig run; the release-line tree (state B) prints the warning because a wifi-lane
  package was added/removed from its package/ dir outside this lane - it does not
  affect the target kernel build.

## 2. Kernel build (state A, no cfg80211 yet)

- make target/linux/prepare: **RC=0** (67/67 patches apply, no rejects).
- make target/linux/compile: **RC=0**.
- Products: vmlinux 57,659,036 B sha256 0ba3c95185ae518dfdc9072527d7784e32319be4e38e171f1c6ec66e8621477c;
  zImage 3,325,728 B sha256 0a37fd1d8959fb3f6fa1da2a0da6a8fbddab3f514d71f104d3a367ffa477b126;
  System.map sha256 ed58bb57eaf0e554f5c62337d15fb1e390a36acff6beef613477de1b0b1e4c88.
- Patch application spot checks: patch 013a present in the compiled source
  (drivers/pci/controller/dwc/pcie-luofu.c carries LUOFU_EP_GPIO_BASE and
  luofu_pcie_ep_reset_pulse); zero *.rej files in the kernel tree; 67 patch files in
  the target dir.

## 3. Full image build (state B, checkpoint target, before cfg80211)

- make -j16 (world): **RC=0**.
- Artifacts:
  - sysupgrade container: 6,246,719 B, sha256 77de5fe9a161d9949ab9a5cc4bba7553b90d0d15c004260bb3083e4303d2f0cb
    (a later rebuild with the corrected INSTALL-STATUS text produced f7718860..., see
    section 4; the final image is section 5's 22b72852...);
  - base DTB: image-luofu-r116.dtb 12,728 B sha256 36034465c49f20588c4292a21858672bf277ca509260033b1c720cb84be34e49
    (8-byte aligned; compiled from the committed DTS with the Wi-Fi GPIO hog retired);
  - loader overlay: image-luofu-r116-overlay.dtbo 230 B sha256 8e4ab83b575dc14d9f3131dc01d1cbdccc63b38f8f8c4fa774da0d1f200b23b6;
  - FIP kernel slice: 3,352,486 B sha256 a5d3ceb191cf707697c1c4718e5967a99af3da0fc0ad88bc06de13e7eb8f2d9a,
    uImage load/entry 0x80608000, gate magic 0xd7b7ab1e at 0x332680, slot limit
    8,650,752 respected (fip-manifest.txt inside the container);
  - DTB check: strings on the compiled DTB contain 0 wifi-pcie-hog/wifi-pcie-ctl
    entries and 2 boot-selector entries (the retired hog is gone, the selector node is
    present).
- Rootfs check (installed root-hisilicon):
  - /etc/rc.d/S96luofu_record_pair -> ../init.d/luofu_record_pair (the validated
    recorder is enabled);
  - no /etc/init.d/luofu-pair-record and no S01/K99luofu-pair-record (bench duplicate
    retired);
  - installed /etc/init.d/luofu_record_pair contains _selector_find (the boot-selector
    path fix).
- Container check: tar members CONTROL, INSTALL-STATUS.txt, fip-manifest.txt, kernel,
  root; INSTALL-STATUS carries the NOT FLASHABLE-UNTIL-INSTALLED marker with the
  updated 2026-11-24 boot-evidence text.

## 4. Provenance rebuild (state B)

- Rebuilt after the INSTALL-STATUS text was updated: RC=0, container 6,246,719 B
  sha256 f7718860f4dad68cc3366223f6fb67e7858785badb48fe9ee1493d1a808df81f.
- Superseded by section 5 (the cfg80211 kernel changes the image again).

## 5. cfg80211 bench kernel + final image (state B, 2026-11-24)

Config: target/linux/hisilicon/luofu/config-6.18 regenerated (canonical
'yes "" | make kernel_oldconfig' derivation) and extended with the cfg80211 bench
delta (CONFIG_CFG80211=m and its resolved symbols; details in CONFIG-AUDIT.md and
CFG80211-BENCH-IMAGE.md).

- Reproducibility test: removed the kernel .config/.configured and rebuilt from the
  committed config delta - make target/linux/prepare **RC=0**, make
  target/linux/compile **RC=0**, **zero "NEW" config prompts, zero "Restart config"
  sections**.
- Kernel products:
  - vmlinux 58,050,272 B sha256 a7ec7ba575cecf912c0caa0fb98ce6f3f6c7b2a6874c0c1618547600949f6356;
  - zImage 3,346,824 B sha256 5cfc1632e0b164ee5a112cf1fa74a7bc44c517e54173cec2680c7764c3e2ea8d;
  - net/wireless/cfg80211.ko 2,328,548 B sha256
    949393f9676cc7bab18def92f1f96a07c45bd0ff599ee418c16ba7d54715170c,
    vermagic 6.18.60 SMP mod_unload ARMv7 p2v8;
  - Module.symvers present and exports the six cfg80211 symbols the out-of-tree
    driver imports (wiphy_new_nm, wiphy_register, wiphy_unregister, wiphy_free,
    cfg80211_register_netdevice, cfg80211_unregister_wdev).
  - kernel .config: CONFIG_CFG80211=m, CONFIG_WLAN=y, CONFIG_RFKILL=y,
    CFG80211_REQUIRE_SIGNED_REGDB=y; include/linux/netdevice.h carries ieee80211_ptr.
- Final image: make -j16 **RC=0**; sysupgrade container 6,267,199 B sha256
  22b72852ff2415a6d951aadc18206f88e3402dd6661252df853f48dffde95c22 (manifest sha256
  335c79b0edd8ab88c1c7c78b9b149771285fa38848abed5e046a2ebe69b18aeb); the kernel slice
  inside it is 3,372,638 B sha256 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f
  (full-slot hash for bench-push-kernel.sh: 1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e).
- Out-of-tree driver re-check against this tree (wifi-cfg80211 lane tools/build.sh,
  re-run at the end of the session): host selftests PASS, kernel module build PASS
  (505,076 B sha256 d2b3cf7917df1a7439c5dad1badd83bf97d627ae54ca9f5db7e34216edc2edaa,
  depends=cfg80211, 0 compiler/modpost warnings). The script's final provider-symbols
  check now exits 1 by design: the real tree Module.symvers already exports those six
  symbols, so the lane's cfg80211-provider.symvers workaround is redundant and
  should be removed / regenerated (lane follow-up; the compile itself is clean).

## 6. Tree-state notes and concurrency

- **Validated tree state:** /home/shibbityshwab/openwrt-lab-selectorfix-img with
  target/linux/hisilicon = this commit's tree (67 patches, DTS, image recipe) and the
  cfg80211 config delta. Its kernel build dir has vmlinux + Module.symvers + all
  modules; that is the state the wifi-cfg80211 lane's module build was re-run against.
- The same tree's target/linux/generic/config-6.18 has a pre-existing local
  modification (CONFIG_DEVMEM=y) from another lane; it is NOT committed by this
  session and our config delta carries CONFIG_DEVMEM=y explicitly so a pristine tree
  gets the same release-line value.
- This lane ran full 'make world' builds in the shared GP1 tree while the
  wifi-cfg80211 lane was verifying its module, which transiently removed
  vmlinux/Module.symvers and broke its check mid-run. Acknowledged; the check was
  re-run at the end (section 5). Recommendation: serialize 'make world' in a tree
  another lane is compiling against, or give each lane its own tree.
- The isolated reflink copy (state A) cannot run a full world build: OpenWrt host
  packages cache absolute build paths in their stamps/ninja files, so a copied tree's
  hostpkg rebuild fails (package/system/apk host compile: stale -I paths from the
  original tree). This is a tree-copy artefact, not a target defect; kernel
  prepare/compile (the target-specific part) is verified in both locations.

## 7. Repro commands

~~~
# host verification (state B), after syncing target-skeleton into the tree:
make defconfig
make -j16 target/linux/prepare
make -j16 target/linux/compile
make -j16
sha256sum bin/targets/hisilicon/luofu/*.bin
tar -xOf bin/targets/hisilicon/luofu/*.sysupgrade.bin sysupgrade-cudy_wr3000-v2/kernel > /tmp/kernel.bin
~~~

The .config/defconfig proof (state A):
~~~
grep -E 'CONFIG_TARGET_hisilicon|CONFIG_TARGET_PROFILE|CONFIG_TARGET_SUBTARGET' .config
~~~
