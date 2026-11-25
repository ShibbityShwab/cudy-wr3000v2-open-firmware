// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_ax_twt.c - TWT-enable config-event frame builder and the
 * fail-closed AP-start gate (see the header for the decoded contract and the
 * F2 field-B rule).
 *
 * ORIGINAL clean-room code (lane wifi-ax-twt-impl, task-199).  The frame
 * layout and the firmware write map are the committed facts of
 * ../wifi-ax-advanced/TABLES.md T2/T3/T5; this file only synthesizes the
 * host-built protocol bytes and refuses malformed requests.
 */
#include "hi5622_ax_twt.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <string.h>
#endif

static void twt_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

const char *hi5622_ax_twt_mode_name(unsigned int twt)
{
	if (twt == HI5622_AX_TWT_OFF)
		return "off";
	if (twt == HI5622_AX_TWT_ENABLE)
		return "enable";
	if (twt == HI5622_AX_TWT_ENABLE_REQUIRED)
		return "enable+required";
	return "invalid";
}

/* The vendor user path wal_hipriv_twt_enable accepts only {1,3} (the two
 * bits the firmware worker decodes); any other value is refused rather than
 * mapped onto a guessed bit combination. */
static int twt_flags_ok(u8 flags)
{
	return flags == HI5622_AX_TWT_ENABLE ||
	       flags == HI5622_AX_TWT_ENABLE_REQUIRED;
}

int hi5622_ax_twt_precheck(unsigned int twt, unsigned int beacon_static,
			   unsigned int vap_start, unsigned int he_caps,
			   unsigned int band, struct hi5622_ax_twt_plan *out)
{
	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (twt > HI5622_AX_TWT_PARM_MAX)
		return -EINVAL;		/* out of contract: never guess */
	if (twt == HI5622_AX_TWT_OFF)
		return 0;		/* clean no-post, current behaviour */
	if (!twt_flags_ok((u8)twt))
		return -EINVAL;		/* twt == 2: not a {1,3} value */
	/* Explicit TWT request: every AP-start precondition must hold, else
	 * the request cannot be honoured and is refused instead of dropped. */
	if (beacon_static != 1u)
		return -EINVAL;		/* TWT post lives in the vendor ladder */
	if (vap_start != 1u)
		return -EINVAL;		/* no AP start, no TWT enable */
	if (he_caps != 1u)
		return -EINVAL;		/* no HE advertisement -> no agreement */
	if (band != 0u)
		return -EOPNOTSUPP;	/* 5 GHz HE AP is not bench-proven */
	out->post = true;
	out->flags = (u8)twt;
	out->field_b = 0;		/* filled at post time from the live CA */
	out->da_mask = (u8)HI5622_AX_TWT_DA_MASK;
	out->da_expected = (u8)(HI5622_AX_TWT_DA_ENABLE |
				((twt == HI5622_AX_TWT_ENABLE_REQUIRED) ?
				 HI5622_AX_TWT_DA_REQUIRED : 0u));
	return 0;
}

int hi5622_ax_twt_field_b(u32 mctx_ca, u8 *out_field_b)
{
	u32 off;

	if (!out_field_b)
		return -EINVAL;
	if (!mctx_ca)
		return -EAGAIN;		/* module context not bound yet */
	if (mctx_ca < HI5622_AX_TWT_CTX0_CA)
		return -ERANGE;
	off = mctx_ca - HI5622_AX_TWT_CTX0_CA;
	if (off % HI5622_AX_TWT_CTX_STRIDE)
		return -ERANGE;		/* not a context-table entry */
	off /= HI5622_AX_TWT_CTX_STRIDE;
	if (off >= HI5622_AX_TWT_CTX_COUNT)
		return -ERANGE;		/* outside the 19 boot contexts */
	*out_field_b = (u8)off;
	return 0;
}

int hi5622_ax_twt_build(u8 *wire, size_t cap, size_t *out_len,
			const struct hi5622_ax_twt_plan *plan)
{
	u8 *b;

	if (!wire || !out_len || !plan)
		return -EINVAL;
	*out_len = 0;
	if (!plan->post)
		return -EINVAL;		/* never build an unrequested frame */
	if (!twt_flags_ok(plan->flags))
		return -EINVAL;
	if (plan->field_b > HI5622_AX_TWT_MAX_FIELD_B)
		return -EINVAL;		/* no such boot context (F2 lesson) */
	if (plan->da_mask != HI5622_AX_TWT_DA_MASK ||
	    (plan->da_expected & ~HI5622_AX_TWT_DA_MASK))
		return -EINVAL;		/* inconsistent read-back contract */
	if (cap < HI5622_AX_TWT_WIRE_LEN)
		return -ENOSPC;

	b = wire;
	memset(b, 0, HI5622_AX_TWT_WIRE_LEN);

	/* 12-byte HCC header (group 0, message id 1).  fb = the AP-start
	 * module context the caller derived from the live context table. */
	b[0] = 0x00u;				/* group 0 */
	b[1] = 0x01u;				/* alloc nibble 1, dst 0 */
	b[2] = 0x00u;				/* fa */
	b[3] = plan->field_b;			/* field B = AP-start module ctx */
	twt_put16(b + 4, (u16)HI5622_AX_TWT_WIRE_LEN);	/* total 0x1d */
	twt_put16(b + 6, 0x0001u);		/* message id 1 */
	b[8] = 0x00u;				/* fc */
	b[9] = 0x00u;				/* dst */
	twt_put16(b + 10, (u16)HI5622_AX_TWT_TAG);	/* 0x5A5A */

	/* 8-byte token: the fired envelope is {00000000 01000000}; the
	 * firmware g0-id1 dispatcher ignores it, so it stays the fired value. */
	b[16] = 0x01u;

	/* key u16 at hdr+20, body length u16 at hdr+22, one-byte body at +24
	 * (the dispatcher reads key = u16[msg+20], len = u16[msg+22] and
	 * passes body = msg+24 to the worker). */
	twt_put16(b + 20, (u16)HI5622_AX_TWT_KEY);
	twt_put16(b + 22, (u16)HI5622_AX_TWT_BODY_LEN);
	b[24] = plan->flags;

	/* Inherited 4-byte 0xFF tail every working g0-id1 frame carries. */
	memset(b + 24 + HI5622_AX_TWT_BODY_LEN, 0xff, 4);

	*out_len = HI5622_AX_TWT_WIRE_LEN;
	return 0;
}

int hi5622_ax_twt_status_line(unsigned int twt, unsigned int he_caps,
			      unsigned int beacon_static, int last_rc,
			      unsigned int posted, unsigned int refused,
			      char *buf, size_t len)
{
	int n;

	if (!buf || !len)
		return -EINVAL;
	n = snprintf(buf, len,
		     "twt: param=%u (%s) he_caps=%u beacon_static=%u last_rc=%d posted=%u refused=%u\n",
		     twt, hi5622_ax_twt_mode_name(twt), he_caps,
		     beacon_static, last_rc, posted, refused);
	if (n < 0)
		return n;
	return (int)((size_t)n < len ? (size_t)n : len - 1);
}
