# wifi-image-integration - SELF-CONTAINED bench image (iw/iwinfo/regdb/kmods/hostapd/ucode)

**Lane:** img-integ (shared task task-112 -> final-image task-132), host-only.
**Date:** 2026-11-27 (final image; rounds 1-2 on 2026-11-24).
**Status:** the **FINAL SELF-CONTAINED BENCH IMAGE** is BUILT and VERIFIED
FROM ITS BYTES host-side with the **hardware-validated kernel slice unchanged**
- a ROOTFS-ONLY rebuild. **Nothing in this lane was flashed or booted**; the ABI
predecessor `df6a78c3` was flashed by the Lead and the AP runs on it.

Recorded final image (2026-11-27, tree `27663a0`, `make -j16`, 12 s):
container **6,922,559 B sha256 `ae5e28bf...`**; kernel slice **UNCHANGED
3,372,638 B sha256 `13e81214...`** (slot `1794a531...`); rootfs UBI
**3,538,944 B sha256 `ff9f3e1c...`**; manifest **83 packages**. Full numbers:
`build-evidence/build-summary.txt` + `BUILD-NOTES.md` round 3; round-2 ABI
image evidence archived in `build-evidence/round2-abi-image-df6a78c3/`.

Out of the box (no /tmp deployment for these):

* userspace: `iw` (with `libnl-tiny`), ucode `iwinfo` + `wifi-scripts`
  ucode files, `hostapd-mini` + `hostapd-common` (v2.12),
  `ucode-mod-nl80211`/`rtnl`, `wireless-regdb`;
* kernel modules in `/lib/modules/6.18.60/`: `cfg80211.ko` from the target
  kernel tree and the **combined dp-air + AP-start v7** `hi5622_wifi.ko`
  (prebuilt ABI-validated artifact 242,572 B sha `44c6cc12...`; no autoload:
  bench policy).
* Still local by policy: the PD4-patched vendor firmware and the bench configs/
  scripts (firmware is vendor-derived and never committed).

## What is in this directory

    config-delta.config              the exact bench-image package delta
    package/hi5622-wifi/             OpenWrt package (KernelPackages
                                     kmod-hi5622-cfg80211 + kmod-hi5622-wifi)
    tools/install-into-tree.sh       copy package + prebuilt module or src snapshot
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

* **No vendor bytes in the repository**: no firmware image, no
  calibration/custom-block data, no vendor object are committed; the prebuilt
  driver module is copied from a local artifact into the untracked OpenWrt tree
  and only its sha256 identity is recorded. The vendor firmware stays local
  evidence; the driver reads it through fw_path at bench time.
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
