// SPDX-License-Identifier: GPL-2.0
/*
 * pciet_hcc_link.c - Hi5622V100 host-side HCC link/event observability and the
 * vendor-attested H2D consumer-liveness ladder (wifi-hcc-link lane).
 *
 * ORIGINAL code written by the wifi-hcc-link lane (host-only, 2026-11-23)
 * from the project's decoded contract; no vendor source text or object code
 * is copied.  Evidence markers: [V] re-verified from a named local artifact,
 * [V*] device-measured by an earlier attended lane, [I] inference, [U] unknown.
 *
 * WHY THERE IS NO "LINK CREATE" FRAME HERE
 *
 *   The lane's question was: which vendor H2D message performs the HCC
 *   link/event bring-up that the firmware is missing?  The static reversal
 *   (../REPORT.md sec 3-4) answers: none.  In both vendor module versions
 *   (2.4.15 and 2.5.24) the host-side HCC framework is host-RAM-local
 *   (hcc_init/hcc_start/bal_init/hcc_msg_register_tab_* never touch the
 *   device), the vendor init/release flow contains no link/event H2D sender,
 *   and the firmware functions that create/bind the per-link +0x4B0 contexts
 *   have no H2D-handler caller.  What the firmware link/event layer needs is
 *   service of the ready gate and a firmware-internal boot stage that has not
 *   been identified yet, not another host frame.  Fabricating device-RAM
 *   state or a speculative payload frame is explicitly not justified
 *   (external-abort risk; wifi-abort-rca sec 10.4 and wifi-h2d-catalog sec 6).
 *
 * WHAT THIS FILE DOES INSTEAD (the honest host counterpart)
 *
 *   1. hcc_link_status (read-only): a bounded observability pack over exactly
 *      the objects the boot fault involves - the g message object, the HCC
 *      instance context, the destination-registry core-0 record, the four
 *      per-link +0x4B0 words, the exception mask/state/slot, the ETE fill
 *      indices and every mailbox word.  It classifies link_ctx=SET/UNSET and
 *      prints the next-lane decision.
 *   2. hcc_link_wake (write "1"): one hard-bounded H2D consumer-liveness
 *      probe.  Posts only mailbox id 1 (the vendor's payload-free global
 *      message wake: out[0] |= BIT(1), out[2] |= 1) and measures whether the
 *      firmware consumes it (out[0] bit clears; the device's own dispatcher
 *      also writes 8 to out[2] and 1 to out[5] on a real consume).  The
 *      result discriminates "firmware parked before its H2D message layer ->
 *      no H2D frame can be the missing input" from "consumer alive".
 *   3. hcc_link_plan (read-only): the bench ladder and its stop rules as text.
 *
 * SAFETY (mirrors the rest of the transport; project hard rules)
 *   - out[5] CA 0x400392f0 is NEVER written (build-time asserts + the
 *     transport's gated writer; it is only read back as a checkpoint).
 *   - mailbox id 5 is never posted; the only posted bit is BIT(1).
 *   - no device RAM or IO write except out[0] / out[2] in the wake probe; the
 *     probe refuses before CPU release and when out[0] is not zero.
 *   - the RC misc window 0x10161000 is not touched; reads are bounded to the
 *     two mapped windows (region-5 CPU RAM, region-3 IO).
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/printk.h>
#include <linux/string.h>
#include <linux/types.h>

#include "hi5622_pciet.h"
#include "pciet_hcc_link.h"

#define HCC_LINK_LOG_PREFIX	"hi5622_pciet hcc-link: "

/* ---------------------------------------------------------------------
 * Contract asserts: the two mailbox registers this file may write are the
 * host-owned ones and are NOT out[5]; the per-link CA list is inside the
 * readable CPU window.  (Compile-time; a violation breaks the build.)
 * ------------------------------------------------------------------- */
static void hcc_link_assert_contract(void)
{
	BUILD_BUG_ON(HCC_LINK_CA_H2D_PENDING != HI5622_CA_H2D_PENDING);
	BUILD_BUG_ON(HCC_LINK_CA_H2D_DOORBELL != HI5622_CA_H2D_DOORBELL);
	BUILD_BUG_ON(HCC_LINK_CA_OUT5 != HI5622_CA_NEVER_WRITE);
	BUILD_BUG_ON(HCC_LINK_WAKE_ID == 5);
	BUILD_BUG_ON(HCC_LINK_CA_H2D_PENDING == HCC_LINK_CA_OUT5);
	BUILD_BUG_ON(HCC_LINK_CA_H2D_DOORBELL == HCC_LINK_CA_OUT5);
	BUILD_BUG_ON(HCC_LINK_CPU_WIN_END > 0x198000u);
	BUILD_BUG_ON(HCC_LINK_PERLINK_COUNT != 4);
}

/* ---------------------------------------------------------------------
 * State
 * ------------------------------------------------------------------- */
struct hcc_link_state {
	struct hi5622_pci	*ctx;
	struct mutex		lock;		/* one probe at a time */
	u32			wake_attempts;
	u32			wake_consumed;
	u32			wake_timeouts;
	/* last wake probe */
	u32			out0_before;
	u32			out0_write;
	u32			out0_after;
	u32			out2_before;
	u32			out2_write;
	u32			out2_after;
	u32			out5_read;	/* READ-ONLY checkpoint */
	u32			out1_read;
	u32			polls;
	bool			last_consumed;
	bool			have_last;
};

static struct hcc_link_state *hcc_link_single;
static DEFINE_MUTEX(hcc_link_reg_lock);

/* The four per-link +0x4B0 word addresses named by the lane brief [V*]:
 * registry-target base + 0x4b0 for targets 0x171b70/0x171e1c/0x171f54/
 * 0x171fa8.  On the parked cold boot all four read ZERO. */
static const u32 hcc_link_perlink_ca[HCC_LINK_PERLINK_COUNT] = {
	0x00172020u, 0x001722ccu, 0x00172404u, 0x00172458u,
};

/* ---------------------------------------------------------------------
 * Bounded read helpers (no writes)
 * ------------------------------------------------------------------- */
static bool hcc_link_cpu_ok(u32 cpu)
{
	return cpu < HCC_LINK_CPU_WIN_END;
}

static u32 hcc_link_read_cpu(struct hi5622_pci *ctx, u32 cpu, bool *ok)
{
	if (!ctx->bar0 || !hcc_link_cpu_ok(cpu)) {
		if (ok)
			*ok = false;
		return 0;
	}
	if (ok)
		*ok = true;
	return readl(ctx->bar0 + HCC_LINK_BAR0_RAM_BASE + cpu);
}

static u32 hcc_link_read_io(struct hi5622_pci *ctx, u32 ca)
{
	if (!ctx->bar0)
		return 0;
	return readl(ctx->bar0 + HI5622_BAR0_IO(ca));
}

static bool hcc_link_ptr_plausible(u32 p)
{
	/* firmware CPU-space pointer inside the readable RAM window, above the
	 * image base (CPU 0x40000) and below the window end */
	return p >= 0x00040000u && p < HCC_LINK_CPU_WIN_END;
}


/* ---------------------------------------------------------------------
 * Snapshot: every read is bounds-checked; 'ok' is false for a window miss.
 * ------------------------------------------------------------------- */
struct hcc_link_snap {
	/* mailbox words */
	u32	out0, out1, out2, ack, rearm, out5, d2h_doorbell, ete_intr;
	/* firmware boot words */
	u32	exc_mask;
	u32	state;
	u32	boot_slot;
	u32	g_ptr;
	/* g object fields (valid only when g_ptr is plausible) */
	bool	g_ok;
	u32	g_out1_ptr, g_armed, g_pending, g_2cc;
	/* HCC instance context (CA 0x10c190) */
	bool	inst_ok;
	u32	inst_reg0, inst_reg1, inst_190, inst_2b8, inst_2cc, inst_4b0;
	/* destination-registry core-0 record */
	bool	reg_ok_window;
	u32	reg[HCC_LINK_REG_NWORDS];
	/* the four per-link +0x4B0 words (lane brief CAs) */
	bool	perlink_ok;
	u32	perlink[HCC_LINK_PERLINK_COUNT];
	u32	perlink_4b4[HCC_LINK_PERLINK_COUNT];
	/* ETE fill state (SR0-2 consumer, DR0-3 fill) */
	u32	sr_cns[3], dr_fill[4];
};

static void hcc_link_take_snapshot(struct hi5622_pci *ctx, struct hcc_link_snap *s)
{
	int i;

	memset(s, 0, sizeof(*s));
	s->out0 = hcc_link_read_io(ctx, HCC_LINK_CA_H2D_PENDING);
	s->out1 = hcc_link_read_io(ctx, HI5622_CA_D2H_BITMAP);
	s->out2 = hcc_link_read_io(ctx, HCC_LINK_CA_H2D_DOORBELL);
	s->ack = hcc_link_read_io(ctx, HCC_LINK_CA_ACK);
	s->rearm = hcc_link_read_io(ctx, HCC_LINK_CA_REARM);
	s->out5 = hcc_link_read_io(ctx, HCC_LINK_CA_OUT5);	/* READ ONLY */
	s->d2h_doorbell = hcc_link_read_io(ctx, HCC_LINK_CA_D2H_DOORBELL);
	s->ete_intr = hcc_link_read_io(ctx, HI5622_CA_ETE_INTR);

	s->exc_mask = hcc_link_read_cpu(ctx, HCC_LINK_CA_EXC_MASK, NULL) & 0xffffu;
	s->state = hcc_link_read_cpu(ctx, HCC_LINK_CA_STATE, NULL) & 0xffu;
	s->boot_slot = hcc_link_read_cpu(ctx, HCC_LINK_CA_BOOT_SLOT, NULL);
	s->g_ptr = hcc_link_read_cpu(ctx, HCC_LINK_CA_G_PTR, NULL);

	s->g_ok = hcc_link_ptr_plausible(s->g_ptr);
	if (s->g_ok) {
		s->g_out1_ptr = hcc_link_read_cpu(ctx,
				s->g_ptr + HCC_LINK_G_OFF_OUT1_PTR, NULL);
		s->g_armed = hcc_link_read_cpu(ctx,
				s->g_ptr + HCC_LINK_G_OFF_ARMED, NULL);
		s->g_pending = hcc_link_read_cpu(ctx,
				s->g_ptr + HCC_LINK_G_OFF_PENDING, NULL);
		s->g_2cc = hcc_link_read_cpu(ctx,
				s->g_ptr + HCC_LINK_G_OFF_2CC, NULL);
	}

	s->inst_reg0 = hcc_link_read_cpu(ctx, HCC_LINK_CA_INSTANCE + 0x0,
					 &s->inst_ok);
	s->inst_reg1 = hcc_link_read_cpu(ctx, HCC_LINK_CA_INSTANCE + 0x4, NULL);
	s->inst_190 = hcc_link_read_cpu(ctx, HCC_LINK_CA_INSTANCE + 0x190, NULL);
	s->inst_2b8 = hcc_link_read_cpu(ctx, HCC_LINK_CA_INSTANCE + 0x2b8, NULL);
	s->inst_2cc = hcc_link_read_cpu(ctx, HCC_LINK_CA_INSTANCE + 0x2cc, NULL);
	s->inst_4b0 = hcc_link_read_cpu(ctx, HCC_LINK_CA_INSTANCE + 0x4b0, NULL);

	s->reg_ok_window = true;
	for (i = 0; i < HCC_LINK_REG_NWORDS; i++) {
		bool ok = true;

		s->reg[i] = hcc_link_read_cpu(ctx,
					      HCC_LINK_CA_REG_REC0 + 4u * i,
					      &ok);
		s->reg_ok_window &= ok;
	}

	s->perlink_ok = true;
	for (i = 0; i < HCC_LINK_PERLINK_COUNT; i++) {
		bool ok = true;

		/* hcc_link_perlink_ca[] entries ARE the +0x4B0 field
		 * addresses (registry target base + 0x4b0; lane brief CAs). */
		s->perlink[i] = hcc_link_read_cpu(ctx,
				hcc_link_perlink_ca[i], &ok);
		s->perlink_ok &= ok;
		if (ok)
			s->perlink_4b4[i] = hcc_link_read_cpu(ctx,
					hcc_link_perlink_ca[i] + 4u, NULL);
	}

	for (i = 0; i < 3; i++) {
		u32 off = HI5622_BAR0_ETE_BASE + HI5622_ETE_SR0_OFF +
			  0x50u * i;

		s->sr_cns[i] = readl(ctx->bar0 + off +
				     HI5622_ETE_CH_A_CNS);
	}
	for (i = 0; i < 4; i++) {
		u32 off = HI5622_BAR0_ETE_BASE + HI5622_ETE_DR0_OFF +
			  0x50u * i;

		s->dr_fill[i] = readl(ctx->bar0 + off +
				      HI5622_ETE_CH_B_CNS);
	}
}

/* verdict helpers */
static bool hcc_link_registry_populated(const struct hcc_link_snap *s)
{
	/* live cold-boot shape: {0, 0x00103fb8, 0x00171b70, 0x00171e1c,
	 * 0x00171f54, 0x00171fa8, 0, 0} [V*] - classify on two shape words */
	return s->reg[1] >= 0x00080000u && s->reg[1] < 0x00120000u &&
	       s->reg[4] >= 0x00170000u && s->reg[4] < 0x00173000u;
}

static unsigned int hcc_link_perlink_set(const struct hcc_link_snap *s)
{
	unsigned int i, n = 0;

	for (i = 0; i < HCC_LINK_PERLINK_COUNT; i++)
		if (s->perlink[i])
			n++;
	return n;
}

static const char *hcc_link_consumer_str(const struct hcc_link_state *st)
{
	if (!st->have_last)
		return "UNKNOWN (run: echo 1 > hcc_link_wake)";
	return st->last_consumed ? "ALIVE (last id-1 wake consumed)"
				 : "DEAD (firmware did not consume the id-1 wake)";
}


/* ---------------------------------------------------------------------
 * hcc_link_status: read-only observability pack
 * ------------------------------------------------------------------- */
static ssize_t hcc_link_status_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hcc_link_state *st = hcc_link_single;
	struct hcc_link_snap s;
	unsigned int i, set;
	bool reg_ok;
	int pos = 0;

	if (!ctx || !st || st->ctx != ctx)
		return -ENODEV;
	hcc_link_take_snapshot(ctx, &s);
	set = hcc_link_perlink_set(&s);
	reg_ok = s.reg_ok_window && hcc_link_registry_populated(&s);

	pos += sysfs_emit_at(buf, pos,
		"HCC link/event state (all reads; nothing written)\n");
	pos += sysfs_emit_at(buf, pos,
		"boot: fw_loaded=%d cpu_released=%d gate_announced=%d windows=%d\n",
		ctx->fw_loaded, ctx->cpu_released, ctx->gate_announced,
		ctx->windows_programmed);
	pos += sysfs_emit_at(buf, pos,
		"mailbox: out[0]=0x%08x out[1]=0x%08x out[2]=0x%08x ack(out3)=0x%08x rearm(out4)=0x%08x\n",
		s.out0, s.out1, s.out2, s.ack, s.rearm);
	pos += sysfs_emit_at(buf, pos,
		"mailbox: out[5]=0x%08x (READ ONLY, never written) d2h_doorbell=0x%08x ete_intr=0x%08x\n",
		s.out5, s.d2h_doorbell, s.ete_intr);
	pos += sysfs_emit_at(buf, pos,
		"firmware: exc_mask=0x%04x state=%u boot_slot=0x%08x g_ptr=0x%08x\n",
		s.exc_mask, s.state, s.boot_slot, s.g_ptr);
	if (s.g_ok)
		pos += sysfs_emit_at(buf, pos,
			"g: +0x9c=%08x +0xb4(armed)=0x%08x +0xb8(pending)=0x%08x +0x2cc=0x%08x\n",
			s.g_out1_ptr, s.g_armed, s.g_pending, s.g_2cc);
	else
		pos += sysfs_emit_at(buf, pos,
			"g: pointer not plausible (window/boot state)\n");
	pos += sysfs_emit_at(buf, pos,
		"instance@0x10c190: reg0=%08x reg4=%08x +190=%08x +2b8=%08x +2cc=%08x +4b0=%08x\n",
		s.inst_reg0, s.inst_reg1, s.inst_190, s.inst_2b8,
		s.inst_2cc, s.inst_4b0);
	pos += sysfs_emit_at(buf, pos,
		"registry rec0@0x171fb4: %08x %08x %08x %08x %08x %08x %08x %08x\n",
		s.reg[0], s.reg[1], s.reg[2], s.reg[3], s.reg[4], s.reg[5],
		s.reg[6], s.reg[7]);
	for (i = 0; i < HCC_LINK_PERLINK_COUNT; i++)
		pos += sysfs_emit_at(buf, pos,
			"per-link %u @%08x: +4b0=%08x +4b4=%08x\n",
			i, hcc_link_perlink_ca[i], s.perlink[i],
			s.perlink_4b4[i]);
	pos += sysfs_emit_at(buf, pos,
		"ETE: SR cns=%08x/%08x/%08x  DR fill=%08x/%08x/%08x/%08x\n",
		s.sr_cns[0], s.sr_cns[1], s.sr_cns[2],
		s.dr_fill[0], s.dr_fill[1], s.dr_fill[2], s.dr_fill[3]);
	pos += sysfs_emit_at(buf, pos,
		"wake probes: attempts=%u consumed=%u timeouts=%u\n",
		st->wake_attempts, st->wake_consumed, st->wake_timeouts);
	if (st->have_last)
		pos += sysfs_emit_at(buf, pos,
			"last wake: out0 %08x->%08x->%08x out2 %08x->%08x->%08x polls=%u out5=%08x out1=%08x\n",
			st->out0_before, st->out0_write, st->out0_after,
			st->out2_before, st->out2_write, st->out2_after,
			st->polls, st->out5_read, st->out1_read);

	pos += sysfs_emit_at(buf, pos,
		"VERDICT: g=%s registry=%s link_ctx=%u/%u %s h2d_consumer=%s\n",
		s.g_ok ? "valid" : "invalid",
		reg_ok ? "populated" : "unexpected",
		set, (unsigned int)HCC_LINK_PERLINK_COUNT,
		set ? "SET" : "UNSET", hcc_link_consumer_str(st));
	if (!ctx->cpu_released)
		pos += sysfs_emit_at(buf, pos,
			"next: not released yet; run the boot procedure first (hcc_link_plan)\n");
	else if (!st->have_last)
		pos += sysfs_emit_at(buf, pos,
			"next: echo 1 > hcc_link_wake - the id-1 wake decides whether ANY H2D frame can matter\n");
	else if (!st->last_consumed)
		pos += sysfs_emit_at(buf, pos,
			"next: consumer DEAD -> no host H2D frame can be the missing input; escalate to the firmware-internal lane (SPEC sec 6)\n");
	else if (!set)
		pos += sysfs_emit_at(buf, pos,
			"next: consumer ALIVE but link_ctx UNSET -> firmware-internal bring-up still missing; do NOT fabricate RAM (SPEC sec 6)\n");
	else
		pos += sysfs_emit_at(buf, pos,
			"next: link/event contexts present; continue with the driver-replica lane\n");
	return pos;
}
static DEVICE_ATTR_RO(hcc_link_status);

/* ---------------------------------------------------------------------
 * hcc_link_plan: the attended ladder and stop rules (text only)
 * ------------------------------------------------------------------- */
static ssize_t hcc_link_plan_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int pos = 0;

	pos += sysfs_emit_at(buf, pos,
		"HCC link/event bench ladder (host-only; attended)\n");
	pos += sysfs_emit_at(buf, pos,
		"L0: cat hcc_link_status - read-only baseline (record it)\n");
	pos += sysfs_emit_at(buf, pos,
		"L1: echo 1 > hcc_link_wake - vendor-attested payload-free mailbox id-1 wake\n");
	pos += sysfs_emit_at(buf, pos,
		"    consumed (out[0] bit1 clears, out[2] reads 8, out[5] reads 1) => H2D consumer ALIVE\n");
	pos += sysfs_emit_at(buf, pos,
		"L2: cat hcc_link_status - classify link_ctx (four +0x4b0 words)\n");
	pos += sysfs_emit_at(buf, pos,
		"L3: ONLY if the h2d_* injector is loaded+armed and out[0]==0: ONE group=0 id=0 dst=0 frame\n");
	pos += sysfs_emit_at(buf, pos,
		"    echo 'group=0 id=0 dst=0' > h2d_inject (payload-free firmware stub handler 0x41695)\n");
	pos += sysfs_emit_at(buf, pos, "STOP RULES\n");
	pos += sysfs_emit_at(buf, pos,
		"- never post mailbox id 5 (firmware BUG trap); never write CA 0x400392f0 (out[5])\n");
	pos += sysfs_emit_at(buf, pos,
		"- wake <= 3 attempts per session; do not clear out[0] after a timeout (that hides a dead consumer)\n");
	pos += sysfs_emit_at(buf, pos,
		"- consumer DEAD => stop the H2D lane entirely; no frame/payload experiment is justified\n");
	pos += sysfs_emit_at(buf, pos,
		"- never write device RAM / per-link +0x4b0 (external-abort risk, abort-rca sec 10.4)\n");
	pos += sysfs_emit_at(buf, pos,
		"- anomaly (staged abort frame change, out[1] new bits, oops) => power-cycle; do not retry in-session\n");
	return pos;
}
static DEVICE_ATTR_RO(hcc_link_plan);


/* ---------------------------------------------------------------------
 * hcc_link_wake: one bounded, vendor-attested H2D consumer-liveness probe
 *
 * Posts ONLY mailbox id 1 (BIT(1)) - the vendor's payload-free wake path -
 * then polls out[0] for consumption.  A real consume shows: out[0] bit1
 * cleared by the device dispatcher, out[2] overwritten with 8 (the device's
 * re-arm write), and out[5] ack = 1 (device-written; the host only reads it).
 * ------------------------------------------------------------------- */
static ssize_t hcc_link_wake_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct hi5622_pci *ctx = pci_get_drvdata(to_pci_dev(dev));
	struct hcc_link_state *st = hcc_link_single;
	u32 i, v;
	int ret;

	if (!ctx)
		return -ENODEV;
	if (!st || st->ctx != ctx)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;

	mutex_lock(&st->lock);
	if (!ctx->windows_programmed) {
		dev_err(dev, HCC_LINK_LOG_PREFIX
			"refusing: endpoint iATU windows not programmed (prepare first)\n");
		ret = -EIO;
		goto out;
	}
	if (!ctx->cpu_released) {
		dev_err(dev, HCC_LINK_LOG_PREFIX
			"refusing: CPU not released - the vendor posts the id-1 wake only against a live firmware\n");
		ret = -EPERM;
		goto out;
	}
	if (st->wake_attempts >= HCC_LINK_WAKE_MAX) {
		dev_err(dev, HCC_LINK_LOG_PREFIX
			"refusing: %u wake probes already done this session (bench limit %u; reload is safe only before release)\n",
			st->wake_attempts, HCC_LINK_WAKE_MAX);
		ret = -EBUSY;
		goto out;
	}
	st->out0_before = hcc_link_read_io(ctx, HCC_LINK_CA_H2D_PENDING);
	if (st->out0_before) {
		dev_err(dev, HCC_LINK_LOG_PREFIX
			"refusing: out[0]=0x%08x is not 0 - something else is in flight; drain it first (h2d_clear only for a stale host-set word)\n",
			st->out0_before);
		ret = -EBUSY;
		goto out;
	}
	st->out2_before = hcc_link_read_io(ctx, HCC_LINK_CA_H2D_DOORBELL);

	/* the two host-owned mailbox writes (vendor pcie_msg_send order) */
	pciet_writel_checked(ctx, HI5622_BAR0_H2D_PENDING, HCC_LINK_WAKE_BIT);
	st->out0_write = hcc_link_read_io(ctx, HCC_LINK_CA_H2D_PENDING);
	pciet_writel_checked(ctx, HI5622_BAR0_H2D_DOORBELL,
			     st->out2_before | 1u);
	st->out2_write = hcc_link_read_io(ctx, HCC_LINK_CA_H2D_DOORBELL);

	for (i = 0; i < HCC_LINK_WAKE_POLLS; i++) {
		v = hcc_link_read_io(ctx, HCC_LINK_CA_H2D_PENDING);
		if (!(v & HCC_LINK_WAKE_BIT))
			break;
		udelay(HCC_LINK_WAKE_POLL_US);
	}
	st->polls = i;
	st->out0_after = hcc_link_read_io(ctx, HCC_LINK_CA_H2D_PENDING);
	st->out2_after = hcc_link_read_io(ctx, HCC_LINK_CA_H2D_DOORBELL);
	st->out5_read = hcc_link_read_io(ctx, HCC_LINK_CA_OUT5);	/* READ */
	st->out1_read = hcc_link_read_io(ctx, HI5622_CA_D2H_BITMAP);
	st->last_consumed = !(st->out0_after & HCC_LINK_WAKE_BIT);
	st->have_last = true;
	st->wake_attempts++;

	if (st->last_consumed) {
		st->wake_consumed++;
		dev_info(dev, HCC_LINK_LOG_PREFIX
			 "id-1 wake CONSUMED after %u x %u us: out[0] %08x->%08x out[2] %08x->%08x out[5]=%08x (read-only) out[1]=%08x - firmware H2D message layer is alive\n",
			 st->polls, HCC_LINK_WAKE_POLL_US,
			 st->out0_before, st->out0_after,
			 st->out2_before, st->out2_after,
			 st->out5_read, st->out1_read);
		ret = (int)count;
	} else {
		st->wake_timeouts++;
		dev_warn(dev, HCC_LINK_LOG_PREFIX
			 "id-1 wake NOT consumed within %u ms: out[0]=0x%08x out[2]=0x%08x out[5]=0x%08x - firmware parked before/inside its H2D message layer; a host H2D frame cannot be the missing input (see hcc_link_status / REPORT sec 4)\n",
			 HCC_LINK_WAKE_POLLS * HCC_LINK_WAKE_POLL_US / 1000,
			 st->out0_after, st->out2_after, st->out5_read);
		ret = -ETIMEDOUT;
	}
out:
	mutex_unlock(&st->lock);
	return ret;
}
static DEVICE_ATTR_WO(hcc_link_wake);

static struct attribute *pciet_hcc_link_attrs[] = {
	&dev_attr_hcc_link_status.attr,
	&dev_attr_hcc_link_plan.attr,
	&dev_attr_hcc_link_wake.attr,
	NULL,
};
ATTRIBUTE_GROUPS(pciet_hcc_link);

/* ---------------------------------------------------------------------
 * Probe / remove (called by pciet_main.c; host-side allocation only)
 * ------------------------------------------------------------------- */
int pciet_hcc_link_probe(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	struct hcc_link_state *st;
	int ret;

	hcc_link_assert_contract();

	mutex_lock(&hcc_link_reg_lock);
	if (hcc_link_single) {
		mutex_unlock(&hcc_link_reg_lock);
		dev_warn(dev, HCC_LINK_LOG_PREFIX
			 "already registered; keeping the first instance\n");
		return -EBUSY;
	}
	st = devm_kzalloc(dev, sizeof(*st), GFP_KERNEL);
	if (!st) {
		mutex_unlock(&hcc_link_reg_lock);
		return -ENOMEM;
	}
	st->ctx = ctx;
	mutex_init(&st->lock);
	hcc_link_single = st;
	mutex_unlock(&hcc_link_reg_lock);

	ret = device_add_groups(dev, pciet_hcc_link_groups);
	if (ret) {
		mutex_lock(&hcc_link_reg_lock);
		hcc_link_single = NULL;
		mutex_unlock(&hcc_link_reg_lock);
		dev_err(dev, HCC_LINK_LOG_PREFIX "device_add_groups: %d\n", ret);
		return ret;
	}
	dev_info(dev, HCC_LINK_LOG_PREFIX
		 "ready: hcc_link_status (read-only link/event state), hcc_link_wake (id-1 consumer-liveness probe), hcc_link_plan; no device writes in probe\n");
	return 0;
}

void pciet_hcc_link_remove(struct hi5622_pci *ctx)
{
	struct device *dev = &ctx->pdev->dev;
	struct hcc_link_state *st;

	mutex_lock(&hcc_link_reg_lock);
	st = hcc_link_single;
	if (st && st->ctx == ctx)
		hcc_link_single = NULL;
	else
		st = NULL;
	mutex_unlock(&hcc_link_reg_lock);
	if (!st)
		return;
	device_remove_groups(dev, pciet_hcc_link_groups);
	dev_info(dev, HCC_LINK_LOG_PREFIX "removed\n");
}

