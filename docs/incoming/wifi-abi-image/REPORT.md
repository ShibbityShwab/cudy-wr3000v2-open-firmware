# REPORT - kmod-ABI image rebuild + attended kernel+rootfs flash plan

**Lane:** wifi-abi-image (shared task task-128), host-only. **Date:** 2026-11-26.
**Status:** the bench image has been **rebuilt from the current tree and verified from its
bytes**, and the attended flash procedure is written (FLASH-RUNBOOK.md). **Nothing in this lane
was flashed, booted or executed on the router.** The attended install is the Lead's call with the
recovery door armed.

**Result in one line:** the current tree reproduces the recorded bench image **byte-for-byte**
(container `df6a78c3...`), that image's apk db carries **kernel = 6.18.60~6f995278...-r1** (the
kmod .apk pin), and the running box still carries the **older rootfs** with
`6.18.60~5de0e425...-r1` - the kernel slice is identical (13e81214...), so the fix is to flash the
rootfs of the image we already had, from the same build as the kernel. The build was never the
gap; the install was.

---

## 1. The defect and the two paths (context from ../wifi-image-deploy/REPORT.md section 7)

OpenWrt's kernel package version is **md5 of the sorted `=[ym]` kernel-config lines**
(`include/kernel-defaults.mk:131`; the tree's `.vermagic`). The kmod packages built by the
Wi-Fi lanes declare `kernel=6.18.60~6f995278...-r1` (the current tree's kernel configuration);
the running pair-B rootfs was built before that configuration settled and its
`/lib/apk/db/installed` says `kernel = 6.18.60~5de0e425...-r1`. Apk therefore **correctly refused**
the T0 full-set install on 2026-11-26 (fail-closed, auto-rollback, box unchanged), while the ELF
ABI was in fact fine (same validated kernel slice, same module vermagic).

* **P1 (canonical, done here):** rebuild the image from the current tree and flash kernel+rootfs
  from the one build - the rootfs apk db, the kmod pins and the kernel slice then all agree.
* **P2 (rejected):** rebuild the kmods against the old `5de0e425` provenance. It bypasses the
  dependency check the package metadata exists to enforce and must be redone on every tree change.

The kernel= string lives in the **rootfs**, not in the kernel image, so a kernel-slice-only write
cannot fix it: the rootfs has to be written.

## 2. Rebuild: inputs, serialization, and byte-identical reproduction

Tool: `tools/build-abi-image.sh` (host-only; reuses the validated inputs of the wifi-image-integration
lane read-only, stores all evidence in this lane). One world build, serialized with the shared tree
lock `/tmp/cudy-openwrt-world-build.lock`; the tree's `.config` and rootfs staging tree are
snapshotted and **restored byte-identically** afterwards (also on failure, EXIT trap).

| gate | value | expected |
| --- | --- | --- |
| tree | `/home/shibbityshwab/openwrt-lab-selectorfix-img` HEAD `27663a0` | same tree that built the validated image |
| pre-build `.config` sha256 | `9052d5e3...ed075d` | `9052d5e3...ed075d` |
| driver source snapshot | 29 files, manifest `10582bb4...948ca` | `10582bb4...948ca` |
| resolved `.config` sha256 | `b377f6c9...f932` | `b377f6c9...f932` |
| target kernel config `config-6.18` | `ecf1aae2...be41f` before/after | unchanged |
| built kernel `.config` sha256 | `08b6e595...676c` before/after | `08b6e595...676c` |
| world build | `make -j16`, rc=0, 24 s | rc=0 |

**Reproduction result (the strong claim):** the build produced the **same image bytes** as the
recorded bench image - container `df6a78c3...` (6,533,439 B), kernel slice `13e81214...`
(3,372,638 B), full kernel slot `1794a531...`, rootfs UBI `62344e80...` (3,145,728 B), squashfs
`f58ee016...`, and all seven deployable `.apk` files with the hashes recorded in
`../wifi-image-deploy/tools/expected-packages.txt`. The current tree therefore reproducibly
describes the validated image, and no new kernel/image bytes are needed for the fix.

## 3. From-bytes ABI verification (`tools/verify-abi-image.sh`, 25/25 PASS)

Everything below is recomputed from the **built container**, not trusted from the build:

* kernel slice sha256 == `13e81214...` and full 8,650,752-byte kernel slot sha256 ==
  `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e` (the flash read-back
  contract);
* tree kernel `.config.set` -> vermagic `6f995278d21563ed4dd8f2609f3659c8` == tree `.vermagic`
  == the kmod .apk pin;
* **image `/lib/apk/db/installed`: `P:kernel V:6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1`** -
  this is the line the device-side kernel-dep gate reads;
* image-installed `kmod-hi5622-cfg80211` and `kmod-hi5622-wifi` depend on
  `kernel=6.18.60~6f995278...-r1`; the built `kmod-hi5622-*.apk` artefacts carry the same
  dependency (`apk adbdump`);
* packaged `cfg80211.ko` / `hi5622_wifi.ko`: vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`,
  driver `depends=cfg80211`; both shipped in `/lib/modules/6.18.60/`;
* bench payload present: `usr/sbin/iw`, `usr/bin/iwinfo`, `libiwinfo`, `wireless-regdb`;
* rootfs UBI reassembles to a squashfs whose volume equals `build_dir/.../root.squashfs` +
  ubinize padding; manifest/package count 81 (74 baseline + 7 bench packages).

Evidence: `build-evidence/abi-verify.txt`. The independent wifi-image-integration verifier also
PASSes on the same container (`build-evidence/integration-verifier.txt`).

## 4. Host kmod-install simulation against the rebuilt rootfs (68/68 PASS)

The proven byte-level transaction simulator of `../wifi-image-deploy/tools/verify-apks-host.sh`
was run against this build's artefacts (same pinned container + seven .apk): it reconstructs the
image rootfs, deletes the seven packages to get the baseline, and runs the exact device apk
transaction flags. Result: **68/68 PASS** - install payload byte-identical to the built image,
rollback byte-identical to the baseline, second add a no-op, and the negative kernel-mismatch
case still fails closed (rc=23, log names the kernel= constraint). Transcript:
`build-evidence/host-apk-simulation.txt`.

This is the host-side mirror of the on-device gate that failed on 2026-11-26: against **this**
rootfs the kernel package version equals the pin, so the full set installs; against the running
(older) rootfs it does not. The device still decides; the runbook's post-boot receipt
(`apk query --fields version kernel`) is the acceptance check.

## 5. Deliverables

| file | what |
| --- | --- |
| `tools/build-abi-image.sh` | serialized, locked rebuild from the current tree + evidence + tree restore |
| `tools/verify-abi-image.sh` | from-bytes kernel/ABI/image verifier, extracts the flashable members |
| `tools/selftest.sh` | device-free lane contract checks (scripts, runbook anchors, pinned hashes, evidence) |
| `FLASH-RUNBOOK.md` | attended stock-assisted kernelb+rootfsb flash: preconditions, exact commands, ABI receipt, stop rules, rollback, manual appendix |
| `build-evidence/` | build summary, ABI verifier, integration verifier, config delta/resolved, host apk simulation, driver install |
| `build/abi-image-artifacts/` (local, gitignored) | container, kernel-slice.bin, rootfs.ubi, root.squashfs, 7 .apk, SHA256SUMS |

Reproduce (host only, no router):

    ./tools/build-abi-image.sh            # serialized world build + both verifiers
    ./tools/selftest.sh                   # lane contracts + recorded evidence
    ./tools/verify-abi-image.sh --artifacts build/abi-image-artifacts

## 6. Honest limits

* **Nothing was flashed or booted by this lane.** The procedure is handed to the Lead; it needs a
  human at the bench, the recovery card armed, a read-only UART capture and a stock pair A.
* The image is **bench-only** (the delta is a build `.config` addition, not a target/image-recipe
  change; the driver is research code). It is not a product upgrade artifact.
* The flash creates a **fresh empty overlay** (`/overlay`): SSH host keys, uci config and any
  receipts there are replaced by the image defaults. The image's dropbear keeps the stock
  OpenWrt blank-root-password patch, so root SSH still answers; re-provision the project key after
  boot. Stock pair A (stock 2.5.24) is the primary rollback and is not written.
* The kernel slice is identical to the already-running validated slice, so the kernel write is
  bookkeeping-safe; it is performed because the installer writes both members of one container
  and verifies the full slot read-back.
* **hostapd/wpad is not in this image** (separate lane). If it lands before the fire, the image
  should be rebuilt again - the kernel package pin is unchanged (6f995278), so the ABI fix holds
  and only the rootfs hash changes; re-run the verifiers.
* The reproduction claim is bound to the pinned tree state recorded in
  `build-evidence/build-summary.txt` (tree HEAD, config shas, driver snapshot sha).
* The vendor firmware blob is not in the image and cannot be; the bench still supplies it locally.

## 7. Concurrency / shared-tree discipline

The world build is serialized: the script refuses to start while a make runs in the shared tree and
holds the `/tmp/cudy-openwrt-world-build.lock` flock. Sibling lanes (hostapd-img, dp-merge) were
notified of the window and acknowledged; the tree's `.config` was restored byte-identically to the
validated baseline (`9052d5e3...`) and the rootfs staging tree was restored from a pre-build
snapshot (no bench packages left installed), so later lane builds cannot inherit this delta.

## 8. Decision and next step

P1 is executed to the host-only limit: the current tree reproduces the ABI-consistent image and
every check that can be made from bytes passes. **Next: the Lead runs FLASH-RUNBOOK.md attended
(kernel+rootfs from this one build, pair B, recovery armed), then the post-boot
`apk query --fields version kernel` receipt and the T0 full-set verify/apply.** Do not force apk
past the kernel= dependency and do not write only the kernel slice.
