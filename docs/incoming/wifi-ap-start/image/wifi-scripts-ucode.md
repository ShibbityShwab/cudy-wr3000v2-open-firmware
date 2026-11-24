# Image requirement: wifi-scripts ucode files (hostapd-mini)

**Bench finding (2026-11-26 real AP fire):** hostapd-mini from the image exits
at startup with

    Error loading ucode: Unable to resolve path for module 'common'

because the image carries hostapd-mini but **not** the OpenWrt
wifi-scripts package, which ships the hostapd ucode modules:

| installed path | source (OpenWrt tree) | package |
| --- | --- | --- |
| /usr/share/hostap/common.uc | package/network/config/wifi-scripts/files/usr/share/hostap/common.uc | wifi-scripts |
| /usr/share/hostap/wdev.uc | .../files/usr/share/hostap/wdev.uc | wifi-scripts |
| /usr/share/hostap/wifi-detect.uc | .../files/usr/share/hostap/wifi-detect.uc | wifi-scripts |

## Canonical fix (image)

Add the OpenWrt main package to the target image (target-skeleton image
config / the bench image config used by the img lane):

    CONFIG_PACKAGE_wifi-scripts=y

That provides the three files above plus /usr/share/ucode/wifi/* and the
netifd wireless scripts.  It is the upstream-correct dependency of any
OpenWrt image that ships hostapd; do not hand-copy the files into
base-files (they would go stale).

## Bench workaround (zero flash)

    opensource/docs/incoming/wifi-ap-start/tools/push-hostapd-ucode.sh
    # then, in the attended session:
    opensource/docs/incoming/wifi-ap-start/tools/push-hostapd-ucode.sh --apply

The script stages the three files from the local OpenWrt source tree and
copies them to /usr/share/hostap/ on the router (bench workaround only).

## Coordination

* img/image lanes: add CONFIG_PACKAGE_wifi-scripts=y to the image config and
  rebuild; that closes the hostapd ucode error without any /tmp workaround.
* wifi-ap-start lane: records the failure, the workaround and the expected
  hostapd flow in ../HOSTAPD-BRINGUP.md.
