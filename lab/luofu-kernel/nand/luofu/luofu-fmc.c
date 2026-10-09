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
 *   c18 = 0xC0DE5001   probe entered, controller + window mapped
 *                      payload: the chip select
 *   c18 = 0xC0DE5002   FMC_CFG read
 *                      payload: FMC_CFG
 *   c18 = 0xC0DE5003   die reset completed and ready
 *                      payload: the GET FEATURES 0xc0 status byte
 *   c18 = 0xC0DE5004   READ ID completed
 *                      payload: ID bytes 0..3, big-endian
 *   c18 = 0xC0DE5005   configuration register read back
 *                      payload: id[4] << 24 | 0xb0 << 16 | 0xc0 << 8 | cfg_bits
 *   c18 = 0xC0DE50E0|n step n failed (n as above)
 *
 * A payload of 0 from a boot that reached the stage means the register read
 * returned 0; the breadcrumb alone never proves a value, so every stage
 * records its payload.
 */

#include <linux/bits.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
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

/* The sysctrl scratch pair the mach breadcrumbs use (arch/arm/mach-luofu). */
#define LUOFU_SYSCTRL_BASE	0x10100000
#define LUOFU_CRUMB_OFFSET	0xc18
#define LUOFU_CRUMB_PAYLOAD_OFF	0xc1c
#define LUOFU_CRUMB_NAND	0xc0de5000

#define LUOFU_CRUMB_STEP_MASK	0xff

struct luofu_fmc {
	struct device	*dev;
	void __iomem	*regs;
	void __iomem	*window;
	void __iomem	*crumb;		/* 8 bytes: crumb + payload */
	u32		cs;
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
/* Probe                                                                */
/* ------------------------------------------------------------------ */

static int luofu_fmc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct luofu_fmc *fmc;
	u8 id[5] = { 0 };
	u8 status = 0, config = 0;
	u32 cfg;
	int ret;

	fmc = devm_kzalloc(dev, sizeof(*fmc), GFP_KERNEL);
	if (!fmc)
		return -ENOMEM;

	fmc->dev = dev;

	fmc->regs = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(fmc->regs))
		return dev_err_probe(dev, PTR_ERR(fmc->regs),
				     "cannot map the FMC register window\n");

	fmc->window = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(fmc->window))
		return dev_err_probe(dev, PTR_ERR(fmc->window),
				     "cannot map the FMC data window\n");

	/* Optional: without it the driver still runs, it just cannot report. */
	fmc->crumb = devm_ioremap(dev, LUOFU_SYSCTRL_BASE + LUOFU_CRUMB_OFFSET, 8);

	if (device_property_read_u32(dev, "spi_cs", &fmc->cs))
		fmc->cs = 1;	/* the pinned DT value */

	luofu_fmc_crumb(fmc, 1, fmc->cs);

	cfg = readl(fmc->regs + FMC_CFG);
	luofu_fmc_crumb(fmc, 2, cfg);

	if (cfg & FMC_CFG_IS_RAW_NAND) {
		dev_err(dev, "the controller is in raw-NAND mode (FMC_CFG=%08x), not SPI\n",
			cfg);
		luofu_fmc_crumb(fmc, 0xe0 | 2, cfg);
		return -ENODEV;
	}

	dev_info(dev, "FMC: cfg=%08x global_cfg=%08x timing=%08x cs=%u\n",
		 cfg, readl(fmc->regs + FMC_GLOBAL_CFG),
		 readl(fmc->regs + FMC_TIMING_SPI_CFG), fmc->cs);

	ret = luofu_fmc_reset_die(fmc, &status);
	if (ret) {
		luofu_fmc_crumb(fmc, 0xe0 | 3, (u32)ret);
		return ret;
	}
	luofu_fmc_crumb(fmc, 3, status);

	ret = luofu_fmc_read_id(fmc, id);
	if (ret) {
		luofu_fmc_crumb(fmc, 0xe0 | 4, (u32)ret);
		return ret;
	}

	luofu_fmc_crumb(fmc, 4, ((u32)id[0] << 24) | ((u32)id[1] << 16) |
				((u32)id[2] << 8) | (u32)id[3]);

	if (luofu_fmc_get_feature(fmc, SPINAND_FEAT_CONFIG, &config))
		config = 0xff;
	if (luofu_fmc_get_feature(fmc, SPINAND_FEAT_STATUS, &status))
		status = 0xff;

	luofu_fmc_crumb(fmc, 5, ((u32)id[4] << 24) | ((u32)config << 16) |
				((u32)status << 8));

	dev_info(dev, "FMC: READ ID %02x %02x %02x %02x %02x, config %02x, status %02x\n",
		 id[0], id[1], id[2], id[3], id[4], config, status);

	if (id[0] != LUOFU_NAND_ID0 || id[1] != LUOFU_NAND_ID1) {
		dev_warn(dev, "unexpected SPI-NAND id %02x %02x (expected %02x %02x)\n",
			 id[0], id[1], LUOFU_NAND_ID0, LUOFU_NAND_ID1);
		luofu_fmc_crumb(fmc, 4 | 0x40,
				((u32)id[0] << 24) | ((u32)id[1] << 16) |
				((u32)id[2] << 8) | (u32)id[3]);
	}

	/*
	 * Stage B attaches here: the mainline SPI-NAND core (already selected
	 * by CONFIG_MTD_SPI_NAND, whose esmt_8c table carries this die) reached
	 * through a spi-mem controller, or a direct mtd_info.  Both need the
	 * DMA page-read operands that come from the bootloader flash-spec table.
	 */

	return 0;
}

static const struct of_device_id luofu_fmc_of_match[] = {
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
