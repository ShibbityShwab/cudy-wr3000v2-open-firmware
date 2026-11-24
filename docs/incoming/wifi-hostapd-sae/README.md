# wifi-hostapd-sae (task-148) - SAE-capable hostapd for the bench image

**Status: BUILT + VERIFIED host-side** (two images re-baked; 14/14 functional
config gate; 37/37 image checks).  Nothing was flashed or booted by this lane,
no device contact happened.

## Why this lane exists

The bench image (../wifi-final-image) ships OpenWrt's `hostapd-mini` v2.12,
which is built **without SAE** and rejects the WPA3 fixtures exactly as the
wpa3-sae lane reported:

    Line 44: invalid key_mgmt 'SAE'
    1 errors found in configuration file '.../hostapd-wpa3-sae.conf'

This lane builds an SAE-capable hostapd for the router's OpenWrt tree and
re-bakes the FINAL bench image (rootfs-only; the hardware-validated kernel
slice 13e81214... is byte-identical):

| what | identity |
| --- | --- |
| package | `hostapd-mbedtls-2026.08.07~831364bf-r7.apk` - OpenWrt "full" hostapd variant, **CONFIG_SAE=y + CONFIG_TLS=mbedtls** (libmbedtls21 was already in the image) |
| shipped binary (image) | 1,032,951 B sha256 `451dd49f0e759cd6da842aafc479fae6ad80ee094d498b270b263a9bddfb7469` (old hostapd-mini: 504,333 B `13cb3a43...`, SAE-less) |
| image A (drop-in on task-147) | container 7,188,799 B sha256 `a31bc405d817f7fcb87d0b1e2e8adbc58d6b654cd4c315f62638785209ae1c75` |
| image B (WPA3 bench combination) | container 7,188,799 B sha256 `8944740ebe4c708eca202241ec1fa3e01fb86cebe3778609335632ddd697aaa1` |
| kernel slice (UNCHANGED) | 3,372,638 B sha256 `13e8121445767fc84d3928bbed276b0a0e1229a315ce2c26d65a99f59669ad7f` |

Image A = task-147 final image + SAE hostapd (strict drop-in, module 308,080 B
sha `5b587949...`).  Image B = image A with the **wpa3-sae PMF/IGTK driver
module** (311,108 B sha `08832e75...`; installed stripped 294,168 B sha
`c7124bb9...`) - the recommended combination for the WPA3-SAE bench attempt,
because that module carries the `.set_default_mgmt_key`/IGTK fix the WPA3
AP-start key path needs.

## What is here

| path | contents |
| --- | --- |
| [REPORT.md](REPORT.md) | full report: variant analysis, proofs, verification, image re-bake, limits, driver-side note |
| [BUILD-NOTES.md](BUILD-NOTES.md) | exact build record, commands, tree state, reproducibility, lab-tree residue |
| [HASHES.txt](HASHES.txt) | authoritative artifact/package/image/fixture hashes |
| `configs/` | the three validated fixtures (WPA3-SAE only, WPA2/WPA3 transition, WPA2-hardened PMF) - same files as `payload/` |
| `payload/etc/hi5622/` | fixtures staged into the image rootfs (text only) |
| `config-hostapd-mbedtls.config` | the package/config delta appended before `make defconfig` |
| `tools/build-hostapd-sae-package.sh` | builds `hostapd-mbedtls` in the shared tree, proves the SAE build, records package evidence, restores the tree |
| `tools/bake-image-with-hostapd-sae.sh` | re-bakes the rootfs-only bench image (variant A or B), verifies it, records hashes, restores the tree |
| `tools/verify-image-with-hostapd-sae.sh` | verifies a built image FROM ITS BYTES (kernel unchanged, SAE hostapd, fixtures, module, manifest, no vendor firmware) |
| `tools/validate-wpa3-configs.sh` | runs the **shipped ARM hostapd under qemu-arm** against the three fixtures + negative controls |
| `evidence/` | package build record, image A/B records + verify output, qemu gate output, applied config diff |

## Fast checks (host only, no device)

    ./tools/validate-wpa3-configs.sh \
        --container /home/shibbityshwab/wifi-hostapd-sae-artifacts/A/final-image-with-hostapd-sae-A.bin

    ./tools/verify-image-with-hostapd-sae.sh --variant A \
        --container /home/shibbityshwab/wifi-hostapd-sae-artifacts/A/final-image-with-hostapd-sae-A.bin \
        --driver-installed-sha f07dd505f1d7ac5dd62a826c440414bf2bc1662fe2fecb504dc4cec5b5e0d412 \
        --root-squashfs /home/shibbityshwab/wifi-hostapd-sae-artifacts/A/root.squashfs

    ./tools/build-hostapd-sae-package.sh     # rebuild the package (host, shared tree)
    ./tools/bake-image-with-hostapd-sae.sh --variant A   # re-bake image A
    ./tools/bake-image-with-hostapd-sae.sh --variant B   # re-bake image B (wpa3-sae module)

Expected tails: `validate-wpa3-configs: 14 passed, 0 failed` and
`verify-image-with-hostapd-sae: 37 passed, 0 failed`.

## Functional state (honest)

* SAE **config/parse** side is now functional end-to-end: the binary that ships
  in the image parses `wpa_key_mgmt=SAE` (WPA3-only), `WPA-PSK SAE`
  (transition) and WPA2-hardened `ieee80211w=2` profiles; the old hostapd-mini
  binary refuses SAE with the exact prior failure (negative control).
* AP-start key path: the WPA3 profiles install GTK **and** IGTK + default
  management key.  GTK is hardware-proven on the final module; IGTK needs the
  wpa3-sae driver module's `.set_default_mgmt_key` (image B carries it, the fix
  is hardware-proven for the WPA2-hardened PMF AP-ENABLED milestone).
* A **usable** WPA3 AP (SAE authentication, association, 4-way PTK, data)
  still needs the AP management TX/RX path (wifi-ap-mgmt lane),
  `.add_station`/`.del_station` (sta-ops lane) and the MAC RX/EAPOL data path.
  This lane does not fake that; it removes the userspace parser blocker.

## Policy

Text only in git: the .apk/.arm/.bin/UBI artifacts live under
`/home/shibbityshwab/wifi-hostapd-sae-artifacts/` (never committed).  No
vendor binary/firmware/calibration byte, no credential and no secret is in
this tree; the fixtures carry only the project's throw-away placeholder
passphrase.  The vendor PD4 firmware and the Wi-Fi custom block remain
manual, local pushes (see ../wifi-final-image/payload/etc/hi5622/FIRMWARE-POLICY.txt).
