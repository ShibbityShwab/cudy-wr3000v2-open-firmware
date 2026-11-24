/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vapscan_frames.h - HCC user/VAP-add (g0 id7/id8), user-del (g0 id9) and
 * scan-request (g0 id0xC) frame builders for the Hi5622V100 clean-room host
 * (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code (lane wifi-vap-scan, shared task task-110,
 * 2026-11-25), written from the project's committed decoded evidence and from
 * disassembly of the locally held vendor modules: no vendor source, object
 * code, firmware byte, calibration value or secret is reproduced here.  Every
 * constant below is a derived protocol constant; the evidence marker for each
 * field is in REPORT.md (section "Frame specifications").
 *
 * The file is pure C99 with no libc dependency: it compiles unchanged as a
 * userspace unit (selftest) and inside a kernel module (define __KERNEL__).
 *
 * Layer model (same as ../wifi-h2d-frames):
 *
 *   HCC frame    12-byte header + payload (this file) - the message body
 *   SR carrier    8-byte node {devva, (total<<16)|0x6D2B} + packed producer
 *   mailbox       out[0] pending bitmap + out[2] doorbell (layer A), the SR
 *                 commit is announced with mailbox id 3.
 *
 * The four messages (all group 0 = chip group; there is no group-1/group-4
 * firmware H2D table):
 *
 *   g0 id 0x07  hmac_user_add            168 B payload [V] vendor builder
 *   g0 id 0x08  hmac_user_add_notify_alg 168 B payload [V] vendor builder
 *   g0 id 0x09  hmac_del_user_notify_dmac 12 B payload [V] vendor builder
 *   g0 id 0x0C  hmac_enc_scan_send_req   508 B payload [V] vendor builder,
 *               scan fields [V]/[I] (some paths [U], see REPORT.md)
 *
 * Evidence anchors (committed unless noted):
 *   ../wifi-h2d-catalog/REPORT.md        firmware g0 receive table + handlers
 *   ../wifi-datapath/FRAME-SPECS.md      HCC header/carrier rules, g0 id9
 *   ../wifi-h2d-frames/src/h2d_frames.*  same header/carrier conventions
 *   ../wifi-vendor-userspace/TABLES.md   vendor/cfg80211 op -> message map
 *   ../wifi-proto-seq/REPORT.md          calibration ladder that precedes this
 *   local vendor hi5622v100_wifi.ko      derived addresses only (not committed)
 */
#ifndef VAPSCAN_FRAMES_H
#define VAPSCAN_FRAMES_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/string.h>
#else
#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Portability edit (wifi-scan-ap integration): shared host type guard with
 * ../wifi-scan-ap/src/hi5622_frames_api.h / h2d_frames.h so a host selftest
 * can include both portable headers under -pedantic.  No protocol content is
 * changed in this file. */
#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef uint64_t u64;
#endif
#endif

/* ===================================================================
 * 1. HCC header and carrier constants (shared with wifi-h2d-frames)
 * =================================================================== */
#define VS_HDR_LEN		12u
#define VS_TAG			0x5a5au	/* transport tag at hdr+0x0a [V] */
#define VS_GROUP_CHIP		0u	/* the only group these messages use [V] */
#define VS_ALLOC_NIBBLE		0x01u	/* hcc_msg_alloc state [V] */
#define VS_MAX_PAYLOAD		0x674u
#define VS_MAX_FRAME		(VS_HDR_LEN + VS_MAX_PAYLOAD)

/* SR node + packed producer (wifi-hcc-re / wifi-h2d-frames sec 5) [V] */
#define VS_SR_NODE_LEN		8u
#define VS_SR_NODE_FLAGS	0x6d2bu
#define VS_SR_IDX_LOW_MASK	0x3ffu
#define VS_SR_IDX_PHASE		0x400u

/* Message ids and payload lengths, firmware g0 receive table [V] */
#define VS_ID_USER_ADD		0x07u
#define VS_ID_USER_ALG		0x08u
#define VS_ID_USER_DEL		0x09u
#define VS_ID_SCAN_REQ		0x0cu
#define VS_LEN_USER_ADD		0xa8u
#define VS_LEN_USER_ALG		0xa8u
#define VS_LEN_USER_DEL		0x0cu
#define VS_LEN_SCAN_REQ		0x1fcu

/* ---- g0 id7 hmac_user_add payload (168 B) [V] vendor builder fields ---- */
#define VS_UA_OFF_UID		0x00u	/* u16 user id (firmware lookup) [V] */
#define VS_UA_OFF_MAC		0x02u	/* u8[6] MAC [V] */
#define VS_UA_OFF_TYPE		0x8eu	/* u8 user type 2 normal [V] */
#define VS_UA_OFF_RSSI		0xa4u	/* s8 RSSI, vendor default +100 [V] */

/* ---- g0 id8 hmac_user_add_notify_alg payload (168 B) [V] ---- */
#define VS_ALG_OFF_UID		0x00u	/* u16 user id [V] */
#define VS_ALG_OFF_ALG		0x08u	/* u16 alg/cfg value from params+0x3e0 [V] */
#define VS_ALG_OFF_VHT		0x10u	/* 24 B VHT capability block [V] */
#define VS_ALG_OFF_HT		0x28u	/* 44 B HT capability block [V] */
#define VS_ALG_OFF_HE		0x54u	/* 58 B HE capability block [V] */
#define VS_ALG_OFF_NSS		0xa5u	/* u8 (vendor user+0xaa6) [V] */
#define VS_ALG_LEN_VHT		0x18u
#define VS_ALG_LEN_HT		0x2cu
#define VS_ALG_LEN_HE		0x3au

/* ---- g0 id9 hmac_del_user_notify_dmac payload (12 B) [V] ---- */
#define VS_UD_OFF_UID		0x00u	/* u16 user id; 0x0fff = by MAC [V] */
#define VS_UD_OFF_MAC		0x02u	/* u8[6] MAC [V] */
#define VS_UD_OFF_FLAG		0x08u	/* u8 (vendor 4th arg, semantic [U]) [V] */
#define VS_UID_BY_MAC		0x0fffu

/* ---- g0 id0xC hmac_enc_scan_send_req payload (508 B) ----
 * Decoded from the vendor cfg80211 STA scan builder + firmware parser.  The
 * struct is copied wholesale by the vendor builder; offsets below are [V]
 * writes/reads, semantics [I] where annotated in REPORT.md. */
#define VS_SC_OFF_FLAGS0	0x00u	/* u8 = 1 [V] */
#define VS_SC_OFF_FLAGS1	0x01u	/* u8 dual-band/scan flag [V] */
#define VS_SC_OFF_HAS_BSSID	0x02u	/* u8 = 1 after bcast copy [V] */
#define VS_SC_OFF_N_SSIDS	0x03u	/* u8 SSID count [V] */
#define VS_SC_OFF_TYPE_B	0x0bu	/* u8 2 or 15 (sign of wal+0x138) [V] */
#define VS_SC_OFF_BSSID		0x0cu	/* u8[6] broadcast BSSID [V] */
#define VS_SC_OFF_SSID_AREA	0x18u	/* 8 x 36 B SSID slots: {str, 0} [V] */
#define VS_SC_SSID_STRIDE	0x24u
#define VS_SC_SSID_MAX		8u
#define VS_SC_SSID_LEN_MAX	32u
#define VS_SC_OFF_SSID_CNT2	0x131u	/* u8 flags written by chan-switch path [V] */
#define VS_SC_OFF_COUNT_B	0x138u	/* u8 = n_ssids (vendor write) [V] */
#define VS_SC_OFF_CHAN_AREA	0x140u	/* up to 43 x 4 B entries [V] */
#define VS_SC_CHAN_STRIDE	4u
#define VS_SC_CHAN_MAX		42u
#define VS_SC_OFF_CHAN_COUNT	0x1ecu	/* u8 channel count [V] */
#define VS_SC_OFF_DWELL		0x1eeu	/* u16 dwell ms 20/40/60 [V] */
#define VS_SC_OFF_SCAN_TYPE	0x1f0u	/* u8, firmware rejects 4 and 8 [V] */
#define VS_SC_OFF_ONE		0x1f1u	/* u8 = 1 [V] */
#define VS_SC_OFF_TYPE_1F2	0x1f2u	/* u8 = 11 vendor STA scan [V] */
#define VS_SC_OFF_TAIL32	0x1f8u	/* u32 = 0 [V] */

/* ===================================================================
 * 2. Frame model, errors, parameter structs
 * =================================================================== */
struct vs_frame {
	u8  buf[VS_MAX_FRAME];
	u16 len;
};

enum vs_err {
	VS_OK = 0,
	VS_EARG,	/* bad argument / field value		    */
	VS_ESHORT,	/* shorter than the 12-byte HCC header	    */
	VS_ETAG,	/* transport tag != 0x5a5a		    */
	VS_ELEN,	/* declared total inconsistent / zero	    */
	VS_EDST,	/* hdr[9] != hdr[1] >> 4			    */
	VS_EALLOC,	/* alloc nibble != 1 on an H2D frame	    */
	VS_EGROUP,	/* group has no firmware H2D receive table   */
	VS_EID,		/* unknown / unsupported message id	    */
	VS_EBOUNDS,	/* payload/field bound exceeded		    */
	VS_EBUFFER,	/* caller output buffer too small	    */
	VS_EPOLICY,	/* lane/field policy refused (see REPORT)    */
};

/* g0 id7 */
struct vs_user_add {
	u16 user_id;		/* payload+0x00 [V] */
	u8  mac[6];		/* payload+0x02 [V] */
	u8  chip_id;		/* hdr[3]: 0 = 2.4 GHz, 1 = 5 GHz [I] */
	u8  vap_id;		/* hdr[2] [I] */
	u8  dst;		/* hdr[1] high nibble, 0 [V] */
	u8  type;		/* payload+0x8e: 2 = normal [V] */
	s8  rssi;		/* payload+0xa4: +100 vendor default [V] */
};

/* g0 id8 */
struct vs_user_alg {
	u16 user_id;		/* payload+0x00 [V] */
	u8  chip_id;
	u8  vap_id;
	u8  dst;
	u16 alg;		/* payload+0x08, vendor params+0x3e0 [V]/[U] */
	u8  nss;		/* payload+0xa5, vendor user+0xaa6 [V]/[I] */
	const u8 *vht;		/* 24 B, NULL = zeros [V] offset, [I] value */
	const u8 *ht;		/* 44 B, NULL = zeros */
	const u8 *he;		/* 58 B, NULL = zeros */
};

/* g0 id0xC */
struct vs_ssid {
	u8 len;			/* 0..32 */
	u8 str[VS_SC_SSID_LEN_MAX];
};

struct vs_chan {
	u8 num;			/* channel number, entry+0x00 [V] */
	u8 band;		/* band/chip id 0/1, entry+0x01 [V] */
	u8 pad;			/* entry+0x02, zero */
	u8 idx;			/* band channel index, entry+0x03 [V]/[I] */
};

struct vs_scan_params {
	u8  chip_id;
	u8  vap_id;
	u8  dst;
	u8  flags0;		/* payload+0x00: 1 [V] */
	u8  flags1;		/* payload+0x01: dual-band flag [V] */
	u8  has_bssid;		/* payload+0x02: 1 [V] */
	u8  type_b;		/* payload+0x0b: 2 (active) or 15 [V]/[I] */
	u8  bssid[6];		/* payload+0x0c: broadcast [V] */
	u8  n_ssids;
	struct vs_ssid ssids[VS_SC_SSID_MAX];
	u8  n_chans;
	struct vs_chan chans[VS_SC_CHAN_MAX];
	u16 dwell_ms;		/* payload+0x1ee: 20/40/60 [V] */
	u8  scan_type;		/* payload+0x1f0: 0, never 4/8 [V] */
};

/* ===================================================================
 * 3. Defaults, builders, validator, carrier helpers
 * =================================================================== */
void vs_user_add_defaults(struct vs_user_add *p);
void vs_user_alg_defaults(struct vs_user_alg *p);
void vs_scan_defaults(struct vs_scan_params *p);

int vs_build_user_add(struct vs_frame *f, const struct vs_user_add *p);
int vs_build_user_alg(struct vs_frame *f, const struct vs_user_alg *p);
int vs_build_user_del(struct vs_frame *f, u16 user_id, const u8 mac[6],
		      u8 chip_id, u8 vap_id, u8 flag);
int vs_build_scan_req(struct vs_frame *f, const struct vs_scan_params *p);

/* Header-level validation + family/payload-size agreement. */
int vs_frame_validate(const u8 *buf, size_t len);

/* Decode the family from the header: family = message id (7/8/9/0xC). */
int vs_frame_decode(const u8 *buf, size_t len, u8 *family, u8 *chip_id,
		    u8 *vap_id, u16 *payload_len);

int vs_frame_export(const struct vs_frame *f, u8 *out, size_t cap,
		    size_t *out_len);

const char *vs_family_name(u8 family);
const char *vs_strerror(int err);

/* SR carrier helpers (only for drivers that do not already own them). */
int vs_sr_node_encode(u8 node[VS_SR_NODE_LEN], u32 buf_devva, u16 total_len);
u32 vs_sr_producer_next(u32 producer, u32 depth);

#endif /* VAPSCAN_FRAMES_H */
