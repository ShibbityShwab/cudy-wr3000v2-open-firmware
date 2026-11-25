// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_he_caps.c - portable HE capability contract / wiphy policy / HE
 * Capabilities element builder (Hi5622V100 clean-room, Cudy WR3000 v2.0,
 * Linux 6.18).
 *
 * ORIGINAL clean-room code (lane wifi-ax-he-caps, shared task task-177).
 * Derivation and evidence: ../REPORT.md, ../TABLES.md (T3 = the decoded
 * vendor wiphy values, T8 = the hostapd gate, T10 = the expected element
 * bytes).  No vendor source/object/firmware byte is reproduced here.
 *
 * This translation unit is compiled twice from the same source: into the
 * kernel module (the driver calls the fill/policy functions) and into the
 * host selftest (src/he_caps_selftest.c).  Nothing here touches hardware,
 * firmware or allocation.
 */
#include "hi5622_he_caps.h"

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

/* The canonical little-endian byte serialisation of one HE-MCS/NSS word. */
static void hic_put_le16(u8 *p, u16 v)
{
	p[0] = (u8)(v & 0xffu);
	p[1] = (u8)((v >> 8) & 0xffu);
}

int hi5622_he_caps_policy(unsigned int he_caps)
{
	if (he_caps == HI5622_HE_CAPS_OFF)
		return (int)HI5622_HE_CAPS_OFF;
	if (he_caps == HI5622_HE_CAPS_ON)
		return (int)HI5622_HE_CAPS_ON;
	/* Never guess: an out-of-contract value is refused at init. */
	return -EINVAL;
}

static const u8 hic_mac_cap[HI5622_HE_MAC_CAP_LEN] = HI5622_HE_MAC_CAP_INIT;
static const u8 hic_phy_cap_2g[HI5622_HE_PHY_CAP_LEN] = HI5622_HE_PHY_CAP_2G_INIT;
static const u8 hic_phy_cap_5g[HI5622_HE_PHY_CAP_LEN] = HI5622_HE_PHY_CAP_5G_INIT;
static const u16 hic_mcs_2g[6] = HI5622_HE_MCS_2G_INIT;
static const u16 hic_mcs_5g[6] = HI5622_HE_MCS_5G_INIT;

int hi5622_he_caps_fill(u8 band, struct hi5622_he_caps *out)
{
	const u8 *phy;
	const u16 *mcs;
	unsigned int i;

	if (!out)
		return -EINVAL;
	memset(out, 0, sizeof(*out));
	if (band == 0u) {
		phy = hic_phy_cap_2g;
		mcs = hic_mcs_2g;
	} else if (band == 1u) {
		phy = hic_phy_cap_5g;
		mcs = hic_mcs_5g;
	} else {
		return -EINVAL;	/* fail closed; blob stays zeroed */
	}
	out->has_he = 1u;
	out->types_mask = (u16)HI5622_HE_TYPES_MASK_AP;
	memcpy(out->mac_cap, hic_mac_cap, sizeof(out->mac_cap));
	memcpy(out->phy_cap, phy, sizeof(out->phy_cap));
	for (i = 0; i < 6u; i++)
		out->he_mcs[i] = mcs[i];
	/* PPE thresholds: none advertised (the PPE-present PHY bit is clear),
	 * so the whole array is zero via the memset above. */
	out->he_6ghz_capa = 0x0000u;	/* no 6 GHz radio on this silicon */
	return 0;
}

const char *hi5622_he_caps_band_name(u8 band)
{
	return band == 1u ? "5GHz" : "2.4GHz";
}

unsigned int hi5622_he_caps_mcs_len(u8 band)
{
	struct hi5622_he_caps caps;
	unsigned int len = 4u;

	if (hi5622_he_caps_fill(band, &caps) < 0)
		return 0u;
	/* hostapd rule (src/ap/ieee802_11_he.c ieee80211_he_mcs_set_size):
	 * 4 B for 80 MHz, +4 B for 160 MHz, +4 B for 80+80 MHz. */
	if (caps.phy_cap[0] & HI5622_HE_PHY_CAP0_160MHZ_IN_5G)
		len += 4u;
	if (caps.phy_cap[0] & HI5622_HE_PHY_CAP0_80PLUS80_IN_5G)
		len += 4u;
	return len;
}

int hi5622_he_caps_ie_len(u8 band)
{
	unsigned int mcs;

	mcs = hi5622_he_caps_mcs_len(band);
	if (!mcs)
		return -EINVAL;
	/* EID + len + ext-id + (mac + phy + mcs); ppet_size is 0 because the
	 * PPE-present bit is clear. */
	return (int)(3u + HI5622_HE_CAP_ELEM_LEN + mcs);
}

int hi5622_he_caps_build_ie(u8 band, u8 *out, size_t cap)
{
	struct hi5622_he_caps caps;
	unsigned int mcs_len, ielen, pos, i;
	int rc;

	if (!out)
		return -EINVAL;
	rc = hi5622_he_caps_fill(band, &caps);
	if (rc)
		return rc;
	mcs_len = hi5622_he_caps_mcs_len(band);
	if (!mcs_len)
		return -EINVAL;
	ielen = 3u + HI5622_HE_CAP_ELEM_LEN + mcs_len;
	if (cap < ielen)
		return -ENOSPC;
	/* Layout (802.11ax): EID 255, length, extension id 35, MAC 6, PHY 11,
	 * HE-MCS/NSS set (4 or 8 B; 80+80 never advertised by this contract). */
	pos = 0u;
	out[pos++] = (u8)HI5622_HE_EID_EXT;
	out[pos++] = (u8)(1u + HI5622_HE_CAP_ELEM_LEN + mcs_len);
	out[pos++] = (u8)HI5622_HE_EXT_ID_CAP;
	memcpy(out + pos, caps.mac_cap, sizeof(caps.mac_cap));
	pos += sizeof(caps.mac_cap);
	memcpy(out + pos, caps.phy_cap, sizeof(caps.phy_cap));
	pos += sizeof(caps.phy_cap);
	for (i = 0u; i < mcs_len / 2u; i++, pos += 2u)
		hic_put_le16(out + pos, caps.he_mcs[i]);
	return (int)ielen;
}
