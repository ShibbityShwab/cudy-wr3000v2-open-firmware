// SPDX-License-Identifier: GPL-2.0
/*
 * barmap: read-only, byte-for-byte map of the Wi-Fi chip's full BAR0 window
 * (and a cheap BAR4 probe) on the WR3000 V2.0.
 *
 * barsnap.c read only the first 1 MiB of each BAR; this module reads the
 * whole thing.  For endpoint 0000:00:00.0 it:
 *
 *   1. reads the BAR0 base from PCI config space with pci_read_config_dword()
 *      (read-only; a vendor-kernel accessor, so no struct pci_dev ABI risk);
 *   2. ioremap()s BAR0 in 1 MiB chunks across the full 16 MiB upper bound,
 *      WITHOUT claiming anything (no pci_request_region, no pci_enable_device,
 *      no reset) - the vendor driver keeps ownership throughout;
 *   3. classifies every 4 KiB page (all-0x00, all-0xff, or data), computes a
 *      128-bit hash of data pages and records the first 16 bytes, then writes
 *      one compact line per page to /tmp/barmap_ep<ep>.txt;
 *   4. appends each raw 1 MiB chunk to /tmp/barmap_ep0_bar0.bin (16 MiB);
 *   5. reads the known register anchor at BAR0+0x3b8000 and prints the three
 *      words (must be 0x101 0x110 0x2) as an on-device cross-check.
 *
 * A second endpoint (0001:00:00.0) gets the same page map but no raw dump.
 * BAR4 is probed once per endpoint only to confirm it is undecoded (0xff).
 *
 * Nothing is written to the BAR or to config space.  All windows are
 * iounmap()ed and all device references dropped on unload.
 *
 * Note on "stop early on an all-0xff chunk": that is the right rule for BAR4
 * (a wholly undecoded window) and it is applied there.  It is deliberately
 * NOT applied to BAR0: the raw dump is required to be the full 16 MiB and the
 * register anchor at 0x3b8000 sits beyond a large undecoded gap, so an
 * all-0xff chunk in between must not truncate the scan.
 */

#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#define OMO_BAR0_MAX	(16UL * 1024 * 1024)	/* sysfs: len 0x1000000 */
#define OMO_BAR4_MAX	(8UL * 1024 * 1024)	/* sysfs: len 0x800000 */
#define OMO_CHUNK	(1024 * 1024)
#define OMO_PAGE_SZ	(4 * 1024)
#define OMO_PAGES_PER_CHUNK	(OMO_CHUNK / OMO_PAGE_SZ)	/* 256 */
#define OMO_TXT_CAP	(512 * 1024)
#define OMO_ANCHOR_OFF	0x3b8000UL

#define OMO_FNV_PRIME	0x01000193u

enum {
	OMO_KIND_ZERO,
	OMO_KIND_FF,
	OMO_KIND_DATA,
};

static struct pci_dev *omo_dev[2];
static void __iomem *omo_anchor[2];

/* all-0x00, all-0xff, or data? */
static int omo_page_kind(const u8 *p)
{
	bool allz = true, allf = true;
	unsigned int i;

	for (i = 0; i < OMO_PAGE_SZ; i++) {
		if (p[i] != 0x00)
			allz = false;
		if (p[i] != 0xff)
			allf = false;
		if (!allz && !allf)
			return OMO_KIND_DATA;
	}

	return allz ? OMO_KIND_ZERO : OMO_KIND_FF;
}

/* 128-bit page fingerprint: four independent FNV-1a lanes. */
static void omo_hash_page(const u8 *p, u32 out[4])
{
	static const u32 seed[4] = {
		0x811c9dc5u, 0x7f4a7c15u, 0x9e3779b9u, 0xc2b2ae35u,
	};
	int k;

	for (k = 0; k < 4; k++) {
		u32 h = seed[k];
		unsigned int i;

		for (i = 0; i < OMO_PAGE_SZ; i++) {
			h ^= p[i];
			h *= OMO_FNV_PRIME;
			h ^= h >> 13;
		}
		out[k] = h;
	}
}

/* Append one page-map line and report the page's classification. */
static size_t omo_txt_page(char *txt, size_t cap, size_t len, unsigned long off,
			   const u8 *page, int *kind_out)
{
	int kind = omo_page_kind(page);
	char hash[33], first[33];

	*kind_out = kind;
	if (kind == OMO_KIND_DATA) {
		u32 h[4];
		int i;

		omo_hash_page(page, h);
		snprintf(hash, sizeof(hash), "%08x%08x%08x%08x",
			 h[0], h[1], h[2], h[3]);
		for (i = 0; i < 16; i++)
			snprintf(first + 2 * i, 3, "%02x", page[i]);
	} else {
		strcpy(hash, "-");
		strcpy(first, "-");
	}

	return len + scnprintf(txt + len, cap - len, "0x%08lx %s %s %s\n", off,
			       kind == OMO_KIND_ZERO ? "zero" :
			       kind == OMO_KIND_FF ? "ff" : "data",
			       hash, first);
}

/* write() a whole buffer at a tracked position. */
static int omo_kwrite(struct file *f, const void *buf, size_t len, loff_t *pos)
{
	size_t done = 0;

	while (done < len) {
		ssize_t w = kernel_write(f, (const char *)buf + done,
					 len - done, pos);

		if (w < 0)
			return (int)w;
		if (w == 0)
			return -EIO;
		done += (size_t)w;
	}

	return 0;
}

/*
 * Map one BAR's full window in 1 MiB chunks.  When stop_on_ff is set the scan
 * ends at the first chunk that reads entirely 0xff (nothing decoded there and
 * nothing further to see).  BAR0 passes 0 because the raw dump must be the
 * full 16 MiB and the register anchor must be reached.
 */
static int omo_scan_bar(int ep, int barnum, u32 lo_reg, u32 hi_reg,
			unsigned long maxlen, int stop_on_ff, int dump_raw)
{
	u32 lo = 0, hi = 0;
	u64 base;
	unsigned long off;
	u8 *chunk = NULL, *txt = NULL;
	size_t txtlen = 0;
	struct file *binf = NULL;
	loff_t binpos = 0;
	char path[64];
	unsigned long total_data = 0, total_zero = 0, total_ff = 0;
	int ret = 0;

	pci_read_config_dword(omo_dev[ep], lo_reg, &lo);
	pci_read_config_dword(omo_dev[ep], hi_reg, &hi);

	if (lo & PCI_BASE_ADDRESS_SPACE_IO) {
		pr_info("omo-barmap: ep%d BAR%d is I/O space (cfg=0x%08x), skipped\n",
			ep, barnum, lo);
		return 0;
	}

	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base) {
		pr_info("omo-barmap: ep%d BAR%d unassigned (cfg=0x%08x), skipped\n",
			ep, barnum, lo);
		return 0;
	}

	chunk = vzalloc(OMO_CHUNK);
	txt = vzalloc(OMO_TXT_CAP);
	if (!chunk || !txt) {
		pr_err("omo-barmap: allocation for ep%d BAR%d failed\n", ep, barnum);
		ret = -ENOMEM;
		goto out;
	}

	if (dump_raw) {
		snprintf(path, sizeof(path), "/tmp/barmap_ep%d_bar%d.bin", ep, barnum);
		binf = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		if (IS_ERR(binf)) {
			ret = PTR_ERR(binf);
			binf = NULL;
			pr_err("omo-barmap: open %s failed: %d\n", path, ret);
			goto out;
		}
		pr_info("omo-barmap: ep%d BAR%d base=0x%llx len=0x%lx -> raw dump %s\n",
			ep, barnum, (unsigned long long)base, maxlen, path);
	}

	txtlen += scnprintf(txt + txtlen, OMO_TXT_CAP - txtlen,
			    "# barmap ep=%d bar=%d base=0x%llx len=0x%lx\n"
			    "# offset kind hash first16\n",
			    ep, barnum, (unsigned long long)base, maxlen);

	for (off = 0; off < maxlen; off += OMO_CHUNK) {
		unsigned int p, d = 0, z = 0, ff = 0;
		void __iomem *m = ioremap(base + off, OMO_CHUNK);
		int all_ff;

		if (!m) {
			pr_err("omo-barmap: ioremap ep%d BAR%d +0x%lx failed\n",
			       ep, barnum, off);
			ret = -ENOMEM;
			break;
		}

		memcpy_fromio(chunk, m, OMO_CHUNK);
		iounmap(m);

		if (binf) {
			ret = omo_kwrite(binf, chunk, OMO_CHUNK, &binpos);
			if (ret) {
				pr_err("omo-barmap: raw write ep%d BAR%d +0x%lx failed: %d\n",
				       ep, barnum, off, ret);
				break;
			}
		}

		if (txtlen + OMO_PAGES_PER_CHUNK * 80 + 96 > OMO_TXT_CAP) {
			pr_err("omo-barmap: page map buffer overflow\n");
			ret = -ENOSPC;
			break;
		}

		for (p = 0; p < OMO_PAGES_PER_CHUNK; p++) {
			const u8 *page = chunk + (size_t)p * OMO_PAGE_SZ;
			int kind;

			txtlen = omo_txt_page(txt, OMO_TXT_CAP, txtlen,
					      off + (unsigned long)p * OMO_PAGE_SZ,
					      page, &kind);
			if (kind == OMO_KIND_DATA)
				d++;
			else if (kind == OMO_KIND_ZERO)
				z++;
			else
				ff++;
		}

		total_data += d;
		total_zero += z;
		total_ff += ff;
		all_ff = (ff == OMO_PAGES_PER_CHUNK);

		pr_info("omo-barmap: ep%d BAR%d %lu/%lu MiB @0x%08lx: data=%u zero=%u ff=%u%s\n",
			ep, barnum, (off / OMO_CHUNK) + 1, maxlen / OMO_CHUNK, off,
			d, z, ff, all_ff ? " (all-0xff)" : "");

		if (all_ff && stop_on_ff) {
			pr_info("omo-barmap: ep%d BAR%d stopped early: chunk @0x%lx reads all-0xff\n",
				ep, barnum, off);
			break;
		}
	}

	/* flush the page map */
	snprintf(path, sizeof(path), "/tmp/barmap_ep%d.txt", ep);
	{
		struct file *f = filp_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
		loff_t pos = 0;

		if (IS_ERR(f)) {
			ret = PTR_ERR(f);
			pr_err("omo-barmap: open %s failed: %d\n", path, ret);
		} else {
			ret = omo_kwrite(f, txt, txtlen, &pos);
			filp_close(f, NULL);
			if (ret)
				pr_err("omo-barmap: write %s failed: %d\n", path, ret);
			else
				pr_info("omo-barmap: ep%d BAR%d wrote %s (%zu bytes)\n",
					ep, barnum, path, txtlen);
		}
	}

	if (binf) {
		filp_close(binf, NULL);
		if (!ret)
			pr_info("omo-barmap: ep%d BAR%d raw dump complete (%llu bytes)\n",
				ep, barnum, (unsigned long long)binpos);
	}

	pr_info("omo-barmap: ep%d BAR%d totals: data=%lu zero=%lu ff=%lu pages\n",
		ep, barnum, total_data, total_zero, total_ff);

out:
	if (binf)
		filp_close(binf, NULL);
	vfree(chunk);
	vfree(txt);
	return ret;
}

/* Cross-check: the known register anchor must read 0x101 0x110 0x2. */
static void omo_anchor_check(int ep)
{
	u32 lo = 0, hi = 0;
	u64 base;

	pci_read_config_dword(omo_dev[ep], PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev[ep], PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base)
		return;

	omo_anchor[ep] = ioremap(base + OMO_ANCHOR_OFF, 16);
	if (!omo_anchor[ep]) {
		pr_err("omo-barmap: ep%d anchor ioremap failed\n", ep);
		return;
	}

	pr_info("omo-barmap: ep%d BAR0+0x%lx = 0x%x 0x%x 0x%x\n", ep,
		OMO_ANCHOR_OFF, ioread32(omo_anchor[ep]),
		ioread32(omo_anchor[ep] + 4), ioread32(omo_anchor[ep] + 8));
}

static int __init omo_barmap_init(void)
{
	/*
	 * Fetch the two endpoints by exact BDF.  pci_get_domain_bus_and_slot()
	 * lives inside the vendor kernel, so it uses the vendor's struct
	 * pci_dev layout; we only pass the pointer back into PCI accessors.
	 */
	omo_dev[0] = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	omo_dev[1] = pci_get_domain_bus_and_slot(1, 0, PCI_DEVFN(0, 0));

	if (!omo_dev[0]) {
		pr_err("omo-barmap: endpoint 0000:00:00.0 not found\n");
		return -ENODEV;
	}

	pr_info("omo-barmap: probing 0000:00:00.0\n");
	omo_scan_bar(0, 0, PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1,
		     OMO_BAR0_MAX, 0, 1);
	omo_anchor_check(0);
	omo_scan_bar(0, 4, PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_5,
		     OMO_BAR4_MAX, 1, 0);

	if (omo_dev[1]) {
		pr_info("omo-barmap: probing 0001:00:00.0\n");
		omo_scan_bar(1, 0, PCI_BASE_ADDRESS_0, PCI_BASE_ADDRESS_1,
			     OMO_BAR0_MAX, 0, 0);
		omo_anchor_check(1);
		omo_scan_bar(1, 4, PCI_BASE_ADDRESS_4, PCI_BASE_ADDRESS_5,
			     OMO_BAR4_MAX, 1, 0);
	} else {
		pr_err("omo-barmap: endpoint 0001:00:00.0 not found\n");
	}

	return 0;
}

static void __exit omo_barmap_exit(void)
{
	int ep;

	for (ep = 0; ep < 2; ep++) {
		if (omo_anchor[ep]) {
			iounmap(omo_anchor[ep]);
			omo_anchor[ep] = NULL;
		}
		if (omo_dev[ep]) {
			pci_dev_put(omo_dev[ep]);
			omo_dev[ep] = NULL;
		}
	}

	pr_info("omo-barmap: anchor windows unmapped, devices released\n");
}

module_init(omo_barmap_init);
module_exit(omo_barmap_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only full-BAR page map of the WR3000 V2.0 Wi-Fi chip");
