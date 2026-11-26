// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_dfsb_k.c - the kernel-side DFS/CAC runtime and the 160 MHz width
 * selector wiring for the Hi5622V100 clean-room full-MAC driver
 * (Cudy WR3000 v2.0, Linux 6.18.60).
 *
 * ORIGINAL clean-room code (lane dfs160b / shared task task-245, 2026-11-26).
 * Protocol facts: the 0x07E7 ARM/CLEAR frame + worker CA 0xB58FC of
 * ../wifi-5g-dfs-set and ../wifi-5g-cac-context, the CAC/NOL timings of
 * ../wifi-5g-160-dfs TABLES T4/T5, the width selector/table facts of
 * TABLES T2/T3/T7.  No vendor source, object code or firmware byte is
 * reproduced; the module artifact is local evidence only and never
 * committed.
 *
 * cfg80211 contract (net/wireless/nl80211.c, mlme.c, 6.18.60):
 *   - .start_radar_detection returning 0 makes cfg80211 mark the wdev CAC
 *     started; the driver must later report cfg80211_cac_event(
 *     NL80211_RADAR_CAC_FINISHED) / (ABORTED) with the same chandef;
 *   - cfg80211_cac_event(FINISHED) WARNs if it arrives before
 *     start_time + cac_time_ms, so the arithmetic below is strictly longer
 *     than the requested CAC (vendor first tick 63000 ms, then 60000 ms);
 *   - .end_cac is the abort path: the driver stops its hardware state, the
 *     core emits NL80211_RADAR_CAC_ABORTED itself.
 *
 * THE HONEST LIMIT (documented in REPORT.md): the firmware 0x07E7 ARM worker
 * needs a started module context (state u8[mctx+0x0E] in 1..3), which the
 * 0x0065 VAP-start step writes.  A RADAR_DETECT arriving before start_ap
 * therefore hits the fail-closed precheck and the op returns -EAGAIN with
 * the live probe pack in dmesg (the bench learns which prerequisite is
 * missing; no frame is posted into a worker that would drop it).
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211.h"
#include "hi5622_dfs_set.h"
#include "hi5622_dfsb.h"
#include "hi5622_dfsb_k.h"
#include "hi5622_driver.h"
#include "hi5622_frame_post.h"
#include "hi5622_scan.h"

/* The model's nl80211 width pins are the 6.18.60 UAPI values: a drift is a
 * compile error here, not a silent wrong selector (the sibling-lane defect
 * class this lane guards against). */
_Static_assert((int)NL80211_CHAN_WIDTH_20_NOHT ==
	       (int)HI5622_DFSB_WIDTH_20NOHT, "nl80211 width 20_NOHT drift");
_Static_assert((int)NL80211_CHAN_WIDTH_20 == (int)HI5622_DFSB_WIDTH_20,
	       "nl80211 width 20 drift");
_Static_assert((int)NL80211_CHAN_WIDTH_40 == (int)HI5622_DFSB_WIDTH_40,
	       "nl80211 width 40 drift");
_Static_assert((int)NL80211_CHAN_WIDTH_80 == (int)HI5622_DFSB_WIDTH_80,
	       "nl80211 width 80 drift");
_Static_assert((int)NL80211_CHAN_WIDTH_80P80 == (int)HI5622_DFSB_WIDTH_80P80,
	       "nl80211 width 80P80 drift (6.18 value is 4)");
_Static_assert((int)NL80211_CHAN_WIDTH_160 == (int)HI5622_DFSB_WIDTH_160,
	       "nl80211 width 160 drift (6.18 value is 5)");
_Static_assert((int)NL80211_CHAN_WIDTH_320 == (int)HI5622_DFSB_WIDTH_320,
	       "nl80211 width 320 drift (6.18 value is 13)");

unsigned int hi5622_ht_bw;
module_param_named(ht_bw, hi5622_ht_bw, uint, 0444);
MODULE_PARM_DESC(ht_bw,
		 "5GHz AP start: 160 MHz/80 MHz channel width selector for the H2D key 0x0124 body[1] (0=off default = legacy byte, 1=derive 20/40/80/160 MHz from the cfg80211 chandef width with the 160 MHz CCFS1 check, >1 refused)");

unsigned int hi5622_dfs_cac;
module_param_named(dfs_cac, hi5622_dfs_cac, uint, 0444);
MODULE_PARM_DESC(dfs_cac,
		 "5GHz DFS: cfg80211 start_radar_detection/end_cac CAC runtime + the start_ap DFS gate (0=off default: no op, no frame; 1=on: fail-closed live precheck, H2D key 0x07E7 ARM/CLEAR, CAC 60000 ms / 600000 ms in the 5600-5650 MHz TDWR window, NOL 1800000 ms, no DFS AP start without a finished CAC; >1 refused; mutually exclusive with dfs_set)");

#define HI5622_DFSB_WIRE_MAX	64u

static void hi5622_dfsb_k_work(struct work_struct *work);

struct hi5622_dfsb_k_rt {
	struct mutex		lock;
	struct hi5622_pci	*ctx;
	struct hi5622_dfsb_cac	model;
	struct delayed_work	work;
	struct device		*pdev;		/* logging only */
	struct net_device	*dev;		/* the CAC wdev's netdev */
	struct cfg80211_chan_def chandef;
	unsigned int		link_id;
	bool			requested;
	bool			initialized;
	int			last_rc;
	unsigned int		arms;
	unsigned int		finishes;
	unsigned int		aborts;
	unsigned int		refusals;
};

static struct hi5622_dfsb_k_rt dfsb_k;

static u32 hi5622_dfsb_k_now_ms(void)
{
	return (u32)jiffies_to_msecs(jiffies);
}

static int hi5622_dfsb_k_post(struct hi5622_pci *ctx, u8 body,
			      const char *name)
{
	u8 wire[HI5622_DFSB_WIRE_MAX];
	size_t len = 0;
	int rc;

	rc = hi5622_dfsb_set_build(wire, sizeof(wire), &len, body);
	if (rc)
		return rc;
	return hi5622_frame_post(ctx, wire, len, name, NULL);
}

void hi5622_dfsb_k_init(void)
{
	if (dfsb_k.initialized)
		return;
	memset(&dfsb_k, 0, sizeof(dfsb_k));
	mutex_init(&dfsb_k.lock);
	hi5622_dfsb_cac_abort(&dfsb_k.model);
	INIT_DELAYED_WORK(&dfsb_k.work, hi5622_dfsb_k_work);
	dfsb_k.last_rc = -ENODATA;
	dfsb_k.initialized = true;
}

void hi5622_dfsb_k_exit(void)
{
	if (!dfsb_k.initialized)
		return;
	cancel_delayed_work_sync(&dfsb_k.work);
	mutex_lock(&dfsb_k.lock);
	hi5622_dfsb_cac_abort(&dfsb_k.model);
	dfsb_k.requested = false;
	dfsb_k.dev = NULL;
	dfsb_k.ctx = NULL;
	mutex_unlock(&dfsb_k.lock);
	dfsb_k.initialized = false;
}

/* The vendor CAC tick work: REARM keeps the 0x07E7 ARM state and schedules
 * the next 60000 ms tick; FINISH posts the 0x07E7 CLEAR and reports
 * NL80211_RADAR_CAC_FINISHED to cfg80211.  A failed CLEAR aborts and reports
 * ABORTED instead - a CAC that cannot be disarmed is not a passed CAC. */
static void hi5622_dfsb_k_work(struct work_struct *work)
{
	struct hi5622_dfsb_k_rt *r = container_of(to_delayed_work(work),
						  struct hi5622_dfsb_k_rt,
						  work);
	enum hi5622_dfsb_cac_step step = HI5622_DFSB_STEP_REARM;
	struct net_device *dev = NULL;
	struct cfg80211_chan_def chandef;
	unsigned int link_id = 0;
	u32 delay;
	int rc;

	mutex_lock(&r->lock);
	if (r->model.state != HI5622_DFSB_CAC_ARMED) {
		mutex_unlock(&r->lock);
		return;
	}
	rc = hi5622_dfsb_cac_tick(&r->model, hi5622_dfsb_k_now_ms(), &step);
	if (rc) {
		r->last_rc = rc;
		hi5622_dfsb_cac_abort(&r->model);
		r->requested = false;
		mutex_unlock(&r->lock);
		return;
	}
	if (step == HI5622_DFSB_STEP_REARM) {
		delay = hi5622_dfsb_cac_delay_ms(&r->model);
		dev_info(r->pdev,
			 "cfg80211: dfs_cac tick %u/%u on ch%u (next in %u ms)\n",
			 r->model.ticks_done, r->model.ticks,
			 r->model.channel, delay);
		if (delay)
			schedule_delayed_work(&r->work,
					      msecs_to_jiffies(delay));
		mutex_unlock(&r->lock);
		return;
	}
	dev = r->dev;
	chandef = r->chandef;
	link_id = r->link_id;
	rc = hi5622_dfsb_k_post(r->ctx, HI5622_DFSB_SET_BODY_CLEAR,
				"dfs_cac_clear");
	r->last_rc = rc;
	if (rc) {
		r->aborts++;
		hi5622_dfsb_cac_abort(&r->model);
		dev_warn(r->pdev,
			 "cfg80211: dfs_cac CLEAR not posted (%d) on ch%u - reporting ABORTED\n",
			 rc, chandef.chan ? chandef.chan->hw_value : 0u);
	} else {
		r->finishes++;
		dev_info(r->pdev,
			 "cfg80211: dfs_cac CAC FINISHED on ch%u (cac_ms=%u, %u tick(s)) - 0x07E7{00} CLEAR posted; reporting NL80211_RADAR_CAC_FINISHED\n",
			 chandef.chan ? chandef.chan->hw_value : 0u,
			 r->model.cac_ms, r->model.ticks_done);
	}
	r->requested = false;
	r->dev = NULL;
	mutex_unlock(&r->lock);
	if (dev)
		cfg80211_cac_event(dev, &chandef,
				   rc ? NL80211_RADAR_CAC_ABORTED :
					NL80211_RADAR_CAC_FINISHED,
				   GFP_KERNEL, link_id);
}

int hi5622_dfsb_k_op_start_radar_detection(struct wiphy *wiphy,
					   struct net_device *dev,
					   struct cfg80211_chan_def *chandef,
					   u32 cac_time_ms, int link_id)
{
	struct hi5622_cfg80211 *c;
	struct hi5622_dfs_set_probe probe;
	struct device *pdev;
	u8 channel;
	int rc;

	if (!wiphy || !dev || !chandef || !chandef->chan)
		return -EINVAL;
	if (hi5622_dfs_cac != HI5622_DFSB_PARM_ON)
		return -EOPNOTSUPP;
	c = wiphy_priv(wiphy);
	if (!c || !c->ctx)
		return -ENODEV;
	if (!dfsb_k.initialized)
		return -ENODEV;
	pdev = &c->ctx->pdev->dev;
	if (!dev->ieee80211_ptr ||
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) {
		dev_warn(pdev,
			 "cfg80211: dfs_cac REFUSED - start_radar_detection is only supported on an AP interface\n");
		dfsb_k.refusals++;
		return -EINVAL;
	}
	if (chandef->chan->band != NL80211_BAND_5GHZ) {
		dev_warn(pdev,
			 "cfg80211: dfs_cac REFUSED - channel %u is not a 5 GHz channel\n",
			 chandef->chan->hw_value);
		dfsb_k.refusals++;
		return -EINVAL;
	}
	channel = (u8)chandef->chan->hw_value;
	mutex_lock(&dfsb_k.lock);
	if (dfsb_k.requested ||
	    dfsb_k.model.state == HI5622_DFSB_CAC_ARMED) {
		dfsb_k.refusals++;
		dev_warn(pdev,
			 "cfg80211: dfs_cac REFUSED - a CAC is already running on ch%u\n",
			 dfsb_k.model.channel);
		mutex_unlock(&dfsb_k.lock);
		return -EBUSY;
	}
	dfsb_k.ctx = c->ctx;
	dfsb_k.pdev = pdev;
	rc = hi5622_dfsb_cac_begin(&dfsb_k.model, channel,
				   chandef->chan->center_freq, cac_time_ms);
	if (rc) {
		dfsb_k.refusals++;
		dfsb_k.last_rc = rc;
		dev_warn(pdev,
			 "cfg80211: dfs_cac REFUSED - CAC on ch%u cannot start (%d): DFS channel=%d\n",
			 channel, rc, hi5622_dfsb_is_dfs_chan(channel));
		mutex_unlock(&dfsb_k.lock);
		return rc == -EALREADY ? -EBUSY :
		       rc == -EOPNOTSUPP ? -EINVAL : rc;
	}
	/* The live firmware precondition: the SAME fail-closed pack the ladder
	 * dfs_set step uses.  The 0x07E7 worker needs a started module context
	 * (the 0x0065 VAP start writes mctx[0x0E]) and the band-1 helper; if
	 * they are absent the frame would be silently dropped, so refuse with
	 * the pack in dmesg instead. */
	hi5622_scan_dfs_set_probe(c->ctx, &probe);
	rc = hi5622_dfs_set_precheck(&probe, HI5622_DFS_SET_MODE_ARM);
	if (rc) {
		dfsb_k.refusals++;
		dfsb_k.last_rc = rc;
		hi5622_dfsb_cac_abort(&dfsb_k.model);
		dev_warn(pdev,
			 "cfg80211: dfs_cac ARM REFUSED (%d): %s - live pack mctx=0x%08x band=%u state=%u ec8=%u cac=%u ctx_present=%u b4a0=0x%08x regs=0x%02x (the firmware ARM worker needs the 0x0065 VAP start's started module context; no frame posted)\n",
			 rc,
			 hi5622_dfs_set_verdict(&probe, HI5622_DFS_SET_MODE_ARM),
			 probe.mctx, probe.band, probe.state, probe.ec8,
			 probe.cac, probe.ctx_present, probe.b4a0,
			 probe.regs_pattern);
		mutex_unlock(&dfsb_k.lock);
		return -EAGAIN;
	}
	rc = hi5622_dfsb_k_post(c->ctx, HI5622_DFSB_SET_BODY_ARM,
				"dfs_cac_arm");
	dfsb_k.last_rc = rc;
	if (rc) {
		dfsb_k.refusals++;
		hi5622_dfsb_cac_abort(&dfsb_k.model);
		dev_warn(pdev,
			 "cfg80211: dfs_cac ARM frame not posted (%d) - no CAC started\n",
			 rc);
		mutex_unlock(&dfsb_k.lock);
		return rc;
	}
	dfsb_k.requested = true;
	dfsb_k.dev = dev;
	dfsb_k.chandef = *chandef;
	dfsb_k.link_id = (unsigned int)link_id;
	dfsb_k.arms++;
	schedule_delayed_work(&dfsb_k.work,
			      msecs_to_jiffies(
				      hi5622_dfsb_cac_delay_ms(&dfsb_k.model)));
	dev_info(pdev,
		 "cfg80211: dfs_cac CAC STARTED on ch%u (%u MHz): 0x07E7{01} ARM posted, cac_ms=%u (tdwr=%u), ticks=%u x %u ms after the %u ms first timer, NOL %u ms - CAC_FINISHED follows when the host timer completes (cfg80211 core emits CAC_STARTED itself)\n",
		 dfsb_k.model.channel, dfsb_k.model.center_freq,
		 dfsb_k.model.cac_ms, dfsb_k.model.tdwr,
		 dfsb_k.model.ticks, dfsb_k.model.tick_ms,
		 dfsb_k.model.first_ms, dfsb_k.model.nol_ms);
	mutex_unlock(&dfsb_k.lock);
	return 0;
}

void hi5622_dfsb_k_op_end_cac(struct wiphy *wiphy, struct net_device *dev,
			      unsigned int link_id)
{
	struct hi5622_cfg80211 *c;
	struct device *pdev;
	int rc;

	(void)dev;
	(void)link_id;
	if (!wiphy || !dfsb_k.initialized)
		return;
	c = wiphy_priv(wiphy);
	if (!c || !c->ctx)
		return;
	pdev = &c->ctx->pdev->dev;
	/* Cancel OUTSIDE the lock: the work item takes the same lock. */
	cancel_delayed_work_sync(&dfsb_k.work);
	mutex_lock(&dfsb_k.lock);
	if (dfsb_k.model.state == HI5622_DFSB_CAC_ARMED) {
		rc = hi5622_dfsb_k_post(c->ctx, HI5622_DFSB_SET_BODY_CLEAR,
					"dfs_cac_clear");
		dfsb_k.last_rc = rc;
		hi5622_dfsb_cac_abort(&dfsb_k.model);
		dfsb_k.aborts++;
		dev_info(pdev,
			 "cfg80211: dfs_cac CAC ABORTED on end_cac: 0x07E7{00} CLEAR %s (%d); cfg80211 emits NL80211_RADAR_CAC_ABORTED\n",
			 rc ? "post failed" : "posted", rc);
	}
	dfsb_k.requested = false;
	dfsb_k.dev = NULL;
	mutex_unlock(&dfsb_k.lock);
}

int hi5622_dfsb_k_ap_plan(u8 band, u8 channel, u32 width, u32 ccfs1,
			  u32 ccfs2, unsigned int dfs_set_on,
			  struct hi5622_dfsb_ap_plan *out)
{
	int rc;

	if (!out || !dfsb_k.initialized)
		return -EINVAL;
	if (hi5622_ht_bw > HI5622_DFSB_PARM_MAX ||
	    hi5622_dfs_cac > HI5622_DFSB_PARM_MAX)
		return -EINVAL;
	mutex_lock(&dfsb_k.lock);
	rc = hi5622_dfsb_ap_plan(hi5622_ht_bw, hi5622_dfs_cac, dfs_set_on,
				 band, channel, width, ccfs1, ccfs2,
				 &dfsb_k.model, hi5622_dfsb_k_now_ms(), out);
	if (rc)
		dfsb_k.refusals++;
	mutex_unlock(&dfsb_k.lock);
	return rc;
}
