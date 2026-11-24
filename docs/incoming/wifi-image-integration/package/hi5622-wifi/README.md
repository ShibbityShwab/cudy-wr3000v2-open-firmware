# hi5622-wifi OpenWrt package (bench image)

Installed into an OpenWrt tree by `../../tools/install-into-tree.sh` as
`package/hi5622-wifi/`; it is untracked in the lab tree (as with the interim
`hi5622-pciet-hcc` package from the wifi-openwrt-pkg lane).

* `kmod-hi5622-cfg80211` - installs `$(LINUX_DIR)/net/wireless/cfg80211.ko`
  built by the target kernel with `CONFIG_CFG80211=m`. No build step.
* `kmod-hi5622-wifi` - two modes, picked by what the installer staged:
  * **prebuilt**: `prebuilt/hi5622_wifi.ko` (a local, ABI-validated bench
    artifact; sha256 in `prebuilt/prebuilt-sha256.txt`). Copied as-is, nothing
    is compiled - the exact validated bytes are installed.
  * **source**: no `prebuilt/`, `src/` snapshot built with
    `$(KERNEL_MAKE) M=$(PKG_BUILD_DIR) modules`.

No AUTOLOAD, no init script, no firmware/custom-block bytes. Load by hand in an
attended session (see ../../DEPLOY.md).
