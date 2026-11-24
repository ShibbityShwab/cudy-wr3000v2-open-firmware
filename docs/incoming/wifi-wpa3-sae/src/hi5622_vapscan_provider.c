// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_vapscan_provider.c - kernel adapter that registers the task-110
 * wifi-vap-scan frame builders as this module's H2D frame provider and wires
 * the vendor-verified task-118 scan D2H decoders.
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-25/26).  Builders: wifi-vap-scan lane (task-110, commit 0e4ad45).
 * D2H decoders: wifi-scan-d2h lane (task-118, commit 469b193), integrated
 * from src/hi5622_scan_d2h.{c,h}.  No vendor source, object code, firmware
 * byte or secret is reproduced.
 *
 * Registered caps (0x19f, wifi-vap-start task-117 + scan-complete task-123):
 *   g0 id7 user_add, g0 id8 user_add_notify_alg, g0 id9 del_user,
 *   g0 id0xC scan_req builders + SCAN_PARSE, WAL g2 VAP add/start
 *   (vap_start_mode 0 = WAL cfg events, 1 = g0 id1 keys 0x64/0x65).
 * SCAN_PARSE is always advertised because the cfg80211 .scan op requires
 * SCAN_REQ|SCAN_PARSE (0x3): a real handler that posts the 520-byte g0 id0xC
 * frame needs both.  The D2H decoders are vendor-verified and fail closed
 * (-EOPNOTSUPP, never a guessed result); g1 id16 status decides
 * aborted/success and g1 id17 / g2 id2 are decoded for completion/observability.
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
#include "vapstart_frames.h"
#include "hi5622_ap_frames.h"
#include "hi5622_key_frames.h"

#include <linux/nl80211.h>

static DEFINE_MUTEX(hi5622_vapscan_lock);
static bool hi5622_vapscan_registered;
static struct hi5622_frame_ops hi5622_vapscan_ops;

/* scan-complete lane (task-123): trailer-RSSI opt-in.  The g1 id15 20-byte
 * trailing descriptor carries two signed bytes (+5/+6) that the vendor host
 * copies into its BSS record; which one is the RSSI is [I] until bench
 * correlation.  Default 0 keeps cfg80211 signal = 0 (never invent a
 * measurement); a bench session may load the module with scan_trust_rssi=1
 * and compare the reported mBm against a known AP.  Read-only after load. */
unsigned int hi5622_scan_trust_rssi;
module_param_named(scan_trust_rssi, hi5622_scan_trust_rssi, uint, 0444);
MODULE_PARM_DESC(scan_trust_rssi,
		 "report cfg80211 signal from g1 id15 trailer byte +5 (0=off default, [I])");

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
	/* wifi-scan-d2h (task-118) tolerant layout probe: 802.11 body plus
	 * optional 20-byte trailer; never a fake BSS.  Signal is the documented
	 * 0 placeholder unless the bench opts into the trailer heuristic. */
	return hi5622_vapscan_decode_scan_result_opts(payload, len, out,
						      hi5622_scan_trust_rssi != 0u);
}

static int vapscan_parse_scan_complete(const u8 *payload, u32 len,
				       struct hi5622_scan_complete *out)
{
	/* g1 id16 hmac_scan_proc_scan_comp_event: status byte at payload[0],
	 * success == 1 is the only proven value -> status != 1 means aborted. */
	return hi5622_vapscan_decode_scan_complete(payload, len, out);
}

static int vapscan_parse_scan_channel(const u8 *payload, u32 len,
				      struct hi5622_scan_channel *out)
{
	/* g1 id17 hmac_scan_process_chan_result_event: fully decoded (slot,
	 * channel, frequency) for bench observability. */
	return hi5622_vapscan_decode_scan_channel(payload, len, out);
}

static int vapscan_parse_scan_sta_complete(const u8 *payload, u32 len,
					   struct hi5622_scan_sta_complete *out)
{
	/* g2 id2 wal_scan_comp_proc_sta: the vendor cfg80211 feed/completion
	 * path; status semantics [I]/[U], exposed for the bench log. */
	return hi5622_vapscan_decode_scan_sta_complete(payload, len, out);
}

/* ------------------------------------------------------------------ */
/* VAP start family (wifi-vap-start lane, task-117)                  */
/* ------------------------------------------------------------------ */

/* Ordering knob owned by the cfg80211 layer (hi5622_scan.c): post the
 * VAP add + start sequence before the first scan request.  Default 1 (shipped
 * image); the AP start path posts the base itself and the "once" guard keeps
 * a later scan from posting it twice. */
unsigned int hi5622_vap_start_first = 1u;
module_param_named(vap_start_first, hi5622_vap_start_first, uint, 0644);
MODULE_PARM_DESC(vap_start_first,
		 "post the VAP add+start sequence before a scan (1=on default, 0=off)");

/* Wire family: 0 = WAL group-2 cfg events (vendor order), 1 = g0 id1
 * config-event keys 0x64/0x65.  Read-only after load. */
unsigned int hi5622_vap_start_mode = 1u;
module_param_named(vap_start_mode, hi5622_vap_start_mode, uint, 0444);
MODULE_PARM_DESC(vap_start_mode,
		 "VAP start wire family: 0=WAL g2 vendor order, 1=g0 id1 keys (default 1)");

/* module/VAP index the start sequence applies to (<= 0x12). */
unsigned int hi5622_vap_start_module;
module_param_named(vap_start_module, hi5622_vap_start_module, uint, 0444);
MODULE_PARM_DESC(vap_start_module, "module/VAP index for the start sequence");

static u8 vapscan_iftype(u8 nl80211_iftype)
{
	return nl80211_iftype == NL80211_IFTYPE_AP ? VSF_IFTYPE_AP :
						     VSF_IFTYPE_STA;
}

static int vapscan_vap_add(u8 *wire, size_t cap, size_t *out_len,
			   const struct hi5622_vap_start_params *p)
{
	struct vsf_frame *f;
	int rc;

	if (!wire || !out_len || !p || p->module > VSF_MODULE_MAX ||
	    p->band > 1u || p->dst > 0x0fu)
		return -EINVAL;
	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	if (hi5622_vap_start_mode == 1u) {
		struct vsf_vap_add gp;

		vsf_vap_add_defaults(&gp);
		gp.iftype = vapscan_iftype(p->iftype);
		gp.module = p->module;
		/* Parent module index: use the module being created (the same
		 * pattern the bench-validated C2 ctx-create frame uses).  The
		 * firmware worker's parent lookup requires parent_ctx[1]==0, which
		 * holds for the target module's untouched static context; using the
		 * boot-created module 18 would depend on its inherited byte. */
		gp.parent = (u8)p->module;
		gp.band = p->band;
		gp.dst = p->dst;
		rc = vsf_build_vap_add(f, &gp);
	} else {
		struct vsf_wal_vap_add wp;

		vsf_wal_vap_add_defaults(&wp);
		wp.iftype = vapscan_iftype(p->iftype);
		wp.band = p->band;
		wp.dst = p->dst;
		wp.seq = p->seq ? p->seq : 1u;
		rc = vsf_build_wal_vap_add(f, &wp);
	}
	if (!rc) {
		size_t n = 0;

		if (vsf_frame_export(f, wire, cap, &n) == 0)
			*out_len = n;
		else
			rc = -ENOSPC;
	}
	kfree(f);
	return rc;
}

static int vapscan_vap_start(u8 *wire, size_t cap, size_t *out_len,
			     const struct hi5622_vap_start_params *p)
{
	struct vsf_frame *f;
	int rc;

	if (!wire || !out_len || !p || p->module > VSF_MODULE_MAX ||
	    p->dst > 0x0fu)
		return -EINVAL;
	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	if (hi5622_vap_start_mode == 1u) {
		struct vsf_vap_start gp;

		vsf_vap_start_defaults(&gp);
		gp.dst = p->dst;
		/* wifi-beacon-static lane: the vendor hmac_config_do_start_vap
		 * body[0] is the channel argument [V]; 0 keeps the legacy
		 * bench-proven frame byte-identical. */
		gp.channel = p->channel;
		rc = vsf_build_vap_start(f, &gp);
	} else {
		struct vsf_wal_vap_start sp;

		vsf_wal_vap_start_defaults(&sp);
		sp.dst = p->dst;
		sp.seq = p->seq ? p->seq : 2u;
		rc = vsf_build_wal_vap_start(f, &sp);
	}
	if (!rc) {
		size_t n = 0;

		if (vsf_frame_export(f, wire, cap, &n) == 0)
			*out_len = n;
		else
			rc = -ENOSPC;
	}
	kfree(f);
	return rc;
}

/* ------------------------------------------------------------------ */
/* wifi-beacon-static lane (task-140): module channel config          */
/* ------------------------------------------------------------------ */

/* The vendor AP start's first message: channel info (WAL cfg cmd 0x0124,
 * 4-byte body) posted BEFORE the SSID/AP-config frames, so the firmware's
 * module channel context (ctx +0x10..+0x13) is programmed when the AP VAP
 * starts its beacon engine.  Same command id on both carriers: the g0 id1
 * config event (key 0x0124 -> table A row 108 -> worker CA 0x841B8) and the
 * WAL group-2 envelope (vendor order).  Default-OFF: the kernel layer only
 * calls this when beacon_static=1. */
extern unsigned int hi5622_ap_start_mode;

static int vapscan_chan_cfg(u8 *wire, size_t cap, size_t *out_len,
			    const struct hi5622_chan_cfg_params *p)
{
	struct haf_ap_params hp;
	struct haf_frame *f;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	if (hi5622_ap_start_mode > 1u)
		return -EOPNOTSUPP;
	if (p->channel == 0u || p->channel > 196u || p->band > 1u)
		return -EINVAL;
	if (p->module > 0x12u || p->dst > 0x0fu || p->vap_id > 0x0fu)
		return -EINVAL;
	memset(&hp, 0, sizeof(hp));
	hp.carrier = hi5622_ap_start_mode ? HAF_CARRIER_WAL_G2
					  : HAF_CARRIER_G0_ID1;
	hp.band = p->band;
	hp.channel = p->channel;
	hp.vap_id = p->vap_id;
	hp.module = p->module;
	hp.dst = p->dst;
	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	rc = haf_build_chan_frame(f, &hp);
	if (!rc) {
		if (f->len > cap)
			rc = -ENOSPC;
		else {
			memcpy(wire, f->buf, f->len);
			*out_len = f->len;
		}
	}
	kfree(f);
	return rc;
}

/* ------------------------------------------------------------------ */
/* AP start sequence (wifi-ap-start lane, task-121)                  */
/* ------------------------------------------------------------------ */

/* Wire family for the AP start: 0 = the static-dispatch g0 id1 config keys
 * 0x0005/0x0127 (default; statically routed by firmware table A), 1 = the
 * vendor group-2 WAL cfg-event envelopes cmd 0x05/0x6D/0x127/0x65. */
unsigned int hi5622_ap_start_mode;
module_param_named(ap_start_mode, hi5622_ap_start_mode, uint, 0444);
MODULE_PARM_DESC(ap_start_mode,
		 "AP start wire family: 0=g0 id1 static keys (default), 1=WAL g2 vendor order");

static int vapscan_ap_start(u8 *wire, size_t cap, size_t *out_len,
			    const struct hi5622_ap_start_params *p)
{
	struct haf_ap_params hp;
	struct haf_frame *f;
	enum haf_frame_id id;
	u8 carrier;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	if (hi5622_ap_start_mode > 1u)
		return -EOPNOTSUPP;
	carrier = hi5622_ap_start_mode ? HAF_CARRIER_WAL_G2 : HAF_CARRIER_G0_ID1;
	/* Past the end of the carrier's sequence: the cfg80211 layer stops. */
	rc = haf_sequence_at(p->frame_index, carrier, &id);
	if (rc == -ENODATA)
		return -ENODATA;
	if (rc)
		return rc;
	memset(&hp, 0, sizeof(hp));
	hp.carrier = carrier;
	hp.dst = 0u;
	hp.band = p->band ? 1u : 0u;
	hp.module = p->module;
	hp.vap_id = p->vap_id;
	hp.iftype = 2u;			/* NL80211_IFTYPE_AP */
	hp.hidden_ssid = p->hidden_ssid ? 1u : 0u;
	hp.auth_algs = p->auth_algs;
	hp.start = 1u;
	hp.ssid_len = p->ssid_len;
	if (hp.ssid_len > HAF_MAX_SSID)
		return -EINVAL;
	memcpy(hp.ssid, p->ssid, hp.ssid_len);
	hp.beacon_interval = p->beacon_interval;
	hp.dtim_period = p->dtim_period;
	hp.center_freq1 = p->center_freq;
	hp.width = p->width;
	hp.head_len = p->head_len;
	hp.tail_len = p->tail_len;
	if (hp.head_len > HAF_MAX_HEAD || hp.tail_len > HAF_MAX_TAIL)
		return -EINVAL;
	memcpy(hp.head, p->head, hp.head_len);
	memcpy(hp.tail, p->tail, hp.tail_len);
	f = kmalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	rc = haf_build_frame(f, id, &hp);
	if (!rc) {
		if (f->len > cap)
			rc = -ENOSPC;
		else {
			memcpy(wire, f->buf, f->len);
			*out_len = f->len;
		}
	}
	kfree(f);
	return rc;
}

/* Carrier-aware sequence names for the AP-start post labels (task-121). */
static const char *vapscan_ap_frame_name(u8 frame_index)
{
	enum haf_frame_id id;
	u8 carrier;

	if (hi5622_ap_start_mode > 1u)
		return NULL;
	carrier = hi5622_ap_start_mode ? HAF_CARRIER_WAL_G2 : HAF_CARRIER_G0_ID1;
	if (haf_sequence_at(frame_index, carrier, &id))
		return NULL;
	return haf_frame_name(id);
}

/* ------------------------------------------------------------------ */
/* WPA2 key path (wifi-wpa-static lane, task-139)                    */
/* ------------------------------------------------------------------ */

static int vapscan_key_add(u8 *wire, size_t cap, size_t *out_len,
			   const struct hi5622_key_params *p)
{
	struct hkf_key_params kp;
	struct hkf_frame f;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	memset(&kp, 0, sizeof(kp));
	kp.key_index = p->key_index;
	kp.pairwise = p->pairwise ? 1u : 0u;
	memcpy(kp.mac, p->mac, sizeof(kp.mac));
	kp.cipher = p->cipher;
	kp.key_len = p->key_len;
	kp.seq_len = p->seq_len;
	memcpy(kp.key, p->key, sizeof(kp.key));
	memcpy(kp.seq, p->seq, sizeof(kp.seq));
	rc = hkf_build_add_key(&f, p->module, p->dst, &kp);
	if (rc)
		return rc;
	if (f.len > cap)
		return -ENOSPC;
	memcpy(wire, f.buf, f.len);
	*out_len = f.len;
	return 0;
}

static int vapscan_key_del(u8 *wire, size_t cap, size_t *out_len,
			   const struct hi5622_key_params *p)
{
	struct hkf_key_params kp;
	struct hkf_frame f;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	memset(&kp, 0, sizeof(kp));
	kp.key_index = p->key_index;
	kp.pairwise = p->pairwise ? 1u : 0u;
	memcpy(kp.mac, p->mac, sizeof(kp.mac));
	rc = hkf_build_del_key(&f, p->module, p->dst, &kp);
	if (rc)
		return rc;
	if (f.len > cap)
		return -ENOSPC;
	memcpy(wire, f.buf, f.len);
	*out_len = f.len;
	return 0;
}

static int vapscan_key_default(u8 *wire, size_t cap, size_t *out_len,
			       const struct hi5622_key_default_params *p)
{
	struct hkf_default_key_params dp;
	struct hkf_frame f;
	int rc;

	if (!wire || !out_len || !p)
		return -EINVAL;
	memset(&dp, 0, sizeof(dp));
	dp.key_index = p->key_index;
	dp.unicast = p->unicast ? 1u : 0u;
	dp.multicast = p->multicast ? 1u : 0u;
	/* wifi-wpa3-sae lane (task-144): PMF default management key variant */
	dp.mgmt = p->mgmt ? 1u : 0u;
	dp.cipher = p->cipher;
	rc = hkf_build_default_key(&f, p->module, p->dst, &dp);
	if (rc)
		return rc;
	if (f.len > cap)
		return -ENOSPC;
	memcpy(wire, f.buf, f.len);
	*out_len = f.len;
	return 0;
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
		HI5622_FRAME_CAP_USER_DEL |
		HI5622_FRAME_CAP_VAP_ADD |
		HI5622_FRAME_CAP_VAP_START |
		HI5622_FRAME_CAP_AP_START |
		HI5622_FRAME_CAP_AP_START | HI5622_FRAME_CAP_CHAN_CFG |
		HI5622_FRAME_CAP_KEY_ADD |
		HI5622_FRAME_CAP_KEY_DEL |
		HI5622_FRAME_CAP_KEY_DEFAULT;
	hi5622_vapscan_ops.build_scan_req = vapscan_scan_req;
	hi5622_vapscan_ops.parse_scan_result = vapscan_parse_scan_result;
	hi5622_vapscan_ops.parse_scan_complete = vapscan_parse_scan_complete;
	hi5622_vapscan_ops.parse_scan_channel = vapscan_parse_scan_channel;
	hi5622_vapscan_ops.parse_scan_sta_complete =
		vapscan_parse_scan_sta_complete;
	hi5622_vapscan_ops.build_user_add = vapscan_user_add;
	hi5622_vapscan_ops.build_user_add_notify = vapscan_user_add_notify;
	hi5622_vapscan_ops.build_user_del = vapscan_user_del;
	hi5622_vapscan_ops.build_vap_add = vapscan_vap_add;
	hi5622_vapscan_ops.build_vap_start = vapscan_vap_start;
	/* wifi-beacon-static lane: channel config posted before the AP frames
	 * when the kernel layer enables beacon_static. */
	hi5622_vapscan_ops.build_chan_cfg = vapscan_chan_cfg;
	/* AP start sequence (task-121): set SSID -> [auth] -> AP config ->
	 * [VAP start]; every frame goes through the fail-closed post gate. */
	hi5622_vapscan_ops.build_ap_start = vapscan_ap_start;
	hi5622_vapscan_ops.ap_frame_name = vapscan_ap_frame_name;
	/* AP stop: still no builder - capability omitted. */
	/* WPA2 key path (task-139): g0 id1 cmd 0x0078/0x007A/0x0079 frames. */
	hi5622_vapscan_ops.build_key_add = vapscan_key_add;
	hi5622_vapscan_ops.build_key_del = vapscan_key_del;
	hi5622_vapscan_ops.build_key_default = vapscan_key_default;

	rc = hi5622_frame_ops_register(&hi5622_vapscan_ops);
	if (!rc) {
		hi5622_vapscan_registered = true;
		pr_info("hi5622 frames: provider '%s' registered (caps=0x%x)\n",
			hi5622_vapscan_ops.name, hi5622_vapscan_ops.caps);
	}
	mutex_unlock(&hi5622_vapscan_lock);
	return rc;
}
