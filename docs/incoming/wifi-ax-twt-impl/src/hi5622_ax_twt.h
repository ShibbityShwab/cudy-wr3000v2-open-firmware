/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_ax_twt.h - TWT (target wake time) enable config event for the
 * Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-ax-twt-impl, shared task task-199,
 * 2026-11-28).  The protocol facts are the committed decode of the sibling
 * lane wifi-ax-advanced (../wifi-ax-advanced/TABLES.md T2/T3/T5 and
 * FIRE-RECIPE.md R2); no vendor source, object code, firmware byte,
 * calibration value, MAC or secret is reproduced here.  The only bytes this
 * module synthesizes are *our own host-built protocol bytes*.
 *
 * WHY THIS EXISTS
 * ---------------
 * TWT is the one 802.11ax feature the vendor host can enable end-to-end
 * without an associated client.  The vendor user path
 * `wal_hipriv_twt_enable` accepts exactly ONE integer and only the values
 * 1 and 3 (1 = TWT enable, 3 = enable + required).  It posts a g0-id1
 * config event with key 0x035D and a ONE-byte body; the firmware worker
 * (CA 0x41E5A) maps body bit0 -> ctx+0xDA.bit6 (TWT enable), bit1 ->
 * ctx+0xDA.bit7 (TWT required).  The per-user TWT agreement is a separate
 * firmware->host event and needs an associated STA (the RX gate), so this
 * module implements the *enable* half only and says so.
 *
 * Envelope [V] (same g0-id1 carrier as the sibling lanes):
 *   12-B HCC header (group 0, id 1) + 8-B token + {u16 key; u16 len; body;
 *   4x 0xFF} = 28 + body_len bytes = 29 bytes for the TWT body.
 *   hdr[3] is the message's field B: the per-module context selector.  The
 *   F2 lesson (../wifi-fw-dfs-worker/patches/0001-fieldb-context.patch) is
 *   that a hard-coded vendor-wire field B silently targets the WRONG
 *   module context; this module therefore takes field B from the AP start's
 *   resolved module context (hi5622_ax_twt_field_b()), never from a magic
 *   constant at the builder level.
 *
 * Fail-closed contract:
 *   twt=0 (default)  no frame at all - byte-identical to the current module;
 *   twt=1            post TWT enable on the AP-start completion path;
 *   twt=3            post TWT enable+required;
 *   any other value  refused (-EINVAL) before any frame.
 *   The post additionally requires the beacon_static vendor AP-start order,
 *   the HE (802.11ax) wiphy advertisement (he_caps=1) and the 2.4 GHz band:
 *   the 2.4 GHz HE AP is the only bench-proven AP shape and a TWT-enable
 *   without the HE advertisement cannot lead to a TWT agreement.  Every
 *   precondition is host-tested (src/twt_selftest.c).
 *
 * Pure C99: the same code compiles in the kernel module and in the host
 * selftest; tools/run-host-tests.sh builds both.
 */
#ifndef HI5622_AX_TWT_H
#define HI5622_AX_TWT_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif
#endif

/* ---- fail-closed module parameter contract ---------------------------- */
/* Accepted non-zero values (the vendor user-path validator's set {1, 3}).
 * 2 is NOT a combination of the two decoded bits and is refused; anything
 * above HI5622_AX_TWT_PARM_MAX is refused before any frame. */
#define HI5622_AX_TWT_OFF			0u
#define HI5622_AX_TWT_ENABLE			1u
#define HI5622_AX_TWT_ENABLE_REQUIRED		3u
#define HI5622_AX_TWT_PARM_MAX			3u

/* ---- frame geometry [V] (wifi-ax-advanced TABLES T2/T3/T5/T12) -------- */
#define HI5622_AX_TWT_HDR_LEN			12u	/* 12-B HCC header */
#define HI5622_AX_TWT_TOKEN_LEN			8u	/* 00..00 01..00 token */
#define HI5622_AX_TWT_KEY			0x035du	/* config key at hdr+20 */
#define HI5622_AX_TWT_BODY_LEN			1u	/* body[0] = flags */
#define HI5622_AX_TWT_TAG			0x5a5au	/* header tag at hdr+10 */
#define HI5622_AX_TWT_MAX_FIELD_B		18u	/* 19 boot contexts 0..18 */
/* 12 hdr + 8 token + 2 key + 2 len + 1 body + 4 inherited tail. */
#define HI5622_AX_TWT_WIRE_LEN			29u

/* ---- firmware read-back contract [V] (TABLES T3/T11) ------------------ */
/* ctx = the AP-start module context (per-module context CA; field B); the
 * worker writes ctx+0xDA bits 6/7.  The 19 boot contexts start at ctx0
 * CA 0x00119758 with stride 0xB40, so the field B index is derived from the
 * resolved AP-start context CA instead of being hard-coded (F2 lesson). */
#define HI5622_AX_TWT_CTX0_CA			0x00119758u
#define HI5622_AX_TWT_CTX_STRIDE		0x0b40u
#define HI5622_AX_TWT_CTX_COUNT			19u
#define HI5622_AX_TWT_DA_OFF			0xdau	/* ctx+0xDA */
#define HI5622_AX_TWT_DA_ENABLE			0x40u	/* bit6 TWT enable */
#define HI5622_AX_TWT_DA_REQUIRED		0x80u	/* bit7 TWT required */
#define HI5622_AX_TWT_DA_MASK			0xc0u	/* bits 6/7 only */

/* ---- fail-closed AP-start decision ------------------------------------ */
/* What one AP start must do with the 0x035D frame.  field_b is filled by
 * hi5622_ax_twt_field_b() at post time; the precheck leaves it 0. */
struct hi5622_ax_twt_plan {
	bool	post;		/* true = build + post the frame */
	u8	flags;		/* 1 (enable) or 3 (enable+required) */
	u8	field_b;	/* AP-start module context selector */
	u8	da_mask;	/* ctx+0xDA bits the worker writes */
	u8	da_expected;	/* expected value of those bits */
};

/* Validate the parameter and the AP-start context, fail closed.
 *  - 0 on success.  post = true only for twt in {1,3} when the vendor-order
 *    AP start is enabled (beacon_static == 1), the VAP start is part of the
 *    ladder (vap_start == 1), the HE advertisement is on (he_caps == 1) and
 *    the AP is the 2.4 GHz band (band == 0);
 *  - -EINVAL for NULL out, twt == 2 or twt > HI5622_AX_TWT_PARM_MAX, or an
 *    explicit TWT request missing one of the AP-start preconditions;
 *  - -EOPNOTSUPP for a non-2.4 GHz AP (the 5 GHz HE AP is not bench-proven);
 *  - twt == 0 is a clean no-post for any other argument combination. */
int hi5622_ax_twt_precheck(unsigned int twt, unsigned int beacon_static,
			   unsigned int vap_start, unsigned int he_caps,
			   unsigned int band, struct hi5622_ax_twt_plan *out);

/* Derive field B from the AP-start module context CA (the live read-back of
 * the module table slot 0).  Returns 0 and the 0..18 index, -EAGAIN when the
 * firmware has not bound a context yet (CA 0), -ERANGE when the CA is not one
 * of the 19 decoded boot contexts (never guess a module selector). */
int hi5622_ax_twt_field_b(u32 mctx_ca, u8 *out_field_b);

/* Build the complete 29-byte g0-id1 wire frame into wire/cap.
 * Refuses (-EINVAL) NULL/bad arguments, a plan that is not armed, a flags
 * value other than 1/3, a field B above HI5622_AX_TWT_MAX_FIELD_B, an
 * inconsistent expectation mask; -ENOSPC when cap < the wire length.
 * The caller posts the result through hi5622_frame_post() (the only wire
 * path); this module never touches the device. */
int hi5622_ax_twt_build(u8 *wire, size_t cap, size_t *out_len,
			const struct hi5622_ax_twt_plan *plan);

/* Human-readable mode name: "off"/"enable"/"enable+required"/"invalid". */
const char *hi5622_ax_twt_mode_name(unsigned int twt);

/* One read-only sysfs status line (appended to cfg80211_status):
 *   twt: param=N (mode) he_caps=M beacon_static=B last_rc=R posted=P refused=F
 * last_rc is -ENODATA until the first attempt.  Returns the length, clamped
 * like the sibling lanes' status lines. */
int hi5622_ax_twt_status_line(unsigned int twt, unsigned int he_caps,
			      unsigned int beacon_static, int last_rc,
			      unsigned int posted, unsigned int refused,
			      char *buf, size_t len);

#endif /* HI5622_AX_TWT_H */
