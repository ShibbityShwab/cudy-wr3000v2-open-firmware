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

### Publication status (2026-11-17)

This tree is **not ready to publish** yet. See `docs/PUBLISH-READINESS.md` for the full
checklist. Two facts to keep straight:

- Ten vendor `.o` files from a TR6560 BSP build were tracked under `lab/luofu-kernel/nand/`
  until 2026-11-17; they are removed from the current tree but **remain in the published
  history** until it is purged. Do not treat the "no vendor binaries" statement above as true
  for history before that purge.
- Licensing is split: scripts/documentation are MIT (`LICENSE`), kernel code under `lab/` is
  GPL-2.0. A GPL-2.0 license text and per-file copyright notices are still to be added. The
  drivers were derived by reverse-engineering the vendor's GPL binaries, so the submission must
  state GPL-2.0 provenance rather than "clean-room".

## GPL note

The device ships an OpenWrt 22.03.6 based system with a Linux 5.10.201 kernel. The vendor Wi-Fi module
`hi5622v100_wifi.ko` carries `license=GPL` in its metadata (all 58 vendor `hi_*` modules do), while
no source is published by the vendor. The GPLv2 section 3 source request was sent 2026-10-01 and no
reply is held (`docs/GPL-REQUEST.md`); the Jeton same-SoC letter is still a draft
(`JETON-GPL-EMAIL.txt` in the parent repo). If you need those sources, re-send the request and keep
the sent copy + receipt in this tree.

## Radio regulation

Any experimentation with the power tables, calibration values or `txpower` described here must stay
inside the regulatory limits of the country you are operating in. The documentation notes where the
limits live; it does not tell you to exceed them.
