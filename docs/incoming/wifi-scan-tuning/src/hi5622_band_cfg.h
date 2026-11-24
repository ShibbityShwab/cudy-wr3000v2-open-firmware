/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_band_cfg.h - optional builder for the vendor scan-context config
 * message (H1 experiment; wifi-scan-tuning lane, task-133 R4).
 *
 * ORIGINAL clean-room code.  No vendor source/object/firmware byte reproduced.
 *
 * The g0 direct H2D message id 10 (handler CA 0x5FF64; group-0 dispatch table
 * CA 0x10CD08 row {10, 0x5FF65}) configures the firmware's per-slot
 * scan/channel context.  Our clean-room host never sends it, which is the H1
 * explanation for the firmware not re-arming for a second scan request.
 * Payload fields decoded from the handler (payload offset -> write):
 *   +0x00 u8  type, must be 3
 *   +0x01 u8  subtype: 1 = per-slot channel/scan context config, 2 = arm/flag
 *   +0x02 u16 channel id (looked up by the firmware via CA 0xD6A1E)
 *   +0x07 u8  slot index 0..7 (band object + 0xEC + slot*0x20)
 *   +0x08 u8  subtype-2 flag (0 = reset path CA 0x5FEC0, else CA 0x5FF06)
 *   +0x09 u8  action/flag (nonzero takes the reset path in subtype 1)
 *   +0x0C u16 start index  -> context +0xFC (if currently zero) / +0x106
 *   +0x10 u16              -> context +0x104
 *   +0x12 u8  mode         -> context +0x103
 *   +0x14 u16 compare field (slot +0x15)
 * Payload is padded to 0x18; wire total = 12 + 0x18 = 36.
 *
 * FAIL CLOSED: nothing in the driver calls this builder unless the
 * scan_band_cfg module parameter is non-zero (default 0 = off).  The field
 * semantics are inference-level ([I]) and only a bench experiment can decide
 * whether they re-arm the scan context; see REPORT.md sec 12.
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
#define HI5622_BAND_CFG_PAYLOAD_LEN	0x18u
#define HI5622_BAND_CFG_TOTAL		0x24u	/* 12-byte HCC header + payload */
#define HI5622_BAND_CFG_TYPE		0x03u
#define HI5622_BAND_CFG_SUB_SLOT	0x01u	/* per-slot context config */
#define HI5622_BAND_CFG_SUB_ARM		0x02u	/* arm/flag path */

struct hi5622_band_cfg_params {
	u8	subtype;	/* 1 = slot config, 2 = arm flag */
	u16	chan_id;	/* payload+0x02 */
	u8	slot;		/* payload+0x07, firmware accepts 0..7 */
	u8	sub2_flag;	/* payload+0x08 (subtype 2 only) */
	u8	action;		/* payload+0x09 */
	u16	start;		/* payload+0x0C -> context +0xFC */
	u16	field10;	/* payload+0x10 -> context +0x104 */
	u8	mode;		/* payload+0x12 -> context +0x103 */
	u16	cmp;		/* payload+0x14 */
};

/* Build one config frame into out (cap >= HI5622_BAND_CFG_TOTAL).
 * Returns 0 on success, -EINVAL for bad arguments/params.  Pure function;
 * host-tested by tools/scan_band_cfg_selftest.c. */
int hi5622_band_cfg_build(u8 *out, size_t cap, size_t *len,
			  const struct hi5622_band_cfg_params *p);

#endif /* HI5622_BAND_CFG_H */

