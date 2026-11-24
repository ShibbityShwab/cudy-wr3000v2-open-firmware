/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_cfg80211_logic.h - portable (host + kernel) cfg80211 VAP lifecycle
 * model, registration gate and cfg80211-op <-> HCC-message map for the
 * Hi5622V100 clean-room Wi-Fi driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code written by the wifi-cfg80211 lane (shared task
 * task-104, 2026-11-24) from the project's committed decoded contract.  No
 * vendor source, object code, firmware byte, calibration value or secret is
 * reproduced here; every address/message constant below is a derived value
 * with an evidence marker (see ../REPORT.md).
 *
 * Why a separate file: the VAP state machine, the "may we register the wiphy
 * yet" gate and the op/message mapping table must be testable WITHOUT a
 * device and WITHOUT a kernel - tools/cfg80211_logic_selftest.c links this
 * file host-side.  The kernel files hi5622_cfg80211.c/hi5622_cfg80211_ops.c
 * add only the cfg80211/netdev/RTNL plumbing around this model.
 *
 * Locking: the kernel layer serialises every entry point with its own mutex;
 * this file has no locking of its own.
 *
 * Milestones (this lane's task-104 definitions; the vendor-userspace lane's
 * M0-M7 numbering in wifi-vendor-userspace/REPORT.md sec 8 is mapped in
 * ../REPORT.md):
 *   M0 wiphy registration + netdev VAP model
 *   M1 VAP create/destroy wired to the HCC user/vap messages
 *   M2 scan + station queries (clean -EOPNOTSUPP until the messages exist)
 *   M3 AP control (start/stop/beacon/keys/station events)
 *   M4 data path (hdpp queues, netdev TX/RX)
 *   M5 scan/STA join + mgmt TX
 *   M6 calibration/power/regulatory/extras
 */
#ifndef HI5622_CFG80211_LOGIC_H
#define HI5622_CFG80211_LOGIC_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int32_t  s32;
#endif

/* Maximum VAPs modelled per endpoint (vendor live: 6 netdevs on 2 phys; 16
 * leaves room for the 4 AP + 2 STA default config plus headroom). */
#define HI5622_CFG_MAX_VAPS	16
/* IFNAMSIZ-compatible upper bound used by the model (kernel: IFNAMSIZ 16). */
#define HI5622_CFG_IFNAMSIZ	16

/* Endpoint band selector.  The two PCIe endpoints are one radio each
 * (0000:00:00.0 / 0001:00:00.0; DRIVER-BLACKBOX sec 1); the domain->band
 * mapping is NOT proven (wifi-thermal-re: is_pcie_cross=1, so domain != band
 * by assumption), therefore the kernel layer exposes band_force and only
 * defaults to domain 0 = 2.4 GHz [I]. */
enum hi5622_band {
	HI5622_BAND_2GHZ = 0,
	HI5622_BAND_5GHZ = 1,
};

/* nl80211 iftype values used by the model.  These are frozen nl80211 UAPI
 * numbers (include/uapi/linux/nl80211.h enum nl80211_iftype); the kernel ops
 * layer translates them explicitly so the host selftest does not depend on
 * kernel headers. */
enum hi5622_iftype {
	HI5622_IFTYPE_STATION	= 2,
	HI5622_IFTYPE_AP	= 3,
	HI5622_IFTYPE_AP_VLAN	= 4,
	HI5622_IFTYPE_MONITOR	= 6,
	HI5622_IFTYPE_P2P_CLIENT = 8,
	HI5622_IFTYPE_P2P_GO	= 9,
};

/* VAP lifecycle states.  ALLOCATED: the model slot and the netdev owner are
 * reserved; REGISTERED: cfg80211_register_netdevice() succeeded and the wdev
 * is visible to nl80211.  HCC_ADD_PENDING/HCC_DEL_PENDING record that the
 * firmware user/vap message could not be sent yet (frame builder pending,
 * task-103); the netdev model still works so the M0 host-only gate is
 * reachable. */
enum hi5622_vap_state {
	HI5622_VAP_FREE = 0,
	HI5622_VAP_ALLOCATED,
	HI5622_VAP_REGISTERED,
};

struct hi5622_vap_model {
	enum hi5622_vap_state	state;
	bool			hcc_add_pending;	/* g0 id7/id8 builder pending */
	bool			hcc_del_pending;	/* g0 id9 not sent at teardown */
	u16			iftype;			/* enum hi5622_iftype */
	u16			slot;
	u32			hcc_user_id;		/* 0 = unassigned */
	u8			addr[6];
	char			name[HI5622_CFG_IFNAMSIZ];
};

/* Registration gate result. */
enum hi5622_reg_action {
	HI5622_REG_WAIT = 0,	/* not released / boot not latched yet */
	HI5622_REG_WAIT_READY,	/* boot latched, ready pair not complete */
	HI5622_REG_NOW,		/* ready: register the wiphy */
	HI5622_REG_DONE,	/* already registered */
	HI5622_REG_DISABLED,	/* cfg80211_auto=0 or removing */
};

struct hi5622_cfg_model {
	bool			wiphy_allocated;
	bool			wiphy_registered;
	bool			register_pending;
	bool			removing;
	u8			band;			/* enum hi5622_band */
	u32			register_attempts;
	u32			register_failures;
	u32			netdev_created;
	u32			netdev_removed;
	u32			netdev_register_failures;
	u32			hcc_vap_msg_deferred;
	u32			op_stub_hits;
	char			last_error[64];
	struct hi5622_vap_model	vap[HI5622_CFG_MAX_VAPS];
};

/* ---------------------------------------------------------------------
 * Model + VAP lifecycle
 * ------------------------------------------------------------------- */
void hi5622_cfg_model_init(struct hi5622_cfg_model *m, u8 band);
void hi5622_cfg_model_deinit(struct hi5622_cfg_model *m);

/* True for the interface modes the M0-M2 model supports: AP, STA, MONITOR.
 * AP_VLAN/WDS/P2P/NAN are deliberately rejected (WDS is not registrable in
 * 6.18 at all - wiphy_register() WARNs on BIT(NL80211_IFTYPE_WDS); AP_VLAN
 * needs the parent-VAP plumbing of M3). */
bool hi5622_cfg_iftype_supported(u16 iftype);
const char *hi5622_cfg_iftype_name(u16 iftype);

/* Reserve a VAP slot and derive its locally-administered unicast address
 * from the wiphy perm_addr + slot.  Returns 0 and fills *slot, or -EINVAL /
 * -EEXIST / -ENOSPC.  This is the model half of add_virtual_intf; the netdev
 * is allocated by the kernel layer only after this succeeds. */
int hi5622_cfg_vap_alloc(struct hi5622_cfg_model *m, const char *name,
			 u16 iftype, u16 *slot);
int hi5622_cfg_vap_mark_registered(struct hi5622_cfg_model *m, u16 slot);
void hi5622_cfg_vap_drop(struct hi5622_cfg_model *m, u16 slot);
struct hi5622_vap_model *hi5622_cfg_vap_get(struct hi5622_cfg_model *m,
					    u16 slot);
struct hi5622_vap_model *hi5622_cfg_vap_find_name(struct hi5622_cfg_model *m,
						  const char *name);
u16 hi5622_cfg_vap_count(const struct hi5622_cfg_model *m);

/* Registration gate: decides whether the wiphy may be registered yet.  The
 * vendor registers the wiphy after the ready handshake inside the driver init
 * (wifi-vendor-userspace sec 2); we latch the boot gate first and then wait
 * for both ready-pair messages, so a wiphy can never appear before the
 * firmware speaks. */
enum hi5622_reg_action hi5622_cfg_reg_gate(const struct hi5622_cfg_model *m,
					   bool boot_latched, int hcc_phase,
					   bool plat_ready, bool dev_ready);
const char *hi5622_cfg_reg_action_name(enum hi5622_reg_action a);

/* Human-readable multi-line status (sysfs); returns the number of bytes
 * written (like scnprintf). */
int hi5622_cfg_status_line(const struct hi5622_cfg_model *m, char *buf,
			   size_t len);

/* ---------------------------------------------------------------------
 * cfg80211-op -> HCC message map
 *
 * The entry list mirrors the vendor's populated cfg80211_ops table (39 of
 * the 112 slots, wifi-driver-re/REPORT.md sec 2.2).  Every entry records:
 *   - the milestone at which the op is wired (M0-M6, above);
 *   - how it is present in the M0-M2 module (implemented / registered
 *     -EOPNOTSUPP stub / deferred => NULL slot, cfg80211 returns EOPNOTSUPP);
 *   - the HCC H2D message (group/id) and its builder status, or 0xff/0xffff
 *     when no confirmed H2D message exists yet;
 *   - the D2H response/event frame ids where known.
 * The builders that still do not exist are task-103 (wifi-proto-seq)'s
 * deliverable; this table references its group/id/keys rather than
 * reconstructing payloads here.
 * ------------------------------------------------------------------- */
enum hi5622_milestone {
	HI5622_M0 = 0,
	HI5622_M1,
	HI5622_M2,
	HI5622_M3,
	HI5622_M4,
	HI5622_M5,
	HI5622_M6,
	HI5622_M_COUNT,
};

enum hi5622_op_impl {
	HI5622_OP_DEFERRED = 0,	/* NULL in struct cfg80211_ops (EOPNOTSUPP) */
	HI5622_OP_STUB,		/* registered, returns -EOPNOTSUPP cleanly */
	HI5622_OP_IMPL,		/* implemented in the M0-M2 driver */
};

struct hi5622_op_map_entry {
	const char		*op;		/* struct cfg80211_ops member */
	enum hi5622_milestone	milestone;
	enum hi5622_op_impl	impl;
	u8			hcc_group;	/* 0xff = none/unknown */
	u16			hcc_id;		/* 0xffff = none/unknown */
	s8			hcc_key;	/* u8 key at payload+8, -1 none */
	const char		*hcc_msg;	/* vendor sender / nickname */
	const char		*note;		/* builder status / event ids */
};

const struct hi5622_op_map_entry *hi5622_cfg_op_map(size_t *count);
const struct hi5622_op_map_entry *hi5622_cfg_op_map_find(const char *op);
const char *hi5622_cfg_milestone_name(enum hi5622_milestone m);
const char *hi5622_cfg_op_impl_name(enum hi5622_op_impl i);

/* H2D/D2H frames still needing builders (task-103 handoff view). */
struct hi5622_hcc_msg_ref {
	const char	*name;
	u8		group;
	u16		id;
	s32		payload_len;	/* -1 = variable/unknown, 0 = header only */
	const char	*builder;	/* "exact" / "pending(task-103)" / ... */
	enum hi5622_milestone milestone;
};

const struct hi5622_hcc_msg_ref *hi5622_cfg_hcc_msg_refs(size_t *count);

#endif /* HI5622_CFG80211_LOGIC_H */
