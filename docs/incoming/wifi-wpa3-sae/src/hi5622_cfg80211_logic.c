// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_cfg80211_logic.c - portable cfg80211 VAP model, registration gate,
 * op/message map and status formatting for the Hi5622V100 clean-room driver.
 *
 * ORIGINAL clean-room code (wifi-cfg80211 lane, shared task task-104,
 * 2026-11-24).  No vendor source/object/firmware/calibration material is
 * reproduced; see hi5622_cfg80211_logic.h and ../REPORT.md for the evidence
 * markers behind every constant.
 *
 * This file has no kernel or libc dependency beyond <string.h>/<stdio.h> and
 * is linked both by the kernel module (__KERNEL__) and by the host selftest.
 */
#include "hi5622_cfg80211_logic.h"

#define HI5622_ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* =====================================================================
 * Small helpers
 * ===================================================================*/
static void hi5622_cfg_strlcpy(char *dst, const char *src, size_t cap)
{
	size_t i;

	if (!cap)
		return;
	for (i = 0; i + 1 < cap && src[i]; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

static void hi5622_cfg_error(struct hi5622_cfg_model *m, const char *msg)
{
	hi5622_cfg_strlcpy(m->last_error, msg, sizeof(m->last_error));
}

/* =====================================================================
 * Model + VAP lifecycle
 * ===================================================================*/
void hi5622_cfg_model_init(struct hi5622_cfg_model *m, u8 band)
{
	if (!m)
		return;
	memset(m, 0, sizeof(*m));
	m->band = band;
}

void hi5622_cfg_model_deinit(struct hi5622_cfg_model *m)
{
	if (!m)
		return;
	memset(m, 0, sizeof(*m));
}

bool hi5622_cfg_iftype_supported(u16 iftype)
{
	switch (iftype) {
	case HI5622_IFTYPE_AP:
	case HI5622_IFTYPE_STATION:
	case HI5622_IFTYPE_MONITOR:
		return true;
	default:
		return false;
	}
}

const char *hi5622_cfg_iftype_name(u16 iftype)
{
	switch (iftype) {
	case HI5622_IFTYPE_STATION:	return "managed";
	case HI5622_IFTYPE_AP:		return "AP";
	case HI5622_IFTYPE_AP_VLAN:	return "AP_VLAN";
	case HI5622_IFTYPE_MONITOR:	return "monitor";
	case HI5622_IFTYPE_P2P_CLIENT:	return "P2P-client";
	case HI5622_IFTYPE_P2P_GO:	return "P2P-GO";
	default:			return "unknown";
	}
}

/* Derive the VAP address placeholder from the slot.  The kernel layer
 * overrides bytes 1..4 from wiphy->perm_addr and only the last byte comes
 * from here; the full placeholder here keeps the host model self-contained.
 * Locally administered + unicast (0x02:...). */
static void hi5622_cfg_vap_addr(u16 slot, u8 addr[6])
{
	addr[0] = 0x02;
	addr[1] = 0x00;
	addr[2] = 0x68;
	addr[3] = 0x69;
	addr[4] = 0x35;
	addr[5] = (u8)(slot & 0xff);
}

int hi5622_cfg_vap_alloc(struct hi5622_cfg_model *m, const char *name,
			 u16 iftype, u16 *slot)
{
	u16 i, free_slot = HI5622_CFG_MAX_VAPS;

	if (!m || !name || !slot)
		return -EINVAL;
	if (m->removing)
		return -ESHUTDOWN;
	if (!m->wiphy_registered)
		return -ENODEV;
	if (!hi5622_cfg_iftype_supported(iftype)) {
		hi5622_cfg_error(m, "iftype not supported by the M0-M2 model");
		return -EOPNOTSUPP;
	}
	if (!name[0] || strlen(name) >= HI5622_CFG_IFNAMSIZ) {
		hi5622_cfg_error(m, "bad interface name length");
		return -EINVAL;
	}
	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++) {
		struct hi5622_vap_model *v = &m->vap[i];

		if (v->state != HI5622_VAP_FREE && !strcmp(v->name, name)) {
			hi5622_cfg_error(m, "duplicate interface name");
			return -EEXIST;
		}
		if (v->state == HI5622_VAP_FREE && free_slot == HI5622_CFG_MAX_VAPS)
			free_slot = i;
	}
	if (free_slot == HI5622_CFG_MAX_VAPS) {
		hi5622_cfg_error(m, "no free VAP slot");
		return -ENOSPC;
	}

	memset(&m->vap[free_slot], 0, sizeof(m->vap[free_slot]));
	m->vap[free_slot].state = HI5622_VAP_ALLOCATED;
	m->vap[free_slot].slot = free_slot;
	m->vap[free_slot].iftype = iftype;
	m->vap[free_slot].hcc_add_pending = true;
	hi5622_cfg_strlcpy(m->vap[free_slot].name, name,
			   sizeof(m->vap[free_slot].name));
	hi5622_cfg_vap_addr(free_slot, m->vap[free_slot].addr);
	m->netdev_created++;
	m->hcc_vap_msg_deferred++;
	*slot = free_slot;
	return 0;
}

int hi5622_cfg_vap_mark_registered(struct hi5622_cfg_model *m, u16 slot)
{
	if (!m || slot >= HI5622_CFG_MAX_VAPS)
		return -EINVAL;
	if (m->vap[slot].state != HI5622_VAP_ALLOCATED)
		return -EINVAL;
	m->vap[slot].state = HI5622_VAP_REGISTERED;
	return 0;
}

void hi5622_cfg_vap_drop(struct hi5622_cfg_model *m, u16 slot)
{
	if (!m || slot >= HI5622_CFG_MAX_VAPS)
		return;
	if (m->vap[slot].state == HI5622_VAP_FREE)
		return;
	m->vap[slot].hcc_del_pending = m->vap[slot].hcc_add_pending;
	m->vap[slot].state = HI5622_VAP_FREE;
	m->netdev_removed++;
	memset(&m->vap[slot], 0, sizeof(m->vap[slot]));
}

struct hi5622_vap_model *hi5622_cfg_vap_get(struct hi5622_cfg_model *m,
					    u16 slot)
{
	if (!m || slot >= HI5622_CFG_MAX_VAPS)
		return NULL;
	if (m->vap[slot].state == HI5622_VAP_FREE)
		return NULL;
	return &m->vap[slot];
}

struct hi5622_vap_model *hi5622_cfg_vap_find_name(struct hi5622_cfg_model *m,
						  const char *name)
{
	u16 i;

	if (!m || !name)
		return NULL;
	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++) {
		if (m->vap[i].state != HI5622_VAP_FREE &&
		    !strcmp(m->vap[i].name, name))
			return &m->vap[i];
	}
	return NULL;
}

u16 hi5622_cfg_vap_count(const struct hi5622_cfg_model *m)
{
	u16 i, n = 0;

	if (!m)
		return 0;
	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++)
		if (m->vap[i].state != HI5622_VAP_FREE)
			n++;
	return n;
}

/* =====================================================================
 * Registration gate
 * ===================================================================*/
enum hi5622_reg_action hi5622_cfg_reg_gate(const struct hi5622_cfg_model *m,
					   bool boot_latched, int hcc_phase,
					   bool plat_ready, bool dev_ready)
{
	if (!m)
		return HI5622_REG_DISABLED;
	if (m->removing || !m->wiphy_allocated)
		return HI5622_REG_DISABLED;
	if (m->wiphy_registered)
		return HI5622_REG_DONE;
	/* hcc_phase: 4 = READY_TXQ_SYNC, 5 = ACTIVE (enum hcc_phase in
	 * hcc_core.h); the numeric value keeps this file header-free. */
	if (!boot_latched || hcc_phase < 4)
		return HI5622_REG_WAIT;
	if (!plat_ready || !dev_ready)
		return HI5622_REG_WAIT_READY;
	return HI5622_REG_NOW;
}

const char *hi5622_cfg_reg_action_name(enum hi5622_reg_action a)
{
	switch (a) {
	case HI5622_REG_WAIT:		return "wait-boot";
	case HI5622_REG_WAIT_READY:	return "wait-ready";
	case HI5622_REG_NOW:		return "register";
	case HI5622_REG_DONE:		return "registered";
	case HI5622_REG_DISABLED:	return "disabled";
	default:			return "?";
	}
}

/* =====================================================================
 * Status line (sysfs)
 * ===================================================================*/
int hi5622_cfg_status_line(const struct hi5622_cfg_model *m, char *buf,
			   size_t len)
{
	size_t off = 0;
	u16 i;
	char line[192];

	if (!m || !buf || !len)
		return 0;
	/* one snprintf per line so the host/kernel builds differ only in the
	 * snprintf implementation itself. */
#define HI5622_APP(...)							\
	do {								\
		int _n = snprintf(line, sizeof(line), __VA_ARGS__);	\
		if (_n > 0) {						\
			size_t _c = (size_t)_n;				\
			if (_c > sizeof(line) - 1)			\
				_c = sizeof(line) - 1;			\
			if (off + _c < len) {				\
				memcpy(buf + off, line, _c);		\
				off += _c;				\
			} else {					\
				off = len - 1;				\
			}						\
		}							\
	} while (0)

	HI5622_APP("cfg80211 model: band=%s wiphy=%s registered=%u removing=%u\n",
		   m->band == HI5622_BAND_2GHZ ? "2.4GHz" : "5GHz",
		   m->wiphy_allocated ? "alloc" : "none",
		   m->wiphy_registered ? 1u : 0u, m->removing ? 1u : 0u);
	HI5622_APP("register: attempts=%u failures=%u vaps=%u netdev(created=%u removed=%u regfail=%u)\n",
		   m->register_attempts, m->register_failures,
		   (unsigned)hi5622_cfg_vap_count(m), m->netdev_created,
		   m->netdev_removed, m->netdev_register_failures);
	HI5622_APP("hcc-vap-messages: deferred=%u (builders pending, task-103); last-error=%s\n",
		   m->hcc_vap_msg_deferred,
		   m->last_error[0] ? m->last_error : "none");
	HI5622_APP("op-stubs-hit=%u (registered M2 stubs return -EOPNOTSUPP)\n",
		   m->op_stub_hits);
	for (i = 0; i < HI5622_CFG_MAX_VAPS; i++) {
		const struct hi5622_vap_model *v = &m->vap[i];

		if (v->state == HI5622_VAP_FREE)
			continue;
		HI5622_APP("vap%u: %s iftype=%s state=%s hcc-add=%s slot=%u\n",
			   i, v->name, hi5622_cfg_iftype_name(v->iftype),
			   v->state == HI5622_VAP_REGISTERED ? "registered" :
			   "allocated",
			   v->hcc_add_pending ? "pending" : "sent",
			   v->slot);
	}
#undef HI5622_APP
	if (off >= len)
		off = len - 1;
	buf[off] = '\0';
	return (int)off;
}

/* =====================================================================
 * cfg80211-op -> HCC message map (39 vendor-populated slots)
 * ===================================================================*/
static const struct hi5622_op_map_entry hi5622_op_map[] = {
	/* field, milestone, impl, group, id, key, hcc sender, note */
	{ "add_virtual_intf", HI5622_M1, HI5622_OP_IMPL,
	  0x00, 0x0007, -1, "hmac_user_add (168 B); g0 id8 notify-alg",
	  "netdev VAP model live (M0); g0 id7/id8 payload [U] - builder still open" },
	{ "del_virtual_intf", HI5622_M1, HI5622_OP_IMPL,
	  0x00, 0x0009, -1, "hmac_del_user_notify_dmac (12 B)",
	  "netdev teardown live; exact g0 id9 builder exists (h2d_build_del_user), user-id map pending" },
	{ "change_virtual_intf", HI5622_M1, HI5622_OP_IMPL,
	  0xff, 0xffff, -1, "-",
	  "same-type no-op accepted; type changes -EOPNOTSUPP until VAP delete/add exists" },
	{ "add_key", HI5622_M3, HI5622_OP_IMPL,
	  0x00, 0x0001, 0x0078, "g0 id1 hdpp cfg event (cmd 0x0078, body 0x44)",
	  "wifi-wpa-static (task-139): decoded from vendor wal_cfg80211_add_key 0x1007f4 + fw worker CA 0xA5825; wpa_keys=1 gate, wpa_keys=0 default returns -EOPNOTSUPP" },
	{ "get_key", HI5622_M3, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event / D2H sync",
	  "the vendor op 0x100b44 exists; the reply payload is not needed by hostapd WPA2-PSK, so not implemented" },
	{ "del_key", HI5622_M3, HI5622_OP_IMPL,
	  0x00, 0x0001, 0x007a, "g0 id1 hdpp cfg event (cmd 0x007A, body 8)",
	  "wifi-wpa-static (task-139): vendor wal_cfg80211_remove_key 0x100d98 + fw worker CA 0xA4271" },
	{ "set_default_key", HI5622_M3, HI5622_OP_IMPL,
	  0x00, 0x0001, 0x0079, "g0 id1 hdpp cfg event (cmd 0x0079, body 0x28)",
	  "wifi-wpa-static (task-139): vendor wal_cfg80211_set_default_key 0x100ffc + fw worker CA 0xA5C9D" },
	{ "set_default_mgmt_key", HI5622_M3, HI5622_OP_IMPL,
	  0x00, 0x0001, 0x0079, "g0 id1 hdpp cfg event (cmd 0x0079, body 0x28, mgmt class)",
	  "wifi-wpa3-sae (task-144): fw worker CA 0xA5C9D mgmt branch (bg+4 cipher from the preceding IGTK add_key; wpa_keys=1 gate)" },
	{ "start_ap", HI5622_M3, HI5622_OP_DEFERRED,
	  0x00, 0x0001, -1, "hmac_config g0 id1 family + g2 WAL cfg",
	  "start_ap config-blob layout is gap G1; vendor wal_cfg80211_start_ap" },
	{ "change_beacon", HI5622_M3, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event",
	  "beacon IE blob [U]" },
	{ "stop_ap", HI5622_M3, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event",
	  "no payload beyond the VAP identity known" },
	{ "add_station", HI5622_M3, HI5622_OP_DEFERRED,
	  0x00, 0x0007, -1, "hmac_user_add (168 B) family",
	  "user-add payload [I] (wifi-vendor-userspace TABLE T6 row 2); M3" },
	{ "del_station", HI5622_M3, HI5622_OP_DEFERRED,
	  0x00, 0x0009, -1, "hmac_del_user_notify_dmac (12 B)",
	  "exact builder exists; station-dmac semantics need M3" },
	{ "change_station", HI5622_M3, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event",
	  "station flags payload [U]" },
	{ "get_station", HI5622_M2, HI5622_OP_STUB,
	  0xff, 0xffff, -1, "D2H station events g2 ids 3/4/5/6",
	  "registered -EOPNOTSUPP stub (M2); needs the station-event decode (M3)" },
	{ "dump_station", HI5622_M2, HI5622_OP_STUB,
	  0xff, 0xffff, -1, "D2H station events g2 ids 3/4/5/6",
	  "registered -EOPNOTSUPP stub (M2); same event source as get_station" },
	{ "change_bss", HI5622_M3, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event",
	  "vendor stub in wifi.ko (8 B); could stay local bookkeeping" },
	{ "scan", HI5622_M2, HI5622_OP_STUB,
	  0x00, 0x000c, -1, "hmac_enc_scan_send_req (508 B payload)",
	  "registered -EOPNOTSUPP stub (M2); request payload [U] - builder still open" },
	{ "connect", HI5622_M5, HI5622_OP_DEFERRED,
	  0x00, 0x000f, -1, "hmac_sta_sync_join_req_params",
	  "STA join family g0 0xF/0x10/0x11 [I] (wifi-vendor-userspace TABLE T6 row 7)" },
	{ "disconnect", HI5622_M5, HI5622_OP_DEFERRED,
	  0x00, 0x0010, -1, "hmac_sta_wait_join family",
	  "STA join family [I]" },
	{ "set_wiphy_params", HI5622_M4, HI5622_OP_DEFERRED,
	  0x00, 0x0001, -1, "hmac_config g0 id1 family",
	  "fragment/RTS/retry config blob [U] (vendor 0x34-byte stub thunk)" },
	{ "set_bitrate_mask", HI5622_M4, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event",
	  "rate-mask payload [U]" },
	{ "dump_survey", HI5622_M2, HI5622_OP_DEFERRED,
	  0x00, 0x000c, -1, "scan result path (g0 id0xC response)",
	  "deferred: needs the scan implementation first (M2 stub not registered)" },
	{ "set_pmksa", HI5622_M3, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event",
	  "PMKSA payload [U]" },
	{ "del_pmksa", HI5622_M3, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event",
	  "PMKSA payload [U]" },
	{ "flush_pmksa", HI5622_M3, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event",
	  "PMKSA payload [U]" },
	{ "remain_on_channel", HI5622_M5, HI5622_OP_DEFERRED,
	  0xff, 0xffff, -1, "mgmt/offchannel family [U]",
	  "vendor implements it; message family not decoded" },
	{ "cancel_remain_on_channel", HI5622_M5, HI5622_OP_DEFERRED,
	  0xff, 0xffff, -1, "mgmt/offchannel family [U]",
	  "symmetric to remain_on_channel" },
	{ "mgmt_tx", HI5622_M5, HI5622_OP_DEFERRED,
	  0xff, 0xffff, -1, "mgmt tx family [U]",
	  "vendor walks a mgmt frame path in hmac mgmt/tx_data" },
	{ "mgmt_tx_cancel_wait", HI5622_M5, HI5622_OP_DEFERRED,
	  0xff, 0xffff, -1, "mgmt/offchannel family [U]",
	  "vendor 8-byte stub (returns -EOPNOTSUPP-like)" },
	{ "set_power_mgmt", HI5622_M6, HI5622_OP_DEFERRED,
	  0xff, 0xffff, -1, "mailbox id1 (PM sleep request), D2H g3 id17",
	  "hcc_core_pm_request_sleep exists but PM is disabled on the boot path" },
	{ "update_mgmt_frame_registrations", HI5622_M6, HI5622_OP_DEFERRED,
	  0xff, 0xffff, -1, "-",
	  "local frame-registration bookkeeping; no H2D until mgmt TX exists" },
	{ "sched_scan_start", HI5622_M5, HI5622_OP_DEFERRED,
	  0x00, 0x000c, -1, "hmac_enc_scan_send_req family",
	  "sched-scan variant of the scan request; payload [U]" },
	{ "sched_scan_stop", HI5622_M5, HI5622_OP_DEFERRED,
	  0x00, 0x000c, -1, "scan family",
	  "symmetric to sched_scan_start" },
	{ "get_channel", HI5622_M6, HI5622_OP_DEFERRED,
	  0xff, 0xffff, -1, "-",
	  "local current-channel state; only meaningful once start_ap/connect exist" },
	{ "start_p2p_device", HI5622_M6, HI5622_OP_DEFERRED,
	  0xff, 0xffff, -1, "-",
	  "vendor 8-byte stub; no wiphy P2P_DEVICE support in M0-M2" },
	{ "stop_p2p_device", HI5622_M6, HI5622_OP_DEFERRED,
	  0xff, 0xffff, -1, "-",
	  "vendor 4-byte stub" },
	{ "set_mac_acl", HI5622_M6, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event",
	  "ACL payload [U]; vendor implements it for softapd APs" },
	{ "external_auth", HI5622_M6, HI5622_OP_DEFERRED,
	  0x02, 0xffff, -1, "WAL cfg event / external auth family",
	  "EasyMesh/OWE external auth; last vendor op (field 103)" },
};

const struct hi5622_op_map_entry *hi5622_cfg_op_map(size_t *count)
{
	if (count)
		*count = HI5622_ARRAY_SIZE(hi5622_op_map);
	return hi5622_op_map;
}

const struct hi5622_op_map_entry *hi5622_cfg_op_map_find(const char *op)
{
	size_t i;

	if (!op)
		return NULL;
	for (i = 0; i < HI5622_ARRAY_SIZE(hi5622_op_map); i++)
		if (!strcmp(hi5622_op_map[i].op, op))
			return &hi5622_op_map[i];
	return NULL;
}

const char *hi5622_cfg_milestone_name(enum hi5622_milestone m)
{
	switch (m) {
	case HI5622_M0:	return "M0 wiphy+netdev";
	case HI5622_M1:	return "M1 VAP create/destroy";
	case HI5622_M2:	return "M2 scan/station stubs";
	case HI5622_M3:	return "M3 AP control";
	case HI5622_M4:	return "M4 data path";
	case HI5622_M5:	return "M5 scan/STA/mgmt";
	case HI5622_M6:	return "M6 cal/power/extras";
	default:	return "?";
	}
}

const char *hi5622_cfg_op_impl_name(enum hi5622_op_impl i)
{
	switch (i) {
	case HI5622_OP_DEFERRED:	return "deferred";
	case HI5622_OP_STUB:		return "stub-eopnotsupp";
	case HI5622_OP_IMPL:		return "implemented";
	default:			return "?";
	}
}

/* =====================================================================
 * Frames still needing builders (task-103 handoff view)
 * ===================================================================*/
static const struct hi5622_hcc_msg_ref hi5622_msg_refs[] = {
	/* name, group, id, payload_len, builder, milestone */
	{ "ctx_create (g0 id1 key 0x64)", 0x00, 0x0001, 0x14,
	  "exact - h2d_build_ctx_create (proven on hardware)", HI5622_M1 },
	{ "txq_sync (g0 id1 cmd 0xd8)", 0x00, 0x0001, 0x14,
	  "exact - h2d_build_txq_sync (proven on hardware)", HI5622_M1 },
	{ "user_add (g0 id7)", 0x00, 0x0007, 168,
	  "payload [U]; builder still pending - open (task-103 does not model it)", HI5622_M1 },
	{ "user_add_notify_alg (g0 id8)", 0x00, 0x0008, 168,
	  "payload [U]; builder still pending - open (task-103 does not model it)", HI5622_M1 },
	{ "del_user (g0 id9)", 0x00, 0x0009, 12,
	  "exact - h2d_build_del_user (user id/addr form)", HI5622_M1 },
	{ "scan_req (g0 id0xC)", 0x00, 0x000c, 508,
	  "payload [U]; builder still pending - open (task-103 does not model it)", HI5622_M2 },
	{ "sta_join (g0 id0xF)", 0x00, 0x000f, -1,
	  "unknown - task-103", HI5622_M5 },
	{ "sta_wait_join (g0 id0x10)", 0x00, 0x0010, -1,
	  "unknown - task-103", HI5622_M5 },
	{ "sta_join_result (g0 id0x11)", 0x00, 0x0011, -1,
	  "unknown - task-103", HI5622_M5 },
	{ "edca_opt_timeout (g0 id0x1D)", 0x00, 0x001d, 60,
	  "exact - h2d_build_edca_opt", HI5622_M3 },
	{ "WAL cfg event (g2)", 0x02, 0xffff, -1,
	  "unknown - WAL config command ids [U]", HI5622_M3 },
	{ "D2H config_syn (g1 id3)", 0x01, 0x0003, 36,
	  "GENERIC config-sync channel: syn_id u16 at +0x08 (0x0101 alg / 0x0102 cali / 0x07xx per-VAP); observed 36 B = syn_id 0x07E0 VAP_MIB_UPDATE (task-103 REPORT sec 2/9.3)", HI5622_M1 },
	{ "D2H g1 id0x12", 0x01, 0x0012, 40,
	  "hmac_event_acs_response family (task-103 REPORT sec 3); host handler forwards the body opaque", HI5622_M1 },
	{ "D2H scan bss (g1 id15)", 0x01, 0x000f, -1,
	  "vendor scan result path [I] (wifi-vendor-userspace TABLE T6 row 6)", HI5622_M2 },
	{ "D2H scan complete (g1 id16)", 0x01, 0x0010, -1,
	  "vendor scan result path [I]", HI5622_M2 },
	{ "D2H scan channel (g1 id17)", 0x01, 0x0011, -1,
	  "vendor scan result path [I]", HI5622_M2 },
	{ "D2H scan rsp sta (g2 id2)", 0x02, 0x0002, -1,
	  "vendor scan response [I]", HI5622_M2 },
	{ "D2H station events (g2 3/4/5/6)", 0x02, 0x0003, -1,
	  "asoc/connect/disconnect events [I]", HI5622_M3 },
	{ "D2H PM event (g3 id17)", 0x03, 0x0011, -1,
	  "hmac_d2h_pm_event; PM layer off on the boot path", HI5622_M6 },
	{ "calibration alg cfg (g0 id2 key 0x0101)", 0x00, 0x0002, 286,
	  "exact - proto_seq_frames.py (298 B total); 6 vendor cal cfg ids (task-103 REPORT sec 5.3)", HI5622_M6 },
};

const struct hi5622_hcc_msg_ref *hi5622_cfg_hcc_msg_refs(size_t *count)
{
	if (count)
		*count = HI5622_ARRAY_SIZE(hi5622_msg_refs);
	return hi5622_msg_refs;
}
