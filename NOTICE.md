# NOTICE

## What is and is not in this repository

Included: our own scripts, build recipes, documentation and reverse-engineering notes for the Cudy
WR3000 V2.0 (`WR3000V2-R116`).

Deliberately **excluded**:

- vendor firmware images and rootfs blobs (Cudy's or anyone else's),
- vendor kernel modules, the Wi-Fi firmware blob (`FIRMWARE.bin`) and calibration files,
- any credentials, keys, tokens, or device-unique identifiers.

The build recipe is written so that you produce the firmware locally from **your own** device's dumps.
That is deliberate: the vendor's binaries are not ours to redistribute, and the recipes do not need
them in the repository.

## GPL note

The device ships an OpenWrt 22.03.6 based system with a Linux 5.10.201 kernel. The vendor Wi-Fi module
`hi5622v100_wifi.ko` carries `license=GPL` in its metadata, while no source is published by the vendor.
If you need those sources, request them from the vendor under GPLv2 section 3 - a written request
naming the model, the firmware versions, and the platform identifiers is usually enough to start the
process.

## Radio regulation

Any experimentation with the power tables, calibration values or `txpower` described here must stay
inside the regulatory limits of the country you are operating in. The documentation notes where the
limits live; it does not tell you to exceed them.
