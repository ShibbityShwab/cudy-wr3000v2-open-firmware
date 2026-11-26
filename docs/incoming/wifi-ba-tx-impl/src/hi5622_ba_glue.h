/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_ba_glue.h - kernel binding for the fail-closed TX-direction BA core
 * (Hi5622V100 clean-room driver, Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-ba-tx-impl, shared task-217).  The
 * portable core (hi5622_ba.c) is callback-driven and host-tested; this file
 * is the kernel glue that binds it to:
 *
 *   - the single H2D wire path hi5622_frame_post() (g0 0x0A relay 44 B,
 *     g0 0x0B A-MPDU start/end 20 B, g0 0x1C TX BA notify 32 B);
 *   - the proven mgmt-TX carrier (HCC g0 id 6, 48 B control + inline
 *     action frame) for the ADDBA request/response/DELBA action frames;
 *   - the D2H BA events (ids 2/3/0x0F/0x1D), the received category-3 action
 *     frames, and the AP data-TX path (the in-kernel ADDBA originator).
 *
 * Fail-closed: every entry point is inert unless ba_enable == 1 (bounded
 * counters are still kept).  The H2D/action posts are deferred to a
 * workqueue: the D2H/mgmt hooks run in the HCC service thread and the TX
 * hook runs in ndo_start_xmit context, neither of which may run the
 * sleeping post path inline.
 */
#ifndef HI5622_BA_GLUE_H
#define HI5622_BA_GLUE_H

#include <linux/types.h>

struct hi5622_pci;

/* Allocate/initialize the core + workqueue state (probe; no device access). */
int  hi5622_ba_glue_init(struct hi5622_pci *ctx);
void hi5622_ba_glue_remove(struct hi5622_pci *ctx);

/* The AP start committed (called from the cfg80211 start_ap success path):
 * records the channel/band and the AP MAC the action frames are sent from. */
void hi5622_ba_glue_ap_up(struct hi5622_pci *ctx, u8 band, u8 channel,
			  const u8 *mac);
void hi5622_ba_glue_ap_down(struct hi5622_pci *ctx);

/* D2H BA events: call from the HCC deliver path for every frame (the glue
 * filters group/id cheaply and defers the real work). */
void hi5622_ba_glue_d2h(struct hi5622_pci *ctx, const u8 *hdr,
			const u8 *payload, u32 payload_avail, u32 wire_len);

/* A received management frame (group1 id13): the glue keeps only category-3
 * action frames and defers the ADDBA/DELBA handling. */
void hi5622_ba_glue_mgmt_rx(struct hi5622_pci *ctx, const u8 *frame,
			    size_t len);

/* One AP data MPDU was posted to a peer (called from the data-TX path after
 * a successful ring post; xmit context, must not sleep). */
void hi5622_ba_glue_tx_data(struct hi5622_pci *ctx, const u8 *peer, u8 tid,
			    u16 seq);

/* Read-only one-line status for cfg80211_status evidence. */
int  hi5622_ba_glue_status_line(struct hi5622_pci *ctx, char *buf, size_t len);

#endif /* HI5622_BA_GLUE_H */
