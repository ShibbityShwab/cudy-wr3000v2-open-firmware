/* SPDX-License-Identifier: GPL-2.0 */
/*
 * h2d_frames.h - exact H2D frame catalogue, builders and validators for the
 * Hi5622V100 clean-room PCIe/ETE transport (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code written by the wifi-h2d-frames lane (task-93,
 * 2026-11-24) from the project's committed decoded contract.  No vendor
 * source, object code, firmware byte, calibration value or secret is
 * reproduced; every constant below is a derived value with an evidence marker
 * in REPORT.md.
 *
 * The file is pure C with no libc dependency beyond <string.h>: it compiles
 * unchanged as a userspace unit (the selftest) and as part of a kernel module
 * (define __KERNEL__; the generated data tables are in h2d_catalogue.inc).
 *
 * Layer model (REPORT.md sec 2):
 *
 *   HCC frame     12-byte header + payload (this file, layer B) - the body
 *   SR carrier    8-byte node {devva, (len<<16)|0x6D2B} + packed producer
 *   mailbox       out[0] pending bitmap + out[2] doorbell (layer A), id 3
 *                 announces an SR commit, id 5 is the firmware BUG() trap.
 *
 * Contract anchors (all committed under opensource/docs/incoming/):
 *   wifi-ete-frames   header layout + tag/length, D2H ready frames, carrier
 *   wifi-h2d-catalog  firmware H2D receive tables (group 0/2), payload sizes,
 *                     mailbox ids, handler payload-access probe risk
 *   wifi-host-replica vendor senders, pcie_msg_send order, id sets
 *   wifi-hcc-re       SR node, packed index, ETE channel registers
 *   wifi-hcc-service  working bench post (SR0 prod=1 cns=1), CECE policy
 *   phase37/sr-message-catalog.md   vendor H2D builders (48 explicit senders)
 *   phase38/announce-body-bytes.md  byte-exact first post-ready g0 id1 frame
 */
#ifndef H2D_FRAMES_H
#define H2D_FRAMES_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Shared portable-type guard with hi5622_frames_api.h (wifi-scan-ap lane) so
 * a host test may include both headers under -pedantic. */
#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int16_t  s16;
typedef int32_t  s32;
#endif
#endif

/* ===================================================================
 * 1. Contract constants
 * =================================================================== */

/* HCC header (wifi-ete-frames sec 1.3, phase37 THE-PROTOCOL-MAP sec 2.2) */
#define H2D_HDR_LEN		12u
#define H2D_TAG			0x5a5au	/* transport tag at +0x0A [V] */
#define H2D_TAG_OFF		0x0au
#define H2D_LEN_OFF		0x04u	/* u16 total = payload + 12 [V] */
#define H2D_ID_OFF		0x06u	/* u16 message id [V] */
#define H2D_GROUP_OFF		0x00u
#define H2D_BID_OFF		0x01u	/* low nibble alloc=1, high nibble dst [V] */
#define H2D_FA_OFF		0x02u
#define H2D_FB_OFF		0x03u
#define H2D_FC_OFF		0x08u
#define H2D_DST_OFF		0x09u	/* = byte[1] >> 4 (hcc_msg_tx) [V] */
#define H2D_ALLOC_NIBBLE	0x01u	/* hcc_msg_alloc sets it [V] */
#define H2D_MAX_DST		0x0fu
#define H2D_MAX_PAYLOAD		0x674u	/* hcc_msg_alloc movw bound [V] */
#define H2D_MAX_TOTAL		(H2D_HDR_LEN + H2D_MAX_PAYLOAD)
#define H2D_MAX_TOTAL_CLASS	0x7ffu	/* header class hdr[5]>>1&7 <= 3 [V] */

/* HCC groups that have a firmware H2D receive table [V] */
#define H2D_GROUP_CHIP		0u
#define H2D_GROUP_CORE		2u
/* Group 4 exists only in the device->host direction (ready pair) [V] */
#define H2D_GROUP_D2H_PLAT	4u

/* WAL group-2 cfg-event family (wifi-vap-start lane, task-117) [V].
 * The vendor host posts VAP create/start/AP-config through
 * wal_send_cfg_event(): the HCC header id is 0 and the operation is the WAL
 * command u16 inside the payload.  The firmware's static group-2 receive
 * table has no id-0 slot (wifi-h2d-catalog anomaly), which is why the
 * generic handler-slot check refuses a shape the vendor host itself emits;
 * h2d_check_wal_cfg() validates the family structurally instead.  Envelope
 * (HCC payload = frame+12): +0x00 u32 response cookie; +0x04 u32 WAL
 * cfg-event id = 1; +0x08 u16 pad; +0x0A u16 data_len = 4 + body_len;
 * +0x0C u32 seq; +0x10 u16 WAL command; +0x12 u16 body_len; +0x14 body.
 * Wire total = 32 + body_len. */
#define H2D_WAL_ID		0u	/* HCC header id of a WAL cfg event [V] */
#define H2D_WAL_CFG_ID		1u	/* u32 at payload+4 [V] */
#define H2D_WAL_HDR_LEN		20u	/* cookie+wal hdr+cmd/body_len [V] */
#define H2D_WAL_CMD_VAP_ADD	0x0064u	/* body 24 [V] */
#define H2D_WAL_CMD_VAP_START	0x0065u	/* body 12 [V] */
#define H2D_WAL_CMD_KICK_STA	0x00d0u	/* body 16 [V] */
#define H2D_WAL_CMD_AP_START	0x0127u	/* body 156 (body still [U]) [V] */

/* Mailbox layer (wifi-hcc-re sec 3, host-replica sec 4) */
#define H2D_CA_MBOX_PENDING	0x40039010u	/* out[0]: H2D pending bitmap [V] */
#define H2D_CA_MBOX_DOORBELL	0x400392d4u	/* out[2]: host ORs bit0 [V] */
#define H2D_CA_MBOX_NEVER_WRITE	0x400392f0u	/* out[5]: NEVER host-write [V] */
#define H2D_CA_D2H_ACK		0x40101438u	/* out[3] [V] */
#define H2D_CA_D2H_REARM	0x40101414u	/* out[4] [V] */
#define H2D_MBOX_MAX_ID		10u		/* dispatcher iterates 0..10 [V] */
#define H2D_MBOX_SEND_MAX_ID	9u		/* pcie_msg_send rejects >9 [V] */
#define H2D_MBOX_ID_PM_SLEEP	1u		/* wlan_pm_sleep_work sender [V] */
#define H2D_MBOX_ID_ETE_ANNOUNCE 3u		/* every SR commit announces id 3 [V] */
#define H2D_MBOX_ID_BUGON	5u		/* firmware BUG() trap [V] */
#define H2D_MBOX_BIT(id)	(1u << (id))

/* SR node + packed producer index (wifi-hcc-re sec 3.3/4, ete-frames 1.1) */
#define H2D_NODE_SIZE		8u
#define H2D_NODE_FLAGS		0x6d2bu	/* node word1 low bits [V] */
#define H2D_NODE_LEN_SHIFT	16u
#define H2D_RING_DEPTH		32u	/* measured vendor depth [V] */
#define H2D_RING_MAX_DEPTH	1024u	/* packed index low field 10 bits */
#define H2D_IDX_LOW_MASK	0x3ffu
#define H2D_IDX_PHASE		0x400u	/* phase bit toggles on wrap [V] */

/* ETE channel offsets (BAR0-relative register block) [V] */
#define H2D_SR0_OFF		0x400u
#define H2D_SR1_OFF		0x450u
#define H2D_SR2_OFF		0x4a0u
#define H2D_ETE_CH_ENABLE	0x00u	/* firmware-owned; host must not write [V] */
#define H2D_ETE_CH_CTRL		0x08u
#define H2D_ETE_CH_A_BASE	0x10u	/* host ring node-array device VA [V] */
#define H2D_ETE_CH_A_DEPTH	0x14u
#define H2D_ETE_CH_A_PRD	0x18u	/* host producer [V] */
#define H2D_ETE_CH_A_CNS	0x1cu
#define H2D_ETE_SEND_SR_PACKED	0xc18u	/* CA 0x4003AC18+4i device-authored [V] */

/* Device IO CA -> host BAR0 (project-wide convention) [V] */
#define H2D_BAR0_OF_CA(ca)	(0x3b8000u + ((ca) - 0x40000000u))

/* D2H ready frames (wifi-ete-frames sec 2, d2h-direct bench ledger) */
#define H2D_READY_MAX_PAYLOAD	0x63u	/* host_ready_msg_process cap [V] */

/* ===================================================================
 * 2. Errors, risk classes, message descriptors
 * =================================================================== */

enum h2d_err {
	H2D_OK = 0,
	H2D_EARG,	/* malformed argument / field value */
	H2D_ESHORT,	/* frame shorter than the 12-byte header */
	H2D_ELEN,	/* declared total length inconsistent / zero */
	H2D_ETAG,	/* transport tag != 0x5A5A */
	H2D_EDST,	/* hdr[9] != hdr[1] >> 4 */
	H2D_EALLOC,	/* alloc nibble != 1 on an H2D frame */
	H2D_EGROUP,	/* group has no H2D receive table (or invalid nibble) */
	H2D_EID,	/* unknown / unregistered message id */
	H2D_EBOUNDS,	/* payload > 0x674, dst > 0xf, ... */
	H2D_EBUFFER,	/* caller buffer too small */
	H2D_ENOHANDLER,	/* firmware receive slot exists but handler is NULL */
	H2D_EWALCMD,	/* WAL cfg-event family: unknown command or bad envelope */
	H2D_EPOLICY,	/* lane safety policy refused the frame */
	H2D_ESTATE,	/* mailbox/ring state precondition not met (no retry) */
	H2D_EWIRE,	/* SR node word1 malformed */
};

/* Probe-safety class of the firmware handler (h2d-catalog App B reading). */
enum h2d_risk {
	H2D_RISK_STUB = 0,	/* payload-free stub handler (manual entry) */
	H2D_RISK_NO_ACCESS,	/* no early payload access found */
	H2D_RISK_BYTE_READ,	/* fixed byte/halfword reads only */
	H2D_RISK_PTR_DEREF,	/* word reads that may be dereferenced */
	H2D_RISK_NO_HANDLER,	/* NULL slot or no slot in the H2D tables */
	H2D_RISK_UNKNOWN,
};

#define H2D_SND_HAS_HANDLER	0x01u	/* firmware has a non-NULL handler */
#define H2D_SND_NO_HANDLER	0x02u	/* vendor sender with no firmware handler */
#define H2D_SND_BUILDER_EXACT	0x04u	/* an exact builder exists in this file */

/* One vendor H2D sender (generated from the committed vendor catalogue). */
struct h2d_vendor_sender {
	const char	*name;
	u8		group;
	u8		id;
	s16		payload_len;	/* -1 = dynamic */
	u8		risk;		/* enum h2d_risk */
	u8		flags;
};

/* One firmware H2D receive-table slot (generated; handler_ca == 0 = NULL). */
struct h2d_fw_handler {
	u8	group;
	u8	id;
	u32	handler_ca;
	u8	risk;
};

const struct h2d_vendor_sender *h2d_vendor_senders_get(size_t *count);
const struct h2d_fw_handler *h2d_fw_handlers_get(size_t *count);
const struct h2d_fw_handler *h2d_fw_handler_lookup(u8 group, u16 id);

/* ===================================================================
 * 3. The frame model
 * =================================================================== */

/* Per-frame policy assertions (set by the exact builders / importer). */
#define H2D_F_EXACT		0x01u	/* built by an exact vendor-shaped builder */
#define H2D_F_CAPTURED		0x02u	/* payload bytes are a captured frame */
#define H2D_F_ALLOW_ID5_HCC	0x04u	/* explicit HCC id-5 override (group 2) */

struct h2d_frame {
	u8	group;		/* hdr[0] & 0xf */
	u8	dst;		/* hdr[1] >> 4 and hdr[9] */
	u8	fa;		/* hdr[2] per-message field A */
	u8	fb;		/* hdr[3] per-message field B */
	u8	fc;		/* hdr[8] per-message field C */
	u16	id;		/* hdr[6..7] */
	u16	payload_len;
	u32	policy;		/* H2D_F_* */
	u8	payload[H2D_MAX_PAYLOAD];
};

/* 1 = the frame is the decoded WAL group-2 cfg-event family.  The firmware
 * WAL/core module consumes these frames; the static H2D handler table has no
 * id-0 slot, so callers that gate on h2d_fw_handler_lookup() must treat this
 * predicate as "handled by the WAL module".  It is true only for a frame that
 * h2d_check_wal_cfg() already accepted (exact envelope plus a command/body
 * size pair from the decoded allowlist) - unknown WAL commands are refused
 * earlier by h2d_frame_decode(). */
int h2d_frame_is_wal_cfg(const struct h2d_frame *f);

/* Validation levels; TRANSPORT is always applied by h2d_frame_decode(). */
#define H2D_VF_TRANSPORT	0x01u	/* header/len/tag/dst/alloc/LE structure */
#define H2D_VF_FIRMWARE		0x02u	/* + group tables, class field, id range */
#define H2D_VF_HANDLER		0x04u	/* + non-NULL handler in the fw tables */
#define H2D_VF_POLICY		0x08u	/* + lane safety policy (see REPORT sec 6) */
#define H2D_VF_D2H		0x10u	/* device->host validation (alloc may be 0) */
#define H2D_VF_ANYGROUP		0x20u	/* diagnostic: skip the group 0/2 rule */

const char *h2d_strerror(enum h2d_err err);
const char *h2d_risk_name(u8 risk);

/* Encode: header from the fields + tag/length; validates at policy level. */
int h2d_frame_encode(const struct h2d_frame *f, u8 *buf, size_t cap,
		     size_t *out_len, enum h2d_err *err);

/* Decode + validate.  Copies at most H2D_MAX_PAYLOAD payload bytes. */
int h2d_frame_decode(const u8 *buf, size_t len, struct h2d_frame *f,
		     u32 vflags, enum h2d_err *err);

/* Import a captured vendor *post buffer*: the declared total may be shorter
 * than the buffer (the vendor's first post is a 72-byte buffer declaring 48).
 * Validates the declared frame and marks it H2D_F_CAPTURED. */
int h2d_frame_import(const u8 *buf, size_t buf_len, struct h2d_frame *f,
		     enum h2d_err *err);

/* Export a frame, optionally padded to an exact post-buffer size (pad_byte is
 * the vendor's uninitialised-buffer tail, e.g. 0xff for the phase-38 capture). */
int h2d_frame_export(const struct h2d_frame *f, u8 *dst, size_t cap,
		     size_t pad_to, u8 pad_byte, size_t *out_len,
		     enum h2d_err *err);

/* ===================================================================
 * 4. Exact builders (structured input -> exact frame)
 * =================================================================== */

/* Group 0 id 0: the payload-free stub frame the firmware logs and drops.
 * Bytes: 00 01 00 00 0c 00 00 00 00 00 5a 5a (the only safe bare probe). */
int h2d_build_safe_probe(struct h2d_frame *f, u8 dst);

/* Group 0 ids 1/2: the shared vendor config-event shape (phase37 helper
 * 0x5714c; hdpp_config_send_event for id 1, hmac_config_alg_send_event for
 * id 2).  Payload = token[8] | u16 word8 | u16 len | data[len] | tail[4];
 * total = 0x1c + len.  NULL token/tail means zero.  EXACT builder. */
int h2d_build_g0_cfg_event(struct h2d_frame *f, u8 id, u8 dst, u8 fa, u8 fb,
			   u8 fc, const u8 token[8], u16 word8,
			   const u8 *data, u16 len, const u8 tail[4],
			   enum h2d_err *err);

/* Group 0 id 1: the vendor's first post-ready send, TXQ config sync.
 * The phase-38 live capture is token {00..00 01 00 00 00}, word8 = 0x00d8
 * (cmd), len = 0x14, data = {01 00 00 00, 0...}, tail 0xffffffff. */
int h2d_build_g0_id1_hdpp_txq_sync(struct h2d_frame *f, u8 dst, u8 fa, u8 fb,
				   const u8 token[8], u16 cmd,
				   const u8 *data, u16 len,
				   const u8 tail[4], enum h2d_err *err);

/* Group 0 id 0x1d: hmac_edca_opt_timeout_fn, zeroed 0x3c-byte payload
 * (the live steady-state frame "message A").  EXACT builder. */
int h2d_build_g0_id1d_edca_opt(struct h2d_frame *f, u8 dst, u8 fa, u8 fb,
			       u8 fc, enum h2d_err *err);

/* Group 0 id 9: hmac_del_user_notify_dmac, 12-byte payload:
 * u16 user id at +0; if user_id == 0x0fff, 4 address bytes at +2. EXACT. */
int h2d_build_g0_id09_del_user(struct h2d_frame *f, u8 dst, u16 user_id,
			       const u8 addr[4], enum h2d_err *err);

/* Generic, fully policy-checked builder (used by the CLI / tests). */
int h2d_build_raw(struct h2d_frame *f, u8 group, u16 id, u8 dst, u8 fa,
		  u8 fb, u8 fc, const u8 *payload, u16 payload_len,
		  u32 policy, enum h2d_err *err);

/* ===================================================================
 * 5. SR node / packed producer / post plan
 * =================================================================== */

/* Node word1 = (frame_total << 16) | 0x6D2B. */
int h2d_sr_node_encode(u8 node[H2D_NODE_SIZE], u32 devva, u32 frame_total,
		       enum h2d_err *err);
int h2d_sr_node_decode(const u8 node[H2D_NODE_SIZE], u32 *devva, u32 *len,
		       u32 *flags, enum h2d_err *err);

/* Packed producer arithmetic (pcie_ete_ring_ptr_plus semantics):
 * idx = (index_low10 | phase<<10); next = idx+1; when (next & 0x3ff) ==
 * depth the low field clears and the phase bit toggles. */
u32 h2d_idx_plus(u32 idx, u32 depth);
u32 h2d_idx_count(u32 from, u32 to, u32 depth);

struct h2d_sr_plan {
	u32	node_index;	/* producer & 0x3ff */
	u32	buffer_devva;	/* word0 / buffer device VA */
	u32	node_word0;
	u32	node_word1;
	u32	producer_before;
	u32	producer_after;
	u16	frame_total;
};

int h2d_sr_plan(const struct h2d_frame *f, u32 producer, u32 depth,
		u32 buffer_devva, struct h2d_sr_plan *p, enum h2d_err *err);

struct h2d_mailbox_plan {
	u16	id;
	u32	pending_ca;	/* H2D_CA_MBOX_PENDING */
	u32	pending_write;	/* BIT(id) */
	u32	doorbell_ca;	/* H2D_CA_MBOX_DOORBELL */
	u32	doorbell_write;	/* out[2] | 1 */
};

/* Mailbox announce plan.  Host posts are only ids 1 (PM sleep) and 3 (SR
 * announce); id 5 is the firmware BUG() trap; a non-zero pending word means the
 * device has not consumed the previous message (coalescing precondition). */
int h2d_mailbox_plan(u16 id, u32 out0_pending, u32 out2_doorbell,
		     struct h2d_mailbox_plan *p, enum h2d_err *err);

struct h2d_post_plan {
	struct h2d_sr_plan	sr;
	struct h2d_mailbox_plan	mb;
	u16			frame_total;
};

/* One-frame post plan: frame -> SR node + packed producer + mailbox id 3.
 * Refuses when the previous announce is still pending (one frame, one idea). */
int h2d_plan_post(const struct h2d_frame *f, u32 producer, u32 depth,
		  u32 buffer_devva, u32 out0_pending, u32 out2_doorbell,
		  struct h2d_post_plan *p, enum h2d_err *err);

/* ===================================================================
 * 6. Device->host ready-frame validator (never send these H2D)
 * =================================================================== */

struct h2d_ready {
	u8	group;
	u8	id;
	u8	core;
	u16	total;
	u16	payload_len;
	u32	payload_dword0;
	const u8 *payload;	/* points into the caller's buffer */
};

int h2d_ready_decode(const u8 *buf, size_t len, struct h2d_ready *r,
		     enum h2d_err *err);

#endif /* H2D_FRAMES_H */
