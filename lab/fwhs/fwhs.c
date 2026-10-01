// SPDX-License-Identifier: GPL-2.0
/*
 * fwhs: firmware handshake listener (phase 19c).
 *
 * After the phase-19b result (writing 0x5a5a to device CA 0x40000108 boots the
 * chip: BSS zeroed, analog/status regs live, HCC mailbox out[1] 0 -> 4), this
 * module reproduces the proven claim/decode/firmware-write path, performs the
 * (now proven) release write, and then READS the chip's mailbox for at least
 * ten seconds, logging every change with a timestamp.
 *
 * Disassembly finding this module is built around (docs/phase19/fwhs.md part A):
 * the vendor's post-download handshake is two device->host HCC messages whose
 * handlers are registered in plat.ko's chip table (plat_init_bal_hcc_excp ->
 * hcc_msg_register_tab_chip(chip=4, table=.data+0x2660, count=5)):
 *
 *   id 1  device_plat_ready_msg_process  "Device plat ready! chip id : %d"
 *                                       completes LANCHOR0+0x234 (200-jiffy wait)
 *   id 2  host_ready_msg_process         "DEVICE READY"
 *                                       copies payload to g_dmac_to_hmac_read_msg
 *                                       completes LANCHOR0+0x220 (2000-jiffy wait)
 *
 * The host answers by ENABLING the endpoint IRQ (bal_irq_enable -> enable_irq,
 * host-side, no device register) and then reading chip status (dev_status_check)
 * between the two waits.  No host mailbox write is proven in the ready-handshake
 * path, so this module performs NO handshake write; it only reads.  The single
 * device write beyond the phase-18 set is the release magic 0x5a5a.
 *
 * Module parameters:
 *   domain=N    endpoint domain (default 0)
 *   program=N   1 = program the six inbound viewports (default)
 *   fwpath=P    firmware path
 *   target=N    BAR0 offset the firmware is written to (default 0x6f8000)
 *   chunk=N     bytes per firmware chunk (default 0x80000)
 *   maxlen=N    limit bytes written (0 = whole file)
 *   release=N   1 = perform the 0x5a5a release write (default)
 *   pollms=N    ms between mailbox polls (default 500)
 *   polldur=N   total ms to poll (default 15000, enforced >= 10000)
 *   scanbase=N  BAR0 offset of the firmware-RAM change scan (default 0x7d8000)
 *   scanlen=N   bytes of the change scan (default 0x60000, 0 = off)
 */

#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/ktime.h>
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
#define RELEASE_OFF	0x3b8108UL	/* device CA 0x40000108 (region 3) */
#define RELEASE_VAL	0x00005a5aU

#define MAILBOX_N	6
#define STATUS_N	12
#define SCAN_CHANGES_MAX 24

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
static char *omo_fwpath = "/lib/firmware/hi_wifi/FIRMWARE.bin";
module_param_named(fwpath, omo_fwpath, charp, 0444);
static unsigned long omo_target = FW_TARGET_OFF;
module_param_named(target, omo_target, ulong, 0444);
static unsigned int omo_chunk = 0x80000;
module_param_named(chunk, omo_chunk, uint, 0444);
static unsigned int omo_maxlen;
module_param_named(maxlen, omo_maxlen, uint, 0444);
static unsigned int omo_release = 1;
module_param_named(release, omo_release, uint, 0444);
static unsigned int omo_pollms = 500;
module_param_named(pollms, omo_pollms, uint, 0444);
static unsigned int omo_polldur = 15000;
module_param_named(polldur, omo_polldur, uint, 0444);
static unsigned long omo_scanbase = 0x7d8000UL;
module_param_named(scanbase, omo_scanbase, ulong, 0444);
static unsigned int omo_scanlen = 0x60000;
module_param_named(scanlen, omo_scanlen, uint, 0444);

static struct pci_dev *omo_dev;
static void __iomem *omo_bar0;
static void __iomem *omo_iatu;
static u64 omo_bar0_base;
static u8 *omo_fw;
static size_t omo_fw_len;
static u8 *omo_scan_base;

/* The six HCC message/mailbox registers, from shuangta_pcie_msg_reg_map. */
struct omo_mbox {
	unsigned long off;
	u32 ca;
	const char *what;
};

static const struct omo_mbox omo_mbox[MAILBOX_N] = {
	{ 0x3f1010, 0x40039010, "out[0] H2D mask" },
	{ 0x3f1014, 0x40039014, "out[1]" },
	{ 0x3f12d4, 0x400392d4, "out[2] doorbell" },
	{ 0x3f12f0, 0x400392f0, "out[5]" },
	{ 0x4b9414, 0x40101414, "out[4] MAC-side" },
	{ 0x4b9438, 0x40101438, "out[3] MAC-side" },
};

/* Chip status registers read by dev_status_check. */
struct omo_stat {
	unsigned long off;
	u32 ca;
	const char *what;
};

static const struct omo_stat omo_stat[STATUS_N] = {
	{ 0x3b82a8, 0x400002a8, "efuse_chip_id" },
	{ 0x3bd00c, 0x4000500c, "dcoldo_vset" },
	{ 0x3bd05c, 0x4000505c, "pbank_code" },
	{ 0x3bd060, 0x40005060, "abank_code" },
	{ 0x3f1224, 0x40039224, "pcie0_status" },
	{ 0x3f1220, 0x40039220, "pcie0 latch" },
	{ 0x4b9230, 0x40101230, "tcxo_pll_mux_sel" },
	{ 0x4b9234, 0x40101234, "tcxo_pll_status" },
	{ 0x7dac18, 0x01322c18, "fw BSS +0x00" },
	{ 0x7dac1c, 0x01322c1c, "fw BSS +0x04" },
	{ 0x8c7ff0, 0x01417ff0, "region5 top word" },
	{ 0x6f8000, 0x01240000, "fw image word0" },
};

static u32 omo_mbox_last[MAILBOX_N];
static u32 omo_stat_last[STATUS_N];
static u64 omo_t0;

static u64 omo_now_ns(void)
{
	return ktime_get_ns();
}

/* ---- iATU programming (vendor membar path, as inbound.c/fwboot.c) ------ */

static void omo_iatu_wr(unsigned long off, u32 val, const char *name)
{
	u32 rb;

	iowrite32(val, omo_iatu + off);
	rb = ioread32(omo_iatu + off);
	pr_info("omo-fwhs:   iatu[0x%03lx] <= 0x%08x readback=0x%08x match=%s  (%s)\n",
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

		pr_info("omo-fwhs: region %u %s: host 0x%llx..0x%llx -> dev 0x%llx size 0x%x\n",
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

/* ---- firmware load + write (phase-18 verified path) ------------------- */

static int omo_load_fw(void)
{
	struct file *f;
	loff_t pos = 0;
	ssize_t n;
	size_t done = 0;

	f = filp_open(omo_fwpath, O_RDONLY, 0);
	if (IS_ERR(f)) {
		pr_err("omo-fwhs: filp_open(%s) failed %ld\n",
		       omo_fwpath, PTR_ERR(f));
		return PTR_ERR(f);
	}
	omo_fw_len = i_size_read(file_inode(f));
	if (!omo_fw_len || omo_fw_len > 16UL * 1024 * 1024) {
		pr_err("omo-fwhs: bad firmware size %zu\n", omo_fw_len);
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
			pr_err("omo-fwhs: kernel_read stopped at %zu/%zu (n=%zd)\n",
			       done, omo_fw_len, n);
			filp_close(f, NULL);
			vfree(omo_fw);
			omo_fw = NULL;
			return n ? (int)n : -EIO;
		}
		done += n;
	}
	filp_close(f, NULL);
	pr_info("omo-fwhs: firmware file %s size=%zu bytes\n",
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
	pr_info("omo-fwhs: verify %s BAR0+0x%lx: file=%zu bytes diffs=%zu match=%s\n",
		name, b0, limit, diffs, diffs ? "NO" : "YES");
	if (first >= 0) {
		u32 w0 = ioread32(omo_bar0 + b0 + (first & ~3UL));

		pr_info("omo-fwhs:   first diff @0x%lx file=0x%02x chip=0x%02x\n",
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
	pr_info("omo-fwhs: writability probe BAR0+0x%lx: wrote 0x%08x read 0x%08x match=%s\n",
		omo_target, probe, rb, rb == probe ? "YES" : "NO");
	if (rb != probe) {
		pr_err("omo-fwhs: target window does not hold a write - refusing the bulk write\n");
		return -EIO;
	}
	pr_info("omo-fwhs: download %zu bytes -> BAR0+0x%lx (device CA 0x01240000) in %u-byte chunks\n",
		limit, omo_target, omo_chunk);
	while (off < limit) {
		size_t n = min_t(size_t, omo_chunk, limit - off);

		memcpy_toio(omo_bar0 + omo_target + off, omo_fw + off, n);
		off += n;
		pr_info("omo-fwhs: wrote %zu/%zu bytes @ BAR0+0x%lx\n",
			off, limit, omo_target + off - n);
	}
	omo_verify_win("target", omo_target, limit);
	return 0;
}

/* ---- release + polling ------------------------------------------------- */

static void omo_decode_mailbox(const char *what, u32 v)
{
	/* The message registers carry pending-message bitmaps: bit N = message N
	 * (pcie_msg_send_irq writes 8 = bit 3 for pcie_ete_transfer_done_handle,
	 * id 3).  Report which bits are set.
	 */
	char bits[80];
	int i, n = 0;

	bits[0] = '\0';
	for (i = 0; i < 16 && n < (int)sizeof(bits) - 8; i++) {
		if (v & (1U << i))
			n += scnprintf(bits + n, sizeof(bits) - n, "%s%d", n ? "," : "", i);
	}
	pr_info("omo-fwhs:   %-18s = 0x%08x  bits={%s}\n", what, v, bits[0] ? bits : "-");
}

static void omo_scan_changes(const char *tag, u64 t)
{
	unsigned long off;
	unsigned int changed = 0, shown = 0;

	if (!omo_scan_base || !omo_scanlen)
		return;
	for (off = 0; off + 4 <= omo_scanlen; off += 4) {
		u32 w = ioread32(omo_bar0 + omo_scanbase + off);

		if (memcmp(&w, omo_scan_base + off, 4) != 0) {
			if (shown < SCAN_CHANGES_MAX) {
				u32 old;

				memcpy(&old, omo_scan_base + off, 4);
				pr_info("omo-fwhs: [%s +%llums] SCAN CA=0x%08x BAR0+0x%lx 0x%08x -> 0x%08x\n",
					tag, (unsigned long long)((t - omo_t0) / 1000000ULL),
					0x01320000U + (u32)off, omo_scanbase + off, old, w);
				shown++;
			}
			changed++;
		}
	}
	if (changed)
		pr_info("omo-fwhs: [%s +%llums] scan window changed %u/%u words (shown %u)\n",
			tag, (unsigned long long)((t - omo_t0) / 1000000ULL),
			changed, omo_scanlen / 4, shown);
}

static void omo_poll_mailbox(const char *tag, u64 t, unsigned int first)
{
	unsigned int i;
	u64 ms = (t - omo_t0) / 1000000ULL;

	for (i = 0; i < MAILBOX_N; i++) {
		u32 v = ioread32(omo_bar0 + omo_mbox[i].off);

		if (v != omo_mbox_last[i] || first) {
			pr_info("omo-fwhs: [%s +%llums] MBOX %-18s CA=0x%08x 0x%08x -> 0x%08x\n",
				tag, (unsigned long long)ms, omo_mbox[i].what,
				omo_mbox[i].ca, omo_mbox_last[i], v);
			omo_decode_mailbox(omo_mbox[i].what, v);
			omo_mbox_last[i] = v;
		}
	}
	for (i = 0; i < STATUS_N; i++) {
		u32 v = ioread32(omo_bar0 + omo_stat[i].off);

		if (v != omo_stat_last[i]) {
			pr_info("omo-fwhs: [%s +%llums] STAT %-18s CA=0x%08x 0x%08x -> 0x%08x\n",
				tag, (unsigned long long)ms, omo_stat[i].what,
				omo_stat[i].ca, omo_stat_last[i], v);
			omo_stat_last[i] = v;
		}
	}
}

static void omo_snapshot_all(void)
{
	unsigned int i;

	for (i = 0; i < MAILBOX_N; i++)
		omo_mbox_last[i] = ioread32(omo_bar0 + omo_mbox[i].off);
	for (i = 0; i < STATUS_N; i++)
		omo_stat_last[i] = ioread32(omo_bar0 + omo_stat[i].off);
	if (omo_scan_base && omo_scanlen) {
		unsigned long off;

		for (off = 0; off + 4 <= omo_scanlen; off += 4) {
			u32 w = ioread32(omo_bar0 + omo_scanbase + off);

			memcpy(omo_scan_base + off, &w, 4);
		}
	}
}

static int omo_release(void)
{
	u32 rb;

	pr_info("omo-fwhs: RELEASE write CA 0x40000108 <- 0x%08x (BAR0+0x%lx)\n",
		RELEASE_VAL, RELEASE_OFF);
	iowrite32(RELEASE_VAL, omo_bar0 + RELEASE_OFF);
	rb = ioread32(omo_bar0 + RELEASE_OFF);
	pr_info("omo-fwhs: release readback = 0x%08x\n", rb);
	return rb == RELEASE_VAL ? 0 : -EIO;
}

/* ---- init / exit ------------------------------------------------------- */

static int __init omo_fwhs_init(void)
{
	u16 cmd0 = 0, cmd1 = 0, rb = 0;
	unsigned int after_tgt;
	unsigned int polls, k;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-fwhs: endpoint %04x:00:00.0 not found\n", omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev, 0x00, &id);
		pr_info("omo-fwhs: endpoint 0000:00:00.0 id %04x:%04x\n",
			id & 0xffff, id >> 16);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("omo-fwhs: unexpected id\n");
	}

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);
	ret = pci_enable_device(omo_dev);
	if (ret) {
		pr_err("omo-fwhs: pci_enable_device rc=%d\n", ret);
		goto err_put;
	}
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("omo-fwhs: pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
		cmd0, cmd1);

	ret = pci_request_mem_regions(omo_dev, "omo-fwhs");
	if (ret) {
		pr_err("omo-fwhs: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-fwhs: pci_request_mem_regions rc=0 (MEM BARs claimed)\n");

	{
		u32 lo = 0, hi = 0, b2 = 0;

		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_2, &b2);
		omo_bar0_base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) |
				((u64)hi << 32);
		pr_info("omo-fwhs: BAR0 base=0x%llx (config space), BAR2=0x%x (iatu_bar1)\n",
			(unsigned long long)omo_bar0_base,
			b2 & PCI_BASE_ADDRESS_MEM_MASK);
	}

	omo_bar0 = pci_iomap(omo_dev, OMO_BAR_NUM, 0);
	omo_iatu = pci_iomap(omo_dev, OMO_IATU_BAR, 0);
	if (!omo_bar0 || !omo_iatu) {
		pr_err("omo-fwhs: pci_iomap FAILED (bar0=%p iatu=%p)\n",
		       omo_bar0, omo_iatu);
		ret = -ENOMEM;
		goto err_release;
	}

	if (omo_scanlen) {
		omo_scan_base = vmalloc(omo_scanlen);
		if (!omo_scan_base) {
			ret = -ENOMEM;
			goto err_unmap;
		}
	}

	pr_info("omo-fwhs: before iatu viewport0 [0x100] = 0x%08x\n",
		ioread32(omo_iatu + 0x100));
	if (!omo_program) {
		pr_info("omo-fwhs: program=0: iATU NOT programmed (read-only run)\n");
		goto skip_program;
	}

	if (ioread32(omo_iatu + OMO_IATU_CTRL2) == 0xffffffffU) {
		pr_err("omo-fwhs: iatu_bar1 window reads 0xffffffff - undecoded, refusing\n");
		ret = -EIO;
		goto err_unmap;
	}

	pr_info("omo-fwhs: programming six inbound viewports via BAR2 (vendor membar path)\n");
	pr_info("omo-fwhs: programmed %d viewports\n", omo_program_regions());

	pci_write_config_word(omo_dev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_dev, PCI_COMMAND, &rb);
	pr_info("omo-fwhs: cfg[0x004] <= 0x0007 readback=0x%04x MEM|MASTER=%s\n",
		rb, (rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		    (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	after_tgt = ioread32(omo_bar0 + omo_target);
	pr_info("omo-fwhs: decode verdict BAR0+0x%lx: after=0x%08x decoded=%s\n",
		omo_target, after_tgt,
		after_tgt != 0xffffffffU ? "YES" : "NO");

skip_program:
	ret = omo_load_fw();
	if (ret)
		goto err_unmap;
	if (omo_write_fw() != 0) {
		pr_err("omo-fwhs: firmware write refused/failed - stopping\n");
		goto err_unmap;
	}

	/* Pre-release baseline of every watched register and the scan window. */
	omo_t0 = omo_now_ns();
	omo_snapshot_all();
	pr_info("omo-fwhs: pre-release baseline taken (t0)\n");
	omo_poll_mailbox("pre", omo_now_ns(), 1);

	if (!omo_release) {
		pr_info("omo-fwhs: release=0: NOT releasing the chip\n");
		goto done;
	}
	if (omo_release()) {
		pr_err("omo-fwhs: release readback mismatch - stopping\n");
		goto err_unmap;
	}

	/* Immediate post-release sample, then the timed poll. */
	omo_poll_mailbox("post0", omo_now_ns(), 0);
	omo_scan_changes("post0", omo_now_ns());

	if (omo_polldur < 10000)
		omo_polldur = 10000;
	if (omo_pollms < 20)
		omo_pollms = 20;
	polls = omo_polldur / omo_pollms;
	pr_info("omo-fwhs: polling %u mailbox regs + %u status regs + %u-byte scan window for %u ms (%u polls, %u ms apart)\n",
		MAILBOX_N, STATUS_N, omo_scanlen, omo_polldur, polls, omo_pollms);

	for (k = 0; k < polls; k++) {
		msleep(omo_pollms);
		omo_poll_mailbox("poll", omo_now_ns(), 0);
		if ((k & 1) == 1)
			omo_scan_changes("poll", omo_now_ns());
	}
	omo_scan_changes("final", omo_now_ns());
	pr_info("omo-fwhs: poll complete after %u ms\n", omo_polldur);

done:
	pr_info("omo-fwhs: done (release=%u pollms=%u polldur=%u scanlen=%u)\n",
		omo_release, omo_pollms, omo_polldur, omo_scanlen);
	return 0;

err_unmap:
	if (omo_scan_base)
		vfree(omo_scan_base);
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

static void __exit omo_fwhs_exit(void)
{
	if (omo_fw)
		vfree(omo_fw);
	if (omo_scan_base)
		vfree(omo_scan_base);
	if (omo_iatu)
		pci_iounmap(omo_dev, omo_iatu);
	if (omo_bar0)
		pci_iounmap(omo_dev, omo_bar0);
	if (omo_dev) {
		pci_release_mem_regions(omo_dev);
		pci_disable_device(omo_dev);
		pci_dev_put(omo_dev);
	}
	pr_info("omo-fwhs: unloaded\n");
}

module_init(omo_fwhs_init);
module_exit(omo_fwhs_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Release the firmware and log its handshake mailbox (phase 19c)");
