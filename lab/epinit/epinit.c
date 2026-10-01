// SPDX-License-Identifier: GPL-2.0
/*
 * epinit: the prefix of the vendor's endpoint-initialisation sequence, done on
 * an endpoint that NO vendor driver owns (the boot-time takeover configuration
 * of docs/phase16/boot-takeover.md).
 *
 * The vendor's firmware download is NOT a memcpy of FIRMWARE.bin into BAR0.
 * Recovered from hi5622v100_plat.ko (docs/phase16/endpoint-init.md, part A):
 * wlan_power_on -> firmware_download_function -> firmware_download ->
 * firmware_mem_try_alloc(0x80000) -> firmware_file_send -> bal_write(chip,
 * dev_addr, buf, len), i.e. a chunked transfer through the vendor's BAL/HCC
 * DMA+message engine with per-chip device addresses.  That engine is not
 * implemented here, so the download path is *not* unambiguous and this module
 * deliberately does not load the firmware.
 *
 * What is left is the part of the sequence that is provably safe on an
 * unowned chip and is documented as constant by the disassembly:
 *
 *   stage 1 (default) - the vendor's config-space claim/init:
 *     - pci_enable_device()            [oal_pci_lres_init]
 *     - pci_request_mem_regions()      [our claim; the vendor relies on probe]
 *     - pci_write_config_word(dev, PCI_COMMAND, 0x0007)
 *                                      [oal_pcie_set_inbound_by_viewport]
 *     - read back the whole documented config-space set and log it
 *     - map BAR0 and log the ROM vector page (read-only)
 *
 *   stage 2 (opt-in, module parameter; NOT performed by the test boot) - the
 *   two constant BAR0 register writes the disassembly proves:
 *     - BAR0+0x02210 = (old & 0x3f) | 0x180   [pcie_main_init,
 *                                               "oal_pcie_set_voltage 0.875V"]
 *     - BAR0+0x3a200 = 0x1                    [shuangta_pcie_enable_remap]
 *   Both offsets lie in the first 256 KiB of BAR0, the region the phase-16
 *   test boot proved writable; every write is read back.  They are gated off
 *   because a *bulk* BAR0 write already soft-locked the bus once and a single
 *   register write has never been proven safe on an unowned chip.
 *
 * The module resets nothing and starts no CPU.  It never writes to the
 * firmware window (BAR0+0x40000..) and never touches BAR2/BAR4.
 *
 * Module parameters:
 *   domain=N   PCI domain of the 59e7:0005 endpoint (default 0 = 2.4 GHz)
 *   stage=N    1 = config-space init only (default); 2 = also the BAR0 writes
 */

#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

/* Config-space offsets the vendor disassembly reads or writes. */
#define OMO_CFG_CMD	0x04	/* PCI_COMMAND: vendor writes 0x0007 */
#define OMO_CFG_BAR0	0x10
#define OMO_CFG_L1SS0	0x80	/* shuangta_pcie_l1ss_set writes 0x10120043 */
#define OMO_CFG_L1SS1	0x98	/* shuangta_pcie_l1ss_set writes 0x00000400 */
#define OMO_CFG_L1SS2	0x158	/* shuangta_pcie_l1ss_set writes 0x40a0000c */
#define OMO_CFG_IATU_VP	0x900	/* pcie_inbound_viewport_switch */
#define OMO_CFG_IATU_CTL 0x908	/* pcie_inbound_viewport_switch */
#define OMO_CFG_IATU_RGN 0x90c	/* pcie_inbound_region_cfg */
#define OMO_CFG_HOSTCAP	0xff8	/* oal_pcie_host_init reads this */

#define OMO_ROM_OFF	0x00000UL
#define OMO_ROM_LEN	0x1000UL
#define OMO_ROM_WORDS	8
#define OMO_VOLT_REG	0x02210UL	/* pcie_main_init voltage trim */
#define OMO_REMAP_REG	0x3a200UL	/* shuangta_pcie_enable_remap */
#define OMO_REG_WIN	0x40000UL	/* both regs live in the first 256 KiB */

static unsigned int omo_domain;
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static unsigned int omo_stage = 1;
module_param_named(stage, omo_stage, uint, 0444);
MODULE_PARM_DESC(stage, "1 = config-space init only (default); 2 = also the two BAR0 register writes");

static struct pci_dev *omo_dev;

/* ---- helpers ----------------------------------------------------------- */

static void omo_cfg_dump(struct pci_dev *dev, u16 off)
{
	u32 v = 0;

	if (pci_read_config_dword(dev, off, &v))
		pr_info("omo-epinit: cfg[0x%03x] read failed\n", off);
	else
		pr_info("omo-epinit: cfg[0x%03x] = 0x%08x\n", off, v);
}

/*
 * Write a word, read it straight back, log both.  Every write is verified.
 *
 * PCI_COMMAND bit 0 (I/O space) is hardwired 0 because the 59e7:0005 endpoint
 * has no I/O BAR, so the vendor's own pci_write_config_word(dev,4,7) reads back
 * 0x0006.  Require the two bits the vendor actually needs (MEM|MASTER) rather
 * than strict equality, and say so in the log.
 */
static int omo_cfg_write_check(struct pci_dev *dev, u16 off, u16 val, const char *why)
{
	u16 rb = 0xffff;
	int ret, ok;

	ret = pci_write_config_word(dev, off, val);
	if (ret) {
		pr_err("omo-epinit: cfg[0x%03x] <= 0x%04x FAILED rc=%d (%s)\n",
		       off, val, ret, why);
		return ret;
	}
	pci_read_config_word(dev, off, &rb);
	if (off == PCI_COMMAND) {
		ok = (rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		     (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
		pr_info("omo-epinit: cfg[0x%03x] <= 0x%04x readback=0x%04x MEM|MASTER=%s (I/O bit RO0, no I/O BAR) (%s)\n",
			off, val, rb, ok ? "set" : "MISSING", why);
	} else {
		ok = (rb == val);
		pr_info("omo-epinit: cfg[0x%03x] <= 0x%04x readback=0x%04x match=%s (%s)\n",
			off, val, rb, ok ? "YES" : "NO", why);
	}
	return ok ? 0 : -EIO;
}

static void omo_log_words(const char *tag, unsigned long off,
			  const void __iomem *win, unsigned int n)
{
	unsigned int i;

	pr_info("omo-epinit: %s BAR0+0x%lx:", tag, off);
	for (i = 0; i < n; i++)
		pr_cont(" %08x", ioread32(win + 4UL * i));
	pr_cont("\n");
}

/* ---- stage 2: the two constant BAR0 register writes -------------------- */

static int omo_bar_writes(u64 base)
{
	void __iomem *win;
	u32 v, nv, rb;

	win = ioremap(base, OMO_REG_WIN);
	if (!win) {
		pr_err("omo-epinit: ioremap BAR0 register window failed\n");
		return -ENOMEM;
	}

	/* BAR0+0x2210, pcie_main_init: (old & 0x3f) | 0x180. */
	v = ioread32(win + OMO_VOLT_REG);
	nv = (v & 0x3f) | 0x180;
	iowrite32(nv, win + OMO_VOLT_REG);
	rb = ioread32(win + OMO_VOLT_REG);
	pr_info("omo-epinit: BAR0+0x%lx <= 0x%08x (from 0x%08x) readback=0x%08x match=%s\n",
		OMO_VOLT_REG, nv, v, rb, rb == nv ? "YES" : "NO");

	/* BAR0+0x3a200, shuangta_pcie_enable_remap: write 1. */
	v = ioread32(win + OMO_REMAP_REG);
	iowrite32(1, win + OMO_REMAP_REG);
	rb = ioread32(win + OMO_REMAP_REG);
	pr_info("omo-epinit: BAR0+0x%lx <= 0x00000001 (from 0x%08x) readback=0x%08x match=%s\n",
		OMO_REMAP_REG, v, rb, rb == 1 ? "YES" : "NO");

	iounmap(win);
	return 0;
}

/* ---- init / exit ------------------------------------------------------- */

static int __init omo_epinit_init(void)
{
	u32 lo = 0, hi = 0;
	u64 base;
	u16 cmd0 = 0, cmd1 = 0;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-epinit: endpoint %04x:00:00.0 not found\n", omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		/* Read the id from config space, not from struct pci_dev (phase-11 ABI lesson). */
		pci_read_config_dword(omo_dev, 0x00, &id);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("omo-epinit: unexpected id %04x:%04x\n", id & 0xffff, id >> 16);
	}

	/* Full documented config-space read-back, before any write. */
	pr_info("omo-epinit: ---- config space BEFORE (read-only) ----\n");
	omo_cfg_dump(omo_dev, 0x00);
	omo_cfg_dump(omo_dev, 0x04);
	omo_cfg_dump(omo_dev, 0x08);
	omo_cfg_dump(omo_dev, 0x0c);
	omo_cfg_dump(omo_dev, 0x10);
	omo_cfg_dump(omo_dev, 0x14);
	omo_cfg_dump(omo_dev, 0x18);
	omo_cfg_dump(omo_dev, 0x1c);
	omo_cfg_dump(omo_dev, 0x20);
	omo_cfg_dump(omo_dev, 0x24);
	omo_cfg_dump(omo_dev, 0x2c);
	omo_cfg_dump(omo_dev, 0x30);
	omo_cfg_dump(omo_dev, 0x34);
	omo_cfg_dump(omo_dev, OMO_CFG_L1SS0);
	omo_cfg_dump(omo_dev, OMO_CFG_L1SS1);
	omo_cfg_dump(omo_dev, OMO_CFG_L1SS2);
	omo_cfg_dump(omo_dev, OMO_CFG_IATU_VP);
	omo_cfg_dump(omo_dev, 0x904);
	omo_cfg_dump(omo_dev, OMO_CFG_IATU_CTL);
	omo_cfg_dump(omo_dev, OMO_CFG_IATU_RGN);
	omo_cfg_dump(omo_dev, OMO_CFG_HOSTCAP);

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);

	ret = pci_enable_device(omo_dev);
	if (ret) {
		pr_err("omo-epinit: pci_enable_device rc=%d\n", ret);
		goto err_put;
	}
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("omo-epinit: pci_enable_device rc=0 command 0x%04x -> 0x%04x [vendor oal_pci_lres_init]\n",
		cmd0, cmd1);

	ret = pci_request_mem_regions(omo_dev, "omo-epinit");
	if (ret) {
		pr_err("omo-epinit: pci_request_mem_regions rc=%d (region busy - vendor stack still loaded?) - refusing to continue\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-epinit: pci_request_mem_regions rc=0 (MEM BARs claimed)\n");

	if (omo_stage >= 1) {
		/* oal_pcie_set_inbound_by_viewport: pci_write_config_word(dev, 4, 7) */
		ret = omo_cfg_write_check(omo_dev, PCI_COMMAND, 0x0007,
					  "oal_pcie_set_inbound_by_viewport");
		if (ret)
			goto err_release;
	}

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("omo-epinit: no usable BAR0 (lo=0x%08x hi=0x%08x)\n", lo, hi);
		ret = -ENODEV;
		goto err_release;
	}
	pr_info("omo-epinit: BAR0 base=0x%llx (config-space read)\n",
		(unsigned long long)base);

	{
		void __iomem *rom = ioremap(base + OMO_ROM_OFF, OMO_ROM_LEN);

		if (!rom) {
			pr_err("omo-epinit: ioremap ROM page failed\n");
			ret = -ENOMEM;
			goto err_release;
		}
		omo_log_words("ROM vector page", OMO_ROM_OFF, rom, OMO_ROM_WORDS);
		iounmap(rom);
	}

	if (omo_stage >= 2) {
		ret = omo_bar_writes(base);
		if (ret)
			goto err_release;
	} else {
		pr_info("omo-epinit: stage=%u: BAR0 register writes NOT performed (not provably safe on an unowned chip); stopping here\n",
			omo_stage);
	}

	pr_info("omo-epinit: done - endpoint claimed, config-space init verified, firmware NOT loaded (BAL/HCC download path not implemented)\n");
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

static void __exit omo_epinit_exit(void)
{
	if (omo_dev) {
		pci_release_mem_regions(omo_dev);
		pci_disable_device(omo_dev);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("omo-epinit: unloaded, BARs released, device put\n");
}

module_init(omo_epinit_init);
module_exit(omo_epinit_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("First provably-safe steps of the vendor endpoint init (config-space claim), no firmware");
