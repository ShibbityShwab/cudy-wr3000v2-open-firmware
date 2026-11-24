# BUILD-NOTES - bench image build record (wifi-image-integration, task-112)

**Recorded build:** 2026-11-24 (local UTC+07), lane img-integ, host-only.
**Result:** `make -j16` RC=0 in 153 s; verifier PASS; shared tree restored.

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
