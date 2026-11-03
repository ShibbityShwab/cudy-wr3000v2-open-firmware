# Phase 53 — the FMC write path is proven

Date: 2026-11-03.

## The result

The luofu FMC driver now programs this board's SPI-NAND. Verified on hardware, read back
from the vendor kernel:

| path | evidence |
| --- | --- |
| direct page program | mtd9 page 0 = `LUFU` + snapshot magic `0xC0DE1055`; mtd16 pages 64/68/128/192 = `LUFU*` |
| mtd write hook (master mtd) | mtd16 pages 70/71 = `MTD1`/`MTD2`, fired twice |
| multi-page, consecutive | the same fires (four direct programs in one boot; two through the hook) |
| partial/short write (RMW) | mtd16 page 72 = 12 non-ff bytes, `CLN1` + 8 — the JFFS2 cleanmarker shape |

The boot’s own evidence moved with it: `C8C = 0x80604000` (the UBI init ran), `C9C = 0x015EB70F`
(the userspace exec marker), and the box returned at t=90 s instead of t=290 s.

## The traps retired on the way (each measured)

- **Pages 163–166 and 66/67 are bad pages.** The same block’s page 0 programs while its p35 stays
  `ff`; the direct write to p66 and p68 lands, the mtd hook to p70/71 lands. Every earlier
  “the program never lands” conclusion was measured through a bad page.
- **The write-protect bit.** `GLOBAL_CFG` bit 6 was being **set** by the probe while the vendor’s
  `hi_spi_nand_hw_init` calls `hi_fmc_wp_en_set(0)`; the comment said “clear” and the code did the
  opposite.
- **The spec pack.** `luofu_fmc_write_page` computed the spec-derived `OP_CFG` and then
  overwrote it with the transient `0x9A0`; the spec pack with `cs=1` is `0x9D0`, the live program
  value.
- **The partition priv.** An mtd partition does not inherit `master->priv`; calling the hook with
  the partition mtd dereferences NULL. The real JFFS2/`dd` path always passes the **master**.
- **The 4 KB short read.** Read-modify-write for partial pages is what JFFS2’s 12-byte cleanmarker
  needs; the old `len % writesize -> -EINVAL` gate refused it.
- **The logged race.** The 12 s log snapshot’s trylock is released before its flash work; disabling
  it changes nothing in the write results, so it is not the write blocker (kept disabled meanwhile).

## The overlay’s remaining gate

The partition registration chain is locally green: the DTB’s `fixed-partitions`, the ofpart
(`mtd->dev.of_node` is wired), `CONFIG_MTD_OF_PARTS`, `CONFIG_MTD_BLOCK`, `CONFIG_DEVTMPFS`.
`rootfs_data` therefore exists, and the record’s older “no `/proc/mtd` rootfs_data” is stale.

The mount decision is `mtd_volume_identify`’s **4-byte read at offset 0** (`__be16_to_cpu(...) ==
0x1985`). With `rootfs_data->oobsize = 0` now set unconditionally, JFFS2 takes its in-band
early-out; the remaining measurement is those four bytes read **under our kernel** (where the
partition is not mounted, so the read is real — a raw read of a mounted partition returns `ff` on
this board and is void).

## Driver state

`lab/luofu-kernel/nand/luofu/luofu-fmc.c` carries the proven write path plus the current probe
writes used to measure it. The probe writes are test instruments, not production behaviour; the
next change strips them while keeping the same functions.

## Related notes

The dated evidence lives in the session memory ledger (`notes/facts/2026-11-03-*`): the write-path
proof, the bad-page discovery, the vendor-init WP finding, the master-mtd priv bug, the RMW proof.
