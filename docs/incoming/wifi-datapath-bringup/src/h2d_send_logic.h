/* SPDX-License-Identifier: GPL-2.0 */
/*
 * h2d_send_logic.h - portable (host + kernel) policy and emission logic for
 * the Hi5622V100 clean-room driver's first REAL post-ready H2D message path.
 *
 * ORIGINAL clean-room code written by the wifi-h2d-send lane (shared tasks
 * task-98/task-102, 2026-11-24) from the project's committed decoded contract
 * (see REPORT.md for the per-claim evidence).  No vendor source, object code,
 * firmware byte, calibration value or secret is reproduced here.
 *
 * Why a separate file: the frame choice, the byte-exact builders, the gate
 * ordering and the sysfs grammar are the parts that must be testable WITHOUT
 * a device and WITHOUT a kernel (the host selftest tools/h2d_logic_selftest.c
 * links this file + the h2d_frames library + the portable hcc_core).  The
 * kernel file h2d_send.c adds only the IO: SR node/producer writes, the core
 * SR commit (allowlisted mailbox announce) and the bounded out[0] poll.
 *
 * Frame policy (inherited from wifi-h2d-frames, enforced by h2d_frames.c):
 *   - only chip groups 0 and 2 exist H2D (group 4 is device->host only);
 *   - mailbox id 5 is the firmware BUG() trap, the core refuses it;
 *   - payload-carrying frames must come from an EXACT builder (never a
 *     synthetic payload: several firmware handlers dereference payload words);
 *   - node word1 = (frame_total << 16) | 0x6D2B, packed producer depth 32.
 *
 * The only messages this driver will synthesize are the six EXACT ones:
 *   stub        group 0 id 0   payload-free liveness stub (safe bare probe)
 *   txq_sync    group 0 id 1   the vendor's first post-ready message, byte-exact
 *   txq_sync72  group 0 id 1   the same 48-byte frame, but the SR node declares
 *                              the vendor's captured 72-byte live buffer
 *                              (48 frame bytes + 24 x 0xff inherited pad)
 *   ctx_create  group 0 id 1   the ordered PRECONDITION of txq_sync: the
 *                              hdpp config event with u16 key 0x0064 that makes
 *                              the firmware create the per-module context
 *                              (ctx+0x190) the key-0xd8 TXQ worker dereferences
 *   edca        group 0 id 0x1d hmac_edca_opt_timeout_fn, zeroed 0x3c payload
 *   del_user    group 0 id 9   hmac_del_user_notify_dmac, 12-byte payload
	*   cfg_cal     group 0 id 2   calibration config event (key 0x0101,
	*                              270-byte alg_cfg blob; cfg id 3502..3507)
 */
#ifndef H2D_SEND_LOGIC_H
#define H2D_SEND_LOGIC_H

#include "hcc_core.h"
#include "h2d_frames.h"

/* The only frames this driver will synthesize. */
enum h2d_msg {
	H2D_MSG_STUB = 0,	/* g0 id0, payload-free */
	H2D_MSG_TXQ_SYNC,	/* g0 id1, vendor first post-ready, byte-exact */
	H2D_MSG_TXQ_SYNC72,	/* g0 id1, same frame, 72-byte live node buffer */
	H2D_MSG_TXQ_ADDR_SYNC,	/* g0 id1 key 0x0320, 4-byte TXQ-table devVA */
	H2D_MSG_EDCA_OPT,	/* g0 id0x1d, exact 0x3c-byte zero payload */
	H2D_MSG_DEL_USER,	/* g0 id9, exact 12-byte payload */
	H2D_MSG_CTX_CREATE,	/* g0 id1 key0x64, data[0]=type 3 (the default) */
	H2D_MSG_CTX_CREATE_T1,	/* same frame, data[0]=type 1 (defaults+2nd alloc) */
	H2D_MSG_CTX_CREATE_T2,	/* same frame, data[0]=type 2 (defaults) */
	H2D_MSG_CFG_CAL,	/* g0 id2 key 0x0101, 270-byte calibration blob */
	H2D_MSG_COUNT,
};

/* The vendor's byte-exact first post-ready frame: declared total 0x30.
 * The live vendor SR node declared a 72-byte buffer (48-byte frame + 24
 * inherited 0xff bytes), so H2D_MSG_TXQ_SYNC72 exposes that node length as a
 * separate, one-variable experiment while the header total stays 0x30. */
#define H2D_TXQ_TOTAL		0x30u
#define H2D_TXQ_NODE_LEN	72u
#define H2D_TXQ_PAD_BYTE	0xffu

/* The vendor's TXQ-table address sync (hmac_txq_table_addr_sync ->
 * hdpp_config_send_event(cmd=0x320, len=4, data=&devva)): group 0 id 1,
 * header field B = 0, token zero, 4-byte LE device VA of the host-DRAM
 * 0x2524-byte TXQ table, zero tail; declared total 0x20, node length = total.
 * Firmware worker CA 0x81445 stores the dword into firmware RAM at
 * (band-array base) + 0x252C*band + 0x2524 [V: FIRMWARE.bin disassembly]. */
#define H2D_TXQ_ADDR_TOTAL	0x20u
#define H2D_TXQ_ADDR_KEY	0x0320u

/* The ordered precondition of txq_sync: the group-0 / id-1 config event with
 * u16 key = 0x0064 at payload+8 (hdpp_config_send_event shape token[8] | key |
 * len | data | pad).  Declared total 0x30, len 0x14, data type 3 / vap 4 /
 * user 4, data[4] == hdr[3] == 4 (the created module is the TXQ context).
 * SYNTHESIZED from the firmware parser, not a vendor capture - see
 * ../wifi-ctx-create/REPORT.md.  Node length = declared total (48 B). */
#define H2D_CTX_TOTAL		0x30u
#define H2D_CTX_KEY		0x0064u
#define H2D_CTX_LEN		0x14u
#define H2D_CTX_TYPE		3u
#define H2D_CTX_VAP		4u
#define H2D_CTX_USER		4u
#define H2D_CTX_MAX_MODULE	0x12u	/* module table index bound */

/* The six vendor calibration alg set_* config events (wifi-proto-seq lane,
 * task-103): HCC group 0 / id 2, config key 0x0101, alg_cfg blob of a fixed
 * 270 bytes; frame total 298 B; node length = declared total.
 *   blob[0..1] u16 cfg id, [2] u8 type (0=SET), [3] u8 pkt_type,
 *   [4..7] u32 dev_ret (0), [8] u8 need_w4 = 1, [9] u8 resv,
 *   [10..] value area (zero-valued structural probe; command-specific size).
 * cfg ids: 3502 2g power, 3503 2g low power, 3504 5g power, 3505 5g low
 * power, 3506 xo ppm cali, 3507 rssi.  Selector values 1..6 map to that
 * vendor order; 3502..3507 are accepted literally. */
#define H2D_CAL_TOTAL		298u
#define H2D_CAL_KEY		0x0101u
#define H2D_CAL_BLOB_LEN	270u
#define H2D_CAL_VALUE_OFF	10u
#define H2D_CAL_NEED_W4_OFF	8u	/* blob offset of the need_w4 byte */
#define H2D_CAL_NEED_W4		1u
#define H2D_CAL_CFG_FIRST	3502u
#define H2D_CAL_CFG_LAST	3507u
#define H2D_CAL_CFG_PROBE	3507u	/* set_rssi_param: first C1 bench fire */
#define H2D_CAL_CFG_COUNT	6u
#define H2D_CAL_FB_DEFAULT	0x04u	/* the module the context-create made */
#define H2D_CAL_FB_DEFAULT_UNSET 0xffu	/* selector sentinel: use the default */

/* Gate refusal reasons; H2D_GATE_OK == 0 is the only allowed value. */
enum h2d_gate {
	H2D_GATE_OK = 0,
	H2D_GATE_BAD_MSG,	/* unknown message selector */
	H2D_GATE_NOT_ARMED,	/* hcc_arm has not programmed the SR rings */
	H2D_GATE_NOT_RELEASED,	/* the 0x5A5A release has not happened */
	H2D_GATE_NOT_LATCHED,	/* latched golden boot gate not satisfied */
	H2D_GATE_PHASE,		/* core phase is not READY_TXQ_SYNC/ACTIVE */
	H2D_GATE_TXQ_PHASE,	/* TXQ sync is only legal in READY_TXQ_SYNC */
	H2D_GATE_LANE_CLOSED,	/* core closed the H2D lane after a timeout */
	H2D_GATE_LIVENESS_USED,	/* the liveness stub already used the idea */
	H2D_GATE_BUDGET,	/* per-load frame budget exhausted */
	H2D_GATE_INFLIGHT,	/* core coalescing state still awaiting clear */
	H2D_GATE_OUT0_PENDING,	/* live out[0] != 0: previous word unconsumed */
	H2D_GATE_DUP_TXQ,	/* TXQ sync already committed this load */
	H2D_GATE_DUP_CTX,	/* ctx_create already committed this load */
	H2D_GATE_CTX_PHASE,	/* ctx_create is legal only in READY_TXQ_SYNC */
	H2D_GATE_NEED_CTX,	/* txq_addr_sync requires this load's ctx_create */
	H2D_GATE_DUP_TXQ_ADDR,	/* TXQ-table address sync already sent this load */
	H2D_GATE_FRAME_INVALID,	/* the frame builder/validator refused */
};

/* Live driver-state snapshot the gate needs (filled by h2d_send.c or a test
 * model; no IO is performed by the gate itself). */
struct h2d_live {
	bool		armed;
	bool		released;
	bool		boot_latched;
	bool		lane_closed;
	bool		txq_submitted;	/* a REAL txq_sync/txq_sync72 frame was sent */
	bool		txq_addr_sent;	/* the key-0x320 TXQ-table address sync was sent */
	bool		ctx_created;	/* a ctx_create frame was sent this load */
	enum hcc_phase	phase;
	hcc_u32		frames_sent;	/* real H2D frames posted this load */
	hcc_u32		budget;		/* per-load real-frame budget */
	hcc_u32		liveness_sent;	/* hcc_sr_announce frames this load */
	hcc_u32		mbox_inflight;	/* core coalescing state */
	hcc_u32		out0;		/* live out[0] pending word */
};

/* Parsed strict h2d_send request (never carries a synthetic payload). */
struct h2d_req {
	enum h2d_msg	msg;
	u8		dst;
	bool		has_user;
	u16		user_id;	/* H2D_MSG_DEL_USER */
	bool		has_addr;
	u8		addr[4];	/* H2D_MSG_DEL_USER, user_id 0x0fff */
	bool	has_cfg;
	u16	cfg_id;	/* H2D_MSG_CFG_CAL: 3502..3507 */
	bool	has_fb;
	u8	fb;	/* H2D_MSG_CFG_CAL: header field B (module) */
};

/* Gate evaluation, in the documented order (see REPORT.md sec 3). */
enum h2d_gate h2d_gate_check(const struct h2d_live *live, enum h2d_msg msg);

/* Names (sysfs/dmesg/selftest). */
const char *h2d_gate_name(enum h2d_gate gate);
const char *h2d_msg_name(enum h2d_msg msg);
int h2d_msg_name_parse(const char *name, enum h2d_msg *msg);

/* True for every context-create variant (they share the once-per-load rule). */
bool h2d_msg_is_ctx(enum h2d_msg msg);

/* Post-buffer length for one emission.  Default rule: node_len == frame_total
 * (the declared header total); only H2D_MSG_TXQ_SYNC72 overrides it to the
 * vendor's captured 72-byte live buffer (requires frame_total == H2D_TXQ_TOTAL).
 * pad_byte is the value of the bytes between frame_total and node_len.
 * Returns 0, -EINVAL for an unknown message or an inconsistent frame_total. */
int h2d_msg_emit_plan(enum h2d_msg msg, u32 frame_total, u32 *node_len,
		      u8 *pad_byte);

/* Build + validate + wire-encode one exact message.
 * Returns 0 and the validated frame (struct h2d_frame) plus its 12-byte
 * header + payload wire image in wire[0..*total) on success, -1 otherwise
 * with *err set to the h2d_frames refusal code. */
int h2d_msg_build(enum h2d_msg msg, u8 dst, u16 user_id, const u8 *addr,
		  struct h2d_frame *f, u8 *wire, u32 cap, u32 *total,
		  enum h2d_err *err);

/* The context-create config event, parameterised for tests: id 1 group 0,
 * key 0x0064, len 0x14, fb == user == the module index that gets created.
 * ctx_type must be 1..3 (only those allocate ctx+0x190); vap/user <= 0x12.
 * The production path only uses the defaults (type 3 / vap 4 / user 4). */
int h2d_build_ctx_create(struct h2d_frame *f, u8 dst, u8 ctx_type, u8 vap,
			 u8 user, enum h2d_err *err);

/* The TXQ-table address sync (data-plane bring-up): group 0 / id 1,
 * key 0x0320, len 4, data = u32 LE device VA of the 0x2524-byte host-DRAM
 * TXQ table.  fb is header field B (0 = the vendor's own header byte for this
 * message).  EXACT builder derived from hmac_txq_table_addr_sync (wifi.ko
 * 2.4.15 0x1af00 / 2.5.24) and the firmware worker it reaches. */
int h2d_build_txq_addr_sync(struct h2d_frame *f, u8 dst, u8 fb,
			    u32 txq_devva, enum h2d_err *err);

/* The calibration config event (wifi-proto-seq lane, task-103): group 0 /
 * id 2, key 0x0101, fixed 270-byte alg_cfg blob with cfg_type SET, dev_ret 0,
 * need_w4 = 1 and a zero value area (the structural probe).  cfg_id must be
 * one of 3502..3507; fb is the header field B (module/vap selector, 0..0x12;
 * 4 = the module the type-1 context-create created).  EXACT builder. */
int h2d_build_cfg_cal(struct h2d_frame *f, u8 dst, u16 cfg_id, u8 fb,
		  enum h2d_err *err);

/* Same, with an explicit value area (len <= 260; the blob is zero-padded).
 * value may be NULL for the structural probe. */
int h2d_build_cfg_cal_raw(struct h2d_frame *f, u8 dst, u16 cfg_id, u8 fb,
		  const u8 *value, u16 value_len,
		  enum h2d_err *err);

/* Extended build: cfg_id == 0 selects H2D_CAL_CFG_PROBE and fb == 0xff the
 * default field B for H2D_MSG_CFG_CAL (ignored for every other message).
 * h2d_msg_build() is the shorthand with both sentinels. */
int h2d_msg_build_ext(enum h2d_msg msg, u8 dst, u16 user_id, const u8 *addr,
		      u16 cfg_id, u8 fb, struct h2d_frame *f, u8 *wire,
		      u32 cap, u32 *total, enum h2d_err *err);

/* Strict h2d_cfg_cal sysfs grammar: "N" | "N fb=M" | "N,M" | "N:M" where N is
 * a 1..6 vendor-order selector or a literal cfg id 3502..3507, and M is the
 * header field B (0..0x12).  Defaults: fb = 4.  Returns 0, -EINVAL/-ERANGE. */
int h2d_cfg_cal_parse(const char *buf, u16 *cfg_id, u8 *fb);

/* Strict sysfs grammar:
 *   h2d_send: "txq_sync" | "txq_sync72" | "ctx_create" | "ctx_create_t1" |
 *             "ctx_create_t2" | "stub [dst=N]" | "edca [dst=N]" |
 *             "del_user user=U [addr=8hex] [dst=N]"
	*             "cfg_cal [cfg=N] [fb=M]"   (N 1..6 or 3502..3507, M 0..0x12)
	*   h2d_cfg_cal (WO): "N" | "N fb=M"   (same N/M grammar;
	*             header field B defaults to 4 = the module the
	*             ordered context-create made)
 * Commas/tabs/newlines are separators; "txq_sync dst=0" is the only accepted
 * TXQ form (the canned frame is byte-exact with dst 0) and "ctx_create dst=0"
 * the only accepted context-create form.  The three ctx_create variants are
 * the same 48-byte frame with data[0] = 3 / 1 / 2 (the create helper's type
 * dispatch; see REPORT.md section 12).
 * Returns 0, -EINVAL (grammar) or -ERANGE (field overflow). */
int h2d_req_parse(const char *buf, struct h2d_req *req);

#endif /* H2D_SEND_LOGIC_H */