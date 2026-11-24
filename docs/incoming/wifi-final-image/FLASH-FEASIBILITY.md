# FLASH-FEASIBILITY - can the FINAL image be installed rootfs-only?

**Lane:** wifi-final-image (task-147), host-only. **Nothing was flashed, written
or rebooted by this lane.** This note re-checks the rootfs-only install
question for the new image against the current repo tooling and the recorded
partition geometry; the flash itself is the Lead's attended decision.

**Answer: YES - the WRITE side is rootfs-only, and the kernelb slot needs no
write at all.** The transport supports it; the current root-repo installer does
not expose a `--rootfs-only` flag, so there are two safe paths (as-is installer,
or the manual strict sequence below). Everything else is a bench-state
prerequisite (stock must be the active pair), not an image-feasibility problem.

## 1. Why no kernel write is needed (proof from the built bytes)

| property | value | consequence |
| --- | --- | --- |
| kernel slice | 3,372,638 B sha256 `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` | identical to the hardware-validated slice already written by the 2026-11-27 ABI-image install |
| full kernel slot (8,650,752 B) | `1794a531ed19548e4b82467f21c5775e6c59cf1ca21d7718a0ec3ec59054460e` | the kernelb/mtd12 content is already the validated slice; a rootfs-only install leaves it untouched |
| built kernel .config | `08b6e59511ab5e568a6afb73da22958283a648c6fffe4987f6d0a245066f676c` (before == after) | the kernel was NOT rebuilt (16 s rootfs-only build) |
| new rootfs UBI | 3,538,944 B sha256 `6196232d878e141ad1833a5dbed4ef4e339dd41d5c450151842c285a328ba30e` | the only changed flash member |
| container | 6,922,559 B sha256 `0efcd1f1a5b16a73f040695f90ae00b41684d0f9348b60b1a13b45b2e27c2ada` | same container SIZE as `ae5e28bf` -> the installer's split arithmetic (kernel 8,650,752 B slot etc.) is unchanged |

The sysupgrade container still carries both members; extracting only `root`
gives the new rootfs UBI with the hash above. The image's baked cmdline still
hardcodes `ubi.mtd=rootfsb`, so the new rootfs must go to **mtd14 (rootfsb)**
and the box must stay on **pair b** - exactly the proven handover.

## 2. Does it fit?

* rootfsb/mtd14 partition size: **0x1740000 = 24,379,392 B**
  (dumps/router-20261108/ubi-readpath-report.md:42 - mtd13/mtd14 are 0x1740000 each).
* new rootfs UBI: **3,538,944 B** -> 14.5 % used, **20,840,448 B free**. Fits
  with the same margin as the previous image (also 3,538,944 B).
* The container's root member is already the exact UBI image `ubiformat`
  expects - no repacking step.

## 3. Does the current installer support it?

`tools/stock-telnet-install.sh` (root repo) is the proven stock-assisted
transport (wget over HTTP from the stock root telnet shell). Re-checked now:

* options are `--inspect/--apply/--attended/--recovery-armed/--image/`
  `--staging/--http-base/--host/--boot-timeout` - **no `--rootfs-only`**
  (grep count for `rootfs-only` = 0; tool sha256
  `3a8fbaa035d4a1800c422195e23bd08a40ab45e778d5e715668c4b6b5f5c13d0`).
* `--apply` always writes BOTH pair-b members: `mtd erase + mtd write`
  `/dev/mtd12` (kernelb, full-slot hash read-back) then `ubiformat
  /dev/mtd14` (rootfsb), then env bootflag + selector cell + reboot.

**Using it AS-IS is SAFE for this image** (the kernel write is the same
`13e81214...` bytes, verified by the tool's slot read-back; pair A/stock stays
untouched) - it is simply an unnecessary kernel-slot erase/write.

## 4. Strict rootfs-only path (choose one)

### (a) Extend the installer (root repo, outside this lane's write scope)

~10 lines: add `--rootfs-only` that skips the `mtd12` erase/write block and
its slot-hash read-back, keeps wget/verify + `ubiformat /dev/mtd14` + the
env/selector handover + post-boot cmdline/marker checks. Everything else in the
tool already applies.

### (b) Manual sequence (proven transport, kernelb untouched)

Host, from the container:

    tar -xOf final-image-sysupgrade.bin sysupgrade-cudy_wr3000-v2/root > rootfs.ubi
    sha256sum rootfs.ubi
    # must be 6196232d878e141ad1833a5dbed4ef4e339dd41d5c450151842c285a328ba30e
    # serve rootfs.ubi over the same HTTP base the tool uses (default
    # http://192.168.10.44:8000)

Stock pair-A root telnet shell (recovery armed; pair A active):

    # 1. record the kernel slot we must NOT change (expect 1794a531...)
    dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum
    # 2. fetch + verify the new rootfs UBI
    cd /tmp && wget -q -O /tmp/fi-r.ubi http://192.168.10.44:8000/rootfs.ubi
    sha256sum /tmp/fi-r.ubi          # must match 6196232d...
    # 3. write ROOTFSB ONLY (mtd14); pair A and kernelb untouched
    ubiformat /dev/mtd14 -y -f /tmp/fi-r.ubi ; echo UBI=$?   # expect UBI=0
    # 4. confirm the kernel slot is still the validated slice
    dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum   # still 1794a531...
    # 5. handover to pair b (proven sequence; see wifi-image-integration/DEPLOY.md)
    ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b
    fw_printenv bootflag             # bootflag=b
    echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg
    sync; reboot

## 5. Prerequisites / bench state (Lead decisions, not image defects)

1. **Stock must be the active pair** for this transport (the broker/telnet route
   only exists on the vendor stock userspace). If the box currently boots our
   pair-b image, switch to stock pair A first (`fw_setenv bootflag a` +
   selector cell 0x10) - see wifi-image-integration/DEPLOY.md preconditions and
   SELECTOR-0x21.md.
2. Human at the bench; recovery card READY (power-off + RESET counter + TFTP);
   UART capture read-only. No human = no fire.
3. Overlay loss accepted: the rootfs write wipes `rootfs_data` (SSH host keys,
   uci config, /root). Back up what is needed; not a substitute for rollback.
4. PD4 firmware + `wifi_block.bin` must be re-pushed by hand after the write
   (they are never packaged; rootfs wipe removes them).

## 6. Rollback

* **stock pair A stays bootable** throughout - kernelb and pair A are never
  written by a rootfs-only install.
* Previous rootfs UBI (the `ae5e28bf` image's root, `ff9f3e1c83c36a2a9170cdf6373228692b46b7fa0fbb64abbdc70a25b841aaa9`)
  can be re-written to mtd14 the same way, then hand back to pair b.
* Physical RESET+TFTP recovery remains the last resort (BENCH-FACTS.md).
* A failed rootfs write cannot remove our kernel: kernelb is never touched, so
  at worst the box boots the validated kernel with the old/new rootfs; pair A
  (stock) is the escape hatch.

## 7. What is NOT possible / NOT claimed

* There is no partial "write only the changed files" path: the rootfs is a
  squashfs volume inside a UBI image; the install unit is the UBI image.
* The image is still BENCH-ONLY (research driver, bench policy payload); the
  install path has not been executed end-to-end **for this image** by any lane.
  The text above proves the write geometry; the Lead's attended fire proves the
  boot.
* No sysupgrade/on-device install path exists on our image for this write; the
  transport is the stock telnet shell.
