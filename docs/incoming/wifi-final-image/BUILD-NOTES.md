# BUILD-NOTES - FINAL bench image build record (wifi-final-image, task-147)

**Host-only build. Nothing was flashed or booted by this lane.**
Recorded build: 2026-11-24T14:43:46Z -> 14:44:02Z (16 s, local clock; the
inherited bench-wave docs date the hardware-validation session 2026-11-27).
Two full runs were made (see section 4); both produced byte-identical artefacts.

## 1. Inputs (all pinned by tools/bake-final-image.sh)

| item | value |
| --- | --- |
| OpenWrt tree | `/home/shibbityshwab/openwrt-lab-selectorfix-img` (shared release line) |
| tree git HEAD | `27663a0` (asserted) |
| target | `hisilicon/luofu`, profile `DEVICE_cudy_wr3000-v2` |
| kernel | linux-6.18.60, OpenWrt GCC 14.4.0 (arm_cortex-a9 musl-eabi) |
| target kernel config | `target/linux/hisilicon/luofu/config-6.18` sha256 `ecf1aae2...` (asserted, unchanged) |
| built kernel .config | sha256 `08b6e595...` (asserted before AND after; kernel compile skipped) |
| driver artifact | `/tmp/wifi-allpatch-artifact/hi5622_wifi.ko` - 308,080 B sha256 `5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41`, 47 params (asserted) |
| package staging | `wifi-image-integration/tools/install-into-tree.sh --driver-ko` (PREBUILT mode; no compile) |
| image delta | `wifi-image-integration/config-delta.config` + `kernel-symbols.config` (same rootfs-only recipe as the ae5e28bf image) |
| this lane's rootfs payload | `payload/` (9 text files, staged via `tools/stage-payload.sh` into the tree's `files/` overlay) |
| build lock | `/tmp/cudy-openwrt-world-build.lock` (flock, same as the image-integration tooling) |

## 2. Command sequence (exactly what tools/bake-final-image.sh does)

    ./tools/stage-payload.sh --tree $OWRT --apply --manifest build-evidence/work/payload-manifest.txt
    <image-int>/tools/install-into-tree.sh --owrt $OWRT \
        --driver-ko /tmp/wifi-allpatch-artifact/hi5622_wifi.ko --label "FINAL all-patch ..." --force
    rm -rf $OWRT/build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/hi5622-wifi
    # append wifi-image-integration/config-delta.config to .config
    make -C $OWRT defconfig
    # append wifi-image-integration/kernel-symbols.config (CONFIG_KERNEL_HOTPLUG_CPU carry-over)
    make -C $OWRT -j$(nproc)
    ./tools/verify-final-image.sh --owrt $OWRT
    # then: record hashes, copy local artefacts, restore the shared tree

The package build dir is purged on purpose: the package version string is
unchanged (`6.18.60-r1`) and the previous image's stamps would otherwise
skip the re-stage, silently keeping the old `hi5622_wifi.ko`. The verifier
proves the built artefact is the pinned 308,080 B `5b587949...` bytes.

## 3. Build log summary (build-evidence/build-log.txt)

* `make -j` world: rootfs/container only; the kernel compile is skipped
  (kernel .config sha unchanged before/after -> kernel slice reused).
* Package compile line `-C package/hi5622-wifi compile` runs (rebuild forced
  by purging the package build dir) and the PREBUILT module is copied, not
  compiled.
* `package/install` + `target/install` + `package/index checksum` regenerate the
  rootfs, UBI and sysupgrade container.
* Build duration 16 s (second run; first run 18 s), build rc 0.

## 4. Reproducibility: two runs, identical bytes

The bake was executed twice end-to-end (the second run after a tooling fix to
the payload-overlay cleanup; same tree, same config, same module):

| artefact | run 1 (18 s) | run 2 (16 s) |
| --- | --- | --- |
| container sha256 | `0efcd1f1a5b16a73f040695f90ae00b41684d0f9348b60b1a13b45b2e27c2ada` | **identical** |
| kernel slice sha256 | `13e81214...` | **identical** |
| rootfs UBI sha256 | `6196232d...` | **identical** |
| squashfs sha256 | `c6f5e779...` / volume `f51a2e0d...` | **identical** |

`build-evidence/kernel-config-sha.txt` records it directly: "container before
this build: `0efcd1f1...` / container after this build: `0efcd1f1...`". The
image is reproducible from this tree/config on this host.

## 5. Result artefacts

| artefact | size | sha256 |
| --- | ---: | --- |
| sysupgrade container | 6,922,559 B | `0efcd1f1a5b16a73f040695f90ae00b41684d0f9348b60b1a13b45b2e27c2ada` |
| kernel slice (container member `kernel`) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` (**UNCHANGED**) |
| rootfs UBI (container member `root`) | 3,538,944 B | `6196232d878e141ad1833a5dbed4ef4e339dd41d5c450151842c285a328ba30e` |
| rootfs squashfs (build_dir) | 3,108,256 B | `c6f5e779e8769c0379c6f69a6f5f40cb2a2ec2dbcda2d8133ea623c02c617f5f` |
| rootfs squashfs volume (UBI) | 3,108,864 B | `f51a2e0d9bb9a7a9ecafbbe0186adad317108863241ccb3908cd94f8d5bbb263` |
| `hi5622_wifi.ko` artifact staged | 308,080 B | `5b5879497c169d6ab57dddd99487de5e69177621eeead64f5a137b31d2e70d41` |
| `hi5622_wifi.ko` installed (stripped) | 291,292 B | `f07dd505f1d7ac5dd62a826c440414bf2bc1662fe2fecb504dc4cec5b5e0d412` |
| `kmod-hi5622-wifi-6.18.60-r1.apk` | 126,774 B | `aa6bdaed4fc2704129bbdbadb5827ae7ad5d458e3ee8161bee19fea915380793` |
| `cfg80211.ko` (tree / installed) | 2,328,548 / 406,028 B | `949393f9...` / `11a5e2b5...` (unchanged) |
| image manifest | 83 packages | `openwrt-hisilicon-luofu-cudy_wr3000-v2.manifest` |

Local convenience copy (NOT committed):
`/home/shibbityshwab/wifi-final-image-artifacts/` -
`final-image-sysupgrade.bin`, `kernel-slice.bin`, `rootfs.ubi`,
`root.squashfs`, `kmod-hi5622-wifi-6.18.60-r1.apk`, `SHA256SUMS`:

    0efcd1f1a5b16a73f040695f90ae00b41684d0f9348b60b1a13b45b2e27c2ada  final-image-sysupgrade.bin
    13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f  kernel-slice.bin
    6196232d878e141ad1833a5dbed4ef4e339dd41d5c450151842c285a328ba30e  rootfs.ubi

Committed text evidence (`build-evidence/`): `hashes.txt` (machine dump),
`final-image-verify.txt` (verify output), `build-log.txt`, `build-summary`-style
`kernel-config-sha.txt`, `config-delta-applied.diff`, `payload-manifest.txt`,
`driver-artifact.txt` (module identity + 47 params). Full working data
(kernel/rootfs extracts, pre-build tarball, resolved .config) stays in the
gitignored `build-evidence/work/`.

## 6. Tree state after the build (verified)

* `.config`: restored byte-identically (sha256 `9052d5e3...`, `cmp` pass).
* built kernel `.config`: restored byte-identically (`08b6e595...`); the
  validated kernel config is unchanged so later lanes' module builds still run
  against the same kernel.
* rootfs staging tree: snapshot before the build, restored from the tar after
  (no bench packages left installed in the tree).
* `files/` overlay: removed (it did not exist before the build).
* `package/hi5622-wifi/` (untracked in the tree, like the other bench
  packages) remains staged with the FINAL module; the tree's `bin/targets/...`
  container is this image and no longer matches the restored `.config` by
  design (re-run the bake to reproduce).
* Shared build lock released on exit.

## 7. What this build does NOT prove

* Not flashed, not booted, not loaded on the device.
* The all-patch module was not fired as this exact combination before (the
  inherited one-boot validation is the combined run of the constituent
  features); one attended install+boot is the next gate.
* Not an upstream/target deliverable: bench-only config delta + research
  driver; PD4 firmware and the wifi_block are manual pushes.
* WPA2 is not end-to-end (client association/mgmt path missing); WPA3-SAE is
  not functional (lane task-144 not delivered).
* The vendor firmware/calibration is not in the image (policy) - the driver
  fails closed without it.

## 8. Regeneration

    ./tools/bake-final-image.sh
    ./tools/verify-final-image.sh --owrt /home/shibbityshwab/openwrt-lab-selectorfix-img
    ./tools/selftest.sh

A different module (new sha/size/params) is a NEW bake: update the pinned
constants in `tools/bake-final-image.sh` (and the verifier) first - the script
refuses a non-pinned artifact by design.
