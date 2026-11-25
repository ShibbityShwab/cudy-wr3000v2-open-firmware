/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_he_caps.h - the vendor-decoded HE (802.11ax) capability contract and
 * the fail-closed wiphy advertisement policy for the Hi5622V100 clean-room
 * driver (Cudy WR3000 v2.0, Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-ax-he-caps, shared task task-177,
 * 2026-11-25).  No vendor source, object code, firmware byte, calibration
 * value or secret is reproduced here; the derivation (vendor host-module
 * addresses and the decoded capability values) is in ../REPORT.md, the
 * evidence tables are ../TABLES.md (T3 = the capability values, T8 = the
 * hostapd gate, T10 = the exact element bytes).
 *
 * WHY THIS FILE EXISTS
 * --------------------
 * The shipped hostapd refuses `ieee80211ax=1` with "Driver does not support
 * HE" unless the wiphy advertises HE capabilities for the AP iftype
 * (hw_features.c hostapd_check_ht_capab).  The vendor driver advertises
 * exactly one ieee80211_sband_iftype_data entry per band, AP iftype only,
 * with sparse HE caps: HTC-HE MAC bit, PHY channel-width bits (2.4 GHz:
 * 40 MHz; 5 GHz: 40/80 + 160 MHz), 2 spatial streams MCS0-9, no PPE
 * thresholds, no 6 GHz capability (the chip has no 6 GHz radio).
 *
 * This header is the single source of truth for those values: the 6.18
 * kernel's static cfg80211 iftype entries AND the portable blob used by the
 * host selftest are both built from the macros below, so they cannot drift.
 * The driver additionally memcmp()s the static entry against the blob at
 * band-setup time and refuses the band on mismatch (fail closed).
 *
 * The advertisement is opt-in: he_caps=0 (default) leaves the wiphy without
 * any iftype data (byte-identical behaviour to the bench-validated module);
 * he_caps=1 advertises the contract for the installed bands; anything else
 * is refused at init (-EINVAL).  Never guess.
 *
 * The HE element / IE sizes are 802.11ax standard; the IE builder here emits
 * the exact HE Capabilities element hostapd will put in the beacon when the
 * wiphy advertises this contract (the fire recipe compares the on-air bytes
 * against it).
 *
 * Pure C99: the same header compiles in the kernel module and in the host
 * selftest.
 */
#ifndef HI5622_HE_CAPS_H
#define HI5622_HE_CAPS_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/errno.h>
#else
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#ifndef HI5622_HOST_TYPES_DEFINED
#define HI5622_HOST_TYPES_DEFINED
typedef uint8_t  u8;
typedef uint16_t u16;
typedef int32_t  s32;
#endif
#endif

/* Policy values for the he_caps module parameter. */
#define HI5622_HE_CAPS_OFF	0u
#define HI5622_HE_CAPS_ON	1u
#define HI5622_HE_CAPS_MAX	HI5622_HE_CAPS_ON

/* The vendor's iftype mask: AP only (NL80211_IFTYPE_AP == 3).  Kept as a
 * plain number so the host selftest does not need nl80211 headers. */
#define HI5622_HE_IFTYPE_AP	3u
#define HI5622_HE_TYPES_MASK_AP	(1u << HI5622_HE_IFTYPE_AP)

/* 802.11ax element sizes (standard). */
#define HI5622_HE_MAC_CAP_LEN	6
#define HI5622_HE_PHY_CAP_LEN	11
#define HI5622_HE_MCS_LEN	12
#define HI5622_HE_PPE_LEN	25
#define HI5622_HE_CAP_ELEM_LEN	(HI5622_HE_MAC_CAP_LEN + HI5622_HE_PHY_CAP_LEN)

#define HI5622_HE_EID_EXT	255u	/* WLAN_EID_EXTENSION */
#define HI5622_HE_EXT_ID_CAP	35u	/* WLAN_EID_EXT_HE_CAPABILITIES */
#define HI5622_HE_EXT_ID_OPER	36u	/* WLAN_EID_EXT_HE_OPERATION */

/* PHY cap byte0 channel-width bits (802.11ax; the only PHY byte the vendor
 * advertises non-zero). */
#define HI5622_HE_PHY_CAP0_40MHZ_IN_2G		0x02
#define HI5622_HE_PHY_CAP0_40_80MHZ_IN_5G	0x04
#define HI5622_HE_PHY_CAP0_160MHZ_IN_5G		0x08
#define HI5622_HE_PHY_CAP0_80PLUS80_IN_5G	0x10

/* HE-MCS/NSS map values: 0xfffa = 2 SS MCS0-9, 0xffff = not supported. */
#define HI5622_HE_MCS_2SS_0_9	0xfffau
#define HI5622_HE_MCS_NONE	0xffffu

/* =====================================================================
 * THE DECODED VENDOR CONTRACT - the single source of truth
 *
 * 2.4 GHz (vendor .rodata iftype entry for the 2 GHz sband):
 *   MAC  01 00 00 00 00 00        (HTC HE only)
 *   PHY  02 00 .. 00              (40 MHz in 2.4 GHz)
 *   MCS  80MHz 2SS MCS0-9, 160/80+80 not supported
 * 5 GHz:
 *   MAC  same
 *   PHY  0c 00 .. 00              (40/80 + 160 MHz in 5 GHz)
 *   MCS  80MHz and 160MHz 2SS MCS0-9, 80+80 not supported
 * Both: PPE thresholds all zero (PPE-present bit clear), HE 6GHz capa 0.
 * ===================================================================== */
#define HI5622_HE_MAC_CAP_INIT {					\
	0x01, 0x00, 0x00, 0x00, 0x00, 0x00				\
}

#define HI5622_HE_PHY_CAP_2G_INIT {					\
	HI5622_HE_PHY_CAP0_40MHZ_IN_2G, 0x00, 0x00, 0x00, 0x00,		\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00				\
}

#define HI5622_HE_PHY_CAP_5G_INIT {					\
	(HI5622_HE_PHY_CAP0_40_80MHZ_IN_5G |				\
	 HI5622_HE_PHY_CAP0_160MHZ_IN_5G),				\
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00	\
}

/* rx_mcs_80, tx_mcs_80, rx_mcs_160, tx_mcs_160, rx_mcs_80p80, tx_mcs_80p80 */
#define HI5622_HE_MCS_2G_INIT {						\
	HI5622_HE_MCS_2SS_0_9, HI5622_HE_MCS_2SS_0_9,			\
	HI5622_HE_MCS_NONE,    HI5622_HE_MCS_NONE,			\
	HI5622_HE_MCS_NONE,    HI5622_HE_MCS_NONE			\
}

#define HI5622_HE_MCS_5G_INIT {						\
	HI5622_HE_MCS_2SS_0_9, HI5622_HE_MCS_2SS_0_9,			\
	HI5622_HE_MCS_2SS_0_9, HI5622_HE_MCS_2SS_0_9,			\
	HI5622_HE_MCS_NONE,    HI5622_HE_MCS_NONE			\
}

/* The portable decoded blob (canonical little-endian semantics for the MCS
 * words; the IE builder writes them little-endian). */
struct hi5622_he_caps {
	u8	has_he;
	u16	types_mask;
	u8	mac_cap[HI5622_HE_MAC_CAP_LEN];
	u8	phy_cap[HI5622_HE_PHY_CAP_LEN];
	u16	he_mcs[6];
	u8	ppe_thres[HI5622_HE_PPE_LEN];
	u16	he_6ghz_capa;
};

/* he_caps parameter -> HI5622_HE_CAPS_OFF / HI5622_HE_CAPS_ON / -EINVAL.
 * Never guesses: anything above the decoded contract is refused. */
int hi5622_he_caps_policy(unsigned int he_caps);

/* Fill the decoded contract for one band (0 = 2.4 GHz, 1 = 5 GHz).  Zeroes
 * *out first; refuses any other band with -EINVAL (the blob then stays
 * zeroed). */
int hi5622_he_caps_fill(u8 band, struct hi5622_he_caps *out);

/* HE-MCS/NSS set length in the HE Capabilities element for the band's PHY
 * width bits (4 B for 2.4 GHz, 8 B for 5 GHz). */
unsigned int hi5622_he_caps_mcs_len(u8 band);

/* Total length of the HE Capabilities element hostapd emits for the band
 * (EID + len + ext-id + caps), or -EINVAL for an unknown band. */
int hi5622_he_caps_ie_len(u8 band);

/* Build the exact HE Capabilities element (T10 bytes) for the band into
 * `out` (capacity `cap`).  Returns the element length, -EINVAL / -ENOSPC.
 * Used by the host selftest and the fire recipe's expected-bytes check. */
int hi5622_he_caps_build_ie(u8 band, u8 *out, size_t cap);

/* Stable band name ("2.4GHz" / "5GHz"). */
const char *hi5622_he_caps_band_name(u8 band);

#endif /* HI5622_HE_CAPS_H */
