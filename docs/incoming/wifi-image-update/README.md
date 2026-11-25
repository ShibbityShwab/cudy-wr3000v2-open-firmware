# wifi-image-update - image #2 + the regdomain + 802.11ax hostapd (host-built, NOT flashed)

Status: 2026-11-27. A rootfs-only update of the verified image #2 carrying the two
bench-proven fixes: (1) the 5 GHz regdomain fix (the cfg80211 REQUIRE_SIGNED_REGDB off ->
country US, 5 GHz IR) and (2) the 802.11ax-capable hostapd (hostapd-mbedtls full variant
rebuilt with CONFIG_DRIVER_11AX_SUPPORT -> the HE AP on air with the HE IEs).

## Artifacts

| file | size | sha256 |
| --- | --- | --- |
| root.squashfs | 3,997,696 B | b461df0c... (the patched rootfs) |
| rootfs.ubi | 4,456,448 B | (the new UBI, 34 PEBs; the original 29) |

The kernel slice is UNCHANGED (3,372,638 B 13e81214... / slot 1794a531...).

## What changed vs image #2

- /lib/modules/6.18.60/cfg80211.ko: 406,028 B -> 2,317,952 B (the regdomain fix,
  docs/incoming/wifi-regdom-fix/README.md)
- /usr/sbin/hostapd: 1,032,951 B -> 1,618,388 B (the ax build; the image's hostapd-mbedtls
  full variant + CONFIG_DRIVER_11AX_SUPPORT; the HE AP proof in the ledger 2026-11-27 04:00)
- everything else byte-identical to image #2 (the unsquashfs -> the two replacements -> the
  mksquashfs with the exact image options: -comp xz -b 262144 -Xbcj arm -noappend).

## Update procedure (rootfs-only flash, the runbook P2 shape)

1. The recovery armed + the stock pair A switch (the proven tools/bench-install path).
2. The stock root channel -> `ubiformat /dev/mtd14 -y -f rootfs.ubi` (the rootfsb only;
   the kernel slot is untouched).
3. The handover (the env bootflag + the selector, the proven sequence) -> boot our image.
4. The post-boot receipts: `iw reg get` -> country US + the 5 GHz IR; `hostapd -v`;
   `ieee80211ax` accepted; the HE AP; the page golden; the modules.

## Verification done host-side

- the new squashfs superblock: xz, 262144, the ARM BCJ filter (the image's exact options)
- the extracted vol0 from the new UBI = the new squashfs byte-identical (b461df0c...)
- the device nodes preserved (/dev/console crw)
- the mtd14 capacity: the earlier feasibility check (20.6 MB free) > the new 4.46 MB UBI

## Not done (the Lead)

- the flash (rootfs-only) + the post-boot receipts; nothing was written to the box by this
  packaging.