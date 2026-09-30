# WR3000 V2.0 firmware builds - flash plan and results

## Maximised 0.3 state (2026-10-01, after the feature pass)

Installed on top of 0.3 (all from the official 22.03.6 arm_cortex-a9 feeds, signature-verified):
`adblock` (service; list fetching needs a WAN), `vnstat` (enabled and running), `mtr`, `nano`,
plus `htop` and `jq` from the earlier pass. opkg itself needed two vendor-stripped pieces restored
before signatures could verify: the `usign` binary (`opkg install --force-reinstall /tmp/usign.ipk`,
because the vendor DB already claimed it was installed) and `/etc/opkg/keys/` (both keys, restored
from the 2.4.15 tree).

Not installable on this release/kernel (tried and confirmed): `iperf3` (depends on `libatomic1`,
not published for 22.03.6 arm_cortex-a9), `nmap` (no such package in the 22.03.6 feeds),
`sqm-scripts` (needs `kmod-ifb` and `kmod-sched-cake` for this exact kernel; neither exists).

**Warning learned the hard way:** do NOT install `luci-base`/`luci-compat`/`luci-app-*` from the
official feeds on this vendor fork. They shadow the vendor's LuCI files; removing them afterwards
leaves overlay whiteouts that hide the vendor originals, and the UI returns 502 / directory listings.
Repair: `sh tools/restore-vendor.sh` (copies every file present in `/rom` but missing in the merged
tree back from `/rom`; 75 files in our case), then `/etc/init.d/uhttpd restart`.

## Driver black-box surface (what is visible without source)

- 58 vendor kernel modules under `/lib/hisilicon/ko/`; the Wi-Fi pair is `hi5622v100_wifi.ko`
  (3,581,748 bytes, `license=GPL`, `vermagic=5.10.201 SMP mod_unload ARMv7`) and `hi5622v100_plat.ko`.
- Closed firmware blob: `/lib/firmware/hi_wifi/FIRMWARE.bin` (928,920 bytes) plus
  `cfg_hi5622v100_hisi.ini` (32,315 bytes) and `cfg_device_hisi.ini` (799 bytes).
- Only one runtime knob is exposed: `/sys/module/hi5622v100_wifi/parameters/g_en_rx_packet_4096_length`.
- debugfs exposes a standard `ieee80211` directory; there is no vendor proc/debugfs control surface in
  the driver strings (only generic watchdog sysctls).

# Custom minimal firmware for WR3000 V2.0 (build 0.3 - personal full build)

Build 0.3 = the official Cudy **2.5.24** firmware (kernel partition + rootfs) plus our access/package
layer. It replaces 0.2 as the slot B build. Slot A stays stock.

## 0.3 delta versus 0.2

- Base: official `WR3000V2-2.5.24` rootfs (from Cudy's sysupgrade zip) instead of 2.4.15.
- Kernel: the official 2.5.24 kernel partition was flashed to **kernelb (mtd12)**. This is mandatory:
  the 2.5.24 rootfs with the older kernel panics at boot with
  `Kernel panic - not syncing: The ShuangTa ep device must enable gpio-timing-quirk, please check the
  PCIe driver configuration.`
- Kernel partition layout lesson: `kernela`/`kernelb` (mtd11/mtd12) are 8,650,752-byte images made of a
  uImage (`Linux-5.10.201`, 4,292,144 bytes) followed by a FIT tail (DTB and friends) at offset
  0x4185D0. Flashing only the uImage makes the bootloader skip the slot silently and fall back to A.
  Extract the full 8,650,752-byte slice from the official container (the slice starts at 0x80E5C in both
  2.4.15 and 2.5.24 images - located by finding the live dump's first bytes inside the older container)
  and write the whole slice.
- Transfer lesson: pushing multi-MB files with `cat > file` over ssh corrupted the kernel image twice
  (hash mismatch on the device). `scp -O` works (this device has no sftp-server).
- Layer kept identical to 0.2: `etc/init.d/omosshd` + rc.d symlink, root authorized_keys, shadow,
  version marker (`omo-minimal-0.3 stock-2.5.24-20260727-122111`), README, the systime wrapper plus the
  vendor 2.5.24 module as `systime-stock.lua`, opkg + opkg-key + `/etc/opkg.conf` + distfeeds +
  customfeeds. libubox comes from the base image.

## 0.3 result (verified on the device)

- Slot B: `mtd_num=14`, `bootflag=b`, `/dev/ubiblock0_0` sha256 = 55f5c5b4..., marker
  `omo-minimal-0.3 stock-2.5.24-20260727-122111`, kernelb readback = 98b11f29...
- LAN up, 6 Wi-Fi interfaces, dropbear running, no panic lines in dmesg, opkg lists 402 packages.
- Web UI: login OK; the systime injection probe returns 200 and creates nothing; a valid time value
  still sets the clock (device moved to 20:30:00 UTC through the page).
- Artifacts: `build/custom/rootfs-custom-0.3.sqfs` (15753216, sha256 55f5c5b4...),
  `build/custom/kernelb-2.5.24-full.img` (8650752, sha256 98b11f29...),
  builder `build/make_custom_tar_v3.py`, README source `build/custom/README-CUSTOM-03.md`.
- Rollback: slot A is stock; kernelb can be restored from `dumps/mtd12-kernelb.gz` and slot B's rootfs
  from `build/custom/rootfs-custom-0.2.sqfs` at any time.

# Custom minimal firmware for WR3000 V2.0 (build 0.2)

Build 0.2 is 0.1 plus a fix for the vendor command injection and a working package manager.
Sections below marked 0.1 still describe the base; the 0.2 delta is in this first part.

## 0.2 delta versus 0.1

- `usr/lib/lua/luci/model/cbi/system/systime.lua`: plain-text LuCI wrapper (this vendor Lua
  loads text modules) that strips single quotes from the `timeclock` and
  `cbid.system.ntp.current` form values, then loads the original stock module from
  `systime-stock.lua` (or `/rom/usr/lib/lua/luci/model/cbi/system/systime.lua` on a stock slot)
  with `setfenv(chunk, getfenv(1))` so LuCI's sandbox environment (translate, etc.) is kept.
- `usr/lib/lua/luci/model/cbi/system/systime-stock.lua`: the untouched stock module (bytecode),
  kept so the wrapper has something to load without depending on /rom.
- `usr/bin/opkg` (official 22.03.6 arm_cortex-a9 build, d038e5b6), `usr/sbin/opkg-key`,
  `etc/opkg.conf` (with `option check_signature`), `etc/opkg/distfeeds.conf` (the five working
  official arm_cortex-a9 feeds), `etc/opkg/customfeeds.conf`. libubox is not added because the
  stock image already ships `/lib/libubox.so.20220515`.
- `etc/custom-firmware-version` = `omo-minimal-0.2 stock-2.4.15-20251030-114751`.
- `root/README-CUSTOM.md` updated for 0.2 (access, opkg, slot switching, recovery).

Artifacts: `build/custom/rootfs-custom-0.2.sqfs` (sha256 f69aa4c5...) and the device-built
`/tmp/custom02.ubi` (sha256 7906e955...). Builder: `build/make_custom_tar_v2.py`.

Verified before flashing: `rdsquashfs --describe` diff versus the 0.1 image shows only the five
added files; `rdsquashfs --cat` confirms the wrapper source, the 0.2 marker, and the new
`distfeeds.conf`. On the live 0.1 system the wrapper was tested from the overlay: exploit POST
returns 200 with no command executed, and a valid time value still sets the clock.

## 0.2 result (executed)

- Slot A was switched to and confirmed first (mtd13, bootflag=a, stock, no marker).
- From slot A, slot B was erased and written: `ubiformat /dev/mtd14 -y -f /root/custom02.ubi`,
  then `/dev/ubi1_0` sha256 = f69aa4c5... (the 0.2 squashfs).
- Switched back to slot B (env bootflag=b on both copies + `boot_reg` 0x21). Live state:
  `/dev/ubiblock0_0` sha256 f69aa4c5..., marker `omo-minimal-0.2 stock-2.4.15-20251030-114751`,
  and the /rom files hash-match the build artifacts: wrapper d284fd77..., stock module 590351e3...
  (untouched stock bytecode), opkg 6b78ea0f....
- Exploit test on 0.2: the payload POST returns 200 and creates nothing; a valid time value still
  sets the clock (device went 18:01:15 -> 19:00:00 UTC through the web UI).
- opkg on 0.2: `opkg update`, `opkg remove jq`, `opkg install jq` all succeed against a local feed
  served by the router itself; jq 1.6 runs; 399 packages listed. Official feeds restored with
  `option check_signature` afterwards.
- Slot A re-verified untouched after the flash: `/dev/ubi2_0` sha256 = 8f5ab9a3... (stock).
- Official feeds verified live through a temporary tunnel (the router still has no WAN cable): the
  router's traffic was routed through the build workstation with `ssh -R 127.0.0.1:8899` plus the
  tiny Node proxy in `tools/proxy.js`. `opkg update` over the shipped HTTPS config then reported
  `Signature check passed` for all five feeds, and `opkg install htop` resolved and installed
  terminfo + libncurses6 + htop (3.3.0 runs). No libustream is needed: the vendor fetch stack
  already speaks TLS. The temporary /etc/hosts entry and the tunnel were removed afterwards.
- Overlay left clean: the staged UBI was deleted from /root (overlay usage 1.8 MB of 22 MB).

The 0.1 result section below describes the earlier build and is kept as history.

# Custom minimal firmware for WR3000 V2.0 (build 0.1)

## Base

Stock 2.4.15 rootfs taken from the live device (`stock/rom-live.tar.gz`, and the raw squashfs
`stock/rootfs-live.sqfs`). Verified byte identical to the official image
`firmware/WR3000V2-2.4.15.zip.rootfs.sqfs` for the full 14,862,432 bytes of the image.
Kernel partitions kernela and kernelb are byte identical to each other (both stock).
Both rootfs slots currently hold the same stock squashfs (sha256 8f5ab9a3...).

## Artifacts

- `build/custom/rootfs-custom.sqfs` sha256 2b21db24... (squashfs 4.0, xz, 128K blocks)
- `build/custom/rootfs-custom.ubi` sha256 22daf1dc... (UBI image, volume name `squashfs`, static,
  117 LEBs of 126976 bytes; geometry p 131072, m 2048, s 2048, O 2048; matches stock geometry)
- `build/custom/rootfs.tar` (intermediate tar)

## Changes versus stock (six entries, nothing removed)

1. `etc/init.d/omosshd` (0755): starts dropbear on port 22 with `-R`.
2. `etc/rc.d/S95omosshd` (symlink to `../init.d/omosshd`).
3. `root/.ssh/authorized_keys` (0600): the build owner key pair.
4. `etc/shadow` (0600 instead of stock 0755): root password hash kept as set during the takeover.
5. `etc/custom-firmware-version` (0644): build marker text.
6. `root/README-CUSTOM.md` (0644): short recovery and access notes.

## Validation performed

- `rdsquashfs --describe` diff versus stock shows only those additions plus the shadow mode change.
- `rdsquashfs --cat` of each injected file works and shows the intended content.
- UBI image parses with ubi_reader; extracted volume payload sha256 equals the squashfs sha256.
- Volume geometry, name, and type match the stock volume on both slots.
- Kernel partitions on both slots are identical stock builds, so kernel A or B behave the same.

## Flash plan (NOT executed; requires explicit approval)

Phase 2, slot switch pilot (both slots stock, fully reversible):
1. Trial A: set the kernel upgrade flag to 1 via `hi_ipc /home/cmd/upgrade/hi_upgrade_flag_set -v flag 1`
   and reboot. Observe `/sys/class/ubi/ubi0/mtd_num` (13 means slot A, 14 means slot B) and
   `fw_printenv bootflag`.
2. Trial B (only if A does nothing): write a new env block with `bootflag=b` to the erased env copy
   (mtd3), CRC32 little endian over the data area, flags 1. Keep the existing env copy (mtd4) intact.
   Reboot and observe again.
3. Revert to slot A with the same lever (flag 0 or env bootflag=a) and confirm the device is back.

Phase 3, flash custom rootfs into slot B (slot A stays stock as the fallback):
4. `ubiformat /dev/mtd14 -y -f /tmp/custom.ubi`
5. Verify: `ubiattach -m 14 -d 1; sha256sum /dev/ubi1_0` must equal the custom squashfs hash
   2b21db24...; then detach.
6. Switch to slot B with the lever proven in phase 2, reboot, then verify:
   `cat /etc/custom-firmware-version`, `id`, ssh works, network and wifi normal.

Rollback options:
- Switch back to slot A with the same lever (A is untouched stock).
- If the switch itself misbehaves, the ultimate recovery path is the UART console
  (ttyS0, 115200, bootdelay 1, bootmenu with TFTP update commands), plus full partition dumps in
  `dumps/` for restore.
- Stock slot B content can be restored from `dumps/mtd14-rootfsb.gz` if ever needed.

## Result (executed)

The custom image booted from slot B and is verified live:

- Active slot: mtd14 (slot B), `fw_printenv bootflag` = b, `/dev/ubiblock0_0` sha256 = 2b21db24...
- Marker present: `/etc/custom-firmware-version` = omo-minimal-0.1 stock-2.4.15-20251030-114751
- LAN 192.168.10.1 up, wifi APs up, web UI answering, root ssh working, overlay intact
- Slot A still holds the untouched stock image (fallback)
- Both kernel partitions are byte identical stock (sha256 039c7877...)

## Slot switch recipe (empirical, verified)

- The selector is the sysenv register at `/sys/devices/platform/sysenv/boot_reg`:
  - `0x10` boots slot A; `0x21` boots slot B (also write the env copies to match).
- Env copies live at mtd3 and mtd4; write BOTH with the same 128 KiB block:
  blocks are `crc32-little-endian(4) | flags(1) | data(131067)`, prepared files:
  `build/custom/env-bootflag-a.bin` (slot A) and `build/custom/env-bootflag-b.bin` (slot B).
- Switch commands used: `flash_eraseall /dev/mtd3` and `/dev/mtd4`, `mtd write /tmp/env-bootflag-X.bin /dev/mtdX`,
  then `echo 0x10` (A) or `echo 0x21` (B) into `/sys/devices/platform/sysenv/boot_reg`, then reboot.
- Stock slot B content is preserved in `dumps/mtd14-rootfsb.gz` (restore with ubiformat if ever needed).

- The vendor systime command injection (published in the gist) is left untouched in this build to
  stay as close to stock as possible. A v2 build can carry the 2.5.24 style fix if wanted.
- No flash writes have happened so far. Slot A and slot B content is stock at the time of writing.
