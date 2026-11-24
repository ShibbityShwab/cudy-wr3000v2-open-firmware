# REPORT - T0 zero-flash apk deploy for native iw/iwinfo + the clean-room driver

**Lane:** img-deploy (shared task task-119), host-only. **Date:** 2026-11-26.
**Status:** the deploy path is **VERIFIED FROM THE BUILT BYTES HOST-SIDE** (package
format, dependency graph, kernel-ABI gate, install payload identity, rollback
identity, fail-closed negative test); the host+device tooling and selftest are
delivered. **Nothing in this lane was deployed to or executed on the router.**
The attended deploy is the Lead's call; the bench validation is defined in
BENCH-VALIDATION.md.

Inputs: the built bench image (wifi-image-integration, task-112): container
6,533,439 B sha256 df6a78c3...; kernel slice **unchanged** 3,372,638 B sha256
13e81214... (the hardware-validated slice); rootfs UBI 3,145,728 B sha256
62344e80...; manifest 81 packages = 74 baseline + the 7 bench packages. The
local convenience copy of the artifacts (container, kernel slice, rootfs UBI,
seven .apk files, SHA256SUMS) is
/home/shibbityshwab/wifi-image-integration-artifacts/ - it is not committed.

## 0. TL;DR

* **The image is apk-based** (OpenWrt CONFIG_USE_APK=y, apk-mbedtls 3.0.5-r4,
  /usr/bin/apk + /lib/apk/db; no opkg), confirmed from the built image bytes.
* The seven bench .apk files are valid **ADB v3** packages for
  **arm_cortex-a9**, decode with the tree's host apk-tools 3.0.5, and carry the
  expected dependencies including **kernel=6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1**
  for both kmods.
* That kernel dependency hash is **recomputed host-side from the 6.18.60 tree's
  own .config.set** (md5 of the sorted =[ym] lines = 6f995278..., the same
  algorithm OpenWrt uses in include/kernel-defaults.mk:131) and matches the
  tree .vermagic - so the packages are ABI-pinned to the *validated* kernel
  slice, not just to "some 6.18.60".
* **The install is provably the right one and reversible:** starting from the
  real built-image rootfs, a host apk transaction del -> add -> del reproduces
  *byte-identical* payload trees (built image / pre-T0 baseline / built image
  again), 81 <-> 74 packages, with the exact command flags the device will run;
  re-running add is a no-op; a mismatched installed kernel package makes apk
  fail closed (rc=23) naming the kernel= constraint. 61/61 full-set
  simulation gates PASS (tools/verify-apks-host.sh), 53/53 for the
  userspace-only set, plus 95 selftest gates.
* **Deploy shape:** all seven packages go into the RUNNING pair-B rootfs through
  the persistent overlay; no flash write, no boot-selector change, no reboot,
  no rmmod, no /dev/mem, no UART. Rollback is one apk del. The scripts refuse
  to act unless --attended --yes-really is given and the bench link + recovery
  door are READY.
* **One real finding that breaks the predecessor recipe:** apk v3 refuses to
  install a local file package without **--force-non-repository** ("would be
  lost on next reboot"); the plain "apk add --allow-untrusted /tmp/*.apk"
  recipe in ../wifi-image-integration/DEPLOY.md T0 step 2 would fail. The
  deploy script uses the verified flag set and pins integrity by sha256 anyway.
* **Hardware follow-up (2026-11-26):** the Lead's attended run executed with
  full preflight, **failed closed at the kernel-ABI gate** and auto-rolled
  back cleanly (28 device gates / 21 rollback gates PASS, nothing left behind).
  Root cause: the running image's kernel package is
  **6.18.60~5de0e425f1313eeec82615317fdc1f76-r1**, the kmod .apk files pin
  **6.18.60~6f995278...-r1** (the tree was rebuilt after the flashed image).
  This is APK build-provenance bookkeeping, not an ELF-ABI mismatch: the
  flashed kernel slice is the same validated **13e81214** bytes, so the module
  vermagic matches. Two consequences are delivered here:
  **--userspace-only** (native iw/iwinfo now, no kmod pin) and section 7 (the
  two kmod-ABI paths, with the canonical one named).

## 1. Verified package set (the exact seven artifacts)

Hashes are authoritative (embedded in deploy-t0.sh and
tools/expected-packages.txt, cross-checked against the artifacts SHA256SUMS and
BUILD-NOTES.md; SHA256SUMS self-lists itself, so its own line is excluded from
sha256sum -c):

| package | version | .apk B | sha256 (12) | installed B |
| --- | --- | --- | --- | --- |
| iw | 6.17-r1 | 52,997 | 9b46713fb5a0 | 116,868 |
| iwinfo | 2026.05.26~66bdd1a0-r1 | 8,322 | 5badc21509df | 16,922 |
| libiwinfo20230701 | 2026.05.26~66bdd1a0-r1 | 25,768 | d1178900f817 | 57,408 |
| libiwinfo-data | 2026.05.26~66bdd1a0-r1 | 3,925 | 3bb216a4f2ae | 18,264 |
| wireless-regdb | 2026.09.03-r1 | 3,440 | afd292e0c094 | 6,360 |
| kmod-hi5622-cfg80211 | 6.18.60-r1 | 183,520 | 716fd384cc7a | 406,061 |
| kmod-hi5622-wifi | 6.18.60-r1 | 59,302 | aac8f3fa0f23 | 133,920 |

Total .apk transfer 337,274 B; installed footprint 755,803 B (+ db/list
bookkeeping). The deploy gates overlay free space at >= 4 MiB.

Installed payload (verified byte-identical to the built image, and the list the
device re-checks after apk add, see tools/expected-files.txt):

| path | sha256 (12) | B |
| --- | --- | --- |
| usr/sbin/iw | e0f4ba8cb1dd | 116,855 |
| usr/bin/iwinfo | 88fb49430a9a | 16,906 |
| usr/lib/libiwinfo.so.20230701 | aa35d698dfb5 | 57,377 |
| usr/share/libiwinfo/devices.txt | dcaaa350ccc7 | 18,231 |
| lib/modules/6.18.60/cfg80211.ko | 11a5e2b57f31 | 406,028 |
| lib/modules/6.18.60/hi5622_wifi.ko | e0608fa6e148 | 133,884 |
| lib/firmware/regulatory.db | c682b8e0f2f6 | 6,332 |

ABI facts read from the packaged modules: both carry vermagic
**6.18.60 SMP mod_unload ARMv7 p2v8**; hi5622_wifi declares
**depends=cfg80211**; cfg80211 is the module built from
target/linux/hisilicon/luofu/config-6.18 (CONFIG_CFG80211=m), not the mac80211
backports package (../wifi-image-integration/REPORT.md 2.1). Userspace runtime
closure was checked with readelf -d against the built rootfs: iw ->
libnl-tiny/libgcc_s/libc; iwinfo and libiwinfo -> libiwinfo/libnl-tiny/libuci/
libubox/libubus/libgcc_s/libc, all present under /lib or /usr/lib (musl's
default loader paths).

## 2. What was actually proven host-side

### 2.1 Kernel-ABI gate (compile-verify against the 6.18.60 tree)

* The tree build dir's .config.set is the exact kernel configuration the
  validated slice was built from. Recomputing OpenWrt's vermagic
  (grep '=[ym]' .config.set | LC_ALL=C sort | md5) yields
  **6f995278d21563ed4dd8f2609f3659c8**, identical to the tree .vermagic file and
  to the kernel= dependency of both kmod packages.
* deploy-t0.sh --check recomputes this at deploy time and fails closed if the
  tree, the packages and the expected hash disagree.
* The APK payload .modinfo of hi5622_wifi.ko (name/vermagic/depends/license) is
  identical to the tree's package build product; the tree's out-of-tree product
  is 505,748 B sha256 34058618..., the packaged installed module is the stripped
  133,884 B sha256 e0608fa6... (OpenWrt does the strip inside the package
  build; the payload identity is proven against the built image, not assumed).
* On the device the real gate is the installed kernel package:
  "apk query --fields version kernel" must return
  6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1, otherwise apk itself refuses the
  kmod install; the negative case was reproduced host-side (rc=23, log names the
  kernel= constraint). The kernel slice is byte-identical to the
  hardware-validated 13e81214... slice, so the running pair-B image is expected
  to carry the same kernel package version - the device gate decides.

### 2.2 Install/rollback identity (tools/verify-apks-host.sh)

Method (all from bytes, no device): extract the container's root member -> UBI
volume 0 -> squashfs -> unsquashfs = the **built image rootfs** (81 packages);
delete the seven packages with the tree's host apk to reconstruct the **pre-T0
state** (74 packages, exactly the seven payload files gone); then run the exact
device command (--allow-untrusted --force-non-repository --network=no
--repositories-file /dev/null, --scripts=no because the host cannot execute the
arm chroot hooks) and compare full payload sha256 manifests.

Results (61/61 gates PASS): container/kernel-slice hashes match; built rootfs =
81 packages and all seven payload files with the expected hashes; baseline = 74
packages and exactly 7 payload lines removed; **install payload tree is
byte-identical to the built image**; all seven versions and the kernel= db entry
present; **second add is a no-op**; **rollback payload tree is byte-identical to
the baseline**; negative kernel-mismatch install fails closed (rc=23); control
add on the untouched baseline succeeds. Transcript:
build-evidence/host-apk-simulation.txt.

### 2.3 Package hooks and format

* Format: "ADBd" (apk-tools v3) container; the tree's host apk-tools 3.0.5
  decodes it (adbdump); the running image ships the same apk-tools 3.0.5
  (armel) built from the same tree, and the deploy gates major version 3.
* Every package carries the OpenWrt-generated hooks: post-install ->
  add_group_and_user + default_postinst, pre-deinstall -> default_prerm,
  post-upgrade for upgrades. There is **no custom code** in them; they only
  call /lib/functions.sh helpers (conffile handling / user-group creation; none
  of these packages declares a user, a group or a conffile). deploy-t0.sh
  extracts these hook sections from adbdump and refuses if any contains a
  device/flash/module operation. No package ships a trigger.
* Host simulation runs with --scripts=no (arm chroot impossible on the host);
  the on-device deploy exercises the stock hooks natively. The hooks are not a
  safety risk: they cannot access the chip, MMIO, flash or the boot selector.
* apk's repository logic is disabled for the transaction (--network=no
  --repositories-file /dev/null), so the bench link's lack of internet cannot
  stall or redirect the install.

## 3. Findings

**F1 - apk v3 needs --force-non-repository for local .apk files (measured).**
"apk add --allow-untrusted /tmp/iw.apk" fails with "non-repository package ...
would be lost on next reboot", rc 99/1. The verified install command is
"apk add --allow-untrusted --force-non-repository --network=no
--repositories-file /dev/null <7 files>". The "lost on next reboot" warning is
Alpine diskless-mode semantics; here the package and its world entry are
committed to /lib/apk/db in the persistent overlay (proved by the rollback
identity and verified again across a reboot in BENCH-VALIDATION.md). The
predecessor recipe in ../wifi-image-integration/DEPLOY.md T0 step 2 would have
failed - fixed here.

**F2 - the running rootfs lacks iw/iwinfo/regdb/.kos; T0 adds exactly those.**
The previous cfg80211 image (container 22b72852..., rootfs UBI b33b1d91...) was
built before the package delta; the bench sessions pushed stock iw + libnl-tiny
+ the modules to /tmp every time. T0 makes the same bytes part of the rootfs so
the bench no longer depends on /tmp staging.

**F3 - no autoload by design.** No package installs an /etc/modules.d entry;
nothing releases the chip at boot. Module load stays a manual attended step
(bench policy).

**F4 - integrity gate is sha256, not package signatures.**
The .apk files are signed by the build tree's local key; the running image's
/etc/apk/keys may not carry that key (apk verify on the host reports "UNTRUSTED
signature" without the build keys), so the deploy uses --allow-untrusted.
Integrity comes from the pinned sha256 list embedded in the scripts + the remote
sha256sum -c re-read before anything is installed + the post-install read-back
hashes. A tampered package fails the host check (covered by selftest).

**F5 - overlay precondition.** T0 only makes sense on the persistent overlayfs
image. The device script refuses if / is not overlayfs, if /overlay is not
mounted, or if free space < 4 MiB - in those cases use the image-rewrite path
(T1, ../wifi-image-integration/DEPLOY.md) instead.

**F6 - the deploy is not a functional driver validation.**
The package install + a write-free module probe (--load-check) prove the rootfs
carries loadable, ABI-correct modules and native iw/iwinfo. Wiphy registration
needs the attended golden boot (ready handshake) and is validated in
BENCH-VALIDATION.md, not here.

## 4. Safety analysis (what the deploy can and cannot do)

Writes are limited to overlay files: the seven payload paths above,
/lib/apk/db (installed, world, scripts.tar.gz, ...), /lib/apk/packages/*.list,
and a /root/.wifi-t0-deploy.receipt receipt. Staging is under
/tmp/t0-deploy-<stamp>/ and is removed on success.
The scripts contain **no** mtd/ubiformat/nvram/sysupgrade command, no
boot-selector write, no reboot, no rmmod, no /dev/mem//devmem, no UART access;
selftest asserts this by command-position scan. --apply/--rollback/--verify
require --attended --yes-really and host preflight (tools/bench-link.sh PASS +
tools/recovery-card.sh READY). A failure after apk add triggers an automatic
apk del rollback; rollback is also a first-class command. Loaded modules are
never unloaded (no rmmod); a reboot clears them.
The deploy script additionally takes an exclusive lock
(/tmp/cudy-t0-deploy.lock via flock when available) so two deploys cannot
interleave.

## 5. Delivered files

| file | what |
| --- | --- |
| deploy-t0.sh | host orchestrator: --check (default, no device), --apply, --rollback, --verify, --load-check, --userspace-only |
| device/t0-device.sh | device-side modes preflight/install/rollback/load/verify with T0GATE/T0KV/T0-RESULT output |
| tools/verify-apks-host.sh | host-only byte-level transaction simulation |
| tools/selftest.sh | device-free lane selftest (syntax/lint/policy/manifests/mocks/real simulation) |
| tools/expected-packages.txt, tools/expected-files.txt | pinned artifact + payload manifests (sha256) |
| build-evidence/host-apk-simulation.txt | recorded 61/61 PASS transcript (full set) |
| build-evidence/host-apk-simulation-userspace.txt | recorded 53/53 PASS transcript (userspace-only set) |
| build-evidence/host-check.txt | recorded deploy-t0.sh --check transcript on the local artifacts/tree |
| build-evidence/host-check-userspace.txt | recorded deploy-t0.sh --check --userspace-only transcript |
| BENCH-VALIDATION.md | attended validation definition (fresh boot, native iw/iwinfo/driver, D1 ladder) |
| README.md | quick start + file map |

Reproduce (host-only, no router):

    ./tools/selftest.sh
    ./deploy-t0.sh --check
    ./tools/verify-apks-host.sh --evidence build-evidence/host-apk-simulation.txt

## 6. Honest limits

* Host-only lane: the running rootfs was not inspected directly; the on-device
  preflight gates (uname -r, apk v3, overlay, installed kernel version, free
  space, conflicting packages) decide at deploy time. The host reconstruction
  starts from the *new* bench image; the assumption is that the running image is
  that image minus the seven packages (same tree, same kernel slice, same
  baseline 74 packages). Any divergence is caught by the device preflight,
  which fails closed.
* The install was not executed on the router by this lane; the recorded
  evidence is host-simulated. The one place host and device differ is the
  package hook execution (arm chroot): the hooks are stock OpenWrt boilerplate,
  verified by content inspection, and cannot touch hardware.
* T0 is a bench convenience and reversible; it is not a product upgrade path
  and does not prove the image boots (that is BENCH-VALIDATION.md / task-112's
  attended T1). The vendor firmware blob is still local evidence only and is
  never shipped in a package.
* The driver's functional state (wiphy/VAP/scan) is unchanged by T0; T0 only
  moves the already-used binaries into the rootfs.

## 7. Hardware follow-up (2026-11-26): kernel-ABI mismatch + the two paths

### 7.1 What the attended run measured

The Lead ran the deploy with full preflight. It **failed closed exactly where it
should** and left the box unchanged:
* device pre-install gates: image/apk/overlay/space/staging all PASS, then
  T0KV kernel_package=6.18.60~5de0e425f1313eeec82615317fdc1f76-r1 ->
  the kernel-dep gate FAILed against the kmod pin 6.18.60~6f995278...-r1, so
  apk add was never executed; automatic rollback then PASSed 21/21 (all seven
  payload files absent, packages absent, receipt absent). Evidence:
  build/evidence-t0-deploy/25691124T090251Z/ (root repo, gitignored).
* The running image was built before the current tree's kernel configuration
  settled: OpenWrt's kernel package version is
  **md5 of the sorted =[ym] kernel-config lines** (include/kernel-defaults.mk:131,
  .vermagic). Two builds can therefore have different kernel package hashes
  while embedding the **same kernel slice bytes** - which is the case here
  (both are the validated 13e81214 slice). The two kmod .apk files carry
  vermagic "6.18.60 SMP mod_unload ARMv7 p2v8", i.e. they are loadable on that
  kernel; what apk refuses is the *build-provenance* pin, and forcing past it
  is not acceptable.
* Later on the same bench the Lead installed the five non-kmod packages
  manually (apk add --force-non-repository, rc=0, 79 packages): native
  /usr/sbin/iw 6.17 and /usr/bin/iwinfo now run from the persistent overlay and
  iwinfo already enumerates wlan0. That is exactly the userspace-only set this
  lane now ships as a mode, so its verify path is the idempotent check for the
  box's current state.

### 7.2 Userspace-only mode (native iw/iwinfo now, no kmod ABI)

    ./deploy-t0.sh --userspace-only --apply --attended --yes-really
    ./deploy-t0.sh --userspace-only --verify --attended --yes-really
    ./deploy-t0.sh --userspace-only --rollback --attended --yes-really

* Installs/verifies/rolls back only iw, iwinfo, libiwinfo, libiwinfo-data and
  wireless-regdb (the five packages with **no kernel= dependency**). The
  device script filters every kmod row and every /lib/modules payload out of
  the full manifests (--set userspace), skips the kernel-dep and module-static
  gates (recording the running kernel package instead), runs the same
  apk add --simulate resolve pre-gate + apk add/read-back/rollback path, and
  refuses load mode ("--load-check needs the full set").
* Host-verified: host check 40/40 PASS (every userspace package proven to have
  no kernel= dependency), transaction simulation 53/53 PASS (install payload ==
  built image minus the two module payloads, no modules installed, five payload
  sha256 read-backs, idempotent re-add, rollback byte-identical to baseline,
  negative "kmod-free set" gate). Selftest covers the mocked userspace flow
  too (95/95 PASS).
* No autoload, no reboot, no flash: same safety envelope as the full set.
  Native iw/iwinfo become available immediately; the modules stay in the
  proven /tmp insmod flow until the ABI path below is taken.

### 7.3 Closing the kmod ABI - two paths, canonical decision

**P1 - rebuild the image from the current tree and flash it: CANONICAL.**
One tree state -> one kernel build -> one kernel package version (currently
6f995278), and the rootfs apk db, the kmod .apk pins and the kernel slice all
agree. Concretely: rebuild the bench image (kernel slice is expected to
reproduce the validated 13e81214 bytes; the rootfs carries kernel=6f995278),
then install it in an attended window (T1 rootfs rewrite in
../wifi-image-integration/DEPLOY.md, or kernel slice + rootfs from the same
build). Important: writing only the kernel slice does NOT change the installed
kernel package version - the "kernel=..." string lives in the rootfs apk db,
not in the kernel image - so the rootfs built with that kernel package must be
written too (even though the kernel bytes are expected to be the same validated
13e81214). After that the full-set T0 deploy (kmods) works and stays consistent
across reboot. This is canonical because it is reproducible from the tree, it
is what a real image does, it keeps APK's dependency checking meaningful, and
it matches the upstream/target direction (the target/linux config is the
source of the kernel package version).
**P2 - rebuild the two kmod .apk files against the running image's kernel
package version (5de0e425): fallback, not canonical.**
The wifi-openwrt-pkg lane did this for its own package (its .apk pins
6.18.60~5de0e425...), so the technique exists. But that pin describes an old
build's kernel configuration that the tree no longer reproduces; every kmod
would have to be rebuilt and re-verified whenever the running image, tree or
kernel config changes, and it deliberately bypasses the self-consistency the
package metadata is there to enforce. Acceptable only as a local, clearly
labelled bench artifact for a short sprint; never merge it as the image's
dependency policy and never force apk past the kernel= dependency.
Interim (safe today): userspace-only T0 for native iw/iwinfo + the proven
/tmp insmod flow for the two modules; both are hardware-validated paths and
leave the eventual P1 decision open.
