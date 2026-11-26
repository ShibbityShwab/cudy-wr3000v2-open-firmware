/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_dfsb.c - the portable 160 MHz width selector + DFS/CAC/NOL model
 * for the Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, 6.18.60).
 *
 * ORIGINAL clean-room code (lane dfs160b / shared task task-245).  The
 * protocol facts are the committed decode lanes ../wifi-5g-160-dfs,
 * ../wifi-5g-dfs-set, ../wifi-5g-dfs-order and ../wifi-5g-cac-context; no
 * vendor source, object code or firmware byte is reproduced.  This file is
 * the DECISION layer only: the kernel glue (hi5622_dfsb_k.c) owns the
 * timers, the H2D posts and the cfg80211 events.
 */
#include "hi5622_dfsb.h"

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

/* ---- the vendor band-1 channel table (29 entries, TABLES T3) [V] ----- */
struct dfsb_chan {
	u8	ch;
	u16	mhz;
};

static const struct dfsb_chan dfsb_band1[29] = {
	{ 36, 5180 }, { 40, 5200 }, { 44, 5220 }, { 48, 5240 },
	{ 52, 5260 }, { 56, 5280 }, { 60, 5300 }, { 64, 5320 },
	{ 100, 5500 }, { 104, 5520 }, { 108, 5540 }, { 112, 5560 },
	{ 116, 5580 }, { 120, 5600 }, { 124, 5620 }, { 128, 5640 },
	{ 132, 5660 }, { 136, 5680 }, { 140, 5700 }, { 144, 5720 },
	{ 149, 5745 }, { 153, 5765 }, { 157, 5785 }, { 161, 5805 },
	{ 165, 5825 }, { 184, 4920 }, { 188, 4940 }, { 192, 4960 },
	{ 196, 4980 },
};

static const struct dfsb_chan *dfsb_find_band1(u8 channel)
{
	unsigned int i;

	for (i = 0; i < sizeof(dfsb_band1) / sizeof(dfsb_band1[0]); i++)
		if (dfsb_band1[i].ch == channel)
			return &dfsb_band1[i];
	return NULL;
}

int hi5622_dfsb_chan_freq(u8 channel, u16 *mhz)
{
	const struct dfsb_chan *c;

	if (!mhz)
		return -EINVAL;
	c = dfsb_find_band1(channel);
	if (!c)
		return -EINVAL;
	*mhz = c->mhz;
	return 0;
}

/* DFS set: 52-64 and 100-144 (the vendor band-1 table includes ch144; the
 * driver list had the ch144 gap). [V, TABLES T4] */
int hi5622_dfsb_is_dfs_chan(u8 channel)
{
	return ((channel >= 52u && channel <= 64u) ||
		(channel >= 100u && channel <= 144u)) ? 1 : 0;
}

/* TDWR/weather window: the vendor computes freq - 5600 and tests <= 50,
 * i.e. 5600..5650 MHz.  In the band-1 table that is ch 120/124/128. */
int hi5622_dfsb_is_tdwr_chan(u8 channel)
{
	u16 mhz = 0;

	if (hi5622_dfsb_chan_freq(channel, &mhz))
		return 0;
	return (mhz >= 5600u && mhz <= 5650u) ? 1 : 0;
}

/* ---- 160 / 80 MHz single-segment centres (IEEE channelisation) ------- */
int hi5622_dfsb_ccfs1_160(u8 channel, u32 *ccfs1_mhz)
{
	u32 c;

	if (!ccfs1_mhz)
		return -EINVAL;
	if (channel >= 36u && channel <= 64u)
		c = 5250u;
	else if (channel >= 100u && channel <= 128u)
		c = 5570u;
	else if (channel >= 149u && channel <= 177u)
		c = 5815u;
	else
		return -EOPNOTSUPP;
	*ccfs1_mhz = c;
	return 0;
}

int hi5622_dfsb_ccfs1_80(u8 channel, u32 *ccfs1_mhz)
{
	u32 c;

	if (!ccfs1_mhz)
		return -EINVAL;
	if (channel >= 36u && channel <= 48u)
		c = 5210u;
	else if (channel >= 52u && channel <= 64u)
		c = 5290u;
	else if (channel >= 100u && channel <= 112u)
		c = 5530u;
	else if (channel >= 116u && channel <= 128u)
		c = 5610u;
	else if (channel >= 132u && channel <= 144u)
		c = 5690u;
	else if (channel >= 149u && channel <= 161u)
		c = 5775u;
	else if (channel >= 165u && channel <= 177u)
		c = 5855u;
	else
		return -EOPNOTSUPP;
	*ccfs1_mhz = c;
	return 0;
}

/* ---- 1. the 0x0124 body[1] width selector ---------------------------- */
int hi5622_dfsb_width_plan(unsigned int ht_bw, u8 band, u8 channel, u32 width,
			   u32 ccfs1, u32 ccfs2,
			   struct hi5622_dfsb_width_plan *out)
{
	u32 centre = 0;
	u8 sel;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (ht_bw > HI5622_DFSB_PARM_MAX)
		return -EINVAL;
	out->width = width <= 255u ? (u8)width : 0xffu;
	if (ht_bw == HI5622_DFSB_PARM_OFF) {
		/* The legacy byte-identical path: body[1] stays 0. */
		return 0;
	}
	if (!channel)
		return -EINVAL;
	if (band == 0u) {
		if (channel < 1u || channel > 14u)
			return -EINVAL;
	} else if (band != 1u) {
		return -EINVAL;
	} else if (!dfsb_find_band1(channel)) {
		return -EINVAL;
	}

	switch (width) {
	case HI5622_DFSB_WIDTH_20NOHT:
	case HI5622_DFSB_WIDTH_20:
		sel = HI5622_DFSB_BW_20;
		break;
	case HI5622_DFSB_WIDTH_40:
		sel = HI5622_DFSB_BW_40;
		break;
	case HI5622_DFSB_WIDTH_80: {
		int rc;

		if (band == 0u)
			return -EOPNOTSUPP;	/* 2.4 GHz has no 80 MHz */
		if (ccfs2 != 0u)
			return -EINVAL;
		rc = hi5622_dfsb_ccfs1_80(channel, &centre);
		if (rc)
			return rc;
		if (ccfs1 != centre)
			return -EINVAL;
		sel = HI5622_DFSB_BW_80;
		break;
	}
	case HI5622_DFSB_WIDTH_160: {
		int rc;

		if (band != 1u)
			return -EOPNOTSUPP;	/* 2.4 GHz has no 160 MHz */
		/* Single segment only: the vendor VHT cap has the 160 bit
		 * but not the 80+80 bit (TABLES T7), so CCFS2 must be 0. */
		if (ccfs2 != 0u)
			return -EINVAL;
		rc = hi5622_dfsb_ccfs1_160(channel, &centre);
		if (rc)
			return rc;
		if (ccfs1 != centre)
			return -EINVAL;
		sel = HI5622_DFSB_BW_160;
		break;
	}
	case HI5622_DFSB_WIDTH_80P80:
	case HI5622_DFSB_WIDTH_320:
		/* Neither capability bit exists in the vendor wiphy. */
		return -EOPNOTSUPP;
	case HI5622_DFSB_WIDTH_5:
	case HI5622_DFSB_WIDTH_10:
	case HI5622_DFSB_WIDTH_1:
	case HI5622_DFSB_WIDTH_2:
	case HI5622_DFSB_WIDTH_4:
	case HI5622_DFSB_WIDTH_8:
	case HI5622_DFSB_WIDTH_16:
		return -EOPNOTSUPP;
	default:
		return -EINVAL;
	}
	out->enabled = true;
	out->sel = sel;
	out->width = (u8)width;
	return 0;
}

/* ---- 3. the CAC / NOL state machine ---------------------------------- */
int hi5622_dfsb_cac_begin(struct hi5622_dfsb_cac *st, u8 channel,
			  u16 center_freq, u32 cac_time_ms)
{
	u16 mhz = 0;
	u32 vendor_ms;

	if (!st)
		return -EINVAL;
	if (st->state == HI5622_DFSB_CAC_ARMED)
		return -EALREADY;
	if (hi5622_dfsb_chan_freq(channel, &mhz))
		return -EINVAL;
	if (!hi5622_dfsb_is_dfs_chan(channel))
		return -EOPNOTSUPP;
	if (center_freq && center_freq != mhz)
		return -EINVAL;
	memset(st, 0, sizeof(*st));
	vendor_ms = hi5622_dfsb_is_tdwr_chan(channel) ?
		     HI5622_DFSB_CAC_MS_TDWR : HI5622_DFSB_CAC_MS;
	if (cac_time_ms > vendor_ms)
		vendor_ms = cac_time_ms;	/* never report before cfg80211's time */
	st->state = HI5622_DFSB_CAC_ARMED;
	st->channel = channel;
	st->tdwr = (u8)hi5622_dfsb_is_tdwr_chan(channel);
	st->center_freq = mhz;
	st->cac_ms = vendor_ms;
	st->ticks = vendor_ms > 59999u ? vendor_ms / 60000u : 0u;
	st->tick_ms = HI5622_DFSB_TICK_MS;
	st->first_ms = vendor_ms > 59999u ?
			HI5622_DFSB_FIRST_TICK_MS : HI5622_DFSB_TICK_MS;
	st->ticks_done = 0u;
	st->finish_ms = 0u;
	st->nol_ms = HI5622_DFSB_NOL_MS;
	return 0;
}

u32 hi5622_dfsb_cac_delay_ms(const struct hi5622_dfsb_cac *st)
{
	if (!st || st->state != HI5622_DFSB_CAC_ARMED)
		return 0u;
	return st->ticks_done == 0u ? st->first_ms : st->tick_ms;
}

int hi5622_dfsb_cac_tick(struct hi5622_dfsb_cac *st, u32 now_ms,
			 enum hi5622_dfsb_cac_step *out)
{
	u32 need;

	if (!st || !out)
		return -EINVAL;
	if (st->state != HI5622_DFSB_CAC_ARMED)
		return -EPERM;
	st->ticks_done++;
	need = st->ticks ? st->ticks : 1u;
	if (st->ticks_done < need) {
		*out = HI5622_DFSB_STEP_REARM;
		return 0;
	}
	st->state = HI5622_DFSB_CAC_FINISHED;
	st->finish_ms = now_ms;
	*out = HI5622_DFSB_STEP_FINISH;
	return 0;
}

void hi5622_dfsb_cac_abort(struct hi5622_dfsb_cac *st)
{
	if (!st)
		return;
	memset(st, 0, sizeof(*st));
}

int hi5622_dfsb_cac_ap_gate(const struct hi5622_dfsb_cac *st, u8 channel,
			    u32 now_ms)
{
	u32 elapsed;

	if (!st || !channel)
		return -EINVAL;
	if (st->state != HI5622_DFSB_CAC_FINISHED)
		return -EAGAIN;
	if (st->channel != channel)
		return -EINVAL;
	/* Wrap-safe u32 millisecond difference; the NOL window is 30 min. */
	elapsed = now_ms - st->finish_ms;
	if (elapsed >= st->nol_ms)
		return -EAGAIN;
	return 0;
}

/* ---- 4. the 0x07E7 DFS-SET frame ------------------------------------- */
static void dfsb_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

int hi5622_dfsb_set_build(u8 *wire, size_t cap, size_t *out_len, u8 body)
{
	if (!wire || !out_len)
		return -EINVAL;
	if (body != HI5622_DFSB_SET_BODY_ARM &&
	    body != HI5622_DFSB_SET_BODY_CLEAR)
		return -EINVAL;
	if (cap < HI5622_DFSB_SET_WIRE_LEN)
		return -ENOSPC;
	memset(wire, 0, HI5622_DFSB_SET_WIRE_LEN);
	/* 12-byte HCC header: g0 id1, the inherited fa/fb bytes (0 here, the
	 * AP-start ladder owns the field-B module byte) and the tag. */
	wire[0] = 0x00u;
	wire[1] = 0x01u;
	wire[2] = 0x00u;
	wire[3] = 0x00u;
	dfsb_put16(wire + 4, (u16)HI5622_DFSB_SET_WIRE_LEN);
	dfsb_put16(wire + 6, (u16)HI5622_DFSB_SET_ID);
	wire[8] = 0x00u;
	wire[9] = 0x00u;
	dfsb_put16(wire + 10, (u16)HI5622_DFSB_SET_TAG);
	/* 8-byte token at +12: the inherited bench-executed pattern, byte
	 * +16 = 0x01 (TABLES of ../wifi-5g-dfs-set). */
	wire[16] = 0x01u;
	dfsb_put16(wire + HI5622_DFSB_SET_KEY_OFF, (u16)HI5622_DFSB_SET_KEY);
	dfsb_put16(wire + HI5622_DFSB_SET_LEN_OFF,
		   (u16)HI5622_DFSB_SET_BODY_LEN);
	wire[HI5622_DFSB_SET_BODY_OFF] = body;
	memset(wire + HI5622_DFSB_SET_TAIL_OFF, 0xff, 4);
	*out_len = HI5622_DFSB_SET_WIRE_LEN;
	return 0;
}

int hi5622_dfsb_order_before(enum hi5622_dfsb_op a, enum hi5622_dfsb_op b)
{
	if ((unsigned int)a >= (unsigned int)HI5622_DFSB_OP_MAX ||
	    (unsigned int)b >= (unsigned int)HI5622_DFSB_OP_MAX)
		return -EINVAL;
	return (unsigned int)a < (unsigned int)b ? 1 : 0;
}

/* ---- 5. the start_ap feature plan ------------------------------------ */
int hi5622_dfsb_ap_plan(unsigned int ht_bw, unsigned int dfs_cac,
			unsigned int dfs_set_on, u8 band, u8 channel,
			u32 width, u32 ccfs1, u32 ccfs2,
			const struct hi5622_dfsb_cac *cac, u32 now_ms,
			struct hi5622_dfsb_ap_plan *out)
{
	struct hi5622_dfsb_width_plan wp;
	int rc;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (ht_bw > HI5622_DFSB_PARM_MAX || dfs_cac > HI5622_DFSB_PARM_MAX)
		return -EINVAL;
	/* One meaning per load: the CAC runtime owns the 0x07E7 frames. */
	if (dfs_cac == HI5622_DFSB_PARM_ON && dfs_set_on)
		return -EINVAL;
	rc = hi5622_dfsb_width_plan(ht_bw, band, channel, width, ccfs1, ccfs2,
				    &wp);
	if (rc)
		return rc;
	out->width_enabled = wp.enabled;
	out->width_sel = wp.sel;
	if (dfs_cac == HI5622_DFSB_PARM_ON && band == 1u &&
	    hi5622_dfsb_is_dfs_chan(channel)) {
		out->cac_required = true;
		rc = hi5622_dfsb_cac_ap_gate(cac, channel, now_ms);
		if (rc)
			return rc;
		out->cac_gate = true;
	}
	return 0;
}
