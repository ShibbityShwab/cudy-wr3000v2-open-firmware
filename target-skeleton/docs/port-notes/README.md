# Port notes (integration records)

These are the per-lane integration records for the driver series. Each directory keeps the
SUMMARY that documents the exact commands, evidence hashes, design decisions and honest gaps of
one porting lane after its patches were merged into patches-6.18/ and the shared DTS/config/docs
files. The patches themselves live in ../../target/linux/hisilicon/patches-6.18/.

- fmc/ - SPI-MEM controller + SPI-NAND (patch 006)
- net/ - MDIO bus + Genius PHY + partial GEMAC (patches 007-009)
- lsw/ - internal LSW switch D1a probe skeleton + binding (patch 016)
- abinstall/ - A/B boot selector interface (017) + sysupgrade write path + mocked test (host-only)

Nothing here is vendor code or a deliverable image; publication of the parent half is NOT READY.
