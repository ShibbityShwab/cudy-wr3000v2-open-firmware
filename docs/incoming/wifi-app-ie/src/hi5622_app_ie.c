// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_app_ie.c - H2D g0 id 0x20 app/vendor IE frame builder, the cfg80211
 * vendor-IE collector and the fail-closed AP-start gate (see the header for
 * the decoded contract).
 *
 * ORIGINAL clean-room code (lane wifi-app-ie, task-234).  The frame layout,
 * the payload geometry and the firmware handler behaviour are the committed
 * facts of ../TABLES.md T1-T5 (stock capture + builder/firmware decode); this
 * file only synthesizes the host-built protocol bytes and refuses malformed
 * requests.
 */
#include "hi5622_app_ie.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <string.h>
#endif

static void ai_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void ai_put32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

/* Walk one IE TLV list: count its elements and vendor-specific (0xDD) IEs and
 * prove the list is well formed (every element's len field fits the buffer).
 * Returns 0 or -EINVAL.  Does not copy. */
static int ai_ie_list_check(const u8 *ies, size_t len, unsigned int *vendor_ies)
{
	size_t off = 0;
	unsigned int v = 0;

	if (!ies || !len)
		return 0;
	while (off + 2u <= len) {
		size_t elen = ies[off + 1u];

		if (off + 2u + elen > len)
			return -EINVAL;
		if (ies[off] == HI5622_APP_IE_ELEM_ID)
			v++;
		off += 2u + elen;
	}
	if (off != len)
		return -EINVAL;
	if (vendor_ies)
		*vendor_ies = v;
	return 0;
}

int hi5622_app_ie_plan(unsigned int app_ie,
		       unsigned int beacon_static,
		       unsigned int vap_start_g0,
		       unsigned int band,
		       unsigned int type,
		       unsigned int vap,
		       const u8 *ies, size_t ies_len,
		       struct hi5622_app_ie_plan *out)
{
	unsigned int vendor = 0;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (app_ie > HI5622_APP_IE_PARM_MAX)
		return -EINVAL;
	if (app_ie == 0u)
		return 0;		/* off: never build a frame */
	/* The ladder slot this lane reuses is the beacon_static vendor order:
	 * it is the only AP-start path that posts the key-0x0065 VAP start
	 * (vap_start_g0) this frame must follow. */
	if (beacon_static != 1u || vap_start_g0 != 1u)
		return -EINVAL;
	if (band > 1u || type > HI5622_APP_IE_TYPE_MAX ||
	    vap > HI5622_APP_IE_VAP_MAX)
		return -EINVAL;
	if (ies_len > HI5622_APP_IE_MAX_BYTES)
		return -EINVAL;
	if (ai_ie_list_check(ies, ies_len, &vendor))
		return -EINVAL;
	out->post = true;
	out->vap = vap;
	out->type = (u8)type;
	out->mode = HI5622_APP_IE_MODE_SAVE;
	out->ie_len = ies_len;
	if (ies_len) {
		if (!ies)
			return -EINVAL;
		memcpy(out->ie, ies, ies_len);
	}
	out->vendor_ies = vendor;
	return 0;
}

int hi5622_app_ie_build(u8 *wire, size_t cap, size_t *out_len,
			const struct hi5622_app_ie_plan *plan)
{
	size_t wire_len;
	u8 *p;

	if (!wire || !out_len || !plan)
		return -EINVAL;
	if (!plan->post)
		return -EINVAL;		/* never build an unrequested frame */
	if (plan->ie_len > HI5622_APP_IE_MAX_BYTES)
		return -EINVAL;
	wire_len = HI5622_APP_IE_HDR_LEN + HI5622_APP_IE_PAYLOAD_HDR_LEN +
		   plan->ie_len;
	if (cap < wire_len)
		return -ENOSPC;

	memset(wire, 0, wire_len);

	/* 12-byte HCC header: group 0, message id 0x20, total length, tag.
	 * Byte-for-byte the vendor builder (CA 0x600cc) and the captured stock
	 * frame (T1): b0 low nibble cleared, b1 = 0x01, b2 = ctx[1] = 0,
	 * b3 = ctx[0] = the VAP/module index. */
	wire[0] = (u8)HI5622_APP_IE_GROUP;
	wire[1] = HI5622_APP_IE_HDR_B1;
	wire[2] = HI5622_APP_IE_HDR_B2;
	wire[3] = (u8)(plan->vap & 0xffu);
	ai_put16(wire + 4, (u16)wire_len);		/* total */
	ai_put16(wire + 6, (u16)HI5622_APP_IE_MSG_ID);
	wire[8] = 0u;
	wire[9] = 0u;
	ai_put16(wire + 10, (u16)HI5622_APP_IE_TAG);

	/* Payload = the vendor caller struct copy: u32 ie_len, u8 type,
	 * u8 mode, 2 pad, ie bytes, and the copied-but-unused 3-byte tail. */
	p = wire + HI5622_APP_IE_HDR_LEN;
	ai_put32(p, (u32)plan->ie_len);
	p[4] = plan->type;
	p[5] = plan->mode;
	if (plan->ie_len)
		memcpy(p + HI5622_APP_IE_DATA_OFF, plan->ie, plan->ie_len);
	/* p[DATA_OFF + ie_len .. +2] stay zero: memset above guarantees the
	 * 3-byte tail the vendor builder copies past the used struct. */

	*out_len = wire_len;
	return 0;
}

int hi5622_app_ie_collect(u8 *out, size_t cap, size_t *out_len,
			  unsigned int *vendor_ies,
			  const u8 *ies, size_t ies_len)
{
	size_t off = 0, used;

	if (!out || !out_len)
		return -EINVAL;
	used = *out_len;
	if (!ies || !ies_len)
		return 0;
	while (off + 2u <= ies_len) {
		size_t elen = ies[off + 1u];
		size_t total = 2u + elen;

		if (off + total > ies_len)
			return -EBADMSG;
		if (ies[off] == HI5622_APP_IE_ELEM_ID) {
			if (used + total > cap)
				return -ENOSPC;
			memcpy(out + used, ies + off, total);
			used += total;
			if (vendor_ies)
				(*vendor_ies)++;
		}
		off += total;
	}
	if (off != ies_len)
		return -EBADMSG;
	*out_len = used;
	return 0;
}

int hi5622_app_ie_status_line(unsigned int app_ie, unsigned int type,
			      unsigned int beacon_static,
			      unsigned int vap_start_g0,
			      int last_rc, unsigned int posted,
			      unsigned int refused, unsigned int ie_len,
			      unsigned int vendor_ies,
			      char *buf, size_t len)
{
	const char *mode;
	int n;

	if (!buf || !len)
		return -EINVAL;
	if (app_ie == 0u)
		mode = "off";
	else if (app_ie == HI5622_APP_IE_PARM_MAX)
		mode = "ap-post";
	else
		mode = "invalid";
	n = snprintf(buf, len,
		     "app_ie: param=%u (%s) type=%u beacon_static=%u vap_start_g0=%u last_rc=%d posted=%u refused=%u ie_len=%u vendor_ies=%u\n",
		     app_ie, mode, type, beacon_static, vap_start_g0,
		     last_rc, posted, refused, ie_len, vendor_ies);
	if (n < 0)
		return n;
	return (int)((size_t)n < len ? (size_t)n : len - 1);
}
