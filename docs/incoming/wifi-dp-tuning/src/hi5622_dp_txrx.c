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

	/* Bounded wait for the device completion producer to move. */
	while (waited < DPTX_RX_POLL_MS) {
		u32 wptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXCMP_WPTR);

		pending = (u32)dp_txrx_idx_count(wptr, t->rx_state.cmp_rptr,
						 DP_TXRX_RX_CMP_DEPTH);
		if (pending > 0u)
			break;
		msleep(DPTX_RX_POLL_STEP_MS);
		waited += DPTX_RX_POLL_STEP_MS;
	}
	t->rx_waited_ms = waited;
	t->rx_cmp_wptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXCMP_WPTR);
	t->rx_cmp_rptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXCMP_RPTR);
	t->rx_norm_rptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXNORM_RPTR);
	t->rx_small_rptr = dptx_mac_read(ctx, DP_TXRX_MAC_REG_RXSMALL_RPTR);
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
				 "cap[%u]: ring=%s idx=%u desc_ok=%u payload_len=%u mpdu_len=%u cmp_raw=0x%08x\n",
				 k, cap->ring_kind == 0u ? "norm" : "small",
				 cap->ring_index, cap->desc_ok,
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
	mutex_unlock(&t->lock);
	return pos;
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
	t->msdu_table_ca = b->msdu_table_ca;
	t->ready_payload = b->ready_payload;
	dp_txrx_rx_state_init(&t->rx_state);
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
