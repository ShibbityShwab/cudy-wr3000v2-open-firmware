/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_band_cfg.h - refined builder for the g0 direct H2D message id 10
 * ("H1 band-config"; CLEAN-ROOM, original code).
 *
 * Lane: wifi-scan-full (shared task-138), builds on wifi-scan-tuning R4.
 * The message is the vendor host's relay of a received BA management frame:
 *   - payload+0x01 = 1: hmac_mgmt_rx_addba_rsp  (2.5.24 builder at .text 0x45A94)
 *   - payload+0x01 = 2: hmac_mgmt_rx_delba      (2.5.24 builder at .text 0x45D88)
 * Firmware handler: g0 direct message id 10, entry CA 0x5FF64, reached from
 * the firmware's g0 H2D table at CA 0x10CCFC (12-byte rows; the row for id 10
 * is at CA 0x10CD5C: {id=10, fn=0x5FF65, aux=0}).
 *
 * R4 used a 36-byte frame (12-byte HCC header + 24-byte payload) and put the
 * subtype-2 flag at payload+0x08.  This revision matches the vendor shape and
 * the firmware reads:
 *   - wire total 0x2C (44) = 12-byte HCC header + 32-byte payload (0x20);
 *     the vendor allocates hcc_msg_alloc(32) and the firmware handler reads
 *     payload+0x14/+0x15 (frame bytes 0x20/0x21), so the R4 36-byte frame was
 *     short of the vendor form;
 *   - subtype 2 reads payload+0x14 (byte) as its flag, not payload+0x08
 *     (0x5FF64 subtype-2 path: CA 0x60184 loads payload+0x07 into r8 and
 *     payload+0x14 into sb; payload+0x08 is never read on either path).
 *
 * FIRMWARE FIELD MAP - subtype 1 (all [V] from CA 0x5FF64, see REPORT.md):
 *   +0x00 u8  type, must be 3 (CA 0x5FFA4/CMP #3; else handler ignores frame)
 *   +0x01 u8  subtype 1 (CA 0x5FFAC/#1 -> CA 0x5FFCE)
 *   +0x02 u16 object key -> 0xD6A1E(key) (object-table lookup; NULL -> return)
 *   +0x07 u8  slot 0..7 (CA 0x5FFF0/CMP #7; >7 -> log+return)
 *   +0x09 u8  action: NONZERO -> clear/remove path 0x5FEC0(module,obj,slot)
 *             (CA 0x6002C/CBNZ -> CA 0x601C2); 0 -> configure path
 *   +0x0C u16 start: writes context +0x10 (min rule, CA 0x600B2) and
 *             context +0x1A = max(1,value) (CA 0x6009E)
 *   +0x10 u16 -> context +0x18 (CA 0x60092)
 *   +0x12 u8  -> context +0x17 mode (CA 0x600BA)
 *   +0x13 u8  -> context +0x13 = 2 state/enable (CA 0x600C0)
 *   +0x14 u16 compared with context +0x15 u16 (CA 0x60048); a mismatch while
 *             context +0x12 != 0 takes the 0x950C4 path (CA 0x60054..0x60086)
 *   +0x15 u8  logged only (CA 0x6005A/0x600E8); not written to the object
 *   +0x08     never read on the subtype-1 path (vendor leaves it 0)
 * Context base = object + slot*0x20 + 0xEC (CA 0x6003E/0x60042/0x60046).
 *
 * FIRMWARE FIELD MAP - subtype 2 (CA 0x60184 path; clear/remove):
 *   +0x00 = 3, +0x01 = 2, +0x02 u16 object key, +0x07 u8 slot (all same gates)
 *   +0x14 u8 flag: 0 -> 0x5FEC0(module,obj,slot), which zeroes context +0x12,
 *             +0x1A and +0x05 and calls 0xBCD54([module+0x4B8], context)
 *             (CA 0x5FEDE/0x5FEF0/0x5FEF2); nonzero -> 0x5FF06(obj+slot*0x20
 *             +0x108 record) (CA 0x601CE/0x601D6)
 *   +0x08     never read.
 *
 * FAIL CLOSED: the driver only calls this builder when the scan_band_cfg
 * module parameter is non-zero (default 0 = nothing is transmitted).  The
 * message is a BA-relay frame being reused as a research lever; the semantics
 * of the values beyond the firmware read-sites are [I], so the builder never
 * changes a value the caller did not ask for and zero-fills the rest.
 */
#ifndef HI5622_BAND_CFG_H
#define HI5622_BAND_CFG_H

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/types.h>
#else
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
typedef uint8_t u8;
typedef uint16_t u16;
#endif

#define HI5622_BAND_CFG_ID		0x0Au	/* g0 direct H2D id 10 */
#define HI5622_BAND_CFG_PAYLOAD_LEN	0x20u	/* vendor payload: 32 B */
#define HI5622_BAND_CFG_TOTAL		0x2Cu	/* 12-byte HCC header + 32 B payload */
#define HI5622_BAND_CFG_TYPE		0x03u	/* payload+0x00, required */
#define HI5622_BAND_CFG_SUB_SLOT	0x01u	/* addba-rsp relay: per-slot context */
#define HI5622_BAND_CFG_SUB_CLEAR	0x02u	/* delba relay: slot clear/remove */
#define HI5622_BAND_CFG_SUB_ARM		HI5622_BAND_CFG_SUB_CLEAR /* R4 compat name */

struct hi5622_band_cfg_params {
	u8	subtype;	/* 1 = slot config, 2 = clear/remove */
	u16	chan_id;	/* payload+0x02 (object-table key; first scan channel) */
	u8	slot;		/* payload+0x07, firmware accepts 0..7 */
	u8	sub2_flag;	/* subtype 2: payload+0x14 flag (0 = clear slot) */
	u8	action;		/* payload+0x09: nonzero = firmware clear path */
	u16	start;		/* payload+0x0C -> context +0x10/+0x1A */
	u16	field10;	/* payload+0x10 -> context +0x18 */
	u8	mode;		/* payload+0x12 -> context +0x17 */
	u16	cmp;		/* subtype 1: payload+0x14 u16 (compare field) */
};

/* Build one config frame into out (cap >= HI5622_BAND_CFG_TOTAL = 44).
 * Returns 0 on success, -EINVAL for bad arguments/params.  Pure function;
 * host-tested by tools/scan_full_selftest.c. */
int hi5622_band_cfg_build(u8 *out, size_t cap, size_t *len,
			  const struct hi5622_band_cfg_params *p);

#endif /* HI5622_BAND_CFG_H */
