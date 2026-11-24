# BUILD-NOTES - wifi-hostapd-sae (task-148)

**Host-only builds.  Nothing was flashed or booted by this lane; no device
contact, no UART, no network on the bench link.**  Recorded runs use the local
clock 2026-11-24 (the project's bench-wave docs date the hardware-validation
session 2026-11-27).

## 1. Environment pins

| item | value |
| --- | --- |
| shared OpenWrt tree | `/home/shibbityshwab/openwrt-lab-selectorfix-img` |
| tree git HEAD (asserted) | `27663a0` |
| target/profile | `hisilicon/luofu`, DEVICE_cudy_wr3000-v2 |
| kernel | linux-6.18.60, OpenWrt GCC 14.4.0 arm_cortex-a9 musl-eabi |
| tree .config (asserted, pre/post) | sha256 `9052d5e36fd30e6c59276910f2b286d5a109a5dffd03f3a8e1386aece0ed075d` |
| built kernel .config (asserted pre/post) | sha256 `08b6e59511ab5e568a6afb73da22958283a648c6fffe4987f6d0a245066f676c` |
| target kernel config | sha256 `ecf1aae26a527737ac94ad62392181e92be1cffe5c03d0f46c16b05cdc9be41f` |
| hostapd source tarball | `$OWRT/dl/hostapd-2026.08.07~831364bf.tar.zst` (sha256 d1b9ca0e6aaaba928fc5d5b02c0084083e745a38ae1214d0e2dc5d1ea6d50393; prefetched, not downloaded by this lane) |
| world-build lock | `/tmp/cudy-openwrt-world-build.lock` (held by every script) |
| qemu-arm (gate only, not a target artifact) | `/tmp/qemu-user/qemu-arm-static` v7.2.0, 9,043,024 B sha256 `9f07762a3cd0f8a199cb5471a92402a4765f8e2fcb7fe91a87ee75da9616a806` (downloaded once from the public multiarch/qemu-user-static release; local verification tool only) |
| local artifact dir | `/home/shibbityshwab/wifi-hostapd-sae-artifacts/` (never committed) |

## 2. Package build (tool: tools/build-hostapd-sae-package.sh)

What it does: snapshots the shared tree, appends
`config-hostapd-mbedtls.config`, runs `make defconfig`, purges the
`hostapd-full-mbedtls` build dir for a fresh compile, builds mbedtls + the
hostapd full-mbedtls variant, proves the SAE build, copies the artifacts to
the local artifact dir, restores .config/kernel .config byte-identically.

``
./tools/build-hostapd-sae-package.sh
`~

Resolved config symbols asserted: `CONFIG_PACKAGE_hostapd-mbedtls=y
CONFIG_PACKAGE_libmbedtls=y CONFIG_PACKAGE_hostapd-common=y
CONFIG_WPA_MBO_SUPPORT=y` and `# CONFIG_PACKAGE_hostapd-mini is not set`.
`make defconfig` also activated `CONFIG_WPA_MSG_MIN_PRIORITY=3` and the
usual WPA defaults, same as the wifi-hostapd-deploy lane recorded.

Result (recorded 2026-11-24T15:09:50Z, evidence/package-build.txt):

| artefact | size | sha256 |
| --- | ---: | --- |
| build-tree binary (unstripped) | 1,214,784 B | `3baf5aa15e3786c625a88ffa3a53fc9f265d4c7dcd6ae3cfb9511f5f70fff614` |
| .pkgdir payload (same bytes) | 1,214,784 B | same |
| `hostapd-mbedtls-2026.08.07~831364bf-r7.apk` | 554,612 B | `b3c95eb6cffcc8de11723125fba3b54f1b51c485a68af4970dbcb463baaa5a6f` |
| image-installed (rstrip.sh stripped) | 1,032,951 B | `451dd49f0e759cd6da842aafc479fae6ad80ee094d498b270b263a9bddfb7469` |
| `hostapd-common` apk (unchanged from task-147) | 1,922 B | `e064cd3f52859ce3bd11fcc6d863524821c471fe9a440f9c0e0769ccee70ef3f` |
| `libmbedtls21` apk (baseline image package, unchanged) | 289,811 B | `7e0fdef54b2535d15f5b62ad1806b9014c1ff2339ba29f383b1874559ea67f42` |

Build log facts: `-DCONFIG_SAE` 151x, `src/common/sae.o` compiled,
`CONFIG_TLS=mbedtls` on the make command line, 13 `SAE:` strings, DT_NEEDED
mbedtls trio.  Full curated log: evidence/package-build-log.txt.

## 3. Image bakes (tool: tools/bake-image-with-hostapd-sae.sh)

Recorded bakes:

| run | variant | start | duration | result |
| --- | --- | --- | ---: | --- |
| 1 | A | 2026-11-24T15:14:05Z | 12 s | PASS (37/37 verify) |
| 2 | B | 2026-11-24T15:14:21Z | 13 s | PASS (37/37 verify) |

The bake sequence (exactly what the script runs, same mechanics as
task-147's bake-final-image.sh):

1. preflight: pinned driver artifact (size/sha/modinfo), tree HEAD, target
   kernel config sha, built kernel .config sha, no stray make, no files/
   overlay; take the world-build flock;
2. snapshot .config, built kernel .config and the target-root staging tree;
3. stage the task-147 rootfs payload, then this lane's fixtures over it
   (the stale transition fixture is superseded by the wpa3-sae validated one);
4. install the prebuilt `kmod-hi5622-wifi` package (no compile) and purge its
   package build dir so the pinned .ko is re-staged;
5. append the bench delta: `wifi-image-integration/config-delta.config` with
   its `CONFIG_PACKAGE_hostapd-mini=y` line replaced by
   `CONFIG_PACKAGE_hostapd-mbedtls=y`, then `make defconfig` + the
   kernel-symbols carry-over (`CONFIG_KERNEL_HOTPLUG_CPU=y`); assert the
   expected symbols resolved and that mini is unset;
6. `make -j16` world (kernel compile skipped because the validated kernel
   .config did not change), extract the container members, assert the kernel
   slice is byte-identical (13e81214.../3,372,638 B);
7. run tools/verify-image-with-hostapd-sae.sh on the built bytes and record
   hashes;
8. copy local artifacts, then restore: files/ overlay removed, .config and
   built kernel .config byte-identical, target-root staging tree from the
   pre-build tar.

Manual equivalent for a one-off investigation:

``
flock /tmp/cudy-openwrt-world-build.lock -c '...'
# (only if you know what you are doing - prefer the script)
`~

## 4. Verification (tool: tools/verify-image-with-hostapd-sae.sh)

37 checks, PASS for both variants:
kernel slice unchanged (sha+size), rootfs UBI magic, UBI volume 0 ==
build root.squashfs prefix + zero padding, shipped hostapd is ARM EABI5 and
NOT the old mini binary, mbedtls DT_NEEDED + runtime libs present, 13 SAE
strings, three fixtures byte-identical to payload, installed module identity
(variant A pinned `f07dd505...`; variant B `c7124bb9...`, name/vermagic/
cfg80211/47 params), manifest has `hostapd-mbedtls`/`libmbedtls21`/
`kmod-hi5622-wifi` and no `hostapd-mini`, only `regulatory.db` under
/lib/firmware, no vendor FIRMWARE*.bin, rc.d link + autoload=0 policy.

Reproducibility: image A was baked twice (22:12 and 22:14 local) and both
times produced container sha `a31bc405...` byte-identically; the verifier
also re-derived the same rootfs hashes.  The two bakes differ only in the
driver module bytes (308,080-byte A vs 311,108-byte B), exactly as designed.

## 5. Functional gate (tool: tools/validate-wpa3-configs.sh)

Runs the shipped ARM hostapd under qemu-arm v7.2.0 against the rootfs
extracted from the built container (A and B roots are identical for
hostapd+fixtures): 14/14 checks, including the two negative controls (broken
config refused; old hostapd-mini refuses SAE).  Evidence:
evidence/qemu-config-gate-A.txt / -B.txt.

Emulation aids used (verification only): static qemu-arm + a temporary
`/lib -> ../usr/lib` symlink farm inside the extracted rootfs (qemu's `-L`
fallback would otherwise pick up host x86_64 libraries with the same sonames,
e.g. host mbedtls 3.6.7).  The image bytes are never modified.

## 6. Shared-tree state after this lane (verified)

* `$OWRT/.config` sha256 `9052d5e3...` (byte-identical to before).
* built kernel .config sha256 `08b6e595...` (unchanged; no kernel rebuild).
* no `$OWRT/files/` overlay left.
* `package/hi5622-wifi/` staged driver package RESTORED to the task-147
  FINAL all-patch artifact (308,080 B sha `5b587949...`) so the task-147
  tooling keeps working; re-running the bake script re-stages the selected
  module for the bake and this lane re-restores A after its last bake.
* world-build lock released.
* Lab-tree residue (expected, untracked build outputs): the new
  `bin/packages/arm_cortex-a9/base/hostapd-mbedtls-2026.08.07~831364bf-r7.apk`;
  the `build_dir/.../hostapd-full-mbedtls/` compile tree; the last baked
  `bin/targets/hisilicon/luofu/` container (variant B) and its package
  manifest; hostapd-mbedtls installed into
  `staging_dir/target-arm_cortex-a9_musl_eabi/root-hisilicon/` (staging is
  rebuilt per selected package on the next world build; the image root staging
  tree was restored by the bake).

## 7. Evidence index

| evidence file | content |
| --- | --- |
| evidence/package-build.txt | package build record: SAE proof, DT_NEEDED, artifact identities, tree pins |
| evidence/package-build-log.txt | curated compile log (SAE/TLS/mbedtls/hostapd link lines) |
| evidence/config-delta-A.diff, config-delta-B.diff | .config diff applied for each bake |
| evidence/image-A.txt, image-B.txt | image records: container/rootfs/squashfs/module hashes, kernel-unchanged proof, rc |
| evidence/image-A-verify.txt, image-B-verify.txt | verifier PASS/FAIL/RECORD lines (37/37) |
| evidence/qemu-config-gate-A.txt, -B.txt | shipped-binary functional gate (14/14) + negative controls |

## 8. What the builds do NOT prove

Not flashed, not booted, no nl80211/driver behavior, no SAE association, no
on-air beacon/rsn-ie check.  These remain bench gates (Lead, attended); see
../wifi-wpa3-sae/LADDER.md and ../wifi-final-image/FLASH-FEASIBILITY.md.
