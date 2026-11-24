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
 * What is registered by default (caps 0x0f):
 *   g0 id7 user_add, g0 id8 user_add_notify_alg, g0 id9 del_user,
 *   g0 id0xC scan_req builders.
 * With d2h_scan_decode=1 the caps gain SCAN_PARSE, which is what makes the
 * cfg80211 .scan op available (it requires SCAN_REQ|SCAN_PARSE); the decoder
 * is tolerant and reports signal 0 mBm because the g1 id15 trailing descriptor
 * is [U].  With the default 0 the scan op stays -EOPNOTSUPP: no speculative
 * BSS entries.
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

/* Default 0: the g1 id15 trailing-descriptor layout is [U]; enabling this is a
 * supervised bench decision, not a default behaviour. */
static unsigned int hi5622_scan_d2h_decode;
module_param_named(d2h_scan_decode, hi5622_scan_d2h_decode, uint, 0644);
MODULE_PARM_DESC(d2h_scan_decode,
		 "enable the best-effort g1 id15 scan-result decoder (default 0: .scan stays -EOPNOTSUPP; signal reported as 0 mBm because the trailer is [U])");

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
		HI5622_FRAME_CAP_USER_ADD |
		HI5622_FRAME_CAP_USER_ADD_NOTIFY |
		HI5622_FRAME_CAP_USER_DEL;
	if (hi5622_scan_d2h_decode)
		hi5622_vapscan_ops.caps |= HI5622_FRAME_CAP_SCAN_PARSE;
	hi5622_vapscan_ops.build_scan_req = vapscan_scan_req;
	hi5622_vapscan_ops.parse_scan_result = vapscan_parse_scan_result;
	hi5622_vapscan_ops.build_user_add = vapscan_user_add;
	hi5622_vapscan_ops.build_user_add_notify = vapscan_user_add_notify;
	hi5622_vapscan_ops.build_user_del = vapscan_user_del;
	/* AP start/stop: no builder (task-110 scope) - capabilities omitted. */

	rc = hi5622_frame_ops_register(&hi5622_vapscan_ops);
	if (!rc) {
		hi5622_vapscan_registered = true;
		pr_info("hi5622 frames: provider '%s' registered (caps=0x%x, d2h_scan_decode=%u)\n",
			hi5622_vapscan_ops.name, hi5622_vapscan_ops.caps,
			hi5622_scan_d2h_decode);
	}
	mutex_unlock(&hi5622_vapscan_lock);
	return rc;
}
