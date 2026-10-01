// SPDX-License-Identifier: GPL-2.0
/*
 * calread: read-only export of the WR3000 V2.0 chip's live calibration block.
 *
 * The chip's 16 MB BAR0 window is its on-chip memory address-for-address
 * (docs/phase11/barmap.md): BAR0 offset X equals CPU address X.  The live
 * calibration block sits at BAR0 0x1b2f00, 576 bytes, and contains the
 * 2.4 GHz power table (26 words at block offset 0x24) and the 5 GHz power
 * table (18 words at block offset 0x1ea) - the same data the vendor answers
 * through 'iwpriv Hisilicon0 alg get_2g_power_param' / 'get_5g_power_param'.
 *
 * This module:
 *   1. reads the BAR0 base from PCI config space with pci_read_config_dword()
 *      (a read-only vendor-kernel accessor - no struct pci_dev layout risk);
 *   2. ioremap()s the 576-byte block at BAR0+0x1b2f00 WITHOUT claiming the
 *      device (no pci_request_region, no pci_enable_device, no reset) - the
 *      vendor driver keeps ownership throughout;
 *   3. copies the block out with memcpy_fromio() into a local buffer once, at
 *      load time, and renders it as text: the raw 576 bytes in hex plus the
 *      two power tables as 32-bit little-endian hex words;
 *   4. exposes that text through /sys/kernel/debug/calread/tables;
 *   5. logs the same summary to dmesg at load.
 *
 * The 5 GHz table begins at a 2-byte-aligned (offset % 4 == 2) address, so
 * the words are assembled bytewise from the copied block rather than read as
 * aligned ioread32()s.  Nothing is written to the BAR or to config space; the
 * window is iounmap()ed and the device reference dropped on unload.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

#define OMO_BLOCK_OFF	0x1b2f00UL	/* BAR0 / CPU address of the block */
#define OMO_BLOCK_LEN	576		/* 0x240 bytes */
#define OMO_2G_OFF	0x24		/* 2.4 GHz table, within the block */
#define OMO_2G_WORDS	26
#define OMO_5G_OFF	0x1ea		/* 5 GHz table, within the block */
#define OMO_5G_WORDS	18
#define OMO_DUMP_CAP	8192

static struct pci_dev *omo_dev;
static void __iomem *omo_win;
static u8 *omo_block;			/* copied 576-byte calibration block */
static char *omo_dump;			/* rendered text shown by debugfs */
static size_t omo_dump_len;
static struct dentry *omo_dir;
static struct dentry *omo_file;

/* Little-endian u32 assembled from bytes (works at any byte offset). */
static u32 omo_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

static size_t omo_append(char *buf, size_t cap, size_t len, const char *fmt, ...)
{
	va_list ap;
	int n;

	if (len >= cap)
		return len;
	va_start(ap, fmt);
	n = vscnprintf(buf + len, cap - len, fmt, ap);
	va_end(ap);

	return len + (size_t)n;
}

/* Render the block hex plus the two tables into omo_dump. */
static void omo_render(void)
{
	size_t len = 0;
	unsigned int i, w, j;

	len = omo_append(omo_dump, OMO_DUMP_CAP, len,
			 "calread: WR3000 V2.0 calibration block (read-only)\n");
	len = omo_append(omo_dump, OMO_DUMP_CAP, len,
			 "block: BAR0+0x%lx len=%u (0x%x)\n",
			 OMO_BLOCK_OFF, OMO_BLOCK_LEN, OMO_BLOCK_LEN);

	len = omo_append(omo_dump, OMO_DUMP_CAP, len,
			 "-- raw %u-byte block --\n", OMO_BLOCK_LEN);
	for (i = 0; i < OMO_BLOCK_LEN; i += 16) {
		len = omo_append(omo_dump, OMO_DUMP_CAP, len, "%04x:", i);
		for (j = 0; j < 16; j++)
			len = omo_append(omo_dump, OMO_DUMP_CAP, len, " %02x",
					 omo_block[i + j]);
		len = omo_append(omo_dump, OMO_DUMP_CAP, len, "\n");
	}

	len = omo_append(omo_dump, OMO_DUMP_CAP, len,
			 "-- 2g_power_param: %u words @ block+0x%x --\n",
			 OMO_2G_WORDS, OMO_2G_OFF);
	for (w = 0; w < OMO_2G_WORDS; w++)
		len = omo_append(omo_dump, OMO_DUMP_CAP, len, "%s%08x",
				 w ? " " : "", omo_le32(omo_block + OMO_2G_OFF + 4 * w));
	len = omo_append(omo_dump, OMO_DUMP_CAP, len, "\n");

	len = omo_append(omo_dump, OMO_DUMP_CAP, len,
			 "-- 5g_power_param: %u words @ block+0x%x --\n",
			 OMO_5G_WORDS, OMO_5G_OFF);
	for (w = 0; w < OMO_5G_WORDS; w++)
		len = omo_append(omo_dump, OMO_DUMP_CAP, len, "%s%08x",
				 w ? " " : "", omo_le32(omo_block + OMO_5G_OFF + 4 * w));
	len = omo_append(omo_dump, OMO_DUMP_CAP, len, "\n");

	omo_dump_len = len;
}

static ssize_t omo_tables_read(struct file *f, char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(ubuf, count, ppos, omo_dump,
				       omo_dump_len);
}

static const struct file_operations omo_tables_fops = {
	.owner = THIS_MODULE,
	.read = omo_tables_read,
	.llseek = default_llseek,
};

static int __init omo_calread_init(void)
{
	u32 lo = 0, hi = 0;
	u64 base;
	unsigned int w;

	omo_dev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-calread: endpoint 0000:00:00.0 not found\n");
		return -ENODEV;
	}

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("omo-calread: no usable BAR0 (lo=0x%08x hi=0x%08x)\n",
		       lo, hi);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENODEV;
	}

	/* No pci_request_region: the vendor driver owns the region. */
	omo_win = ioremap(base + OMO_BLOCK_OFF, OMO_BLOCK_LEN);
	if (!omo_win) {
		pr_err("omo-calread: ioremap of 0x%llx (%u bytes) failed\n",
		       (unsigned long long)(base + OMO_BLOCK_OFF), OMO_BLOCK_LEN);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENOMEM;
	}

	omo_block = kmalloc(OMO_BLOCK_LEN, GFP_KERNEL);
	omo_dump = kmalloc(OMO_DUMP_CAP, GFP_KERNEL);
	if (!omo_block || !omo_dump) {
		pr_err("omo-calread: allocation failed\n");
		kfree(omo_dump);
		kfree(omo_block);
		omo_dump = NULL;
		omo_block = NULL;
		iounmap(omo_win);
		omo_win = NULL;
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENOMEM;
	}

	memcpy_fromio(omo_block, omo_win, OMO_BLOCK_LEN);
	omo_render();

	omo_dir = debugfs_create_dir("calread", NULL);
	if (IS_ERR_OR_NULL(omo_dir)) {
		pr_warn("omo-calread: debugfs dir unavailable (%ld)\n",
			PTR_ERR_OR_ZERO(omo_dir));
		omo_dir = NULL;
	} else {
		omo_file = debugfs_create_file("tables", 0444, omo_dir, NULL,
					       &omo_tables_fops);
		if (IS_ERR_OR_NULL(omo_file)) {
			pr_warn("omo-calread: debugfs file unavailable (%ld)\n",
				PTR_ERR_OR_ZERO(omo_file));
			omo_file = NULL;
		}
	}

	pr_info("omo-calread: BAR0 base=0x%llx mapped +0x%lx (%u bytes), read-only, no claim\n",
		(unsigned long long)base, OMO_BLOCK_OFF, OMO_BLOCK_LEN);
	pr_info("omo-calread: block first words: %08x %08x %08x %08x\n",
		omo_le32(omo_block + 0x00), omo_le32(omo_block + 0x04),
		omo_le32(omo_block + 0x08), omo_le32(omo_block + 0x0c));
	pr_info("omo-calread: 2g_power_param[%u] @+0x%x:",
		OMO_2G_WORDS, OMO_2G_OFF);
	for (w = 0; w < OMO_2G_WORDS; w++)
		pr_cont(" %08x", omo_le32(omo_block + OMO_2G_OFF + 4 * w));
	pr_cont("\n");
	pr_info("omo-calread: 5g_power_param[%u] @+0x%x:",
		OMO_5G_WORDS, OMO_5G_OFF);
	for (w = 0; w < OMO_5G_WORDS; w++)
		pr_cont(" %08x", omo_le32(omo_block + OMO_5G_OFF + 4 * w));
	pr_cont("\n");

	return 0;
}

static void __exit omo_calread_exit(void)
{
	if (omo_file) {
		debugfs_remove(omo_file);
		omo_file = NULL;
	}
	if (omo_dir) {
		debugfs_remove(omo_dir);
		omo_dir = NULL;
	}
	kfree(omo_dump);
	kfree(omo_block);
	omo_dump = NULL;
	omo_block = NULL;
	if (omo_win) {
		iounmap(omo_win);
		omo_win = NULL;
	}
	if (omo_dev) {
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("omo-calread: block unmapped, debugfs removed, device released\n");
}

module_init(omo_calread_init);
module_exit(omo_calread_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only export of the WR3000 V2.0 live calibration block");
