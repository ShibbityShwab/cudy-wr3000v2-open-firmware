/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_pm_perf_logic.h - portable, device-free logic for the power-
 * management + performance tuning lane (task-160) of the Hi5622V100
 * clean-room host (Cudy WR3000 v2.0, luofu v2).
 *
 * ORIGINAL clean-room code.  This header contains ONLY pure functions: no
 * kernel headers, no device access, no libc.  It is shared verbatim by the
 * kernel module (hcc.c / service.c / hi5622_data_tx.c / main.c) and by the
 * host selftest (tools/pm_perf_selftest.c), so the policies that gate power
 * and polling are testable on the host before any attended fire.
 *
 * Evidence grades follow the project convention: [V] decoded from a named
 * vendor artifact, [V*] measured on the bench (cited in REPORT.md), [I]
 * inference.  Every constant names its origin.
 */
#ifndef HI5622_PM_PERF_LOGIC_H
#define HI5622_PM_PERF_LOGIC_H

#ifdef __KERNEL__
#include <linux/types.h>
typedef u8  hspp_u8;
typedef u16 hspp_u16;
typedef u32 hspp_u32;
typedef u64 hspp_u64;
#else
#include <stdint.h>
typedef uint8_t  hspp_u8;
typedef uint16_t hspp_u16;
typedef uint32_t hspp_u32;
typedef uint64_t hspp_u64;
#endif

/* =====================================================================
 * 1. PM gate - the safe subset of the vendor PM sleep path
 *
 * RE facts (REPORT.md sec 2, all [V]/[I] graded there):
 *   - the vendor platform PM sleep is: H2D g0 id1 (bal_msg_send(0,1)),
 *     a <= 50 jiffies (500 ms) wait for st_slp_ack_event, then BAL close +
 *     a GPIO level handshake, and on wake bal_reinit + another <= 50 jiffies
 *     wait (D2H id4 disallow / id5 wake-ok callbacks complete those events);
 *   - the gate that arms this path is g_wlan_pm_info+0x124, written only by
 *     plat_pm_vote() - which no loaded module imports on this build, and the
 *     only hipriv pm_switch command lives in wifi_debug.ko, loaded only when
 *     /etc/hi_version contains "turbo" (it does not on stock 2.4.15/2.5.24);
 *   - so the vendor sleep path is DEAD on this board; the vendor's active
 *     runtime difference is per-VAP chip power gating and cfg80211 STA
 *     power-save config (both need firmware cooperation and are NOT part of
 *     the safe subset).
 *
 * Our clean-room stack therefore keeps PM disabled by default.  This gate is
 * the single decision point for a future attended PM rung: it mirrors the
 * hcc_core_pm_request_sleep() preconditions exactly so a host test can prove
 * the refusals before the attribute is ever armed.
 * ===================================================================== */
#define HSPP_PM_OK		0	/* request may be posted			*/
#define HSPP_PM_REFUSE_DISABLED	1	/* pm_enable=0 (the default)		*/
#define HSPP_PM_REFUSE_NOT_READY 2	/* no boot latch or phase != ACTIVE	*/
#define HSPP_PM_REFUSE_PENDING	3	/* a previous sleep ack is outstanding	*/

struct hspp_pm_input {
	hspp_u8 enabled;		/* config pm_enabled (module param)	*/
	hspp_u8 boot_latched;		/* handshake page +8 == 2 latched	*/
	hspp_u8 active_phase;		/* HCC_PHASE_ACTIVE reached this load	*/
	hspp_u8 pending;		/* pm_sleep_pending still true		*/
};

static inline hspp_u32 hspp_pm_gate(const struct hspp_pm_input *in)
{
	if (!in || !in->enabled)
		return HSPP_PM_REFUSE_DISABLED;
	if (!in->boot_latched || !in->active_phase)
		return HSPP_PM_REFUSE_NOT_READY;
	if (in->pending)
		return HSPP_PM_REFUSE_PENDING;
	return HSPP_PM_OK;
}

/* Vendor-ish bounded ack window: 50 jiffies at HZ=100 = 500 ms [V].
 * The local copy clamps a user override to [50, 5000] ms so a typo cannot
 * turn a bounded one-shot into an unbounded wait. */
#define HSPP_PM_ACK_MS_DEFAULT	500u
#define HSPP_PM_ACK_MS_MIN	50u
#define HSPP_PM_ACK_MS_MAX	5000u

static inline hspp_u32 hspp_pm_ack_ms_clamp(hspp_u32 ms)
{
	if (ms == 0u)
		return HSPP_PM_ACK_MS_DEFAULT;
	if (ms < HSPP_PM_ACK_MS_MIN)
		return HSPP_PM_ACK_MS_MIN;
	if (ms > HSPP_PM_ACK_MS_MAX)
		return HSPP_PM_ACK_MS_MAX;
	return ms;
}

/* =====================================================================
 * 2. Adaptive service-poll governor (CPU idle / wakeup cost)
 *
 * The clean-room transport is polled: one kthread per endpoint calls
 * hcc_service_pass() and then msleep()s.  The historic cadence is 5 ms
 * (200 wakeups/s/endpoint, measured ~145 effective polls/s in the live
 * tuning sample).  While idle this is pure wakeup cost; while traffic flows
 * it bounds RX latency.  The governor keeps the fast cadence for a short
 * activity window and backs off to an idle cadence otherwise:
 *   - any pass that decoded something (or tick serviced work) = activity,
 *     which resets the streak and forces the fast cadence immediately;
 *   - idle cadence is never shorter than the active one.
 * Both intervals are module parameters, so the bench can A/B the trade-off
 * without a rebuild; idle_ms == 0 or < active_ms disables the backoff.
 * ===================================================================== */
#define HSPP_POLL_IDLE_MS_DEFAULT 20u	/* 5x fewer idle wakeups than 5 ms */
#define HSPP_POLL_IDLE_STREAK_DEFAULT 25u /* 25 x 5 ms = 125 ms fast window */

struct hspp_poll_gov {
	hspp_u32 active_ms;
	hspp_u32 idle_ms;
	hspp_u32 threshold;
	hspp_u32 streak;
};

static inline void hspp_poll_gov_init(struct hspp_poll_gov *g,
				      hspp_u32 active_ms, hspp_u32 idle_ms,
				      hspp_u32 threshold)
{
	if (!g)
		return;
	g->active_ms = active_ms ? active_ms : 1u;
	if (idle_ms == 0u || idle_ms < g->active_ms)
		idle_ms = g->active_ms;	/* backoff disabled */
	g->idle_ms = idle_ms;
	g->threshold = threshold ? threshold : 1u;
	g->streak = 0u;
}

/* Record one pass.  active != 0 means the pass did work.  Returns the next
 * sleep interval in milliseconds (always >= 1). */
static inline hspp_u32 hspp_poll_gov_step(struct hspp_poll_gov *g,
					  hspp_u32 active)
{
	if (!g)
		return HSPP_POLL_IDLE_MS_DEFAULT;
	if (active) {
		g->streak = 0u;
	} else if (g->streak < 0x7fffffffu) {
		g->streak++;
	}
	if (g->idle_ms > g->active_ms && g->streak >= g->threshold)
		return g->idle_ms;
	return g->active_ms;
}

static inline hspp_u32 hspp_poll_gov_interval(const struct hspp_poll_gov *g)
{
	if (!g)
		return HSPP_POLL_IDLE_MS_DEFAULT;
	if (g->idle_ms > g->active_ms && g->streak >= g->threshold)
		return g->idle_ms;
	return g->active_ms;
}

/* =====================================================================
 * 3. PCIe ASPM state (safe subset: assert vendor parity, never enable)
 *
 * Vendor RE [V]: hi_pcie_enable_aspm is called only when the DT PCIe node
 * has an "aspm-enable" property; the stock luofu DTS does NOT have it, so
 * the vendor RC never sets LNKCTL.ASPM (PCI_EXP_LNKCTL bits 1:0) and the
 * board runs with L0s/L1 disabled.  Our target DTS also omits the property,
 * so the safe action is: read the endpoint LNKCTL once at probe, log the
 * state, and keep the bits at 0 (vendor parity).  Enabling ASPM would be a
 * link-stability experiment, not a safe subset change; the public Hi1105
 * quirk ("avoid L0s and L1") points the same way.
 * ===================================================================== */
#define HSPP_LNKCTL_ASPM_MASK	0x0003u	/* bit0 L0s, bit1 L1 */
#define HSPP_LNKCTL_ASPM_L0S	0x0001u
#define HSPP_LNKCTL_ASPM_L1	0x0002u

struct hspp_aspm_state {
	hspp_u16 raw;		/* the full LNKCTL value that was read		*/
	hspp_u8  l0s;		/* L0s enabled					*/
	hspp_u8  l1;		/* L1 enabled					*/
	hspp_u8  enabled;	/* l0s || l1					*/
};

static inline struct hspp_aspm_state hspp_aspm_decode(hspp_u16 lnkctl)
{
	struct hspp_aspm_state s;

	s.raw = lnkctl;
	s.l0s = (lnkctl & HSPP_LNKCTL_ASPM_L0S) ? 1u : 0u;
	s.l1 = (lnkctl & HSPP_LNKCTL_ASPM_L1) ? 1u : 0u;
	s.enabled = (s.l0s || s.l1) ? 1u : 0u;
	return s;
}

/* LNKCTL with the ASPM bits cleared; every other bit preserved.  Disabling
 * ASPM cannot break a trained link (it only removes an optional low-power
 * substate), which is why this direction is inside the safe subset while
 * enabling is not. */
static inline hspp_u16 hspp_aspm_disable_bits(hspp_u16 lnkctl)
{
	return (hspp_u16)(lnkctl & (hspp_u16)~HSPP_LNKCTL_ASPM_MASK);
}

/* =====================================================================
 * 4. TX path capacity model (host -> AP data)
 *
 * Geometry [V]: 8 TID rings x 64 entries x 8 B (dp_rings.h DP_TX_MSDU_*),
 * host in-flight pool 16 frames (hi5622_data_tx_logic.h HDTX_POOL_DEPTH),
 * MPDU <= 768 B (HDTX_MPDU_MAX = DP_TXRX_MAX_MPDU), one TID, non-QoS, NO
 * aggregation (no A-MPDU/A-MSDU in this stack).  Completion is a delayed
 * work reading the device rptr: first arm 5 ms after the first post, rearm
 * 5 ms while frames are in flight (HDTX_POLL_MS), so the host-side ceiling is
 * pool_depth * (1000 / poll_ms) frames/s.  At the defaults that is
 * 16 * 200 = 3200 frames/s = 2.46 MB/s of MPDU bytes.
 *
 * The over-the-air ceiling of a NON-aggregated 768 B frame is computed with
 * the HT-mixed 2.4 GHz 20 MHz 1SS frame timing (preamble 40 us, SIFS 10 us,
 * ACK 40 us, DIFS 34 us, average backoff 8 x 9 us = 72 us - the classic
 * 802.11 values, [I] for the exact firmware rate choice).  At MCS7
 * (65 Mbps PHY) the cycle is ~287 us -> ~3480 frames/s -> ~20.5 Mbps of
 * 736 B payload, i.e. the host default 3200 frames/s is *close to* the
 * non-aggregated air ceiling.  The real headroom steps are therefore
 * (a) raising the host window for bursts, and (b) aggregation, which this
 * clean-room stack does not implement.
 * ===================================================================== */
#define HSPP_TX_RING_DEPTH	64u	/* [V] per-TID MSDU ring depth		*/
#define HSPP_TX_RING_COUNT	8u	/* [V] one ring per TID 0..7		*/
#define HSPP_TX_POOL_DEPTH	16u	/* [V*] host in-flight pool (current)	*/
#define HSPP_TX_MPDU_MAX	768u	/* [V] DP_TXRX_MAX_MPDU		*/
#define HSPP_TX_MPDU_HDR		24u	/* 802.11 data hdr 24 B (no QoS)	*/
#define HSPP_TX_LLC_BYTES	8u	/* LLC/SNAP 8 B				*/
#define HSPP_TX_POLL_MS		5u	/* [V*] current completion rearm	*/

/* Host-side frame rate ceiling for a (pool_depth, poll_ms) pair. */
static inline hspp_u32 hspp_tx_pps(hspp_u32 pool_depth, hspp_u32 poll_ms)
{
	if (poll_ms == 0u || pool_depth == 0u)
		return 0u;
	return (hspp_u32)(((hspp_u64)pool_depth * 1000u) / poll_ms);
}

/* Bytes/s of MPDU (not payload) at that ceiling. */
static inline hspp_u64 hspp_tx_mpdu_bytes_per_s(hspp_u32 pool_depth,
						hspp_u32 poll_ms,
						hspp_u32 mpdu_bytes)
{
	return (hspp_u64)hspp_tx_pps(pool_depth, poll_ms) * mpdu_bytes;
}

/* Application payload bytes/s (strip the 24 B header + 8 B LLC/SNAP). */
static inline hspp_u64 hspp_tx_payload_bytes_per_s(hspp_u32 pool_depth,
						   hspp_u32 poll_ms,
						   hspp_u32 mpdu_bytes)
{
	hspp_u32 over = HSPP_TX_MPDU_HDR + HSPP_TX_LLC_BYTES;

	if (mpdu_bytes <= over)
		return 0u;
	return (hspp_u64)hspp_tx_pps(pool_depth, poll_ms) *
	       (mpdu_bytes - over);
}

/* ---------------------------------------------------------------------
 * Single-frame airtime model (HT-mixed, 20 MHz, 1 spatial stream).
 * phy_kbps is the PHY rate in kbit/s (65,000 = MCS7 / 65 Mbps).
 * Returns the whole frame cycle in microseconds and the associated rates.
 * ------------------------------------------------------------------- */
struct hspp_air_model {
	hspp_u32 frame_us;	/* preamble + data symbols			*/
	hspp_u32 cycle_us;	/* + SIFS + ACK + DIFS + average backoff	*/
	hspp_u32 pps;		/* 1e6 / cycle_us				*/
	hspp_u64 payload_bps;	/* payload bits per second at this cycle	*/
};

struct hspp_air_params {
	hspp_u32 phy_kbps;
	hspp_u32 preamble_us;	/* 40 HT-mixed 1SS 20 MHz [I]			*/
	hspp_u32 sifs_us;	/* 10						*/
	hspp_u32 ack_us;	/* 40						*/
	hspp_u32 difs_us;	/* 34						*/
	hspp_u32 backoff_slots;	/* average 8					*/
	hspp_u32 slot_us;	/* 9						*/
};

static inline struct hspp_air_model
hspp_air_model_frame(const struct hspp_air_params *p, hspp_u32 mpdu_bytes)
{
	struct hspp_air_model m;
	hspp_u64 bits, data_us;
	hspp_u32 over = HSPP_TX_MPDU_HDR + HSPP_TX_LLC_BYTES;

	if (!p || p->phy_kbps == 0u) {
		m.frame_us = 0u;
		m.cycle_us = 0u;
		m.pps = 0u;
		m.payload_bps = 0u;
		return m;
	}
	bits = (hspp_u64)mpdu_bytes * 8u;
	data_us = (bits * 1000u + p->phy_kbps - 1u) / p->phy_kbps;
	m.frame_us = p->preamble_us + (hspp_u32)data_us;
	m.cycle_us = m.frame_us + p->sifs_us + p->ack_us + p->difs_us +
		     p->backoff_slots * p->slot_us;
	m.pps = m.cycle_us ? (hspp_u32)(1000000u / m.cycle_us) : 0u;
	m.payload_bps = (mpdu_bytes > over) ?
		(hspp_u64)(mpdu_bytes - over) * 8u * m.pps : 0u;
	return m;
}

/* Convenience: the classic 2.4 GHz HT20 1SS reference parameters. */
static inline struct hspp_air_params hspp_air_params_ht20_2g(hspp_u32 phy_kbps)
{
	struct hspp_air_params p;

	p.phy_kbps = phy_kbps ? phy_kbps : 65000u;
	p.preamble_us = 40u;
	p.sifs_us = 10u;
	p.ack_us = 40u;
	p.difs_us = 34u;
	p.backoff_slots = 8u;
	p.slot_us = 9u;
	return p;
}

/* =====================================================================
 * 5. Scan pace model
 *
 * Measured [V*]: 6 channel results in ~0.94 s then a hard stop = ~156 ms per
 * channel; the firmware processes at most 6 channels per g0 id0xC request.
 * The driver already batches ceil(n/scan_req_batch) requests (default 6)
 * with scan_batch_gap_ms (default 300).  Model the expected wall time for a
 * full 13-channel scan with and without batching; this is the number the
 * ladder checks.
 * ===================================================================== */
#define HSPP_SCAN_PER_CH_MS	156u	/* [V*] measured 2026-11-27 20:30	*/
#define HSPP_SCAN_FW_MAX_CH	6u	/* [V*] per-request firmware cap	*/
#define HSPP_SCAN_BATCH_GAP_MS	300u	/* current driver default		*/

static inline hspp_u32 hspp_scan_total_ms(hspp_u32 channels,
					  hspp_u32 per_ch_ms)
{
	if (!channels || !per_ch_ms)
		return 0u;
	return channels * per_ch_ms;
}

/* Number of g0 id0xC requests for a channel list at a batch size
 * (batch == 0 means "one request", the legacy form). */
static inline hspp_u32 hspp_scan_requests(hspp_u32 channels, hspp_u32 batch)
{
	if (!channels || batch == 0u || batch >= channels)
		return 1u;
	return (channels + batch - 1u) / batch;
}

static inline hspp_u32 hspp_scan_batched_ms(hspp_u32 channels,
					    hspp_u32 batch,
					    hspp_u32 per_ch_ms,
					    hspp_u32 gap_ms)
{
	hspp_u32 reqs = hspp_scan_requests(channels, batch);
	hspp_u32 total = hspp_scan_total_ms(channels, per_ch_ms);

	if (reqs > 1u)
		total += (reqs - 1u) * gap_ms;
	return total;
}

/* =====================================================================
 * 6. Beacon / DTIM client-wakeup model (AP side)
 *
 * The AP beacons every ap.beacon_interval TU (100 TU = 102.4 ms on the
 * bench [V*]) and advertises DTIM period 2.  A power-saving client may wake
 * only every DTIM period, so the AP's DTIM setting is a client-side power
 * knob; the AP chip itself is always-on in our stack (no chip PM), so this
 * changes no AP power - only client wakeups and multicast latency.
 * ===================================================================== */
#define HSPP_BEACON_TU_DEFAULT	100u
#define HSPP_DTIM_DEFAULT	2u

static inline hspp_u32 hspp_beacon_ms(hspp_u32 beacon_tu)
{
	hspp_u32 ms;

	if (!beacon_tu)
		beacon_tu = HSPP_BEACON_TU_DEFAULT;
	/* 1 TU = 1024 us; integer milliseconds, minimum 1. */
	ms = (hspp_u32)(((hspp_u64)beacon_tu * 1024u) / 1000u);
	return ms ? ms : 1u;
}

static inline hspp_u32 hspp_client_wakeups_per_min(hspp_u32 beacon_tu,
						   hspp_u32 dtim_period)
{
	hspp_u32 interval;

	if (!dtim_period)
		dtim_period = HSPP_DTIM_DEFAULT;
	interval = hspp_beacon_ms(beacon_tu) * dtim_period;
	if (!interval)
		return 0u;
	return 60000u / interval;
}

/* ---------------------------------------------------------------------
 * TX completion rearm interval.  The data-tx lane rearms a delayed work
 * 5 ms after the first post and every 5 ms while frames are in flight, so
 * the poll interval and the in-flight pool together set the host ceiling
 * (sec 4).  The knob is clamped so a typo can never hammer the workqueue
 * (min 1 ms) or stall completions past the bounded timeout (max 100 ms).
 * ------------------------------------------------------------------- */
#define HSPP_DATA_TX_POLL_MS_DEFAULT	5u
#define HSPP_DATA_TX_POLL_MS_MIN	1u
#define HSPP_DATA_TX_POLL_MS_MAX	100u

static inline hspp_u32 hspp_data_tx_poll_ms_clamp(hspp_u32 ms)
{
	if (ms == 0u)
		return HSPP_DATA_TX_POLL_MS_DEFAULT;
	if (ms < HSPP_DATA_TX_POLL_MS_MIN)
		return HSPP_DATA_TX_POLL_MS_MIN;
	if (ms > HSPP_DATA_TX_POLL_MS_MAX)
		return HSPP_DATA_TX_POLL_MS_MAX;
	return ms;
}

/* =====================================================================
 * 7. PCIe link power note (documented, not acted on)
 *
 * ASPM L1 gives the endpoint a lower link power state between frames, but
 * the vendor board disables it (sec 3) and public Hi1105 guidance says
 * "avoid L0s and L1".  DTIM/beacon tuning is client-visible only.  The only
 * always-on cost this lane can reduce without firmware cooperation is the
 * host poll cadence (sec 2) and the TX completion rearm interval (sec 4).
 * ===================================================================== */

#endif /* HI5622_PM_PERF_LOGIC_H */
