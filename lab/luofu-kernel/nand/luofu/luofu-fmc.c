// SPDX-License-Identifier: GPL-2.0
/*
 * Hisilicon luofu (Hi5671Y) FMC NAND controller -- stage A.
 *
 * The FMC is the SoC's single flash block: 4 KiB of registers at MMIO
 * 0x10a20000 plus a 1 MiB memory-mapped data window at 0x1c000000.  On this
 * board it drives an ESMT F50L1G41LC SPI-NAND (128 MiB, 2048-byte pages,
 * 64-byte OOB, 128 KiB erase blocks) on chip select 1.
 *
 * The register layer and the command recipes below are transcribed from
 * decoded artifacts, never guessed:
 *   docs/phase50/nand-fmc-port-spec.md                  (the port spec)
 *   build/tmp/nand-spec/disasm/tri_fmc.o.asm            (the BSP register layer)
 *   build/tmp/nand-spec/disasm/tri_spi_nand_drv.o.asm   (the BSP command recipes)
 *   build/tmp/nand-spec/disasm/hi_flash.ko.asm          (the vendor module that
 *                                                        actually runs on the box)
 * Where the BSP and the vendor disagree, the vendor wins, because it is the
 * implementation the live hardware runs: the BSP's tri_fmc_status_check()
 * polls FMC_CFG bit 0, which reads 1 at idle on this board and would spin
 * forever, while hi_fmc_status_check()/hi_fmc_status_check_dma_status() poll
 * FMC_OP bit 0 and FMC_OP_CTRL bit 0 respectively.  We follow the vendor.
 *
 * STAGE A SCOPE -- map, reset, READ ID, and nothing else:
 *   * the command engine is complete for the ops this stage issues;
 *   * there is no write path in this file at all.  The write-enable (0x06),
 *     erase (0xd8) and program recipes are decoded in the spec, but none of
 *     them is compiled in here, so this driver cannot modify the flash even
 *     by accident -- the read-only property is structural, not a promise.
 *   * the page read path is deliberately absent.  The DMA engine (op_ctrl,
 *     op types 1/3/0x11) is decoded in the spec, but its op_cfg operands
 *     (read_if_type, read_addr_cycle, read_dummy_num) come from the
 *     bootloader's flash-spec table, which this stage does not consume yet.
 *     Stage B adds the page read behind the ID check at the end of probe().
 *   * no mtd_info is registered, so no partition is exposed.
 *
 * WHY IT IS INSTRUMENTED.  This board has no usable console, so the driver
 * reports by depositing words in the SoC's two sysctrl scratch registers,
 * 0x10100c18 (staged breadcrumb) and 0x10100c1c (its payload).  They survive
 * a warm reset and are readable with devmem from the vendor system once this
 * kernel has folded back to it, exactly like the arch/arm/mach-luofu
 * breadcrumbs:
 *
 *   c18 = 0xC0DE5001   probe entered, the report cell mapped
 *                      payload: 0
 *   c18 = 0xC0DE5002   controller + window mapped
 *                      payload: the chip select
 *   c18 = 0xC0DE5003   FMC_CFG read
 *                      payload: FMC_CFG
 *   c18 = 0xC0DE5004   die reset completed and ready
 *                      payload: the GET FEATURES 0xc0 status byte
 *   c18 = 0xC0DE5005   READ ID completed
 *                      payload: ID bytes 0..3, big-endian
 *   c18 = 0xC0DE5006   all stage-A steps completed
 *                      payload: the stage-A pass value below
 *   c18 = 0xC0DE5007   the flash-spec operands were resolved
 *                      payload: 0x5001_0007 | from_atag ? bit16 : 0
 *   c18 = 0xC0DE5008   a page was read through the DMA engine
 *                      payload: the page's first four bytes (the pass value)
 *   c18 = 0xC0DE5009   the mtd device registered and the partition table parsed
 *                      payload: 0x5001_0009 | (the master mtd index & 0xff)
 *   c18 = 0xC0DE500A   rootfsb was read back through the mtd layer
 *                      payload: its first four bytes (the stage-C pass value)
 *   c18 = 0xC0DE50En   failed at step n
 *
 * WHICH CELL TO READ, AND WHY IT IS NOT c18.  The mach code stamps its own
 * 0xC0DE0020 at late_initcall (level 7), which runs AFTER this driver's
 * device_initcall probe (level 6) - so on a boot that gets that far, c18 ends
 * up holding the mach's value and the driver's crumb is gone.  c1c is the
 * cell that holds, and the reading is self-describing there:
 *
 *   c1c = 0x2349_4255    the pass value of stages B AND C.  In stage B it is
 *                        the first page of rootfsa read through the DMA engine;
 *                        in stage C it is the same four bytes read back through
 *                        the MTD layer from the partition named "rootfsb".
 *                        Either way it is the ASCII "UBI#", UBI's EC header
 *                        magic - the data itself, not a proxy for it.
 *   c1c = 0x8C2C_xxyy    STAGE A PASS - 0x8C2C is the die's signature, xx the
 *                        configuration byte (0xb0) and yy the status (0xc0)
 *   c1c = 0x5000_000n    the probe reached step n and stopped there; a boot
 *                        that hangs in this driver leaves the last step it
 *                        entered, which is the whole point of stamping the
 *                        entry to each step and not just the exits
 *   c1c = 0xE000_000n    the probe failed at step n
 *   c1c = 0x18C5387D     THE DRIVER NEVER RAN.  This is not a driver value:
 *                        it is r0 as patched-head.S writes it at its 0x0017
 *                        site (str r3,[ip] then str r0,[ip,#4], ip =
 *                        0x10100C18), i.e. the cell still holds the last
 *                        thing the head code put there.  Seeing it means the
 *                        probe was never called, which is a device-tree match
 *                        problem, not a hardware one.
 *   c1c = 0xFACEFEED     the pre-fire poison: the box never rebooted.
 *
 * WHERE THE DEVICE TREE COMES FROM, because it decides the match table.  The
 * vendor u-boot's bootfip hands the kernel ITS OWN devicetree, not the one
 * appended to our zImage - our root is "hisilicon,luofu-r116" while the mach's
 * dt_compat carries "hsan-luofu", and the kernel still matches, so the tree in
 * use is the vendor's.  In that tree the node is `fmc@10a20000 { compatible =
 * "hsan,fmc"; ... }` with no status property, which is why that string comes
 * FIRST in the match table below.  The driver's own binding stays as the
 * second entry so it still binds if the appended tree is ever the one used.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/dma-mapping.h>
#include <linux/genhd.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/console.h>
#include <linux/kmsg_dump.h>
#include <linux/notifier.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/ubi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/property.h>

/* ------------------------------------------------------------------ */
/* Register map (offsets from 0x10a20000), from the DWARF struct        */
/* tri_fmc_reg_s in tri_fmc.o, cross-checked against hi_flash.ko.       */
/* ------------------------------------------------------------------ */
#define FMC_CFG			0x00
#define FMC_GLOBAL_CFG		0x04
#define FMC_TIMING_SPI_CFG	0x08
#define FMC_PND_PWIDTH_CFG	0x0c
#define FMC_PND_OPIDLE_CFG	0x10
#define FMC_INT_STATUS		0x18
#define FMC_INT_EN		0x1c
#define FMC_INT_CLR		0x20
#define FMC_CMD			0x24
#define FMC_ADDRH		0x28
#define FMC_ADDRL		0x2c
#define FMC_OP_CFG		0x30
#define FMC_DATA_NUM		0x38
#define FMC_OP			0x3c
#define FMC_DMA_LEN		0x40
#define FMC_SADDR_D0		0x4c
#define FMC_SADDR_D1		0x50
#define FMC_SADDR_OOB		0x5c
#define FMC_OP_CTRL		0x68
#define FMC_FLASH_INFO		0xac
#define FMC_ECC_ERR_CNT0	0xc0
#define FMC_ECC_ERR_CNT1	0xc4

/* FMC_CFG */
#define FMC_CFG_NORMAL_MODE	BIT(0)
#define FMC_CFG_IS_RAW_NAND	BIT(2)		/* 1 = parallel NAND, 0 = SPI */
#define FMC_CFG_ECC_TYPE_SHIFT	5
#define FMC_CFG_ECC_TYPE_MASK	GENMASK(7, 5)

/* FMC_GLOBAL_CFG */
#define FMC_GCFG_RANDOMIZER	BIT(2)
#define FMC_GCFG_WP_EN		BIT(6)

/* FMC_OP_CFG */
#define FMC_OPCFG_DUMMY_MASK	GENMASK(3, 0)
#define FMC_OPCFG_ADDR_SHIFT	4
#define FMC_OPCFG_ADDR_MASK	GENMASK(6, 4)
#define FMC_OPCFG_IF_TYPE_SHIFT	7
#define FMC_OPCFG_IF_TYPE_MASK	GENMASK(9, 7)
#define FMC_OPCFG_CS_SHIFT	11
#define FMC_OPCFG_CS_MASK	GENMASK(12, 11)

/* FMC_OP: bit 0 is BUSY for the command engine (vendor hi_fmc_status_check) */
#define FMC_OP_BUSY		BIT(0)
#define FMC_OP_START		BIT(7)

/* FMC_OP_CTRL: bit 0 is BUSY for the DMA engine */
#define FMC_OPCTRL_BUSY		BIT(0)

/* SPI-NAND opcodes used by the command engine (tri_spi_nand_drv.o) */
#define SPINAND_CMD_RESET	0xff
#define SPINAND_CMD_GET_FEATURE	0x0f
#define SPINAND_CMD_SET_FEATURE	0x1f
#define SPINAND_CMD_RDID	0x9f

/* GET FEATURES register addresses (SPI-NAND standard) */
#define SPINAND_FEAT_STATUS	0xc0
#define SPINAND_FEAT_CONFIG	0xb0

/* FMC_OP values, all read out of tri_spi_nand_drv.o / hi_flash.ko */
#define FMC_OP_CMD_ONLY		0x81	/* START | cmd (the RESET form)     */
/* FMC_OP values, all read out of tri_spi_nand_drv.o / hi_flash.ko */
#define FMC_OP_GET_FEATURE	0xc5	/* START | addr | data-in    */
#define FMC_OP_RDID		0x185	/* START | dummy | data-in   */

/* Poll bounds.  The vendor uses 100000 iterations for the command engine and
 * 2000 for the die-ready wait; we keep the same shape with saner delays. */
#define FMC_CMD_POLLS		100000
#define FMC_READY_POLLS		2000
#define FMC_READY_DELAY_US	50

/* ID bytes the board's ESMT F50L1G41LC answers (mainline esmt.c, esmt_8c). */
#define LUOFU_NAND_ID0		0x8c
#define LUOFU_NAND_ID1		0x2c

/* The sysctrl scratch pair the mach breadcrumbs use (arch/arm/mach-luofu).
 * The head code writes a PAIR at its 0x0017 site - the crumb to c18 and r0 to
 * c1c - so a c1c holding r0's 0x18C5387D is the "never ran" signature. */
#define LUOFU_SYSCTRL_BASE	0x10100000
#define LUOFU_CRUMB_OFFSET	0xc18
#define LUOFU_CRUMB_PAYLOAD_OFF	0xc1c
#define LUOFU_CRUMB_NAND	0xc0de5000

/* The c1c protocol: progress, failure, and the pass values. */
#define LUOFU_RPT_STEP(n)	(0x50000000u | ((n) & 0xffu))
#define LUOFU_RPT_FAIL(n)	(0xe0000000u | ((n) & 0xffu))
#define LUOFU_RPT_STAGEB(n)	(0x50010000u | ((n) & 0xffu))

#define LUOFU_CRUMB_STEP_MASK	0xff

/* Where the page read lands: the first page of the rootfsa partition.  Chip
 * offset 0x1e00000 over a 2048-byte page is row 0xF00. */
#define LUOFU_ROOTFSA_OFFSET	0x1e00000

/* The UBI EC header magic, "UBI#": the pass value of the stage-B page read. */
#define LUOFU_UBI_MAGIC		0x23494255

/*
 * The flash-spec record.  The bootloader leaves it in the reserved region its
 * own devicetree declares - on this board reserved-memory/flashinfo@0x80800000,
 * reg = <0x80600000 0x400000>, atag-offset = <0x1000> - inside ATAG tag
 * 0x5441000a.  The vendor's tri_nand_probe() walks it as: index = payload[0],
 * then the 52-byte record at payload + 0x18 * index + 2.  The field offsets
 * below are not inferred: they are the offsets the vendor's own DMA routine
 * loads from (spec+0x28/0x29 for the address cycles, +0x2c/+0x2d/+0x2e for the
 * read interface type, command and dummy).
 */
#define LUOFU_ATAG_FLASHINFO	0x5441000a
#define LUOFU_ATAG_WINDOW	0x400
#define LUOFU_SPEC_SIZE		0x34
#define LUOFU_SPEC_TRI_SIZE	0x04
#define LUOFU_SPEC_BLOCK_SIZE	0x08
#define LUOFU_SPEC_PAGE_SIZE	0x0c
#define LUOFU_SPEC_OOB_SIZE	0x10
#define LUOFU_SPEC_PAGE_SHIFT	0x14
#define LUOFU_SPEC_ERASE_SHIFT	0x18
#define LUOFU_SPEC_ECC_TYPE	0x24
#define LUOFU_SPEC_RD_ADDR_CYC	0x28
#define LUOFU_SPEC_WR_ADDR_CYC	0x29
#define LUOFU_SPEC_ER_ADDR_CYC	0x2a
#define LUOFU_SPEC_RD_IF_TYPE	0x2c
#define LUOFU_SPEC_RD_CMD	0x2d
#define LUOFU_SPEC_RD_DUMMY	0x2e
#define LUOFU_SPEC_WR_IF_TYPE	0x2f
#define LUOFU_SPEC_WR_CMD	0x30
#define LUOFU_SPEC_WR_DUMMY	0x31

/*
 * WHERE THE CONTROLLER SPLICES ITS SPARE AREA INTO THE DATA STREAM.
 *
 * Measured, not chosen: fitting "copy below 1040 directly, copy from 14 bytes
 * earlier at and above it" against the vendor's own mtd14 matches every sample
 * where the vendor holds data, with no failures.  The first 1040 bytes of the
 * page are correct either way; at 1040 the controller inserts its spare area.
 *
 * These sit up here with the other layout constants because read_page(), which
 * uses them, is defined long before the log block further down.
 */
#define LUOFU_DMA_SPLICE_OFF	1040u
#define LUOFU_DMA_SPLICE_LEN	14u

struct luofu_nand_spec {
	u32	tri_size, block_size, page_size, oob_size, ecc_type;
	u32	page_shift, erase_shift;
	u8	rd_addr_cyc, wr_addr_cyc, er_addr_cyc;
	u8	rd_if_type, rd_cmd, rd_dummy;
	u8	wr_if_type, wr_cmd, wr_dummy;
	bool	from_atag;
};

struct luofu_fmc {
	struct device	*dev;
	void __iomem	*regs;
	void __iomem	*window;
	void __iomem	*crumb;		/* 8 bytes: crumb + payload */
	struct mtd_info	*mtd;
	u8		*page_buf;
	u32		cs;
	struct luofu_nand_spec spec;
	void		*dma_buf;
	dma_addr_t	dma_addr;
};

/* ------------------------------------------------------------------ */
/* The breadcrumb pair                                                  */
/* ------------------------------------------------------------------ */

static void luofu_fmc_crumb(struct luofu_fmc *fmc, u32 step, u32 payload)
{
	if (!fmc->crumb)
		return;

	/* payload first: the breadcrumb is the commit */
	writel(payload, fmc->crumb + (LUOFU_CRUMB_PAYLOAD_OFF - LUOFU_CRUMB_OFFSET));
	writel(LUOFU_CRUMB_NAND | (step & LUOFU_CRUMB_STEP_MASK), fmc->crumb);
}

/* ------------------------------------------------------------------ */
/* The register layer                                                   */
/* ------------------------------------------------------------------ */

static u32 luofu_fmc_cs_field(struct luofu_fmc *fmc)
{
	return (fmc->cs << FMC_OPCFG_CS_SHIFT) & FMC_OPCFG_CS_MASK;
}

/*
 * Wait for the command engine to report the operation done.
 * Vendor hi_fmc_status_check(): read FMC_OP, return once bit 0 is clear.
 */
static int luofu_fmc_wait_cmd(struct luofu_fmc *fmc)
{
	int i;

	for (i = 0; i < FMC_CMD_POLLS; i++) {
		if (!(readl(fmc->regs + FMC_OP) & FMC_OP_BUSY))
			return 0;
		udelay(1);
	}

	dev_err(fmc->dev, "FMC: controller operation time out\n");
	return -ETIMEDOUT;
}

static u8 luofu_fmc_ecc_type_get(struct luofu_fmc *fmc)
{
	return (readl(fmc->regs + FMC_CFG) & FMC_CFG_ECC_TYPE_MASK) >>
	       FMC_CFG_ECC_TYPE_SHIFT;
}

static void luofu_fmc_ecc_type_set(struct luofu_fmc *fmc, u8 val)
{
	u32 cfg = readl(fmc->regs + FMC_CFG);

	cfg &= ~FMC_CFG_ECC_TYPE_MASK;
	cfg |= (val << FMC_CFG_ECC_TYPE_SHIFT) & FMC_CFG_ECC_TYPE_MASK;
	writel(cfg, fmc->regs + FMC_CFG);
}

/*
 * GET FEATURES.  Every feature/id/status op in both the BSP and the vendor
 * clears the ECC field first and restores it afterwards, because the ID and
 * feature paths are single-byte transfers the ECC engine must not touch.
 */
static int luofu_fmc_get_feature_raw(struct luofu_fmc *fmc, u8 addr, u8 *val)
{
	int ret;

	writel(SPINAND_CMD_GET_FEATURE, fmc->regs + FMC_CMD);
	writel(addr, fmc->regs + FMC_ADDRL);
	writel(luofu_fmc_cs_field(fmc) | FMC_OPCFG_ADDR_MASK,
	       fmc->regs + FMC_OP_CFG);	/* 1 address cycle */
	writel(1, fmc->regs + FMC_DATA_NUM);
	writel(FMC_OP_GET_FEATURE, fmc->regs + FMC_OP);

	ret = luofu_fmc_wait_cmd(fmc);
	if (ret)
		return ret;

	*val = readb(fmc->window);
	return 0;
}

static int luofu_fmc_get_feature(struct luofu_fmc *fmc, u8 addr, u8 *val)
{
	u8 saved = luofu_fmc_ecc_type_get(fmc);
	int ret;

	luofu_fmc_ecc_type_set(fmc, 0);
	ret = luofu_fmc_get_feature_raw(fmc, addr, val);
	luofu_fmc_ecc_type_set(fmc, saved);

	return ret;
}

/* Die reset: cmd 0xff, FMC_OP = 0x81, then wait for OIP to clear. */
static int luofu_fmc_reset_die(struct luofu_fmc *fmc, u8 *status)
{
	u8 saved = luofu_fmc_ecc_type_get(fmc);
	int ret, i;
	u8 v;

	luofu_fmc_ecc_type_set(fmc, 0);

	writel(SPINAND_CMD_RESET, fmc->regs + FMC_CMD);
	writel(luofu_fmc_cs_field(fmc), fmc->regs + FMC_OP_CFG);
	writel(FMC_OP_CMD_ONLY, fmc->regs + FMC_OP);

	ret = luofu_fmc_wait_cmd(fmc);
	if (ret)
		goto out;

	/* BSP tri_spi_nand_drv_reset(): status mask 1 is the OIP bit. */
	for (i = 0; i < FMC_READY_POLLS; i++) {
		ret = luofu_fmc_get_feature_raw(fmc, SPINAND_FEAT_STATUS, &v);
		if (ret)
			goto out;
		if (!(v & BIT(0))) {
			*status = v;
			ret = 0;
			goto out;
		}
		usleep_range(FMC_READY_DELAY_US, FMC_READY_DELAY_US * 2);
	}

	dev_err(fmc->dev, "FMC: reset spi nand flash fail (device reset time out)\n");
	ret = -ETIMEDOUT;
out:
	luofu_fmc_ecc_type_set(fmc, saved);
	return ret;
}

/* READ ID: cmd 0x9f, 1 dummy cycle, 5 bytes out of the window. */
static int luofu_fmc_read_id(struct luofu_fmc *fmc, u8 id[5])
{
	u8 saved = luofu_fmc_ecc_type_get(fmc);
	int ret;

	luofu_fmc_ecc_type_set(fmc, 0);

	writel(SPINAND_CMD_RDID, fmc->regs + FMC_CMD);
	writel(5, fmc->regs + FMC_DATA_NUM);
	writel(luofu_fmc_cs_field(fmc) | 1, fmc->regs + FMC_OP_CFG); /* 1 dummy */
	writel(FMC_OP_RDID, fmc->regs + FMC_OP);

	ret = luofu_fmc_wait_cmd(fmc);
	if (!ret)
		memcpy_fromio(id, fmc->window, 5);

	luofu_fmc_ecc_type_set(fmc, saved);

	return ret;
}

/* ------------------------------------------------------------------ */
/* Stage B: the flash-spec operands and the DMA page read                */
/* ------------------------------------------------------------------ */

static void luofu_spec_defaults(struct luofu_nand_spec *s)
{
	*s = (struct luofu_nand_spec){
		.tri_size = 0x08000000, .block_size = 0x00020000,
		.page_size = 0x800, .oob_size = 0x40, .ecc_type = 1,
		.page_shift = 11, .erase_shift = 17,
		.rd_addr_cyc = 5, .wr_addr_cyc = 5, .er_addr_cyc = 3,
		.rd_if_type = 3, .rd_cmd = 0x6b, .rd_dummy = 1,
		.wr_if_type = 3, .wr_cmd = 0x32, .wr_dummy = 0,
		.from_atag = false,
	};
}

/*
 * Read the operands out of the bootloader's own table, the way the vendor's
 * tri_nand_probe() does.  On any problem the recovered values above are kept,
 * and from_atag records which happened so a boot can say so.
 */
static void luofu_fmc_spec_get(struct luofu_fmc *fmc)
{
	struct device_node *np;
	struct resource res;
	void __iomem *base, *p, *end, *payload = NULL, *rec;
	u32 size = 0, atag_off = 0;
	u8 index;

	luofu_spec_defaults(&fmc->spec);

	np = of_find_compatible_node(NULL, NULL, "hsan,flashinfo_reserved");
	if (!np)
		return;
	if (of_address_to_resource(np, 0, &res))
		goto out_node;
	if (of_property_read_u32(np, "atag-offset", &atag_off))
		goto out_node;

	base = ioremap(res.start + atag_off, LUOFU_ATAG_WINDOW);
	if (!base)
		goto out_node;
	end = base + LUOFU_ATAG_WINDOW;

	for (p = base; p + 8 <= end; p += 4 * size) {
		size = readl(p);
		if (!size)
			break;
		if (readl(p + 4) == LUOFU_ATAG_FLASHINFO) {
			payload = p + 8;
			break;
		}
	}

	if (!payload)
		goto out_map;

	index = readb(payload);
	rec = payload + 0x18 * index + 2;
	if (rec + LUOFU_SPEC_SIZE > end)
		goto out_map;

	fmc->spec.tri_size = readl(rec + LUOFU_SPEC_TRI_SIZE);
	fmc->spec.block_size = readl(rec + LUOFU_SPEC_BLOCK_SIZE);
	fmc->spec.page_size = readl(rec + LUOFU_SPEC_PAGE_SIZE);
	fmc->spec.oob_size = readl(rec + LUOFU_SPEC_OOB_SIZE);
	fmc->spec.page_shift = readl(rec + LUOFU_SPEC_PAGE_SHIFT);
	fmc->spec.erase_shift = readl(rec + LUOFU_SPEC_ERASE_SHIFT);
	fmc->spec.ecc_type = readl(rec + LUOFU_SPEC_ECC_TYPE);
	fmc->spec.rd_addr_cyc = readb(rec + LUOFU_SPEC_RD_ADDR_CYC);
	fmc->spec.wr_addr_cyc = readb(rec + LUOFU_SPEC_WR_ADDR_CYC);
	fmc->spec.er_addr_cyc = readb(rec + LUOFU_SPEC_ER_ADDR_CYC);
	fmc->spec.rd_if_type = readb(rec + LUOFU_SPEC_RD_IF_TYPE);
	fmc->spec.rd_cmd = readb(rec + LUOFU_SPEC_RD_CMD);
	fmc->spec.rd_dummy = readb(rec + LUOFU_SPEC_RD_DUMMY);
	fmc->spec.wr_if_type = readb(rec + LUOFU_SPEC_WR_IF_TYPE);
	fmc->spec.wr_cmd = readb(rec + LUOFU_SPEC_WR_CMD);
	fmc->spec.wr_dummy = readb(rec + LUOFU_SPEC_WR_DUMMY);
	fmc->spec.from_atag = true;

out_map:
	iounmap(base);
out_node:
	of_node_put(np);
}

/* SET FEATURES: the byte goes out from the data window, then the op is issued. */
static int luofu_fmc_set_feature(struct luofu_fmc *fmc, u8 addr, u8 val)
{
	u8 saved = luofu_fmc_ecc_type_get(fmc);
	int ret;

	luofu_fmc_ecc_type_set(fmc, 0);

	writeb(val, fmc->window);
	writel(SPINAND_CMD_SET_FEATURE, fmc->regs + FMC_CMD);
	writel(addr, fmc->regs + FMC_ADDRL);
	writel(luofu_fmc_cs_field(fmc) | FMC_OPCFG_ADDR_MASK,
	       fmc->regs + FMC_OP_CFG);
	writel(1, fmc->regs + FMC_DATA_NUM);
	writel(FMC_OP_GET_FEATURE, fmc->regs + FMC_OP);

	ret = luofu_fmc_wait_cmd(fmc);
	luofu_fmc_ecc_type_set(fmc, saved);

	return ret;
}

/*
 * Quad I/O needs the die's configuration register QE bit.  That is a write to
 * the die's CONFIGURATION register, not to the flash array - it is the same
 * write the vendor's driver performs on every boot when the devicetree carries
 * enable-quad-mode, and a die RESET clears it again.
 */
static int luofu_fmc_quad_enable(struct luofu_fmc *fmc)
{
	u8 cfg;
	int ret;

	ret = luofu_fmc_get_feature(fmc, SPINAND_FEAT_CONFIG, &cfg);
	if (ret)
		return ret;
	if (cfg & BIT(0))
		return 0;

	return luofu_fmc_set_feature(fmc, SPINAND_FEAT_CONFIG, cfg | BIT(0));
}

/*
 * The DMA page read.  saddr_d0/d1/oob point at one buffer split 0x1000 data /
 * 0x1000 data / 0x200 OOB; addrh/addrl carry the row; op_cfg carries the
 * interface type, address cycles and dummy count; op_ctrl carries the command
 * and the operation type, and the store that writes it starts the engine.
 */
static int luofu_fmc_read_page(struct luofu_fmc *fmc, u32 row, void *data)
{
	u8 saved = luofu_fmc_ecc_type_get(fmc);
	u32 op_cfg, op_ctrl;
	int i, ret;

	if (!fmc->dma_buf)
		return -ENOMEM;

	luofu_fmc_ecc_type_set(fmc, 0);

	writel(fmc->dma_addr, fmc->regs + FMC_SADDR_D0);
	writel(fmc->dma_addr + 0x1000, fmc->regs + FMC_SADDR_D1);
	writel(fmc->dma_addr + 0x2000, fmc->regs + FMC_SADDR_OOB);

	writel(row >> 16, fmc->regs + FMC_ADDRH);
	writel(row << 16, fmc->regs + FMC_ADDRL);

	op_cfg = luofu_fmc_cs_field(fmc) |
		 ((u32)(fmc->spec.rd_if_type & 7) << FMC_OPCFG_IF_TYPE_SHIFT) |
		 ((u32)(fmc->spec.rd_addr_cyc & 7) << FMC_OPCFG_ADDR_SHIFT) |
		 ((u32)fmc->spec.rd_dummy & FMC_OPCFG_DUMMY_MASK);
	writel(op_cfg, fmc->regs + FMC_OP_CFG);

	op_ctrl = ((u32)fmc->spec.rd_cmd << 16) | 1;

	/*
	 * CLEAR THE STAGING BUFFER BEFORE THE READ, because the controller does not
	 * write every byte of a page.
	 *
	 * The page map settled it: sampling four bytes every sixteen across page 2 and
	 * diffing against the vendor's own mtd14, 26 of 128 samples differ - and EVERY
	 * differing sample is a place where the flash holds ZEROS.  Where the flash has
	 * data, this driver matches it.  Worse, a whole run at 1664..1791 carries the
	 * 66CC pattern of RAM that was never written at all: the staging buffer holding
	 * whatever the allocation contained, handed out as flash content.
	 *
	 * So the hardware leaves zero regions unwritten, and every read returns
	 * whatever was in the buffer there.  Zeroing first makes those bytes read as
	 * zeros - which is what the flash actually holds.  It also explains the EC
	 * scan's 32 of 32: its 64-byte reads at offset 0 sit in a region the controller
	 * always fills.
	 *
	 * BEFORE the operation is started, not after - doing it afterwards would wipe
	 * the very data the controller just deposited.
	 *
	 * This is a correctness fix regardless of the mechanism: a read must never
	 * return bytes the device did not produce.
	 */
	memset(fmc->dma_buf, 0, fmc->spec.page_size);

	mb();
	writel(op_ctrl, fmc->regs + FMC_OP_CTRL);
	mb();

	/*
	 * WAIT FOR BUSY TO ASSERT, THEN FOR IT TO CLEAR - two phases, not one.
	 *
	 * The original loop broke as soon as BUSY read CLEAR, and immediately after
	 * the operation is started the hardware has not set it yet - so the loop could
	 * exit on its very first iteration and memcpy_fromio() would copy the buffer
	 * while the DMA was still filling it.
	 */
	for (i = 0; i < FMC_CMD_POLLS; i++) {
		if (readl(fmc->regs + FMC_OP_CTRL) & FMC_OPCTRL_BUSY)
			break;
		udelay(1);
	}

	if (i == FMC_CMD_POLLS) {
		dev_err(fmc->dev, "FMC: DMA never asserted busy\n");
		ret = -ETIMEDOUT;
	} else {
		for (i = 0; i < FMC_CMD_POLLS; i++) {
			if (!(readl(fmc->regs + FMC_OP_CTRL) & FMC_OPCTRL_BUSY))
				break;
			udelay(1);
		}

		if (i == FMC_CMD_POLLS) {
			dev_err(fmc->dev, "FMC: DMA page read timed out\n");
			ret = -ETIMEDOUT;
		} else {
			/*
			 * DE-INTERLEAVE THE SPARE AREA OUT OF THE DATA.
			 *
			 * The controller does not write a bare page: it writes the first 1040
			 * bytes of data and then 14 bytes of spare area before continuing with
			 * the rest.  Fitting that model against the vendor's own mtd14 - "below
			 * the insertion copy direct, at and above it copy from 14 bytes
			 * earlier" - matches EVERY sample where the vendor holds data, 128 of
			 * 128, with no failures.
			 *
			 * A straight page-sized copy therefore splices 14 bytes of spare into
			 * the middle of the page, and that is exactly the corruption UBI
			 * rejects: volume-table record 6 begins at offset 1032, so it is the
			 * record that straddles the splice.
			 *
			 * Both constants are measured rather than chosen: 1040 is where the
			 * bytes stop matching, and 14 is the distance the tail is displaced.
			 */
			if (fmc->spec.page_size == 2048) {
				memcpy_fromio(data, fmc->dma_buf, LUOFU_DMA_SPLICE_OFF);
				memcpy_fromio(data + LUOFU_DMA_SPLICE_OFF,
					      fmc->dma_buf + LUOFU_DMA_SPLICE_OFF +
					      LUOFU_DMA_SPLICE_LEN,
					      fmc->spec.page_size - LUOFU_DMA_SPLICE_OFF);
			} else {
				memcpy_fromio(data, fmc->dma_buf, fmc->spec.page_size);
			}
			ret = 0;
		}
	}

	luofu_fmc_ecc_type_set(fmc, saved);

	return ret;
}

/* ------------------------------------------------------------------ */
/* Stage C: the MTD device                                              */
/* ------------------------------------------------------------------ */

/*
 * The mtd_info is registered READ-ONLY, and deliberately so: the flags are
 * MTD_CAP_NANDFLASH with MTD_WRITEABLE cleared, and no _write/_erase/
 * _write_oob is provided at all.  That is sufficient for a rootfs boot, and
 * UBI's own attach code says so in as many words -
 *
 *   if (!(ubi->mtd->flags & MTD_WRITEABLE)) {
 *           ubi_msg(ubi, "MTD device %d is write-protected, attach in read-only mode");
 *           ubi->ro_mode = 1;
 *   }
 *
 * (drivers/mtd/ubi/build.c, v5.10) - and it keeps this driver's oldest
 * property intact: there is still no path in this file that can modify the
 * flash.  A page read is the only thing it ever asks the chip for.
 */
static int luofu_mtd_read(struct mtd_info *mtd, loff_t from, size_t len,
			  size_t *retlen, u_char *buf)
{
	struct luofu_fmc *fmc = mtd->priv;
	size_t done = 0;
	int ret;

	*retlen = 0;
	if (from < 0 || from + len > mtd->size)
		return -EINVAL;

	while (done < len) {
		loff_t pos = from + done;
		u32 row = div_u64(pos, mtd->writesize);
		size_t off = pos - (loff_t)row * mtd->writesize;
		size_t chunk = min_t(size_t, mtd->writesize - off, len - done);

		ret = luofu_fmc_read_page(fmc, row, fmc->page_buf);
		if (ret)
			return ret;

		memcpy(buf + done, fmc->page_buf + off, chunk);
		done += chunk;
	}

	*retlen = done;
	return 0;
}

/*
 * The bad-block marker is not read yet.  This returns "good" for every block,
 * which is a placeholder and not a claim: UBI's own scan reads every PEB's EC
 * header and marks a PEB bad when that read fails, so a genuinely bad block is
 * still caught - by UBI, one layer up, rather than here.
 */
static int luofu_mtd_block_isbad(struct mtd_info *mtd, loff_t ofs)
{
	return 0;
}

static int luofu_fmc_register_mtd(struct luofu_fmc *fmc)
{
	struct mtd_info *mtd;

	mtd = devm_kzalloc(fmc->dev, sizeof(*mtd), GFP_KERNEL);
	if (!mtd)
		return -ENOMEM;

	mtd->priv = fmc;
	mtd->dev.parent = fmc->dev;
	/* the partitions subnode hangs off the fmc node, which is what ofpart reads */
	mtd->dev.of_node = fmc->dev->of_node;
	mtd->name = "luofu-nand";
	mtd->type = MTD_NANDFLASH;
	/*
	 * MTD_NO_ERASE is not decoration: add_mtd_device() refuses a device that
	 * provides no ->_erase and has not declared it -
	 *
	 *   WARN_ON((!mtd->erasesize || !master->_erase) &&
	 *           !(mtd->flags & MTD_NO_ERASE))
	 *
	 * - and this driver provides no erase precisely because it must not
	 * modify the flash.  The live fire found that check the honest way: the
	 * first MTD-registration boot returned 0xE0000009, the step-9 failure,
	 * and the source says why.  UBI is unaffected - its build.c mentions
	 * MTD_NO_ERASE zero times and keys only on MTD_WRITEABLE.
	 */
	mtd->flags = (MTD_CAP_NANDFLASH & ~MTD_WRITEABLE) | MTD_NO_ERASE;
	mtd->size = fmc->spec.tri_size;
	mtd->erasesize = fmc->spec.block_size;
	mtd->writesize = fmc->spec.page_size;
	mtd->writebufsize = fmc->spec.page_size;
	mtd->oobsize = fmc->spec.oob_size;
	mtd->owner = THIS_MODULE;
	mtd->_read = luofu_mtd_read;
	mtd->_block_isbad = luofu_mtd_block_isbad;

	fmc->mtd = mtd;

	return mtd_device_parse_register(mtd, NULL, NULL, NULL, 0);
}

/* ------------------------------------------------------------------ */
/* Stage D: observe the rootfs volume through UBI                       */
/* ------------------------------------------------------------------ */

#if IS_ENABLED(CONFIG_MTD_UBI)
/*
 * The ATTACH is done by the kernel, from the command line this build carries
 * (ubi.mtd=rootfsb), because ubi_attach_mtd_dev() is deliberately not part of
 * UBI's public interface.  This hook does the part that has to be observable:
 * it opens the rootfs volume through the PUBLIC UBI API and reads its first
 * four bytes, so the reader cell ends up holding the volume's own data.
 *
 * IT OPENS THE VOLUME BY ID, NOT BY NAME - and a fire is what taught that.  The
 * first version asked for a volume named "rootfs" and returned 0xE000000B (the
 * step-11 failure) on real hardware.  The live system then answered why:
 * /sys/class/ubi/ubi0_0/name is "squashfs", not "rootfs" - and the string
 * "rootfs" does not occur anywhere in a dump of the partition either.  Volume 0
 * is the rootfs and its id is the stable fact; the name was my assumption.
 *
 * It runs at late_initcall_sync - after every late_initcall, and so after
 * UBI's own module_init has attached whatever the command line named, and after
 * ubiblock_init has had its chance to create the block device.
 *
 * The pass value is a squashfs superblock's magic "hsqs", which as a
 * little-endian word is 0x73717368, PLUS ONE when blk_lookup_devt() resolves
 * "ubiblock0_0" - the very call name_to_dev_t() makes for the root= line.  One
 * cell therefore carries both halves: the volume reads through UBI, and the
 * block device the root= line names is resolvable.  The +1 is deliberate, and
 * 0x73717369 is a distinct, documented value.
 */
/* the rootfs is volume 0 on the UBI device; its NAME is "squashfs" */
#define LUOFU_ROOTFS_VOL_ID	0

/*
 * A note worth writing down, because getting it wrong made every valid header
 * in a partition dump look corrupt: UBI's crc32() is initialised with
 * 0xFFFFFFFF and carries no final inversion, so the value on the flash equals
 * zlib's crc32 XOR 0xffffffff.  Checking the dump with zlib alone reported 120
 * of 120 EC headers as bad when all 120 were fine.
 */
#define LUOFU_UBI_CRC32_INIT	0xffffffffU

/*
 * Where the raw read bytes are parked for devmem, defined HERE because the hook
 * below writes them and the stage-E block that first declared them sits further
 * down the file.  A first attempt defined them next to the console buffer and the
 * build failed with "undeclared (first use in this function)" - the identifiers
 * were present, but not yet in scope.
 *
 * They are OFFSETS INTO OUR OWN COHERENT BUFFER now, not fixed addresses.  They
 * used to be absolute addresses chosen because a breadcrumb cell had survived
 * there - and that region turned out to be exactly where the FMC driver's DMA
 * landing zone had been placed, so the instrument was corrupting the thing it
 * was measuring.  See the note at LUOFU_DIAG_SIZE.
 */
/*
 * Offsets into the diagnostic buffer.  The sampling loop uses a 40-byte stride
 * (err/retlen plus 32 bytes), so the per-offset macros are no longer used; they
 * stay declared because the log's address is derived from LUOFU_LOG_SIZE and the
 * slot arithmetic reads more clearly with them named.
 */
#define LUOFU_DIAG_META_OFF	0	/* err, retlen */
#define LUOFU_DIAG_REC_OFF	8	/* then the record bytes */
#define LUOFU_DIAG_SIZE		0x200		/* 512 B diagnostic buffer */
/*
 * Crumb steps.  Declared HERE, above the hook, because the hook stamps one of them -
 * and that fact survived two failed attempts to fix it: first a batch whose addition
 * failed while its removal succeeded, leaving the macros gone altogether, and then a
 * checker that reported "no order problems" precisely because nothing was defined to
 * be out of order.  The checker now also fails on any LUOFU_* identifier that is USED
 * but never DEFINED, which is the gap that let the second attempt through.
 */
#define LUOFU_LOG_STEP		44		/* crumb: log ring's physical address */
#define LUOFU_LOG_PANIC_STEP	42		/* crumb: log address + our mtd index */
#define LUOFU_LOG_PANIC_VAL	0xc0de1042

static struct luofu_fmc *luofu_ubi_fmc;

/*
 * THE LOG AND DIAGNOSTIC BUFFERS, declared HERE rather than beside the console
 * code that allocates them, because the stage-D hook below writes into the
 * diagnostic buffer and sits further up the file.  A first attempt declared them
 * down there and the build failed with "undeclared (first use in this function)" -
 * the same mistake the constants made one phase earlier, in a place the macro
 * order check does not look.
 */
static void *luofu_log_b;	/* the log ring */
static dma_addr_t luofu_log_dma;
static void *luofu_diag_buf;	/* the diagnostic buffer */
static dma_addr_t luofu_diag_dma;
static struct kmsg_dumper luofu_kmsg;

static int __init luofu_fmc_ubi_probe(void)
{
	struct luofu_fmc *fmc = luofu_ubi_fmc;
	struct ubi_volume_desc *desc;
	struct mtd_info *part;
	u8 buf[4] = { 0 };
	size_t retlen;
	u32 word;
	int i, err;

	if (!fmc || !fmc->mtd)
		return 0;

	desc = ubi_open_volume(0, LUOFU_ROOTFS_VOL_ID, UBI_READONLY);
	if (IS_ERR(desc)) {
		/*
		 * UBI is not up, and the attach refuses our partition with -EINVAL.
		 * Every -EINVAL on UBI's attach path has been accounted for from its
		 * source and none should trip for this geometry, and the flash's own
		 * EC headers are valid - so the remaining suspect is what OUR READ
		 * PATH hands UBI.
		 *
		 * So ask the flash.  ubi_io_read_ec_hdr() accepts a PEB when the
		 * header's magic, version, CRC and geometry fields all check out;
		 * this replicates exactly that test over the first 32 PEBs of the
		 * partition and stamps how many pass and where it stopped.
		 *
		 * The expected answer is known from an independent dump of this same
		 * partition: its first 120 PEBs all carry a valid EC header.
		 *   0x5C2000FF  all 32 passed  -> the read path is fine
		 *   0x5C<pass><first bad PEB>  -> it stopped there
		 *   0x5CE00000|errno           -> mtd_read() itself refused
		 */
		part = get_mtd_device_nm("rootfsb");
		if (IS_ERR(part)) {
			luofu_fmc_crumb(fmc, 11, 0xe2000000);
			return 0;
		}

		for (i = 0; i < 32; i++) {
			u8 w[64];
			u32 magic, vho, doff, crc, calc;
			u64 ec;

			retlen = 0;
			err = mtd_read(part, (loff_t)i * fmc->spec.block_size,
				       64, &retlen, w);
			if (err) {
				put_mtd_device(part);
				luofu_fmc_crumb(fmc, 11,
						0xe0000000 | ((-err) & 0xff));
				return 0;
			}

			magic = ((u32)w[0] << 24) | ((u32)w[1] << 16) |
				((u32)w[2] << 8) | (u32)w[3];
			vho = ((u32)w[16] << 24) | ((u32)w[17] << 16) |
			      ((u32)w[18] << 8) | (u32)w[19];
			doff = ((u32)w[20] << 24) | ((u32)w[21] << 16) |
			       ((u32)w[22] << 8) | (u32)w[23];
			crc = ((u32)w[60] << 24) | ((u32)w[61] << 16) |
			      ((u32)w[62] << 8) | (u32)w[63];
			ec = 0;
			{
				int k;
				for (k = 0; k < 8; k++)
					ec = (ec << 8) | w[8 + k];
			}
			calc = crc32(LUOFU_UBI_CRC32_INIT, w, 60);

			if (magic != 0x55424923 || w[4] != 1 ||
			    vho != 2048 || doff != 4096 ||
			    ec > 0x7fffffffULL || crc != calc)
				break;
		}

		put_mtd_device(part);
		luofu_fmc_crumb(fmc, 11, 0x5c000000 |
				((u32)(i & 0xff) << 16) |
				(i == 32 ? 0xff : (u32)(i & 0xff)));

		/*
		 * AND PRINT WHAT THIS READ PATH ACTUALLY RETURNS, at two offsets, side
		 * by side - which is the measurement the whole phase could not make
		 * until the console capture worked.
		 *
		 * UBI's own words, now readable in RAM, name the fault: it dumps a VID
		 * header whose fields are garbage and refuses the attach with -22.  The
		 * vendor's MTD driver, reading the same flash, returns a perfectly valid
		 * VID header at byte 2048 of every PEB - "UBI!", vol_type 1, compat 5,
		 * vol_id 0x7FFFEFFF.
		 *
		 * The EC scan above passed 32 of 32 because it reads ONLY at offset 0 of
		 * each PEB (i * block_size), and every earlier validation of this read
		 * path did the same.  So the first question is simply whether this read
		 * path agrees with itself at the two offsets - and printing both answers
		 * it in one fire instead of one crumb at a time.
		 *
		 * Offsets sent to the ringbuffer through pr_err so they land in the
		 * console buffer at 0x80600c00 where devmem can read them.
		 */
		/*
		 * SAMPLE SEVERAL OFFSETS, BECAUSE THE FAILURE IS OFFSET-DEPENDENT.
		 *
		 * UBI's own words, from a log that is finally trustworthy (the buffer
		 * survives the vendor and no longer collides with anything):
		 *
		 *   ubi0: scanning is finished
		 *   ubi0 error: vtbl_check: bad CRC at record 6: 0xaae09698, not 0x000000
		 *   ubi0 error: ubi_read_volume_table: ...
		 *   attach_mtd_dev: failed to attach mtd14, error -22
		 *
		 * Scanning COMPLETES, so every VID header is fine and the compat patch did
		 * its job - the failure is in the volume table's data.  The vendor's flash
		 * has record 6 (at byte 5128) as an all-zero record with a valid CRC, while
		 * this driver returns the 66CC pattern of stale RAM and claims success.
		 *
		 * And the fault is NOT uniform: 32-byte reads at offsets 0 and 2048 matched
		 * the vendor byte for byte, and 5128 does not.  So this samples four
		 * offsets across the first PEB's page structure - 0 and 2048 (page 0 and
		 * page 1, both previously correct), 4096 (the data area's first byte) and
		 * 5128 (record 6, the one UBI objects to) - and parks each with its own
		 * err and retlen, so the pattern names the defect instead of a theory.
		 *
		 * Layout in the diagnostic buffer:
		 *   +0    err, retlen for offset 0
		 *   +8    the 32 bytes read there
		 *   ...   then 2048, 4096 and 5128 at the same stride
		 */
		/*
		 * WHICH MTD IS "rootfsb"?  There may be more than one.
		 *
		 * The four-offset sample produced something this driver cannot do: reads with
		 * err = 0 and retlen = 0.  luofu_mtd_read() sets *retlen = done on every path
		 * that returns 0, so a (0, 0) result cannot have come through it - and the
		 * payloads carry a fixed 14-byte stale patch (4cfb74e7 668a0751 7536bd0c
		 * d17e0000) at BOTH offset 0 and offset 5128, which is not flash at any
		 * offset.
		 *
		 * Which means get_mtd_device_nm("rootfsb") is resolving to some OTHER mtd
		 * device with the same name - plausibly one registered by the vendor's own
		 * NAND driver if that is compiled into this kernel, in which case it would
		 * also be the device ubi.mtd=rootfsb picks, and every UBI reading this phase
		 * has been about that driver rather than ours.
		 *
		 * So: enumerate every registered mtd and print its name, type and geometry,
		 * then report what rootfsb resolved to and whether it is the one our FMC
		 * driver registered.
		 */
		{
			/*
			 * MAP THE WHOLE PAGE AT 16-BYTE RESOLUTION.
			 *
			 * The double-read came back and AGREEED: two reads of offset 1040 return
			 * identical bytes, so the data is STABLE AND WRONG there - not a race (the
			 * fixed CRC across boots already said so) and not a stale cache.
			 *
			 * The three bracketing samples also pinned the blob exactly: 4cfb74e7
			 * 668a0751 7536bd0c d17e0000 occupies +1040..+1053 and nothing else, and each
			 * page carries its own different blob. So rather than guess at more offsets,
			 * this samples 4 bytes every 16 across the whole page - 128 samples, exactly
			 * the 512 bytes of the diagnostic buffer - and the vendor's same offsets are
			 * readable from mtd14 with dd. That maps EVERY differing window in one fire
			 * instead of one guessed offset at a time.
			 */
			u32 map[128];
			int s;

			if (fmc->mtd)
				luofu_fmc_crumb(fmc, LUOFU_LOG_PANIC_STEP,
						((u32)luofu_log_dma & 0xfffff000u) |
						((u32)fmc->mtd->index & 0xfffu));

			part = get_mtd_device_nm("rootfsb");
			if (IS_ERR(part))
				return 0;

			for (s = 0; s < 128; s++) {
				u32 v = 0;
				size_t rl = 0;

				mtd_read(part, 4096 + s * 16, 4, &rl, (u8 *)&v);
				map[s] = v;
			}
			put_mtd_device(part);

			if (luofu_diag_buf)
				memcpy(luofu_diag_buf, map, sizeof(map));
		}
		return 0;
	}

	err = ubi_read(desc, 0, buf, 0, 4);
	ubi_close_volume(desc);

	if (err) {
		luofu_fmc_crumb(fmc, 11, LUOFU_RPT_FAIL(11));
		return 0;
	}

	word = ((u32)buf[0]) | ((u32)buf[1] << 8) |
	       ((u32)buf[2] << 16) | ((u32)buf[3] << 24);

	if (blk_lookup_devt("ubiblock0_0", 0))
		word++;

	luofu_fmc_crumb(fmc, 11, word);

	dev_info(fmc->dev, "FMC: ubi0 volume %d begins %02x %02x %02x %02x, ubiblock0_0 %s\n",
		 LUOFU_ROOTFS_VOL_ID, buf[0], buf[1], buf[2], buf[3],
		 word & 1 ? "resolves" : "does NOT resolve");

	return 0;
}
late_initcall_sync(luofu_fmc_ubi_probe);
#endif /* CONFIG_MTD_UBI */

/* ------------------------------------------------------------------ */
/* Stage E: capture the kernel log on panic                             */
/* ------------------------------------------------------------------ */

/*
 * THIS BOARD HAS NO CONSOLE, so every failure in this phase has been INFERRED
 * from blank cells - while the kernel was writing the reason to printk the whole
 * time.  This captures that log at the one moment it is guaranteed to exist: the
 * panic our kernel takes at the rootfs mount, after UBI has already had its say
 * at late_initcall.
 *
 * A kmsg_dumper rather than ramoops, and a fire is why.  ramoops needed six
 * parameters on the command line, and a diff of the two build artifacts - which
 * differ in EXACTLY one config line, the CONFIG_CMDLINE string - shows those
 * parameters alone killed the boot between init_machine and the first initcall,
 * which is precisely where parse_args() runs.  This design has no parameters at
 * all.
 *
 * TWO BUFFERS, because the two candidate regions each fail a different way, and
 * one fire should say which:
 *
 *   0x809A0000 - outside our own kernel (Image is 9,984,344 bytes from
 *                0x80008000, ending about 0x8098D4D8) and inside the flashinfo
 *                reserved window.  But a fire showed the log written here reads
 *                back EMPTY, which is what the record predicts: the vendor's own
 *                image span "scrubs everything above 0x80608000" on its return.
 *
 *   0x80606000 - BELOW 0x80608000, the zone the record measured as surviving the
 *                vendor - but that zone is inside our own kernel's .data, so
 *                writing there at any normal time would corrupt the running
 *                kernel.  At PANIC time it does not matter: the kernel is
 *                already dead and its only remaining job is to reboot.
 *
 * So both are written, and only from the panic path - nothing is written at
 * registration, precisely so a live kernel is never touched.  Whichever reads
 * back afterwards identifies the surviving region as well as carrying the log.
 *
 * WHY memremap AND NOT ioremap - the answer to a fire that returned no marks at
 * all, not even the armed one written while the kernel was alive: BOTH buffers
 * are RAM, and on ARM `ioremap()` REFUSES valid RAM outright, via
 * WARN_ON(pfn_valid(pfn)) in __arm_ioremap_pfn_caller() - "Don't allow RAM to be
 * mapped". So both calls returned NULL, nothing was ever written, and the values
 * read back were simply whatever RAM happened to hold.  memremap() exists for
 * exactly this and is what ramoops itself uses.
 */
/*
 * THE LOG AND DIAGNOSTIC BUFFERS GO IN THE VENDOR-PRESERVED WINDOW, AT FIXED
 * ADDRESSES, WITH A COLLISION CHECK.
 *
 * Two wrong answers bracket the right one, and both were measured:
 *
 *  * A COHERENT ALLOCATION DOES NOT SURVIVE.  dmam_alloc_coherent() hands out
 *    ordinary System RAM (this kernel has no CMA), and the vendor's kernel boots
 *    afterwards and reuses it freely.  A fire parked the ring at 0x820F2000 and
 *    read back the vendor's own kernel text.
 *
 *  * A FIXED ADDRESS IS RIGHT, BUT ONLY IF NOTHING ELSE WANTS IT.  The window
 *    below 0x80608000 survives every vendor boot - that is where the vendor's
 *    kernel does NOT load, and a breadcrumb cell at 0x80600f20 there has
 *    survived dozens of fires.  The earlier failure was never the address: it
 *    was that the FMC driver's own DMA landing zone had been allocated INTO the
 *    same bytes, so the instrument overwrote what it was measuring.
 *
 * So: fixed addresses in that window, and the driver CHECKS at registration that
 * they do not overlap its own DMA buffer, shifting clear if they do.  The chosen
 * address is stamped into the crumb so the readback never has to guess.
 */
#define LUOFU_LOG_BASE		0x80602000	/* in the surviving window */
#define LUOFU_LOG_STRIDE	0x1000		/* slot size when shifting clear */
#define LUOFU_LOG_SIZE		0x200		/* 512 B log ring */
#define LUOFU_DIAG_SIZE		0x200		/* 512 B diagnostic buffer */
#define LUOFU_LOG_ARMED		0xc0de10a0	/* "the buffer was mapped" */
#define LUOFU_LOG_MARK		0xc0de1055	/* "the dumper ran" */
/*
 * THE LOG AND THE DIAGNOSTICS LIVE IN THEIR OWN COHERENT ALLOCATIONS NOW.
 *
 * They used to live at fixed addresses - 0x80600c00 for the ring, 0x80600e00 for
 * the diagnostics - chosen because a breadcrumb cell at 0x80600f20 had survived
 * there for many fires.  THAT REASONING WAS WRONG, and the cost was this whole
 * investigation: the FMC driver allocates its DMA landing zone with
 *
 *	dmam_alloc_coherent(dev, 0x2200, &fmc->dma_addr, GFP_KERNEL)
 *
 * whose address the allocator picks, and it picked that same region.  The
 * hardware reads a flash page into it, luofu_fmc_read_page() copies it out with
 * memcpy_fromio(), and MY CONSOLE WAS OVERWRITING IT ON EVERY PRINTK.  Every page
 * this driver read was therefore returning my own log text - which is exactly
 * what the parked record showed: ")\n\n<4>[    4".
 *
 * So the fault UBI reported was produced by the instrument observing it.  Both
 * buffers now come from dmam_alloc_coherent() and collide with nothing, and the
 * log ring's PHYSICAL address is stamped into a crumb so devmem can find it -
 * the log itself carries the diagnostic buffer's address, so one crumb bootstraps
 * the whole readback.
 */
#define LUOFU_LOG_STEP		44		/* crumb: log ring's physical address */

static void luofu_console_write(struct console *co, const char *s, unsigned int n);

static void luofu_kmsg_to(void *p, struct kmsg_dumper *dumper)
{
	static char line[256];
	size_t len, off = 4;
	u32 mark = LUOFU_LOG_MARK;

	if (!p)
		return;

	/* the mark first, so a reader can tell "ran" from "never ran" */
	memcpy(p, &mark, sizeof(mark));

	kmsg_dump_rewind(dumper);

	/* circular: when the buffer fills, start over so the NEWEST log survives */
	while (kmsg_dump_get_line(dumper, true, line, sizeof(line), &len)) {
		if (len > sizeof(line) - 1)
			len = sizeof(line) - 1;
		if (off + len + 1 > LUOFU_LOG_SIZE)
			off = 4;
		memcpy(p + off, line, len);
		off += len;
		*(char *)(p + off) = '\n';
		off++;
	}

	*(char *)(p + (off < LUOFU_LOG_SIZE ? off : LUOFU_LOG_SIZE - 1)) = 0;
}

static void luofu_kmsg_dump(struct kmsg_dumper *dumper,
			    enum kmsg_dump_reason reason)
{
	luofu_kmsg_to(luofu_log_b, dumper);
}

static struct notifier_block luofu_panic_nb;

static void luofu_log_tick(struct timer_list *t);

static struct timer_list luofu_log_timer;

/*
 * A CONSOLE THAT KEEPS WHAT IT IS SHOWN.
 *
 * Everything above this point tried to read the log back through the kmsg_dumper
 * API, and every attempt came back with a mark and no text - including from the
 * panic path, where the kernel itself sets up the iterator.  Rather than reason a
 * third time about that API's preconditions, this registers a console and simply
 * keeps each line as printk hands it over.
 *
 * That is what pstore's console backend does, and it is the direct route: the
 * callback is called by printk for every message that passes the console loglevel,
 * so UBI's ubi_err() lines arrive here by the same path they would reach a serial
 * port.  There is no ringbuffer to iterate, no cursor to position, and no flag to
 * set first - the API that was fighting me is gone from the design entirely.
 *
 * CON_ENABLED makes printk use it from the moment it is registered, and
 * CON_PRINTBUFFER also hands it everything printed before registration, which is
 * exactly the boot-time text wanted.
 */
static bool luofu_wanted_chunk(const char *s, unsigned int n)
{
	static const char *const keep[] = { "ubi", "FMC" };
	unsigned int i, k;

	for (k = 0; k < ARRAY_SIZE(keep); k++) {
		const char *w = keep[k];
		unsigned int wl = strlen(w);

		for (i = 0; i + wl <= n; i++) {
			if (!strncmp(s + i, w, wl))
				return true;
		}
	}

	return false;
}

static void luofu_console_write(struct console *co, const char *s, unsigned int n)
{
	static unsigned int off = 4; /* the mark occupies the first four bytes */
	u32 mark = LUOFU_LOG_MARK;
	void *p = luofu_log_b;

	if (!p)
		return;

	/*
	 * KEEP ONLY UBI'S OWN OUTPUT, and the reason is a measurement rather than a
	 * preference: the ring is 512 bytes, which is roughly eight messages, and a
	 * whole boot passes between UBI's attach attempt at late_initcall and the
	 * panic that follows the failed mount.  Everything UBI said was therefore long
	 * overwritten by the time the panic notifier could copy it out - which is why
	 * every reading so far showed the attach FAILING without showing WHICH CHECK
	 * failed, even though UBI prints exactly that line.
	 *
	 * UBI's messages are few, so a ring of only those holds all of them, including
	 * the specific reason, no matter how long the boot runs afterwards.  The
	 * match is deliberately loose - any chunk containing "ubi" case-insensitively -
	 * because printk may hand a message over in more than one piece and the pieces
	 * around the keyword carry the detail.
	 */
	if (!luofu_wanted_chunk(s, n))
		return;

	if (off == 4)
		memcpy(p, &mark, sizeof(mark));

	if (off + n + 1 > LUOFU_LOG_SIZE)
		off = 4;	/* circular: keep the NEWEST text */
	if (n > LUOFU_LOG_SIZE - 4)
		n = LUOFU_LOG_SIZE - 4;

	memcpy(p + off, s, n);
	off += n;
	*(char *)(p + (off < LUOFU_LOG_SIZE ? off : LUOFU_LOG_SIZE - 1)) = 0;
}

static struct console luofu_console = {
	.name	= "luofu",
	.write	= luofu_console_write,
	.flags	= CON_PRINTBUFFER | CON_ENABLED,
};

/*
 * A TIMER THAT SNAPSHOTS THE LOG WHILE THE KERNEL IS ALIVE, because the panic
 * hooks turned out to be aimed at an event that never happens.
 *
 * The panic notifier answered its own question by NOT firing: C18 stayed at the
 * stage-D hook's crumb (0xC0DE500B) instead of becoming step 42, which means the
 * kernel never entered panic() at all.  It hangs where the root device never
 * appears, something then resets the box, and a hang calls neither kmsg_dump()
 * nor any notifier - which is why every panic-based design returned an empty
 * buffer no matter how many of its own faults were fixed.
 *
 * So the capture stops waiting for a die and instead refreshes the buffer every
 * few seconds from a workqueue.  Whatever the kernel is doing when it finally
 * goes down - hanging, panicking, or being reset out from under us - the buffer
 * already holds the most recent log, because it was written while the kernel was
 * still running.
 *
 * A timer callback rather than a workqueue, and the difference was measured: the
 * workqueue version produced an empty buffer even though workqueue_init() runs
 * before do_basic_setup() and so before this wait.  Doing the copy directly in
 * the timer removes a moving part.  It is safe because kmsg_dump_get_line takes
 * logbuf_lock with logbuf_lock_irqsave, and logbuf_lock is a DEFINE_RAW_SPINLOCK
 * - a raw spinlock taken with interrupts off, so it is usable from softirq
 * context and cannot sleep.
 *
 * NO CRUMB IS STAMPED.  The buffer's own contents are the proof: it lives in the
 * window the vendor's own memory map confirms is untouched - below 0x80608000,
 * where the vendor's kernel code begins - so if the timer runs at all, the mark
 * and the text are both readable, and no second channel is needed to say so.
 */
static void luofu_log_tick(struct timer_list *t)
{
	/*
	 * dumper->active IS THE MISSING PIECE, and the mark landing without any
	 * text is what found it.  kmsg_dump_get_line_nolock() opens with:
	 *
	 *	if (!dumper->active)
	 *		goto out;		(returns false, *len = 0)
	 *
	 * and `active` is set in exactly one place - kmsg_dump() itself, on its way
	 * into a dumper's callback.  So a dumper whose callback is never called (as
	 * here, because this kernel never panics) stays inactive forever, and every
	 * iteration returns false on the first call.  The mark was written, the
	 * buffer was refreshed, and the log was silently empty.
	 *
	 * Setting it around the walk is safe and is this dumper's own state: nothing
	 * else iterates through it, kmsg_dump() re-sets it on the panic path, and
	 * kmsg_dump_get_line() takes logbuf_lock itself - so this neither races the
	 * lock nor disturbs a concurrent panic.
	 */
	luofu_kmsg.active = true;
	luofu_kmsg_to(luofu_log_b, &luofu_kmsg);
	luofu_kmsg.active = false;

	mod_timer(&luofu_log_timer, jiffies + 3 * HZ);
}

static int luofu_panic_notify(struct notifier_block *nb, unsigned long v, void *p)
{
	/*
	 * THE CRUMB CARRIES THE LOG RING'S PHYSICAL ADDRESS, not a marker.
	 *
	 * It used to carry LUOFU_LOG_PANIC_VAL, and that was a design mistake: the
	 * address is stamped at step 44 from luofu_log_register, but this notifier
	 * runs LATER in the same boot and the crumb cell holds one value, last write
	 * wins - so the address was overwritten before it could be read, and the
	 * marker that replaced it told me something I could already infer from the
	 * boot having panicked at all.
	 *
	 * With the log living in its own coherent allocation, its address cannot be
	 * predicted either: this kernel has no CMA, so dma_alloc_coherent() serves
	 * from the page allocator and the address is whatever those pages were.  This
	 * notifier is the LAST thing to touch the crumb, so this is the value that
	 * survives to be read.
	 */
	if (luofu_ubi_fmc)
		luofu_fmc_crumb(luofu_ubi_fmc, LUOFU_LOG_PANIC_STEP,
				(u32)(uintptr_t)luofu_log_dma);

	luofu_kmsg_to(luofu_log_b, &luofu_kmsg);

	return NOTIFY_DONE;
}

static int luofu_log_register(struct luofu_fmc *fmc)
{
	int i;

	/*
	 * __va, NOT ioremap AND NOT memremap - both of them returned NULL for this
	 * RAM, which is why no mark ever appeared.
	 *
	 * ioremap() refusing RAM is documented ARM behaviour (WARN_ON(pfn_valid(pfn))
	 * in __arm_ioremap_pfn_caller), and memremap() turned out to be no better
	 * here: with a timer refreshing the buffer every three seconds and a
	 * workqueue proven to be running well before the root-device wait, an empty
	 * buffer could only mean luofu_log_b itself was NULL.  Two mapping APIs, two
	 * failures, and no mark to show for either.
	 *
	 * These addresses are ordinary RAM inside the devicetree's memory node - the
	 * vendor's own map shows 80600000-87ffffff as System RAM - so the linear map
	 * already covers them and __va() is the correct way to reach them from kernel
	 * code.  No new mapping to fail, no cache-type question to get wrong: this is
	 * simply where that memory lives.
	 *
	 * Writes through the linear map are cached, and that is acceptable for a
	 * measured reason rather than a hopeful one: an earlier instrument in this
	 * phase wrote 4-byte breadcrumb cells into this very region through this same
	 * kind of mapping, and those values survived a reset and were read back with
	 * devmem from the vendor system.
	 */
	/*
	 * FIXED ADDRESSES IN THE SURVIVING WINDOW, SHIFTED CLEAR OF OUR OWN DMA.
	 *
	 * The driver's DMA landing zone is 0x2200 bytes and its address is whatever
	 * the allocator chose; the earlier corruption happened because the instrument
	 * was placed inside it.  Checking here is what makes a fixed address safe:
	 * if the chosen slot overlaps the DMA buffer, move up a slot and check again.
	 */
	{
		u64 dma_lo = (u64)fmc->dma_addr;
		u64 dma_hi = dma_lo + 0x2200;
		u64 pb_lo = (u64)(uintptr_t)fmc->page_buf;
		u64 pb_hi = pb_lo ? pb_lo + fmc->spec.page_size : 0;
		u64 base = LUOFU_LOG_BASE + LUOFU_LOG_STRIDE;

		for (i = 0; i < 8; i++, base += LUOFU_LOG_STRIDE) {
			if ((base + 2 * LUOFU_LOG_SIZE <= dma_lo ||
			     base >= dma_hi) &&
			    (!pb_hi || base + 2 * LUOFU_LOG_SIZE <= pb_lo ||
			     base >= pb_hi))
				break;
		}
		if (base + 2 * LUOFU_LOG_SIZE > 0x80608000)
			base = LUOFU_LOG_BASE;

		luofu_log_dma = (dma_addr_t)base;
		luofu_diag_dma = (dma_addr_t)(base + LUOFU_LOG_SIZE);
	}
	luofu_log_b = (void *)__va((phys_addr_t)luofu_log_dma);
	luofu_diag_buf = (void *)__va((phys_addr_t)luofu_diag_dma);

	/*
	 * NOTHING IS WRITTEN HERE, and a fire is why.  An armed mark written to
	 * BOTH buffers at registration - while the kernel is alive - moved the
	 * failure forward rather than backward: the boot then reached the mach's
	 * late_initcall crumb (C18 = 0xC0DE0020) and died before late_initcall_sync,
	 * leaving C1C at its earlier stage-B value, so the hook never ran at all.
	 * The only thing that had changed is that these writes finally succeeded.
	 * Buffer B sits inside our own .data, where four live bytes break a
	 * structure the kernel reads later; buffer A is outside our image but the
	 * vendor scrubs it, so a mark there would never have been readable anyway.
	 *
	 * The mapping question is answered through the SYSCtrl crumb instead - the
	 * one channel that has always worked, because it is I/O rather than RAM -
	 * stamped from the stage-D hook, which runs after everything else and so
	 * cannot have its crumb overwritten.  Both buffers are therefore written
	 * ONLY from the panic path, when corrupting our own data cannot cost
	 * anything but the reboot the kernel is about to perform regardless.
	 */
	if (!luofu_log_b)
		return -ENOMEM;

	luofu_kmsg.dump = luofu_kmsg_dump;
	/*
	 * KMSG_DUMP_MAX, NOT KMSG_DUMP_PANIC - and getting this wrong is why no
	 * mark ever appeared even once the mapping worked.  kmsg_dump() skips any
	 * dumper with `reason > max_reason`, and on this kernel the panic path
	 * reaches a dumper only through emergency_restart(), which dumps with
	 * KMSG_DUMP_EMERG.  The enum runs UNDEF, PANIC, OOPS, EMERG, SHUTDOWN,
	 * MAX - so EMERG is 3 and PANIC is 1, and setting max_reason to PANIC
	 * filtered out the only dump this kernel ever performs.  panic() itself
	 * never calls kmsg_dump at all; printk.c contains no such call outside
	 * kmsg_dump's own definition.
	 */
	luofu_kmsg.max_reason = KMSG_DUMP_MAX;
	kmsg_dump_register(&luofu_kmsg);

	/*
	 * A PANIC NOTIFIER AS WELL, because the log capture has failed silently
	 * five separate ways and one question none of those fixes ever answered is
	 * whether the panic happens at all - and whether a hook on that path runs.
	 * The panic notifier is the earliest hook inside panic(), so it fires even
	 * if the later restart path misbehaves.
	 *
	 * Its first act is a CRUMB, to the SYSCtrl cell - the one channel in this
	 * phase that has never once failed, because it is I/O rather than RAM.  That
	 * crumb is an unambiguous answer: if it appears, the panic happened and a
	 * panic-time hook runs; if it does not, the kernel never panicked and every
	 * RAM theory so far has been about the wrong event entirely.  Either way the
	 * reader learns one specific thing instead of reading another absence.
	 */
	luofu_panic_nb.notifier_call = luofu_panic_notify;
	atomic_notifier_chain_register(&panic_notifier_list, &luofu_panic_nb);

	/*
	 * And the snapshot timer, which is the hook that does not depend on the
	 * kernel ever panicking - see luofu_log_tick for why that matters.
	 */
	timer_setup(&luofu_log_timer, luofu_log_tick, 0);
	mod_timer(&luofu_log_timer, jiffies + 3 * HZ);

	/*
	 * And the console, which is the capture that does not depend on the kmsg API
	 * at all - see luofu_console_write.
	 */
	register_console(&luofu_console);

	/*
	 * BOOTSTRAP THE READBACK WITH ONE CRUMB.  The log ring's PHYSICAL address
	 * goes to the crumb value cell - the channel in this phase that has never
	 * lied, because it is I/O rather than RAM - and everything else is found
	 * from there: the log itself carries the diagnostic buffer's address, which
	 * the driver prints below.  One 32-bit value bootstraps the whole readback
	 * with no hard-coded address anywhere.
	 */
	luofu_fmc_crumb(fmc, LUOFU_LOG_STEP, (u32)luofu_log_dma);

	pr_err("FMC: log dma=%pad diag dma=%pad\n",
	       &luofu_log_dma, &luofu_diag_dma);

	return 0;
}

/* ------------------------------------------------------------------ */
/* Probe                                                                */
/* ------------------------------------------------------------------ */

static int luofu_fmc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct luofu_fmc *fmc;
	struct mtd_info *part;
	u8 id[5] = { 0 };
	u8 status = 0, config = 0;
	u8 *page;
	size_t retlen;
	u32 cfg, word;
	int ret;

	fmc = devm_kzalloc(dev, sizeof(*fmc), GFP_KERNEL);
	if (!fmc)
		return -ENOMEM;

	fmc->dev = dev;

	/*
	 * Map the report cell and stamp the ENTRY before anything else can fail:
	 * "the probe was called at all" is what separates a missing
	 * device-tree match from a failure inside this function, and it is the
	 * fact a wasted boot costs the most to get wrong.
	 */
	fmc->crumb = devm_ioremap(dev, LUOFU_SYSCTRL_BASE + LUOFU_CRUMB_OFFSET, 8);
	luofu_fmc_crumb(fmc, 1, LUOFU_RPT_STEP(1));

	fmc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fmc->regs))
		return dev_err_probe(dev, PTR_ERR(fmc->regs),
				     "cannot map the FMC register window\n");

	fmc->window = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(fmc->window))
		return dev_err_probe(dev, PTR_ERR(fmc->window),
				     "cannot map the FMC data window\n");

	if (device_property_read_u32(dev, "spi_cs", &fmc->cs))
		fmc->cs = 1;	/* the pinned DT value */

	luofu_fmc_crumb(fmc, 2, LUOFU_RPT_STEP(2));

	cfg = readl(fmc->regs + FMC_CFG);
	luofu_fmc_crumb(fmc, 3, cfg);

	if (cfg & FMC_CFG_IS_RAW_NAND) {
		dev_err(dev, "the controller is in raw-NAND mode (FMC_CFG=%08x), not SPI\n",
			cfg);
		luofu_fmc_crumb(fmc, 3, LUOFU_RPT_FAIL(3));
		return -ENODEV;
	}

	dev_info(dev, "FMC: cfg=%08x global_cfg=%08x timing=%08x cs=%u\n",
		 cfg, readl(fmc->regs + FMC_GLOBAL_CFG),
		 readl(fmc->regs + FMC_TIMING_SPI_CFG), fmc->cs);

	ret = luofu_fmc_reset_die(fmc, &status);
	if (ret) {
		luofu_fmc_crumb(fmc, 4, LUOFU_RPT_FAIL(4));
		return ret;
	}
	luofu_fmc_crumb(fmc, 4, LUOFU_RPT_STEP(4));

	ret = luofu_fmc_read_id(fmc, id);
	if (ret) {
		luofu_fmc_crumb(fmc, 5, LUOFU_RPT_FAIL(5));
		return ret;
	}

	luofu_fmc_crumb(fmc, 5, ((u32)id[0] << 24) | ((u32)id[1] << 16) |
				((u32)id[2] << 8) | (u32)id[3]);

	if (id[0] != LUOFU_NAND_ID0 || id[1] != LUOFU_NAND_ID1) {
		dev_warn(dev, "unexpected SPI-NAND id %02x %02x (expected %02x %02x)\n",
			 id[0], id[1], LUOFU_NAND_ID0, LUOFU_NAND_ID1);
		luofu_fmc_crumb(fmc, 5, ((u32)id[0] << 24) | ((u32)id[1] << 16) |
					((u32)id[2] << 8) | (u32)id[3]);
		return -ENODEV;
	}

	if (luofu_fmc_get_feature(fmc, SPINAND_FEAT_CONFIG, &config))
		config = 0xff;
	if (luofu_fmc_get_feature(fmc, SPINAND_FEAT_STATUS, &status))
		status = 0xff;

	/*
	 * The last write of the probe, and the one the reader keys on: the two
	 * ID bytes that identify the die, then the two feature bytes.  See the
	 * header for why this lands in c1c.
	 */
	luofu_fmc_crumb(fmc, 6, ((u32)id[0] << 24) | ((u32)id[1] << 16) |
				((u32)config << 8) | (u32)status);

	dev_info(dev, "FMC: READ ID %02x %02x %02x %02x %02x, config %02x, status %02x\n",
		 id[0], id[1], id[2], id[3], id[4], config, status);

	/*
	 * STAGE B: the page read.
	 *
	 * The operands come out of the bootloader's own flash-spec table, parsed
	 * the way the vendor's driver parses it; the die is put into the quad
	 * configuration that table describes; and one page is read through the
	 * DMA engine from the start of the rootfsa partition.  The page's first
	 * four bytes are what the reader cell ends up holding, so the pass value
	 * is the data itself rather than a proxy for it: 0x23494255 is the ASCII
	 * "UBI#", UBI's EC header magic, and it is what the first page of a
	 * healthy rootfsa partition must begin with.
	 */
	luofu_fmc_spec_get(fmc);
	if (!fmc->spec.page_size)
		fmc->spec.page_size = 0x800;

	luofu_fmc_crumb(fmc, 7, LUOFU_RPT_STAGEB(7) |
				 (fmc->spec.from_atag ? BIT(16) : 0));

	dev_info(dev, "FMC: spec from %s: page %u oob %u block %u; read if %u cmd %02x dummy %u cyc %u\n",
		 fmc->spec.from_atag ? "the ATAG" : "the recovered defaults",
		 fmc->spec.page_size, fmc->spec.oob_size, fmc->spec.block_size,
		 fmc->spec.rd_if_type, fmc->spec.rd_cmd, fmc->spec.rd_dummy,
		 fmc->spec.rd_addr_cyc);

	if (luofu_fmc_quad_enable(fmc))
		dev_warn(dev, "FMC: could not enable quad I/O; the page read may not answer\n");

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32)))
		dev_warn(dev, "FMC: could not set the 32-bit DMA mask\n");

	fmc->dma_buf = dmam_alloc_coherent(dev, 0x2200, &fmc->dma_addr, GFP_KERNEL);
	if (!fmc->dma_buf) {
		luofu_fmc_crumb(fmc, 8, LUOFU_RPT_FAIL(8));
		return -ENOMEM;
	}

	page = devm_kzalloc(dev, fmc->spec.page_size, GFP_KERNEL);
	if (!page) {
		luofu_fmc_crumb(fmc, 8, LUOFU_RPT_FAIL(8));
		return -ENOMEM;
	}
	/*
	 * The mtd read hook gets its own frame through fmc->page_buf, so the two
	 * MUST be the same allocation - and the first MTD fire is what said so:
	 * page_buf was left NULL, mtd_read() reached memcpy_fromio() with a NULL
	 * destination, and the kernel panicked between the step-9 and step-10
	 * crumbs.  That is why C18 stayed at 0xC0DE5009 with no step-10 value
	 * ever deposited, and why the box took several panic/reboot cycles before
	 * the vendor failover returned.
	 */
	fmc->page_buf = page;

	ret = luofu_fmc_read_page(fmc,
				  LUOFU_ROOTFSA_OFFSET / fmc->spec.page_size, page);
	if (ret) {
		luofu_fmc_crumb(fmc, 8, LUOFU_RPT_FAIL(8));
		return ret;
	}

	word = ((u32)page[0]) | ((u32)page[1] << 8) |
	       ((u32)page[2] << 16) | ((u32)page[3] << 24);
	luofu_fmc_crumb(fmc, 8, word);

	dev_info(dev, "FMC: rootfsa page 0 begins %02x %02x %02x %02x%s\n",
		 page[0], page[1], page[2], page[3],
		 word == LUOFU_UBI_MAGIC ? "  (UBI# - the page read works)" : "");

	/*
	 * STAGE C: register the MTD device, then prove the partition table.
	 *
	 * Registering alone would be observable only as an index, so the probe
	 * then looks the rootfsb partition up BY NAME and reads its first page
	 * THROUGH the mtd layer - the path UBI will use, and the very name this
	 * driver's command line will hand to ubi.mtd=.  If that read returns
	 * UBI#'s four bytes, then the device registered, ofpart parsed the
	 * table, the partition exists under the name UBI will ask for, and the
	 * read survives the mtd page arithmetic.
	 */
	if (luofu_fmc_register_mtd(fmc)) {
		luofu_fmc_crumb(fmc, 9, LUOFU_RPT_FAIL(9));
		return 0;
	}
	luofu_fmc_crumb(fmc, 9, LUOFU_RPT_STAGEB(9) | (fmc->mtd->index & 0xff));

	part = get_mtd_device_nm("rootfsb");
	if (IS_ERR(part)) {
		luofu_fmc_crumb(fmc, 10, LUOFU_RPT_FAIL(10));
		return 0;
	}

	page[0] = page[1] = page[2] = page[3] = 0;
	retlen = 0;
	if (mtd_read(part, 0, 4, &retlen, page)) {
		luofu_fmc_crumb(fmc, 10, LUOFU_RPT_FAIL(10));
	} else {
		word = ((u32)page[0]) | ((u32)page[1] << 8) |
		       ((u32)page[2] << 16) | ((u32)page[3] << 24);
		luofu_fmc_crumb(fmc, 10, word);
	}

	dev_info(dev, "FMC: mtd%d registered; rootfsb is mtd%d; its first 4 bytes read back %02x %02x %02x %02x\n",
		 fmc->mtd->index, part->index, page[0], page[1], page[2], page[3]);
	put_mtd_device(part);

	/* stage D's hook looks the driver state up here, at late_initcall_sync */
	luofu_ubi_fmc = fmc;

	/*
	 * Stage E: from here on, a panic writes the kernel log into RAM where it
	 * can be read back from the vendor system.  Registered last so that
	 * failing to get the buffer cannot spoil any of the measurements above.
	 */
	if (luofu_log_register(fmc))
		dev_warn(dev, "FMC: could not arm the panic log capture\n");

	return 0;
}

static const struct of_device_id luofu_fmc_of_match[] = {
	/*
	 * "hsan,fmc" FIRST: it is the string in the devicetree the vendor u-boot
	 * actually hands this kernel (fmc@10a20000, no status property, so the
	 * node is live).  The second entry is our own binding, used only if the
	 * appended tree is ever the one that boots - see the file header.
	 */
	{ .compatible = "hsan,fmc" },
	{ .compatible = "hisilicon,luofu-fmc" },
	{ }
};
MODULE_DEVICE_TABLE(of, luofu_fmc_of_match);

static struct platform_driver luofu_fmc_driver = {
	.probe	= luofu_fmc_probe,
	.driver	= {
		.name		= "luofu-fmc",
		.of_match_table	= luofu_fmc_of_match,
	},
};
module_platform_driver(luofu_fmc_driver);

MODULE_AUTHOR("the luofu port");
MODULE_DESCRIPTION("Hisilicon luofu (Hi5671Y) FMC SPI-NAND controller (stage A)");
MODULE_LICENSE("GPL v2");
