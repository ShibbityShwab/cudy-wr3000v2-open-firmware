// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_app_ie_wext.c - the vendor WEXT set_vendor_ie call sequence: the WEXT
 * request parser, the 0x7f IE strip, the host mirror of the per-VAP IE slot
 * table and the WAL cfg-event 0x0276 SET_VENDOR_IE envelope builder (see the
 * header for the decoded contract, TABLES.md W1-W5).
 *
 * ORIGINAL clean-room code (lane appie-wext2, task-239).  It only synthesizes
 * protocol bytes and refuses malformed requests; it never touches a device.
 */
#include "hi5622_app_ie_wext.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <string.h>
#endif

static void aiw_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void aiw_put32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

static u32 aiw_get32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) |
	       ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

/* Walk one IE TLV list and prove it is well formed.  Returns 0 or -EINVAL. */
static int aiw_ie_list_check(const u8 *ies, size_t len)
{
	size_t off = 0;

	if (!ies || !len)
		return 0;
	while (off + 2u <= len) {
		size_t elen = ies[off + 1u];

		if (off + 2u + elen > len)
			return -EINVAL;
		off += 2u + elen;
	}
	if (off != len)
		return -EINVAL;
	return 0;
}

int hi5622_aiw_wext_parse(const u8 *ubuf, size_t ulen,
			  struct hi5622_aiw_req *out)
{
	u32 cmd, ie_len;

	if (!ubuf || !out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (ulen < HI5622_AIW_WEXT_HDR_LEN)
		return -EINVAL;
	cmd = aiw_get32(ubuf);
	if (cmd != HI5622_AIW_WEXT_CMD_SET_VENDOR_IE)
		return -EINVAL;
	ie_len = aiw_get32(ubuf + 4u);
	if (ie_len > HI5622_AIW_WEXT_IE_MAX)
		return -EINVAL;
	if (ubuf[8] > HI5622_AIW_TYPE_MAX)
		return -EINVAL;
	if (ulen < HI5622_AIW_WEXT_HDR_LEN + (size_t)ie_len)
		return -EINVAL;
	if (ie_len)
		memcpy(out->ie, ubuf + HI5622_AIW_WEXT_HDR_LEN, ie_len);
	out->valid = true;
	out->ie_len = ie_len;
	out->type = ubuf[8];
	out->mode = HI5622_AIW_MODE_SAVE;
	return 0;
}

int hi5622_aiw_strip_7f(u8 *ie, size_t *ie_len)
{
	size_t off = 0, len;

	if (!ie || !ie_len)
		return -EINVAL;
	len = *ie_len;
	while (off + 2u <= len) {
		size_t elen = ie[off + 1u];
		size_t total = 2u + elen;

		if (off + total > len)
			return -EINVAL;
		if (ie[off] == 0x7fu) {
			memmove(ie + off, ie + off + total,
				len - off - total);
			*ie_len = len - total;
			return 0;
		}
		off += total;
	}
	if (off != len)
		return -EINVAL;
	return 0;
}

int hi5622_aiw_mirror_save(struct hi5622_aiw_mirror *m, unsigned int type,
			   const u8 *ie, size_t ie_len,
			   unsigned int *out_len)
{
	struct hi5622_aiw_mirror_slot *slot;

	if (!m || type >= HI5622_AIW_MIRROR_TYPES ||
	    ie_len > HI5622_AIW_MIRROR_IE_MAX)
		return -EINVAL;
	slot = &m->slot[type];
	if (ie_len == 0u) {
		/* mac_vap_save_app_ie clear: free, ptr/len/cap = 0, key zero. */
		slot->used = false;
		slot->len = 0u;
		slot->cap = 0u;
		memset(slot->ie, 0, sizeof(slot->ie));
		if (out_len)
			*out_len = 0u;
		return 0;
	}
	if (!ie)
		return -EINVAL;
	if (slot->cap < (u32)ie_len || !slot->used) {
		slot->cap = (u32)ie_len;
		slot->used = true;
	}
	memcpy(slot->ie, ie, ie_len);
	slot->len = (u32)ie_len;
	if (out_len)
		*out_len = (unsigned int)ie_len;
	return 0;
}

int hi5622_aiw_plan(unsigned int app_ie_mode, unsigned int type,
		    const u8 *ctx3, u32 seq,
		    const u8 *ies, size_t ies_len,
		    struct hi5622_aiw_plan *out)
{
	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (app_ie_mode > 1u)
		return -EINVAL;
	if (app_ie_mode == 0u)
		return 0;		/* direct-frame mode: never build the envelope */
	if (!ctx3)
		return -EINVAL;
	if (type > HI5622_AIW_TYPE_MAX)
		return -EINVAL;
	if ((ctx3[1] & 0xf0u) || (ctx3[2] & 0xf0u))
		return -EINVAL;
	if (ies_len > HI5622_AIW_WEXT_IE_MAX)
		return -EINVAL;
	if (aiw_ie_list_check(ies, ies_len))
		return -EINVAL;
	out->post = true;
	out->ctx[0] = ctx3[0];
	out->ctx[1] = ctx3[1];
	out->ctx[2] = ctx3[2];
	out->type = (u8)type;
	out->seq = seq;
	out->ie_len = ies_len;
	if (ies_len) {
		if (!ies)
			return -EINVAL;
		memcpy(out->ie, ies, ies_len);
	}
	return 0;
}

int hi5622_aiw_cfg_event_build(u8 *wire, size_t cap, size_t *out_len,
			       const struct hi5622_aiw_plan *plan)
{
	u8 *p;

	if (!wire || !out_len || !plan)
		return -EINVAL;
	if (!plan->post)
		return -EINVAL;		/* never build an unrequested frame */
	if (plan->type > HI5622_AIW_TYPE_MAX ||
	    plan->ie_len > HI5622_AIW_WEXT_IE_MAX)
		return -EINVAL;
	if ((plan->ctx[1] & 0xf0u) || (plan->ctx[2] & 0xf0u))
		return -EINVAL;
	if (aiw_ie_list_check(plan->ie, plan->ie_len))
		return -EINVAL;
	if (cap < HI5622_AIW_WIRE_LEN)
		return -ENOSPC;

	/* The whole fixed 536-byte frame, zeroed; the IE area is zero padded
	 * because the vendor sends the complete 504-byte request struct. */
	memset(wire, 0, HI5622_AIW_WIRE_LEN);

	/* 12-byte HCC header: group in the low nibble of b0 (TABLES W3). */
	wire[0] = (u8)(HI5622_AIW_GROUP & 0x0fu);
	wire[1] = (u8)(HI5622_AIW_HDR_B1 | ((plan->ctx[2] & 0x0fu) << 4));
	wire[2] = plan->ctx[1];
	wire[3] = plan->ctx[0];
	aiw_put16(wire + 4u, (u16)HI5622_AIW_WIRE_LEN);
	aiw_put16(wire + 6u, (u16)HI5622_AIW_MSG_ID);
	aiw_put16(wire + 10u, (u16)HI5622_AIW_HCC_TAG);

	/* hdr+12: the transport/response cookie (zero for a driver post) [I]. */
	/* The WAL RPC prefix at hdr+16. */
	p = wire + HI5622_AIW_HDR_LEN + HI5622_AIW_COOKIE_LEN;
	aiw_put32(p + 0u, HI5622_AIW_RPC_TAG);
	aiw_put16(p + 6u, (u16)HI5622_AIW_CFG_MSG_LEN);
	aiw_put32(p + 8u, plan->seq);

	/* The 508-byte cfg-event message at hdr+28. */
	p += HI5622_AIW_RPC_HDR_LEN;
	aiw_put16(p + 0u, (u16)HI5622_AIW_EVENT_SET_VENDOR_IE);
	aiw_put16(p + 2u, (u16)HI5622_AIW_EVENT_PAYLOAD_LEN);
	aiw_put32(p + 4u, (u32)plan->ie_len);
	p[8] = plan->type;
	p[9] = HI5622_AIW_MODE_SAVE;
	if (plan->ie_len)
		memcpy(p + HI5622_AIW_CFG_HDR_LEN + HI5622_AIW_REQ_HDR_LEN,
		       plan->ie, plan->ie_len);

	*out_len = HI5622_AIW_WIRE_LEN;
	return 0;
}

int hi5622_aiw_status_line(unsigned int app_ie_mode, unsigned int app_ie_when,
			   unsigned int ctx0, int last_rc,
			   unsigned int posted, unsigned int refused,
			   unsigned int mirror_len, unsigned int mirror_used,
			   char *buf, size_t len)
{
	const char *mode, *when;
	int n;

	if (!buf || !len)
		return -EINVAL;
	mode = app_ie_mode == 0u ? "direct" :
	       app_ie_mode == 1u ? "wext-event" : "invalid";
	when = app_ie_when == 0u ? "pre-start" :
	       app_ie_when == 1u ? "post-enable" : "invalid";
	n = snprintf(buf, len,
		     "app_ie_wext: mode=%u (%s) when=%u (%s) ctx[0]=%u last_rc=%d posted=%u refused=%u mirror_len=%u mirror_used=%u\n",
		     app_ie_mode, mode, app_ie_when, when, ctx0, last_rc,
		     posted, refused, mirror_len, mirror_used);
	if (n < 0)
		return n;
	return (int)((size_t)n < len ? (size_t)n : len - 1);
}
