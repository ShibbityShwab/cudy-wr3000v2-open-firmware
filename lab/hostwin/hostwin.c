// SPDX-License-Identifier: GPL-2.0
/*
 * hostwin: make host memory device-visible + route the interrupt (phase 20c).
 *
 * Direct successor to lab/rtmsg.  rtmsg proved that with the six inbound iATU
 * viewports and the ETE SR/DR rings programmed the released firmware emits a
 * new PCIe word (out[1] = 0x40, bit 6 = pcie_trigger_ete_sending_handle) but no
 * payload ever lands in a ring we own, because the ring base is a HOST address
 * the device cannot reach: the endpoint's *outbound* iATU viewport (BAR2
 * offset 0x000) - the one that maps a device-visible address onto the host
 * window - was never programmed.  This module adds exactly that step, following
 * docs/phase20/host-window.md Part A:
 *
 *   hostca -> devva (pcie_hostca_to_devva @0xaefc):
 *       devva = win->devva_base + hostca - win->hostca_base
 *     where win = chip->[4]->[0xc4], a per-chiptype static descriptor
 *     (.data+0x1fb8 for chiptype 0) holding
 *       +0x00 devva_base  = 0x80000000   (+0x08 devva_end  = 0xffffffff)
 *       +0x10 hostca_base = 0x80000000   (so devva == hostca here)
 *
 *   outbound viewport (oal_pcie_set_outbound_by_membar, inlined in
 *     oal_pcie_set_inbound @0x9a38..0x9af4): at BAR2+0x000, in order
 *       ctrl1=0, ctrl2=0x80000000|bar, base=win->devva_base,
 *       base_hi, limit=win->devva_end, target=win->hostca_base, target_hi
 *     Live vendor boot reads: base 0x80000000, limit 0xffffffff, target
 *     0x80000000.
 *
 * This module keeps the rtmsg message context, the ETE SR/DR rings and the
 * IRQ request, but: (1) programs the outbound viewport so the endpoint can
 * reach the host window, (2) converts every ring/descriptor host address with
 * the recovered hostca->devva formula before writing it to the device, and
 * (3) writes the vendor's observed PCI_INTERRUPT_LINE (0xcf = 207) into config
 * space when a takeover boot left it at 0xff - the one unproven-but-quoted
 * write - then requests that line.
 *
 * The phase-20a module (lab/msgd) proved that a boot-time takeover can claim the
 * endpoint, program the six inbound iATU viewports, load FIRMWARE.bin, release
 * the chip (0x5a5a -> CA 0x40000108) and observe exactly one mailbox word
 * (out[1], CA 0x40039014, 0 -> 4).  It could not service that word: the vendor
 * pipeline needs the *runtime message context* and the *ETE SR/DR rings* that
 * pcie_msg_init @0xb6e4 and pcie_ete_init @0x7820 build, plus an interrupt line.
 *
 * This module stands those up following docs/phase20/runtime-msg.md Part A:
 *
 *   message context (pcie_msg_init):  six mailbox CAs -> reg[0..5], an
 *     11-entry {fn,arg} handler table (kmalloc 0x58), the per-chip dispatch
 *     binding chip[i]+0x60..0x6c (pcie_msg_send_irq / ctx / pcie_msg_handle /
 *     ctx+0x2c), and the pcie_msg_handle contract: pending = *(ctx+4),
 *     ack = *(ctx+0xc) = 1, re-arm = *(ctx+0x10) = 1, dispatch lowest set bit
 *     of the pending mask through table[bit] (ctx+0x20).
 *
 *   ETE rings (pcie_ete_init / pcie_ete_init_src_ring / _dst_ring):
 *     3 SR channels {0x400,0x450,0x4a0} stride 0x114, 4 DR channels
 *     {0x590,0x5e0,0x630,0x680} stride 0x6c, depth 32, node = 8 bytes
 *     {u32 buffer address; u32 (len<<16)|flags}.  SR node array (depth+2)*8,
 *     DR node array depth*8, both coherent DMA.  Program order is base,
 *     depth-1, wptr, ctrl (SR) / base, depth-1, wptr (DR), then the +0x2e8
 *     read-modify-write (& 0xfffffc20) in pcie_ete_chn_res @0x7490.
 *
 *   IRQ: read PCI_INTERRUPT_LINE and request_irq(irq, IRQF_SHARED) exactly as
 *     do_request_irq @0x1081c.  The handler is deliberately defensive: it
 *     returns IRQ_NONE unless a watched register changed, and disables the
 *     (shared, level) line after the first hit so an un-cleared source cannot
 *     storm.  The ISR's job in the vendor is to clear the PCIe glue status
 *     (+0x2ec & 0x3d8, oal_pcie_transfer_done @0x83e4 / pcie_intr_handle
 *     @0x82e4) and then run pcie_msg_handle; we cannot reach that glue from the
 *     takeover, so we do not pretend to.
 *
 * Write policy: every device write is quoted from the disassembly or a live
 * vendor read.  The iATU viewports (six inbound + the one outbound), PCI_COMMAND,
 * the firmware, the 0x5a5a release and every SR/DR program register are quoted.
 * The ETE ring/descriptor addresses are now converted with the recovered
 * hostca->devva formula rather than written raw; the window values themselves
 * are quoted from the vendor's live iATU and from .data+0x1fb8.  The single
 * unproven-but-quoted write is the PCI_INTERRUPT_LINE byte (0xcf = 207) that a
 * vendor boot has and a takeover boot leaves at 0xff - no cfg 0x3c store exists
 * in plat.ko, so *how* it is set is inferred, only the value is measured.
 * There is no host->device message reply: the vendor handshake has none.
 *
 * Module parameters:
 *   domain=N   endpoint domain (default 0)
 *   program=N  1 = program the six inbound iATU viewports (default)
 *   fwpath=P   firmware path
 *   target=N   BAR0 offset the firmware is written to (default 0x6f8000)
 *   chunk=N    bytes per firmware chunk (default 0x80000)
 *   maxlen=N   limit bytes written (0 = whole file)
 *   release=N  1 = perform the 0x5a5a release write (default)
 *   irq=N      IRQ to request; 0 = read PCI_INTERRUPT_LINE (default 0)
 *   useirq=N   1 = request_irq the endpoint line (default)
 *   rings=N    1 = allocate + program the ETE SR/DR rings (default)
 *   acpoff=N   added to the coherent DMA address for the ETE ring base
 *              (device-VA window compensation; 0 = write the DMA address as-is)
 *   pollms=N   ms between polls (default 500)
 *   polldur=N  total ms to poll (default 25000)
 *   scanbase=N BAR0 offset of the firmware-RAM change scan (default 0x7d8000)
 *   scanlen=N  bytes of the change scan (default 0x60000, 0 = off)
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
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
#define RELEASE_OFF	0x3b8108UL	/* device CA 0x40000108 (region 3) */
#define RELEASE_VAL	0x00005a5aU

#define MAILBOX_N	6
#define MSG_HANDLER_N	11
#define STATUS_N	12
#define SCAN_CHANGES_MAX 24
#define ISR_LOG_MAX	8

/* ETE register block (docs/phase17/ete-init.md A.1): static .data+0x2944.
 *
 * Device CA 0x4003a000 is reached through the region-3 viewport (host
 * 0x403b8000 -> dev CA 0x40000000), so its BAR0 offset is
 * 0x3b8000 + (0x4003a000 - 0x40000000) = 0x3f2000.  The phase-17 text's
 * "BAR0+0x3a000" conflated the device CA with a flat BAR0 offset. */
#define ETE_BAR0_OFF	0x3f2000UL
#define ETE_WIN_LEN	0x1000UL
#define ETE_CHN_RES	0x2e8
#define ETE_CHN_RES_MASK 0xfffffc20U
#define ETE_SR_CTRL	0x08
#define ETE_SR_BASE	0x10
#define ETE_SR_DEPTH	0x14
#define ETE_SR_WPTR	0x18
#define ETE_DR_BASE	0x30
#define ETE_DR_DEPTH	0x34
#define ETE_DR_WPTR	0x38
#define ETE_DEPTH	32
#define ETE_SR_N	3
#define ETE_DR_N	4
#define ETE_DR_PAYLOAD	2048

static const unsigned long omo_sr_block[ETE_SR_N] = { 0x400, 0x450, 0x4a0 };
static const unsigned long omo_dr_block[ETE_DR_N] = { 0x590, 0x5e0, 0x630, 0x680 };

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
static int omo_irq;
module_param_named(irq, omo_irq, int, 0444);
static unsigned int omo_useirq = 1;
module_param_named(useirq, omo_useirq, uint, 0444);
static unsigned int omo_rings = 1;
module_param_named(rings, omo_rings, uint, 0444);
static int omo_acpoff;
module_param_named(acpoff, omo_acpoff, int, 0444);
static unsigned int omo_outwin = 1;
module_param_named(outwin, omo_outwin, uint, 0444);
static unsigned int omo_devva_base = 0x80000000U;
module_param_named(devvabase, omo_devva_base, uint, 0444);
static unsigned int omo_devva_end = 0xffffffffU;
module_param_named(devvaend, omo_devva_end, uint, 0444);
static unsigned int omo_hostca_base = 0x80000000U;
module_param_named(hostcabase, omo_hostca_base, uint, 0444);
static unsigned int omo_hostirq = 207;
module_param_named(hostirq, omo_hostirq, uint, 0444);
static unsigned int omo_pollms = 500;
module_param_named(pollms, omo_pollms, uint, 0444);
static unsigned int omo_polldur = 25000;
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

/* The six HCC message/mailbox registers, from shuangta_pcie_msg_reg_map @0x1b1a0.
 * slot order is the vendor's out[0..5] = msg ctx +0x2c..+0x40. */
struct omo_mbox {
	unsigned long off;	/* BAR0 offset */
	u32 ca;
	const char *what;
};

static const struct omo_mbox omo_mbox[MAILBOX_N] = {
	{ 0x3f1010, 0x40039010, "out[0] H2D mask" },
	{ 0x3f1014, 0x40039014, "out[1] msg1" },
	{ 0x3f12d4, 0x400392d4, "out[2] doorbell" },
	{ 0x3f12f0, 0x400392f0, "out[5] msg5" },
	{ 0x4b9414, 0x40101414, "out[4] MAC-side" },
	{ 0x4b9438, 0x40101438, "out[3] MAC-side" },
};

static const struct omo_stat {
	unsigned long off;
	u32 ca;
	const char *what;
} omo_stat[STATUS_N] = {
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

/* ---- the runtime message context (pcie_msg_init @0xb6e4) ---------------- */
struct omo_msg_handler {
	void (*fn)(void *);
	void *arg;
};

struct omo_msgctx {
	void __iomem *reg[MAILBOX_N];	/* ctx+0x2c..+0x40: the six CAs      */
	void __iomem *pending;		/* ctx+0x04: *(ctx+4) != 0 = pending */
	void __iomem *ack;		/* ctx+0x0c: write 1 to ack          */
	void __iomem *rearm;		/* ctx+0x10: write 1 to re-arm       */
	struct omo_msg_handler *table;	/* ctx+0x20: 11 x {fn,arg}           */
};

static struct omo_msgctx omo_ctx;
static unsigned int omo_ctx_ready;

/* ---- ETE ring state ----------------------------------------------------- */
static void *omo_sr_va[ETE_SR_N];
static dma_addr_t omo_sr_dma[ETE_SR_N];
static void *omo_dr_va[ETE_DR_N];
static dma_addr_t omo_dr_dma[ETE_DR_N];
static void *omo_dr_pay[ETE_DR_N];
static dma_addr_t omo_dr_pay_dma[ETE_DR_N];
static u8 *omo_dr_snap[ETE_DR_N];
static u8 *omo_pay_snap[ETE_DR_N];
static unsigned int omo_ete_ready;

/* ---- IRQ state ---------------------------------------------------------- */
static int omo_irq_num = -1;
static unsigned int omo_irq_ok;
static atomic_t omo_irq_count = ATOMIC_INIT(0);
static atomic_t omo_irq_hits = ATOMIC_INIT(0);
static unsigned int omo_isr_logs;
static unsigned int omo_irq_disabled;
static u32 omo_isr_seen[MAILBOX_N];

static u32 omo_mbox_last[MAILBOX_N];
static u32 omo_stat_last[STATUS_N];
static unsigned long omo_t0;
static unsigned int omo_msgs;

static unsigned long omo_ms_now(void)
{
	return jiffies_to_msecs(jiffies - omo_t0);
}

static const char *omo_msgid_name(int bit)
{
	switch (bit) {
	case 1: return "pcie_dev_ready_msg_handle(stub)";
	case 3: return "pcie_ete_transfer_done_handle";
	case 5: return "pcie_ete_rcv_reclaim(send id5)";
	case 6: return "pcie_trigger_ete_sending_handle";
	case 7: return "pcie_trigger_ete_sending_handle";
	default: return "unregistered";
	}
}

/* ---- iATU programming (vendor membar path, as inbound.c/fwboot.c) ------ */

static void omo_iatu_wr(unsigned long off, u32 val, const char *name)
{
	u32 rb;

	iowrite32(val, omo_iatu + off);
	rb = ioread32(omo_iatu + off);
	pr_info("omo-hostwin:   iatu[0x%03lx] <= 0x%08x readback=0x%08x match=%s  (%s)\n",
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

		pr_info("omo-hostwin: region %u %s: host 0x%llx..0x%llx -> dev 0x%llx size 0x%x\n",
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

/* ---- the device-visible host window (pcie_hostca_to_devva @0xaefc) ---- */

struct omo_hostwin {
	u32 devva_base;   /* win+0x00 */
	u32 devva_end;    /* win+0x08 */
	u32 hostca_base;  /* win+0x10 */
};

static struct omo_hostwin omo_win;

/* pcie_hostca_to_devva: devva = devva_base + hostca - hostca_base. */
static u32 omo_hostca_to_devva(u64 hostca)
{
	u64 devva;

	if (hostca < omo_win.hostca_base)
		return 0xffffffffU;
	devva = (u64)omo_win.devva_base + (hostca - omo_win.hostca_base);
	return (u32)devva;
}

/*
 * oal_pcie_set_outbound_by_membar (inlined in oal_pcie_set_inbound @0x9a38):
 * one outbound viewport at BAR2+0x000 that translates the device-visible range
 * devva_base..devva_end onto the host window hostca_base.  Without it the
 * endpoint drops every DMA to a host address - the missing piece from rtmsg.
 */
static void omo_program_outbound(void)
{
	pr_info("omo-hostwin: outbound viewport0: devva 0x%08x..0x%08x -> host 0x%08x (oal_pcie_set_outbound_by_membar @0x9a38)\n",
		omo_win.devva_base, omo_win.devva_end, omo_win.hostca_base);
	omo_iatu_wr(0x000, 0, "ob ctrl1=0");
	omo_iatu_wr(0x004, 0x80000000U, "ob ctrl2=ena|bar0");
	omo_iatu_wr(0x008, omo_win.devva_base, "ob base_lo=devva_base");
	omo_iatu_wr(0x00c, 0, "ob base_hi");
	omo_iatu_wr(0x010, omo_win.devva_end, "ob limit=devva_end");
	omo_iatu_wr(0x014, omo_win.hostca_base, "ob target_lo=hostca_base");
	omo_iatu_wr(0x018, 0, "ob target_hi");
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
		pr_err("omo-hostwin: filp_open(%s) failed %ld\n",
		       omo_fwpath, PTR_ERR(f));
		return PTR_ERR(f);
	}
	omo_fw_len = i_size_read(file_inode(f));
	if (!omo_fw_len || omo_fw_len > 16UL * 1024 * 1024) {
		pr_err("omo-hostwin: bad firmware size %zu\n", omo_fw_len);
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
			pr_err("omo-hostwin: kernel_read stopped at %zu/%zu (n=%zd)\n",
			       done, omo_fw_len, n);
			filp_close(f, NULL);
			vfree(omo_fw);
			omo_fw = NULL;
			return n ? (int)n : -EIO;
		}
		done += n;
	}
	filp_close(f, NULL);
	pr_info("omo-hostwin: firmware file %s size=%zu bytes\n",
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
	pr_info("omo-hostwin: verify %s BAR0+0x%lx: file=%zu bytes diffs=%zu match=%s\n",
		name, b0, limit, diffs, diffs ? "NO" : "YES");
	if (first >= 0) {
		u32 w0 = ioread32(omo_bar0 + b0 + (first & ~3UL));

		pr_info("omo-hostwin:   first diff @0x%lx file=0x%02x chip=0x%02x\n",
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
	pr_info("omo-hostwin: writability probe BAR0+0x%lx: wrote 0x%08x read 0x%08x match=%s\n",
		omo_target, probe, rb, rb == probe ? "YES" : "NO");
	if (rb != probe) {
		pr_err("omo-hostwin: target window does not hold a write - refusing the bulk write\n");
		return -EIO;
	}
	pr_info("omo-hostwin: download %zu bytes -> BAR0+0x%lx (device CA 0x01240000) in %u-byte chunks\n",
		limit, omo_target, omo_chunk);
	while (off < limit) {
		size_t n = min_t(size_t, omo_chunk, limit - off);

		memcpy_toio(omo_bar0 + omo_target + off, omo_fw + off, n);
		off += n;
		pr_info("omo-hostwin: wrote %zu/%zu bytes @ BAR0+0x%lx\n",
			off, limit, omo_target + off - n);
	}
	omo_verify_win("target", omo_target, limit);
	return 0;
}

/* ---- the runtime message context --------------------------------------- */

static void omo_msg_handler_stub(void *arg)
{
	pr_info("omo-hostwin: [msg-handler] dispatch stub arg=%px (bit=?) - no reply written\n",
		arg);
}

/*
 * Build the context the vendor's pcie_msg_init @0xb6e4 builds: map the six
 * CAs (shuangta_pcie_msg_reg_map @0x1b1a0), kmalloc the 11-entry table
 * (0x58 bytes) and register the four PCIe-level handlers (ids 1,3,6,7).
 *
 * The +4/+0xc/+0x10 register pointers are the vendor's per-chip context and
 * are NOT statically attributable to specific CAs (phase-20a A.6): we bind the
 * plausible reading - pending = the register the released chip actually wrote
 * (out[1], CA 0x40039014), ack = out[0], re-arm = out[2] - mark them inferred
 * and perform NO device writes through them.
 */
static int omo_msgctx_build(void)
{
	unsigned int i;

	for (i = 0; i < MAILBOX_N; i++)
		omo_ctx.reg[i] = omo_bar0 + omo_mbox[i].off;

	omo_ctx.table = kzalloc(sizeof(struct omo_msg_handler) * MSG_HANDLER_N,
				GFP_KERNEL);
	if (!omo_ctx.table)
		return -ENOMEM;
	omo_ctx.table[1].fn = omo_msg_handler_stub;
	omo_ctx.table[1].arg = (void *)1;
	omo_ctx.table[3].fn = omo_msg_handler_stub;
	omo_ctx.table[3].arg = (void *)3;
	omo_ctx.table[6].fn = omo_msg_handler_stub;
	omo_ctx.table[6].arg = (void *)6;
	omo_ctx.table[7].fn = omo_msg_handler_stub;
	omo_ctx.table[7].arg = (void *)7;
	omo_ctx.table[0].arg = NULL;

	omo_ctx.pending = omo_ctx.reg[1];
	omo_ctx.ack = omo_ctx.reg[0];
	omo_ctx.rearm = omo_ctx.reg[2];

	pr_info("omo-hostwin: msg ctx @ %px (pcie_msg_init @0xb6e4):\n", &omo_ctx);
	for (i = 0; i < MAILBOX_N; i++)
		pr_info("omo-hostwin:   ctx.reg[%u] = %px  CA=0x%08x  %s\n",
			i, omo_ctx.reg[i], omo_mbox[i].ca, omo_mbox[i].what);
	pr_info("omo-hostwin:   ctx+0x04 pending -> out[1] CA 0x%08x (INFERRED)\n",
		omo_mbox[1].ca);
	pr_info("omo-hostwin:   ctx+0x0c ack     -> out[0] CA 0x%08x (INFERRED)\n",
		omo_mbox[0].ca);
	pr_info("omo-hostwin:   ctx+0x10 re-arm  -> out[2] CA 0x%08x (INFERRED)\n",
		omo_mbox[2].ca);
	pr_info("omo-hostwin:   ctx+0x20 handler table = %px (11 x {fn,arg}, kmalloc 0x58)\n",
		omo_ctx.table);
	pr_info("omo-hostwin:   registered ids 1/3/6/7 -> %px (pcie_msg_register @0x15fbc)\n",
		(void *)omo_msg_handler_stub);
	pr_info("omo-hostwin:   NO device write through ctx+4/+0xc/+0x10 (binding not statically provable)\n");
	omo_ctx_ready = 1;
	return 0;
}

/* ---- the ETE SR/DR rings (pcie_ete_init @0x7820) ----------------------- */

static int omo_rings_alloc(void)
{
	unsigned int i;

	if (pci_set_dma_mask(omo_dev, DMA_BIT_MASK(32)))
		pr_warn("omo-hostwin: pci_set_dma_mask(32) failed\n");
	if (pci_set_consistent_dma_mask(omo_dev, DMA_BIT_MASK(32)))
		pr_warn("omo-hostwin: pci_set_consistent_dma_mask(32) failed\n");

	for (i = 0; i < ETE_SR_N; i++) {
		size_t sz = (ETE_DEPTH + 2) * 8;

		omo_sr_va[i] = dma_alloc_coherent(&omo_dev->dev, sz,
						  &omo_sr_dma[i], GFP_KERNEL);
		if (!omo_sr_va[i]) {
			pr_err("omo-hostwin: SR ch%u dma_alloc_coherent(%zu) FAILED\n",
			       i, sz);
			return -ENOMEM;
		}
		pr_info("omo-hostwin: SR ch%u nodes=%zuB virt=%px dma=0x%llx\n",
			i, sz, omo_sr_va[i], (unsigned long long)omo_sr_dma[i]);
	}

	for (i = 0; i < ETE_DR_N; i++) {
		size_t sz = ETE_DEPTH * 8;

		omo_dr_va[i] = dma_alloc_coherent(&omo_dev->dev, sz,
						  &omo_dr_dma[i], GFP_KERNEL);
		if (!omo_dr_va[i]) {
			pr_err("omo-hostwin: DR ch%u dma_alloc_coherent(%zu) FAILED\n",
			       i + 3, sz);
			return -ENOMEM;
		}
		omo_dr_pay[i] = dma_alloc_coherent(&omo_dev->dev, ETE_DR_PAYLOAD,
						   &omo_dr_pay_dma[i], GFP_KERNEL);
		if (!omo_dr_pay[i]) {
			pr_err("omo-hostwin: DR ch%u payload alloc FAILED\n", i + 3);
			return -ENOMEM;
		}
		omo_dr_snap[i] = kzalloc(sz, GFP_KERNEL);
		omo_pay_snap[i] = kzalloc(ETE_DR_PAYLOAD, GFP_KERNEL);
		if (!omo_dr_snap[i] || !omo_pay_snap[i])
			return -ENOMEM;

		/* The DR nodes are left zeroed: the device fills them (word0 =
		 * buffer device VA, word1 = (len<<16)|flags), and the device VA
		 * conversion (pcie_hostca_to_devva) is not available in the
		 * takeover - see docs/phase20/runtime-msg.md A.2.  We own the
		 * arrays and payload buffers and scan them for a deposit. */
		pr_info("omo-hostwin: DR ch%u nodes=%zuB virt=%px dma=0x%llx  payload=%px dma=0x%llx\n",
			i + 3, sz, omo_dr_va[i],
			(unsigned long long)omo_dr_dma[i], omo_dr_pay[i],
			(unsigned long long)omo_dr_pay_dma[i]);
	}

	omo_ete_ready = 1;
	return 0;
}

static void omo_ete_wr(void __iomem *win, unsigned long off, u32 val,
		       const char *name)
{
	u32 rb;

	iowrite32(val, win + off);
	rb = ioread32(win + off);
	pr_info("omo-hostwin:   %-22s [0x%03lx] <= 0x%08x readback=0x%08x match=%s\n",
		name, off, val, rb, rb == val ? "YES" : "NO");
}

static void omo_ete_program(void)
{
	void __iomem *win;
	unsigned int i;

	if (!omo_ete_ready) {
		pr_warn("omo-hostwin: rings=0 - no ETE programming\n");
		return;
	}

	win = omo_bar0 + ETE_BAR0_OFF;
	pr_info("omo-hostwin: ETE block CA 0x4003a000 = BAR0+0x%lx (via region-3 viewport 0x403b8000->CA 0x40000000; the old phase-17 flat offset 0x3a000 = host 0x4003a000 reads 0x%08x, wrong region)\n",
		ETE_BAR0_OFF, ioread32(omo_bar0 + 0x3a000));

	/* Read-only pre-state. */
	pr_info("omo-hostwin: ---- SR/DR program registers BEFORE ----\n");
	for (i = 0; i < ETE_SR_N; i++)
		pr_info("omo-hostwin:   SR ch%u ctrl=0x%08x base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i, ioread32(win + omo_sr_block[i] + ETE_SR_CTRL),
			ioread32(win + omo_sr_block[i] + ETE_SR_BASE),
			ioread32(win + omo_sr_block[i] + ETE_SR_DEPTH),
			ioread32(win + omo_sr_block[i] + ETE_SR_WPTR));
	for (i = 0; i < ETE_DR_N; i++)
		pr_info("omo-hostwin:   DR ch%u base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i + 3, ioread32(win + omo_dr_block[i] + ETE_DR_BASE),
			ioread32(win + omo_dr_block[i] + ETE_DR_DEPTH),
			ioread32(win + omo_dr_block[i] + ETE_DR_WPTR));

	/* SR: base, depth-1, wptr, ctrl (pcie_ete_sr_reg_init @0x14a48). */
	for (i = 0; i < ETE_SR_N; i++) {
		unsigned long b = omo_sr_block[i];
		u32 devva = omo_hostca_to_devva(omo_sr_dma[i]) + (u32)omo_acpoff;
		char t[40];

		scnprintf(t, sizeof(t), "SR ch%u base", i);
		omo_ete_wr(win, b + ETE_SR_BASE, devva, t);
		scnprintf(t, sizeof(t), "SR ch%u depth-1", i);
		omo_ete_wr(win, b + ETE_SR_DEPTH, ETE_DEPTH - 1, t);
		scnprintf(t, sizeof(t), "SR ch%u wptr", i);
		omo_ete_wr(win, b + ETE_SR_WPTR, 0, t);
		scnprintf(t, sizeof(t), "SR ch%u ctrl", i);
		omo_ete_wr(win, b + ETE_SR_CTRL, 0, t);
	}

	/* DR: base, depth-1, wptr (pcie_ete_dr_reg_init @0x1483c). */
	for (i = 0; i < ETE_DR_N; i++) {
		unsigned long b = omo_dr_block[i];
		u32 devva = omo_hostca_to_devva(omo_dr_dma[i]) + (u32)omo_acpoff;
		char t[40];

		scnprintf(t, sizeof(t), "DR ch%u base", i + 3);
		omo_ete_wr(win, b + ETE_DR_BASE, devva, t);
		scnprintf(t, sizeof(t), "DR ch%u depth-1", i + 3);
		omo_ete_wr(win, b + ETE_DR_DEPTH, ETE_DEPTH - 1, t);
		scnprintf(t, sizeof(t), "DR ch%u wptr", i + 3);
		omo_ete_wr(win, b + ETE_DR_WPTR, 0, t);
	}

	/* pcie_ete_chn_res @0x7490: read, AND 0xfffffc20, write back. */
	pr_info("omo-hostwin: ---- pcie_ete_chn_res (mask 0x%08x) ----\n",
		ETE_CHN_RES_MASK);
	for (i = 0; i < ETE_SR_N; i++) {
		unsigned long b = omo_sr_block[i];
		u32 v = ioread32(win + b + ETE_CHN_RES);
		char t[40];

		scnprintf(t, sizeof(t), "SR ch%u chn_res", i);
		omo_ete_wr(win, b + ETE_CHN_RES, v & ETE_CHN_RES_MASK, t);
	}
	for (i = 0; i < ETE_DR_N; i++) {
		unsigned long b = omo_dr_block[i];
		u32 v = ioread32(win + b + ETE_CHN_RES);
		char t[40];

		scnprintf(t, sizeof(t), "DR ch%u chn_res", i + 3);
		omo_ete_wr(win, b + ETE_CHN_RES, v & ETE_CHN_RES_MASK, t);
	}

	pr_info("omo-hostwin: ETE base values are the coherent DMA address + acpoff=%d (INFERRED: the vendor converts host CA -> device VA via the runtime window chip->[4]->[0xc4], pcie_hostca_to_devva @0xaefc)\n",
		omo_acpoff);
	pr_info("omo-hostwin: ---- SR/DR program registers AFTER ----\n");
	for (i = 0; i < ETE_SR_N; i++)
		pr_info("omo-hostwin:   SR ch%u ctrl=0x%08x base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i, ioread32(win + omo_sr_block[i] + ETE_SR_CTRL),
			ioread32(win + omo_sr_block[i] + ETE_SR_BASE),
			ioread32(win + omo_sr_block[i] + ETE_SR_DEPTH),
			ioread32(win + omo_sr_block[i] + ETE_SR_WPTR));
	for (i = 0; i < ETE_DR_N; i++)
		pr_info("omo-hostwin:   DR ch%u base=0x%08x depth=0x%08x wptr=0x%08x\n",
			i + 3, ioread32(win + omo_dr_block[i] + ETE_DR_BASE),
			ioread32(win + omo_dr_block[i] + ETE_DR_DEPTH),
			ioread32(win + omo_dr_block[i] + ETE_DR_WPTR));
}

/* ---- IRQ ---------------------------------------------------------------- */

static void omo_decode_mailbox(const char *what, u32 v)
{
	int i;

	pr_info("omo-hostwin:   %-18s = 0x%08x", what, v);
	if (!v) {
		pr_info("omo-hostwin:     bits={-}\n");
		return;
	}
	for (i = 0; i < 16; i++)
		if (v & (1U << i))
			pr_info("omo-hostwin:     bit %d (id %d = %s)\n",
				i, i, omo_msgid_name(i));
}

/* Returns IRQ_HANDLED for the first mailbox change, IRQ_NONE otherwise. */
static irqreturn_t omo_irq_handler(int irq, void *dev_id)
{
	u32 m[MAILBOX_N];
	unsigned int i, changed = 0;

	(void)dev_id;
	atomic_inc(&omo_irq_count);

	for (i = 0; i < MAILBOX_N; i++)
		m[i] = ioread32(omo_bar0 + omo_mbox[i].off);

	for (i = 0; i < MAILBOX_N; i++) {
		if (m[i] != omo_isr_seen[i]) {
			changed = 1;
			if (omo_isr_logs < ISR_LOG_MAX) {
				omo_isr_logs++;
				pr_info("omo-hostwin: [ISR +%lums] irq=%d %-18s CA=0x%08x 0x%08x -> 0x%08x\n",
					omo_ms_now(), irq, omo_mbox[i].what,
					omo_mbox[i].ca, omo_isr_seen[i], m[i]);
				omo_decode_mailbox(omo_mbox[i].what, m[i]);
			}
			omo_isr_seen[i] = m[i];
		}
	}

	if (!changed)
		return IRQ_NONE;

	atomic_inc(&omo_irq_hits);
	/* Shared, level, and the device source is not ours to clear: take one
	 * and stop, so a storm cannot wedge the line.  Polling continues. */
	omo_irq_disabled = 1;
	disable_irq_nosync(irq);
	return IRQ_HANDLED;
}

static int omo_request_irq_line(void)
{
	u8 line = 0;
	int irq, rc;

	pci_read_config_byte(omo_dev, PCI_INTERRUPT_LINE, &line);
	pr_info("omo-hostwin: INTx config before: PCI_INTERRUPT_LINE=%u (IRQ pin from cfg[0x3d])\n",
		line);
	/*
	 * The one unproven-but-quoted write: a takeover boot leaves
	 * PCI_INTERRUPT_LINE at 0xff; the live vendor boot has it at 0xcf
	 * (=207, the RC radm GIC-0 91 virq).  The vendor module never writes
	 * this byte (no cfg 0x3c store in plat.ko) - the platform writes it
	 * when a driver enables the device, which the takeover skipped.  Write
	 * the observed vendor value so the config space matches a vendor boot.
	 */
	if ((line == 0xff || line == 0) && omo_hostirq) {
		pci_write_config_byte(omo_dev, PCI_INTERRUPT_LINE,
				      (u8)omo_hostirq);
		pci_read_config_byte(omo_dev, PCI_INTERRUPT_LINE, &line);
		pr_info("omo-hostwin: PCI_INTERRUPT_LINE <= %u (unproven-but-quoted, live vendor value) readback=%u\n",
			omo_hostirq, line);
	}
	irq = omo_irq ? omo_irq : line;
	pr_info("omo-hostwin: INTx config: PCI_INTERRUPT_LINE=%u requested irq=%d\n",
		line, irq);
	if (irq <= 0) {
		pr_warn("omo-hostwin: no usable IRQ line - polling only\n");
		return -EINVAL;
	}
	omo_irq_num = irq;
	memcpy(omo_isr_seen, omo_mbox_last, sizeof(omo_isr_seen));
	rc = request_irq(irq, omo_irq_handler, IRQF_SHARED, "omo-hostwin", omo_dev);
	if (rc) {
		pr_err("omo-hostwin: request_irq(%d, IRQF_SHARED) rc=%d - polling only\n",
		       irq, rc);
		omo_irq_num = -1;
		return rc;
	}
	omo_irq_ok = 1;
	pr_info("omo-hostwin: request_irq(%d, IRQF_SHARED, \"omo-hostwin\") rc=0 - IRQ path live\n",
		irq);
	return 0;
}

static void omo_release_irq(void)
{
	if (!omo_irq_ok)
		return;
	if (omo_irq_disabled) {
		enable_irq(omo_irq_num);
		omo_irq_disabled = 0;
	}
	free_irq(omo_irq_num, omo_dev);
	omo_irq_ok = 0;
	pr_info("omo-hostwin: freed irq %d (taken=%d handled=%d)\n",
		omo_irq_num, atomic_read(&omo_irq_count),
		atomic_read(&omo_irq_hits));
	omo_irq_num = -1;
}

/* ---- polling ----------------------------------------------------------- */

static void omo_scan_changes(const char *tag)
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
				pr_info("omo-hostwin: [%s +%lums] SCAN CA=0x%08x BAR0+0x%lx 0x%08x -> 0x%08x\n",
					tag, omo_ms_now(),
					0x01320000U + (u32)off, omo_scanbase + off,
					old, w);
				shown++;
			}
			changed++;
		}
	}
	if (changed)
		pr_info("omo-hostwin: [%s +%lums] scan window changed %u/%u words (shown %u)\n",
			tag, omo_ms_now(), changed, omo_scanlen / 4, shown);
}

/* Scan the DR node arrays and payload buffers we own for a device deposit. */
static void omo_scan_dr(const char *tag)
{
	unsigned int i;
	unsigned long off;
	unsigned int hits = 0;

	if (!omo_ete_ready)
		return;
	for (i = 0; i < ETE_DR_N; i++) {
		u8 *n = omo_dr_va[i];
		u8 *p = omo_dr_pay[i];

		for (off = 0; off + 8 <= ETE_DEPTH * 8; off += 8) {
			if (memcmp(n + off, omo_dr_snap[i] + off, 8) != 0) {
				u32 w0, w1;

				memcpy(&w0, n + off, 4);
				memcpy(&w1, n + off + 4, 4);
				pr_info("omo-hostwin: [%s +%lums] DR ch%u node[%lu] CHANGED addr=0x%08x ctl=0x%08x (len=%u flag=0x%03x)\n",
					tag, omo_ms_now(), i + 3, off / 8, w0, w1,
					w1 >> 16, w1 & 0x1fff);
				memcpy(omo_dr_snap[i] + off, n + off, 8);
				hits++;
			}
		}
		for (off = 0; off + 4 <= ETE_DR_PAYLOAD; off += 4) {
			if (memcmp(p + off, omo_pay_snap[i] + off, 4) != 0) {
				u32 w;

				memcpy(&w, p + off, 4);
				pr_info("omo-hostwin: [%s +%lums] DR ch%u payload+0x%03lx = 0x%08x\n",
					tag, omo_ms_now(), i + 3, off, w);
				memcpy(omo_pay_snap[i] + off, p + off, 4);
				hits++;
			}
		}
	}
	if (hits)
		pr_info("omo-hostwin: [%s +%lums] DR/payload changes: %u\n",
			tag, omo_ms_now(), hits);
}

static void omo_poll_mailbox(const char *tag, unsigned int first)
{
	unsigned int i;

	for (i = 0; i < MAILBOX_N; i++) {
		u32 v = ioread32(omo_bar0 + omo_mbox[i].off);

		if (v != omo_mbox_last[i] || first) {
			pr_info("omo-hostwin: [%s +%lums] MBOX %-18s CA=0x%08x 0x%08x -> 0x%08x\n",
				tag, omo_ms_now(), omo_mbox[i].what,
				omo_mbox[i].ca, omo_mbox_last[i], v);
			omo_decode_mailbox(omo_mbox[i].what, v);
			if (!first && v)
				omo_msgs++;
			omo_mbox_last[i] = v;
		}
	}
	for (i = 0; i < STATUS_N; i++) {
		u32 v = ioread32(omo_bar0 + omo_stat[i].off);

		if (v != omo_stat_last[i]) {
			pr_info("omo-hostwin: [%s +%lums] STAT %-18s CA=0x%08x 0x%08x -> 0x%08x\n",
				tag, omo_ms_now(), omo_stat[i].what,
				omo_stat[i].ca, omo_stat_last[i], v);
			omo_stat_last[i] = v;
		}
	}
}

static void omo_snapshot_all(void)
{
	unsigned int i;
	unsigned long off;

	for (i = 0; i < MAILBOX_N; i++)
		omo_mbox_last[i] = ioread32(omo_bar0 + omo_mbox[i].off);
	for (i = 0; i < STATUS_N; i++)
		omo_stat_last[i] = ioread32(omo_bar0 + omo_stat[i].off);
	if (omo_scan_base && omo_scanlen)
		for (off = 0; off + 4 <= omo_scanlen; off += 4) {
			u32 w = ioread32(omo_bar0 + omo_scanbase + off);

			memcpy(omo_scan_base + off, &w, 4);
		}
	if (omo_ete_ready)
		for (i = 0; i < ETE_DR_N; i++) {
			memcpy(omo_dr_snap[i], omo_dr_va[i], ETE_DEPTH * 8);
			memcpy(omo_pay_snap[i], omo_dr_pay[i], ETE_DR_PAYLOAD);
		}
}

static int omo_do_release(void)
{
	u32 rb;

	pr_info("omo-hostwin: RELEASE write CA 0x40000108 <- 0x%08x (BAR0+0x%lx)\n",
		RELEASE_VAL, RELEASE_OFF);
	iowrite32(RELEASE_VAL, omo_bar0 + RELEASE_OFF);
	rb = ioread32(omo_bar0 + RELEASE_OFF);
	pr_info("omo-hostwin: release readback = 0x%08x\n", rb);
	return rb == RELEASE_VAL ? 0 : -EIO;
}

/* ---- init / exit -------------------------------------------------------- */

static void omo_free_rings(void)
{
	unsigned int i;

	for (i = 0; i < ETE_SR_N; i++)
		if (omo_sr_va[i]) {
			dma_free_coherent(&omo_dev->dev, (ETE_DEPTH + 2) * 8,
					  omo_sr_va[i], omo_sr_dma[i]);
			omo_sr_va[i] = NULL;
		}
	for (i = 0; i < ETE_DR_N; i++) {
		if (omo_dr_va[i]) {
			dma_free_coherent(&omo_dev->dev, ETE_DEPTH * 8,
					  omo_dr_va[i], omo_dr_dma[i]);
			omo_dr_va[i] = NULL;
		}
		if (omo_dr_pay[i]) {
			dma_free_coherent(&omo_dev->dev, ETE_DR_PAYLOAD,
					  omo_dr_pay[i], omo_dr_pay_dma[i]);
			omo_dr_pay[i] = NULL;
		}
		kfree(omo_dr_snap[i]);
		kfree(omo_pay_snap[i]);
		omo_dr_snap[i] = NULL;
		omo_pay_snap[i] = NULL;
	}
	omo_ete_ready = 0;
}

static int __init omo_hostwin_init(void)
{
	u16 cmd0 = 0, cmd1 = 0, rb = 0;
	unsigned int after_tgt, polls, k;
	int ret;

	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("omo-hostwin: endpoint %04x:00:00.0 not found\n", omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev, 0x00, &id);
		pr_info("omo-hostwin: endpoint %04x:00:00.0 id %04x:%04x\n",
			omo_domain, id & 0xffff, id >> 16);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("omo-hostwin: unexpected id\n");
	}

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);
	ret = pci_enable_device(omo_dev);
	if (ret) {
		pr_err("omo-hostwin: pci_enable_device rc=%d\n", ret);
		goto err_put;
	}
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("omo-hostwin: pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
		cmd0, cmd1);

	ret = pci_request_mem_regions(omo_dev, "omo-hostwin");
	if (ret) {
		pr_err("omo-hostwin: pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       ret);
		goto err_disable;
	}
	pr_info("omo-hostwin: pci_request_mem_regions rc=0 (MEM BARs claimed)\n");

	{
		u32 lo = 0, hi = 0, b2 = 0;

		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
		pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_2, &b2);
		omo_bar0_base = (u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) |
				((u64)hi << 32);
		pr_info("omo-hostwin: BAR0 base=0x%llx (config space), BAR2=0x%x (iatu_bar1)\n",
			(unsigned long long)omo_bar0_base,
			(u32)(b2 & PCI_BASE_ADDRESS_MEM_MASK));
	}

	omo_bar0 = pci_iomap(omo_dev, OMO_BAR_NUM, 0);
	omo_iatu = pci_iomap(omo_dev, OMO_IATU_BAR, 0);
	if (!omo_bar0 || !omo_iatu) {
		pr_err("omo-hostwin: pci_iomap FAILED (bar0=%p iatu=%p)\n",
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

	omo_win.devva_base = omo_devva_base;
	omo_win.devva_end = omo_devva_end;
	omo_win.hostca_base = omo_hostca_base;
	pr_info("omo-hostwin: hostca->devva window: win[0]=0x%08x win[8]=0x%08x win[0x10]=0x%08x (chip->[4]->[0xc4]; pcie_hostca_to_devva @0xaefc)\n",
		omo_win.devva_base, omo_win.devva_end, omo_win.hostca_base);

	if (!omo_program) {
		pr_info("omo-hostwin: program=0: iATU NOT programmed (read-only run)\n");
		goto skip_program;
	}
	if (ioread32(omo_iatu + OMO_IATU_CTRL2) == 0xffffffffU) {
		pr_err("omo-hostwin: iatu window reads 0xffffffff - undecoded, refusing\n");
		ret = -EIO;
		goto err_unmap;
	}
	pr_info("omo-hostwin: programming six inbound viewports via BAR2 (vendor membar path)\n");
	pr_info("omo-hostwin: programmed %d viewports\n", omo_program_regions());

	pci_write_config_word(omo_dev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_dev, PCI_COMMAND, &rb);
	pr_info("omo-hostwin: cfg[0x004] <= 0x0007 readback=0x%04x MEM|MASTER=%s\n",
		rb, (rb & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
		    (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	/* The missing piece from rtmsg: the outbound (device->host) window. */
	pr_info("omo-hostwin: outbound viewport BEFORE: [0x000]=0x%08x [0x004]=0x%08x [0x008]=0x%08x [0x010]=0x%08x [0x014]=0x%08x\n",
		ioread32(omo_iatu + 0x000), ioread32(omo_iatu + 0x004),
		ioread32(omo_iatu + 0x008), ioread32(omo_iatu + 0x010),
		ioread32(omo_iatu + 0x014));
	if (omo_outwin)
		omo_program_outbound();
	pr_info("omo-hostwin: outbound viewport AFTER:  [0x000]=0x%08x [0x004]=0x%08x [0x008]=0x%08x [0x010]=0x%08x [0x014]=0x%08x\n",
		ioread32(omo_iatu + 0x000), ioread32(omo_iatu + 0x004),
		ioread32(omo_iatu + 0x008), ioread32(omo_iatu + 0x010),
		ioread32(omo_iatu + 0x014));

	after_tgt = ioread32(omo_bar0 + omo_target);
	pr_info("omo-hostwin: decode verdict BAR0+0x%lx: after=0x%08x decoded=%s\n",
		omo_target, after_tgt,
		after_tgt != 0xffffffffU ? "YES" : "NO");

skip_program:
	/* Build the runtime message context (host structures only). */
	if (omo_msgctx_build()) {
		ret = -ENOMEM;
		goto err_unmap;
	}

	/* Allocate the ETE ring node arrays and payload buffers, then program
	 * the SR/DR registers exactly as pcie_ete_init does. */
	if (omo_rings && omo_rings_alloc()) {
		ret = -ENOMEM;
		goto err_ctx;
	}
	if (omo_rings)
		omo_ete_program();

	ret = omo_load_fw();
	if (ret)
		goto err_ctx;
	if (omo_write_fw() != 0) {
		pr_err("omo-hostwin: firmware write refused/failed - stopping\n");
		goto err_ctx;
	}

	omo_t0 = jiffies;
	omo_snapshot_all();
	pr_info("omo-hostwin: pre-release baseline taken (t0)\n");
	omo_poll_mailbox("pre", 1);

	if (omo_useirq)
		omo_request_irq_line();

	if (!omo_release) {
		pr_info("omo-hostwin: release=0: NOT releasing the chip\n");
		goto done;
	}
	if (omo_do_release()) {
		pr_err("omo-hostwin: release readback mismatch - stopping\n");
		goto err_ctx;
	}

	omo_poll_mailbox("post0", 0);
	omo_scan_changes("post0");
	omo_scan_dr("post0");

	if (omo_polldur < 10000)
		omo_polldur = 10000;
	if (omo_pollms < 20)
		omo_pollms = 20;
	polls = omo_polldur / omo_pollms;
	pr_info("omo-hostwin: polling %u mailbox + %u status regs, DR rings + payloads, %u-byte fw scan for %u ms (%u polls); irq=%s\n",
		MAILBOX_N, STATUS_N, omo_scanlen, omo_polldur, polls,
		omo_irq_ok ? "requested" : "none");

	for (k = 0; k < polls; k++) {
		msleep(omo_pollms);
		omo_poll_mailbox("poll", 0);
		omo_scan_dr("poll");
		if ((k & 1) == 1)
			omo_scan_changes("poll");
	}
	omo_scan_changes("final");
	omo_scan_dr("final");

done:
	pr_info("omo-hostwin: done (release=%u rings=%u acpoff=%d pollms=%u polldur=%u irq=%d irq_taken=%d irq_handled=%d msgs=%u)\n",
		omo_release, omo_rings, omo_acpoff, omo_pollms, omo_polldur,
		omo_irq_num, atomic_read(&omo_irq_count),
		atomic_read(&omo_irq_hits), omo_msgs);
	omo_release_irq();
	return 0;

err_ctx:
	omo_release_irq();
	omo_free_rings();
	kfree(omo_ctx.table);
	omo_ctx.table = NULL;
	if (omo_fw)
		vfree(omo_fw);
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

err_unmap:
	if (omo_scan_base)
		vfree(omo_scan_base);
	if (omo_iatu)
		pci_iounmap(omo_dev, omo_iatu);
	if (omo_bar0)
		pci_iounmap(omo_dev, omo_bar0);
	goto err_release;
}

static void __exit omo_hostwin_exit(void)
{
	omo_release_irq();
	kfree(omo_ctx.table);
	omo_ctx.table = NULL;
	omo_free_rings();
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
	pr_info("omo-hostwin: unloaded\n");
}

module_init(omo_hostwin_init);
module_exit(omo_hostwin_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Device-visible host window + ETE rings + IRQ for the takeover (phase 20c)");
