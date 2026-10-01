// SPDX-License-Identifier: GPL-2.0
/*
 * fwboot: after placing FIRMWARE.bin in the chip (phase 18's verified path),
 * OBSERVE the chip state that the vendor's bring-up says should change if the
 * firmware is running.  No release write is performed: the phase-19 disassembly
 * recovery (docs/phase19/fw-boot.md part A) found NO proven CPU-release/reset
 * register write in hi5622v100_plat.ko or hi5622v100_wifi.ko.  The vendor's
 * only post-download step is host-side (bal_irq_enable) followed by a wait for
 * the firmware's HCC "ready" messages.  So this module does exactly the
 * observation and stops.
 *
 * Steps (all previously proven, no new device writes):
 *   1. claim the endpoint, read config space, take BAR0 base from config space
 *   2. program the six inbound iATU viewports (BAR2, vendor revision-0 membar path)
 *   3. PCI_COMMAND = 7
 *   4. write FIRMWARE.bin to BAR0+0x6f8000 (device CA 0x01240000), read back
 *   5. SAMPLE the observation set `nobs` times, `obsdelay` ms apart, and report
 *      which words changed between samples (change over time = the chip is doing
 *      something; stability = the chip is still in ROM state)
 *
 * Module parameters:
 *   domain=N    endpoint domain (default 0)
 *   program=N   1 = program the six inbound viewports (default)
 *   mode=N      1 = observe after writing the firmware (default)
 *               0 = observe without writing the firmware
 *   fwpath=P    firmware path (mode=1)
 *   target=N    BAR0 offset the firmware is written to (default 0x6f8000)
 *   chunk=N     bytes per firmware chunk (default 0x80000, the vendor's)
 *   maxlen=N    limit bytes written (0 = whole file)
 *   obsdelay=N  ms between observation samples (default 3000)
 *   nobs=N      number of observation samples (default 3)
 *
 * Based on lab/inbound/inbound.c (phase 18) so the claim/decode/write path is
 * literally the one whose read-back was verified byte-for-byte.
 */

#include <linux/delay.h>
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
#define OMO_IATU_CTRL2	0x104UL

#define FW_TARGET_OFF	0x6f8000UL	/* device CA 0x01240000 */
#define FW_VERIFY_OFF	0x40000UL

struct omo_region {
	unsigned long off;
	u32 size;
	u64 target;
	const char *name;
};

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
static unsigned int omo_program = 1;
module_param_named(program, omo_program, uint, 0444);
static unsigned int omo_mode = 1;
module_param_named(mode, omo_mode, uint, 0444);
static char *omo_fwpath = "/lib/firmware/hi_wifi/FIRMWARE.bin";
module_param_named(fwpath, omo_fwpath, charp, 0444);
static unsigned long omo_target = FW_TARGET_OFF;
module_param_named(target, omo_target, ulong, 0444);
static unsigned int omo_chunk = 0x80000;
module_param_named(chunk, omo_chunk, uint, 0444);
static unsigned int omo_maxlen;
module_param_named(maxlen, omo_maxlen, uint, 0444);
static unsigned int omo_obsdelay = 3000;
module_param_named(obsdelay, omo_obsdelay, uint, 0444);
static unsigned int omo_nobs = 3;
module_param_named(nobs, omo_nobs, uint, 0444);

static struct pci_dev *omo_dev;
static void __iomem *omo_bar0;
static void __iomem *omo_iatu;
static u64 omo_bar0_base;
static u8 *omo_fw;
static size_t omo_fw_len;

/*
 * The observation set.  Every offset is a BAR0 offset reachable through the
 * six programmed regions; the CA column is the device address it decodes to,
 * named exactly as the disassembly names it.  Offsets are computed by the
 * region map: BAR0+0 == CA 0 (region 0), BAR0+0x3b8000 == CA 0x40000000
 * (region 3, "IO"), BAR0+0x6b8000 == CA 0x01200000 (region 5, ACP-fw).
 */
struct omo_obs {
	unsigned long off;	/* BAR0 offset */
	u32 ca;			/* device chip address */
	const char *what;
};

static const struct omo_obs omo_obs[] = {
	/* ROM/RAM code (region 0: BAR0+0 == CA 0) */
	{ 0x000000, 0x00000000, "ROM vector word0" },
	{ 0x000004, 0x00000004, "ROM vector word1" },
	{ 0x000008, 0x00000008, "ROM vector word2" },
	{ 0x00000c, 0x0000000c, "ROM vector word3" },
	/* region 5 alias of CA 0, then the firmware we placed (CA 0x1240000) */
	{ 0x6b8000, 0x01200000, "region5 alias of CA0 w0" },
	{ 0x6f8000, 0x01240000, "fw image word0" },
	{ 0x6f8004, 0x01240004, "fw image word1" },
	/* firmware RAM past the 928920-byte image: CA 0x1240000+0xE2C18 */
	{ 0x7dac18, 0x01322c18, "fw BSS/stack +0x00" },
	{ 0x7dac1c, 0x01322c1c, "fw BSS/stack +0x04" },
	{ 0x7dac20, 0x01322c20, "fw BSS/stack +0x08" },
	{ 0x7dac24, 0x01322c24, "fw BSS/stack +0x0c" },
	{ 0x8c7ff0, 0x01417ff0, "region5 top word" },
	/* HCC message / doorbell registers (region 3), from shuangta_pcie_msg_reg_map */
	{ 0x3f1010, 0x40039010, "msg out[0] H2D mask" },
	{ 0x3f1014, 0x40039014, "msg out[1]" },
	{ 0x3f12d0, 0x400392d0, "pcie0 status latch" },
	{ 0x3f12d4, 0x400392d4, "msg out[2] doorbell" },
	{ 0x3f12f0, 0x400392f0, "msg out[5]" },
	{ 0x4b9414, 0x40101414, "msg out[4] MAC-side" },
	{ 0x4b9438, 0x40101438, "msg out[3] MAC-side" },
	/* chip status registers read by dev_status_check (region 3) */
	{ 0x3b82a8, 0x400002a8, "efuse_chip_id" },
	{ 0x3b82d4, 0x400002d4, "dcoldo_efuse" },
	{ 0x3bd00c, 0x4000500c, "dcoldo_vset" },
	{ 0x3bd05c, 0x4000505c, "pbank_code" },
	{ 0x3bd060, 0x40005060, "abank_code" },
	{ 0x3fc110, 0x40004110, "temp" },
	{ 0x3fc208, 0x40004208, "lock_status" },
	{ 0x3f1224, 0x40039224, "pcie0_status" },
	{ 0x3f1220, 0x40039220, "pcie0 latch" },
	{ 0x4b9230, 0x40101230, "tcxo_pll_mux_sel" },
	{ 0x4b9234, 0x40101234, "tcxo_pll_status" },
};

#define OMO_NOBS_MAX 8
static u32 omo_sample[OMO_NOBS_MAX][ARRAY_SIZE(omo_obs)];

/* ---- iATU programming (vendor membar path, as inbound.c) --------------- */

static void omo_iatu_wr(unsigned long off, u32 val, const char *name)
{
	u32 rb;

	iowrite32(val, omo_iatu + off);
	rb = ioread32(omo_iatu + off);
	pr_info("omo-fwboot:   iatu[0x%03lx] <= 0x%08x readback=0x%08x match=%s  (%s)\n",
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

		pr_info("omo-fwboot: region %u %s: host 0x%llx..0x%llx -> dev 0x%llx size 0x%x\n",
			i, r->name, (unsigned long long)base,
			(unsigned long long)limit, (unsigned long long)r->target,
			r->size);

		scnprintf(nm, sizeof(nm), "r%u ctrl2=0", i);
		omo_iatu_wr(ctrl2, 0, nm);
		scnprintf(nm, sizeof(nm), "r%u ctrl2=ena", i);
		omo_iatu_wr(ctrl2, 0x80000000U, nm);
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

/* ---- firmware load + write (as verified in phase 18) ------------------- */

static int omo_load_fw(void)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t n;
	size_t done = 0;

	f = filp_open(omo_fwpath, O_RDONLY, 0);
	if (IS_ERR(f)) {
		pr_err("omo-fwboot: filp_open(%s) failed %ld\n",
		       omo_fwpath, PTR_ERR(f));
		return PTR_ERR(f);
	}
	omo_fw_len = i_size_read(file_inode(f));
	if (!omo_fw_len || omo_fw_len > 16UL * 1024 * 1024) {
		pr_err("omo-fwboot: bad firmware size %zu\n", omo_fw_len);
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
			pr_err("omo-fwboot: kernel_read stopped at %zu/%zu (n=%zd)\n",
			       done, omo_fw_len, n);
			filp_close(f, NULL);
			vfree(omo_fw);
			omo_fw = NULL;
			return n ? (int)n : -EIO;
		}
		done += n;
	}
	filp_close(f, NULL);
	pr_info("omo-fwboot: firmware file %s size=%zu bytes\n",
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
	pr_info("omo-fwboot: verify %s BAR0+0x%lx: file=%zu bytes diffs=%zu match=%s\n",
		name, b0, limit, diffs, diffs ? "NO" : "YES");
	if (first >= 0) {
		u32 w0 = ioread32(omo_bar0 + b0 + (first & ~3UL));

		pr_info("omo-fwboot:   first diff @0x%lx file=0x%02x chip=0x%02x\n",
			first, omo_fw[first], (u8)(w0 >> (8 * (first & 3))));
	}
}

static int omo_write_fw(void)
{
	size_t limit = omo_maxlen ? min_t(size_t, omo_maxlen, omo_fw_len) : omo_fw_len;
	size_t off = 0;
	u32 probe = 0xdeadbeef, rb;

	iowrite32(probe, omo_bar0 + omo_target);
	rb = ioread32(omo_bar0 + omo_target);
	pr_info("omo-fwboot: writability probe BAR0+0x%lx: wrote 0x%08x read 0x%08x match=%s\n",
		omo_target, probe, rb, rb == probe ? "YES" : "NO");
	if (rb != probe) {
		pr_err("omo-fwboot: target window does not hold a write - refusing the bulk write\n");
		return -EIO;
	}
	pr_info("omo-fwboot: download %zu bytes -> BAR0+0x%lx (device CA 0x01240000) in %u-byte chunks\n",
		limit, omo_target, omo_chunk);
	while (off < limit) {
		size_t n = min_t(size_t, omo_chunk, limit - off);

		memcpy_toio(omo_bar0 + omo_target + off, omo_fw + off, n);
		off += n;
		pr_info("omo-fwboot: wrote %zu/%zu bytes @ BAR0+0x%lx\n",
			off, limit, omo_target + off - n);
	}
	omo_verify_win("target", omo_target, limit);
	return 0;
}

/* ---- the observation --------------------------------------------------- */

static void omo_sample_once(unsigned int k)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(omo_obs); i++) {
		omo_sample[k][i] = ioread32(omo_bar0 + omo_obs[i].off);
		pr_info("omo-fwboot: sample%u %-24s CA=0x%08x BAR0+0x%06lx = 0x%08x\n",
			k + 1, omo_obs[i].what, omo_obs[i].ca,
			omo_obs[i].off, omo_sample[k][i]);
	}
}

static void omo_report_changes(void)
{
	unsigned int k, i;
	unsigned int changed_12 = 0, changed_23 = 0;

	for (i = 0; i < ARRAY_SIZE(omo_obs); i++) {
		u32 a = omo_sample[0][i];
		u32 b = omo_sample[1][i];
		u32 c = omo_sample[omo_nobs - 1][i];
		int ch12 = (a != b);
		int ch23 = (b != c);

		if (ch12)
			changed_12++;
		if (ch23)
			changed_23++;
		if (ch12 || ch23)
			pr_info("omo-fwboot: CHANGED %-24s CA=0x%08x  0x%08x -> 0x%08x -> 0x%08x\n",
				omo_obs[i].what, omo_obs[i].ca, a, b, c);
	}
	pr_info("omo-fwboot: change verdict over %u samples: s1->s2 changed=%u/%zu  s2->s%u changed=%u/%zu\n",
		omo_nobs, changed_12, ARRAY_SIZE(omo_obs),
		omo_nobs, changed_23, ARRAY_SIZE(omo_obs));
	for (k = 1; k < omo_nobs; k++)
		pr_info("omo-fwboot:   sample%u taken\n", k + 1);
}

/* ---- init / exit ------------------------------------------------------- */

static int __init omo_fwboot_init(void)
{
	u16 cmd0 = 0, cmd1 = 0, rb = 0;
	unsigned int before_tgt, after_tgt;
	unsigned int k;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-fwboot: endpoint %04x:00:00.0 not found\n", omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev, 0x00, &id);
		pr_info("omo-fwboot: endpoint 0000:00:00.0 id %04x:%04x\n",
			id & 0xffff, id >> 16);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("omo-fwboot: unexpected id\n");
	}

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);
	ret = pci_enable_device(omo_dev);
	if (ret) {
		pr_err("omo-fwboot: pci_enable_device rc=%d\n", ret);
		goto err_put;
	}
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("omo-fwboot: pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
		cmd0, cmd1);

	ret = pci_request_mem_regions(omo_dev, "omo-fwboot");
	if (ret) {
		pr_err("omo-fwboot: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-fwboot: pci_request_mem_regions rc=0 (MEM BARs claimed)\n");

	{
		u32 lo = 0, hi = 0, b2 = 0;

		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_2, &b2);
		omo_bar0_base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) |
				((u64)hi << 32);
		pr_info("omo-fwboot: BAR0 base=0x%llx (config space), BAR2=0x%x (iatu_bar1)\n",
			(unsigned long long)omo_bar0_base,
			b2 & PCI_BASE_ADDRESS_MEM_MASK);
	}

	omo_bar0 = pci_iomap(omo_dev, OMO_BAR_NUM, 0);
	omo_iatu = pci_iomap(omo_dev, OMO_IATU_BAR, 0);
	if (!omo_bar0 || !omo_iatu) {
		pr_err("omo-fwboot: pci_iomap FAILED (bar0=%p iatu=%p)\n",
		       omo_bar0, omo_iatu);
		ret = -ENOMEM;
		goto err_release;
	}

	pr_info("omo-fwboot: before iatu viewport0 [0x100] = 0x%08x\n",
		ioread32(omo_iatu + 0x100));
	before_tgt = ioread32(omo_bar0 + omo_target);
	pr_info("omo-fwboot: before BAR0+0x%lx = 0x%08x\n", omo_target, before_tgt);

	if (!omo_program) {
		pr_info("omo-fwboot: program=0: iATU NOT programmed (read-only run)\n");
		goto skip_program;
	}

	if (ioread32(omo_iatu + OMO_IATU_CTRL2) == 0xffffffffU) {
		pr_err("omo-fwboot: iatu_bar1 window reads 0xffffffff - undecoded, refusing\n");
		ret = -EIO;
		goto err_unmap;
	}

	pr_info("omo-fwboot: programming six inbound viewports via BAR2 (vendor membar path)\n");
	pr_info("omo-fwboot: programmed %d viewports\n", omo_program_regions());

	/* The vendor's final init write (oal_pcie_set_inbound_by_viewport tail). */
	pci_write_config_word(omo_dev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_dev, PCI_COMMAND, &rb);
	pr_info("omo-fwboot: cfg[0x004] <= 0x0007 readback=0x%04x MEM|MASTER=%s\n",
		rb, (rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		    (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	after_tgt = ioread32(omo_bar0 + omo_target);
	pr_info("omo-fwboot: decode verdict BAR0+0x%lx: before=0x%08x after=0x%08x decoded=%s\n",
		omo_target, before_tgt, after_tgt,
		after_tgt != 0xffffffffU ? "YES" : "NO");

skip_program:
	if (omo_mode == 1) {
		ret = omo_load_fw();
		if (ret)
			goto err_unmap;
		if (omo_write_fw() != 0) {
			pr_err("omo-fwboot: firmware write refused/failed - stopping\n");
			goto err_unmap;
		}
	} else {
		pr_info("omo-fwboot: mode=0: firmware NOT written (observation only)\n");
	}

	/*
	 * Observe.  No release/handshake write is performed: the disassembly
	 * recovery found none that is proven (part A of the report).  The chip's
	 * own state is sampled `nobs` times, `obsdelay` ms apart, so a state that
	 * changes over time (the firmware executing) is distinguishable from a
	 * state that is frozen (still in ROM).
	 */
	if (omo_nobs < 2)
		omo_nobs = 2;
	if (omo_nobs > OMO_NOBS_MAX)
		omo_nobs = OMO_NOBS_MAX;

	pr_info("omo-fwboot: observing %zu registers, %u samples, %u ms apart (no release write)\n",
		ARRAY_SIZE(omo_obs), omo_nobs, omo_obsdelay);
	for (k = 0; k < omo_nobs; k++) {
		omo_sample_once(k);
		if (k + 1 < omo_nobs)
			msleep(omo_obsdelay);
	}
	omo_report_changes();

	pr_info("omo-fwboot: done (mode=%u program=%u nobs=%u obsdelay=%u)\n",
		omo_mode, omo_program, omo_nobs, omo_obsdelay);
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

static void __exit omo_fwboot_exit(void)
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
	pr_info("omo-fwboot: unloaded\n");
}

module_init(omo_fwboot_init);
module_exit(omo_fwboot_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Place the firmware and observe the chip state (phase 19)");
