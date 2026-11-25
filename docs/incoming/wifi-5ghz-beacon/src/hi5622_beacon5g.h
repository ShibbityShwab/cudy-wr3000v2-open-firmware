/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_beacon5g.h - fail-closed 5 GHz AP-start channel-activation planner
 * (Hi5622V100 clean-room host, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-5ghz-beacon, shared task task-175,
 * 2026-11-25).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced here.  The derivation is in ../REPORT.md;
 * the evidence tables are ../TABLES.md.
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The 5 GHz AP reaches AP-ENABLED with the full vendor message order and a
 * golden page, but its beacons never reach the air, while the 2.4 GHz AP with
 * the same driver code beacons.  The vendor host module decode (2.5.24 and
 * 2.4.15 builds, ../TABLES.md T1-T4) shows:
 *
 *  - the vendor AP start is five frames and no more: 0x0124 channel config,
 *    0x0005 SSID, 0x006D auth, 0x0127 AP config, 0x0065 VAP start.  There is
 *    no per-band host activation message and no band byte in the set-SSID
 *    call (its second argument is the VAP index, not the band);
 *  - the only 0x0124 sender is hmac_config_set_channel, and the only
 *    hmac_band_set_channel/_and_bandwith callers are that builder (AP path)
 *    and hmac_config_set_freq (STA join sync, cmd 0x0052).  Those band-struct
 *    writes (band+0xd28 type, +0xd29 channel, +0x10d2 bandwidth) are HOST
 *    bookkeeping - they put no message on the wire;
 *  - the vendor's 0x0124 channel config is SYNCHRONOUS: the helper calls
 *    wal_send_cfg_event and then wal_check_and_release_msg_resp, i.e. it
 *    waits for the firmware's channel-config response before the SSID/AP
 *    config/VAP start frames are posted.  Its AP VAP create and VAP start
 *    likewise wait for the firmware response.  Our ladder posts all frames
 *    back-to-back and only waits for the coalesced H2D announce consumption,
 *    which is the transport drain, not the firmware worker's completion.
 *
 * The firmware's channel worker (CA 0x841B8) is what programs the RF channel;
 * it validates (band, channel), stores the 4-byte body at module_ctx+0x10
 * (channel, band, body[1], valid=1) and then runs the PHY channel program.
 * For band 1 that programming is the 5 GHz RF path.  Our five frames are
 * posted within microseconds, so on the 2-band profile the AP config/VAP
 * start can run before the 5 GHz channel/RF programming has finished; the
 * 2.4 GHz path tolerates it because its channel/RF state is fast/default.
 *
 * This file holds the decision as pure host-testable code:
 *
 *   ap_chan_wait_ms=0 (default, legacy, fail closed)  = no wait, behaviour of
 *   the bench-fired vap5g module is unchanged;
 *   1..1000 = on a band-1 AP (beacon_static=1 only) wait, after the ap_chan
 *   frame, until the firmware module channel context at module_ctx+0x10
 *   reports the programmed (channel, band, valid) tuple, bounded by the
 *   parameter; a timeout aborts the start BEFORE any AP frame (fail closed,
 *   never shotguns the AP frames at an unprogrammed band).  Every other value
 *   or band is refused (-EINVAL), never guessed.
 *
 * The readback constants are also corrected here (../TABLES.md T6): the
 * previous tree constants for the module table were derived with the wrong
 * BAR0 window base and addressed 0x171458+0x4000 / 0x172504+0x8000, so the
 * E2 fire's "VAP object readback=ZERO" and "channel ctx readback garbage"
 * are NOT evidence about the firmware objects.  BAR0 = CPU + 0x6b8000
 * (hi5622_contract.h HI5622_FW_RAM_BAR_BASE): module-table slot 0 CPU
 * 0x171458 = BAR0 0x829458 and the base u16 CPU 0x172504 = BAR0 0x82a504,
 * exactly the committed bench-validated recipe.
 *
 * Pure C99: the same code compiles in the kernel module and in the host
 * selftest (src/beacon5g_selftest.c).
 */
#ifndef HI5622_BEACON5G_H
#define HI5622_BEACON5G_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef int32_t  s32;
typedef uint32_t u32;
#endif
#endif

/* Bounded wait: 0 = legacy (no wait), >0 = poll the firmware module channel
 * context after the ap_chan frame.  Anything above the maximum is refused. */
#define HI5622_BEACON5G_WAIT_MAX_MS		1000u
#define HI5622_BEACON5G_WAIT_POLL_MS		5u

/* Firmware RAM convention (project-wide [V*]): host BAR0 of a CPU-RAM object
 * = CPU + 0x6b8000 (region 5 base; hi5622_contract.h). */
#define HI5622_BEACON5G_BAR0_BASE		0x6b8000u

/* Committed, bench-validated module-table recipe [V*] (CPU addresses; the
 * BAR0 forms 0x829458 / 0x82a504 are the ones the fire recipes read).
 * ctx0 = u32(slot0) + u16(base). */
#define HI5622_BEACON5G_MCTX_SLOT0_CPU		0x171458u	/* BAR0 0x829458 */
#define HI5622_BEACON5G_MCTX_BASE_CPU		0x172504u	/* BAR0 0x82a504 */

/* Firmware module context object fields [V] (committed lanes; see TABLES.md
 * T7).  The channel config worker stores its 4-byte body at ctx+0x10. */
#define HI5622_BEACON5G_MCTX_CHAN_OFF		0x010u
#define HI5622_BEACON5G_MCTX_VAP_OFF		0x190u
#define HI5622_BEACON5G_MCTX_LINK_OFF		0x4b0u
#define HI5622_BEACON5G_MCTX_BAND_OBJ_OFF	0x4b8u

/* Per-band link/bank publish words [V] from the committed boot-gate decode:
 * band 0 object CA 0x15ca08 + 0x1a00 = 0x15e408 (BAR0 0x816408),
 * band 1 object CA 0x15e570 + 0x1a00 = 0x15ff70 (BAR0 0x817f70).  The live
 * values are the per-bank contexts (bank0 0x00174bf0 / bank1 0x00175a18). */
#define HI5622_BEACON5G_BAND0_PUBLISH_CPU	0x15e408u	/* BAR0 0x816408 */
#define HI5622_BEACON5G_BAND1_PUBLISH_CPU	0x15ff70u	/* BAR0 0x817f70 */
#define HI5622_BEACON5G_BANK0_CTX_CPU		0x174bf0u
#define HI5622_BEACON5G_BANK1_CTX_CPU		0x175a18u

/* What one band-1 AP start must do after the ap_chan frame. */
struct hi5622_beacon5g_plan {
	unsigned int	wait_ms;	/* 0 = legacy (no wait) */
	bool		readback;	/* log the read-only gate pack */
	u32		mctx;		/* ctx0 CA, 0 = unresolved */
};

/* Fill *out with the channel-activation decision.
 *  - 0 on success; legacy (wait_ms = 0, mctx = 0) for a non-band-1 AP, for
 *    beacon_static != 1 and for wait_param == 0;
 *  - -EINVAL for NULL out, wait_param > HI5622_BEACON5G_WAIT_MAX_MS or
 *    band > 1 (fail closed, never guess).
 * The caller passes the current module-table words it read from firmware RAM
 * (0/0 when the chip is not yet readable). */
int hi5622_beacon5g_plan(unsigned int wait_param, unsigned int beacon_static,
			 unsigned int band, u32 slot0, u32 base,
			 struct hi5622_beacon5g_plan *out);

/* ctx0 = slot0 + u16(base); 0 when slot0 is 0 or the sum leaves the firmware
 * RAM window [0x100000, 0x217ffff] (fail closed: the caller must not
 * dereference a guessed context). */
u32 hi5622_beacon5g_mctx_ca(u32 slot0, u32 base);

/* True when the module channel config worker's 4-byte body at ctx+0x10
 * reports (channel, band, valid).  The worker stores body[2] (channel) at
 * byte 0, body[0] (band) at byte 1, body[1] at byte 2 and 1 at byte 3 after
 * its band/channel validation [V] - so this is the firmware's own
 * "channel programmed" flag, not a timing guess. */
bool hi5622_beacon5g_chan_ctx_ready(u32 word, unsigned int band,
				    unsigned int channel);

#endif /* HI5622_BEACON5G_H */
