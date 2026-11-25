/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_vapmsg5g.h - fail-closed vendor-order planner for the 5 GHz AP's
 * user/VAP messages (Hi5622V100 clean-room host, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-5ghz-vap-msg, shared task task-174,
 * 2026-11-25).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced here.  The derivation (vendor module
 * disassembly addresses, the wire layouts, the firmware-side reasoning) is in
 * ../REPORT.md; the evidence tables are ../TABLES.md.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The vendor's cfg80211 .add_virtual_intf is a stub (-EOPNOTSUPP): the vendor
 * never posts a g0 id7/id8 user add from interface creation.  Its AP VAP is
 * created (band id at parameter +0x12, chip at +0x13) before its users
 * (hmac_vap_add_multi_user), and the AP start (start_ap) drives the five
 * vendor frames with the band taken from the per-band wiphy private area
 * (*(u8 *)(dev_priv+0x328)).
 *
 * Our one-radio/two-band driver (band5g=1) has a single wiphy carrying both
 * bands, so the band of a newly created AP interface is NOT known at
 * .add_virtual_intf time (cfg80211 supplies no chandef there).  Posting the
 * user add at interface creation therefore puts a band-less user message in
 * front of the band-1 VAP that the firmware resolves it against: on a fresh
 * chip the 2-band AP's user add wedges (announce not consumed -> h2d_timeout
 * -> H2D lane closed -> the AP-start vap_add is refused -> firmware exception
 * mask 0x10).  The 2.4 GHz single-band path with the same bytes is
 * bench-validated; it tolerates the legacy ordering.
 *
 * The fix is an ORDER fix, not a field fix (the g0 id7/id8 bodies our builder
 * emits are already the vendor bytes for band 0, and the band is carried only
 * by the VAP-create/channel messages).  This file holds that order as a pure,
 * host-testable decision so the bench rung can be gated without any frame on
 * the wire:
 *
 *   vap5g=0 (default, fail closed) = legacy ordering, byte-identical to the
 *   bench-validated module; vap5g=1 = vendor ordering for an AP on a
 *   multi-band wiphy: defer the interface-creation user add and post it
 *   AFTER the AP's structural vap_add (band known + firmware VAP exists).
 *
 * Anything outside the decoded contract fails closed: an out-of-range mode is
 * refused (-EINVAL) instead of guessing, a single-band wiphy or a non-AP
 * interface keeps the legacy path, and a deferred user add that fails aborts
 * start_ap before any AP frame.
 *
 * Pure C99: the same code compiles in the kernel module and in the host
 * selftest (tools/vapmsg5g_selftest.c).
 */
#ifndef HI5622_VAPMSG5G_H
#define HI5622_VAPMSG5G_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef int32_t  s32;
#endif
#endif

#define HI5622_VAPMSG5G_MODE_LEGACY		0u
#define HI5622_VAPMSG5G_MODE_VENDOR_ORDER	1u

/* nl80211 iftype the planner cares about (the only one that has a firmware
 * VAP create in our start_ap path). */
#define HI5622_VAPMSG5G_IFTYPE_AP		3u

/* The wire order of one AP start, as the firmware sees it.  The legacy order
 * starts with the interface-creation user add; the vendor order puts the VAP
 * create first and the user add after it. */
enum hi5622_vapmsg5g_step {
	HI5622_VAPMSG5G_STEP_VAP_ADD = 0,
	HI5622_VAPMSG5G_STEP_USER_ADD,
	HI5622_VAPMSG5G_STEP_USER_ADD_NOTIFY,
	HI5622_VAPMSG5G_STEP_CHAN_CFG,
	HI5622_VAPMSG5G_STEP_SSID,
	HI5622_VAPMSG5G_STEP_AUTH,
	HI5622_VAPMSG5G_STEP_AP_CFG,
	HI5622_VAPMSG5G_STEP_VAP_START,
	HI5622_VAPMSG5G_STEP_MAX
};

/* What the caller must do for one AP interface. */
struct hi5622_vapmsg5g_plan {
	bool	defer_user_add;			/* skip the add_interface user add */
	bool	post_user_after_vap_add;	/* post it after the AP VAP add */
};

/* Fill *out with the ordering decision.
 *  - 0 on success; out->defer/out->post are false for the legacy path;
 *  - -EINVAL for a NULL out or mode > 1 (fail closed, never guess).
 * A single-band wiphy or a non-AP iftype always keeps the legacy path (the
 * mode is only meaningful on the one-radio/two-band profile). */
int hi5622_vapmsg5g_iface_plan(unsigned int mode, unsigned int wiphy_bands,
			       unsigned int iftype,
			       struct hi5622_vapmsg5g_plan *out);

/* Fill steps[] with the wire order this mode produces (for host tests and for
 * the fire recipe's dmesg assertion); *n receives the step count.
 *  - 0 on success, -EINVAL for bad arguments / mode > 1,
 *  - -ENOSPC when max is smaller than the produced order. */
int hi5622_vapmsg5g_order(unsigned int mode, unsigned int wiphy_bands,
			  unsigned int iftype, u8 *steps, unsigned int max,
			  unsigned int *n);

/* Stable names for the order steps ("vap_add", "user_add", ...). */
const char *hi5622_vapmsg5g_step_name(u8 step);

/* "legacy" / "vendor-order" / "invalid". */
const char *hi5622_vapmsg5g_mode_name(unsigned int mode);

#endif /* HI5622_VAPMSG5G_H */
