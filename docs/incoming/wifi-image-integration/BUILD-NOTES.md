# BUILD-NOTES - bench image build records (wifi-image-integration, task-112/132)

**Round 3 (FINAL SELF-CONTAINED IMAGE) recorded build:** 2026-11-27 (local
UTC+07), lane img-integ, host-only. **Result:** `make -j16` RC=0 in 12 s
(kernel compile skipped: kernel `.config` unchanged), verifier PASS, shared
tree restored. Round 3 is at the bottom of this file.

**Round 2 (ABI image) recorded build:** 2026-11-24, `make -j16` RC=0 in 153 s;
verifier PASS; shared tree restored. Evidence archived under
`build-evidence/round2-abi-image-df6a78c3/`.

## Tree and inputs

| item | value |
| --- | --- |
| OpenWrt tree | `/home/shibbityshwab/openwrt-lab-selectorfix-img` (shared release-line tree) |
| tree git HEAD | `27663a0` |
| target | `hisilicon/luofu`, profile `DEVICE_cudy_wr3000-v2` |
| kernel | linux-6.18.60, OpenWrt GCC 14.4.0 (arm_cortex-a9 musl-eabi) |
| target kernel config | `target/linux/hisilicon/luofu/config-6.18` sha256 `ecf1aae26a527737ac94ad62392181e92be1cffe5c03d0f46c16b05cdc9be41f` (unchanged before/after) |
| driver source | `docs/incoming/wifi-cfg80211/src` -> package snapshot, 29 files, manifest sha256 `10582bb49af68c8360d569d88e59fd994d370e76e4440e48fc24cc856af948ca` |
| package | `package/hi5622-wifi/` (installed into the tree by `tools/install-into-tree.sh`, untracked in the tree) |
| pre-build `.config` sha256 | `9052d5e36fd30e6c59276910f2b286d5a109a5dffd03f3a8e1386aece0ed075d` |
| resolved `.config` sha256 | `b377f6c9edfcc41e51061ee6b0c8ad0702b82dc8d5bd50a449ff301a29a0f932` |
| kernel `.config` sha256 before/after | `8ddb783d...` -> `08b6e595...` (== the hardware-validated build's kernel config) |
| kernel symbols carried over | `kernel-symbols.config` sha256 `d2f12ad2aca24e5d1b1c1eeb81401ad0746664ce69d9f8c8f17abd21ab487bc0` |

## Command line

    OWRT=/home/shibbityshwab/openwrt-lab-selectorfix-img
    ./tools/install-into-tree.sh --owrt "$OWRT" --force
    cat config-delta.config >> "$OWRT/.config"
    (cd "$OWRT" && make defconfig)
    cat kernel-symbols.config >> "$OWRT/.config"     # see Finding F1 (REPORT.md)
    (cd "$OWRT" && make -j16)

`tools/build-image.sh` performs exactly this sequence, then runs
`tools/verify-image.sh`, writes `build-evidence/`, restores `.config`
byte-identically and restores the rootfs staging tree. It refuses to start if
a make process is working in the tree and holds an flock on
`/tmp/cudy-openwrt-world-build.lock` for the duration (no concurrent world
builds).

## Result artefacts

| artefact | size | sha256 |
| --- | --- | --- |
| sysupgrade container (`openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin`) | 6,533,439 B | `df6a78c3717b0f792391e7f7d488a9f2850c9640553ef391163dd34abdb47310` |
| kernel slice (container member `kernel`) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` (UNCHANGED, hardware-validated) |
| rootfs UBI (container member `root`) | 3,145,728 B | `62344e8040f4b17ae8d3a9843dd6a57ec35f55636cd3aa8e868b605951642746` |
| rootfs squashfs (build_dir) | 2,753,136 B | `68450ee4730302747508b94ef45875c433e48b14c1540975ea2143ae72d3af44` |
| rootfs squashfs volume inside the UBI (padded) | 2,756,608 B | `f58ee016098f53d57bcc8fd0d5c5e5e16157f1e35f8ce343042954fdca185e18` |
| `cfg80211.ko` tree artifact / installed | 2,328,548 B / 406,028 B | `949393f9676cc7bab18def92f1f96a07c45bd0ff599ee418c16ba7d54715170c` / `11a5e2b57f31e07b3e692ac084c2aa278bc5d1e5485e7c51e6cb114cfe57c46a` |
| `hi5622_wifi.ko` package build / installed | 505,748 B / 133,884 B | `340586180860c326ef1e5a7fd96c8851acaa953e828c10f67c3ddd856c0cdad1` / `e0608fa6e1489035d58febce279a13baf6988a5664b0404e1135d0b53d124094` |
| `iw` | 116,855 B | `e0f4ba8cb1dd8f748924f7333475f6d4120bc0b9290d1cf67df1be8687accb23` |
| `iwinfo` (C CLI) | 16,906 B | `88fb49430a9a0b5095f10eacf1ea63a6c8906b450a71b01bfebfc01ba959cb4c` |
| `libiwinfo.so.20230701` | 57,377 B | `aa35d698dfb5ec20ff343bde48f10f6cc055aeaeda62630f4615847bc3c2a806` |
| `libiwinfo-data/devices.txt` | 18,231 B | `dcaaa350ccc73b16341fc792d712f5b45c546344c3dc7457d141c5f2b3a804f3` |
| `libnl-tiny.so.1` (already in baseline) | 29,034 B | `d7d73dbd242c45416faa29e557ccb976a309fb90808c37f9521265cb1b389916` |
| `regulatory.db` | 6,332 B | `c682b8e0f2f61385929586409f092638ba84dd795503d220be15b5c3d16dd75d` |

Image manifest: 81 packages (baseline 74 + the 7 additions, see REPORT.md
section 1). Full per-file evidence: `image-verify.txt`.

Deployable .apk files produced by the same build:

| package | size | sha256 |
| --- | --- | --- |
| `iw-6.17-r1.apk` | 52,997 B | `9b46713fb5a0e866e7b1fb919f2a30eca4bc01d9aeabf49fa50b20faedd69f08` |
| `iwinfo-2026.05.26~66bdd1a0-r1.apk` | 8,322 B | `5badc21509df6ba6df109697879cf28960315c8ceb19aabb258f79564ae5de23` |
| `libiwinfo20230701-...apk` | 25,768 B | `d1178900f817c08c232bf4a704e5b883f02c1f67dbe93e41af9f4b8132ce3f55` |
| `libiwinfo-data-...apk` | 3,925 B | `3bb216a4f2ae69a27cfe86daf97c597b92daa4f0fca68b00c0d842076d933320` |
| `wireless-regdb-2026.09.03-r1.apk` | 3,440 B | `afd292e0c0947aaa5497e52b2a9e9484e690a9abfef1153c36d9a06143a758a1` |
| `kmod-hi5622-cfg80211-6.18.60-r1.apk` | - | `716fd384cc7ad58b7c3e4000046addce7c76b3223fe3b8d5b05600fb46ed624c` |
| `kmod-hi5622-wifi-6.18.60-r1.apk` | - | `aac8f3fa0f230ac19f94cb010c6f15e443a52e35b095a7e54908b2185eaf9aef` |

Local convenience copy of the artefacts (NOT in the repo):
`/home/shibbityshwab/wifi-image-integration-artifacts/` (container, kernel
slice, rootfs UBI, squashfs, the seven .apk files, `SHA256SUMS`).

## Build history (two runs, same day)

1. **Attempt 1** (\~147 s): kernel rebuilt with a CHANGED config
   (`CONFIG_HOTPLUG_CPU` lost by `make defconfig`; see REPORT.md F1) ->
   new kernel slice 3,369,166 B sha256 `853ac764...`; the verifier also failed
   on the ABI-versioned manifest name `libiwinfo20230701` (the verifier
   expected `libiwinfo`). Both findings fixed (`kernel-symbols.config`,
   manifest pattern) and the tree restored.
2. **Attempt 2 (recorded)**: kernel slice back to `13e81214...`, verifier PASS.
   The tree's kernel `.config` is back to sha256 `08b6e595...`, so later
   module builds by other lanes run against the same kernel configuration as
   the validated image.

## Tree state after the build (verified)

* `.config`: restored, `cmp` byte-identical to the pre-build copy.
* rootfs staging tree `build_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon`:
  restored from a pre-build tar snapshot (no bench packages left installed, so
  a later world build cannot silently inherit them).
* `package/hi5622-wifi/`: left in the tree (untracked, like the interim
  `hi5622-pciet-hcc` package); not selected in the restored `.config`.
* `target/linux/`, DTS and kernel patch files: untouched (kernel config sha
  identical before/after).
* The image artefact in `bin/targets/hisilicon/luofu/` is the bench image
  recorded above; it no longer matches the restored `.config` (re-run
  `tools/build-image.sh` to reproduce it).

## What this build does NOT prove

* Not flashed, not booted, not loaded on the device.
* Not an upstream/target deliverable: the bench-only delta is not in
  `target/linux/hisilicon/luofu/target.mk` or the image stanza, and the driver
  is research code.
* No multi-run byte-reproducibility claim for the image container by this lane
  (the second run was made after a kernel-config fix; the kernel slice itself
  reproduced the earlier validated build exactly).


---

# Round 3 - FINAL self-contained image (2026-11-27)

**Driver packaged:** the ap-start lane's COMBINED dp-air + AP-start v7 module,
canonical deployable debug-stripped artifact
`/tmp/wifi-ap-dp-combined-artifact/hi5622_wifi.ko` (242,572 B sha256
`44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b`; documented in
`docs/incoming/wifi-ap-start/combined/README.md`, commit `bacd3c1`).
Staged PREBUILT (no compile) via
`tools/install-into-tree.sh --driver-ko FILE`; identity recorded in
`build-evidence/driver-artifact.txt` (sha256/size/modinfo/params).
The v7-only AP-start artifact (206,708 B sha `fd8e1439...`) remains the
fallback for `--driver-ko`.

## Result artifacts (final image)

| artefact | size | sha256 |
| --- | ---: | --- |
| sysupgrade container | **6,922,559 B** | **`ae5e28bfef591fad7d9b4da726128ec2307273aebc41008e0ad9c3867bde6994`** |
| kernel slice (container member `kernel`) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` (UNCHANGED) |
| rootfs UBI (container member `root`) | **3,538,944 B** | **`ff9f3e1c83c36a2a9170cdf6373228692b46b7fa0fbb64abbdc70a25b841aaa9`** |
| rootfs squashfs (build_dir) | 3,077,036 B | `1dbd0d19bc21332684704859a8fb6da890984adf13ebdca569ce22207eea4d98` |
| rootfs squashfs volume in the UBI (padded) | 3,080,192 B | see `image-verify.txt` |
| `cfg80211.ko` installed (stripped) | 406,028 B | `11a5e2b57f31e07b3e692ac084c2aa278bc5d1e5485e7c51e6cb114cfe57c46a` |
| `hi5622_wifi.ko` artifact / installed (stripped) | 242,572 B / 231,132 B | `44c6cc12...` / `51608fd556fa474d2da0dd88975c2a8b610301fb6b8a22d4b6e0589ade247a78` |
| `iw` | 116,855 B | `e0f4ba8cb1dd8f748924f7333475f6d4120bc0b9290d1cf67df1be8687accb23` |
| ucode `iwinfo` | 6,371 B | `ecd3a04a31f6fbbf4426f330a67ffffbed7455f40373b9f1452f27b452686674` |
| `hostapd` | 504,333 B | `13cb3a4306f752f4e1638585e734cd29368ee393096b75931f89e71e01b21919` |
| `nl80211.so` / `rtnl.so` | 61,442 / 73,731 B | `54ac57bc...` / `fc42ccf2...` |
| hostap ucode (common/wdev/wifi-detect) | 10,171 / 6,097 / 5,952 B | see `image-verify.txt` |
| `regulatory.db` | 6,332 B | `c682b8e0...` |

Image manifest: **83 packages**. Userspace apk artefacts (same build; local
copy in the artifacts dir, `SHA256SUMS`):

| package | apk size | sha256 |
| --- | ---: | --- |
| `iw-6.17-r1.apk` | 52,997 B | `9b46713f...` |
| `wifi-scripts-1.0-r5.apk` | 82,672 B | `64cfa515480ae6dd9348a6ef8457fb1995ea4116edce2afe7510d869aeb8fc72` |
| `hostapd-mini-...r7.apk` | 283,790 B | `daf80bebc7495273dbd3a30627349b9063451183686fac314061ab92fb0ce10c` |
| `hostapd-common-...r7.apk` | 1,922 B | `e064cd3f...` |
| `ucode-mod-nl80211-...apk` | 24,228 B | `af25dfe5...` |
| `ucode-mod-rtnl-...apk` | 29,292 B | `685443d6...` |
| `wireless-regdb-2026.09.03-r1.apk` | 3,440 B | `afd292e0...` |
| `kmod-hi5622-cfg80211-6.18.60-r1.apk` | 183,520 B | `716fd384...` |
| `kmod-hi5622-wifi-6.18.60-r1.apk` | 101,923 B | `59286d639a2a3d6bbb9c576fa257ed72252a2d8615c8c7aeafa28041c647f85c` |

Full per-file verification output: `build-evidence/image-verify.txt`
(PASS, kernel check UNCHANGED, all IDENT lines exact).

## Config delta round 3 (bench-only, appended then `make defconfig`)

`CONFIG_PACKAGE_iw`, `wireless-regdb`, `wifi-scripts`,
`CONFIG_WIFI_SCRIPTS_UCODE`, `hostapd-mini`, `hostapd-common`,
`ucode-mod-nl80211`, `ucode-mod-rtnl`, `kmod-hi5622-cfg80211`,
`kmod-hi5622-wifi` (see `config-delta.config`); still appended
`kernel-symbols.config` (`CONFIG_KERNEL_HOTPLUG_CPU=y`) after defconfig so the
validated kernel config/slice is reproduced.

## Tree state after the round-3 build (verified)

* `.config` restored byte-identically (sha256 `9052d5e3...`, `cmp` pass).
* Built kernel `.config` sha256 `08b6e595...` unchanged before/after
  (kernel slice `13e81214...`; no kernel rebuild - 12 s build).
* rootfs staging tree restored from the pre-build snapshot (no bench packages
  left installed); `package/hi5622-wifi/` remains untracked in the tree (not
  selected by the restored `.config`).
* The container in `bin/targets/hisilicon/luofu/` is the final image recorded
  above; it does not match the restored `.config` (re-run `build-image.sh` to
  reproduce).

## What round 3 does NOT prove

* The final image was never flashed/booted by this lane. The ABI predecessor
  `df6a78c3` was flashed and the AP verified live on it; the combined module
  itself has not been fired yet.
* No multi-run byte-reproducibility claim for the container by this lane (the
  package set is pinned by apk identities and the kernel slice by hash).

