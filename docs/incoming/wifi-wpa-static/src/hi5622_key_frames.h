/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_key_frames.h - WPA2 key-install H2D frame builders (Hi5622V100
 * clean-room driver, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-wpa-static lane, shared task task-139,
 * 2026-11-28).  Derived from static reversal of the local vendor host module
 * (2.4.15 md5 1745ef77... and 2.5.24 md5 4737fcb2...) and the local vendor
 * firmware blob (sha256 7fc87e20...).  Only derived addresses, constants and
 * field offsets plus original code appear here - no vendor source, object
 * byte, firmware byte, calibration value or secret is reproduced.  Evidence:
 * ../REPORT.md, ../TABLES.md, ../EVIDENCE-INDEX.md.
 *
 * WHAT THIS BUILDS
 * ----------------
 * The vendor cfg80211 key ops (wal_cfg80211_add_key 2.4.15 0x1007f4 /
 * 2.5.24 0x1009fc, identical 848-B code; remove_key 0x100d98 / 0x100ffa;
 * set_default_key 0x100ffc / 0x101204) send ONE firmware config event per
 * key operation.  The static-dispatch carrier is the group-0 id-1 config
 * event (the same family the bench-proven AP-start / TXQ-sync frames use):
 * the vendor hmac path hmac_user_add_key (0x1638) -> hdpp_config_send_event
 * (0x59c0) emits exactly that frame with command 0x0078 and body 0x44.  The
 * firmware table A rows are [V]:
 *
 *   key 0x0078 -> worker CA 0x0A5825  key install
 *   key 0x0079 -> worker CA 0x0A5C9D  default key
 *   key 0x007A -> worker CA 0x0A4271  key remove
 *
 * BODY LAYOUTS (all offsets firmware-worker-verified)
 * ---------------------------------------------------
 * add-key body (0x44 B, worker 0xA5825 reads every field):
 *   +0x00 u8  key_index            (<= 5; data ciphers <= 3, mgmt 4..5)
 *   +0x01 u8  pairwise             (1 = per-STA key, 0 = group/VAP key)
 *   +0x02 u8  mac_addr[6]          (pairwise: the firmware looks the user up)
 *   +0x08 u8  key[32]              (firmware key cache, <= 0x20)
 *   +0x28 u8  seq[16]              (<= 0x10)
 *   +0x38 u32 key_len
 *   +0x3c u32 seq_len
 *   +0x40 u32 cipher               (cfg80211 WLAN_CIPHER_SUITE_*; the worker
 *                                   inspects the low byte as its cipher enum)
 *
 * del-key body (8 B, worker 0xA4271):
 *   +0x00 u8 key_index, +0x01 u8 pairwise, +0x02 u8 mac[6]
 *
 * default-key body (0x28 B, worker 0xA5C9D):
 *   +0x00 u8 key_index, +0x01 u8 unicast, +0x02 u8 multicast
 *
 * The firmware worker cipher classes (low byte of the cfg80211 suite) are
 * [V] from worker 0xA5825:
 *   data  0x0714 mask = {TKIP 2, CCMP 4, GCMP 8, GCMP-256 9, CCMP-256 10}
 *   mgmt  0x3840 mask = {AES-CMAC 6, BIP-GMAC-128 11, BIP-GMAC-256 12,
 *                        BIP-CMAC-256 13}   (keys 4..5 only)
 *   WEP   0x0022 mask = {WEP40 1, WEP104 5}
 * Anything else (0, 3, 7, >=14) is rejected by the firmware and refused here.
 *
 * Pure C99: the host selftest (tools/key_selftest.c) and the 6.18 kernel
 * module compile the same code.  No speculative bytes: a builder that cannot
 * produce a fully decoded frame returns -EOPNOTSUPP and the caller posts
 * nothing.
 */
#ifndef HI5622_KEY_FRAMES_H
#define HI5622_KEY_FRAMES_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/string.h>
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
typedef uint64_t u64;
#endif
#endif

/* ---- wire/HCC bounds (inherited, [V]) ---- */
#define HKF_HCC_HDR_LEN		12u
#define HKF_HCC_TAG		0x5a5au
#define HKF_HCC_GROUP_CHIP	0u
#define HKF_HCC_ID_CFG_EVENT	1u
#define HKF_ALLOC_NIBBLE	0x01u
/* g0 id1 config event: hdr[12] token[8] key[2] len[2] body[len] pad[4] */
#define HKF_G0_OVERHEAD		28u
#define HKF_MAX_FRAME		256u
#define HKF_MAX_MODULE		0x12u
#define HKF_MAX_DST		0x0fu

/* g0 id1 config-event command ids = firmware table A keys [V] */
#define HKF_CMD_ADD_KEY		0x0078u
#define HKF_CMD_SET_DEFAULT_KEY	0x0079u
#define HKF_CMD_DEL_KEY		0x007au

/* Body sizes [V] */
#define HKF_ADD_KEY_BODY_LEN	0x44u
#define HKF_DEL_KEY_BODY_LEN	8u
#define HKF_DEFAULT_KEY_BODY_LEN 0x28u

/* Firmware worker bounds [V] */
#define HKF_KEY_MAX		32u
#define HKF_SEQ_MAX		16u
#define HKF_MAX_KEY_INDEX	5u
#define HKF_MAX_DATA_KEY_INDEX	3u
#define HKF_MIN_MGMT_KEY_INDEX	4u

/* Firmware worker cipher low-byte class masks [V] */
#define HKF_CIPHER_CLASS_DATA	0x0714u
#define HKF_CIPHER_CLASS_MGMT	0x3840u
#define HKF_CIPHER_CLASS_WEP	0x0022u
#define HKF_CIPHER_LOW_MAX	0x0du

enum hkf_cipher_class {
	HKF_CIPHER_UNKNOWN = 0,
	HKF_CIPHER_DATA,
	HKF_CIPHER_MGMT,
	HKF_CIPHER_WEP,
};

/* One key installation in the neutral form the wire builders need. */
struct hkf_key_params {
	u8	key_index;	/* 0..5 (data <= 3; mgmt 4..5) */
	u8	pairwise;	/* 1 = per-STA key; 0 = group/VAP key */
	u8	mac[6];		/* pairwise peer address; zero for group keys */
	u32	cipher;		/* cfg80211 WLAN_CIPHER_SUITE_* */
	u16	key_len;	/* <= 32 */
	u16	seq_len;	/* <= 16 */
	u8	key[HKF_KEY_MAX];
	u8	seq[HKF_SEQ_MAX];
};

/* One default-key selection (data keys only; keys 4/5 are the separate
 * set_default_mgmt_key firmware path, not modelled here). */
struct hkf_default_key_params {
	u8	key_index;	/* 0..3 */
	u8	unicast;
	u8	multicast;
};

/* One built wire frame. */
struct hkf_frame {
	u8	buf[HKF_MAX_FRAME];
	size_t	len;
	u16	cmd;
};

/* Cipher classification from the cfg80211 suite's low byte, exactly as the
 * firmware worker does it.  HKF_CIPHER_UNKNOWN = the firmware refuses it. */
enum hkf_cipher_class hkf_cipher_classify(u32 cipher);
const char *hkf_cipher_class_name(enum hkf_cipher_class c);

/* 0 = the parameter struct is structurally usable and the firmware worker
 * would accept it (index/cipher-class pairing, lengths, pairwise MAC). */
int hkf_key_params_validate(const struct hkf_key_params *p);
int hkf_default_key_params_validate(const struct hkf_default_key_params *p);

/* Body builders.  The caller supplies a buffer of at least the matching
 * *_BODY_LEN bytes; every body is zero-filled first so reserved bytes are
 * deterministic (no uninitialised wire bytes). */
int hkf_build_body_add_key(u8 *body, size_t cap,
			   const struct hkf_key_params *p);
int hkf_build_body_del_key(u8 *body, size_t cap,
			   const struct hkf_key_params *p);
int hkf_build_body_default_key(u8 *body, size_t cap,
			       const struct hkf_default_key_params *p);

/* Complete g0 id1 frames.  dst is the HCC destination nibble (0 = the local
 * endpoint on this board); module is the g0 header vendor module/VAP index. */
int hkf_build_add_key(struct hkf_frame *f, u8 module, u8 dst,
		      const struct hkf_key_params *p);
int hkf_build_del_key(struct hkf_frame *f, u8 module, u8 dst,
		      const struct hkf_key_params *p);
int hkf_build_default_key(struct hkf_frame *f, u8 module, u8 dst,
			  const struct hkf_default_key_params *p);

#endif /* HI5622_KEY_FRAMES_H */
