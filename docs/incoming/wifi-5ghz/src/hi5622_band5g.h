/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_band5g.h - 5 GHz band-enable / band-config helpers for the
 * Hi5622V100 clean-room Wi-Fi driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-5ghz, shared task task-145,
 * 2026-11-24).  No vendor source text, object code, firmware byte or
 * calibration value is reproduced; every constant is a decoded fact with the
 * vendor-module / firmware address recorded in ../REPORT.md and ../TABLES.md.
 *
 * Purpose
 * -------
 * The vendor driver builds ONE wiphy per logical band (band type 0 = 2.4 GHz,
 * 1 = 5 GHz) and keeps two static band/channel tables:
 *   - 14 channels: ch1..14   (2412..2484 MHz)
 *   - 29 channels: ch36..64, 100..144, 149..165, 184..196
 *                 (5180..5320, 5500..5720, 5745..5825, 4920..4980 MHz)
 * The firmware carries the SAME two tables (2.4 GHz at CA 0x117B48, 5 GHz at
 * CA 0x117AD4, 4-byte stride) and resolves a channel NUMBER to a band-local
 * INDEX 0..13 / 0..28 (CA 0x10172C).  The H1 band-config message (g0 id10,
 * firmware handler CA 0x5FF64) carries the channel NUMBER in a u16 and asks
 * the firmware chip channel table (CA 0xD6A1E) for the per-channel context,
 * so no separate 5 GHz "channel id" table exists.
 *
 * Everything here is pure C99 (host-testable with tools/selftest.sh); the
 * driver only acts on it when the band5g module parameter is 1 (default 0 =
 * bench-validated 2.4 GHz behaviour, 5 GHz AP start fail-closed).
 */
#ifndef HI5622_BAND5G_H
#define HI5622_BAND5G_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif

#define HI5622_BAND5G_2GHZ		0u
#define HI5622_BAND5G_5GHZ		1u

#define HI5622_BAND5G_2G_COUNT		14u
#define HI5622_BAND5G_5G_COUNT		29u

/* Channel flags (derived from the vendor wiphy tables + the regulatory
 * rules the bench-validated driver already applies; the kernel regdb still
 * has the final say at wiphy registration). */
#define HI5622_BAND5G_CHAN_NO_IR	0x01u	/* passive / no initiator */
#define HI5622_BAND5G_CHAN_RADAR	0x02u	/* DFS radar channel */

struct hi5622_band5g_chan {
	u16	freq;	/* centre frequency, MHz */
	u8	num;	/* IEEE channel number */
	u8	flags;	/* HI5622_BAND5G_CHAN_* */
};

/* Vendor static band table (2.4 GHz: 14 entries; 5 GHz: 29 entries incl
 * ch144 and the 4.9 GHz public-safety block).  NULL for an invalid band. */
const struct hi5622_band5g_chan *hi5622_band5g_channels(u8 band, u16 *count);

/* Channel count of a band, 0 for an invalid band. */
int hi5622_band5g_chan_count(u8 band);

/* Centre frequency <-> IEEE channel number using the vendor tables.
 * Returns 0 on success, -EINVAL for an invalid band, -ENOENT when the
 * frequency/channel is not in the vendor table. */
int hi5622_band5g_freq_to_chan(u8 band, u16 freq, u8 *out_chan);
int hi5622_band5g_chan_to_freq(u8 band, u8 chan, u16 *out_freq);

/* Band-local channel index 0..13 (2.4 GHz) / 0..28 (5 GHz) - the index the
 * firmware CA 0x10172C validator produces and the scan request's entry+0x03
 * field carries. */
int hi5622_band5g_chan_index(u8 band, u8 chan, u8 *out_idx);

/* 1 = channel number is in the band table, 0 = not (or invalid band). */
int hi5622_band5g_chan_valid(u8 band, u8 chan);

/* AP-start module channel config (vendor WAL cmd 0x0124 / g0 id1 key 0x0124,
 * 4-byte body {band, vap, channel, 0}): resolve+validate the band/channel
 * pair the cfg80211 chandef asks for.
 *
 * FAIL CLOSED: on the 5 GHz band the vendor table validation and the band5g
 * gate both apply - band5g_enable == 0 returns -EOPNOTSUPP before any frame
 * can be built.  0 = 2.4 GHz keeps the bench-validated behaviour. */
struct hi5622_band5g_ap_chan {
	u8	band;		/* 0 = 2.4 GHz, 1 = 5 GHz */
	u8	channel;	/* IEEE channel number */
};

int hi5622_band5g_ap_chan_prepare(u32 band5g_enable, u8 band, u16 center_freq,
				  u8 channel,
				  struct hi5622_band5g_ap_chan *out);

/* H1 band-config (g0 id10, handler CA 0x5FF64) channel field: the firmware
 * looks the channel up in its chip channel table, so this must be the IEEE
 * channel NUMBER derived from the scan frequency - never the raw MHz value
 * (the old auto path sent 5180 for a 5 GHz scan, which the firmware lookup
 * cannot resolve).  Same fail-closed band5g gate for the 5 GHz band. */
int hi5622_band5g_band_cfg_chan(u32 band5g_enable, u8 band, u16 freq,
				u16 *out_chan);

/* 5 GHz wiphy profile words (vendor static .data+0xe54 supported-band
 * template): HT cap 0x0062 (20/40 + SGI20/40), VHT cap 0x00400064
 * (HTC_VHT | 160 MHz | SGI80 | SGI160), VHT MCS map 0xfffa (2 SS MCS0-9).
 * The 2.4 GHz values are reported for symmetry only. */
u16 hi5622_band5g_ht_cap_word(u8 band);
u32 hi5622_band5g_vht_cap_word(u8 band);
u16 hi5622_band5g_vht_mcs_map(u8 band);
u8  hi5622_band5g_max_power(u8 band);
const char *hi5622_band5g_name(u8 band);

#endif /* HI5622_BAND5G_H */
