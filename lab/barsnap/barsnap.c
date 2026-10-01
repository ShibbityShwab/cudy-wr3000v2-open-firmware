// SPDX-License-Identifier: GPL-2.0
/*
 * barsnap: read-only snapshot of the Wi-Fi chip's memory-mapped BAR space.
 *
 * For each of the two 59e7:0005 endpoints (0000:00:00.0 and 0001:00:00.0)
 * this module:
 *
 *   1. reads the BAR0 and BAR4 base addresses from PCI config space with
 *      pci_read_config_dword() - read-only accessors that live inside the
 *      vendor kernel and therefore use its own struct pci_dev ABI;
 *   2. ioremap()s the first 1 MiB of each BAR WITHOUT claiming anything
 *      (no pci_request_region, no pci_enable_device, no reset) - the vendor
 *      driver keeps ownership throughout;
 *   3. copies the raw bytes out with memcpy_fromio() and writes them to
 *      /tmp/barsnap_ep<ep>_bar<bar>.bin via the kernel file API
 *      (filp_open + kernel_write), exactly 1 MiB per file;
 *   4. reports how many of the 256 4 KiB pages carry anything that is
 *      neither 0x00 nor 0xff (the relevance summary; 0xff is what an
 *      undecoded BAR read returns).
 *
 * Nothing is written to the BAR or to config space.  All windows are
 * iounmap()ed and all device references dropped on unload.
 */

#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

#define OMO_EPS		2		/* two endpoints, one per radio */
#define OMO_BARS	2		/* BAR0 and BAR4 */
#define OMO_SNAP_SZ	SZ_1M		/* snapshot the first 1 MiB of each BAR */
#define OMO_PAGE_SZ	SZ_4K
#define OMO_PAGES	(OMO_SNAP_SZ / OMO_PAGE_SZ)	/* 256 pages */

static const int omo_bar_lo_reg[OMO_BARS] = {
	PCI_BASE_ADDRESS_0,
	PCI_BASE_ADDRESS_4,
};
static const int omo_bar_hi_reg[OMO_BARS] = {
	PCI_BASE_ADDRESS_1,
	PCI_BASE_ADDRESS_5,
};
static const int omo_bar_num[OMO_BARS] = { 0, 4 };

static struct pci_dev *omo_dev[OMO_EPS];
static void __iomem *omo_map[OMO_EPS][OMO_BARS];

/*
 * Count the 4 KiB pages that hold at least one byte which is neither 0x00
 * (unwritten / reserved) nor 0xff (BAR read that decoded to nothing).
 */
static unsigned int omo_page_hits(const u8 *buf)
{
	unsigned int p, hits = 0;

	for (p = 0; p < OMO_PAGES; p++) {
		const u8 *page = buf + (size_t)p * OMO_PAGE_SZ;
		unsigned int i;

		for (i = 0; i < OMO_PAGE_SZ; i++) {
			if (page[i] != 0x00 && page[i] != 0xff) {
				hits++;
				break;
			}
		}
	}

	return hits;
}

/* First page that carries data, or -1 if the whole snapshot is 00/ff. */
static int omo_first_hit_page(const u8 *buf)
{
	unsigned int p;

	for (p = 0; p < OMO_PAGES; p++) {
		const u8 *page = buf + (size_t)p * OMO_PAGE_SZ;
		unsigned int i;

		for (i = 0; i < OMO_PAGE_SZ; i++) {
			if (page[i] != 0x00 && page[i] != 0xff)
				return (int)p;
		}
	}

	return -1;
}

static int omo_write_bin(int ep, int bar, const u8 *buf)
{
	char path[64];
	struct file *f;
	loff_t pos = 0;
	size_t done = 0;
	int ret = 0;

	snprintf(path, sizeof(path), "/tmp/barsnap_ep%d_bar%d.bin", ep, bar);

	f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (IS_ERR(f)) {
		ret = PTR_ERR(f);
		pr_err("omo-barsnap: open %s failed: %d\n", path, ret);
		return ret;
	}

	while (done < OMO_SNAP_SZ) {
		ssize_t w = kernel_write(f, buf + done, OMO_SNAP_SZ - done, &pos);

		if (w < 0) {
			ret = (int)w;
			pr_err("omo-barsnap: write %s failed: %d\n", path, ret);
			break;
		}
		if (w == 0) {
			ret = -EIO;
			pr_err("omo-barsnap: short write on %s\n", path);
			break;
		}
		done += (size_t)w;
	}

	filp_close(f, NULL);
	if (!ret)
		pr_info("omo-barsnap: wrote %s (%zu bytes)\n", path, done);

	return ret;
}

static int omo_snap_one(int ep, struct pci_dev *dev)
{
	int b;

	for (b = 0; b < OMO_BARS; b++) {
		u32 lo = 0, hi = 0;
		u64 base;
		u8 *buf;
		unsigned int hits;
		int first;

		pci_read_config_dword(dev, omo_bar_lo_reg[b], &lo);
		pci_read_config_dword(dev, omo_bar_hi_reg[b], &hi);

		if (lo & PCI_BASE_ADDRESS_SPACE_IO) {
			pr_info("omo-barsnap: ep%d BAR%d is I/O space (cfg=0x%08x), skipped\n",
				ep, omo_bar_num[b], lo);
			continue;
		}

		base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) |
		       ((u64)hi << 32);
		if (!base) {
			pr_info("omo-barsnap: ep%d BAR%d unassigned (cfg=0x%08x), skipped\n",
				ep, omo_bar_num[b], lo);
			continue;
		}

		/* No pci_request_region(): the vendor driver owns the region. */
		omo_map[ep][b] = ioremap(base, OMO_SNAP_SZ);
		if (!omo_map[ep][b]) {
			pr_err("omo-barsnap: ioremap ep%d BAR%d base=0x%llx failed\n",
			       ep, omo_bar_num[b], (unsigned long long)base);
			return -ENOMEM;
		}

		buf = vzalloc(OMO_SNAP_SZ);
		if (!buf) {
			pr_err("omo-barsnap: vzalloc for ep%d BAR%d failed\n",
			       ep, omo_bar_num[b]);
			return -ENOMEM;
		}

		memcpy_fromio(buf, omo_map[ep][b], OMO_SNAP_SZ);
		hits = omo_page_hits(buf);
		first = omo_first_hit_page(buf);

		pr_info("omo-barsnap: ep%d BAR%d base=0x%llx cfg=0x%08x: %u/%u 4K pages carry data (non-zero, non-0xff), first page %d\n",
			ep, omo_bar_num[b], (unsigned long long)base, lo,
			hits, OMO_PAGES, first);

		omo_write_bin(ep, omo_bar_num[b], buf);
		vfree(buf);
	}

	return 0;
}

static int __init omo_barsnap_init(void)
{
	int ep;

	/*
	 * Fetch the two endpoints by exact BDF.  pci_get_domain_bus_and_slot()
	 * is implemented inside the vendor kernel, so it uses the vendor's own
	 * struct pci_dev layout - we never dereference the struct ourselves.
	 */
	omo_dev[0] = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	omo_dev[1] = pci_get_domain_bus_and_slot(1, 0, PCI_DEVFN(0, 0));

	for (ep = 0; ep < OMO_EPS; ep++) {
		if (!omo_dev[ep]) {
			pr_err("omo-barsnap: endpoint %04d:00:00.0 not found\n", ep);
			continue;
		}

		pr_info("omo-barsnap: probing %04d:00:00.0\n", ep);
		if (omo_snap_one(ep, omo_dev[ep]))
			return -ENOMEM;
	}

	return 0;
}

static void __exit omo_barsnap_exit(void)
{
	int ep, b;

	for (ep = 0; ep < OMO_EPS; ep++) {
		for (b = 0; b < OMO_BARS; b++) {
			if (omo_map[ep][b]) {
				iounmap(omo_map[ep][b]);
				omo_map[ep][b] = NULL;
			}
		}
	}

	for (ep = 0; ep < OMO_EPS; ep++) {
		if (omo_dev[ep]) {
			pci_dev_put(omo_dev[ep]);
			omo_dev[ep] = NULL;
		}
	}

	pr_info("omo-barsnap: unmapped all BAR windows, devices released\n");
}

module_init(omo_barsnap_init);
module_exit(omo_barsnap_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only 1 MiB snapshot of the WR3000 V2.0 Wi-Fi BAR space");
