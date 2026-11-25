/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rx_diff.h - portable VENDOR-vs-OURS RX state parity, status decode
 * and read-only capture rules (lane wifi-rx-vendor-diff, shared task task-162).
 *
 * ORIGINAL clean-room code.  Every constant below is a DERIVED FACT decoded by
 * the project's committed static-RE lanes from the local vendor artifacts and
 * quoted in docs/incoming/wifi-rx-vendor-diff/REPORT.md; no vendor code text,
 * binary, firmware byte or per-unit identity is reproduced here.
 *
 * Purpose: the MAC RX path captures nothing even though the free-ring consumer
 * is armed (live bank CA 0x40050000 status=0x182 norm/small rptr=0x20,
 * cmp_wptr=0).  The definitive way to close that gap is a byte-exact diff of
 * the vendor's stock RX state (committed dump opensource/dumps/reg_all.txt,
 * taken with the vendor driver live) against ours.  This file carries:
 *
 *   1. the vendor stock value of every RX-relevant word,
 *   2. the classification of each word (host writes it / host maintains a
 *      pointer / device-owned telemetry / vendor never touches it),
 *   3. the host-MAC interrupt status bit decode decoded from the vendor ISR
 *      shuangta_irq_host_mac_isr, and
 *   4. the read-only observation rules used to prove "the MAC wrote a
 *      completion" without moving a single ring pointer (crash-safe rung).
 *
 * The driver (hi5622_dp_txrx.c) renders these tables as the read-only sysfs
 * action "dp_rx_parity" and uses the observer in the observe-only rung of
 * dp_rx_drain.  Everything here is host-tested by tools/rx_parity_selftest.c.
 */
#ifndef _HI5622_RX_DIFF_H
#define _HI5622_RX_DIFF_H

#include "hi5622_rx_live.h"
#include "dp_rings.h"

/* -------------------------------------------------------------------
 * 1. Word classification
 * ------------------------------------------------------------------- */
enum hi5622_rx_word_expect {
	HIRD_KIND_MATCH = 0,	/* fixed host-written value; compare exactly   */
	HIRD_KIND_RANGE,	/* host-written 32-bit device VA (window check) */
	HIRD_KIND_DYNAMIC,	/* host-maintained packed index / W1C status   */
	HIRD_KIND_DEVICE,	/* device-owned: printed for parity, never written */
	HIRD_KIND_UNKNOWN	/* vendor stock value, writer unknown [U]      */
};

struct hi5622_rx_word {
	rx_u16 off;
	rx_u32 v;		/* vendor stock value (0 where not fixed)  */
	rx_u8 kind;
	const char *name;
};

/* Result of comparing one actual word against the table. */
enum hi5622_rx_parity {
	HIRD_PAR_MATCH = 0,	/* exact match                                 */
	HIRD_PAR_MISMATCH,	/* fixed expected differs                      */
	HIRD_PAR_RANGE_OK,	/* address inside the host DMA window          */
	HIRD_PAR_RANGE_BAD,	/* address outside / unaligned                 */
	HIRD_PAR_DYNAMIC,	/* pointer/status: no fixed expected value     */
	HIRD_PAR_READ_FAIL,	/* transport miss (0xffffffff)                 */
	HIRD_PAR_DEVICE_ZERO,	/* device-owned word reads zero: vendor nonzero */
	HIRD_PAR_DEVICE_OK	/* device-owned word equals vendor stock       */
};

/* Vendor stock tables.  Band-0 CAs: config block 0x40040000 (machw array
 * word 5) and live bank 0x40050000 (machw array word 8). */
#define HIRD_CFG_CA		0x40040000u
#define HIRD_LIVE_CA		0x40050000u
#define HIRD_LIVE_SPAN		0x44u	/* vendor dump covers +0x00..+0x40 */
#define HIRD_CFG_SPAN		0xb0u	/* vendor dump covers +0x00..+0xac */

const struct hi5622_rx_word *hi5622_rx_cfg_words(unsigned int *count);
const struct hi5622_rx_word *hi5622_rx_live_words(unsigned int *count);

/* Classify one word.  range_ok is used only for HIRD_KIND_RANGE words
 * (1 = valid device VA inside the host DMA window, 0 = invalid). */
int hi5622_rx_parity_classify(const struct hi5622_rx_word *w,
			      rx_u32 actual, int range_ok,
			      enum hi5622_rx_parity *out);
const char *hi5622_rx_parity_name(enum hi5622_rx_parity p);

/* Number of device-owned live-bank words the vendor stock dump shows as
 * non-zero (the "unexplained live-bank state" this lane tracks). */
#define HIRD_LIVE_EXTRA_COUNT	5u
extern const rx_u16 hi5622_rx_live_extra_offsets[HIRD_LIVE_EXTRA_COUNT];

/* -------------------------------------------------------------------
 * 2. Host-MAC interrupt status decode (CA 0x40050000 +0x00, mask +0x48)
 *
 * Decoded from the vendor ISR shuangta_irq_host_mac_isr (0x39a1c) bit walk:
 *   bit 2  : RX completion service (refill free rings, drain completion ring,
 *            publish cmp consumer, W1C served bits)                      [V]
 *   bit 3  : norm free-ring condition (service helper may mask irq 3)     [V]
 *   bit 4  : small free-ring condition (may mask irq 4)                   [V]
 *   bit 1  : vendor forwards an HCC control message to the firmware core
 *            (helper 0x39558, hcc_msg_alloc(4))                           [V]
 *   bit 8  : vendor forwards an event to the firmware core
 *            (helper 0x3920c, hcc_msg_alloc(16), group 0 id 7 header)     [V]
 *   bits 0,5,6,7,9,10 : further vendor service/forward paths              [V]
 * Mask 0xfffffcc1 leaves bits 1,2,3,4,5,8,9 observable (status & ~mask).
 * Vendor stock idle status = 0x400 (masked away); ours reads 0x182 => active
 * bits 1 and 8, both of which the vendor services by forwarding messages to
 * the firmware core - a host write class our driver does not implement (a
 * candidate link, not proven needed for RX capture).
 * ------------------------------------------------------------------- */
#define HIRD_STATUS_BITS	16u
#define HIRD_STATUS_RX_BIT	2u

struct hi5622_rx_status_dec {
	rx_u32 status;
	rx_u32 mask;
	rx_u32 active;		/* status & ~mask                     */
	rx_u32 rx_active;	/* bit 2 observable                   */
	rx_u32 forward_bits;	/* active bits the vendor forwards    */
};

int hi5622_rx_status_decode(rx_u32 status, rx_u32 mask,
			    struct hi5622_rx_status_dec *out);
const char *hi5622_rx_status_bit_name(unsigned int bit);
int hi5622_rx_status_bits_str(const struct hi5622_rx_status_dec *d,
			      char *buf, unsigned int cap);

/* -------------------------------------------------------------------
 * 3. Read-only completion observation (crash-safe capture rung)
 *
 * Observe-only mode proves the device path by reading the completion ring
 * memory and the returned buffers WITHOUT publishing any producer/consumer
 * index.  Rules (all fail-closed, host-tested):
 *   - the entry is the device VA of buffer+4, 4-aligned;
 *   - it must equal one of the two pool device VAs (norm then small);
 *   - the slot must be below the ring depth;
 *   - the 36-byte header at buffer+4 must parse (length bounds, no chain).
 * ------------------------------------------------------------------- */
enum hi5622_rx_obs {
	HIRD_OBS_NONE = 0,	/* no pending completion                    */
	HIRD_OBS_FRAME,		/* completion entry valid, header accepted  */
	HIRD_OBS_BAD_ENTRY,	/* entry not in either RX pool              */
	HIRD_OBS_BAD_HDR,	/* 36-B header refused                      */
	HIRD_OBS_CHAIN		/* sub-MSDU chain (refused until decoded)   */
};

/* Locate a completion entry in the two pools read-only.  On HIRD_OBS_FRAME/
 * BAD_HDR/CHAIN, *ring_kind (0 norm, 1 small), *slot and *buf_dma are set. */
int hi5622_rx_obs_entry(rx_u32 entry,
			const rx_u32 *norm_dma, rx_u32 ndepth,
			const rx_u32 *small_dma, rx_u32 sdepth,
			rx_u8 *ring_kind, rx_u32 *slot,
			rx_u32 *buf_dma);

/* -------------------------------------------------------------------
 * 4. Vendor RX-init program parity (the 12-write enable, statically decoded)
 *
 * Vendor sequence: shuangta_rx_host_init_dscr_queue 0x37888 writes +0x1c
 * (norm len), +0x20, +0x2c, +0x1c (small len), +0x24, +0x30, +0x28, +0x34,
 * fills both free rings, then chip IRQ init writes +0x48 = 0xfffffcc1.  The
 * clean-room 14-write program (dp_mac_program) covers the same registers.
 * This check asserts the fixed words of a built program are the vendor-derived
 * values and that the required offsets are all present.
 * ------------------------------------------------------------------- */
struct hi5622_rx_init_check {
	rx_u32 missing_mask;	/* bit i set = required word i absent */
	const char *fail_name;		/* first failing word name, or NULL   */
	rx_u32 bad_value;	/* first mismatching value, or 0      */
};

#define HIRD_INIT_FIXED_COUNT	6u
extern const struct hi5622_rx_word hi5622_rx_init_fixed[HIRD_INIT_FIXED_COUNT];

int hi5622_rx_init_program_check(const struct dp_mac_reg *regs,
				 unsigned int count,
				 struct hi5622_rx_init_check *out);

#endif /* _HI5622_RX_DIFF_H */
