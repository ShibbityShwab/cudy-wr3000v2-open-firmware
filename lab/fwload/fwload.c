// SPDX-License-Identifier: GPL-2.0
/*
 * fwload: the first complete open path from image to silicon.
 *
 * In the boot-time takeover configuration of docs/phase16/boot-takeover.md the
 * vendor Wi-Fi modules are renamed away, so our module claims endpoint
 * 0000:00:00.0 (59e7:0005, 2.4 GHz) and no vendor driver owns it.  This module
 * then reproduces the vendor's firmware download and verifies it.
 *
 * The vendor path, recovered from hi5622v100_plat.ko (docs/phase17/fw-download.md
 * part A, every step a disassembly quotation):
 *
 *   wlan_power_on @0xe46c
 *     -> firmware_download_function @0xf9f8   builds the per-chip descriptor,
 *                                             file table {path, 0x01240000}
 *     -> firmware_download @0xf834            firmware_mem_try_alloc(0x80000)
 *     -> firmware_file_send @0xf56c           read <=512 KiB chunks, for each:
 *           bal_write(chip, target + off, buf, n)
 *     -> bal_write @0x10c04                   dispatch, NOT an MMIO write:
 *           g_st_pcie_bus_driver[0x38] == pcie_write
 *     -> pcie_write @0xbdc0                   memcpy_s(dst, n, buf, n)
 *     -> pcie_para_check @0xbc04              dev CA -> host VA via
 *           oal_pcie_inbound_ca_to_va @0x8dd8 (g_shuangta_region_types table)
 *
 * So the firmware data path is a chunked host memcpy into the PCIe window that
 * backs device CA 0x01240000 - there is NO ETE descriptor, NO SR channel and
 * NO doorbell in it.  The ETE rings/doorbell belong to the HCC *message* path
 * (bal_port_start_xfer -> pcie_xfer_data -> pcie_tx_request_handle), which
 * bal_write does not use.
 *
 * Device CA 0x01240000 resolves to BAR0+0x6f8000: the region table's sixth
 * entry (SHUANGTA_REGION_ACP, device range 0x01200000-0x01417fff) is exposed at
 * BAR0+0x6b8000, and that window is a mirror of ROM_WRAM at BAR0+0x0 (proven:
 * /proc/iomem names both sub-regions and the loaded BAR0 dump has BAR0+0x6b8000
 * byte-identical to BAR0+0x0, and BAR0+0x6f8000 byte-identical to BAR0+0x40000,
 * which is where FIRMWARE.bin sits).  So the firmware download and the
 * verification window are the same SRAM seen through two aliases.
 *
 * What this module does, in order (every write logged with its read-back):
 *   1. claim the endpoint exactly as epinit/eteinit: pci_enable_device,
 *      pci_request_mem_regions (refuses if the vendor stack holds it),
 *      cfg[0x04]=7, config-space BAR0 read;
 *   2. initialise the ETE engine exactly as lab/eteinit does (coherent node
 *      arrays + the proven SR/DR program registers + chn_res RMW);
 *   3. read the firmware blob (module param fwpath);
 *   4. optional 4-byte write/read-back probe at the download target, and STOP
 *      before the bulk write if it does not take (cheap, bounded check);
 *   5. stream the blob in `chunk`-sized pieces to the download target
 *      (default BAR0+0x6f8000 = device CA 0x01240000), logging per chunk;
 *   6. read the target and the verification window (default BAR0+0x40000) back
 *      and compare with the file, reporting match/mismatch and the first
 *      differing offset.
 *
 * Module parameters:
 *   domain=N   PCI domain of the endpoint (default 0 = 2.4 GHz)
 *   fwpath=P   firmware path (default /lib/firmware/hi_wifi/FIRMWARE.bin)
 *   target=N   BAR0 offset to download to (default 0x6f8000 = CA 0x01240000)
 *   verify=N   BAR0 offset to verify against the file (default 0x40000)
 *   chunk=N    bytes per transfer (default 65536; vendor uses 0x80000)
 *   maxlen=N   limit bytes written/verified (0 = whole file)
 *   wregs=N    1 = do the ETE SR/DR register init (default), 0 = read-only
 *   probe=N    1 = 4-byte writability probe at target before the bulk write
 *   dowrite=N  1 = perform the download (default); 0 = read the windows only
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

/* ETE register block (ete-init.md A.1): static .data+0x2944 word0. */
#define ETE_BAR0_OFF	0x3a000UL
#define ETE_WIN_LEN	0x1000UL

/* The two firmware-window aliases (see header comment). */
#define FW_CA_TARGET	0x01240000UL		/* device CA the vendor writes */
#define FW_TARGET_OFF	0x6f8000UL		/* its BAR0 host alias */
#define FW_VERIFY_OFF	0x40000UL		/* ROM_WRAM firmware window */
#define FW_WIN_LEN	0x200000UL		/* 2 MiB covers the 928,920 B file */

/* Per-channel register-block offsets (pcie_ete_get_chn_cfg, .rodata+0x101c). */
static const unsigned long sr_block[3] = { 0x400, 0x450, 0x4a0 };
static const unsigned long dr_block[4] = { 0x590, 0x5e0, 0x630, 0x680 };

#define ETE_SR_CTRL	0x08
#define ETE_SR_BASE	0x10
#define ETE_SR_DEPTH	0x14
#define ETE_SR_WPTR	0x18
#define ETE_DR_BASE	0x30
#define ETE_DR_DEPTH	0x34
#define ETE_DR_WPTR	0x38
#define ETE_CHN_RES	0x2e8
#define ETE_CHN_RES_MASK 0xfffffc20U
#define ETE_DEPTH	32

static unsigned int omo_domain;
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static char *omo_fwpath = "/lib/firmware/hi_wifi/FIRMWARE.bin";
module_param_named(fwpath, omo_fwpath, charp, 0444);
MODULE_PARM_DESC(fwpath, "firmware file to download");

static unsigned long omo_target = FW_TARGET_OFF;
module_param_named(target, omo_target, ulong, 0444);
MODULE_PARM_DESC(target, "BAR0 offset to download to (default 0x6f8000 = CA 0x01240000)");

static unsigned long omo_verify = FW_VERIFY_OFF;
module_param_named(verify, omo_verify, ulong, 0444);
MODULE_PARM_DESC(verify, "BAR0 offset to verify against the file");

static unsigned int omo_chunk = 0x10000;
module_param_named(chunk, omo_chunk, uint, 0444);
MODULE_PARM_DESC(chunk, "bytes per transfer (vendor uses 0x80000)");

static unsigned int omo_maxlen;
module_param_named(maxlen, omo_maxlen, uint, 0444);
MODULE_PARM_DESC(maxlen, "limit bytes written/verified (0 = whole file)");

static unsigned int omo_wregs = 1;
module_param_named(wregs, omo_wregs, uint, 0444);
MODULE_PARM_DESC(wregs, "1 = perform the proven SR/DR register writes");

static unsigned int omo_probe = 1;
module_param_named(probe, omo_probe, uint, 0444);
MODULE_PARM_DESC(probe, "1 = 4-byte writability probe at target before the bulk write");

static unsigned int omo_dowrite = 1;
module_param_named(dowrite, omo_dowrite, uint, 0444);
MODULE_PARM_DESC(dowrite, "1 = perform the download");

static struct pci_dev *omo_dev;
static void __iomem *omo_ete_win;
static void __iomem *omo_tgt_win;
static void __iomem *omo_ver_win;

static void *omo_sr_va[3];
static dma_addr_t omo_sr_dma[3];
static void *omo_dr_va[4];
static dma_addr_t omo_dr_dma[4];

static u8 *omo_fw;
static size_t omo_fw_len;

/* ---- ETE engine init (ported from lab/eteinit, proven in phase 17) ----- */

static void omo_log_words(const char *tag, unsigned long off,
			  const void __iomem *win, unsigned int n)
{
	unsigned int i;

	pr_info("omo-fwload: %s BAR0+0x%lx:", tag, off);
	for (i = 0; i < n; i++)
		pr_cont(" %08x", ioread32(win + 4UL * i));
	pr_cont("\n");
}

static void omo_wr(const void __iomem *win, unsigned long off, u32 val,
		   const char *name)
{
	u32 rb;

	iowrite32(val, win + off);
	rb = ioread32(win + off);
	pr_info("omo-fwload:   %-22s [0x%03lx] <= 0x%08x readback=0x%08x match=%s\n",
		name, off, val, rb, rb == val ? "YES" : "NO");
}

static int omo_alloc_rings(struct pci_dev *dev)
{
	size_t i;

	for (i = 0; i < 3; i++) {
		size_t sz = (ETE_DEPTH + 2) * 8;

		omo_sr_va[i] = dma_alloc_coherent(&dev->dev, sz,
						  &omo_sr_dma[i], GFP_KERNEL);
		if (!omo_sr_va[i]) {
			pr_err("omo-fwload: SR ch%zu dma_alloc_coherent FAILED\n", i);
			return -ENOMEM;
		}
	}
	for (i = 0; i < 4; i++) {
		size_t sz = ETE_DEPTH * 8;

		omo_dr_va[i] = dma_alloc_coherent(&dev->dev, sz,
						  &omo_dr_dma[i], GFP_KERNEL);
		if (!omo_dr_va[i]) {
			pr_err("omo-fwload: DR ch%zu dma_alloc_coherent FAILED\n", i);
			return -ENOMEM;
		}
	}
	pr_info("omo-fwload: ETE ring arrays allocated (3x272B SR, 4x256B DR)\n");
	return 0;
}

static void omo_ete_init(u64 base)
{
	u32 v;
	int i;

	omo_ete_win = ioremap(base + ETE_BAR0_OFF, ETE_WIN_LEN);
	if (!omo_ete_win) {
		pr_err("omo-fwload: ioremap ETE block FAILED\n");
		return;
	}
	pr_info("omo-fwload: ETE block mapped at BAR0+0x%lx\n", ETE_BAR0_OFF);
	omo_log_words("ETE block +0x000", 0x000, omo_ete_win, 8);

	if (!omo_wregs) {
		pr_info("omo-fwload: wregs=0: SR/DR register writes NOT performed\n");
		return;
	}
	for (i = 0; i < 3; i++) {
		unsigned long b = sr_block[i];
		char t[32];

		scnprintf(t, sizeof(t), "SR ch%d base", i);
		omo_wr(omo_ete_win, b + ETE_SR_BASE, (u32)omo_sr_dma[i], t);

		v = ioread32(omo_ete_win + b + ETE_SR_DEPTH);
		scnprintf(t, sizeof(t), "SR ch%d depth-1", i);
		omo_wr(omo_ete_win, b + ETE_SR_DEPTH, (v & ~0x3ffU) | (ETE_DEPTH - 1), t);

		scnprintf(t, sizeof(t), "SR ch%d wptr", i);
		omo_wr(omo_ete_win, b + ETE_SR_WPTR, 0, t);

		v = ioread32(omo_ete_win + b + ETE_SR_CTRL);
		scnprintf(t, sizeof(t), "SR ch%d ctrl", i);
		omo_wr(omo_ete_win, b + ETE_SR_CTRL, (v & ~0x7U), t);
	}
	for (i = 0; i < 4; i++) {
		unsigned long b = dr_block[i];
		char t[32];

		scnprintf(t, sizeof(t), "DR ch%d base", i + 3);
		omo_wr(omo_ete_win, b + ETE_DR_BASE, (u32)omo_dr_dma[i], t);

		v = ioread32(omo_ete_win + b + ETE_DR_DEPTH);
		scnprintf(t, sizeof(t), "DR ch%d depth-1", i + 3);
		omo_wr(omo_ete_win, b + ETE_DR_DEPTH, (v & ~0x3ffU) | (ETE_DEPTH - 1), t);

		scnprintf(t, sizeof(t), "DR ch%d wptr", i + 3);
		omo_wr(omo_ete_win, b + ETE_DR_WPTR, 0, t);
	}
	for (i = 0; i < 3; i++) {
		char t[32];

		v = ioread32(omo_ete_win + sr_block[i] + ETE_CHN_RES);
		scnprintf(t, sizeof(t), "SR ch%d chn_res", i);
		omo_wr(omo_ete_win, sr_block[i] + ETE_CHN_RES, v & ETE_CHN_RES_MASK, t);
	}
	for (i = 0; i < 4; i++) {
		char t[32];

		v = ioread32(omo_ete_win + dr_block[i] + ETE_CHN_RES);
		scnprintf(t, sizeof(t), "DR ch%d chn_res", i + 3);
		omo_wr(omo_ete_win, dr_block[i] + ETE_CHN_RES, v & ETE_CHN_RES_MASK, t);
	}
	pr_info("omo-fwload: ETE SR/DR registers initialised (wregs=1)\n");
}

/* ---- firmware file ----------------------------------------------------- */

static int omo_load_fw(void)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t n;
	size_t done = 0;

	f = filp_open(omo_fwpath, O_RDONLY, 0);
	if (IS_ERR(f)) {
		pr_err("omo-fwload: filp_open(%s) failed %ld\n",
		       omo_fwpath, PTR_ERR(f));
		return PTR_ERR(f);
	}
	omo_fw_len = i_size_read(file_inode(f));
	if (!omo_fw_len || omo_fw_len > 16UL * 1024 * 1024) {
		pr_err("omo-fwload: bad firmware size %zu\n", omo_fw_len);
		filp_close(f, NULL);
		return -EINVAL;
	}
	omo_fw = vmalloc(omo_fw_len);
	if (!omo_fw) {
		filp_close(f, NULL);
		return -ENOMEM;
	}
	while (done < omo_fw_len) {
		n = kernel_read(f, omo_fw + done, omo_fw_len - done, &pos);
		if (n <= 0) {
			pr_err("omo-fwload: kernel_read stopped at %zu/%zu (n=%zd)\n",
			       done, omo_fw_len, n);
			filp_close(f, NULL);
			vfree(omo_fw);
			omo_fw = NULL;
			return n ? (int)n : -EIO;
		}
		done += n;
	}
	filp_close(f, NULL);
	pr_info("omo-fwload: firmware file %s size=%zu bytes\n",
		omo_fwpath, omo_fw_len);
	return 0;
}

/* ---- the download ------------------------------------------------------ */

static int omo_download(void)
{
	size_t limit = omo_maxlen ? min_t(size_t, omo_maxlen, omo_fw_len) : omo_fw_len;
	size_t off = 0;
	u32 probe_val = 0xdeadbeef, probe_rb;

	if (omo_probe) {
		iowrite32(probe_val, omo_tgt_win);
		probe_rb = ioread32(omo_tgt_win);
		pr_info("omo-fwload: writability probe target BAR0+0x%lx: wrote 0x%08x read 0x%08x match=%s\n",
			omo_target, probe_val, probe_rb,
			probe_rb == probe_val ? "YES" : "NO");
		if (probe_rb != probe_val) {
			pr_err("omo-fwload: target window does NOT hold a write - refusing the bulk download (documented stop)\n");
			return -EIO;
		}
	}

	pr_info("omo-fwload: download %zu bytes -> BAR0+0x%lx (= device CA 0x%lx) in %u-byte chunks\n",
		limit, omo_target, (unsigned long)FW_CA_TARGET, omo_chunk);

	while (off < limit) {
		size_t n = min_t(size_t, omo_chunk, limit - off);

		memcpy_toio(omo_tgt_win + off, omo_fw + off, n);
		off += n;
		pr_info("omo-fwload: wrote %zu/%zu bytes @ BAR0+0x%lx\n",
			off, limit, omo_target + off - n);
	}
	return 0;
}

/* ---- verification ------------------------------------------------------ */

static void omo_verify_win(const char *name, unsigned long b0,
			   const void __iomem *win, size_t limit)
{
	size_t off, diffs = 0;
	long first = -1;
	u32 w;
	int k;

	for (off = 0; off + 4 <= limit; off += 4) {
		w = ioread32(win + off);
		for (k = 0; k < 4; k++) {
			u8 got = (w >> (8 * k)) & 0xff;
			u8 exp = omo_fw[off + k];

			if (got != exp) {
				if (first < 0)
					first = (long)(off + k);
				diffs++;
			}
		}
	}
	pr_info("omo-fwload: verify %s BAR0+0x%lx: file=%zu bytes diffs=%zu first_diff=%s match=%s\n",
		name, b0, limit, diffs,
		first < 0 ? "none" : "see below", diffs ? "NO" : "YES");
	if (first >= 0)
		pr_info("omo-fwload:   first differing offset = 0x%lx (file 0x%02x, chip 0x%02x)\n",
			first, omo_fw[first],
			(u8)(ioread32(win + (first & ~3UL)) >> (8 * (first & 3))));
}

/* ---- init / exit ------------------------------------------------------- */

static int __init omo_fwload_init(void)
{
	u32 lo = 0, hi = 0;
	u64 base;
	u16 cmd0 = 0, cmd1 = 0, rb = 0xffff;
	size_t limit;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-fwload: endpoint %04x:00:00.0 not found\n", omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev, 0x00, &id);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("omo-fwload: unexpected id %04x:%04x\n",
				id & 0xffff, id >> 16);
	}

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);
	ret = pci_enable_device(omo_dev);
	if (ret)
		goto err_put;
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("omo-fwload: pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
		cmd0, cmd1);

	ret = pci_request_mem_regions(omo_dev, "omo-fwload");
	if (ret) {
		pr_err("omo-fwload: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-fwload: pci_request_mem_regions rc=0 (MEM BARs claimed)\n");

	ret = pci_write_config_word(omo_dev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_dev, PCI_COMMAND, &rb);
	pr_info("omo-fwload: cfg[0x004] <= 0x0007 readback=0x%04x MEM|MASTER=%s\n",
		rb, (rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		    (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) | ((u64)hi << 32);
	if (!base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("omo-fwload: no usable BAR0 (lo=0x%08x hi=0x%08x)\n", lo, hi);
		ret = -ENODEV;
		goto err_release;
	}
	pr_info("omo-fwload: BAR0 base=0x%llx (config-space read)\n",
		(unsigned long long)base);
	{
		void __iomem *rom = ioremap(base, 0x40);

		if (rom) {
			omo_log_words("ROM vector page", 0x0, rom, 8);
			iounmap(rom);
		}
	}

	/* (2) ETE engine, as lab/eteinit. */
	ret = omo_alloc_rings(omo_dev);
	if (ret)
		goto err_release;
	omo_ete_init(base);

	ret = omo_load_fw();
	if (ret)
		goto err_release;

	limit = omo_maxlen ? min_t(size_t, omo_maxlen, omo_fw_len) : omo_fw_len;

	omo_tgt_win = ioremap(base + omo_target, FW_WIN_LEN);
	omo_ver_win = ioremap(base + omo_verify, FW_WIN_LEN);
	if (!omo_tgt_win || !omo_ver_win) {
		pr_err("omo-fwload: ioremap of the firmware windows FAILED\n");
		ret = -ENOMEM;
		goto err_release;
	}
	pr_info("omo-fwload: target window BAR0+0x%lx and verify window BAR0+0x%lx mapped\n",
		omo_target, omo_verify);

	if (omo_dowrite && omo_download() == 0)
		pr_info("omo-fwload: download complete (%zu bytes)\n", limit);
	else if (!omo_dowrite)
		pr_info("omo-fwload: dowrite=0: no write performed\n");

	/* (6) read both aliases back and compare with the file. */
	omo_verify_win("target", omo_target, omo_tgt_win, limit);
	omo_verify_win("verify", omo_verify, omo_ver_win, limit);

	pr_info("omo-fwload: done - endpoint claimed, ETE engine initialised, %zu bytes streamed to BAR0+0x%lx\n",
		omo_dowrite ? limit : 0UL, omo_target);
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

static void __exit omo_fwload_exit(void)
{
	int i;

	if (omo_ete_win)
		iounmap(omo_ete_win);
	if (omo_tgt_win)
		iounmap(omo_tgt_win);
	if (omo_ver_win)
		iounmap(omo_ver_win);
	if (omo_fw)
		vfree(omo_fw);
	for (i = 0; i < 3; i++)
		if (omo_sr_va[i])
			dma_free_coherent(&omo_dev->dev, (ETE_DEPTH + 2) * 8,
					  omo_sr_va[i], omo_sr_dma[i]);
	for (i = 0; i < 4; i++)
		if (omo_dr_va[i])
			dma_free_coherent(&omo_dev->dev, ETE_DEPTH * 8,
					  omo_dr_va[i], omo_dr_dma[i]);
	if (omo_dev) {
		pci_release_mem_regions(omo_dev);
		pci_disable_device(omo_dev);
		pci_dev_put(omo_dev);
	}
	pr_info("omo-fwload: unloaded\n");
}

module_init(omo_fwload_init);
module_exit(omo_fwload_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Open firmware download to the unowned 59e7:0005 endpoint (chunked write + BAR0 verify)");
