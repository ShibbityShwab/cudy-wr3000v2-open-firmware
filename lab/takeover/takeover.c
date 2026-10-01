// SPDX-License-Identifier: GPL-2.0
/*
 * takeover: inspect one WR3000 V2.0 Wi-Fi endpoint's BAR0 window while the
 * vendor driver owns it, and again while nothing owns it.
 *
 * This is the read-only half of the phase-15 takeover milestone.  It is the
 * hwprobe/ringwatch pattern aimed at a *selectable* endpoint:
 *
 *   1. picks the endpoint by PCI *domain* (module parameter `domain`), because
 *      the two radios are 0000:00:00.0 and 0001:00:00.0 - same bus/dev/func,
 *      different domain;
 *   2. reads BAR0's base from PCI config space with pci_read_config_dword()
 *      (a read-only vendor-kernel accessor, so there is no struct pci_dev
 *      layout risk) and ioremap()s three small windows WITHOUT claiming the
 *      device - no pci_request_region, no pci_enable_device, no reset, so the
 *      vendor driver keeps (or, after an unbind, has already given up) the
 *      region and we only borrow the mapping;
 *   3. logs, and exposes through bounded debugfs reads:
 *        identity  - the register-block anchor words at BAR0+0x3b8000,
 *        fwheader  - the firmware-image header at BAR0+0x40000,
 *        rings     - the two live cursor markers, sampled fresh on each read,
 *        refcheck  - PCI config identity plus a pci_dev_get()/pci_dev_put()
 *                    reference round trip, to show the device object is still
 *                    present on the bus after an unbind.
 *
 * Everything is read-only toward the chip: there is no iowrite32, no config
 * write, no claim and no reset anywhere in this file.  It never unbinds or
 * rebinds anything - the handover is driven from userspace.
 *
 * Module parameter:
 *   domain=N   PCI domain of the 59e7:0005 endpoint to inspect (default 0)
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/string.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

/* The three bounded windows, all BAR0 byte offsets. */
#define OMO_ID_OFF	0x3b8000UL	/* register-block anchor (identity) */
#define OMO_ID_LEN	16
#define OMO_FW_OFF	0x040000UL	/* firmware image header */
#define OMO_FW_LEN	32
#define OMO_RING_OFF	0x1d0900UL	/* cursor ring A (region) */
#define OMO_RING_LEN	0x100
#define OMO_RING_ALIAS_OFF	0x8c8900UL	/* cursor ring A alias */
#define OMO_RING_ALIAS_LEN	0x100
#define OMO_RING_MARK_A	0x3c		/* head marker within the region */
#define OMO_RING_MARK_B	0xc0		/* tail marker within the region */

static unsigned int omo_domain;		/* 0 = 0000:00:00.0, 1 = 0001:... */
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static struct pci_dev *omo_dev;
static void __iomem *omo_id_win;
static void __iomem *omo_fw_win;
static void __iomem *omo_ring_win;
static void __iomem *omo_ring_alias_win;
static struct dentry *omo_dir;

static u32 omo_fw_cache[OMO_FW_LEN / 4];
static u32 omo_id_cache[OMO_ID_LEN / 4];

static void omo_log_words(const char *tag, unsigned long off, const u32 *w,
			  unsigned int n)
{
	unsigned int i;

	pr_info("omo-takeover: %s BAR0+0x%lx:", tag, off);
	for (i = 0; i < n; i++)
		pr_cont(" %08x", w[i]);
	pr_cont("\n");
}

static void omo_read_id(void)
{
	unsigned int i;

	for (i = 0; i < OMO_ID_LEN / 4; i++)
		omo_id_cache[i] = ioread32(omo_id_win + 4UL * i);
}

static void omo_read_fw(void)
{
	unsigned int i;

	for (i = 0; i < OMO_FW_LEN / 4; i++)
		omo_fw_cache[i] = ioread32(omo_fw_win + 4UL * i);
}

static void omo_read_rings(u32 out[4])
{
	out[0] = ioread32(omo_ring_win + OMO_RING_MARK_A);
	out[1] = ioread32(omo_ring_win + OMO_RING_MARK_B);
	out[2] = ioread32(omo_ring_alias_win + OMO_RING_MARK_A);
	out[3] = ioread32(omo_ring_alias_win + OMO_RING_MARK_B);
}

/* ---- debugfs read handlers -------------------------------------------- */

static int omo_identity_show(struct seq_file *s, void *v)
{
	unsigned int i;

	omo_read_id();
	seq_printf(s, "takeover: endpoint domain=%u identity words BAR0+0x%lx (%u)\n",
		   omo_domain, OMO_ID_OFF, OMO_ID_LEN / 4);
	for (i = 0; i < OMO_ID_LEN / 4; i++)
		seq_printf(s, "0x%08lx %08x\n", OMO_ID_OFF + 4UL * i,
			   omo_id_cache[i]);
	return 0;
}

static int omo_fwheader_show(struct seq_file *s, void *v)
{
	unsigned int i;

	omo_read_fw();
	seq_printf(s, "takeover: endpoint domain=%u firmware header BAR0+0x%lx (%u bytes)\n",
		   omo_domain, OMO_FW_OFF, OMO_FW_LEN);
	for (i = 0; i < OMO_FW_LEN / 4; i++)
		seq_printf(s, "0x%08lx %08x\n", OMO_FW_OFF + 4UL * i,
			   omo_fw_cache[i]);
	return 0;
}

static int omo_rings_show(struct seq_file *s, void *v)
{
	u32 m[4];
	u64 ts;

	omo_read_rings(m);
	ts = ktime_get_ns();
	seq_printf(s, "takeover: endpoint domain=%u ring sample ts_ns=%llu\n",
		   omo_domain, (unsigned long long)ts);
	seq_printf(s, "0x%08lx %08x\n", OMO_RING_OFF + OMO_RING_MARK_A, m[0]);
	seq_printf(s, "0x%08lx %08x\n", OMO_RING_OFF + OMO_RING_MARK_B, m[1]);
	seq_printf(s, "0x%08lx %08x\n",
		   OMO_RING_ALIAS_OFF + OMO_RING_MARK_A, m[2]);
	seq_printf(s, "0x%08lx %08x\n",
		   OMO_RING_ALIAS_OFF + OMO_RING_MARK_B, m[3]);

	pr_info("omo-takeover: rings domain=%u ts_ns=%llu %08x %08x %08x %08x\n",
		omo_domain, (unsigned long long)ts, m[0], m[1], m[2], m[3]);
	return 0;
}

static int omo_refcheck_show(struct seq_file *s, void *v)
{
	struct pci_dev *again;
	u16 vid = 0, did = 0;
	u8 rev = 0;
	u32 cls = 0;

	pci_read_config_word(omo_dev, PCI_VENDOR_ID, &vid);
	pci_read_config_word(omo_dev, PCI_DEVICE_ID, &did);
	pci_read_config_byte(omo_dev, PCI_REVISION_ID, &rev);
	pci_read_config_dword(omo_dev, PCI_CLASS_REVISION, &cls);

	/* A reference round trip: does the device object still exist? */
	again = pci_dev_get(omo_dev);
	pci_dev_put(omo_dev);

	seq_printf(s, "takeover: endpoint domain=%u present=%s\n",
		   omo_domain, again ? "yes" : "no");
	seq_printf(s, "config vendor=%04x device=%04x class=0x%06x rev=0x%02x\n",
		   vid, did, cls >> 8, rev);
	seq_printf(s, "pci_dev_get returned %s (pci_dev_put done)\n",
		   again ? "the device" : "NULL");

	pr_info("omo-takeover: refcheck domain=%u present=%d vendor=%04x device=%04x class=0x%06x rev=0x%02x\n",
		omo_domain, again ? 1 : 0, vid, did, cls >> 8, rev);
	return 0;
}

#define OMO_DEFINE_SEQ(_name)						\
static int omo_##_name##_open(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, omo_##_name##_show, NULL);		\
}									\
static const struct file_operations omo_##_name##_fops = {		\
	.owner = THIS_MODULE,						\
	.open = omo_##_name##_open,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
}

OMO_DEFINE_SEQ(identity);
OMO_DEFINE_SEQ(fwheader);
OMO_DEFINE_SEQ(rings);
OMO_DEFINE_SEQ(refcheck);

/* ---- init / exit ------------------------------------------------------- */

static int __init omo_takeover_init(void)
{
	u32 lo = 0, hi = 0;
	u64 base;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-takeover: endpoint %04x:00:00.0 not found\n",
		       omo_domain);
		return -ENODEV;
	}

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("omo-takeover: no usable BAR0 (lo=0x%08x hi=0x%08x)\n",
		       lo, hi);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENODEV;
	}

	/* No pci_request_region: the vendor driver owns (or owned) the region. */
	omo_id_win = ioremap(base + OMO_ID_OFF, OMO_ID_LEN);
	omo_fw_win = ioremap(base + OMO_FW_OFF, OMO_FW_LEN);
	omo_ring_win = ioremap(base + OMO_RING_OFF, OMO_RING_LEN);
	omo_ring_alias_win = ioremap(base + OMO_RING_ALIAS_OFF,
				     OMO_RING_ALIAS_LEN);
	if (!omo_id_win || !omo_fw_win || !omo_ring_win || !omo_ring_alias_win) {
		pr_err("omo-takeover: ioremap failed\n");
		ret = -ENOMEM;
		goto err_unmap;
	}

	/* Load-time read + log: identity, firmware header, one ring sample. */
	omo_read_id();
	omo_log_words("identity", OMO_ID_OFF, omo_id_cache,
		      OMO_ID_LEN / 4);
	omo_read_fw();
	omo_log_words("fw header", OMO_FW_OFF, omo_fw_cache,
		      OMO_FW_LEN / 4);
	{
		u32 m[4];

		omo_read_rings(m);
		pr_info("omo-takeover: rings BAR0+0x%lx/%lx = %08x %08x, alias %08x %08x\n",
			OMO_RING_OFF + OMO_RING_MARK_A,
			OMO_RING_OFF + OMO_RING_MARK_B, m[0], m[1], m[2], m[3]);
	}

	pr_info("omo-takeover: domain=%u BAR0 base=0x%llx, read-only, no claim\n",
		omo_domain, (unsigned long long)base);

	omo_dir = debugfs_create_dir("takeover", NULL);
	if (IS_ERR_OR_NULL(omo_dir)) {
		pr_warn("omo-takeover: debugfs dir unavailable (%ld)\n",
			PTR_ERR_OR_ZERO(omo_dir));
		omo_dir = NULL;
		ret = -ENODEV;
		goto err_unmap;
	}
	if (IS_ERR_OR_NULL(debugfs_create_file("identity", 0444, omo_dir, NULL,
					       &omo_identity_fops)) ||
	    IS_ERR_OR_NULL(debugfs_create_file("fwheader", 0444, omo_dir, NULL,
					       &omo_fwheader_fops)) ||
	    IS_ERR_OR_NULL(debugfs_create_file("rings", 0444, omo_dir, NULL,
					       &omo_rings_fops)) ||
	    IS_ERR_OR_NULL(debugfs_create_file("refcheck", 0444, omo_dir, NULL,
					       &omo_refcheck_fops))) {
		pr_warn("omo-takeover: debugfs files unavailable\n");
		ret = -ENODEV;
		goto err_debugfs;
	}

	pr_info("omo-takeover: ready (debugfs /sys/kernel/debug/takeover/{identity,fwheader,rings,refcheck})\n");
	return 0;

err_debugfs:
	debugfs_remove_recursive(omo_dir);
	omo_dir = NULL;
err_unmap:
	if (omo_ring_alias_win) {
		iounmap(omo_ring_alias_win);
		omo_ring_alias_win = NULL;
	}
	if (omo_ring_win) {
		iounmap(omo_ring_win);
		omo_ring_win = NULL;
	}
	if (omo_fw_win) {
		iounmap(omo_fw_win);
		omo_fw_win = NULL;
	}
	if (omo_id_win) {
		iounmap(omo_id_win);
		omo_id_win = NULL;
	}
	pci_dev_put(omo_dev);
	omo_dev = NULL;
	return ret;
}

static void __exit omo_takeover_exit(void)
{
	if (omo_dir) {
		debugfs_remove_recursive(omo_dir);
		omo_dir = NULL;
	}
	if (omo_ring_alias_win) {
		iounmap(omo_ring_alias_win);
		omo_ring_alias_win = NULL;
	}
	if (omo_ring_win) {
		iounmap(omo_ring_win);
		omo_ring_win = NULL;
	}
	if (omo_fw_win) {
		iounmap(omo_fw_win);
		omo_fw_win = NULL;
	}
	if (omo_id_win) {
		iounmap(omo_id_win);
		omo_id_win = NULL;
	}
	if (omo_dev) {
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("omo-takeover: debugfs removed, windows unmapped, device released\n");
}

module_init(omo_takeover_init);
module_exit(omo_takeover_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only inspector for one WR3000 V2.0 Wi-Fi endpoint BAR0 window");
