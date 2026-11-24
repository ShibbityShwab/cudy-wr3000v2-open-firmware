# DEPLOY - installing the bench image / its bits on the box (attended)

**Lane:** img-integ (task-112). Host-only lane: **none of these steps was
executed on the router by this lane.** They are the handoff to the Lead for an
attended session (human at the bench, recovery door armed, UART capture
read-only - see BENCH-FACTS.md / RUNBOOK.md).

Read this together with:
* `../wifi-upstream-target/CFG80211-BENCH-IMAGE.md` - the cfg80211 kernel
  config handoff and the proven kernel-slot push procedure;
* `../cudy-backup-root/REPORT.md` + RUNBOOK section 6 - the no-broker root
  channel on stock (used for the 2026-11-25 install);
* `../../target-skeleton/RELEASE-NOTES.md`, `../../SELECTOR-0x21.md` - A/B
  selector rules; and the project hard rules (never write the active pair,
  never write CA 0x400392F0, no rmmod after release, never write 0xCECE during
  boot).

## What the built image changes

| item | built image (this lane) | previous running image 22b72852 |
| --- | --- | --- |
| container | 6,533,439 B sha256 `df6a78c3...` | 6,267,199 B sha256 `22b72852...` |
| kernel slice | **UNCHANGED** 3,372,638 B sha256 `13e81214...` (verifier asserts it) | 3,372,638 B sha256 `13e81214...` |
| rootfs UBI | 3,145,728 B sha256 `62344e80...`; + iw, libiwinfo(+data), iwinfo, wireless-regdb, cfg80211.ko, hi5622_wifi.ko | 2,883,584 B sha256 `b33b1d91...`; no iw/iwinfo/regdb, cfg80211 + driver insmodded from /tmp |

Facts and hashes are in `build-evidence/build-summary.txt`; a local convenience
copy of the container, kernel slice, rootfs UBI, squashfs and the seven .apk
packages is at `/home/shibbityshwab/wifi-image-integration-artifacts/`
(not in the repository).

Deployable .apk files from the same build (paths in the release tree):
* userspace: `bin/packages/arm_cortex-a9/base/{iw-6.17-r1.apk,
  iwinfo-2026.05.26~66bdd1a0-r1.apk, libiwinfo20230701-...apk,
  libiwinfo-data-...apk, wireless-regdb-2026.09.03-r1.apk}`
  (sha256 in BUILD-NOTES.md);
* modules: `bin/targets/hisilicon/luofu/packages/{kmod-hi5622-cfg80211,
  kmod-hi5622-wifi}-6.18.60-r1.apk` (kernel dependency
  `kernel=6.18.60~...`, i.e. the same kernel package as the running image).
The image is still **BENCH-ONLY / NOT FLASHABLE AS A PRODUCT** (the container
keeps its `BOOT-VERIFIED RECIPE / NOT FLASHABLE-UNTIL-INSTALLED` marker).

Decision (see REPORT.md sec. 4): for the **next** bench session keep the
proven zero-flash deployment (T0 below); rewrite the pair-B rootfs (T1) only in
a dedicated attended window where losing the persistent `/overlay` is
acceptable. There is no kernel write in either path.

## T0 - zero-flash deployment of the same artefacts (recommended next session)

The image's rootfs content can also be installed into the RUNNING pair-B
rootfs (the persistent overlay is on UBIFS, so this survives reboot) without
touching flash layout. This is the least-risk way to use the integration:

1. Host: take the built packages (same artefacts the image was built from;
   paths and sha256 in BUILD-NOTES.md, copy in
   `/home/shibbityshwab/wifi-image-integration-artifacts/`):
   ```
   ls $OWRT/bin/packages/arm_cortex-a9/base/ | grep -E '^(iw|iwinfo|libiwinfo|wireless-regdb)'
   ls $OWRT/bin/targets/hisilicon/luofu/packages/ | grep -E '^kmod-hi5622'
   ```
2. Copy `iw`, `libiwinfo*`, `iwinfo`, `wireless-regdb`, `kmod-hi5622-*`
   .apk files to `/tmp` on the box (rscp.sh), then:
   ```
   apk add --allow-untrusted /tmp/iw-*.apk /tmp/libiwinfo-*.apk /tmp/iwinfo-*.apk \
       /tmp/wireless-regdb-*.apk /tmp/kmod-hi5622-*.apk
   ```
   Verify the kmod kernel dependency string matches the running kernel
   (`apk info -R kmod-hi5622-wifi` / the apk's `kernel=6.18.60~...` field); if
   the running kernel was built in this tree the versions match. If apk refuses
   (kernel hash differs), fall back to the proven direct copy:
   `/tmp` insmod of `cfg80211.ko` + `hi5622_wifi.ko` (as in the 2026-11-25
   milestone) plus `/tmp/iw` + `LD_LIBRARY_PATH` for libnl-tiny.
3. Rollback: `apk del <packages>` (or delete the copied files); no flash
   change, no selector change, no recovery cycle.

T0 is a deployment convenience only; it does not prove the image boots.

## T1 - full pair-B rewrite of the bench image (dedicated attended window)

Preconditions (all must hold):
1. Human at the bench; recovery card READY (power-off + RESET counter + TFTP,
   `tools/recovery-card.sh`); UART capture running read-only to an absolute
   path (`tools/uart-capture.sh`).
2. Recovery image digest matches the proven one and the box is healthy on our
   image, pair B (`/proc/cmdline` ubi.mtd=rootfsb; selector raw 0x11).
3. The **current** rootfs UBI image has been extracted and saved as the
   rollback artefact, and the current kernelb slot hash recorded:
   ```
   # host: rollback rootfs = the root volume of the running image 22b72852
   tar -xOf openwrt-hisilicon-luofu-cudy_wr3000-v2-squashfs-sysupgrade.bin \
       sysupgrade-cudy_wr3000-v2/root > /tmp/rollback-rootfsb.ubi
   sha256sum /tmp/rollback-rootfsb.ubi   # record it
   ```
   On the box, read the running kernel slot hash with the bench tooling
   (`tools/bench-push-kernel.sh --print-slot` or the documented mtd read) and
   compare with the expected slot hash; if it already equals the image's slot
   hash the kernel needs no write at all.
4. Overlay loss accepted: writing rootfsb destroys `rootfs_data` (SSH host
   keys, uci config, /root scripts). Back up what is needed first:
   `tar -C /overlay -cf /tmp/overlay-backup.tar .` (read-only copy; not a
   substitute for the rootfs rollback artefact).

Procedure (attended, one idea at a time):
1. Boot **stock pair A** (selector a: env `bootflag=a` + cell 0x10, per
   `../../SELECTOR-0x21.md`), obtain root through the documented no-broker
   channel (cudy-backup-root), reach the box over the bench link.
2. Push the new artefacts to /tmp on stock: the extracted kernel slice
   (`sysupgrade-.../kernel`) and the new rootfs UBI
   (`sysupgrade-.../root`), verifying sha256 on the box.
3. Write ONLY the inactive pair-B slots: the kernel slice is UNCHANGED
   (`13e81214...`), so **write only the rootfs** unless the running kernelb
   slot hash proves otherwise: `mtd erase rootfsb` then
   `mtd write /tmp/new-rootfs.ubi rootfsb` (the installed UBI image is the
   ubinize output; do **not** ubiformat it again). Verify the whole written
   range read-back (mtd read hash) against the rootfs UBI sha256
   `62344e80...` before continuing. Never touch pair A, rootfs_data or the env
   blocks.
4. Handover to pair B exactly as the proven 2026-11-25 install did (env
   `bootflag=b` + `boot_reg=0x21`), sync, reboot.
5. On boot: capture UART; expect the GP1 pulse line, the golden handshake page
   (`0x86807FEF`/`7`/`2`/`0xABCD`, state 1, mask 0) only after the attended
   driver sequence; check `ls /usr/sbin/iw /usr/bin/iwinfo
   /lib/modules/6.18.60/cfg80211.ko /lib/modules/6.18.60/hi5622_wifi.ko` and
   their sha256 against `build-evidence/image-verify.txt`.
6. Load by hand (bench policy; no autoload):
   ```
   modprobe cfg80211
   modprobe hi5622_wifi fw_path=<local firmware copy>
   ```
   then the existing attended sequence (prepare/upload/verify/seed/hcc_arm/
   hcc_loop/release 0x5A5A only) and `iw phy`/`iw dev` checks.
7. Stop rules: one module load, no retries; if the image fails to reach
   userspace or SSH, do not fight it - reboot to pair A (stock) and, if needed,
   rewrite rootfsb from the saved rollback UBI.

Rollback (in order of preference):
1. selector back to pair A (stock) - always available, no host artefacts;
2. from stock pair A, rewrite rootfsb with `/tmp/rollback-rootfsb.ubi` and
   hand back to pair B;
3. physical recovery (power-off + RESET counter + TFTP recovery image).

## T2 - kernel-only install

Not needed for this image: the kernel slice is unchanged (`13e81214...`). If a
future rebuild changes it, reuse the proven procedure in
`../wifi-upstream-target/CFG80211-BENCH-IMAGE.md` section 3 (push to the
inactive kernel slot, full-slot hash verify, rollback by rebooting into the
pair recorded by luofu_record_pair).
