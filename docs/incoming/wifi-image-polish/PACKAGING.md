# PACKAGING - kmod package, versioned ABI, and the firmware policy

## 1. What already exists in the image

The flashed self-contained image (container `ae5e28bf...`) already carries the
right OpenWrt package shape, built by the wifi-image-integration lane
(`docs/incoming/wifi-image-integration/package/hi5622-wifi/Makefile`):

* `kmod-hi5622-cfg80211` - installs the **target-kernel-tree** `cfg80211.ko`
  (`$(LINUX_DIR)/net/wireless/cfg80211.ko`, kernel config `CONFIG_CFG80211=m`).
  It must be that one: the clean-room driver was compiled against this kernel
  tree.  Do **not** also select `package/kernel/mac80211`'s
  `kmod-cfg80211` (backports) - two different `cfg80211.ko` files must never
  coexist in one rootfs.
* `kmod-hi5622-wifi` - installs `hi5622_wifi.ko`; `DEPENDS:=@TARGET_hisilicon_luofu
  +kmod-hi5622-cfg80211`; two build modes: **prebuilt** (the hardware-validated
  artifact is copied as-is; used for the bench image) and **source** build from
  a committed driver snapshot.  The package has **deliberately no AUTOLOAD**:
  bench policy is that nothing touches the Wi-Fi chip without an operator.

## 2. This lane's additions to the package image

The kmod package is unchanged; the polish is userspace policy, staged as a
rootfs `files/` overlay by `tools/install-into-image.sh` (dry-run by
default, refuses binary/firmware payloads, creates the rc.d enable symlink
that a files/ overlay does not create by itself):

    etc/config/hi5622                  uci policy (autoload=0 bench default)
    etc/init.d/hi5622-wifi             START=96 service
    etc/rc.d/S96hi5622-wifi            -> ../init.d/hi5622-wifi
    etc/modprobe.d/hi5622-wifi.conf    packed defaults mirror
    etc/hi5622/hostapd-*.conf          AP fixtures
    etc/hi5622/FIRMWARE-POLICY.txt     policy text for the operator
    usr/sbin/hi5622-load               explicit-insmod loader

Two equivalent ways to ship it with the image:

1. **files/ overlay (recommended, no package edit):**
   `tools/install-into-image.sh --tree $OPENWRT --apply`, then rebuild the
   rootfs with `wifi-image-integration/tools/build-image.sh`.  Commit the
   overlay in the target tree (or keep it as a staged artifact) - it is plain
   text, no binaries.
2. **package FILES (for a real packaged release):** append the same paths to
   `define Package/kmod-hi5622-wifi/install` after the module install
   (`$(INSTALL_DIR) $(1)/etc/config ...`, `$(INSTALL_BIN)` for
   `usr/sbin/hi5622-load`, `ln -sf` for rc.d), and add
   `define Package/kmod-hi5622-wifi/conffiles` for `/etc/config/hi5622`
   and `/etc/hi5622/*.conf`.  Do not add AUTOLOAD: the init script owns the
   load decision.

## 3. Versioned ABI note

| item | pinned value |
| --- | --- |
| kernel | `6.18.60` (uImage `Linux-6.18.60`, load/entry `0x80608000`) |
| kernel ABI pin | `6.18.60~6f995278` (apk kernel version on the flashed image) |
| kernel slice | 3,372,638 B sha256 `13e81214...` (never rebuilt for rootfs-only images) |
| module vermagic | `6.18.60 SMP mod_unload ARMv7 p2v8` |
| module depends | `cfg80211` |
| installed module | 231,132 B sha256 `51608fd5...` (stripped form of the 242,572 B prebuilt) |
| validated prebuilt | 242,572 B sha256 `44c6cc12...`, unstripped 925,192 B sha256 `21d05161...` |
| polished (NOT fired) | 242,920 B sha256 `d145a205...`, unstripped 926,120 B sha256 `512b4734...`, 20 params |
| cfg80211.ko in image | 406,028 B sha256 `11a5e2b5...` from the target kernel tree |

Rules:

* The module **must** come from the same 6.18.60 tree/build as the image's
  `cfg80211.ko`; check `vermagic` and `depends=cfg80211` before loading.
* A module change is a **versioned event**: bump the kmod `PKG_RELEASE`, record
  the new size/sha in `refs/artifact-identity.txt` and `REPORT.md`, and do a
  fresh fire (the only exception: passing parameters to the validated module,
  which changes no bytes).
* The parameter surface is part of the interface: 19 names for the validated
  module, 20 for the polished build (`mgmt_diag`).  Do not pass parameters a
  module does not expose - insmod fails on unknown parameters.
* The toolchain build-id hashes debug info, so the **same source built in a
  different absolute directory differs only in the 20-byte build-id**.  Identity
  checks must reproduce the canonical workdir (the build script does), and the
  kernel ignores build-id for loading.  `.text`, parameters and vermagic are
  path-independent.
* `refs/artifact-identity.txt` is the machine-readable pin used by the lane
  validation; keep it in sync with this table.

## 4. Firmware and calibration policy (PD4 is never packaged)

The vendor-derived firmware and the Wi-Fi custom block are **never** committed
and **never** packaged.  The image ships only the text policy
(`etc/hi5622/FIRMWARE-POLICY.txt`) and the driver fails closed without them
(`allow_unseeded_release=0`).  Required manual push, attended session, host
side with the bench link up:

    # on the host (repo root), from the local bench-session artifacts:
    sha256sum /tmp/hi5622-pd4/FIRMWARE.PD4.bin
    #   928920 B  sha256 cfe24bfb194899f362bfcb0301d7e28a3b4dce0ef4e70b705d54a1f78d3037b1
    tools/rscp.sh /tmp/hi5622-pd4/FIRMWARE.PD4.bin \
        root@192.168.1.1:/lib/firmware/hi_wifi/FIRMWARE.PD4.bin
    tools/rscp.sh <local capture>/wifi_block.bin \
        root@192.168.1.1:/lib/firmware/hi_wifi/wifi_block.bin
    tools/rsh.sh root@192.168.1.1 'sha256sum /lib/firmware/hi_wifi/FIRMWARE.PD4.bin'

Provenance of the PD4 artifact (from `/tmp/hi5622-pd4/PD4-MANIFEST.txt`):
patched from the stock image `FIRMWARE.bin` (sha256
`7fc87e20...`, 928,920 B) with a consume-only research patch (entry/loop
segments); it is vendor-derived and is **not** a redistribution candidate.
The patch is what makes the boot handshake work on this unit - the stock
firmware's mailbox dispatch is not host-programmable (STATUS.md 3m).

Consequences of the policy: a reflash/overlay wipe removes the files (the
driver then refuses to release the chip and the loader stops at the preflight);
the image is not a turnkey artifact, and that is deliberate.

## 5. Upstream path (documentation only)

The packaged default `fw_path=hi_wifi/FIRMWARE.PD4.bin` is **bench-specific**.
If the vendor GPL source / redistributable firmware ever arrives, an
upstreamable driver would default to a clean-room name (e.g.
`hi5622/firmware.bin`) and the file would be supplied by a firmware package
carrying the vendor's redistribution terms - not by a manual push.  Until
then the Wi-Fi driver stays research-only and out of any release image stanza;
only the bench image config delta selects these packages
(`docs/incoming/wifi-image-integration/config-delta.config`).
