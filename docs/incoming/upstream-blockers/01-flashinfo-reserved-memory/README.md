# Blocker 2 - upstream binding for the loader hand-off window (CLOSED)

**STATUS: ADOPTED after hardware boot-test image 6e9c54d0 (2026-11-21).  No
upstream blockers remain.**  The target DTS now uses
`compatible = "hisilicon,luofu-flashinfo", "hsan,flashinfo_reserved"` and the
stock loader accepted the documented first compatible in the boot-test;
the drafted reserved-memory child binding stays as the maintainer-facing
documentation.

## Measured contract (SMP-RE.md section 24)

The stock `bootfip` refuses to stage the kernel unless the base DTB carries:

1. `/aliases/console` - resolved to the console UART node;
2. `/reserved-memory/flashinfo@80600000` - measured vendor form
   `compatible = "hsan,flashinfo_reserved"`, `reg = <0x80600000 0x4000>`,
   `atag-offset = <0x1000>`; the loader computes
   `atag_addr = 0x80601000`.

## Boot-test PASS (image 6e9c54d0)

The image was built from the current adopted DTS with only the compatible list
changed, flashed over the broker channel to pair b (mtd write kernelb,
ubiformat mtd14, `fw_setenv bootflag=b` verified, selector boot_reg 0x11
verified, FIRE_OK) and booted:

- U-Boot logged `bootflag b bootreg 11` and `Image Name: Linux-6.18.60`;
- `bootfip` staging lines appear for the new first compatible:
  `alias console=/serial@1010f000`, `serialnr->high=0x8250`,
  `serialnr->low=0x1010f000`, `atag_addr=0x80601000` (stock/vendor boot
  prints `alias console=/uart1@...` - the same device);
- booted 6.18.60, 2 CPUs, `GIC-0 29 Level twd`, cmdline
  `ubi.mtd=rootfsb` + full `luofu_switch` marker;
  `/proc/device-tree/reserved-memory/flashinfo@80600000/compatible` =
  `hisilicon,luofu-flashinfo hsan,flashinfo_reserved`; selector 0x11, no
  Oops/BUG/panic/RCU stall.  Evidence:
  `build/evidence-bench-gates/flashinfo-loader/`.  Adopted in commit
  `71b2167`.

## Final artefacts

- proposed binding: `hisilicon,luofu-flashinfo.yaml` (reserved-memory child;
  accepts the documented string, the vendor string, and the pair in the adopted
  order);
- DTS compatible-list delta: `dts-flashinfo-compatible.diff` (adopted);
- current DTS 681 lines sha256 `381b2e8c...`; DTB 12,678 B sha256
  `62574b11...` (pad 12,680 B sha256 `8d79794e...`); DTS-0001 sha256
  `886f3ad0...` (694 lines); release-line validator expects the new
  compatible pair and PASSes.

## Remaining work (none for the blocker)

- The binding still needs upstream maintainer review before posting; that is
  the normal RFC path, not a blocker item.
- Fallback if a maintainer requires vendor-only: keep the vendor string
  first/only and register `hsan` in vendor-prefixes.yaml (no hardware
  change; the loader already accepted both strings).
