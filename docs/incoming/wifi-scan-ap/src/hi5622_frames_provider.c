// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_frames_provider.c - registry for the H2D frame-builder provider
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-24).  No vendor source/object/firmware byte is reproduced.
 *
 * Integration contract (see ../REPORT.md sec 4): the payload builders for the
 * still-undecoded H2D messages (g0 id0xC scan request, g0 id7/id8 user add,
 * g0 id9 del, WAL group-2 AP start/stop) live in their own object - today the
 * wifi-vap-scan lane's pure-C library (opensource/docs/incoming/wifi-vap-scan/
 * src/vapscan_frames.c).  An adapter object implements struct
 * hi5622_frame_ops and calls hi5622_frame_ops_register() at module init.
 *
 * The registry is intentionally tiny and static: one provider, validated, no
 * lifetime ambiguity.  With nothing registered, all message ops return
 * -EOPNOTSUPP and hi5622_frame_post() is never reached.
 */
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>

#include "hi5622_frames_api.h"

static DEFINE_MUTEX(hi5622_provider_lock);
static const struct hi5622_frame_ops *hi5622_provider;

int hi5622_frame_ops_register(const struct hi5622_frame_ops *ops)
{
	int ret;

	ret = hi5622_frame_ops_validate(ops);
	if (ret) {
		pr_warn("hi5622 frames: provider registration refused (invalid ops table: %d)\n",
			ret);
		return ret;
	}
	mutex_lock(&hi5622_provider_lock);
	if (hi5622_provider) {
		mutex_unlock(&hi5622_provider_lock);
		pr_warn("hi5622 frames: provider '%s' already registered - refusing '%s'\n",
			hi5622_provider->name, ops->name);
		return -EEXIST;
	}
	hi5622_provider = ops;
	mutex_unlock(&hi5622_provider_lock);
	pr_info("hi5622 frames: provider '%s' registered (caps=0x%x)\n",
		ops->name, ops->caps);
	return 0;
}
EXPORT_SYMBOL_GPL(hi5622_frame_ops_register);

void hi5622_frame_ops_unregister(const struct hi5622_frame_ops *ops)
{
	mutex_lock(&hi5622_provider_lock);
	if (hi5622_provider == ops)
		hi5622_provider = NULL;
	mutex_unlock(&hi5622_provider_lock);
}
EXPORT_SYMBOL_GPL(hi5622_frame_ops_unregister);

const struct hi5622_frame_ops *hi5622_frame_ops_get(void)
{
	return READ_ONCE(hi5622_provider);
}
