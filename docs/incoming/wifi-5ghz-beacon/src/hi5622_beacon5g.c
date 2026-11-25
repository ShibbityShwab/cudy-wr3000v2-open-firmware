// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_beacon5g.c - fail-closed 5 GHz AP-start channel-activation planner
 * (lane wifi-5ghz-beacon, shared task task-175).  Pure C99; see the header
 * for the derivation and ../REPORT.md for the vendor decode.
 */
#include "hi5622_beacon5g.h"

/* Firmware RAM window bounds (region 5; project convention, hi5622_contract.h:
 * CPU 0x00100000..0x0217ffff).  A ctx outside it is a bad read, not a
 * context: return 0 so the caller fails closed instead of dereferencing. */
#define HI5622_BEACON5G_RAM_LO		0x00100000u
#define HI5622_BEACON5G_RAM_HI		0x0217ffffu

u32 hi5622_beacon5g_mctx_ca(u32 slot0, u32 base)
{
	u32 ca;

	if (!slot0)
		return 0;
	ca = slot0 + (u32)(u16)base;
	if (ca < HI5622_BEACON5G_RAM_LO || ca > HI5622_BEACON5G_RAM_HI)
		return 0;
	return ca;
}

bool hi5622_beacon5g_chan_ctx_ready(u32 word, unsigned int band,
				    unsigned int channel)
{
	if (band > 1u || channel == 0u || channel > 0xffffu)
		return false;
	/* Worker body map [V]: +0x10 channel, +0x11 band, +0x13 valid. */
	return (unsigned int)(word & 0xffu) == channel &&
	       (unsigned int)((word >> 8) & 0xffu) == band &&
	       ((word >> 24) & 0xffu) == 1u;
}

int hi5622_beacon5g_plan(unsigned int wait_param, unsigned int beacon_static,
			 unsigned int band, u32 slot0, u32 base,
			 struct hi5622_beacon5g_plan *out)
{
	if (!out || band > 1u || wait_param > HI5622_BEACON5G_WAIT_MAX_MS)
		return -EINVAL;
	out->wait_ms = 0;
	out->readback = (beacon_static == 1u);
	out->mctx = 0;
	if (beacon_static != 1u)
		return 0;
	out->mctx = hi5622_beacon5g_mctx_ca(slot0, base);
	/* The legacy 2.4 GHz path is bench-validated byte-for-byte; the wait is
	 * only meaningful for the band-1 AP (the 5 GHz RF path).  wait 0 = the
	 * bench-fired vap5g module behaviour, unchanged. */
	if (band == 1u)
		out->wait_ms = wait_param;
	return 0;
}
