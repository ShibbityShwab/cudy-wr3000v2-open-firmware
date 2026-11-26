// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_monitor_logic.c - portable local-mirror monitor logic (host-testable).
 *
 * ORIGINAL clean-room code (monitor2 lane, task-246, 2026-11-26).
 * See hi5622_monitor_logic.h for the semantics and the pinned byte vectors.
 * No vendor material is reproduced.
 */
#include "hi5622_monitor_logic.h"

int himon_mode_ok(uint32_t mode)
{
	return mode == HIMON_OFF || mode == HIMON_LOCAL_MIRROR;
}

const char *himon_mode_name(uint32_t mode)
{
	switch (mode) {
	case HIMON_OFF:
		return "off";
	case HIMON_LOCAL_MIRROR:
		return "local-mirror";
	default:
		return "invalid";
	}
}

int himon_chan_to_freq(uint32_t band, uint32_t channel, uint16_t *freq)
{
	uint32_t mhz;

	if (!freq)
		return -1;
	if (band == 0u) {
		if (channel >= 1u && channel <= 13u)
			mhz = 2407u + 5u * channel;
		else if (channel == 14u)
			mhz = 2484u;
		else
			return -1;
	} else if (band == 1u) {
		/* 5 GHz UNII-1..UNII-3/ISM centre channels (36..177 step 4
		 * plus the common 5 MHz-indexed values); a simple static map,
		 * no regulatory decision is made here. */
		if (channel < 36u || channel > 177u)
			return -1;
		mhz = 5000u + 5u * channel;
	} else {
		return -1;
	}
	if (mhz > 0xffffu)
		return -1;
	*freq = (uint16_t)mhz;
	return 0;
}

static void himon_put_le16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)(v & 0xffu);
	p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void himon_put_le32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xffu);
	p[1] = (uint8_t)((v >> 8) & 0xffu);
	p[2] = (uint8_t)((v >> 16) & 0xffu);
	p[3] = (uint8_t)((v >> 24) & 0xffu);
}

size_t himon_rt_build(uint8_t *buf, size_t cap, uint16_t freq,
		      int have_chan, int rssi, int have_rssi)
{
	size_t len = 8u;
	size_t off = 8u;
	uint32_t present = 0u;

	if (!buf)
		return 0u;
	if (have_chan && have_rssi)
		len = 13u;
	else if (have_chan)
		len = 12u;
	if (cap < len)
		return 0u;

	/* radiotap fixed part: version 0, pad, it_len, it_present */
	buf[0] = 0u;
	buf[1] = 0u;
	himon_put_le16(&buf[2], (uint16_t)len);
	if (have_chan && have_rssi) {
		present = 0x00000008u;		/* IEEE80211_RADIOTAP_CHANNEL */
		present |= 0x00000808u;		/* IEEE80211_RADIOTAP_DBM_ANTSIGNAL */
	} else if (have_chan) {
		present = 0x00000008u;
	}
	himon_put_le32(&buf[4], present);

	if (!have_chan)
		return len;	/* R0: no per-frame channel metadata */

	himon_put_le16(&buf[off], freq);
	off += 2u;
	himon_put_le16(&buf[off], 0u);	/* channel flags: unknown, never guessed */
	off += 2u;
	if (have_rssi) {
		buf[off] = (uint8_t)(int8_t)rssi;
		off += 1u;
	}
	return len;
}
