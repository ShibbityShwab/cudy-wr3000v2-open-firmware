// SPDX-License-Identifier: GPL-2.0
/*
 * calwrite: the first WRITE path to the WR3000 V2.0 chip's live calibration
 * block, deliberately reversible and single-byte.
 *
 * calread (docs/phase12/calread.md) proved we can read the chip's BAR0 window
 * and reproduce the vendor's `iwpriv Hisilicon0 alg get_2g_power_param` answers
 * from the 576-byte calibration block at BAR0 0x1b2f00.  This module proves we
 * can WRITE the same window, using the smallest possible experiment:
 *
 *   noop    - read the 576-byte block, write the SAME bytes back to the SAME
 *             addresses, read it again, and report byte-identity.  Pure
 *             write-capability test with zero semantic change.
 *   patch   - write the single 32-bit word 0x17161607 to BAR0 0x1b2f24, the
 *             first 2.4 GHz trim word (one nibble changed from 0x17161605).
 *   restore - write 0x17161605 back to BAR0 0x1b2f24 and read it back.
 *
 * Safety rules, coded in:
 *   - the only ioremap()ed window is exactly [0x1b2f00, 0x1b2f00+576);
 *   - patch/restore touch exactly the 4 bytes at 0x1b2f24 (block offset 0x24);
 *   - noop writes back only bytes it just read, over that same 576-byte window;
 *   - every trigger first takes a full 576-byte local backup, exposes it at
 *     /sys/kernel/debug/calwrite/backup as hex, and logs before/after dumps;
 *   - no pci_request_region, no pci_enable_device, no config-space write, no
 *     reset: the vendor driver keeps ownership throughout.
 *
 * The BAR0 base is read from PCI config space with pci_read_config_dword()
 * (the same read-only vendor-kernel accessor calread/barmap use).  Nothing is
 * written outside our own debugfs triggers.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

#define OMO_BLOCK_OFF	0x1b2f00UL	/* BAR0 / CPU address of the block */
#define OMO_BLOCK_LEN	576		/* 0x240 bytes */
#define OMO_2G_OFF	0x24		/* first 2.4 GHz trim word, in-block */
#define OMO_WORD_ORIG	0x17161605u	/* known-good value at 0x1b2f24 */
#define OMO_WORD_PATCH	0x17161607u	/* one-nibble patch */

static struct pci_dev *omo_dev;
static void __iomem *omo_win;		/* exactly OMO_BLOCK_LEN bytes */
static u8 *omo_backup;			/* latest full-block local backup */
static bool omo_backup_valid;
static char omo_backup_action[32];
static u64 omo_backup_ts;
static DEFINE_MUTEX(omo_lock);

static struct dentry *omo_dir;
static struct dentry *omo_noop_file;
static struct dentry *omo_patch_file;
static struct dentry *omo_restore_file;
static struct dentry *omo_backup_file;
static struct dentry *omo_block_file;

/* ---------- logging helpers ---------- */

/* Log a full buffer as 16-byte hex rows (each row a separate dmesg line). */
static void omo_log_dump(const char *tag, const u8 *buf)
{
	unsigned int i, j;

	pr_info("omo-calwrite: %s (%u bytes @ BAR0+0x%lx)\n",
		tag, OMO_BLOCK_LEN, OMO_BLOCK_OFF);
	for (i = 0; i < OMO_BLOCK_LEN; i += 16) {
		pr_info("omo-calwrite: %s %04x:", tag, i);
		for (j = 0; j < 16; j++)
			pr_cont(" %02x", buf[i + j]);
		pr_cont("\n");
	}
}

/* Full local backup of the 576-byte block, taken by every trigger. */
static void omo_take_backup(const char *action)
{
	memcpy_fromio(omo_backup, omo_win, OMO_BLOCK_LEN);
	omo_backup_valid = true;
	omo_backup_ts = ktime_get_ns();
	snprintf(omo_backup_action, sizeof(omo_backup_action), "%s", action);
}

/* ---------- dump rendering (debugfs read side) ---------- */

static void omo_render_hex(struct seq_file *s, const u8 *buf)
{
	unsigned int i, j;

	for (i = 0; i < OMO_BLOCK_LEN; i += 16) {
		seq_printf(s, "%04x:", i);
		for (j = 0; j < 16; j++)
			seq_printf(s, " %02x", buf[i + j]);
		seq_putc(s, '\n');
	}
}

static u32 omo_le32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

/* /sys/kernel/debug/calwrite/backup: the last full-block local backup. */
static int omo_backup_show(struct seq_file *s, void *v)
{
	mutex_lock(&omo_lock);
	seq_printf(s, "calwrite backup: BAR0+0x%lx len=%u (0x%x)\n",
		   OMO_BLOCK_OFF, OMO_BLOCK_LEN, OMO_BLOCK_LEN);
	if (!omo_backup_valid) {
		seq_puts(s, "no backup taken yet\n");
		mutex_unlock(&omo_lock);
		return 0;
	}
	seq_printf(s, "taken_by=%s ts_ns=%llu word@+0x%x=%08x\n",
		   omo_backup_action, (unsigned long long)omo_backup_ts,
		   OMO_2G_OFF, omo_le32(omo_backup + OMO_2G_OFF));
	omo_render_hex(s, omo_backup);
	mutex_unlock(&omo_lock);
	return 0;
}

static int omo_backup_open(struct inode *inode, struct file *file)
{
	return single_open(file, omo_backup_show, NULL);
}

static const struct file_operations omo_backup_fops = {
	.owner = THIS_MODULE,
	.open = omo_backup_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* /sys/kernel/debug/calwrite/block: a fresh live read of the block. */
static int omo_block_show(struct seq_file *s, void *v)
{
	u8 *buf;
	u32 w;

	buf = kmalloc(OMO_BLOCK_LEN, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	mutex_lock(&omo_lock);
	memcpy_fromio(buf, omo_win, OMO_BLOCK_LEN);
	w = omo_le32(buf + OMO_2G_OFF);
	seq_printf(s, "calwrite live block: BAR0+0x%lx len=%u (0x%x)\n",
		   OMO_BLOCK_OFF, OMO_BLOCK_LEN, OMO_BLOCK_LEN);
	seq_printf(s, "word@+0x%x=%08x (orig=%08x patched=%08x)\n",
		   OMO_2G_OFF, w, OMO_WORD_ORIG, OMO_WORD_PATCH);
	omo_render_hex(s, buf);
	mutex_unlock(&omo_lock);

	kfree(buf);
	return 0;
}

static int omo_block_open(struct inode *inode, struct file *file)
{
	return single_open(file, omo_block_show, NULL);
}

static const struct file_operations omo_block_fops = {
	.owner = THIS_MODULE,
	.open = omo_block_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/* ---------- write triggers ---------- */

static ssize_t omo_noop_write(struct file *f, const char __user *ubuf,
			      size_t count, loff_t *ppos)
{
	u8 *before, *after;
	unsigned int i;
	bool same = true;

	if (!omo_win)
		return -ENODEV;
	if (count == 0)
		return 0;

	before = kmalloc(OMO_BLOCK_LEN, GFP_KERNEL);
	after = kmalloc(OMO_BLOCK_LEN, GFP_KERNEL);
	if (!before || !after) {
		kfree(before);
		kfree(after);
		return -ENOMEM;
	}

	mutex_lock(&omo_lock);
	omo_take_backup("noop");
	memcpy_fromio(before, omo_win, OMO_BLOCK_LEN);
	omo_log_dump("noop BEFORE", before);

	/* Write the exact bytes we just read back to the exact addresses. */
	memcpy_toio(omo_win, before, OMO_BLOCK_LEN);
	mb();
	memcpy_fromio(after, omo_win, OMO_BLOCK_LEN);
	omo_log_dump("noop AFTER", after);

	for (i = 0; i < OMO_BLOCK_LEN; i++) {
		if (before[i] != after[i]) {
			same = false;
			break;
		}
	}
	pr_info("omo-calwrite: noop: wrote back %u bytes @ BAR0+0x%lx, re-read; identical=%s (first diff @%u)\n",
		OMO_BLOCK_LEN, OMO_BLOCK_OFF, same ? "yes" : "no",
		same ? 0u : i);
	mutex_unlock(&omo_lock);

	kfree(before);
	kfree(after);
	return count;
}

static const struct file_operations omo_noop_fops = {
	.owner = THIS_MODULE,
	.write = omo_noop_write,
	.llseek = no_llseek,
};

static ssize_t omo_patch_write(struct file *f, const char __user *ubuf,
			       size_t count, loff_t *ppos)
{
	u32 before, after;

	if (!omo_win)
		return -ENODEV;
	if (count == 0)
		return 0;

	mutex_lock(&omo_lock);
	omo_take_backup("patch");
	before = ioread32(omo_win + OMO_2G_OFF);
	pr_info("omo-calwrite: patch BEFORE word@BAR0+0x%lx=%08x\n",
		OMO_BLOCK_OFF + OMO_2G_OFF, before);

	iowrite32(OMO_WORD_PATCH, omo_win + OMO_2G_OFF);
	mb();
	after = ioread32(omo_win + OMO_2G_OFF);
	pr_info("omo-calwrite: patch: wrote %08x to BAR0+0x%lx, read back %08x, reached=%s\n",
		OMO_WORD_PATCH, OMO_BLOCK_OFF + OMO_2G_OFF, after,
		after == OMO_WORD_PATCH ? "yes" : "no");
	mutex_unlock(&omo_lock);

	return count;
}

static const struct file_operations omo_patch_fops = {
	.owner = THIS_MODULE,
	.write = omo_patch_write,
	.llseek = no_llseek,
};

static ssize_t omo_restore_write(struct file *f, const char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	u32 before, after;

	if (!omo_win)
		return -ENODEV;
	if (count == 0)
		return 0;

	mutex_lock(&omo_lock);
	omo_take_backup("restore");
	before = ioread32(omo_win + OMO_2G_OFF);
	pr_info("omo-calwrite: restore BEFORE word@BAR0+0x%lx=%08x\n",
		OMO_BLOCK_OFF + OMO_2G_OFF, before);

	iowrite32(OMO_WORD_ORIG, omo_win + OMO_2G_OFF);
	mb();
	after = ioread32(omo_win + OMO_2G_OFF);
	pr_info("omo-calwrite: restore: wrote %08x to BAR0+0x%lx, read back %08x, restored=%s\n",
		OMO_WORD_ORIG, OMO_BLOCK_OFF + OMO_2G_OFF, after,
		after == OMO_WORD_ORIG ? "yes" : "no");
	mutex_unlock(&omo_lock);

	return count;
}

static const struct file_operations omo_restore_fops = {
	.owner = THIS_MODULE,
	.write = omo_restore_write,
	.llseek = no_llseek,
};

/* ---------- init / exit ---------- */

static int __init omo_calwrite_init(void)
{
	u32 lo = 0, hi = 0;
	u64 base;

	omo_dev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-calwrite: endpoint 0000:00:00.0 not found\n");
		return -ENODEV;
	}

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("omo-calwrite: no usable BAR0 (lo=0x%08x hi=0x%08x)\n",
		       lo, hi);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENODEV;
	}

	/* No pci_request_region: the vendor driver owns the region. */
	omo_win = ioremap(base + OMO_BLOCK_OFF, OMO_BLOCK_LEN);
	if (!omo_win) {
		pr_err("omo-calwrite: ioremap of 0x%llx (%u bytes) failed\n",
		       (unsigned long long)(base + OMO_BLOCK_OFF), OMO_BLOCK_LEN);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENOMEM;
	}

	omo_backup = kmalloc(OMO_BLOCK_LEN, GFP_KERNEL);
	if (!omo_backup) {
		iounmap(omo_win);
		omo_win = NULL;
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENOMEM;
	}

	/* Load-time local backup, so /backup is populated from the start. */
	omo_take_backup("load");
	pr_info("omo-calwrite: BAR0 base=0x%llx window +0x%lx len=%u, writable window mapped, no claim\n",
		(unsigned long long)base, OMO_BLOCK_OFF, OMO_BLOCK_LEN);
	pr_info("omo-calwrite: load word@BAR0+0x%lx=%08x (orig=%08x patched=%08x) backup=%s\n",
		OMO_BLOCK_OFF + OMO_2G_OFF, omo_le32(omo_backup + OMO_2G_OFF),
		OMO_WORD_ORIG, OMO_WORD_PATCH, omo_backup_valid ? "yes" : "no");

	omo_dir = debugfs_create_dir("calwrite", NULL);
	if (IS_ERR_OR_NULL(omo_dir)) {
		pr_warn("omo-calwrite: debugfs dir unavailable (%ld)\n",
			PTR_ERR_OR_ZERO(omo_dir));
		omo_dir = NULL;
		iounmap(omo_win);
		omo_win = NULL;
		kfree(omo_backup);
		omo_backup = NULL;
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENODEV;
	}

	omo_noop_file = debugfs_create_file("noop", 0200, omo_dir, NULL,
					    &omo_noop_fops);
	omo_patch_file = debugfs_create_file("patch", 0200, omo_dir, NULL,
					     &omo_patch_fops);
	omo_restore_file = debugfs_create_file("restore", 0200, omo_dir, NULL,
					       &omo_restore_fops);
	omo_backup_file = debugfs_create_file("backup", 0444, omo_dir, NULL,
					      &omo_backup_fops);
	omo_block_file = debugfs_create_file("block", 0444, omo_dir, NULL,
					     &omo_block_fops);
	if (IS_ERR_OR_NULL(omo_noop_file) || IS_ERR_OR_NULL(omo_patch_file) ||
	    IS_ERR_OR_NULL(omo_restore_file) || IS_ERR_OR_NULL(omo_backup_file) ||
	    IS_ERR_OR_NULL(omo_block_file)) {
		pr_warn("omo-calwrite: debugfs files unavailable\n");
		debugfs_remove_recursive(omo_dir);
		omo_dir = NULL;
		iounmap(omo_win);
		omo_win = NULL;
		kfree(omo_backup);
		omo_backup = NULL;
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENODEV;
	}

	pr_info("omo-calwrite: /sys/kernel/debug/calwrite ready (noop patch restore backup block)\n");
	return 0;
}

static void __exit omo_calwrite_exit(void)
{
	if (omo_dir) {
		debugfs_remove_recursive(omo_dir);
		omo_dir = NULL;
		omo_noop_file = NULL;
		omo_patch_file = NULL;
		omo_restore_file = NULL;
		omo_backup_file = NULL;
		omo_block_file = NULL;
	}
	kfree(omo_backup);
	omo_backup = NULL;
	if (omo_win) {
		iounmap(omo_win);
		omo_win = NULL;
	}
	if (omo_dev) {
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("omo-calwrite: debugfs removed, window unmapped, device released\n");
}

module_init(omo_calwrite_init);
module_exit(omo_calwrite_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Reversible single-word write path to the WR3000 V2.0 calibration block");
