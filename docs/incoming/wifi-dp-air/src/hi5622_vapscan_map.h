/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_vapscan_map.h - portable mapping between the neutral cfg80211
 * parameter structs of this lane and the task-110 wifi-vap-scan frame
 * builders (vapscan_frames.h/.c), plus the best-effort D2H scan-result
 * decoder.
 *
 * ORIGINAL clean-room code (wifi-scan-ap lane, shared task task-113,
 * 2026-11-25).  The frame builders themselves are the wifi-vap-scan lane's
 * deliverable (task-110, opensource commit 0e4ad45) and are integrated
 * unmodified except for the shared host-type guard; no vendor source, object
 * code, firmware byte, calibration value or secret is reproduced.
 *
 * Why a separate portable file: the byte mapping (cfg80211 request -> vendor
 * builder fields) and the tolerant D2H parse are the parts that must be
 * host-tested without a kernel (tools/scan_logic_selftest.c).  The kernel
 * adapter (hi5622_vapscan_provider.c) only wraps these in the provider ops
 * table + the module parameter that enables the [U] D2H decoder.
 *
 * Evidence: the frame layouts are the task-110 [V] vendor-derived specs; the
 * derived fields this map adds are marked inline.  D2H g1 id15 carries an
 * 802.11 beacon/probe-response frame followed by a 20-byte descriptor whose
 * field semantics are [U] (task-118 REPORT): the decoder probes the layout,
 * validates the 802.11 management frame, derives the channel from the DS/HT
 * operation IE, summarises the SSID/DS/HT/VHT/RSN/WMM elements for the
 * cfg80211 evidence line, and reports signal 0 mBm unless a bench caller
 * explicitly opts into the trailer heuristic - it never guesses a
 * firmware-specific footer field.
 */
#ifndef HI5622_VAPSCAN_MAP_H
#define HI5622_VAPSCAN_MAP_H

#include "hi5622_frames_api.h"
#include "vapscan_frames.h"

/* Provider identity shown in dmesg/status. */
#define HI5622_VAPSCAN_PROVIDER_NAME	"vapscan-0e4ad45"

/* Map the vs_err space to negative Linux errnos. */
int hi5622_vapscan_errno(int vs_err);

/* neutral params -> complete HCC wire frame in *f (task-110 builders). */
int hi5622_vapscan_scan_req(struct vs_frame *f,
			    const struct hi5622_scan_req_params *p);
int hi5622_vapscan_user_add(struct vs_frame *f,
			    const struct hi5622_vap_add_params *p);
int hi5622_vapscan_user_alg(struct vs_frame *f,
			    const struct hi5622_vap_add_params *p);
int hi5622_vapscan_user_del(struct vs_frame *f,
			    const struct hi5622_vap_del_params *p);

/* Vendor-verified D2H decoders (wifi-scan-d2h lane, task-118, commit 469b193)
 * wrapped for the provider contract.  These replace this lane's earlier
 * 'trim 20 bytes from the end' heuristic; the decoder probes the layout
 * (802.11 body + 20-byte trailer / bare frame / prefix variants) and returns
 * -EOPNOTSUPP instead of guessing. */
int hi5622_vapscan_decode_scan_result(const u8 *payload, u32 len,
				      struct hi5622_scan_result *out);
/* Same decoder with the trailer-RSSI opt-in.  The 20-byte descriptor's signed
 * byte +5 is the vendor host's signal-like read [I]; it is NOT used unless
 * trust_trailer_signal is set (bench correlation, scan-complete REPORT sec
 * 4.4).  Default callers use the wrapper above (trust = false). */
int hi5622_vapscan_decode_scan_result_opts(const u8 *payload, u32 len,
						   struct hi5622_scan_result *out,
						   bool trust_trailer_signal);
int hi5622_vapscan_decode_scan_complete(const u8 *payload, u32 len,
					struct hi5622_scan_complete *out);
int hi5622_vapscan_decode_scan_channel(const u8 *payload, u32 len,
				       struct hi5622_scan_channel *out);
int hi5622_vapscan_decode_scan_sta_complete(const u8 *payload, u32 len,
					    struct hi5622_scan_sta_complete *out);

#ifdef __KERNEL__
/* Kernel adapter: register the task-110 builders as the module's single frame
 * provider.  Idempotent (two PCI endpoints call it); 0 = registered/already. */
int hi5622_vapscan_provider_register(void);
#endif

#endif /* HI5622_VAPSCAN_MAP_H */
