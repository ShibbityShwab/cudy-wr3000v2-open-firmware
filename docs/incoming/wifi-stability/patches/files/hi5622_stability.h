/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_stability.h - pure, host-testable policy gates added by the
 * wifi-stability lane (shared task task-136, Cudy WR3000 v2.0 RE project).
 *
 * ORIGINAL clean-room code.  No device access, no vendor material.
 *
 * The AP-start gate exists because a scan can post the mode-1 VAP base
 * (vap_start_first=1) and a later interface delete/re-add keeps the firmware
 * VAP state stale while cfg80211 starts from a clean wdev: the measured
 * failure is hostapd "Failed to set beacon parameters" / "Could not connect
 * to kernel driver" after a scan -> iface del -> __ap add -> start_ap
 * sequence.  Each refusal below is a fail-closed, documented state, never a
 * fabricated success.
 *
 * Host test: cc -DHI5622_STABILITY_HOST -I<dir> <test>.c (see
 * opensource/docs/incoming/wifi-stability/tools/hardening_selftest.c).
 */
#ifndef _HI5622_STABILITY_H
#define _HI5622_STABILITY_H

#ifdef HI5622_STABILITY_HOST
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
#else
#include <linux/types.h>
#endif

/* NL80211_IFTYPE_AP (the vendor AP path is iftype 3; cf. hi5622_scan.c). */
#define HI5622_STAB_IFTYPE_AP	3u

/* AP-start gate results.  0 = allowed. */
enum hi5622_stab_ap_gate {
	HI5622_STAB_AP_OK = 0,
	HI5622_STAB_AP_ALREADY_STARTED,	/* one committed start per load */
	HI5622_STAB_AP_SEQ_DIRTY,	/* aborted sequence posted frames */
	HI5622_STAB_AP_BASE_WRONG_IFTYPE, /* base posted for a non-AP iftype */
};

/*
 * Gate one cfg80211 start_ap request.  Inputs are the per-load latches in
 * struct hi5622_pci (all false/0 on a fresh module load).
 *
 *   ap_started     - a previous start_ap sequence committed this load
 *   ap_seq_dirty   - a previous sequence aborted after >= 1 frame was posted
 *   base_posted    - the mode-1 VAP base frame pair was posted this load
 *   base_iftype    - the iftype that base was posted for (0 = never posted)
 *
 * A wrong-iftype base is refused because the firmware VAP iftype is fixed by
 * the base message; re-posting it host-side would be a speculative duplicate.
 */
static inline enum hi5622_stab_ap_gate
hi5622_stab_ap_gate(bool ap_started, bool ap_seq_dirty,
		    bool base_posted, u8 base_iftype)
{
	if (ap_started)
		return HI5622_STAB_AP_ALREADY_STARTED;
	if (ap_seq_dirty)
		return HI5622_STAB_AP_SEQ_DIRTY;
	if (base_posted && base_iftype != HI5622_STAB_IFTYPE_AP)
		return HI5622_STAB_AP_BASE_WRONG_IFTYPE;
	return HI5622_STAB_AP_OK;
}

#endif /* _HI5622_STABILITY_H */
