// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_mgmt.c - kernel-side management-frame TX/RX glue (Hi5622V100
 * clean-room driver, Cudy WR3000 v2.0).
 *
 * ORIGINAL clean-room code (lane wifi-ap-mgmt, shared task task-143).
 * See hi5622_mgmt_logic.h for the decoded vendor wire layouts and
 * ../REPORT.md / ../TABLES.md for the evidence behind every constant.
 *
 * SAFETY: this file allocates one coherent frame buffer (only when mgmt_tx=1
 * actually posts), never writes a device register itself, and routes every
 * H2D byte through hi5622_frame_post() (allow_frames budget, inter-frame gap,
 * armed/released/ACTIVE lane gates, h2d_frames validator incl. the WAL
 * cfg-event family allowlist).  The RX path is read-only: decode + cfg80211
 * notification for frames the device already staged.
 */
#include <linux/dma-mapping.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/cfg80211.h>

#include "hi5622_driver.h"
#include "hi5622_cfg80211.h"
#include "hi5622_frame_post.h"
#include "hi5622_frames_api.h"
#include "hi5622_mgmt.h"
#include "hi5622_mgmt_logic.h"

/* Management-frame TX carrier (wifi-ap-mgmt, task-143).  Default 0 keeps the
 * .mgmt_tx op exactly as fail-closed as the bench-proven image: hostapd's
 * frame-registration gate is satisfied by the op's presence, and every frame
 * request is refused before any buffer/device work. */
static unsigned int hi5622_mgmt_tx;
module_param_named(mgmt_tx, hi5622_mgmt_tx, uint, 0644);
MODULE_PARM_DESC(mgmt_tx,
		 "management frame TX: 0=fail-closed default (-EOPNOTSUPP), 1=vendor WAL cfg-event carrier cmd 0x01F2 (needs allow_frames budget + armed/ACTIVE lane)");
/* Management-frame RX decode/delivery.  Default 0 = count only. */
static unsigned int hi5622_mgmt_rx;
module_param_named(mgmt_rx, hi5622_mgmt_rx, uint, 0644);
MODULE_PARM_DESC(mgmt_rx,
		 "D2H mgmt RX: 0=count only (fail-closed default), 1=decode group1 id13 and deliver to cfg80211_rx_mgmt for the AP wdev");

/* One driver instance is the bench model; a second probe is refused rather
 * than silently sharing the buffer/counters. */
struct hi5622_mgmt_state {
	struct mutex	lock;		/* serializes buffer init/post/teardown */
	struct hi5622_pci *ctx;
	void		*tx_buf;	/* HIMG_MAX_FRAME_TX coherent bytes */
	dma_addr_t	tx_dma;		/* device-visible address */
	u32		seq;		/* WAL cfg-event sequence */
	u32		tx_posts;
	u32		tx_refused;
	u32		tx_errors;
	u32		rx_events;
	u32		rx_delivered;
	u32		rx_malformed;
	u32		rx_busy;
	u32		rx_no_wdev;
};

static struct hi5622_mgmt_state mgmt_state = {
	.lock = __MUTEX_INITIALIZER(mgmt_state.lock),
};

/* Allocate the single mgmt-frame DMA buffer (the descriptor hands the firmware
 * a device-visible host-DRAM address; the vendor relies on its 2 GiB/2 GiB
 * VMSPLIT identity that this kernel does not have - see REPORT section 6). */
static int hi5622_mgmt_ensure_buf(struct hi5622_pci *ctx)
{
	struct device *dev;
	void *buf;
	dma_addr_t dma;

	if (mgmt_state.tx_buf) {
		if (mgmt_state.ctx != ctx)
			return -EBUSY;
		return 0;
	}
	if (!ctx || !ctx->pdev)
		return -ENODEV;
	dev = &ctx->pdev->dev;
	buf = dma_alloc_coherent(dev, HIMG_MAX_FRAME_TX, &dma, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;
	if ((u64)dma < (u64)HI5622_HOST_DMA_LO ||
	    (u64)dma > (u64)HI5622_HOST_DMA_HI) {
		dev_err(dev,
			"mgmt_tx: frame buffer DMA 0x%llx outside the host DMA window 0x%08x..0x%08x - refusing the vendor descriptor carrier\n",
			(unsigned long long)dma,
			(unsigned int)HI5622_HOST_DMA_LO,
			(unsigned int)HI5622_HOST_DMA_HI);
		dma_free_coherent(dev, HIMG_MAX_FRAME_TX, buf, dma);
		return -ERANGE;
	}
	mgmt_state.ctx = ctx;
	mgmt_state.tx_buf = buf;
	mgmt_state.tx_dma = dma;
	dev_info(dev,
		 "mgmt_tx: DMA frame buffer %u B (devva 0x%08x) - WAL cfg-event carrier cmd 0x%04x\n",
		 (unsigned int)HIMG_MAX_FRAME_TX, (unsigned int)dma,
		 (unsigned int)HIMG_WAL_CMD_MGMT_TX);
	return 0;
}

/* cfg80211_ops.mgmt_tx.  Fail-closed order: parameter sanity -> param gate ->
 * AP-only policy -> provider capability -> build -> post gate.  Nothing is
 * allocated or written unless every earlier step passed. */
int hi5622_mgmt_op_tx(struct wiphy *wiphy, struct wireless_dev *wdev,
		      struct cfg80211_mgmt_tx_params *params, u64 *cookie)
{
	struct hi5622_cfg80211 *c;
	const struct hi5622_frame_ops *ops;
	struct hi5622_mgmt_tx_params p;
	u8 wire[HIMG_WAL_OVERHEAD + HIMG_WAL_CMD_MGMT_TX_BODY];
	size_t wire_len = 0;
	u64 ck = 0;
	bool ack = false;
	int rc;

	if (!wiphy || !wdev || !params || !cookie)
		return -EINVAL;
	if (!params->buf || params->len == 0u)
		return -EINVAL;
	if (!hi5622_mgmt_tx) {
		pr_info_once("hi5622: mgmt_tx op reached - mgmt_tx=0 fail-closed (management TX stays off until the attended ladder enables it)\n");
		return -EOPNOTSUPP;
	}
	if (hi5622_mgmt_tx > 1u)
		return -EOPNOTSUPP;
	/* The AP-side exchange is the decoded path; the vendor STA offchannel
	 * branch (wal_cfg80211_mgmt_tx) is a different function and stays out
	 * of scope for this lane, so refuse non-AP interfaces explicitly. */
	if (wdev->iftype != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;
	if (!wdev->wiphy || wdev->wiphy != wiphy)
		return -EINVAL;
	/* Vendor requires a channel: for the AP the frame goes out on the AP's
	 * channel and cfg80211/hostapd always pass it (the vendor logs "vap
	 * pchan ptr is null" and refuses otherwise) [V]. */
	if (!params->chan)
		return -EINVAL;
	if (himg_mgmt_frame_ok(params->buf, params->len))
		return -EINVAL;
	c = wiphy_priv(wiphy);
	if (!c || !c->ctx)
		return -ENODEV;

	mutex_lock(&mgmt_state.lock);
	rc = hi5622_mgmt_ensure_buf(c->ctx);
	if (rc) {
		mgmt_state.tx_refused++;
		goto out_unlock;
	}
	ops = hi5622_frame_ops_get();
	if (!ops || !(ops->caps & HI5622_FRAME_CAP_MGMT_TX) ||
	    !ops->build_mgmt_tx) {
		mgmt_state.tx_refused++;
		rc = -EOPNOTSUPP;
		goto out_unlock;
	}
	/* The frame must live in the coherent buffer the descriptor points at;
	 * the cfg80211 buffer itself is not device-visible in this kernel. */
	memcpy(mgmt_state.tx_buf, params->buf, params->len);
	memset(&p, 0, sizeof(p));
	p.channel = params->chan->hw_value;
	p.cookie_idx = 0u;	/* vendor cookie-array slot; firmware echoes it */
	p.len = (u16)params->len;
	p.frame_devaddr = (u32)mgmt_state.tx_dma;
	p.seq = ++mgmt_state.seq;
	p.dst = 0u;		/* single AP VAP -> dst nibble 0 [V] */
	rc = ops->build_mgmt_tx(wire, sizeof(wire), &wire_len, &p);
	if (rc) {
		mgmt_state.tx_errors++;
		goto out_unlock;
	}
	rc = hi5622_frame_post(c->ctx, wire, wire_len, "mgmt_tx", NULL);
	if (!rc) {
		mgmt_state.tx_posts++;
		ck = (u64)p.seq;
		*cookie = ck;
		ack = true;
	} else {
		mgmt_state.tx_errors++;
	}
out_unlock:
	mutex_unlock(&mgmt_state.lock);
	/* hostapd wants a status notification for every accepted request; report
	 * the post result (the frame-post consumption poll already waited for the
	 * coalesced announce, so this is a real ack fact, not a guess). */
	if (ck)
		cfg80211_mgmt_tx_status(wdev, ck, params->buf, params->len,
					ack, GFP_ATOMIC);
	return rc;
}

/* Device->host delivery (both receive paths).  Runs in the HCC service
 * thread: trylock only, bounded reads, no retry. */
void hi5622_mgmt_deliver(struct hi5622_pci *ctx, const u8 *hdr,
			 const u8 *payload, u32 avail, u32 wire_len)
{
	struct himg_rx_frame rx;
	struct hi5622_cfg80211 *c;
	struct wireless_dev *wdev = NULL;
	int rc;
	unsigned int i;

	if (!ctx || !hdr || !payload)
		return;
	rc = himg_parse_rx_mgmt(hdr, payload, avail, wire_len, &rx);
	if (rc) {
		if (rc != -ENODATA)
			mgmt_state.rx_malformed++;
		return;
	}
	mgmt_state.rx_events++;
	if (!hi5622_mgmt_rx)
		return;		/* count only (fail-closed default) */
	c = ctx->cfg;
	if (!c || !c->wiphy_registered)
		return;
	if (!mutex_trylock(&c->lock)) {
		mgmt_state.rx_busy++;	/* dropped, never retried */
		return;
	}
	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++) {
		if (c->vap_wdev[i] &&
		    c->vap_wdev[i]->iftype == NL80211_IFTYPE_AP) {
			wdev = c->vap_wdev[i];
			break;
		}
	}
	if (!wdev) {
		mgmt_state.rx_no_wdev++;
		mutex_unlock(&c->lock);
		return;
	}
	/* Vendor oal_cfg80211_rx_mgmt passes sig_dbm=0 [V]; the decoded RX-info
	 * RSSI is signed dBm-like ([I]); use it only when it is in the plausible
	 * dBm range, else 0 (unknown) - cfg80211 accepts 0. */
	cfg80211_rx_mgmt(wdev, rx.freq,
			 (rx.rssi <= 0 && rx.rssi >= -120) ? (int)rx.rssi : 0,
			 rx.frame, rx.frame_len, 0);
	mgmt_state.rx_delivered++;
	mutex_unlock(&c->lock);
	dev_info_ratelimited(&ctx->pdev->dev,
			     "cfg80211: mgmt RX delivered (subtype 0x%02x ch %u %u MHz rssi %d len %u) to the AP wdev\n",
			     rx.subtype, rx.channel, rx.freq, rx.rssi,
			     rx.frame_len);
}

void hi5622_mgmt_remove(struct hi5622_pci *ctx)
{
	void *buf;
	dma_addr_t dma;

	if (!ctx)
		return;
	mutex_lock(&mgmt_state.lock);
	buf = mgmt_state.tx_buf;
	dma = mgmt_state.tx_dma;
	mgmt_state.tx_buf = NULL;
	mgmt_state.tx_dma = 0;
	mgmt_state.ctx = NULL;
	mutex_unlock(&mgmt_state.lock);
	if (buf)
		dma_free_coherent(&ctx->pdev->dev, HIMG_MAX_FRAME_TX, buf, dma);
}
