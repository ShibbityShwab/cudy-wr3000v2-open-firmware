// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_keys.c - cfg80211 key-install path (add/del/set_default_key) for the
 * Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-wpa-static lane, shared task task-139,
 * 2026-11-28).  No vendor source/object/firmware byte is reproduced.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * hostapd AP-ENABLED needs the WPA2 group key before an AP with WPA2 can be
 * considered up: hostapd_start_beacon() -> wpa_init_keys() steps the group
 * state machine and installs the GTK through:
 *   NL80211_CMD_NEW_KEY    (group, key_idx = GN = 1, CCMP/TKIP suite, GTK)
 *   NL80211_CMD_SET_KEY    (key.idx 1, KEY_DEFAULT + TYPE_MULTICAST)
 * and later, per associated STA:
 *   NL80211_CMD_NEW_KEY    (pairwise, key_idx 0, peer MAC) for the PTK,
 *   NL80211_CMD_DEL_KEY    on deauth/disconnect.
 * cfg80211 accepted none of these before this lane (.add_key absent ->
 * -EOPNOTSUPP), which is the M3 blocker recorded by wifi-image-polish.
 *
 * Vendor mapping (static RE, wifi-wpa-static REPORT.md):
 *   add_key     -> firmware g0 id1 cfg event cmd 0x0078, body 0x44
 *                  (vendor wal_cfg80211_add_key 2.4.15 0x1007f4; the same
 *                   body the hmac path sends through hdpp_config_send_event
 *                   with hmac_user_add_key 0x1638)
 *   del_key     -> cmd 0x007A, body 8   {u8 key_index, u8 pairwise, mac[6]}
 *   set_default -> cmd 0x0079, body 0x28 {u8 key_index, u8 unicast,
 *                  u8 multicast, ...}
 * The firmware workers are table-A CAs 0xA5825 / 0xA5C9D / 0xA4271 and they
 * validate exactly the bounds enforced here.
 *
 * FAIL-CLOSED POLICY
 * ------------------
 * wpa_keys=0 (default) returns -EOPNOTSUPP before any frame is built; the
 * bench enables it with wpa_keys=1 and allow_frames>0.  Key material is never
 * logged or printed: only index/cipher-class/length metadata.
 */
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/errno.h>
#include <linux/if_arp.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/string.h>
#include <linux/types.h>
#include <net/cfg80211.h>

#include "hi5622_cfg80211.h"
#include "hi5622_driver.h"
#include "hi5622_frame_post.h"
#include "hi5622_frames_api.h"
#include "hi5622_key_frames.h"
#include "hi5622_keys.h"
#include "hi5622_scan.h"

/* Fail-closed default: a module loaded without wpa_keys=1 can never write a
 * key frame, exactly like allow_frames=0 for the other provider paths. */
unsigned int hi5622_wpa_keys;
module_param_named(wpa_keys, hi5622_wpa_keys, uint, 0644);
MODULE_PARM_DESC(wpa_keys,
		 "WPA2 key install path (0=fail-closed default refues all key ops, 1=post the decoded firmware key frames)");

/* g0 header module/VAP index the key event targets.  The bench-proven AP
 * start posts its frames against module 0; the key event must use the same
 * module context (it is the hdpp dispatch field the firmware handler
 * resolves to module_ctx). */
unsigned int hi5622_wpa_key_module;
module_param_named(wpa_key_module, hi5622_wpa_key_module, uint, 0444);
MODULE_PARM_DESC(wpa_key_module,
		 "g0 header module/VAP index for key events (default 0, bench-proven AP-start module)");

/* Counters/status for the sysfs line and the bench ladder. */
struct hi5622_key_stats {
	u32 add_ok;
	u32 add_err;
	u32 del_ok;
	u32 del_err;
	u32 def_ok;
	u32 def_err;
	u32 refused_gate;
	u32 refused_validate;
	s32 last_errno;
	u8 last_op;		/* 1 add, 2 del, 3 default */
	u8 last_key_index;
	u8 last_pairwise;
};

static DEFINE_MUTEX(hi5622_keys_lock);
static struct hi5622_key_stats hi5622_key_stats;

static const char *hi5622_keys_op_name(u8 op)
{
	switch (op) {
	case 1:	return "add_key";
	case 2:	return "del_key";
	case 3:	return "set_default_key";
	default: return "?";
	}
}

/* Common fail-closed gate: config knob, provider capability, AP-started
 * state (the firmware group-key path installs against the running VAP; the
 * hostapd order is start_ap -> AP-ENABLED -> wpa_init_keys -> NEW_KEY). */
static int hi5622_keys_gate(struct hi5622_cfg80211 *c,
			    const struct hi5622_frame_ops **out_ops,
			    u32 cap, const char *what, struct device *dev)
{
	const struct hi5622_frame_ops *ops;

	if (!c || !c->ctx)
		return -ENODEV;
	if (!hi5622_wpa_keys) {
		hi5622_key_stats.refused_gate++;
		dev_dbg(dev,
			"cfg80211: %s refused - wpa_keys=0 (fail-closed default; bench loads wpa_keys=1)\n",
			what);
		return -EOPNOTSUPP;
	}
	ops = hi5622_frame_ops_get();
	if (!ops || !(ops->caps & cap)) {
		hi5622_key_stats.refused_gate++;
		dev_warn(dev,
			 "cfg80211: %s refused - no frame provider advertises this key builder (caps=0x%x)\n",
			 what, ops ? ops->caps : 0u);
		return -EOPNOTSUPP;
	}
	if (!c->ctx->ap_started) {
		hi5622_key_stats.refused_gate++;
		dev_warn(dev,
			 "cfg80211: %s refused - no AP start committed on this module load; the firmware group-key path needs the running VAP (start the AP first)\n",
			 what);
		return -EBUSY;
	}
	if (hi5622_wpa_key_module > HKF_MAX_MODULE) {
		hi5622_key_stats.refused_gate++;
		dev_warn(dev,
			 "cfg80211: %s refused - wpa_key_module=%u out of range\n",
			 what, hi5622_wpa_key_module);
		return -EINVAL;
	}
	*out_ops = ops;
	return 0;
}

static int hi5622_keys_fill_params(struct hkf_key_params *p, u8 key_index,
				   bool pairwise, const u8 *mac_addr,
				   struct key_params *params)
{
	memset(p, 0, sizeof(*p));
	p->key_index = key_index;
	p->pairwise = pairwise ? 1u : 0u;
	if (pairwise) {
		if (!mac_addr || !is_unicast_ether_addr(mac_addr))
			return -EINVAL;
		memcpy(p->mac, mac_addr, 6);
	}
	if (!params)
		return -EINVAL;
	if (params->key_len < 0 || params->key_len > (int)HKF_KEY_MAX ||
	    params->seq_len < 0 || params->seq_len > (int)HKF_SEQ_MAX)
		return -EINVAL;
	if (!params->key || params->key_len == 0)
		return -EINVAL;
	p->key_len = (u16)params->key_len;
	p->seq_len = (u16)params->seq_len;
	memcpy(p->key, params->key, p->key_len);
	if (p->seq_len && params->seq)
		memcpy(p->seq, params->seq, p->seq_len);
	p->cipher = params->cipher;
	return 0;
}

static int hi5622_keys_post(struct hi5622_cfg80211 *c, const u8 *wire,
			    size_t len, const char *label)
{
	return hi5622_frame_post(c->ctx, wire, len, label, NULL);
}

int hi5622_keys_op_add(struct wiphy *wiphy, struct net_device *netdev,
		       int link_id, u8 key_index, bool pairwise,
		       const u8 *mac_addr, struct key_params *params)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	const struct hi5622_frame_ops *ops = NULL;
	struct device *dev = wiphy_dev(wiphy);
	struct hkf_key_params p;
	struct hkf_frame *f;
	enum hkf_cipher_class cls;
	size_t len = 0;
	int rc;

	if (!dev || !netdev || !netdev->ieee80211_ptr)
		return -EINVAL;
	if (link_id > 0) {
		dev_warn(dev, "cfg80211: add_key refused - link_id=%d (single-link device)\n",
			 link_id);
		return -EOPNOTSUPP;
	}
	if (netdev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP) {
		dev_dbg(dev, "cfg80211: add_key in iftype %d - STA connect path is M5 (not implemented)\n",
			netdev->ieee80211_ptr->iftype);
		return -EOPNOTSUPP;
	}
	rc = hi5622_keys_gate(c, &ops, HI5622_FRAME_CAP_KEY_ADD, "add_key", dev);
	if (rc)
		return rc;
	rc = hi5622_keys_fill_params(&p, key_index, pairwise, mac_addr, params);
	if (rc) {
		mutex_lock(&hi5622_keys_lock);
		hi5622_key_stats.refused_validate++;
		hi5622_key_stats.last_errno = rc;
		mutex_unlock(&hi5622_keys_lock);
		dev_warn(dev,
			 "cfg80211: add_key refused - invalid key params (idx=%u pairwise=%u key_len=%d seq_len=%d)\n",
			 key_index, pairwise, params ? params->key_len : -1,
			 params ? params->seq_len : -1);
		return -EINVAL;
	}
	rc = hkf_key_params_validate(&p);
	if (rc) {
		mutex_lock(&hi5622_keys_lock);
		hi5622_key_stats.refused_validate++;
		hi5622_key_stats.last_errno = rc;
		mutex_unlock(&hi5622_keys_lock);
		dev_warn(dev,
			 "cfg80211: add_key refused - firmware worker bounds/cipher-class check failed (idx=%u pairwise=%u cipher=0x%x key_len=%u seq_len=%u class=%s, rc=%d)\n",
			 p.key_index, p.pairwise, p.cipher, p.key_len, p.seq_len,
			 hkf_cipher_class_name(hkf_cipher_classify(p.cipher)), rc);
		return rc == -EOPNOTSUPP ? rc : -EINVAL;
	}
	cls = hkf_cipher_classify(p.cipher);

	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	rc = hkf_build_add_key(f, (u8)hi5622_wpa_key_module, 0u, &p);
	if (rc) {
		kfree(f);
		return rc < 0 ? rc : -EIO;
	}
	len = f->len;
	rc = hi5622_keys_post(c, f->buf, len, "key_add");
	mutex_lock(&hi5622_keys_lock);
	if (rc) {
		hi5622_key_stats.add_err++;
		hi5622_key_stats.last_errno = rc;
	} else {
		hi5622_key_stats.add_ok++;
		hi5622_key_stats.last_op = 1;
		hi5622_key_stats.last_key_index = p.key_index;
		hi5622_key_stats.last_pairwise = p.pairwise;
		hi5622_key_stats.last_errno = 0;
	}
	mutex_unlock(&hi5622_keys_lock);
	kfree(f);
	if (rc) {
		dev_warn(dev,
			 "cfg80211: add_key NOT posted (idx=%u pairwise=%u cipher=0x%x class=%s key_len=%u) rc=%d\n",
			 p.key_index, p.pairwise, p.cipher,
			 hkf_cipher_class_name(cls), p.key_len, rc);
		return rc;
	}
	dev_info(dev,
		 "cfg80211: add_key posted (g0 id1 cmd 0x%04x 'key_add', %zu B, module=%u idx=%u pairwise=%u cipher=0x%x class=%s key_len=%u seq_len=%u) - firmware worker CA 0x0A5825 installs the key; no key material is logged\n",
		 HKF_CMD_ADD_KEY, len, hi5622_wpa_key_module, p.key_index,
		 p.pairwise, p.cipher, hkf_cipher_class_name(cls), p.key_len,
		 p.seq_len);
	return 0;
}

int hi5622_keys_op_del(struct wiphy *wiphy, struct net_device *netdev,
		       int link_id, u8 key_index, bool pairwise,
		       const u8 *mac_addr)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	const struct hi5622_frame_ops *ops = NULL;
	struct device *dev = wiphy_dev(wiphy);
	struct hkf_key_params p;
	struct hkf_frame *f;
	size_t len = 0;
	int rc;

	if (!dev || !netdev || !netdev->ieee80211_ptr)
		return -EINVAL;
	if (link_id > 0)
		return -EOPNOTSUPP;
	if (netdev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;
	rc = hi5622_keys_gate(c, &ops, HI5622_FRAME_CAP_KEY_DEL, "del_key", dev);
	if (rc)
		return rc;
	memset(&p, 0, sizeof(p));
	p.key_index = key_index;
	p.pairwise = pairwise ? 1u : 0u;
	if (pairwise) {
		if (!mac_addr || !is_unicast_ether_addr(mac_addr))
			return -EINVAL;
		memcpy(p.mac, mac_addr, 6);
	}
	if (key_index > HKF_MAX_KEY_INDEX)
		return -EINVAL;
	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	rc = hkf_build_del_key(f, (u8)hi5622_wpa_key_module, 0u, &p);
	if (rc) {
		kfree(f);
		return rc < 0 ? rc : -EIO;
	}
	len = f->len;
	rc = hi5622_keys_post(c, f->buf, len, "key_del");
	mutex_lock(&hi5622_keys_lock);
	if (rc) {
		hi5622_key_stats.del_err++;
		hi5622_key_stats.last_errno = rc;
	} else {
		hi5622_key_stats.del_ok++;
		hi5622_key_stats.last_op = 2;
		hi5622_key_stats.last_key_index = p.key_index;
		hi5622_key_stats.last_pairwise = p.pairwise;
		hi5622_key_stats.last_errno = 0;
	}
	mutex_unlock(&hi5622_keys_lock);
	kfree(f);
	if (rc) {
		dev_warn(dev, "cfg80211: del_key NOT posted (idx=%u pairwise=%u) rc=%d\n",
			 p.key_index, p.pairwise, rc);
		return rc;
	}
	dev_info(dev,
		 "cfg80211: del_key posted (g0 id1 cmd 0x%04x 'key_del', %zu B, module=%u idx=%u pairwise=%u) - firmware worker CA 0x0A4271 removes the key\n",
		 HKF_CMD_DEL_KEY, len, hi5622_wpa_key_module, p.key_index,
		 p.pairwise);
	return 0;
}

int hi5622_keys_op_set_default(struct wiphy *wiphy, struct net_device *netdev,
			       int link_id, u8 key_index, bool unicast,
			       bool multicast)
{
	struct hi5622_cfg80211 *c = wiphy_priv(wiphy);
	const struct hi5622_frame_ops *ops = NULL;
	struct device *dev = wiphy_dev(wiphy);
	struct hkf_default_key_params p;
	struct hkf_frame *f;
	size_t len = 0;
	int rc;

	if (!dev || !netdev || !netdev->ieee80211_ptr)
		return -EINVAL;
	if (link_id > 0)
		return -EOPNOTSUPP;
	if (netdev->ieee80211_ptr->iftype != NL80211_IFTYPE_AP)
		return -EOPNOTSUPP;
	rc = hi5622_keys_gate(c, &ops, HI5622_FRAME_CAP_KEY_DEFAULT,
			      "set_default_key", dev);
	if (rc)
		return rc;
	memset(&p, 0, sizeof(p));
	p.key_index = key_index;
	p.unicast = unicast ? 1u : 0u;
	p.multicast = multicast ? 1u : 0u;
	rc = hkf_default_key_params_validate(&p);
	if (rc) {
		mutex_lock(&hi5622_keys_lock);
		hi5622_key_stats.refused_validate++;
		hi5622_key_stats.last_errno = rc;
		mutex_unlock(&hi5622_keys_lock);
		dev_warn(dev,
			 "cfg80211: set_default_key refused - firmware worker bounds failed (idx=%u unicast=%u multicast=%u rc=%d)\n",
			 p.key_index, p.unicast, p.multicast, rc);
		return rc == -EOPNOTSUPP ? rc : -EINVAL;
	}
	f = kzalloc(sizeof(*f), GFP_KERNEL);
	if (!f)
		return -ENOMEM;
	rc = hkf_build_default_key(f, (u8)hi5622_wpa_key_module, 0u, &p);
	if (rc) {
		kfree(f);
		return rc < 0 ? rc : -EIO;
	}
	len = f->len;
	rc = hi5622_keys_post(c, f->buf, len, "key_default");
	mutex_lock(&hi5622_keys_lock);
	if (rc) {
		hi5622_key_stats.def_err++;
		hi5622_key_stats.last_errno = rc;
	} else {
		hi5622_key_stats.def_ok++;
		hi5622_key_stats.last_op = 3;
		hi5622_key_stats.last_key_index = p.key_index;
		hi5622_key_stats.last_pairwise = 0;
		hi5622_key_stats.last_errno = 0;
	}
	mutex_unlock(&hi5622_keys_lock);
	kfree(f);
	if (rc) {
		dev_warn(dev,
			 "cfg80211: set_default_key NOT posted (idx=%u uni=%u multi=%u) rc=%d\n",
			 p.key_index, p.unicast, p.multicast, rc);
		return rc;
	}
	dev_info(dev,
		 "cfg80211: set_default_key posted (g0 id1 cmd 0x%04x 'key_default', %zu B, module=%u idx=%u uni=%u multi=%u) - firmware worker CA 0x0A5C9D selects the default key\n",
		 HKF_CMD_SET_DEFAULT_KEY, len, hi5622_wpa_key_module,
		 p.key_index, p.unicast, p.multicast);
	return 0;
}

int hi5622_keys_status(struct hi5622_cfg80211 *c, char *buf, size_t len)
{
	struct hi5622_key_stats st;
	int n;

	if (!c || !buf || !len)
		return -EINVAL;
	mutex_lock(&hi5622_keys_lock);
	st = hi5622_key_stats;
	mutex_unlock(&hi5622_keys_lock);
	n = scnprintf(buf, len,
		      "wpa_keys=%u key_module=%u add_ok=%u add_err=%u del_ok=%u del_err=%u def_ok=%u def_err=%u gate_refused=%u validate_refused=%u last_op=%s last_idx=%u last_pairwise=%u last_errno=%d\n",
		      hi5622_wpa_keys, hi5622_wpa_key_module, st.add_ok,
		      st.add_err, st.del_ok, st.del_err, st.def_ok,
		      st.def_err, st.refused_gate, st.refused_validate,
		      hi5622_keys_op_name(st.last_op), st.last_key_index,
		      st.last_pairwise, st.last_errno);
	return n;
}
