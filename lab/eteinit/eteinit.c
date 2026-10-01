// SPDX-License-Identifier: GPL-2.0
/*
 * eteinit: reproduce the vendor's ETE transport-engine initialisation on an
 * endpoint that NO vendor driver owns (the boot-time takeover configuration of
 * docs/phase16/boot-takeover.md), and stop at the first genuinely ambiguous
 * step.
 *
 * Recovered from hi5622v100_plat.ko (docs/phase17/ete-init.md part A).  The
 * resource chain is STATIC, contrary to docs/phase17/ete-engine.md A.9:
 *
 *   plat_res_init -> shuangta_get_plat_res -> set_plat_res(0, res)
 *   shuangta_get_ete_priv_res == .data+0x2944 (a static object; .rel.data
 *   fills its function-pointer slots), whose word 0 is the ETE register-block
 *   device CA  0x4003a000  -> BAR0 + 0x3a000.
 *
 * Per-channel program registers, from .rodata+0x101c (7 entries x 12 B,
 * pcie_ete_get_chn_cfg @0x15f00 = base + 0xc*index + .rodata+0x101c):
 *
 *   chan 0..2 (SR): block 0x400/0x450/0x4a0   depth byte = 0x20 (32)
 *   chan 3..6 (DR): block 0x590/0x5e0/0x630/0x680
 *   SR regs +0x08 ctrl(+queue<<0..2) +0x10 base +0x14 depth-1 +0x18 wptr
 *   DR regs +0x30 base +0x34 depth-1 +0x38 wptr
 *
 * What this module performs (every write logged with its read-back):
 *   1. claim + config-space init exactly as epinit stage 1;
 *   2. dma_alloc_coherent() a node array per channel, with the vendor's exact
 *      sizes: SR (depth+2)*8 = 272 B, DR depth*8 = 256 B
 *      (shuangta_ete_{sr,dr}_node_init_handle @0x17728/@0x17970);
 *   3. read-only pre-log of all seven channel register sets at the proven
 *      offsets (this is the state the "uninitialised" claim of phase 17 was
 *      never actually measured against - eteprobe read BAR0+0x39010, which is
 *      the MESSAGE register, not SR ch0's +0x410);
 *   4. the proven register writes in the vendor's order (sr_reg_init
 *      @0x14a48 / dr_reg_init @0x1483c): base, depth-1=31, wptr=0, ctrl, plus
 *      the pcie_ete_chn_res @0x7490 read-modify-write of +0x2e8 with mask
 *      0xfffffc20.
 *
 * What this module deliberately does NOT do, and why (the documented stop):
 *   It does NOT fill a descriptor or ring the doorbell.  The node's address
 *   field is the DEVICE VA of a HOST buffer: the only producer,
 *   pcie_ete_sending_trigger @0x13f90, builds a full HCC/BAL skb and converts
 *   the buffer with pcie_get_ete_addr -> oal_dma_map_single ->
 *   pcie_hostca_to_devva (host CA -> device VA through the runtime inbound
 *   window at chip->[4]->[0xc4]).  There is no chip-scratch target in the node
 *   at all, so the brief's "write a known pattern to a chosen chip location"
 *   cannot be expressed with a bare SR/DR node; and the completion path needs
 *   pcie_msg_init's handler table + IRQ dispatch, which are also absent.
 *
 * The ring-base register value is written from the coherent DMA handle
 * (dma_alloc_coherent's bus address).  That single value is the one INFERRED
 * field: the vendor further converts it with pcie_hostca_to_devva through the
 * runtime inbound window, which is not a file constant.  Every descriptor node
 * is left zeroed (not owned), so even if the engine prefetches it sees
 * word1==0, never the 0x6d2b host-fill magic.
 *
 * Module parameters:
 *   domain=N   PCI domain of the 59e7:0005 endpoint (default 0 = 2.4 GHz)
 *   wregs=N    1 = perform the register writes (default); 0 = read-only
 *   submit=N   1 = attempt the descriptor submit.  REFUSED (see above); the
 *              parameter exists only to document the boundary.
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

/* ETE register block: .data+0x2944 word0 = 0x4003a000. */
#define ETE_CA_BASE	0x4003a000UL
#define ETE_BAR0_OFF	0x3a000UL
#define ETE_WIN_LEN	0x1000UL

/* Message / glue block (shuangta_pcie_msg_reg_map): BAR0+0x39000. */
#define MSG_BAR0_OFF	0x39000UL
#define MSG_WIN_LEN	0x1000UL
#define MSG0_OFF	0x010	/* 0x40039010 pending/message mask */
#define MSG1_OFF	0x014	/* 0x40039014 */
#define DOORBELL_OFF	0x2d4	/* 0x400392d4 - OR bit 0 */
#define MSG5_OFF	0x2f0	/* 0x400392f0 */

/* Per-channel register-block offsets (pcie_ete_get_chn_cfg, .rodata+0x101c). */
static const unsigned long sr_block[3] = { 0x400, 0x450, 0x4a0 };
static const unsigned long dr_block[4] = { 0x590, 0x5e0, 0x630, 0x680 };

#define ETE_SR_CTRL	0x08
#define ETE_SR_BASE	0x10
#define ETE_SR_DEPTH	0x14
#define ETE_SR_WPTR	0x18
#define ETE_DR_BASE	0x30
#define ETE_DR_DEPTH	0x34
#define ETE_DR_WPTR	0x38
#define ETE_CHN_RES	0x2e8
#define ETE_CHN_RES_MASK 0xfffffc20U

#define ETE_DEPTH	32		/* .rodata+0x101c byte4 = 0x20 */

static unsigned int omo_domain;
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static unsigned int omo_wregs = 1;
module_param_named(wregs, omo_wregs, uint, 0444);
MODULE_PARM_DESC(wregs, "1 = perform the proven SR/DR register writes (default)");

static unsigned int omo_submit;
module_param_named(submit, omo_submit, uint, 0444);
MODULE_PARM_DESC(submit, "1 = attempt descriptor submit (REFUSED: no chip-scratch target)");

static struct pci_dev *omo_dev;

/* per-channel coherent node arrays */
static void *omo_sr_va[3];
static dma_addr_t omo_sr_dma[3];
static void *omo_dr_va[4];
static dma_addr_t omo_dr_dma[4];

/* ---- helpers ----------------------------------------------------------- */

static void omo_log_words(const char *tag, unsigned long off,
			  const void __iomem *win, unsigned int n)
{
	unsigned int i;

	pr_info("omo-eteinit: %s BAR0+0x%lx:", tag, off);
	for (i = 0; i < n; i++)
		pr_cont(" %08x", ioread32(win + 4UL * i));
	pr_cont("\n");
}

/* read one register and log it (no side effect) */
static u32 omo_rd(const void __iomem *win, unsigned long off, const char *name)
{
	u32 v = ioread32(win + off);

	pr_info("omo-eteinit:   %-22s [0x%03lx] = 0x%08x\n", name, off, v);
	return v;
}

/* write, then read straight back; log both */
static void omo_wr(const void __iomem *win, unsigned long off, u32 val,
		   const char *name)
{
	u32 rb;

	iowrite32(val, win + off);
	rb = ioread32(win + off);
	pr_info("omo-eteinit:   %-22s [0x%03lx] <= 0x%08x readback=0x%08x match=%s\n",
		name, off, val, rb, rb == val ? "YES" : "NO");
}

/* ---- register block ---------------------------------------------------- */

static int omo_alloc_rings(struct pci_dev *dev)
{
	size_t i;
	int ret;

	ret = pci_set_dma_mask(dev, DMA_BIT_MASK(32));
	pr_info("omo-eteinit: pci_set_dma_mask(32) rc=%d\n", ret);
	ret = pci_set_consistent_dma_mask(dev, DMA_BIT_MASK(32));
	pr_info("omo-eteinit: pci_set_consistent_dma_mask(32) rc=%d\n", ret);

	/* SR: (depth+2) nodes of 8 bytes, one array per channel 0..2. */
	for (i = 0; i < 3; i++) {
		size_t sz = (ETE_DEPTH + 2) * 8;

		omo_sr_va[i] = dma_alloc_coherent(&dev->dev, sz,
						  &omo_sr_dma[i], GFP_KERNEL);
		if (!omo_sr_va[i]) {
			pr_err("omo-eteinit: SR ch%zu dma_alloc_coherent(%zu) FAILED\n",
			       i, sz);
			return -ENOMEM;
		}
		pr_info("omo-eteinit: SR ch%zu nodes=%zuB virt=%px dma=0x%llx (zeroed)\n",
			i, sz, omo_sr_va[i],
			(unsigned long long)omo_sr_dma[i]);
	}

	/* DR: depth nodes of 8 bytes, one array per channel 3..6. */
	for (i = 0; i < 4; i++) {
		size_t sz = ETE_DEPTH * 8;

		omo_dr_va[i] = dma_alloc_coherent(&dev->dev, sz,
						  &omo_dr_dma[i], GFP_KERNEL);
		if (!omo_dr_va[i]) {
			pr_err("omo-eteinit: DR ch%zu dma_alloc_coherent(%zu) FAILED\n",
			       i + 3, sz);
			return -ENOMEM;
		}
		pr_info("omo-eteinit: DR ch%zu nodes=%zuB virt=%px dma=0x%llx (zeroed)\n",
			i + 3, sz, omo_dr_va[i],
			(unsigned long long)omo_dr_dma[i]);
	}
	return 0;
}

/*
 * Read-only pre-state of all seven channel register sets, then the vendor's
 * proven writes (pcie_ete_sr_reg_init @0x14a48 / pcie_ete_dr_reg_init
 * @0x1483c order: base, depth-1, wptr, ctrl) and the pcie_ete_chn_res RMW.
 */
static void omo_ete_init_block(u64 base)
{
	void __iomem *win;
	u32 v, nv;
	int i;

	win = ioremap(base + ETE_BAR0_OFF, ETE_WIN_LEN);
	if (!win) {
		pr_err("omo-eteinit: ioremap ETE block BAR0+0x%lx FAILED\n",
		       ETE_BAR0_OFF);
		return;
	}

	pr_info("omo-eteinit: ETE block mapped: CA 0x%lx = BAR0+0x%lx\n",
		ETE_CA_BASE, ETE_BAR0_OFF);
	omo_log_words("ETE block +0x000", 0x000, win, 8);
	omo_log_words("ETE block +0x2c0", 0x2c0, win, 8);
	omo_log_words("ETE block +0x840", 0x840, win, 8);

	/* ---- read-only pre-state ---- */
	pr_info("omo-eteinit: ---- SR/DR program registers BEFORE (read-only) ----\n");
	for (i = 0; i < 3; i++) {
		unsigned long b = sr_block[i];
		char t[32];

		scnprintf(t, sizeof(t), "SR ch%d ctrl", i);
		omo_rd(win, b + ETE_SR_CTRL, t);
		scnprintf(t, sizeof(t), "SR ch%d base", i);
		omo_rd(win, b + ETE_SR_BASE, t);
		scnprintf(t, sizeof(t), "SR ch%d depth-1", i);
		omo_rd(win, b + ETE_SR_DEPTH, t);
		scnprintf(t, sizeof(t), "SR ch%d wptr", i);
		omo_rd(win, b + ETE_SR_WPTR, t);
	}
	for (i = 0; i < 4; i++) {
		unsigned long b = dr_block[i];
		char t[32];

		scnprintf(t, sizeof(t), "DR ch%d base", i + 3);
		omo_rd(win, b + ETE_DR_BASE, t);
		scnprintf(t, sizeof(t), "DR ch%d depth-1", i + 3);
		omo_rd(win, b + ETE_DR_DEPTH, t);
		scnprintf(t, sizeof(t), "DR ch%d wptr", i + 3);
		omo_rd(win, b + ETE_DR_WPTR, t);
	}

	if (!omo_wregs) {
		pr_info("omo-eteinit: wregs=0: register writes NOT performed\n");
		omo_log_words("ETE block AFTER (read-only)", 0x400, win, 0x2c0 / 4);
		iounmap(win);
		return;
	}

	/* ---- the proven writes ---- */
	pr_info("omo-eteinit: ---- proven SR/DR register writes ----\n");
	for (i = 0; i < 3; i++) {
		unsigned long b = sr_block[i];
		char t[32];

		scnprintf(t, sizeof(t), "SR ch%d base", i);
		omo_wr(win, b + ETE_SR_BASE, (u32)omo_sr_dma[i], t);

		v = ioread32(win + b + ETE_SR_DEPTH);
		nv = (v & ~0x3ffU) | (ETE_DEPTH - 1);
		scnprintf(t, sizeof(t), "SR ch%d depth-1", i);
		omo_wr(win, b + ETE_SR_DEPTH, nv, t);

		scnprintf(t, sizeof(t), "SR ch%d wptr", i);
		omo_wr(win, b + ETE_SR_WPTR, 0, t);

		v = ioread32(win + b + ETE_SR_CTRL);
		nv = (v & ~0x7U) | 0;	/* cfg[5] queue select = 0 */
		scnprintf(t, sizeof(t), "SR ch%d ctrl", i);
		omo_wr(win, b + ETE_SR_CTRL, nv, t);
	}
	for (i = 0; i < 4; i++) {
		unsigned long b = dr_block[i];
		char t[32];

		scnprintf(t, sizeof(t), "DR ch%d base", i + 3);
		omo_wr(win, b + ETE_DR_BASE, (u32)omo_dr_dma[i], t);

		v = ioread32(win + b + ETE_DR_DEPTH);
		nv = (v & ~0x3ffU) | (ETE_DEPTH - 1);
		scnprintf(t, sizeof(t), "DR ch%d depth-1", i + 3);
		omo_wr(win, b + ETE_DR_DEPTH, nv, t);

		scnprintf(t, sizeof(t), "DR ch%d wptr", i + 3);
		omo_wr(win, b + ETE_DR_WPTR, 0, t);
	}

	/* pcie_ete_chn_res @0x7490: read, AND 0xfffffc20, write back. */
	pr_info("omo-eteinit: ---- pcie_ete_chn_res (mask 0x%08x) ----\n",
		ETE_CHN_RES_MASK);
	for (i = 0; i < 3; i++) {
		unsigned long b = sr_block[i];
		char t[32];

		v = ioread32(win + b + ETE_CHN_RES);
		nv = v & ETE_CHN_RES_MASK;
		scnprintf(t, sizeof(t), "SR ch%d chn_res", i);
		omo_wr(win, b + ETE_CHN_RES, nv, t);
	}
	for (i = 0; i < 4; i++) {
		unsigned long b = dr_block[i];
		char t[32];

		v = ioread32(win + b + ETE_CHN_RES);
		nv = v & ETE_CHN_RES_MASK;
		scnprintf(t, sizeof(t), "DR ch%d chn_res", i + 3);
		omo_wr(win, b + ETE_CHN_RES, nv, t);
	}

	pr_info("omo-eteinit: ---- SR/DR program registers AFTER ----\n");
	for (i = 0; i < 3; i++) {
		unsigned long b = sr_block[i];

		pr_info("omo-eteinit:   SR ch%d ctrl=0x%08x base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i, ioread32(win + b + ETE_SR_CTRL),
			ioread32(win + b + ETE_SR_BASE),
			ioread32(win + b + ETE_SR_DEPTH),
			ioread32(win + b + ETE_SR_WPTR));
	}
	for (i = 0; i < 4; i++) {
		unsigned long b = dr_block[i];

		pr_info("omo-eteinit:   DR ch%d base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i + 3, ioread32(win + b + ETE_DR_BASE),
			ioread32(win + b + ETE_DR_DEPTH),
			ioread32(win + b + ETE_DR_WPTR));
	}

	iounmap(win);
}

static void omo_msg_probe(u64 base)
{
	void __iomem *win = ioremap(base + MSG_BAR0_OFF, MSG_WIN_LEN);

	if (!win) {
		pr_err("omo-eteinit: ioremap message block BAR0+0x%lx FAILED\n",
		       MSG_BAR0_OFF);
		return;
	}
	pr_info("omo-eteinit: ---- message registers (read-only) ----\n");
	omo_rd(win, MSG0_OFF, "msg reg0/pending");
	omo_rd(win, MSG1_OFF, "msg reg1");
	omo_rd(win, DOORBELL_OFF, "doorbell 0x400392d4");
	omo_rd(win, MSG5_OFF, "msg reg5");
	iounmap(win);
}

/* ---- init / exit ------------------------------------------------------- */

static int __init omo_eteinit_init(void)
{
	u32 lo = 0, hi = 0;
	u64 base;
	u16 cmd0 = 0, cmd1 = 0, rb = 0xffff;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-eteinit: endpoint %04x:00:00.0 not found\n", omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev, 0x00, &id);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("omo-eteinit: unexpected id %04x:%04x\n",
				id & 0xffff, id >> 16);
	}

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);
	ret = pci_enable_device(omo_dev);
	if (ret) {
		pr_err("omo-eteinit: pci_enable_device rc=%d\n", ret);
		goto err_put;
	}
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("omo-eteinit: pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
		cmd0, cmd1);

	ret = pci_request_mem_regions(omo_dev, "omo-eteinit");
	if (ret) {
		pr_err("omo-eteinit: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-eteinit: pci_request_mem_regions rc=0 (MEM BARs claimed)\n");

	ret = pci_write_config_word(omo_dev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_dev, PCI_COMMAND, &rb);
	pr_info("omo-eteinit: cfg[0x004] <= 0x0007 readback=0x%04x MEM|MASTER=%s\n",
		rb, (rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		    (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("omo-eteinit: no usable BAR0 (lo=0x%08x hi=0x%08x)\n", lo, hi);
		ret = -ENODEV;
		goto err_release;
	}
	pr_info("omo-eteinit: BAR0 base=0x%llx (config-space read)\n",
		(unsigned long long)base);

	/* (2) the ring node arrays, vendor sizes. */
	ret = omo_alloc_rings(omo_dev);
	if (ret)
		goto err_release;

	/* (3+4) program registers at the proven offsets. */
	omo_ete_init_block(base);

	/* read-only message-register state. */
	omo_msg_probe(base);

	/* (5) the documented stop. */
	if (omo_submit) {
		pr_warn("omo-eteinit: submit=1 REFUSED - a node's address is the device VA of a HOST buffer (pcie_get_ete_addr -> oal_dma_map_single -> pcie_hostca_to_devva, runtime inbound window); there is no chip-scratch target in a bare node, and pcie_ete_sending_trigger builds a full HCC/BAL skb that this module does not reproduce. See docs/phase17/ete-init.md.\n");
	} else {
		pr_info("omo-eteinit: STOP before descriptor: node format and ring offsets are proven, but the node address is a host-buffer device VA (no chip-scratch target) and the transfer/completion semantics need the HCC/BAL + pcie_msg_init handler layer; no descriptor filled, no doorbell rung.\n");
	}

	pr_info("omo-eteinit: done - endpoint claimed, ring arrays allocated, SR/DR registers initialised and read back (wregs=%u)\n",
		omo_wregs);
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

static void __exit omo_eteinit_exit(void)
{
	int i;

	for (i = 0; i < 3; i++)
		if (omo_sr_va[i]) {
			dma_free_coherent(&omo_dev->dev, (ETE_DEPTH + 2) * 8,
					  omo_sr_va[i], omo_sr_dma[i]);
			omo_sr_va[i] = NULL;
		}
	for (i = 0; i < 4; i++)
		if (omo_dr_va[i]) {
			dma_free_coherent(&omo_dev->dev, ETE_DEPTH * 8,
					  omo_dr_va[i], omo_dr_dma[i]);
			omo_dr_va[i] = NULL;
		}
	if (omo_dev) {
		pci_release_mem_regions(omo_dev);
		pci_disable_device(omo_dev);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("omo-eteinit: unloaded, ring arrays freed, BARs released\n");
}

module_init(omo_eteinit_init);
module_exit(omo_eteinit_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Reproduce the vendor ETE engine init on an unowned endpoint; stop before the ambiguous descriptor");
