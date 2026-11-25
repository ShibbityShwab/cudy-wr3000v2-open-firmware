// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_chan_gate.c - decoded firmware channel-context semantics
 * (lane wifi-fw-chan-worker, task-180).  Pure C99; the derivation is in
 * ../REPORT.md, the evidence in ../TABLES.md.  No vendor byte is reproduced.
 */
#include "hi5622_chan_gate.h"

/* The firmware's per-band channel lists, read from the pinned image by
 * ../tools/fw_chan_worker_re.py --tables (asserted by --selftest):
 * band 0 table CA 0x117B48 (14 entries), band 1 table CA 0x117AD4 (29). */
static const u8 hi5622_chan_gate_band0_table[HI5622_CHAN_GATE_BAND0_COUNT] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14
};

static const u8 hi5622_chan_gate_band1_table[HI5622_CHAN_GATE_BAND1_COUNT] = {
	36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124,
	128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 184, 188, 192, 196
};

int hi5622_chan_gate_index(unsigned int band, unsigned int channel)
{
	const u8 *tab;
	unsigned int n, i;

	if (band == 0u) {
		tab = hi5622_chan_gate_band0_table;
		n = HI5622_CHAN_GATE_BAND0_COUNT;
	} else if (band == 1u) {
		tab = hi5622_chan_gate_band1_table;
		n = HI5622_CHAN_GATE_BAND1_COUNT;
	} else {
		return -1;
	}
	for (i = 0u; i < n; i++) {
		if ((unsigned int)tab[i] == channel)
			return (int)i;
	}
	return -1;
}

bool hi5622_chan_gate_expected_word(unsigned int band, unsigned int channel,
				    unsigned int field, u32 *word)
{
	int idx;

	if (!word || channel == 0u || channel > 0xffu || field > 0xffu)
		return false;
	idx = hi5622_chan_gate_index(band, channel);
	if (idx < 0)
		return false;
	*word = (u32)(channel & 0xffu) | ((u32)(band & 0xffu) << 8) |
		((u32)(field & 0xffu) << 16) | ((u32)idx << 24);
	return true;
}

bool hi5622_chan_gate_word_ready(u32 word, unsigned int band,
				 unsigned int channel)
{
	unsigned int field = (unsigned int)((word >> 16) & 0xffu);
	u32 want;

	if (!hi5622_chan_gate_expected_word(band, channel, field, &want))
		return false;
	return word == want;
}

bool hi5622_chan_gate_rf_ran(u32 module_type, u32 module_440,
			     u32 band_obj_ec8)
{
	/* CA 0x40684: type gate; CA 0x944DE: band 1 (already stored),
	 * module+0x440 == 1, band_object+0xEC8 == 1. */
	return module_type == 2u && module_440 == 1u && band_obj_ec8 == 1u;
}
