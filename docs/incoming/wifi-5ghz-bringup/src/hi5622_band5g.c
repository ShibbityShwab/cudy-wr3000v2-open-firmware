// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_band5g.c - 5 GHz band-enable / band-config helpers (clean-room).
 *
 * See hi5622_band5g.h for the design and ../REPORT.md for the evidence.
 * The tables are the vendor's static band tables, decoded from:
 *   host module  g_ast_channel_list_2G / g_ast_channel_list_5G
 *                (.data+0x3b8 / .data+0x3f0, 4-byte entries {u8 chan; u8 0xff})
 *                g_ast_freq_map_2g / g_ast_freq_map_5g
 *                (.rodata+0x918 / .rodata+0x8a4, 4-byte {u16 freq; u8 chan})
 *   firmware     CA 0x117B48 / CA 0x117AD4 (same 14/29 channel numbers),
 *                validator CA 0x10172C, H1 handler CA 0x5FF64.
 */
#include "hi5622_band5g.h"

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

/* 2.4 GHz: 14 channels, 2412..2472 + 2484 (ch14).  Flags 0: the kernel
 * regulatory core owns the restrictions on this band (same policy as the
 * bench-validated driver). */
static const struct hi5622_band5g_chan hi5622_band5g_2g[] = {
	{ 2412,  1,  0 }, { 2417,  2,  0 }, { 2422,  3,  0 },
	{ 2427,  4,  0 }, { 2432,  5,  0 }, { 2437,  6,  0 },
	{ 2442,  7,  0 }, { 2447,  8,  0 }, { 2452,  9,  0 },
	{ 2457, 10,  0 }, { 2462, 11,  0 }, { 2467, 12,  0 },
	{ 2472, 13,  0 }, { 2484, 14,  0 },
};

/* 5 GHz: 29 channels in the vendor table order (index 0..28):
 * 36 40 44 48 | 52 56 60 64 | 100..140 | 144 | 149 153 157 161 165 |
 * 184 188 192 196.  DFS channels carry RADAR|NO_IR (the kernel regdb still
 * decides); the 4.9 GHz block is passive-only. */
static const struct hi5622_band5g_chan hi5622_band5g_5g[] = {
	{ 5180,  36, 0 },
	{ 5200,  40, 0 },
	{ 5220,  44, 0 },
	{ 5240,  48, 0 },
	{ 5260,  52, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5280,  56, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5300,  60, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5320,  64, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5500, 100, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5520, 104, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5540, 108, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5560, 112, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5580, 116, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5600, 120, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5620, 124, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5640, 128, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5660, 132, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5680, 136, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5700, 140, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5720, 144, HI5622_BAND5G_CHAN_RADAR | HI5622_BAND5G_CHAN_NO_IR },
	{ 5745, 149, 0 },
	{ 5765, 153, 0 },
	{ 5785, 157, 0 },
	{ 5805, 161, 0 },
	{ 5825, 165, 0 },
	{ 4920, 184, HI5622_BAND5G_CHAN_NO_IR },
	{ 4940, 188, HI5622_BAND5G_CHAN_NO_IR },
	{ 4960, 192, HI5622_BAND5G_CHAN_NO_IR },
	{ 4980, 196, HI5622_BAND5G_CHAN_NO_IR },
};

const struct hi5622_band5g_chan *hi5622_band5g_channels(u8 band, u16 *count)
{
	if (band == HI5622_BAND5G_2GHZ) {
		if (count)
			*count = (u16)(sizeof(hi5622_band5g_2g) /
				       sizeof(hi5622_band5g_2g[0]));
		return hi5622_band5g_2g;
	}
	if (band == HI5622_BAND5G_5GHZ) {
		if (count)
			*count = (u16)(sizeof(hi5622_band5g_5g) /
				       sizeof(hi5622_band5g_5g[0]));
		return hi5622_band5g_5g;
	}
	if (count)
		*count = 0;
	return NULL;
}

int hi5622_band5g_chan_count(u8 band)
{
	u16 n = 0;

	hi5622_band5g_channels(band, &n);
	return (int)n;
}

int hi5622_band5g_freq_to_chan(u8 band, u16 freq, u8 *out_chan)
{
	const struct hi5622_band5g_chan *tbl;
	u16 n = 0, i;

	if (!out_chan)
		return -EINVAL;
	tbl = hi5622_band5g_channels(band, &n);
	if (!tbl || !n)
		return -EINVAL;
	if (!freq)
		return -ENOENT;
	for (i = 0; i < n; i++) {
		if (tbl[i].freq == freq) {
			*out_chan = tbl[i].num;
			return 0;
		}
	}
	return -ENOENT;
}

int hi5622_band5g_chan_to_freq(u8 band, u8 chan, u16 *out_freq)
{
	const struct hi5622_band5g_chan *tbl;
	u16 n = 0, i;

	if (!out_freq)
		return -EINVAL;
	tbl = hi5622_band5g_channels(band, &n);
	if (!tbl || !n)
		return -EINVAL;
	for (i = 0; i < n; i++) {
		if (tbl[i].num == chan) {
			*out_freq = tbl[i].freq;
			return 0;
		}
	}
	return -ENOENT;
}

int hi5622_band5g_chan_index(u8 band, u8 chan, u8 *out_idx)
{
	const struct hi5622_band5g_chan *tbl;
	u16 n = 0, i;

	if (!out_idx)
		return -EINVAL;
	tbl = hi5622_band5g_channels(band, &n);
	if (!tbl || !n)
		return -EINVAL;
	for (i = 0; i < n; i++) {
		if (tbl[i].num == chan) {
			if (i > 0xffu)
				return -EINVAL;
			*out_idx = (u8)i;
			return 0;
		}
	}
	return -ENOENT;
}

int hi5622_band5g_chan_valid(u8 band, u8 chan)
{
	const struct hi5622_band5g_chan *tbl;
	u16 n = 0, i;

	tbl = hi5622_band5g_channels(band, &n);
	if (!tbl || !n)
		return 0;
	for (i = 0; i < n; i++) {
		if (tbl[i].num == chan)
			return 1;
	}
	return 0;
}

int hi5622_band5g_ap_chan_prepare(u32 band5g_enable, u8 band, u16 center_freq,
				  u8 channel,
				  struct hi5622_band5g_ap_chan *out)
{
	u8 chan = channel;
	int rc;

	if (!out)
		return -EINVAL;
	if (band > HI5622_BAND5G_5GHZ)
		return -EINVAL;

	/* Fail closed: the 5 GHz AP path is only reachable with band5g=1. */
	if (band == HI5622_BAND5G_5GHZ && !band5g_enable)
		return -EOPNOTSUPP;

	if (!chan) {
		if (!center_freq)
			return -EINVAL;
		rc = hi5622_band5g_freq_to_chan(band, center_freq, &chan);
		if (rc)
			return rc;
	} else if (center_freq) {
		u8 derived = 0;

		rc = hi5622_band5g_freq_to_chan(band, center_freq, &derived);
		if (rc)
			return rc;
		if (derived != chan)
			return -EINVAL;
	}

	if (!hi5622_band5g_chan_valid(band, chan))
		return -EINVAL;
	out->band = band;
	out->channel = chan;
	return 0;
}

int hi5622_band5g_band_cfg_chan(u32 band5g_enable, u8 band, u16 freq,
				u16 *out_chan)
{
	u8 chan = 0;
	int rc;

	if (!out_chan)
		return -EINVAL;
	if (band > HI5622_BAND5G_5GHZ)
		return -EINVAL;
	if (band == HI5622_BAND5G_5GHZ && !band5g_enable)
		return -EOPNOTSUPP;
	rc = hi5622_band5g_freq_to_chan(band, freq, &chan);
	if (rc)
		return rc;
	*out_chan = chan;
	return 0;
}

u16 hi5622_band5g_ht_cap_word(u8 band)
{
	/* 2.4 GHz: the bench-validated baseline word (SGI20 | MAX_AMSDU);
	 * 5 GHz: vendor 0x0062 = SUP_WIDTH_20_40 | SGI_20 | SGI_40. */
	return band == HI5622_BAND5G_5GHZ ? 0x0062u : 0x0820u;
}

u32 hi5622_band5g_vht_cap_word(u8 band)
{
	if (band == HI5622_BAND5G_2GHZ)
		return 0x00400000u;	/* HTC_VHT only */
	if (band == HI5622_BAND5G_5GHZ)
		return 0x00400064u;	/* HTC_VHT|160 MHz|SGI80|SGI160 */
	return 0xffffffffu;
}

u16 hi5622_band5g_vht_mcs_map(u8 band)
{
	if (band > HI5622_BAND5G_5GHZ)
		return 0xffffu;
	return 0xfffau;		/* 2 SS, MCS 0..9, rx and tx */
}

u8 hi5622_band5g_max_power(u8 band)
{
	return band == HI5622_BAND5G_5GHZ ? 23u : 30u;
}

const char *hi5622_band5g_name(u8 band)
{
	return band == HI5622_BAND5G_5GHZ ? "5GHz" : "2.4GHz";
}

/* ---------------------------------------------------------------------
 * One-radio/two-band wiphy plan (lane wifi-5ghz-bringup, task-154)
 * -------------------------------------------------------------------*/
int hi5622_band5g_wiphy_bands(u32 band5g_enable, int pci_domain,
			      u8 *out_bands, unsigned int max,
			      unsigned int *out_n)
{
	if (!out_bands || !out_n || pci_domain < 0)
		return -EINVAL;
	*out_n = 0;

	/* band5g=0: preserve the bench-validated per-endpoint single-band model
	 * (the same rule as hi5622_cfg11_pick_band()'s domain auto value). */
	if (!band5g_enable) {
		if (max < 1u)
			return -ENOSPC;
		out_bands[0] = (pci_domain == 1) ? HI5622_BAND5G_5GHZ :
						      HI5622_BAND5G_2GHZ;
		*out_n = 1u;
		return 0;
	}

	/* band5g=1: only the primary endpoint carries a wiphy; it carries both
	 * bands because the chip is one radio with two band tables.  The second
	 * host view has no wiphy: there is no second HCC service to drive one
	 * (see REPORT.md "one chip, two host views"). */
	if (pci_domain != 0)
		return 0;
	if (max < HI5622_BAND5G_MAX_WIPHY_BANDS)
		return -ENOSPC;
	out_bands[0] = HI5622_BAND5G_2GHZ;
	out_bands[1] = HI5622_BAND5G_5GHZ;
	*out_n = HI5622_BAND5G_MAX_WIPHY_BANDS;
	return 0;
}

int hi5622_band5g_freq_band(u16 freq)
{
	u8 band;

	for (band = 0; band <= HI5622_BAND5G_5GHZ; band++) {
		const struct hi5622_band5g_chan *tbl;
		u16 n = 0, i;

		tbl = hi5622_band5g_channels(band, &n);
		if (!tbl || !n)
			continue;
		for (i = 0; i < n; i++)
			if (tbl[i].freq == freq)
				return (int)band;
	}
	return -ENOENT;
}
