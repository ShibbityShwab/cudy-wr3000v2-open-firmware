// SPDX-License-Identifier: GPL-2.0
/*
 * iatuview: probe two SPARE PCIe inbound viewports (iATU indices 6 and 7) and
 * read the firmware interrupt-controller block through the new window.
 *
 * Spec: opensource/docs/phase49/spare-viewport-spec.md; this module is plan
 * task 13 (.omo/plans/wifi-forward.md:223).  Every constant below cites the
 * spec section it comes from.
 *
 * The endpoint implements its iATU in BAR2 (OMO_IATU_BAR = 2, wifidrv1.c:72),
 * not in config space, so the viewports live at BAR2 + 0x100 + 0x200*i (spec
 * sec 1.2).  Two windows are programmed; both are under 2^32 so both high words
 * are 0:
 *
 *   A  idx 6  BAR2 block 0xD00  host 0x40500000-0x4050ffff -> CA 0x40160000
 *             the GIC / interrupt-controller block (expected no-decode)
 *   B  idx 7  BAR2 block 0xF00  host 0x40510000-0x40510fff -> CA 0x40039000
 *             the ctrl-rb / mailbox block - the positive control
 *
 * Why B is not optional (spec sec 0): an unprogrammed window and an unrouted host
 * address both read 0xffffffff, so A alone proves nothing.  Only a B that
 * decodes in the same run makes A's 0xffffffff a statement about the device.
 *
 * Preconditions (spec sec 4):
 *   P1 pci_request_mem_regions succeeds (the vendor does not hold the MEM BARs).
 *   P2 BAR2 is decoded: BAR2+0x104 (viewport 0 CTRL2) must not read 0xffffffff.
 *   P3 program ONLY the spare viewports 6 and 7; the six named regions
 *      (indices 0..5) stay unprogrammed.  Both host windows lie inside region
 *      4's measured range, so programming the six in the same run would
 *      confound the measurement completely (spec sec 6).
 *
 * Order of operations (spec sec 4), followed literally:
 *   1 claim EP0, map BAR0 and BAR2; read bar0_base from config space
 *   2 P2 check on BAR2+0x104
 *   3 program viewport 6 (A): the seven words of spec sec 2, reading each back
 *   4 program viewport 7 (B): the seven words of spec sec 3, reading each back
 *   5 print all 14 readbacks (+ CTRL1 of both); any mismatch -> FAIL, no verdict
 *   6 read B's 0x2e4 / 0x2e8 / 0x2ec (pre-doorbell snapshot)
 *   7 write 0x1 to B+0x2d4 (the doorbell, out[2] dev CA 0x400392d4)
 *   8 read B's 0x2e4 / 0x2e8 / 0x2ec again (post-doorbell snapshot)
 *   9 read A's window at 0x40500000, 0x4050010c, 0x40501100, 0x40501108,
 *     0x40501800
 *  10 print one machine-greppable verdict line per window
 *
 * Guardrails (spec sec 8): never write CA 0x400392f0 (HOST_INTR_CLR,
 * write-1-to-clear); never write BAR2+0x000..0x01c (the outbound viewport);
 * never write CTRL1; never read the RC misc window 0x10161000; never program
 * the six named regions in this run.
 *
 * Module parameter:
 *   domain=N  PCI domain of the endpoint (default 0 = 2.4 GHz)
 */

#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>

#define OMO_VENDOR_ID		0x59e7
#define OMO_DEVICE_ID		0x0005

/* BARs (spec sec 1.1). */
#define OMO_BAR2_BASE_DEF	0x41800000UL	/* [measured] phase11/barmap.md:63 */
#define OMO_BAR2_LEN		0x4000UL	/* 16 KiB iATU window (spec sec 1.4) */

/* iATU register layout, per viewport i (spec sec 1.2): 0x100 + 0x200*i. */
#define IATU_STRIDE		0x200u
#define IATU_BLOCK(i)		(0x100 + IATU_STRIDE * (i))
#define IATU_CTRL1		0x00
#define IATU_CTRL2		0x04
#define IATU_LOWER_BASE		0x08
#define IATU_UPPER_BASE		0x0c
#define IATU_LIMIT		0x10
#define IATU_LOWER_TARGET	0x14
#define IATU_UPPER_TARGET	0x18
#define IATU_CTRL2_DISABLE	0x00000000	/* vendor step 1 (wifidrv1.c:317) */
#define IATU_CTRL2_ENABLE	0x80000000	/* vendor step 2: enable|BAR0 (wifidrv1.c:318) */

/* P2 probe: viewport 0's CTRL2 (spec sec 4 P2). */
#define IATU_P2_OFFSET		(IATU_BLOCK(0) + IATU_CTRL2)	/* 0x104 */

/* The six named regions occupy indices 0..5 (spec sec 1.4) - never programmed. */
#define IATU_NAMED_COUNT	6

/* Window A - viewport 6 - CA 0x40160000 (spec sec 2). */
#define A_IDX			6
#define A_HOST_BASE		0x40500000u	/* host window base */
#define A_HOST_LIMIT		0x4050ffffu	/* base + size - 1 */
#define A_TARGET		0x40160000u	/* device CA target */
#define A_SIZE			0x10000u
#define A_BAR0_OFF		0x500000UL	/* A_HOST_BASE - BAR0 base (sec 2) */

/* Window B - viewport 7 - CA 0x40039000, the positive control (spec sec 3). */
#define B_IDX			7
#define B_HOST_BASE		0x40510000u
#define B_HOST_LIMIT		0x40510fffu
#define B_TARGET		0x40039000u
#define B_SIZE			0x1000u
#define B_BAR0_OFF		0x510000UL	/* B_HOST_BASE - BAR0 base (sec 3) */

/* B's mailbox register map (spec sec 3; sibling map phase45/hi1105-mailbox-irq.md:35-40). */
#define MBX_DOORBELL		0x2d4u	/* HOST2DEVICE_INTR_SET - the H2D doorbell */
#define MBX_DOORBELL_VAL	0x00000001u
#define MBX_RAW			0x2e4u	/* HOST_INTR_RAW_STATUS */
#define MBX_MASK		0x2e8u	/* HOST_INTR_MASK */
#define MBX_STATUS		0x2ecu	/* HOST_INTR_STATUS (post-mask) */
/*
 * 0x2f0 HOST_INTR_CLR is write-1-to-clear and is READ ONLY here: it is never
 * written by this module (spec sec 3, sec 8).
 */

/* A's window reads (spec sec 2): offsets into the 0x10000 A window. */
#define A_RD_BASE		0x0000u	/* CA 0x40160000 */
#define A_RD_IAR		0x010cu	/* CA 0x4016010c pending/IAR */
#define A_RD_EN0		0x1100u	/* CA 0x40161100 enable word 0 */
#define A_RD_EN1		0x1108u	/* CA 0x40161108 enable word 1 (bit 12 = source 0x4c) */
#define A_RD_PRIO		0x1800u	/* CA 0x40161800 priority sibling */

#define OMO_NO_DECODE		0xffffffffU

struct omo_win {
	const char *name;
	unsigned int idx;
	u32 lower_base;
	u32 upper_base;
	u32 limit;
	u32 lower_tgt;
	u32 upper_tgt;
};

static unsigned int omo_domain;
module_param_named(domain, omo_domain, uint, 0444);
MODULE_PARM_DESC(domain, "PCI domain of the 59e7:0005 endpoint (default 0)");

static struct pci_dev *omo_dev;
static void __iomem *omo_bar2;
static void __iomem *omo_a_win;
static void __iomem *omo_b_win;

/* One register write with its immediate readback (every write is logged). */
static void omo_wr(void __iomem *base, unsigned long off, u32 val,
		   const char *win, unsigned int idx, const char *name)
{
	u32 rb;

	iowrite32(val, base + off);
	rb = ioread32(base + off);
	pr_info("iatuview: write %s idx%u %-18s [0x%03lx] <= 0x%08x readback=0x%08x match=%s\n",
		win, idx, name, off, val, rb, rb == val ? "YES" : "NO");
}

/* One readback assertion; returns the mismatch count (0 = match). */
static int omo_assert(void __iomem *base, const char *win, unsigned int idx,
		      const char *name, unsigned long off, u32 expect)
{
	u32 got = ioread32(base + off);

	pr_info("iatuview: check %s idx%u %-18s [0x%03lx] = 0x%08x expect=0x%08x match=%s\n",
		win, idx, name, off, got, expect, got == expect ? "YES" : "NO");
	return got == expect ? 0 : 1;
}

/*
 * Program one viewport with the vendor's seven-word order (spec sec 1.3, sec 2, sec 3)
 * and assert the seven-register readback.  Returns the assertion mismatch count.
 */
static int omo_win_program(const struct omo_win *w, void __iomem *bar2)
{
	unsigned long b = IATU_BLOCK(w->idx);
	int bad = 0;

	pr_info("iatuview: program %s idx%u BAR2 block +0x%03lx (spec sec %s)\n",
		w->name, w->idx, b, w->idx == A_IDX ? "2" : "3");

	omo_wr(bar2, b + IATU_CTRL2, IATU_CTRL2_DISABLE, w->name, w->idx, "CTRL2 disable");
	omo_wr(bar2, b + IATU_CTRL2, IATU_CTRL2_ENABLE, w->name, w->idx, "CTRL2 enable|BAR0");
	omo_wr(bar2, b + IATU_LOWER_BASE, w->lower_base, w->name, w->idx, "LOWER_BASE");
	omo_wr(bar2, b + IATU_UPPER_BASE, w->upper_base, w->name, w->idx, "UPPER_BASE");
	omo_wr(bar2, b + IATU_LIMIT, w->limit, w->name, w->idx, "LIMIT");
	omo_wr(bar2, b + IATU_LOWER_TARGET, w->lower_tgt, w->name, w->idx, "LOWER_TARGET");
	omo_wr(bar2, b + IATU_UPPER_TARGET, w->upper_tgt, w->name, w->idx, "UPPER_TARGET");

	bad += omo_assert(bar2, w->name, w->idx, "CTRL1", b + IATU_CTRL1, 0x00000000);
	bad += omo_assert(bar2, w->name, w->idx, "CTRL2", b + IATU_CTRL2, IATU_CTRL2_ENABLE);
	bad += omo_assert(bar2, w->name, w->idx, "LOWER_BASE", b + IATU_LOWER_BASE, w->lower_base);
	bad += omo_assert(bar2, w->name, w->idx, "UPPER_BASE", b + IATU_UPPER_BASE, w->upper_base);
	bad += omo_assert(bar2, w->name, w->idx, "LIMIT", b + IATU_LIMIT, w->limit);
	bad += omo_assert(bar2, w->name, w->idx, "LOWER_TARGET", b + IATU_LOWER_TARGET, w->lower_tgt);
	bad += omo_assert(bar2, w->name, w->idx, "UPPER_TARGET", b + IATU_UPPER_TARGET, w->upper_tgt);

	pr_info("iatuview: %s idx%u readback mismatches=%d\n", w->name, w->idx, bad);
	return bad;
}

static int __init omo_iatuview_init(void)
{
	static const struct omo_win win[2] = {
		{
			.name = "A", .idx = A_IDX,
			.lower_base = A_HOST_BASE, .upper_base = 0,
			.limit = A_HOST_LIMIT, .lower_tgt = A_TARGET, .upper_tgt = 0,
		},
		{
			.name = "B", .idx = B_IDX,
			.lower_base = B_HOST_BASE, .upper_base = 0,
			.limit = B_HOST_LIMIT, .lower_tgt = B_TARGET, .upper_tgt = 0,
		},
	};
	u32 lo = 0, hi = 0, cfg2 = 0, p2, a_rd[5];
	u32 b_raw_pre, b_mask_pre, b_status_pre;
	u32 b_raw_post, b_mask_post, b_status_post;
	u32 named_pre[IATU_NAMED_COUNT], named_post[IATU_NAMED_COUNT];
	unsigned long bar0_base, bar2_base;
	u16 cmd0 = 0, cmd1 = 0, rb_cmd = 0xffff;
	int bad_a, bad_b, i, same, ret;

	/* (1) claim EP0 as fwload does. */
	omo_dev = pci_get_domain_bus_and_slot(omo_domain, 0, PCI_DEVFN(0, 0));
	if (!omo_dev) {
		pr_err("iatuview: endpoint %04x:00:00.0 not found\n", omo_domain);
		return -ENODEV;
	}
	{
		u32 id = 0;

		pci_read_config_dword(omo_dev, 0x00, &id);
		if ((id & 0xffff) != OMO_VENDOR_ID || (id >> 16) != OMO_DEVICE_ID)
			pr_warn("iatuview: unexpected id %04x:%04x\n",
				id & 0xffff, id >> 16);
	}

	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd0);
	ret = pci_enable_device(omo_dev);
	if (ret)
		goto err_put;
	pci_read_config_word(omo_dev, PCI_COMMAND, &cmd1);
	pr_info("iatuview: pci_enable_device rc=0 command 0x%04x -> 0x%04x\n",
		cmd0, cmd1);

	/* P1: the fwload guard - refuse if the vendor stack holds the regions. */
	ret = pci_request_mem_regions(omo_dev, "iatuview");
	if (ret) {
		pr_err("iatuview: P1 pci_request_mem_regions rc=%d (vendor stack loaded?) - refusing\n",
		       ret);
		goto err_disable;
	}
	pr_info("iatuview: P1 pci_request_mem_regions rc=0 (MEM BARs claimed, vendor not holding)\n");

	ret = pci_write_config_word(omo_dev, PCI_COMMAND, 0x0007);
	pci_read_config_word(omo_dev, PCI_COMMAND, &rb_cmd);
	pr_info("iatuview: cfg[0x004] <= 0x0007 readback=0x%04x MEM|MASTER=%s\n",
		rb_cmd, (rb_cmd & (PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER)) ==
			(PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER) ? "set" : "MISSING");

	/* bar0_base from config space ONLY (never pci_resource_start, spec sec 1.1). */
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_0, &lo);
	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_1, &hi);
	bar0_base = (unsigned long)((u64)(lo & PCI_BASE_ADDRESS_MEM_MASK) |
				    ((u64)hi << 32));
	if (!bar0_base || (lo & PCI_BASE_ADDRESS_SPACE_IO)) {
		pr_err("iatuview: no usable BAR0 (lo=0x%08x hi=0x%08x)\n", lo, hi);
		ret = -ENODEV;
		goto err_release;
	}
	pr_info("iatuview: BAR0 base=0x%lx (config space PCI_BASE_ADDRESS_0)\n", bar0_base);

	pci_read_config_dword(omo_dev, PCI_BASE_ADDRESS_2, &cfg2);
	bar2_base = (unsigned long)(cfg2 & PCI_BASE_ADDRESS_MEM_MASK);
	if (!bar2_base) {
		bar2_base = OMO_BAR2_BASE_DEF;
		pr_warn("iatuview: config-space BAR2 is 0; using documented 0x%lx\n",
			bar2_base);
	}
	pr_info("iatuview: BAR2 base=0x%lx (config space PCI_BASE_ADDRESS_2=0x%08x)\n",
		bar2_base, cfg2);

	omo_bar2 = ioremap(bar2_base, OMO_BAR2_LEN);
	if (!omo_bar2) {
		pr_err("iatuview: ioremap BAR2 FAILED\n");
		ret = -ENOMEM;
		goto err_release;
	}
	pr_info("iatuview: BAR2 mapped (0x%lx bytes)\n", OMO_BAR2_LEN);

	/* (2) P2: BAR2 must be decoded before anything is written. */
	p2 = ioread32(omo_bar2 + IATU_P2_OFFSET);
	pr_info("iatuview: P2 BAR2+0x%x (viewport 0 CTRL2) = 0x%08x\n",
		IATU_P2_OFFSET, p2);
	if (p2 == OMO_NO_DECODE) {
		pr_err("iatuview: P2 FAIL - BAR2 is not decoded; refusing to program\n");
		ret = -ENODEV;
		goto err_unmap;
	}
	pr_info("iatuview: P2 OK (BAR2 decoded)\n");

	/* P3: snapshot the six named viewports' CTRL2 before touching anything. */
	for (i = 0; i < IATU_NAMED_COUNT; i++)
		named_pre[i] = ioread32(omo_bar2 + IATU_BLOCK(i) + IATU_CTRL2);
	pr_info("iatuview: P3 pre  named idx0-5 CTRL2: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		named_pre[0], named_pre[1], named_pre[2],
		named_pre[3], named_pre[4], named_pre[5]);

	/* (3) (4) program the two SPARE viewports only. */
	bad_a = omo_win_program(&win[0], omo_bar2);
	bad_b = omo_win_program(&win[1], omo_bar2);

	/* P3: the six named viewports must be untouched by this run. */
	for (i = 0; i < IATU_NAMED_COUNT; i++)
		named_post[i] = ioread32(omo_bar2 + IATU_BLOCK(i) + IATU_CTRL2);
	pr_info("iatuview: P3 post named idx0-5 CTRL2: 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
		named_post[0], named_post[1], named_post[2],
		named_post[3], named_post[4], named_post[5]);
	same = 1;
	for (i = 0; i < IATU_NAMED_COUNT; i++)
		if (named_pre[i] != named_post[i])
			same = 0;
	pr_info("iatuview: P3 six named viewports idx0-5 unchanged=%s\n",
		same ? "YES" : "NO");

	/* (5) any readback mismatch stops the run: FAIL, no verdict (spec sec 5). */
	pr_info("iatuview: STEP5 readback A idx%d mismatch=%d, B idx%d mismatch=%d, total=%d\n",
		A_IDX, bad_a, B_IDX, bad_b, bad_a + bad_b);
	if (bad_a + bad_b) {
		pr_err("iatuview: STEP5 FAIL - a readback mismatched; no doorbell, no verdict (spec sec 5)\n");
		pr_info("iatuview: RESULT FAIL reason=readback-mismatch\n");
		/* Leave the module loaded so the programmed state stays readable. */
		return 0;
	}
	pr_info("iatuview: STEP5 OK - all 14 readbacks match the spec tables\n");

	/* Map the two host windows through BAR0 (spec sec 2/sec 3). */
	omo_a_win = ioremap(bar0_base + A_BAR0_OFF, A_SIZE);
	omo_b_win = ioremap(bar0_base + B_BAR0_OFF, B_SIZE);
	if (!omo_a_win || !omo_b_win) {
		pr_err("iatuview: ioremap of the A/B host windows FAILED\n");
		ret = -ENOMEM;
		goto err_unmap;
	}
	pr_info("iatuview: host window A 0x%lx-0x%lx and B 0x%lx-0x%lx mapped\n",
		bar0_base + A_BAR0_OFF, bar0_base + A_HOST_LIMIT,
		bar0_base + B_BAR0_OFF, bar0_base + B_HOST_LIMIT);

	/* (6) B's pre-doorbell snapshot. */
	b_raw_pre = ioread32(omo_b_win + MBX_RAW);
	b_mask_pre = ioread32(omo_b_win + MBX_MASK);
	b_status_pre = ioread32(omo_b_win + MBX_STATUS);
	pr_info("iatuview: STEP6 B pre-doorbell  raw[0x2e4]=0x%08x mask[0x2e8]=0x%08x status[0x2ec]=0x%08x\n",
		b_raw_pre, b_mask_pre, b_status_pre);

	/* (7) ring the doorbell once - out[2], dev CA 0x400392d4, through B. */
	iowrite32(MBX_DOORBELL_VAL, omo_b_win + MBX_DOORBELL);
	pr_info("iatuview: STEP7 doorbell out[2] CA 0x400392d4 [B+0x%03x] <= 0x%08x (ringed once)\n",
		MBX_DOORBELL, MBX_DOORBELL_VAL);

	/* (8) B's post-doorbell snapshot. */
	b_raw_post = ioread32(omo_b_win + MBX_RAW);
	b_mask_post = ioread32(omo_b_win + MBX_MASK);
	b_status_post = ioread32(omo_b_win + MBX_STATUS);
	pr_info("iatuview: STEP8 B post-doorbell raw[0x2e4]=0x%08x mask[0x2e8]=0x%08x status[0x2ec]=0x%08x\n",
		b_raw_post, b_mask_post, b_status_post);
	pr_info("iatuview: B raw status bit0 pre=%u post=%u rise=%s\n",
		b_raw_pre & 1U, b_raw_post & 1U,
		((b_raw_post & 1U) && !(b_raw_pre & 1U)) ? "YES" : "NO");

	/* (9) read A's window. */
	a_rd[0] = ioread32(omo_a_win + A_RD_BASE);
	a_rd[1] = ioread32(omo_a_win + A_RD_IAR);
	a_rd[2] = ioread32(omo_a_win + A_RD_EN0);
	a_rd[3] = ioread32(omo_a_win + A_RD_EN1);
	a_rd[4] = ioread32(omo_a_win + A_RD_PRIO);
	pr_info("iatuview: STEP9 A read 0x40500000 (CA 0x40160000 GIC base)    = 0x%08x\n", a_rd[0]);
	pr_info("iatuview: STEP9 A read 0x4050010c (CA 0x4016010c pending/IAR) = 0x%08x\n", a_rd[1]);
	pr_info("iatuview: STEP9 A read 0x40501100 (CA 0x40161100 enable w0)   = 0x%08x\n", a_rd[2]);
	pr_info("iatuview: STEP9 A read 0x40501108 (CA 0x40161108 enable w1)   = 0x%08x\n", a_rd[3]);
	pr_info("iatuview: STEP9 A read 0x40501800 (CA 0x40161800 priority)    = 0x%08x\n", a_rd[4]);

	/* (10) one machine-greppable verdict line per window. */
	pr_info("iatuview: WINDOW A idx%d base=0x%08x limit=0x%08x target=0x%08x read=0x%08x class=%s\n",
		A_IDX, A_HOST_BASE, A_HOST_LIMIT, A_TARGET, a_rd[1],
		a_rd[1] == OMO_NO_DECODE ? "no-decode" : "decoded");
	pr_info("iatuview: WINDOW B idx%d base=0x%08x limit=0x%08x target=0x%08x raw_pre=0x%08x raw_post=0x%08x bit0=%u class=%s\n",
		B_IDX, B_HOST_BASE, B_HOST_LIMIT, B_TARGET,
		b_raw_pre, b_raw_post, b_raw_post & 1U,
		b_raw_post == OMO_NO_DECODE ? "no-decode" : "decode");

	/* The interpretation table of spec sec 5. */
	if (b_raw_post == OMO_NO_DECODE || !(b_raw_post & 1U)) {
		pr_info("iatuview: RESULT FAIL reason=B-not-decoded (programming/host-route defect, NOT a GIC verdict; spec sec 5)\n");
	} else if (a_rd[1] == OMO_NO_DECODE) {
		pr_info("iatuview: RESULT PASS verdict=expected-negative A=no-decode B=decode (GATE G5 default branch)\n");
	} else {
		pr_info("iatuview: RESULT PASS verdict=host-visible A=decoded B=decode (GATE G5 follow-on)\n");
	}
	return 0;

err_unmap:
	if (omo_a_win) {
		iounmap(omo_a_win);
		omo_a_win = NULL;
	}
	if (omo_b_win) {
		iounmap(omo_b_win);
		omo_b_win = NULL;
	}
	if (omo_bar2) {
		iounmap(omo_bar2);
		omo_bar2 = NULL;
	}
err_release:
	pci_release_mem_regions(omo_dev);
err_disable:
	pci_disable_device(omo_dev);
err_put:
	pci_dev_put(omo_dev);
	omo_dev = NULL;
	return ret;
}

static void __exit omo_iatuview_exit(void)
{
	if (omo_a_win)
		iounmap(omo_a_win);
	if (omo_b_win)
		iounmap(omo_b_win);
	if (omo_bar2)
		iounmap(omo_bar2);
	if (omo_dev) {
		pci_release_mem_regions(omo_dev);
		pci_disable_device(omo_dev);
		pci_dev_put(omo_dev);
		omo_dev = NULL;
	}
	pr_info("iatuview: unloaded\n");
}

module_init(omo_iatuview_init);
module_exit(omo_iatuview_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Spare iATU viewport probe: program idx6/idx7, ring the doorbell once, read the GIC window");
