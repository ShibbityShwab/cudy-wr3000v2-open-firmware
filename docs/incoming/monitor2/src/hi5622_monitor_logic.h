/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_monitor_logic.h - portable, kernel-free logic for the local-mirror
 * monitor mode of the Hi5622V100 clean-room driver (Cudy WR3000 v2.0).
 *
 * ORIGINAL clean-room code (monitor2 lane, shared task task-246, 2026-11-26).
 * Materialises the monitor-mode design first committed as documentation by the
 * monitor-mode lane (task-241) / MONITOR-MODE/CFG80211-MONITOR.md.
 *
 * This file has NO kernel dependencies (stdint/stddef only) so the exact byte
 * vectors below can be asserted by a host compiler without a kernel tree.
 * No vendor source/object/firmware bytes are reproduced.
 *
 * v1 semantics (deliberate, fail-closed):
 *   - monitor=0 (off)     : the monitor netdev is inert, set channel is
 *                           -EOPNOTSUPP, nothing is mirrored;
 *   - monitor=1 (mirror)  : frames that already reach the two existing RX
 *                           sinks are copied to the first monitor netdev with
 *                           a radiotap header; NO firmware write, NO channel
 *                           tune - the mirror follows the AP;
 *   - monitor>1           : refused, mirroring disabled.
 * This is NOT a promiscuous sniffer: true promiscuous mode needs a decoded
 * hi5622 firmware switch that does not exist in the vendor driver.
 */
#ifndef _HI5622_MONITOR_LOGIC_H
#define _HI5622_MONITOR_LOGIC_H

/* kernel build: <linux/types.h> provides size_t + uint8_t/uint16_t/uint32_t;
 * host build: the C library headers do.  No other kernel dependency. */
#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stddef.h>
#include <stdint.h>
#endif

#define HIMON_OFF		0u	/* default, fail-closed */
#define HIMON_LOCAL_MIRROR	1u

/* RX MPDU sanity cap used by the data path (0x618 = 1560 B). */
#define HIMON_MAX_FRAME		0x618u
/* Largest radiotap header this builder produces (R2 = 13 B fixed+fields). */
#define HIMON_RT_MAX		13u

/* 1 = mode allowed by policy (0 or 1), 0 = refused. */
int himon_mode_ok(uint32_t mode);
/* "off" / "local-mirror" / "invalid"; never NULL. */
const char *himon_mode_name(uint32_t mode);

/* Channel -> centre frequency (MHz). band 0 = 2.4 GHz, band 1 = 5 GHz.
 * 0 = mapped, -1 = not a channel this band plan can map. */
int himon_chan_to_freq(uint32_t band, uint32_t channel, uint16_t *freq);

/* Radiotap header builder (version 0, no FCS, no TX flags).
 * have_chan selects the CHANNEL field (u16 freq + u16 flags = 0); have_rssi
 * additionally selects DBM_ANTSIGNAL (s8 rssi).  have_rssi without have_chan
 * degrades to the R0 8-byte header (no per-frame channel metadata available).
 *
 * Pinned vectors (asserted by tools/monitor_selftest.c):
 *   R0                     = 00 00 08 00 00 00 00 00
 *   R1 @2412 MHz           = 00 00 0c 00 08 00 00 00 | 6c 09 | 00 00
 *   R2 @2437 MHz rssi -62  = 00 00 0d 00 08 08 00 00 | 85 09 | 00 00 | c2
 *
 * Returns the header length in bytes, or 0 when refused (NULL buf, cap too
 * small).  Never writes past cap.
 */
size_t himon_rt_build(uint8_t *buf, size_t cap, uint16_t freq,
		      int have_chan, int rssi, int have_rssi);

#endif /* _HI5622_MONITOR_LOGIC_H */
