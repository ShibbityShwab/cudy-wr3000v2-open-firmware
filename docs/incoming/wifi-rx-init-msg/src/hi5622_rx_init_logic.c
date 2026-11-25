// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_rx_init_logic.c - portable model of the candidate firmware RX-init
 * config message (lane rx-init-decode/task-170).  See the header + REPORT.md.
 *
 * Frame (32 bytes, group 0 / id 1 / total at +4 / tag 0x5a5a at +10):
 *
 *   00 00 00 04 | 20 00 01 00 00 00 5a 5a        header (fb=4 = created module)
 *   00 00 00 00 01 00 00 00                     inherited 8-byte token
 *   04 00                                       u16 key 0x0004  (hdr+20)
 *   04 00                                       u16 len 4       (hdr+22)
 *   01 00 00 00                                 data u32 = 1    (hdr+24)
 *   ff ff ff ff                                 inherited tail
 */
#include "hi5622_rx_init_logic.h"

static const u8 rx_init_token[8] = { 0x00, 0x00, 0x00, 0x00,
				     0x01, 0x00, 0x00, 0x00 };
static const u8 rx_init_tail[4] = { 0xff, 0xff, 0xff, 0xff };

int hi5622_rx_init_key_allowed(u16 key)
{
	return key == HI5622_RX_INIT_KEY_DEFAULT;
}

int hi5622_rx_init_build(struct hi5622_rx_init_frame *f, u16 key, u32 value)
{
	u8 *w;

	if (!f || !hi5622_rx_init_key_allowed(key))
		return -1;
	memset(f, 0, sizeof(*f));
	w = f->wire;
	w[0] = 0x00;			/* group 0 */
	w[1] = 0x01;			/* hcc_msg_alloc alloc-state nibble, dst 0 */
	w[2] = 0x00;			/* fa */
	w[3] = HI5622_RX_INIT_MODULE;	/* fb = the created module (hdr[3]) */
	w[4] = (u8)(HI5622_RX_INIT_TOTAL & 0xffu);
	w[5] = (u8)(HI5622_RX_INIT_TOTAL >> 8);
	w[6] = 0x01;			/* message id 1 = config event */
	w[7] = 0x00;
	w[8] = 0x00;			/* fc */
	w[9] = 0x00;			/* dst */
	w[10] = (u8)(HI5622_RX_INIT_TAG & 0xffu);
	w[11] = (u8)(HI5622_RX_INIT_TAG >> 8);
	memcpy(w + 12, rx_init_token, sizeof(rx_init_token));
	w[20] = (u8)(key & 0xffu);	/* key low */
	w[21] = (u8)(key >> 8);		/* key high */
	w[22] = (u8)(HI5622_RX_INIT_LEN & 0xffu);
	w[23] = (u8)(HI5622_RX_INIT_LEN >> 8);
	w[24] = (u8)(value & 0xffu);
	w[25] = (u8)((value >> 8) & 0xffu);
	w[26] = (u8)((value >> 16) & 0xffu);
	w[27] = (u8)((value >> 24) & 0xffu);
	memcpy(w + 28, rx_init_tail, sizeof(rx_init_tail));
	f->total = HI5622_RX_INIT_TOTAL;
	return 0;
}

int hi5622_rx_init_validate(const u8 *wire, u32 len)
{
	u16 total, id, key, dlen;

	if (!wire || len != HI5622_RX_INIT_TOTAL)
		return -1;
	total = (u16)(wire[4] | (wire[5] << 8));
	id = (u16)(wire[6] | (wire[7] << 8));
	key = (u16)(wire[20] | (wire[21] << 8));
	dlen = (u16)(wire[22] | (wire[23] << 8));
	if ((wire[0] & 0x0fu) != 0u)		/* group 0 only */
		return -1;
	if ((wire[1] & 0x0fu) != 0x01u)		/* hcc_msg_alloc nibble (vendor) */
		return -1;
	if (total != len)			/* declared total == wire length */
		return -1;
	if (id != 0x0001u)			/* g0 id1 config event */
		return -1;
	if (!hi5622_rx_init_key_allowed(key))
		return -1;
	if (dlen != HI5622_RX_INIT_LEN)
		return -1;
	if ((u16)(wire[10] | (wire[11] << 8)) != HI5622_RX_INIT_TAG)
		return -1;
	if (wire[28] != 0xffu || wire[29] != 0xffu ||
	    wire[30] != 0xffu || wire[31] != 0xffu)
		return -1;
	return 0;
}

enum hi5622_rx_init_refusal
hi5622_rx_init_gate(const struct hi5622_rx_init_live *live)
{
	if (!live)
		return HI5622_RX_INIT_NOT_ARMED;
	if (!live->armed)
		return HI5622_RX_INIT_NOT_ARMED;
	if (!live->released)
		return HI5622_RX_INIT_NOT_RELEASED;
	if (!live->boot_latched)
		return HI5622_RX_INIT_NOT_LATCHED;
	if (live->lane_closed)
		return HI5622_RX_INIT_LANE_CLOSED;
	if (!live->phase_ready)
		return HI5622_RX_INIT_PHASE;
	if (!live->ctx_created)
		return HI5622_RX_INIT_NO_CTX;
	if (live->already_sent)
		return HI5622_RX_INIT_DUP;
	if (live->liveness_used)
		return HI5622_RX_INIT_LIVENESS_USED;
	if (live->frames_sent >= live->budget)
		return HI5622_RX_INIT_BUDGET;
	if (live->mbox_inflight)
		return HI5622_RX_INIT_INFLIGHT;
	if (live->out0)
		return HI5622_RX_INIT_OUT0;
	return HI5622_RX_INIT_OK;
}

const char *hi5622_rx_init_refusal_name(enum hi5622_rx_init_refusal r)
{
	switch (r) {
	case HI5622_RX_INIT_OK:			return "ok";
	case HI5622_RX_INIT_NOT_ARMED:		return "not_armed";
	case HI5622_RX_INIT_NOT_RELEASED:	return "not_released";
	case HI5622_RX_INIT_NOT_LATCHED:	return "not_latched";
	case HI5622_RX_INIT_LANE_CLOSED:	return "lane_closed";
	case HI5622_RX_INIT_PHASE:		return "phase_not_ready";
	case HI5622_RX_INIT_NO_CTX:		return "no_ctx_create";
	case HI5622_RX_INIT_DUP:		return "rx_init_already_sent";
	case HI5622_RX_INIT_LIVENESS_USED:	return "liveness_probe_used";
	case HI5622_RX_INIT_BUDGET:		return "budget_exhausted";
	case HI5622_RX_INIT_INFLIGHT:		return "announce_inflight";
	case HI5622_RX_INIT_OUT0:		return "out0_not_consumed";
	default:				return "?";
	}
}

enum hi5622_rx_init_result
hi5622_rx_init_readback(u32 out0_after_polls, u32 polls_done, u32 poll_max,
			u8 lane_closed)
{
	if (poll_max == 0u)
		return HI5622_RX_INIT_RESULT_BAD_ARG;
	if (!out0_after_polls && polls_done <= poll_max)
		return HI5622_RX_INIT_RESULT_CONSUMED;
	if (lane_closed)
		return HI5622_RX_INIT_RESULT_LANE_CLOSED;
	return HI5622_RX_INIT_RESULT_TIMEOUT;
}
