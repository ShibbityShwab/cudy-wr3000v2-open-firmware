// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_dfs160.c - 5 GHz 160 MHz width selector + DFS/CAC/NOL model
 * (lane dfs160 / shared task task-240, 2026-11-26).  Clean-room, portable
 * C99: the same file compiles in the 6.18.60 kernel module and in the host
 * selftest.  Constants are decoded protocol facts (see the header and
 * ../wifi-5g-160-dfs/TABLES.md); no vendor source/object/firmware bytes.
 */
#include "hi5622_dfs160.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <string.h>
#endif

/* ===================================================================
 * 1. 160 MHz width selector
 * =================================================================== */

int hi5622_dfs160_width_selector(u32 width, u8 *sel)
{
	u8 out;

	switch (width) {
	case HI5622_DFS160_WIDTH_20NOHT:
	case HI5622_DFS160_WIDTH_20:
		out = HI5622_DFS160_BW_20;
		break;
	case HI5622_DFS160_WIDTH_40:
		out = HI5622_DFS160_BW_40;
		break;
	case HI5622_DFS160_WIDTH_80:
		out = HI5622_DFS160_BW_80;
		break;
	case HI5622_DFS160_WIDTH_160:
		out = HI5622_DFS160_BW_160;
		break;
	case HI5622_DFS160_WIDTH_80P80:
	case HI5622_DFS160_WIDTH_320:
		/* The vendor wiphy VHT cap 0x00400064 has the 160 MHz bit but
		 * neither the 160+80+80 nor the 320 MHz bit: refuse instead of
		 * pretending a width the firmware cannot select. */
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
	if (!sel)
		return -EINVAL;
	*sel = out;
	return 0;
}

int hi5622_dfs160_ccfs1_160(u8 channel, u32 *ccfs1)
{
	u32 out;

	if (channel >= 36u && channel <= 64u)
		out = 50u;		/* 36-64 @ 5250 MHz */
	else if (channel >= 100u && channel <= 128u)
		out = 114u;		/* 100-128 @ 5570 MHz */
	else if (channel >= 149u && channel <= 177u)
		out = 163u;		/* 149-177 @ 5815 MHz */
	else
		return -EOPNOTSUPP;
	if (!ccfs1)
		return -EINVAL;
	*ccfs1 = out;
	return 0;
}

int hi5622_dfs160_ccfs1_80(u8 channel, u32 *ccfs1)
{
	u32 out;

	if (channel >= 36u && channel <= 48u)
		out = 42u;
	else if (channel >= 52u && channel <= 64u)
		out = 58u;
	else if (channel >= 100u && channel <= 112u)
		out = 106u;
	else if (channel >= 116u && channel <= 128u)
		out = 122u;
	else if (channel >= 132u && channel <= 144u)
		out = 138u;
	else if (channel >= 149u && channel <= 161u)
		out = 155u;
	else if (channel >= 165u && channel <= 177u)
		out = 171u;
	else
		return -EOPNOTSUPP;
	if (!ccfs1)
		return -EINVAL;
	*ccfs1 = out;
	return 0;
}

int hi5622_dfs160_width_check(u32 width, u8 channel, u32 ccfs1, u32 ccfs2)
{
	u8 sel = HI5622_DFS160_BW_LEGACY;
	u32 want = 0u;
	int rc;

	if (!channel)
		return -EINVAL;
	rc = hi5622_dfs160_width_selector(width, &sel);
	if (rc)
		return rc;	/* 80+80/320 are refused before any centre logic */
	if (ccfs2)
		return -EINVAL;	/* no 80+80 support: CCFS2 must be 0 */
	switch (sel) {
	case HI5622_DFS160_BW_20:
		break;		/* no centre required */
	case HI5622_DFS160_BW_40:
		/* 40 MHz CCFS1 is optional in cfg80211; accept 0 or the
		 * derived centre when the channel is in a 40 MHz segment.
		 * The exact 40 MHz centre check is left to cfg80211. */
		break;
	case HI5622_DFS160_BW_80:
		rc = hi5622_dfs160_ccfs1_80(channel, &want);
		if (rc)
			return rc;
		if (ccfs1 && ccfs1 != want)
			return -EINVAL;
		break;
	case HI5622_DFS160_BW_160:
		rc = hi5622_dfs160_ccfs1_160(channel, &want);
		if (rc)
			return rc;
		if (ccfs1 != want)
			return -EINVAL;
		break;
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

/* ===================================================================
 * 2. DFS channel set / hours
 * =================================================================== */

int hi5622_dfs160_chan_freq(u8 channel, u16 *mhz)
{
	u16 f;

	if (!channel)
		return -EINVAL;
	if (channel == 14u)
		f = 2484u;
	else if (channel >= 1u && channel <= 13u)
		f = (u16)(2407u + 5u * channel);
	else if (channel == 144u)
		f = 5720u;
	else if (channel >= 184u && channel <= 196u)
		f = (u16)(4000u + 5u * channel);	/* 4920..4980 */
	else if ((channel >= 36u && channel <= 64u) ||
		 (channel >= 100u && channel <= 140u) ||
		 (channel >= 149u && channel <= 165u))
		f = (u16)(5000u + 5u * channel);
	else
		return -EINVAL;
	if (mhz)
		*mhz = f;
	return 0;
}

int hi5622_dfs160_is_dfs_chan(u8 channel)
{
	/* The vendor band-1 table CA 0x117AD4 DFS channels: 52-64 and 100-144.
	 * ch144 is the "ch144 gap" of our current wiphy list (hi5622_cfg80211.c
	 * advertises 52-64/100-140 only) and is restored by this lane. */
	return (channel >= 52u && channel <= 64u) ||
	       (channel >= 100u && channel <= 144u);
}

int hi5622_dfs160_is_tdwr_chan(u8 channel)
{
	return channel >= 100u && channel <= 128u;
}

int hi5622_dfs160_is_tdwr_center(u16 center_freq)
{
	return center_freq >= 5600u && center_freq <= 5650u;
}

/* ===================================================================
 * 3. CAC / NOL timing and state machine
 * =================================================================== */

int hi5622_dfs160_cac_plan(u8 channel, u16 center_freq, u32 requested_ms,
			   struct hi5622_dfs160_cac_plan *out)
{
	u32 cac_ms;

	if (!out || !channel)
		return -EINVAL;
	if (!hi5622_dfs160_is_dfs_chan(channel))
		return -EOPNOTSUPP;
	if (!center_freq) {
		u16 f;

		if (hi5622_dfs160_chan_freq(channel, &f))
			return -EINVAL;
		center_freq = f;
	}
	memset(out, 0, sizeof(*out));
	out->channel = channel;
	if (hi5622_dfs160_is_tdwr_center(center_freq) ||
	    hi5622_dfs160_is_tdwr_chan(channel))
		out->tdwr = 1u;
	cac_ms = out->tdwr ? HI5622_DFS160_CAC_MS_TDWR
			   : HI5622_DFS160_CAC_MS_DEFAULT;
	if (requested_ms > cac_ms)
		cac_ms = requested_ms;	/* hostapd may ask for longer */
	out->cac_ms = cac_ms;
	/* The vendor tick arithmetic (hmac_dfs_cac_start): CAC > 59999 ms
	 * uses a 63000 ms first timer and max = cac_ms/60000 ticks; the
	 * short path registers one 60000 ms timer.  We always take the
	 * 63000 ms first timer so the cfg80211 CAC_FINISHED jiffies check
	 * (start_time + cac_time_ms) can never fire early. */
	out->first_ms = HI5622_DFS160_CAC_FIRST_MS;
	out->tick_ms = HI5622_DFS160_CAC_TICK_MS;
	out->ticks = cac_ms > 59999u ? cac_ms / 60000u : 1u;
	if (!out->ticks)
		out->ticks = 1u;
	out->nol_ms = HI5622_DFS160_NOL_MS;
	return 0;
}

void hi5622_dfs160_cac_init(struct hi5622_dfs160_cac *st)
{
	if (!st)
		return;
	memset(st, 0, sizeof(*st));
	st->state = HI5622_DFS160_CAC_OFF;
	st->radar_type = 0xffu;
}

int hi5622_dfs160_cac_begin(struct hi5622_dfs160_cac *st, u8 channel,
			    u16 center_freq, u32 requested_ms)
{
	struct hi5622_dfs160_cac_plan plan;
	int rc;

	if (!st)
		return -EINVAL;
	if (st->state == HI5622_DFS160_CAC_ARMED)
		return -EALREADY;
	rc = hi5622_dfs160_cac_plan(channel, center_freq, requested_ms, &plan);
	if (rc)
		return rc;
	st->channel = channel;
	st->center_freq = center_freq;
	st->tdwr = plan.tdwr;
	st->cac_ms = plan.cac_ms;
	st->first_ms = plan.first_ms;
	st->tick_ms = plan.tick_ms;
	st->ticks = plan.ticks;
	st->ticks_done = 0u;
	st->nol_ms = plan.nol_ms;
	st->nol_end_ms = 0u;
	st->radar_type = 0xffu;
	st->plan = plan;
	st->state = HI5622_DFS160_CAC_ARMED;
	return 0;
}

u32 hi5622_dfs160_cac_delay_ms(const struct hi5622_dfs160_cac *st)
{
	if (!st || st->state != HI5622_DFS160_CAC_ARMED)
		return 0u;
	if (st->ticks_done >= st->ticks)
		return 0u;	/* the FINISH tick: no re-arm */
	return st->ticks_done ? st->tick_ms : st->first_ms;
}

enum hi5622_dfs160_cac_step
hi5622_dfs160_cac_tick(struct hi5622_dfs160_cac *st)
{
	if (!st || st->state != HI5622_DFS160_CAC_ARMED)
		return HI5622_DFS160_STEP_IDLE;
	st->ticks_done++;
	if (st->ticks_done >= st->ticks)
		return HI5622_DFS160_STEP_FINISH;
	return HI5622_DFS160_STEP_REARM;
}

void hi5622_dfs160_cac_finish(struct hi5622_dfs160_cac *st)
{
	if (!st)
		return;
	st->state = HI5622_DFS160_CAC_FINISHED;
}

void hi5622_dfs160_cac_abort(struct hi5622_dfs160_cac *st)
{
	if (!st)
		return;
	st->state = HI5622_DFS160_CAC_ABORTED;
	st->ticks_done = 0u;
}

int hi5622_dfs160_cac_radar(struct hi5622_dfs160_cac *st, u8 type, u32 now_ms)
{
	if (!st)
		return -EINVAL;
	if (type > 8u)
		return -EINVAL;	/* the vendor counts radar types 0..8 */
	st->radar_type = type;
	st->radar_count++;
	st->state = HI5622_DFS160_CAC_ABORTED;
	st->nol_end_ms = now_ms + st->nol_ms;
	return 0;
}

int hi5622_dfs160_nol_active(const struct hi5622_dfs160_cac *st, u32 now_ms)
{
	if (!st || !st->nol_end_ms)
		return 0;
	/* Wrapping-safe subtraction on the caller's monotonic clock. */
	return (int32_t)(st->nol_end_ms - now_ms) > 0;
}

int hi5622_dfs160_cac_ap_gate(const struct hi5622_dfs160_cac *st, u8 channel,
			      u16 center_freq, u32 now_ms)
{
	u16 want = 0u;

	if (!st || !channel)
		return -EINVAL;
	if (!hi5622_dfs160_is_dfs_chan(channel))
		return 0;	/* non-DFS channel: the gate does not apply */
	if (hi5622_dfs160_chan_freq(channel, &want) || !want)
		return -EINVAL;
	if (center_freq && center_freq != want)
		return -EINVAL;
	if (st->state != HI5622_DFS160_CAC_FINISHED)
		return -EAGAIN;
	if (st->channel != channel)
		return -EAGAIN;
	if (hi5622_dfs160_nol_active(st, now_ms))
		return -EAGAIN;
	return 0;
}

const char *hi5622_dfs160_cac_state_name(u8 state)
{
	switch (state) {
	case HI5622_DFS160_CAC_OFF:		return "off";
	case HI5622_DFS160_CAC_ARMED:		return "armed";
	case HI5622_DFS160_CAC_FINISHED:	return "finished";
	case HI5622_DFS160_CAC_ABORTED:		return "aborted";
	default:				return "?";
	}
}

int hi5622_dfs160_status_line(unsigned int ht_bw, unsigned int dfs_cac,
			      const struct hi5622_dfs160_cac *st,
			      int last_rc, unsigned int arms,
			      unsigned int finishes, unsigned int aborts,
			      unsigned int refusals, char *buf, size_t len)
{
	int n;

	if (!buf || !len)
		return -EINVAL;
	n = snprintf(buf, len,
		     "dfs_cac: ht_bw=%u (%s) dfs_cac=%u (%s) state=%s channel=%u cac_ms=%u ticks=%u/%u nol_ms=%u nol_active=%u radar=%u arms=%u finishes=%u aborts=%u refusals=%u last_rc=%d\n",
		     ht_bw, ht_bw ? "0x0124 width selector" : "off",
		     dfs_cac, dfs_cac ? "cfg80211 CAC op" : "off",
		     st ? hi5622_dfs160_cac_state_name(st->state) : "?",
		     st ? st->channel : 0u,
		     st ? st->cac_ms : 0u,
		     st ? st->ticks_done : 0u, st ? st->ticks : 0u,
		     st ? st->nol_ms : 0u,
		     st ? (unsigned int)(st->nol_end_ms ? 1u : 0u) : 0u,
		     st ? st->radar_type : 0xffu,
		     arms, finishes, aborts, refusals, last_rc);
	if (n < 0)
		return -EINVAL;
	return (size_t)n < len ? n : (int)(len - 1u);
}
