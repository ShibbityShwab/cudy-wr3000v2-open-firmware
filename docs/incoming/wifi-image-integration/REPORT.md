# wifi-image-integration - bench image with iw/libnl-tiny/iwinfo/regdb + the clean-room driver

**Lane:** img-integ (shared task task-112), host-only. **Date:** 2026-11-24.
**Status:** package + config delta + build/verify tooling delivered; the bench
image is **BUILT and VERIFIED FROM ITS BYTES** host-side with the
**hardware-validated kernel slice unchanged**. **Nothing was flashed or booted by
this lane** - the attended install is a separate decision (DEPLOY.md).

## 0. TL;DR

* **Config delta (bench-only, 7 packages)**: `iw`, `iwinfo` (C CLI),
  `libiwinfo` (+ `libiwinfo-data`), `wireless-regdb`,
  `kmod-hi5622-cfg80211`, `kmod-hi5622-wifi`; `CONFIG_WIFI_SCRIPTS_UCODE`
  is disabled so package `iwinfo` installs the C binary (with the ucode scripts
  selected it installs nothing).
* **Packaging**: one tracked OpenWrt package defines two KernelPackages:
  `kmod-hi5622-cfg80211` installs the target kernel tree's own
  `net/wireless/cfg80211.ko` (NOT the mac80211 backports `kmod-cfg80211`),
  `kmod-hi5622-wifi` builds the clean-room driver from a source snapshot of
  `docs/incoming/wifi-cfg80211/src` (29 source files + sha256 manifest, no
  binary committed).
* **Build (serialized, shared release tree, `make -j16`, 153 s)**: container
  **6,533,439 B sha256 `df6a78c3...`**; kernel slice **UNCHANGED**
  **3,372,638 B sha256 `13e81214...`** (the hardware-validated cfg80211
  slice); rootfs UBI **3,145,728 B sha256 `62344e80...`**; squashfs
  **2,753,136 B sha256 `68450ee4...`**; manifest **81** packages (baseline 74).
* **Verification from the built bytes**: the rootfs squashfs is reassembled
  from the UBI image in the sysupgrade container (read-only tool here), then
  `unsquashfs`; all required paths are PRESENT with hashes (iw, iwinfo,
  libiwinfo, libnl-tiny, regulatory.db, cfg80211.ko, hi5622_wifi.ko); both
  modules carry vermagic `6.18.60 SMP mod_unload ARMv7 p2v8` and the driver
  `depends=cfg80211`. `tools/verify-image.sh` exits 0 only on a full PASS.
* **Key finding (F1)**: normalizing the release-line `.config` with
  `make defconfig` silently DROPS the out-of-menu
  `CONFIG_KERNEL_HOTPLUG_CPU=y` override. `include/kernel-defaults.mk` maps
  every `CONFIG_KERNEL_*` line to the kernel's `CONFIG_*`, so the first build
  attempt rebuilt the kernel with `CONFIG_HOTPLUG_CPU` unset and produced a
  **new** kernel slice (3,369,166 B sha256 `853ac764...`) instead of the
  validated `13e81214...`. `kernel-symbols.config` carries the override over
  after defconfig; the re-run reproduces the validated slice exactly (kernel
  `.config` sha before/after: `8ddb783d...` -> `08b6e595...`, i.e. the
  pre-build/validated kernel config).
* **Deploy decision**: the kernel slice is unchanged, so the only on-device
  change is the rootfs. Recommended next-session path is the zero-flash
  `apk add` of the same built packages (T0 in DEPLOY.md); a full pair-B rootfs
  rewrite (T1) is a dedicated attended window with a saved rollback UBI and an
  accepted overlay loss. No kernel write is needed.

## 1. What the image now carries

| package | version | what it provides |
| --- | --- | --- |
| `iw` | 6.17-r1 | `/usr/sbin/iw` (nl80211 tool used for the 2026-11-25 phy/VAP milestone) |
| `libnl-tiny` | 2025.12.02~40493a65 | `/usr/lib/libnl-tiny.so.1` - was ALREADY in the baseline image (see F2) |
| `iwinfo` | 2026.05.26~66bdd1a0 | `/usr/bin/iwinfo` C CLI |
| `libiwinfo` (+data) | 2026.05.26~66bdd1a0 | `/usr/lib/libiwinfo.so.20230701`, `/usr/share/libiwinfo/devices.txt` |
| `wireless-regdb` | 2026.09.03 | `/lib/firmware/regulatory.db` |
| `kmod-hi5622-cfg80211` | 6.18.60-r1 | `/lib/modules/6.18.60/cfg80211.ko` (target kernel tree, stripped at install) |
| `kmod-hi5622-wifi` | 6.18.60-r1 | `/lib/modules/6.18.60/hi5622_wifi.ko` (clean-room driver) |

No autoload entry and no init script are installed: the modules are loaded by
hand in an attended session (bench policy). No vendor firmware, no calibration
or custom-block bytes are in the image; the driver reads the local firmware via
its `fw_path` module parameter.

Size effect vs the previous release image `22b72852` (kernel-only cfg80211
config change): container +266,240 B, rootfs UBI +262,144 B (2 extra 128 KiB
PEBs), kernel slice +-0 B.

## 2. Packaging design and decisions

### 2.1 cfg80211: target kernel tree, not the mac80211 backports package

The clean-room driver is compiled against the luofu kernel tree's
`net/wireless` headers and imports six cfg80211 symbols, so at runtime it must
use the cfg80211 built from THAT tree. `target/linux/hisilicon/luofu/config-6.18`
already sets `CONFIG_CFG80211=m` (handoff from the wifi-upstream-target lane,
`docs/incoming/wifi-upstream-target/CFG80211-BENCH-IMAGE.md`), so the module is
produced by `target/linux/compile`; `kmod-hi5622-cfg80211` only installs it
(`FILES:=$(LINUX_DIR)/net/wireless/cfg80211.ko`, `KCONFIG:=CONFIG_CFG80211`).
OpenWrt's own `kmod-cfg80211` (defined in `package/kernel/mac80211`) builds a
DIFFERENT cfg80211 from the backports tarball; selecting both would install two
different `cfg80211.ko` files. The delta leaves `CONFIG_PACKAGE_kmod-cfg80211`
unset; the package description states this explicitly.

### 2.2 Driver source snapshot

`tools/install-into-tree.sh` copies the source files (Kbuild, Makefile, *.c,
*.h, *.inc, and the committed `cfg80211-provider.symvers`) from
`docs/incoming/wifi-cfg80211/src` into the OpenWrt tree's
`package/hi5622-wifi/src/`, rejects any build product that would leak (e.g.
the generated `hi5622_wifi.mod.c` or a kbuild `Module.symvers`), and records
`src-sha256.txt` (29 files, manifest sha256
`10582bb49af68c8360d569d88e59fd994d370e76e4440e48fc24cc856af948ca`). The
package runs `$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules`, so it uses the same
out-of-tree kbuild line as the lane's own build.sh. The module hash is
build-path dependent (debug info records the build directory), so the package
build hash differs from the lane's recorded hash by design:
* lane artifact: 505,076 B sha256 `d2b3cf79...` / later `2c30051e...`;
* this package build: 505,748 B sha256
  `340586180860c326ef1e5a7fd96c8851acaa953e828c10f67c3ddd856c0cdad1`;
* installed (stripped) module: 133,884 B sha256 `e0608fa6...`, vermagic
  `6.18.60 SMP mod_unload ARMv7 p2v8`, `depends=cfg80211`.

### 2.3 iwinfo: C CLI (WIFI_SCRIPTS_UCODE disabled)

With the tree default `CONFIG_WIFI_SCRIPTS_UCODE=y`, package `iwinfo` has an
empty install rule (`package/network/utils/iwinfo/Makefile`: with the ucode
scripts the CLI is provided by `wifi-scripts/files-ucode/usr/bin/iwinfo`
instead) and its dependency on `libiwinfo` is conditional, so selecting
`iwinfo` alone would put NO iwinfo binary in the rootfs. The delta disables
`WIFI_SCRIPTS_UCODE` and selects `iwinfo` + `libiwinfo`: the rootfs gets the
classic C CLI and the library. `wifi-scripts` itself is not selected, so no
netifd wifi integration is activated by this change.

### 2.4 wireless-regdb and the signed-db caveat

The kernel delta keeps `CONFIG_CFG80211_REQUIRE_SIGNED_REGDB=y` and
`CONFIG_CFG80211_USE_KERNEL_REGDB_KEYS=y`, while OpenWrt's `wireless-regdb`
package installs only the unsigned `regulatory.db` (no `.p7s`). At runtime
cfg80211 therefore rejects the file and falls back to the built-in world
regulatory domain - permissive, and the driver's RE-derived bands/channels are
unaffected (`iw phy` enumeration is driver-provided; the 2026-11-25 milestone
worked with the world domain). The file is shipped so the artefact and the
behaviour are explicit; if a bench needs a real country domain, the right fix
is the M6 vendor INI/regdomain port or `REQUIRE_SIGNED_REGDB=n`, both kernel
config changes deliberately NOT made here (they would rebuild a new kernel).

### 2.5 Bench policy

No AUTOLOAD, no init script: nothing in the rootfs touches the Wi-Fi chip at
boot. Loading is `modprobe cfg80211` + `modprobe hi5622_wifi fw_path=...` in
an attended session (driver probe is write-free and sends no H2D message).

## 3. Build and verification evidence

Committed evidence (`build-evidence/`, text only):

| file | content |
| --- | --- |
| `build-summary.txt` | tree/HEAD, config shas, package delta, artefact sizes+sha256, verify status |
| `image-verify.txt` | full verifier output (per-file presence/hashes, module metadata, manifest) |
| `build-log.txt` | `make -j16` log of the recorded build (4.1 KiB) |
| `config-resolved.txt` | the resolved .config used for the build |
| `config-delta-applied.diff` | pre-build .config vs resolved .config |
| `kernel-config-sha.txt` | target kernel config + built kernel .config sha256 before/after |

Recorded build (2026-11-24 14:05-14:08 UTC+07: tree `27663a0`):

    container  : 6,533,439 B sha256 df6a78c3717b0f792391e7f7d488a9f2850c9640553ef391163dd34abdb47310
    kernel     : 3,372,638 B sha256 13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f  (UNCHANGED, hardware-validated)
    rootfs ubi : 3,145,728 B sha256 62344e8040f4b17ae8d3a9843dd6a57ec35f55636cd3aa8e868b605951642746
    squashfs   : 2,753,136 B sha256 68450ee4730302747508b94ef45875c433e48b14c1540975ea2143ae72d3af44
    manifest   : 81 packages (+ iw, iwinfo, libiwinfo20230701, libiwinfo-data, wireless-regdb, kmod-hi5622-cfg80211, kmod-hi5622-wifi)

The verifier is NOT a staging-tree check: it takes the newest sysupgrade
container, extracts the `kernel` and `root` members, reassembles the squashfs
volume from the UBI image with `tools/extract-ubi-rootfs.py` (per-PEB EC/VID
parsing, LEB order, 128 KiB PEB / 4 KiB data offset as ubinize emitted here),
verifies the volume prefix against `build_dir/.../root.squashfs` (remaining
bytes are ubinize padding: 3,472 B), extracts with `unsquashfs`, checks every
required path's size+sha256, dumps each module's vermagic/depends/license and
the manifest package lines.

## 4. Deployment decision (details in DEPLOY.md)

The kernel slice is byte-identical to the hardware-validated one, so:

* **T0 (recommended next session, zero flash)**: `apk add --allow-untrusted` the
  same built packages on the running pair-B system (persistent overlay); fully
  reversible; no selector/flash change.
* **T1 (clean image, dedicated attended window)**: rewrite only `rootfsb`
  (rootfs UBI from the container; kernel slot unchanged), with the current
  rootfs UBI saved as rollback and the overlay loss accepted; stock pair A and
  the physical recovery door remain the fallbacks.
* **No kernel write is required** (`kernelb` already carries `13e81214...`
  per STATUS 3k); verify the running slot hash before deciding otherwise.

## 5. Findings

**F1 - defconfig normalization silently changes the kernel (measured).**
The release-line `.config` carries an out-of-menu override
`CONFIG_KERNEL_HOTPLUG_CPU=y` (appended after the generated config, no
`config/Config-kernel.in` entry). `make defconfig` drops it, and
`include/kernel-defaults.mk` maps every `CONFIG_KERNEL_*` line into the
kernel's `CONFIG_*`, so the first build attempt rebuilt the kernel with
`CONFIG_HOTPLUG_CPU` unset: new slice 3,369,166 B sha256 `853ac764...`
(`build-evidence/work/build-summary-first-attempt.txt` not kept; the fact is
recorded here and in `kernel-symbols.config`). Adding
`kernel-symbols.config` after defconfig restored kernel `.config` sha
`08b6e595...` and reproduced the validated slice `13e81214...` exactly.
Any lane that runs `make defconfig` in this tree and then builds a kernel will
hit the same trap.

**F2 - libnl-tiny was already in the baseline image.** The task premise said
the image lacks iw/libnl-tiny; the manifest of `22b72852` already lists
`libnl-tiny1 - 2025.12.02~40493a65-r1` and `/usr/lib/libnl-tiny.so.1`
(sha256 `d7d73dbd...`). What was actually missing: `iw`, `iwinfo`,
`libiwinfo`, `wireless-regdb` and both modules (`cfg80211.ko` was built but
not packaged).

**F3 - iwinfo is a no-op package under the tree default.** See 2.3; the delta
explicitly disables `WIFI_SCRIPTS_UCODE` and the verifier checks
`/usr/bin/iwinfo` (16,906 B sha256 `88fb4943...`) so a regression to an empty
package cannot pass silently.

**F4 - regdb is installed but not loaded by the kernel** (signed-db
requirement, section 2.4). Documented, not hidden.

**F5 - module identity.** The installed `cfg80211.ko` is the OpenWrt-stripped
form of the tree's `949393f9...` module (2,328,548 B unstripped -> 406,028 B
installed, sha256 `11a5e2b5...`; same vermagic; the tree artifact hash equals
the upstream-target lane's recorded cfg80211 build). The installed
`hi5622_wifi.ko` is byte-identical to the package's ipkg tree file
(133,884 B stripped, `depends=cfg80211`).

**F6 - shared-tree hygiene implemented.** `tools/build-image.sh` refuses to run
while a make process works in the tree, holds `/tmp/cudy-openwrt-world-build.lock`,
snapshots and restores `build_dir/target-*/root-hisilicon` and restores
`.config` byte-identically (verified with `cmp`), so a later build by another
lane cannot inherit the bench package set. The tree's `target/linux/*` and
kernel patch files were not modified by this lane (target kernel config sha
unchanged: `ecf1aae2...`).

## 6. Limitations / honest notes

* Host-only: no boot, no flash, no on-device load; the image is not a release
  image (container keeps its NOT FLASHABLE marker; the A/B install path has
  never run end to end).
* The driver snapshot is research code (cfg80211 M0-M2: wiphy + VAP lifecycle;
  scan/AP firmware messages still pending - see wifi-cfg80211 REPORT). The
  package does not update when the sibling lane changes its src; re-run
  `install-into-tree.sh` (or `build-image.sh`) to refresh and re-record the
  manifest/hash.
* The module hash is build-path dependent; compare behaviour/vermagic, not the
  lane's recorded hash.
* `CONFIG_DEVMEM=y` is part of the release-line tree (uncommitted
  `target/linux/generic/config-6.18` change by an earlier lane) and is needed
  by the bench `bar0read` tool; this lane did not touch it.
* One recorded build, not a multi-run reproducibility claim (the image chain
  itself is claimed byte-reproducible elsewhere; the second run here was used
  to fix F1 and produced the recorded artefact).

## 7. Follow-ups

1. Attended T0/T1 execution per DEPLOY.md; after boot verify
   `sha256sum` of the rootfs files against `build-evidence/image-verify.txt`.
2. When the wifi-cfg80211 lane's M3+ code lands, re-run
   `tools/install-into-tree.sh` + `tools/build-image.sh` and update the pin.
3. Optional: decide the Wi-Fi userland stack for a target PR (`wifi-scripts`
   ucode vs C iwinfo) - this delta is bench-only.
4. Optional bench improvement: `CONFIG_CFG80211_REQUIRE_SIGNED_REGDB=n` (kernel
   rebuild) if a real country domain is ever needed before the M6 regdomain
   port.

## 8. Provenance and regeneration

    ./tools/selftest.sh                       # device-free contract checks
    ./tools/install-into-tree.sh --owrt <tree>   # package + driver snapshot
    ./tools/build-image.sh                    # serialize, build, verify, restore
    ./tools/verify-image.sh --owrt <tree>     # re-verify the newest container
    sha256sum -c build-evidence/<recorded hashes in build-summary.txt>

Everything is text in this directory (no binary, no vendor bytes); the built
artefacts and the full 27 MiB work tree (config snapshot, rootfs staging tar,
extracted UBI/squashfs) stay LOCAL and are excluded via
`build-evidence/.gitignore`; the local convenience copy for deployment is
`/home/shibbityshwab/wifi-image-integration-artifacts/` (sysupgrade container,
kernel slice, rootfs UBI, squashfs, the seven .apk packages, `SHA256SUMS`).
