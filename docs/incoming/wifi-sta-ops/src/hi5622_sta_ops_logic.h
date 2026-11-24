/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_sta_ops_logic.h - portable, kernel-free station-operations logic for
 * the Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-sta-ops, shared task task-149,
 * 2026-11-24).  Only derived wire facts are encoded here; the vendor host
 * module / firmware image is never linked or reproduced.  Every constant is
 * re-derived by tools/sta_ops_re.py from the local (gitignored) artifacts and
 * re-asserted by tools/sta_ops_selftest.c against LITERAL wire offsets.
 *
 * WHAT THE VENDOR DOES (2.5.24 link-time .text offsets; PD4 firmware
 * workers are file offsets + 0x40000 = CA)
 * -------------------------------------------------------------------
 * AP client station creation (the hmac AP-SME path; the cfg80211
 * .add_station/.change_station ops are 8-byte "return 0" stubs because the
 * vendor does the AP SME in its own hmac layer):
 *
 *   hmac_ap_assoc_check_chip_exist_user_proc  +0x4e598
 *     -> hmac_ap_rx_auth_chip_find_user_proc  +0x9fb90  (find/alloc user)
 *     -> hmac_user_add                        +0x8eb34  (g0 id7, 180 B)
 *     -> hmac_user_get_valid / hmac_user_init_key
 *     -> mac_user_set_asoc_state(user+0xa6c, state=1)
 *     -> hmac_config_user_asoc_state_syn      +0x63338  (g0 id1 cmd 0x07d2)
 *
 *   hmac_config_user_asoc_state_syn sends a g0 id1 config event:
 *     cmd = 0x07d2, body 4 B {u16 AID/user_id; u8 asoc_state; u8 pad}
 *   Firmware table A (CA 0x10c23c, 8-byte rows {u16 key; u16 pad; u32 worker})
 *   has row key 0x07d2 -> worker CA 0x83e25 [V], and that worker reads
 *   body[0..1] as the user id (lookup), writes body[2] to user+0x3d
 *   (the same "association state" byte the host mirror writes), and returns
 *   0 / 0x64 (user not found).
 *
 *   hmac_user_add sends a g0 id7 HCC message, 180 B wire (12 B header +
 *   168 B body).  Body fields it writes: +0x00 u16 user id/AID (from
 *   user+0xa84), +0x02 u8 MAC[6], +0x8e u8 user type (2 for an AP mode VAP),
 *   +0xa4 s8 RSSI (+100 when the host has no scanned BSS entry).
 *
 *   hmac_del_user_notify_dmac +0x8d7f0 (called from hmac_user_del):
 *   g0 id9, 24 B wire (12 + 12): body {u16 user id/AID, u8 MAC[6],
 *   u8 flag} - flag is 2 for an AP-mode VAP user.
 *
 *   vendor cfg80211 .del_station +0x102470 posts the WAL cfg-event kick
 *   (opcode 0x00d0, body 16 B {u8 peer MAC[6]; u16 reason; u8 subtype_flag;
 *   pad[7]; u8 1}) where subtype_flag = 0xc0 when the cfg80211
 *   station_del_parameters.subtype is 12 (deauthentication), else 0.  This
 *   refines wifi-sta-static TABLES T3 (which saw +0x0f = 1 only).
 *
 * EAPOL / data RX (vendor data path):
 *   hmac_rx_ring_entry_proc (+0x101e8) -> hmac_rx_ring_entry_proc_frame
 *   (+0xf258) -> hmac_rx_frame_80211_to_eth (+0x125f8):
 *     - strip the 802.11 MAC header; length from the frame control:
 *       24 B base, 30 B for a 4-address frame, +2 for QoS, +4 for HT control
 *       (hmac_rx_get_mac_header_len +0x13b94);
 *     - LLC/SNAP type extraction (hmac_rx_frame_get_snap_type +0x12274);
 *     - build a 14-byte Ethernet header: for FromDS=0 frames src = addr2
 *       and dst = (ToDS ? addr3 : addr1); for FromDS=1 frames src =
 *       addr3/addr4 and dst = addr1;
 *     - EAPOL detection: ethertype 0x888e; EAPOL-Key = EAPOL type 3 with
 *       length > 0x5e; pairwise = EAPOL-Key key-info bit 3; a PTK
 *       "4/4-like" message additionally has key-data-length 0
 *       (hmac_is_eapol_key_ptk_4_4 +0x18750 -> mac_is_eapol_key_ptk
 *       +0x23094; the vendor uses it on the TX completion of the message).
 *   Delivery to a host netdev is the clean-room driver's job: the vendor
 *   forwards to its own offload/bridging layer (hyfi), not netif_rx (only
 *   its proxy-ARP/ND path calls netif_rx).  This driver uses the standard
 *   cfg80211 full-MAC convention: convert to Ethernet and netif_rx() on the
 *   AP VAP netdev.
 *
 * FAIL-CLOSED: every builder is pure and refuses impossible input; the
 * kernel glue only posts when the sta_ops/eapol_rx module params are set and
 * the RX delivery is gated by station lookup + authorization.
 */
#ifndef HI5622_STA_OPS_LOGIC_H
#define HI5622_STA_OPS_LOGIC_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
#endif
#endif

/* ===================================================================
 * 1. Wire constants (decoded; every one is re-derived by sta_ops_re.py)
 * =================================================================== */
#define HSOP_HCC_HDR_LEN	12u
#define HSOP_HCC_GROUP_CHIP	0u
#define HSOP_ALLOC_NIBBLE	0x01u
#define HSOP_HCC_TAG		0x5a5au

/* g0 id7 user_add: 12 B header + 168 B body = 180 B [V]. */
#define HSOP_ID_USER_ADD	7u
#define HSOP_USER_ADD_BODY_LEN	168u
#define HSOP_USER_ADD_WIRE_LEN	(HSOP_HCC_HDR_LEN + HSOP_USER_ADD_BODY_LEN)
#define HSOP_UA_OFF_UID		0x00u	/* u16 user id / AID            */
#define HSOP_UA_OFF_MAC		0x02u	/* u8[6] MAC                    */
#define HSOP_UA_OFF_TYPE	0x8eu	/* u8 user type (2 = normal AP) */
#define HSOP_UA_OFF_RSSI	0xa4u	/* s8 RSSI (+100 default)      */
#define HSOP_USER_TYPE_AP	2u
#define HSOP_DEFAULT_RSSI	100

/* g0 id9 user_del: 12 B header + 12 B body = 24 B [V]. */
#define HSOP_ID_USER_DEL	9u
#define HSOP_USER_DEL_BODY_LEN	12u
#define HSOP_USER_DEL_WIRE_LEN	(HSOP_HCC_HDR_LEN + HSOP_USER_DEL_BODY_LEN)
#define HSOP_UD_OFF_UID		0x00u	/* u16 user id / AID */
#define HSOP_UD_OFF_MAC		0x02u	/* u8[6] MAC         */
#define HSOP_UD_OFF_FLAG	0x08u	/* u8 flag (2 = AP)  */
#define HSOP_USER_DEL_FLAG_AP	2u

/* g0 id1 config event (the proven key-lane carrier shape):
 *   hdr[12] | token[8] | cmd u16 | len u16 | body[len] | pad[4]  (0xff pad)
 * The firmare g0 id1 handler receives the message with the data pointer at
 * the token, i.e. cmd = payload+8, len = payload+10, body = payload+12 [V]. */
#define HSOP_ID_CFG_EVENT	1u
#define HSOP_CFG_OVERHEAD	28u	/* 12 hdr + 8 token + 2 cmd + 2 len + 4 pad */
#define HSOP_CFG_CMD_ASOC_SYN	0x07d2u
#define HSOP_ASOC_SYN_BODY_LEN	4u
#define HSOP_ASOC_SYN_WIRE_LEN	(HSOP_CFG_OVERHEAD + HSOP_ASOC_SYN_BODY_LEN)
#define HSOP_ASOC_OFF_UID	0x00u	/* u16 AID/user id   */
#define HSOP_ASOC_OFF_STATE	0x02u	/* u8 association state */
#define HSOP_ASOC_STATE_ASSOC	1u	/* hmac_ap_* state=1 [V] */
#define HSOP_ASOC_STATE_FT	3u	/* hmac_ft_assoc state=3 [V] */
#define HSOP_CFG_TOKEN_BYTE	4u	/* token[8] pattern byte (proven key carrier) */
#define HSOP_CFG_TOKEN_VALUE	0x01u
#define HSOP_CFG_PAD_VALUE	0xffu

/* WAL cfg-event kick, opcode 0x00d0, inherited H2D_WAL_CMD_KICK_STA. */
#define HSOP_WAL_KICK_CMD	0x00d0u
#define HSOP_KICK_BODY_LEN	16u
#define HSOP_KICK_OFF_MAC	0x00u
#define HSOP_KICK_OFF_REASON	0x06u
#define HSOP_KICK_OFF_SUBTYPE	0x08u	/* 0xc0 = deauth (subtype 12) [V] */
#define HSOP_KICK_OFF_LAST	0x0fu	/* vendor constant 1 [V] */
#define HSOP_KICK_SUBTYPE_DEAUTH 12u
#define HSOP_KICK_FLAG_DEAUTH	0xc0u

/* ===================================================================
 * 2. Neutral parameter structs + wire builders
 * =================================================================== */
struct hsop_user_add_params {
	u8	band;		/* 0 = 2.4 GHz (header high nibble)   */
	u8	vap_id;		/* header[3]                          */
	u8	hdr2;		/* header[2] (vendor vap+0xfa9, [U])   */
	u8	dst;		/* HCC destination nibble (0 = local) */
	u16	aid;		/* firmware user id / AID             */
	u8	mac[6];
	u8	type;		/* 2 = normal AP-mode user             */
	s8	rssi;
};

struct hsop_user_del_params {
	u8	band;
	u8	vap_id;
	u8	hdr2;
	u8	dst;
	u16	uid;
	u8	mac[6];
	u8	flag;		/* 2 = AP-mode user */
};

struct hsop_asoc_syn_params {
	u8	band;
	u8	vap_id;
	u8	hdr2;
	u8	dst;
	u8	module;		/* g0 header module/VAP index          */
	u16	aid;
	u8	state;
};

struct hsop_kick_params {
	u8	mac[6];
	u16	reason;
	u8	subtype;	/* cfg80211 station_del_parameters.subtype */
};

/* Builders: fill the complete HCC wire frame; return 0 / -errno.
 * The caller supplies a buffer of at least the matching *_WIRE_LEN. */
int hsop_build_user_add(u8 *wire, size_t cap, size_t *out_len,
			const struct hsop_user_add_params *p);
int hsop_build_user_del(u8 *wire, size_t cap, size_t *out_len,
			const struct hsop_user_del_params *p);
int hsop_build_asoc_syn(u8 *wire, size_t cap, size_t *out_len,
			const struct hsop_asoc_syn_params *p);
int hsop_build_kick_body(u8 *body, size_t cap,
			 const struct hsop_kick_params *p);

/* Structural re-check of a built frame (0 = acceptable).  These re-read the
 * wire bytes, so a header regression cannot make the host test lie. */
int hsop_check_user_add(const u8 *wire, size_t len);
int hsop_check_user_del(const u8 *wire, size_t len);
int hsop_check_asoc_syn(const u8 *wire, size_t len);

/* ===================================================================
 * 3. Host station table (mirrors the cfg80211 station state)
 * =================================================================== */
#define HSOP_MAX_STA	32u

struct hsop_sta {
	u8	used;
	u8	mac[6];
	u16	aid;
	int	authorized;	/* cfg80211 STA_FLAG_AUTHORIZED set  */
	int	authenticated;
	int	associated;
	u32	flags_mask;	/* last station_parameters mask/set  */
	u32	flags_set;
};

struct hsop_sta_stats {
	u32	added;
	u32	updated;
	u32	deleted;
	u32	refused;
	u32	flush;
	u32	dup_updates;
	u32	unknown_del;
};

struct hsop_sta_table {
	struct hsop_sta	sta[HSOP_MAX_STA];
	struct hsop_sta_stats stats;
};

void hsop_sta_table_init(struct hsop_sta_table *t);
int hsop_sta_add(struct hsop_sta_table *t, const u8 *mac, u16 aid,
		 u32 flags_mask, u32 flags_set);
struct hsop_sta *hsop_sta_find(struct hsop_sta_table *t, const u8 *mac);
const struct hsop_sta *hsop_sta_find_const(const struct hsop_sta_table *t,
					   const u8 *mac);
int hsop_sta_update(struct hsop_sta_table *t, const u8 *mac,
		    u32 flags_mask, u32 flags_set);
int hsop_sta_del(struct hsop_sta_table *t, const u8 *mac);
u32 hsop_sta_count(const struct hsop_sta_table *t);
/* Return the unix-time-ish index-th station (dump_station helper):
 * 0 = found, -ENOENT past the end. */
int hsop_sta_at(const struct hsop_sta_table *t, u32 index, struct hsop_sta *out);

/* ===================================================================
 * 4. 802.11 data RX -> Ethernet conversion + EAPOL classification
 *
 * These are the vendor rules decoded from hmac_rx_frame_80211_to_eth /
 * hmac_rx_get_mac_header_len / hmac_rx_frame_get_snap_type, restricted to
 * what a host AP needs: data frames from an associated station.
 * =================================================================== */
#define HSOP_ETH_HDR_LEN	14u
#define HSOP_ETH_P_PAE		0x888eu
#define HSOP_ETH_P_MAX		0xffffu
#define HSOP_SNAP_HDR_LEN	8u	/* AA AA 03 00 00 00 + ethertype */
#define HSOP_EAPOL_HDR_LEN	4u
#define HSOP_EAPOL_KEY_MIN	99u	/* 4 + 95 fixed EAPOL-Key bytes  */
#define HSOP_EAPOL_KEY_INFO_OFF	5u	/* inside the EAPOL frame        */
#define HSOP_EAPOL_KEYDLEN_OFF	97u	/* inside the EAPOL frame        */
#define HSOP_EAPOL_KEY_PAIRWISE	0x0008u	/* key-info bit 3                */

struct hsop_rx_info {
	u16	fc;		/* frame control (little-endian)     */
	int	is_data;	/* 802.11 type == 2                  */
	u8	type;
	u8	subtype;
	int	tods;
	int	fromds;
	u32	hdr_len;	/* 802.11 header length              */
	u32	payload_off;	/* = hdr_len + SNAP (when present)   */
	u8	src[6];
	u8	dst[6];
	int	has_snap;
	u16	ethertype;	/* host order                        */
	int	is_eapol;
	u8	eapol_type;
	u16	eapol_len;
	int	eapol_key;
	int	eapol_key_pairwise;
	u16	eapol_key_datalen;
	int	eapol_ptk_done_like; /* pairwise + key-datalen 0 [V] */
};

/* Parse an 802.11 MPDU as delivered by the MAC.  -EINVAL/-ENOSPC style
 * negative errors refuse a frame; is_data is set for data frames even when
 * the LLC/SNAP type is absent (ethertype 0). */
int hsop_rx_parse(const u8 *mpdu, u32 len, struct hsop_rx_info *out);

/* Convert one parsed data MPDU to a complete Ethernet frame (14 B header +
 * payload).  Refuses non-data frames and frames without an LLC/SNAP. */
int hsop_rx_to_eth(const u8 *mpdu, u32 len, u8 *eth, size_t cap,
		   size_t *eth_len, struct hsop_rx_info *out);

/* True when the frame may be delivered to the host for the given station
 * authorization state: EAPOL always (the 4-way needs it before the key is
 * installed), other data only when authorized. */
int hsop_rx_deliverable(const struct hsop_rx_info *rx, int authorized);

#endif /* HI5622_STA_OPS_LOGIC_H */
