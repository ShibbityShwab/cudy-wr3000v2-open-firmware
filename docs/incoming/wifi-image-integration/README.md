# wifi-image-integration - bench image with iw/libnl-tiny/iwinfo/regdb + the clean-room driver

**Lane:** img-integ (shared task task-112), host-only. **Date:** 2026-11-24.
**Status:** package + config delta + image build tooling delivered; the bench
image is **BUILT and VERIFIED FROM ITS BYTES** host-side with the
**hardware-validated kernel slice unchanged**. **Nothing in this lane was
flashed or booted** - the attended install is the Lead's call (DEPLOY.md).

Recorded image (2026-11-24, tree `27663a0`, `make -j16`, 153 s):
container **6,533,439 B sha256 `df6a78c3...`**; kernel slice **unchanged
3,372,638 B sha256 `13e81214...`**; rootfs UBI 3,145,728 B sha256
`62344e80...`; squashfs 2,753,136 B sha256 `68450ee4...`; manifest 81
packages (+7). Full numbers: `build-evidence/build-summary.txt` and
`BUILD-NOTES.md`; the one non-obvious build requirement (kernel-symbol
carry-over) is `kernel-symbols.config` + REPORT.md finding F1.

The cfg80211 bench image (kernel slice `13e81214...`) proved the wiphy/VAP
milestone only by pushing stock binaries and modules to `/tmp`. This lane
makes the image self-contained for that bench:

* userspace: `iw` (with `libnl-tiny`), `iwinfo` + `libiwinfo`,
  `wireless-regdb`;
* kernel modules: `cfg80211.ko` from the target kernel tree and the
  clean-room `hi5622_wifi.ko` driver, both installed in
  `/lib/modules/6.18.60/` (no autoload: bench policy).

## What is in this directory

    config-delta.config              the exact bench-image package delta
    package/hi5622-wifi/             OpenWrt package (KernelPackages
                                     kmod-hi5622-cfg80211 + kmod-hi5622-wifi)
    tools/install-into-tree.sh       copy package + driver source snapshot
    tools/build-image.sh             serialized build + evidence + tree restore
    tools/verify-image.sh            verify the BUILT bytes (unsquashfs + UBI)
    tools/extract-ubi-rootfs.py      read-only UBI image -> squashfs volume
    tools/selftest.sh                device-free contract checks
    build-evidence/                  recorded build/verification evidence
    REPORT.md                        lane report, decisions, limitations
    BUILD-NOTES.md                   build record and reproducibility
    DEPLOY.md                        attended deploy options + rollback

## Quick start (host only, no device)

    ./tools/selftest.sh
    ./tools/install-into-tree.sh --owrt /home/shibbityshwab/openwrt-lab-selectorfix-img
    # or the full serialized build (installs, applies the delta, builds,
    # verifies, records evidence and restores the shared tree):
    ./tools/build-image.sh

## Hard rules honoured

* **No vendor bytes**: no firmware image, no calibration/custom-block data, no
  vendor object. The vendor firmware stays local evidence; the driver reads it
  through fw_path at bench time.
* **Bench-only**: the delta is applied to a build .config and documented here;
  the release/target image stanza (target/linux/hisilicon/...) is untouched.
* **No device writes at load / no autoload**: the module is loaded by hand in
  an attended session; nothing in the rootfs releases the chip CPU by itself.
* **Shared tree discipline**: build-image.sh refuses to run while another make
  runs in the tree, holds /tmp/cudy-openwrt-world-build.lock, records the
  evidence and restores `.config` and the rootfs staging tree afterwards.

## Honest limits

* Host-built and verified only; no boot/flash claim. The kernel slice is
  expected to stay the hardware-validated `13e81214...` (verify-image.sh
  asserts it); only the rootfs changes.
* The driver snapshot is a copy of `docs/incoming/wifi-cfg80211/src` at build
  time (sha256 manifest recorded); it is research code, not upstreamable, and
  its module hash is build-path dependent.
* The vendor firmware is not in the image and cannot be; the bench session
  still copies it locally.
* `wireless-regdb` ships only the unsigned `regulatory.db` (OpenWrt package)
  while the kernel keeps `CONFIG_CFG80211_REQUIRE_SIGNED_REGDB=y`, so cfg80211
  falls back to the built-in world regulatory domain - permissive and
  irrelevant for wiphy/VAP enumeration (documented in REPORT.md).
