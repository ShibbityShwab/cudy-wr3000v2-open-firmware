
# Loader / recovery / upgrade formats - SUMMARY (HiSilicon luofu, Cudy WR3000 v2.0)

Status: host-only, read-only RE (2026-11-21). Full detail and disassembly references are in
[`REPORT.md`](REPORT.md). No device was touched; no vendor binary or full firmware blob is
committed.

## What was reversed

- **Boot chain**: BootROM -> esbc (mtd0, two copies) -> U-Boot 2022.07 -> kernel. esbc is
  linked at 0x80010000 (Thumb-2), validates only the `hboot1tag` magic and the
  region-size/code-length fields of the U-Boot header, loads U-Boot to 0x81000000 and jumps.
  No crypto/hash code exists in esbc in this build. [V]/[I]
- **`hboot1tag` header (0x100 B)**: magic, version=1, SHA-256 of the U-Boot code (verified),
  code offset 0x100, code length, an unexplained constant 0x3CB0 at +0x60, region size
  0x100000. `OEM_BOOT %08u` marker at 0x7FC00: 5 in the 2.4.15/recovery image, 7 in the
  current loader/2.5.24 image. [V]
- **Vendor recovery/sysupgrade container** (recovery.bin is byte-identical to the 2.4.15
  web sysupgrade image): 512 KiB U-Boot section -> 256-byte RSA signature at 0x80000 ->
  0x700-byte unsigned hole -> FIT at 0x80800 whose `/images/ubi` data property is a
  HiSilicon `hi_upgrade` package (header 0x5B8 + kernel slice + rootfs UBI + 0x180 trailer)
  -> fwtool JSON + `FWx0` trailer both inside the FIT data tail and appended after the FIT.
  Exact offsets/sizes/CRCs for 2.4.15 and 2.5.24 are tabulated in the report. [V]
- **RSA gate (recovery path)**: RSA-2048 PKCS#1 v1.5 SHA-1 over
  `image[0,0x80000) || image[0x80800,EOF)`, signature at 0x80000, exponent 65537. Scheme,
  key struct and PKCS#1 block were extracted from U-Boot and the signature reproduced in
  Python; both vendor images verify; the same public key is in `oem-check`. Digest 2.4.15
  `5d11a3e3...`, 2.5.24 `897fcb9e...`; modulus sha256 `f3e22478...`. [V]
- **TFTP recovery semantics**: RESET-button poll (2 s) sets a flag + `ledblink 24 500`;
  `multiupg detect`/bootdelay; recovery decision prints "recovery button press"; loop
  TFTPs `recovery.bin` from 192.168.1.88 to 0x83200000, checks FIT description "R116",
  RSA-verifies, then `imxtract ubi` + `hi_upgrade`; writes **kernela AND kernelb**,
  **rootfsa AND rootfsb** with the same image and **erases rootfs_data**, then resets.
  Never writes U-Boot/esbc/env/bdinfo/fac/cfg/log/pstore or the selector cell. [V]
- **`bootfip` handoff**: gate word 0xd7b7ab1e at base-DTB+totalsize (4-byte alignment minimum or the
  32-bit load data-aborts; the project pads totalsize to 8 bytes); descriptor/overlay match rule as in
  TRAILER-FORMAT; after overlay apply it stages an ATAG list at
  `0x80600000+atag-offset(0x1000)`, including custom flash-info ATAG 0x5441000a, and enters
  the kernel with r0=0, r1=machid (env or default), r2=patched DTB. [V]
- **Selector semantics re-confirmed**: env `bootflag` selects the pair; SYSCTRL
  `0x10100C08` is the failure record (bootfip writes the *opposite* pair, low-byte RMW); a
  boot-time userspace writer (`boot_done` / `luofu_record_pair`) is required for stability;
  0x21 is the project's bench value only; true POR clears the cell. [V]
- **Vendor verification paths**: stock `platform_check_image` -> `oem-check` (format/name/
  header+data CRC in the uImage fallback, same RSA key) -> `image_demux` (dd split at
  ubootver-offset 512 KiB, FIT at 0x80800, `fdt_dumpimage` extracts "ubi") ->
  `platform_do_upgrade_uboot` (OEM_BOOT version gate, writes U-Boot only if newer) ->
  `platform_do_mtd_upgrade` (hi_upgrade header fields at 248/324, writes inactive pair,
  sets sysenv cell 0x10/0x11). Web path = `libhi_upgrade.so` with the same container/key
  (strings only; full order not decoded). [V]/[I]

## Answers to the task questions

1. **Bootcmd/FIP flow, handoff, selector**: decoded; the FIP/DTB contract incl. gate,
   alignment, descriptor/overlay match and ATAG/flashinfo staging is in REPORT section 6;
   selector semantics and the pair recorder are in REPORT section 5 (re-confirmed).
2. **recovery.bin format, RSA gate, writes, custom recovery**: decoded; RSA gate is
   effective; recovery writes both pairs + rootfs_data; **a custom recovery image is not
   possible** (vendor private key required). A custom *U-Boot* written from stock root is
   [I]/[U] (esbc has no crypto) but high risk and not recommended.
3. **Vendor sysupgrade format + stock web/serial verification**: decoded structurally
   (top-level map, FIT, hi_upgrade header/layers, fwtool trailers) and behaviourally
   (oem-check/image_demux/platform.sh); the web path's exact check order remains [U].
4. **Robust install/recovery needs**: never write both slots or U-Boot; keep the stock
   signed recovery image pinned; env re-stamp through the loader mismatch path (or
   carefully via raw env blocks); per-slot FIP slice must be complete and at least 4-byte aligned (project pads to 8);
   `platform_do_upgrade` must keep its current guarantees (inactive pair, selector last,
   no env/rootfs_data writes, two-witness check) and its recorder dependency must be
   validated on hardware; recovery fallback = physical RESET+TFTP stock image -> telnet
   installer. Details in REPORT section 11.

## Compatibility (why stock cannot install our image and vice versa)

| | vendor | project |
| --- | --- | --- |
| container | hboot1tag + RSA + FIT + hi_upgrade | OpenWrt ustar tar (CONTROL/kernel/root/manifest) |
| check | vendor RSA (U-Boot, oem-check, web) | project byte-level `platform_check_image` (refuses vendor images) |
| install | stock inactive-pair writer + cell 0x10/0x11 | project inactive-pair writer + selector last |
| signed | yes (vendor key) | no (structural checks + sha256 manifest) |

The install vehicle is the stock userspace telnet installer; the recovery door is the stock
signed TFTP image. UART TX is dead, so no U-Boot-console path exists.

## Highest-value gaps

- `hboot1tag +0x60 = 0x3CB0` meaning; esbc main/backup OOB record format (U1/U2).
- 384-byte high-entropy trailer after the rootfs inside the hi_upgrade package (U3).
- fwtool `FWx0` trailer +4 word / 8-byte chunk header semantics (U4).
- Consumer of the U-Boot code SHA-256; the `ubootb` 0xA0000 blob; eFuse secure-boot state
  (U5/U6/U8).
- Web `libhi_upgrade.so` exact check order (U9).
- Hardware rehearsal: A/B install with deliberate new-pair failure + stock-recovery
  rehearsal (REPORT section 11.3/11.4).

## Tooling added

`decode-vendor-image.py` in this directory: read-only parser/classifier for the vendor
container (outer header, SHA-256 check, FIT walk, hi_upgrade header/CRC, layers, uImage+DTB+
gate, fwtool metadata/trailers, RSA verification against a supplied U-Boot image). It embeds
no vendor bytes; keys are extracted from the user-supplied U-Boot dump. Existing
`fip-slice.py` / `luofu-image.py` remain the canonical FIP/container checks.
