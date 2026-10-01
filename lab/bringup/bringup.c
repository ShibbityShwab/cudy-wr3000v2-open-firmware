// SPDX-License-Identifier: GPL-2.0
/*
 * bringup: the first real bring-up of one WR3000 V2.0 Wi-Fi endpoint by our
 * own code - the memory-download half only.
 *
 * Assumes the vendor wireless stack (hi5622v100_wifi, then hi5622v100_plat /
 * rox_pci0) has already been unloaded from userspace, so the 59e7:0005
 * endpoint is unbound and its BAR regions are free.  The module then:
 *
 *   1. claims the endpoint the documented way:
 *      pci_enable_device() + pci_request_mem_regions();
 *   2. maps BAR0 (base read from PCI *config space*: our struct pci_dev is the
 *      vanilla layout and pci_resource_start()/len() return 0 across the ABI
 *      boundary, see docs/phase11/hwprobe.md) and logs:
 *        - the ARM ROM exception-vector page at BAR0+0x0,
 *        - the firmware-image header at BAR0+0x40000;
 *   3. reads a firmware image from `fwpath` (default
 *      /lib/firmware/hi_wifi/FIRMWARE.bin), writes it to BAR0+0x40000 - the
 *      address the vendor loads it to - reads the same region back, and
 *      reports whether the bytes and CRC32 match.
 *
 * Rollback is a reboot: the vendor stack re-probes on boot and reloads the
 * vendor firmware.  The module never resets the device and never starts the
 * chip's CPU.  It covers the download only: no DMA rings, no message
 * protocol, no interrupt setup.
 *
 * Module parameters:
 *   domain=N   PCI domain of the 59e7:0005 endpoint (default 0 = 2.4 GHz)
 *   fwpath=P   firmware image to write (default /lib/firmware/hi_wifi/FIRMWARE.bin)
 */

#include <linux/crc32.h>
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/string.h>
#include <linux/vmalloc.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

#define OMO_ROM_OFF	0x00000UL	/* ARM exception-vector page */
#define OMO_ROM_LEN	0x1000UL
#define OMO_ROM_WORDS	8
#define OMO_FW_OFF	0x40000UL	/* the vendor loads the image here */
#define OMO_FW_HDR_WORDS 8
#define OMO_FW_MAX	(2UL * 1024 * 1024)	/* hard bound on the write */
#define OMO_WRITE_CHUNK	(256UL * 1024)		/* progress granularity */

static unsigned int omo_domain;
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static char *omo_fwpath = "/lib/firmware/hi_wifi/FIRMWARE.bin";
module_param_named(fwpath, omo_fwpath, charp, 0444);
MODULE_PARM_DESC(fwpath, "firmware image written into BAR0+0x40000");

static struct pci_dev *omo_dev;
static void __iomem *omo_rom_win;
static void __iomem *omo_fw_win;
static u8 *omo_fwbuf;			/* bytes read from the file */
static u8 *omo_rbbuf;			/* bytes read back from the chip */
static size_t omo_fwlen;
static struct dentry *omo_dbg;

/* ---- firmware file ----------------------------------------------------- */

/*
 * Read the whole file into a vmalloc'd buffer.  We deliberately do NOT use
 * i_size_read()/file_inode(): those are inlines over struct file/inode and we
 * keep every struct access inside the vendor kernel (the pci_dev lesson).
 * Instead read until kernel_read() reports EOF, bounded by OMO_FW_MAX.
 */
static int omo_read_file(const char *path, u8 **out, size_t *outlen)
{
	struct file *f;
	loff_t pos = 0;
	u8 *buf;
	size_t total = 0;
	ssize_t n;

	f = filp_open(path, O_RDONLY, 0);
	if (IS_ERR(f)) {
		pr_err("omo-bringup: open %s failed: %ld\n", path, PTR_ERR(f));
		return PTR_ERR(f);
	}

	buf = vmalloc(OMO_FW_MAX);
	if (!buf) {
		filp_close(f, NULL);
		return -ENOMEM;
	}

	for (;;) {
		n = kernel_read(f, buf + total, OMO_FW_MAX - total, &pos);
		if (n < 0) {
			pr_err("omo-bringup: read %s failed: %zd\n", path, n);
			filp_close(f, NULL);
			vfree(buf);
			return n;
		}
		if (n == 0)
			break;
		total += n;
		if (total >= OMO_FW_MAX)
			break;
	}
	filp_close(f, NULL);

	if (!total) {
		pr_err("omo-bringup: %s is empty\n", path);
		vfree(buf);
		return -EINVAL;
	}

	*out = buf;
	*outlen = total;
	return 0;
}

/* ---- debugfs: the exact bytes read back from the chip ------------------- */

static ssize_t omo_readback_read(struct file *file, char __user *ubuf,
				 size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(ubuf, count, ppos, omo_rbbuf, omo_fwlen);
}

static const struct file_operations omo_readback_fops = {
	.owner = THIS_MODULE,
	.read = omo_readback_read,
	.llseek = default_llseek,
};

/* ---- init / exit ------------------------------------------------------- */

static void omo_log_words(const char *tag, unsigned long off,
			  const void __iomem *win, unsigned int n)
{
	unsigned int i;

	pr_info("omo-bringup: %s BAR0+0x%lx:", tag, off);
	for (i = 0; i < n; i++)
		pr_cont(" %08x", ioread32(win + 4UL * i));
	pr_cont("\n");
}

static int __init omo_bringup_init(void)
{
	u32 lo = 0, hi = 0;
	u64 base;
	size_t off;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-bringup: endpoint %04x:00:00.0 not found\n",
		       omo_domain);
		return -ENODEV;
	}

	ret = pci_enable_device(omo_dev);
	if (ret) {
		pr_err("omo-bringup: pci_enable_device rc=%d\n", ret);
		goto err_put;
	}
	pr_info("omo-bringup: ep%u pci_enable_device rc=0\n", omo_domain);

	ret = pci_request_mem_regions(omo_dev);
	if (ret) {
		pr_err("omo-bringup: pci_request_mem_regions rc=%d (region busy - vendor stack still loaded?) - refusing to write\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-bringup: ep%u pci_request_mem_regions rc=0 (MEM BARs claimed)\n",
		omo_domain);

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("omo-bringup: no usable BAR0 (lo=0x%08x hi=0x%08x)\n",
		       lo, hi);
		ret = -ENODEV;
		goto err_release;
	}
	pr_info("omo-bringup: ep%u BAR0 base=0x%llx (config-space read; pci_resource_start() is ABI-unsafe here)\n",
		omo_domain, (unsigned long long)base);

	/* 1. ROM vector page. */
	omo_rom_win = ioremap(base + OMO_ROM_OFF, OMO_ROM_LEN);
	if (!omo_rom_win) {
		pr_err("omo-bringup: ioremap ROM page failed\n");
		ret = -ENOMEM;
		goto err_release;
	}
	omo_log_words("ROM vector page", OMO_ROM_OFF, omo_rom_win, OMO_ROM_WORDS);

	/* 2. Firmware image from the file, and its window. */
	ret = omo_read_file(omo_fwpath, &omo_fwbuf, &omo_fwlen);
	if (ret)
		goto err_unmap_rom;
	pr_info("omo-bringup: firmware file %s size=%zu bytes\n",
		omo_fwpath, omo_fwlen);

	omo_fw_win = ioremap(base + OMO_FW_OFF, omo_fwlen);
	if (!omo_fw_win) {
		pr_err("omo-bringup: ioremap firmware window failed\n");
		ret = -ENOMEM;
		goto err_free_fw;
	}
	omo_log_words("firmware header BEFORE", OMO_FW_OFF, omo_fw_win,
		      OMO_FW_HDR_WORDS);

	omo_rbbuf = vmalloc(omo_fwlen);
	if (!omo_rbbuf) {
		ret = -ENOMEM;
		goto err_unmap_fw;
	}

	/* 3. Write the image, then read the same region back. */
	for (off = 0; off < omo_fwlen; off += OMO_WRITE_CHUNK) {
		size_t n = min_t(size_t, OMO_WRITE_CHUNK, omo_fwlen - off);

		memcpy_toio(omo_fw_win + off, omo_fwbuf + off, n);
		pr_info("omo-bringup: wrote %zu/%zu bytes @ BAR0+0x%lx\n",
			off + n, omo_fwlen, OMO_FW_OFF + (unsigned long)off);
	}
	memcpy_fromio(omo_rbbuf, omo_fw_win, omo_fwlen);

	{
		u32 fw_crc = crc32_le(~0u, omo_fwbuf, omo_fwlen);
		u32 rb_crc = crc32_le(~0u, omo_rbbuf, omo_fwlen);
		size_t first = omo_fwlen, diffs = 0, i;

		for (i = 0; i < omo_fwlen; i++) {
			if (omo_fwbuf[i] != omo_rbbuf[i]) {
				if (first == omo_fwlen)
					first = i;
				diffs++;
			}
		}

		pr_info("omo-bringup: write verdict: file=%zu bytes wrote_crc32=%08x readback_crc32=%08x diffs=%zu first_diff=0x%zx match=%s\n",
			omo_fwlen, fw_crc, rb_crc, diffs,
			first == omo_fwlen ? 0 : first,
			diffs == 0 ? "YES" : "NO");
		if (diffs)
			pr_err("omo-bringup: firmware write did NOT verify\n");
		else
			pr_info("omo-bringup: firmware image loaded and verified at BAR0+0x%lx\n",
				OMO_FW_OFF);
	}

	omo_dbg = debugfs_create_dir("bringup", NULL);
	if (IS_ERR_OR_NULL(omo_dbg)) {
		pr_warn("omo-bringup: debugfs unavailable (%ld)\n",
			PTR_ERR_OR_ZERO(omo_dbg));
		omo_dbg = NULL;
	} else if (IS_ERR_OR_NULL(debugfs_create_file("readback", 0444,
						      omo_dbg, NULL,
						      &omo_readback_fops))) {
		pr_warn("omo-bringup: debugfs readback unavailable\n");
		debugfs_remove_recursive(omo_dbg);
		omo_dbg = NULL;
	} else {
		pr_info("omo-bringup: /sys/kernel/debug/bringup/readback = the %zu bytes read back from the chip\n",
			omo_fwlen);
	}

	pr_info("omo-bringup: ep%u claimed, ROM+FW logged, image written (no reset, no CPU start)\n",
		omo_domain);
	return 0;

err_unmap_fw:
	iounmap(omo_fw_win);
	omo_fw_win = NULL;
err_free_fw:
	vfree(omo_fwbuf);
	omo_fwbuf = NULL;
err_unmap_rom:
	iounmap(omo_rom_win);
	omo_rom_win = NULL;
err_release:
	pci_release_mem_regions(omo_dev);
err_disable:
	pci_disable_device(omo_dev);
err_put:
	pci_dev_put(omo_dev);
	omo_dev = NULL;
	return ret;
}

static void __exit omo_bringup_exit(void)
{
	if (omo_dbg) {
		debugfs_remove_recursive(omo_dbg);
		omo_dbg = NULL;
	}
	if (omo_fw_win) {
		iounmap(omo_fw_win);
		omo_fw_win = NULL;
	}
	if (omo_rom_win) {
		iounmap(omo_rom_win);
		omo_rom_win = NULL;
	}
	vfree(omo_fwbuf);
	omo_fwbuf = NULL;
	vfree(omo_rbbuf);
	omo_rbbuf = NULL;
	if (omo_dev) {
		pci_release_mem_regions(omo_dev);
		pci_disable_device(omo_dev);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("omo-bringup: unloaded, windows unmapped, BARs released, device put\n");
}

module_init(omo_bringup_init);
module_exit(omo_bringup_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Claim one WR3000 V2.0 Wi-Fi endpoint and load its firmware image into BAR0");
