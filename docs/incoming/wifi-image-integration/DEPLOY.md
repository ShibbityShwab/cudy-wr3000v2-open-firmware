# DEPLOY - installing the FINAL self-contained bench image (attended)

**Lane:** img-integ (task-112/task-132). Host-only lane: **none of these steps
was executed on the router by this lane.** They are the handoff to the Lead for
an attended session (human at the bench, recovery door armed, UART capture
read-only - see BENCH-FACTS.md / RUNBOOK.md).

Read together with:
* `../wifi-abi-image/FLASH-RUNBOOK.md` - the proven stock-assisted flash
  procedure that installed the ABI image on 2026-11-27 (same transport);
* `../cudy-backup-root/REPORT.md` + RUNBOOK section 6 - the no-broker root
  channel on stock;
* `../../SELECTOR-0x21.md` - A/B selector rules; and the project hard rules
  (never write the active pair, never write CA 0x400392F0, no rmmod after
  release, never write 0xCECE during boot).

## What this image changes

| item | final image (this lane) | running ABI image `df6a78c3` |
| --- | --- | --- |
| container | **6,922,559 B sha256 `ae5e28bf...`** | 6,533,439 B sha256 `df6a78c3...` |
| kernel slice | **UNCHANGED** 3,372,638 B sha256 `13e81214...` (slot `1794a531...`) | same |
| rootfs UBI | **3,538,944 B sha256 `ff9f3e1c...`** | 3,145,728 B sha256 `62344e80...` |
| userspace | + `hostapd-mini`/`hostapd-common` v2.12, `wifi-scripts` ucode + ucode `iwinfo`, `ucode-mod-nl80211/rtnl` | iw/iwinfo(C)/regdb, no hostapd/ucode |
| modules | `cfg80211.ko` + **combined dp-air + AP-start v7** `hi5622_wifi.ko` (242,572 B sha `44c6cc12...`, installed stripped 231,132 B sha `51608fd5...`) | `hi5622_wifi.ko` from the wifi-cfg80211 snapshot |

All hashes: `build-evidence/build-summary.txt` + `build-evidence/image-verify.txt`.
Local convenience copy: `/home/shibbityshwab/wifi-image-integration-artifacts/`
(final container, kernel slice, rootfs UBI, squashfs, the nine .apk files,
`SHA256SUMS`).

**This is a ROOTFS-ONLY change.** The kernel slice is byte-identical to the one
already on flash (`kernelb`, slot `1794a531...`), so the kernel slot needs no
write. The image is still BENCH-ONLY (container carries the NOT FLASHABLE
marker; install path never run end-to-end).

## Is a rootfs-only flash possible via the stock-telnet-install path?

**The transport supports it; the current tool does not expose it.**

* `tools/stock-telnet-install.sh` (root repo) is the proven stock-assisted
  installer: boot STOCK on pair A, then over its root telnet shell it wgets the
  container members over HTTP and writes pair b:
  `mtd erase + mtd write /dev/mtd12 (kernelb)` (full-slot hash read-back vs
  `1794a531...`) and `ubiformat /dev/mtd14 (rootfsb)`, then sets the U-Boot env
  bootflag and the selector cell, reboots and verifies our image.
  **It always writes BOTH members** - there is no `--rootfs-only` flag.
* Using it AS-IS for this image is SAFE (the kernel write is the same
  `13e81214...` bytes, erase+write+full-slot verify; pair-A/stock untouched),
  but it is an unnecessary kernel-slot erase/write.
* A strict rootfs-only flash is possible with the same transport in two ways:
  **(a)** small extension of the root-repo tool (`--rootfs-only`: skip the
  `mtd erase/write mtd12` block, keep the rootfs + handover + verification
  steps; ~10 lines, outside this lane's write scope), or **(b)** the manual
  sequence below from the stock pair-A telnet shell.

## Preconditions (attended)

1. Human at the bench; recovery card READY (power-off + RESET counter + TFTP,
   `tools/recovery-card.sh`); UART capture read-only to an absolute path.
2. The box currently runs OUR image on pair B (AP live per the Lead's
   2026-11-27 session). The install transport needs **stock running on pair A
   as the active pair**: switch to stock first (env `bootflag=a` + selector
   cell 0x10; see `../../SELECTOR-0x21.md`; this lane does not script it).
   Do this while the bench session allows a reboot; the current AP state and
   overlay are not needed for the flash.
3. Rollback artefacts saved before writing:
   * round-2/current rootfs UBI (from the ABI image container):
     `tar -xOf <abi-container> sysupgrade-cudy_wr3000-v2/root > /tmp/rollback-rootfsb.ubi`
     sha256 `62344e80...`;
   * the final image's own rootfs UBI (`ff9f3e1c...`) is the re-flash artefact;
   * stock pair A stays bootable throughout (we never write pair A).
4. Overlay loss accepted: the rootfs write wipes `rootfs_data` (SSH host keys,
   uci config, /root scripts). Back up what is needed
   (`tar -C /overlay -cf /tmp/overlay-backup.tar .`) - not a substitute for the
   rootfs rollback artefact.

## A. Stock-telnet install as-is (simplest, writes both pair-b members)

Follow `../wifi-abi-image/FLASH-RUNBOOK.md` exactly, substituting the final
container (or its extracted members) as the image:

    tools/stock-telnet-install.sh --inspect --image <final-container>   # plan only
    tools/stock-telnet-install.sh --apply --attended --recovery-armed \
        --image <final-container>

The tool verifies: upload sha256 of kernel+rootfs members, kernel-slot
read-back `1794a531...`, ubiformat rc=0, active pair unchanged, env
bootflag/selector handover, reboot, cmdline `ubi.mtd=rootfsb` + project marker.
Then run the post-boot receipt below.

## B. Manual ROOTFS-ONLY flash (same transport, kernelb untouched)

From the host, serve the final image's root member over the same HTTP base the
tool uses (default `http://192.168.10.44:8000`):

    tar -xOf bench-image-final-sysupgrade.bin sysupgrade-cudy_wr3000-v2/root \
        > rootfs.ubi
    sha256sum rootfs.ubi       # must be ff9f3e1c83c36a2a9170cdf6373228692b46b7fa0fbb64abbdc70a25b841aaa9

From the STOCK pair-A root telnet shell (recovery armed; pair A active):

    # 1. record the kernel slot we must NOT change (expect 1794a531...)
    dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum
    # 2. fetch + verify the new rootfs UBI
    cd /tmp && wget -q -O /tmp/fi-r.ubi http://192.168.10.44:8000/rootfs.ubi
    sha256sum /tmp/fi-r.ubi     # must match ff9f3e1c...
    # 3. write ROOTFSB ONLY (mtd14), pair A untouched
    ubiformat /dev/mtd14 -y -f /tmp/fi-r.ubi ; echo UBI=$?   # expect UBI=0
    # 4. confirm the kernel slot is still the validated slice
    dd if=/dev/mtd12 bs=131072 count=66 2>/dev/null | sha256sum   # still 1794a531...
    # 5. handover to pair b (proven sequence; see the runbook/handover2.sh)
    ln -sf /bin/fw_printenv /tmp/fw_setenv; /tmp/fw_setenv bootflag b
    fw_printenv bootflag                     # bootflag=b
    echo -n 0x11 > /sys/devices/platform/sysenv/boot_reg
    sync; reboot

Then run the post-boot receipt below. Rollback: boot pair A (stock) and rewrite
`/dev/mtd14` with `/tmp/rollback-rootfsb.ubi` (or the final image again), then
hand back to pair b; kernelb is never touched, so a failed rootfs write cannot
remove our kernel.

## Post-boot receipt (host-only checks over ssh)

    uname -r                                   # 6.18.60
    cat /proc/cmdline                          # ubi.mtd=rootfsb + project marker
    apk query --fields version kernel          # 6.18.60~6f995278...-r1 (ABI pin)
    apk list --installed | grep -E 'hostapd-mini|wifi-scripts|kmod-hi5622|ucode-mod-nl80211|ucode-mod-rtnl'
    ls -l /usr/sbin/hostapd /usr/bin/iwinfo /usr/share/hostap/{common,wdev,wifi-detect}.uc \
          /lib/modules/6.18.60/{cfg80211,hi5622_wifi}.ko
    sha256sum /usr/sbin/hostapd                # 13cb3a43...
    sha256sum /lib/modules/6.18.60/hi5622_wifi.ko   # 51608fd5... (stripped combined module)
    modprobe cfg80211 && modprobe hi5622_wifi fw_path=hi_wifi/FIRMWARE.PD4.bin allow_frames=12 \
        frame_gap_ms=100 h2d_send_budget=4 vap_start_mode=1 vap_start_module=0
    dmesg | tail -30

Then run the single-boot air-TX ladder of
`../wifi-ap-start/combined/README.md` (C2 -> iftype __ap -> hostapd -B
AP-ENABLED -> dp_probe/dp_setup/h2d_txq_addr_sync/dp_tx_frame ->
dp_tx_status expect COMPLETED -> dp_rx_poll). **The combined module has not
been fired yet** (host-built only); one scheduled attempt, stop rules as in the
ladder README.

## Residual local pushes (by design, NOT in the image)

* **PD4-patched vendor firmware** (vendor-derived; publication rule): copied at
  session time, `fw_path=...`. The image never contains it.
* hostapd config fixture (`hostapd-open.conf`), bench scripts and the
  wifi-hostapd-deploy helper: normal local pushes from their lanes.
* T0 (zero-flash `apk add`) is **no longer needed for the package set** on this
  image - iw/iwinfo/regdb/kmods/hostapd/ucode are in the rootfs. T0 remains only
  useful if the Lead wants the new userspace on the CURRENT (ABI) rootfs
  without flashing; the .apk files are in the local artifacts dir.

## Hard rules (unchanged)

Never write the active pair; never write CA `0x400392F0`; no rmmod after a CPU
release; never write `0xCECE` during boot; recovery door armed before any
write; one idea at a time. WiFi bring-up stays attended (the driver sends no
H2D message and performs no device writes on load).
