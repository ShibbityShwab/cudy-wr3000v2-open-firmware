// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_desc_table.c - H2D 0x00F7 descriptor-table frame builder and the
 * fail-closed AP-start gate (see the header for the decoded contract).
 *
 * ORIGINAL clean-room code (lane wifi-desc-table-send, task-188).  The frame
 * layout and the blob field map are the committed facts of
 * ../wifi-fw-desc-table/TABLES.md T2/T3; this file only synthesizes the
 * host-built protocol bytes and refuses malformed requests.
 */
#include "hi5622_desc_table.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <string.h>
#endif

static void dt_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void dt_put32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

/* The boot pool's country string is "99\0" and the firmware install gate is
 * an ARM strcmp(pool, blob) dedupe: a blob whose cc equals "99" is silently
 * skipped (only the old pool is re-expanded), so it must be refused here.
 * Printable ASCII keeps the strcmp input unambiguous; the two letters are
 * the regdomain country code the vendor hmac_config_set_country ships. */
static bool dt_cc_ok(const u8 cc[2])
{
	if (cc[0] < 0x20u || cc[0] > 0x7eu || cc[1] < 0x20u || cc[1] > 0x7eu)
		return false;
	if (cc[0] == '9' && cc[1] == '9')
		return false;
	return true;
}

int hi5622_desc_table_plan(unsigned int desc_table, unsigned int beacon_static,
			   unsigned int band, struct hi5622_desc_table_plan *out)
{
	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (desc_table > HI5622_DESC_TABLE_PARM_MAX)
		return -EINVAL;
	if (band > 1u)
		return -EINVAL;
	/* An explicit 0xF7 request without the vendor-order AP start cannot be
	 * honoured (the frame has no safe slot in the legacy ladder): refuse
	 * instead of silently dropping it. */
	if (desc_table == HI5622_DESC_TABLE_PARM_MAX && beacon_static != 1u)
		return -EINVAL;
	out->cc[0] = (u8)HI5622_DESC_TABLE_CC0;
	out->cc[1] = (u8)HI5622_DESC_TABLE_CC1;
	out->recs = (u8)HI5622_DESC_TABLE_RECS;
	/* Band-1 AP start only: the pool record the fix arms is the 5 GHz one,
	 * and the RF programme this unblocks is the band-1-only branch. */
	out->post = (desc_table == HI5622_DESC_TABLE_PARM_MAX && band == 1u);
	return 0;
}

/* One 16-byte descriptor record: {u32 word, u8 field4, u8 pad, u16 field6,
 * 4 zero, u32 channel bitmap} (wifi-fw-desc-table TABLES T3). */
static void dt_record(u8 *p, u32 word, u8 f4, u16 f6, u32 bitmap)
{
	dt_put32(p, word);
	p[4] = f4;
	p[5] = 0u;
	dt_put16(p + 6, f6);
	dt_put32(p + 8, 0u);
	dt_put32(p + 12, bitmap);
}

int hi5622_desc_table_build(u8 *wire, size_t cap, size_t *out_len,
			    const struct hi5622_desc_table_plan *plan)
{
	u8 *b, *blob;

	if (!wire || !out_len || !plan)
		return -EINVAL;
	if (!plan->post)
		return -EINVAL;		/* never build an unrequested frame */
	if (plan->recs != HI5622_DESC_TABLE_RECS)
		return -EINVAL;
	if (!dt_cc_ok(plan->cc))
		return -EINVAL;
	if (cap < HI5622_DESC_TABLE_WIRE_LEN)
		return -ENOSPC;

	b = wire;
	memset(b, 0, HI5622_DESC_TABLE_WIRE_LEN);

	/* 12-byte HCC header (group 0, message id 1).  fb = 0x04 and the token
	 * pattern are the vendor emitter's / lane frame's exact values [V]. */
	b[0] = 0x00u;				/* group 0 */
	b[1] = 0x01u;				/* alloc nibble 1, dst 0 */
	b[2] = 0x00u;				/* fa */
	b[3] = 0x04u;				/* fb */
	dt_put16(b + 4, (u16)HI5622_DESC_TABLE_WIRE_LEN);	/* total 0x44 */
	dt_put16(b + 6, 0x0001u);		/* message id 1 */
	b[8] = 0x00u;				/* fc */
	b[9] = 0x00u;				/* dst */
	dt_put16(b + 10, (u16)HI5622_DESC_TABLE_TAG);		/* 0x5A5A */

	/* 8-byte token, then key u16 at hdr+20 and payload length at hdr+22
	 * (the firmware g0-id1 handler reads key = u16[msg+20] and passes
	 * body = msg+24, len = u16[msg+22] to the worker). */
	b[16] = 0x01u;
	dt_put16(b + 20, (u16)HI5622_DESC_TABLE_KEY);
	dt_put16(b + 22, (u16)HI5622_DESC_TABLE_BLOB_LEN);

	/* Descriptor blob = the 0x148-byte pool image for N = 2 records. */
	blob = b + 24;
	blob[0] = plan->cc[0];
	blob[1] = plan->cc[1];
	blob[2] = 0u;		/* NUL: the gate's strcmp stops here */
	blob[3] = 0u;		/* chip / regdomain id (boot default 0) */
	blob[4] = plan->recs;	/* N: the worker's length formula input */
	blob[5] = 0u;
	blob[6] = 0u;
	blob[7] = 0u;
	dt_record(blob + 8u, HI5622_DESC_TABLE_WORD_2G,
		  (u8)HI5622_DESC_TABLE_PLAN_4,
		  (u16)HI5622_DESC_TABLE_PLAN_6,
		  HI5622_DESC_TABLE_BITMAP_2G);
	dt_record(blob + 8u + HI5622_DESC_TABLE_REC_LEN,
		  HI5622_DESC_TABLE_WORD_5G, (u8)HI5622_DESC_TABLE_PLAN_4,
		  (u16)HI5622_DESC_TABLE_PLAN_6,
		  HI5622_DESC_TABLE_BITMAP_5G);

	/* Inherited 4-byte 0xFF tail every working g0-id1 frame carries. */
	memset(b + 24 + HI5622_DESC_TABLE_BLOB_LEN, 0xff, 4);

	*out_len = HI5622_DESC_TABLE_WIRE_LEN;
	return 0;
}

int hi5622_desc_table_status_line(unsigned int desc_table,
				  unsigned int beacon_static, int last_rc,
				  unsigned int posted, unsigned int refused,
				  char *buf, size_t len)
{
	const char *mode;
	int n;

	if (!buf || !len)
		return -EINVAL;
	if (desc_table == 0u)
		mode = "off";
	else if (desc_table == HI5622_DESC_TABLE_PARM_MAX)
		mode = "band1-post";
	else
		mode = "invalid";
	n = snprintf(buf, len,
		     "desc_table: param=%u (%s) beacon_static=%u last_rc=%d posted=%u refused=%u\n",
		     desc_table, mode, beacon_static, last_rc, posted, refused);
	if (n < 0)
		return n;
	return (int)((size_t)n < len ? (size_t)n : len - 1);
}
