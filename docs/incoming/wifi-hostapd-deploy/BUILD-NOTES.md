# BUILD-NOTES - wifi-hostapd-deploy (task-125)

**Recorded build:** 2026-11-26, host-only, OpenWrt release-line tree
`/home/shibbityshwab/openwrt-lab-selectorfix-img` (git HEAD `27663a0`,
target `hisilicon/luofu`, profile `DEVICE_cudy_wr3000-v2`, arm_cortex-a9
musl, GCC 14.4.0).  **No image, kernel or flash build was run.**  The tree was
used under the project world-build flock and its `.config` was restored
byte-identically afterwards.

## 1. Why a package-only build

The serialized image-build lane had just closed its window with a
byte-identical image (container `df6a78c3...`); a second image build was out
of scope for this lane.  Both new package groups are additive userspace
selections, so a targeted per-package build is sufficient and does not touch
`target/linux` or the image.

## 2. Exact commands

    cd /home/shibbityshwab/openwrt-lab-selectorfix-img
    flock /tmp/cudy-openwrt-world-build.lock -c '... the block below ...'

    cp -a .config /tmp/hostapd-lane-build/config.before
    sha256sum .config          # 9052d5e3...
    # keep the validated kernel config proof before/after:
    sha256sum build_dir/target-arm_cortex-a9_musl_eabi/linux-hisilicon_luofu/linux-6.18.60/.config
                               # 08b6e595...

    cat >> .config <<'EOF'
    CONFIG_PACKAGE_hostapd-mini=y
    CONFIG_PACKAGE_hostapd-common=y
    CONFIG_PACKAGE_ucode-mod-nl80211=y
    CONFIG_PACKAGE_ucode-mod-rtnl=y
    EOF
    make defconfig V=s         # rc=0; selects the dependency closure

    # make defconfig drops the two trailing kernel symbols the validated
    # image config carries; re-append them so a later world build keeps the
    # hardware-validated kernel .config (sha 08b6e595...) unchanged.
    cat >> .config <<'EOF'
    CONFIG_KERNEL_DEVMEM=y
    CONFIG_KERNEL_HOTPLUG_CPU=y
    EOF

    make package/utils/ucode/compile V=s -j8            # rc=0
    make package/network/services/hostapd/compile V=s -j8   # rc=0

    cp -a /tmp/hostapd-lane-build/config.before .config
    sha256sum .config          # 9052d5e3... (byte-identical restore)

Note: `package/<dir>/install` is **not** a valid per-package target in this
tree (`DEFAULT_SUBDIR_TARGETS` in rules.mk has no `install`); the correct
single-package target is `compile`, and `include/package-pack.mk` makes
`compile` produce the `.apk` (`compile: $(PACK_<pkg>)`).  A first attempt
with `package/utils/ucode/install` failed with "No rule to make target" and
mutated nothing.

## 3. Result

`config.after` differs from the baseline only in the four package selections
(+ their `select`ed deps) and the hostapd Config.in defaults:
`CONFIG_WPA_MSG_MIN_PRIORITY=3`, `CONFIG_WPA_MBO_SUPPORT=y`,
`CONFIG_WPA_RFKILL_SUPPORT`/WEP unset, plus the two kernel symbols moved to
the end of the file.  `make defconfig` reordering is expected; the restored
baseline is byte-identical.

| artefact | size | sha256 |
| --- | ---: | --- |
| `hostapd-mini-2026.08.07~831364bf-r7.apk` | 283,790 B | `daf80bebc7495273dbd3a30627349b9063451183686fac314061ab92fb0ce10c` |
| `hostapd-common-2026.08.07~831364bf-r7.apk` | 1,922 B | `e064cd3f52859ce3bd11fcc6d863524821c471fe9a440f9c0e0769ccee70ef3f` |
| `ucode-mod-nl80211-2026.10.28~dfc64d41-r1.apk` | 24,228 B | `af25dfe5b7513d3a6a70082f3470abe2d329c641fc2f335357ff735e0558fa44` |
| `ucode-mod-rtnl-2026.10.28~dfc64d41-r1.apk` | 29,292 B | `685443d6e5a58a00940268ea94d3e2e5e50af090dd5ddad7a75b62d8a9a1f3c4` |

Produced under `bin/packages/arm_cortex-a9/base/`; a local convenience copy
is at `/home/shibbityshwab/hostapd-lane-artifacts/` (not in the repository;
the deploy tool also falls back to the tree's `bin/packages` path).

Installed payload (8 files, manifest in
[tools/expected-files.txt](tools/expected-files.txt)):

| file | size | note |
| --- | ---: | --- |
| `usr/sbin/hostapd` | 504,333 B | ELF32 ARM EABI5, PIE, musl, stripped |
| `usr/share/hostap/hostapd.uc` | 71,062 B | APUP ucode support script (hostapd-common) |
| `usr/lib/ucode/nl80211.so` | 61,442 B | ucode-mod-nl80211 runtime module |
| `usr/lib/ucode/rtnl.so` | 73,731 B | ucode-mod-rtnl runtime module |
| `etc/capabilities/wpad.json` | 407 B | hostapd-common capability manifest |
| `etc/init.d/wpad` | 1,262 B | procd init script; **not** enabled (no rc.d link) |
| `lib/netifd/dhcp-get-server.sh` | 47 B | hostapd-common helper |
| `usr/share/acl.d/wpad_acl.json` | 521 B | ubus ACL for wpad |

hostapd DT_NEEDED: `libubox.so.20261103 libubus.so.20261028
libblobmsg_json.so.20261103 libucode.so.20261009 libnl-tiny.so.1 libudebug.so
libgcc_s.so.1 libc.so` - all provided by baseline image packages
(`libubox20261103 libubus20261028 libblobmsg-json20261103 libucode20261009
libnl-tiny1 libudebug libgcc1 libc`).

## 4. Host hostapd build (config validation only, NOT a target artifact)

    ./tools/build-host-hostapd.sh --force
    -> /tmp/hostapd-host/src/hostapd/hostapd  (hostapd v2.12)

Built with the host cc from the tree's prefetched source tarball
`<owrt>/dl/hostapd-2026.08.07~831364bf.tar.zst`; wired/none drivers only.
It exists so the config fixtures are parsed by the same hostapd source
version the target package uses; it is never copied to the device.

## 5. Tree state after the build (verified)

* `.config` sha256 back to `9052d5e3...` (byte-identical to the pre-build
  baseline; `cmp` clean).
* kernel `.config` sha256 unchanged `08b6e595...` (no kernel regeneration).
* no target/linux, image or `bin/targets` rebuild.
* rootfs/package staging untouched except the four new package artifacts;
  no other lane artifact was modified.
* the hostapd source tarball in `dl/` was already prefetched by
  `make package/network/services/hostapd/download` (OpenWrt CDN mirror,
  sha256 pinned by the package Makefile) and is an additive download.

## 6. Lane checks (recorded in build-evidence/)

    ./deploy-hostapd.sh --check --evidence build-evidence/host-check.txt   # 87 passed, 0 failed
    ./tools/selftest.sh --evidence build-evidence/selftest.txt             # 14 passed, 0 failed
    ./tools/validate-configs.sh --evidence build-evidence/config-validation.txt  # 4 passed, 0 failed
    ./tools/check-ladder.py                                                # 107 passed, 0 failed

## 7. Not run

No device contact, no deploy, no module load, no hostapd start on the router,
no image build, no flash.  The attended T0 deploy and AP-start ladder are the
Lead's call ([DEPLOY.md](DEPLOY.md), [AP-START-LADDER.md](AP-START-LADDER.md)).
