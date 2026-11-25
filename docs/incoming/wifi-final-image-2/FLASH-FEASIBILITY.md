# FLASH-FEASIBILITY - can FINAL IMAGE #2 be installed rootfs-only?

**Lane:** wifi-final-image-2 (task-159), host-only. **Nothing was flashed,
written or rebooted by this lane.** This note re-checks the rootfs-only install
question for the new image against the recorded partition geometry and the
built bytes. The fire itself is the Lead's attended decision.

**Answer: YES - the WRITE side is rootfs-only; the kernelb slot needs no write
at all.** Same conclusion as wifi-final-image (task-147), re-derived for the new
rootfs; what changed versus image B is the rootfs content only.

## 1. Why no kernel write is needed (proof from the built bytes)

| property | value | consequence |
| --- | --- | --- |
| kernel slice | 3,372,638 B sha256 `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` | identical to the hardware-validated slice already written by the ABI-image install |
| full kernel slot (8,650,752 B) | `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e` | the kernelb/mtd12 content is already the validated slice; a rootfs-only install leaves it untouched |
| built kernel .config | `08b6e59511ab5e568a6afb73da22958283a648c6fffe4987f6d0a245066f676c` (pre == post, all runs) | the kernel was NOT rebuilt (8-10 s rootfs-only builds) |
| FORCED cmdline | `CONFIG_CMDLINE="ubi.mtd=rootfsb ..."` + `CONFIG_CMDLINE_FORCE=y` | the image still roots from the **rootfsb** UBI volume -> write mtd14, stay pair b |
| new rootfs UBI | 3,801,088 B sha256 `44df3a63cfd9b9811a4ad99d8f49ae917a49c4408719d0df4582ed50785faedb` | the only changed flash member |
| container | 7,188,799 B sha256 `0d9429ec4681d43966bd2f23dc3fbe872f232961eaf142c16f565c67be5b5a15` | same container SIZE as image B `8944740e` (7,188,799 B) -> the installer's split arithmetic is unchanged |

The sysupgrade container still carries both members; extracting only `root`
gives the new rootfs UBI with the hash above.

## 2. Does it fit?

* rootfsb/mtd14 partition size: **0x1740000 = 24,379,392 B**
  (dumps/router-20261108/ubi-readpath-report.md:42 - mtd13/mtd14 are 0x1740000 each).
* new rootfs UBI: **3,801,088 B** -> 15.6 % used, **20,578,304 B free**.
  Fits with the same margin as image B (identical 3,801,088 B).
* The container's root member is already the exact UBI image `ubiformat` expects.

## 3. Does the current installer support it?

Unchanged from wifi-final-image task-147: `tools/stock-telnet-install.sh`
(root repo) has no `--rootfs-only` flag; `--apply` always writes BOTH pair-b
members. Using it AS-IS is **SAFE for this image** (the kernel write is the same
`13e81214...` bytes, verified by the tool's slot read-back), it is simply an
unnecessary kernel-slot erase/write.

## 4. Strict rootfs-only path (unchanged sequence, new hash)

    # host, from the container
    tar -xOf final-image-2-sysupgrade.bin sysupgrade-cudy_wr3000-v2/root > rootfs.ubi
    sha256sum rootfs.ubi
    # must be 44df3a63cfd9b9811a4ad99d8f49ae917a49c4408719d0df4582ed50785faedb
    # serve rootfs.ubi over the same HTTP base the tool uses (default
    # http://192.168.10.44:8000)

    # stock pair-A root telnet shell (recovery armed; pair A active)
    dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum   # expect 1794a531...
    cd /tmp && wget -q -O /tmp/fi2-r.ubi http://192.168.10.44:8000/rootfs.ubi
    sha256sum /tmp/fi2-r.ubi      # must match 44df3a63...
    ubiformat /dev/mtd14 -y -f /tmp/fi2-r.ubi ; echo UBI=$?   # expect UBI=0
    dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum   # still 1794a531...
    ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b
    fw_printenv bootflag         # bootflag=b
    echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg
    sync; reboot

## 5. Prerequisites / bench state (Lead decisions, not image defects)

1. **Stock must be the active pair** for this transport; switch to pair A first
   if the box currently boots our pair-b image.
2. Human at the bench; recovery card READY (power-off + RESET counter + TFTP);
   UART capture read-only. No human = no fire.
3. Overlay loss accepted: the rootfs write wipes `rootfs_data` (SSH host keys,
   uci config, /root).
4. PD4 firmware + `wifi_block.bin` must be re-pushed by hand after the write
   (they are never packaged; see payload/etc/hi5622/FIRMWARE-POLICY.txt).
5. **The association fire needs the antennas attached** - the last bench blocker
   is RF (~44 dB loss without antennas) plus the flaky client MT7922.

## 6. Rollback

* stock pair A stays bootable throughout - kernelb and pair A are never written
  by a rootfs-only install.
* The previous image rootfs (image B `7817f090...`, or task-147 `6196232d...`)
  can be re-written to mtd14 the same way, then handed back to pair b.
* Physical RESET+TFTP recovery remains the last resort (BENCH-FACTS.md).

## 7. What is NOT possible / NOT claimed

* No partial "write only the changed files" path: the rootfs is a squashfs
  volume inside a UBI image; the install unit is the UBI image.
* The image is BENCH-ONLY (research driver, bench policy payload); the install
  path has not been executed end-to-end **for this image** by any lane.
* No sysupgrade/on-device install path exists on our image for this write; the
  transport is the stock telnet shell.
