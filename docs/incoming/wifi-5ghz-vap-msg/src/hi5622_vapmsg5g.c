// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_vapmsg5g.c - fail-closed vendor-order planner for the 5 GHz AP's
 * user/VAP messages.  See hi5622_vapmsg5g.h for the design and ../REPORT.md
 * for the vendor decode this encodes.
 *
 * ORIGINAL clean-room code (lane wifi-5ghz-vap-msg, shared task task-174).
 */
#include "hi5622_vapmsg5g.h"

#ifndef __KERNEL__
#include <stddef.h>
#endif

const char *hi5622_vapmsg5g_mode_name(unsigned int mode)
{
	switch (mode) {
	case HI5622_VAPMSG5G_MODE_LEGACY:
		return "legacy";
	case HI5622_VAPMSG5G_MODE_VENDOR_ORDER:
		return "vendor-order";
	default:
		return "invalid";
	}
}

const char *hi5622_vapmsg5g_step_name(u8 step)
{
	switch (step) {
	case HI5622_VAPMSG5G_STEP_VAP_ADD:
		return "vap_add";
	case HI5622_VAPMSG5G_STEP_USER_ADD:
		return "user_add";
	case HI5622_VAPMSG5G_STEP_USER_ADD_NOTIFY:
		return "user_add_notify";
	case HI5622_VAPMSG5G_STEP_CHAN_CFG:
		return "ap_chan";
	case HI5622_VAPMSG5G_STEP_SSID:
		return "ap_ssid";
	case HI5622_VAPMSG5G_STEP_AUTH:
		return "ap_auth";
	case HI5622_VAPMSG5G_STEP_AP_CFG:
		return "ap_cfg";
	case HI5622_VAPMSG5G_STEP_VAP_START:
		return "ap_vap_start";
	default:
		return "invalid";
	}
}

/* The decoded rule.  Only this one shape changes the order: a multi-band
 * (one-radio/two-band) wiphy, an AP interface, vendor-order mode.  Everything
 * else - including an invalid mode - keeps the callers on the legacy path;
 * mode validation happens in hi5622_vapmsg5g_iface_plan(), and the driver
 * treats its -EINVAL as fail-closed. */
static bool hi5622_vapmsg5g_vendor_order(unsigned int mode,
					 unsigned int wiphy_bands,
					 unsigned int iftype)
{
	return mode == HI5622_VAPMSG5G_MODE_VENDOR_ORDER &&
	       wiphy_bands > 1u &&
	       iftype == HI5622_VAPMSG5G_IFTYPE_AP;
}

int hi5622_vapmsg5g_iface_plan(unsigned int mode, unsigned int wiphy_bands,
			       unsigned int iftype,
			       struct hi5622_vapmsg5g_plan *out)
{
	bool vendor;

	if (!out)
		return -EINVAL;
	out->defer_user_add = false;
	out->post_user_after_vap_add = false;
	/* Fail closed on an unknown mode: the caller must not fall back to a
	 * path it does not understand. */
	if (mode > HI5622_VAPMSG5G_MODE_VENDOR_ORDER)
		return -EINVAL;
	vendor = hi5622_vapmsg5g_vendor_order(mode, wiphy_bands, iftype);
	out->defer_user_add = vendor;
	out->post_user_after_vap_add = vendor;
	return 0;
}

int hi5622_vapmsg5g_order(unsigned int mode, unsigned int wiphy_bands,
			  unsigned int iftype, u8 *steps, unsigned int max,
			  unsigned int *n)
{
	u8 legacy[] = {
		HI5622_VAPMSG5G_STEP_USER_ADD,
		HI5622_VAPMSG5G_STEP_USER_ADD_NOTIFY,
		HI5622_VAPMSG5G_STEP_VAP_ADD,
		HI5622_VAPMSG5G_STEP_CHAN_CFG,
		HI5622_VAPMSG5G_STEP_SSID,
		HI5622_VAPMSG5G_STEP_AUTH,
		HI5622_VAPMSG5G_STEP_AP_CFG,
		HI5622_VAPMSG5G_STEP_VAP_START,
	};
	u8 vendor[] = {
		HI5622_VAPMSG5G_STEP_VAP_ADD,
		HI5622_VAPMSG5G_STEP_USER_ADD,
		HI5622_VAPMSG5G_STEP_USER_ADD_NOTIFY,
		HI5622_VAPMSG5G_STEP_CHAN_CFG,
		HI5622_VAPMSG5G_STEP_SSID,
		HI5622_VAPMSG5G_STEP_AUTH,
		HI5622_VAPMSG5G_STEP_AP_CFG,
		HI5622_VAPMSG5G_STEP_VAP_START,
	};
	const u8 *src;
	unsigned int cnt, i;
	struct hi5622_vapmsg5g_plan plan;
	int rc;

	if (!steps || !n)
		return -EINVAL;
	if (mode > HI5622_VAPMSG5G_MODE_VENDOR_ORDER)
		return -EINVAL;
	rc = hi5622_vapmsg5g_iface_plan(mode, wiphy_bands, iftype, &plan);
	if (rc)
		return rc;
	if (plan.defer_user_add) {
		src = vendor;
		cnt = (unsigned int)sizeof(vendor);
	} else {
		src = legacy;
		cnt = (unsigned int)sizeof(legacy);
	}
	if (max < cnt)
		return -ENOSPC;
	for (i = 0; i < cnt; i++)
		steps[i] = src[i];
	*n = cnt;
	return 0;
}
