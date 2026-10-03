# Can the rootfs simply be swapped? No - and the reason defines the deliverable (phase 26c, 2026-10-03)

Phases 26a/26b established that a **newer** OpenWrt userland (24.10) runs on the vendor kernel. The obvious
next move is "so replace the rootfs". Reading the running system says **no**, and the reason sharpens what
the deliverable actually is.

## What the vendor's rootfs carries that upstream does not

**Vendor binaries** in `/usr/sbin`: `hipriv`, `hi_app`, `hi_appenv`, `hi_appm`, `hi_cfm`, `hi_getenv`,
`hi_ipc`, `hi_md`, `hi_mw`, `hi_printenv`, `hi_setenv` - the HiSilicon app framework (privileged ops,
config manager, app manager, IPC).

**Vendor init scripts**: `hsan_appm`, `hsan_easymesh`, `hsan_prepare`, `hsan_start`, `cmagent`, `cmsd`,
`hcsh`, `softapd`, `umd`, `automesh` - the app layer that starts and supervises them.

**Wi-Fi calibration**: `Hi5622V200_cal_init.sh`, `Hi5622V200_cal_save.sh`, `hi5620v100_cal_get.sh`,
`hi5622v100_cal_init.sh`, `hi5622v100_cal_get.sh` - the per-unit radio calibration flow, which this
project's own safety rules already treat as precious (`MANIFEST.sha256` before any restore).

**Vendor ubus services**, queried live - and this is the part that decides it:

```
cmagent.router
hi_hi_board_info_get_call
hi_hi_sysinfo_data_get_call
hi_hi_encrypt_rsa_de_call
hi_hi_upgrade_block_start_call   hi_hi_upgrade_block_stop_call   hi_hi_upgrade_block_write_call
hi_hi_upgrade_cmd_call           hi_hi_upgrade_dump_call         hi_hi_upgrade_flag_get_call
hi_hi_upgrade_flag_set_call      hi_hi_upgrade_image_check_file_call   hi_hi_upg_get_img_version_call
```

The firmware **upgrade mechanism is a family of vendor ubus calls**. A stock OpenWrt rootfs does not have
them, so replacing the rootfs **removes the device's own ability to flash** - on a board whose recovery
path already depends on a working bootloader and UART.

## What is NOT vendor: the Wi-Fi plumbing

The same read shows the opposite for the radios: **there is no vendor Wi-Fi userland at all.** A search of
`/lib/wifi`, `/sbin/wifi`, `/usr/sbin` and `/etc/init.d` for `hsanwifi`/`hi5622` matches **nothing**.
`/etc/config/wireless` is ordinary mac80211 (`option type 'mac80211'`), `/lib/wifi/mac80211.sh` is the
stock detection script, and `hostapd`/`wpa_supplicant` are the stock `wpad`. The vendor's Wi-Fi reaches
the userland through **mac80211/cfg80211 in the kernel modules** - which is exactly what phase 25's named
tables showed when the wal layer turned out to front cfg80211.

## So the shape of the deliverable, now grounded in both halves

| half | replaceable? | why |
| --- | --- | --- |
| Wi-Fi plumbing (mac80211.sh, wpad, /sbin/wifi) | **yes** - it is stock OpenWrt | no vendor code in it |
| generic userland (busybox, dropbear, libs, tools) | **yes** - 24.10 demonstrated running | phase 26a/26b |
| vendor app layer (hi_*, hsan_*, cmagent, cmds) | **no, not wholesale** | carries the config manager, app supervision and the **upgrade mechanism** |
| Wi-Fi calibration scripts | **no** | per-unit radio calibration; the project's own safety rules protect it |
| kernel + Wi-Fi modules | **no** | no stable module ABI; source not public (GPL request) |

**Therefore: the viable custom firmware is an UPGRADE-IN-PLACE of the vendor rootfs, not a replacement of
it** - keep the vendor base (app layer, calibration, upgrade path), and bring in newer upstream components
where they do not collide. That is what the deployed `omo-minimal-0.3` already does in miniature, and it is
the shape any larger build should take.

## What would change this answer

The vendor source (G004). With the `hi_*` app framework and the target patches, a from-source build could
reproduce the app layer and the upgrade path, and then a genuine wholesale replacement becomes possible.
Until then, "replace the rootfs" would trade the Wi-Fi and the ability to flash for a newer busybox.

