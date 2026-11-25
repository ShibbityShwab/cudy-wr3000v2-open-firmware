/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_ba.h - fail-closed block-ack (BA) core for the Hi5622V100 clean-room
 * Wi-Fi driver.
 *
 * Lane wifi-ba-agg (shared task-178).  Everything here is decoded from the
 * vendor host module hi5622v100_wifi.ko 2.5.24 (see ../TABLES.md); the wire
 * shapes are the vendor's, byte for byte.  The core is callback-driven so it
 * compiles standalone for the host selftest (src/ba_selftest.c) and as a
 * kernel module file.
 *
 * Fail-closed contract: every public entry point is inert unless the module
 * parameter ba_enable == 1.  ba_enable == 0 (default) returns -EOPNOTSUPP
 * without building a frame, posting a message or changing state; ba_enable > 1
 * returns -EINVAL.  No caller may bypass this.
 *
 * Wire shapes (vendor parity, see TABLES.md T3-T5, T8):
 *   g0 0x0A ADDBA/DELBA relay   12 B hdr + 32 B payload = 44 B
 *   g0 0x0B A-MPDU TX start/end 12 B hdr +  8 B payload = 20 B
 *   g0 0x1C TX BA notify        12 B hdr + 20 B payload = 32 B
 *   ADDBA request action frame  33 B
 *   ADDBA response action frame 33 B
 *   DELBA action frame          30 B
 */
#ifndef HI5622_BA_H
#define HI5622_BA_H

#ifdef HI5622_BA_HOST_TEST
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#else
#include <linux/types.h>
#endif

/* ---- decoded constants ------------------------------------------------- */

#define HI5622_BA_H2D_BA_RELAY		0x0A	/* 32 B payload */
#define HI5622_BA_H2D_AMPDU_TX		0x0B	/*  8 B payload */
#define HI5622_BA_H2D_TX_BA_NOTIFY	0x1C	/* 20 B payload */

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

#define HI5622_BA_HDR_LEN		12
#define HI5622_BA_RELAY_PAYLOAD_LEN	32
#define HI5622_BA_RELAY_TOTAL_LEN	44
#define HI5622_BA_AMPDU_PAYLOAD_LEN	8
#define HI5622_BA_AMPDU_TOTAL_LEN	20
#define HI5622_BA_NOTIFY_PAYLOAD_LEN	20
#define HI5622_BA_NOTIFY_TOTAL_LEN	32
#define HI5622_BA_FRAME_ADDBA_REQ_LEN	33
#define HI5622_BA_FRAME_ADDBA_RSP_LEN	33
#define HI5622_BA_FRAME_DELBA_LEN	30
#define HI5622_BA_ACTION_MIN_LEN	9

#define HI5622_BA_MAX_PEERS		8
#define HI5622_BA_MAX_TID		8
#define HI5622_BA_BAW_MIN		1
#define HI5622_BA_BAW_MAX		256
#define HI5622_BA_BAW_DEFAULT		64
#define HI5622_BA_BURST_MAX		32

#define HI5622_BA_OK			0
#define HI5622_BA_EINVAL		(-22)
#define HI5622_BA_ENOSPC		(-28)
#define HI5622_BA_EOPNOTSUPP		(-95)

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
};

struct hi5622_ba_peer {
	u8  used;
	u8  mac[6];
	u16 uid;		/* firmware user index (user+0xa84) */
	struct hi5622_ba_session tx[HI5622_BA_MAX_TID];
};

struct hi5622_ba_core {
	struct hi5622_ba_peer peers[HI5622_BA_MAX_PEERS];
	u8  own_mac[6];		/* our SA / BSSID for action frames */
	u8  bssid[6];
};

struct hi5622_ba_ops {
	void *ctx;
	/* post one group-0 H2D message: msg = 12 B header + payload (the
	 * driver adds the transport tag / length).  Return 0 on success. */
	int (*post_h2d)(void *ctx, const u8 *msg, size_t total_len);
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
void hi5622_ba_peer_drop(struct hi5622_ba_core *core,
			 struct hi5622_ba_peer *peer);

/* ---- byte-exact builders (vendor shapes) ------------------------------- */

size_t hi5622_ba_build_addba_req(u8 *frame, size_t cap,
				 const struct hi5622_ba_params *p,
				 const u8 *da, const u8 *sa, const u8 *bssid);
size_t hi5622_ba_build_addba_rsp(u8 *frame, size_t cap,
				 const struct hi5622_ba_params *p,
				 const u8 *da, const u8 *sa, const u8 *bssid);
size_t hi5622_ba_build_delba(u8 *frame, size_t cap, u8 tid, u8 initiator,
			     u16 reason, const u8 *da, const u8 *sa,
			     const u8 *bssid);
size_t hi5622_ba_build_relay_addba_rsp(u8 *payload, size_t cap, u16 uid,
				       const struct hi5622_ba_params *p);
size_t hi5622_ba_build_relay_delba(u8 *payload, size_t cap, u16 uid, u8 tid,
				   u8 initiator);
size_t hi5622_ba_build_ampdu_start(u8 *payload, size_t cap, u8 tid, u16 seq,
				   u16 uid, u8 flag);
size_t hi5622_ba_build_ampdu_end(u8 *payload, size_t cap, u8 tid, u16 uid);
size_t hi5622_ba_build_tx_notify(u8 *payload, size_t cap, u16 uid, u8 tid,
				 u8 value);

void hi5622_ba_wrap_h2d(u8 *msg, size_t payload_len, u8 id, u8 chip,
			u8 dev1, u8 dev2);

/* ---- parsers ----------------------------------------------------------- */

int hi5622_ba_parse_action(const u8 *frame, size_t len,
			   struct hi5622_ba_action *out);

/* ---- high-level state machine ------------------------------------------ */

int hi5622_ba_tx_start(struct hi5622_ba_core *core,
		       const struct hi5622_ba_ops *ops,
		       const u8 *mac, const struct hi5622_ba_params *p);

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

#endif /* HI5622_BA_H */
