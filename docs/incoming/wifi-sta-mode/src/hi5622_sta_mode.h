/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_sta_mode.h - kernel-side STA (client) kernel-SME glue for the
 * Hi5622V100 clean-room driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-sta-mode, shared task task-155,
 * 2026-11-25).  Splits the STA connect work into:
 *   - hi5622_sta_sme.c  portable C99 logic (frame builders, decoders, FSM)
 *   - this file         kernel glue (cfg80211 completion, module params,
 *                       mgmt-carrier TX, CRX delivery hook)
 *
 * FLOW after the WAL cfg-event 0x123 join post (hi5622_sta.c):
 *   1. hi5622_sta_mode_begin() stores the connect context and posts the
 *      802.11 authentication request on the corrected g0 id6 mgmt carrier.
 *   2. Device->host auth response arrives on the CRX path
 *      (D2H g1 id13 -> hi5622_mgmt_deliver -> hi5622_sta_crx_deliver), the
 *      assoc request is posted.
 *   3. Device->host assoc response completes the connect:
 *      cfg80211_connect_done(status=0, bssid, req_ie, resp_ie).  This is the
 *      "assoc completion on the CRX path" - NOT a data-ring event and NOT a
 *      firmware-allocated completion record (see hi5622_sta_sme.h).
 *   4. A bounded connect timer (sta_connect_timeout_ms) fails the connect
 *      through cfg80211_connect_done(status=-1, timeout_reason=...) when the
 *      AP never answers - one shot, no retry.
 *
 * Fail-closed contract (compiled defaults):
 *   sta_connect=0  (default) OFF: the .connect/.disconnect path is the
 *                  wifi-sta-join lane's join-only path; nothing new posts.
 *   sta_connect=1  DRY: the join body (hi5622_sta.c) and the auth/assoc
 *                  frames are built+validated, NOTHING is posted.
 *   sta_connect=2  LIVE: requires sta_join=2 (the join post), a board that
 *                  passed the post gates and allow_frames budget; posts the
 *                  join, then auth, then assoc, and completes through CRX.
 * No key material is ever touched by these builders.
 */
#ifndef HI5622_STA_MODE_H
#define HI5622_STA_MODE_H

#ifndef __KERNEL__
#error "hi5622_sta_mode.h is kernel-only; portable logic is hi5622_sta_sme.h"
#endif

#include <linux/types.h>

struct cfg80211_connect_params;
struct hi5622_cfg80211;
struct hi5622_pci;
struct himg_rx_frame;
struct net_device;

/* module parameter (defined in hi5622_sta_mode.c; read by hi5622_sta.c). */
extern unsigned int hi5622_sta_connect_mode;

/* Pre-.connect validation.  Returns 0 for OFF (legacy join-only path) and
 * for a consistent DRY/LIVE request; -EINVAL/-EPERM otherwise.  Called
 * before the join frame is built or posted. */
int hi5622_sta_mode_precheck(struct net_device *dev,
			     struct cfg80211_connect_params *sme);

/* After a successful WAL join post: start the host-SME handshake (LIVE) or
 * return 0 for DRY.  0 = the connect is in flight (completion owed through
 * cfg80211_connect_done); negative errno = refused before the first mgmt
 * frame (cfg80211 will report the .connect error). */
int hi5622_sta_mode_begin(struct hi5622_cfg80211 *c, struct net_device *dev,
			  struct cfg80211_connect_params *sme);

/* DRY (sta_connect=1): build+validate the auth/assoc frames, post nothing. */
int hi5622_sta_mode_dry(struct hi5622_cfg80211 *c, struct net_device *dev,
			struct cfg80211_connect_params *sme);

/* Teardown after a posted WAL disconnect: reports cfg80211_disconnected()
 * when connected, or fails a pending connect, under the mode lock. */
void hi5622_sta_mode_disconnect(struct hi5622_cfg80211 *c,
				struct net_device *dev, u16 reason);

/* Cancel the timer and drop the context (cfg80211 removal). */
void hi5622_sta_mode_remove(struct hi5622_cfg80211 *c);

/* CRX delivery hook: called from hi5622_mgmt_deliver() for every decoded
 * D2H group-1 id-13 management frame.  Returns 1 when the STA kernel-SME
 * consumed the frame (the AP-delivery path must then not forward it), 0
 * otherwise.  Never sleeps under the HCC service thread lock (trylock) and
 * is fail-closed (an out-of-order response is counted and dropped). */
int hi5622_sta_crx_deliver(struct hi5622_pci *ctx,
			   const struct himg_rx_frame *rx);

#endif /* HI5622_STA_MODE_H */
