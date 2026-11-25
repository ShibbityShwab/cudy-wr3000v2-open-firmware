/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_vap_index.h - fail-closed VAP/module-index contract for the
 * structural H2D g0-id1 key-0x0064 VAP add (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-band1-start-send-2, shared task
 * task-207, 2026-11-25).  No vendor source, object code, firmware byte,
 * calibration value, MAC or secret is reproduced here.
 *
 * WHY THIS EXISTS
 * ---------------
 * The 5 GHz AP start's last precondition (R3b iteration 2) is the per-band
 * run-state field state[4] ("bound VAP index").  The key-0x0064 VAP-add
 * worker CA 0x449E4 creates/binds the VAP/module context selected by the
 * body data[4] (ctx lookup CA 0x961AE -> 0xFE578) and appends that index to
 * the band object's VAP list (bandobj+0x12 count, entries at bandobj+0).
 * The key-0x0242 band-start worker CA 0x4C524 resolves its ctx from the
 * header field B and calls CA 0x4C3FC -> CA 0x4AE7C, which scans that list
 * (it needs exactly ONE entry) and writes state[4] from the bound VAP before
 * the start.  The bind index (data[4]) and the start ctx (field B) must be
 * the same index or the start early-outs at CA 0x4AF32
 * ("no bound VAP index", state[4] == 0xFF).
 *
 * This model owns that contract: the index bounds (0..0x12), the
 * index == vap_start_module agreement, the plan fields that go into the
 * frame (data[4], data[1] parent, hdr[3] field B), and the read-only
 * readback verdict strings the driver logs after the post.
 *
 * Pure C99: compiles in the kernel module and in the host selftest
 * (src/vap_index_selftest.c).
 */
#ifndef HI5622_VAP_INDEX_H
#define HI5622_VAP_INDEX_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif
#endif

/* Module-table index bound (the worker's 0xBC726 refcount accepts <= 0x12). */
#define HI5622_VAP_INDEX_MAX		0x12u

/* nl80211 iftype values we accept (the provider maps them to the create
 * type: AP -> 2, STA -> 3). */
#define HI5622_VAP_IFTYPE_STA		2u
#define HI5622_VAP_IFTYPE_AP		3u

/* Frame-field offsets in the key-0x0064 g0-id1 body [V] (wifi-vap-start
 * TABLES: data[0] create type, data[1] parent module, data[4] module/VAP
 * index, data[0x12] band selector). */
#define HI5622_VAP_ADD_BODY_LEN		20u
#define HI5622_VAP_ADD_DATA_TYPE	0u
#define HI5622_VAP_ADD_DATA_PARENT	1u
#define HI5622_VAP_ADD_DATA_INDEX	4u
#define HI5622_VAP_ADD_DATA_BAND	0x12u
#define HI5622_VAP_ADD_FIELD_B_OFF	3u	/* hdr[3] = the dispatcher ctx index */

/* One validated VAP-add decision. */
struct hi5622_vap_add_plan {
	bool	post;		/* always true for a validated plan */
	u8	index;		/* -> body data[4] and hdr[3] */
	u8	parent;		/* -> body data[1] (self-parent) */
	u8	field_b;	/* -> hdr[3] (must equal index) */
	u8	band;		/* -> body data[0x12] (0 = 2.4, 1 = 5 GHz) */
	u8	create_type;	/* -> body data[0] (2 AP, 3 STA) */
};

/* The single fail-closed gate the cfg80211 layer applies before any frame:
 * 0 = allowed; -EINVAL for an out-of-table index or an index that differs
 * from the start module (the bind ctx and the start ctx would diverge). */
int hi5622_vap_add_gate(unsigned int index, unsigned int start_module);

/* Validate a full plan for one AP start (returns 0 or -EINVAL). */
int hi5622_vap_add_plan_build(unsigned int index, unsigned int start_module,
			      unsigned int band, unsigned int nl80211_iftype,
			      struct hi5622_vap_add_plan *out);

/* Read-only post-add verdict (the driver logs this after the frame post).
 * ctx_ok = module ctx resolved, bandobj = ctx+0x4B8, count = bandobj+0x12. */
const char *hi5622_vap_add_verdict(bool ctx_ok, u32 bandobj, u32 count);

#endif /* HI5622_VAP_INDEX_H */
