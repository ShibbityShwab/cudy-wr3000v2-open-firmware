/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_dfs160.h - 5 GHz 160 MHz width selector + DFS/CAC/NOL model for the
 * Hi5622V100 clean-room full-MAC driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane dfs160 / shared task task-240, 2026-11-26).
 * No vendor source, object code or firmware byte is reproduced; every
 * constant is a decoded protocol fact from the committed decode lanes
 * ../wifi-5g-160-dfs/ (TABLES T2/T4/T5/T7/T8), ../wifi-5g-dfs-set/
 * (0x07E7 frame/worker) and ../wifi-5g-cac-context/ (+0x4A0 writer), with
 * the grade markers kept in that lane's TABLES.md.
 *
 * WHAT THIS MODULE DECIDES (pure C99, host-testable):
 *   1. the 160 MHz WIDTH SELECTOR: the H2D g0-id1 key 0x0124 channel-config
 *      body is {band, bandwidth, channel, 0}; byte[1] is the vendor bandwidth
 *      selector (0=20M, 1=40M, 2=80M, 3=160M, the value the firmware worker
 *      CA 0x841B8 stores at module ctx+0x12 and the band-1 RF branch
 *      CA 0x40684 feeds to CA 0x101890's descriptor selector).  The
 *      cfg80211 chandef width maps onto that selector; 80+80 and 320 MHz are
 *      refused because the vendor wiphy VHT cap 0x00400064 has neither bit.
 *      The 160 MHz centre (CCFS1) is checked against the IEEE channelisation
 *      (36-64 -> 50, 100-128 -> 114, 149-177 -> 163).
 *   2. the DFS CHANNEL SET: 52-64 and 100-144 (the vendor band-1 table
 *      CA 0x117AD4's DFS channels, including the ch144 entry our channel
 *      list is missing - "the ch144 gap") and the 5600..5650 MHz TDWR
 *      window (ch 100-128) that carries the 600000 ms CAC time.
 *   3. the CAC/NOL TIMING state machine (the HOST-side hmac_dfs_* machinery):
 *      CAC 60000 ms default / 600000 ms TDWR, the vendor tick arithmetic
 *      (first timer 63000 ms, then 60000 ms per tick, ticks = cac_ms/60000)
 *      and the 1800000 ms (30 min) non-occupancy period.  No vendor bytes
 *      are decoded here; the timing constants are the committed TABLES T4
 *      values, and the 0x07E7 ARM/CLEAR frame itself is built by the
 *      sibling hi5622_dfs_set.c (lane wifi-5g-dfs-set).
 *
 * The kernel-side wire state machine and the cfg80211 radar-detect ops live
 * in hi5622_dfs_cac.c (src/); this header is portable so the host selftest
 * (src/dfs160_selftest.c) exercises every decision without a device.
 */
#ifndef HI5622_DFS160_H
#define HI5622_DFS160_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/stddef.h>
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

/* ===================================================================
 * 1. The 160 MHz width selector (0x0124 body[1])
 * =================================================================== */

/* nl80211_chan_width UAPI values (linux/nl80211.h, stable ABI). */
#define HI5622_DFS160_WIDTH_20NOHT	0u
#define HI5622_DFS160_WIDTH_20		1u
#define HI5622_DFS160_WIDTH_40		2u
#define HI5622_DFS160_WIDTH_80		3u
#define HI5622_DFS160_WIDTH_160		4u
#define HI5622_DFS160_WIDTH_80P80	5u
#define HI5622_DFS160_WIDTH_320		6u

/* The vendor bandwidth selector written to 0x0124 body[1] [V for 0; I 1..3
 * from the HAL channel-assemble enum order; PROBES P-160a decides 3]. */
#define HI5622_DFS160_BW_20		0u
#define HI5622_DFS160_BW_40		1u
#define HI5622_DFS160_BW_80		2u
#define HI5622_DFS160_BW_160		3u
#define HI5622_DFS160_BW_MAX		3u
/* Sentinel for the legacy path: body[1] keeps the old vap_id byte (the
 * bench-validated F2/first fire wire bytes are byte-identical then). */
#define HI5622_DFS160_BW_LEGACY		0xffu

/* Map a cfg80211 chandef width onto the vendor selector.
 *  0 on success (sel in 0..3);
 * -EOPNOTSUPP for 80+80 and 320 MHz (the vendor cap has neither bit);
 * -EINVAL     for NULL/unknown widths. */
int hi5622_dfs160_width_selector(u32 width, u8 *sel);

/* 160 MHz single-segment CCFS1 (IEEE channelisation [I, IEEE]).
 * 36-64 -> 50 (5250), 100-128 -> 114 (5570), 149-177 -> 163 (5815).
 * 0 on success; -EOPNOTSUPP when the channel is not in a 160 MHz segment. */
int hi5622_dfs160_ccfs1_160(u8 channel, u32 *ccfs1);

/* 80 MHz CCFS1 for the fail-closed 80 MHz centre check: 36-48->42,
 * 52-64->58, 100-112->106, 116-128->122, 132-144->138, 149-161->155,
 * 165-177->171.  0 on success; -EOPNOTSUPP outside the 80 MHz segments. */
int hi5622_dfs160_ccfs1_80(u8 channel, u32 *ccfs1);

/* Full fail-closed start_ap chandef check for the ht_bw=1 path.
 * Validates the selector width, the centre-frequency arguments and, on
 * 160 MHz, that ccfs1 is the segment centre and ccfs2 == 0.  Returns 0/-errno.
 * (cfg80211 validates the chandef first; this is the driver-side belt.) */
int hi5622_dfs160_width_check(u32 width, u8 channel, u32 ccfs1, u32 ccfs2);

/* ===================================================================
 * 2. The DFS channel set (the ch144 gap)
 * =================================================================== */

/* IEEE channel number -> centre frequency in MHz (only the 29 vendor
 * band-1 channels are known; 0 on success, -EINVAL otherwise). */
int hi5622_dfs160_chan_freq(u8 channel, u16 *mhz);

/* 1 = the vendor band-1 table channel is a DFS channel (52-64, 100-144). */
int hi5622_dfs160_is_dfs_chan(u8 channel);

/* 1 = the channel is in the 5600..5650 MHz TDWR window (ch 100-128) - the
 * 600000 ms CAC time channel class (the vendor adds the 600 s CAC for
 * frequency - 5600 <= 50). */
int hi5622_dfs160_is_tdwr_chan(u8 channel);

/* 1 = centre_freq is inside 5600..5650 MHz (the same TDWR rule on MHz). */
int hi5622_dfs160_is_tdwr_center(u16 center_freq);

/* ===================================================================
 * 3. CAC / NOL timing and the host state machine
 * =================================================================== */
#define HI5622_DFS160_CAC_MS_DEFAULT	60000u		/* UNII-2/2e */
#define HI5622_DFS160_CAC_MS_TDWR	600000u		/* 5600..5650 MHz */
#define HI5622_DFS160_CAC_FIRST_MS	63000u		/* vendor first timer */
#define HI5622_DFS160_CAC_TICK_MS	60000u		/* vendor re-arm tick */
#define HI5622_DFS160_NOL_MS		1800000u	/* 30 min non-occupancy */
#define HI5622_DFS160_CAC_MIN_MS	60000u

/* One CAC plan (derived from the channel, the centre frequency and the
 * cfg80211-provided cac_time_ms; the driver floor is the vendor value). */
struct hi5622_dfs160_cac_plan {
	u8	channel;
	u8	tdwr;		/* 1 = the 560000..5650 window */
	u32	cac_ms;		/* 60000 or 600000 (or a longer request) */
	u32	first_ms;	/* 63000 */
	u32	tick_ms;	/* 60000 */
	u32	ticks;		/* cac_ms / 60000 (>= 1) */
	u32	nol_ms;		/* 1800000 */
};

/* Fill *out for a DFS channel.  The driver floor wins: cac_ms >= 60000 and,
 * in the TDWR window, >= 600000; a cfg80211 cac_time_ms above the floor is
 * honoured (hostapd's own value).  Returns 0 or -EINVAL/-EOPNOTSUPP. */
int hi5622_dfs160_cac_plan(u8 channel, u16 center_freq, u32 requested_ms,
			   struct hi5622_dfs160_cac_plan *out);

enum hi5622_dfs160_cac_state {
	HI5622_DFS160_CAC_OFF = 0,	/* no CAC attempted / idle */
	HI5622_DFS160_CAC_ARMED = 1,	/* 0x07E7{01} posted, timer running */
	HI5622_DFS160_CAC_FINISHED = 2,	/* CAC complete, 0x07E7{00} posted */
	HI5622_DFS160_CAC_ABORTED = 3,	/* end_cac / teardown */
};

struct hi5622_dfs160_cac {
	u8	state;
	u8	channel;
	u8	tdwr;
	u8	radar_type;	/* last radar type 0..8, 0xff = none */
	u16	center_freq;
	u32	cac_ms;
	u32	first_ms;
	u32	tick_ms;
	u32	ticks;
	u32	ticks_done;
	u32	nol_ms;
	u32	nol_end_ms;	/* 0 = no NOL */
	u32	radar_count;
	struct hi5622_dfs160_cac_plan plan;
};

void hi5622_dfs160_cac_init(struct hi5622_dfs160_cac *st);

/* Begin CAC on a DFS channel (the state must be OFF/FINISHED/ABORTED).
 *  0 ok; -EALREADY when already ARMED; -EINVAL bad args/not a DFS channel. */
int hi5622_dfs160_cac_begin(struct hi5622_dfs160_cac *st, u8 channel,
			    u16 center_freq, u32 requested_ms);

/* The delay before the next timer callback: first_ms on the first tick,
 * tick_ms afterwards.  0 when not ARMED. */
u32 hi5622_dfs160_cac_delay_ms(const struct hi5622_dfs160_cac *st);

enum hi5622_dfs160_cac_step {
	HI5622_DFS160_STEP_REARM = 0,	/* timer must be re-armed */
	HI5622_DFS160_STEP_FINISH = 1,	/* CAC complete: post 0x07E7{00} */
	HI5622_DFS160_STEP_IDLE = 2,	/* not armed */
};

/* Advance one timer tick.  FINISH when ticks_done reached ticks. */
enum hi5622_dfs160_cac_step hi5622_dfs160_cac_tick(struct hi5622_dfs160_cac *st);

/* Terminal transitions (idempotent): FINISHED / ABORTED. */
void hi5622_dfs160_cac_finish(struct hi5622_dfs160_cac *st);
void hi5622_dfs160_cac_abort(struct hi5622_dfs160_cac *st);

/* Radar detected: abort a running CAC (a radar hit during CAC invalidates
 * it) and start the 30 min non-occupancy period.  type is the decoded
 * radar type 0..8 (the vendor's per-type counters).  now_ms is the caller's
 * monotonic clock (only used for the NOL end).  0/-EINVAL. */
int hi5622_dfs160_cac_radar(struct hi5622_dfs160_cac *st, u8 type, u32 now_ms);

/* 1 = the NOL for this channel is still running at now_ms. */
int hi5622_dfs160_nol_active(const struct hi5622_dfs160_cac *st, u32 now_ms);

/* start_ap gate: the CAC on (channel, center_freq) is finished and no NOL is
 * running => 0; -EAGAIN otherwise (the AP must not beacon on an unchained
 * DFS channel); -EINVAL bad args. */
int hi5622_dfs160_cac_ap_gate(const struct hi5622_dfs160_cac *st, u8 channel,
			      u16 center_freq, u32 now_ms);

const char *hi5622_dfs160_cac_state_name(u8 state);

/* One read-only sysfs status line (the driver appends it to cfg80211_status):
 *   dfs_cac: param=N (...) bw=N state=... channel=... cac_ms=... nol_ms=...
 *            ticks=... radar=... arms=P finishes=F aborts=A refusals=R last_rc=R
 * Returns the length, clamped like the sibling status lines. */
int hi5622_dfs160_status_line(unsigned int ht_bw, unsigned int dfs_cac,
			      const struct hi5622_dfs160_cac *st,
			      int last_rc, unsigned int arms,
			      unsigned int finishes, unsigned int aborts,
			      unsigned int refusals, char *buf, size_t len);

#endif /* HI5622_DFS160_H */
