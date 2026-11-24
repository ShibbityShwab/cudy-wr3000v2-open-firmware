// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_sta_logic.c - portable STA/band/HT capability logic (clean-room).
 * See hi5622_sta_logic.h and ../REPORT.md for the decoded facts.
 */
#include "hi5622_sta_logic.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#else
#include <errno.h>
#endif

/* Vendor 2.4 GHz channel table (.data+0x16e4, 14 x 56 B): ch1..13 + ch14. */
static const struct hi5622_sta_chan hi5622_sta_chan_2g[] = {
	{ 2412, 1,  0 }, { 2417, 2,  0 }, { 2422, 3,  0 }, { 2427, 4,  0 },
	{ 2432, 5,  0 }, { 2437, 6,  0 }, { 2442, 7,  0 }, { 2447, 8,  0 },
	{ 2452, 9,  0 }, { 2457, 10, 0 }, { 2462, 11, 0 }, { 2467, 12, 0 },
	{ 2472, 13, 0 }, { 2484, 14, 0 },
};

/* Vendor 5 GHz channel table (.data+0x108c, 29 x 56 B).  Flags are 0 in the
 * vendor table; the 4.9 GHz block (4920..4980) is marked NO_IR here because
 * no kernel regulatory domain enables it for initiator use. */
static const struct hi5622_sta_chan hi5622_sta_chan_5g[] = {
	{ 5180,  36,  0 }, { 5200,  40,  0 }, { 5220,  44,  0 },
	{ 5240,  48,  0 }, { 5260,  52,  HI5622_STA_CHAN_RADAR },
	{ 5280,  56,  HI5622_STA_CHAN_RADAR }, { 5300,  60, HI5622_STA_CHAN_RADAR },
	{ 5320,  64,  HI5622_STA_CHAN_RADAR }, { 5500, 100, HI5622_STA_CHAN_RADAR },
	{ 5520, 104, HI5622_STA_CHAN_RADAR }, { 5540, 108, HI5622_STA_CHAN_RADAR },
	{ 5560, 112, HI5622_STA_CHAN_RADAR }, { 5580, 116, HI5622_STA_CHAN_RADAR },
	{ 5600, 120, HI5622_STA_CHAN_RADAR }, { 5620, 124, HI5622_STA_CHAN_RADAR },
	{ 5640, 128, HI5622_STA_CHAN_RADAR }, { 5660, 132, HI5622_STA_CHAN_RADAR },
	{ 5680, 136, HI5622_STA_CHAN_RADAR }, { 5700, 140, HI5622_STA_CHAN_RADAR },
	{ 5720, 144, HI5622_STA_CHAN_RADAR }, { 5745, 149, 0 },
	{ 5765, 153, 0 }, { 5785, 157, 0 }, { 5805, 161, 0 }, { 5825, 165, 0 },
	{ 4920, 184, HI5622_STA_CHAN_NO_IR }, { 4940, 188, HI5622_STA_CHAN_NO_IR },
	{ 4960, 192, HI5622_STA_CHAN_NO_IR }, { 4980, 196, HI5622_STA_CHAN_NO_IR },
};

const struct hi5622_sta_chan *hi5622_sta_band_channels(u8 band, u16 *count)
{
	if (band == 0u) {
		if (count)
			*count = (u16)(sizeof(hi5622_sta_chan_2g) /
				       sizeof(hi5622_sta_chan_2g[0]));
		return hi5622_sta_chan_2g;
	}
	if (band == 1u) {
		if (count)
			*count = (u16)(sizeof(hi5622_sta_chan_5g) /
				       sizeof(hi5622_sta_chan_5g[0]));
		return hi5622_sta_chan_5g;
	}
	if (count)
		*count = 0;
	return (const struct hi5622_sta_chan *)0;
}

u16 hi5622_sta_ht_cap_word(u32 ht_mode)
{
	switch (ht_mode) {
	case HI5622_STA_HT_MODE_BASELINE:
		return HI5622_STA_HT_CAP_BASELINE;
	case HI5622_STA_HT_MODE_VENDOR20:
		return HI5622_STA_HT_CAP_SGI20;
	case HI5622_STA_HT_MODE_VENDOR40:
		return HI5622_STA_HT_CAP_VENDOR;
	default:
		return 0xffffu;
	}
}

int hi5622_sta_ht_mode_is_40(u32 ht_mode)
{
	return ht_mode == HI5622_STA_HT_MODE_VENDOR40 ? 1 : 0;
}

u32 hi5622_sta_vht_cap_word(u8 band)
{
	if (band == 0u)
		return HI5622_STA_VHT_CAP_2G;
	if (band == 1u)
		return HI5622_STA_VHT_CAP_5G;
	return 0xffffffffu;
}

int hi5622_sta_auth_type_valid(u32 auth_type)
{
	/* nl80211_auth_type: OPEN=0 .. AUTOMATIC=8 (vendor passes the value
	 * straight into the connect body byte +17, wal_cfg80211_connect [V]). */
	return auth_type <= 8u ? 1 : 0;
}

int hi5622_sta_connect_check(const struct hi5622_sta_connect_check *p)
{
	if (!p)
		return -EINVAL;
	if (!p->ssid || p->ssid_len == 0u || p->ssid_len > HI5622_STA_MAX_SSID)
		return -EINVAL;
	if (p->ie_len > HI5622_STA_MAX_IE)
		return -E2BIG;
	if (p->key_len > HI5622_STA_MAX_KEY)
		return -E2BIG;
	if (!hi5622_sta_auth_type_valid(p->auth_type))
		return -EINVAL;
	return 0;
}
