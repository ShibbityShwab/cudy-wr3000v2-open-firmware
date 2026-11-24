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
 * 802.11 beacon/probe-response frame followed by a ~20-byte descriptor whose
 * exact layout is [U] (task-110 REPORT sec 6): the decoder strips 20 bytes,
 * validates the 802.11 management frame, derives the channel from the DS/HT
 * operation IE and reports signal 0 mBm (UNKNOWN placeholder) - it never
 * guesses a firmware-specific footer field.
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

/* Tolerant g1 id15 (hmac_scan_proc_scanned_bss) decoder: payload = 802.11
 * beacon/probe-response frame + trailing 20-byte host descriptor [U].
 * Returns 0 and fills *out, -EOPNOTSUPP when the body is not a decodable scan
 * result (unknown footer/variant), -EINVAL on a malformed body.  signal_mbm
 * is 0 (UNKNOWN placeholder) because the descriptor layout is [U]. */
int hi5622_vapscan_parse_scan_result(const u8 *payload, u32 len,
				     struct hi5622_scan_result *out);

#ifdef __KERNEL__
/* Kernel adapter: register the task-110 builders as the module's single frame
 * provider.  Idempotent (two PCI endpoints call it); 0 = registered/already. */
int hi5622_vapscan_provider_register(void);
#endif

#endif /* HI5622_VAPSCAN_MAP_H */
