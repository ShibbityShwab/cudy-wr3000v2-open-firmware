// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_bss_color.c - portable builder/policy for the vendor g0 id36
 * hmac_scan_config_bss_color frame + the fail-closed AP-colour posting path.
 *
 * ORIGINAL clean-room code (lane wifi-bss-color, shared task-233). Evidence:
 * ../REPORT.md (decode), ../TABLES.md (T1 layout, T3 KAT). No vendor byte is
 * reproduced; the wire constants are derived values with evidence markers.
 */
#include "hi5622_bss_color.h"

#ifdef __KERNEL__
#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include <net/cfg80211.h>
#include "hi5622_driver.h"
#include "hi5622_frame_post.h"
#else
#include <string.h>
#endif

static void hbc_put_le16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void hbc_put_le64(u8 *p, u64 v)
{
	unsigned int i;

	for (i = 0; i < 8u; i++)
		p[i] = (u8)((v >> (8u * i)) & 0xffu);
}

static u16 hbc_get_le16(const u8 *p)
{
	return (u16)((u16)p[0] | ((u16)p[1] << 8));
}

static u64 hbc_get_le64(const u8 *p)
{
	u64 v = 0;
	unsigned int i;

	for (i = 0; i < 8u; i++)
		v |= (u64)p[i] << (8u * i);
	return v;
}

int hi5622_bss_color_policy(unsigned int value)
{
	if (value == HI5622_BSS_COLOR_PARAM_OFF)
		return (int)HI5622_BSS_COLOR_PARAM_OFF;
	if (value == HI5622_BSS_COLOR_PARAM_ON)
		return (int)HI5622_BSS_COLOR_PARAM_ON;
	/* Never guess: an out-of-contract value is refused (no frame). */
	return -EINVAL;
}

int hi5622_bss_color_mask_from_color(u8 color, u64 *mask_out)
{
	if (!mask_out)
		return -EINVAL;
	if (color == 0u || color > HI5622_BSS_COLOR_MAX)
		return -EINVAL;
	*mask_out = (u64)1u << color;
	return 0;
}

int hi5622_bss_color_build(u8 *wire, size_t cap, u8 fa, u8 fb, u8 bid_hi,
			   u64 mask, size_t *len_out)
{
	if (!wire || !len_out || cap < HI5622_BSS_COLOR_TOTAL_LEN)
		return -EINVAL;
	if (bid_hi > 0x0fu)
		return -EINVAL;
	memset(wire, 0, HI5622_BSS_COLOR_TOTAL_LEN);
	/* header: group-nibble 0 (low), direct id 0x24, transport tag. */
	wire[HI5622_BSS_COLOR_OFF_GROUP] = (u8)HI5622_BSS_COLOR_GROUP;
	wire[HI5622_BSS_COLOR_OFF_BID] =
		(u8)(HI5622_BSS_COLOR_ALLOC_NIBBLE | (bid_hi << 4));
	wire[HI5622_BSS_COLOR_OFF_FA] = fa;
	wire[HI5622_BSS_COLOR_OFF_FB] = fb;
	hbc_put_le16(&wire[HI5622_BSS_COLOR_OFF_LEN],
		     (u16)HI5622_BSS_COLOR_TOTAL_LEN);
	hbc_put_le16(&wire[HI5622_BSS_COLOR_OFF_ID],
		     (u16)HI5622_BSS_COLOR_ID);
	wire[HI5622_BSS_COLOR_OFF_FC] = 0u;
	wire[HI5622_BSS_COLOR_OFF_DST] = 0u;
	hbc_put_le16(&wire[HI5622_BSS_COLOR_OFF_TAG],
		     (u16)HI5622_BSS_COLOR_TAG);
	hbc_put_le64(&wire[HI5622_BSS_COLOR_OFF_MASK], mask);
	*len_out = HI5622_BSS_COLOR_TOTAL_LEN;
	return 0;
}

int hi5622_bss_color_validate(const u8 *wire, size_t len, u64 *mask_out)
{
	if (!wire || len != HI5622_BSS_COLOR_TOTAL_LEN)
		return -EINVAL;
	if ((wire[HI5622_BSS_COLOR_OFF_GROUP] & 0x0fu) !=
	    HI5622_BSS_COLOR_GROUP)
		return -EINVAL;
	if ((wire[HI5622_BSS_COLOR_OFF_BID] & 0x0fu) !=
	    HI5622_BSS_COLOR_ALLOC_NIBBLE)
		return -EINVAL;
	if (hbc_get_le16(&wire[HI5622_BSS_COLOR_OFF_LEN]) !=
	    HI5622_BSS_COLOR_TOTAL_LEN)
		return -EINVAL;
	if (hbc_get_le16(&wire[HI5622_BSS_COLOR_OFF_ID]) !=
	    HI5622_BSS_COLOR_ID)
		return -EINVAL;
	if (wire[HI5622_BSS_COLOR_OFF_FC] != 0u ||
	    wire[HI5622_BSS_COLOR_OFF_DST] != 0u)
		return -EINVAL;
	if (hbc_get_le16(&wire[HI5622_BSS_COLOR_OFF_TAG]) !=
	    HI5622_BSS_COLOR_TAG)
		return -EINVAL;
	if (mask_out)
		*mask_out = hbc_get_le64(&wire[HI5622_BSS_COLOR_OFF_MASK]);
	return 0;
}

#ifdef __KERNEL__
/* Fail-closed module knob: 0 = off (default, no device write at all),
 * 1 = post the start_ap HE BSS colour as one g0 id36 frame. */
unsigned int hi5622_bss_color;
module_param_named(bss_color, hi5622_bss_color, uint, 0644);
MODULE_PARM_DESC(bss_color,
		 "AP BSS-color posting: 0=off (default, fail closed, no g0 id36 frame), 1=post the start_ap cfg80211 HE BSS colour (NL80211_ATTR_HE_BSS_COLOR, hostapd he_bss_color=N) as one g0 id36 hmac_scan_config_bss_color bitmap after the AP config; >1 refused (no frame)");

int hi5622_bss_color_post_ap(struct hi5622_pci *ctx, struct device *dev,
			     const struct cfg80211_ap_settings *settings,
			     u8 band)
{
	u8 wire[HI5622_BSS_COLOR_TOTAL_LEN];
	const struct cfg80211_he_bss_color *hc;
	u64 mask = 0;
	size_t len = 0;
	int rc;

	if (!ctx || !dev || !settings)
		return -EINVAL;
	rc = hi5622_bss_color_policy(hi5622_bss_color);
	if (rc < 0) {
		dev_warn_ratelimited(dev,
				     "cfg80211: bss_color=%u refused (0=off, 1=post AP BSS colour); no g0 id36 frame\n",
				     hi5622_bss_color);
		return rc;
	}
	if (rc == (int)HI5622_BSS_COLOR_PARAM_OFF)
		return 0;
	hc = &settings->beacon.he_bss_color;
	if (!settings->beacon.he_bss_color_valid || !hc->enabled) {
		dev_warn_ratelimited(dev,
				     "cfg80211: bss_color=1 but start_ap carried no enabled HE BSS colour (hostapd he_bss_color unset?); no g0 id36 frame\n");
		return -EOPNOTSUPP;
	}
	rc = hi5622_bss_color_mask_from_color(hc->color, &mask);
	if (rc) {
		dev_warn_ratelimited(dev,
				     "cfg80211: bss_color=1 but start_ap HE BSS colour %u is outside the 1..63 contract; no g0 id36 frame\n",
				     hc->color);
		return rc;
	}
	rc = hi5622_bss_color_build(wire, sizeof(wire), 0u, band, 0u, mask, &len);
	if (rc)
		return rc;
	rc = hi5622_frame_post(ctx, wire, len, "bss_color", NULL);
	if (rc) {
		dev_warn(dev,
			 "cfg80211: BSS Color %u frame (g0 id36, mask 0x%016llx, chip=%u) NOT posted (%d) - AP start continues\n",
			 hc->color, (unsigned long long)mask, (unsigned)band, rc);
		return rc;
	}
	dev_info(dev,
		 "cfg80211: BSS Color %u posted to firmware (g0 id36 hmac_scan_config_bss_color, bitmap 0x%016llx, chip=%u, %zu B)%s\n",
		 hc->color, (unsigned long long)mask, (unsigned)band, len,
		 hc->partial ? " [partial flag ignored: not in the wire bitmap]" : "");
	return 0;
}
#endif /* __KERNEL__ */
