/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_sta_sme.h - portable kernel-SME logic for the STA (client) connect
 * flow of the Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-sta-mode, shared task task-155,
 * 2026-11-25).  Pure C99: compiles unchanged in the 6.18 kernel module and
 * host-side (tools/sta_mode_selftest.c).  No vendor source text, object code,
 * firmware byte or calibration value is reproduced.
 *
 * Evidence markers: [V] re-derived from the pinned local vendor module
 * (hi5622v100_wifi.ko 2.5.24; read-only local artifact, never committed),
 * [W] from the public Hi3881V100 GPL sibling source, [I] inference.
 *
 * MODEL - why this file exists
 * ----------------------------
 * The luofu vendor driver is a split-MAC host SME, not a full-MAC device
 * SME: hmac_handle_asoc_rsp_sta (wifi.ko 2.5.24 .text+0x99040) is part of the
 * STA FSM dispatch table {scan_rsp, join_rsp, auth_rsp, asoc_rsp} at
 * .rodata+0x8074 and is reached from the management-RX path
 * (hmac_rx_process_mgmt_event = D2H chip group 1 id 13) [V].  It parses the
 * received 802.11 association response, syncs the association state to the
 * device (H2D group 2 id 3, 12-byte HCC header + 28-byte payload copied from
 * its FSM record, .text+0x99090/0x99134) and reports the result to cfg80211
 * through the WAL asoc-completion path [V].  The sibling [W] hmac_sme_sta.c
 * follows the same order: join -> auth request -> auth response -> assoc
 * request -> assoc response -> connect result.
 *
 * Therefore the COMPLETION of a client (STA) association on our driver is
 * NOT a data-ring event and NOT a firmware-allocated completion record: it
 * is the received association response frame arriving on the CRX management
 * event path (our D2H g1 id13 decoder), processed by the driver-side SME.
 * The vendor .connect first posts the 108-byte WAL cfg-event 0x123 join body
 * (hi5622_sta_frames.c) to configure the device STA context; the host SME
 * then transmits auth/assoc requests on the corrected device-facing mgmt-TX
 * carrier (raw HCC group 0 id 6 control message with the frame inline,
 * sibling-gpl task-151) and consumes the responses here.
 *
 * The state machine is deliberately small and fail-closed: every transition
 * is checked by this portable layer, the kernel glue only posts frames when
 * the module parameters request it, and no key material ever passes through
 * these builders (the WPA2/WPA3 keys travel in the join body and the 4-way
 * EAPOL exchange, which rides the inherited data path).
 */
#ifndef HI5622_STA_SME_H
#define HI5622_STA_SME_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
#endif
#endif

/* ===================================================================
 * 1. 802.11 management frame constants (IEEE 802.11-2020)
 *
 * Frame control byte 0: protocol version 0 (bits 0-1), type management 0
 * (bits 2-3), subtype in bits 4-7.  All frames here use the three-address
 * management header (24 bytes): FC, duration, addr1(DA), addr2(SA),
 * addr3(BSSID), sequence control.  Little-endian on the wire.
 * =================================================================== */
#define HISM_MGMT_HDR_LEN	24u
#define HISM_FC0_AUTH		0xb0u	/* type 0, subtype 0x0b (auth)     */
#define HISM_FC0_ASSOC_REQ	0x00u	/* type 0, subtype 0x00 (assoc req)*/
#define HISM_FC0_ASSOC_RESP	0x10u	/* type 0, subtype 0x01 (assoc rsp)*/
#define HISM_SUBTYPE_ASSOC_REQ	0x00u
#define HISM_SUBTYPE_ASSOC_RESP	0x01u
#define HISM_SUBTYPE_AUTH	0x0bu

/* Authentication body: algorithm (0 = open system), sequence (1), status. */
#define HISM_AUTH_ALGO_OPEN	0u
#define HISM_AUTH_SEQ_OPEN	1u
#define HISM_AUTH_BODY_LEN	6u
#define HISM_AUTH_FRAME_LEN	(HISM_MGMT_HDR_LEN + HISM_AUTH_BODY_LEN)

/* Association request body: capability info + listen interval + IEs. */
#define HISM_ASSOC_FIXED_LEN	4u
#define HISM_ASSOC_HDR_IE_OFF	(HISM_MGMT_HDR_LEN + HISM_ASSOC_FIXED_LEN)
#define HISM_CAP_ESS		0x0001u
#define HISM_CAP_PRIVACY	0x0010u
#define HISM_CAP_SHORT_PREAMBLE	0x0020u
#define HISM_LISTEN_INTERVAL	10u

/* Association response body: capability + status + AID, then IEs. */
#define HISM_ASSOC_RESP_MIN_LEN	30u
#define HISM_ASSOC_RESP_IE_OFF	30u
#define HISM_AUTH_RESP_MIN_LEN	30u
#define HISM_STATUS_SUCCESS	0u

/* Element IDs and the fixed basic-rate set this driver advertises.
 * Supported rates IE: 8 bytes 1..18 Mbit/s; extended supported rates IE:
 * the 24/36/48/54 Mbit/s remainder (802.11-2020 9.4.2.3 / table 9-43). */
#define HISM_EID_SSID		0u
#define HISM_EID_SUPPORTED_RATES 1u
#define HISM_EID_EXT_SUPPORTED_RATES 50u
#define HISM_RATES_BASIC_LEN	8u
#define HISM_RATES_EXT_LEN	4u
static const u8 hism_rates_basic[HISM_RATES_BASIC_LEN] = {
	0x82u, 0x84u, 0x8bu, 0x96u, 0x0cu, 0x12u, 0x18u, 0x24u
};
static const u8 hism_rates_ext[HISM_RATES_EXT_LEN] = {
	0x30u, 0x48u, 0x60u, 0x6cu
};

/* Bounds.  32 = SSID max, the IE blob is bounded so the whole assoc frame
 * always fits HISM_MAX_FRAME (and the vendor host frame bound of 588 B). */
#define HISM_MAX_SSID		32u
#define HISM_MAX_IE		512u
#define HISM_MAX_FRAME		(HISM_ASSOC_HDR_IE_OFF + 2u + HISM_MAX_SSID + \
				 2u + HISM_RATES_BASIC_LEN + 2u + HISM_RATES_EXT_LEN)

/* ===================================================================
 * 2. SME state machine (cfg80211 kernel-SME connect)
 *
 *   OFF -> JOIN_POSTED (WAL 0x123 join accepted by the post gate)
 *       -> AUTH_SENT   (auth request posted)
 *       -> AUTH_DONE   (auth response status 0; assoc request next)
 *       -> ASSOC_SENT  (assoc request posted)
 *       -> CONNECTED   (assoc response status 0; cfg80211_connect_done(0))
 *   any pending state + AP status != 0 / timeout -> FAILED (connect_done)
 *
 * Every transition is validated here; an out-of-order response is refused
 * (-EPROTO) and never completes the connect.
 * =================================================================== */
#define HISM_OFF		0u
#define HISM_JOIN_POSTED	1u
#define HISM_AUTH_SENT		2u
#define HISM_AUTH_DONE		3u
#define HISM_ASSOC_SENT		4u
#define HISM_CONNECTED		5u
#define HISM_FAILED		6u
#define HISM_STATE_MAX		HISM_FAILED

const char *hism_state_name(u32 state);

/* 1 = the state is a pending connect (in flight, completion still owed). */
int hism_state_pending(u32 state);

/* Apply the auth response.  *state must be HISM_AUTH_SENT.
 * status 0 -> HISM_AUTH_DONE (caller posts the assoc request and moves to
 * HISM_ASSOC_SENT); status != 0 -> HISM_FAILED.  Returns 0 on a valid
 * transition, -EPROTO on an out-of-order response. */
int hism_auth_rsp_apply(u32 *state, u16 status);

/* Apply the assoc response.  *state must be HISM_ASSOC_SENT.
 * status 0 -> HISM_CONNECTED, status != 0 -> HISM_FAILED. */
int hism_assoc_rsp_apply(u32 *state, u16 status);

/* cfg80211 timeout_reason for a timeout in this state (enum
 * nl80211_timeout_reason: 0 unspecified, 2 AUTH, 3 ASSOC). */
#define HISM_TIMEOUT_UNSPECIFIED	0u
#define HISM_TIMEOUT_AUTH		2u
#define HISM_TIMEOUT_ASSOC		3u
u16 hism_timeout_reason(u32 state);

/* ===================================================================
 * 3. Frame builders (host-tested byte layouts)
 * =================================================================== */
struct hism_auth_req {
	const u8 *own;		/* addr2 = our STA MAC (6 bytes)          */
	const u8 *bssid;	/* addr1/addr3 = target AP BSSID (6 bytes)*/
	u16 seq;		/* sequence-control value (4-bit seq)     */
};

struct hism_assoc_req {
	const u8 *own;
	const u8 *bssid;
	const u8 *ssid;		/* 1..32 bytes                            */
	u8 ssid_len;
	int privacy;		/* 1 = set the Privacy capability bit      */
	u16 seq;
};

/* 0 = built; -EINVAL/-ENOSPC otherwise.  out_len is set on success. */
int hism_build_auth_req(u8 *frame, size_t cap, size_t *out_len,
			const struct hism_auth_req *p);
/* assoc_ie/assoc_ie_len (optional) receive the IE blob offset/length inside
 * the built frame; that blob is the association-request IE list. */
int hism_build_assoc_req(u8 *frame, size_t cap, size_t *out_len,
			 const u8 **assoc_ie, size_t *assoc_ie_len,
			 const struct hism_assoc_req *p);

/* Address sanity used by the glue before building. */
int hism_mac_ok(const u8 *mac);

/* ===================================================================
 * 4. Response decoders (reject partial/other-subtype frames)
 * =================================================================== */
struct hism_resp {
	u16 status;		/* 802.11 status code (0 = success)        */
	u16 aid;		/* assoc response AID (0 for auth)        */
	const u8 *ies;		/* assoc response IEs (NULL for auth)     */
	size_t ies_len;
};

/* 0 = decoded; -ENODATA = not our subtype; -EPROTO = malformed. */
int hism_decode_auth_resp(const u8 *frame, size_t len, struct hism_resp *out);
int hism_decode_assoc_resp(const u8 *frame, size_t len, struct hism_resp *out);

/* 1 = this response is addressed to us (addr1 == own, addr3 == bssid). */
int hism_resp_for_us(const u8 *frame, size_t len, const u8 *own,
		     const u8 *bssid);

/* Bounded copy of a response IE blob; returns 0 or -E2BIG. */
int hism_ie_copy(u8 *dst, size_t cap, const u8 *ies, size_t ies_len,
		 size_t *out_len);

/* ===================================================================
 * 5. Association-state sync (H2D) - decoded but NOT posted by this lane
 *
 * hmac_handle_asoc_rsp_sta posts the association state to the device:
 * H2D HCC group 2, id 3, 12-byte header + 28-byte payload copied from its
 * FSM record (= the same 28-byte assoc-completion record shape decoded by
 * the wifi-sta-join lane) [V .text+0x99090 hcc_msg_alloc(28),
 * .text+0x99134 memcpy 28 B to payload+12].  The firmware receive-table
 * entry for group 2 id 3 is [U] on our image and hi5622_frame_post() would
 * refuse it, so the kernel glue keeps this builder host-tested only until a
 * ladder rung proves the firmware accepts it.
 * =================================================================== */
#define HISM_SYNC_GROUP		2u
#define HISM_SYNC_ID		3u
#define HISM_HCC_HDR_LEN	12u
#define HISM_HCC_TAG		0x5a5au
#define HISM_SYNC_PAYLOAD_LEN	28u
#define HISM_SYNC_FRAME_LEN	(HISM_HCC_HDR_LEN + HISM_SYNC_PAYLOAD_LEN)
#define HISM_SYNC_OFF_STATUS	0x02u
#define HISM_SYNC_OFF_BSSID	0x04u
#define HISM_SYNC_OFF_REQ_IE_LEN 0x0cu
#define HISM_SYNC_OFF_RESP_IE_LEN 0x10u
#define HISM_SYNC_OFF_REQ_IE_VA	0x14u
#define HISM_SYNC_OFF_RESP_IE_VA 0x18u
struct hism_sync {
	u16 status;
	const u8 *bssid;
	u32 req_ie_len;
	u32 resp_ie_len;
	u32 req_ie_va;		/* device-visible host-DRAM address        */
	u32 resp_ie_va;
	u8 dst;			/* hdr[1] high nibble (band/core)         */
	u8 vap_id;		/* hdr[3]                                 */
	u8 hdr2;		/* hdr[2]                                 */
};
int hism_build_assoc_sync(u8 *frame, size_t cap, size_t *out_len,
			  const struct hism_sync *p);

#endif /* HI5622_STA_SME_H */
