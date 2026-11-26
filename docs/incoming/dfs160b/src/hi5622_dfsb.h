/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_dfsb.h - the 5 GHz 160 MHz width selector + the DFS/CAC/NOL model
 * for the Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux
 * 6.18.60).
 *
 * ORIGINAL clean-room code (lane dfs160b / shared task task-245, 2026-11-26).
 * Pure C99, portable: the same file compiles into the kernel module (via
 * ../hi5622_dfsb_k.c) and into the host selftest (src/dfs160b_selftest.c).
 * No vendor source, object code or firmware byte is reproduced; every
 * constant is a decoded protocol fact of the committed decode lanes
 * ../wifi-5g-160-dfs (T2/T3/T4/T5/T7/T8), ../wifi-5g-dfs-set (0x07E7 frame
 * and worker), ../wifi-5g-dfs-order (the forced order) and
 * ../wifi-5g-cac-context (the +0x49C/+0x4A0 CAC context).
 *
 * WHAT THIS MODULE DECIDES
 *  1. the 0x0124 WIDTH SELECTOR.  The H2D g0-id1 channel-config body is
 *     {band, bandwidth, channel, 0}; byte[1] is the vendor bandwidth selector
 *     (0=20M, 1=40M, 2=80M, 3=160M; the firmware worker CA 0x841B8 stores it
 *     at module ctx+0x12, and the 5 GHz RF branch CA 0x40684 feeds it to the
 *     descriptor selector CA 0x101890).  This model maps a cfg80211 chandef
 *     width onto it and validates the single-segment 160 MHz centre (CCFS1).
 *     IMPORTANT: the nl80211_chan_width values are the Linux 6.18 UAPI ones
 *     (80P80=4, 160=5, 320=13) - the pre-5.19 numbering (160=4, 80P80=5) is
 *     WRONG for this target; hi5622_dfsb_k.c pins that with _Static_assert.
 *  2. the DFS CHANNEL SET: the vendor band-1 table's 29 channels, the DFS
 *     subset 52-64 / 100-144 (including the ch144 the driver list missed) and
 *     the 5600..5650 MHz TDWR window that carries the 600000 ms CAC time.
 *  3. the CAC/NOL TIMING state machine: 60000 ms default / 600000 ms TDWR
 *     CAC, the vendor tick arithmetic (first timer 63000 ms, then 60000 ms
 *     per tick, ticks = cac_ms/60000 floor) and the 1800000 ms (30 min)
 *     non-occupancy period (../wifi-5g-160-dfs TABLES T4/T5).
 *  4. the 0x07E7 DFS-SET wire frame (ARM {01} / CLEAR {00}, 29-B carrier)
 *     and the ladder ORDER contract 0x0124 -> ... -> 0x0065 -> 0x07E7 ->
 *     0x01F7 -> 0x0242.
 */
#ifndef HI5622_DFSB_H
#define HI5622_DFSB_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/stddef.h>
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

/* =====================================================================
 * 1. The 0x0124 body[1] width selector
 * ===================================================================== */

/* enum nl80211_chan_width, Linux 6.18.60 UAPI (include/uapi/linux/nl80211.h).
 * These are NOT the pre-5.19 values: 80P80 is 4 and 160 is 5. */
#define HI5622_DFSB_WIDTH_20NOHT	0u
#define HI5622_DFSB_WIDTH_20		1u
#define HI5622_DFSB_WIDTH_40		2u
#define HI5622_DFSB_WIDTH_80		3u
#define HI5622_DFSB_WIDTH_80P80		4u
#define HI5622_DFSB_WIDTH_160		5u
#define HI5622_DFSB_WIDTH_5		6u
#define HI5622_DFSB_WIDTH_10		7u
#define HI5622_DFSB_WIDTH_1		8u
#define HI5622_DFSB_WIDTH_2		9u
#define HI5622_DFSB_WIDTH_4		10u
#define HI5622_DFSB_WIDTH_8		11u
#define HI5622_DFSB_WIDTH_16		12u
#define HI5622_DFSB_WIDTH_320		13u

/* The vendor bandwidth selector (0x0124 body[1]) [V 0/2 from the vendor
 * width->value table 20/20/40/80/80/160; I for the 40/80/160 assemble
 * order, to be pinned by probe P-160a]. */
#define HI5622_DFSB_BW_20		0u
#define HI5622_DFSB_BW_40		1u
#define HI5622_DFSB_BW_80		2u
#define HI5622_DFSB_BW_160		3u
#define HI5622_DFSB_BW_MAX		3u

/* ht_bw / dfs_cac module-parameter contract (owned by hi5622_dfsb_k.c). */
#define HI5622_DFSB_PARM_OFF		0u
#define HI5622_DFSB_PARM_ON		1u
#define HI5622_DFSB_PARM_MAX		1u

struct hi5622_dfsb_width_plan {
	bool	enabled;	/* ht_bw=1: the selector drives body[1] */
	u8	sel;		/* the 0x0124 body[1] value when enabled */
	u8	width;		/* the nl80211_chan_width value */
};

/* Plan the 0x0124 body[1] byte.
 *  ht_bw    : the module parameter (0/1; >1 -> -EINVAL)
 *  band     : 0 = 2.4 GHz, 1 = 5 GHz
 *  channel  : the control channel (hw_value)
 *  width    : nl80211_chan_width
 *  ccfs1/2  : the chandef centre frequencies (MHz)
 *  0 on success; -EINVAL/-EOPNOTSUPP on a bad parameter or an unselectable
 *  width (80+80 and 320 MHz are refused: the vendor VHT cap 0x00400064 has
 *  neither the 80+80 nor the 320 MHz bit).  ht_bw=0 always yields sel=0 -
 *  the pre-feature wire byte. */
int hi5622_dfsb_width_plan(unsigned int ht_bw, u8 band, u8 channel, u32 width,
			   u32 ccfs1, u32 ccfs2,
			   struct hi5622_dfsb_width_plan *out);

/* The single-segment 160 MHz CCFS1 of a control channel:
 * 36-64 -> 50, 100-128 -> 114, 149-177 -> 163 (IEEE channelisation).
 * 0 on success; -EOPNOTSUPP when the channel is not in a 160 MHz segment. */
int hi5622_dfsb_ccfs1_160(u8 channel, u32 *ccfs1_mhz);

/* The 80 MHz CCFS1: 36-48->42, 52-64->58, 100-112->106, 116-128->122,
 * 132-144->138, 149-161->155, 165-177->171. */
int hi5622_dfsb_ccfs1_80(u8 channel, u32 *ccfs1_mhz);

/* =====================================================================
 * 2. The DFS channel set (the ch144 gap, the TDWR window)
 * ===================================================================== */

/* The vendor band-1 table (29 channels): 36 40 44 48 52 56 60 64 100 104
 * 108 112 116 120 124 128 132 136 140 144 149 153 157 161 165 184 188 192
 * 196 (../wifi-5g-160-dfs TABLES T3).  0 on success / -EINVAL. */
int hi5622_dfsb_chan_freq(u8 channel, u16 *mhz);

/* 1 when the channel is a DFS channel (52-64 or 100-144) [V, TABLES T4]. */
int hi5622_dfsb_is_dfs_chan(u8 channel);

/* 1 when the channel frequency is in the 5600..5650 MHz TDWR/weather window
 * (vendor: freq - 5600 <= 50) - the 600000 ms CAC channel class. */
int hi5622_dfsb_is_tdwr_chan(u8 channel);

/* =====================================================================
 * 3. The CAC / NOL state machine (host side, vendor hmac_dfs_cac_*)
 * ===================================================================== */

#define HI5622_DFSB_CAC_MS		60000u
#define HI5622_DFSB_CAC_MS_TDWR		600000u
#define HI5622_DFSB_NOL_MS		1800000u
#define HI5622_DFSB_TICK_MS		60000u
#define HI5622_DFSB_FIRST_TICK_MS	63000u

enum hi5622_dfsb_cac_state {
	HI5622_DFSB_CAC_IDLE = 0,
	HI5622_DFSB_CAC_ARMED = 1,
	HI5622_DFSB_CAC_FINISHED = 2,
};

enum hi5622_dfsb_cac_step {
	HI5622_DFSB_STEP_REARM = 0,
	HI5622_DFSB_STEP_FINISH = 1,
};

struct hi5622_dfsb_cac {
	u8	state;		/* enum hi5622_dfsb_cac_state */
	u8	channel;
	u8	tdwr;
	u8	pad;
	u16	center_freq;	/* MHz */
	u32	cac_ms;		/* the effective CAC time */
	u32	ticks;		/* timer ticks (floor(cac_ms/60000)) */
	u32	tick_ms;	/* 60000 */
	u32	first_ms;	/* 63000 when cac_ms > 59999, else 60000 */
	u32	ticks_done;
	u32	finish_ms;	/* now_ms at FINISH (wrap-safe difference) */
	u32	nol_ms;		/* 1800000 */
};

/* Arm the state machine for a DFS channel.
 *  - -EOPNOTSUPP when the channel is not a DFS channel;
 *  - -EALREADY when a CAC is already running;
 *  - 0 on success: ARMED, tdwr from the frequency window, cac_ms =
 *    max(the vendor class time, cac_time_ms) so a report can never be
 *    earlier than the cfg80211-provided CAC time.
 * The caller keeps the 0x07E7 ARM/CLEAR frames and the cfg80211 events. */
int hi5622_dfsb_cac_begin(struct hi5622_dfsb_cac *st, u8 channel,
			  u16 center_freq, u32 cac_time_ms);

/* The delay from now to the next tick (the first 63000/60000 ms, then
 * 60000 ms).  0 when no CAC is armed. */
u32 hi5622_dfsb_cac_delay_ms(const struct hi5622_dfsb_cac *st);

/* One vendor timer tick at now_ms: REARM or FINISH (which sets FINISHED and
 * records finish_ms).  0 on success; -EPERM when no CAC is armed. */
int hi5622_dfsb_cac_tick(struct hi5622_dfsb_cac *st, u32 now_ms,
			 enum hi5622_dfsb_cac_step *out);

/* Abort/cancel: IDLE with the channel cleared. */
void hi5622_dfsb_cac_abort(struct hi5622_dfsb_cac *st);

/* The start_ap DFS gate with the feature ON: 0 only when a CAC FINISHED on
 * exactly this channel and the 1800000 ms NOL window since finish_ms has not
 * elapsed; -EAGAIN otherwise (-EINVAL on a bad argument). */
int hi5622_dfsb_cac_ap_gate(const struct hi5622_dfsb_cac *st, u8 channel,
			    u32 now_ms);

/* =====================================================================
 * 4. The 0x07E7 DFS-SET frame and the ladder order contract
 * ===================================================================== */

#define HI5622_DFSB_SET_WIRE_LEN	29u
#define HI5622_DFSB_SET_HDR_LEN		12u
#define HI5622_DFSB_SET_KEY_OFF		20u
#define HI5622_DFSB_SET_LEN_OFF		22u
#define HI5622_DFSB_SET_BODY_OFF		24u
#define HI5622_DFSB_SET_TAIL_OFF		25u
#define HI5622_DFSB_SET_TAG		0x5a5au
#define HI5622_DFSB_SET_ID		1u
#define HI5622_DFSB_SET_KEY		0x07e7u
#define HI5622_DFSB_SET_BODY_LEN		1u
#define HI5622_DFSB_SET_BODY_ARM		0x01u
#define HI5622_DFSB_SET_BODY_CLEAR	0x00u
#define HI5622_DFSB_SET_ARM_PATTERN	0x2du	/* the six sub-base bit0s {1,0,1,1,0,1} */

/* Build the g0-id1 key 0x07E7 wire frame (the committed ../wifi-5g-dfs-set
 * carrier geometry; ARM = body 0x01, CLEAR = body 0x00).  Byte-exact KAT:
 * ARM = 00 01 00 00 1d 00 01 00 00 00 5a 5a | 00 00 00 00 01 00 00 00 |
 *       e7 07 01 00 01 ff ff ff ff. */
int hi5622_dfsb_set_build(u8 *wire, size_t cap, size_t *out_len, u8 body);

/* The ladder steps this feature constrains. */
enum hi5622_dfsb_op {
	HI5622_DFSB_OP_VAP_ADD = 0,
	HI5622_DFSB_OP_DESC_TABLE,
	HI5622_DFSB_OP_AP_CHAN,
	HI5622_DFSB_OP_AP_SSID,
	HI5622_DFSB_OP_AP_CFG,
	HI5622_DFSB_OP_VAP_START,
	HI5622_DFSB_OP_DFS_SET,
	HI5622_DFSB_OP_DFS_MODE,
	HI5622_DFSB_OP_BAND_START,
	HI5622_DFSB_OP_MAX,
};

/* 1 when op a must be posted strictly before op b in the AP-start ladder
 * (task-228 order: ..., ap_chan, ..., vap_start(0x0065), dfs_set(0x07E7),
 * dfs_mode(0x01F7), band_start(0x0242)); -EINVAL on a bad op. */
int hi5622_dfsb_order_before(enum hi5622_dfsb_op a, enum hi5622_dfsb_op b);

/* =====================================================================
 * 5. The start_ap feature plan (the one decision point both features share)
 * ===================================================================== */

struct hi5622_dfsb_ap_plan {
	bool	width_enabled;
	u8	width_sel;
	bool	cac_required;	/* the chandef is a DFS channel and dfs_cac=1 */
	bool	cac_gate;	/* the CAC gate passed for this channel */
};

/* Combine the two module parameters with the live state for one start_ap.
 *  - -EINVAL for ht_bw/dfs_cac > 1, or dfs_cac=1 together with the ladder
 *    dfs_set step on (one meaning per load: the CAC runtime owns 0x07E7);
 *  - the width errors of hi5622_dfsb_width_plan();
 *  - -EAGAIN from the CAC gate when dfs_cac=1 and the DFS channel has no
 *    valid finished CAC (the AP-start DFS gate).
 * now_ms: the wrap-safe millisecond clock the caller uses for the CAC
 * machine (0 when the caller has no clock). */
int hi5622_dfsb_ap_plan(unsigned int ht_bw, unsigned int dfs_cac,
			unsigned int dfs_set_on, u8 band, u8 channel,
			u32 width, u32 ccfs1, u32 ccfs2,
			const struct hi5622_dfsb_cac *cac, u32 now_ms,
			struct hi5622_dfsb_ap_plan *out);

#endif /* HI5622_DFSB_H */
