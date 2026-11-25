// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_band_start.c - H2D 0x0242/0x0243 band-1 MAC-start frames and the
 * fail-closed AP-start gate (see the header for the decoded contract).
 *
 * ORIGINAL clean-room code (lane wifi-band1-start-send, task-206).  The frame
 * layout is the committed g0-id1 config-event shape of
 * ../wifi-fw-desc-table/TABLES.md T2; the key/worker/subcommand facts are
 * ../wifi-band1-start/TABLES.md T1/T2/T5.  This file only synthesizes the
 * host-built protocol bytes and refuses malformed requests.
 */
#include "hi5622_band_start.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <string.h>
#endif

static void bs_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

int hi5622_band_start_plan(unsigned int band_start,
			   unsigned int band_start_fallback,
			   unsigned int beacon_static, unsigned int band,
			   unsigned int module, struct hi5622_band_start_plan *out)
{
	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (band_start > HI5622_BAND_START_PARM_MAX)
		return -EINVAL;
	if (band_start_fallback > HI5622_BAND_START_PARM_MAX)
		return -EINVAL;
	if (band > 1u || module > HI5622_BAND_START_MODULE_MAX)
		return -EINVAL;
	/* An explicit 0x0242 request without the vendor-order AP start cannot be
	 * honoured (the frame has no safe slot in the legacy ladder): refuse
	 * instead of silently dropping it. */
	if (band_start == HI5622_BAND_START_PARM_MAX && beacon_static != 1u)
		return -EINVAL;
	/* The fallback ladder only exists as a continuation of the primary
	 * rung; a fallback-only request has no primary arm to verify. */
	if (band_start_fallback == HI5622_BAND_START_PARM_MAX &&
	    band_start != HI5622_BAND_START_PARM_MAX)
		return -EINVAL;
	out->module = (u8)(module & 0xffu);
	/* Band-1 AP start only: CA 0x4AF0C's band-1 branch owns the
	 * 0x4010C220 arm bits; the band-0 branch has its own start path. */
	out->post = (band_start == HI5622_BAND_START_PARM_MAX && band == 1u);
	out->fallback = out->post &&
			(band_start_fallback == HI5622_BAND_START_PARM_MAX);
	return 0;
}

/* The 12-byte HCC header shared by every g0-id1 config-event frame this
 * model builds: group 0, alloc 1 / dst 0, the inherited fa, field B = the
 * module/context index, total, message id 1, fc/dst 0, transport tag. */
static void bs_header(u8 *b, u16 total, u8 module)
{
	b[0] = 0x00u;				/* group 0 */
	b[1] = 0x01u;				/* alloc nibble 1, dst 0 */
	b[2] = (u8)HI5622_BAND_START_FA;
	b[3] = module;				/* field B */
	bs_put16(b + 4, total);
	bs_put16(b + 6, (u16)HI5622_BAND_START_ID);
	b[8] = 0x00u;				/* fc */
	b[9] = 0x00u;				/* dst */
	bs_put16(b + 10, (u16)HI5622_BAND_START_TAG);
	/* 8-byte token: the inherited bench-executed g0-id1 pattern (payload
	 * word at +4 = 1); the key workers do not parse it. */
	b[HI5622_BAND_START_TOKEN_OFF + 4u] = 0x01u;
}

static size_t bs_body_len(unsigned int frame)
{
	switch (frame) {
	case HI5622_BAND_START_FRAME_PRIMARY:
	case HI5622_BAND_START_FRAME_FORCED:
		return HI5622_BAND_START_BODY_LEN;
	case HI5622_BAND_START_FRAME_NUDGE:
		return HI5622_BAND_START_NUDGE_BODY_LEN;
	default:
		return 0u;
	}
}

int hi5622_band_start_build(u8 *wire, size_t cap, size_t *out_len,
			    const struct hi5622_band_start_plan *plan,
			    unsigned int frame)
{
	size_t body_len = bs_body_len(frame);
	size_t wire_len;
	u16 key;
	u8 *b;

	if (!wire || !out_len || !plan)
		return -EINVAL;
	if (!body_len)
		return -EINVAL;		/* unknown frame selector */
	if (!plan->post)
		return -EINVAL;		/* never build an unrequested frame */
	if (frame != HI5622_BAND_START_FRAME_PRIMARY && !plan->fallback)
		return -EINVAL;		/* fallback rung not armed */
	if (plan->module > HI5622_BAND_START_MODULE_MAX)
		return -EINVAL;
	wire_len = (size_t)HI5622_BAND_START_HDR_LEN +
		   HI5622_BAND_START_TOKEN_LEN + 2u + 2u + body_len + 4u;
	if (cap < wire_len)
		return -ENOSPC;

	b = wire;
	memset(b, 0, wire_len);
	bs_header(b, (u16)wire_len, plan->module);
	if (frame == HI5622_BAND_START_FRAME_NUDGE) {
		/* key 0x0243: worker CA 0x8419C -> 0x4BDCE(bandobj, 5, 0); the
		 * worker reads no body byte, so the minimal one-byte body is
		 * sent as a well-formed carrier. */
		key = (u16)HI5622_BAND_START_FALLBACK_KEY;
	} else {
		key = (u16)HI5622_BAND_START_KEY;
	}
	bs_put16(b + HI5622_BAND_START_KEY_OFF, key);
	bs_put16(b + HI5622_BAND_START_LEN_OFF, (u16)body_len);
	if (frame == HI5622_BAND_START_FRAME_PRIMARY) {
		/* body {01,00,01,00,00}: subcmd 1 (sync/conditional start),
		 * body[2]=1 sets the run-state bit before the 0x4C3FC gate. */
		b[HI5622_BAND_START_BODY_OFF + 0u] =
			(u8)HI5622_BAND_START_SUBCMD;
		b[HI5622_BAND_START_BODY_OFF + 2u] =
			(u8)HI5622_BAND_START_ARM_FLAG;
	} else if (frame == HI5622_BAND_START_FRAME_FORCED) {
		/* body {02,00,00,00,00}: subcmd 2 (forced start). */
		b[HI5622_BAND_START_BODY_OFF + 0u] =
			(u8)HI5622_BAND_START_SUBCMD_FORCED;
	}
	/* The inherited 4-byte 0xFF tail every working g0-id1 frame carries. */
	memset(b + HI5622_BAND_START_BODY_OFF + body_len, 0xff, 4);

	*out_len = wire_len;
	return 0;
}

int hi5622_band_start_precheck(const struct hi5622_band_start_state *st)
{
	/* The worker CA 0x4C524 returns when the band object (ctx+0x4B8) is
	 * absent; CA 0x4AF0C returns at 0x4AF2C when the start marker is 2 and
	 * at 0x4AF32 when no VAP index is bound (state[4] == 0xFF). */
	if (!st)
		return -EINVAL;
	if (!st->has_bandobj || st->vap_index == HI5622_BAND_START_STATE_NO_VAP)
		return -EAGAIN;
	if (st->started == HI5622_BAND_START_STATE_STARTED)
		return -EALREADY;
	return 0;
}

int hi5622_band_start_status_line(unsigned int band_start,
				  unsigned int band_start_fallback,
				  unsigned int beacon_static, int last_rc,
				  unsigned int posted, unsigned int forced,
				  unsigned int nudge, unsigned int refused,
				  char *buf, size_t len)
{
	const char *mode;
	int n;

	if (!buf || !len)
		return -EINVAL;
	if (band_start == 0u)
		mode = "off";
	else if (band_start == HI5622_BAND_START_PARM_MAX)
		mode = "band1-post";
	else
		mode = "invalid";
	n = snprintf(buf, len,
		     "band_start: param=%u (%s) fallback=%u beacon_static=%u last_rc=%d posted=%u forced=%u nudge=%u refused=%u\n",
		     band_start, mode, band_start_fallback, beacon_static,
		     last_rc, posted, forced, nudge, refused);
	if (n < 0)
		return n;
	return (int)((size_t)n < len ? (size_t)n : len - 1);
}
