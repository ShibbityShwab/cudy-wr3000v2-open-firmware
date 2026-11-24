/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vapstart_frames.h - byte-exact builders for the Hi5622V100 VAP start family
 * (Cudy WR3000 v2.0, luofu v2): the g0 id1 config events that create/start a
 * firmware VAP/module context, the g0 id0xF/id0x10 start_sta pair, and the WAL
 * group-2 cfg-event envelope (VAP add / VAP start) that is the vendor's real
 * VAP-create/start path.
 *
 * ORIGINAL clean-room code (lane wifi-vap-start, shared task task-117,
 * 2026-11-26).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced here; the derivation (firmware handler decode,
 * vendor builder decode, address/constant evidence) is recorded in
 * ../REPORT.md and ../TABLES.md.
 *
 * Design rules (inherited project discipline):
 *   - pure C99: compiles host-side and inside the 6.18.60 kernel module;
 *   - builders zero-fill the whole frame, then write only decoded fields;
 *   - builders fail closed (negative errno) on any out-of-bounds parameter;
 *   - the validator re-checks every structural rule the firmware enforces
 *     (tag, total == length, group/alloc/dst, known id/payload family);
 *   - nothing here touches a device.
 *
 * Address conventions used in comments: firmware CPU address (CA) = file
 * offset + 0x40000.  Evidence markers: [V] decoded here from the pinned local
 * blobs, [V*] device-measured by an earlier lane, [I] inference, [U] unknown.
 */
#ifndef VAPSTART_FRAMES_H
#define VAPSTART_FRAMES_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/string.h>
#else
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* Shared portable-type guard with hi5622_frames_api.h / h2d_frames.h so a
 * host test (and the kernel module) may include both headers under -pedantic. */
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
 * 1. Wire bounds / constants (shared with the other H2D lanes)
 * =================================================================== */
#define VSF_HDR_LEN		12u
#define VSF_MAX_PAYLOAD		0x674u	/* hcc_msg_alloc bound [V] */
#define VSF_MAX_FRAME		(VSF_HDR_LEN + VSF_MAX_PAYLOAD)
#define VSF_TAG			0x5a5au	/* transport tag at hdr+0x0A [V] */
#define VSF_HCC_GROUP_CHIP	0u
#define VSF_HCC_GROUP_CORE	2u

#define VSF_ID_CFG_EVENT		1u	/* g0 id1 config-event demux [V] */

/* g0 id1 config-event keys (table A CA 0x10C23C, 8 B records {u16 key; u16
 * pad; u32 worker}) [V]:
 *   0x0064 -> worker CA 0x449E4  module/VAP context add (create helper
 *                                CA 0xFE6BC; binds module+0x4B0/+0x4B8)
 *   0x0065 -> worker CA 0x46394  VAP start (hmac_config_start_vap_event)
 *   (0x0066/0x0067 = VAP delete/down; their wire bodies are still [U].) */
#define VSF_KEY_VAP_ADD		0x0064u
#define VSF_KEY_VAP_START	0x0065u

/* g0 direct start_sta pair [V]:
 *   0x000F -> handler CA 0x62EC0, 64-byte payload (join request params)
 *   0x0010 -> handler CA 0x5E28C, 16-byte payload (join/start) */
#define VSF_ID_STA_JOIN_PARAMS	0x000fu
#define VSF_ID_STA_JOIN_START	0x0010u

/* WAL group-2 cfg-event envelope (vendor wal_alloc_cfg_event @0x126ef0 /
 * wal_send_cfg_event @0x1270d8) [V]: 12-byte HCC header + u32 response
 * cookie + {u32 wal_id=1; u16 pad; u16 data_len; u32 seq} + {u16 cmd;
 * u16 body_len; body[body_len]}.  Frame total = 32 + body_len. */
#define VSF_WAL_MSG_CFG		1u
#define VSF_WAL_HDR_OFF		0x10u
#define VSF_WAL_DATA_OFF		0x1cu
#define VSF_WAL_OVERHEAD		32u
#define VSF_WAL_CMD_VAP_ADD	0x0064u
#define VSF_WAL_CMD_VAP_START	0x0065u
#define VSF_WAL_CMD_AP_START	0x0127u

/* VAP iftype values carried in the g0 id1 data[0] and WAL VAP-add body[0].
 * hmac_config_add_vap only accepts modes 1/2/3 [V]; the firmware create helper
 * allocates the context for data[0] in {1,2,3} [V].  2/3 are the vendor AP/STA
 * interface types [I]. */
#define VSF_IFTYPE_AP		2u
#define VSF_IFTYPE_STA		3u
#define VSF_IFTYPE_MINIMAL	1u

#define VSF_MODULE_MAX		0x12u	/* module-table index bound [V] */
#define VSF_MAC_LEN		6u
#define VSF_STA_JOIN_IE_LEN	33u

/* ===================================================================
 * 2. Frame object + parameter structs
 * =================================================================== */
struct vsf_frame {
	u8	buf[VSF_MAX_FRAME];
	u16	len;
};

struct vsf_vap_add {
	u8	iftype;		/* VSF_IFTYPE_* -> data[0] (create type) */
	u8	parent;		/* parent module index -> data[1] [I] */
	u8	module;		/* module/VAP index to create -> data[4] */
	u8	band;		/* 0 = 2.4 GHz, 1 = 5 GHz -> data[0x12] [I] */
	u8	field_a;	/* hdr[2] (VAP id source), default 0 */
	u8	field_b;	/* hdr[3] (handler module lookup index) */
	u8	field_c;	/* hdr[8], default 0 */
	u8	dst;
};

struct vsf_vap_start {
	u8	channel;	/* data[0] - vendor hmac_config_do_start_vap arg [V] */
	u8	state;		/* data[1] <- vap+0x0f [V] */
	u8	cfg_a;		/* data[2] <- vap+0x11 [V] */
	u8	cfg_b;		/* data[3] <- vap+0x12 [V] */
	u32	cfg_word;	/* data[4..7] <- vap+0x414 [V] */
	u8	field_a;
	u8	field_b;	/* hdr[3] = module index the VAP start applies to */
	u8	field_c;
	u8	dst;
};

struct vsf_sta_join_start {
	u8	flag5;		/* payload[0] <- join params +0x05 [V] */
	u8	flag6;		/* payload[4] <- join params +0x06 [V] */
	u8	mac[VSF_MAC_LEN];	/* payload[8..13] <- vap+0xF68 own MAC [V] */
	u8	dst;
};

struct vsf_sta_join_params {
	u8	bssid[VSF_MAC_LEN];	/* payload[0..5] <- join +0x36 [V] */
	u16	channel;	/* payload[6..7] <- join +0x2c [V] */
	u32	band_info;	/* payload[8..11] <- vap+0xFB8 [V] */
	u8	ie[VSF_STA_JOIN_IE_LEN];	/* payload[20..52] [V] */
	u8	flag52;		/* payload[52] (vendor writes 0) [V] */
	u8	flag54;		/* payload[54] machine bit [V] */
	u8	dst;
};

struct vsf_wal_vap_add {
	u8	iftype;		/* body[0] */
	u8	sub_type;	/* body[1] */
	u8	flag7;		/* body[7] */
	u8	band;		/* body[12] [I] */
	u8	band_type;	/* body[17] [I] */
	u8	band_id;	/* body[18] */
	u8	sub19;		/* body[19] */
	u32	cookie;		/* body[20..23] response-matching cookie [V] */
	u32	seq;		/* WAL cfg-event sequence word [I] */
	u8	field_a;
	u8	field_b;
	u8	field_c;
	u8	dst;
};

struct vsf_wal_vap_start {
	u8	start_flag;	/* body[0] = 1 in the vendor builder [V] */
	u32	cookie;		/* body[8..11] [V] */
	u32	seq;		/* WAL cfg-event sequence word [I] */
	u8	field_a;
	u8	field_b;
	u8	field_c;
	u8	dst;
};

struct vsf_wal_ap_start_cfg {
	const u8 *body;		/* caller-supplied 156-byte AP cfg body (M3 gap) */
	u32	cookie;
	u32	seq;		/* WAL cfg-event sequence word [I] */
	u8	field_a;
	u8	field_b;
	u8	field_c;
	u8	dst;
};

/* ===================================================================
 * 3. Builders (0 on success; -EINVAL/-ENOSPC on refusal)
 * =================================================================== */
int vsf_build_vap_add(struct vsf_frame *f, const struct vsf_vap_add *p);
int vsf_build_vap_start(struct vsf_frame *f, const struct vsf_vap_start *p);
int vsf_build_sta_join_start(struct vsf_frame *f, const struct vsf_sta_join_start *p);
int vsf_build_sta_join_params(struct vsf_frame *f, const struct vsf_sta_join_params *p);
int vsf_build_wal_vap_add(struct vsf_frame *f, const struct vsf_wal_vap_add *p);
int vsf_build_wal_vap_start(struct vsf_frame *f, const struct vsf_wal_vap_start *p);
int vsf_build_wal_ap_start_cfg(struct vsf_frame *f,
			      const struct vsf_wal_ap_start_cfg *p);

/* Defaults = the vendor boot-time baseline where known, otherwise zero. */
void vsf_vap_add_defaults(struct vsf_vap_add *p);
void vsf_vap_start_defaults(struct vsf_vap_start *p);
void vsf_sta_join_start_defaults(struct vsf_sta_join_start *p);
void vsf_sta_join_params_defaults(struct vsf_sta_join_params *p);
void vsf_wal_vap_add_defaults(struct vsf_wal_vap_add *p);
void vsf_wal_vap_start_defaults(struct vsf_wal_vap_start *p);

/* ===================================================================
 * 4. Validator / decoder (structural rules the firmware enforces)
 * =================================================================== */
#define VSF_FAMILY_NONE		0
#define VSF_FAMILY_CFG_EVENT	1	/* g0 id1 */
#define VSF_FAMILY_DIRECT_G0	2	/* g0 id in {0xF,0x10} */
#define VSF_FAMILY_WAL_CFG	3	/* group 2, WAL cfg envelope */

struct vsf_info {
	u8	family;
	u8	group;
	u16	id;
	u16	total;
	u16	key;		/* cfg-event key, else 0 */
	u16	wal_cmd;	/* WAL command, else 0 */
	u16	wal_len;	/* WAL body length, else 0 */
};

int vsf_frame_validate(const struct vsf_frame *f);
int vsf_frame_decode(const struct vsf_frame *f, struct vsf_info *out);

/* Copy a built frame into a caller wire buffer (kernel provider path). */
int vsf_frame_export(const struct vsf_frame *f, u8 *dst, size_t cap,
		    size_t *out_len);

#endif /* VAPSTART_FRAMES_H */