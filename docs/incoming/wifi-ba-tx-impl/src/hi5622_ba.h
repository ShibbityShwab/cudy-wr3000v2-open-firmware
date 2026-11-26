/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_ba.h - fail-closed TX-direction block-ack (BA) core for the
 * Hi5622V100 clean-room Wi-Fi driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * Lane wifi-ba-tx-impl (shared task-217, 2026-11-26).  PORTED from the
 * committed BA skeleton (../wifi-ba-agg/src/hi5622_ba.{c,h}, task-178,
 * artifact 78460841...cc33de02, 183 host checks) and extended with the
 * TX-direction cut:
 *
 *   - the in-kernel ADDBA negotiation with the AP as TX originator
 *     (hi5622_ba_tx_data(): first QoS MPDU to a bound peer -> ADDBA request;
 *      the received ADDBA response -> g0 0x0A relay + TX BA notify + active);
 *   - complete HCC wires (12-byte header + payload) for the three H2D BA
 *     messages, byte-pinned in the host tests:
 *       g0 0x0A BA relay        44 B (12 + 32)
 *       g0 0x0B A-MPDU start/end 20 B (12 +  8)
 *       g0 0x1C TX BA notify     32 B (12 + 20)
 *   - the g0 0x1C TX BA notify emitted on the pending->active transition
 *     (value 0) and on teardown (value 2), per the decoded vendor state
 *     machine;
 *   - the A-MPDU window boundaries on the TX path (start before a burst,
 *     end after HI5622_BA_BURST_MAX MPDUs / at burst end / at teardown);
 *   - a read-only status snapshot for the driver's cfg80211_status evidence.
 *
 * Wire facts and their evidence:
 *   ../wifi-ba-agg/TABLES.md T3-T5/T8 (host-module decode) and
 *   ../wifi-ba-agg/REPORT.md sec. 4/7.  The HCC header geometry of the three
 *   messages was re-verified this lane from the pinned local vendor module
 *   with objdump (hdr[0] group, hdr[6] id, total at hdr[4], tag 0x5a5a at
 *   hdr[10], alloc nibble 1 / dst nibble hdr[1].high == hdr[9]); see
 *   ../TABLES.md T1 of this lane.  Only derived addresses/byte values are
 *   documented; no vendor binary is reproduced.
 *
 * Fail-closed contract (unchanged from the skeleton): every public entry
 * point is inert unless the module parameter ba_enable == 1.  ba_enable == 0
 * (default) returns -EOPNOTSUPP without building a frame, posting a message
 * or changing state; ba_enable > 1 returns -EINVAL.  No caller may bypass it.
 * The core is callback-driven (no device access), so the same code compiles
 * in the kernel module and in the host selftest (-DHI5622_BA_HOST_TEST).
 */
#ifndef HI5622_BA_H
#define HI5622_BA_H

#ifdef HI5622_BA_HOST_TEST
#include <stddef.h>
#include <stdint.h>
/* shared with h2d_frames.h (the host test compiles both) */
#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t  s16;
typedef int32_t  s32;
#endif
#else
#include <linux/types.h>
#endif

/* ---- decoded constants [V] --------------------------------------------- */

#define HI5622_BA_H2D_BA_RELAY		0x0A	/* 32 B payload, 44 B wire */
#define HI5622_BA_H2D_AMPDU_TX		0x0B	/*  8 B payload, 20 B wire */
#define HI5622_BA_H2D_TX_BA_NOTIFY	0x1C	/* 20 B payload, 32 B wire */

#define HI5622_BA_H2D_GROUP_CHIP	0u	/* HCC group 0 = chip/hmac     */
/* The relay and A-MPDU builders write hdr[0] group nibble 0 (verified this
 * lane from the vendor module: hmac_mgmt_rx_addba_rsp 0x45a84 bfi r2,r5,#0,4;
 * the 0x0B builders preserve hcc_msg_alloc's group).  The vendor's TX BA
 * notify builder forces hdr[0] = 2 (hmac_tx_ba_notify 0x16b90: mov r2,#2 /
 * bfi r3,r2,#0,4) while the firmware advertises only a GROUP-0 id-0x1C
 * handler (CA 0x5DFA0).  This lane posts the notify as group 0 (the
 * handler-admitted pair; the informational notify must not weaken the
 * driver's post gate) and records the vendor group-2 byte as a documented
 * divergence - see REPORT.md sec. "the group byte" and TABLES.md T1. */
#define HI5622_BA_H2D_GROUP_RELAY	0u
#define HI5622_BA_H2D_GROUP_AMPDU	0u
#define HI5622_BA_H2D_GROUP_NOTIFY	0u

#define HI5622_BA_D2H_CREATE_BA		0x02
#define HI5622_BA_D2H_DEL_BA		0x03
#define HI5622_BA_D2H_RX_DELBA_EVENT	0x0F
#define HI5622_BA_D2H_BA_ACTION		0x1D

#define HI5622_BA_RELAY_TYPE		3
#define HI5622_BA_RELAY_ADDBA_RSP	1
#define HI5622_BA_RELAY_DELBA		2

#define HI5622_BA_CATEGORY		3
#define HI5622_BA_ACTION_ADDBA_REQ	0
#define HI5622_BA_ACTION_ADDBA_RSP	1
#define HI5622_BA_ACTION_DELBA		2

#define HI5622_BA_HCC_TAG		0x5a5au
#define HI5622_BA_HDR_LEN		12u
#define HI5622_BA_ALLOC_NIBBLE		0x01u
#define HI5622_BA_RELAY_PAYLOAD_LEN	32u
#define HI5622_BA_RELAY_WIRE_LEN	44u
#define HI5622_BA_AMPDU_PAYLOAD_LEN	8u
#define HI5622_BA_AMPDU_WIRE_LEN	20u
#define HI5622_BA_NOTIFY_PAYLOAD_LEN	20u
#define HI5622_BA_NOTIFY_WIRE_LEN	32u
#define HI5622_BA_WIRE_MAX		44u
#define HI5622_BA_FRAME_ADDBA_REQ_LEN	33u
#define HI5622_BA_FRAME_ADDBA_RSP_LEN	33u
#define HI5622_BA_FRAME_DELBA_LEN	30u
#define HI5622_BA_ACTION_MIN_LEN	9u
#define HI5622_BA_FRAME_MAX		33u

#define HI5622_BA_MAX_PEERS		8
#define HI5622_BA_MAX_TID		8
#define HI5622_BA_BAW_MIN		1
#define HI5622_BA_BAW_MAX		256
#define HI5622_BA_BAW_DEFAULT		64
#define HI5622_BA_TIMEOUT_DEFAULT	0	/* vendor sends 0 (no timeout) */
#define HI5622_BA_BURST_MAX		32

/* TX BA notify values (the vendor state machine: 0 = pending->active,
 * 2 = teardown) [V]. */
#define HI5622_BA_NOTIFY_ACTIVE		0u
#define HI5622_BA_NOTIFY_TEARDOWN	2u

#define HI5622_BA_OK			0
#define HI5622_BA_EINVAL		(-22)
#define HI5622_BA_ENOSPC		(-28)
#define HI5622_BA_EOPNOTSUPP		(-95)
#define HI5622_BA_EAGAIN		(-11)

/* ---- wire structures --------------------------------------------------- */

struct hi5622_ba_params {
	u8  tid;
	u8  dialog;
	u8  policy;		/* BA parameter set bit 1 */
	u8  amsdu;		/* BA parameter set bit 0 */
	u16 baw;		/* buffer size, 10-bit field, 1..256 */
	u16 timeout_tu;		/* BA timeout in TU */
	u16 start_seq;		/* starting sequence control */
	u16 status;		/* ADDBA response status (0 = success) */
};

struct hi5622_ba_action {
	u8  action;		/* 0 req, 1 rsp, 2 delba */
	u8  tid;
	u8  dialog;
	u8  policy;
	u8  amsdu;
	u8  initiator;		/* DELBA only */
	u16 baw;
	u16 timeout_tu;
	u16 start_seq;
	u16 status;		/* ADDBA rsp only */
	u16 reason;		/* DELBA only */
};

struct hi5622_ba_session {
	u8  used;
	u8  state;		/* 0 idle, 1 ADDBA pending, 2 active */
	u8  tid;
	u8  dialog;
	u8  policy;
	u8  amsdu;
	u8  ampdu_started;
	u8  burst_count;
	u16 baw;
	u16 timeout_tu;
	u16 start_seq;
	u16 last_seq;
};

struct hi5622_ba_peer {
	u8  used;
	u8  mac[6];
	u16 uid;		/* firmware user index (vendor user+0xa84) */
	struct hi5622_ba_session tx[HI5622_BA_MAX_TID];
};

struct hi5622_ba_stats {
	u32 addba_req;		/* ADDBA request action frames sent     */
	u32 addba_rsp;		/* ADDBA response action frames sent    */
	u32 delba;		/* DELBA action frames sent             */
	u32 relay_rsp;		/* g0 0x0A subtype 1 posted             */
	u32 relay_delba;	/* g0 0x0A subtype 2 posted             */
	u32 ampdu_start;	/* g0 0x0B start posted                 */
	u32 ampdu_end;		/* g0 0x0B end posted                   */
	u32 notify;		/* g0 0x1C posted                       */
	u32 sessions;		/* sessions that reached active         */
	u32 refused;		/* entry points refused (gate/args)     */
	u32 d2h_events;		/* BA D2H events consumed               */
	u32 last_rc;		/* last post/action return code         */
};

struct hi5622_ba_core {
	struct hi5622_ba_peer peers[HI5622_BA_MAX_PEERS];
	u8  own_mac[6];		/* our SA / BSSID for action frames */
	u8  bssid[6];
	struct hi5622_ba_stats stats;
};

struct hi5622_ba_ops {
	void *ctx;
	/* post one complete HCC wire (12 B header + payload) through the
	 * driver's single post path.  The core has already filled group, id,
	 * total length, tag and payload.  Return 0 on success. */
	int (*post_h2d)(void *ctx, const u8 *wire, size_t total_len);
	/* transmit one 802.11 action frame through the proven mgmt-TX path */
	int (*tx_action)(void *ctx, const u8 *frame, size_t len);
	void (*log)(void *ctx, const char *msg);
};

/* ---- module param / enable gate ---------------------------------------- */

extern unsigned int hi5622_ba_enable;
int hi5622_ba_check_enabled(void);	/* 0, -EOPNOTSUPP or -EINVAL */

/* ---- core -------------------------------------------------------------- */

void hi5622_ba_core_init(struct hi5622_ba_core *core);
void hi5622_ba_core_set_addrs(struct hi5622_ba_core *core, const u8 *own_mac,
			      const u8 *bssid);
struct hi5622_ba_peer *hi5622_ba_peer_get(struct hi5622_ba_core *core,
					  const u8 *mac);
struct hi5622_ba_peer *hi5622_ba_peer_get_by_uid(struct hi5622_ba_core *core,
						 u16 uid);
/* Bind (create or refresh) the peer and its firmware user index.  The glue
 * calls this from the cfg80211 station table before every D2H/action event;
 * an unknown uid never creates a peer (fail closed, no guessed MAC). */
struct hi5622_ba_peer *hi5622_ba_peer_bind(struct hi5622_ba_core *core,
					   const u8 *mac, u16 uid);
void hi5622_ba_peer_drop(struct hi5622_ba_core *core,
			 struct hi5622_ba_peer *peer);
u32 hi5622_ba_active_sessions(const struct hi5622_ba_core *core);

/* ---- byte-exact action-frame builders (vendor shapes) ------------------ */

size_t hi5622_ba_build_addba_req(u8 *frame, size_t cap,
				 const struct hi5622_ba_params *p,
				 const u8 *da, const u8 *sa, const u8 *bssid);
size_t hi5622_ba_build_addba_rsp(u8 *frame, size_t cap,
				 const struct hi5622_ba_params *p,
				 const u8 *da, const u8 *sa, const u8 *bssid);
size_t hi5622_ba_build_delba(u8 *frame, size_t cap, u8 tid, u8 initiator,
			     u16 reason, const u8 *da, const u8 *sa,
			     const u8 *bssid);

/* ---- byte-exact payload builders (vendor shapes, T3-T5) ---------------- */

size_t hi5622_ba_build_relay_addba_rsp(u8 *payload, size_t cap, u16 uid,
				       const struct hi5622_ba_params *p);
size_t hi5622_ba_build_relay_delba(u8 *payload, size_t cap, u16 uid, u8 tid,
				   u8 initiator);
size_t hi5622_ba_build_ampdu_start(u8 *payload, size_t cap, u8 tid, u16 seq,
				   u16 uid, u8 flag);
size_t hi5622_ba_build_ampdu_end(u8 *payload, size_t cap, u8 tid, u16 uid);
size_t hi5622_ba_build_tx_notify(u8 *payload, size_t cap, u16 uid, u8 tid,
				 u8 value);

/* ---- complete HCC wire builders (12 B header + payload) ---------------- */

/* Fill a complete 44/20/32-byte wire (group/id/total/tag/payload) and return
 * its length, 0 on a refused argument or disabled gate.  These are the byte
 * shapes the host tests pin and the glue posts through hi5622_frame_post()
 * (which re-validates group/id/length/tag/dst/alloc against the firmware
 * handler tables before any device write). */
size_t hi5622_ba_wire_relay_addba_rsp(u8 *wire, size_t cap, u16 uid,
				      const struct hi5622_ba_params *p);
size_t hi5622_ba_wire_relay_delba(u8 *wire, size_t cap, u16 uid, u8 tid,
				  u8 initiator);
size_t hi5622_ba_wire_ampdu_start(u8 *wire, size_t cap, u8 tid, u16 seq,
				  u16 uid, u8 flag);
size_t hi5622_ba_wire_ampdu_end(u8 *wire, size_t cap, u8 tid, u16 uid);
size_t hi5622_ba_wire_tx_notify(u8 *wire, size_t cap, u16 uid, u8 tid,
				u8 value);
/* Wrap one raw payload into a complete header (id/group per message). */
void hi5622_ba_wrap_h2d(u8 *wire, size_t payload_len, u8 group, u8 id);

/* ---- parsers ----------------------------------------------------------- */

int hi5622_ba_parse_action(const u8 *frame, size_t len,
			   struct hi5622_ba_action *out);

/* ---- high-level state machine ------------------------------------------ */

/* Host-initiated TX BA session: send the ADDBA request, state = pending. */
int hi5622_ba_tx_start(struct hi5622_ba_core *core,
		       const struct hi5622_ba_ops *ops,
		       const u8 *mac, const struct hi5622_ba_params *p);
/* The in-kernel TX-originator step for one QoS data MPDU:
 *   idle    -> start the ADDBA negotiation (one request, state pending);
 *   pending -> -EAGAIN (the request is outstanding; never aggregate yet);
 *   active  -> emit the g0 0x0B A-MPDU start/end window boundaries.
 * Returns 0 on success, -EOPNOTSUPP when no session/peer, or a refused arg. */
int hi5622_ba_tx_data(struct hi5622_ba_core *core,
		      const struct hi5622_ba_ops *ops, const u8 *mac, u8 tid,
		      u16 seq);
int hi5622_ba_mgmt_rx(struct hi5622_ba_core *core,
		      const struct hi5622_ba_ops *ops, u16 uid, const u8 *mac,
		      const u8 *frame, size_t len);
int hi5622_ba_d2h(struct hi5622_ba_core *core,
		  const struct hi5622_ba_ops *ops, const u8 *hdr, size_t len);
int hi5622_ba_tx_mpdu(struct hi5622_ba_core *core,
		      const struct hi5622_ba_ops *ops, const u8 *mac, u8 tid,
		      u16 seq);
int hi5622_ba_tx_burst_end(struct hi5622_ba_core *core,
			   const struct hi5622_ba_ops *ops, const u8 *mac,
			   u8 tid);

/* ---- read-only status (cfg80211_status evidence) ----------------------- */

int hi5622_ba_status_line(const struct hi5622_ba_core *core,
			  unsigned int ba_enable, char *buf, size_t len);

#endif /* HI5622_BA_H */
