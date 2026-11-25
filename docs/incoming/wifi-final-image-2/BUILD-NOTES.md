# BUILD-NOTES - FINAL IMAGE #2 build record (wifi-final-image-2, task-159)

**Host-only build. Nothing was flashed, written to a device or booted by this
lane.** The image is a ROOTFS-ONLY successor of the wifi-hostapd-sae image B
(container `8944740e...`): the hardware-validated kernel slice
`13e81214...` is byte-identical; only the rootfs changed (new association +
RX-watermark driver module, the WPA2/WPA3 fixtures, the retargeted hi5622
loader/init).

**Two image-2 bakes, same tree/recipe; the second supersedes the first:**

| bake | module | container | note |
| --- | --- | --- | --- |
| image-2 baseline | `ab9e18a4...` 344,372 B, 56 params (corrected mgmt carrier) | `0d9429ec...` | first bake; 3x byte-identical; superseded by the RX-watermark module |
| **image-2 FINAL** | `d2c608e7...` 347,388 B, 57 params (corrected carrier + `rx_refill_wm`) | **`8ba1ad22...`** | the current image; 3x byte-identical after the final payload edit |

The module was rebuilt by the wifi-rx-watermark lane (task-156) as
assoc tree 251d08f0 + corrected mgmt-carrier patch + RX-refill-watermark patch;
it was delivered host-verified (127-check selftest) and was baked here exactly
as delivered (sha-pinned).

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
| driver artifact | `/tmp/wifi-rx-watermark-artifact/hi5622_wifi.stripped.ko` (durable copy `build/wifi-rx-watermark/hi5622_wifi.stripped.ko`) - 347,388 B sha256 `d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6`, 57 params, vermagic 6.18.60, depends cfg80211 (asserted size+sha+params) |
| package staging | `wifi-image-integration/tools/install-into-tree.sh --driver-ko` (PREBUILT; no compile) |
| image delta | `wifi-image-integration/config-delta.config` (hostapd-mini -> hostapd-mbedtls SAE) + `kernel-symbols.config`, identical to the image-B recipe |
| rootfs payload | this lane's `payload/` (11 files) staged via `$OWRT/files`; image-B payload set plus the corrected module's uci/loader policy (incl. `rx_refill_wm`) |
| build lock | `/tmp/cudy-openwrt-world-build.lock` (flock) |

## 2. Command sequence (exactly what tools/bake-final-image-2.sh does)

    ./tools/selftest.sh                                 # host checks first (optional)
    ./tools/bake-final-image-2.sh
    # 1. snapshot .config, built kernel .config, target root
    # 2. stage payload/ -> $OWRT/files (+ /etc/rc.d/S96hi5622-wifi link)
    # 3. install-into-tree.sh --driver-ko <d2c608e7> --force  (PREBUILT)
    # 4. purge $KDIR/hi5622-wifi (stale stamp guard), append image-B delta,
    #    make defconfig, assert the resolved symbols, append kernel-symbols
    # 5. make -j$(nproc)   (world; kernel compile must be skipped)
    # 6. extract container members; ASSERT kernel slice 13e81214 unchanged;
    #    extract the built rootfs (fresh dir - a stale extraction would fake
    #    the module identity) and check the installed module params/sha
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
  UBI and sysupgrade container. Build duration 7-8 s, build rc 0.

## 4. Reproducibility

The FINAL image reproduced **byte-identically** in the three recorded builds
after the final payload edit (build-evidence/build-runs.txt):

| artefact | runs 05:48:53Z / 05:49:20Z / 05:49:33Z |
| --- | --- |
| container | `8ba1ad22...` (identical x3; the 05:48:53 run's verify FAIL was a verifier-wording bug, fixed afterwards without changing image bytes) |
| kernel slice | `13e81214...` (identical) |
| rootfs UBI | `31e7a39c...` (identical) |
| squashfs | `cb42bfed...` (identical) |
| installed module | `b4c076bf...` (identical) |
| verify | 84/84 PASS |

The earlier image-2 baseline bake likewise reproduced 3x (`0d9429ec...`,
module `4f1b45c1...`); both phases are in the run history. A transient
container from the mid-edit state (`fe4e6fa5...`) is not the final
configuration and is superseded.

## 5. Result artefacts

| artefact | size | sha256 |
| --- | ---: | --- |
| sysupgrade container (flash unit) | 7,188,799 B | `8ba1ad22d78f89e1fa2f7ed45dbffea0bc8a6619890f820cc8ba27ce5c9bd8b8` |
| kernel slice (container member `kernel`) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` (**UNCHANGED**) |
| rootfs UBI (container member `root`) | 3,801,088 B | `31e7a39c2d7b022ee16b850a72508c256d2e6c5265ab695717f08df466b9035d` |
| rootfs squashfs (build_dir) | 3,318,696 B | `cb42bfed25a12698770236b44bb8e73fafcf362d7571911f6470374090ce0578` |
| `hi5622_wifi.ko` staged artifact | 347,388 B | `d2c608e7c8f578c2443b2dabc381cc660cdb12055ff682a662ab1a31eb609ab6` (57 params) |
| `hi5622_wifi.ko` installed (stripped) | 328,788 B | `b4c076bf37099b21bcb5055f23678ffee38e86da9b3975e36d5c50025b31065e` (.text 146,448 B byte-identical to the artifact) |
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

## 7. Re-baking with a different module

`tools/bake-final-image-2.sh` accepts explicit pins, so any successor artifact
bakes without editing the script:

    DRIVER_KO=/path/to/hi5622_wifi.stripped.ko \
    DRIVER_SHA=<sha256> DRIVER_SIZE=<size> DRIVER_PARAMS=<n> \
    DRIVER_INSTALLED_SHA= ./tools/bake-final-image-2.sh

Any sha/size/param mismatch aborts. The verifier's module checks are adapted by
the same env pins (`--module-params` etc.); the corrected-carrier assertions are
carrier-specific (a successor must keep the corrected g0 id6 carrier).

## 8. Rebuilding the modules if /tmp was cleaned

**RX-watermark (current) module** - durable gitignored copy already exists at
`build/wifi-rx-watermark/hi5622_wifi.stripped.ko` (sha `d2c608e7...`). If it
is gone too, rebuild from committed sources with the wifi-rx-watermark lane's
`tools/build-final-module.sh` (assoc tree 251d08f0 + sibling carrier patch +
watermark patch; the lane's evidence/identity.txt records the expected sha).
Note: the GNU build-id (20 bytes) hashes the pre-strip DWARF `comp_dir`, so a
rebuild at a different path keeps identical code/params but changes the sha -
bake the exact file you quote.

**Corrected association (pre-watermark) module** - reproduce from committed
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
    # strip -> 344,372 B sha256 ab9e18a4... (56 params)

## 9. Verify / selftest expected tails

    ./tools/verify-final-image-2.sh --container /home/shibbityshwab/wifi-final-image-2-artifacts/final-image-2-sysupgrade.bin \
        --module-artifact /tmp/wifi-rx-watermark-artifact/hi5622_wifi.stripped.ko \
        --module-artifact-sha d2c608e7... --module-artifact-size 347388 --module-params 57
    # -> "== verify-final-image-2: 84 passed, 0 failed =="

    ./tools/selftest.sh
    # -> "== selftest: 50 passed, 0 failed, 1 skipped =="
