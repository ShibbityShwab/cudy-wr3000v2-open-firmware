# wifi-image-integration - self-contained bench image (iw/iwinfo/regdb/kmods/hostapd/ucode)

**Lane:** img-integ (shared task task-112 -> final-image task-132), host-only.
**Date:** 2026-11-24 (rounds 1-2) / 2026-11-27 (round 3, final image).
**Status:** the **SELF-CONTAINED FINAL BENCH IMAGE** is BUILT and VERIFIED FROM
ITS BYTES host-side in the shared release-line tree, with the
**hardware-validated kernel slice UNCHANGED** (`13e81214...`, slot
`1794a531...`): a rootfs-only rebuild. **Nothing was flashed or booted by this
lane** - the ABI predecessor image was flashed by the Lead on 2026-11-27 and the
AP runs on it; installing this final image is the next attended decision
(DEPLOY.md).

## 0. TL;DR (round 3 - final image)

* **Image (recorded build 2026-11-27, tree `27663a0`, `make -j16`, 12 s):**

  | artefact | size | sha256 |
  | --- | ---: | --- |
  | sysupgrade container | **6,922,559 B** | **`ae5e28bfef591fad7d9b4da726128ec2307273aebc41008e0ad9c3867bde6994`** |
  | kernel slice (container member `kernel`) | 3,372,638 B | `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` (**UNCHANGED**) |
  | rootfs UBI (container member `root`) | **3,538,944 B** | **`ff9f3e1c83c36a2a9170cdf6373228692b46b7fa0fbb64abbdc70a25b841aaa9`** |
  | rootfs squashfs (padded volume) | 3,080,192 B | `b5a8d10b...` (see image-verify.txt) |
  | apk manifest | **83 packages** | see `build-evidence/image-verify.txt` |

* **Rootfs-only by construction**: the build reused the validated kernel build
  (kernel `.config` sha `08b6e595...` unchanged before/after, kernel slice
  identical, kernel compile skipped), so flashing this image needs **only a
  rootfsb write** - the kernelb slot `13e81214...`/`1794a531...` is untouched.
  `tools/verify-image.sh` asserts the kernel hash and fails on any drift.
* **Self-contained userspace + modules** (no /tmp deployment for these):
  `iw` 6.17, ucode `iwinfo` + `wifi-scripts` (its ucode files incl.
  `/usr/share/hostap/{common,wdev,wifi-detect}.uc` and
  `/usr/share/ucode/wifi/*`), `hostapd-mini` v2.12 + `hostapd-common`,
  `ucode-mod-nl80211`/`ucode-mod-rtnl`, `wireless-regdb`, and both kernel
  modules `/lib/modules/6.18.60/{cfg80211,hi5622_wifi}.ko`.
  `hostapd` binary sha256 `13cb3a43...` (504,333 B) and the ucode module
  hashes match the wifi-hostapd-deploy lane's authoritative expected-files
  manifest exactly (verifier IDENT lines).
* **Driver packaged = the COMBINED dp-air + AP-start v7 module** (ap-start lane
  delivered it before this build: `dataops/incoming/wifi-ap-start/combined/
  README.md`): deployable debug-stripped **242,572 B sha256
  `44c6cc12b5c43d47b4b0ccd1da693c77b00e5ef48912625add52370ea4612c0b`**, packaged
  as PREBUILT (nothing recompiled; the validated bytes are installed,
  OpenWrt-stripped to 231,132 B sha256 `51608fd5...` in the rootfs). It carries
  **both** feature sets - AP-start (v7) and the data plane
  (`dp_probe/dp_setup/dp_txq_sync/h2d_txq_addr_sync/dp_tx_frame/dp_tx_status/
  dp_rx_poll`) - so one module load on a fresh flash can run the single-boot
  air-TX milestone without a /tmp module push.
* **Still LOCAL by policy (not in the image)**: the PD4-patched vendor firmware
  (vendor-derived, never committed) and the attended bench scripts/configs
  (hostapd-open.conf etc.); the bench copies the firmware at session time
  (`fw_path`).
* **Deployment**: the new rootfs UBI goes to `rootfsb` (mtd14) from a stock
  pair-A boot via the proven stock-telnet transport. The existing
  `tools/stock-telnet-install.sh --apply` writes BOTH pair-b members; a strict
  rootfs-only write is possible with the same transport but needs either a
  small `--rootfs-only` extension of that tool (root repo, not this lane) or
  the manual 4-command sequence in DEPLOY.md. Writing the identical kernel slot
  with the existing tool is safe and verified (slot hash read-back), just
  unnecessary. Full analysis + rollback: DEPLOY.md.
* **Round 2 (ABI image `df6a78c3`) is preserved as history**: evidence moved to
  `build-evidence/round2-abi-image-df6a78c3/`; that image was flashed by the
  Lead (ABI closed, AP-ENABLED live) and this final image is its rootfs-only
  successor.

## 0b. Round 2 history (ABI image, 2026-11-24) - superseded by round 3

The first self-contained step packaged `iw`, C `iwinfo` + `libiwinfo`,
`wireless-regdb`, `kmod-hi5622-cfg80211` and a `kmod-hi5622-wifi` compiled
from the `docs/incoming/wifi-cfg80211/src` snapshot. Recorded image:
container 6,533,439 B sha `df6a78c3...`, kernel slice unchanged
`13e81214...`, rootfs UBI 3,145,728 B sha `62344e80...`, 81 packages. That
image was flashed 2026-11-27 via the stock-telnet path and closed the kmod ABI
mismatch (native `modprobe cfg80211` + `hi5622_wifi` from `/lib/modules`,
hostapd AP-ENABLED live). Its evidence and numbers are in
`build-evidence/round2-abi-image-df6a78c3/`.

Key finding from round 2 that still governs this tooling (**F1**): normalizing
the release-line `.config` with `make defconfig` silently DROPS the
out-of-menu `CONFIG_KERNEL_HOTPLUG_CPU=y` override; `include/kernel-defaults.mk`
maps every `CONFIG_KERNEL_*` line to the kernel's `CONFIG_*`, so a naive
incremental build rebuilds a DIFFERENT kernel slice (measured:
3,369,166 B sha `853ac764...`). `kernel-symbols.config` re-appends the
override after defconfig; the kernel `.config` then reproduces the validated
`08b6e595...` and the kernel slice stays `13e81214...`.

## 1. What the final image carries

| package | version | what it provides |
| --- | --- | --- |
| `iw` | 6.17-r1 | `/usr/sbin/iw` (nl80211 tool used for the phy/VAP milestone) |
| `wifi-scripts` | 1.0-r5 | ucode wifi scripts: `/usr/bin/iwinfo`, `/usr/share/ucode/iwinfo.uc`, `/usr/share/ucode/wifi/*`, hostapd ucode `/usr/share/hostap/{common,wdev,wifi-detect}.uc`, netifd integration |
| `hostapd-mini` | 2026.08.07~831364bf-r7 | `/usr/sbin/hostapd` (504,333 B), hostapd config backend |
| `hostapd-common` | 2026.08.07~831364bf-r7 | `/etc/init.d/wpad`, `/etc/capabilities/wpad.json`, acl |
| `ucode-mod-nl80211` / `ucode-mod-rtnl` | 2026.10.28~dfc64d41 | `/usr/lib/ucode/{nl80211,rtnl}.so` |
| `wireless-regdb` | 2026.09.03-r1 | `/lib/firmware/regulatory.db` (see 2.4: unsigned db => world regdomain) |
| `kmod-hi5622-cfg80211` | 6.18.60-r1 | `/lib/modules/6.18.60/cfg80211.ko` (target kernel tree, stripped at install) |
| `kmod-hi5622-wifi` | 6.18.60-r1 | `/lib/modules/6.18.60/hi5622_wifi.ko` (combined dp-air + AP-start v7, prebuilt) |
| `libnl-tiny` | 2025.12.02~40493a65 | `/usr/lib/libnl-tiny.so.1` - already in the baseline image |

No autoload entry and no driver init script: the driver is loaded by hand in an
attended session (bench policy). No vendor firmware, no calibration or
custom-block bytes are in the image.

Size vs the ABI image `df6a78c3`: container +389,120 B, rootfs UBI +393,216 B
(3 extra 128 KiB PEBs), kernel slice +-0 B.

## 2. Packaging design and decisions

### 2.1 cfg80211: target kernel tree, not the mac80211 backports package

The clean-room driver is compiled against the luofu kernel tree's
`net/wireless` headers and imports six cfg80211 symbols, so at runtime it must
use the cfg80211 built from THAT tree. `target/linux/hisilicon/luofu/config-6.18`
already sets `CONFIG_CFG80211=m` (handoff from the wifi-upstream-target lane),
so the module is produced by `target/linux/compile`;
`kmod-hi5622-cfg80211` only installs it
(`FILES:=$(LINUX_DIR)/net/wireless/cfg80211.ko`, `KCONFIG:=CONFIG_CFG80211`).
OpenWrt's own `kmod-cfg80211` (package/kernel/mac80211) builds a DIFFERENT
cfg80211 from the backports tarball; selecting both would install two different
`cfg80211.ko` files. The delta leaves `CONFIG_PACKAGE_kmod-cfg80211` unset.

### 2.2 Driver packaging: PREBUILT (validated bytes) or SOURCE

`kmod-hi5622-wifi` has two modes, chosen by what `tools/install-into-tree.sh`
staged into the (untracked) package dir:

* **prebuilt (final image)**: `prebuilt/hi5622_wifi.ko` - the local,
  ABI-validated bench artifact. `install-into-tree.sh --driver-ko FILE`
  validates the ELF/RAM/vermagic/depends/name, records sha256+size+modinfo in
  `prebuilt/prebuilt-sha256.txt` and the package copies the bytes as-is (no
  compile). Nothing is stored in the git repository; only the identity manifest
  is recorded in the build evidence. This is what lets the flashed image carry
  the EXACT hardware-validated module.
* **source**: no `prebuilt/` => build from the `src/` snapshot of a committed
  driver tree with `$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules` (round-1/2
  behaviour; snapshot filter drops generated `*.mod.c`/Module.symvers).

Round-3 identity: artifact 242,572 B sha `44c6cc12...` (combined dp-air +
AP-start v7, from `docs/incoming/wifi-ap-start/combined/README.md`), installed
(OpenWrt-stripped) 231,132 B sha `51608fd5...`, `depends=cfg80211`,
vermagic `6.18.60 SMP mod_unload ARMv7 p2v8`, AP-start + dp sysfs feature
attributes verified by `tools/verify-image.sh`.

### 2.3 iwinfo: ucode CLI via wifi-scripts (changed in round 3)

The tree default `CONFIG_WIFI_SCRIPTS_UCODE=y` + `CONFIG_PACKAGE_wifi-scripts=y`
installs `/usr/bin/iwinfo` as the OpenWrt ucode implementation (6,371 B) and
the ucode wifi modules; `hostapd-mini` additionally REQUIRES the
`/usr/share/hostap/*.uc` files that only wifi-scripts ships (bench finding:
`../wifi-ap-start/image/wifi-scripts-ucode.md`, hostapd exited with
"Unable to resolve path for module 'common'"). Round 2's C iwinfo /
libiwinfo selection is therefore retired: with ucode enabled the C package
installs nothing and there is no reason to ship two CLIs.

### 2.4 wireless-regdb and the signed-db caveat

The kernel delta keeps `CONFIG_CFG80211_REQUIRE_SIGNED_REGDB=y` and
`CONFIG_CFG80211_USE_KERNEL_REGDB_KEYS=y`, while OpenWrt's `wireless-regdb`
package installs only the unsigned `regulatory.db` (no `.p7s`). At runtime
cfg80211 rejects the file and falls back to the built-in world regulatory
domain - permissive; the driver's RE-derived bands/channels are unaffected.
The file is shipped so the artefact and the behaviour are explicit; a real
country domain would need the M6 vendor regdomain port or
`REQUIRE_SIGNED_REGDB=n` (kernel config change, deliberately not made here).

### 2.5 Bench policy

No AUTOLOAD, no driver init script: nothing in the rootfs touches the Wi-Fi
chip at boot. Loading is `modprobe cfg80211` + `modprobe hi5622_wifi
fw_path=...` in an attended session (driver probe is write-free and sends no
H2D message). The vendor firmware is local-only evidence, never in the image.

## 3. Build and verification evidence (round 3)

Committed evidence (`build-evidence/`, text only):
`build-summary.txt`, `image-verify.txt`, `driver-artifact.txt` (prebuilt
module identity + modinfo), `build-log.txt`, `config-resolved.txt`,
`config-delta-applied.diff`, `kernel-config-sha.txt`; round-2 evidence under
`build-evidence/round2-abi-image-df6a78c3/`; local work tree under
`build-evidence/work/` (gitignored: full config snapshot, rootfs staging tar,
extracted UBI/squashfs).

The verifier is NOT a staging-tree check: it takes the newest sysupgrade
container, extracts `kernel`/`root`, reassembles the squashfs from the UBI
image (`tools/extract-ubi-rootfs.py`: per-PEB EC/VID parsing, LEB order,
128 KiB PEB / 4 KiB data offset), checks the volume prefix against
`build_dir/.../root.squashfs`, extracts with `unsquashfs`, checks every
required path, asserts known-good hashes for iw/hostapd/ucode/regdb/cfg80211
(IDENT lines), dumps module vermagic/depends/license/params+features, and
checks the apk manifest package list.

Recorded build: kernel `.config` sha unchanged (`08b6e595...`), kernel slice
`13e81214...` (UNCHANGED), build duration 12 s (kernel compile skipped).
Deployable .apk files from the same build (paths+sha in BUILD-NOTES.md; local
copy `/home/shibbityshwab/wifi-image-integration-artifacts/`):
`iw`, `wifi-scripts`, `hostapd-mini`, `hostapd-common`,
`ucode-mod-nl80211`, `ucode-mod-rtnl`, `wireless-regdb`,
`kmod-hi5622-cfg80211`, `kmod-hi5622-wifi`.

## 4. Deployment (details + exact commands in DEPLOY.md)

* The kernel slice is byte-identical to the running validated one, so the new
  image is a **rootfs-only change**: write the new rootfs UBI to `rootfsb`
  (mtd14) from a stock pair-A boot over the proven stock-telnet transport; keep
  kernelb untouched; hand back to pair b; the overlay is wiped by the rootfs
  write (accepted; back it up if needed).
* **Answer to "is a rootfs-only flash possible via the stock-telnet-install
  path?"**: the TRANSPORT supports it, the current TOOL does not have a
  rootfs-only mode - `tools/stock-telnet-install.sh --apply` always writes
  kernelb (mtd12, erase+write, full-slot hash verify) and then ubiformats
  rootfsb (mtd14). Using it as-is is SAFE here (the kernel write is
  content-identical and verified), just unnecessary; a strict rootfs-only write
  needs either a small `--rootfs-only` extension of that root-repo tool or the
  documented manual 4-command sequence in DEPLOY.md (wget root member, sha
  verify, `ubiformat /dev/mtd14`, handover b).
* Rollback: stock pair A remains bootable; previous rootfs UBI (round-2
  `62344e80...` or this image's own) can be re-written the same way; physical
  RESET+TFTP recovery is the last resort. No kernel write is needed at all.

## 5. Findings

**F1 - defconfig normalization silently changes the kernel (measured in round 2,
carried into the final tooling).** See 0b. `kernel-symbols.config` is required
for any world build in this tree that must keep the validated kernel.

**F2 - the baseline image already carried libnl-tiny** (round 2 finding). The
missing pieces were iw/iwinfo/regdb/hostapd/ucode and the packaged modules.

**F3 - hostapd needs wifi-scripts ucode files** (bench finding from the AP
work): image fix = `CONFIG_PACKAGE_wifi-scripts=y`; verified by the IDENT
hashes of the ucode files and by `usr/share/hostap/{common,wdev,
wifi-detect}.uc` presence.

**F4 - regdb is installed but not loaded by the kernel** (signed-db
requirement, 2.4). Documented, not hidden.

**F5 - module identity**: prebuilt combined module artifact `44c6cc12...`
(242,572 B) -> installed stripped `51608fd5...` (231,132 B); package ipkg
tree file byte-identical to the installed one; vermagic/depends/feature
attributes verified from the built image. The verifier would fail if the wrong
module were packaged (AP or dp features missing).

**F6 - shared-tree hygiene** (`tools/build-image.sh`): refuses to run while a
make process works in the tree, holds
`/tmp/cudy-openwrt-world-build.lock`, snapshots/restores the rootfs staging
tree and restores `.config` byte-identically (`cmp`-verified). The round-3 run
was refused once because another lane was running a module build against the
shared kernel tree - serialization works as intended.

**F7 - the combined module is host-built, not yet hardware-fired** (the air-TX
single-boot run it was made for is the next bench step, not done by this
host-only lane). The v7 AP half of it was hardware-validated (AP-ENABLED) and
the dp half derives from the fired dp module (`ca32aec4...`); the combined
binary itself has not been fired.

## 6. Limitations / honest notes

* Host-only: no boot/flash by this lane; the final image has never been booted.
  The ABI predecessor was flashed and the AP verified live on it.
* The combined module is verified only by identity/build evidence and its
  components' prior bench runs (F7); it must be fired before claiming its
  behaviour.
* The vendor firmware (PD4-patched) and bench configs/scripts are still local
  pushes at session time; the image covers userspace + modules only.
* The image is BENCH-ONLY: the delta is not in `target/linux/hisilicon/
  luofu/target.mk` or the image stanza; the driver is research code.
* `wireless-regdb` does not actually load (2.4).
* `CONFIG_DEVMEM=y` comes from the release-line tree (uncommitted
  `target/linux/generic/config-6.18` change by an earlier lane, needed by the
  bench `bar0read`); this lane does not touch it.

## 7. Follow-ups

1. Attended install of this image per DEPLOY.md (rootfs-only if a
   `--rootfs-only` flag is added to the stock-telnet tool, else the tool as-is;
   the kernel write is identical bytes).
2. Post-boot receipt: `apk query hostapd-mini wifi-scripts kmod-hi5622-wifi`,
   `sha256sum /usr/sbin/hostapd /lib/modules/6.18.60/hi5622_wifi.ko`,
   `modprobe cfg80211 hi5622_wifi fw_path=...`, then the single-boot air-TX
   ladder of `../wifi-ap-start/combined/README.md`.
3. If the ap-start lane delivers a newer combined module: one command refresh
   (`tools/install-into-tree.sh --driver-ko FILE` + `tools/build-image.sh`);
   the build re-records everything.
4. Optional: bake the local PD4 firmware into a LOCAL-ONLY image variant if the
   publication rules allow (currently out of scope; firmware is never
   committed).

## 8. Provenance and regeneration

    ./tools/selftest.sh                              # device-free contract checks
    ./tools/install-into-tree.sh --driver-ko FILE    # stage prebuilt module + package
    ./tools/build-image.sh                           # serialize, build, verify, restore
    ./tools/verify-image.sh --owrt <tree>            # re-verify the newest container

Everything in this directory is text (no binary, no vendor bytes); build
artefacts and the local work tree stay untracked/gitignored. Local convenience
copy for the bench: `/home/shibbityshwab/wifi-image-integration-artifacts/`
(final container, kernel slice, rootfs UBI, squashfs, the nine .apk files,
`SHA256SUMS`).
