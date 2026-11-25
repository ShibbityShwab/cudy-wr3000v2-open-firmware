# BUILD-NOTES - FINAL IMAGE #2 build record (wifi-final-image-2, task-159)

**Host-only build. Nothing was flashed, written to a device or booted by this
lane.** The image is a ROOTFS-ONLY successor of the wifi-hostapd-sae image B
(container `8944740e...`): the hardware-validated kernel slice
`13e81214...` is byte-identical; only the rootfs changed (new association
driver module + the WPA2/WPA3 fixtures + the retargeted hi5622 loader/init).

Recorded runs (UTC): 2026-11-25T05:43:46Z, 05:43:56Z, 05:44:21Z - three
byte-identical containers (see `build-evidence/build-runs.txt`). Two earlier
runs during verifier development produced the same container hash
`0d9429ec...`; only verifier wording changed afterwards.

## 1. Inputs (pinned by tools/bake-final-image-2.sh)

| item | value |
| --- | --- |
| OpenWrt tree | `/home/shibbityshwab/openwrt-lab-selectorfix-img` (asserted) |
| tree git HEAD | `27663a0` (asserted) |
| target | `hisilicon/luofu`, profile `DEVICE_cudy_wr3000-v2` |
| kernel | linux-6.18.60, OpenWrt GCC 14.4.0 (arm_cortex-a9 musl-eabi) |
| target kernel config | `target/linux/hisilicon/luofu/config-6.18` sha256 `ecf1aae2...` (asserted) |
| built kernel .config | sha256 `08b6e595...` (asserted before AND after; kernel rebuild impossible in this path) |
| kernel slice | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` 3,372,638 B (asserted in the built container) |
| driver artifact | `/tmp/sibling-mgmt-fix/hi5622_wifi.stripped.ko` - 344,372 B sha256 `ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e`, 56 params, vermagic 6.18.60, depends cfg80211 (asserted size+sha+params) |
| package staging | `wifi-image-integration/tools/install-into-tree.sh --driver-ko` (PREBUILT; no compile) |
| image delta | `wifi-image-integration/config-delta.config` (hostapd-mini -> hostapd-mbedtls SAE) + `kernel-symbols.config`, identical to the image-B recipe |
| rootfs payload | this lane's `payload/` (11 files) staged via `$OWRT/files`; image-B payload set plus the corrected module's uci/loader policy |
| build lock | `/tmp/cudy-openwrt-world-build.lock` (flock) |

## 2. Command sequence (exactly what tools/bake-final-image-2.sh does)

    ./tools/selftest.sh                                 # host checks first (optional)
    ./tools/bake-final-image-2.sh
    # 1. snapshot .config, built kernel .config, target root
    # 2. stage payload/ -> $OWRT/files (+ /etc/rc.d/S96hi5622-wifi link)
    # 3. install-into-tree.sh --driver-ko <ab9e18a4> --force  (PREBUILT)
    # 4. purge $KDIR/hi5622-wifi (stale stamp guard), append image-B delta,
    #    make defconfig, assert the resolved symbols, append kernel-symbols
    # 5. make -j$(nproc)   (world; kernel compile must be skipped)
    # 6. extract container members; ASSERT kernel slice 13e81214 unchanged
    # 7. verify tools/verify-final-image-2.sh over the built bytes
    # 8. record evidence, copy local artefacts, restore the shared tree

The package build dir is purged on purpose: the package version string is
unchanged and previous image stamps would otherwise skip the re-stage, silently
keeping the old `hi5622_wifi.ko`. The verifier proves the built/installed
module is the pinned artifact (sha + .text identity).

## 3. Build log summary (build-evidence/build-log.txt)

* `make -j`: rootfs/container only; kernel compile skipped (kernel .config sha
  unchanged before/after; kernel slice asserted in the container).
* Package compile line runs and the PREBUILT module is copied, not compiled.
* `package/install` + `target/install` + package index regenerate the rootfs,
  UBI and sysupgrade container. Build duration 8-10 s, build rc 0.

## 4. Reproducibility: three recorded runs, identical bytes

| artefact | run 1 (05:43:46Z) | run 2 (05:43:56Z) | run 3 (05:44:21Z) |
| --- | --- | --- | --- |
| container | `0d9429ec...` | identical | identical |
| kernel slice | `13e81214...` | identical | identical |
| rootfs UBI | `44df3a63...` | identical | identical |
| squashfs | `f0f970e2...` | identical | identical |
| installed module | `4f1b45c1...` | identical | identical |
| verify | 82/82 PASS | 82/82 PASS | 82/82 PASS |

`build-evidence/kernel-config-sha.txt` records container before == after, and
`build-evidence/build-runs.txt` is the machine record of the runs.

## 5. Result artefacts

| artefact | size | sha256 |
| --- | ---: | --- |
| sysupgrade container (flash unit) | 7,188,799 B | `0d9429ec4681d43966bd2f23dc3fbe872f232961eaf142c16f565c67be5b5a15` |
| kernel slice (container member `kernel`) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` (**UNCHANGED**) |
| rootfs UBI (container member `root`) | 3,801,088 B | `44df3a63cfd9b9811a4ad99d8f49ae917a49c4408719d0df4582ed50785faedb` |
| rootfs squashfs (build_dir) | 3,316,684 B | `f0f970e20d9ea412f6f535c835cadbb57655824d3236a0f63243f394a7dc7a94` |
| `hi5622_wifi.ko` staged artifact | 344,372 B | `ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e` (56 params) |
| `hi5622_wifi.ko` installed (stripped) | 325,932 B | `4f1b45c187e9dfc61783a458f6a60056763f1da037bebec239b5b571f4ff4de1` (.text 144,572 B byte-identical to the artifact) |
| image manifest | 83 packages | `openwrt-hisilicon-luofu-cudy_wr3000-v2.manifest` |
| `/usr/sbin/hostapd` (in image) | 1,032,951 B | `451dd49f0e759cd6da842aafc479fae6ad80ee094d498b270b263a9bddfb7469` (SAE full variant) |

Local convenience copy (NOT committed):
`/home/shibbityshwab/wifi-final-image-2-artifacts/` -
`final-image-2-sysupgrade.bin`, `kernel-slice.bin`, `rootfs.ubi`,
`root.squashfs`, `hi5622_wifi.installed.ko`, `SHA256SUMS`.

Committed text evidence (`build-evidence/`): `build-runs.txt`,
`build-summary.txt`, `build-log.txt`, `driver-stage.txt`,
`driver-artifact.txt`, `config-delta-applied.diff`, `payload-manifest.txt`,
`kernel-config-sha.txt`, `hashes.txt`, `final-image-verify.txt`. Full working
data (extracted rootfs, pre-build tarball, resolved .config) stays in the
gitignored `build-evidence/work/`.

## 6. Tree state after the build (verified)

* `.config` restored byte-identically (`9052d5e3...`),
* built kernel `.config` restored byte-identically (`08b6e595...`),
* the `files/` overlay removed,
* residue (untracked, as in the prior image lanes): `package/hi5622-wifi/`
  (prebuilt staging) and the new image in `bin/targets/hisilicon/luofu/`.
  Neither is part of this repository.

## 7. Re-baking with a newer module (e.g. the rx-watermark rebuild)

`tools/bake-final-image-2.sh` accepts explicit pins, so a successor artifact
bakes without editing the script:

    DRIVER_KO=/tmp/wifi-rx-watermark-build/hi5622_wifi.stripped.ko \
    DRIVER_SHA=<new sha256> DRIVER_SIZE=<new size> DRIVER_PARAMS=57 \
    DRIVER_INSTALLED_SHA= ./tools/bake-final-image-2.sh

Any sha/size/param mismatch aborts. The verifier's module checks are adapted by
the same env pins (`--module-params`), and the corrected-carrier assertions are
carrier-specific (a watermark build must keep the corrected g0 id6 carrier).

## 8. Rebuilding the module if /tmp was cleaned

The artifact is local-only (never committed). Reproduce it from committed
sources:

    # 1) rebuild the FINAL ASSOCIATION tree+artifact (344,472 B 251d08f0)
    opensource/docs/incoming/wifi-stability/tools/build-association-module.sh

    # 2) apply the sibling corrected-carrier patch and rebuild (wifi-sibling-gpl
    #    MGMT-CARRIER-CORRECTION.md "Reproduction" block)
    cd /tmp && rm -rf mgmt-fix && cp -a /tmp/wifi-assoc-build/integ mgmt-fix
    cd mgmt-fix && patch -p1 < <repo>/opensource/docs/incoming/wifi-sibling-gpl/patches/0001-mgmt-tx-hcc-g0id6-carrier.patch
    export KDIR=/home/shibbityshwab/openwrt-lab-upstream-target/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60
    export STAGING_DIR=/home/shibbityshwab/openwrt-lab-upstream-target/staging_dir
    export PATH=$STAGING_DIR/toolchain-arm_cortex-a9_gcc-14.4.0_musl_eabi/bin:$PATH
    make ARCH=arm CROSS_COMPILE=arm-openwrt-linux-muslgnueabi- -C $KDIR M=$(pwd) modules

    # 3) strip and check the expected identity
    arm-openwrt-linux-muslgnueabi-strip --strip-debug -o /tmp/hi5622_wifi.stripped.ko hi5622_wifi.ko
    # expect 344,372 B sha256 ab9e18a4fb4cedbbfd03349461c37ae0143ecec582f2395c0770c7415c72662e,
    # 56 params; the unstripped .ko is 1,324,532 B sha256 2d9c3f11...;
    # 0 compiler/modpost warnings; strip leaves .text byte-identical.

## 9. Verify / selftest expected tails

    ./tools/verify-final-image-2.sh --container /home/shibbityshwab/wifi-final-image-2-artifacts/final-image-2-sysupgrade.bin \
        --module-artifact /tmp/sibling-mgmt-fix/hi5622_wifi.stripped.ko \
        --module-artifact-sha ab9e18a4... --module-artifact-size 344372 --module-params 56
    # -> "== verify-final-image-2: 82 passed, 0 failed =="

    ./tools/selftest.sh
    # -> "== selftest: 49 passed, 0 failed, 1 skipped =="
