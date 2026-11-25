// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_sta_sme.c - portable kernel-SME logic for the STA (client) connect
 * flow of the Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-sta-mode, shared task task-155,
 * 2026-11-25).  See hi5622_sta_sme.h for the decoded model and evidence
 * markers.  Pure C99, no libc: host-tested by tools/sta_mode_selftest.c and
 * built into the 6.18 kernel module unchanged.
 *
 * SAFETY: these builders only produce public over-the-air management frames
 * (auth request / assoc request) and the decoded H2D association-state sync;
 * no key material, no secret, no device register access.  Every decoder is
 * fail-closed: a short, wrong-subtype or malformed-IE frame is refused, never
 * partially accepted.
 */
#include "hi5622_sta_sme.h"

/* ---------------------------------------------------------------------
 * Small endian helpers (the wire format is little-endian; the target is
 * ARMv7 little-endian, but keep this explicit for the host tests).
 * -------------------------------------------------------------------*/
static u16 hl_get16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static void hl_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void hl_copy(u8 *dst, const u8 *src, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		dst[i] = src[i];
}

static void hl_zero(u8 *dst, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++)
		dst[i] = 0u;
}

static int hl_mac_eq(const u8 *a, const u8 *b)
{
	size_t i;

	for (i = 0; i < 6u; i++) {
		if (a[i] != b[i])
			return 0;
	}
	return 1;
}

/* ---------------------------------------------------------------------
 * State machine
 * -------------------------------------------------------------------*/
const char *hism_state_name(u32 state)
{
	switch (state) {
	case HISM_OFF:		return "off";
	case HISM_JOIN_POSTED:	return "join_posted";
	case HISM_AUTH_SENT:	return "auth_sent";
	case HISM_AUTH_DONE:	return "auth_done";
	case HISM_ASSOC_SENT:	return "assoc_sent";
	case HISM_CONNECTED:	return "connected";
	case HISM_FAILED:	return "failed";
	default:		return "invalid";
	}
}

int hism_state_pending(u32 state)
{
	return state == HISM_JOIN_POSTED || state == HISM_AUTH_SENT ||
	       state == HISM_AUTH_DONE || state == HISM_ASSOC_SENT;
}

int hism_auth_rsp_apply(u32 *state, u16 status)
{
	if (!state || *state > HISM_STATE_MAX)
		return -1;
	if (*state != HISM_AUTH_SENT)
		return -2;	/* out of order: never completes a connect */
	*state = (status == HISM_STATUS_SUCCESS) ? HISM_AUTH_DONE
						 : HISM_FAILED;
	return 0;
}

int hism_assoc_rsp_apply(u32 *state, u16 status)
{
	if (!state || *state > HISM_STATE_MAX)
		return -1;
	if (*state != HISM_ASSOC_SENT)
		return -2;	/* out of order */
	*state = (status == HISM_STATUS_SUCCESS) ? HISM_CONNECTED
						 : HISM_FAILED;
	return 0;
}

u16 hism_timeout_reason(u32 state)
{
	if (state == HISM_AUTH_SENT)
		return HISM_TIMEOUT_AUTH;
	if (state == HISM_ASSOC_SENT || state == HISM_AUTH_DONE)
		return HISM_TIMEOUT_ASSOC;
	return HISM_TIMEOUT_UNSPECIFIED;
}

/* ---------------------------------------------------------------------
 * Address / frame builders
 * -------------------------------------------------------------------*/
int hism_mac_ok(const u8 *mac)
{
	u8 or_all = 0u;
	size_t i;

	if (!mac)
		return 0;
	for (i = 0; i < 6u; i++)
		or_all = (u8)(or_all | mac[i]);
	if (or_all == 0u)
		return 0;			/* all-zero */
	if (mac[0] & 0x01u)
		return 0;			/* multicast/broadcast */
	return 1;
}

int hism_build_auth_req(u8 *frame, size_t cap, size_t *out_len,
			const struct hism_auth_req *p)
{
	if (!frame || !out_len || !p || !hism_mac_ok(p->own) ||
	    !hism_mac_ok(p->bssid))
		return -1;
	if (cap < HISM_AUTH_FRAME_LEN)
		return -2;
	hl_zero(frame, HISM_AUTH_FRAME_LEN);
	frame[0] = HISM_FC0_AUTH;
	frame[1] = 0x00u;
	hl_put16(frame + 2, 0u);		/* duration */
	hl_copy(frame + 4, p->bssid, 6);	/* addr1 = DA = AP */
	hl_copy(frame + 10, p->own, 6);		/* addr2 = SA = us  */
	hl_copy(frame + 16, p->bssid, 6);	/* addr3 = BSSID    */
	hl_put16(frame + 22, (u16)((p->seq & 0x0fu) << 4));
	hl_put16(frame + 24, HISM_AUTH_ALGO_OPEN);
	hl_put16(frame + 26, HISM_AUTH_SEQ_OPEN);
	hl_put16(frame + 28, HISM_STATUS_SUCCESS);
	*out_len = HISM_AUTH_FRAME_LEN;
	return 0;
}

int hism_build_assoc_req(u8 *frame, size_t cap, size_t *out_len,
			 const u8 **assoc_ie, size_t *assoc_ie_len,
			 const struct hism_assoc_req *p)
{
	size_t off, need;
	u16 capab;

	if (assoc_ie)
		*assoc_ie = NULL;
	if (assoc_ie_len)
		*assoc_ie_len = 0;
	if (!frame || !out_len || !p || !hism_mac_ok(p->own) ||
	    !hism_mac_ok(p->bssid))
		return -1;
	if (!p->ssid || p->ssid_len == 0u || p->ssid_len > HISM_MAX_SSID)
		return -1;
	/* 24 hdr + 4 fixed + SSID IE + rates IE + ext-rates IE */
	need = HISM_ASSOC_HDR_IE_OFF + 2u + (size_t)p->ssid_len +
	       2u + HISM_RATES_BASIC_LEN + 2u + HISM_RATES_EXT_LEN;
	if (need > HISM_MAX_FRAME || cap < need)
		return -2;

	hl_zero(frame, need);
	frame[0] = HISM_FC0_ASSOC_REQ;
	frame[1] = 0x00u;
	hl_put16(frame + 2, 0u);		/* duration */
	hl_copy(frame + 4, p->bssid, 6);	/* addr1 = DA = AP */
	hl_copy(frame + 10, p->own, 6);		/* addr2 = SA = us  */
	hl_copy(frame + 16, p->bssid, 6);	/* addr3 = BSSID    */
	hl_put16(frame + 22, (u16)((p->seq & 0x0fu) << 4));

	capab = (u16)(HISM_CAP_ESS | HISM_CAP_SHORT_PREAMBLE);
	if (p->privacy)
		capab = (u16)(capab | HISM_CAP_PRIVACY);
	hl_put16(frame + 24, capab);
	hl_put16(frame + 26, HISM_LISTEN_INTERVAL);

	off = HISM_ASSOC_HDR_IE_OFF;
	frame[off++] = HISM_EID_SSID;
	frame[off++] = p->ssid_len;
	hl_copy(frame + off, p->ssid, p->ssid_len);
	off += p->ssid_len;
	frame[off++] = HISM_EID_SUPPORTED_RATES;
	frame[off++] = HISM_RATES_BASIC_LEN;
	hl_copy(frame + off, hism_rates_basic, HISM_RATES_BASIC_LEN);
	off += HISM_RATES_BASIC_LEN;
	frame[off++] = HISM_EID_EXT_SUPPORTED_RATES;
	frame[off++] = HISM_RATES_EXT_LEN;
	hl_copy(frame + off, hism_rates_ext, HISM_RATES_EXT_LEN);
	off += HISM_RATES_EXT_LEN;

	*out_len = off;
	if (assoc_ie)
		*assoc_ie = frame + HISM_ASSOC_HDR_IE_OFF;
	if (assoc_ie_len)
		*assoc_ie_len = off - HISM_ASSOC_HDR_IE_OFF;
	return 0;
}

/* ---------------------------------------------------------------------
 * Response decoders.  Subtype first, then length, then the fixed body;
 * the assoc-response IE list is boundary-checked element by element so a
 * truncated/corrupt IE can never reach cfg80211.
 * -------------------------------------------------------------------*/
static int hl_check_mgmt(const u8 *frame, size_t len, u8 subtype)
{
	if (!frame)
		return -1;
	if (len < HISM_MGMT_HDR_LEN)
		return -1;
	if ((frame[0] & 0x03u) != 0u)		/* protocol version 0 */
		return -1;
	if (((frame[0] >> 2) & 0x03u) != 0u)	/* type management    */
		return -1;
	if (((frame[0] >> 4) & 0x0fu) != subtype)
		return -3;			/* not our subtype    */
	return 0;
}

int hism_decode_auth_resp(const u8 *frame, size_t len, struct hism_resp *out)
{
	int rc;

	if (!out)
		return -1;
	rc = hl_check_mgmt(frame, len, HISM_SUBTYPE_AUTH);
	if (rc)
		return rc;
	if (len < HISM_AUTH_RESP_MIN_LEN)
		return -1;
	hl_zero((u8 *)out, sizeof(*out));
	out->status = hl_get16(frame + 28);
	out->aid = 0u;
	out->ies = NULL;
	out->ies_len = 0u;
	return 0;
}

int hism_decode_assoc_resp(const u8 *frame, size_t len, struct hism_resp *out)
{
	size_t off;

	if (!out)
		return -1;
	if (hl_check_mgmt(frame, len, HISM_SUBTYPE_ASSOC_RESP))
		return hl_check_mgmt(frame, len, HISM_SUBTYPE_ASSOC_RESP);
	if (len < HISM_ASSOC_RESP_MIN_LEN)
		return -1;
	/* IE list boundary check (element id + length + body) */
	off = HISM_ASSOC_RESP_IE_OFF;
	while (off < len) {
		size_t elen;

		if (len - off < 2u)
			return -1;
		elen = (size_t)frame[off + 1u];
		if (len - off - 2u < elen)
			return -1;
		off += 2u + elen;
	}
	if (off != len)
		return -1;
	hl_zero((u8 *)out, sizeof(*out));
	out->status = hl_get16(frame + 26);
	out->aid = (u16)(hl_get16(frame + 28) & 0x3fffu);
	out->ies = frame + HISM_ASSOC_RESP_IE_OFF;
	out->ies_len = len - HISM_ASSOC_RESP_IE_OFF;
	return 0;
}

int hism_resp_for_us(const u8 *frame, size_t len, const u8 *own,
		     const u8 *bssid)
{
	if (!frame || !own || !bssid || len < HISM_MGMT_HDR_LEN)
		return 0;
	return hl_mac_eq(frame + 4, own) && hl_mac_eq(frame + 16, bssid);
}

int hism_ie_copy(u8 *dst, size_t cap, const u8 *ies, size_t ies_len,
		 size_t *out_len)
{
	if (out_len)
		*out_len = 0;
	if (!dst)
		return -1;
	if (ies_len == 0u || !ies)
		return 0;
	if (ies_len > cap)
		return -2;
	hl_copy(dst, ies, ies_len);
	if (out_len)
		*out_len = ies_len;
	return 0;
}

/* ---------------------------------------------------------------------
 * Association-state sync H2D frame (decoded, host-tested, not posted)
 * -------------------------------------------------------------------*/
int hism_build_assoc_sync(u8 *frame, size_t cap, size_t *out_len,
			  const struct hism_sync *p)
{
	u8 *pl;

	if (!frame || !out_len || !p || !hism_mac_ok(p->bssid))
		return -1;
	if (p->dst > 0x0fu)
		return -1;
	if (p->req_ie_len > HISM_MAX_IE || p->resp_ie_len > HISM_MAX_IE)
		return -1;
	/* both IE buffers must be device-visible addresses: non-zero, aligned */
	if (p->req_ie_va == 0u || p->resp_ie_va == 0u)
		return -1;
	if ((p->req_ie_va & 0x3u) || (p->resp_ie_va & 0x3u))
		return -1;
	if (cap < HISM_SYNC_FRAME_LEN)
		return -2;

	hl_zero(frame, HISM_SYNC_FRAME_LEN);
	frame[0] = (u8)HISM_SYNC_GROUP;			/* group nibble   */
	frame[1] = (u8)(((p->dst & 0x0fu) << 4) | 0x01u);/* dst + alloc    */
	frame[2] = p->hdr2;
	frame[3] = p->vap_id;
	hl_put16(frame + 4, (u16)HISM_SYNC_FRAME_LEN);	/* total length   */
	frame[6] = (u8)HISM_SYNC_ID;
	frame[7] = 0x00u;
	frame[8] = 0x00u;
	frame[9] = (u8)(p->dst & 0x0fu);
	hl_put16(frame + 10, HISM_HCC_TAG);

	pl = frame + HISM_HCC_HDR_LEN;
	hl_put16(pl + HISM_SYNC_OFF_STATUS, p->status);
	hl_copy(pl + HISM_SYNC_OFF_BSSID, p->bssid, 6);
	/* 32-bit fields (little-endian) */
	pl[HISM_SYNC_OFF_REQ_IE_LEN + 0u] = (u8)(p->req_ie_len & 0xffu);
	pl[HISM_SYNC_OFF_REQ_IE_LEN + 1u] = (u8)((p->req_ie_len >> 8) & 0xffu);
	pl[HISM_SYNC_OFF_REQ_IE_LEN + 2u] = (u8)((p->req_ie_len >> 16) & 0xffu);
	pl[HISM_SYNC_OFF_REQ_IE_LEN + 3u] = (u8)((p->req_ie_len >> 24) & 0xffu);
	pl[HISM_SYNC_OFF_RESP_IE_LEN + 0u] = (u8)(p->resp_ie_len & 0xffu);
	pl[HISM_SYNC_OFF_RESP_IE_LEN + 1u] = (u8)((p->resp_ie_len >> 8) & 0xffu);
	pl[HISM_SYNC_OFF_RESP_IE_LEN + 2u] = (u8)((p->resp_ie_len >> 16) & 0xffu);
	pl[HISM_SYNC_OFF_RESP_IE_LEN + 3u] = (u8)((p->resp_ie_len >> 24) & 0xffu);
	pl[HISM_SYNC_OFF_REQ_IE_VA + 0u] = (u8)(p->req_ie_va & 0xffu);
	pl[HISM_SYNC_OFF_REQ_IE_VA + 1u] = (u8)((p->req_ie_va >> 8) & 0xffu);
	pl[HISM_SYNC_OFF_REQ_IE_VA + 2u] = (u8)((p->req_ie_va >> 16) & 0xffu);
	pl[HISM_SYNC_OFF_REQ_IE_VA + 3u] = (u8)((p->req_ie_va >> 24) & 0xffu);
	pl[HISM_SYNC_OFF_RESP_IE_VA + 0u] = (u8)(p->resp_ie_va & 0xffu);
	pl[HISM_SYNC_OFF_RESP_IE_VA + 1u] = (u8)((p->resp_ie_va >> 8) & 0xffu);
	pl[HISM_SYNC_OFF_RESP_IE_VA + 2u] = (u8)((p->resp_ie_va >> 16) & 0xffu);
	pl[HISM_SYNC_OFF_RESP_IE_VA + 3u] = (u8)((p->resp_ie_va >> 24) & 0xffu);
	*out_len = HISM_SYNC_FRAME_LEN;
	return 0;
}
