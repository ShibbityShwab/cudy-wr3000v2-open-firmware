# wifi-abi-image - kmod-ABI image rebuild + attended kernel+rootfs flash plan

**Lane:** wifi-abi-image (shared task **task-128**), host-only. **Date:** 2026-11-26.
**Status:** bench image **rebuilt from the current tree and verified from its bytes**; attended
flash procedure written. **Nothing here was flashed or booted.** Host-side build + verification
only; the fire is the Lead's attended call (FLASH-RUNBOOK.md).

Part of the wave-6 Wi-Fi program under `opensource/docs/incoming/`. The parent state lives in the
repository's `STATUS.md`; the lane history is in `REPORT.md`.

## Why this lane exists

The kmod `.apk` files of the Wi-Fi lanes are ABI-pinned to kernel package
**6.18.60~6f995278d21563ed4dd8f2609f3659c8-r1** (the current tree's kernel configuration), but the
box's running pair-B rootfs carries the older **6.18.60~5de0e425...-r1**, so apk correctly refused
the T0 full-set install on 2026-11-26 (fail-closed, box unchanged). The kernel slice itself is
unchanged (`13e81214...`), i.e. this is build-provenance bookkeeping in the rootfs apk db, not an
ELF-ABI mismatch. The canonical fix (P1 in `../wifi-image-deploy/REPORT.md` 7.3) is: rebuild the
image from the current tree and flash **kernel + rootfs from that one build**, so the apk db, the
kmod pins and the kernel slice agree.

## Result (see REPORT.md for evidence)

* Serialized world build from tree `27663a0` reproduces the recorded bench image **byte-for-byte**:
  container `df6a78c3...`, kernel slice `13e81214...` / slot `1794a531...`, rootfs UBI
  `62344e80...`, and the seven deployable `.apk` hashes unchanged.
* From-bytes ABI verifier 25/25 PASS: image apk db kernel version == tree `.vermagic` == kmod
  `.apk` kernel= pin (`6f995278...`), module vermagic + depends correct, bench payload present.
* Host T0 transaction simulation against the rebuilt rootfs 68/68 PASS (install/rollback
  byte-identical, kernel-mismatch fails closed).
* Attended flash runbook written; nothing executed on the router.

## Quick start (host only, no router)

```sh
# 1. serialized rebuild + both verifiers (also refreshes build-evidence/)
./tools/build-abi-image.sh

# 2. lane self-check (scripts, runbook, pinned hashes, recorded evidence)
./tools/selftest.sh

# 3. verify an already-built container / refresh the flashable artifacts
./tools/verify-abi-image.sh \
    --container /home/shibbityshwab/openwrt-lab-selectorfix-img/bin/targets/hisilicon/luofu/openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin \
    --artifacts build/abi-image-artifacts
```

The flashable artefacts are extracted to `build/abi-image-artifacts/` (gitignored):
`bench-image-sysupgrade.bin`, `kernel-slice.bin`, `rootfs.ubi`, `root.squashfs`, the seven
`.apk` files and `SHA256SUMS`.

## Reading order

| file | what |
| --- | --- |
| `REPORT.md` | lane report: defect, rebuild inputs/gates, reproduction, from-bytes ABI verification, host simulation, limits, next step |
| `FLASH-RUNBOOK.md` | **the deliverable for the bench**: attended stock-assisted kernelb + rootfsb flash, preconditions, exact commands, post-boot ABI receipt, stop rules, rollback, manual appendix |
| `build-evidence/` | committed text evidence (build summary, ABI verifier, integration verifier, config delta, host apk simulation, driver install) |
| `tools/build-abi-image.sh` | serialized locked world build + tree restore + both verifiers |
| `tools/verify-abi-image.sh` | from-bytes image/ABI verifier + artefact extraction |
| `tools/selftest.sh` | device-free lane contract checks |

Related: `../wifi-image-integration/` (bench image build + T1 deploy), `../wifi-image-deploy/`
(T0 zero-flash deploy + the kernel-ABI failure analysis), `../cudy-backup-root/` (stock root
channel used by the runbook), repository `RUNBOOK.md` section 6a and `opensource/docs/SELECTOR-0x21.md`.

## Hard rules honoured

* **Host-only**: both scripts contain no device or flash command (selftest asserts it); no
  device/flash/reboot/write happens in this lane.
* **Serialized world build**: refuses while a make runs in the shared tree, holds
  `/tmp/cudy-openwrt-world-build.lock`, restores `.config` byte-identically and the rootfs
  staging tree afterwards (also on failure).
* **No vendor bytes**: the image carries the clean-room driver source snapshot and OpenWrt
  packages only; the vendor firmware stays local evidence and is never shipped.
* **Fail closed**: the verifiers pin the validated kernel slice/slot and the kernel package
  dependency; a drifted tree/config/driver snapshot is refused (unless `--allow-tree-drift` is
  explicitly passed for a deliberate new build).
* **No apk forcing**: the runbook never bypasses the kernel= dependency; it changes the rootfs so
  the dependency is satisfied.

## Honest limits

Host-built and verified only. The image is a bench instrument (build `.config` delta, research
driver), not an upstream/product artifact. The flash destroys the current `/overlay` (fresh
empty rootfs_data volume) and stock pair A is the rollback. hostapd/wpad is not in this image
(separate lane); a later rebuild keeps the same kernel package pin and only changes the rootfs.
