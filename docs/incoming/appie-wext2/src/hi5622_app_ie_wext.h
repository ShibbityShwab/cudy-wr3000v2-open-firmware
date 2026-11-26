/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_app_ie_wext.h - the vendor WEXT set_vendor_ie call sequence
 * (nlwifi_set_app_ie -> WAL cfg event 0x0276 SET_VENDOR_IE -> hmac chain ->
 * host mirror -> the direct g0 id 0x20 frame) for the Hi5622V100 clean-room
 * full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane appie-wext2, shared task task-239, 2026-11-26).
 * The layouts/constants are the committed decode of the local vendor driver
 * disassembly (sibling lanes' anchors + this lane's TABLES.md W1-W5).  No
 * vendor source, object code, firmware byte, calibration value, MAC or secret
 * is reproduced here - only protocol layouts.
 *
 * WHY THIS EXISTS
 * ---------------
 * The app-IE H2D frame (g0 id 0x20, hi5622_app_ie.h) is accepted and consumed
 * by the firmware but the per-VAP class slot stays empty when it is posted from
 * the AP-start ladder.  The vendor's userspace does not post the bare frame: it
 * uses the Hi5622 WEXT private ioctl cmd 0x8eec (the "nlwifi_set_app_ie" ABI),
 * which wraps the same {ie_len, type, mode=0, ie} request into a WAL cfg-event
 * RPC - event 0x0276 SET_VENDOR_IE - sent as one fixed 536-byte group-2 / id-0
 * HCC frame and waits for the response.  The cfg-event dispatcher then runs
 * hmac_config_set_vendor_ie -> hmac_config_set_app_ie_to_vap, which (a) saves
 * the host mirror (mac_vap_save_app_ie: slot = vap + 20*type + 0x24c) and
 * (b) posts the direct g0 id 0x20 frame.  This lane's model reproduces that
 * whole sequence byte for byte so the driver can fire the vendor-exact object
 * and the vendor call order.
 *
 * Frame/exception constants are decoded protocol layout quoted as numbers in
 * TABLES.md; the two fields marked [I] (the 4-byte transport cookie and the
 * 2-byte reserved word) are zeroed by this model.
 *
 * Pure C99: the same code compiles in the kernel module and in the host
 * selftest (src/app_ie_wext_selftest.c); tools/run-host-tests.sh compiles both.
 */
#ifndef HI5622_APP_IE_WEXT_H
#define HI5622_APP_IE_WEXT_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif
#endif

/* ---- the WEXT userspace ABI [V] (TABLES W1) --------------------------- */
/* The private-ioctl command of the vendor "nlwifi_set_app_ie" op:
 * wal_witp_wifi_priv_cmd_proc_set (CA 0x118d94) maps cmd - 0x8ee1 == 11. */
#define HI5622_AIW_WEXT_CMD_SET_VENDOR_IE	0x8eecu
/* {u32 cmd; u32 ie_len; u8 type; u8 pad[3]; u8 ie[ie_len <= 496];} */
#define HI5622_AIW_WEXT_HDR_LEN		12u
#define HI5622_AIW_WEXT_IE_MAX		496u
#define HI5622_AIW_TYPE_MAX		19u	/* firmware per-VAP IE classes */
#define HI5622_AIW_VAP_MAX		255u

/* ---- the WAL cfg-event RPC envelope [V geometry] (TABLES W2/W3) ------- */
#define HI5622_AIW_HDR_LEN		12u	/* HCC header */
#define HI5622_AIW_COOKIE_LEN		4u	/* hdr+12: transport cookie [I] */
#define HI5622_AIW_RPC_HDR_LEN		12u	/* u32 tag; u16 reserved; u16 msg_len; u32 seq */
#define HI5622_AIW_CFG_HDR_LEN		4u	/* u16 event; u16 payload len */
#define HI5622_AIW_REQ_HDR_LEN		8u	/* u32 ie_len; u8 type; u8 mode; u8 pad[2] */
#define HI5622_AIW_EVENT_SET_VENDOR_IE	0x0276u	/* SET_VENDOR_IE */
#define HI5622_AIW_EVENT_PAYLOAD_LEN	0x01f8u	/* 504 = the app_ie_req size */
#define HI5622_AIW_CFG_MSG_LEN		0x01fcu	/* 508 = 4 + 504 */
#define HI5622_AIW_GROUP		2u	/* hdr[0] low nibble (TABLES W3) */
#define HI5622_AIW_MSG_ID		0u	/* hdr[6..7] u16 LE */
#define HI5622_AIW_HCC_TAG		0x5a5au	/* hdr[10..11] u16 LE */
#define HI5622_AIW_RPC_TAG		1u	/* payload+0 u32 (wal_send_cfg_event arg1) */
#define HI5622_AIW_HDR_B1		0x01u	/* destination/alloc nibble (as the stock g0 0x20 frame) */
#define HI5622_AIW_WIRE_LEN		(HI5622_AIW_HDR_LEN + HI5622_AIW_COOKIE_LEN + \
					 HI5622_AIW_RPC_HDR_LEN + HI5622_AIW_CFG_HDR_LEN + \
					 HI5622_AIW_REQ_HDR_LEN + HI5622_AIW_WEXT_IE_MAX)	/* 536 */
#define HI5622_AIW_IE_OFF		(HI5622_AIW_HDR_LEN + HI5622_AIW_COOKIE_LEN + \
					 HI5622_AIW_RPC_HDR_LEN + HI5622_AIW_CFG_HDR_LEN + \
					 HI5622_AIW_REQ_HDR_LEN)				/* 40 */
#define HI5622_AIW_MIRROR_TYPES		20u	/* slot = vap + 20*type + 0x24c */
#define HI5622_AIW_MIRROR_IE_MAX	608u	/* firmware ie_len bound */

/* One decoded WEXT request (the hmac_config_set_vendor_ie request struct). */
struct hi5622_aiw_req {
	bool	valid;
	u32	ie_len;
	u8	type;
	u8	mode;		/* always HI5622_AIW_MODE_SAVE (the WEXT path) */
	u8	ie[HI5622_AIW_WEXT_IE_MAX];
};

/* One prepared object to post (the cfg-event envelope or the direct frame is
 * selected by the caller; this model only builds the envelope). */
struct hi5622_aiw_plan {
	bool	post;
	u8	ctx[3];		/* the HCC context trio: ctx[0] -> hdr[3] */
	u8	type;
	u32	seq;		/* the WAL cfg-event sequence (g_wal_config_seq_num) */
	size_t	ie_len;
	u8	ie[HI5622_AIW_WEXT_IE_MAX];
};

/* The host mirror of the per-VAP IE slot table (mac_vap_save_app_ie CA 0x2ac18:
 * slot = vap + 20*type + 0x24c = {ptr, len, cap, key[6]}; this model stores the
 * payload, the length and the capacity; the 6-byte key is always zero). */
struct hi5622_aiw_mirror_slot {
	bool	used;
	u32	cap;
	u32	len;
	u8	ie[HI5622_AIW_MIRROR_IE_MAX];
};

struct hi5622_aiw_mirror {
	struct hi5622_aiw_mirror_slot slot[HI5622_AIW_MIRROR_TYPES];
};

#define HI5622_AIW_MODE_SAVE		0u	/* payload byte 5 = 0 (path A) */

/*
 * Parse one userspace WEXT request buffer (the struct of TABLES W1) into the
 * hmac-side request.  Fail-closed (-EINVAL): NULL/short buffer, cmd != 0x8eec,
 * ie_len > 496, type > 19, or the buffer shorter than 12+ie_len.  mode is
 * always 0: the WEXT ABI has no mode field.
 */
int hi5622_aiw_wext_parse(const u8 *ubuf, size_t ulen,
			  struct hi5622_aiw_req *out);

/*
 * Strip the first IEEE 802.11 element id 0x7f from an IE list in place (the
 * vendor's mac_find_ie(0x7f) step in hmac_config_set_app_ie_to_vap).  Returns
 * 0 whether or not an element was removed; -EINVAL on a malformed TLV list.
 */
int hi5622_aiw_strip_7f(u8 *ie, size_t *ie_len);

/*
 * Save/clear one host-mirror slot, exactly the vendor mac_vap_save_app_ie
 * semantics: type <= 19; ie_len == 0 clears (ptr/len/cap and the 6-byte key);
 * otherwise grow-if-needed (cap tracks the allocation), copy the IE bytes and
 * report the new length.  Fail-closed (-EINVAL) on type > 19 or ie_len > 608.
 * Returns 0 on success; *out_len (optional) reports the stored length.
 */
int hi5622_aiw_mirror_save(struct hi5622_aiw_mirror *m, unsigned int type,
			   const u8 *ie, size_t ie_len,
			   unsigned int *out_len);

/*
 * Decide, fail-closed, whether one cfg-event envelope may be built: mode 1
 * only (the WEXT envelope; mode 0 is the sibling direct-frame path), type <=
 * 19, ctx[1]/ctx[2] <= 0xf, ie_len <= 496, well-formed IE TLVs.  app_ie_mode
 * values above 1 are refused.  mode 0 returns a plan with post = false.
 */
int hi5622_aiw_plan(unsigned int app_ie_mode, unsigned int type,
		    const u8 *ctx3, u32 seq,
		    const u8 *ies, size_t ies_len,
		    struct hi5622_aiw_plan *out);

/*
 * Build the fixed 536-byte group-2 / id-0 WAL cfg-event 0x0276 frame
 * (TABLES W3).  Fail-closed (-EINVAL) on a non-post plan, type > 19,
 * ie_len > 496, a bad TLV list; -ENOSPC on a short buffer.  The 496-byte IE
 * area is zero padded: the vendor sends the whole fixed 504-byte request.
 */
int hi5622_aiw_cfg_event_build(u8 *wire, size_t cap, size_t *out_len,
			       const struct hi5622_aiw_plan *plan);

/*
 * One read-only status line for the driver's cfg80211_status attribute.
 */
int hi5622_aiw_status_line(unsigned int app_ie_mode, unsigned int app_ie_when,
			   unsigned int ctx0, int last_rc,
			   unsigned int posted, unsigned int refused,
			   unsigned int mirror_len, unsigned int mirror_used,
			   char *buf, size_t len);

#endif /* HI5622_APP_IE_WEXT_H */
