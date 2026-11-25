/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_ax_events.h - portable model of the vendor 802.11ax config-event
 * frames (Hi5622V100, Cudy WR3000 v2.0 clean-room research).
 *
 * Lane: wifi-ax-advanced (shared task task-195).  HOST-ONLY: this is a
 * re-derivation of the protocol bytes the vendor host module builds
 * (hi5622v100_wifi.ko, local fixture) - the firmware side is decoded in
 * ../TABLES.md.  Kernel and host compile the same code; nothing here talks
 * to a device.
 *
 * Envelope (g0 id1): 12-B HCC header + 8-B token + {u16 key; u16 body_len;
 * body; 4x 0xFF}; total = 28 + body_len.  Field B (hdr[3]) is the per-module
 * context selector; it must be the AP-start module index (0 in the fired
 * 2.4 GHz ladder, ../wifi-fw-dfs-worker).
 *
 * Fail-closed contract: hi5622_ax_event() refuses any key/length pair that
 * is not in the decoded set and every builder validates its value range.
 * The AX features are opt-in; an integration lane must keep them behind a
 * default-off parameter and only post them after AP start.
 */
#ifndef HI5622_AX_EVENTS_H
#define HI5622_AX_EVENTS_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#else
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

/* g0-id1 config keys (firmware table A rows, TABLE T2) */
#define HI5622_AX_KEY_HE_GI		0x006F
#define HI5622_AX_KEY_EXT_TWT_IE	0x00E9
#define HI5622_AX_KEY_TWT_ENABLE	0x035D
#define HI5622_AX_KEY_MU_EDCA_BASE	0x005B	/* + selector 0..4 */
#define HI5622_AX_KEY_BFER		0x04C8
#define HI5622_AX_KEY_BFEE		0x0335
#define HI5622_AX_KEY_TRIG_SU_BF_FB	0x032E
#define HI5622_AX_KEY_CYC_SCHE		0x0353
#define HI5622_AX_KEY_SR_ENABLE		0x04B6
#define HI5622_AX_KEY_ALG		0x0128

#define HI5622_AX_MU_EDCA_SEL_MIN	0x5B
#define HI5622_AX_MU_EDCA_SEL_MAX	0x5F

/* body lengths (decoded, TABLE T3) */
#define HI5622_AX_LEN_TWT		1
#define HI5622_AX_LEN_HE_GI		1
#define HI5622_AX_LEN_BF		1
#define HI5622_AX_LEN_EXT_TWT_IE	4
#define HI5622_AX_LEN_MU_EDCA		12
#define HI5622_AX_ALG_MIN		8
#define HI5622_AX_ALG_MAX		128

#define HI5622_AX_ENV_LEN		28
#define HI5622_AX_FRAME_MAX		(HI5622_AX_ENV_LEN + HI5622_AX_ALG_MAX)

struct hi5622_ax_frame {
	u8 buf[HI5622_AX_FRAME_MAX];
	u16 len;
};

/* Build one validated g0-id1 config event.  Returns 0 or -errno. */
int hi5622_ax_event(u8 field_b, u16 key, const u8 *body, u16 body_len,
		    struct hi5622_ax_frame *out);

/* The decoded AX builders (all return 0 or -errno, all fail closed). */
int hi5622_ax_twt_enable(struct hi5622_ax_frame *out, u8 field_b, u8 flags);
int hi5622_ax_ext_twt_ie(struct hi5622_ax_frame *out, u8 field_b, bool on);
int hi5622_ax_mu_edca_param(struct hi5622_ax_frame *out, u8 field_b,
			    u8 selector, u8 ac, u32 value);
int hi5622_ax_he_gi(struct hi5622_ax_frame *out, u8 field_b, u8 gi);
int hi5622_ax_bfee(struct hi5622_ax_frame *out, u8 field_b, u8 mode);
int hi5622_ax_bfer(struct hi5622_ax_frame *out, u8 field_b, u8 mode);
int hi5622_ax_trig_su_bf_fb(struct hi5622_ax_frame *out, u8 field_b, u8 on);
int hi5622_ax_cyc_sche(struct hi5622_ax_frame *out, u8 field_b, u8 on);
int hi5622_ax_sr_enable(struct hi5622_ax_frame *out, u8 field_b, u8 on);

/* ALG text body: {u8 n; u8 off[n]; pad; tokens}; token[0] = module name.
 * n must be 1..127; every token NUL-terminated; module in the decoded set. */
int hi5622_ax_alg(struct hi5622_ax_frame *out, u8 field_b,
		  const char *const *tokens, unsigned int n_tokens);

/* Validate/parse an ALG text body (for host tests and tool cross-checks).
 * On success fills up to max_tokens pointers into the body and returns the
 * token count, or -errno when the body is not decodable. */
int hi5622_ax_alg_parse(const u8 *body, u16 body_len,
			const char **tokens, unsigned int max_tokens);

#endif /* HI5622_AX_EVENTS_H */
