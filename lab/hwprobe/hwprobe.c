// SPDX-License-Identifier: GPL-2.0
/*
 * hwprobe: read-only peek at the WR3000 V2.0 Wi-Fi chip register window.
 *
 * The two Wi-Fi PCIe endpoints (59e7:0005) own BAR0. Their register I/O
 * window used by the vendor driver lives at BAR0 + 0x3b8000. This module
 * only *reads* three u32 words there to prove that a module we build can
 * touch the device's MMIO without disturbing the vendor driver.
 *
 * It deliberately does NOT claim the device (no pci_enable_device, no
 * pci_request_region, no config-space writes, no reset). The vendor driver
 * keeps ownership; we only borrow the mapping for a read, then drop it.
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005
#define OMO_WIN_OFF	0x3b8000
#define OMO_WIN_LEN	16

static struct pci_dev *omo_dev;
static void __iomem *omo_win;

static int __init omo_hwprobe_init(void)
{
	resource_size_t start, len;
	u32 w0, w1, w2;

	omo_dev = pci_get_device(OMO_VENDOR_ID, OMO_DEVICE_ID, NULL);
	if (!omo_dev) {
		pr_err("omo-hwprobe: device %04x:%04x not found\n",
		       OMO_VENDOR_ID, OMO_DEVICE_ID);
		return -ENODEV;
	}

	start = pci_resource_start(omo_dev, 0);
	len = pci_resource_len(omo_dev, 0);
	pr_info("omo-hwprobe: found %04x:%04x BAR0 start=0x%llx len=0x%llx\n",
		OMO_VENDOR_ID, OMO_DEVICE_ID,
		(unsigned long long)start, (unsigned long long)len);

	/* No pci_request_region: the vendor driver owns the region. */
	omo_win = ioremap(start + OMO_WIN_OFF, OMO_WIN_LEN);
	if (!omo_win) {
		pr_err("omo-hwprobe: ioremap of 0x%llx (16 bytes) failed\n",
		       (unsigned long long)(start + OMO_WIN_OFF));
		pci_dev_put(omo_dev);
		omo_dev = NULL;
		return -ENOMEM;
	}

	w0 = ioread32(omo_win + 0x00);
	w1 = ioread32(omo_win + 0x04);
	w2 = ioread32(omo_win + 0x08);
	pr_info("omo-hwprobe: BAR0+0x%x = 0x%x 0x%x 0x%x\n",
		OMO_WIN_OFF, w0, w1, w2);

	return 0;
}

static void __exit omo_hwprobe_exit(void)
{
	if (omo_win) {
		iounmap(omo_win);
		omo_win = NULL;
	}
	if (omo_dev) {
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("omo-hwprobe: unmapped BAR window, device released\n");
}

module_init(omo_hwprobe_init);
module_exit(omo_hwprobe_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("WR3000 V2.0 read-only Wi-Fi register window probe");
