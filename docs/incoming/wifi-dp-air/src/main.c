// SPDX-License-Identifier: GPL-2.0
/*
 * main.c - PCI driver core for the Hi5622V100 clean-room Wi-Fi transport
 * driver (Linux 6.18, Cudy WR3000 v2.0 / HiSilicon luofu).
 *
 * ORIGINAL clean-room code.  This file owns the PCI lifecycle, the module
 * parameter surface and the sysfs attribute-group wiring; the protocol code
 * lives in transport.c / ete.c / d2h.c / hcc.c / service.c and the hardware
 * contract in hi5622_contract.h.
 *
 * probe() performs NO device register writes: only PCI enable, BAR mapping,
 * host-DRAM ring allocation and sysfs creation.  Every device write in this
 * driver is an explicit attended sysfs action behind a boot-stage gate, and
 * CA 0x400392F0 (out[5]) can never be written.
 */
#include <linux/device.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#include "hi5622_hcc.h"
#include "hi5622_cfg80211.h"
#include "hi5622_dp.h"
#include "h2d_send.h"

/* ---------------------------------------------------------------------
 * MODULE PARAMS
 *
 * fw_path            firmware image name relative to /lib/firmware (the
 *                    vendor layout is hi_wifi/FIRMWARE.bin).
 * wifi_block_path    local Wi-Fi PHY/radio custom block (0x9a8 B) relative to
 *                    /lib/firmware; captured from a stock boot at bench time
 *                    and NEVER committed (project publication rule).
 * plat_block_path    optional 0x60 B override of the built-in INI-derived
 *                    platform descriptor (empty = use the built-in one).
 * allow_unseeded_release  diagnostic-only: allow the release without the
 *                    Wi-Fi custom block (unproven configuration; default 0).
 * -------------------------------------------------------------------*/
char *hi5622_fw_path = HI5622_FW_PATH_DEFAULT;
module_param_named(fw_path, hi5622_fw_path, charp, 0444);
MODULE_PARM_DESC(fw_path,
		 "firmware image name relative to /lib/firmware (default hi_wifi/FIRMWARE.bin)");

char *hi5622_wifi_block_path = HI5622_WIFI_BLOCK_PATH_DEFAULT;
module_param_named(wifi_block_path, hi5622_wifi_block_path, charp, 0444);
MODULE_PARM_DESC(wifi_block_path,
		 "Wi-Fi PHY custom block (0x9a8 B) relative to /lib/firmware; local evidence, never committed");

char *hi5622_plat_block_path = "";
module_param_named(plat_block_path, hi5622_plat_block_path, charp, 0444);
MODULE_PARM_DESC(plat_block_path,
		 "optional 0x60 B platform-descriptor override relative to /lib/firmware (empty = built-in INI descriptor)");

unsigned int hi5622_allow_unseeded_release;
module_param_named(allow_unseeded_release, hi5622_allow_unseeded_release,
		   uint, 0644);
MODULE_PARM_DESC(allow_unseeded_release,
		 "diagnostic: allow release without the Wi-Fi custom block (default 0 = refuse)");

/* ---------------------------------------------------------------------
 * sysfs attribute-group wiring (each subsystem owns its group)
 * -------------------------------------------------------------------*/
struct hi5622_group_reg {
	const char		*name;
	int			(*add)(struct device *dev);
	void			(*del)(struct device *dev);
};

static const struct hi5622_group_reg hi5622_groups[] = {
	{ "transport", hi5622_transport_add_attrs, hi5622_transport_del_attrs },
	{ "service", hi5622_service_add_attrs, hi5622_service_del_attrs },
	{ "hcc", hi5622_hcc_add_attrs, hi5622_hcc_del_attrs },
	{ "h2d", hi5622_h2d_add_attrs, hi5622_h2d_del_attrs },
	{ "d2h", hi5622_d2h_add_attrs, hi5622_d2h_del_attrs },
	{ "dp", hi5622_dp_add_attrs, hi5622_dp_del_attrs },
	{ "cfg80211", hi5622_cfg80211_add_attrs, hi5622_cfg80211_del_attrs },
};

static int hi5622_add_all_attrs(struct device *dev)
{
	size_t i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(hi5622_groups); i++) {
		ret = hi5622_groups[i].add(dev);
		if (ret) {
			dev_err(dev, "sysfs group '%s': %d\n",
				hi5622_groups[i].name, ret);
			while (i-- > 0)
				hi5622_groups[i].del(dev);
			return ret;
		}
	}
	return 0;
}

static void hi5622_del_all_attrs(struct device *dev)
{
	size_t i = ARRAY_SIZE(hi5622_groups);

	while (i-- > 0)
		hi5622_groups[i].del(dev);
}

/* ---------------------------------------------------------------------
 * PCI probe / remove
 * -------------------------------------------------------------------*/
static int hi5622_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct hi5622_pci *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(dev, ret, "pcim_enable_device\n");
	pci_set_master(pdev);

	/* BAR0 = 16 MiB six-region window; BAR2 = 16 KiB endpoint iATU.
	 * BAR4 (8 MiB) is not mapped: its role is [U]. */
	ret = pcim_iomap_regions(pdev,
				 BIT(HI5622_BAR_MEM_WIN) | BIT(HI5622_BAR_IATU),
				 HI5622_DRV_NAME);
	if (ret)
		return dev_err_probe(dev, ret, "pcim_iomap_regions\n");

	ctx->pdev = pdev;
	ctx->bar0 = pcim_iomap_table(pdev)[HI5622_BAR_MEM_WIN];
	ctx->bar2 = pcim_iomap_table(pdev)[HI5622_BAR_IATU];
	spin_lock_init(&ctx->doorbell_lock);
	pci_set_drvdata(pdev, ctx);

	/* Host-DRAM HCC service + direct D2H reader (no device writes). */
	ret = hi5622_hcc_probe(ctx);
	if (ret)
		return dev_err_probe(dev, ret, "HCC service probe\n");

	/* cfg80211 full-MAC layer: allocates the wiphy at probe (no device
	 * access); registration is gated on the HCC ready handshake. */
	ret = hi5622_cfg80211_init(ctx);
	if (ret) {
		hi5622_hcc_remove(ctx);
		return dev_err_probe(dev, ret, "cfg80211 init\n");
	}

	/* MAC data plane (merged wifi-dp-air lane): host state only at probe
	 * (no rings, no device access); dp_setup is the one MAC/ring action. */
	ret = hi5622_dp_probe(ctx);
	if (ret) {
		hi5622_cfg80211_remove(ctx);
		hi5622_hcc_remove(ctx);
		return dev_err_probe(dev, ret, "dp probe\n");
	}

	ret = hi5622_add_all_attrs(dev);
	if (ret) {
		hi5622_dp_remove(ctx);
		hi5622_cfg80211_remove(ctx);
		hi5622_hcc_remove(ctx);
		return ret;
	}

	dev_info(dev,
		 "%s: endpoint %s BAR0 0x%pap BAR2 0x%pap; corrected boot protocol: release-only + custom-block pre-seed; probe performed no device writes\n",
		 HI5622_DRV_NAME, pci_name(pdev),
		 &pdev->resource[HI5622_BAR_MEM_WIN].start,
		 &pdev->resource[HI5622_BAR_IATU].start);
	return 0;
}

static void hi5622_remove(struct pci_dev *pdev)
{
	struct hi5622_pci *ctx = pci_get_drvdata(pdev);

	hi5622_del_all_attrs(&pdev->dev);
	hi5622_dp_remove(ctx);
	hi5622_hcc_remove(ctx);
	hi5622_cfg80211_remove(ctx);
	dev_info(&pdev->dev, "%s removed\n", HI5622_DRV_NAME);
}

static const struct pci_device_id hi5622_id_table[] = {
	{ PCI_DEVICE(HI5622_VENDOR_ID, HI5622_DEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, hi5622_id_table);

static struct pci_driver hi5622_driver = {
	.name		= HI5622_DRV_NAME,
	.id_table	= hi5622_id_table,
	.probe		= hi5622_probe,
	.remove		= hi5622_remove,
};
module_pci_driver(hi5622_driver);

MODULE_LICENSE("GPL");
/* The project kernel is built with CONFIG_MODULE_STRIPPED, which turns the
 * standard MODULE_DESCRIPTION()/MODULE_AUTHOR() helpers into no-ops; emit the
 * metadata unconditionally so modinfo/modpost still see it. */
#ifdef CONFIG_MODULE_STRIPPED
MODULE_INFO(description,
	    "Hi5622V100 clean-room PCIe/ETE Wi-Fi transport + HCC service + post-ready H2D send path incl. ordered context-create + MAC data-plane host bring-up + first TX/RX actions (Linux 6.18)");
MODULE_INFO(author, "cudy-wr3000v2 clean-room reverse-engineering project");
#else
MODULE_DESCRIPTION("Hi5622V100 clean-room PCIe/ETE Wi-Fi transport + HCC service + post-ready H2D send path incl. ordered context-create + MAC data-plane host bring-up + first TX/RX actions (Linux 6.18)");
MODULE_AUTHOR("cudy-wr3000v2 clean-room reverse-engineering project");
#endif
