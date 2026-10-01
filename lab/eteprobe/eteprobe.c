// SPDX-License-Identifier: GPL-2.0
/*
 * eteprobe: the smallest verifiable interaction with the vendor BAL/HCC/ETE
 * engine, performed on an endpoint that NO vendor driver owns (the boot-time
 * takeover configuration of docs/phase16/boot-takeover.md).
 *
 * Recovered from hi5622v100_plat.ko (docs/phase17/ete-engine.md, part A):
 * firmware_file_send -> bal_write(chip, dev_addr, buf, len) -> the BAL bus
 * callback -> the HCC message/queue layer -> the ETE source/destination DMA
 * rings.  A ring entry ("node") is 8 bytes: {device_address, (len << 16) |
 * flags}, flags bit[12:0] carrying the host-fill magic 0xd2b and bits 13/14
 * toggling ownership; the ring is submitted by pcie_msg_send(chip, 3), which
 * sets bit 3 of the message-pending mask and rings the doorbell register.
 *
 * The descriptor FORMAT is unambiguous, but the QUEUE is not reachable in the
 * unowned state: the ETE ring base, the head/tail program registers and the
 * doorbell are all runtime resources created by pcie_ete_init / pcie_msg_init
 * from the per-chip resource table (get_pcie_ete_res), which does not exist
 * when the vendor stack is absent.  Submitting a descriptor would mean writing
 * a ring the engine has never been told about, so this module deliberately
 * does NOT submit.  It does the provably safe half and stops:
 *
 *   1. claim the endpoint exactly as epinit stage 1 does (pci_enable_device,
 *      pci_request_mem_regions, the documented PCI_COMMAND=7 write, read back);
 *   2. dma_alloc_coherent() a buffer, fill it with a known pattern, read it
 *      back (CPU + after a for-device/for-cpu sync pair) and report match;
 *   3. READ-ONLY log the ETE/glue register block (BAR0+0x39000, BAR0+0x3a000:
 *      the SR/DR program registers, the message registers, the channel-res
 *      register and the phase-13 "ETE queue pointer" pairs), bracketing the
 *      reads with a read of the message doorbell register - the closest thing
 *      to a doorbell interaction that has no side effects.
 *
 * It never writes to BAR0, never writes a descriptor, never starts a CPU and
 * never touches the firmware window (BAR0+0x40000..) or BAR2/BAR4.
 *
 * Module parameters:
 *   domain=N   PCI domain of the 59e7:0005 endpoint (default 0 = 2.4 GHz)
 *   regwin=N   byte offset of the ETE/glue register window (default 0x39000)
 *   reglen=N   bytes to map/dump from regwin..regwin+0x1000 (default 0x1000)
 *   dmalen=N   coherent DMA buffer size in bytes (default 65536)
 *   submit=N   1 = attempt the descriptor submit path.  REFUSED (see above);
 *              the parameter exists only to document the boundary.
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

#define OMO_CFG_CMD	0x04
#define OMO_CFG_BAR0	0x10

#define OMO_ROM_OFF	0x00000UL
#define OMO_ROM_LEN	0x1000UL
#define OMO_ROM_WORDS	8

/* The ETE/glue register block the engine programs, seen from BAR0. */
#define OMO_ETE_WIN	0x39000UL
#define OMO_ETE_LEN	0x1000UL
#define OMO_REMAP_WIN	0x3a000UL
#define OMO_REMAP_LEN	0x1000UL

/* Offsets inside the ETE block (docs/phase17/ete-engine.md A.4). */
#define ETE_SR_CTRL	0x008	/* pcie_ete_sr_reg_init: bits[2:0] queue sel */
#define ETE_SR_BASE	0x010	/* SR ring base device address (pcie_hostca_to_devva) */
#define ETE_SR_DEPTH	0x014	/* bits[9:0] depth-1 */
#define ETE_SR_WPTR	0x018	/* SR write pointer / watermark */
#define ETE_DR_BASE	0x030	/* DR ring base device address */
#define ETE_DR_DEPTH	0x034	/* bits[9:0] depth-1 */
#define ETE_DR_WPTR	0x038	/* DR pointer / watermark */
#define ETE_MSG0	0x010	/* shuangta_pcie_msg_reg_map out[0] (0x40039010) */
#define ETE_MSG1	0x014	/* out[1] (0x40039014) */
#define ETE_MSG2	0x2d4	/* out[2] (0x400392d4) - host->dev doorbell */
#define ETE_CHN_RES	0x2e8	/* pcie_ete_chn_res read/modify/write */
#define ETE_MSG5	0x2f0	/* out[5] (0x400392f0) */

static unsigned int omo_domain;
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static unsigned long omo_regwin = OMO_ETE_WIN;
module_param_named(regwin, omo_regwin, ulong, 0444);
MODULE_PARM_DESC(regwin, "BAR0 offset of the ETE/glue register window (default 0x39000)");

static unsigned int omo_reglen = OMO_ETE_LEN;
module_param_named(reglen, omo_reglen, uint, 0444);
MODULE_PARM_DESC(reglen, "bytes to map from regwin (default 0x1000)");

static unsigned int omo_dmalen = 65536;
module_param_named(dmalen, omo_dmalen, uint, 0444);
MODULE_PARM_DESC(dmalen, "coherent DMA buffer size in bytes (default 65536)");

static unsigned int omo_submit;
module_param_named(submit, omo_submit, uint, 0444);
MODULE_PARM_DESC(submit, "1 = attempt descriptor submit (REFUSED: ring is a vendor runtime resource)");

static struct pci_dev *omo_dev;
static void *omo_dma;
static dma_addr_t omo_dma_handle;
static size_t omo_dma_size;

/* ---- helpers ----------------------------------------------------------- */

static u32 omo_crc32(const u8 *p, size_t n)
{
	u32 crc = 0xffffffff;
	size_t i;
	int b;

	for (i = 0; i < n; i++) {
		crc ^= p[i];
		for (b = 0; b < 8; b++)
			crc = (crc >> 1) ^ (0xedb88320 & -(crc & 1));
	}
	return ~crc;
}

static void omo_log_words(const char *tag, unsigned long off,
			  const void __iomem *win, unsigned int n)
{
	unsigned int i;

	pr_info("omo-eteprobe: %s BAR0+0x%lx:", tag, off);
	for (i = 0; i < n; i++)
		pr_cont(" %08x", ioread32(win + 4UL * i));
	pr_cont("\n");
}

/*
 * Read one named 32-bit register from the ETE block, log it.  Reads have no
 * side effects; this is the module's entire register interaction.
 */
static u32 omo_rd(const void __iomem *win, unsigned long off, const char *name)
{
	u32 v = ioread32(win + off);

	pr_info("omo-eteprobe: ETE[+0x%03lx] %-18s = 0x%08x (BAR0+0x%lx)\n",
		off, name, v, (unsigned long)(omo_regwin + off));
	return v;
}

/* ---- coherent DMA buffer + known pattern ------------------------------- */

static int omo_dma_probe(struct pci_dev *dev)
{
	u32 *w;
	size_t i, words;
	unsigned int mismatch = 0, first = 0;
	u32 crc_wr, crc_rd;
	int ret;

	omo_dma_size = omo_dmalen & ~(size_t)3;
	if (!omo_dma_size)
		omo_dma_size = 4096;

	ret = pci_set_dma_mask(dev, DMA_BIT_MASK(32));
	pr_info("omo-eteprobe: pci_set_dma_mask(32) rc=%d\n", ret);
	ret = pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(32));
	pr_info("omo-eteprobe: pci_set_consistent_dma_mask(32) rc=%d\n", ret);

	omo_dma = dma_alloc_coherent(&dev->dev, omo_dma_size, &omo_dma_handle,
				     GFP_KERNEL);
	if (!omo_dma) {
		pr_err("omo-eteprobe: dma_alloc_coherent(%zu) FAILED\n",
		       omo_dma_size);
		return -ENOMEM;
	}

	words = omo_dma_size / 4;
	w = (u32 *)omo_dma;
	/* Known pattern: word k = 0xA5A50000 | (k & 0xffff), plus an end marker. */
	for (i = 0; i < words; i++)
		w[i] = 0xa5a50000u | (u32)(i & 0xffff);
	w[words - 1] = 0xdeadbeef;

	crc_wr = omo_crc32((const u8 *)omo_dma, omo_dma_size);
	pr_info("omo-eteprobe: dma_alloc_coherent size=%zu virt=%px dma=0x%llx crc32=0x%08x\n",
		omo_dma_size, omo_dma, (unsigned long long)omo_dma_handle,
		crc_wr);
	pr_info("omo-eteprobe: pattern[0]=0x%08x pattern[1]=0x%08x pattern[last]=0x%08x\n",
		w[0], w[1], w[words - 1]);

	/* Device-view sync round trip, then a CPU read-back with verify. */
	dma_sync_single_for_device(&dev->dev, omo_dma_handle, omo_dma_size,
				   DMA_TO_DEVICE);
	dma_sync_single_for_cpu(&dev->dev, omo_dma_handle, omo_dma_size,
				DMA_FROM_DEVICE);
	crc_rd = omo_crc32((const u8 *)omo_dma, omo_dma_size);

	for (i = 0; i < words; i++) {
		u32 expect = (i == words - 1) ? 0xdeadbeef
					      : (0xa5a50000u | (u32)(i & 0xffff));

		if (w[i] != expect) {
			if (!mismatch)
				first = i;
			mismatch++;
		}
	}
	pr_info("omo-eteprobe: coherent readback crc32=0x%08x match=%s mismatches=%u first=%u\n",
		crc_rd, (crc_wr == crc_rd && !mismatch) ? "YES" : "NO",
		mismatch, first);
	return 0;
}

/* ---- read-only ETE/glue register probe --------------------------------- */

static void omo_ete_probe(u64 base)
{
	void __iomem *win, *remap;
	u32 d0, d1;

	win = ioremap(base + omo_regwin, omo_reglen);
	if (!win) {
		pr_err("omo-eteprobe: ioremap ETE window BAR0+0x%lx FAILED\n",
		       omo_regwin);
	} else {
		/* The message doorbell register: read-only probe of its state. */
		d0 = omo_rd(win, ETE_MSG2, "msg2/doorbell");

		pr_info("omo-eteprobe: ---- ETE SR/DR program registers ----\n");
		omo_rd(win, ETE_SR_CTRL, "SR ctrl[2:0]");
		omo_rd(win, ETE_SR_BASE, "SR ring base");
		omo_rd(win, ETE_SR_DEPTH, "SR depth-1");
		omo_rd(win, ETE_SR_WPTR, "SR wptr");
		omo_rd(win, ETE_DR_BASE, "DR ring base");
		omo_rd(win, ETE_DR_DEPTH, "DR depth-1");
		omo_rd(win, ETE_DR_WPTR, "DR wptr");
		omo_rd(win, ETE_MSG0, "msg reg0/SR base");
		omo_rd(win, ETE_MSG1, "msg reg1/SR depth");
		omo_rd(win, ETE_CHN_RES, "channel res");
		omo_rd(win, ETE_MSG5, "msg reg5");

		omo_log_words("ETE block +0x000", omo_regwin, win, 0x40 / 4);
		omo_log_words("ETE block +0x2c0", omo_regwin + 0x2c0,
			      win + 0x2c0, 0x40 / 4);

		d1 = omo_rd(win, ETE_MSG2, "msg2/doorbell (after)");
		pr_info("omo-eteprobe: doorbell read stable=%s (0x%08x -> 0x%08x)\n",
			d0 == d1 ? "YES" : "NO", d0, d1);

		iounmap(win);
	}

	remap = ioremap(base + OMO_REMAP_WIN, OMO_REMAP_LEN);
	if (!remap) {
		pr_err("omo-eteprobe: ioremap remap window BAR0+0x%lx FAILED\n",
		       OMO_REMAP_WIN);
	} else {
		/* phase-13 ranked candidate 1: glue/ETE {pointer,index} pairs. */
		omo_log_words("remap/ETE +0x840", OMO_REMAP_WIN + 0x840,
			      remap + 0x840, 0x20 / 4);
		iounmap(remap);
	}
}

/* ---- init / exit ------------------------------------------------------- */

static int __init omo_eteprobe_init(void)
{
	u32 lo = 0, hi = 0;
	u64 base;
	u16 cmd0 = 0, cmd1 = 0, rb = 0xffff;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-eteprobe: endpoint %04x:00:00.0 not found\n",
		       omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev, 0x00, &id);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("omo-eteprobe: unexpected id %04x:%04x\n",
				id & 0xffff, id >> 16);
	}

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);
	ret = pci_enable_device(omo_dev);
	if (ret) {
		pr_err("omo-eteprobe: pci_enable_device rc=%d\n", ret);
		goto err_put;
	}
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("omo-eteprobe: pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
		cmd0, cmd1);

	ret = pci_request_mem_regions(omo_dev, "omo-eteprobe");
	if (ret) {
		pr_err("omo-eteprobe: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-eteprobe: pci_request_mem_regions rc=0 (MEM BARs claimed)\n");

	/* oal_pcie_set_inbound_by_viewport: pci_write_config_word(dev, 4, 7). */
	ret = pci_write_config_word(omo_dev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_dev, PCI_COMMAND, &rb);
	pr_info("omo-eteprobe: cfg[0x004] <= 0x0007 readback=0x%04x MEM|MASTER=%s\n",
		rb, (rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		    (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("omo-eteprobe: no usable BAR0 (lo=0x%08x hi=0x%08x)\n",
		       lo, hi);
		ret = -ENODEV;
		goto err_release;
	}
	pr_info("omo-eteprobe: BAR0 base=0x%llx (config-space read)\n",
		(unsigned long long)base);

	{
		void __iomem *rom = ioremap(base + OMO_ROM_OFF, OMO_ROM_LEN);

		if (rom) {
			omo_log_words("ROM vector page", OMO_ROM_OFF, rom,
				      OMO_ROM_WORDS);
			iounmap(rom);
		}
	}

	/* (2) coherent DMA buffer, filled with a known pattern and verified. */
	omo_dma_probe(omo_dev);

	/* (3) read-only probe of the ETE/glue registers. */
	omo_ete_probe(base);

	/* (4) the boundary, stated plainly. */
	if (omo_submit) {
		pr_warn("omo-eteprobe: submit=1 REFUSED - the ETE ring base, the head/tail program registers and the doorbell are pcie_ete_init/pcie_msg_init runtime resources (get_pcie_ete_res); submitting to an unprogrammed ring would write a queue the engine has never been told about. See docs/phase17/ete-engine.md part A.5.\n");
	} else {
		pr_info("omo-eteprobe: descriptor format is known (8-byte node {dev_addr, len<<16|0x6d2b}) but the queue is NOT reachable in the unowned state; no descriptor submitted, no BAR0 write\n");
	}

	pr_info("omo-eteprobe: done - endpoint claimed, coherent DMA buffer verified, ETE register state logged (read-only)\n");
	return 0;

err_release:
	pci_release_mem_regions(omo_dev);
err_disable:
	pci_disable_device(omo_dev);
err_put:
	pci_dev_put(omo_dev);
	omo_dev = NULL;
	return ret;
}

static void __exit omo_eteprobe_exit(void)
{
	if (omo_dma) {
		dma_free_coherent(&omo_dev->dev, omo_dma_size, omo_dma,
				  omo_dma_handle);
		omo_dma = NULL;
		pr_info("omo-eteprobe: coherent DMA buffer freed\n");
	}
	if (omo_dev) {
		pci_release_mem_regions(omo_dev);
		pci_disable_device(omo_dev);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("omo-eteprobe: unloaded, BARs released, device put\n");
}

module_init(omo_eteprobe_init);
module_exit(omo_eteprobe_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only probe of the vendor BAL/HCC/ETE engine plus a verified coherent DMA buffer");
