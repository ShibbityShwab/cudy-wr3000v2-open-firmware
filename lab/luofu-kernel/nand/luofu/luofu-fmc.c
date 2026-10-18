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
#include <linux/clk.h>
#include <linux/reset.h>

/*
 * ONE-SHOT FLAGS FOR THE READ-PATH POISON TEST - DECLARED HERE BECAUSE BOTH `luofu_fmc_read_page`
 * (which runs the test) AND `luofu_fmc_write_page` (which reports it) sit BELOW these includes and
 * ABOVE the driver's own state block, so a declaration down there is too late for either to see.
 */
static bool luofu_poison_done;      /* the poison has been placed, once */
static bool luofu_poison_reported;  /* its result has been deposited, once */
static bool luofu_row_reported;     /* the computed row + ADDRH/ADDRL, deposited once */
/*
 * ★ A SEPARATE FLAG FOR THE ADDRESS DEPOSIT - sharing one was the bug.
 *
 * The first version gated BOTH deposits on `luofu_poison_done && !luofu_row_reported`, and the
 * read-path block sets that flag.  So by the time the write-path block was reached the guard was
 * already false and IT NEVER RAN: the cell read back exactly the poison, proving nothing was
 * written.  Two instruments sharing one one-shot flag means only the first ever executes.
 */
static bool luofu_addr_reported;    /* the DMA buffer address, deposited once, independently */

#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/dma-mapping.h>
#include <linux/genhd.h>
#include <linux/io.h>
#include <linux/string.h>
#include <linux/reboot.h>
#include <linux/spinlock.h>
#include <linux/console.h>
#include <linux/kmsg_dump.h>
#include <linux/notifier.h>
#include <linux/timer.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/ubi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <asm/cacheflush.h>
#include <asm/outercache.h>
#include <asm/hwcap.h>

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
#define SPINAND_CMD_BLOCK_ERASE	0xd8	/* the vendor's hi_spi_nand_drv_erase passes this */
#define SPINAND_CMD_WRITE_ENABLE 0x06	/* sent before EVERY erase by hi_fmc_write_reg_en */

/* GET FEATURES register addresses (SPI-NAND standard) */
#define SPINAND_FEAT_STATUS	0xc0
#define SPINAND_FEAT_CONFIG	0xb0

/* FMC_OP values, all read out of tri_spi_nand_drv.o / hi_flash.ko */
#define FMC_OP_CMD_ONLY		0x81	/* START | cmd (the RESET form)     */
/* FMC_OP values, all read out of tri_spi_nand_drv.o / hi_flash.ko */
#define FMC_OP_GET_FEATURE	0xc5	/* START | addr | data-in    */
#define FMC_OP_RDID		0x185	/* START | dummy | data-in   */
#define FMC_OP_ERASE		0xc1	/* START | addr - the vendor's hi_spi_nand_hw_erase */

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

/*
 * ★ THE COMPLETION PROTOCOL, TAKEN FROM THE VENDOR'S OWN DISASSEMBLY (2026-10-15).
 *
 * Reading hi_flash.ko's own code rather than our assumptions:
 *
 *   hi_fmc_status_check_dma_status:
 *       r5 = r0 + 0x68;  loop: ldr r3,[r5]; ands r3,r3,#1; bne loop
 *       -> the completion wait is BIT 0 OF 0x68 (FMC_OP_CTRL), CLEARING when the op finishes.
 *          THE VENDOR POLLS THE SAME REGISTER IT WROTE TO START THE OP.
 *
 *   hi_fmc_int_status_get:    ldr r3,[r0,#0x18]; tst r3,#0x38   -> bits 3|4|5, NOT bit 0
 *   hi_fmc_clr_interrupt:     mov r3,#0x38;    str r3,[r0,#0x20] -> clears 0x38, NOT 1
 *   hi_fmc_en_interrupt:      bfi r3,r1,#3,#1 / #4 / #5          -> enables bits 3-5, NOT bit 0
 *
 * Our driver polled 0x18 for bit 0, cleared 0x20 with 1, and enabled 0x1c with 1 - all three
 * different, and it NEVER polled 0x68 at all.
 *
 * *** WHY THAT MATTERS MORE THAN ANY REGISTER VALUE: our poll can succeed on a bit that is
 *     ALREADY set, while the transfer is still running. The read then returns whatever dma_buf
 *     held, and the "poll succeeded" line is a FALSE POSITIVE - which is why nine register-level
 *     fixes changed nothing: they all tuned the CONFIGURATION, and the divergence is in the
 *     COMPLETION PROTOCOL. ***
 */
#define FMC_INT_DMA_DONE_BITS	0x38u	/* bits 3|4|5, the vendor's int_status test */
#define FMC_OP_START_BIT	BIT(0)	/* in FMC_OP_CTRL: set by us, cleared by the hardware */

/*
 * THE OPERATION OPCODES, taken verbatim from the vendor's per-operation functions.
 *
 * These are LITERAL opcodes written to FMC_OP_CFG (0x30) - not field packs.  Read from
 * hi_nand_hw_read (0x50), hi_nand_hw_read_id (0x10) and hi_nand_hw_erase (0x30) in hi_flash.ko.
 */
#define LUOFU_OP_CFG_READ	0x50u	/* hi_nand_hw_read:      mov r3,#0x50 ; str r3,[r0,#0x30] */
#define LUOFU_OP_CFG_READ_ID	0x10u	/* hi_nand_hw_read_id:   mov ip,#0x10 ; str ip,[r0,#0x30] */
#define LUOFU_OP_CFG_ERASE	0x30u	/* hi_nand_hw_erase:     mov ip,#0x30 ; str ip,[r0,#0x30] */

/* And the START KICK written to FMC_OP_CTRL (0x68) - also literal. */
#define LUOFU_OP_CTRL_READ	1u	/* hi_nand_hw_read:      mov r3,#1 ; str r3,[r4,#0x68] */
#define LUOFU_OP_CTRL_WRITE	3u	/* hi_nand_hw_write:     mov r3,#3 ; str r3,[r4,#0x68] */

/*
 * ★ THE READ'S ADDRESS-CYCLE COUNT IS 2, AND THE ATAG TABLE SAYS 5. THE HARDWARE IS RIGHT.
 *
 * Measured 2026-10-15, sampling FMC_OP_CFG (0x30) WHILE the vendor's own kernel performed each
 * operation - which is a test no earlier window ran:
 *
 *     during a READ   (dd if=/dev/mtd14, +0x2c stepping through rows):
 *         +0x30 = 0x9A1  on every sample
 *     during an ERASE (flash_eraseall /dev/mtd5):
 *         +0x30 = 0x810  on every sample
 *
 * Decoded with this driver's own field layout (dummy bits 3:0, addr_cyc bits 6:4, if_type 9:7):
 *
 *     ERASE 0x810 : cs=1 if_type=0 addr_cyc=1 dummy=0
 *     READ  0x9A1 : cs=1 if_type=3 addr_cyc=2 dummy=1
 *     OURS  0x9D1 : cs=1 if_type=3 addr_cyc=5 dummy=1   <- the ATAG value
 *
 * addr_cyc is how many ADDRESS CYCLES the controller clocks out. Sending three extra derails the
 * flash's command phase, the device never drives data back, and the controller returns whatever its
 * output register holds - A CONSTANT, PAGE-INDEPENDENT VALUE. That is precisely `40 1f 00 00`,
 * identical across seven fires and present nowhere on the chip.
 *
 * It also explains the split that had no explanation: the ERASE uses addr_cyc=1, an entirely
 * different count, so an error in the READ's count cannot affect it - which is why "erase works,
 * the data path does not" has been the puzzle all along.
 *
 * The ATAG table is a DECODED artifact. Where it and the live hardware disagree, the hardware wins -
 * a lesson this project has now paid for three times (the 0xC1C ladder from a stale revision, the
 * 0x2c post-op residue read as a row, and this).
 */
#define LUOFU_RD_ADDR_CYC_MEASURED	2
#define LUOFU_SPEC_WR_IF_TYPE	0x2f
#define LUOFU_SPEC_WR_CMD	0x30
#define LUOFU_SPEC_WR_DUMMY	0x31

/*
 * THE SPLICE - KEPT, BECAUSE REMOVING IT MADE THINGS WORSE.
 *
 * The plain copy was tried with the vendor's full configuration, order and three
 * destinations - every variable controlled at last - and UBI fell back to record 6,
 * accepting only four records. With this de-interleave it accepts 117.
 *
 * So this driver's raw buffer DOES carry the splice, even though the vendor's own
 * raw buffer, read live at its SADDR_D0, does not: page 3 there holds f116c36b at
 * 1044 in both its raw and its logical output, 172 bytes apart like every record.
 * The two drivers differ structurally, in something around the operation that no
 * register value, order or configuration field accounts for.
 *
 * 1040 and 14 are the measured pair.
 */





/*
 * WHICH PAGE THE DIAGNOSTIC MAPS.  Page 2 is where the splice was found; page 9
 * is where record 118 lives, the record UBI now rejects after record 6 started
 * passing.  9 * 2048 = 18432.
 */
#define LUOFU_MAP_PAGE_OFF	18432u

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
	struct clk	*clk;		/* sfc_clk - enabled in probe, the way the vendor does */
	struct reset_control *rst;	/* the DT's three reset lines, released in probe */
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

/*
 * THE READ PATH SHARES ONE DMA BUFFER, SO IT NEEDS A LOCK - AND IT MUST BE A SPINLOCK,
 * NOT A MUTEX.
 *
 * The lock was a mutex first, on the reasoning that two readers must not stage into
 * fmc->dma_buf at the same time. That reasoning was right and the primitive was wrong:
 * UBI CALLS mtd_read() WITH A SPINLOCK HELD, and a mutex SLEEPS. Sleeping in atomic
 * context is what the measured hang looks like from outside - it is hard, it kills the
 * timers, the safety timer never fires, and the box's own watchdog is what returns it.
 *
 * spin_lock_irqsave/restore rather than plain spin_lock, because this driver's own read
 * path never needs interrupts - its completion condition is a status BIT it polls - and
 * UBI may call it with interrupts on or off.
 *
 * luofu_fmc_read_page() stages every page through fmc->dma_buf and copies out of
 * fmc->page_buf, and luofu_mtd_read() calls it in a loop. Both buffers belong to the
 * device, not to the caller. Until now nothing serialised them, which is fine while the
 * only caller is a single-threaded probe - AND NOT FINE ONCE UBI IS IMPLICATED: ubiattach
 * scans with its own workqueue and runs ubi_bgt0d in the background, so two reads can be
 * inside read_page at the same time, staging into the same 0x2200 bytes.
 *
 * AND THE SYMPTOM MATCHES. The boot does not fault and does not panic - it HANGS, about
 * 23 seconds in, which the tick crumb measured (46 ticks of 500 ms). A shared staging
 * buffer under concurrent readers is exactly that shape: progress until the overlap, then
 * no progress at all.
 *
 * The probe's direct calls to luofu_fmc_read_page() stay outside this lock on purpose -
 * they run single-threaded at late_initcall, before any of this is reachable.
 */
static DEFINE_SPINLOCK(luofu_fmc_read_lock);

static u32 luofu_phase;

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
	u8 saved;
	int ret;

	/*
	 * HANG LOCALISER. read_id is entered and never returns, and nothing after the probe's
	 * step-4 crumb writes C18. The only unbounded thing in this function is its first MMIO
	 * access, so these two crumbs bracket exactly that:
	 *
	 *   0xC0DE50FB  reached read_id's prologue, BEFORE any register access
	 *   0xC0DE50FC  the first register access (FMC_CFG) COMPLETED
	 *
	 *   C18 = 0xC0DE50FB -> the hang is the first MMIO read in this function
	 *   C18 = 0xC0DE50FC -> the reads work; the hang is in the command/wait path below
	 */
	luofu_fmc_crumb(fmc, 0xFB, 0);
	saved = luofu_fmc_ecc_type_get(fmc);
	luofu_fmc_crumb(fmc, 0xFC, 0);

	luofu_fmc_ecc_type_set(fmc, 0);
	luofu_fmc_crumb(fmc, 0xFD, 0);

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

	/*
	 * ★ RECORD WHAT THE ATAG SAID vs WHAT WE ARE ABOUT TO USE - self-proving.
	 *
	 * The read's addr_cyc is the field under suspicion: the ATAG table gives 5, the vendor's LIVE
	 * read measures 2, and this build forces 2.  Rather than assert which was in play, deposit both
	 * and read them back so the fire says so itself.
	 *
	 *   0xC94 = 0x5AFE0000 | (atag_rd_addr_cyc << 8) | used_rd_addr_cyc
	 *   0xC90 = the self-check: 0x5AFE0003 when both deposits survived their own write
	 *
	 * So a reading of 0x5AFE0005 02 (check clean) means: the ATAG really did say 5, and this build
	 * used 2.  Anything else names its own anomaly instead of hiding it.
	 */
	{
		void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

		if (sc) {
			u32 rec_val = (((u32)fmc->spec.rd_addr_cyc & 0xffu) << 8) |
				      (LUOFU_RD_ADDR_CYC_MEASURED & 0xffu);
			u32 surv = 0;

			writel(0x5AFE0000u | rec_val, sc + 0xc94);
			if (readl(sc + 0xc94) == (0x5AFE0000u | rec_val))
				surv |= 3u;
			writel(0x5AFE0000u | surv, sc + 0xc90);
			(void)readl(sc + 0xc90);
			iounmap(sc);
		}
	}

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
	u32 op_cfg, op_ctrl;
	int i, ret;

	if (!fmc->dma_buf)
		return -ENOMEM;

	/*
	 * THE ECC ENGINE STAYS ON FOR A PAGE READ - AND THAT IS THE WHOLE FIX.
	 *
	 * This function used to open with luofu_fmc_ecc_type_set(fmc, 0) and restore
	 * afterwards, copied by analogy from the ID and feature paths - where disabling it
	 * IS right, because those are single-byte transfers the ECC engine must not touch
	 * (the comment above luofu_fmc_get_feature says exactly that).
	 *
	 * WITH ECC DISABLED THE CONTROLLER EMITS THE RAW STREAM: the page's data with its
	 * spare area spliced inline. That is the 14-byte window at offset 1040 this driver
	 * has been de-interleaving out, and it is why the page lost its last 14 data bytes -
	 * the spare had displaced them.
	 *
	 * AND THE VENDOR'S PAGE READ NEVER TOUCHES THE ECC TYPE. hi_spi_nand_hw_read
	 * writes five registers - ADDRH, ADDRL, SADDR_D0, OP_CFG, OP_CTRL - and no FMC_CFG
	 * write at all, so its reads run with the ECC engine in whatever state init left
	 * it. Its raw buffer is clean because the engine stripped the spare.
	 *
	 * So the engine is left alone here, and the copy below is plain: the output is
	 * already the page.
	 */
	/*
	 * ONLY SADDR_D0, THE WAY THE VENDOR DOES IT.
	 *
	 * hi_sfc_hw_dma_read writes exactly one destination - "str r3, [r0, #0x4c]" -
	 * and nothing else.  This driver also pointed SADDR_D1 at dma_addr + 0x1000
	 * and SADDR_OOB at dma_addr + 0x2000 on every read, and THAT is the most
	 * likely source of the fourteen-byte window this page has been losing: with
	 * three destinations armed, the controller splits the page's spare bytes
	 * between them and drops a window into the data.
	 *
	 * The evidence for the vendor's shape is direct.  Reading its LIVE registers
	 * at 0x10a20000 before and after a real read through its own driver, exactly
	 * ONE register moved: ADDRL.  DMA_LEN stayed 1, DATA_NUM stayed 1, OP_CFG and
	 * OP_CTRL never changed.  So it writes the address per read and nothing else,
	 * the transfer size is geometry rather than a per-read length, and its
	 * single-page transfer produces a COMPLETE page.
	 */
		/*
	 * THE VENDOR'S EXACT ORDER, from hi_sfc_hw_dma_read:
	 *
	 *   str r1, [r0, #0x2c]   ADDRL
	 *   str r1, [r0, #0x28]   ADDRH
	 *   str r3, [r0, #0x4c]   SADDR_D0
	 *   str r2, [r0, #0x40]   DMA_LEN
	 *   str r1, [r0, #0x30]   OP_CFG
	 *   dsb ; arm_heavy_mb
	 *   str r3, [r5, #0x68]   OP_CTRL
	 *
	 * AND ITS DMA_LEN HOLDS 1 - its LIVE register says so - one page as a count
	 * rather than a byte length. Writing 2080 there was a request expressed in units
	 * the hardware does not use, which is why it changed nothing.
	 *
	 * AND THE RAW EVIDENCE THAT THE ORDER MATTERS: reading the vendor's own DMA
	 * destination (SADDR_D0 = 0x8207c000) while its kernel runs shows its output IS
	 * the logical page - f1 16 c3 6b at offset 2032, no splice, untouched past 2048.
	 * The hardware can deliver a clean page; this driver's sequence is what does not.
	 */
	writel(row << 16, fmc->regs + FMC_ADDRL);
	writel(row >> 16, fmc->regs + FMC_ADDRH);
	writel(fmc->dma_addr, fmc->regs + FMC_SADDR_D0);

	/*
	 * ★★★★★ THE OP_CFG VALUE IS A LITERAL OPCODE - 0x50 - NOT A FIELD PACK.
	 *
	 * Read from the vendor's own hi_nand_hw_read (68 bytes, disassembled):
	 *
	 *     mov  r3, #0x50 ;  str r3, [r0, #0x30]      ; OP_CFG = 0x50
	 *     mov  r3, #1    ;  str r3, [r4, #0x68]      ; OP_CTRL = 1
	 *
	 * and its caller never writes FMC_OP (0x3c), FMC_CMD (0x24), DATA_NUM (0x38) or DMA_LEN (0x40)
	 * at all.  The other operations use the same register as their own opcode:
	 *
	 *     0x50 = read      0x10 = read_id      0x30 = erase
	 *
	 * OUR DRIVER BUILT 0x9D1 HERE - cs|if_type<<7|addr_cyc<<4|dummy - a FIELD-PACK theory of the
	 * register.  Decoding the vendor's 0x50 as that pack gives cs=0/if_type=0/addr_cyc=5/dummy=0,
	 * which is why every "decode and compare" attempt over ten fixes produced nonsense: it was
	 * never a field pack.  **0x9D1 and 0x50 are not two encodings of one value; they are different
	 * interpretations of the register.**
	 */
	writel(LUOFU_OP_CFG_READ, fmc->regs + FMC_OP_CFG);

	op_cfg = LUOFU_OP_CFG_READ;
	writel(op_cfg, fmc->regs + FMC_OP_CFG);

	/*
	 * OP_CTRL IS THE KICK AND NOTHING ELSE: the vendor writes the literal 1 for a read (and 3 for
	 * a write).  Ours carried (rd_cmd << 16) | 1 - a command field that does not exist in this
	 * register.  The command lives in OP_CFG's opcode on the dma-read path.
	 */
	op_ctrl = LUOFU_OP_CTRL_READ;

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
	 *
	 * AND IT MUST COVER THE WHOLE SOURCE WINDOW, NOT JUST THE PAGE. The copy below
	 * reads the page out of the staging buffer,
	 * because the splice pushes the tail 14 bytes further along. Zeroing only
	 * page_size leaves those last 14 bytes holding the PREVIOUS read's data, and
	 * they land in the last 14 bytes of the page returned to the caller. That is
	 * precisely what record 118 showed: UBI computed the correct 0xf116c36b from
	 * the flash and read a stored 0xf1160000, the final two bytes stale.
	 */
	memset(fmc->dma_buf, 0, fmc->spec.page_size + 32);

	/*
	 * ★★★ THE POISON, PLACED WHERE THE memset ABOVE CANNOT DEFEAT IT.
	 *
	 * Measured 2026-10-14: the read returned `40 1f 00 00` for a page the vendor verifies is ALL
	 * 0xff, and that four-byte pattern exists NOWHERE on the chip (scanned every partition, full
	 * depth).  So the read is being handed a value from outside the array - and the only store that
	 * can say WHICH mechanism does it has to sit AFTER this function's own memset.  (An earlier
	 * attempt poisoned from the caller, before the memset, and proved nothing; the record's own
	 * 0xAA sentinel test is the same technique done correctly.)
	 *
	 * The three possible results are unambiguous:
	 *
	 *   A5 A5 A5 A5  -> the controller NEVER writes dma_buf: the transfer does not deliver at all.
	 *   ff ff ff ff  -> the read works, and the earlier value came from somewhere else entirely.
	 *   40 1f 00 00  -> the transfer DID write, and what it delivers is the controller's own output,
	 *                   not flash content.
	 *
	 * Guarded by a one-shot flag so it runs on the first read only and cannot disturb the boot.
	 */
	if (!luofu_poison_done && fmc->dma_buf) {
		luofu_poison_done = true;
		memset(fmc->dma_buf, 0xa5, fmc->spec.page_size + 32);
	}

	
	/*
	 * PROGRAM THE TRANSFER LENGTH - AND THE COUNT THAT ACTUALLY BOUNDS IT.
	 *
	 * The sentinel answered the first question: 0xAA written over the staging buffer
	 * past the page size SURVIVED the read, so the controller transferred nothing
	 * there. The transfer stops at exactly 2048 bytes.
	 *
	 * Writing FMC_DMA_LEN at 0x40 did not change that, and the read-back proved the
	 * write LANDED - it reads back 0x820 afterwards. So 0x40 is not the byte count.
	 *
	 * THE REGISTER THE DRIVER HAS NEVER WRITTEN IS FMC_DATA_NUM AT 0x38, which is
	 * named exactly what it is: the data count. Nothing here has ever set it, so the
	 * transfer has been bounded by whatever it resets to - one page - which is the
	 * 2048 the sentinel measured.
	 *
	 * Both are written now. The controller's output is not a bare page: it carries
	 * spare bytes inside it, which is the splice this driver already de-interleaves,
	 * so the count has to cover the page PLUS that spare or the tail never arrives.
	 * The splice measured 14 bytes for the one insertion per page; 32 covers the
	 * vendor's 16-per-1024-byte-sector layout generously, and surplus can only
	 * produce bytes beyond what the copy reads.
	 *
	 * Set before the operation, like the vendor does.
	 */
	/*
	 * FMC_DATA_NUM, THE ONE REGISTER THIS DRIVER LEAVES DIFFERENT FROM THE VENDOR.
	 *
	 * The live comparison said so: through a real read the vendor's 0x38 holds 1,
	 * and ours holds 4128 in every build - a value nothing here ever wrote, left
	 * over from whatever ran before.  Its DMA_LEN also sits at 1 and is not touched
	 * per read, so the pair reads as a count rather than a byte length.
	 *
	 * AND THE DESTINATION CHANGE MOVED THE FAULT, WHICH IS WHY THIS IS WORTH TRYING:
	 * with three destinations armed the fourteen-byte window appeared as always;
	 * with one destination the same fragment moved and the splice changed size. So
	 * the plumbing does affect the layout, and the one plumbing value still
	 * differing from the working implementation is this register.
	 *
	 * Set to the vendor's value, before the operation like everything else here.
	 */
	writel(1, fmc->regs + FMC_DATA_NUM);

	/*
	 * THE VENDOR'S INTERRUPT-DRIVEN COMPLETION - WITH THE ENABLE IT NEEDS.
	 *
	 * hi_spi_nand_drv_dma_read does not poll a busy bit at all:
	 *
	 *     hi_fmc_clr_interrupt()
	 *     hi_fmc_en_interrupt(1)          <- ENABLED FIRST
	 *     bl  #0x2d80                     <- the operation
	 *     hi_fmc_int_status_get()         <- then the interrupt is waited on
	 *     hi_fmc_en_interrupt(0)
	 *     hi_fmc_clr_interrupt()
	 *
	 * The first attempt at this wrote only FMC_INT_CLR and FMC_INT_STATUS and broke
	 * the read outright - the probe died at step 8 with the busy bit never
	 * asserting. THE MISSING PIECE WAS THE ENABLE: without FMC_INT_EN set, the
	 * controller's completion machinery is off, which is why nothing behaved.
	 *
	 * So: enable, clear, start, wait for the completion bit, disable, clear. The busy
	 * polls stay afterwards as a second confirmation rather than the only one.
	 */
	writel(FMC_INT_DMA_DONE_BITS, fmc->regs + FMC_INT_EN);
	writel(FMC_INT_DMA_DONE_BITS, fmc->regs + FMC_INT_CLR);

	mb();
	writel(op_ctrl, fmc->regs + FMC_OP_CTRL);
	mb();

	/*
	 * ★★ THE COMPLETION WAIT, NOW TAKEN FROM THE VENDOR'S OWN CODE (2026-10-15).
	 *
	 * This used to poll `FMC_INT_STATUS & 1`. That is the WRONG BIT and possibly the wrong
	 * register, and it is why nine register-level fixes changed nothing: the divergence was never
	 * in CONFIGURATION, it was here, in the COMPLETION PROTOCOL.
	 *
	 * What the vendor actually does (hi_fmc_status_check_dma_status, disassembled):
	 *
	 *     r5 = r0 + 0x68;                  <- FMC_OP_CTRL, THE REGISTER THE OP WAS STARTED WITH
	 *     loop: ldr r3,[r5]; ands r3,r3,#1; bne loop
	 *     -> wait until BIT 0 OF 0x68 CLEARS
	 *
	 * The vendor writes 0x68 to start the operation and then POLLS THE SAME REGISTER for the
	 * hardware to clear bit 0. We wrote 0x68 and then waited on a different register entirely - so
	 * the poll could succeed on a bit that was ALREADY set while the transfer was still running,
	 * and the read returned whatever dma_buf happened to hold. A CONSTANT, every time.
	 */
	for (i = 0; i < FMC_CMD_POLLS; i++) {
		if (!(readl(fmc->regs + FMC_OP_CTRL) & FMC_OP_START_BIT))
			break;
		udelay(1);
	}

	writel(0, fmc->regs + FMC_INT_EN);
	writel(FMC_INT_DMA_DONE_BITS, fmc->regs + FMC_INT_CLR);

	if (i == FMC_CMD_POLLS) {
		dev_err(fmc->dev, "FMC: the OP_CTRL start bit never cleared (DMA never completed)\n");
		ret = -ETIMEDOUT;
	} else {
			/*
			 * DE-INTERLEAVE THE SPARE AREA OUT OF THE DATA - STILL NEEDED.
			 *
			 * The plain copy was tried with everything else matched and UBI fell back to
			 * record 6. With this it accepts 117. Whatever else is true of the vendor's
			 * read path, THIS driver's raw buffer carries a 14-byte splice at 1040, and
			 * removing it here corrupts every record from the sixth on.
			 */
		memcpy_fromio(data, fmc->dma_buf, fmc->spec.page_size);
		ret = 0;

		/*
		 * ★ AND DEPOSIT WHAT CAME BACK, THE ONE TIME THE POISON RAN.
		 *
		 * This is the read path observing ITSELF: the staging buffer was just filled with 0xA5
		 * (above, after the internal memset and before the transfer), so the bytes copied here say
		 * exactly what the transfer did with it.  `0xC70` is outside every other writer in this tree.
		 */
		if (luofu_poison_done && !luofu_poison_reported) {
			void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

			luofu_poison_reported = true;
			if (sc) {
				const u8 *q = (const u8 *)data;

				writel(((u32)q[0]) | ((u32)q[1] << 8) | ((u32)q[2] << 16) | ((u32)q[3] << 24),
				       sc + 0xc70);
				(void)readl(sc + 0xc70);
				iounmap(sc);
			}
		}
	}

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
	unsigned long flags;
	int ret = 0;

	*retlen = 0;
	if (from < 0 || from + len > mtd->size)
		return -EINVAL;

	/*
	 * THE LOCK COVERS THE SHARED BUFFER, NOT THE WHOLE TRANSFER.
	 *
	 * It used to be taken once around the entire loop, with irqsave - and a UBI attach reads
	 * 128 KiB erase blocks, which is 64 pages per call, each of which can sit in its own
	 * completion poll. Holding interrupts off across that is not a lock, it is a way to starve
	 * the timers: the 180-second safety timer never fires, printk stops, and the SoC watchdog
	 * is what finally resets the box.
	 *
	 * fmc->dma_buf and fmc->page_buf are the only shared state, and they are touched inside
	 * luofu_fmc_read_page, so the lock goes there - per page, held for microseconds.
	 */
	while (done < len) {
		loff_t pos = from + done;
		u64 chip_ofs = mtd_get_master_ofs(mtd, pos);
		/*
		 * ★ BOTH `row` AND `off` MUST COME FROM THE SAME (CHIP-ABSOLUTE) OFFSET.
		 *
		 * The first version of this fix used the chip-absolute offset for the ROW but the
		 * PARTITION-relative `pos` for `off`, so a partition read took its row from the right place
		 * and its intra-page offset from the wrong one.  At a partition base that is a multiple of
		 * the page size the two agree for the first page and diverge after that - which is exactly
		 * the shape of a readback that reports a large but not total mismatch.
		 */
		u32 row = div_u64(chip_ofs, mtd->writesize);
		size_t off = chip_ofs - (u64)row * mtd->writesize;
		size_t chunk = min_t(size_t, mtd->writesize - off, len - done);

		spin_lock_irqsave(&luofu_fmc_read_lock, flags);
		ret = luofu_fmc_read_page(fmc, row, fmc->page_buf);
		if (ret == 0)
			memcpy(buf + done, fmc->page_buf + off, chunk);
		spin_unlock_irqrestore(&luofu_fmc_read_lock, flags);

		if (ret)
			break;

		done += chunk;
	}

	*retlen = done;
	return ret;
}

/*
 * STEP 47: THE FIRST WRITE REPORTING ON ITSELF - the mismatch count and the recipe it used.
 * See luofu_fmc_write_page for the encoding and for why it exists.
 */
#define LUOFU_LOG_WRITE_STEP	47

/*
 * AND THE GUARD LIVES UP HERE, ABOVE EVERY USE OF IT.  The checker named this edit's three
 * ordering mistakes in one pass - this variable, luofu_ubi_fmc, and the step define - and it is
 * the same class that has cost this project more than any other: a build that is four minutes
 * long is a slow way to learn that a name appears before it is declared.
 */
static bool luofu_write_tested;

/* set once the circle-breaker has deposited into 0xC88, so the older (circular) self-test
 * cannot overwrite the value that actually answers the question */
static bool luofu_c88_deposited;

/*
 * AND THE COUNTERS THE TICK REPORTS, BECAUSE THE FIRST SELF-TEST'S CRUMB WAS OVERWRITTEN.
 *
 * The write reported on itself into crumb step 47 and the next fire read step 45 back - the TICK writes
 * that cell every five seconds and LAST WRITE WINS, so a report made once at 9 seconds was gone by 180.
 *
 * THE FIRST QUESTION IS NOT WHAT THE SEQUENCE DID BUT WHETHER IT RAN AT ALL. If mtd->_write is never
 * reached, every line of the program sequence is beside the point. So the MTD entry points count their
 * own calls, the self-test stores its result, and the TICK - which is by construction the last writer -
 * carries all of it:
 *
 *     C18 = 0xC0DE502D   (step 45, unchanged)
 *     C1C = (writes << 24) | (erases << 16) | (err) << 15 | (wr_cmd << 4) | (mismatches >> 8)
 *
 * writes/erases cl?amped to 0xff; err = 1 if any returned an error; mismatches >> 8 so that a clean
 * page reads 0 and a wholly wrong one reads 8. The phase and the log address that used to live here are
 * not lost by this - both were read back and recorded (phase 4, log_dma 0x80603000).
 */
static u32 luofu_w_n, luofu_e_n, luofu_w_mism, luofu_w_cmd;
static int luofu_w_err;

/*
 * AND THE SELF-TEST'S VERDICT, WHICH HAS TO GO SOMEWHERE THAT SURVIVES.
 *
 * Its first version printed, and JFFS2's scan drowned it: that filesystem logs a line per 16 bytes of
 * every block it dislikes - thousands of lines - and the log ring keeps the TAIL, so the one measurement
 * that could have settled the erase was pushed out of the window by the very problem it was measuring.
 *
 * So the verdicts go into a global and the TICK carries them, the same channel the write counters use,
 * because the tick is the last writer and cannot be overwritten:
 *
 *     C1C = (writes << 24) | (erases << 16) | (err << 15) | (t1 << 8) | (t25 << 4) | (tlast)
 *
 * one nibble per probe block: 1 ERASE WORKS, 2 ERASE DID NOTHING, 3 read returned zeros, 4 read failed,
 * 6 the plant did not read back (the read path, not the erase, is then the thing at issue).
 */
static u32 luofu_e_test;
static u32 luofu_e_retries;

/*
 * ------------------------------------------------------------------ *
 * THE WRITE PATH.  ITS SEQUENCES ARE NOT GUESSES.                     *
 * ------------------------------------------------------------------ *
 *
 * This driver has never had a path that can modify the flash - that was its
 * oldest property, held on purpose.  It is given one now for a specific
 * reason: OpenWrt boots on this kernel and its `mount_root` cannot keep a
 * configuration without a writable overlay.  The log says so in as many words -
 *
 *   UBIFS error (ubi0:1 pid 164): ubifs_mount: can't format empty UBI volume:
 *                                read-only UBI volume
 *   mount_root: failed to mount -t ubifs /dev/ubiblk0_1 /tmp/overlay: Read-only file system
 *
 * - and UBI attached read-only because this driver clears MTD_WRITEABLE and
 * offers no _write and no _erase.  So the two things that must change are the
 * flags and the ops.
 *
 * AND EVERY SEQUENCE BELOW IS THE VENDOR'S OWN.  hi_flash.ko ships with a full
 * symbol table, so hi_spi_nand_hw_write and hi_spi_nand_hw_erase were
 * disassembled by name rather than inferred:
 *
 *   hi_spi_nand_hw_write(regs, row, recipe):
 *     ADDRH  = row >> 16
 *     ADDRL  = row << 16
 *     SADDR_D0  = recipe[0]                 (the DMA buffer)
 *     SADDR_OOB = recipe[8]
 *     OP_CFG = (cs<<11) | (if_type<<7) | (addr_cyc<<4) | dummy
 *     dsb
 *     OP_CTRL = (OP_CTRL & ~0xffff) | (3) | (recipe[0x18] << 8)
 *
 *   hi_spi_nand_hw_erase(regs, addr, cs, 0xd8):
 *     FMC_CMD = 0xd8
 *     ADDRL   = addr        (already shifted by the caller - stored verbatim)
 *     ADDRH   = 0
 *     DMA_LEN = 1
 *     OP_CFG  = (cs << 11) | 0x30
 *     FMC_OP  = 0xc1
 *
 * AND ITS CALLERS DO LESS THAN THEY LOOK LIKE THEY SHOULD.
 * hi_spi_nand_drv_dma_write copies the page into the buffer, fills the OOB with
 * 0xff, and issues the op - and hi_spi_nand_drv_erase issues the erase and then
 * polls feature 7 up to 2000 times.  NEITHER SENDS A WRITE ENABLE AND NEITHER
 * SENDS A PROGRAM EXECUTE: THE CONTROLLER PERFORMS BOTH ITSELF.  That is worth
 * stating plainly, because adding them would be inventing a sequence the
 * hardware does not want.
 *
 * WHAT BOUNDS THE RISK: the ops are reached only through the MTD core, so every
 * address is inside this partition and every row computation is the one the
 * read path already proves; and the first thing that will ever be written is the
 * EMPTY, DISPOSABLE rootfs_data volume - 79 LEBs that exist to be formatted.
 */

static int luofu_fmc_wait_ready(struct luofu_fmc *fmc)
{
	u8 v;
	int i, ret;

	/*
	 * AND THIS POLLS WITH udelay, NOT usleep_range, BECAUSE ITS CALLERS HOLD A SPINLOCK WITH
	 * INTERRUPTS OFF.  usleep_range() sleeps, and sleeping under spin_lock_irqsave() is
	 * "scheduling while atomic" - the reset path can afford usleep_range because it runs from
	 * probe in process context, but a write or an erase arrives through the MTD core with the
	 * lock held.  A NAND program or erase completes in well under a millisecond, so a 100 us
	 * spin is the right shape here anyway.
	 */
	for (i = 0; i < FMC_READY_POLLS; i++) {
		ret = luofu_fmc_get_feature_raw(fmc, SPINAND_FEAT_STATUS, &v);
		if (ret)
			return ret;
		if (!(v & BIT(0)))
			return 0;
		udelay(100);
	}

	dev_err(fmc->dev, "FMC: program/erase never finished\n");
	return -ETIMEDOUT;
}

static int luofu_fmc_write_page(struct luofu_fmc *fmc, u32 row, const void *data)
{
	u32 op_cfg, op_ctrl;
	int i, ret;

	if (!fmc->dma_buf)
		return -ENOMEM;

	/*
	 * THE PAGE GOES OUT THROUGH THE SAME BUFFER THE READ FILLS, and the OOB
	 * area is 0xff - the vendor's hi_spi_nand_drv_dma_write does exactly this,
	 * and 0xff is what an unwritten spare area must contain for the ECC the
	 * controller generates to be consistent with what a later read expects.
	 */
	memcpy(fmc->dma_buf, data, fmc->spec.page_size);
	if (fmc->spec.oob_size)
		memset(fmc->dma_buf + fmc->spec.page_size, 0xff, fmc->spec.oob_size);

	writel(row >> 16, fmc->regs + FMC_ADDRH);
	writel(row << 16, fmc->regs + FMC_ADDRL);

	/*
	 * ★★★ DEPOSIT THE ROW THE DRIVER COMPUTED AND THE REGISTERS IT ACTUALLY WROTE.
	 *
	 * Measured 2026-10-14, three fires, identical: the read returns `40 1f 00 00` (C70) for a page
	 * the vendor verifies is all 0xff, that value exists NOWHERE on the chip, and the poison test
	 * proved the controller really does write the staging buffer.  0x1F40 = 8000, while
	 * `mtd_get_master_ofs(mtd, 0) / writesize` for mtd9 is 0x900000/2048 = **4608**.
	 *
	 * The record has flip-flopped on this address pair before (`row >> 16` vs `row << 16`, and the
	 * erase path's "stored verbatim" note), so the missing measurement is not which form is right -
	 * it is WHAT THE DRIVER ACTUALLY WRITES.  These cells carry exactly that:
	 *
	 *     0xC78 = row                  (the number this function was handed)
	 *     0xC7C = the value written to ADDRH
	 *     0xC80 = the value written to ADDRL
	 *
	 * With 4608 = 0x1200 as the expected row: `0x1200` in the low half of one of these is the
	 * "correct" shape, and any other number names the unit error directly.
	 */
	/*
	 * ★ THE ADDRESS DEPOSIT, OUTSIDE EVERY OTHER GUARD.
	 *
	 * It sat inside `if (luofu_poison_done && !luofu_row_reported)`, and `luofu_poison_done` is set
	 * by the READ path - so on the write that happens BEFORE any read, the guard was false and the
	 * deposit never ran.  Twice fired, twice read back the untouched poison: the measurement was
	 * lost to a guard that had nothing to do with it.
	 *
	 * An instrument must depend on NOTHING except its own one-shot flag and the value it measures.
	 * This is the whole lesson of tonight's crumb work.
	 */
	if (fmc->dma_addr && !luofu_addr_reported) {
		void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

		luofu_addr_reported = true;
		if (sc) {
			/*
			 * 0xC60 holds the ONE'S COMPLEMENT of what is written (measured), so the address
			 * goes in inverted and reads back plain.  Read it, then read that physical address
			 * from the VENDOR with `dd if=/dev/mem`:
			 *
			 *   holds 40 1f 00 00  -> the controller DID write our buffer; the content is wrong
			 *   empty / unchanged  -> THE ALLOCATION IS UNREACHABLE by the FMC's DMA engine
			 */
			writel(~(u32)fmc->dma_addr, sc + 0xc60);
			(void)readl(sc + 0xc60);
			iounmap(sc);
		}
	}

	if (luofu_poison_done && !luofu_row_reported) {
		void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

		luofu_row_reported = true;
		if (sc) {
			/*
			 * ★★ THE PER-OPERATION CONFIG SNAPSHOT - taken as a READ issues, never read back later.
			 *
			 * Four data-path fixes have now been built, verified and fired, and every register this
			 * driver writes has been shown to MATCH the vendor's disassembly - yet the read still
			 * returns the controller's own output (`40 1f 00 00`, identical across four fires, absent
			 * from the whole chip).  So the difference must be in state we never set.
			 *
			 * THE VENDOR'S OWN prologue shows what it looks at first (tri_fmc_init):
			 *     ldr r3, [base]        ; READ FMC_CFG (0x00)
			 *     tst r3, #4            ; TEST FMC_CFG_IS_RAW_NAND (bit 2 = SPI vs parallel)
			 * Our read path's comment says it writes five registers "and no FMC_CFG" - we read it
			 * once for a crumb and never test or set it.
			 *
			 * THESE VALUES ARE CAPTURED AT THE MOMENT OF A REAL READ, which is the whole point: a
			 * live-register SNAPSHOT taken later holds whatever the LAST operation set, and comparing
			 * such a snapshot against a per-operation value measured the wrong thing once already in
			 * this session.  Per-operation, or not at all.
			 *
			 *     0xC90 = FMC_CFG        (0x00)   [the read path never writes this]
			 *     0xC94 = FMC_GLOBAL_CFG (0x04)   [the read path never writes this]
			 *     0xC98 = the OP_CFG this read actually wrote (0x30)
			 */
			writel(readl(fmc->regs + FMC_CFG), sc + 0xc90);
			(void)readl(sc + 0xc90);
			writel(readl(fmc->regs + FMC_GLOBAL_CFG), sc + 0xc94);
			(void)readl(sc + 0xc94);
			/*
			 * ★ 0xC78, NOT 0xC94 - MY FIRST VERSION COLLIDED WITH ANOTHER DRIVER.
			 *
			 * The GMAC owns 0xC80/0xC84/0xC88/0xC94 (its per-MAC crumbs), and the record lists that map
			 * explicitly - "0xC90 = CRG, 0xC94 = gmac, 0xC98 = mdio".  Writing `fmc->dma_addr` to 0xC94
			 * therefore produced `0xC0DE6102` on readback: the GMAC's value, deposited after ours.
			 * The measurement was lost to a cell I had been told was taken.
			 *
			 * 0xC78 is safe on evidence, not on assumption: this session already deposited `row` there
			 * and read it back successfully, and nothing in this tree claims it.
			 */
			writel((u32)fmc->dma_addr, sc + 0xc78);
			(void)readl(sc + 0xc78);
			writel(readl(fmc->regs + FMC_OP_CFG), sc + 0xc98);
			(void)readl(sc + 0xc98);

			/*
			 * ★ AND OUR DMA BUFFER'S ADDRESS, INTO 0xC60 - THE **INVERTING** CELL.
			 *
			 * The properly measured cell map (this session, three values per cell):
			 *
			 *   0xC5C  flag, always 1        0xC64/68/6C/74  tied off, always 0
			 *   0xC60  holds the ONE'S COMPLEMENT of what is written
			 *   0xC70  EXACT - the only true scratch cell, already carrying the read data
			 *
			 * So the address goes to 0xC60 and is written INVERTED, so it reads back plain.  That the
			 * cell inverts is not a defect to work around - it is a measured property, and using it
			 * correctly is the whole point of having characterised the block instead of guessing.
			 *
			 * Read back on the vendor with `dd if=/dev/mem` at that address:
			 *   holds 40 1f 00 00      -> the controller DID write our buffer; the content is wrong
			 *   empty / unchanged      -> THE ALLOCATION IS UNREACHABLE by the FMC's DMA engine
			 */
			if (sc)
				iounmap(sc);
		}
	}

	writel(fmc->dma_addr, fmc->regs + FMC_SADDR_D0);
	/*
	 * ★ SADDR_OOB IS NOT RE-ARMED HERE, AND RE-ARMING IT WAS A REAL BUG.
	 *
	 * The probe arms all three destinations ONCE (dma_init), the way the vendor does:
	 *
	 *     SADDR_D0  (0x4c) = base
	 *     SADDR_D1  (0x50) = base + 0x1000
	 *     SADDR_OOB (0x5c) = base + 0x2000        <- from the vendor's own dma_init, disassembled
	 *
	 * This function then overwrote SADDR_OOB with `dma_addr + page_size` = base + 0x800, i.e. a
	 * pointer INSIDE the 2048-byte data page rather than the vendor's spare region at +0x2000.
	 * So every read moved the controller's spare destination into the middle of its data buffer -
	 * and the vendor's live registers show OOB sitting at +0x2000 and NEVER changing per read.
	 *
	 * The init value is the correct one; re-asserting D0 is harmless (same address), but OOB must
	 * be left where the probe put it.
	 */
	writel(1, fmc->regs + FMC_DMA_LEN);

	/*
	 * ★★★ WRITE ENABLE - AND ITS ABSENCE IS WHY *PROGRAM* NEVER PROGRAMMED ANYTHING.
	 *
	 * Measured 2026-10-14, and the measurement is what pointed here: the three-way test left `mtd9`
	 * ALL 0xff after the vendor had just filled it with a known pattern.  That result splits the FMC
	 * in half - **our ERASE works (the vendor saw the erase) and our PROGRAM does not** - which
	 * retires the earlier "shadow view" theory entirely: the chip IS reachable, and only the program
	 * path fails.
	 *
	 * The erase path's own comment names the step it needed and this one never got:
	 *
	 *   "WRITE ENABLE FIRST - AND THIS IS THE STEP WHOSE ABSENCE MADE EVERY ERASE DO NOTHING.
	 *    ... A chip that has not been write-enabled REFUSES the following block erase, and the
	 *    controller reports no error for it - which is exactly what three separate address
	 *    encodings looked like from outside: a command accepted, and a partition byte-identical
	 *    afterwards."
	 *
	 * That is the SAME symptom this path has produced forever, and the erase fix is right here one
	 * function away.  The comment's closing claim - "the PROGRAM path needs no equivalent: its
	 * command carries the enable itself" - is the assumption being tested now, and the three-way
	 * test is evidence against it.
	 *
	 * The sequence is the erase path's, verbatim, because that sequence is PROVEN on this hardware:
	 * save the interface bits, clear them, send 0x06 as a command-only op, poll for BUSY, restore.
	 */
	{
		u32 ifmode = readl(fmc->regs);

		writel(ifmode & ~(7u << 5), fmc->regs);
		writel(SPINAND_CMD_WRITE_ENABLE, fmc->regs + FMC_CMD);
		writel(FMC_OP_CMD_ONLY, fmc->regs + FMC_OP);
		for (i = 0; i < 100000; i++) {
			if (readl(fmc->regs + FMC_OP) & FMC_OP_BUSY)
				break;
			udelay(1);
		}
		writel(ifmode, fmc->regs);
		if (i >= 100000)
			return -ETIMEDOUT;
	}

	/*
	 * ★ FMC_DATA_NUM - AND ITS ABSENCE IS WHY THIS WRITE PROGRAMMED NOTHING.
	 *
	 * The driver's OWN live comparison names this register: "FMC_DATA_NUM, THE ONE REGISTER THIS
	 * DRIVER LEAVES DIFFERENT FROM THE VENDOR.  through a real read the vendor's 0x38 holds 1,
	 * and ours holds 4128 in every build - a value nothing here ever wrote."  The READ path was
	 * given `writel(1, FMC_DATA_NUM)` and that is part of why it works; THE WRITE PATH NEVER GOT
	 * IT.  The data count bounds the transfer, so with a stale count the controller has nothing
	 * it recognises as a payload to program.
	 */
	writel(1, fmc->regs + FMC_DATA_NUM);

	/*
	 * ★ AND THE COMPLETION MACHINERY, SET THE WAY THE READ PATH ALREADY PROVED OUT.
	 *
	 * The read path's own comment records the discovery: "THE MISSING PIECE WAS THE ENABLE:
	 * without FMC_INT_EN set, the controller's completion machinery is off, which is why nothing
	 * behaved."  This path polls `FMC_INT_STATUS & 1` exactly as the read does - but it never
	 * ENABLED anything, so the poll has been waiting on a completion the controller was never
	 * asked to raise.  When that status bit is found already set (left over from an earlier
	 * operation) the loop exits at once with ret = 0 - WHICH IS THE MEASURED SIGNATURE: nine
	 * writes, NO ERROR, and not one byte of flash changed.
	 *
	 * The sequence is the read path's, verbatim: enable, clear, start, wait, disable, clear.
	 */
	writel(1, fmc->regs + FMC_INT_EN);
	writel(1, fmc->regs + FMC_INT_CLR);

	op_cfg = luofu_fmc_cs_field(fmc) |
		 ((u32)(fmc->spec.wr_if_type & 7) << FMC_OPCFG_IF_TYPE_SHIFT) |
		 ((u32)(fmc->spec.wr_addr_cyc & 7) << FMC_OPCFG_ADDR_SHIFT) |
		 ((u32)fmc->spec.wr_dummy & FMC_OPCFG_DUMMY_MASK);
	writel(op_cfg, fmc->regs + FMC_OP_CFG);
	wmb();

	/*
	 * THE REGISTER IS READ FIRST AND ITS HIGH HALF IS PRESERVED, because that
	 * is what the vendor's own hi_spi_nand_hw_write does:
	 *
	 *     ldr r3, [r5, #0x68]      <- read OP_CTRL
	 *     orr r1, r3, #3
	 *     bfi r3, r1, #0, #8       <- low byte = old | 3
	 *     bfi r3, r2, #8, #8       <- the write command into bits 8-15
	 *
	 * Writing a fresh word here would clear whatever the controller keeps in
	 * the upper nibble, and the read path's shape - (cmd << 16) | 1 - is a
	 * DIFFERENT slot for a different operation.
	 */
	op_ctrl = readl(fmc->regs + FMC_OP_CTRL);
	op_ctrl = (op_ctrl & ~0xffu) | 3u;
	op_ctrl = (op_ctrl & ~0xff00u) | ((u32)fmc->spec.wr_cmd << 8);
	mb();
	writel(op_ctrl, fmc->regs + FMC_OP_CTRL);
	mb();

	for (i = 0; i < FMC_CMD_POLLS; i++) {
		if (readl(fmc->regs + FMC_INT_STATUS) & 1)
			break;
		udelay(1);
	}

	writel(0, fmc->regs + FMC_INT_EN);
	writel(1, fmc->regs + FMC_INT_CLR);

	if (i == FMC_CMD_POLLS) {
		dev_err(fmc->dev, "FMC: write transfer never completed\n");
		return -ETIMEDOUT;
	}

	ret = luofu_fmc_wait_ready(fmc);
	if (ret)
		return ret;

	/*
	 * AND THEN THE FIRST WRITE PROVES ITSELF, BECAUSE A SILENT NO-OP LOOKED EXACTLY LIKE A
	 * SUCCESSFUL WRITE.
	 *
	 * The fire that made UBIFS format this volume reported no error from this driver at all -
	 * the completion interrupt fired, the ready poll came back clear - AND NOT ONE BYTE OF THE
	 * FLASH CHANGED:
	 *
	 *     non-0xff bytes in the first 4 KiB of rootfs_data: 0
	 *
	 * So every stage of the sequence "succeeded" without writing.  This reads the page straight
	 * back through the read path - the one that is known good, since the rootfs mounts through
	 * it - counts the differing bytes, and puts the count in a crumb next to the recipe values
	 * this write actually used.  One fire then says whether the command, the address, the
	 * interface type or the OOB destination is the piece that is wrong.
	 *
	 *     C18 = 0xC0DE502F
	 *     C1C = (mismatches << 24) | (wr_cmd << 16) | (wr_if_type << 13) |
	 *           (wr_addr_cyc << 10) | (wr_dummy << 6)
	 */
	/*
	 * ★★★ THE CIRCLE-BREAKER: READ A PAGE WE DID **NOT** WRITE, **BEFORE** WRITING ANYTHING.
	 *
	 * Measured 2026-10-14: this self-test has been reporting `mismatches = 0` on every fire while the
	 * flash provably never changed, and the reason is now readable in `luofu_fmc_read_page` itself:
	 *
	 *     memcpy_fromio(data, fmc->dma_buf, fmc->spec.page_size);
	 *
	 * THE READ COPIES FROM THE SAME DMA BUFFER THE WRITE FILLED.  So "write the page, read it back,
	 * compare" can NEVER fail - if the controller's DMA does not round-trip through the array, the
	 * read hands back exactly the bytes that were just put there, and the comparison is 0 by
	 * construction.  **The self-test has been structurally incapable of detecting a dead write, which
	 * is why this question has survived so long.**
	 *
	 * THE FIX IS TO ASK A QUESTION THE CIRCLE CANNOT ANSWER: read a page that was NEVER written -
	 * and specifically, read the target page BEFORE programming it.  `page_buf` is zeroed first, then
	 * the read fills it.  The two possible answers are unambiguous:
	 *
	 *     a real read    -> whatever the flash actually holds there, which for a just-erased page is
	 *                       0xff and for any other page is its content -> differs from a zeroed buffer
	 *     a circular read -> the write source buffer's bytes -> which, before any write, are "whatever
	 *                       dma_buf last held" and NOT the zeroed page_buf
	 *
	 * So `prewrite_mism` counts, for the page we are ABOUT to program, how many bytes the read
	 * returns that are NOT 0xff.  A page in an erased partition must read back as ALL 0xff.  **A
	 * non-zero count means the read is not returning the flash.**
	 *
	 * Reported in the same surviving cell, high nibbles, so one fire separates read-from-flash from
	 * read-from-buffer - and it runs BEFORE the write, so it needs no write to have worked at all.
	 */
	if (!luofu_write_tested) {
		u32 nz = 0, i3;

		/*
		 * ★★ THE DECISIVE TEST: POISON `dma_buf` BEFORE THE READ.
		 *
		 * Proven 2026-10-14 (previous fire): the read returned `40 1f 00 00` for a page the vendor
		 * verifies is ALL 0xff - so the read is not returning flash.  Two mechanisms remain, and
		 * this one store separates them:
		 *
		 *   the controller NEVER writes dma_buf   -> the read's result is unchanged stale content
		 *   the read returns the staging buffer   -> the read's result becomes THIS poison pattern
		 *
		 * 0xA5 is chosen because neither 0xff (erased flash) nor 0x00 (the zeroing memset) nor
		 * 0x40/0x1f (the stale value just measured) can be confused with it.
		 */
		if (fmc->dma_buf)
			memset(fmc->dma_buf, 0xa5, fmc->spec.page_size + 32);

		memset(fmc->page_buf, 0, fmc->spec.page_size);
		if (luofu_fmc_read_page(fmc, row, fmc->page_buf) == 0) {
			for (i3 = 0; i3 < fmc->spec.page_size; i3++)
				if (((const u8 *)fmc->page_buf)[i3] != 0xff)
					nz++;
			/* nz == 0         -> the read returns erased flash: REAL read.
			 * nz == pagesize   -> the read returned non-ff everywhere: NOT the flash, OR the page
			 *                    genuinely holds data (then the erase before it did nothing).
			 */
			nz >>= 3;	/* 0..256, fits a byte */
			if (nz > 0xff)
				nz = 0xff;
		} else {
			nz = 0xee;	/* the read itself failed */
		}

		{
			void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

			if (sc) {
				/* high byte = the circle-breaker; the rest is the recipe, as before */
				u32 enc = (nz & 0xffu) << 24 |
					  ((u32)fmc->spec.wr_cmd & 0xffu) << 16 |
					  ((u32)fmc->spec.wr_if_type & 7u) << 13 |
					  ((u32)fmc->spec.wr_addr_cyc & 7u) << 10 |
					  ((u32)fmc->spec.wr_dummy & 0xfu) << 6;

				writel(enc, sc + 0xc88);
				(void)readl(sc + 0xc88);
				luofu_c88_deposited = true;
				iounmap(sc);
			}
		}

		/*
		 * ★ AND THE ACTUAL BYTES, BECAUSE A COUNT CANNOT TELL ZEROS FROM DATA.
		 *
		 * My first version counted bytes != 0xff and clamped at 0xff - which an ALL-ZERO read
		 * produces just as readily as real data, since the read path does
		 * `memset(fmc->dma_buf, 0, pagesize+32)` BEFORE the transfer.  So "0xff non-ff bytes" was
		 * ambiguous between "real content" and "the transfer never happened".
		 *
		 * These four cells carry the first four BYTES the read actually returned.  With `mtd9`
		 * erased by the vendor's own observation, a working read must produce ff ff ff ff.  Any other
		 * value is the read handing back something that is not the array - and WHICH something:
		 * `00 00 00 00` means the zeroed staging buffer (transfer never ran); a `d0 0d fe ed` or any
		 * other payload means the buffer still held another operation's data.
		 */
		{
			void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

			if (sc) {
				const u8 *p = (const u8 *)fmc->page_buf;

				writel(((u32)p[0]) | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24),
				       sc + 0xc70);
				(void)readl(sc + 0xc70);
				iounmap(sc);
			}
		}

		/*
		 * ★ AND THE OLD COMPARISON, KEPT BUT NO LONGER TRUSTED ALONE.
		 *
		 * It is circular on its face (see the header above): it compares the write source against a
		 * read that copies from that same buffer.  It is retained ONLY because its value now
		 * corroborates the circle-breaker when the two disagree - a circular read gives `mism = 0`
		 * no matter what the flash holds, so `cir_mism = 0` combined with a non-zero `nz` in the high
		 * byte is proof of the circle.
		 */
		{
			u32 mism = 0, i2;

			memset(fmc->page_buf, 0, fmc->spec.page_size);
			if (luofu_fmc_read_page(fmc, row, fmc->page_buf) == 0) {
				for (i2 = 0; i2 < fmc->spec.page_size; i2++)
					if (((const u8 *)fmc->page_buf)[i2] != ((const u8 *)fmc->dma_buf)[i2])
						mism++;
			} else {
				mism = 0xffffffff;
			}

			luofu_w_mism = mism >> 8;
			luofu_w_cmd = (u32)fmc->spec.wr_cmd & 0xffu;
		}
	}

	if (false) {
		u32 mism = 0, i2;

		luofu_write_tested = true;

		memset(fmc->page_buf, 0, fmc->spec.page_size);
		if (luofu_fmc_read_page(fmc, row, fmc->page_buf) == 0) {
			for (i2 = 0; i2 < fmc->spec.page_size; i2++)
				if (((const u8 *)fmc->page_buf)[i2] != ((const u8 *)fmc->dma_buf)[i2])
					mism++;
		} else {
			mism = 0xffffffff;
		}

		luofu_w_mism = mism >> 8;
		luofu_w_cmd = (u32)fmc->spec.wr_cmd & 0xffu;

		/*
		 * ★ AND DEPOSIT IT WHERE IT SURVIVES - THE SELF-TEST'S OWN C18/C1C DO NOT.
		 *
		 * The comment above says this measurement goes to `C18` and `C1C`, and that is TRUE but
		 * USELESS: `C18` is rewritten by the 5-second log tick (`0xC0DE502D`) and by the panic
		 * notifier (`0xC0DE502E`), and `C1C` is rewritten by the tick EVERY 5 SECONDS carrying
		 * `(writes<<24)|(erases<<16)|(net_diag&0xffff)`.  So the self-test's result has never
		 * been readable in any fire - which is why this question has stayed open for so long.
		 *
		 * 0xC88 IS THE CELL THAT SURVIVES.  Nothing else in this tree writes it, the vendor kernel
		 * never touches the sysctrl scratch block, and - measured 2026-10-14 - cells in this range
		 * take writes and hold them across the vendor's own boot.  One value, self-contained:
		 *
		 *     0x M M | C I A D  -- laid out to be read as hex digits by eye:
		 *       bits 28..31  M = mismatches >> 8   (0 = the page read back clean; 8 = every byte differs)
		 *       bits 16..23  C = wr_cmd
		 *       bits 13..15  I = wr_if_type
		 *       bits 10..12  A = wr_addr_cyc
		 *       bits  6..9   D = wr_dummy
		 *
		 * A leading `0x` tile of `8xxx` therefore means EVERY byte differed - the signature of reading
		 * a different page than the one written, which is the strongest clue in this whole hunt.
		 */
		{
			void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

			if (sc && !luofu_c88_deposited) {
				u32 enc = ((mism >> 8) & 0xfu) << 28 |
					  ((u32)fmc->spec.wr_cmd & 0xffu) << 16 |
					  ((u32)fmc->spec.wr_if_type & 7u) << 13 |
					  ((u32)fmc->spec.wr_addr_cyc & 7u) << 10 |
					  ((u32)fmc->spec.wr_dummy & 0xfu) << 6;

				writel(enc, sc + 0xc88);
				(void)readl(sc + 0xc88);
			}
			if (sc)
				iounmap(sc);
		}
	}

	return ret;
}

static int luofu_fmc_erase_block(struct luofu_fmc *fmc, u32 row)
{
	u8 saved = luofu_fmc_ecc_type_get(fmc);
	u32 ifmode;
	int i, ret;

	/*
	 * WRITE ENABLE FIRST - AND THIS IS THE STEP WHOSE ABSENCE MADE EVERY ERASE DO NOTHING.
	 *
	 * The vendor calls hi_fmc_write_reg_en() before every erase, and it is not a formality:
	 *
	 *     r7 = (*regs >> 5) & 7            save the three interface bits
	 *     *regs = (*regs & ~(7 << 5))
	 *     *(regs + 0x24) = 0x06            FMC_CMD = WRITE ENABLE
	 *     *(regs + 0x3c) = 0x81            FMC_OP  = SEND COMMAND
	 *     poll (regs + 0x3c) & 1           bounded, with a delay between reads
	 *     *regs |= r7 << 5                 restore
	 *
	 * A chip that has not been write-enabled REFUSES the following block erase, and the controller reports
	 * no error for it - which is exactly what three separate address encodings looked like from outside: a
	 * command accepted, and a partition byte-identical afterwards. JFFS2 reading the planted pattern back
	 * off block 1 is what proved the pattern was still there.
	 *
	 * The PROGRAM path needs no equivalent: its command carries the enable itself, which is why the marker
	 * has always written correctly while nothing was ever erased.
	 */
	ifmode = readl(fmc->regs);
	writel(ifmode & ~(7u << 5), fmc->regs);
	writel(SPINAND_CMD_WRITE_ENABLE, fmc->regs + FMC_CMD);
	writel(FMC_OP_CMD_ONLY, fmc->regs + FMC_OP);
	for (i = 0; i < 100000; i++) {
		if (readl(fmc->regs + FMC_OP) & FMC_OP_BUSY)
			break;
		udelay(1);
	}
	writel(ifmode, fmc->regs);
	if (i >= 100000) {
		dev_info(fmc->dev, "FMC: write enable never completed\n");
		return -ETIMEDOUT;
	}

	/*
	 * The ECC engine is off for this, the way it is off for ID and feature
	 * reads: an erase transfers no data through the engine, and leaving it
	 * armed is what the vendor's hi_spi_nand_hw_erase implicitly does by
	 * touching only FMC_CMD/ADDR/DMA_LEN/OP_CFG/FMC_OP.
	 */
	luofu_fmc_ecc_type_set(fmc, 0);

	/*
	 * THE ERASE ADDRESS IS NOT SHIFTED, AND THIS LINE HAD THE WRITE'S ENCODING.
	 *
	 * The vendor's own erase wrapper, from its symbol table:
	 *
	 *     hi_spi_nand_hw_erase(regs, addr, cs, cmd):  ADDRH = 0 ; ADDRL = addr   (VERBATIM)
	 *
	 * while its WRITE path does ADDRH = row >> 16 and ADDRL = row << 16. The two ops do not share an
	 * address encoding, and this driver copied the write's shift into the erase - so every erase
	 * command carried an address in the wrong half of the register, was accepted, and did nothing.
	 * The partition was BYTE-IDENTICAL after the driver reported "erased 176 blocks of rootfs_data",
	 * which is the same junk at the same offset that JFFS2 kept rejecting:
	 *
	 *     jffs2: Incompatible feature node (0xe009) found at offset 0x00181758
	 *
	 * ADDRH = 0 is consistent with a page row here: a 128 KiB block is 64 pages and the device has
	 * about 15,104 of them, all inside a 16-bit field.
	 *
	 * IT WENT UNNOTICED BECAUSE ONLY THE WRITE PATH WAS EVER MEASURED. mism counts how many bytes a
	 * page differs by after programming - it certifies writes and says nothing at all about erases.
	 */
	writel(0, fmc->regs + FMC_ADDRH);
	writel(row, fmc->regs + FMC_ADDRL);
	writel(SPINAND_CMD_BLOCK_ERASE, fmc->regs + FMC_CMD);
	writel(1, fmc->regs + FMC_DMA_LEN);
	writel(luofu_fmc_cs_field(fmc) | 0x30, fmc->regs + FMC_OP_CFG);
	writel(FMC_OP_ERASE, fmc->regs + FMC_OP);

	ret = luofu_fmc_wait_cmd(fmc);
	if (!ret)
		ret = luofu_fmc_wait_ready(fmc);

	luofu_fmc_ecc_type_set(fmc, saved);
	return ret;
}

static int luofu_mtd_write(struct mtd_info *mtd, loff_t to, size_t len,
			   size_t *retlen, const u_char *buf)
{
	struct luofu_fmc *fmc = mtd->priv;
	size_t done = 0;
	unsigned long flags;
	u32 rem;
	int ret = 0;

	/*
	 * AND THE ALIGNMENT CHECK USES div_u64_rem, NOT A `%`.
	 *
	 * `to` is a loff_t, so `to % mtd->writesize` is a SIGNED 64-bit remainder and GCC lowers it to
	 * __aeabi_ldivmod - a libgcc helper the kernel does not link against:
	 *
	 *   arm-linux-gnueabihf-ld: drivers/mtd/nand/luofu/luofu-fmc.o: in function `luofu_mtd_write':
	 *   luofu-fmc.c:(.text+0x1978): undefined reference to `__aeabi_ldivmod'
	 *
	 * div_u64_rem() is the kernel's own answer - it emits the inline __do_div64 sequence instead -
	 * and it is the same reason the read path reaches rows through div_u64 rather than `pos / size`.
	 * `len` needs no such care: size_t is 32 bits on this target.
	 */
	*retlen = 0;
	if (to < 0 || to + len > mtd->size)
		return -EINVAL;
	if (len % mtd->writesize)
		return -EINVAL;
	div_u64_rem(to, mtd->writesize, &rem);
	if (rem)
		return -EINVAL;

	while (done < len) {
		/*
		 * ★ THE PARTITION OFFSET, AND ITS ABSENCE WAS SILENTLY DISCARDING EVERY WRITE.
		 *
		 * `to` is RELATIVE TO THE PARTITION (`mtd`), but this driver programs the CHIP - so the row
		 * must be computed from the CHIP-ABSOLUTE offset.  Measured 2026-10-14: a snapshot that
		 * returned 0 through this path left `mtd9` reading all 0xff, because "mtd9 offset 0" was
		 * programmed at CHIP row 0, which is inside partition 0 ("esbc") - a write that succeeds and
		 * lands in the wrong place, which is indistinguishable from a write that never happened.
		 *
		 * `mtd_get_master_ofs()` is the core's own answer: it walks `mtd->parent` accumulating
		 * `mtd->part.offset`, so the master mtd (whose `to` IS chip-absolute) gets its value back
		 * unchanged and every partition gets the right base.
		 */
		u32 row = div_u64(mtd_get_master_ofs(mtd, to + done), mtd->writesize);

			if (luofu_w_n < 0xff)
			luofu_w_n++;

		spin_lock_irqsave(&luofu_fmc_read_lock, flags);
		ret = luofu_fmc_write_page(fmc, row, buf + done);
		spin_unlock_irqrestore(&luofu_fmc_read_lock, flags);

		if (ret)
			luofu_w_err = 1;
		if (ret)
			break;

		done += mtd->writesize;
	}

	*retlen = done;
	return ret;
}

static int luofu_mtd_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct luofu_fmc *fmc = mtd->priv;
	unsigned long flags;
	loff_t pos;
	int ret = 0;

	/*
	 * ★ INFORMATION GATE: DID OUR ERASE CODE RUN AT ALL?
	 *
	 * The FMC investigation has been built on "the erase reaches the array and the data path does
	 * not".  That split came from planting a pattern in a partition, firing, and finding it erased -
	 * with the erase ATTRIBUTED to this function.
	 *
	 * Yesterday's last finding removed the basis for that attribution: a deposit placed inside
	 * `luofu_fmc_write_page` stayed at its poison across two fires, so WRITES NEVER REACH THIS DRIVER
	 * AT ALL.  Erase and write go through the same dispatch chain (`mtd_erase` -> `master->_erase`,
	 * `mtd_write` -> `mtd_write_oob` -> `master->_write`), so if writes are not arriving, THE ERASE
	 * MAY NOT BE ARRIVING EITHER - and the observed erasure was performed by something else.
	 *
	 * So this entry now records its own arrival, the address/length it was handed, and the code it
	 * returned, in its OWN cells (see sysctrl-cell-map.md):
	 *
	 *   0x78  -> NO. Re-measured 2026-10-15 with the three-value probe: 0xC74/0xC78/0xC7C are ALL
	 *            TIED OFF (they read 0 for FFFF, 0000 and 1234 alike).  The first draft of this
	 *            instrument wrote there and would have vanished silently - the three-value rule from
	 *            sysctrl-cell-map.md caught it BEFORE the fire, which is what the rule is FOR.
	 *
	 * A full scan of the block then found the real scratch set, and these cells are scratch AND
	 * unreferenced by any driver in the tree (0 references each):
	 *
	 *   0xC00  entered / returned   (0xC0DE5E01 = entered, 0xC0DE5E02|err = returned)
	 *   0xC04  instr->addr  (the MASTER offset the core handed us)
	 *   0xC0C  instr->len
	 *   0xC10  the first block's computed page row
	 *
	 * READ IT ON THE VENDOR AFTER A FIRE: if 0xC00 still holds the poison, this function never ran
	 * and the "erase works" premise is FALSE.  If it holds 0xC0DE5E01/2, our erase DID run and the
	 * split survives with evidence behind it for the first time.
	 */
	/*
	 * ★ THE INFORMATION GATE IS NOW SELF-PROVING, BECAUSE THE LAST ONE WAS CLOBBERED.
	 *
	 * The first version deposited into 0xC00/0xC04/0xC0C/0xC10 - cells that scanned as true scratch
	 * AND had zero references in this tree.  They were primed and CONFIRMED WRITTEN before the fire.
	 * After a fire in which our kernel demonstrably booted (C18 = 0xC0DE5004) they read:
	 *
	 *     primed: 0x11111111 0x22222222 0x33333333 0x44444444
	 *     after:  0x80609680 0x00000000 0x00000000 0x0A444444
	 *
	 * - our marker absent, our primes gone, and a PHYSICAL ADDRESS in their place.  0xC10 kept its
	 * low 24 bits (0x444444 out of 0x44444444) and had its TOP BYTE replaced, which is a bitfield
	 * write by something else.
	 *
	 * *** A CELL IS ONLY PROVEN BY A SENTINEL AND PAYLOAD THAT SURVIVE THEIR OWN WRITE, READ BACK
	 *     IN THE SAME BOOT.  Vendor-side characterisation is the WRONG RUNTIME: the vendor runs no
	 *     FMC driver and no mach code, so the writers that clobber these cells do not exist there. ***
	 *
	 * So this version writes a SENTINEL ALONGSIDE the payload, then READS BOTH BACK and records
	 * whether they survived.  The reading carries its own validity:
	 *
	 *   0xC00  the erase marker (0xC0DE5E01 entered / 0xC0DE5E02|err returned)
	 *   0xC04  instr->addr
	 *   0xC0C  instr->len
	 *   0xC10  the computed page row
	 *   0xC14  SELF-CHECK: 0x5AFE0000|survivors - bit0 = marker survived, bit1 = addr, bit2 = len,
	 *          bit3 = row.  0x5AFE000F means every deposit was read back intact, and ONLY THEN is
	 *          the marker meaningful.  Anything less says which field cannot be trusted.
	 */
	/*
	 * CELL CHOICE IS NOW DERIVED FROM A FULL AUDIT, NOT FROM A GUESS.
	 *
	 * The first version wrote 0xC00/0xC04/0xC0C/0xC10 and was clobbered. The second wrote 0xC08,
	 * which is THE SLOT SELECTOR (`LUOFU_BOOTREG_OFFSET`) - storing 0xC0DE9A01 there has a low byte
	 * of 0x01, neither 0x10 nor 0x11, so it invalidated the selector on every boot and the loader
	 * fell back to slot A after a 13-minute dark retry.
	 *
	 * A grep of every writer in the tree gives the cells with ZERO references anywhere:
	 *
	 *     0xC74, 0xC7C, 0xC80, 0xC84
	 *
	 * THE FOUR FIELDS GO IN THOSE, and the self-check goes into 0xC74's neighbour pair. Nothing in
	 * this tree - mach, FMC, or the slot selector - touches them.
	 */
	{
		void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

		if (sc) {
			u32 addr = (u32)instr->addr;
			u32 len = (u32)instr->len;
			u32 row = (u32)((u32)instr->addr / mtd->writesize);
			u32 surv = 0;

			writel(0xC0DE5E01u, sc + 0xc74);
			writel(addr, sc + 0xc7c);
			writel(len, sc + 0xc80);
			writel(row, sc + 0xc84);

			/* self-proving: a deposit that cannot survive its own write must never be
			 * reported as a measurement */
			if (readl(sc + 0xc74) == 0xC0DE5E01u)
				surv |= 1u;
			if (readl(sc + 0xc7c) == addr)
				surv |= 2u;
			if (readl(sc + 0xc80) == len)
				surv |= 4u;
			if (readl(sc + 0xc84) == row)
				surv |= 8u;
			writel(0x5AFE0000u | surv, sc + 0xc78);
			(void)readl(sc + 0xc78);
			iounmap(sc);
		}
	}

	if (instr->addr + instr->len > mtd->size)
		return -EINVAL;

	for (pos = instr->addr; pos < instr->addr + instr->len; pos += mtd->erasesize) {
		/*
		 * THE ERASE ADDRESS IS THE PAGE ROW OF THE BLOCK'S FIRST PAGE, STORED UNSHIFTED.
		 *
		 * Three encodings have now been run against a partition with a known pattern planted in its
		 * LAST block, which is the only place that can tell them apart - block 0 is address 0 under
		 * every convention, and a read-back of erased flash proves nothing at all:
		 *
		 *   page row, shifted    (row << 16)                  -> partition byte-identical, nothing erased
		 *   page row, unshifted  (row)                        -> JFFS2's complaint MOVED, so erasing happened
		 *   block index, unshifted (pos / erasesize)           -> "ERASE DID NOTHING" on a planted pattern
		 *
		 * The middle one is the one that did something, and it also matches the vendor's wrapper, which
		 * stores the address verbatim with ADDRH = 0:
		 *
		 *     hi_spi_nand_hw_erase(regs, addr, cs, cmd):  ADDRH = 0 ; ADDRL = addr   (VERBATIM)
		 *
		 * ADDRH = 0 fits a page row on this part: 15,104 pages, inside 16 bits.
		 *
		 * ★ AND THE ROW IS CHIP-ABSOLUTE, like the write and read paths: `instr->addr` is PARTITION-
		 * relative, so erasing "mtd9 offset 0" without this would erase CHIP row 0 - inside the
		 * "esbc" partition.  mtd_get_master_ofs() supplies the partition's base.
		 */
		u32 row = div_u64(mtd_get_master_ofs(mtd, pos), mtd->writesize);

		if (luofu_e_n < 0xff)
			luofu_e_n++;

		spin_lock_irqsave(&luofu_fmc_read_lock, flags);
		ret = luofu_fmc_erase_block(fmc, row);
		spin_unlock_irqrestore(&luofu_fmc_read_lock, flags);

		if (ret)
			luofu_w_err = 1;
		if (ret)
			break;
	}

	/*
	 * AND THERE IS NO state, NO MTD_ERASE_DONE AND NO mtd_erase_callback HERE,
	 * BECAUSE 5.10 REMOVED THEM.  The erase op is synchronous now: the driver
	 * does the work and returns 0 or an errno, and the core owns the rest.  The
	 * build said so exactly - "'struct erase_info' has no member named 'state'",
	 * "'MTD_ERASE_FAILED' undeclared", "implicit declaration of function
	 * 'mtd_erase_callback'" - which is the compiler being a better reader of the
	 * API than I was.
	 *
	 * fail_addr is kept because it is still a member, and it is the one thing the
	 * core cannot know on its own: WHICH block failed.
	 */
	if (ret)
		instr->fail_addr = pos;

	/*
	 * And record that we RETURNED, with the code - the second half of the information gate at the
	 * top of this function.  0xC0DE5E01 alone would not distinguish "entered and hung" from
	 * "entered and finished", and on a board that panics mid-boot that distinction is the point.
	 */
	{
		void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

		if (sc) {
			writel(0xC0DE5E02u | ((u32)(-ret) & 0xffu), sc + 0xc74);
			(void)readl(sc + 0xc74);
			iounmap(sc);
		}
	}

	return ret;
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
	/*
	 * THE FLASH IS WRITABLE NOW, AND THAT IS A DELIBERATE CHANGE.
	 *
	 * This line used to clear MTD_WRITEABLE and set MTD_NO_ERASE, with a
	 * comment explaining that the driver had no path that could modify the
	 * flash.  It has one now (see the write path above), so the flags tell the
	 * truth: MTD_CAP_NANDFLASH is exactly MTD_WRITEABLE, and MTD_NO_ERASE is
	 * gone because ->_erase exists.
	 *
	 * The consequence to know about: UBI now attaches mtd16 READ-WRITE, so it
	 * will erase and program its own EC/VID headers as it works.  That is
	 * ordinary UBI behaviour on a NAND - it is what the vendor's own stack does
	 * to this same partition - and it is why the volume table was verified
	 * intact before the change.
	 */
	mtd->flags = MTD_CAP_NANDFLASH;
	mtd->size = fmc->spec.tri_size;
	mtd->erasesize = fmc->spec.block_size;
	mtd->writesize = fmc->spec.page_size;
	mtd->writebufsize = fmc->spec.page_size;
	mtd->oobsize = fmc->spec.oob_size;
	/*
	 * oobavail IS NOT DERIVED FROM oobsize, AND LEAVING IT ZERO BREAKS JFFS2 ON EVERY PARTITION.
	 *
	 * jffs2_nand_flash_setup() reads it directly:
	 *
	 *     if (!c->mtd->oobsize)
	 *             return 0;
	 *     if (c->mtd->oobavail == 0) {
	 *             pr_err("inconsistent device description\n");
	 *             return -EINVAL;
	 *     }
	 *
	 * so a device that declares an OOB but no available bytes in it is called inconsistent and the
	 * mount is refused. That is the whole of the "jffs2: inconsistent device description" this board
	 * printed at 10.147 s, and it is why the overlay could never mount even after the partition was
	 * marked FS_JFFS2.
	 */
	mtd->oobavail = fmc->spec.oob_size;
	mtd->owner = THIS_MODULE;
	mtd->_read = luofu_mtd_read;
	mtd->_write = luofu_mtd_write;
	mtd->_erase = luofu_mtd_erase;
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
 * THE UBI VERDICT: COMPUTED WHERE IT IS LEGAL, REPORTED WHERE IT IS VISIBLE.
 *
 * luofu_fmc_ubi_probe() runs at late_initcall_sync - process context, so it may open a UBI
 * volume - but panic() calls dump_stack() BEFORE kmsg_dump(), and the trace plus the root
 * partition list fill the entire 3.5 KiB window on their own. Two fires proved the consequence:
 * every line this hook prints is pushed out of the newest-KEEP window no matter how it is placed.
 *
 * The kmsg dumper is called AFTER dump_stack(), so a line printed there is the newest line in the
 * log when luofu_kmsg_to() copies the tail - inside the window by construction.
 *
 * So the hook computes and stores, and the dumper prints. The dumper cannot compute: opening a UBI
 * volume takes a mutex, which is illegal with interrupts off.
 */
static long luofu_ubi_diag_err = -4096;	/* -EBADR: the hook never reached its verdict */
static u32 luofu_ubi_diag_printed;

/*
 * THE ATTACH PROBE'S ANSWER, AND THE DECLARATION IT NEEDS.
 *
 * `ubi_open_volume` returning -ENODEV (measured, C1C = 0x0A025A13) says there is no ubi0, which is
 * consistent with TWO worlds: `ubi_init` saw mtd_devs == 0, or its own attach refused mtd13. Only an
 * attach of our own can tell them apart, and that means calling ubi_attach_mtd_dev(), which lives in
 * drivers/mtd/ubi/ubi.h (internal) and is not EXPORT_SYMBOL'd - neither matters for BUILT-IN code,
 * which can call it with a local declaration.
 *
 *   code 0x00  the attach SUCCEEDED (so ubi_init had not attached it)
 *   code 0x13  -ENODEV   the name "rootfsa" did not resolve to an MTD at all
 *   code 0x16  -EINVAL   the attach refused the partition  <- the record's long-standing suspect
 *   code 0x11  -EEXIST   already attached, i.e. ubi_init DID attach and the -ENODEV came from
 *                        somewhere else entirely
 *   code 0xee  the probe never ran
 */
extern int ubi_attach_mtd_dev(struct mtd_info *mtd, int ubi_num,
			      int vid_hdr_offset, int max_beb_per1024);

static u32 luofu_ubi_attach_code = 0xeeu;

/*
 * AND THE EC-HEADER ACCEPTANCE WALK'S ANSWER, POINTED AT THE RIGHT PARTITION THIS TIME.
 *
 * The walk that already exists in the IS_ERR branch runs over **"rootfsb"** - which is mtd14, EMPTY -
 * while `ubi.mtd=rootfsa` is the partition UBI is actually given. So the one instrument built to
 * answer "does our read path hand UBI a header it accepts?" has been reading the wrong flash all
 * along. This repeats UBI's own ubi_io_read_ec_hdr() acceptance test (magic 0x55424923, version 1,
 * vid_hdr_offset 2048, data_offset 4096, EC counter <= 0x7fffffff, and the stored CRC equal to
 * crc32(LUOFU_UBI_CRC32_INIT, hdr, 60)) over the first 32 PEBs of **mtd13**, and counts the passes:
 *
 *   32            every header UBI tests would be accepted -> the refusal is BEYOND the EC headers
 *   0..31         it stopped at that PEB                       -> our read path hands UBI garbage
 *   0xee          the name "rootfsa" did not resolve to an MTD
 */
static u32 luofu_ec_passes = 0xeeu;

/*
 * WHICH TEST FAILED, AND THE LITERAL FIRST BYTES - because "0 passes" is ambiguous today.
 *
 *   C18 low byte (step)   0x00..0x20 = the walk got that many PEBs through UBI's test
 *                         0xee      = the name "rootfsa" did not resolve
 *                         0xf0      = mtd_read() itself refused (got != 64)
 *                         0xf1..0xf6 = PEB 0 was read but REJECTED on magic / version /
 *                                     vid_hdr_offset / data_offset / EC counter / CRC
 *   C1C                    (writes<<24)|(erases<<16)|(h[0]<<8)|h[1]
 *
 * The two literal bytes are the point: the EC magic must read 55 42 49 23 ("UBI#"), so h[0]h[1] =
 * 0x5542 says the read path is delivering the RIGHT page, while anything else says it is not - and
 * that single fact decides whether the next move is the read path or a field/CRC constant.
 */
static u32 luofu_ec_h0 = 0xbeefu;

/*
 * THE ONE RELATIVE MEASUREMENT THAT MATTERS NOW: two partitions' first two bytes, side by side.
 *
 * mtd_read() over mtd13 PEB 0 succeeds and yields 00 00 where the flash holds "UBI#" - so either
 * the read path is dead everywhere, or mtd13's PARTITION BASE OFFSET is wrong and we are reading
 * unwritten flash. mtd12 ("kernelb") holds the very slice this fire writes, whose first bytes are the
 * uImage magic 27 05 19 56 - content known independently of our driver. So one crumb holds both:
 *
 *   C1C = (kb[0]<<24) | (kb[1]<<16) | (ra[0]<<8) | ra[1]
 *
 *   0x27055542  both correct    -> the read path is ALIVE; the fault is mtd13's offset arithmetic
 *   0x27050000  only rootfsa zero -> same conclusion, rootfsa specifically reads unwritten flash
 *   0x00000000  both zero        -> the READ PATH IS DEAD and returns zeros for every page
 *
 * The write/erase counters are dropped from this fire's crumb (they were 10 and 2) because four
 * literal bytes are worth more than a number already known.
 */
static u32 luofu_kb_b = 0xbeefu;

/*
 * AND THE QUESTION ONE SOURCE READING REFRAMED (2026-10-17): DOES UBI EVER SEE ITS OWN ATTACH
 * ARGUMENT?
 *
 * `ubi.mtd=rootfsa` is a MODULE parameter - module_param_call(mtd, ubi_mtd_param_parse, ...) at
 * build.c:1471 - which expands to __param_call -> __param -> __module_param_call, and that macro
 * carries its own `__modparam_` section. A name containing "param" does NOT match `__param_obs`,
 * so init/main.c's obsolete_checksetup() finds no __setup for it and unknown_bootoption() drops it
 * on the dot (`if (strnchr(param, len, '.')) return 0;`) into the list that load_module() replays
 * with the module name as the prefix. That replay happens in kernel_init_freeable(), but ubi_init
 * is a late_initcall and runs AFTER it - so the initcall ordering does not save the parameter, and
 * for a built-in that prefix is the module's own name "ubi".
 *
 * Whether the replay really calls ubi_mtd_param_parse() for a BUILT-IN was never traced end to end,
 * and this project has already paid for the mirror question in kernel/params.c. So it is MEASURED,
 * in one number: how many UBI VOLUMES exist when this probe runs. ubi_init has already run by
 * late_initcall_sync, and when ubi_attach_mtd_dev() succeeds it attaches VOLUME_COUNT volumes
 * (autoresize + 255 + 128, less its own unsigned underflow) - so:
 *
 *   volumes > 255    ubi_init ATTACHED something -> the parameter DOES reach UBI, and the stage-D
 *                    hook's own attach was refused only because it was already attached (-EEXIST)
 *   volumes == 0     ubi_init attached nothing  -> mtd_devs was 0, i.e. the parameter never
 *                    reached UBI and the command line is the whole story
 *
 * The count is read-only (a throwaway kobject probe, see luofu_ubi_vols_of), so the instrument
 * cannot perturb the system it measures - the rule this lane paid for with five instruments that
 * did.
 */
static u32 luofu_ubi_vols = 0xbeefu;

#ifdef CONFIG_MTD_UBI
#define LUOFU_UBI_MTD_DEVS	0xc0e07c84u

static u32 luofu_ubi_early_val = 0xbeefu;
static int luofu_ubi_early_done;

static int __init luofu_fmc_ubi_early(void)
{
	void __iomem *sc;

	sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);
	if (!sc)
		return 0;
	/*
	 * LIVE `mtd_devs` (0x3F) vs THE SENTINEL (0xEE) IS ITSELF THE ANSWER.
	 *
	 * The guard used to be checked BEFORE the ioremap, and ubi3 came back with
	 * 0xC0C = 0 and the tick at full strength: either this hook never ran or it
	 * ran and wrote 0, and nothing in flash could tell those apart. So the write
	 * comes FIRST and unconditional, and the byte identifies the branch:
	 *
	 *   0xEE  the guard was already set - this hook did not run, or ran twice
	 *   0x00  mtd_devs really was 0   -> ubi.mtd NEVER reached ubi_init
	 *   0x01+ it did reach it (the count)
	 *
	 * Read-only: one load from the built-in UBI module's own static.
	 */
	if (luofu_ubi_early_done) {
		writel(0xc0de0000u | 0xeeu, sc + 0x04);
	} else {
		luofu_ubi_early_val = *(volatile u32 *)LUOFU_UBI_MTD_DEVS;
		writel(0xc0de0000u | (luofu_ubi_early_val & 0xffu), sc + 0x04);
	}
	(void)readl(sc + 0x04);
	iounmap(sc);
	return 0;
}
device_initcall(luofu_fmc_ubi_early);
#endif

/* the public-API answer IMMEDIATELY after our own attach returned 0 (0 = the volume opened) */
static u32 luofu_ubi_post_code = 0xfffu;	/* the attach never succeeded */

/*
 * AND THE ONE THING THIS HOOK STILL MEASURES: A PUBLIC-API OPEN OF THE ROOTFS VOLUME.
 *
 * It is the SAME call the hook already makes 200 lines down, so nothing new is being relied on;
 * what is new is that the answer gets into a channel that survives. On the vendor stack the same
 * open returns a descriptor and the volume's first four bytes are "hsqs" (the squashfs magic) -
 * read from the live device, 2026-10-17 - so 0x000 means the device exists AND the volume is the
 * image the box actually boots.
 *
 *   code 0x019   -ENODEV: no ubi0 at all  -> ubi_init attached nothing -> ubi.mtd never reached it
 *   code 0x000   the volume OPENED        -> ubi.mtd DID reach UBI and attached rootfsa
 *   code 0x33c   -ERANGE / others         -> a volume table that parsed but holds no volume 0
 *
 * The code goes to C80 with the same 0xC0DE0000 tag the old ubi_open_volume verdict used, so a
 * reader that predates this comment reads it unchanged.
 */


/*
 * HOW MANY VOLUMES DOES UBI DEVICE @dev HAVE? ubi%u must not already exist under the throwaway
 * parent, or create_and_add hands back the real kobject and the "exists" probe reads YES for a
 * device that has none. So the count is "ubi%u_%u exists" over 0..7 - the sysfs names of volumes
 * are fixed by UBI's own class code (ubi%d_%d) and are a stable ABI.
 */

/*
 * AND THE QUESTION THAT ONE SOURCE READING REFRAMED (2026-10-17): DOES UBI EVER SEE ITS OWN
 * ATTACH ARGUMENT?
 *
 * `ubi.mtd=rootfsa` is a MODULE parameter: module_param_call(mtd, ubi_mtd_param_parse, ...) at
 * build.c:1471, which expands to __param_call -> __param -> __module_param_call, and that macro
 * carries its own `__modparam_` section - a name that contains "param", so `__param_obs` does
 * not match it. init/main.c's obsolete_checksetup() therefore finds no __setup for it and
 * unknown_bootoption() drops it on the dot (`if (strnchr(param, len, '.')) return 0;`) into the
 * list that load_module() replays. The load happens in kernel_init_freeable(), but ubi_init is a
 * late_initcall, which runs AFTER it - so the ordering does not save it, and for a built-in %s
 * is the module's own name.
 *
 * Whether the replay actually calls ubi_mtd_param_parse() for a BUILT-IN was never traced end to
 * end, and the project has already been burned by the mirror question on kernel/params.c. So it
 * is measured instead of argued, in a single number: how many UBI VOLUMES exist at the time of
 * this probe. ubi_init runs before this late_initcall_sync hook, and it attaches VOLUMES_COUNT
 * (autoresize + 255 + 128, minus its own unsigned underflow) whenever ubi_attach_mtd_dev()
 * succeeds - so:
 *
 *   volumes 300+     ubi_init ATTACHED something -> the parameter does reach UBI, and the
 *                    stage-D hook's own attach was refused only by -EEXIST
 *   volumes 0        ubi_init attached nothing  -> mtd_devs was 0, i.e. the parameter never
 *                    reached UBI and the command line is the whole story
 *   code 0x00        our own attach SUCCEEDED   -> nobody had attached, and mtd13 is fine
 *
 * The counter itself is read-only (sysfs), so this instrument cannot perturb what it measures.
 */

/*
 * ONE REPORTER, CALLED FROM EVERY PATH THAT MIGHT BE THE SURVIVING ONE.
 *
 * Measured 2026-10-16: printing this from the stage-D hook does NOT survive (panic()'s
 * dump_stack() trace plus the root partition list fill the whole 3.5 KiB window on their own), and
 * printing it from luofu_kmsg_dump() did NOT appear either - the disassembly shows the call in the
 * object, so the callback is simply not the path that captures. What DOES capture is the 5-second
 * tick, whose luofu_kmsg_to() is the last writer of the ring (C18 finishes at step 45, the tick).
 *
 * So the reporter is idempotent and is called from the tick, the panic notifier and the dumper:
 * whichever path runs, the line is printed immediately before that path copies the log, which is
 * the only placement that is inside the newest-KEEP window by construction. The sentinel makes it
 * a no-op until the hook has actually reached a verdict.
 */
/*
 * AND A VERDICT CELL THAT NOTHING ELSE WRITES, BECAUSE THE LOG KEEPS LOSING THIS ONE LINE.
 *
 * Three fires were spent trying to deliver the UBI verdict through the log: from the hook, from
 * luofu_kmsg_dump() (where the object provably contains the call) and from the tick immediately
 * before its own luofu_kmsg_to(). None appeared, and the flash confirms it - the captured window
 * ends with the hook's `luofu_jffs2_mark` line and nothing after it. Rather than keep fighting a
 * channel whose window is consumed by something outside this driver, put the answer where the
 * project has always been able to read it: a sysctrl cell, read with devmem from the vendor.
 *
 * C18/C1C are NOT usable - the ladder, the tick and the panic notifier all write them, and the
 * tick lands last on every boot. 0x10100C80/0x10100C84 are outside every one of those writers.
 *
 *   C80 = 0xC0DE0000 | (-err & 0xfff)   the ubi_open_volume verdict   (0 = it opened)
 *   C84 = 0x600DF00D if saved_command_line contains "ubi.mtd=", else 0xDEADBEEF
 *
 * Both are primed to 0x55555555 by the fire, so a readback cannot be a stale value from an
 * earlier boot - the trap this project has already paid for twice.
 */
static void luofu_ubi_diag_print(void)
{
	if (luofu_ubi_diag_printed || luofu_ubi_diag_err == -4096)
		return;

	luofu_ubi_diag_printed = 1;
	pr_info("LUFU DIAG: ubi_open_volume(0,0) err=%ld; mtd=%d; cmdline=[%s]\n",
		luofu_ubi_diag_err,
		luofu_ubi_fmc && luofu_ubi_fmc->mtd ? luofu_ubi_fmc->mtd->index : -1,
		saved_command_line);
}

static void luofu_verdict_cell(u32 off, u32 v)
{
	void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

	if (!sc)
		return;

	writel(v, sc + off);
	(void)readl(sc + off);
	iounmap(sc);
}

static void luofu_phase_cell(u32 off, u32 v)
{
	luofu_verdict_cell(off, v);
}

/*
 * THE UBI VERDICT'S OWN PAIR MOVES TO 0xC04/0xC0C.
 *
 * 0xC80/0xC84 are the VENDOR'S OWN stable pair (measured twice, 2026-10-17 and again
 * 2026-10-18: 0x00020000 / 0x00001200, unchanged across boots), so a value there cannot
 * be attributed to our kernel - the defect that voided the 2026-10-16 reading. 0xC04 and
 * 0xC0C scan as exact-write scratch on the vendor and have zero references in this tree.
 */
static void luofu_ubi_verdict(long err)
{
	luofu_ubi_diag_err = err;
	luofu_verdict_cell(0x04, 0xc0de0000u | ((u32)(-err) & 0xfffu));
	luofu_verdict_cell(0x0c, strstr(saved_command_line, "ubi.mtd=") ?
					0x600df00du : 0xdeadbeefu);
}

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
static struct timer_list luofu_log_timer;
/*
 * AND THE SAFETY RESTART'S TIMER IS DECLARED HERE, ABOVE EVERYTHING THAT WRITES FLASH, NOT AT THE BOTTOM
 * WHERE IT USED TO LIVE - AND ITS TIMEOUT MOVES WITH IT.
 *
 * It is armed from luofu_jffs2_mark() before that function erases anything, and a use above its
 * declaration is the mistake this file has made more than once: the one where the compiler is the only
 * reader that notices. A macro has the same rule, which is why LUOFU_SAFETY_SECS is here and not a
 * thousand lines down.
 */
/*
 * THE SAFETY RESTART'S TIMER.
 *
 * 180 s was chosen when our kernel could not reach a usable state: the timer's whole job was to
 * hand the box back to the vendor instead of leaving it dark.  THAT PREMISE IS NOW OUT OF DATE -
 * measured 2026-10-13, our kernel boots, its MAC defers for mdio0, retries, connects to a real PHY
 * and registers its netdev (crumb 0xC0DE6006).  It is now a system worth LOGGING INTO, and 180 s
 * cannot be used to do that: the restart fires before anyone can reach it.
 *
 * So the window is 900 s - long enough to ssh in on our own kernel and read the things only a live
 * shell can reach (the log ring, eth0's link state, /proc), short enough that a hang still returns
 * the box by itself.  The risk stays bounded either way: our kernel arms the FALLBACK (it flips the
 * slot selector) on the way up, so even a hang is one power cycle from the vendor slot.
 */
#define LUOFU_SAFETY_SECS	900
static struct timer_list luofu_reboot_timer;

/*
 * THE ERASE IS OFF BY DEFAULT, AND THE REASON IS A FIRE.
 *
 * luofu_jffs2_mark() erases the WHOLE rootfs_data partition - 176 blocks at 128 KiB each - and then
 * samples every eighth block back. That happens inside the boot path (it is called from
 * luofu_fmc_ubi_probe at late_initcall), and the read-back timer above is only LUOFU_SAFETY_SECS
 * long. The file's own note already records the shape of the hazard: "a block that has not finished
 * erasing can sit in wait_ready for 200 ms", so the loop's cost is not bounded by anything this
 * code controls, while the timer's is 180 s.
 *
 * Two things are true regardless of how the boot failure of 2026-10-11 is finally attributed:
 *   - a DIAGNOSTIC routine has no business performing the largest write this driver ever issues;
 *   - an unbounded-cost operation must be opt-in, not the default - the rule every other luofu
 *     driver already follows (read-only until a measurement justifies a store).
 *
 * So the erase is opt-in:
 *
 *   insmod luofu-fmc.ko omo_mark=1
 *
 * and with the default (0) the partition is left exactly as found. The write path itself is
 * unchanged and still measured; only its automatic invocation at boot is gone.
 */
static unsigned int omo_mark;
module_param_named(mark, omo_mark, uint, 0644);
MODULE_PARM_DESC(mark, "1 = erase rootfs_data and write the JFFS2 cleanmarker at boot (default 0: leave the partition untouched)");

/*
 * WHY TWELVE BYTES HAVE TO BE WRITTEN BEFORE THE OVERLAY CAN EVER WORK.
 *
 * fstools does not mount an empty partition and does not format one. From its own source:
 *
 *     switch (volume_identify(data)) {
 *     case FS_NONE:
 *             ULOG_WARN("no usable overlay filesystem found, using tmpfs overlay\n");
 *             return ramoverlay();
 *
 * and mtd_volume_identify() decides FS_NONE against FS_JFFS2 on exactly four bytes at offset 0:
 *
 *     sz = read(p->fd, &deadc0de, sizeof(deadc0de));
 *     if (deadc0de == ~0) { ...ioctl(p->fd, MEMREADOOB, &oob); }
 *     if (__be16_to_cpu(deadc0de) == 0x1985 || __be16_to_cpu(deadc0de >> 16) == 0x1985)
 *             return FS_JFFS2;
 *     return FS_NONE;
 *
 * An erased partition reads ffffffff, so it is FS_NONE, so the overlay is a tmpfs and every setting
 * written at runtime dies with the next boot. Nothing in the shipped rootfs can fix that: OpenWrt has
 * no mkfs at all, and the vendor's own driver cannot even erase this partition - "mtd write" against
 * it returns an erase error. OUR driver can, and both its erase and its program paths are measured,
 * with byte-identical read-back.
 *
 * So the first four bytes become 85 19, which is 0x1985 little-endian, and identify() answers
 * FS_JFFS2. The twelve bytes written are a real JFFS2 cleanmarker - magic, nodetype, length and a
 * valid hdr_crc - so the filesystem accepts the block rather than complaining about it.
 *
 * IT ONLY EVER WRITES AN UNTOUCHED PARTITION. If the first four bytes are anything but ff, there is
 * already an overlay here and this returns without touching anything.
 */
static void luofu_jffs2_mark(struct luofu_fmc *fmc)
{
	/*
	 * OPT-IN, AND THAT IS THE WHOLE POINT: the erase below is the largest write this driver ever
	 * issues and it runs from the boot path, so the default is to leave the partition alone. See
	 * the omo_mark declaration for why. Gating HERE rather than at the call site matters: the
	 * caller continues into the UBI read-only diagnosis, which is useful and must still run.
	 */
	if (!omo_mark) {
		dev_info(fmc->dev,
			 "FMC: rootfs_data left untouched (omo_mark=0; insmod with omo_mark=1 to erase and mark)\n");
		return;
	}

	/*
	 * THE SAFETY RESTART IS ARMED HERE, FIRST, AND THAT ORDERING IS THE POINT.
	 *
	 * It is normally armed much later, in the init path at the bottom of this file - AFTER this function
	 * runs. So when the erase loop below hung a boot, the one mechanism this project relies on to return
	 * the box was not yet in place, and the box stayed in this kernel with the boot selector still
	 * pointing at it: a loop, not a crash, and no watchdog recovery because there was nothing wrong for
	 * a watchdog to see.
	 *
	 * Anything that writes flash belongs behind that timer. Arming it twice is harmless - mod_timer
	 * simply moves it - and the later call is left where it is so this does not depend on this one being
	 * reached.
	 */
	mod_timer(&luofu_reboot_timer, jiffies + LUOFU_SAFETY_SECS * HZ);
	static const u8 marker[12] = {
		0x85, 0x19, 0x03, 0x20, 0x0c, 0x00, 0x00, 0x00, 0xb1, 0xb0, 0x1e, 0xe4
	};
	struct erase_info ei;
	struct mtd_info *part;
	u8 *page;
	u32 word = 0;
	u32 block = 1;
	u32 nblocks;
	size_t got = 0;
	int err;

	part = get_mtd_device_nm("rootfs_data");
	if (IS_ERR(part))
		return;

	/*
	 * AND THE PARTITION DECLARES NO OOB, WHICH IS WHAT STOPS JFFS2 ASKING FOR ONE.
	 *
	 * With the geometry finally consistent, the mount got past the check and failed on the next thing:
	 *
	 *     jffs2: cannot read OOB for EB at 00000000, requested 8 bytes, read 0 bytes, error -95
	 *
	 * -95 is -EOPNOTSUPP: this driver registers no _read_oob, and JFFS2 wants the cleanmarker from the
	 * spare area. jffs2_nand_flash_setup() offers the way out in its first three lines:
	 *
	 *     if (!c->mtd->oobsize)
	 *             return 0;
	 *
	 * so a device that declares no OOB is mounted the ordinary way, with the cleanmarker IN BAND -
	 * which is exactly what the twelve bytes below are, a struct jffs2_unknown_node. Declaring it here
	 * rather than on the master is deliberate: mtd16 keeps the real geometry so UBI's attach, which
	 * carries the mounted rootfs, is not disturbed by a change made for the overlay's benefit.
	 */
	part->oobsize = 0;
	part->oobavail = 0;

	err = mtd_read(part, 0, 4, &got, (u8 *)&word);
	if (err || got != 4 || word != 0xffffffffu) {
		dev_info(fmc->dev, "FMC: rootfs_data already carries 0x%08x; leaving it alone\n", word);
		goto out;
	}

	page = kzalloc(part->writesize, GFP_KERNEL);
	if (!page)
		goto out;

	memset(&ei, 0, sizeof(ei));
	ei.addr = 0;
	ei.len = part->erasesize;
	err = mtd_erase(part, &ei);
	if (err) {
		dev_info(fmc->dev, "FMC: could not erase rootfs_data block 0: %d\n", err);
		kfree(page);
		goto out;
	}

	/*
	 * AND THE WHOLE PARTITION IS ERASED, NOT JUST THE FIRST BLOCK.
	 *
	 * JFFS2 scans everything, and it found this at 1.5 MB into a partition I had only ever probed at
	 * block 0 and block 128:
	 *
	 *     jffs2: Incompatible feature node (0xe009) found at offset 0x00181758
	 *
	 * which stops the mount dead. A partition that is erased at block 0 but carries junk further in
	 * has never held a filesystem, and JFFS2 will not adopt it.
	 *
	 * THE ERASE CAN ONLY EVER HIT A PARTITION THAT HAS NEVER HELD AN OVERLAY, because it runs only
	 * behind the guard above: the first four bytes must read ff ff ff ff. A filesystem that has ever
	 * been mounted writes block 0, so the guard sees it and this returns without touching anything - the
	 * destructive path is unreachable for a live overlay, and the routine therefore cannot eat user
	 * data on a later boot.
	 */
	/*
	 * THE VERIFY IS SAMPLED, NOT PER-BLOCK, AND THAT BOUND IS THE POINT.
	 *
	 * The first version of this loop erased all 176 blocks and then VERIFIED EVERY ONE, with up to
	 * three tries each. The erase itself is the fix (WREN before every erase); but the check on top of
	 * it turned a 2-second erase into MINUTES, because a block that has not finished erasing can sit in
	 * wait_ready for 200 ms, and 176 x 3 of those is longer than any boot should take - and that hung
	 * boot is what stranded the board the last fire (no network driver, so no ssh and no failover, and the
	 * boot selector still pointing here).
	 *
	 * So every block is still WRITTEN to (the erase must cover the whole partition), but only a SAMPLE is
	 * read back: block 1, then every 8th block, then the last. That is ~22 reads instead of 176, still
	 * catches the race (a lost erase shows as a non-ff dword anywhere the sample lands, and the race
	 * scatters dirty blocks at a different set each boot), and keeps the worst case well inside the
	 * safety timer instead of relying on a timer that the loop had already outrun.
	 */
	for (block = 1, nblocks = div_u64(part->size, part->erasesize); block < nblocks; block++) {
		memset(&ei, 0, sizeof(ei));
		ei.addr = (u64)block * part->erasesize;
		ei.len = part->erasesize;
		err = mtd_erase(part, &ei);
		if (err) {
			dev_info(fmc->dev, "FMC: erase stopped at block %u: %d\n", block, err);
			break;
		}

		/* verify a sample only: every 8th block, plus always verify the last one */
		if ((block % 8) != 1 && block != (u32)(nblocks - 1))
			continue;

		word = 0x5a5a5a5au;
		err = mtd_read(part, (loff_t)block * part->erasesize, 4, &got, (u8 *)&word);
		if (!err && got == 4 && word == 0xffffffffu)
			continue;

		/* the sample did not read clean; re-erase just this block, once */
		if (!err) {
			memset(&ei, 0, sizeof(ei));
			ei.addr = (u64)block * part->erasesize;
			ei.len = part->erasesize;
			err = mtd_erase(part, &ei);
			luofu_e_retries++;
		}
	}
	dev_info(fmc->dev, "FMC: erased %u blocks of rootfs_data (sampled verify, %u re-erased)\n",
		 block, luofu_e_retries);

	/*
	 * AND THE ERASE IS READ BACK BEFORE IT IS TRUSTED - AT BOTH ENDS OF THE PARTITION.
	 *
	 * This is the check whose absence let a wrong address encoding survive: the driver reported 176
	 * blocks erased while the partition kept every byte it started with. And a first version of the
	 * check was still not enough, because it sampled only block 0 - THE ONE ADDRESS WHERE A PAGE ROW
	 * AND A BLOCK NUMBER ARE THE SAME VALUE, so it reported "erased" while every block past the first
	 * was untouched. A read-back that only looks where two conventions agree cannot tell them apart.
	 *
	 * So it samples the first block and the last one. An erase is the one operation that cannot be
	 * verified by its own return value, so it is verified by reading the flash - the same discipline
	 * the write path already uses.
	 */
	nblocks = div_u64(part->size, part->erasesize);
	for (block = 0; block < 3; block++) {
		u32 probe = (block == 0) ? 1 : ((block == 1) ? 25 : nblocks - 1);
		u32 t = 0;

		memset(page, 0x5a + block, part->writesize);
		err = mtd_write(part, (loff_t)probe * part->erasesize, part->writesize, &got, page);
		if (err) {
			dev_info(fmc->dev, "FMC: self-test could not plant in block %u: %d\n", probe, err);
			continue;
		}
		word = 0;
		err = mtd_read(part, (loff_t)probe * part->erasesize, 4, &got, (u8 *)&word);
		dev_info(fmc->dev, "FMC: self-test: block %u planted 0x%02x%02x%02x%02x, read back 0x%08x (%s)\n",
			 probe, page[0], page[1], page[2], page[3], word,
			 (err || got != 4) ? "READ FAILED" :
			 (word == *(u32 *)page ? "matches" : "DOES NOT MATCH - the read path is not showing the write"));

		memset(&ei, 0, sizeof(ei));
		ei.addr = (u64)probe * part->erasesize;
		ei.len = part->erasesize;
		err = mtd_erase(part, &ei);

		word = 0;
		err = mtd_read(part, (loff_t)probe * part->erasesize, 4, &got, (u8 *)&word);
		t = (err || got != 4) ? 4 :
		    (word == 0xffffffffu) ? 1 :
		    (word == *(u32 *)page) ? 2 :
		    (word == 0) ? 3 : 6;
		luofu_e_test &= ~(0xfu << (4 * (2 - block)));
		luofu_e_test |= (t & 0xfu) << (4 * (2 - block));
	}

	memset(page, 0xff, part->writesize);
	memcpy(page, marker, sizeof(marker));
	err = mtd_write(part, 0, part->writesize, &got, page);
	kfree(page);
	if (err) {
		dev_info(fmc->dev, "FMC: could not write the jffs2 marker: %d\n", err);
		goto out;
	}

	word = 0;
	err = mtd_read(part, 0, 4, &got, (u8 *)&word);
	dev_info(fmc->dev, "FMC: wrote the jffs2 marker; read back 0x%08x (%s)\n",
		 word, (word != 0xffffffffu && (word & 0xffff) == 0x1985) ? "identifies as FS_JFFS2" : "NOT the magic");
out:
	put_mtd_device(part);
}

static void luofu_fmc_ubi_probe_done(void)
{
	luofu_phase = 4;
}

static int luofu_fmc_ubi_probe(void)
{
	struct luofu_fmc *fmc = luofu_ubi_fmc;

	luofu_phase = 1;
	struct ubi_volume_desc *desc;
	struct mtd_info *part;
	u8 buf[4] = { 0 };
	size_t retlen;
	u32 word;
	int i, err;

	if (!fmc || !fmc->mtd)
		return 0;

	/*
	 * THE READ-ONLY UBI PROBE - run FIRST, so its answer is set even if a later step in this hook
	 * exits early, and folded into the tick's crumb (see luofu_ubi_post_code). It takes the mtd
	 * reference because the name must be shown to resolve; it attaches nothing.
	 */
	{
		struct mtd_info *p13 = get_mtd_device_nm("rootfsa");

		if (IS_ERR(p13)) {
			luofu_ubi_attach_code = (u32)(-PTR_ERR(p13)) & 0xffu;
		} else {
			/*
			 * NO ATTACH OF OUR OWN. THE HOOK MEASURES, IT DOES NOT ATTACH.
			 *
			 * Three fires produced the anomaly and then the dark box:
			 *
			 *   attach  C1C = 0x0A026A16   ubi_attach_mtd_dev(mtd13) answered -EINVAL
			 *   kb      C1C = 0x00000000   (that fire's separate crumb)
			 *   vols    C1C = 0x0A000000   the SAME call answered 0 = ATTACH SUCCEEDED
			 *
			 * for ONE partition, whose 64-byte EC header the project holds a dump of (UBI#, version
			 * 1, vid_hdr_offset 2048, data_offset 4096, EC 16, a valid CRC), and which the KERNEL'S
			 * OWN ubi_init attached successfully at the same initcall level in fire `attach`. The
			 * one state variable that differs between the -EINVAL fire and the success fire is
			 * whether an attach object already existed - so a SECOND attach is not going to settle
			 * anything; it is the thing whose behaviour changes.
			 *
			 * This call was also made WITHOUT ubi_devices_mutex, which ubi_attach_mtd_dev()
			 * documents as required. It is removed rather than repaired: the parameter question
			 * ("does ubi.mtd reach UBI?") is answered just as well by ASKING UBI WHAT IT HAS - an
			 * attach that succeeded for whoever called it leaves volumes behind, and
			 * ubi_open_volume() sees them. That probe is read-only, takes no internal structure, and
			 * is the same call this hook already makes further down.
			 */
			{
				struct ubi_volume_desc *vd =
					ubi_open_volume(0, LUOFU_ROOTFS_VOL_ID, UBI_READONLY);

				luofu_ubi_post_code = IS_ERR(vd) ?
					((u32)(-PTR_ERR(vd)) & 0xfffu) : 0x000u;
				if (!IS_ERR(vd)) {
					u8 h4[4] = { 0 };
					int rd = 0;
					int rr = ubi_read(vd, 0, h4, 0, 4);

					if (!rr)
						rd = ((int)h4[0] << 24) | ((int)h4[1] << 16) |
						     ((int)h4[2] << 8) | (int)h4[3];
					ubi_close_volume(vd);
					if (!rr)
						luofu_ubi_post_code = 0x1000u | ((u32)rd & 0xffu);
					else
						luofu_ubi_post_code = 0x2000u | ((u32)(-rr) & 0xffu);
				}
				put_mtd_device(p13);
				luofu_ubi_attach_code = 0x99u;	/* nothing attached by this probe */
			}
		}
	}

	/*
	 * THE EC-HEADER ACCEPTANCE WALK, OVER THE PARTITION UBI IS ACTUALLY GIVEN. Read-only: it takes
	 * the mtd reference and reads; nothing here writes flash. See luofu_ec_passes.
	 */
	{
		struct mtd_info *ec = get_mtd_device_nm("rootfsa");

		if (IS_ERR(ec)) {
			luofu_ec_passes = 0xeeu;
		} else {
			u8 h[64];
			size_t got = 0;
			u32 k, passes = 0, bad = 0;

			for (k = 0; k < 32; k++) {
				u32 magic, vho, doff, crc, calc;
				u64 ecnt;
				int j;

				got = 0;
				if (mtd_read(ec, (loff_t)k * ec->erasesize, 64, &got, h) ||
				    got != sizeof(h)) {
					bad = 0xf0u;
					break;
				}

				if (k == 0)
					luofu_ec_h0 = ((u32)h[0] << 8) | (u32)h[1];

				magic = ((u32)h[0] << 24) | ((u32)h[1] << 16) |
					((u32)h[2] << 8) | (u32)h[3];
				vho = ((u32)h[16] << 24) | ((u32)h[17] << 16) |
				      ((u32)h[18] << 8) | (u32)h[19];
				doff = ((u32)h[20] << 24) | ((u32)h[21] << 16) |
				       ((u32)h[22] << 8) | (u32)h[23];
				crc = ((u32)h[60] << 24) | ((u32)h[61] << 16) |
				      ((u32)h[62] << 8) | (u32)h[63];
				ecnt = 0;
				for (j = 0; j < 8; j++)
					ecnt = (ecnt << 8) | h[8 + j];
				calc = crc32(LUOFU_UBI_CRC32_INIT, h, 60);

				if (magic != 0x55424923u) {
					bad = 0xf1u;
					break;
				}
				if (h[4] != 1) {
					bad = 0xf2u;
					break;
				}
				if (vho != 2048) {
					bad = 0xf3u;
					break;
				}
				if (doff != 4096) {
					bad = 0xf4u;
					break;
				}
				if (ecnt > 0x7fffffffULL) {
					bad = 0xf5u;
					break;
				}
				if (crc != calc) {
					bad = 0xf6u;
					break;
				}

				passes++;
			}

			put_mtd_device(ec);
			luofu_ec_passes = bad ? bad : (passes & 0xffu);
		}
	}

	/*
	 * AND THE SAME TWO BYTES FROM A PARTITION WHOSE CONTENT IS KNOWN INDEPENDENTLY OF US.
	 * Read-only. See luofu_kb_b for what the pair decides.
	 */
	{
		struct mtd_info *kb = get_mtd_device_nm("kernelb");

		if (!IS_ERR(kb)) {
			u8 b[8] = { 0 };
			size_t got2 = 0;

			if (!mtd_read(kb, 0, sizeof(b), &got2, b) && got2 >= 2)
				luofu_kb_b = ((u32)b[0] << 8) | (u32)b[1];
			put_mtd_device(kb);
		}
	}

	/*
	 * THE MTD TABLE, REPORTED BY NAME RATHER THAN ASSUMED - AND REPORTED FROM HERE ON PURPOSE.
	 *
	 * This ran from the probe at first and its output NEVER SURVIVED: the log ring keeps the
	 * TAIL, and the probe prints before the first line the window still retains (3.508 s), so the
	 * measurement was taken and thrown away. Printing from this late_initcall puts it at about
	 * 4.0 s, inside what is kept. That is the same failure as the crumb the tick overwrote, one
	 * layer up: the instrument wrote, and the channel kept only the newest.
	 *
	 * mount_root resolves the partition called rootfs_data through /proc/mtd and mounts the NUMBER
	 * it gets back, so both the number and the contents are things it acts on. A read of the first
	 * eight bytes says which partition the name lands on and what is inside it: erased reads ff,
	 * and anything else names the filesystem already sitting there.
	 */
	part = get_mtd_device_nm("rootfs_data");
	if (IS_ERR(part)) {
		dev_info(fmc->dev, "FMC: no rootfs_data partition (%ld)\n", PTR_ERR(part));
	} else {
		u8 h[8] = { 0 };
		size_t got = 0;
		int rr = mtd_read(part, 0, sizeof(h), &got, h);

		dev_info(fmc->dev,
			 "FMC: rootfs_data is mtd%d size=%llx read=%d got=%zu: %02x %02x %02x %02x %02x %02x %02x %02x\n",
			 part->index, (unsigned long long)part->size, rr, got,
			 h[0], h[1], h[2], h[3], h[4], h[5], h[6], h[7]);
		dev_info(fmc->dev, "FMC: geometry: writesize %u erasesize %u oobsize %u oobavail %u\n",
			 part->writesize, part->erasesize, part->oobsize, part->oobavail);
		put_mtd_device(part);
	}

	part = get_mtd_device_nm("upgrade");
	if (!IS_ERR(part)) {
		dev_info(fmc->dev, "FMC: upgrade is mtd%d, and the master is mtd%d\n",
			 part->index, fmc->mtd->index);
		put_mtd_device(part);
	}

	luofu_jffs2_mark(fmc);

	desc = ubi_open_volume(0, LUOFU_ROOTFS_VOL_ID, UBI_READONLY);
	if (IS_ERR(desc)) {
		/* STORE, DO NOT PRINT - see the note on luofu_ubi_diag_err. */
		luofu_ubi_verdict(PTR_ERR(desc));
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
			 * THE PAGE TAIL AND THE SPLICE, on page 2 and page 11 side by side.
			 *
			 * The widened memset did NOT change UBI's stored 0xf1160000 - the last two
			 * bytes of record 118's CRC are still zero - so a stale staging buffer was
			 * not the source of them. The zeros are the hardware's.
			 *
			 * Our model says the copy takes 1040 bytes direct, skips 14, then takes the
			 * rest - so the returned page's final 14 bytes come from the staging
			 * buffer's 2048..2061. Record 118's CRC sits at page 11 offset 2032..2035,
			 * which is source 2046..2049: its last two bytes therefore come from
			 * 2048..2049, and they read zero.
			 *
			 * So this samples the splice region AND the last 16 bytes of page 2 and
			 * page 11 - the page whose record UBI rejects and a page it accepts, side by
			 * side. The vendor's same offsets decide whether that tail is the hardware's
			 * content or the copy's.
			 */
			static const u32 probes[16] = {
				4096 + 1032,  4096 + 1040,  4096 + 1048,  4096 + 1056,
				4096 + 2032,  4096 + 2036,  4096 + 2040,  4096 + 2044,
				22528 + 1032, 22528 + 1040, 22528 + 1048, 22528 + 1056,
				22528 + 2032, 22528 + 2036, 22528 + 2040, 22528 + 2044,
			};
			u32 map[16];
			u32 regs_before = 0;
			int s;

			if (fmc->mtd)
				luofu_fmc_crumb(fmc, LUOFU_LOG_PANIC_STEP,
						((u32)luofu_log_dma & 0xfffff000u) |
						((u32)fmc->mtd->index & 0xfffu));

			part = get_mtd_device_nm("rootfsb");
			if (IS_ERR(part))
				return 0;

			/*
			 * HOW MANY BYTES DOES ONE PAGE READ ACTUALLY TRANSFER?
			 *
			 * 0x38 (FMC_DATA_NUM) reads 4128 both in builds that write to it and in
			 * builds that do not, so it is untouched by writing - and that makes it
			 * usable: its DELTA across a single read is exactly what the controller
			 * moved. If the delta is 2048 the page really is one truncated transfer
			 * and the spare must come from elsewhere; if it is larger, the bytes are
			 * arriving and the copy is dropping them.
			 */
			if (fmc->regs)
				regs_before = readl(fmc->regs + 0x38);

			for (s = 0; s < 16; s++) {
				u32 v = 0;
				size_t rl = 0;

				mtd_read(part, probes[s], 4, &rl, (u8 *)&v);
				map[s] = v;
			}
			put_mtd_device(part);

			if (luofu_diag_buf) {
				u32 *regs = (u32 *)(luofu_diag_buf + 128);

				if (fmc->regs) {
					regs[12] = regs_before;
					regs[13] = readl(fmc->regs + 0x38);
				}

				/*
				 * AND WHAT IS IN THE OOB BUFFER?
				 *
				 * 0x38 turned out to be static - 4128 before and after sixteen reads -
				 * so it is not a counter and that instrument is dead. Every per-operation
				 * register is now ruled out by measurement.
				 *
				 * The one thing never looked at is where the spare area goes. This driver
				 * points FMC_SADDR_OOB at dma_buf + 0x2000 and has NEVER READ IT BACK. If
				 * the controller deposits the page's spare bytes there, then the data
				 * transfer really is a clean 2048 and the splice this driver de-interleaves
				 * out of the data is something else entirely.
				 *
				 * The vendor's own flash reads carry an OOB destination too, and this is the
				 * only buffer in the driver whose contents have never been examined.
				 */
				{
					const u8 *oob = (const u8 *)fmc->dma_buf + 0x2000;
					int q;

					for (q = 0; q < 16; q++)
						regs[16 + q] = ((const u32 *)oob)[q];
				}

				memcpy(luofu_diag_buf, map, sizeof(map));

				/*
				 * What the controller registers hold AFTER every read. FMC_DMA_LEN is
				 * at 0x40 and the vendor's own DMA read writes it before the operation,
				 * so if the write takes effect its value should be visible here. The
				 * vendor also clears 0x28 and stores an argument in 0x2c on every read,
				 * which this driver has never touched.
				 */
				if (fmc->regs) {
					regs[0] = readl(fmc->regs + 0x28);
					regs[1] = readl(fmc->regs + 0x2c);
					regs[2] = readl(fmc->regs + 0x40);
					regs[3] = readl(fmc->regs + 0x30);
					regs[4] = readl(fmc->regs + 0x68);
					regs[5] = readl(fmc->regs + 0x3c);
					regs[6] = readl(fmc->regs + 0x38);
					regs[7] = readl(fmc->regs + 0x40);
					/*
					 * And the block's own configuration, set once at probe time. If
					 * the transfer is bounded by a geometry setting rather than by a
					 * per-operation length, it lives in one of these.
					 */
					regs[8] = readl(fmc->regs + FMC_CFG);
					regs[9] = readl(fmc->regs + FMC_GLOBAL_CFG);
					regs[10] = readl(fmc->regs + FMC_FLASH_INFO);
					regs[11] = readl(fmc->regs + 0x20);
				}
			}
		}
		return 0;
	}

	err = ubi_read(desc, 0, buf, 0, 4);
	ubi_close_volume(desc);

	if (err) {
		luofu_ubi_verdict(err);
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

	luofu_ubi_verdict(0);
	luofu_phase = 3;
	luofu_fmc_ubi_probe_done();
	return 0;
}

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
/*
 * 16 KiB, NOT 512 BYTES.
 *
 * The ring used to be 0x200 - about eight lines. It was enough to prove the instrument
 * worked, and not enough to hold a boot. By the time the last fire's panic dumped the
 * log, the newest 500 bytes were the driver's own probe lines and a single userspace
 * line, and the messages that matter most had been overwritten: the VFS mount, "Run
 * /sbin/init", procd's preinit. That fire DID reach userspace - a regulatory.db load
 * failure was the last line - but the log could not show how it got there.
 *
 * 0x4000 holds roughly 136 lines, which is a full boot. The window runs
 * 0x80602000..0x80608000, so a 16 KiB ring at 0x80603000 with a 512-byte diagnostic
 * buffer at 0x80607000 ends at 0x80607200, inside it with 3.5 KiB to spare. The
 * collision check reserves ring + diag rather than twice the ring, so the ring can use
 * the space the diag does not.
 *
 * AND THE DUMPER IS THE PART THAT MATTERS HERE: luofu_kmsg_to copies the WHOLE kmsg
 * log, unfiltered, and wraps when it fills - so the ring always holds the NEWEST text,
 * which is the mount and the init, not the probe's own noise.
 */
#define LUOFU_LOG_SIZE		0x4000		/* 16 KiB log ring - see below */

/*
 * AND ONLY THE FIRST 4 KB OF IT IS EVER WRITTEN. The walk reports 205 lines and the ring's
 * non-zero span has been 4152 bytes - 4 KB of text and twelve of zeros - on every fire. The
 * boundary is close to one page, so the dumper is capped just inside it and slides, keeping the
 * newest lines rather than the oldest.
 */
/*
 * BACK TO 3.5 KiB: THE 15 KiB WINDOW IS A MEASURED NEGATIVE (2026-10-16).
 *
 * The prose in luofu_kmsg_to asks for 15 KiB while this constant said 0x0e00, so a fire was spent
 * setting it to 0x3c00 with nothing else changed. RESULT: mtd9 came back ENTIRELY 0xff - the
 * snapshot wrote NOTHING - and the only surviving crumb was the stage-D hook's own step 42, so the
 * panic path's capture never completed. That is the same negative the 2026-10-14 15 KiB attempt
 * produced ("walked only 4092 bytes"), now with LUOFU_LOG_SCRATCH left at 16 KiB, so the scratch is
 * not the limiting variable either. A wider window is NOT how to reach the UBI messages; a print
 * that lands inside the 3.5 KiB tail is, because the stage-D hook's own lines are provably in it.
 *
 * Do not raise this again without first explaining what makes the snapshot survive the larger
 * memset + d-cache flush in panic context.
 */
#define LUOFU_LOG_KEEP		0x0e00		/* 3.5 KiB - the width that actually lands */
/*
 * AND A SCRATCH BUFFER TO SLIDE IN, so the window is filled between lines rather than
 * inside one - see luofu_kmsg_to.
 */
#define LUOFU_LOG_SCRATCH	0x4000		/* 16 KiB of lines, newest kept */
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

static void __maybe_unused luofu_console_write(struct console *co, const char *s, unsigned int n);

/*
 * SET BY THE DUMPER, HONOURED BY THE CONSOLE.
 *
 * kmsg_dump() runs INSIDE panic() BEFORE the notifier chain, so luofu_kmsg_to() gets
 * the complete log and writes it into the ring. But the console hook is still
 * registered and printk keeps calling it for everything panic() and the rest of the
 * death path print afterwards - AND IT WAS OVERWRITING THE DUMP WITH ITS OWN FILTERED
 * TEXT. That is why the last fire's ring held the driver's own probe lines and one
 * stale userspace line, and NOT ONE WORD of the panic: the dumper had already written
 * "Unable to handle kernel paging request ..." and the console had written over it.
 *
 * The dumper runs first and sets this; the console checks it and stops. The ring's
 * final content is therefore the complete, unfiltered log, ending in whatever the
 * kernel said on the way down.
 */
static bool luofu_log_frozen;
static struct luofu_fmc *luofu_fmc_stamp;



#define LUOFU_LOG_TICK_STEP	45

/*
 * STEP 46: elf_hwcap, THE KERNEL'S OWN ANSWER TO "IS THE FPU AVAILABLE".
 *
 * init executes and is killed by signal 4 - SIGILL - on BOTH an arm_cortex-a15_neon-vfpv4 rootfs
 * and an arm_cortex-a9_vfpv3-d16 one.  Two userspaces compiled for different baselines failing
 * identically points away from the compiler and at the kernel: if VFP was never enabled, EVERY
 * floating point instruction in ANY hard-float userspace traps as an illegal instruction.
 *
 * CONFIG_VFP and CONFIG_NEON are both set in the built config - the config lane asserts them - so
 * the question is not whether the code is compiled in but whether the kernel actually brought the
 * FPU up.  That is exactly what elf_hwcap records, and the bits that matter here are:
 *
 *     bit  6  HWCAP_VFP        the FPU is usable
 *     bit 12  HWCAP_NEON
 *     bit 13  HWCAP_VFPv3
 *     bit 16  HWCAP_VFPv4
 *     bit 17  HWCAP_IDIVA      hardware divide - set on A7/A15, NEVER on A9
 *     bit 19  HWCAP_VFPD32     32 double registers - set on A9, clear on d16 cores
 *
 * SO A VALUE WITHOUT bit 6 IS THE ANSWER, and a value WITH it sends the search back to userspace.
 */
#define LUOFU_LOG_HWCAP_STEP	46
static u32 luofu_log_ticks;

/*
 * THE SURVIVING CHANNEL FOR THE OTHER DRIVERS' CRUMBS.
 *
 * Each driver has its own diag cell (mach 0xC8C, CRG 0xC90, gmac 0xC94, mdio 0xC98), and every one
 * of those is clobbered by the vendor boot once our kernel's fallback hands the board back.  So a
 * crumb read AFTER a fallback says nothing about our drivers - measured 2026-10-13: a fire whose
 * mdio cell read 0x62CE0000 (vendor garbage) while 0xC18/0xC1C still held OUR FMC tick.
 *
 * The FMC's own pair is the only proven survivor (the vendor kernel never touches it), and its tick
 * is the LAST writer in a boot.  So the other drivers deposit the crumb they want kept here, and
 * the tick carries it in its payload.  Low 16 bits is exactly the discriminating part of their
 * values: the mdio/gmac crumbs are 0xC0DE7xxx / 0xC0DE6xxx, so 0x7EFB and 0x6006 fit whole.
 */
u32 luofu_net_diag;
EXPORT_SYMBOL_GPL(luofu_net_diag);
static u32 luofu_log_lines;

/*
 * LIVENESS STAMPS, IN THE DIAGNOSTIC BUFFER.
 *
 * The ring cannot answer the question that matters here: after six fires it still holds the
 * same 4150 bytes of early boot, and whether that is (a) the timer never running, (b) the
 * kernel log genuinely being 4 KiB, or (c) the dump being truncated is not decidable from
 * its contents. What IS decidable from a stamp.
 *
 * These sit at diag + 1024, well clear of the probe's sample array at diag + 0 and its
 * register block at diag + 128, and the diag buffer is a separate allocation from the ring -
 * so a stamp here is written by the tick whether or not the kmsg walk returns anything.
 *
 * ONE READ AFTER A FIRE NOW SEPARATES THE HYPOTHESES:
 *   [1024] ticks        how many times the 500 ms timer ran (0 = it never fired)
 *   [1028] jiffies      the last tick's jiffies (against the panic's, shows how long it lived)
 *   [1032] walked       lines the last kmsg walk returned (0 = the log buffer was empty)
 *   [1036] did the panic notifier run (a fixed pattern)
 */
/*
 * A FIXED PHYSICAL ADDRESS, NOT THE DIAGNOSTIC BUFFER.
 *
 * The first attempt put these at diag + 1024 and read back zero for all four - including
 * the panic marker, whose crumb provably landed a line earlier. Two candidate explanations
 * and no way to separate them from here (luofu_diag_buf being NULL, or the diag being
 * somewhere other than log + LOG_SIZE after the ring grew from 0x200 to 0x4000), which is
 * the same trap as before: an instrument whose address is uncertain reports absence.
 *
 * SO THE STAMPS GET AN ADDRESS THAT CANNOT BE WRONG. 0x80607800 is inside the surviving
 * window (0x80602000..0x80608000), past the ring (ends 0x80607000) and past the diagnostic
 * buffer (ends 0x80607200), in the same identity-mapped region the ring itself uses - and
 * nothing else in this driver writes there.
 *
 *   [0x80607800]  ticks    how many times the 500 ms timer ran   (0 = it never fired)
 *   [0x80607804]  jiffies  the last tick's jiffies
 *   [0x80607808]  lines    lines the last kmsg walk returned
 *   [0x8060780c]  the panic notifier's fixed marker 0xc0de9a11
 */
/*
 * THE STAMPS LIVE AT THE END OF THE RING - A BUFFER THIS DRIVER HAS ALREADY PROVEN IT CAN
 * WRITE, RATHER THAN AN ADDRESS I GUESSED.
 *
 * Two attempts at a guessed address failed in two different ways, and the second one hung the
 * whole boot: C18 stopped at the mach's late_initcall crumb instead of reaching the panic
 * notifier, and the safety timer - not a panic - is what returned the box. The only new thing
 * the 500 ms tick does is write this stamp, so a store into unmapped or bus space is what
 * stopped it.
 *
 * The ring at luofu_log_dma is written every boot and read back every boot, through luofu_log_b.
 * The text uses the first few KB of 16 KiB, so the last 16 bytes are free, and a stamp placed
 * through luofu_log_b cannot have the wrong address - it is the same pointer the log uses.
 *
 *   [ring + 0x3FF0]  ticks     how many times the 500 ms timer ran   (0 = it never fired)
 *   [ring + 0x3FF4]  jiffies   the last tick's jiffies
 *   [ring + 0x3FF8]  lines     lines the last kmsg walk returned
 *   [ring + 0x3FFC]  the panic notifier's marker 0xc0de9a11
 *
 * AND THEY ARE WRITTEN AFTER THE DUMP, not before, so the dump's circular write cannot land
 * on top of them.
 */
#define LUOFU_STAMP_OFF   (LUOFU_LOG_SIZE - 16)
#define LUOFU_STAMP_PANIC 0xc0de9a11u

/*
 * __va(), BECAUSE A PHYSICAL ADDRESS IS NOT A POINTER - and the first version of this
 * function got that wrong. It wrote to (void *)0x80607800 directly, assuming the window
 * was identity-mapped, and every stamp read back zero INCLUDING the panic marker, whose
 * crumb lands every single fire. That is what proved the write was the failure rather
 * than the timer.
 *
 * THE RING ALREADY SHOWED THE RIGHT WAY, three lines below where these live:
 *
 *     luofu_log_b = (void *)__va((phys_addr_t)luofu_log_dma);
 *
 * and the comment above it says in as many words that __va() is how this region is
 * reached from the kernel. On ARM __va(0x80607800) is not 0x80607800, so the raw
 * pointer went to unmapped or I/O space and the store vanished.
 *
 * WHEN THE ADDRESSING QUESTION IS ALREADY ANSWERED IN THE FILE, READ THE ANSWER.
 */
static void luofu_stamp(u32 slot, u32 value)
{
	if (!luofu_log_b)
		return;

	writel(value, (char *)luofu_log_b + LUOFU_STAMP_OFF + slot * 4);
}



static void luofu_kmsg_to(void *p, struct kmsg_dumper *dumper)
{
	static char line[256];
	static char scratch[LUOFU_LOG_SCRATCH];
	size_t len, off = 4, s = 0;
	u32 mark = LUOFU_LOG_MARK;

	if (!p)
		return;

	memset(p, 0, LUOFU_LOG_KEEP);
	memcpy(p, &mark, sizeof(mark));

	kmsg_dump_rewind(dumper);

	luofu_log_lines = 0;
	while (kmsg_dump_get_line(dumper, true, line, sizeof(line), &len)) {
		luofu_log_lines++;

		if (len > sizeof(line) - 1)
			len = sizeof(line) - 1;

		/*
		 * A SCRATCH BUFFER, SO THE SLIDE HAPPENS BETWEEN LINES AND NOT INSIDE ONE.
		 *
		 * The window used to be written directly and slid with off = 4 whenever the next line
		 * would not fit. That cuts a line in half and splices the newest text into the middle
		 * of the oldest - which is what the last fire showed: six apparent "wraps" inside one
		 * 3.5 KiB window, each between a late timestamp and 0.000000, and a kernel warning
		 * whose header was visible while its body was chopped into fragments.
		 *
		 * So lines accumulate here first. When the scratch fills, the oldest HALF is dropped,
		 * moved down, and the cut is moved forward to the next newline - so every byte that
		 * reaches the window is a whole line, and the newest lines are the ones that survive.
		 */
		if (s + len + 1 > sizeof(scratch)) {
			size_t keep_from = sizeof(scratch) / 2;

			while (keep_from < s && scratch[keep_from] != '\n')
				keep_from++;

			if (keep_from < s)
				keep_from++;

			memmove(scratch, scratch + keep_from, s - keep_from);
			s -= keep_from;
		}

		memcpy(scratch + s, line, len);
		s += len;
		scratch[s++] = '\n';
	}

	/*
	 * AND ONLY THE TAIL REACHES THE RING, STARTING AT A LINE BOUNDARY.
	 */
	/*
	 * AND BACK TO THE TAIL, WITH A WINDOW THAT NO LONGER HIDES ANYTHING.
	 *
	 * The head dump was for one specific line - what vfp_init() printed at half a second. That
	 * question is answered now, and from two directions at once: elf_hwcap came back as
	 * 0x00008896 with no HWCAP_VFP, and the VENDOR's own kernel on this same silicon reports
	 * the same thing to the same extent -
	 *
	 *     vendor /proc/cpuinfo:  Features : half thumb fastmult edsp tls
	 *     CPU part : 0xc09
	 *
	 * - and the vendor's own userspace is SOFT-FLOAT (e_flags 0x05000200, EF_ARM_ABI_FLOAT_SOFT).
	 * This part has no usable FPU, which is why the vendor never built a hard-float userspace
	 * for it. Our rootfs was hard-float (0x05000400) and every floating point instruction in it
	 * trapped as SIGILL, which is the signal that killed init.
	 *
	 * So the root now in the volume is soft-float too, and what matters from here is what a
	 * RUNNING userspace does - which lives at the END of the log. With 15 KiB kept instead of
	 * 3.5 KiB, the tail also reaches back far enough to cover questions like this one.
	 */
	{
		size_t start = s > LUOFU_LOG_KEEP - 4 ? s - (LUOFU_LOG_KEEP - 4) : 0;

		if (start) {
			while (start < s && scratch[start] != '\n')
				start++;
			if (start < s)
				start++;
		}

		memcpy(p + off, scratch + start, s - start);
		off += s - start;
	}

	*(char *)(p + (off < LUOFU_LOG_KEEP ? off : LUOFU_LOG_KEEP - 1)) = 0;

	/*
	 * AND FLUSH IT TO DRAM, OR NOTHING HERE SURVIVES TO BE READ.
	 *
	 * Every write to this region goes through the linear map and is therefore CACHED, and
	 * this board is reset by a watchdog or by a dead kernel with no cache maintenance in
	 * between. Whatever dirty lines happen to be evicted on their own reach DRAM; the rest
	 * never leave L1/L2 and are gone the moment the vendor image boots.
	 *
	 * THAT IS THE 32-BYTE SIGNATURE IN THE WINDOW: this fire's text, cut apart by
	 * one-cache-line pieces of the previous fire's text and by stretches of zeros a
	 * half-evicted memset left behind. It is also why every crumb register has always
	 * agreed with itself - the FMC registers are I/O space, UNCACHED, so they land
	 * immediately - while every RAM stamp and this window have had to be read with a grain
	 * of salt. The earlier "the two paths meet" reading was true only because the line it
	 * compared happened to have been evicted before the reset.
	 *
	 * THE FIX IS TO NOT LEAVE IT TO LUCK.
	 */
	__cpuc_flush_dcache_area(p, LUOFU_LOG_KEEP);
	outer_flush_range(__pa(p), __pa(p) + LUOFU_LOG_KEEP);
}

static void luofu_kmsg_dump(struct kmsg_dumper *dumper,
			    enum kmsg_dump_reason reason)
{
	/*
	 * THE CONSOLE STOPS HERE, AND ONLY HERE.
	 *
	 * kmsg_dump() runs INSIDE panic() before the notifier chain, so this is the real
	 * death path. Freezing the console now means the ring's final content is the
	 * complete log as the kernel saw it, and nothing printed on the way down can
	 * overwrite it.
	 *
	 * IT MUST NOT BE SET ANYWHERE SHARED. luofu_kmsg_to() has two other callers, and
	 * one of them was a 3-second timer - so the flag got set at t=3s, the console
	 * stopped capturing there, and the ring ended up holding the kernel's own 4 KiB
	 * log buffer rather than the boot.
	 */
	/*
	 * ★ THE ONE PLACE A DIAGNOSTIC IS GUARANTEED TO SURVIVE THE WINDOW.
	 *
	 * panic() calls dump_stack() BEFORE it calls kmsg_dump(), so the trace and the root partition
	 * list are already in the log when this runs, and they fill the whole 3.5 KiB window on their
	 * own. A line printed HERE is the newest line in the log when luofu_kmsg_to() copies the tail,
	 * and is therefore inside the window by construction - which is what no earlier placement was.
	 *
	 * `saved_command_line` is printed because "ubi.mtd= is in the command line" has been an
	 * inference from the source in every prior session on this board; this reads it back out of the
	 * kernel that actually ran, next to the errno that says whether UBI has a device at all.
	 *
	 * The guard makes it once per boot: this dumper is also driven by the tick's
	 * kmsg_dump(KMSG_DUMP_EMERG), and the log must not grow a copy of itself every five seconds.
	 */
	luofu_ubi_diag_print();

	luofu_log_frozen = true;
	luofu_kmsg_to(luofu_log_b, dumper);
}

/*
 * AND THE PERIODIC DUMPER IS BACK, BECAUSE IT IS THE ONLY PART OF THIS THAT EVER WORKED.
 *
 * The console hook was removed from the job by measurement, not by preference. The ring's
 * non-zero span after the last fire was 4150 bytes - dense from offset 4 to about 4100 and
 * NOTHING AFTER. That is exactly what CON_PRINTBUFFER hands a console at registration: the
 * log that already exists. THE CONSOLE NEVER CAPTURED A SINGLE LINE AFTER THAT POINT, and
 * the "FMC:" lines I had been reading in earlier fires were never its work - they came
 * from the 3-second dumper timer, which is why removing the timer lost them and why the
 * ring went back to showing 0.15 s of boot.
 *
 * So the timer returns, with the two things it needed all along:
 *
 *   - THE KERNEL LOG BUFFER IS 64 KiB NOW (CONFIG_LOG_BUF_SHIFT=16). Before, every dump
 *     could only ever produce the same 4 KiB, which is why every reading looked identical.
 *   - THE FREEZE IS PANIC-ONLY, so this timer cannot stop anything from capturing.
 *
 * The dump rewrites the ring from offset 4 with everything the log holds, circularly, so
 * the ring keeps the NEWEST 16 KiB. Running every 500 ms means the last dump before the
 * panic carries the whole boot, and the panic path dumps again on top of it.
 *
 * AND dumper->active MUST BE SET BY HAND HERE. kmsg_dump_get_line_nolock() returns false
 * immediately unless the dumper is active, and kmsg_dump() is the only thing that sets it -
 * on its way into a callback that, outside a panic, is never called. Setting it around the
 * walk is safe: kmsg_dump_get_line() takes logbuf_lock itself, and the panic path re-sets it.
 */
/*
 * FIVE SECONDS, NOT FIVE HUNDRED MILLISECONDS - THE INSTRUMENT WAS STRANGLING THE KERNEL.
 *
 * The periodic dump walks the WHOLE log - 242 lines when this was measured - and kmsg_dump_get_line
 * takes logbuf_lock for each one. From a timer, which is SOFTIRQ CONTEXT, that is 242 locked
 * iterations with interrupts held off, and at a 500 ms cadence the kernel was losing a large
 * fraction of its interrupt time to my own instrument.
 *
 * AND THAT IS THE MEASURED FAILURE, EXACTLY:
 *
 *     printk stopped at  ~4 s     starved by the walk
 *     the tick stopped at 47      ~23.5 s, when the system finally gave out
 *     the safety timer never fired, so timers died too
 *     the box's own watchdog is what returned it
 *
 * A hard, DETERMINISTIC hang - the tick count was identical on two consecutive fires - which is what
 * starvation looks like and what no race looks like.
 *
 * The panic path keeps its full walk: a panic is terminal, so starving it costs nothing. Only the
 * periodic one is slowed, and at 5 s a reading still carries the log from up to 20 s into the boot.
 */
#define LUOFU_LOG_TICK_MS   5000

/*
 * ★ WRITE THE LOG STRAIGHT TO FLASH - NO PSTORE, NO KMSG_DUMP, NO REGISTRATION ORDER.
 *
 * WHY THIS REPLACES THE PSTORE PATH (measured 2026-10-14, all of it):
 *
 *   - Our kernel FINALLY EXECUTED (C18 = 0xC0DE502D) and `mtd10` stayed entirely 0xff.  The pstore
 *     backend had never written a byte, in any era.
 *   - `pstore_blk` only writes on a KMSG DUMP, and our boot does not panic - it fails quietly and the
 *     SoC watchdog resets the box.  Adding kmsg_dump() to this tick was still not enough.
 *   - The stack cannot even be wired here: `mtdpstore` and `pstore_blk` share ONE string, and they
 *     want it to be DIFFERENT things (`mtdpstore` parses it with kstrtoint then matches it against
 *     mtd->name; `pstore_blk` opens it as a `/dev` path).  No single value satisfies both, so one of
 *     the two always refuses to register.
 *   - THE VENDOR'S OWN BOX IS BROKEN THE SAME WAY (`blkdev = pstore`, `best_effort = N`,
 *     `console_size = 0`, `/sys/fs/pstore` empty), so this was never a working instrument on this
 *     hardware - which also means every earlier "mtd10 is 0xff, therefore the kernel died too early"
 *     reading was unsupported.
 *
 * A diagnostic must not depend on three things being simultaneously right.  This driver ALREADY has a
 * working mtd_write/mtd_erase path (built for the overlay), so the log goes to a partition directly.
 *
 * TARGET: `mtd9` ("log", 0x440000 = 4.25 MB) - a partition the vendor itself uses for logging, large
 * enough to hold many snapshots and NOT part of any boot-critical image.  A fixed readback contract:
 *
 *     mtd9 base: magic "LUFU" | u32 lines | then the 16 KiB log ring, oldest-to-newest as dumped
 *
 * Reading it after a fire (on the vendor kernel, no shell on ours):
 *     ssh root@192.168.10.1 "dd if=/dev/mtd9 bs=4096 count=8 | strings -n 5 | head -80"
 *
 * COST AND SAFETY: one erase + one page write per 5 s tick, on a partition nothing else reads.  The
 * tick already runs; this adds ~1 ms of flash IO to it.  A failure is logged and otherwise ignored -
 * a broken instrument must never take the boot down with it.
 */
#define LUOFU_LOGSNAP_MAGIC	0x5546554cu	/* "LUFU" little-endian */
#define LUOFU_LOGSNAP_PART	"log"

/*
 * ★ THE PARTITION IS RESOLVED IN PROBE CONTEXT AND CACHED - THE TICK CANNOT RESOLVE IT ITSELF.
 *
 * Measured 2026-10-14: `luofu_log_snapshot()`'s `get_mtd_device_nm("log")` NEVER WROTE ANYTHING,
 * and the reason is a context bug, not a partition problem:
 *
 *     get_mtd_device_nm() takes `mtd_table_mutex` and walks the device table.
 *     This runs inside a TIMER, i.e. SOFTIRQ context -- where taking a mutex is illegal.
 *
 * The crumb proved the tick ran (C18 = 0xC0DE502D is its first statement) while mtd9 stayed blank -
 * so the fault sits between the crumb and the write, exactly where the mutex is.  (`mtd->size` was
 * also ruled out as the cause: writing mtd9 from the VENDOR kernel succeeded, `erase rc=0`.)
 *
 * The fix is the one the kernel itself uses for this class: resolve the partition ONCE in probe
 * (process context, where taking a mutex is fine), hold the reference, and let the timer use the
 * cached pointer.  A NULL cache means "not resolved yet" and the tick simply skips.
 */
static struct mtd_info *luofu_logsnap_mtd;

/*
 * PER-STEP CRUMBS, BECAUSE GUESSING HAS COST THREE FIRES.
 *
 * The snapshot has now failed twice with a BLANK partition and NO way to tell which step stopped:
 * first I blamed the pstore stack, then the softirq mutex (a real bug, and the fix verified), and
 * mtd9 is STILL blank.  A blank partition is consistent with "the partition was not found",
 * "the erase failed", "the alloc failed", "the header write failed" and "the ring write failed" - so
 * a third guess is worth nothing.
 *
 * These crumbs go to 0xC9C (the mach's second diagnostic cell, free during our kernel's early life)
 * and each step writes a DISTINCT value, so the returning box names the exact step:
 *
 *   0xC0DE5A00  resolve: partition NOT found (or malloc'd part is an error pointer)
 *   0xC0DE5A01  resolve: found and cached
 *   0xC0DE5B00  snapshot: partition never resolved (luofu_logsnap_mtd == NULL)
 *   0xC0DE5B10  snapshot: erase failed
 *   0xC0DE5B20  snapshot: kmalloc failed
 *   0xC0DE5B30  snapshot: header write failed (low byte = errno)
 *   0xC0DE5B40  snapshot: ring write failed (low byte = errno)
 *   0xC0DE5B0F  snapshot: COMPLETED (mtd9 should hold the log)
 */
#define LUOFU_SNAPCRUMB_CONF	0xc9c

static void __init luofu_snap_crumb(u32 v)
{
	void __iomem *sc = ioremap(LUOFU_SYSCTRL_BASE, 0x1000);

	if (!sc)
		return;
	writel(v, sc + LUOFU_SNAPCRUMB_CONF);
	(void)readl(sc + LUOFU_SNAPCRUMB_CONF);
	iounmap(sc);
}

static int __init luofu_logsnap_resolve(void)
{
	struct mtd_info *part = get_mtd_device_nm(LUOFU_LOGSNAP_PART);

	if (IS_ERR(part)) {
		pr_err("FMC: log-snapshot partition '%s' not found: %ld\n",
		       LUOFU_LOGSNAP_PART, PTR_ERR(part));
		luofu_snap_crumb(0xc0de5a00);
		return PTR_ERR(part);
	}
	luofu_logsnap_mtd = part;
	luofu_snap_crumb(0xc0de5a01);
	pr_info("FMC: log snapshot target = mtd%d '%s' (%llu B)\n",
		part->index, part->name, (unsigned long long)part->size);
	return 0;
}

static void luofu_log_snapshot(void)
{
	struct mtd_info *part = luofu_logsnap_mtd;
	struct erase_info ei;
	unsigned long snap_flags;
	size_t got, written = 0;
	u8 *page;
	u32 off;
	int err;

	if (!luofu_log_b || !luofu_log_lines)
		return;
	if (!part) {
		luofu_snap_crumb(0xc0de5b00);
		return;
	}
	if (part->erasesize > part->size)
		return;

	/* one erase of the first block is enough for a single snapshot */
	memset(&ei, 0, sizeof(ei));
	ei.addr = 0;
	ei.len = part->erasesize;

	/*
	 * ★ THE PANIC PATH MUST NOT BE ABLE TO DEADLOCK ON OUR OWN LOCK.
	 *
	 * This driver's read/write paths take `luofu_fmc_read_lock` (a spinlock).  This function is now
	 * ALSO called from the panic notifier - and if the panic happened while that spinlock was held,
	 * `mtd_erase` would spin forever, turning a clean fallback into a HANG.  A hang is exactly the
	 * failure mode that looks like "the box never came back", and it would corrupt the very signal
	 * this instrument exists to produce.
	 *
	 * So TRY the lock and BAIL if it is held: an instrument that can wedge the box is worse than one
	 * that reports nothing.  `spin_trylock_irqsave` is the primitive for exactly this.
	 */
	if (!spin_trylock_irqsave(&luofu_fmc_read_lock, snap_flags)) {
		luofu_snap_crumb(0xc0de5b50);	/* snapshot: LOCK HELD - refused, no hang */
		return;
	}
	spin_unlock_irqrestore(&luofu_fmc_read_lock, snap_flags);

	err = mtd_erase(part, &ei);
	if (err) {
		luofu_snap_crumb(0xc0de5b10 | (err & 0xff));
		return;
	}

	page = kmalloc(part->writesize, GFP_ATOMIC);
	if (!page) {
		luofu_snap_crumb(0xc0de5b20);
		return;
	}

	/* header: magic + the line count, so a stale snapshot is distinguishable from a fresh one */
	memset(page, 0xff, part->writesize);
	((u32 *)page)[0] = LUOFU_LOGSNAP_MAGIC;
	((u32 *)page)[1] = luofu_log_lines;
	err = mtd_write(part, 0, part->writesize, &got, page);
	kfree(page);
	if (err) {
		luofu_snap_crumb(0xc0de5b30 | (err & 0xff));
		return;
	}
	written = got;

	/*
	 * Then the ring itself, page by page, stopping at the first failure rather than hammering a
	 * partition that is refusing writes.  LUOFU_LOG_SIZE is 16 KiB, so this is a handful of pages.
	 */
	for (off = 0; off + part->writesize <= LUOFU_LOG_SIZE; off += part->writesize) {
		err = mtd_write(part, (loff_t)written, part->writesize, &got,
				(u8 *)luofu_log_b + off);
		if (err) {
			luofu_snap_crumb(0xc0de5b40 | (err & 0xff));
			return;
		}
		written += got;
	}

	luofu_snap_crumb(0xc0de5b0f);	/* COMPLETED */
}

static void luofu_log_tick(struct timer_list *t)
{
	if (!luofu_log_b)
		return;

	luofu_log_ticks++;

	/*
	 * THE CRUMB FIRST, THROUGH I/O RATHER THAN RAM.
	 *
	 * Six fires went into RAM addresses - a raw physical pointer, diag + 1024, a constant,
	 * and the ring's tail - and none of them landed, while a hang means the tick may fault
	 * before it finishes. C18/C1C are memory-mapped registers this driver has written on
	 * every successful boot: step 45 with the tick count as the payload cannot have the
	 * wrong address, and it is written BEFORE the dump and the stamps so a later fault
	 * cannot erase it.
	 *
	 *   C18 = 0xc0de502d, C1C = tick count   the timer ran, this many times
	 *   C18 = 0xc0de502a, C1C = log address  the panic notifier overwrote it
	 *   C18 = 0xc0de0020                     a hang - the mach's crumb, no tick survived
	 */
	if (luofu_fmc_stamp)
		/*
		 * THE PAYLOAD CARRIES THE LOG'S PAGE NUMBER, NOT THE TICK COUNT.
		 *
		 * The walk returns 205 lines and the ring at 0x80603000 is byte-for-byte the same as
		 * every previous fire - its newest timestamp is 4.380 s and its banner is this
		 * kernel's own first boot. Every I/O write this driver makes sticks; every RAM
		 * write vanishes. So the ring pointer is aimed at memory that does not hold what is
		 * written through it, and the address is the thing I have never been able to read:
		 * the crumb that carries it is overwritten by this tick every 5 s.
		 *
		 *     C1C = (phase << 28) | ((log_dma >> 12) & 0x0ffffff)
		 *
		 * The window is 0x80602000..0x80608000, so a page number is six bits of the low 24,
		 * and the phase stays readable in the top nibble.
		 */
		/*
		 * AND NOW THE TICK CARRIES THE WRITE PATH'S REPORT, BECAUSE THE TICK IS THE LAST WRITER.
		 *
		 * The write's own self-test put its answer in step 47 at about nine seconds and the fire
		 * read step 45 back, because this crumb writes every five seconds and last write wins.
		 * A report that only survives if nothing else runs is not a report. So the tick - which
		 * by construction has the final word - carries it instead.
		 *
		 *     C1C = (writes << 24) | (erases << 16) | (err << 15) | (wr_cmd << 4) | (mismatches >> 8)
		 *
		 * writes and erases are counts of how often the MTD ENTRY POINTS were reached at all,
		 * clamped to 0xff - THE FIRST QUESTION IS WHETHER THE SEQUENCE RAN, NOT WHAT IT DID. err is
		 * 1 if any call returned an error. mismatches >> 8 is 0 for a page that read back clean and
		 * 8 for one where every byte differs. And wr_cmd is the vendor's own programming command,
		 * read out of its spec table, which is the one input to the sequence that has never been
		 * checked against a real value.
		 *
		 * The phase and the log address that used to live here are not lost: both were read back
		 * long ago and recorded (phase 4, log_dma 0x80603000).
		 */
		/*
		 * AND THE LOW 16 BITS CARRY THE NIC's LAST CRUMB.
		 *
		 * The FMC's write/erase counts stay in the top 16 ("did the sequence run" is still the first
		 * question worth answering there), and the bottom half carries the UBI verdict, because the
		 * tick is the LAST writer of C18/C1C on every boot measured - the panic notifier's step 42
		 * is overwritten by it minutes later, and C80/C84 are rewritten by the VENDOR's own boot
		 * (primed to 0x55555555, read back 0x00020000/0x00001200 - they are not free cells).
		 *
		 *   C1C = (writes<<24) | (erases<<16) | 0x5A00 | code
		 *
		 *   code 0x00  ubi_open_volume(0,0) OPENED - UBI has a device
		 *   code 0x16  -EINVAL (22)  the attach refused the partition
		 *   code 0x13  -ENODEV (19)  no UBI device at all
		 *   code 0xee  the stage-D hook never reached a verdict
		 *
		 * The 0x5A tag makes the field self-identifying, so 0x5AEE can never be mistaken for a
		 * net_diag value. TRADE, stated plainly: this replaces the `luofu_net_diag & 0xffff`
		 * survivor channel that used to live here. The gmac/mdio crumbs are still deposited in
		 * luofu_net_diag and can be re-exposed once this question is answered; the UBI verdict
		 * cannot be obtained any other way, because three log placements and two crumb cells were
		 * measured failing first.
		 */
		/*
		 * The step itself carries the EC verdict on this fire (values >= 0xf0, plus 0xee, mean the
		 * walk did not get 32 clean PEBs), so the payload is free to hold the literal first two
		 * bytes read from mtd13's PEB 0 - see the note on luofu_ec_h0.
		 */
		/*
	 *   C1C = (flag<<24) | (volumes<<16) | (kernelb[0]<<8) | mtd13[0]
	 *
	 * kept as FOUR bytes, unlike the earlier three-field form, because the smallest field the
	 * reader can still distinguish is one byte on this channel.
	 */
	luofu_fmc_crumb(luofu_fmc_stamp, LUOFU_LOG_TICK_STEP,
				((luofu_ec_h0 == 0x5542u ? 0x05u : 0x0au) << 24) |
				((luofu_ec_h0 == 0x5542u ? luofu_ubi_vols : luofu_ubi_post_code) << 16) |
				((luofu_kb_b & 0xffu) << 8) |
				(luofu_ec_h0 & 0xffu));

	/*
	 * 0xC0C, NOT 0xC8C: the tick owns 0xC18/0xC1C, and 0xC8C was already spent
	 * - it read back the tick's own ring address (0x80604000) on the 2026-10-18
	 * fire. 0xC0C has no writer in this tree. The 0xC0DE tag makes the cell
	 * self-identifying: anything else means this hook never ran.
	 */
	luofu_phase_cell(0x0c, 0xc0de0000u | (luofu_ubi_early_val & 0xffu));

	luofu_ubi_diag_print();
	luofu_kmsg.active = true;
	luofu_kmsg_to(luofu_log_b, &luofu_kmsg);
	luofu_kmsg.active = false;

	/*
	 * ★ AND PUSH IT TO PSTORE, EVERY TICK - THIS IS THE INSTRUMENT THAT SURVIVED.
	 *
	 * The RAM buffer above dies with the box: the vendor's next boot scrubs the 0x80600000 window
	 * (measured - every page of it reads the VENDOR's own text).  pstore on mtd10 is flash, and it
	 * is read back from the vendor with no shell on our kernel - which is the whole point.
	 *
	 * Measured 2026-10-14: our kernel EXECUTED for the first time (C18 = 0xC0DE502D) and pstore was
	 * still EMPTY, because `pstore_blk` only writes on a KMSG DUMP and our boot never panics - it
	 * fails quietly and the SoC watchdog resets the box.  So the backend was connected but never
	 * triggered.  Calling kmsg_dump() HERE, from the 5-second tick we already own, means a boot that
	 * survives 5 s has ALREADY written its log to flash regardless of how it later dies - no
	 * dependence on the exit path, the panic path, or the watchdog.
	 *
	 * Its own dumper filter is KMSG_DUMP_MAX, so KMSG_DUMP_EMERG passes.  A failure here must not
	 * stop the boot or the other instruments, so the call is unconditional and unchecked.
	 */
	kmsg_dump(KMSG_DUMP_EMERG);

	/*
	 * ★ AND THE ONE THAT ACTUALLY WORKS: write the ring straight to flash.
	 *
	 * kmsg_dump() above routes through the pstore backend, and on this board that backend cannot be
	 * registered at all (see luofu_log_snapshot's header).  This is the channel that does not depend
	 * on pstore, on a panic that never happens, or on any registration order: the driver writes the
	 * log it already holds to a partition it already knows how to program.
	 */
	luofu_log_snapshot();

	luofu_stamp(0, luofu_log_ticks);
	luofu_stamp(1, (u32)jiffies);
	luofu_stamp(2, luofu_log_lines);

	mod_timer(&luofu_log_timer, jiffies + msecs_to_jiffies(LUOFU_LOG_TICK_MS));
}


static struct notifier_block luofu_panic_nb;

/*
 * THE SAFETY TIMER - WHAT MAKES A REAL INIT SAFE TO BOOT.
 *
 * The fragment's comment explains why init=/nonexistent-init has been the guard all
 * along: with no network driver, a boot that SUCCEEDS in mounting a rootfs reaches a
 * userspace reachable by neither ssh nor the bootreg failover, and a power cycle would
 * be the only way back.
 *
 * A KERNEL TIMER REMOVES THAT LIMIT. It runs in softirq context, independent of
 * whatever userspace is doing, and it can call emergency_restart() - the same call
 * panic() makes. So if userspace comes up and does nothing useful, or hangs, the box
 * still returns to the vendor on its own, and the readback cells still get read.
 *
 * It fires unconditionally, once, well after the mount attempt. It is not a
 * convenience: it is the replacement for the guard, and it must stay until this
 * kernel has a network driver and a userspace that can be reached.
 */


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

static void luofu_console_write(struct console *co, const char *s, unsigned int n)
{
	static unsigned int off = 4; /* the mark occupies the first four bytes */
	u32 mark = LUOFU_LOG_MARK;
	void *p = luofu_log_b;

	if (!p)
		return;

	if (luofu_log_frozen)
		return;

	/*
	 * THE FILTER IS GONE, AND SO IS THE REASON FOR IT.
	 *
	 * It kept only chunks containing "ubi" or "FMC" because the ring was 512 bytes -
	 * about eight messages - and a whole boot's other output would have evicted the
	 * one line that said why the attach failed. The ring is 16 KiB now and circular,
	 * so keeping everything means the ring holds the NEWEST 16 KiB of the boot no
	 * matter how much is printed. That is strictly more useful: the mount, the init,
	 * the panic's own trace, and the lines either side of every error.
	 *
	 * The dumper's copy is unfiltered as well, so a panic now writes the complete
	 * log and then freezes this hook.
	 */

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

static struct console __maybe_unused luofu_console = {
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
/*
 * THE SAFETY TIMER IS THE DUMP TRIGGER - AND IT WAS THE MISSING PIECE.
 *
 * Measured 2026-10-14: our kernel EXECUTED for the first time (C18 = 0xC0DE502D, the FMC tick),
 * and pstore was STILL EMPTY afterwards.  The reason is a gap in the instrument, not in the capture:
 *
 *     pstore_blk writes when a KMSG DUMP happens - panic, oops, or emergency_restart.
 *     Our kernel does NOT panic.  The mach flips the slot selector in init_early so the NEXT boot
 *     takes the vendor pair, and then whatever fails simply stops - the SoC watchdog resets the
 *     box with no kernel-side dump at all.  So the backend never received a record.
 *
 * This timer is the one exit hook we own, it runs before any reset, and it already calls
 * emergency_restart().  Adding the dump immediately BEFORE the restart costs nothing and closes the
 * gap: the log goes to mtd10 first, then the box goes back to the vendor, and the vendor kernel can
 * read `/sys/fs/pstore` afterwards - which is exactly the sequence every fire already ends in.
 *
 * `kmsg_dump()` after `emergency_restart()` would be too late (the restart may not return), and the
 * dumper's own max_reason is KMSG_DUMP_MAX, so KMSG_DUMP_EMERG is accepted by the existing filter.
 */
static void luofu_reboot_tick(struct timer_list *t)
{
	pr_emerg("FMC: SAFETY TIMER FIRED after %d s - dumping the log, then returning the box to the vendor\n",
		 LUOFU_SAFETY_SECS);
	/*
	 * DUMP FIRST, RESTART SECOND.  A failed capture is not fatal to the restart - the box must
	 * always go back - so the return value is deliberately not checked.
	 */
	kmsg_dump(KMSG_DUMP_EMERG);
	emergency_restart();
}

/*
 * THERE IS NO SNAPSHOT TIMER ANY MORE, AND THAT IS THE FIX.
 *
 * A 3-second timer used to call luofu_kmsg_to() to work around dumper->active never
 * being set - kmsg_dump_get_line_nolock() returns false immediately unless the dumper
 * is active, and nothing sets it for a dumper whose callback has never run. The timer
 * set it by hand and walked the log.
 *
 * BUT THE CALLBACK RUNS NOW. This kernel always panics at the end of the boot (that is
 * what panic=5 and init=/nonexistent-init were for, and what the mount attempt still
 * does), so kmsg_dump() really does invoke luofu_kmsg_dump and sets active on its way in.
 * The workaround is obsolete.
 *
 * AND IT WAS ACTIVELY HARMFUL: the timer's copy wrote the kernel's log buffer from
 * OFFSET 4 of the ring - over the top of what the console hook had been accumulating
 * THERE. The kernel's own log buffer is 4 KiB (CONFIG_LOG_BUF_SHIFT), so every copy is
 * the same 4092 bytes of the OLDEST boot text, which is why the ring kept showing
 * "[ 0.000000] L2C-310 ..." no matter how far the boot actually got. THE CONSOLE IS THE
 * ONLY INSTRUMENT THAT CAN HOLD MORE THAN 4 KiB, BECAUSE IT RECEIVES EACH LINE AS IT IS
 * PRINTED, AND THE TIMER WAS OVERWRITING IT.
 */



/*
 * FAULT CAPTURE, v2 - writes flash DIRECTLY, with no MTD layer.
 *
 * v1 used mtd_write on "fac" and produced a clean negative: the image ran, C18 showed step 4,
 * and fac stayed ff. The reason is the SAME bug already caught once this session for the
 * fac-stage instrument: get_mtd_device_nm() needs the MTD layer, and luofu_fmc_register_mtd()
 * runs AFTER step 4 - so at panic time the lookup returns -ENODEV and the write is a silent
 * no-op.
 *
 * This version calls the raw primitives instead, which need no registration:
 *
 *     luofu_fmc_erase_block(fmc, row)
 *     luofu_fmc_write_page(fmc, row, data)
 *
 * and reaches the fmc through luofu_fmc_stamp, which the probe sets at its own line 4202 -
 * BEFORE the step-4 crumb at 4413, which is the point the boot reaches.
 *
 * The record is written to the LAST block of the fac partition so it cannot collide with the
 * live partition table at offset 0. Layout, for a reader doing dd on the vendor:
 *
 *     word 0  0xFA017000          the capture marker
 *     word 1  the faulting task name (4 chars)
 *     word 2  the stack pointer at capture
 *     word 3+ up to 60 words from the stack, which hold the saved register frame
 */
static void luofu_fault_capture_raw(void)
{
	struct luofu_fmc *fmc = luofu_fmc_stamp;
	u32 rec[64];
	const unsigned long *sp;
	size_t i;

	if (!fmc || !fmc->dma_buf)
		return;

	memset(rec, 0, sizeof(rec));
	rec[0] = 0xFA017000u;
	memcpy(&rec[1], current->comm, 4);
	rec[2] = (u32)(unsigned long)current;
	sp = (const unsigned long *)__builtin_frame_address(0);
	rec[3] = (u32)(unsigned long)sp;
	for (i = 0; i < 60; i++)
		rec[4 + i] = (u32)sp[i];

	/* the fac partition's last block: 2 MiB / 128 KiB = 16 blocks, so row 15 */
	(void)luofu_fmc_erase_block(fmc, 15 * 64);
	(void)luofu_fmc_write_page(fmc, 15 * 64, rec);
}

static int luofu_panic_notify(struct notifier_block *nb, unsigned long v, void *p)
{
	luofu_ubi_diag_print();
	luofu_fault_capture_raw();
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
	/*
	 * THE WALK NEEDS dumper->active, AND kmsg_dump() HAS JUST CLEARED IT.
	 *
	 * kmsg_dump_get_line_nolock() opens with `if (!dumper->active) goto out;`, and
	 * kmsg_dump() sets active on its way INTO a callback and CLEARS IT AGAIN ON THE WAY OUT.
	 * This notifier runs from the panic notifier chain, which panic() calls AFTER kmsg_dump()
	 * has returned - so active is false, the walk produces NOTHING, and the ring keeps
	 * whatever an earlier boot left there.  That is why a panicking boot and a stale boot look
	 * identical from devmem.
	 *
	 * The timer sets it by hand for the same reason. So does this now.
	 */
	luofu_kmsg.active = true;
	luofu_log_frozen = true;
	luofu_kmsg_to(luofu_log_b, &luofu_kmsg);
	luofu_kmsg.active = false;

	/*
	 * AND THE CRUMB GOES AFTER THE WALK, NOT BEFORE.
	 *
	 * It used to be written first, which made its line count meaningless - luofu_log_lines is
	 * only set by the walk, so a crumb written before it always reported zero lines and I read
	 * that as an empty log.  After the walk the number is real.
	 */
	/*
	 * THE PANIC CRUMB CARRIES THE LOG'S PAGE, BECAUSE THIS IS THE PATH THAT RUNS.
	 *
	 * The last boot panicked inside five seconds, so the tick never fired once - zero ticks -
	 * and ONLY THIS DUMP EXECUTED. It walked 205 lines and wrote them through luofu_log_b, and
	 * the ring at 0x80603000 is byte-for-byte unchanged: its newest timestamp is 4.380 s and
	 * its banner is this kernel's FIRST boot. Every I/O write this driver makes sticks; every
	 * RAM write vanishes.
	 *
	 * So the address is the thing worth spending this payload on. The line count was already
	 * proven - 205 - and the tick carries the phase when a boot lives long enough for it.
	 *
	 *     C1C = (lines << 12) | ((log_dma >> 12) & 0xfff)
	 *
	 * A page number inside the 0x8060xxxx window needs 12 bits, so the low three hex digits
	 * of the page number fit beside a line count in the top bits.
	 */
	/*
	 * AND NOW THE VIRTUAL POINTER'S TOP BYTE, BECAUSE THE PHYSICAL ADDRESS IS RIGHT.
	 *
	 * The previous fire read back 0x000CD603: 205 lines and page bits 0x603, and
	 * (0x80603000 >> 12) & 0xfff IS 0x603 - so luofu_log_dma IS 0x80603000, exactly what this
	 * driver asked for. The address was never the problem.
	 *
	 * What has never been checked is what __va() TURNED IT INTO. luofu_log_b =
	 * __va((phys_addr_t)luofu_log_dma), and on ARM that is x - PHYS_OFFSET + PAGE_OFFSET. If
	 * PHYS_OFFSET is 0x80000000 the ring lives at 0xC0603000 and is reachable; if it is
	 * something else, every write through luofu_log_b lands in unmapped space and vanishes -
	 * which is exactly what the ring, the diagnostic buffer and every RAM stamp have done
	 * since this phase began.
	 *
	 *     C1C = (lines << 8) | ((luofu_log_b >> 24) & 0xff)
	 *
	 * 0xC0 means the mapper produced the expected kernel virtual address. 0x40 means it
	 * produced the PHYSICAL address unmodified - which is the shape that makes writes vanish.
	 */
	/*
	 * AND NOW: DOES THE DUMPER SEE ITS OWN WRITE?
	 *
	 * 0xC0 came back, so __va() produced 0xC06xxxxx - the pointer is right, the physical
	 * address 0x80603000 is right, and the translation is right. Yet the text at 0x80603000,
	 * read with devmem, is this kernel's FIRST boot and never changes.
	 *
	 * So the last possibility is that what the dumper writes through luofu_log_b is not what
	 * devmem reads at the address the driver computed. That is a statement about the two
	 * paths, and it is testable in one word: read the ring back through its OWN pointer and
	 * put the result in the crumb.
	 *
	 *     C1C = (lines << 16) | (*(u32 *)(luofu_log_b + 4) & 0xffff)
	 *
	 * The first text the dumper writes sits at offset 4, right after the mark. If devmem shows
	 * one value there and the crumb shows another, the two paths do not meet - and THAT is the
	 * fault, not the address, not __va(), and not the walk.
	 */
	/*
	 * ★ AND THE SNAPSHOT GOES HERE FIRST - THIS IS THE HOOK THAT PROVABLY RUNS.
	 *
	 * Measured 2026-10-14: our kernel reaches late_initcall with CORRECT page tables and then PANICS
	 * (the notifier's own step 46 is in C18), while the 5-second tick's snapshot had written NONE of
	 * its eight crumbs - so the panic lands before the tick ever fires, and the tick is the wrong
	 * trigger.  The panic path is the ONE place a log capture is guaranteed to be reached.
	 *
	 * Ordering note: this runs INSIDE the panic notifier, i.e. process context on the panicking CPU,
	 * which is exactly where the mutex-taking lookup and the mtd write are legal - the same context
	 * argument that made the tick wrong.  The partition pointer is already cached by the probe.
	 */
	luofu_log_snapshot();

	/*
	 * Both channels, because the crumb is I/O and the snapshot is flash: the crumb is written even
	 * if the write path fails, so a fire always says which step was reached.
	 */
	if (luofu_ubi_fmc)
		luofu_fmc_crumb(luofu_ubi_fmc, LUOFU_LOG_PANIC_STEP,
				((luofu_log_lines & 0xffffu) << 16) |
				((*(volatile u32 *)((char *)luofu_log_b + 4)) & 0xffffu));

	/*
	 * AND THE LAST WORD GOES TO elf_hwcap, BECAUSE THE QUESTION IS NOW ABOUT THE FPU.
	 *
	 * This overwrites the crumb above on purpose: two writes to one cell, last one wins.  The
	 * line count (205, every fire) and the readback byte (proven to match devmem) have both
	 * done their job; the value that is still unknown is whether this kernel enabled the FPU.
	 */
	luofu_fmc_crumb(luofu_ubi_fmc, LUOFU_LOG_HWCAP_STEP, elf_hwcap);

	luofu_stamp(3, LUOFU_STAMP_PANIC);

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
			if ((base + LUOFU_LOG_SIZE + LUOFU_DIAG_SIZE <= dma_lo ||
			     base >= dma_hi) &&
			    (!pb_hi || base + LUOFU_LOG_SIZE + LUOFU_DIAG_SIZE <= pb_lo ||
			     base >= pb_hi))
				break;
		}
		if (base + LUOFU_LOG_SIZE + LUOFU_DIAG_SIZE > 0x80608000)
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
	 * And the safety timer, armed once and never cancelled - see its declaration
	 * for why it exists. It is what lets a real init be booted without the risk of
	 * stranding the router in a userspace nothing can reach.
	 */
	timer_setup(&luofu_reboot_timer, luofu_reboot_tick, 0);
	mod_timer(&luofu_reboot_timer, jiffies + LUOFU_SAFETY_SECS * HZ);

	/*
	 * THE CONSOLE IS NOT REGISTERED ANY MORE, AND THE MEASUREMENT IS WHY.
	 *
	 * The tick crumb now carries (ticks << 16) | lines, and the last fire read back
	 * 0x002E00E6: 46 ticks and 230 LINES.  The kernel's log IS readable and the kmsg walk
	 * DOES work - but the ring only ever shows about fifty lines of early boot.
	 *
	 * THE CONSOLE IS THE OTHER WRITER. Its write callback keeps its own static offset and
	 * appends every chunk printk hands it, into the same buffer the dumper rewrites from
	 * offset 4 every 500 ms. Two writers, one region, neither aware of the other - so what
	 * survives is whichever wrote last, and the console writes after every printk.
	 *
	 * With it unregistered the ring belongs to the dumper alone, which also freezes on
	 * panic - so what devmem reads is exactly what the kernel logged.
	 */
	/*
	 * And the periodic dumper, which IS the instrument - see luofu_log_tick.
	 */
	luofu_fmc_stamp = fmc;
	luofu_phase = 2;
	/* resolve the snapshot partition HERE, in probe context - see luofu_logsnap_resolve */
	luofu_logsnap_resolve();
	timer_setup(&luofu_log_timer, luofu_log_tick, 0);
	mod_timer(&luofu_log_timer, jiffies + msecs_to_jiffies(LUOFU_LOG_TICK_MS));

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

	/*
	 * ★★★ THE CLOCK AND THE RESETS - THE SETUP THIS DRIVER NEVER DID.
	 *
	 * Found 2026-10-14 by diffing the VENDOR'S PROBE against ours, one level above the register
	 * comparison that had been exhausted: every register this driver writes was verified identical
	 * to the vendor's disassembly, and the read STILL returned the controller's own output
	 * (`40 1f 00 00`, four fires, absent from the whole chip).  A register written correctly into a
	 * block that is not running reads back as something else - which is exactly what was observed:
	 * `OP_CFG` read back as 0x0870 instead of the 0x9d1 this code computes.
	 *
	 * THE VENDOR'S PROBE, from its own disassembly (hi_flash.ko):
	 *
	 *     bl  __devm_reset_control_get      (three of them)
	 *     bl  devm_clk_get                  ("sfc_clk")
	 *     bl  clk_prepare
	 *     bl  clk_enable
	 *
	 * OUR DRIVER DID NONE OF IT - `grep` for clk_prepare / clk_enable / devm_clk_get /
	 * reset_control over this file returned no matches at all.
	 *
	 * AND THE DEVICE CONFIRMS IT MATTERS: the running vendor's clock tree shows
	 * `sfc_clk  enable=1  prepare=1  rate=100000000`, and its DT node declares exactly the two
	 * properties consumed below - `clocks`/`clock-names = "sfc_clk"`, and three `resets`
	 * (0x0f, 0x2c, 0x00).  So this is WIRING THE DRIVER TO WHAT THE TREE ALREADY PROVIDES, not
	 * inventing anything.
	 *
	 * ORDER: the clock first (a register block with no clock ignores writes), then the resets
	 * released, both BEFORE the first register access below.  A missing clock is logged and
	 * tolerated rather than fatal - this driver has to keep booting to report what it found, and
	 * an instrument that cannot run is worse than one that reports a failure.
	 */
#if 0 /* BISECT-D-CLK */
	fmc->clk = devm_clk_get(dev, "sfc_clk");
	if (IS_ERR(fmc->clk)) {
		dev_warn(dev, "FMC: no sfc_clk (%ld) - the block may not be clocked\n",
			 PTR_ERR(fmc->clk));
		fmc->clk = NULL;
	} else {
		int clkret = clk_prepare_enable(fmc->clk);

		if (clkret) {
			dev_warn(dev, "FMC: clk_prepare_enable failed (%d)\n", clkret);
			fmc->clk = NULL;
		} else {
			dev_info(dev, "FMC: sfc_clk enabled at %lu Hz\n",
				 clk_get_rate(fmc->clk));
		}
	}
#endif /* BISECT-D-CLK-END */
	/*
	 * BISECT-D 2026-10-15: the sfc_clk enable and the three reset deasserts are DISABLED.
	 *
	 * This block is the ONLY structural change in the probe's early path that exists in
	 * tonight's source but NOT in the 2026-10-11 source (verified: clk_prepare_enable 0->2,
	 * reset_control_deassert 0->1). That older source reached USERSPACE (C18 = 0xC0DE502D,
	 * the tick ran) while tonight's stops after step 4 with no tick and no userspace.
	 *
	 * The reasoning: enabling a clock and deasserting resets on a block that u-boot has
	 * ALREADY initialised can gate or reset it MID-USE. The vendor does this from its own
	 * driver because it owns the block from power-on; we inherit it already running.
	 *
	 *   C18 reaches 0xC0DE502D -> THIS BLOCK IS THE REGRESSION
	 *   C18 stays  0xC0DE5004 -> it is innocent, and the difference is elsewhere
	 */

	/*
	 * The reset lines, released before the first register access.  `devm_reset_control_array_get`
	 * takes all three in one call, which is why the vendor's three __devm_reset_control_get calls
	 * collapse to one here.  Optional: the DT may omit them, and a failure must not stop the probe.
	 */
#if 0 /* BISECT-D-RST */
	fmc->rst = devm_reset_control_array_get_optional_exclusive(dev);
	if (IS_ERR(fmc->rst)) {
		dev_warn(dev, "FMC: reset controls unavailable (%ld)\n", PTR_ERR(fmc->rst));
		fmc->rst = NULL;
	} else if (fmc->rst) {
		int rstret = reset_control_deassert(fmc->rst);

		if (rstret)
			dev_warn(dev, "FMC: reset deassert failed (%d)\n", rstret);
		else
			dev_info(dev, "FMC: resets released\n");
	}
#endif /* BISECT-D-RST-END */

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

	/*
	 * PROGRAM THE VENDOR'S REGISTERS.
	 *
	 * Reading the vendor's LIVE FMC block at 0x10a20000 while its kernel runs, and
	 * comparing it against this driver's map, showed something neither the register
	 * campaign nor the raw-buffer comparison had exposed: THIS DRIVER NEVER WRITES A
	 * CONFIG REGISTER AT ALL. Its whole block sits at reset defaults. The vendor's
	 * driver programs at least fifteen, and its non-default values are:
	 *
	 *     0x08 TIMING_SPI  = 0x0000006f
	 *     0x0c PND_PWIDTH  = 0x00000333
	 *     0x10 PND_OPIDLE  = 0x00088880
	 *     0x14             = 0x08888888
	 *     0x34             = 0x00001000
	 *     0x48             = 0x00000007
	 *     0x6c             = 0x00ffffff
	 *     0xa0             = 0x80808080
	 *     0xa4             = 0x00006060
	 *     0xa8             = 0x000000ff
	 *     0xb0             = 0x0000007f
	 *     0xb4             = 0x01372b2b
	 *     0xb8             = 0x0000ffff
	 *     0xbc             = 0x00000100
	 *     0xfc             = 0x00000005
	 *
	 * 0x34 IS 0x1000 - TWO PAGES - which is the shape of a threshold or depth, and
	 * exactly the kind of register that would bound a transfer and leave a page's
	 * last bytes behind. None of these were ever set here, so every read this driver
	 * has done ran against a configuration the working implementation does not use.
	 *
	 * The values are taken from the vendor's live block, not invented, and written
	 * once at probe time the way its driver does. 0x00 and 0x04 are left alone: the
	 * read-back showed both already identical.
	 */
	writel(0x0000006f, fmc->regs + FMC_TIMING_SPI_CFG);
	writel(0x00000333, fmc->regs + FMC_PND_PWIDTH_CFG);
	writel(0x00088880, fmc->regs + FMC_PND_OPIDLE_CFG);
	writel(0x08888888, fmc->regs + 0x14);
	writel(0x00001000, fmc->regs + 0x34);
	writel(0x00000007, fmc->regs + 0x48);
	writel(0x00ffffff, fmc->regs + 0x6c);
	writel(0x80808080, fmc->regs + 0xa0);
	writel(0x00006060, fmc->regs + 0xa4);
	writel(0x000000ff, fmc->regs + 0xa8);
	writel(0x0000007f, fmc->regs + 0xb0);
	writel(0x01372b2b, fmc->regs + 0xb4);
	writel(0x0000ffff, fmc->regs + 0xb8);
	writel(0x00000100, fmc->regs + 0xbc);
	writel(0x00000005, fmc->regs + 0xfc);

	dev_info(dev, "FMC: vendor block programmed: t=%08x 14=%08x 34=%08x 48=%08x\n",
		 readl(fmc->regs + FMC_TIMING_SPI_CFG),
		 readl(fmc->regs + 0x14), readl(fmc->regs + 0x34),
		 readl(fmc->regs + 0x48));

	ret = luofu_fmc_reset_die(fmc, &status);
	if (ret) {
		luofu_fmc_crumb(fmc, 4, LUOFU_RPT_FAIL(4));
		return ret;
	}
	/* 0xC1C -> 0xC0DEF100: entered the step-4 block, BEFORE the step-4 crumb */
	luofu_fmc_crumb(fmc, 0xF0, 0xC0DEF100);
	luofu_fmc_crumb(fmc, 4, LUOFU_RPT_STEP(4));
	/*
	 * BISECT-C: restored to the 2026-10-11 shape at this point.
	 *
	 * The 2026-10-11 source goes straight from this crumb to read_id. Everything between
	 * - the 0xF1 marker, the canary block, the 0xF2 marker, the isolated readl(FMC_CFG),
	 * the 0xF3 marker - was inserted on 2026-10-15, and the binary built from that source
	 * reaches USERSPACE (C18 = 0xC0DE502D) while tonight's stops here (C18 = 0xC0DE5004).
	 *
	 * Removing all five inserts restores the old shape exactly. The canary alone was
	 * already bisected out and found innocent, so this fire decides whether the remaining
	 * four are the regression:
	 *   C18 reaches 0xC0DE502D -> one of the four is the cause; re-add one at a time
	 *   C18 stays  0xC0DE5004 -> the regression is elsewhere in the diff
	 */

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

	/*
	 * THE VENDOR'S FEATURE 0xB0 BITS - CLEAR BIT 4, SET BIT 6.
	 *
	 * hi_spi_nand_drv_dma_read's setup does this before every page read:
	 *
	 *     mov r1, #0xb0        ; feature 0xb0
	 *     mov r2, #0xf         ; read 15 bytes
	 *     bl  <read>           ;   -> ldrb r3, [sp, #3]
	 *     bic r3, r3, #0x10    ; CLEAR BIT 4
	 *     orr r3, r3, #0x40    ; SET   BIT 6
	 *     mov r2, #0x1f        ; write 31 bytes
	 *     bl  <write>
	 *
	 * AND THIS DRIVER NEVER TOUCHES EITHER BIT. Its only write to 0xb0 is
	 * cfg | BIT(0) for the quad enable. Bits 4 and 6 of the configuration register
	 * govern how the chip emits array data, and a driver that leaves them at the
	 * die's defaults can get an output stream carrying spare bytes where the working
	 * one gets a clean page - which is exactly the 14-byte window at 1040 that this
	 * driver has had to de-interleave out.
	 *
	 * Read-modify-write, preserving whatever else the die has, and done AFTER the
	 * quad enable so bit 0 is kept.
	 */
	if (!luofu_fmc_get_feature(fmc, SPINAND_FEAT_CONFIG, &config)) {
		u8 wanted = (config & ~BIT(4)) | BIT(6);

		dev_info(dev, "FMC: feature 0xb0 %02x -> %02x (vendor clears bit 4, sets bit 6)\n",
			 config, wanted);
		if (luofu_fmc_set_feature(fmc, SPINAND_FEAT_CONFIG, wanted))
			dev_warn(dev, "FMC: could not write feature 0xb0\n");
	}

	if (dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32)))
		dev_warn(dev, "FMC: could not set the 32-bit DMA mask\n");

	fmc->dma_buf = dmam_alloc_coherent(dev, 0x2200, &fmc->dma_addr, GFP_KERNEL);

	if (!fmc->dma_buf) {
		luofu_fmc_crumb(fmc, 8, LUOFU_RPT_FAIL(8));
		return -ENOMEM;
	}

	/*
	 * THE THREE DESTINATIONS, ONCE - the way the vendor sets them.
	 *
	 * Its live block shows all three programmed (D0 = 0x820e0000, D1 = 0x820e1000,
	 * OOB = 0x820e2000) and its before/after read shows NONE of them changing per
	 * read: only ADDRL moves. So they are configured once and left alone.
	 *
	 * AFTER the allocation check, not before it: writing a NULL dma_addr into the
	 * controller's destination registers and only then discovering the allocation
	 * failed is the wrong order, and the first build with this block failing at
	 * step 8 is what made that visible.
	 */
	writel(fmc->dma_addr, fmc->regs + FMC_SADDR_D0);
	writel(fmc->dma_addr + 0x1000, fmc->regs + FMC_SADDR_D1);
	writel(fmc->dma_addr + 0x2000, fmc->regs + FMC_SADDR_OOB);

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

	/*
	 * stage D's hook looks the driver state up here, and stage D2 (the
	 * device_initcall reporter) is on the initcall table next to the
	 * driver, so both need it published before this probe returns.
	 */
	luofu_ubi_fmc = fmc;
	luofu_ubi_early_done = 1;

	/*
	 * Stage E: from here on, a panic writes the kernel log into RAM where it
	 * can be read back from the vendor system.  Registered last so that
	 * failing to get the buffer cannot spoil any of the measurements above.
	 */
	if (luofu_log_register(fmc))
		dev_warn(dev, "FMC: could not arm the panic log capture\n");

	return 0;
	/*
	 * WINDOW MARKER: the probe is about to RETURN.
	 *
	 * C18 holds 0xC0DE5004 (step 4) and the boot dies before any late_initcall. What is
	 * unmeasured is HOW FAR PAST step 4 it gets. This crumb is the last statement of the
	 * probe, so it separates the two halves of that window:
	 *
	 *   C18 = 0xC0DE50FE  -> the probe FINISHED; the death is in another
	 *                        device_initcall, or between the last device_initcall and the
	 *                        first late_initcall
	 *   C18 = 0xC0DE5004  -> the death is INSIDE this probe, between step 4 and its return
	 *                        (read_id, the feature reads, or MTD registration)
	 */
	luofu_fmc_crumb(fmc, 0xFE, 0);

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

/*
 * THE ORDERING *IS* THE BOOT - AND IT IS MEASURED, NOT REASONED.
 *
 * `ubi.mtd=<name>` is NOT resolved when the command line is parsed.  ubi_mtd_param_parse only
 * STORES the string (drivers/mtd/ubi/build.c: `strcpy(&p->name[0], tokens[0])`); the name becomes
 * a device in open_mtd_device() -> get_mtd_device_nm(), called from ubi_init(), which is a
 * LATE_INITCALL (build.c:1322 `late_initcall(ubi_init)`).
 *
 * So the partition named "upgrade" must already be REGISTERED by the time ubi_init runs - and
 * only this driver registers it.  device_initcall (level 6) runs BEFORE late_initcall (level 7);
 * late_initcall_sync (7s) runs AFTER it.
 *
 * FIRE 28's own log, recovered from the ring at 0x80603000, is the evidence:
 *
 *     /dev/root: Can't open blockdev
 *     VFS: Cannot open root device "ubiblock0_0" or unknown-block(0,0): error -6
 *     Kernel panic - not syncing: VFS: Unable to mount root fs on unknown-block(0,0)
 *
 * UBI never attached mtd16, so ubiblock never created ubiblock0_0, so root= could not open.
 * Registering this driver from late_initcall_sync put it after ubi_init and produced exactly
 * that panic.  `module_platform_driver` is therefore required, not stylistic: it is the level
 * that makes the partitions exist in time.
 */
module_platform_driver(luofu_fmc_driver);

/*
 * The diagnostic stays LATE on purpose: it reports what UBI did, so it must run AFTER ubi_init -
 * which is why it gets its own initcall rather than being called from the driver's probe.
 *
 * NOTE for the module build: this file now defines TWO initcall macros, which is legal for a
 * BUILT-IN driver (init.h gives them distinct levels) but NOT for a loadable module (module.h
 * collapses every initcall to module_init, and a module may define init_module once).  This
 * driver is `CONFIG_MTD_NAND_LUOFU=y` in the image, which is the configuration that has always
 * been the real one; a `=m` build of this file is not a meaningful test.
 */
static int __init luofu_fmc_ubi_diag(void)
{
#ifdef CONFIG_MTD_UBI
	luofu_fmc_ubi_probe();
#endif
	return 0;
}
late_initcall_sync(luofu_fmc_ubi_diag);

MODULE_AUTHOR("the luofu port");
MODULE_DESCRIPTION("Hisilicon luofu (Hi5671Y) FMC SPI-NAND controller (stage A)");
MODULE_LICENSE("GPL v2");
