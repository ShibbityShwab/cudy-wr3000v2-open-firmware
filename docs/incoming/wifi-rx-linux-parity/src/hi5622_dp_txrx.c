/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_dp_txrx.c - kernel side of the first MAC data-plane TX/RX frames
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (lane wifi-dp-txrx, shared task task-122).
 * Portable, host-tested logic lives in dp_txrx.c; this file owns the one
 * coherent TX frame buffer, the sysfs actions and the device reads/writes.
 *
 * Bench model (project rules): every action is attended and explicitly
 * triggered from sysfs; dp_tx_frame is the FIRST frame write and requires
 * dp_txq_sync (key 0x0320) to have been committed; one frame per write with a
 * small per-load budget; no retry after a failed step; no register is touched
 * outside MAC band 0; CA 0x400392f0 and the RC misc window are never touched.
 * The RX action is read-only (it captures MAC counters + host-DRAM RX
 * descriptors and moves NO ring pointer).
 */
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <linux/types.h>

#include "hi5622_driver.h"
#include "hi5622_hcc.h"
#include "hi5622_dp.h"
#include "hi5622_dp_txrx.h"
#include "dp_rings.h"
#include "dp_txrx.h"
#include "hi5622_rx_live.h"
#include "hi5622_rx_parity.h"

#define DPTX_FRAME_OWNER 0x54585258u /* "TXRX" host bookkeeping word */
#define DPTX_TX_POST_MAX 8u          /* per-load first-frame budget */
#define DPTX_RX_POLL_MS 2000u        /* bounded RX observation window */
#define DPTX_RX_POLL_STEP_MS 50u
#define DPTX_TX_COMPLETE_STEP_MS 25u

/* wifi-dp-tuning (shared task task-134) knobs.  The defaults are the vendor
 * behaviour the lane's RE found; the attended ladder can override per load. */
static unsigned int dptx_txq_publish_en = 1u;
module_param_named(txq_publish, dptx_txq_publish_en, uint, 0644);
MODULE_PARM_DESC(txq_publish,
	"publish the vendor TXQ-table queue state after each TX post (0 = old wptr-only behaviour)");
static unsigned int dptx_tx_complete_ms = 2000u;
module_param_named(tx_complete_ms, dptx_tx_complete_ms, uint, 0644);
MODULE_PARM_DESC(tx_complete_ms,
	"bounded dp_tx_complete_poll observation window in milliseconds");
static unsigned int dptx_mac_tail_writes = 0u;
module_param_named(mac_tail_writes, dptx_mac_tail_writes, uint, 0644);
MODULE_PARM_DESC(mac_tail_writes,
	"opt-in experiment: write the vendor-live value 4 to MAC band0 +0x5c and +0x6c in dp_mac_tail_init [U]");

/* wifi-rx-static (shared task task-137).  FAIL-CLOSED: the RX consumer path
 * (live-bank arm + completion drain) is disabled unless rx_enable=1 is set on
 * the module load.  rx_clear_status additionally W1Cs the observed host MAC
 * interrupt status to the config block after a drain (default 0: status is
 * read only).  rx_drain_max bounds one drain action. */
static unsigned int dptx_rx_enable = 0u;
module_param_named(rx_enable, dptx_rx_enable, uint, 0644);
MODULE_PARM_DESC(rx_enable,
	"enable the fail-closed RX consumer path (live bank CA 0x40050000 + completion-ring drain); default 0 = observe only");
static unsigned int dptx_rx_clear_status = 0u;
module_param_named(rx_clear_status, dptx_rx_clear_status, uint, 0644);
MODULE_PARM_DESC(rx_clear_status,
	"after a drain, W1C the observed host MAC interrupt status to MAC band0 +0x44 (vendor clear path; default 0)");
static unsigned int dptx_rx_drain_max = HIL_RX_DRAIN_MAX;
module_param_named(rx_drain_max, dptx_rx_drain_max, uint, 0644);
MODULE_PARM_DESC(rx_drain_max,
	"maximum completion-ring entries consumed by one dp_rx_drain write (1..64)");

/* wifi-rx-watermark (shared task task-156): sustained free-ring refill.  The
 * drain re-publishes buffers handed back by validated completions; a backlog
 * larger than one bounded drain action used to leak posted entries (their
 * free-ring slots were never re-published), so sustained EAPOL/data traffic
 * could starve the MAC at its prefetch watermark.  The watermark pass keeps
 * both free rings posted at >= rx_refill_wm percent of depth as long as
 * returned buffers exist (ath11k-style >= 3/4; iwlwifi RX_STALLED is the
 * avoided failure mode).  It is gated exactly like the rest of the RX path
 * (rx_enable=1 + dp_rx_arm + write gate); 0 is the old rx-static behaviour.
 * Values above 100 are clamped to 100 at use (an invalid value can never
 * over-publish or over-refill; the clamp direction is fail-closed). */
static unsigned int dptx_rx_refill_wm = HIL_RX_WM_PCT_DEF;
module_param_named(rx_refill_wm, dptx_rx_refill_wm, uint, 0644);
MODULE_PARM_DESC(rx_refill_wm,
	"RX free-ring refill watermark as a percent of ring depth (0..100; 0=off=old behaviour, default 75 = keep >=3/4 posted); the drain re-publishes completed buffers when the posted count drops below it");

/* wifi-rx-linux-parity (shared task task-166).  The Linux RX-enable order has
 * steps this driver did not implement (see hi5622_rx_parity.h section 1).  The
 * ones that can be applied safely are gated here, fail-closed:
 *   rx_entry_wait  bounded completion-entry commit wait (0 = old single read)
 *   rx_ring_kick   opt-in second producer store ("ring kick") after arm
 *   rx_ctl_*       one-shot allowlisted RX-control candidate-word experiment
 *   rx_legacy_bank opt-in legacy reads of config +0x4c/+0x50/+0x54, which the
 *                  vendor never reads (read-side hazard candidate) */
static unsigned int dptx_rx_entry_wait = RXP_ENTRY_WAIT_DEF;
module_param_named(rx_entry_wait, dptx_rx_entry_wait, uint, 0644);
MODULE_PARM_DESC(rx_entry_wait,
	"bounded completion-entry commit wait in re-reads (default 32; 0 = old single-read behaviour; a still-zero entry is a spurious producer advance, not corruption)");
static unsigned int dptx_rx_ring_kick = 0u;
module_param_named(rx_ring_kick, dptx_rx_ring_kick, uint, 0644);
MODULE_PARM_DESC(rx_ring_kick,
	"opt-in attended experiment: re-publish the two free-ring producer words with the exact vendor zero-extended u16 after dp_rx_arm (one shot; default 0)");
static unsigned int dptx_rx_ctl_enable = 0u;
module_param_named(rx_ctl_enable, dptx_rx_ctl_enable, uint, 0644);
MODULE_PARM_DESC(rx_ctl_enable,
	"opt-in attended experiment: allow ONE allowlisted RX-control candidate-word write (default 0 = refused)");
static unsigned int dptx_rx_ctl_off = 0xffffffffu;
module_param_named(rx_ctl_off, dptx_rx_ctl_off, uint, 0644);
MODULE_PARM_DESC(rx_ctl_off,
	"candidate word offset inside MAC band 0 for rx_ctl_enable (allowlist 0x08/0x0c/0x14/0x18 only; default invalid)");
static unsigned int dptx_rx_ctl_set = 0u;
module_param_named(rx_ctl_set, dptx_rx_ctl_set, uint, 0644);
MODULE_PARM_DESC(rx_ctl_set,
	"bits to set in the one-shot RX-control candidate word (set & clr must be 0)");
static unsigned int dptx_rx_ctl_clr = 0u;
module_param_named(rx_ctl_clr, dptx_rx_ctl_clr, uint, 0644);
MODULE_PARM_DESC(rx_ctl_clr,
	"bits to clear in the one-shot RX-control candidate word (set & clr must be 0)");
static unsigned int dptx_rx_legacy_bank = 0u;
module_param_named(rx_legacy_bank, dptx_rx_legacy_bank, uint, 0644);
MODULE_PARM_DESC(rx_legacy_bank,
	"opt-in: let dp_rx_poll read config +0x4c/+0x50/+0x54 (the vendor never reads those words; default 0 = live bank only)");

/* Documentation-example address (02:00:... locally administered); never a
 * per-unit value. The project already documents wlan0 as 02:00:68:69:35:01. */
static const u8 dptx_example_addr[6] = { 0x02, 0x00, 0x68, 0x69, 0x35, 0x01 };

struct hi5622_dp_txrx {
	struct list_head node;
	struct hi5622_pci *ctx;
	struct mutex lock;

	bool bound;
	bool failed;
	bool tx_frame_ready;
	u32 tx_posted;
	u32 tx_wptr;
	u32 tx_completion_rptr;
	u32 tx_last_devva;
	u32 tx_last_type;
	u32 tx_last_ring_index;
	u32 tx_last_tid;
	u32 tx_last_vap_type;
	u32 tx_last_mpdu_len;
	u8  tx_last_mpdu[DP_TXRX_MAX_MPDU];
	u8  tx_last_entry[DP_TXRX_TXE_BYTES];
	u8  tx_last_table[DP_TXRX_TABLE_ENTRY_BYTES];

	void *tx_frame;
	u64  tx_frame_dma;

	/* E1 objects (copied from the bind snapshot) */
	void *tx_msdu_rings;
	u32  tx_msdu_rings_dma;
	u32  txq_table_devva;
	void *rx_norm_ring;
	void *rx_small_ring;
	void *rx_cmp_ring;
	void **rx_norm_cpu;
	void **rx_small_cpu;
	u32  msdu_table_ca;
	u32  ready_payload;

	/* wifi-dp-tuning (task-134): TXQ-table queue-state publish + per-TID
	 * ring indices (vendor: one packed producer index per (user,tid)). */
	void *txq_table;
	void *txba;
	u32  txba_bytes;
	u32  tx_tid_wptr[DP_TXRX_TABLE_TIDS];
	u32  tx_tid_count[DP_TXRX_TABLE_TIDS];
	u32  tx_tid_bytes[DP_TXRX_TABLE_TIDS];
	u16  tx_tid_sn[DP_TXRX_TABLE_TIDS];
	bool txq_published;
	u32  txq_publish_count;
	bool tx_complete_polled;
	u32  tx_complete_waited_ms;
	u32  tx_complete_pending;
	u32  tx_complete_verdict;
	u32  tx_complete_table[3];
	u32  tx_complete_mac44;
	u32  tx_complete_mac4c;
	u32  tx_complete_txba[4];
	bool mac_tail_done;
	u32  mac_tail_values[2];

	/* RX capture evidence (read-only; ring pointers are not moved) */
	struct dp_txrx_rx_state rx_state;
	bool rx_polled;
	unsigned long rx_waited_ms;
	u32 rx_cmp_wptr;
	u32 rx_cmp_rptr;
	u32 rx_norm_rptr;
	u32 rx_small_rptr;
	u32 rx_pending;
	u32 rx_last_cmp_raw;
	u32 rx_cap_count;
	u32 rx_scanned_norm;
	u32 rx_scanned_small;
	struct dp_txrx_rx_capture rx_caps[DP_TXRX_RX_CAPTURE_MAX];

	/* wifi-rx-static (task-137): live-bank RX arm + completion drain state.
	 * The two free rings are kept logically full: rx_*_prod is the host
	 * packed producer (0x8000 after dp_setup filled all slots), and the
	 * device consumer is only ever read from the live bank, never from the
	 * config-block +0x4c/+0x50/+0x54 words (the vendor never reads those). */
	const u32 *rx_norm_dma;
	const u32 *rx_small_dma;
	bool rx_armed;
	bool rx_failed;
	u32  rx_arm_status;
	u32  rx_arm_mask;
	u32  rx_arm_norm_rptr;
	u32  rx_arm_small_rptr;
	u32  rx_arm_cmp_wptr;
	u32  rx_live_status;
	u32  rx_live_norm_rptr;
	u32  rx_live_small_rptr;
	u32  rx_live_cmp_wptr;
	u32  rx_norm_prod;
	u32  rx_small_prod;
	u32  rx_cmp_cons;
	u32  rx_last_norm_rptr;
	u32  rx_last_small_rptr;
	u32  rx_drain_actions;
	u32  rx_drained;
	u32  rx_bad_entry;
	u32  rx_chain_refused;
	u32  rx_refilled_norm;
	u32  rx_refilled_small;
	u32  rx_refill_norm_q[HIL_RX_NORM_DEPTH];
	u32  rx_refill_small_q[HIL_RX_SMALL_DEPTH];
	u16  rx_refill_norm_head;
	u16  rx_refill_norm_count;
	u16  rx_refill_small_head;
	u16  rx_refill_small_count;
	u32  rx_new_cap_count;
	/* wifi-rx-watermark (task-156): sustained refill telemetry */
	u32  rx_wm_posted_norm;
	u32  rx_wm_posted_small;
	u32  rx_wm_target_norm;
	u32  rx_wm_target_small;
	u32  rx_wm_trips;
	u32  rx_wm_credit_low;
	u32  rx_wm_refilled_norm;
	u32  rx_wm_refilled_small;
	bool rx_wm_low;

	/* wifi-rx-linux-parity (task-166): commit-wait / kick / candidate-control
	 * telemetry and the log-only stall classification. */
	u32  rx_commit_waits;
	u32  rx_commit_ok;
	u32  rx_spurious;
	u32  rx_spurious_closed;
	u32  rx_parity_probes;
	bool rx_kick_done;
	u32  rx_kick_values[2];
	bool rx_ctl_done;
	u32  rx_ctl_off;
	u32  rx_ctl_old;
	u32  rx_ctl_value;
	u32  rx_legacy_reads;
	int  rx_stall_class;
	u32  rx_last_err;
};

static LIST_HEAD(dptx_list);
static DEFINE_MUTEX(dptx_list_lock);

static struct hi5622_dp_txrx *dptx_find(struct hi5622_pci *ctx)
{
	struct hi5622_dp_txrx *t;

	list_for_each_entry(t, &dptx_list, node) {
		if (t->ctx == ctx)
			return t;
	}
	return NULL;
}

static struct hi5622_pci *dptx_ctx_from_dev(struct device *dev)
{
	return pci_get_drvdata(to_pci_dev(dev));
}

static struct hi5622_dp_txrx *dptx_from_dev(struct device *dev)
{
	struct hi5622_pci *ctx = dptx_ctx_from_dev(dev);

	if (!ctx)
		return NULL;
	return dptx_find(ctx);
}

/* MAC band-0 word read through the driver's range-checked translation. */
static u32 dptx_mac_read(struct hi5622_pci *ctx, u32 off)
{
	u32 bar0 = hi5622_ca_to_bar0(DP_MAC_CA_BAND0 + off);

	if (!bar0)
		return 0xffffffffu;
	return readl(ctx->bar0 + bar0);
}

/* Only dp_mac_tail_init (opt-in experiment) writes a MAC word, and only the
 * two fixed vendor-live tails; the same range-checked translation helper is
 * used for every access. */
static int dptx_mac_write_checked(struct hi5622_pci *ctx, u32 off, u32 val)
{
	u32 bar0 = hi5622_ca_to_bar0(DP_MAC_CA_BAND0 + off);

	if (!bar0)
		return -EFAULT;
	writel(val, ctx->bar0 + bar0);
	wmb();
	return 0;
}

/* The live device-side ring/interrupt block (vendor machw array word 8).  It
 * is a *different* CA from the host-interface config block the 14-write
 * program touches: the vendor's get_host_mac_int_status reads *[machw+0x20]
 * = CA 0x40050000 (band 0) and the RX ring binders store +8/+0xc/+0x10 of
 * that word for the norm rptr, small rptr and completion wptr.  Our region 3
 * window (CA 0x40000000..0x4011ffff) covers it, so the same range-checked
 * translation applies. */
static u32 dptx_mac_read_live(struct hi5622_pci *ctx, u32 off)
{
	u32 bar0 = hi5622_ca_to_bar0(HIL_RX_LIVE_CA_BAND0 + off);

	if (!bar0)
		return 0xffffffffu;
	return readl(ctx->bar0 + bar0);
}

static void dptx_live_read(struct hi5622_pci *ctx, u32 *status, u32 *norm_rptr,
			   u32 *small_rptr, u32 *cmp_wptr)
{
	*status = dptx_mac_read_live(ctx, HIL_RX_LIVE_STATUS);
	*norm_rptr = dptx_mac_read_live(ctx, HIL_RX_LIVE_NORM_RPTR);
	*small_rptr = dptx_mac_read_live(ctx, HIL_RX_LIVE_SMALL_RPTR);
	*cmp_wptr = dptx_mac_read_live(ctx, HIL_RX_LIVE_CMP_WPTR);
}

/* Read the live ring bank and refuse the 0xffffffff transport-miss value in
 * one place (wifi-rx-watermark task-156 uses this for the fresh consumer
 * snapshot the refill watermark is computed against). */
static int dptx_live_read_checked(struct hi5622_pci *ctx, u32 *status,
				  u32 *norm_rptr, u32 *small_rptr, u32 *cmp_wptr)
{
	dptx_live_read(ctx, status, norm_rptr, small_rptr, cmp_wptr);
	if (*status == 0xffffffffu || *norm_rptr == 0xffffffffu ||
	    *small_rptr == 0xffffffffu || *cmp_wptr == 0xffffffffu)
		return -EFAULT;
	return 0;
}

/* Publish a packed 16-bit ring index to a config-block word with the exact
 * vendor write width.  The luofu vendor hal_ring_set_sw2hw stores a
 * zero-extended u16 (index[14:0] | phase<<15), not a 32-bit RMW [V
 * wifi-rx-static REPORT sec. 4]; the inherited RMW preserved an upper half the
 * vendor never writes, which is a divergence on the same registers the MAC
 * uses for its ring pointers.  Rule (hi5622_rx_parity.h sec. 2): refuse an
 * unexpected non-zero upper half instead of clobbering it, write exactly the
 * u16, and verify the low half plus the (expected zero) upper half. */
static int dptx_publish16(struct hi5622_pci *ctx, u32 off, u32 packed,
			  u32 *readback)
{
	u32 old, rb;

	if (packed > RXP_PTR_MAX)
		return -ERANGE;
	old = dptx_mac_read(ctx, off);
	if (old == 0xffffffffu)
		return -EFAULT;
	if ((old & 0xffff0000u) != 0u) {
		dev_err(&ctx->pdev->dev,
			"DP rx: MAC +0x%02x upper half 0x%08x is non-zero - the vendor zero-extended u16 publish cannot produce this state; refused (no write)\n",
			off, old & 0xffff0000u);
		return -ERANGE;
	}
	if (dptx_mac_write_checked(ctx, off, packed) != 0)
		return -EFAULT;
	rb = dptx_mac_read(ctx, off);
	if (readback)
		*readback = rb;
	if ((rb & 0xffffu) != packed)
		return -EIO;
	if ((rb & 0xffff0000u) != 0u)
		dev_warn(&ctx->pdev->dev,
			 "DP rx: MAC +0x%02x read back upper half 0x%08x (vendor path leaves it zero; recorded, not retried)\n",
			 off, rb & 0xffff0000u);
	return 0;
}

static u16 dptx_le16(const u8 *p)
{
	return (u16)(p[0] | ((u16)p[1] << 8));
}

static const char *dptx_gate_name(int rc)
{
	switch (rc) {
	case 0:         return "ok";
	case -ENODEV:   return "no_hcc_service";
	case -EBUSY:    return "not_armed_or_not_released";
	case -EPERM:    return "phase_or_ctx_or_txq_gate";
	case -EALREADY: return "already_done";
	case -EIO:      return "device_write_failed";
	default:        return "error";
	}
}

/* Write-path gate: identical to the dp_setup gate plus "this load's 0x0320\n * TXQ-table address sync was committed". */
static int dptx_write_gate(struct hi5622_pci *ctx)
{
	struct hi5622_hcc_service *h = ctx->hcc;
	struct hcc_core *core;

	if (!h)
		return -ENODEV;
	core = &h->hcc.core;
	if (!ctx->windows_programmed || !h->armed)
		return -EBUSY;
	if (!core->released || !ctx->cpu_released || !core->boot_latched)
		return -EBUSY;
	if (core->phase != HCC_PHASE_READY_TXQ_SYNC &&
	    core->phase != HCC_PHASE_ACTIVE)
		return -EPERM;
	if (!h->h2d_ctx_create_sent || !h->h2d_txq_addr_sent)
		return -EPERM;
	return 0;
}

/* ------------------------------------------------------------------ */
/* device-RAM MSDU-info table helpers (user 0, requested TID)         */
/* ------------------------------------------------------------------ */
static int dptx_table_bar0(struct hi5622_dp_txrx *t, unsigned int tid,
			    u32 *bar0, u32 *ca)
{
	u32 acp;

	if (dp_msdu_table_entry_ca(t->msdu_table_ca, DP_TXRX_TX_USER_DEFAULT,
				   tid, ca, &acp) != DP_EOK)
		return -EINVAL;
	*bar0 = hi5622_ca_to_bar0(acp);
	if (!*bar0)
		return -EFAULT;
	return 0;
}

static int dptx_table_snapshot_tid(struct hi5622_dp_txrx *t,
				   unsigned int tid, u32 d[3])
{
	u32 bar0 = 0, ca = 0;
	u32 i;

	if (dptx_table_bar0(t, tid, &bar0, &ca) != 0)
		return -EFAULT;
	for (i = 0; i < DP_TXRX_TABLE_ENTRY_DWORDS; i++)
		d[i] = readl(t->ctx->bar0 + bar0 + 4u * i);
	return 0;
}

static int dptx_table_snapshot(struct hi5622_dp_txrx *t, u32 d[3])
{
	return dptx_table_snapshot_tid(t, DP_TXRX_TX_USER_DEFAULT, d);
}

/* Publish the packed producer index into table dword1 exactly like the vendor
 * ring push (dmb ish before the store; the vendor then reads +8 back). */
static int dptx_table_publish(struct hi5622_dp_txrx *t, unsigned int tid,
			      u32 wptr, u32 ring_devva, u32 d[3])
{
	u32 bar0 = 0, ca = 0;
	u32 i;
	int rc;

	rc = dptx_table_bar0(t, tid, &bar0, &ca);
	if (rc)
		return rc;
	dma_wmb();
	writel(wptr, t->ctx->bar0 + bar0 + 4u);
	wmb();
	for (i = 0; i < DP_TXRX_TABLE_ENTRY_DWORDS; i++)
		d[i] = readl(t->ctx->bar0 + bar0 + 4u * i);
	if (d[0] != ring_devva || d[1] != wptr)
		return -EIO;
	return 0;
}

/* ------------------------------------------------------------------ */
/* TX frame actions                                                   */
/* ------------------------------------------------------------------ */
static int dptx_build_frame(struct hi5622_dp_txrx *t, u32 type, dp_u8 *mpdu,
			    size_t cap, size_t *mpdu_len, dp_u16 *fc,
			    dp_u8 *vap, dp_u8 *tid, dp_u8 *hdr_flag)
{
	dp_u16 seq = (dp_u16)(t->tx_posted & 0x0fffu);
	u16 fc_le = 0;
	int rc;

	switch (type) {
	case 1u: /* probe request, management, broadcast DA */
		rc = dp_txrx_build_probe_req(mpdu, cap, dptx_example_addr, seq,
					     mpdu_len);
		fc_le = DP_TXRX_FC_PROBE_REQ;
		break;
	case 2u: /* null-data, broadcast DA */
		rc = dp_txrx_build_null_data(mpdu, cap, dptx_example_addr, seq,
					     mpdu_len);
		fc_le = DP_TXRX_FC_NULL_DATA;
		break;
	case 3u: /* QoS null, broadcast DA */
		rc = dp_txrx_build_qos_null(mpdu, cap, dptx_example_addr, seq,
					    mpdu_len);
		fc_le = DP_TXRX_FC_QOS_NULL;
		break;
	case 4u: { /* broadcast ARP request (documentation IPs) */
		static const dp_u8 sip[4] = { 192u, 0u, 2u, 10u };
		static const dp_u8 tip[4] = { 192u, 0u, 2u, 1u };

		rc = dp_txrx_build_arp_bcast(mpdu, cap, dptx_example_addr, sip,
					     tip, seq, mpdu_len);
		fc_le = DP_TXRX_FC_QOS_DATA;
		break;
	}
	default:
		return -EINVAL;
	}
	if (rc != DP_TXRX_OK)
		return -EINVAL;
	*fc = fc_le;
	if (dp_txrx_frame_class(fc_le, vap, tid, hdr_flag) != DP_TXRX_OK)
		return -EINVAL;
	return 0;
}

ssize_t hi5622_dp_tx_frame_store(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct hi5622_dp_txrx *t = dptx_from_dev(dev);
	struct hi5622_pci *ctx;
	dp_u8 mpdu[DP_TXRX_MAX_MPDU];
	dp_u8 entry[DP_TXRX_TXE_BYTES];
	dp_u8 vap = 0, tid = 0, hdr_flag = 0;
	dp_u16 fc = 0;
	size_t mpdu_len = 0;
	u32 type = 0, off = 0, result = 0, ring_devva = 0, next = 0;
	u32 entry_addr = 0, d[3];
	u32 index, prev;
	int rc;

	(void)attr;
	if (!t || !t->ctx)
		return -ENODEV;
	ctx = t->ctx;
	if (sysfs_streq(buf, "1"))
		type = 1u;
	else if (sysfs_streq(buf, "2"))
		type = 2u;
	else if (sysfs_streq(buf, "3"))
		type = 3u;
	else if (sysfs_streq(buf, "4"))
		type = 4u;
	else
		return -EINVAL;

	mutex_lock(&t->lock);
	if (t->failed || !t->bound || !t->tx_frame_ready) {
		mutex_unlock(&t->lock);
		return -ENODEV;
	}
	if (t->tx_posted >= DPTX_TX_POST_MAX) {
		mutex_unlock(&t->lock);
		return -EALREADY;
	}
	rc = dptx_write_gate(ctx);
	if (rc) {
		dev_warn(dev,
			 "DP tx_frame: refused by gate '%s' - no device write (need dp_setup + dp_txq_sync in this load)\n",
			 dptx_gate_name(rc));
		mutex_unlock(&t->lock);
		return rc;
	}

	rc = dptx_build_frame(t, type, mpdu, sizeof(mpdu), &mpdu_len, &fc,
			      &vap, &tid, &hdr_flag);
	if (rc) {
		mutex_unlock(&t->lock);
		return -EINVAL;
	}
	rc = dp_txrx_tx_buffer_init(t->tx_frame, DP_TXRX_FRAME_AREA,
				    DPTX_FRAME_OWNER, mpdu, mpdu_len, &off);
	if (rc != DP_TXRX_OK) {
		dev_err(dev, "DP tx_frame: frame buffer refused (%d)\n", rc);
		t->failed = true;
		mutex_unlock(&t->lock);
		return -EINVAL;
	}
	if (off != DP_TXRX_FRAME_HOST_HDR) {
		t->failed = true;
		mutex_unlock(&t->lock);
		return -EIO;
	}
	result = (u32)t->tx_frame_dma + off; /* coherent DMA addr = device VA */
	rc = dp_txrx_txe_encode(entry, result, (dp_u16)mpdu_len, 0u, hdr_flag,
				tid, vap);
	if (rc != DP_TXRX_OK) {
		t->failed = true;
		mutex_unlock(&t->lock);
		return -EINVAL;
	}
	if (tid >= DP_TXRX_TABLE_TIDS) {
		t->failed = true;
		mutex_unlock(&t->lock);
		return -EINVAL;
	}
	index = t->tx_tid_wptr[tid] & 0x7fffu;
	if (index >= DP_TXRX_TX_RING_DEPTH) {
		t->failed = true;
		mutex_unlock(&t->lock);
		return -EIO;
	}
	entry_addr = (u32)(unsigned long)t->tx_msdu_rings +
		     tid * DP_TXRX_TX_RING_BYTES +
		     index * DP_TXRX_TXE_BYTES;
	memcpy((void *)(unsigned long)entry_addr, entry, DP_TXRX_TXE_BYTES);
	dma_wmb();
	ring_devva = dp_tx_ring_devva(t->tx_msdu_rings_dma, tid);
	next = dp_txrx_idx_next(t->tx_tid_wptr[tid], DP_TXRX_TX_RING_DEPTH);
	prev = t->tx_tid_wptr[tid];
	rc = dptx_table_publish(t, tid, next, ring_devva, d);
	if (rc) {
		dev_err(dev,
			"DP tx_frame: MSDU table write read-back FAILED (rc %d) - lane closed for this load, no retry\n",
			rc);
		t->failed = true;
		mutex_unlock(&t->lock);
		return -EIO;
	}

	t->tx_tid_wptr[tid] = next;
	t->tx_wptr = next;
	t->tx_completion_rptr = d[2];
	t->tx_tid_count[tid]++;
	t->tx_tid_bytes[tid] += (u32)mpdu_len;
	t->tx_tid_sn[tid] = (u16)(t->tx_posted & 0x0fffu);

	/* wifi-dp-tuning: publish the per-TID queue state the way the vendor's
	 * enqueue tail does (hmac_tx_skb_pop_tid ->
	 * hmac_update_user_tid_txq_table, after the ring push). */
	if (dptx_txq_publish_en && t->txq_table) {
		dma_wmb();
		dp_txrx_txq_publish(t->txq_table, DP_TXRX_TX_USER_DEFAULT, tid,
				    t->tx_tid_count[tid],
				    (dp_u32)t->tx_tid_sn[tid], true,
				    t->tx_tid_bytes[tid],
				    (dp_u16)jiffies_to_msecs(jiffies));
		dma_wmb();
		t->txq_published = true;
		t->txq_publish_count++;
	}
	t->tx_last_devva = result;
	t->tx_last_type = type;
	t->tx_last_ring_index = index;
	t->tx_last_tid = tid;
	t->tx_last_vap_type = vap;
	t->tx_last_mpdu_len = (u32)mpdu_len;
	memcpy(t->tx_last_mpdu, mpdu, mpdu_len);
	memcpy(t->tx_last_entry, entry, DP_TXRX_TXE_BYTES);
	dp_txrx_table_entry_pack(t->tx_last_table, d[0], d[1], d[2]);
	t->tx_posted++;

	if (t->txq_published)
		dev_info(dev,
			 "DP txq: published user=%u tid=%u count=%u bytes=%u sn=%u ts_ms=0x%04x flags=0x%02x (vendor hmac_update_user_tid_txq_table order, after the ring push)\n",
			 DP_TXRX_TX_USER_DEFAULT, tid, t->tx_tid_count[tid],
			 t->tx_tid_bytes[tid], (u32)t->tx_tid_sn[tid],
			 (u32)(dp_txrx_le32((const dp_u8 *)t->txq_table) & 0xffffu),
			 ((const dp_u8 *)t->txq_table)[DP_TXRX_TXQ_FLAGS_OFF]);

	dev_info(dev,
		 "DP tx_frame: type=%u fc=0x%04x mpdu=%u B buf_devva=0x%08x ring[tid=%u idx=%u]={buf=0x%08x ctl=0x%08x} wptr 0x%04x->0x%04x table@CA 0x%08x={ring=0x%08x wptr=0x%08x rptr=0x%08x} completion_pending=%d\n",
		 type, fc, (unsigned int)mpdu_len, result, tid, index,
		 dp_txrx_le32(entry), dp_txrx_le32(entry + 4),
		 prev, next, t->msdu_table_ca, d[0], d[1], d[2],
		 dp_txrx_idx_count(next, d[2], DP_TXRX_TX_RING_DEPTH));
	mutex_unlock(&t->lock);
	return (ssize_t)count;
}

ssize_t hi5622_dp_tx_status_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct hi5622_dp_txrx *t = dptx_from_dev(dev);
	u32 d[3] = { 0u, 0u, 0u };
	int pending = -1;
	int pos = 0;
	u32 i;

	(void)attr;
	if (!t || !t->ctx)
		return -ENODEV;
	mutex_lock(&t->lock);
	if (dptx_table_snapshot(t, d) == 0)
		pending = dp_txrx_idx_count(d[1], d[2],
					    DP_TXRX_TX_RING_DEPTH);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "DP tx status: bound=%u failed=%u frame_ready=%u posted=%u wptr=0x%04x completion_pending=%d\n",
			 t->bound ? 1u : 0u, t->failed ? 1u : 0u,
			 t->tx_frame_ready ? 1u : 0u, t->tx_posted,
			 t->tx_wptr, pending);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "E1 objects: msdu_table_ca=0x%08x ready_payload=0x%08x tx_rings_dma=0x%08x txq_table_devva=0x%08x rx_cmp_ring=0x%08x\n",
			 t->msdu_table_ca, t->ready_payload,
			 t->tx_msdu_rings_dma, t->txq_table_devva,
			 (u32)(unsigned long)t->rx_cmp_ring);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "MSDU table entry (user 0 tid 0) currently = {0x%08x, 0x%08x, 0x%08x}\n",
			 d[0], d[1], d[2]);
	if (t->tx_posted) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "last frame: type=%u mpdu_len=%u buf_devva=0x%08x ring_index=%u tid=%u vap_type=%u\n",
				 t->tx_last_type, t->tx_last_mpdu_len,
				 t->tx_last_devva, t->tx_last_ring_index,
				 t->tx_last_tid, t->tx_last_vap_type);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "last MSDU entry = ");
		for (i = 0; i < DP_TXRX_TXE_BYTES; i++)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos, "%02x",
					 t->tx_last_entry[i]);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "  (magic_processed=%u)\n",
				 dp_txrx_txe_is_processed(t->tx_last_entry) ? 1u : 0u);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "last table image = {0x%08x, 0x%08x, 0x%08x} (published wptr vs device rptr)\n",
				 dp_txrx_table_entry_wptr(t->tx_last_table),
				 dp_txrx_table_entry_rptr(t->tx_last_table),
				 dp_txrx_le32(t->tx_last_table));
	}
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "MAC band0 read-only: +0x00=0x%08x +0x38=0x%08x +0x3c=0x%08x +0x40=0x%08x +0x48=0x%08x +0x4c=0x%08x +0x50=0x%08x +0x54=0x%08x\n",
			 dptx_mac_read(t->ctx, 0x00u),
			 dptx_mac_read(t->ctx, DP_TXRX_MAC_REG_RXNORM_WPTR),
			 dptx_mac_read(t->ctx, DP_TXRX_MAC_REG_RXSMALL_WPTR),
			 dptx_mac_read(t->ctx, DP_TXRX_MAC_REG_RXCMP_RPTR),
			 dptx_mac_read(t->ctx, 0x48u),
			 dptx_mac_read(t->ctx, DP_TXRX_MAC_REG_RXCMP_WPTR),
			 dptx_mac_read(t->ctx, DP_TXRX_MAC_REG_RXNORM_RPTR),
			 dptx_mac_read(t->ctx, DP_TXRX_MAC_REG_RXSMALL_RPTR));
	if (t->txq_table) {
		const dp_u8 *tab = (const dp_u8 *)t->txq_table;

		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "TXQ publish: enabled=%u publications=%u ts_ms=0x%04x user0_flags=0x%02x (tid0 table count/sn/bytes read with dp_txq_table)\n",
				 dptx_txq_publish_en, t->txq_publish_count,
				 (u32)(dp_txrx_le32(tab) & 0xffffu),
				 tab[DP_TXRX_TXQ_FLAGS_OFF]);
	}
	if (t->tx_complete_polled)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "tx_complete_poll: verdict=%s waited_ms=%u pending=%u table={0x%08x,0x%08x,0x%08x}\n",
				 t->tx_complete_verdict == 1u ? "COMPLETED" :
				 t->tx_complete_verdict == 2u ? "POST-ONLY" : "none",
				 t->tx_complete_waited_ms,
				 t->tx_complete_pending,
				 t->tx_complete_table[0],
				 t->tx_complete_table[1],
				 t->tx_complete_table[2]);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "completion rule: TX done when device rptr == published wptr (dp_tx_complete_poll is the bounded, once-per-load read that settles COMPLETED vs POST-ONLY); stop if a table read-back mismatches\n");
	mutex_unlock(&t->lock);
	return pos;
}

/* ------------------------------------------------------------------ */
/* RX capture (read-only: no ring pointer is moved)                   */
/* ------------------------------------------------------------------ */
static void dptx_rx_scan(struct hi5622_dp_txrx *t)
{
	unsigned int i;

	t->rx_cap_count = 0u;
	t->rx_scanned_norm = 0u;
	t->rx_scanned_small = 0u;
	if (t->rx_norm_cpu) {
		for (i = 0; i < DP_TXRX_RX_NORM_DEPTH; i++) {
			const dp_u8 *buf;

			if (!t->rx_norm_cpu[i])
				continue;
			buf = (const dp_u8 *)t->rx_norm_cpu[i] +
			      DP_TXRX_RX_BUF_HDR;
			t->rx_scanned_norm++;
			if (t->rx_cap_count >= DP_TXRX_RX_CAPTURE_MAX)
				continue;
			if (dp_txrx_rx_capture_build(buf, DP_TXRX_RX_NORM_BUFLEN,
						     0u, i,
						     &t->rx_caps[t->rx_cap_count]) ==
			    DP_TXRX_OK) {
				t->rx_caps[t->rx_cap_count].cmp_raw =
					t->rx_last_cmp_raw;
				t->rx_cap_count++;
			}
		}
	}
	if (t->rx_small_cpu) {
		for (i = 0; i < DP_TXRX_RX_SMALL_DEPTH; i++) {
			const dp_u8 *buf;

			if (!t->rx_small_cpu[i])
				continue;
			buf = (const dp_u8 *)t->rx_small_cpu[i] +
			      DP_TXRX_RX_BUF_HDR;
			t->rx_scanned_small++;
			if (t->rx_cap_count >= DP_TXRX_RX_CAPTURE_MAX)
				continue;
			if (dp_txrx_rx_capture_build(buf,
						     DP_TXRX_RX_SMALL_BUFLEN,
						     1u, i,
						     &t->rx_caps[t->rx_cap_count]) ==
			    DP_TXRX_OK) {
				t->rx_caps[t->rx_cap_count].cmp_raw =
					t->rx_last_cmp_raw;
				t->rx_cap_count++;
			}
		}
	}
}

ssize_t hi5622_dp_rx_poll_store(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct hi5622_dp_txrx *t = dptx_from_dev(dev);
	struct hi5622_pci *ctx;
	unsigned long waited = 0;
	u32 pending = 0;
	int rc = 0;

	(void)attr;
	if (!t || !t->ctx)
		return -ENODEV;
	ctx = t->ctx;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;

	mutex_lock(&t->lock);
	if (t->failed || !t->bound) {
		mutex_unlock(&t->lock);
		return -ENODEV;
	}
	if (t->rx_polled) {
		mutex_unlock(&t->lock);
		return -EALREADY;
	}
	if (!ctx->windows_programmed || !ctx->hcc || !ctx->hcc->armed) {
		dev_warn(dev,
			 "DP rx_poll: refused - transport windows/armed gate is not up (read-only action)\n");
		mutex_unlock(&t->lock);
		return -EBUSY;
	}

	/* Bounded wait for the device completion producer to move.  The default
	 * source is the live bank CA 0x40050000 +0x10 - the pointer the vendor
	 * ISR reads.  The config-block +0x4c/+0x50/+0x54 words are never read by
	 * the vendor, read 0 in the vendor stock capture, and a read-sensitive
	 * register there is a candidate for the observed firmware exception; they
	 * are reachable only behind the opt-in rx_legacy_bank=1. */
	while (waited < DPTX_RX_POLL_MS) {
		u32 wptr;

		if (dptx_rx_legacy_bank) {
			t->rx_legacy_reads++;
			wptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXCMP_WPTR);
		} else {
			wptr = dptx_mac_read_live(ctx, HIL_RX_LIVE_CMP_WPTR);
		}
		pending = (u32)dp_txrx_idx_count(wptr, t->rx_state.cmp_rptr,
						 DP_TXRX_RX_CMP_DEPTH);
		if (pending > 0u)
			break;
		msleep(DPTX_RX_POLL_STEP_MS);
		waited += DPTX_RX_POLL_STEP_MS;
	}
	t->rx_waited_ms = waited;
	if (dptx_rx_legacy_bank) {
		t->rx_legacy_reads += 3u;
		t->rx_cmp_wptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXCMP_WPTR);
		t->rx_cmp_rptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXCMP_RPTR);
		t->rx_norm_rptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXNORM_RPTR);
		t->rx_small_rptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXSMALL_RPTR);
	} else {
		t->rx_cmp_wptr = dptx_mac_read_live(ctx, HIL_RX_LIVE_CMP_WPTR);
		t->rx_cmp_rptr = t->rx_cmp_cons;
		t->rx_norm_rptr = dptx_mac_read_live(ctx, HIL_RX_LIVE_NORM_RPTR);
		t->rx_small_rptr = dptx_mac_read_live(ctx, HIL_RX_LIVE_SMALL_RPTR);
	}
	if (t->rx_cmp_wptr != 0xffffffffu) {
		pending = (u32)dp_txrx_idx_count(t->rx_cmp_wptr,
						 t->rx_state.cmp_rptr,
						 DP_TXRX_RX_CMP_DEPTH);
	}
	t->rx_pending = pending;
	t->rx_last_cmp_raw = 0u;
	if (pending > 0u && t->rx_cmp_ring) {
		struct dp_txrx_rx_capture headcap;

		rc = dp_txrx_rx_plan((const dp_u8 *)t->rx_cmp_ring,
				     4u * DP_TXRX_RX_CMP_DEPTH,
				     t->rx_cmp_wptr, &t->rx_state,
				     &headcap);
		if (rc == DP_TXRX_OK)
			t->rx_last_cmp_raw = headcap.cmp_raw;
	}
	dma_rmb();
	dptx_rx_scan(t);
	if (t->rx_armed) {
		u32 lst, lnr, lsr, lcw;

		dptx_live_read(ctx, &lst, &lnr, &lsr, &lcw);
		t->rx_live_status = lst;
		t->rx_live_norm_rptr = lnr;
		t->rx_live_small_rptr = lsr;
		t->rx_live_cmp_wptr = lcw;
		dev_info(dev,
			 "DP rx_poll live bank (CA 0x40050000): status=0x%08x norm_rptr=0x%04x small_rptr=0x%04x cmp_wptr=0x%04x | config-bank +0x4c/+0x50/+0x54 are NOT the live device pointers (the vendor never reads them; the stock dump keeps them 0 while the live bank carries the real rptrs)\n",
			 lst, lnr, lsr, lcw);
	}
	t->rx_polled = true;
	mutex_unlock(&t->lock);

	dev_info(dev,
		 "DP rx_poll: waited_ms=%lu pending=%u cmp_wptr=0x%08x cmp_rptr=0x%08x norm_rptr=0x%08x small_rptr=0x%08x head_cmp=0x%08x captures=%u (norm=%u small=%u scanned; no ring pointer moved)\n",
		 waited, t->rx_pending, t->rx_cmp_wptr, t->rx_cmp_rptr,
		 t->rx_norm_rptr, t->rx_small_rptr, t->rx_last_cmp_raw,
		 t->rx_cap_count, t->rx_cap_count, 0u);
	if (pending == 0u)
		dev_info(dev,
			 "DP rx_poll: no RX completion within %u ms - capture-only (the MAC RX data path may still need a started VAP/radio; record, no retry)\n",
			 DPTX_RX_POLL_MS);
	return (ssize_t)count;
}

ssize_t hi5622_dp_rx_capture_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct hi5622_dp_txrx *t = dptx_from_dev(dev);
	int pos = 0;
	unsigned int k, i;

	(void)attr;
	if (!t || !t->ctx)
		return -ENODEV;
	mutex_lock(&t->lock);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "DP rx capture: polled=%u waited_ms=%lu pending=%u cmp_wptr=0x%08x cmp_rptr=0x%08x norm_rptr=0x%08x small_rptr=0x%08x head_cmp=0x%08x captures=%u (norm+small scanned)\n",
			 t->rx_polled ? 1u : 0u, t->rx_waited_ms, t->rx_pending,
			 t->rx_cmp_wptr, t->rx_cmp_rptr, t->rx_norm_rptr,
			 t->rx_small_rptr, t->rx_last_cmp_raw, t->rx_cap_count);
	for (k = 0; k < t->rx_cap_count && k < 4u; k++) {
		struct dp_txrx_rx_capture *cap = &t->rx_caps[k];

		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "cap[%u]: ring=%s idx=%u desc_ok=%u payload_len(+0x1a)=%u payload_len_old(+0x1e)=%u mpdu_len=%u cmp_raw=0x%08x\n",
				 k, cap->ring_kind == 0u ? "norm" : "small",
				 cap->ring_index, cap->desc_ok,
				 cap->desc_ok ? dptx_le16(cap->desc + 0x1a) : 0u,
				 dptx_le16(cap->desc + 0x1e), cap->mpdu_len,
				 cap->cmp_raw);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "  desc = ");
		for (i = 0; i < DP_TXRX_RX_DESC_LEN; i++)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos, "%02x",
					 cap->desc[i]);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "\n  mpdu[0..31] = ");
		for (i = 0; i < 32u && i < cap->mpdu_len; i++)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos, "%02x",
					 cap->mpdu[i]);
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "\n");
	}
	if (t->rx_cap_count > 4u)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos,
				 "... %u more captures not printed (PAGE_SIZE bound)\n",
				 t->rx_cap_count - 4u);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "note: descriptor field semantics stay [I]/[U] until the bench capture fixes them; mpdu starts at descriptor+0x28 and payload_len is the vendor skb_put field\n");
	/* wifi-rx-static (task-137): live-bank arm/drain state. */
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "RX static: rx_enable=%u armed=%u failed=%u actions=%u drained=%u bad_entry=%u chain_refused=%u new_caps=%u refilled{norm=%u small=%u}\n",
			 dptx_rx_enable, t->rx_armed ? 1u : 0u,
			 t->rx_failed ? 1u : 0u, t->rx_drain_actions,
			 t->rx_drained, t->rx_bad_entry, t->rx_chain_refused,
			 t->rx_new_cap_count, t->rx_refilled_norm,
			 t->rx_refilled_small);
	/* wifi-rx-watermark (task-156): sustained refill state. */
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "RX watermark: rx_refill_wm=%u%% target{norm=%u small=%u} posted{norm=%u small=%u} trips=%u credit_low=%u%s refilled{norm=%u small=%u}\n",
			 dptx_rx_refill_wm, t->rx_wm_target_norm,
			 t->rx_wm_target_small, t->rx_wm_posted_norm,
			 t->rx_wm_posted_small, t->rx_wm_trips,
			 t->rx_wm_credit_low,
			 t->rx_wm_low ? " CREDIT_LOW" : "",
			 t->rx_wm_refilled_norm, t->rx_wm_refilled_small);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "RX parity (task-166): rx_entry_wait=%u commit{waits=%u ok=%u spurious=%u closed=%u} ring_kick=%u done=%u ctl_enable=%u done=%u legacy_bank=%u reads=%u stall=%s\n",
			 dptx_rx_entry_wait, t->rx_commit_waits, t->rx_commit_ok,
			 t->rx_spurious, t->rx_spurious_closed,
			 dptx_rx_ring_kick, t->rx_kick_done ? 1u : 0u,
			 dptx_rx_ctl_enable, t->rx_ctl_done ? 1u : 0u,
			 dptx_rx_legacy_bank, t->rx_legacy_reads,
			 rxp_stall_name((enum rxp_stall) t->rx_stall_class));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "RX static live bank CA 0x40050000: status=0x%08x norm_rptr=0x%04x small_rptr=0x%04x cmp_wptr=0x%04x prod{norm=0x%04x small=0x%04x} cmp_cons=0x%04x last_err=%d\n",
			 t->rx_live_status, t->rx_live_norm_rptr,
			 t->rx_live_small_rptr, t->rx_live_cmp_wptr,
			 t->rx_norm_prod & 0xffffu, t->rx_small_prod & 0xffffu,
			 t->rx_cmp_cons & 0xffffu, (int)t->rx_last_err);
	mutex_unlock(&t->lock);
	return pos;
}


/* ------------------------------------------------------------------ */
/* wifi-rx-static (task-137): live-bank RX arm + completion drain     */
/* ------------------------------------------------------------------ */

/* Locate a completion entry (device VA of buffer+4) in the two RX pools.  The
 * pools are individual coherent allocations, so this is a bounded linear
 * search (256 + 512 compares for at most rx_drain_max=64 entries). */
static int dptx_rx_find_buffer(struct hi5622_dp_txrx *t, u32 entry,
			       u8 *ring_kind, u32 *slot)
{
	unsigned int i;

	if (!ring_kind || !slot)
		return HIL_RX_EARG;
	for (i = 0; i < HIL_RX_NORM_DEPTH; i++) {
		if (!t->rx_norm_cpu || !t->rx_norm_cpu[i])
			continue;
		if ((u32)t->rx_norm_dma[i] + HIL_RX_SKB_WORD == entry) {
			*ring_kind = 0u;
			*slot = i;
			return HIL_RX_OK;
		}
	}
	for (i = 0; i < HIL_RX_SMALL_DEPTH; i++) {
		if (!t->rx_small_cpu || !t->rx_small_cpu[i])
			continue;
		if ((u32)t->rx_small_dma[i] + HIL_RX_SKB_WORD == entry) {
			*ring_kind = 1u;
			*slot = i;
			return HIL_RX_OK;
		}
	}
	return HIL_RX_ERANGE;
}

/* Append one completed buffer (its DMA address) to a per-ring refill queue. */
static int dptx_rx_queue_completed(struct hi5622_dp_txrx *t, u8 ring_kind,
				   u32 buf_dma)
{
	u32 depth, tail;
	u32 *q;
	u16 *head, *count;

	if (ring_kind == 0u) {
		depth = HIL_RX_NORM_DEPTH;
		q = t->rx_refill_norm_q;
		head = &t->rx_refill_norm_head;
		count = &t->rx_refill_norm_count;
	} else {
		depth = HIL_RX_SMALL_DEPTH;
		q = t->rx_refill_small_q;
		head = &t->rx_refill_small_head;
		count = &t->rx_refill_small_count;
	}
	if ((u32)*count >= depth)
		return HIL_RX_ERANGE;
	tail = ((u32)*head + (u32)*count) % depth;
	q[tail] = buf_dma;
	(*count)++;
	return HIL_RX_OK;
}

/* Refill one free ring from its refill queue inside the bounded parity window
 * (hi5622_rx_parity.h sec. 5): plan = min(device-consumed delta, returned
 * buffers queued, depth - posted, action cap).  The inherited code refilled by
 * the raw delta and could over-publish past the posted/unposted room if the
 * phase bookkeeping and the queue ever disagreed.  Buffers being re-published
 * were handed back by validated completions, so no buffer can be in flight
 * twice.  Returns the refilled count or a negative error (the caller closes
 * the RX lane: no retry). */
static int dptx_rx_refill(struct hi5622_dp_txrx *t, u8 ring_kind)
{
	struct hi5622_pci *ctx = t->ctx;
	u32 old, fresh, buf_dma, idx, prod, new_prod, depth, cfg_off;
	u32 queued, plan, cap;
	u32 *q;
	u16 *head, *count;
	void *ring;
	int rc, refilled = 0;

	if (ring_kind == 0u) {
		old = t->rx_last_norm_rptr;
		fresh = t->rx_live_norm_rptr;
		depth = HIL_RX_NORM_DEPTH;
		cfg_off = HIL_RX_CFG_WPTR_NORM;
		q = t->rx_refill_norm_q;
		head = &t->rx_refill_norm_head;
		count = &t->rx_refill_norm_count;
		ring = t->rx_norm_ring;
		prod = t->rx_norm_prod;
	} else {
		old = t->rx_last_small_rptr;
		fresh = t->rx_live_small_rptr;
		depth = HIL_RX_SMALL_DEPTH;
		cfg_off = HIL_RX_CFG_WPTR_SMALL;
		q = t->rx_refill_small_q;
		head = &t->rx_refill_small_head;
		count = &t->rx_refill_small_count;
		ring = t->rx_small_ring;
		prod = t->rx_small_prod;
	}
	if (!ring)
		return HIL_RX_EARG;
	cap = dptx_rx_drain_max;
	if (cap == 0u || cap > HIL_RX_DRAIN_MAX)
		cap = HIL_RX_DRAIN_MAX;
	queued = (u32)*count;
	rc = rxp_refill_plan(old, fresh, prod, depth, queued, cap, &plan,
			     &new_prod);
	if (rc != 0)
		return HIL_RX_ERANGE;
	while (plan > 0u) {
		idx = prod & HIL_RX_IDX_MASK;
		if (idx >= depth)
			return HIL_RX_ERANGE;
		buf_dma = q[*head];
		((u32 *)ring)[idx] = buf_dma + HIL_RX_SKB_WORD;
		*head = (u16)(((u32)*head + 1u) % depth);
		(*count)--;
		prod = hi5622_rx_idx_advance(prod, depth);
		plan--;
		refilled++;
	}
	if (ring_kind == 0u) {
		t->rx_last_norm_rptr = fresh;
		t->rx_norm_prod = new_prod;
		t->rx_refilled_norm += (u32)refilled;
	} else {
		t->rx_last_small_rptr = fresh;
		t->rx_small_prod = new_prod;
		t->rx_refilled_small += (u32)refilled;
	}
	if (refilled > 0) {
		dma_wmb();
		rc = dptx_publish16(ctx, cfg_off, new_prod, NULL);
		if (rc)
			return rc;
	}
	return refilled;
}

/* ------------------------------------------------------------------ */
/* wifi-rx-watermark (task-156): sustained free-ring refill pass      */
/* ------------------------------------------------------------------ */

/* Re-publish queued completed buffers into one free ring when its posted count
 * (producer - live consumer) is below the watermark.  Bounds: only buffers
 * queued by validated completions, only depth - posted ring slots (the exact
 * number of consumed-but-not-republished slots), and at most cap buffers this
 * action (cap = the clamped rx_drain_max).  On success the producer is
 * published once and last_*_rptr is refreshed so the next window refill cannot
 * double-count a slot this pass already re-published.  Any error is returned
 * for the caller to close the lane (fail-closed, no retry). */
static int dptx_rx_wm_ring(struct hi5622_dp_txrx *t, u8 ring_kind,
			   unsigned int wm_pct, u32 fresh_rptr, u32 cap,
			   int *refilled)
{
	struct hi5622_pci *ctx = t->ctx;
	u32 depth, cfg_off, prod, posted, target, plan, buf_dma, idx, queued;
	u32 *q;
	u16 *head, *count;
	void *ring;
	int rc;

	if (ring_kind == 0u) {
		depth = HIL_RX_NORM_DEPTH;
		cfg_off = HIL_RX_CFG_WPTR_NORM;
		q = t->rx_refill_norm_q;
		head = &t->rx_refill_norm_head;
		count = &t->rx_refill_norm_count;
		ring = t->rx_norm_ring;
		prod = t->rx_norm_prod;
	} else {
		depth = HIL_RX_SMALL_DEPTH;
		cfg_off = HIL_RX_CFG_WPTR_SMALL;
		q = t->rx_refill_small_q;
		head = &t->rx_refill_small_head;
		count = &t->rx_refill_small_count;
		ring = t->rx_small_ring;
		prod = t->rx_small_prod;
	}
	if (!ring)
		return HIL_RX_EARG;
	rc = hi5622_rx_ring_pending(prod, fresh_rptr, depth, &posted);
	if (rc != HIL_RX_OK)
		return rc;
	rc = hi5622_rx_wm_target(depth, wm_pct, &target);
	if (rc != HIL_RX_OK)
		return rc;
	if (ring_kind == 0u) {
		t->rx_wm_posted_norm = posted;
		t->rx_wm_target_norm = target;
	} else {
		t->rx_wm_posted_small = posted;
		t->rx_wm_target_small = target;
	}
	if (posted >= target)
		return 0; /* healthy */
	t->rx_wm_trips++;
	queued = (u32)*count;
	rc = hi5622_rx_wm_plan(depth, wm_pct, posted, queued, cap, &plan);
	if (rc != HIL_RX_OK)
		return rc;
	while (plan > 0u) {
		idx = prod & HIL_RX_IDX_MASK;
		if (idx >= depth)
			return HIL_RX_ERANGE;
		buf_dma = q[*head];
		((u32 *)ring)[idx] = buf_dma + HIL_RX_SKB_WORD;
		*head = (u16)(((u32)*head + 1u) % depth);
		(*count)--;
		prod = hi5622_rx_idx_advance(prod, depth);
		plan--;
		(*refilled)++;
	}
	if (*refilled > 0) {
		dma_wmb();
		rc = dptx_publish16(ctx, cfg_off, prod, NULL);
		if (rc)
			return rc;
	}
	if (ring_kind == 0u) {
		t->rx_norm_prod = prod;
		t->rx_last_norm_rptr = fresh_rptr;
		t->rx_wm_refilled_norm += (u32)*refilled;
		t->rx_wm_posted_norm = posted + (u32)*refilled;
	} else {
		t->rx_small_prod = prod;
		t->rx_last_small_rptr = fresh_rptr;
		t->rx_wm_refilled_small += (u32)*refilled;
		t->rx_wm_posted_small = posted + (u32)*refilled;
	}
	rc = hi5622_rx_wm_triggered(depth, wm_pct, posted + (u32)*refilled);
	if (rc < 0)
		return rc;
	if (rc == 1) {
		/* Still below the watermark after publishing everything returned:
		 * the completion backlog is larger than this action can hand back.
		 * Classify as CREDIT_LOW (sibling H4 starvation precondition); the
		 * next drain re-checks.  This is telemetry, not a lane-closing
		 * condition by itself. */
		t->rx_wm_credit_low++;
		t->rx_wm_low = true;
	}
	return 0;
}

/* Both free rings.  wm_pct == 0 disables the pass and keeps the old rx-static
 * behaviour exactly (no ring pointer is touched by it). */
static int dptx_rx_wm_refill(struct hi5622_dp_txrx *t, unsigned int wm_pct,
			     u32 fresh_norm_rptr, u32 fresh_small_rptr, u32 cap,
			     int *norm_refilled, int *small_refilled)
{
	int rc;

	*norm_refilled = 0;
	*small_refilled = 0;
	if (wm_pct == 0u)
		return 0;
	if (wm_pct > HIL_RX_WM_PCT_MAX)
		wm_pct = HIL_RX_WM_PCT_MAX; /* clamp: never over-refill */
	rc = dptx_rx_wm_ring(t, 0u, wm_pct, fresh_norm_rptr, cap, norm_refilled);
	if (rc != HIL_RX_OK)
		return rc;
	return dptx_rx_wm_ring(t, 1u, wm_pct, fresh_small_rptr, cap,
			       small_refilled);
}

static ssize_t dptx_rx_fail(struct device *dev, struct hi5622_dp_txrx *t,
			    int rc, const char *why)
{
	t->rx_failed = true;
	t->rx_last_err = (u32)rc;
	dev_err(dev,
		"DP rx_drain: REFUSED (%s, rc=%d) - RX lane closed, no further ring pointer published, no retry until reload (power-cycle if the module must be reloaded)\n",
		why, rc);
	return rc < 0 ? rc : -EIO;
}

/* Log-only stall classification (iwlwifi RX_STALLED / sibling P3 analogue).
 * Never moves a pointer; the drain calls it once per action. */
static void dptx_rx_stall_update(struct hi5622_dp_txrx *t, u32 st, u32 nr,
				 u32 sr, u32 cw)
{
	int cls = RXP_STALL_IDLE;

	if (rxp_stall_class(t->rx_arm_status, t->rx_arm_norm_rptr,
			    t->rx_arm_small_rptr, t->rx_arm_cmp_wptr,
			    st, nr, sr, cw, t->rx_spurious, &cls) == 0)
		t->rx_stall_class = cls;
}

/* dp_rx_arm: read-only validation of the live RX bank.  This is the mirror of
 * the TXQ publish proof for the RX direction: it must show that the MAC has
 * consumed the free-ring entries the 14-write program published.  It never
 * writes a register (rx_enable gates only the drain). */
ssize_t hi5622_dp_rx_arm_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct hi5622_dp_txrx *t = dptx_from_dev(dev);
	struct hi5622_pci *ctx;
	u32 st, nr, sr, cw, mask, nw, sw, cr, active = 0u, pending = 0u;
	int rc;

	(void)attr;
	if (!t || !t->ctx)
		return -ENODEV;
	ctx = t->ctx;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	mutex_lock(&t->lock);
	if (!t->bound || t->failed) {
		mutex_unlock(&t->lock);
		return -ENODEV;
	}
	if (t->rx_failed) {
		dev_warn(dev, "DP rx_arm: REFUSED - the RX lane is already closed in this load\n");
		mutex_unlock(&t->lock);
		return -EIO;
	}
	if (t->rx_armed) {
		mutex_unlock(&t->lock);
		return -EALREADY;
	}
	if (!dptx_rx_enable) {
		dev_warn(dev,
			 "DP rx_arm: REFUSED - rx_enable=0.  The RX consumer path is fail-closed; reload the module with rx_enable=1 for the attended RX ladder.\n");
		mutex_unlock(&t->lock);
		return -EPERM;
	}
	if (!ctx->windows_programmed || !ctx->hcc || !ctx->hcc->armed) {
		dev_warn(dev,
			 "DP rx_arm: refused - transport windows/armed gate is not up (read-only action)\n");
		mutex_unlock(&t->lock);
		return -EBUSY;
	}
	if (!t->rx_norm_ring || !t->rx_small_ring || !t->rx_cmp_ring ||
	    !t->rx_norm_cpu || !t->rx_small_cpu || !t->rx_norm_dma ||
	    !t->rx_small_dma) {
		mutex_unlock(&t->lock);
		return -ENODEV;
	}
	/* Read-only validation. */
	dptx_live_read(ctx, &st, &nr, &sr, &cw);
	mask = dptx_mac_read(ctx, HIL_RX_CFG_INT_MASK);
	nw = dptx_mac_read(ctx, HIL_RX_CFG_WPTR_NORM);
	sw = dptx_mac_read(ctx, HIL_RX_CFG_WPTR_SMALL);
	cr = dptx_mac_read(ctx, HIL_RX_CFG_CMP_RPTR);
	if (st == 0xffffffffu || nr == 0xffffffffu || sr == 0xffffffffu ||
	    cw == 0xffffffffu || mask == 0xffffffffu || nw == 0xffffffffu ||
	    sw == 0xffffffffu || cr == 0xffffffffu) {
		rc = -EFAULT;
		goto arm_refuse;
	}
	if (mask != HIL_RX_INT_MASK_INIT) {
		rc = -ERANGE;
		goto arm_refuse;
	}
	rc = hi5622_rx_ring_pending(cw, t->rx_cmp_cons, HIL_RX_CMP_DEPTH,
				    &pending);
	if (rc != HIL_RX_OK)
		goto arm_refuse;
	if ((nw & HIL_RX_IDX_MASK) >= HIL_RX_NORM_DEPTH ||
	    (sw & HIL_RX_IDX_MASK) >= HIL_RX_SMALL_DEPTH) {
		rc = -ERANGE;
		goto arm_refuse;
	}
	hi5622_rx_status_rx_bit(st, mask, &active);
	t->rx_armed = true;
	t->rx_arm_status = st;
	t->rx_arm_mask = mask;
	t->rx_arm_norm_rptr = nr;
	t->rx_arm_small_rptr = sr;
	t->rx_arm_cmp_wptr = cw;
	t->rx_live_status = st;
	t->rx_live_norm_rptr = nr;
	t->rx_live_small_rptr = sr;
	t->rx_live_cmp_wptr = cw;
	t->rx_last_norm_rptr = nr;
	t->rx_last_small_rptr = sr;
	mutex_unlock(&t->lock);

	dev_info(dev,
		 "DP rx_arm: live bank CA 0x%08x status=0x%08x rx_pending=%u mask=0x%08x (vendor 0xfffffcc1) norm_rptr=0x%04x small_rptr=0x%04x cmp_wptr=0x%04x pending=%u | config: norm_wptr=0x%04x small_wptr=0x%04x cmp_rptr=0x%04x\n",
		 HIL_RX_LIVE_CA_BAND0, st, active, mask, nr, sr, cw, pending,
		 nw & 0xffffu, sw & 0xffffu, cr & 0xffffu);
	dev_info(dev,
		 "DP rx_arm: verdict - free-rptr movement proves the MAC consumed published free entries (vendor stock mirror was norm/small rptr = 0x20 after a full fill); config-bank +0x4c/+0x50/+0x54 are NOT the live counters (vendor never reads them, and the stock dump keeps them 0 while 0x40050008/0x0c = 0x20).  Arm is read-only; dp_rx_drain is the first pointer-moving action.\n");
	return (ssize_t)count;

arm_refuse:
	dev_err(dev,
		"DP rx_arm: REFUSED - live-bank validation failed (rc=%d: status=0x%08x norm_rptr=0x%04x small_rptr=0x%04x cmp_wptr=0x%04x mask=0x%08x; expected mask 0xfffffcc1 and 15-bit indices below the ring depths) - no write performed\n",
		rc, st, nr, sr, cw, mask);
	mutex_unlock(&t->lock);
	return rc;
}

/* dp_rx_drain: the only RX action that moves ring pointers.  Consumes a
 * bounded prefix of the completion ring, validates every entry against the
 * host pools and the 36-byte MAC header, queues the buffers for refill,
 * publishes the completion-ring consumer index and refills the free rings up
 * to the live device-rptr delta.  Any anomaly closes the lane before a single
 * pointer is published. */
ssize_t hi5622_dp_rx_drain_store(struct device *dev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	struct hi5622_dp_txrx *t = dptx_from_dev(dev);
	struct hi5622_pci *ctx;
	u32 st, nr, sr, cw, pending, max, cons, active = 0u;
	u32 stopped = 0u, processed = 0u;
	int rc, i, norm_refill, small_refill, wm_norm_refill, wm_small_refill;

	(void)attr;
	if (!t || !t->ctx)
		return -ENODEV;
	ctx = t->ctx;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	mutex_lock(&t->lock);
	if (!t->bound || t->failed) {
		mutex_unlock(&t->lock);
		return -ENODEV;
	}
	if (!dptx_rx_enable) {
		dev_warn(dev, "DP rx_drain: REFUSED - rx_enable=0 (fail-closed)\n");
		mutex_unlock(&t->lock);
		return -EPERM;
	}
	if (t->rx_failed) {
		dev_warn(dev, "DP rx_drain: REFUSED - the RX lane is already closed in this load\n");
		mutex_unlock(&t->lock);
		return -EIO;
	}
	if (!t->rx_armed) {
		dev_warn(dev, "DP rx_drain: REFUSED - dp_rx_arm has not validated the live bank in this load\n");
		mutex_unlock(&t->lock);
		return -EINVAL;
	}
	rc = dptx_write_gate(ctx);
	if (rc) {
		dev_warn(dev,
			 "DP rx_drain: refused by gate '%s' - no device write (needs dp_setup + dp_txq_sync in this load)\n",
			 dptx_gate_name(rc));
		mutex_unlock(&t->lock);
		return rc;
	}
	dptx_live_read(ctx, &st, &nr, &sr, &cw);
	t->rx_live_status = st;
	t->rx_live_norm_rptr = nr;
	t->rx_live_small_rptr = sr;
	t->rx_live_cmp_wptr = cw;
	if (st == 0xffffffffu || nr == 0xffffffffu || sr == 0xffffffffu ||
	    cw == 0xffffffffu) {
		rc = -EFAULT;
		goto drain_refuse;
	}
	rc = hi5622_rx_ring_pending(cw, t->rx_cmp_cons, HIL_RX_CMP_DEPTH,
				    &pending);
	if (rc != HIL_RX_OK)
		goto drain_refuse;
	max = dptx_rx_drain_max;
	if (max == 0u || max > HIL_RX_DRAIN_MAX)
		max = HIL_RX_DRAIN_MAX;
	if (pending > max)
		pending = max;
	if (pending == 0u) {
		hi5622_rx_status_rx_bit(st, dptx_mac_read(ctx, HIL_RX_CFG_INT_MASK),
					&active);
		/* task-156: with no new completion there can still be returned
		 * buffers queued from an earlier bounded drain (the old code leaked
		 * exactly those slots).  The watermark pass is a no-op at
		 * rx_refill_wm=0; otherwise it re-publishes up to the ring capacity
		 * from the queue - bounded by max and by depth - posted. */
		rc = dptx_rx_wm_refill(t, dptx_rx_refill_wm, nr, sr, max,
				       &wm_norm_refill, &wm_small_refill);
		if (rc < 0)
			goto drain_refuse;
		dptx_rx_stall_update(t, st, nr, sr, cw);
		if (wm_norm_refill + wm_small_refill > 0) {
			mutex_unlock(&t->lock);
			dev_info(dev,
				 "DP rx_drain: no new completion (live status=0x%08x rx_pending=%u norm_rptr=0x%04x small_rptr=0x%04x cmp_wptr=0x%04x cmp_cons=0x%04x stall=%s) - watermark refill re-published %d norm / %d small queued buffers (producer moved, completion consumer did not)\n",
				 st, active, nr, sr, cw, t->rx_cmp_cons,
				 rxp_stall_name((enum rxp_stall)t->rx_stall_class),
				 wm_norm_refill, wm_small_refill);
			return (ssize_t)count;
		}
		mutex_unlock(&t->lock);
		dev_info(dev,
			 "DP rx_drain: no new completion (live status=0x%08x rx_pending=%u norm_rptr=0x%04x small_rptr=0x%04x cmp_wptr=0x%04x cmp_cons=0x%04x stall=%s) - no pointer moved\n",
			 st, active, nr, sr, cw, t->rx_cmp_cons,
			 rxp_stall_name((enum rxp_stall) t->rx_stall_class));
		return (ssize_t)count;
	}
	cons = t->rx_cmp_cons;
	for (i = 0; i < (int)pending; i++) {
		u32 slot = cons & HIL_RX_IDX_MASK;
		u32 entry, slot_idx, buf_dma;
		u8 ring_kind;
		const u8 *cpu;
		const u32 *pool_dma;
		struct hi5622_rx_hdr hdr;
		u32 dma_cap;
		u16 copy;

		if (slot >= HIL_RX_CMP_DEPTH) {
			rc = -ERANGE;
			goto drain_refuse;
		}
		/* Completion-entry commit decision (hi5622_rx_parity.h sec. 3):
		 * the device producer pointer can become visible before the entry
		 * payload is committed (the sibling Hi1105 HAL spins until the entry
		 * is non-zero for this reason).  A zero entry is a spurious producer
		 * advance, not corruption; it is retried within rx_entry_wait and
		 * never consumed while zero. */
		dma_rmb(); /* the live wptr was read before the ring memory */
		{
			const u32 *cmp = (const u32 *)(const void *)t->rx_cmp_ring;
			unsigned int waits = 0;
			u32 first;

			first = cmp[slot];
			entry = first;
			while (entry == 0u && waits < dptx_rx_entry_wait) {
				cpu_relax();
				dma_rmb();
				entry = cmp[slot];
				waits++;
			}
			if (waits > 0u)
				t->rx_commit_waits++;
			if (entry == 0u) {
				int close_lane = 0;

				t->rx_spurious++;
				dptx_rx_stall_update(t, st, nr, sr, cw);
				if (rxp_spurious_limit(t->rx_spurious,
						       RXP_SPURIOUS_MAX,
						       &close_lane) != 0 ||
				    close_lane) {
					t->rx_spurious_closed = 1u;
					rc = -EIO;
					goto drain_refuse;
				}
				dev_warn(dev,
					 "DP rx_drain: completion entry %d still zero after %u bounded re-reads - spurious producer advance (not corruption); %u/%u consecutive; entry not consumed\n",
					 i, waits, t->rx_spurious,
					 (u32)RXP_SPURIOUS_MAX);
				stopped = 1u;
				break;
			}
			{
				int settled = 0;

				if (rxp_commit_settled(first, entry, &settled) !=
				    0 || !settled) {
					t->rx_spurious++;
					dptx_rx_stall_update(t, st, nr, sr, cw);
					dev_warn(dev,
						 "DP rx_drain: completion entry %d changed across the bounded commit reads (0x%08x -> 0x%08x) - producer/entry desync, entry not consumed\n",
						 i, first, entry);
					stopped = 1u;
					break;
				}
				t->rx_commit_ok++;
			}
		}
		rc = dptx_rx_find_buffer(t, entry, &ring_kind, &slot_idx);
		if (rc != HIL_RX_OK) {
			t->rx_bad_entry++;
			goto drain_refuse;
		}
		if (ring_kind == 0u) {
			cpu = (const u8 *)t->rx_norm_cpu[slot_idx];
			pool_dma = t->rx_norm_dma;
			dma_cap = HIL_RX_NORM_BUFLEN;
		} else {
			cpu = (const u8 *)t->rx_small_cpu[slot_idx];
			pool_dma = t->rx_small_dma;
			dma_cap = HIL_RX_SMALL_BUFLEN;
		}
		if (!cpu) {
			rc = -ENODEV;
			goto drain_refuse;
		}
		rc = hi5622_rx_hdr_parse(cpu + HIL_RX_SKB_WORD, dma_cap, &hdr);
		if (rc == HIL_RX_ECHAIN) {
			t->rx_chain_refused++;
			goto drain_refuse;
		}
		if (rc != HIL_RX_OK) {
			t->rx_bad_entry++;
			goto drain_refuse;
		}
		/* capture (diagnostic; bounded by DP_TXRX_RX_CAPTURE_MAX) */
		if (t->rx_cap_count < DP_TXRX_RX_CAPTURE_MAX) {
			struct dp_txrx_rx_capture *cap =
				&t->rx_caps[t->rx_cap_count];

			memset(cap, 0, sizeof(*cap));
			cap->cmp_raw = entry;
			cap->ring_kind = ring_kind;
			cap->ring_index = slot_idx;
			cap->desc_ok = 1u;
			memcpy(cap->desc, cpu + HIL_RX_SKB_WORD,
			       HIL_RX_HDR_BYTES);
			copy = hdr.payload_len;
			if (copy > DP_TXRX_RX_CAPTURE_MPDU)
				copy = DP_TXRX_RX_CAPTURE_MPDU;
			memcpy(cap->mpdu, cpu + HIL_RX_PAYLOAD_OFF, copy);
			cap->mpdu_len = copy;
			t->rx_cap_count++;
			t->rx_new_cap_count++;
		}
		buf_dma = (u32)pool_dma[slot_idx];
		rc = dptx_rx_queue_completed(t, ring_kind, buf_dma);
		if (rc != HIL_RX_OK)
			goto drain_refuse;
		/* clear-after-read: never hand a stale entry to a later lane
		 * re-arm/restart (Hi1105 hal_ring_get_entries zeroes consumed
		 * entries for this reason). */
		((u32 *)(void *)t->rx_cmp_ring)[slot] = 0u;
		cons = hi5622_rx_idx_advance(cons, HIL_RX_CMP_DEPTH);
		processed++;
	}
	if (stopped && processed == 0u) {
		mutex_unlock(&t->lock);
		dev_warn(dev,
			 "DP rx_drain: stopped before any validated completion (commit desync, spurious=%u) - no ring pointer published\n",
			 t->rx_spurious);
		return (ssize_t)count;
	}
	if (stopped)
		dev_warn(dev,
			 "DP rx_drain: stopped early after %u validated completions (commit desync, spurious=%u) - publishing only the validated prefix\n",
			 processed, t->rx_spurious);
	pending = processed;
	dma_wmb();
	/* Commit point: every entry validated.  Refill the free rings first (the
	 * buffers were handed back by completions, so this cannot double-hand a
	 * live buffer), then run the watermark pass against a FRESH live-bank
	 * consumer snapshot (task-156: the bounded window refill alone leaks
	 * posted entries when the completion backlog exceeds rx_drain_max), then
	 * publish the completion-ring consumer last. */
	norm_refill = dptx_rx_refill(t, 0u);
	if (norm_refill < 0) {
		rc = norm_refill;
		goto drain_refuse;
	}
	small_refill = dptx_rx_refill(t, 1u);
	if (small_refill < 0) {
		rc = small_refill;
		goto drain_refuse;
	}
	rc = dptx_live_read_checked(ctx, &st, &nr, &sr, &cw);
	if (rc)
		goto drain_refuse;
	t->rx_live_status = st;
	t->rx_live_norm_rptr = nr;
	t->rx_live_small_rptr = sr;
	t->rx_live_cmp_wptr = cw;
	rc = dptx_rx_wm_refill(t, dptx_rx_refill_wm, nr, sr, max,
			       &wm_norm_refill, &wm_small_refill);
	if (rc < 0)
		goto drain_refuse;
	t->rx_cmp_cons = cons;
	rc = dptx_publish16(ctx, HIL_RX_CFG_CMP_RPTR, cons, NULL);
	if (rc)
		goto drain_refuse;
	if (dptx_rx_clear_status) {
		/* Vendor ISR clear path: status &= ~mask, then W1C exactly the
		 * served bits (the inherited code W1C'd the raw status word, i.e.
		 * bits the vendor never acknowledges). */
		u32 mask_now = dptx_mac_read(ctx, HIL_RX_CFG_INT_MASK);
		u32 w1c = 0u;

		if (mask_now == 0xffffffffu ||
		    rxp_w1c_served(st, mask_now, &w1c) != 0) {
			rc = -EFAULT;
			goto drain_refuse;
		}
		if (dptx_mac_write_checked(ctx, HIL_RX_CFG_INT_STATUS, w1c) != 0) {
			rc = -EIO;
			goto drain_refuse;
		}
	}
	t->rx_drained += pending;
	t->rx_drain_actions++;
	dptx_rx_stall_update(t, st, nr, sr, cw);
	mutex_unlock(&t->lock);
	dev_info(dev,
		 "DP rx_drain: drained=%u/%u cmp_cons=0x%04x live{cmp_wptr=0x%04x norm_rptr=0x%04x small_rptr=0x%04x status=0x%08x stall=%s commit{waits=%u ok=%u spurious=%u}} refilled{norm=%d small=%d} wm{pct=%u trips=%u credit_low=%u refilled{norm=%d small=%d} posted{norm=%u small=%u} target{norm=%u small=%u}} totals{drained=%u actions=%u bad=%u chain=%u} - completion ring consumer published, free rings refilled\n",
		 pending, pending, cons, cw, nr, sr, st,
		 rxp_stall_name((enum rxp_stall) t->rx_stall_class),
		 t->rx_commit_waits, t->rx_commit_ok, t->rx_spurious,
		 norm_refill, small_refill,
		 dptx_rx_refill_wm, t->rx_wm_trips, t->rx_wm_credit_low,
		 wm_norm_refill, wm_small_refill, t->rx_wm_posted_norm,
		 t->rx_wm_posted_small, t->rx_wm_target_norm,
		 t->rx_wm_target_small, t->rx_drained, t->rx_drain_actions,
		 t->rx_bad_entry, t->rx_chain_refused);
	return (ssize_t)count;

drain_refuse:
	mutex_unlock(&t->lock);
	return dptx_rx_fail(dev, t, rc, "completion/header validation");
}

/* ------------------------------------------------------------------ */
/* wifi-dp-tuning (task-134): TXQ-table publish + consumer evidence   */
/* ------------------------------------------------------------------ */
ssize_t hi5622_dp_txq_show(struct device *dev, struct device_attribute *attr,
                           char *buf)
{
    struct hi5622_dp_txrx *t = dptx_from_dev(dev);
    const dp_u8 *tab;
    u32 blk;
    int pos = 0;
    unsigned int tid;

    (void)attr;
    if (!t || !t->ctx)
        return -ENODEV;
    mutex_lock(&t->lock);
    if (!t->bound || !t->txq_table) {
        mutex_unlock(&t->lock);
        return -ENODEV;
    }
    tab = (const dp_u8 *)t->txq_table;
    blk = dp_txrx_txq_user_block(DP_TXRX_TX_USER_DEFAULT);
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "DP TXQ table (host DRAM, devva 0x%08x, %u B): txq_publish=%u publications=%u ts_ms=0x%04x user0_flags=0x%02x (bitmap: bit tid = TID non-empty)\n",
                     t->txq_table_devva, DP_TXRX_TXQ_TABLE_BYTES,
                     dptx_txq_publish_en, t->txq_publish_count,
                     (u32)(dp_txrx_le32(tab) & 0xffffu),
                     tab[DP_TXRX_TXQ_FLAGS_OFF]);
    for (tid = 0; tid < DP_TXRX_TABLE_TIDS; tid++) {
        u32 raw_qlen = dp_txrx_le32(tab + blk + DP_TXRX_TXQ_QLEN_OFF +
                                    2u * tid) & 0xffffu;
        u32 raw_sn = dp_txrx_le32(tab + blk + DP_TXRX_TXQ_START_SN_OFF +
                                  2u * tid) & 0xffffu;
        u32 raw_bytes = dp_txrx_le32(tab + blk + DP_TXRX_TXQ_BYTES_OFF +
                                     4u * tid);

        pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                         "  tid %u: table{count=%u sn=%u bytes=%u} software{posted=%u wptr=0x%04x bytes=%u sn=%u}\n",
                         tid, raw_qlen, raw_sn, raw_bytes,
                         t->tx_tid_count[tid], t->tx_tid_wptr[tid],
                         t->tx_tid_bytes[tid], (u32)t->tx_tid_sn[tid]);
    }
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "vendor order [V]: ring entry -> dmb -> device-RAM table wptr -> TXQ table {ts, count/sn/bytes, bitmap}; [I] the firmware TXQ consumer keys on this state\n");
    mutex_unlock(&t->lock);
    return pos;
}

ssize_t hi5622_dp_mac_ext_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    static const u32 offs[] = {
        0x58u, 0x5cu, 0x60u, 0x64u, 0x68u, 0x6cu, 0x70u, 0x74u,
        0x78u, 0x7cu, 0x80u, 0x84u, 0x88u, 0x8cu, 0x90u, 0x94u,
        0x98u, 0x9cu, 0xa0u, 0xa4u, 0xa8u, 0xacu,
    };
    struct hi5622_dp_txrx *t = dptx_from_dev(dev);
    struct hi5622_pci *ctx;
    int pos = 0;
    unsigned int i;

    (void)attr;
    if (!t || !t->ctx)
        return -ENODEV;
    ctx = t->ctx;
    if (!ctx->windows_programmed || !ctx->hcc || !ctx->hcc->armed)
        return -EBUSY;
    mutex_lock(&t->lock);
    pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                     "DP MAC band0 role-5 tail (read-only; host programs 0x00..0x54 only):\n");
    for (i = 0; i < ARRAY_SIZE(offs); i++)
        pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                         "  +0x%02x = 0x%08x%s\n", offs[i],
                         dptx_mac_read(ctx, offs[i]),
                         (offs[i] == 0x5cu || offs[i] == 0x6cu) ?
                         "  (vendor live dump [V*]: 0x00000004)" : "");
    if (t->mac_tail_done)
        pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                         "mac_tail_init ran: +0x5c=0x%08x +0x6c=0x%08x (opt-in experiment)\n",
                         t->mac_tail_values[0], t->mac_tail_values[1]);
    else
        pos += scnprintf(buf + pos, PAGE_SIZE - pos,
                         "mac_tail_init not run (mac_tail_writes=%u; the vendor-live 4s at +0x5c/+0x6c are [U] - writer not found in the shipped host module)\n",
                         dptx_mac_tail_writes);
    mutex_unlock(&t->lock);
    return pos;
}

ssize_t hi5622_dp_tx_complete_poll_store(struct device *dev,
                                         struct device_attribute *attr,
                                         const char *buf, size_t count)
{
    struct hi5622_dp_txrx *t = dptx_from_dev(dev);
    unsigned long waited = 0;
    unsigned int tid;
    u32 d[3] = { 0u, 0u, 0u };
    int pending = -1;
    int rc = 0;

    (void)attr;
    if (!t || !t->ctx)
        return -ENODEV;
    if (!sysfs_streq(buf, "1"))
        return -EINVAL;
    mutex_lock(&t->lock);
    if (t->failed || !t->bound) {
        mutex_unlock(&t->lock);
        return -ENODEV;
    }
    if (t->tx_complete_polled) {
        mutex_unlock(&t->lock);
        return -EALREADY;
    }
    if (!t->tx_posted) {
        dev_warn(dev,
                 "DP tx_complete_poll: no TX frame posted in this module load - post one first\n");
        mutex_unlock(&t->lock);
        return -EINVAL;
    }
    rc = dptx_write_gate(t->ctx);
    if (rc) {
        dev_warn(dev,
                 "DP tx_complete_poll: refused by gate '%s' - no device write (needs dp_setup + dp_txq_sync)\n",
                 dptx_gate_name(rc));
        mutex_unlock(&t->lock);
        return rc;
    }
    tid = t->tx_last_tid;
    if (tid >= DP_TXRX_TABLE_TIDS)
        tid = 0u;
    while (waited < (unsigned long)dptx_tx_complete_ms) {
        if (dptx_table_snapshot_tid(t, tid, d) != 0)
            break;
        pending = dp_txrx_idx_count(d[1], d[2], DP_TXRX_TX_RING_DEPTH);
        if (pending == 0)
            break;
        msleep(DPTX_TX_COMPLETE_STEP_MS);
        waited += DPTX_TX_COMPLETE_STEP_MS;
    }
    t->tx_complete_waited_ms = (u32)waited;
    t->tx_complete_pending = pending < 0 ? 0xffffffffu : (u32)pending;
    t->tx_complete_table[0] = d[0];
    t->tx_complete_table[1] = d[1];
    t->tx_complete_table[2] = d[2];
    t->tx_complete_mac44 = dptx_mac_read(t->ctx, 0x44u);
    t->tx_complete_mac4c = dptx_mac_read(t->ctx, 0x4cu);
    if (t->txba) {
        const dp_u8 *ba = (const dp_u8 *)t->txba;
        unsigned int k;

        for (k = 0; k < 4u; k++)
            t->tx_complete_txba[k] = dp_txrx_le32(ba + 4u * k);
    }
    t->tx_complete_verdict = (pending == 0) ? 1u : 2u;
    t->tx_complete_polled = true;
    mutex_unlock(&t->lock);

    if (t->tx_complete_verdict == 1u)
        dev_info(dev,
                 "DP tx_complete_poll: COMPLETED (MAC) after %lu ms - device rptr 0x%08x == published wptr 0x%08x for tid %u; table={0x%08x,0x%08x,0x%08x} MAC+0x44=0x%08x MAC+0x4c=0x%08x txba[0..3]=0x%08x/0x%08x/0x%08x/0x%08x (air needs an external observer)\n",
                 waited, d[2], d[1], tid, d[0], d[1], d[2],
                 t->tx_complete_mac44, t->tx_complete_mac4c,
                 t->tx_complete_txba[0], t->tx_complete_txba[1],
                 t->tx_complete_txba[2], t->tx_complete_txba[3]);
    else
        dev_info(dev,
                 "DP tx_complete_poll: POST-ONLY after %lu ms - device rptr 0x%08x != published wptr 0x%08x for tid %u; table={0x%08x,0x%08x,0x%08x} MAC+0x44=0x%08x MAC+0x4c=0x%08x txba[0..3]=0x%08x/0x%08x/0x%08x/0x%08x (record, no retry)\n",
                 waited, d[2], d[1], tid, d[0], d[1], d[2],
                 t->tx_complete_mac44, t->tx_complete_mac4c,
                 t->tx_complete_txba[0], t->tx_complete_txba[1],
                 t->tx_complete_txba[2], t->tx_complete_txba[3]);
    return (ssize_t)count;
}

ssize_t hi5622_dp_mac_tail_init_store(struct device *dev,
                                      struct device_attribute *attr,
                                      const char *buf, size_t count)
{
    static const u32 offs[2] = { 0x5cu, 0x6cu };
    static const u32 vals[2] = { 4u, 4u };
    struct hi5622_dp_txrx *t = dptx_from_dev(dev);
    struct hi5622_pci *ctx;
    struct hcc_core *core;
    unsigned int i;
    int rc;

    (void)attr;
    if (!t || !t->ctx)
        return -ENODEV;
    if (!sysfs_streq(buf, "1"))
        return -EINVAL;
    if (!dptx_mac_tail_writes) {
        dev_warn(dev,
                 "DP mac_tail_init: refused - mac_tail_writes=0 (default). The vendor-live 4s at MAC band0 +0x5c/+0x6c have no identified writer and their meaning is [U]; this is an opt-in attended experiment only\n");
        return -EPERM;
    }
    ctx = t->ctx;
    mutex_lock(&t->lock);
    if (t->mac_tail_done || t->failed || !t->bound) {
        mutex_unlock(&t->lock);
        return -EALREADY;
    }
    if (!ctx->windows_programmed || !ctx->hcc || !ctx->hcc->armed) {
        mutex_unlock(&t->lock);
        return -EBUSY;
    }
    core = &ctx->hcc->hcc.core;
    if (!core->released || !ctx->cpu_released || !core->boot_latched) {
        mutex_unlock(&t->lock);
        return -EBUSY;
    }
    if (core->phase != HCC_PHASE_READY_TXQ_SYNC &&
        core->phase != HCC_PHASE_ACTIVE) {
        mutex_unlock(&t->lock);
        return -EPERM;
    }
    for (i = 0; i < ARRAY_SIZE(offs); i++) {
        u32 rb;

        rc = dptx_mac_write_checked(ctx, offs[i], vals[i]);
        if (rc) {
            mutex_unlock(&t->lock);
            return rc;
        }
        rb = dptx_mac_read(ctx, offs[i]);
        if (rb != vals[i]) {
            dev_err(dev,
                    "DP mac_tail_init: +0x%02x read-back 0x%08x != written 0x%08x - lane closed, no retry\n",
                    offs[i], rb, vals[i]);
            t->failed = true;
            mutex_unlock(&t->lock);
            return -EIO;
        }
        t->mac_tail_values[i] = rb;
    }
    t->mac_tail_done = true;
    mutex_unlock(&t->lock);
    dev_warn(dev,
             "DP mac_tail_init: EXPERIMENT - wrote vendor-live 4 to MAC band0 +0x5c/+0x6c (read-back verified); meaning [U], one shot per load, no retry; observe dp_tx_frame/dp_tx_complete_poll next\n");
    return (ssize_t)count;
}

/* ------------------------------------------------------------------ */
/* wifi-rx-linux-parity (task-166): read-only parity probe, the opt-in */
/* ring kick, and the one-shot allowlisted RX-control experiment       */
/* ------------------------------------------------------------------ */

/* Absolute-CA MAC read through the same range-checked translation.  The probe
 * reads the other band-0 machw blocks, whose CAs are not the host-interface
 * base; the helper refuses any CA the six inbound regions do not cover. */
static u32 dptx_mac_read_ca(struct hi5622_pci *ctx, u32 ca)
{
	u32 bar0 = hi5622_ca_to_bar0(ca);

	if (!bar0)
		return 0xffffffffu;
	return readl(ctx->bar0 + bar0);
}

/* dp_rx_parity - READ-ONLY parity probe.  Linux drivers program and inspect
 * the whole RX queue state (base/size/index/enable) as one object; our driver
 * only ever reads the live bank and the config words it writes.  This probe
 * reads, in one bounded pass:
 *   - the live bank CA 0x40050000 +0x00..+0x3c (16 words),
 *   - the MAC host-interface config block CA 0x40040000 +0x00..+0xac (44
 *     words, classified against the committed vendor stock capture),
 *   - the first 16 words of the other eight band-0 machw blocks (words 0..4,
 *     6, 7, 9) so a missing host-writable enable/filter word in another block
 *     becomes visible instead of being guessed at.
 * The vendor's own register dumper reads exactly these blocks (its output is
 * opensource/dumps/reg_all.txt), so the reads are proven safe; the probe never
 * writes.  Compare with tools/compare_stock.py. */
ssize_t hi5622_dp_rx_parity_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	static const u32 blocks[] = {
		0x40042000u, 0x40044000u, 0x40046000u, 0x40048000u,
		0x4004a000u, 0x4004c000u, 0x40052000u, 0x40054000u,
	};
	struct hi5622_dp_txrx *t = dptx_from_dev(dev);
	struct hi5622_pci *ctx;
	int pos = 0;
	unsigned int i, k;
	u32 off;

	(void)attr;
	if (!t || !t->ctx)
		return -ENODEV;
	ctx = t->ctx;
	if (!ctx->windows_programmed || !ctx->hcc || !ctx->hcc->armed)
		return -EBUSY;
	mutex_lock(&t->lock);
	if (!t->bound || t->failed) {
		mutex_unlock(&t->lock);
		return -ENODEV;
	}
	t->rx_parity_probes++;
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "DP rx parity probe #%u (read-only, one MAC band-0 pass; the vendor register dumper reads the same blocks):\n",
			 t->rx_parity_probes);
	pos += scnprintf(buf + pos, PAGE_SIZE - pos, "live %08x:",
			 HIL_RX_LIVE_CA_BAND0);
	for (i = 0; i < 16u; i++)
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, " %08x",
				 dptx_mac_read_live(ctx, i * 4u));
	pos += scnprintf(buf + pos, PAGE_SIZE - pos, "\n");
	for (k = 0; k < RXP_MAC_CFG_SPAN / 4u; k++) {
		int stock_nonzero = 0, matches = 0, cls;
		const char *mark = "";
		u32 value;

		off = k * 4u;
		if (k % 16u == 0u)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos,
					 "%s%08x:", k == 0u ? "" : "\n",
					 DP_MAC_CA_BAND0 + off);
		value = dptx_mac_read(ctx, off);
		cls = rxp_mac_cfg_class(off, value, &stock_nonzero, &matches);
		if (cls == 3)
			mark = "*";  /* vendor non-zero, never written by us */
		else if (cls == 2)
			mark = "!";  /* programmed word differs from stock   */
		else if (value == 0xffffffffu)
			mark = "?";
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, " %08x%s",
				 value, mark);
	}
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "\nlegend: * = non-zero in the vendor stock capture but never written by our 14-write program; ! = programmed word differing from the stock capture; ? = transport miss\n");
	for (i = 0; i < ARRAY_SIZE(blocks); i++) {
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "blk  %08x:",
				 blocks[i]);
		for (k = 0; k < 16u; k++)
			pos += scnprintf(buf + pos, PAGE_SIZE - pos, " %08x",
					 dptx_mac_read_ca(ctx,
							  blocks[i] + k * 4u));
		pos += scnprintf(buf + pos, PAGE_SIZE - pos, "\n");
	}
	pos += scnprintf(buf + pos, PAGE_SIZE - pos,
			 "parity: commit{waits=%u ok=%u spurious=%u closed=%u} ring_kick{en=%u done=%u} ctl{en=%u done=%u off=0x%02x old=0x%08x new=0x%08x} legacy_bank{en=%u reads=%u} stall=%s\n",
			 t->rx_commit_waits, t->rx_commit_ok, t->rx_spurious,
			 t->rx_spurious_closed, dptx_rx_ring_kick,
			 t->rx_kick_done ? 1u : 0u, dptx_rx_ctl_enable,
			 t->rx_ctl_done ? 1u : 0u, t->rx_ctl_off,
			 t->rx_ctl_old, t->rx_ctl_value, dptx_rx_legacy_bank,
			 t->rx_legacy_reads,
			 rxp_stall_name((enum rxp_stall)t->rx_stall_class));
	mutex_unlock(&t->lock);
	return pos;
}

/* dp_rx_kick - opt-in attended experiment: re-publish the two free-ring
 * producer words with the exact vendor zero-extended u16 AFTER dp_rx_arm.  The
 * Linux analogues are the explicit kick store mt76_dma_kick_queue performs
 * after it fills descriptors (wmb then index store) and brcmfmac's
 * write-complete (wptr store then doorbell).  Our 14-write program publishes
 * the producer once; this lane offers a second, identical-width publish to
 * test whether the MAC latches its ring-armed state on a later kick.  One shot
 * per load, gated exactly like the drain; default 0 = refused. */
ssize_t hi5622_dp_rx_kick_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	static const u32 offs[2] = {
		HIL_RX_CFG_WPTR_NORM, HIL_RX_CFG_WPTR_SMALL
	};
	struct hi5622_dp_txrx *t = dptx_from_dev(dev);
	struct hi5622_pci *ctx;
	u32 vals[2], rb;
	unsigned int i;
	int rc;

	(void)attr;
	if (!t || !t->ctx)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	ctx = t->ctx;
	mutex_lock(&t->lock);
	if (!dptx_rx_ring_kick) {
		mutex_unlock(&t->lock);
		dev_warn(dev,
			 "DP rx_kick: refused - rx_ring_kick=0 (default). This is an opt-in attended experiment only\n");
		return -EPERM;
	}
	if (!t->bound || t->failed) {
		mutex_unlock(&t->lock);
		return -ENODEV;
	}
	if (t->rx_kick_done) {
		mutex_unlock(&t->lock);
		return -EALREADY;
	}
	if (!dptx_rx_enable || !t->rx_armed || t->rx_failed) {
		mutex_unlock(&t->lock);
		dev_warn(dev,
			 "DP rx_kick: refused - needs rx_enable=1, a successful dp_rx_arm and an open RX lane\n");
		return -EPERM;
	}
	rc = dptx_write_gate(ctx);
	if (rc) {
		mutex_unlock(&t->lock);
		dev_warn(dev, "DP rx_kick: refused by gate '%s' - no device write\n",
			 dptx_gate_name(rc));
		return rc;
	}
	vals[0] = t->rx_norm_prod;
	vals[1] = t->rx_small_prod;
	for (i = 0; i < 2u; i++) {
		rc = dptx_publish16(ctx, offs[i], vals[i], &rb);
		if (rc) {
			mutex_unlock(&t->lock);
			dev_err(dev,
				"DP rx_kick: +0x%02x publish failed (%d) - lane not marked done, reload to retry\n",
				offs[i], rc);
			return rc;
		}
		t->rx_kick_values[i] = rb;
	}
	t->rx_kick_done = true;
	mutex_unlock(&t->lock);
	dev_warn(dev,
		 "DP rx_kick: EXPERIMENT - re-published exact-width producer words norm +0x38=0x%08x small +0x3c=0x%08x (read-back 0x%08x/0x%08x); one shot per load, no retry; observe dp_rx_parity/dp_rx_capture next\n",
		 vals[0], vals[1], t->rx_kick_values[0], t->rx_kick_values[1]);
	return (ssize_t)count;
}

/* dp_rx_ctl - opt-in, one-shot, allowlisted RX-control candidate-word write.
 * Linux drivers always write an explicit RX engine/ring enable word before a
 * ring can produce completions (mt76 WPDMA_GLO_CFG RX_DMA_EN, iwlwifi
 * FH_MEM_RCSR_CHNL0_CONFIG_REG channel enable / RFH_RXF_DMA_CFG RFH_DMA_EN,
 * ath11k HAL_REO1_RING_MISC_SRNG_ENABLE).  Our chip has no decoded equivalent
 * yet [U]; the four allowlisted words (0x08/0x0c/0x14/0x18) are the only
 * MAC-config-block words that are zero in the vendor stock capture and are not
 * part of the 14-write ring program.  The action is a deliberate attended
 * experiment: explicit module gate, one word, one shot per load, read-back
 * verified, and a read-back mismatch closes the tail lane (no retry). */
ssize_t hi5622_dp_rx_ctl_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct hi5622_dp_txrx *t = dptx_from_dev(dev);
	struct hi5622_pci *ctx;
	u32 off, old, value = 0u, rb;
	int rc;

	(void)attr;
	if (!t || !t->ctx)
		return -ENODEV;
	if (!sysfs_streq(buf, "1"))
		return -EINVAL;
	ctx = t->ctx;
	mutex_lock(&t->lock);
	if (!dptx_rx_ctl_enable) {
		mutex_unlock(&t->lock);
		dev_warn(dev,
			 "DP rx_ctl: refused - rx_ctl_enable=0 (default). One allowlisted candidate-word experiment only, attended\n");
		return -EPERM;
	}
	if (!t->bound || t->failed) {
		mutex_unlock(&t->lock);
		return -ENODEV;
	}
	if (t->rx_ctl_done) {
		mutex_unlock(&t->lock);
		return -EALREADY;
	}
	if (!dptx_rx_enable || !t->rx_armed || t->rx_failed) {
		mutex_unlock(&t->lock);
		dev_warn(dev,
			 "DP rx_ctl: refused - needs rx_enable=1, a successful dp_rx_arm and an open RX lane\n");
		return -EPERM;
	}
	off = dptx_rx_ctl_off;
	if (!rxp_ctl_offset_allowed(off)) {
		mutex_unlock(&t->lock);
		dev_warn(dev,
			 "DP rx_ctl: refused - +0x%02x is not an allowlisted candidate word (0x08/0x0c/0x14/0x18 only)\n",
			 off);
		return -EINVAL;
	}
	rc = dptx_write_gate(ctx);
	if (rc) {
		mutex_unlock(&t->lock);
		dev_warn(dev, "DP rx_ctl: refused by gate '%s' - no device write\n",
			 dptx_gate_name(rc));
		return rc;
	}
	old = dptx_mac_read(ctx, off);
	if (old == 0xffffffffu) {
		mutex_unlock(&t->lock);
		return -EFAULT;
	}
	if (rxp_ctl_value(off, old, dptx_rx_ctl_set, dptx_rx_ctl_clr,
			  &value) != 0) {
		mutex_unlock(&t->lock);
		dev_warn(dev,
			 "DP rx_ctl: refused - invalid set/clr composition (set=0x%08x clr=0x%08x; a bit cannot be both and set|clr must be non-zero)\n",
			 dptx_rx_ctl_set, dptx_rx_ctl_clr);
		return -EINVAL;
	}
	/* one shot: mark done before the write so no path can retry it */
	t->rx_ctl_done = true;
	t->rx_ctl_off = off;
	t->rx_ctl_old = old;
	t->rx_ctl_value = value;
	if (dptx_mac_write_checked(ctx, off, value) != 0) {
		mutex_unlock(&t->lock);
		return -EIO;
	}
	rb = dptx_mac_read(ctx, off);
	if ((rb & (dptx_rx_ctl_set | dptx_rx_ctl_clr)) !=
	    (value & (dptx_rx_ctl_set | dptx_rx_ctl_clr))) {
		t->failed = true;
		mutex_unlock(&t->lock);
		dev_err(dev,
			"DP rx_ctl: read-back 0x%08x != written 0x%08x - tail lane CLOSED, no retry\n",
			rb, value);
		return -EIO;
	}
	mutex_unlock(&t->lock);
	dev_warn(dev,
		 "DP rx_ctl: EXPERIMENT - MAC band0 +0x%02x old=0x%08x new=0x%08x (set=0x%08x clr=0x%08x) read-back=0x%08x; meaning [U], one shot per load, no retry; observe dp_rx_parity / live-bank rptr / cmp_wptr\n",
		 off, old, value, dptx_rx_ctl_set, dptx_rx_ctl_clr, rb);
	return (ssize_t)count;
}

/* ------------------------------------------------------------------ */
/* bind / release                                                     */
/* ------------------------------------------------------------------ */
int hi5622_dp_txrx_bind(struct hi5622_pci *ctx,
			const struct hi5622_dp_txrx_bind *b)
{
	struct device *dev;
	struct hi5622_dp_txrx *t;
	dma_addr_t dma = 0;
	u32 devva;

	if (!ctx || !b)
		return -EINVAL;
	dev = &ctx->pdev->dev;
	mutex_lock(&dptx_list_lock);
	if (dptx_find(ctx)) {
		mutex_unlock(&dptx_list_lock);
		return -EALREADY;
	}
	t = devm_kzalloc(dev, sizeof(*t), GFP_KERNEL);
	if (!t) {
		mutex_unlock(&dptx_list_lock);
		return -ENOMEM;
	}
	t->ctx = ctx;
	mutex_init(&t->lock);
	t->tx_msdu_rings = b->tx_msdu_rings;
	t->tx_msdu_rings_dma = (u32)b->tx_msdu_rings_dma;
	t->txq_table_devva = b->txq_table_devva;
	t->txq_table = b->txq_table;
	t->txba = b->txba;
	t->txba_bytes = b->txba_bytes;
	t->rx_norm_ring = b->rx_norm_ring;
	t->rx_small_ring = b->rx_small_ring;
	t->rx_cmp_ring = b->rx_cmp_ring;
	t->rx_norm_cpu = b->rx_norm_cpu;
	t->rx_small_cpu = b->rx_small_cpu;
	t->rx_norm_dma = b->rx_norm_dma;
	t->rx_small_dma = b->rx_small_dma;
	t->msdu_table_ca = b->msdu_table_ca;
	t->ready_payload = b->ready_payload;
	dp_txrx_rx_state_init(&t->rx_state);
	/* dp_setup filled both free rings completely and published index 0 /
	 * phase 1 (0x8000) as the producer; the completion-ring consumer starts
	 * at 0.  These are the host-side bookkeeping values only. */
	t->rx_norm_prod = HIL_RX_PHASE_BIT;
	t->rx_small_prod = HIL_RX_PHASE_BIT;
	t->rx_cmp_cons = 0u;
	t->tx_frame = dma_alloc_coherent(dev, DP_TXRX_FRAME_AREA, &dma,
					 GFP_KERNEL);
	if (!t->tx_frame) {
		mutex_unlock(&dptx_list_lock);
		return -ENOMEM;
	}
	if ((u64)dma < (u64)DP_HOST_DMA_LO || (u64)dma > (u64)DP_HOST_DMA_HI) {
		dev_err(dev,
			"DP txrx: TX frame buffer DMA 0x%llx outside the device window 0x%08x..0x%08x\n",
			(unsigned long long)dma, DP_HOST_DMA_LO, DP_HOST_DMA_HI);
		dma_free_coherent(dev, DP_TXRX_FRAME_AREA, t->tx_frame, dma);
		t->tx_frame = NULL;
		mutex_unlock(&dptx_list_lock);
		return -ERANGE;
	}
	t->tx_frame_dma = (u64)dma;
	devva = (u32)dma + DP_TXRX_FRAME_HOST_HDR;
	memset(t->tx_frame, 0, DP_TXRX_FRAME_AREA);
	t->tx_frame_ready = true;
	t->bound = true;
	t->rx_stall_class = RXP_STALL_IDLE;
	list_add_tail(&t->node, &dptx_list);
	mutex_unlock(&dptx_list_lock);

	dev_info(dev,
		 "DP txrx: bound - TX frame buffer %u B devva 0x%08x (host word + 16 B zero DMA header + MPDU); MSDU table CA 0x%08x; attrs dp_tx_frame/dp_tx_status/dp_rx_poll/dp_rx_capture\n",
		 DP_TXRX_FRAME_AREA, devva, t->msdu_table_ca);
	return 0;
}

void hi5622_dp_txrx_release(struct hi5622_pci *ctx)
{
	struct device *dev;
	struct hi5622_dp_txrx *t;

	if (!ctx)
		return;
	dev = &ctx->pdev->dev;
	mutex_lock(&dptx_list_lock);
	t = dptx_find(ctx);
	if (!t) {
		mutex_unlock(&dptx_list_lock);
		return;
	}
	list_del(&t->node);
	mutex_unlock(&dptx_list_lock);
	if (t->tx_frame) {
		dma_free_coherent(dev, DP_TXRX_FRAME_AREA, t->tx_frame,
				  (dma_addr_t)t->tx_frame_dma);
		t->tx_frame = NULL;
		t->tx_frame_dma = 0;
	}
	t->bound = false;
	t->tx_frame_ready = false;
}
