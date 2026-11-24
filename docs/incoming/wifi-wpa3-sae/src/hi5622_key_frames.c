// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_key_frames.c - WPA2 key-install H2D frame builders (Hi5622V100
 * clean-room driver, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-wpa-static lane, shared task task-139,
 * 2026-11-28).  Every constant is a derived value with an evidence marker in
 * ../REPORT.md; no vendor source/object/firmware byte is reproduced.
 *
 * The wire family is the group-0 id-1 cfg event that the bench has already
 * executed (AP start / TXQ sync): 12-byte HCC header + token[8] + u16 cmd +
 * u16 body_len + body + 4 pad bytes.  The vendor host builds the same family
 * from hmac_user_add_key -> hdpp_config_send_event(vap, 0x78, 0x44, body)
 * and the firmware table A row 0x78 lands on the key-install worker
 * CA 0x0A5825, which reads the 0x44-byte body field by field.
 */
#include "hi5622_key_frames.h"

static void hkf_put16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

static void hkf_put32(u8 *p, u32 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
	p[2] = (u8)((v >> 16) & 0xffu);
	p[3] = (u8)((v >> 24) & 0xffu);
}

static int hkf_mac_is_unicast(const u8 *mac)
{
	if (!mac)
		return 0;
	if (mac[0] & 0x01u)	/* multicast/broadcast bit */
		return 0;
	return (mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) != 0;
}

enum hkf_cipher_class hkf_cipher_classify(u32 cipher)
{
	u32 low = cipher & 0xffu;
	u32 bit;

	if (low == 0u || low > HKF_CIPHER_LOW_MAX)
		return HKF_CIPHER_UNKNOWN;
	bit = 1u << low;
	if (bit & HKF_CIPHER_CLASS_DATA)
		return HKF_CIPHER_DATA;
	if (bit & HKF_CIPHER_CLASS_MGMT)
		return HKF_CIPHER_MGMT;
	if (bit & HKF_CIPHER_CLASS_WEP)
		return HKF_CIPHER_WEP;
	return HKF_CIPHER_UNKNOWN;
}

const char *hkf_cipher_class_name(enum hkf_cipher_class c)
{
	switch (c) {
	case HKF_CIPHER_DATA:	return "data";
	case HKF_CIPHER_MGMT:	return "mgmt";
	case HKF_CIPHER_WEP:	return "wep";
	default:		return "unknown";
	}
}

int hkf_key_params_validate(const struct hkf_key_params *p)
{
	enum hkf_cipher_class cls;

	if (!p)
		return -EINVAL;
	if (p->key_index > HKF_MAX_KEY_INDEX)
		return -EINVAL;
	if (p->pairwise > 1u)
		return -EINVAL;
	if (p->key_len > HKF_KEY_MAX || p->seq_len > HKF_SEQ_MAX)
		return -EINVAL;
	cls = hkf_cipher_classify(p->cipher);
	if (cls == HKF_CIPHER_UNKNOWN)
		return -EOPNOTSUPP;
	if (cls == HKF_CIPHER_DATA && p->key_index > HKF_MAX_DATA_KEY_INDEX)
		return -EINVAL;	/* firmware worker 0xA5825: data key idx <= 3 */
	if (cls == HKF_CIPHER_MGMT &&
	    (p->key_index < HKF_MIN_MGMT_KEY_INDEX ||
	     p->key_index > HKF_MAX_KEY_INDEX))
		return -EINVAL;	/* firmware worker 0xA5825: mgmt key idx 4..5 */
	if (p->pairwise) {
		if (!hkf_mac_is_unicast(p->mac))
			return -EINVAL;
	} else {
		/* Group keys are addressed to the VAP; the vendor builder passes
		 * the broadcast address for GTK (the firmware ignores the MAC on
		 * the group path), so a nonzero MAC here is a caller bug. */
		if (hkf_mac_is_unicast(p->mac))
			return -EINVAL;
	}
	/* WEP keys may be 5 or 13 bytes (and 16 in the vendor quirks); the
	 * firmware worker bounds only key_len <= 32.  Empty keys are refused
	 * by cfg80211 before this layer, so require a non-empty key. */
	if (!p->key_len)
		return -EINVAL;
	return 0;
}

int hkf_default_key_params_validate(const struct hkf_default_key_params *p)
{
	if (!p)
		return -EINVAL;
	if (p->mgmt > 1u)
		return -EINVAL;
	if (p->mgmt) {
		/* wifi-wpa3-sae lane (task-144): the firmware default-key
		 * worker 0xA5C9D routes key_index > 3 to the management-key
		 * branch, which requires:
		 *   - key_index 4..5 (worker: sub #4, cmp #1, bhi error 0x520a);
		 *   - one of the body+1/body+2 class flags set (worker error
		 *     0x545) - the vendor/IGTK form is unicast=0, multicast=1;
		 *   - a cipher byte at body+0x04 that is in the mgmt class mask
		 *     0x3840 (worker error 0x520b for 0 or any non-mgmt value).
		 * cfg80211's set_default_mgmt_key op does not carry the
		 * cipher, so the caller supplies the one observed on the
		 * preceding add_key (IGTK) and an unknown cipher is refused
		 * here, never posted as a guessed byte. */
		if (p->key_index < HKF_MIN_MGMT_KEY_INDEX ||
		    p->key_index > HKF_MAX_KEY_INDEX)
			return -EINVAL;
		if (p->unicast || !p->multicast)
			return -EINVAL;
		if (hkf_cipher_classify(p->cipher) != HKF_CIPHER_MGMT)
			return -EOPNOTSUPP;
		return 0;
	}
	/* data-key path: keys 0..3 only */
	if (p->key_index > HKF_MAX_DATA_KEY_INDEX)
		return -EINVAL;
	if (!p->unicast && !p->multicast)
		return -EINVAL;	/* worker 0xA5C9D: one class flag required */
	return 0;
}

int hkf_build_body_add_key(u8 *body, size_t cap, const struct hkf_key_params *p)
{
	int rc = hkf_key_params_validate(p);

	if (rc)
		return rc;
	if (!body || cap < HKF_ADD_KEY_BODY_LEN)
		return -ENOSPC;
	memset(body, 0, HKF_ADD_KEY_BODY_LEN);
	body[0x00] = p->key_index;
	body[0x01] = p->pairwise;
	if (p->pairwise)
		memcpy(body + 0x02, p->mac, 6);
	memcpy(body + 0x08, p->key, p->key_len);
	memcpy(body + 0x28, p->seq, p->seq_len);
	hkf_put32(body + 0x38, p->key_len);
	hkf_put32(body + 0x3c, p->seq_len);
	hkf_put32(body + 0x40, p->cipher);
	return 0;
}

int hkf_build_body_del_key(u8 *body, size_t cap, const struct hkf_key_params *p)
{
	if (!p)
		return -EINVAL;
	if (p->key_index > HKF_MAX_KEY_INDEX || p->pairwise > 1u)
		return -EINVAL;
	if (p->pairwise && !hkf_mac_is_unicast(p->mac))
		return -EINVAL;
	if (!body || cap < HKF_DEL_KEY_BODY_LEN)
		return -ENOSPC;
	memset(body, 0, HKF_DEL_KEY_BODY_LEN);
	body[0x00] = p->key_index;
	body[0x01] = p->pairwise;
	if (p->pairwise)
		memcpy(body + 0x02, p->mac, 6);
	return 0;
}

int hkf_build_body_default_key(u8 *body, size_t cap,
			       const struct hkf_default_key_params *p)
{
	int rc = hkf_default_key_params_validate(p);

	if (rc)
		return rc;
	if (!body || cap < HKF_DEFAULT_KEY_BODY_LEN)
		return -ENOSPC;
	memset(body, 0, HKF_DEFAULT_KEY_BODY_LEN);
	body[0x00] = p->key_index;
	body[0x01] = p->unicast;
	body[0x02] = p->multicast;
	if (p->mgmt)
		body[0x04] = (u8)(p->cipher & 0xffu);
	return 0;
}

/* g0 id1 config-event frame; identical envelope to the bench-executed AP
 * start frames (haf_g0_frame) and to vendor hdpp_config_send_event. */
static int hkf_g0_frame(struct hkf_frame *f, u8 module, u8 dst, u16 cmd,
			const u8 *body, u16 body_len)
{
	size_t total = (size_t)HKF_G0_OVERHEAD + body_len;
	u8 *b;

	if (!f)
		return -EINVAL;
	if (!body || !body_len)
		return -EINVAL;
	if (dst > HKF_MAX_DST || module > HKF_MAX_MODULE)
		return -EINVAL;
	if (total > HKF_MAX_FRAME || total > sizeof(f->buf))
		return -ENOSPC;
	memset(f->buf, 0, sizeof(f->buf));
	b = f->buf;
	/* 12-byte HCC header [V]: group 0, alloc 1, dst nibble, module index,
	 * total, id 1, dst, tag 0x5a5a. */
	b[0] = (u8)(HKF_HCC_GROUP_CHIP & 0x0fu);
	b[1] = (u8)(HKF_ALLOC_NIBBLE | ((dst & 0x0fu) << 4));
	b[2] = 0u;
	b[3] = (u8)(module & 0xffu);
	hkf_put16(b + 4, (u16)total);
	hkf_put16(b + 6, (u16)HKF_HCC_ID_CFG_EVENT);
	b[8] = 0u;
	b[9] = (u8)(dst & 0x0fu);
	hkf_put16(b + 10, HKF_HCC_TAG);
	/* token[8]: inherited g0 id1 token pattern from the bench-executed
	 * context-create frame; the key workers do not read it [V*]. */
	b[16] = 0x01u;
	hkf_put16(b + 20, cmd);
	hkf_put16(b + 22, body_len);
	memcpy(b + 24, body, body_len);
	memset(b + 24 + body_len, 0xff, 4);	/* inherited pad [V] */
	f->len = total;
	f->cmd = cmd;
	return 0;
}

int hkf_build_add_key(struct hkf_frame *f, u8 module, u8 dst,
		      const struct hkf_key_params *p)
{
	u8 body[HKF_ADD_KEY_BODY_LEN];
	int rc;

	rc = hkf_build_body_add_key(body, sizeof(body), p);
	if (rc)
		return rc;
	return hkf_g0_frame(f, module, dst, HKF_CMD_ADD_KEY, body,
			    HKF_ADD_KEY_BODY_LEN);
}

int hkf_build_del_key(struct hkf_frame *f, u8 module, u8 dst,
		      const struct hkf_key_params *p)
{
	u8 body[HKF_DEL_KEY_BODY_LEN];
	int rc;

	rc = hkf_build_body_del_key(body, sizeof(body), p);
	if (rc)
		return rc;
	return hkf_g0_frame(f, module, dst, HKF_CMD_DEL_KEY, body,
			    HKF_DEL_KEY_BODY_LEN);
}

int hkf_build_default_key(struct hkf_frame *f, u8 module, u8 dst,
			  const struct hkf_default_key_params *p)
{
	u8 body[HKF_DEFAULT_KEY_BODY_LEN];
	int rc;

	rc = hkf_build_body_default_key(body, sizeof(body), p);
	if (rc)
		return rc;
	return hkf_g0_frame(f, module, dst, HKF_CMD_SET_DEFAULT_KEY, body,
			    HKF_DEFAULT_KEY_BODY_LEN);
}
