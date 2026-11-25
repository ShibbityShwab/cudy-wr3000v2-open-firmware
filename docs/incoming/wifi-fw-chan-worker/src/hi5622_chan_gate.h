/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_chan_gate.h - decoded firmware channel-context semantics for the
 * Hi5622V100 clean-room host (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-fw-chan-worker, shared task task-180,
 * 2026-11-27).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced here; the derivation is in ../REPORT.md and
 * the evidence tables are ../TABLES.md.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The firmware group-0 id-1 channel-config worker (key 0x0124, CA 0x841B8)
 * validates (band, channel) against the firmware's own per-band channel
 * tables (CA 0x117B48 band 0: 14 channels 1..14; CA 0x117AD4 band 1: 29
 * channels 36..196) and, on success, stores four bytes at module_ctx+0x10:
 *
 *   +0x10 = channel        +0x11 = band
 *   +0x12 = body[1]        +0x13 = THE CHANNEL-TABLE INDEX
 *
 * The byte at +0x13 is NOT a validity flag.  For band 1 channel 36 it is 0
 * by construction.  The previous lane's wait gate (task-175) required that
 * byte to be 1, so its G6b rung could never pass for ch36 - a host-side gate
 * bug (REPORT section 4).  This file implements the decoded semantics:
 *
 *  - hi5622_chan_gate_index(): the table index the firmware computes, or -1
 *    when the worker would reject the pair with return 0x3E8;
 *  - hi5622_chan_gate_expected_word(): the exact u32 the worker stores;
 *  - hi5622_chan_gate_word_ready(): the fail-closed readiness predicate
 *    (the word must equal the expected store, including the index);
 *  - hi5622_chan_gate_rf_ran(): the post-store 5 GHz RF-programme gate.  The
 *    worker's CA 0x40684 call runs CA 0x101890 (the band-1 RF programme) only
 *    when module[4] (the create type) == 2, module+0x440 == 1 and
 *    band_object+0xEC8 == 1; otherwise it is skipped silently and the worker
 *    still returns 0.  The module gate pack logs the three read-only inputs
 *    and this predicate classifies them.
 *
 * The 2.4 GHz path is unchanged by construction (the wait only applies to a
 * band-1 AP and this file is only consulted by the band-1 wait and the
 * read-only gate pack).
 *
 * Pure C99: the same code compiles in the kernel module and in the host
 * selftest (../../src/chan_gate_selftest.c).
 */
#ifndef HI5622_CHAN_GATE_H
#define HI5622_CHAN_GATE_H

#ifdef __KERNEL__
#include <linux/types.h>
#else
#include <stdbool.h>
#include <stdint.h>

#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
#endif
#endif

/* Firmware per-band channel tables (validate helper CA 0x10172C). */
#define HI5622_CHAN_GATE_BAND0_COUNT	14u
#define HI5622_CHAN_GATE_BAND1_COUNT	29u

/* Module-context / band-object / per-link field offsets used by the
 * read-only gate pack (CPU addresses in ../TABLES.md T5/T7). */
#define HI5622_CHAN_GATE_MCTX_TYPE_OFF		0x004u
#define HI5622_CHAN_GATE_MCTX_440_OFF		0x440u
#define HI5622_CHAN_GATE_BAND_OBJ_EC8_OFF	0xEC8u
#define HI5622_CHAN_GATE_PERLINK_C8_OFF		0x1C8u

/* The firmware channel-table index for (band, channel); -1 (as an int) when
 * the worker would reject the pair (return 0x3E8, no store). */
int hi5622_chan_gate_index(unsigned int band, unsigned int channel);

/* The exact u32 the 0x0124 worker stores at module_ctx+0x10 for
 * (band, channel, body[1]=field).  false when the pair would be rejected. */
bool hi5622_chan_gate_expected_word(unsigned int band, unsigned int channel,
				    unsigned int field, u32 *word);

/* Decoded readiness: the stored word equals the expected store for this
 * (band, channel) and its index byte is the firmware table index.  A stale
 * word, the previous channel's word or the pre-store zero all fail. */
bool hi5622_chan_gate_word_ready(u32 word, unsigned int band,
				 unsigned int channel);

/* The post-store 5 GHz RF-programme gate (CA 0x40684 -> CA 0x101890):
 * module[4] (create type) must be 2 and both module+0x440 and the band
 * object's +0xEC8 must be 1.  All three are read-only firmware RAM reads. */
bool hi5622_chan_gate_rf_ran(u32 module_type, u32 module_440,
			     u32 band_obj_ec8);

#endif /* HI5622_CHAN_GATE_H */
