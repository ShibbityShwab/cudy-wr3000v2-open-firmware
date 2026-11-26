// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_dfs_cac.c - the kernel-side DFS/CAC runtime for the Hi5622V100
 * clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane dfs160 / shared task task-240, 2026-11-26).
 * Wire facts: the H2D g0-id1 key 0x07E7 body {01}=ARM / {00}=CLEAR and the
 * firmware worker CA 0xB58FC are the committed decode of
 * ../wifi-5g-dfs-set (TABLES) and ../wifi-5g-cac-context; the CAC/NOL
 * timings are the committed decode of ../wifi-5g-160-dfs/TABLES T4/T5.  No
 * vendor source, object code or firmware byte is reproduced.
 *
 * The cfg80211 contract (net/wireless/nl80211.c + mlme.c, 6.18):
 *   - start_radar_detection(...) returning 0 makes cfg80211 mark the wdev
 *     CAC as started; the driver must later report
 *     cfg80211_cac_event(NL80211_RADAR_CAC_FINISHED) (or ABORTED);
 *   - cfg80211_cac_event(FINISHED) WARNs when it arrives before
 *     start_time + cac_time_ms, so the first timer is 63000 ms (the vendor
 *     hmac_dfs_cac_start timer is 63000 ms too) and the re-arm tick is
 *     60000 ms: 60000 ms -> 63 s, 600000 ms -> 63 s + 9 x 60 s = 603 s;
 *   - end_cac(..) is called by cfg80211 for an abort/teardown; the driver
 *     only stops its hardware state (the core emits NL80211_RADAR_CAC_ABORTED).
 *
 * The 0x07E7 ARM is posted only after the SAME live precheck the AP-start
 * dfs_set step uses (hi5622_dfs_set_precheck on the hi5622_scan_dfs_set_probe
 * pack): the firmware ARM worker's started-module gate needs the 0x0065 VAP
 * start, so on a fresh interface the op fails CLOSED with -EAGAIN and the
 * probe pack in dmesg - the bench learns exactly which prerequisite is
 * missing instead of the driver posting a frame the worker would drop.
 */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211.h"
#include "hi5622_dfs_cac.h"
#include "hi5622_dfs160.h"
#include "hi5622_dfs_set.h"
#include "hi5622_driver.h"
#include "hi5622_frame_post.h"
#include "hi5622_scan.h"

unsigned int hi5622_dfs_cac;
module_param_named(dfs_cac, hi5622_dfs_cac, uint, 0444);
MODULE_PARM_DESC(dfs_cac,
		 "5 GHz DFS: cfg80211 start_radar_detection/end_cac CAC runtime (0=off default = no op, no frame, no AP-start DFS gate; 1=on: probe the live 0x0065/started-module prerequisite, post the H2D key 0x07E7 ARM, run the CAC 60000 ms (600000 ms in the 5600-5650 MHz TDWR window) host timer, report CAC_FINISHED, keep a DFS AP from beaconing before the CAC finished; >1 refused)");

#define HI5622_DFS_CAC_WIRE_MAX	64u

static void hi5622_dfs_cac_work(struct work_struct *work);

static struct device *hi5622_dfs_cac_dev(struct hi5622_dfs_cac *r)
{
	return &r->ctx->pdev->dev;
}

static int hi5622_dfs_cac_post(struct hi5622_dfs_cac *r, u8 body,
			       const char *name)
{
	u8 wire[HI5622_DFS_CAC_WIRE_MAX];
	size_t len = 0;
	int rc;

	rc = hi5622_dfs_set_build(wire, sizeof(wire), &len, body);
	if (rc)
		return rc;
	return hi5622_frame_post(r->ctx, wire, len, name, NULL);
}

/* The timer callback: one vendor CAC tick.  REARM schedules the next tick;
 * FINISH posts 0x07E7{00} and (only for a cfg80211-owned CAC) reports
 * NL80211_RADAR_CAC_FINISHED. */
static void hi5622_dfs_cac_work(struct work_struct *work)
{
	struct hi5622_dfs_cac *r = container_of(to_delayed_work(work),
						struct hi5622_dfs_cac, work);
	struct device *dev = hi5622_dfs_cac_dev(r);
	struct hi5622_dfs160_cac *st = &r->model;
	enum hi5622_dfs160_cac_step step;
	u32 delay;
	int rc;

	mutex_lock(&r->lock);
	if (st->state != HI5622_DFS160_CAC_ARMED) {
		mutex_unlock(&r->lock);
		return;
	}
	step = hi5622_dfs160_cac_tick(st);
	if (step == HI5622_DFS160_STEP_REARM) {
		delay = hi5622_dfs160_cac_delay_ms(st);

		dev_info(dev,
			 "cfg80211: dfs_cac tick %u/%u on ch%u (next in %u ms)\n",
			 st->ticks_done, st->ticks, st->channel, delay);
		if (delay)
			schedule_delayed_work(&r->work,
					      msecs_to_jiffies(delay));
		mutex_unlock(&r->lock);
		return;
	}
	/* CAC complete: disarm the firmware radar detector, then report. */
	rc = hi5622_dfs_cac_post(r, HI5622_DFS_SET_BODY_CLEAR, "dfs_cac_clear");
	r->last_rc = rc ? (unsigned int)-rc : 0u;
	if (rc) {
		r->aborts++;
		dev_warn(dev,
			 "cfg80211: dfs_cac CLEAR not posted (%d) on ch%u - CAC state kept, reporting ABORTED\n",
			 rc, st->channel);
		hi5622_dfs160_cac_abort(st);
	} else {
		hi5622_dfs160_cac_finish(st);
		r->finishes++;
		dev_info(dev,
			 "cfg80211: dfs_cac CAC FINISHED on ch%u (cac_ms=%u, %u tick(s)) - 0x07E7{00} CLEAR posted\n",
			 st->channel, st->cac_ms, st->ticks);
		if (r->requested && r->dev)
			cfg80211_cac_event(r->dev, &r->chandef,
					   NL80211_RADAR_CAC_FINISHED,
					   GFP_KERNEL, r->link_id);
	}
	r->requested = 0u;
	r->dev = NULL;
	mutex_unlock(&r->lock);
}

struct hi5622_dfs_cac *hi5622_dfs_cac_create(struct hi5622_pci *ctx)
{
	struct hi5622_dfs_cac *r;

	if (!ctx)
		return NULL;
	r = kzalloc(sizeof(*r), GFP_KERNEL);
	if (!r)
		return NULL;
	r->ctx = ctx;
	r->last_rc = (unsigned int)-ENODATA;
	mutex_init(&r->lock);
	hi5622_dfs160_cac_init(&r->model);
	INIT_DELAYED_WORK(&r->work, hi5622_dfs_cac_work);
	return r;
}

void hi5622_dfs_cac_destroy(struct hi5622_dfs_cac *r)
{
	if (!r)
		return;
	cancel_delayed_work_sync(&r->work);
	mutex_destroy(&r->lock);
	kfree(r);
}

int hi5622_dfs_cac_op_start(struct wiphy *wiphy, struct net_device *dev,
			    struct cfg80211_chan_def *chandef,
			    u32 cac_time_ms, int link_id)
{
	struct hi5622_cfg80211 *c;
	struct hi5622_dfs_cac *r;
	struct hi5622_dfs_set_probe probe;
	struct device *pdev;
	u8 channel;
	int rc;

	if (!wiphy || !dev)
		return -EINVAL;
	c = wiphy_priv(wiphy);
	if (!c || !c->dfs_cac)
		return -EOPNOTSUPP;
	r = c->dfs_cac;
	if (hi5622_dfs_cac != 1u)
		return -EOPNOTSUPP;	/* the default: no DFS service */
	pdev = hi5622_dfs_cac_dev(r);
	if (!dev->ieee80211_ptr ||
	    dev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) {
		dev_warn(pdev,
			 "cfg80211: dfs_cac REFUSED - start_radar_detection is only supported on an AP interface\n");
		r->refusals++;
		return -EINVAL;
	}
	if (!chandef || !chandef->chan)
		return -EINVAL;
	if (chandef->chan->band != NL80211_BAND_5GHZ) {
		dev_warn(pdev,
			 "cfg80211: dfs_cac REFUSED - channel %u is not a 5 GHz channel\n",
			 chandef->chan->hw_value);
		r->refusals++;
		return -EINVAL;
	}
	channel = (u8)chandef->chan->hw_value;
	mutex_lock(&r->lock);
	rc = hi5622_dfs160_cac_begin(&r->model, channel,
				     chandef->chan->center_freq, cac_time_ms);
	if (rc) {
		r->refusals++;
		r->last_rc = (unsigned int)-rc;
		dev_warn(pdev,
			 "cfg80211: dfs_cac REFUSED - CAC on ch%u cannot start (%d): %s (already armed=%d, DFS channel=%d)\n",
			 channel, rc,
			 rc == -EALREADY ? "a CAC is already running on this module load" :
			 rc == -EOPNOTSUPP ? "not a DFS channel" : "bad parameter",
			 r->model.state == HI5622_DFS160_CAC_ARMED,
			 hi5622_dfs160_is_dfs_chan(channel));
		mutex_unlock(&r->lock);
		return rc == -EALREADY ? -EBUSY : -EINVAL;
	}
	/* The live firmware precondition: the SAME fail-closed pack the
	 * AP-start dfs_set step uses.  The 0x07E7 worker needs a started
	 * module context (the 0x0065 VAP start writes mctx[0x0E]=1) and the
	 * band-1 CAC context; without them the frame would be silently
	 * dropped, so refuse and print the pack. */
	hi5622_scan_dfs_set_probe(c->ctx, &probe);
	rc = hi5622_dfs_set_precheck(&probe, HI5622_DFS_SET_MODE_ARM);
	if (rc) {
		r->refusals++;
		r->last_rc = (unsigned int)-rc;
		hi5622_dfs160_cac_abort(&r->model);
		dev_warn(pdev,
			 "cfg80211: dfs_cac ARM REFUSED (%d): %s - live pack mctx=0x%08x band=%u state=%u ec8=%u cac=%u ctx_present=%u b4a0=0x%08x regs=0x%02x (the firmware ARM worker needs the 0x0065 VAP start's started module context; no frame posted)\n",
			 rc, hi5622_dfs_set_verdict(&probe,
						    HI5622_DFS_SET_MODE_ARM),
			 probe.mctx, probe.band, probe.state, probe.ec8,
			 probe.cac, probe.ctx_present, probe.b4a0,
			 probe.regs_pattern);
		mutex_unlock(&r->lock);
		return -EAGAIN;
	}
	rc = hi5622_dfs_cac_post(r, HI5622_DFS_SET_BODY_ARM, "dfs_cac_arm");
	r->last_rc = rc ? (unsigned int)-rc : 0u;
	if (rc) {
		r->refusals++;
		hi5622_dfs160_cac_abort(&r->model);
		dev_warn(pdev,
			 "cfg80211: dfs_cac ARM frame not posted (%d) - no CAC started\n",
			 rc);
		mutex_unlock(&r->lock);
		return rc;
	}
	r->dev = dev;
	r->chandef = *chandef;
	r->link_id = (unsigned int)link_id;
	r->requested = 1u;
	r->arms++;
	schedule_delayed_work(&r->work,
			      msecs_to_jiffies(
				      hi5622_dfs160_cac_delay_ms(&r->model)));
	dev_info(pdev,
		 "cfg80211: dfs_cac CAC STARTED on ch%u (%u MHz): 0x07E7{01} ARM posted, cac_ms=%u (tdwr=%u), ticks=%u x %u ms after the %u ms first timer, NOL %u ms - cfg80211 will get CAC_FINISHED after the CAC\n",
		 r->model.channel, r->model.center_freq, r->model.cac_ms,
		 r->model.tdwr, r->model.ticks, r->model.tick_ms,
		 r->model.first_ms, r->model.nol_ms);
	mutex_unlock(&r->lock);
	cfg80211_cac_event(dev, chandef, NL80211_RADAR_CAC_STARTED,
			   GFP_KERNEL, r->link_id);
	return 0;
}

void hi5622_dfs_cac_op_end(struct wiphy *wiphy, struct net_device *dev,
			   unsigned int link_id)
{
	struct hi5622_cfg80211 *c;
	struct hi5622_dfs_cac *r;
	struct device *pdev;
	int rc;

	(void)dev;
	(void)link_id;
	if (!wiphy)
		return;
	c = wiphy_priv(wiphy);
	if (!c || !c->dfs_cac)
		return;
	r = c->dfs_cac;
	pdev = hi5622_dfs_cac_dev(r);
	/* Cancel OUTSIDE the lock: the work item takes the same lock. */
	cancel_delayed_work_sync(&r->work);
	mutex_lock(&r->lock);
	if (r->model.state == HI5622_DFS160_CAC_ARMED) {
		rc = hi5622_dfs_cac_post(r, HI5622_DFS_SET_BODY_CLEAR,
					 "dfs_cac_clear");
		r->last_rc = rc ? (unsigned int)-rc : 0u;
		hi5622_dfs160_cac_abort(&r->model);
		r->aborts++;
		dev_info(pdev,
			 "cfg80211: dfs_cac CAC ABORTED on ch%u (end_cac): 0x07E7{00} CLEAR %s (%d)\n",
			 r->model.channel, rc ? "post failed" : "posted", rc);
	}
	r->requested = 0u;
	r->dev = NULL;
	mutex_unlock(&r->lock);
}

int hi5622_dfs_cac_ap_gate(struct hi5622_cfg80211 *c, u8 channel,
			   u16 center_freq)
{
	struct hi5622_dfs_cac *r;
	u32 now_ms;
	int rc;

	if (!c || !channel)
		return -EINVAL;
	if (hi5622_dfs_cac != 1u)
		return 0;	/* feature off: the pre-feature AP path */
	r = c->dfs_cac;
	if (!r)
		return -EIO;
	now_ms = (u32)jiffies_to_msecs(jiffies);
	mutex_lock(&r->lock);
	rc = hi5622_dfs160_cac_ap_gate(&r->model, channel, center_freq, now_ms);
	mutex_unlock(&r->lock);
	return rc;
}

int hi5622_dfs_cac_status_line(struct hi5622_cfg80211 *c, unsigned int ht_bw,
			       char *buf, size_t len)
{
	struct hi5622_dfs_cac *r;
	int rc;

	if (!c || !buf || !len)
		return -EINVAL;
	if (hi5622_dfs_cac != 1u) {
		r = c->dfs_cac;
		mutex_lock(&c->lock);
		rc = hi5622_dfs160_status_line(ht_bw, 0u, r ? &r->model : NULL,
					       0, 0u, 0u, 0u, 0u, buf, len);
		mutex_unlock(&c->lock);
		return rc;
	}
	r = c->dfs_cac;
	if (!r)
		return -EIO;
	mutex_lock(&c->lock);
	rc = hi5622_dfs160_status_line(ht_bw, 1u, &r->model,
				       (int)r->last_rc, r->arms, r->finishes,
				       r->aborts, r->refusals, buf, len);
	mutex_unlock(&c->lock);
	return rc;
}