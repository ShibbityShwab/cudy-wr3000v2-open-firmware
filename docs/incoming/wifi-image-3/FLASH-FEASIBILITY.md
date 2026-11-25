# FLASH-FEASIBILITY - can IMAGE #3 be installed rootfs-only?

**Lane:** wifi-image-3 (task-196), host-only. **Nothing was flashed, written or
rebooted by this lane.** This note re-checks the install question for the new
image from the recorded geometry and the built bytes; the fire is the Lead's
attended decision.

**Answer: YES - the WRITE side can stay rootfs-only; the kernelb slot needs no
write at all.** The kernel slice and the forced command line are unchanged from
image #2; only the rootfs member is new.

## 1. Why no kernel write is needed (from the built bytes)

| property | value | consequence |
| --- | --- | --- |
| kernel slice in the container | 3,372,638 B sha256 `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` | identical to the hardware-validated slice already in kernelb |
| full kernel slot (8,650,752 B) | `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e` | kernelb/mtd12 is untouched by a rootfs-only install |
| forced cmdline | `CONFIG_CMDLINE="ubi.mtd=rootfsb ..."` + `CONFIG_CMDLINE_FORCE=y` (unchanged kernel) | the image still roots from the **rootfsb** UBI volume |
| new rootfs UBI | 4,456,448 B sha256 `90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3` | the only changed flash member |
| container | 7,843,840 B sha256 `4cd85b5d24826774878504c333d7ea24a58b423401245f2d52c699d9e54a5f53` | 655,041 B larger than image #2 - the whole difference is the larger rootfs UBI (the bigger module + 3 payload files, xz); the kernel member is byte-identical |

## 2. Does it fit?

* rootfsb/mtd14 partition size: **0x1740000 = 24,379,392 B**
  (dumps/router-20261108/ubi-readpath-report.md:42 - mtd13/mtd14 are 0x1740000).
* IMAGE #3 rootfs UBI: **4,456,448 B** (34 PEBs) -> 18.3 % used,
  **19,922,944 B free** (the wifi-image-update lane's capacity check said
  20.6 MB free for the same UBI; the exact arithmetic is here).
* The container's root member is already the exact UBI image `ubiformat`
  expects; extract it with `tar -xOf ... sysupgrade-cudy_wr3000-v2/root`.

## 3. Strict rootfs-only path (same sequence as image #2, new hash)

    # host, from the container
    tar -xOf image-3-sysupgrade.bin sysupgrade-cudy_wr3000-v2/root > rootfs.ubi
    sha256sum rootfs.ubi
    # must be 90ae340cdd2df7c77a2840accbbebe9f537b417eac1d5d4384723b7a7cfdf2e3
    # serve it over the same HTTP base the tool uses

    # stock pair-A root telnet shell (recovery armed; pair A active)
    dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum   # expect 1794a531...
    cd /tmp && wget -q -O /tmp/i3-r.ubi http://192.168.10.44:8000/rootfs.ubi
    sha256sum /tmp/i3-r.ubi      # must match 90ae340c...
    ubiformat /dev/mtd14 -y -f /tmp/i3-r.ubi ; echo UBI=$?   # expect UBI=0
    dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum   # still 1794a531...
    ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b
    fw_printenv bootflag         # bootflag=b
    echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg
    sync; reboot

The current installer `tools/stock-telnet-install.sh` (root repo) has no
`--rootfs-only` flag; `--apply` always writes both pair-b members. Using it
AS-IS is safe for this image too (the kernel write is the same `13e81214...`
bytes), it is simply an unnecessary kernel-slot erase/write. The strict path
above is the minimal write.

## 4. Prerequisites / bench state (Lead decisions, not image defects)

1. Stock must be the active pair for this transport; switch to pair A first if
   the box currently boots our pair-b image.
2. Human at the bench; recovery card READY; UART capture read-only.
   **No human = no fire.**
3. Overlay loss accepted: the rootfs write wipes `rootfs_data` (SSH host keys,
   uci config, /root).
4. **PD4 firmware + wifi_block.bin must be re-pushed by hand after the write**
   (they are never packaged; see /etc/hi5622/FIRMWARE-POLICY.txt and
   IMAGE-3-NOTES.txt).
5. The image loads NOTHING at boot (autoload=0): after the reboot the operator
   runs `hi5622-load` with the gate set of the rung being fired.

## 5. Rollback

* stock pair A stays bootable throughout - kernelb and pair A are never written
  by a rootfs-only install.
* A previous rootfs UBI (image #2 `31e7a39c...`, the wifi-image-update
  `d3c665f1...`) can be re-written to mtd14 the same way, then handed back to
  pair b.
* Physical RESET+TFTP recovery remains the last resort (BENCH-FACTS.md).

## 6. What is NOT possible / NOT claimed

* No partial "write only the changed files" path: the rootfs is a squashfs
  volume inside a UBI image; the install unit is the UBI image.
* This lane did not execute any install; no boot of IMAGE #3 is claimed.
* The image is BENCH-ONLY (research driver, bench policy payload).
* No sysupgrade/on-device install path exists on our image for this write; the
  transport is the stock telnet shell.
