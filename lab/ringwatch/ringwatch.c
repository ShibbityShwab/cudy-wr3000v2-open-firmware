// SPDX-License-Identifier: GPL-2.0
/*
 * ringwatch: watch the WR3000 V2.0 chip's live pointer rings move, in place.
 *
 * docs/phase13/rings.md found two 32-slot pointer arrays (BAR0/CPU 0x1d0900
 * and 0x8c8900) whose packed head/tail markers changed between two snapshots,
 * and a live queue struct at 0x1b8e00.  This module watches them in real time:
 *
 *   1. reads the BAR0 base from PCI config space with pci_read_config_dword()
 *      (a read-only vendor-kernel accessor - no struct pci_dev layout risk);
 *   2. ioremap()s each configured region WITHOUT claiming the device (no
 *      pci_request_region, no pci_enable_device, no reset) - the vendor driver
 *      keeps ownership throughout;
 *   3. on a debugfs write to /sys/kernel/debug/ringwatch/sample, reads every
 *      region with ioread32() and stores a timestamped snapshot in a kernel
 *      ring buffer that keeps the last N snapshots (N configurable, default
 *      32), including an empty-snapshot counter so a full lap is detectable;
 *   4. renders every stored snapshot as text (one "offset value timestamp"
 *      line per word) at /sys/kernel/debug/ringwatch/log;
 *   5. exposes one full dump of the same regions, captured at load, at
 *      /sys/kernel/debug/ringwatch/regions;
 *   6. logs a one-line summary per snapshot to dmesg.
 *
 * Everything is read-only toward the chip: there is no iowrite32, no config
 * write, no claim and no reset anywhere in this file.  The only write accepted
 * is by userspace into OUR debugfs node, which selects "take a snapshot".
 *
 * Module parameters:
 *   regions="start:len,start:len,..."  BAR0 byte ranges (default: the three
 *                                      phase-13 regions, 0x1d0900:0x100,
 *                                      0x8c8900:0x100, 0x1b8e00:0x40)
 *   nsnapshots=N                        ring depth (default 32)
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005
#define OMO_BAR_SIZE	(16UL * 1024 * 1024)	/* sysfs BAR0 len 0x1000000 */
#define OMO_MAX_REGIONS	8
#define OMO_MAX_WORDS	65536			/* total across all regions */

static char *regions = "0x1d0900:0x100,0x8c8900:0x100,0x1b8e00:0x40";
module_param(regions, charp, 0444);
MODULE_PARM_DESC(regions, "comma-separated BAR0 ranges start:len (len in bytes, multiple of 4)");

static unsigned int nsnapshots = 32;
module_param(nsnapshots, uint, 0444);
MODULE_PARM_DESC(nsnapshots, "number of snapshots kept in the ring (default 32)");

struct omo_region {
	unsigned long start;	/* BAR0 / CPU byte offset */
	unsigned int len;	/* bytes */
	unsigned int words;	/* len / 4 */
	void __iomem *win;
};

static struct pci_dev *omo_dev;
static struct omo_region omo_reg[OMO_MAX_REGIONS];
static unsigned int omo_nreg;
static unsigned int omo_total_words;

/* The snapshot ring: one timestamp and one sequence number per entry, plus
 * omo_total_words values per entry, indexed modulo nsnapshots. */
static u64 *omo_ts;		/* [nsnapshots] */
static u64 *omo_seq;		/* [nsnapshots] monotonic snapshot number */
static u32 *omo_vals;		/* [nsnapshots * omo_total_words] */
static unsigned int omo_head;	/* next slot to write */
static unsigned int omo_count;	/* slots currently valid */
static u64 omo_next_seq = 1;
static DEFINE_MUTEX(omo_lock);

/* The load-time one-shot dump. */
static u32 *omo_dump_vals;
static u64 omo_dump_ts;

static struct dentry *omo_dir;
static struct dentry *omo_log_file;
static struct dentry *omo_regions_file;
static struct dentry *omo_sample_file;

static int omo_parse_regions(void)
{
	char *base, *p, *tok;
	unsigned int r = 0;
	unsigned long long total = 0;

	base = kstrdup(regions, GFP_KERNEL);
	if (!base)
		return -ENOMEM;

	p = base;
	while ((tok = strsep(&p, ",")) != NULL) {
		char *colon;
		unsigned long long start, len;

		if (!*tok)
			continue;
		if (r >= OMO_MAX_REGIONS) {
			pr_err("omo-ringwatch: more than %u regions\n",
			       OMO_MAX_REGIONS);
			kfree(base);
			return -EINVAL;
		}

		colon = strchr(tok, ':');
		if (colon) {
			*colon = '\0';
			if (kstrtoull(colon + 1, 0, &len)) {
				pr_err("omo-ringwatch: bad length '%s'\n",
				       colon + 1);
				kfree(base);
				return -EINVAL;
			}
		} else {
			len = 4;
		}

		if (kstrtoull(tok, 0, &start)) {
			pr_err("omo-ringwatch: bad start '%s'\n", tok);
			kfree(base);
			return -EINVAL;
		}
		if (!len || (len & 3) || start + len > OMO_BAR_SIZE) {
			pr_err("omo-ringwatch: range %llx:%llx out of bounds or unaligned\n",
			       start, len);
			kfree(base);
			return -EINVAL;
		}

		omo_reg[r].start = (unsigned long)start;
		omo_reg[r].len = (unsigned int)len;
		omo_reg[r].words = (unsigned int)(len / 4);
		total += len / 4;
		r++;
	}
	kfree(base);

	if (!r) {
		pr_err("omo-ringwatch: no usable region in '%s'\n", regions);
		return -EINVAL;
	}
	if (total > OMO_MAX_WORDS) {
		pr_err("omo-ringwatch: %llu words exceeds cap %u\n",
		       total, OMO_MAX_WORDS);
		return -EINVAL;
	}

	omo_nreg = r;
	omo_total_words = (unsigned int)total;
	return 0;
}

static void omo_read_region(const struct omo_region *reg, u32 *dst)
{
	unsigned int w;

	for (w = 0; w < reg->words; w++)
		dst[w] = ioread32(reg->win + (unsigned long)w * 4);
}

/* Read every region into vals[] (omo_total_words entries, region order). */
static void omo_read_all(u32 *vals)
{
	unsigned int r, k = 0;

	for (r = 0; r < omo_nreg; r++) {
		omo_read_region(&omo_reg[r], vals + k);
		k += omo_reg[r].words;
	}
}

static void omo_take_snapshot(void)
{
	unsigned int r, k = 0;
	u64 now, seq;
	u32 *dst;

	mutex_lock(&omo_lock);
	now = ktime_get_ns();
	seq = omo_next_seq++;
	dst = omo_vals + (size_t)(omo_head % nsnapshots) * omo_total_words;

	for (r = 0; r < omo_nreg; r++) {
		omo_read_region(&omo_reg[r], dst + k);
		k += omo_reg[r].words;
	}
	omo_ts[omo_head % nsnapshots] = now;
	omo_seq[omo_head % nsnapshots] = seq;
	omo_head++;
	if (omo_count < nsnapshots)
		omo_count++;
	mutex_unlock(&omo_lock);

	pr_info("omo-ringwatch: snapshot %llu ts_ns=%llu",
		(unsigned long long)seq, (unsigned long long)now);
	k = 0;
	for (r = 0; r < omo_nreg; r++) {
		pr_cont(" r%u@0x%lx=%08x", r, omo_reg[r].start, dst[k]);
		k += omo_reg[r].words;
	}
	pr_cont("\n");
}

/* Render one snapshot's words as "offset value timestamp" lines. */
static void omo_show_snapshot(struct seq_file *s, unsigned int slot)
{
	unsigned int r, w, k = 0;
	u64 ts = omo_ts[slot];
	const u32 *vals = omo_vals + (size_t)slot * omo_total_words;

	for (r = 0; r < omo_nreg; r++) {
		for (w = 0; w < omo_reg[r].words; w++) {
			seq_printf(s, "0x%08lx 0x%08x %llu\n",
				   omo_reg[r].start + (unsigned long)w * 4,
				   vals[k + w], (unsigned long long)ts);
		}
		k += omo_reg[r].words;
	}
}

static int omo_log_show(struct seq_file *s, void *v)
{
	unsigned int i;

	mutex_lock(&omo_lock);
	seq_printf(s, "ringwatch: regions=%u words_per_snapshot=%u kept=%u/%u total_taken=%llu\n",
		   omo_nreg, omo_total_words, omo_count, nsnapshots,
		   (unsigned long long)(omo_next_seq - 1));
	for (i = 0; i < omo_count; i++) {
		unsigned int idx = (omo_count < nsnapshots)
			? i : (omo_head + i) % nsnapshots;

		seq_printf(s, "== snapshot %llu ts_ns=%llu ==\n",
			   (unsigned long long)omo_seq[idx],
			   (unsigned long long)omo_ts[idx]);
		omo_show_snapshot(s, idx);
	}
	mutex_unlock(&omo_lock);
	return 0;
}

static int omo_log_open(struct inode *inode, struct file *file)
{
	return single_open(file, omo_log_show, NULL);
}

static const struct file_operations omo_log_fops = {
	.owner = THIS_MODULE,
	.open = omo_log_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int omo_regions_show(struct seq_file *s, void *v)
{
	unsigned int r, w, k = 0;

	seq_printf(s, "ringwatch: load-time dump of %u regions, %u words\n",
		   omo_nreg, omo_total_words);
	for (r = 0; r < omo_nreg; r++) {
		seq_printf(s, "== region %u BAR0+0x%lx len=0x%x ==\n",
			   r, omo_reg[r].start, omo_reg[r].len);
		for (w = 0; w < omo_reg[r].words; w++)
			seq_printf(s, "0x%08lx 0x%08x %llu\n",
				   omo_reg[r].start + (unsigned long)w * 4,
				   omo_dump_vals[k + w],
				   (unsigned long long)omo_dump_ts);
		k += omo_reg[r].words;
	}
	return 0;
}

static int omo_regions_open(struct inode *inode, struct file *file)
{
	return single_open(file, omo_regions_show, NULL);
}

static const struct file_operations omo_regions_fops = {
	.owner = THIS_MODULE,
	.open = omo_regions_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static void omo_clear_ring(void)
{
	mutex_lock(&omo_lock);
	omo_head = 0;
	omo_count = 0;
	omo_next_seq = 1;
	mutex_unlock(&omo_lock);
}

static ssize_t omo_sample_write(struct file *file, const char __user *ubuf,
				size_t count, loff_t *ppos)
{
	char buf[16];

	if (count == 0)
		return 0;
	if (count < sizeof(buf) && copy_from_user(buf, ubuf, count) == 0) {
		buf[count] = '\0';
		if (!strncmp(buf, "clear", 5)) {
			omo_clear_ring();
			return count;
		}
	}
	omo_take_snapshot();
	return count;
}

static const struct file_operations omo_sample_fops = {
	.owner = THIS_MODULE,
	.write = omo_sample_write,
	.llseek = no_llseek,
};

static int __init omo_ringwatch_init(void)
{
	u32 lo = 0, hi = 0;
	u64 base;
	unsigned int r;
	int ret;

	if (!nsnapshots)
		nsnapshots = 1;

	ret = omo_parse_regions();
	if (ret)
		return ret;

	omo_dev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-ringwatch: endpoint 0000:00:00.0 not found\n");
		return -ENODEV;
	}

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("omo-ringwatch: no usable BAR0 (lo=0x%08x hi=0x%08x)\n",
		       lo, hi);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENODEV;
	}

	/* No pci_request_region: the vendor driver owns the region. */
	for (r = 0; r < omo_nreg; r++) {
		omo_reg[r].win = ioremap(base + omo_reg[r].start,
					 omo_reg[r].len);
		if (!omo_reg[r].win) {
			pr_err("omo-ringwatch: ioremap of +0x%lx (%u) failed\n",
			       omo_reg[r].start, omo_reg[r].len);
			ret = -ENOMEM;
			goto err_unmap;
		}
	}

	omo_ts = kcalloc(nsnapshots, sizeof(*omo_ts), GFP_KERNEL);
	omo_seq = kcalloc(nsnapshots, sizeof(*omo_seq), GFP_KERNEL);
	omo_vals = kvcalloc((size_t)nsnapshots * omo_total_words,
			    sizeof(*omo_vals), GFP_KERNEL);
	omo_dump_vals = kcalloc(omo_total_words, sizeof(*omo_dump_vals),
				GFP_KERNEL);
	if (!omo_ts || !omo_seq || !omo_vals || !omo_dump_vals) {
		ret = -ENOMEM;
		goto err_free;
	}

	omo_read_all(omo_dump_vals);
	omo_dump_ts = ktime_get_ns();

	omo_dir = debugfs_create_dir("ringwatch", NULL);
	if (IS_ERR_OR_NULL(omo_dir)) {
		pr_warn("omo-ringwatch: debugfs dir unavailable (%ld)\n",
			PTR_ERR_OR_ZERO(omo_dir));
		omo_dir = NULL;
		ret = -ENODEV;
		goto err_free;
	}
	omo_log_file = debugfs_create_file("log", 0444, omo_dir, NULL,
					   &omo_log_fops);
	omo_regions_file = debugfs_create_file("regions", 0444, omo_dir, NULL,
					       &omo_regions_fops);
	omo_sample_file = debugfs_create_file("sample", 0200, omo_dir, NULL,
					      &omo_sample_fops);
	if (IS_ERR_OR_NULL(omo_log_file) || IS_ERR_OR_NULL(omo_regions_file) ||
	    IS_ERR_OR_NULL(omo_sample_file)) {
		pr_warn("omo-ringwatch: debugfs files unavailable\n");
		ret = -ENODEV;
		goto err_debugfs;
	}

	pr_info("omo-ringwatch: BAR0 base=0x%llx, %u regions, %u words/snapshot, ring=%u, read-only, no claim\n",
		(unsigned long long)base, omo_nreg, omo_total_words, nsnapshots);
	{
		unsigned int k = 0;

		for (r = 0; r < omo_nreg; r++) {
			pr_info("omo-ringwatch: region %u BAR0+0x%lx len=0x%x first=%08x\n",
				r, omo_reg[r].start, omo_reg[r].len,
				omo_dump_vals[k]);
			k += omo_reg[r].words;
		}
	}
	pr_info("omo-ringwatch: load-time dump ts_ns=%llu ready\n",
		(unsigned long long)omo_dump_ts);

	return 0;

err_debugfs:
	debugfs_remove_recursive(omo_dir);
	omo_dir = NULL;
err_free:
	kfree(omo_dump_vals);
	omo_dump_vals = NULL;
	kvfree(omo_vals);
	omo_vals = NULL;
	kfree(omo_seq);
	omo_seq = NULL;
	kfree(omo_ts);
	omo_ts = NULL;
err_unmap:
	while (r--)
		iounmap(omo_reg[r].win);
	pci_dev_put(omo_dev);
	omo_dev = NULL;
	return ret;
}

static void __exit omo_ringwatch_exit(void)
{
	unsigned int r;

	if (omo_dir) {
		debugfs_remove_recursive(omo_dir);
		omo_dir = NULL;
		omo_log_file = NULL;
		omo_regions_file = NULL;
		omo_sample_file = NULL;
	}
	kfree(omo_dump_vals);
	omo_dump_vals = NULL;
	kvfree(omo_vals);
	omo_vals = NULL;
	kfree(omo_seq);
	omo_seq = NULL;
	kfree(omo_ts);
	omo_ts = NULL;
	for (r = 0; r < omo_nreg; r++) {
		if (omo_reg[r].win) {
			iounmap(omo_reg[r].win);
			omo_reg[r].win = NULL;
		}
	}
	if (omo_dev) {
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("omo-ringwatch: debugfs removed, windows unmapped, device released\n");
}

module_init(omo_ringwatch_init);
module_exit(omo_ringwatch_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only real-time watcher for the WR3000 V2.0 pointer rings");
