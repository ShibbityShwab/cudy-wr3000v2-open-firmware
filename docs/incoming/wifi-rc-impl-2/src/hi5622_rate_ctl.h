/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_rate_ctl.h - kernel surface of the fail-closed rate-control /
 * alg_cfg command path (Hi5622V100 clean-room driver, wifi-rate-ctl lane).
 *
 * RC-2 (wifi-rc-impl-2 lane): every write additionally requires the CORRECTED
 * C2 PRECONDITION recorded by the H2D layer this load - ctx_create was TYPE 1,
 * its ctx4+0x190 attestation held, and the ordered txq_sync72 was committed
 * (fw-h2d-stall task-181 + the proven calibration ladder).  Without it the
 * write refuses before any device write (fail-closed; see rc_precond_check).
 *
 * Default OFF: every write attribute refuses until the module is loaded with
 * rate_ctl=1.  The send path reuses the proven post-ready config-event carrier
 * (HCC group 0 / id 2 / key 0x0101, the calibration ladder's carrier) and the
 * same SR-node/mailbox gates, budget and one-frame-per-idea rules as the
 * wifi-h2d-send lane.  No register is written by this surface; the only device
 * effect is the firmware alg_cfg config event the Lead asks for.
 */
#ifndef HI5622_RATE_CTL_H
#define HI5622_RATE_CTL_H

#include <linux/device.h>
#include <linux/types.h>

struct hi5622_pci;

/* Module parameters (defined in hi5622_rate_ctl.c). */
extern unsigned int hi5622_rate_ctl;
extern unsigned int hi5622_rate_ctl_fb;
extern unsigned int hi5622_rate_ctl_arbitrary;

int  hi5622_rate_ctl_add_attrs(struct device *dev);
void hi5622_rate_ctl_del_attrs(struct device *dev);

/* Called from hi5622_hcc_deliver() for every received frame (D2H).  Decodes
 * the key-0x0101 config-sync answers (g1 id3) into the read-only status/log
 * attributes; a non-matching frame is ignored.  Never writes the device. */
void hi5622_rate_ctl_rx(struct hi5622_pci *ctx, const u8 *hdr,
			const u8 *payload, u32 payload_len);

#endif /* HI5622_RATE_CTL_H */
