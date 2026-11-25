// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_vap_index.c - fail-closed VAP/module-index contract for the
 * structural H2D g0-id1 key-0x0064 VAP add.  Original clean-room code; see
 * hi5622_vap_index.h for the derivation and the firmware evidence markers.
 */
#include "hi5622_vap_index.h"

int hi5622_vap_add_gate(unsigned int index, unsigned int start_module)
{
	if (index > HI5622_VAP_INDEX_MAX)
		return -EINVAL;
	if (index != start_module)
		return -EINVAL;
	return 0;
}

int hi5622_vap_add_plan_build(unsigned int index, unsigned int start_module,
			      unsigned int band, unsigned int nl80211_iftype,
			      struct hi5622_vap_add_plan *out)
{
	struct hi5622_vap_add_plan p;

	if (!out)
		return -EINVAL;
	if (band > 1u)
		return -EINVAL;
	if (nl80211_iftype != HI5622_VAP_IFTYPE_AP &&
	    nl80211_iftype != HI5622_VAP_IFTYPE_STA)
		return -EINVAL;
	if (hi5622_vap_add_gate(index, start_module))
		return -EINVAL;
	p.post = true;
	p.index = (u8)index;
	p.parent = (u8)index;		/* self-parent (bench-validated C2 shape) */
	p.field_b = (u8)index;		/* the dispatcher ctx must be the bind ctx */
	p.band = (u8)band;
	p.create_type = (nl80211_iftype == HI5622_VAP_IFTYPE_AP) ? 2u : 3u;
	*out = p;
	return 0;
}

const char *hi5622_vap_add_verdict(bool ctx_ok, u32 bandobj, u32 count)
{
	if (!ctx_ok)
		return "module ctx unresolved - the key-0x0064 bind cannot have run";
	if (!bandobj)
		return "ctx+0x4B8 is ZERO - the key-0x0064 bind did NOT run (check data[4]/hdr[3]/band/published)";
	if (count == 1u)
		return "bound, band VAP list count=1 - a later state[4]=0xFF is NOT a VAP-add failure";
	if (count == 0u)
		return "band VAP list EMPTY - the key-0x0064 worker stopped before the list add";
	return "band VAP list count>1 - CA 0x4AE7C needs exactly one entry";
}
