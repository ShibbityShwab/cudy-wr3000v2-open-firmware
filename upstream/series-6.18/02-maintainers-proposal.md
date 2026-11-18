# MAINTAINERS proposal - HiSilicon luofu (Hi5671Y) platform

Status: proposal for the luofu 6.18 series.  A real submission needs this
as its own small patch, either as patch 17/18 of the platform series or as
the first patch of the first driver series (MAINTAINERS get_maintainer
lookups must find the new files).

## Maintainer identity

The repository git configuration identity is:

    ShibbityShwab <18537102+ShibbityShwab@users.noreply.github.com>

This is the identity already used in the 13 new devicetree binding files
(`maintainers:` fields) and in the Signed-off-by lines of the commit-message
drafts.  It is a **placeholder for DCO purposes**: the Linux kernel requires
a real legal name and a sign-off the sender can legally give, so replace it
in all 13 YAML files and in every Signed-off-by before sending.  A second
maintainer (or a reviewer who becomes a co-maintainer) should be added if
one volunteers; the entry is intentionally single-maintainer for now.

## Proposed entry (recommended variant)

Add one platform entry to MAINTAINERS.  The file patterns cover every file
added by this series, including the files of the RFC/draft drivers, so that
a later merge of those drivers does not need a second MAINTAINERS patch:

    HISILICON LUOFU (Hi5671Y) ARM SOC
    M:	ShibbityShwab <18537102+ShibbityShwab@users.noreply.github.com>
    L:	linux-arm-kernel@lists.infradead.org (moderated for non-subscribers)
    L:	linux-kernel@vger.kernel.org
    S:	Maintained
    F:	arch/arm/mach-hisilicon/
    F:	arch/arm/boot/dts/hisilicon/luofu-*
    F:	Documentation/devicetree/bindings/clock/hisilicon,luofu-crg.yaml
    F:	Documentation/devicetree/bindings/watchdog/hisilicon,luofu-wdt.yaml
    F:	Documentation/devicetree/bindings/mtd/hisilicon,luofu-fmc.yaml
    F:	Documentation/devicetree/bindings/net/hisilicon,luofu-gmac.yaml
    F:	Documentation/devicetree/bindings/net/hisilicon,luofu-mdio.yaml
    F:	Documentation/devicetree/bindings/net/hisilicon,luofu-pie.yaml
    F:	Documentation/devicetree/bindings/net/hisilicon,luofu-switch.yaml
    F:	Documentation/devicetree/bindings/pci/hisilicon,luofu-pcie.yaml
    F:	Documentation/devicetree/bindings/pinctrl/hisilicon,luofu-pinctrl.yaml
    F:	Documentation/devicetree/bindings/power/reset/hisilicon,luofu-reboot.yaml
    F:	Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-boot-selector.yaml
    F:	Documentation/devicetree/bindings/soc/hisilicon/hisilicon,luofu-sysctrl.yaml
    F:	Documentation/devicetree/bindings/thermal/hisilicon,luofu-tvsensor.yaml
    F:	Documentation/ABI/testing/sysfs-driver-luofu-selector
    F:	include/dt-bindings/clock/hisilicon,luofu-crg.h
    F:	drivers/clk/hisilicon/crg-luofu.c
    F:	drivers/watchdog/luofu-wdt.c
    F:	drivers/spi/spi-luofu-fmc.c
    F:	drivers/net/mdio/mdio-luofu.c
    F:	drivers/net/phy/luofu-phy.c
    F:	drivers/net/ethernet/hisilicon/luofu-gmac.c
    F:	drivers/net/ethernet/hisilicon/luofu-pie.c
    F:	drivers/net/ethernet/hisilicon/luofu-switch.c
    F:	drivers/pinctrl/hisilicon/
    F:	drivers/thermal/luofu_tvsensor.c
    F:	drivers/pci/controller/dwc/pcie-luofu.c
    F:	drivers/power/reset/luofu-reboot.c
    F:	drivers/soc/hisilicon/luofu-selector.c

Notes on the variant:

- `S: Maintained` is the honest status while there is no hardware
  validation and the maintainer is a volunteer: it says someone is
  responsible for the code, not that a vendor supports it.  `S: Supported`
  is appropriate only if the maintainer commits to user support; that is a
  decision for the submitter.  There is **no MAINTAINERS status value
  "RFC"** - `RFC` is the series/cover-letter marker for patches that are
  not mergeable yet (see 01-commit-messages.md status column).  Do not
  write `S: RFC` in MAINTAINERS.
- `F: arch/arm/boot/dts/hisilicon/luofu-*` covers the future board DTS
  patch; it does not exist in this series.  Do not use
  `F: arch/arm/boot/dts/hisilicon/` without the prefix: it would claim the
  existing hi3xxx/hiP0x boards.
- The 13 binding files each carry the same `maintainers:` identity; keep the
  YAML and MAINTAINERS entries in sync.  The ABI document has no
  maintainer field, only the MAINTAINERS entry covers it.
- If reviewers prefer the usual split for a vendor platform, the same
  files can be covered by the subsystem entries instead (clk/reset,
  watchdog, spi, netdev, pinctrl, thermal, PCI, power/reset, soc); but a
  new platform with a new machine directory still needs the SoC entry above
  for the mach and board files.  The single-entry version is proposed for
  review simplicity.
- Add the entry with a plain `git format-patch` patch and no file changes
  in the same patch.  `scripts/get_maintainer.pl` on each new file should
  then return the maintainer; test it in a scratch tree before sending.

## Optional extra patterns

If the series is split by subsystem, these narrower additions are useful
for the driver maintainers (they do not replace the SoC entry):

    F:	drivers/pinctrl/hisilicon/

is the only new directory that has no existing directory owner, so it is
included above.  Everything else is an existing directory with existing
maintainers that the subsystem trees will route by their own entries.

## Status used by this series (S/RFC mapping)

| series state | MAINTAINERS action |
| --- | --- |
| ready-for-review patches (0004, 0005, 0007, 0008, 0012, 0014) | add the SoC entry (this file) in the same submission window; `S: Maintained` |
| RFC patches (0001-0003, 0006, 0011, 0013, 0017) | same entry when posted; the RFC marker goes in the subject, not in MAINTAINERS |
| draft/refusing patches (0009, 0010, 0015, 0016) | do not post as merge candidates; if they are posted at all, use `[RFC PATCH]` and still reference the same entry (files are already covered) |

The identity must be updated in all 13 YAML files at the same time as this
entry; see `grep -rl 18537102+ShibbityShwab@users.noreply.github.com
Documentation/devicetree/bindings/`.
