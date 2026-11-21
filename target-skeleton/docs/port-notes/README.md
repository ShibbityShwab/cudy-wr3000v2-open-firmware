# Port notes (integration records)

These are the per-lane integration records for the driver series.  Each directory keeps the
SUMMARY that documents the exact commands, evidence hashes, design decisions and honest gaps of
one porting lane after its patches were merged into patches-6.18/ and the shared DTS/config/docs
files.  The patches themselves live in ../../target/linux/hisilicon/patches-6.18/.

- fmc/ - SPI-MEM controller + SPI-NAND (patch 006)
- net/ - MDIO bus + Genius PHY + partial GEMAC (patches 007-009)
- lsw/ - internal LSW switch D1a probe skeleton + binding (patch 016)
- abinstall/ - A/B boot selector interface (017) + sysupgrade write path + mocked test (host-only)
- smp/ - vendor SMP enable-method decode + platsmp draft (patch 018; 2026-11-18)
- clk/ - CRG clock tree (patch 019)
- pie/ - PIE ring-engine draft + GEMAC front end (patch 020)
- lsw-egress/ - LSW W4 egress/probe-configuration stage (patch 025)
- pie-decode/ - PIE RX completion decode + TX descriptor (patch 024)
- mac/ - GEMAC netdev readiness gate (patch 023)
- mac-nvmem/ - bdinfo-MAC nvmem source (patch 026)
- overlay/ - persistent rootfs_data overlay image work
- firstinstall/ - stock-to-image first-install design (host-only)
- pcie/, pinctrl/, thermal/, restart/, image/ - earlier driver/target lanes (patches 004-017)
- smp-decode/ - FINAL SMP diagnosis record (task-62, 2026-11-21): the root cause was the
  loader hand-off devicetree contract (DTS-only fix), the 2-CPU baseline image 974e6ca6 is
  verified, and the SMP bench diagnostics (060/061 and 033-042/059) are excluded from the
  release line.  See ../../../../docs/SMP-RE.md sections 21-24 and ../../RELEASE-NOTES.md.

Nothing here is vendor code or a deliverable image.  The release line (51 target patches + the
loader hand-off DTS fix; bench-only SMP diagnostics excluded) and the short release status are
in ../../../upstream/series-6.18/05-release-line.md and ../../RELEASE-NOTES.md.  Publication of
the parent half is NOT READY.
