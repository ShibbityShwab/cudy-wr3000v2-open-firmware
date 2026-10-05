# HYBRID-IMAGE PLAN: the newest OpenWrt userland on this device, no upstream kernel support

Scope: a flashable rootfs that grafts the newest upstream OpenWrt userland onto the vendor kernel and its
Wi-Fi modules. Every claim below carries a repo-relative citation (`path:line`) that was re-grepped when this
document was written (2026-10-05). No device action is taken here.

## 0. Verdict in one paragraph

The vendor firmware is OpenWrt 22.03.6, private target `hisilicon/luofu`, over stock upstream
`arm_cortex-a9` musl packages (`opensource/docs/phase26/how-current-userland.md:12-13,25`). A 24.10.5
upstream binary runs on the vendor 5.10.201 kernel (`opensource/docs/phase26/how-current-userland.md:31,62`)
and the 24.10 ubus/ubox IPC stack completes a round trip on it
(`opensource/docs/phase26/ubus-ipc-on-vendor-kernel.md:12,36,54`). The rootfs cannot be swapped wholesale:
the vendor base carries the `hi_*` app layer, the Wi-Fi calibration flow and the device's own flashing path
(`opensource/docs/phase26/rootfs-swap-verdict.md:9,13,24-32`), and the library closure forbids a partial mix
(`opensource/docs/phase26/library-closure-constraint.md:22-24`). The deliverable is therefore an
upgrade-in-place graft: keep the vendor base, bring in the newest userland where it does not collide.
`omo-minimal-0.3` is that shape in miniature (`opensource/docs/FLASH-PLAN.md:48-56`).

## 1. Keep / graft lists

### 1.1 MUST keep from the vendor rootfs (do NOT replace)

| item | detail | why | cite |
| --- | --- | --- | --- |
| vendor app framework binaries | `hipriv, hi_app, hi_appenv, hi_appm, hi_cfm, hi_getenv, hi_ipc, hi_md, hi_mw, hi_printenv, hi_setenv` | privileged ops, config manager, app manager, IPC | `opensource/docs/phase26/rootfs-swap-verdict.md:9` |
| vendor init scripts | `hsan_appm, hsan_easymesh, hsan_prepare, hsan_start, cmagent, cmsd, hcsh, softapd, umd, automesh` | app layer that starts and supervises the above | `opensource/docs/phase26/rootfs-swap-verdict.md:13` |
| board / sysinfo / crypto ubus | `hi_hi_board_info_get_call, hi_hi_sysinfo_data_get_call, hi_hi_encrypt_rsa_de_call`, `cmagent.router` | identity plus the RSA device-secret exchange | `opensource/docs/phase26/rootfs-swap-verdict.md:24-26` |
| the flashing mechanism | `hi_hi_upgrade_block_start_call, hi_hi_upgrade_block_stop_call, hi_hi_upgrade_block_write_call, hi_hi_upgrade_cmd_call, hi_hi_upgrade_dump_call, hi_hi_upgrade_flag_get_call, hi_hi_upgrade_flag_set_call, hi_hi_upgrade_image_check_file_call, hi_hi_upg_get_img_version_call` | a stock rootfs has none of these, so swapping removes the device's own ability to flash | `opensource/docs/phase26/rootfs-swap-verdict.md:26-29,32` |
| Wi-Fi calibration flow | `Hi5622V200_cal_init.sh, Hi5622V200_cal_save.sh, hi5620v100_cal_get.sh, hi5622v100_cal_init.sh, hi5622v100_cal_get.sh` | per-unit radio calibration; the project rule protects it (`MANIFEST.sha256` before any restore) | `opensource/docs/phase26/rootfs-swap-verdict.md:16`; `CUSTOM-FIRMWARE-PLAN.md:174` |
| kernel plus Wi-Fi modules | vendor 5.10.201 plus `hi5622v100_wifi.ko` / `hi5622v100_plat.ko` and the roughly 60 vendor modules | no stable module ABI, and the source is not public (GPL request) | `opensource/docs/phase26/rootfs-swap-verdict.md:53`; `opensource/docs/TIMELINE-AND-WAY-FORWARD.md:255` |

### 1.2 GET the newest userland (graft targets, safe to replace)

| item | detail | why safe | cite |
| --- | --- | --- | --- |
| generic userland | busybox, dropbear, libs, tools | 24.10 demonstrated running on the vendor kernel | `opensource/docs/phase26/rootfs-swap-verdict.md:50` |
| IPC / daemon layer | `ubus`, `ubusd`, `libubus`, `libubox`, `libblobmsg-json`, `libjson-c` | a 24.10 daemon and client completed a round trip on 5.10.201 | `opensource/docs/phase26/ubus-ipc-on-vendor-kernel.md:12,36` |
| Wi-Fi plumbing | `/etc/config/wireless`, `/lib/wifi/mac80211.sh`, `/sbin/wifi`, `wpad` | no vendor Wi-Fi userland exists, it is all stock OpenWrt, and the vendor Wi-Fi is mac80211/cfg80211 in the kernel modules | `opensource/docs/phase26/rootfs-swap-verdict.md:38-42,49` |
| package manager / feeds | `opkg` plus signed 22.03.6 feeds (as in 0.3) | restores upgrade capability; already done in 0.3 | `opensource/docs/FLASH-PLAN.md:51-56` |
| our hardening / access layer | `/etc/init.d/omosshd` plus the rc.d symlink, root key, the systime wrapper and `systime-stock.lua`, the version marker | closes the systime RCE and adds the SSH lane | `opensource/docs/FLASH-PLAN.md:48-56` |

### 1.3 Explicitly forbidden to replace

- the kernel partition and every `.ko` (no stable ABI): `opensource/docs/TIMELINE-AND-WAY-FORWARD.md:255`,
  `opensource/docs/phase26/rootfs-swap-verdict.md:53`.
- any `hi_*` binary or `hsan_*` script, and every `hi_hi_*` ubus object:
  `opensource/docs/phase26/rootfs-swap-verdict.md:51-52`.
- the calibration scripts: `opensource/docs/phase26/rootfs-swap-verdict.md:52`.
- never install official `luci-base` / `luci-compat` / `luci-app-*` on the vendor fork, because they shadow
  vendor LuCI files and leave overlay whiteouts: `opensource/docs/FLASH-PLAN.md:16-18`.

## 2. Build inputs and steps

### 2.1 Inputs

- Base userland version: 24.10.x, the proven-running userland (24.10.5 measured),
  `opensource/docs/phase26/how-current-userland.md:31,62`. Alternatives: 22.03.6 (matches the vendor
  exactly), 25.12 (same armv7 musl ABI but the apk v3 container means extra tooling for no ABI gain),
  `CUSTOM-FIRMWARE-PLAN.md:151-153`.
- Feeds: upstream OpenWrt `arm_cortex-a9` feeds. The vendor itself points at them
  (`opensource/docs/phase26/how-current-userland.md:20-23`); the 24.10.5 packages live at
  `downloads.openwrt.org/releases/24.10.5/packages/arm_cortex-a9/...`
  (`opensource/docs/phase26/how-current-userland.md:34`).
- Where the build flow lives in this repo:
  - rootfs builder plus artifacts: `build/make_custom_tar_v3.py`, `build/custom/rootfs-custom-0.3.sqfs`
    (`opensource/docs/FLASH-PLAN.md:64`), base tarball `build/custom/rootfs-2.5.24-base.tar`;
  - slot-switch recipes: `build/custom/env-bootflag-a.bin` / `env-bootflag-b.bin`
    (`CUSTOM-FIRMWARE-PLAN.md:58-59`);
  - the `.ko` CI job `opensource/.github/workflows/build-load-test-module.yml`
    (`START-HERE.md:81-84`) is NOT part of this plan, because the kernel is untouched.
- Prebuild check: run `node opensource/lab/wifidrv1/tools/prebuild-check.js` before every build
  (`START-HERE.md:62`).

### 2.2 Steps

1. Assemble a coherent 24.10 userland closure for `arm_cortex-a9`/musl. Do NOT mix 24.10 binaries with 22.03
   libraries, it is untested: `opensource/docs/phase26/library-closure-constraint.md:22-24`.
2. Graft it onto the vendor 2.5.24 rootfs, keeping every item in section 1.1. If any single component cannot
   be made coherent, use a private lib path (as 26b did) rather than replacing the shared 22.03 libs:
   `opensource/docs/phase26/ubus-ipc-on-vendor-kernel.md:36-38`.
3. Build the squashfs plus UBI image with the stock volume geometry and name (`squashfs`, static):
   `opensource/docs/FLASH-PLAN.md:137-138`.
4. Validate before flashing: an `rdsquashfs --describe` diff against the base must show only the intended
   deltas: `opensource/docs/FLASH-PLAN.md:93-95`.
5. Transfer over a verified path: `scp -O` (no sftp-server) or base64 chunks plus `openssl base64 -d -A`, with
   the md5 compared on both ends: `CUSTOM-FIRMWARE-PLAN.md:71-72` and
   `opensource/docs/phase26/how-current-userland.md:84-88`. Never trust an unverified transfer to this box.

## 3. Flashing procedure (A/B safety)

### 3.1 Slot facts

- A/B slots are real and switchable: `bootcmd=mtd read kernel${bootflag}`; the live selector is the sysenv
  register `/sys/devices/platform/sysenv/boot_reg` (`0x10` -> slot A, `0x21` -> slot B); both env copies sit
  at mtd3 / mtd4 (block = `crc32-LE(4) | flags(1) | data(131067)`),
  `CUSTOM-FIRMWARE-PLAN.md:55-59`; switch recipe `opensource/docs/FLASH-PLAN.md:195-201`.
- Every partition is dumped locally (`dumps/mtd0..mtd16`, 38 MB), which is the documented recovery path:
  `CUSTOM-FIRMWARE-PLAN.md:61`.

### 3.2 Procedure

1. Take a calibration snapshot before anything that touches the radio: `CUSTOM-FIRMWARE-PLAN.md:174`,
   `opensource/docs/TIMELINE-AND-WAY-FORWARD.md:32-34`.
2. Confirm the inactive slot is the write target, and keep one slot always stock: `CUSTOM-FIRMWARE-PLAN.md:68-69`,
   `opensource/docs/DEPLOYMENT-DECISION.md:78`.
3. Write only the inactive rootfs slot: `ubiformat /dev/mtd14 -y -f /tmp/hybrid.ubi` (slot B example):
   `opensource/docs/FLASH-PLAN.md:170,102`.
4. Verify the volume hash before switching: `ubiattach -m 14 -d 1; sha256sum /dev/ubi1_0` must equal the
   built squashfs hash: `opensource/docs/FLASH-PLAN.md:171`, `CUSTOM-FIRMWARE-PLAN.md:69-70`.
5. Switch: write both env copies, then `echo 0x21` into `boot_reg`, then reboot:
   `opensource/docs/FLASH-PLAN.md:200-201`.
6. Kernel slot writes (only if the kernel were ever touched) must write the whole 8,650,752-byte slice
   (uImage plus the FIT tail at `0x4185D0`) or the bootloader silently falls back to slot A:
   `opensource/docs/FLASH-PLAN.md:44-47`, `opensource/docs/DEPLOYMENT-DECISION.md:78-79`. Not required for a
   rootfs-only graft.
7. UART is the last resort: ttyS0, 115200 8N1, VCC not connected, and the bootmenu has TFTP update commands
   (rootfs.rw / rootfs.ro / kernel.images / uboot / esbc / bdinfo): `CUSTOM-FIRMWARE-PLAN.md:61-63`,
   `opensource/docs/DEPLOYMENT-DECISION.md:79`.

### 3.3 The executed precedent (build 0.3)

- Slot B: `mtd_num=14`, `bootflag=b`, `/dev/ubiblock0_0` sha256 `55f5c5b4...`, marker
  `omo-minimal-0.3 stock-2.5.24-20260727-122111`: `opensource/docs/FLASH-PLAN.md:57-62`.
- Rollback: slot A stayed stock (provably DISTRIB_REVISION 2.4.15, no marker, no omosshd); kernelb is
  restorable from `dumps/mtd12-kernelb.gz`: `opensource/docs/FLASH-PLAN.md:67-68`, `mem-entries.md:5,7`.
- The custom layer is baked into the slot-B squashfs, not overlay-only: `mem-entries.md:3`.

## 4. Acceptance gates

| # | gate | how to check | cite |
| --- | --- | --- | --- |
| G1 | SSH lane up | `omosshd` / dropbear answers on port 22; `[ -e /etc/rc.d/S95omosshd ]` (use `-e`, not `ls`) | `opensource/docs/FLASH-PLAN.md:48-56`; `opensource/docs/phase25/deploy-reverify-dangling-link.md:36-45` |
| G2 | Wi-Fi up | `WIPHY=2 IFACE=6` on the live device | `START-HERE.md:101`; `mem-entries.md:5` |
| G3 | calibration healthy | calibration `[SUCC]` on both bands | `START-HERE.md:101`; `CUSTOM-FIRMWARE-PLAN.md:174` |
| G4 | upgrade path still functional | a read-only `hi_hi_upgrade_*` ubus call (for example `..._flag_get_call`, `..._image_check_file_call`) still answers, and NEVER a real flash | `opensource/docs/phase26/rootfs-swap-verdict.md:26-29` |
| G5 | build identity | `/etc/custom-firmware-version` marker plus `/dev/ubiblock0_0` sha256 equal to the local artifact | `opensource/docs/FLASH-PLAN.md:59`; `opensource/docs/phase25/deploy-reverify-dangling-link.md:5-11` |
| G6 | revert path | switch `bootflag` back to `a` (both env copies plus `boot_reg=0x10`), reboot, confirm the stock slot boots | `opensource/docs/FLASH-PLAN.md:200-201`; `CUSTOM-FIRMWARE-PLAN.md:61-63` |
| G7 | no leftovers | no `.omo-off` leftovers, no new pstore records, no staged loaders | `START-HERE.md:101`; `mem-entries.md:5` |

Hard rules that gate every step above: never write CA `0x400392f0`; never read the RC misc window
`0x10161000`; never `rmmod` the vendor modules; device cycles serial/detached only via `tools/exp.sh`:
`START-HERE.md:99-101`, `CUSTOM-FIRMWARE-PLAN.md:170-174`.

## 5. Open decisions for the lead

1. Which OpenWrt release as the base: 22.03.6 (exact vendor match), 24.10.x (proven running, recommended), or
   25.12 (apk v3, extra tooling, no ABI gain): `opensource/docs/phase26/how-current-userland.md:62`,
   `CUSTOM-FIRMWARE-PLAN.md:151-153`.
2. Swap scope: the whole coherent library closure, or a curated safe subset. The closure doc says a subset is
   not safely isolable because it drags its libraries:
   `opensource/docs/phase26/library-closure-constraint.md:22-25,32`.
3. libc / libubox / libopenssl strategy: replace the shared libs wholesale, or keep the 22.03 libs and use a
   private lib path for the new userland (26b's method):
   `opensource/docs/phase26/ubus-ipc-on-vendor-kernel.md:36-38`.
4. Include our tools and hardening layer? The omosshd SSH lane, systime fix, opkg and marker are carried in
   0.3 and cheap to keep: `opensource/docs/FLASH-PLAN.md:48-56`.
5. luci decision: the vendor LuCI must not be shadowed by upstream `luci-base` / `luci-app-*`
   (`opensource/docs/FLASH-PLAN.md:16-18`), so decide whether the hybrid ships vendor LuCI as-is or a curated
   LuCI set.
6. Acceptance threshold: is section 4's set sufficient, or must G4 (the upgrade path) be exercised with a real
   `image_check_file` against a signed image before the slot is blessed?
7. Scope confirmation: the user declined the wholesale modern rootfs on 2026-10-03
   (`opensource/docs/phase28/firmware-half-settled.md:29`), so confirm the graft (rootfs-level, slot B, slot A
   stock) is now the wanted direction, since it is the same rootfs-level operation that verdict warned about.
