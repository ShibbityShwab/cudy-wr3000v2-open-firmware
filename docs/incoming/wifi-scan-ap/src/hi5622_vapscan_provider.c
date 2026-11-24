// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_vapscan_provider.c - kernel adapter that registers the task-110
 * wifi-vap-scan frame builders as this module's H2D frame provider, plus the
 * module parameter that enables the best-effort [U] D2H scan-result decode.
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-25).  Builders: wifi-vap-scan lane (task-110, commit 0e4ad45).
 * No vendor source, object code, firmware byte or secret is reproduced.
 *
 * Registered caps (0x1f):
 *   g0 id7 user_add, g0 id8 user_add_notify_alg, g0 id9 del_user,
 *   g0 id0xC scan_req builders + SCAN_PARSE.
 * SCAN_PARSE is always advertised because the cfg80211 .scan op requires
 * SCAN_REQ|SCAN_PARSE (0x3): a real handler that posts the 520-byte g0 id0xC
 * frame needs both.  The g1 id15 decoder is tolerant - it reports only what
 * the 802.11 body proves and returns -EOPNOTSUPP for undecodable variants, so
 * "scan posted, zero BSS entries, bounded scan_done" is a valid outcome.
 *
 * Registration is harmless: no frame can reach the wire unless the separate
 * hi5622_frame_post() gate has allow_frames>0 (default 0).
 *
 * AP start/stop builders are NOT provided by task-110 (WAL group-2 gap G1), so
 * those capabilities are intentionally absent -> start_ap/stop_ap remain
 * -EOPNOTSUPP.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "hi5622_frames_api.h"
#include "hi5622_vapscan_map.h"

static DEFINE_MUTEX(hi5622_vapscan_lock);
static bool hi5622_vapscan_registered;
static struct hi5622_frame_ops hi5622_vapscan_ops;

/* ------------------------------------------------------------------ */
/* provider callbacks (portable map -> task-110 builders)             */
/* ------------------------------------------------------------------ */
static int vapscan_scan_req(u8 *wire, size_t cap, size_t *out_len,
			    const struct hi5622_scan_req_params *p)
{
	struct vs_frame *f;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	rc = hi5622_vapscan_scan_req(f, p);
	if (!rc) {
		rc = vs_frame_export(f, wire, cap, out_len);
		rc = hi5622_vapscan_errno(rc);
	}
	kfree(f);
	return rc;
}

static int vapscan_user_add(u8 *wire, size_t cap, size_t *out_len,
			    const struct hi5622_vap_add_params *p)
{
	struct vs_frame *f;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	rc = hi5622_vapscan_user_add(f, p);
	if (!rc) {
		rc = vs_frame_export(f, wire, cap, out_len);
		rc = hi5622_vapscan_errno(rc);
	}
	kfree(f);
	return rc;
}

static int vapscan_user_add_notify(u8 *wire, size_t cap, size_t *out_len,
				   const struct hi5622_vap_add_params *p)
{
	struct vs_frame *f;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	rc = hi5622_vapscan_user_alg(f, p);
	if (!rc) {
		rc = vs_frame_export(f, wire, cap, out_len);
		rc = hi5622_vapscan_errno(rc);
	}
	kfree(f);
	return rc;
}

static int vapscan_user_del(u8 *wire, size_t cap, size_t *out_len,
			    const struct hi5622_vap_del_params *p)
{
	struct vs_frame *f;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	rc = hi5622_vapscan_user_del(f, p);
	if (!rc) {
		rc = vs_frame_export(f, wire, cap, out_len);
		rc = hi5622_vapscan_errno(rc);
	}
	kfree(f);
	return rc;
}

static int vapscan_parse_scan_result(const u8 *payload, u32 len,
				     struct hi5622_scan_result *out)
{
	return hi5622_vapscan_parse_scan_result(payload, len, out);
}

/* ------------------------------------------------------------------ */
/* registration                                                       */
/* ------------------------------------------------------------------ */
int hi5622_vapscan_provider_register(void)
{
	int rc;

	mutex_lock(&hi5622_vapscan_lock);
	if (hi5622_vapscan_registered) {
		mutex_unlock(&hi5622_vapscan_lock);
		return 0;
	}
	memset(&hi5622_vapscan_ops, 0, sizeof(hi5622_vapscan_ops));
	hi5622_vapscan_ops.name = HI5622_VAPSCAN_PROVIDER_NAME;
	hi5622_vapscan_ops.caps =
		HI5622_FRAME_CAP_SCAN_REQ |
		HI5622_FRAME_CAP_SCAN_PARSE |
		HI5622_FRAME_CAP_USER_ADD |
		HI5622_FRAME_CAP_USER_ADD_NOTIFY |
		HI5622_FRAME_CAP_USER_DEL;
	hi5622_vapscan_ops.build_scan_req = vapscan_scan_req;
	hi5622_vapscan_ops.parse_scan_result = vapscan_parse_scan_result;
	hi5622_vapscan_ops.build_user_add = vapscan_user_add;
	hi5622_vapscan_ops.build_user_add_notify = vapscan_user_add_notify;
	hi5622_vapscan_ops.build_user_del = vapscan_user_del;
	/* AP start/stop: no builder (task-110 scope) - capabilities omitted. */

	rc = hi5622_frame_ops_register(&hi5622_vapscan_ops);
	if (!rc) {
		hi5622_vapscan_registered = true;
		pr_info("hi5622 frames: provider '%s' registered (caps=0x%x)\n",
			hi5622_vapscan_ops.name, hi5622_vapscan_ops.caps);
	}
	mutex_unlock(&hi5622_vapscan_lock);
	return rc;
}
