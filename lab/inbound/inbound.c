// SPDX-License-Identifier: GPL-2.0
/*
 * inbound: program the endpoint's inbound iATU (the vendor's probe-time region
 * map) in the takeover boot, then probe whether the firmware window decodes.
 *
 * Recovered from hi5622v100_plat.ko (see docs/phase18/inbound-map.md part A).
 * The vendor path is:
 *
 *   oal_pci_lres_init @0x7bb0
 *     pcie_get_bus_id_host_view, get_chip_type, pci_enable_device,
 *     pci_read_config_byte(dev, 8)                     ; PCI revision id
 *     -> oal_pcie_host_init @0xbefc                    ; -> host->rev = cfg[8]
 *     -> oal_pcie_dev_init @0xa090
 *          oal_pcie_enable_regions @0x91e4             ; sets priv+0x30 = 1
 *          <inlined oal_pcie_set_inbound> @0x989c
 *              if (priv->rev == 1)  oal_pcie_set_inbound_by_viewport @0x97a8
 *                      -> pcie_inbound_viewport_switch @0x9264   cfg 0x900/0x908
 *                      -> pcie_inbound_region_cfg @0x94e0         cfg 0x90c..0x91c
 *              if (priv->rev == 0)  the "membar" path: per-region MMIO writes
 *                      to the iATU register window (BAR2, resource "iatu_bar1")
 *                      at  0x104 + 0x200*i
 *          pci_write_config_word(dev, 4, 7)
 *
 * Our endpoint reports PCI revision id 0 (cfg[0x008] = 0x02800000, byte 8 =
 * 0x00), so the vendor takes the membar path on this silicon and never writes
 * the config-space 0x900 block - which is exactly why cfg[0x900] reads
 * 0xffffffff in the takeover state (phase 16 measured it).  This module
 * reproduces the membar path.
 *
 * Register layout of the iATU window (16 KiB at BAR2), per viewport i:
 *   ctrl1      @ 0x100 + 0x200*i   (never written; default 0 = memory region)
 *   ctrl2      @ 0x104 + 0x200*i   (0, then (bar&7)<<8 | 0x80000000)
 *   base_lo    @ 0x108 + 0x200*i   (host-side PCI bus base, low 32)
 *   base_hi    @ 0x10c + 0x200*i
 *   limit_lo   @ 0x110 + 0x200*i   (base + size - 1)
 *   target_lo  @ 0x114 + 0x200*i   (device chip address, low 32)
 *   target_hi  @ 0x118 + 0x200*i
 * This is the same register file as the config-space iATU (0x904..0x91c),
 * shifted by -0x804; the config-space path additionally selects the viewport
 * through 0x900, which the MMIO window does not need.
 *
 * The six regions (vendor dmesg: "bar idx:0, region idx:N" + /proc/iomem):
 *   idx host base    size      device CA    region
 *   0   0x40000000   0x1c0000  0x00000000   SHUANGTA_REGION_ROM_WRAM
 *   1   0x401c0000   0x018000  0x00400000   SHUANGTA_REGION_TCM_NOACP
 *   2   0x401d8000   0x1e0000  0x01000000   SHUANGTA_REGION_PKTRAM_NOACP
 *   3   0x403b8000   0x120000  0x40000000   SHUANGTA_REGION_IO
 *   4   0x404d8000   0x1e0000  0x02000000   SHUANGTA_REGION_ACP
 *   5   0x406b8000   0x218000  0x01200000   SHUANGTA_REGION_ACP (firmware)
 * so device CA 0x01240000 (the vendor firmware target) is BAR0+0x6f8000.
 *
 * Module parameters:
 *   domain=N   endpoint domain (default 0 = 2.4 GHz, 0000:00:00.0)
 *   program=N  1 = program the six inbound viewports (default); 0 = read only
 *   fwpath=P   firmware path (mode=2)
 *   mode=N     1 = program + probe the decode (default)
 *              2 = program + write fwpath to BAR0+0x6f8000 + verify
 *   target=N   BAR0 offset the firmware is written to (default 0x6f8000)
 *   verify=N   BAR0 offset probed for the ROM/firmware image (default 0x40000)
 *   chunk=N    bytes per firmware chunk (default 0x10000, vendor 0x80000)
 *   maxlen=N   limit bytes written/verified (0 = whole file)
 *   dowrite=N  mode=2 only: 1 = write the firmware (default)
 */

#include <linux/fs.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#define OMO_VENDOR_ID	0x59e7
#define OMO_DEVICE_ID	0x0005

#define OMO_BAR_NUM	0
#define OMO_IATU_BAR	2
#define OMO_IATU_STRIDE	0x200UL
#define OMO_IATU_CTRL2	0x104UL		/* + 0x200*i */
#define OMO_IATU_VP0	0x100UL
#define OMO_IATU_BAR_LEN 0x4000UL

#define FW_TARGET_OFF	0x6f8000UL	/* device CA 0x01240000 */
#define FW_VERIFY_OFF	0x40000UL
#define FW_WIN_LEN	0x200000UL

struct omo_region {
	unsigned long off;	/* BAR0 offset of the host window */
	u32 size;
	u64 target;		/* device chip address */
	const char *name;
};

/* Proven table: vendor dmesg + g_shuangta_region_types + /proc/iomem. */
static const struct omo_region omo_regions[6] = {
	{ 0x000000, 0x1c0000, 0x00000000UL, "ROM_WRAM" },
	{ 0x1c0000, 0x018000, 0x00400000UL, "TCM_NOACP" },
	{ 0x1d8000, 0x1e0000, 0x01000000UL, "PKTRAM_NOACP" },
	{ 0x3b8000, 0x120000, 0x40000000UL, "IO" },
	{ 0x4d8000, 0x1e0000, 0x02000000UL, "ACP" },
	{ 0x6b8000, 0x218000, 0x01200000UL, "ACP-fw" },
};

static unsigned int omo_domain;
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static unsigned int omo_program = 1;
module_param_named(program, omo_program, uint, 0444);
MODULE_PARM_DESC(program, "1 = program the six inbound iATU viewports (default)");

static unsigned int omo_mode = 1;
module_param_named(mode, omo_mode, uint, 0444);
MODULE_PARM_DESC(mode, "1 = program + decode probe, 2 = program + firmware write");

static char *omo_fwpath = "/lib/firmware/hi_wifi/FIRMWARE.bin";
module_param_named(fwpath, omo_fwpath, charp, 0444);
MODULE_PARM_DESC(fwpath, "firmware file to download (mode=2)");

static unsigned long omo_target = FW_TARGET_OFF;
module_param_named(target, omo_target, ulong, 0444);
MODULE_PARM_DESC(target, "BAR0 offset the firmware is written to (default 0x6f8000)");

static unsigned long omo_verify = FW_VERIFY_OFF;
module_param_named(verify, omo_verify, ulong, 0444);
MODULE_PARM_DESC(verify, "BAR0 offset probed for the ROM/firmware image");

static unsigned int omo_chunk = 0x10000;
module_param_named(chunk, omo_chunk, uint, 0444);
MODULE_PARM_DESC(chunk, "bytes per firmware chunk (vendor uses 0x80000)");

static unsigned int omo_maxlen;
module_param_named(maxlen, omo_maxlen, uint, 0444);
MODULE_PARM_DESC(maxlen, "limit bytes written/verified (0 = whole file)");

static unsigned int omo_dowrite = 1;
module_param_named(dowrite, omo_dowrite, uint, 0444);
MODULE_PARM_DESC(dowrite, "mode=2: 1 = write the firmware");

static struct pci_dev *omo_dev;
static void __iomem *omo_bar0;
static void __iomem *omo_iatu;
static u64 omo_bar0_base;

static u8 *omo_fw;
static size_t omo_fw_len;

/* ---- logging helpers --------------------------------------------------- */

static void omo_words(const char *tag, const void __iomem *win,
		      unsigned long off, unsigned int n)
{
	unsigned int i;

	pr_info("omo-inbound: %s [0x%lx]:", tag, off);
	for (i = 0; i < n; i++)
		pr_cont(" %08x", ioread32(win + off + 4UL * i));
	pr_cont("\n");
}

static void omo_cfg(const char *name, int off)
{
	u32 v = 0;

	pci_read_config_dword(omo_dev, off, &v);
	pr_info("omo-inbound: cfg[0x%03x] = 0x%08x  %s\n", off, v, name);
}

/* ---- the inbound iATU programming (vendor membar path) ----------------- */

static void omo_iatu_wr(unsigned long off, u32 val, const char *name)
{
	u32 rb;

	iowrite32(val, omo_iatu + off);
	rb = ioread32(omo_iatu + off);
	pr_info("omo-inbound:   iatu[0x%03lx] <= 0x%08x readback=0x%08x match=%s  (%s)\n",
		off, val, rb, rb == val ? "YES" : "NO", name);
}

static int omo_program_regions(void)
{
	unsigned int i;
	int programmed = 0;

	for (i = 0; i < ARRAY_SIZE(omo_regions); i++) {
		const struct omo_region *r = &omo_regions[i];
		u64 base = omo_bar0_base + r->off;
		u64 limit = base + r->size - 1;
		unsigned long ctrl2 = OMO_IATU_CTRL2 + OMO_IATU_STRIDE * i;
		char nm[40];

		pr_info("omo-inbound: region %u %s: host 0x%llx..0x%llx -> dev 0x%llx size 0x%x\n",
			i, r->name, (unsigned long long)base,
			(unsigned long long)limit, (unsigned long long)r->target,
			r->size);

		/* vendor order: ctrl2 = 0, ctrl2 = enable|bar, base, limit, target */
		scnprintf(nm, sizeof(nm), "r%u ctrl2=0", i);
		omo_iatu_wr(ctrl2, 0, nm);

		scnprintf(nm, sizeof(nm), "r%u ctrl2=ena", i);
		omo_iatu_wr(ctrl2, 0x80000000U, nm);	/* bar 0 (vendor log bar idx:0) */

		scnprintf(nm, sizeof(nm), "r%u base_lo", i);
		omo_iatu_wr(ctrl2 + 4, (u32)base, nm);
		scnprintf(nm, sizeof(nm), "r%u base_hi", i);
		omo_iatu_wr(ctrl2 + 8, (u32)(base >> 32), nm);
		scnprintf(nm, sizeof(nm), "r%u limit", i);
		omo_iatu_wr(ctrl2 + 12, (u32)limit, nm);
		scnprintf(nm, sizeof(nm), "r%u target_lo", i);
		omo_iatu_wr(ctrl2 + 16, (u32)r->target, nm);
		scnprintf(nm, sizeof(nm), "r%u target_hi", i);
		omo_iatu_wr(ctrl2 + 20, (u32)(r->target >> 32), nm);

		programmed++;
	}
	return programmed;
}

/* ---- decode probe / firmware verify ------------------------------------ */

static unsigned int omo_non_ff(const void __iomem *win, unsigned long off,
			       unsigned int words)
{
	unsigned int i, n = 0;

	for (i = 0; i < words; i++)
		if (ioread32(win + off + 4UL * i) != 0xffffffffU)
			n++;
	return n;
}

static int omo_load_fw(void)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t n;
	size_t done = 0;

	f = filp_open(omo_fwpath, O_RDONLY, 0);
	if (IS_ERR(f)) {
		pr_err("omo-inbound: filp_open(%s) failed %ld\n",
		       omo_fwpath, PTR_ERR(f));
		return PTR_ERR(f);
	}
	omo_fw_len = i_size_read(file_inode(f));
	if (!omo_fw_len || omo_fw_len > 16UL * 1024 * 1024) {
		pr_err("omo-inbound: bad firmware size %zu\n", omo_fw_len);
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
			pr_err("omo-inbound: kernel_read stopped at %zu/%zu (n=%zd)\n",
			       done, omo_fw_len, n);
			filp_close(f, NULL);
			vfree(omo_fw);
			omo_fw = NULL;
			return n ? (int)n : -EIO;
		}
		done += n;
	}
	filp_close(f, NULL);
	pr_info("omo-inbound: firmware file %s size=%zu bytes\n",
		omo_fwpath, omo_fw_len);
	return 0;
}

static void omo_verify_win(const char *name, unsigned long b0, size_t limit)
{
	size_t off, diffs = 0;
	long first = -1;
	u32 w;
	int k;

	for (off = 0; off + 4 <= limit; off += 4) {
		w = ioread32(omo_bar0 + b0 + off);
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
	pr_info("omo-inbound: verify %s BAR0+0x%lx: file=%zu bytes diffs=%zu match=%s\n",
		name, b0, limit, diffs, diffs ? "NO" : "YES");
	if (first >= 0) {
		u32 w0 = ioread32(omo_bar0 + b0 + (first & ~3UL));

		pr_info("omo-inbound:   first diff @0x%lx file=0x%02x chip=0x%02x\n",
			first, omo_fw[first],
			(u8)(w0 >> (8 * (first & 3))));
	}
}

static int omo_write_fw(void)
{
	size_t limit = omo_maxlen ? min_t(size_t, omo_maxlen, omo_fw_len) : omo_fw_len;
	size_t off = 0;
	u32 probe = 0xdeadbeef, rb;

	iowrite32(probe, omo_bar0 + omo_target);
	rb = ioread32(omo_bar0 + omo_target);
	pr_info("omo-inbound: writability probe BAR0+0x%lx: wrote 0x%08x read 0x%08x match=%s\n",
		omo_target, probe, rb, rb == probe ? "YES" : "NO");
	if (rb != probe) {
		pr_err("omo-inbound: target window does not hold a write - refusing the bulk write (documented stop)\n");
		return -EIO;
	}
	pr_info("omo-inbound: download %zu bytes -> BAR0+0x%lx (device CA 0x01240000) in %u-byte chunks\n",
		limit, omo_target, omo_chunk);
	while (off < limit) {
		size_t n = min_t(size_t, omo_chunk, limit - off);

		memcpy_toio(omo_bar0 + omo_target + off, omo_fw + off, n);
		off += n;
		pr_info("omo-inbound: wrote %zu/%zu bytes @ BAR0+0x%lx\n",
			off, limit, omo_target + off - n);
	}
	omo_verify_win("target", omo_target, limit);
	return 0;
}

/* ---- init / exit ------------------------------------------------------- */

static int __init omo_inbound_init(void)
{
	u16 cmd0 = 0, cmd1 = 0, rb = 0;
	unsigned int before_tgt, before_ver, after_tgt, after_ver;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-inbound: endpoint %04x:00:00.0 not found\n", omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev, 0x00, &id);
		pr_info("omo-inbound: endpoint 0000:00:00.0 id %04x:%04x\n",
			id & 0xffff, id >> 16);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("omo-inbound: unexpected id\n");
	}

	/* Read-only config-space dump before anything is claimed. */
	omo_cfg("vendor/device", 0x00);
	omo_cfg("command/status", 0x04);
	omo_cfg("revision[8]/class[9..b]", 0x08);
	omo_cfg("cache/header", 0x0c);
	omo_cfg("BAR0", 0x10);
	omo_cfg("BAR1", 0x14);
	omo_cfg("BAR2 (iatu_bar1)", 0x18);
	omo_cfg("BAR3", 0x1c);
	omo_cfg("BAR4", 0x20);
	omo_cfg("BAR5", 0x24);
	omo_cfg("subsystem", 0x2c);
	omo_cfg("0x30", 0x30);
	omo_cfg("0x34", 0x34);
	omo_cfg("rev/version 0xff8", 0xff8);
	omo_cfg("iATU viewport 0x900", 0x900);
	omo_cfg("iATU ctrl1 0x904", 0x904);
	omo_cfg("iATU ctrl2 0x908", 0x908);
	omo_cfg("iATU base_lo 0x90c", 0x90c);

	{
		u32 rev = 0;

		pci_read_config_dword(omo_dev, 0x08, &rev);
		pr_info("omo-inbound: PCI revision id = 0x%02x -> vendor %s path\n",
			rev & 0xff, (rev & 0xff) == 1 ?
			"config-space viewport (cfg 0x900)" :
			"membar (BAR2 iATU window)");
	}

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);
	ret = pci_enable_device(omo_dev);
	if (ret) {
		pr_err("omo-inbound: pci_enable_device rc=%d\n", ret);
		goto err_put;
	}
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("omo-inbound: pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
		cmd0, cmd1);

	ret = pci_request_mem_regions(omo_dev, "omo-inbound");
	if (ret) {
		pr_err("omo-inbound: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-inbound: pci_request_mem_regions rc=0 (MEM BARs claimed)\n");

	{
		u32 lo = 0, hi = 0, b2 = 0;

		/*
		 * pci_resource_start() reads the wrong struct offsets on this vendor
		 * kernel (the 5.10.201 layout delta phase 16 recorded: it returned 0
		 * for BAR0).  Take the base from config space instead.
		 */
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_2, &b2);
		omo_bar0_base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) |
				((u64)hi << 32);
		pr_info("omo-inbound: BAR0 base=0x%llx (config space), BAR2=0x%x (iatu_bar1)\n",
			(unsigned long long)omo_bar0_base,
			b2 & PCI_BASE_ADDRESS_MEM_MASK);
	}

	omo_bar0 = pci_iomap(omo_dev, OMO_BAR_NUM, 0);
	omo_iatu = pci_iomap(omo_dev, OMO_IATU_BAR, 0);
	if (!omo_bar0 || !omo_iatu) {
		pr_err("omo-inbound: pci_iomap FAILED (bar0=%p iatu=%p)\n",
		       omo_bar0, omo_iatu);
		ret = -ENOMEM;
		goto err_release;
	}

	/* Baseline. */
	omo_words("ROM vector page BAR0+0x0", omo_bar0, 0x0, 8);
	omo_words("before BAR0+0x40000", omo_bar0, omo_verify, 8);
	omo_words("before BAR0+0x6f8000", omo_bar0, omo_target, 8);
	omo_words("before iatu viewport0", omo_iatu, OMO_IATU_VP0, 8);
	before_tgt = omo_non_ff(omo_bar0, omo_target, 8);
	before_ver = omo_non_ff(omo_bar0, omo_verify, 8);
	pr_info("omo-inbound: baseline non-0xff: BAR0+0x%lx=%u/8  BAR0+0x%lx=%u/8\n",
		omo_target, before_tgt, omo_verify, before_ver);

	if (!omo_program) {
		pr_info("omo-inbound: program=0: iATU NOT programmed (read-only run)\n");
		goto out;
	}

	/* If the iATU window itself does not decode, programming is pointless. */
	if (ioread32(omo_iatu + OMO_IATU_CTRL2) == 0xffffffffU) {
		pr_err("omo-inbound: iatu_bar1 window reads 0xffffffff - undecoded, refusing (documented stop)\n");
		ret = -EIO;
		goto err_unmap;
	}

	pr_info("omo-inbound: programming six inbound viewports via BAR2 (vendor membar path)\n");
	{
		int n = omo_program_regions();

		pr_info("omo-inbound: programmed %d viewports\n", n);
	}

	/* The vendor's final init write (oal_pcie_set_inbound_by_viewport tail). */
	pci_write_config_word(omo_dev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_dev, PCI_COMMAND, &rb);
	pr_info("omo-inbound: cfg[0x004] <= 0x0007 readback=0x%04x MEM|MASTER=%s\n",
		rb, (rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		    (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	/* Decode probe. */
	omo_words("after  BAR0+0x6f8000", omo_bar0, omo_target, 8);
	omo_words("after  BAR0+0x40000", omo_bar0, omo_verify, 8);
	after_tgt = omo_non_ff(omo_bar0, omo_target, 8);
	after_ver = omo_non_ff(omo_bar0, omo_verify, 8);
	pr_info("omo-inbound: decode verdict BAR0+0x%lx: before=%u/8 after=%u/8 decoded=%s\n",
		omo_target, before_tgt, after_tgt, after_tgt > 0 ? "YES" : "NO");
	pr_info("omo-inbound: decode verdict BAR0+0x%lx: before=%u/8 after=%u/8 decoded=%s\n",
		omo_verify, before_ver, after_ver, after_ver > 0 ? "YES" : "NO");

	if (omo_mode == 2 && omo_dowrite) {
		ret = omo_load_fw();
		if (ret)
			goto err_unmap;
		if (omo_write_fw() != 0)
			pr_err("omo-inbound: firmware write refused/failed\n");
	}

out:
	pr_info("omo-inbound: done (mode=%u program=%u)\n", omo_mode, omo_program);
	return 0;

err_unmap:
	if (omo_iatu)
		pci_iounmap(omo_dev, omo_iatu);
	if (omo_bar0)
		pci_iounmap(omo_dev, omo_bar0);
err_release:
	pci_release_mem_regions(omo_dev);
err_disable:
	pci_disable_device(omo_dev);
err_put:
	pci_dev_put(omo_dev);
	omo_dev = NULL;
	return ret;
}

static void __exit omo_inbound_exit(void)
{
	if (omo_fw)
		vfree(omo_fw);
	if (omo_iatu)
		pci_iounmap(omo_dev, omo_iatu);
	if (omo_bar0)
		pci_iounmap(omo_dev, omo_bar0);
	if (omo_dev) {
		pci_release_mem_regions(omo_dev);
		pci_disable_device(omo_dev);
		pci_dev_put(omo_dev);
	}
	pr_info("omo-inbound: unloaded\n");
}

module_init(omo_inbound_init);
module_exit(omo_inbound_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Program the 59e7:0005 inbound iATU in the takeover boot (phase 18)");
