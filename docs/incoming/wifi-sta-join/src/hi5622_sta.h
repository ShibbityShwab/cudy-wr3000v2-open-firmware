/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_sta.h - kernel-side cfg80211 STA connect/disconnect path for the
 * Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (wifi-sta-static lane, shared task task-141;
 * hardened by the wifi-sta-join lane, shared task task-146).  The wire
 * frames are built by the portable hi5622_sta_frames.c; this file maps
 * cfg80211 state to those frames, applies the fail-closed one-shot gates and
 * posts them through the inherited hi5622_frame_post() gate.
 */
#ifndef HI5622_STA_H
#define HI5622_STA_H

#include <linux/types.h>

struct cfg80211_connect_params;
struct hi5622_cfg80211;
struct hi5622_pci;
struct net_device;

/* cfg80211 .connect / .disconnect.  Both are fail-closed: they return
 * -EOPNOTSUPP unless the hi5622_sta_join module parameter enables the
 * vendor WAL cfg-event join path (see hi5622_sta.c):
 *   sta_join=0  fail closed (default)
 *   sta_join=1  build+validate the frame only (dry run, no device write)
 *   sta_join=2  one single-shot post per module load (also needs
 *               allow_frames>0); no retry, stop on firmware exception. */
int hi5622_sta_connect(struct hi5622_cfg80211 *c, struct net_device *dev,
		       struct cfg80211_connect_params *sme);
int hi5622_sta_disconnect(struct hi5622_cfg80211 *c, struct net_device *dev,
			  u16 reason_code);

/* Log-only D2H completion decode (g2 id3 assoc / g2 id4 disassoc; see
 * hi5622_sta_logic.h).  Called from the direct D2H reader; NEVER calls
 * cfg80211_connect_done() and never dereferences the firmware-supplied IE
 * addresses - the completion wiring belongs to the RX/AP-mgmt lanes. */
void hi5622_sta_d2h_deliver(struct hi5622_pci *ctx, const u8 *hdr,
			    const u8 *payload, u32 payload_len);

/* Free the per-connect host buffer (called from hi5622_cfg80211_remove()). */
void hi5622_sta_remove(struct hi5622_cfg80211 *c);

#endif /* HI5622_STA_H */
