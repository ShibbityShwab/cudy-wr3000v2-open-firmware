// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_rx_diff.c - portable VENDOR-vs-OURS RX state parity tables and rules
 * (lane wifi-rx-vendor-diff, shared task task-162).  ORIGINAL clean-room code;
 * derived facts only (see hi5622_rx_diff.h and the lane REPORT.md).
 */
#include "hi5622_rx_diff.h"

#ifdef __KERNEL__
#include <linux/kernel.h>
#else
#include <stdio.h>
#endif

/* -------------------------------------------------------------------
 * 1a. Vendor stock config block CA 0x40040000 (machw array word 5)
 *
 * Source: committed register dump opensource/dumps/reg_all.txt (vendor
 * driver live, band 0) + the statically decoded clean-room 14-write model
 * (dp_rings.c).  "vendor never reads" words +0x4c/+0x50/+0x54 are 0 in the
 * stock dump, exactly as the vendor accessor census predicted.
 * ------------------------------------------------------------------- */
static const struct hi5622_rx_word rx_cfg_words[] = {
	{ 0x00u, 0x00000100u, HIRD_KIND_MATCH,   "tx_ba_info depth (init 0x100)" },
	{ 0x04u, 0x00000000u, HIRD_KIND_RANGE,   "tx_ba_info addr (host devVA)" },
	{ 0x10u, 0x00000000u, HIRD_KIND_RANGE,   "msdu_info_ring_ptr_table ACP" },
	{ 0x1cu, 0x00b0063cu, HIRD_KIND_MATCH,   "rx_norm_len[15:0]|small[31:16]" },
	{ 0x20u, 0x00000000u, HIRD_KIND_RANGE,   "rx norm free ring addr" },
	{ 0x24u, 0x00000000u, HIRD_KIND_RANGE,   "rx small free ring addr" },
	{ 0x28u, 0x00000000u, HIRD_KIND_RANGE,   "rx completion ring addr" },
	{ 0x2cu, 0x00000100u, HIRD_KIND_MATCH,   "rx norm ring depth 256" },
	{ 0x30u, 0x00000200u, HIRD_KIND_MATCH,   "rx small ring depth 512" },
	{ 0x34u, 0x00000fffu, HIRD_KIND_MATCH,   "rx completion depth 4095" },
	{ 0x38u, 0x00008000u, HIRD_KIND_DYNAMIC, "norm free-ring wptr (host)" },
	{ 0x3cu, 0x00008000u, HIRD_KIND_DYNAMIC, "small free-ring wptr (host)" },
	{ 0x40u, 0x00000000u, HIRD_KIND_DYNAMIC, "completion ring rptr (host)" },
	{ 0x44u, 0x00000000u, HIRD_KIND_DYNAMIC, "host MAC int status W1C" },
	{ 0x48u, 0xfffffcc1u, HIRD_KIND_MATCH,   "host MAC int mask" },
	{ 0x4cu, 0x00000000u, HIRD_KIND_UNKNOWN, "vendor never reads (stock 0)" },
	{ 0x50u, 0x00000000u, HIRD_KIND_UNKNOWN, "vendor never reads (stock 0)" },
	{ 0x54u, 0x00000000u, HIRD_KIND_UNKNOWN, "vendor never reads (stock 0)" },
	{ 0x58u, 0x00000000u, HIRD_KIND_UNKNOWN, "stock 0, writer [U]" },
	{ 0x5cu, 0x00000004u, HIRD_KIND_UNKNOWN, "stock 4, writer [U]" },
	{ 0x6cu, 0x00000004u, HIRD_KIND_UNKNOWN, "stock 4, writer [U]" },
};

/* -------------------------------------------------------------------
 * 1b. Vendor stock live bank CA 0x40050000 (machw array word 8)
 *
 * Source: the same committed stock dump.  Status/ring pointers are device
 * telemetry (dynamic); +0x14 and +0x24..+0x30 are NON-ZERO in the vendor idle
 * dump but have NO host-driver accessor in the 2.4.15/2.5.24 modules - i.e.
 * device-owned state.  They are the highest-value parity targets for the
 * "RX armed but silent" symptom: an all-zero read from our box would prove the
 * device-side RX/MAC state never reached the vendor stock shape.
 * ------------------------------------------------------------------- */
static const struct hi5622_rx_word rx_live_words[] = {
	{ 0x00u, 0x00000400u, HIRD_KIND_DYNAMIC, "host MAC int status (live)" },
	{ 0x04u, 0x00000000u, HIRD_KIND_UNKNOWN, "device-owned, stock 0" },
	{ 0x08u, 0x00000020u, HIRD_KIND_DYNAMIC, "norm free-ring device rptr" },
	{ 0x0cu, 0x00000020u, HIRD_KIND_DYNAMIC, "small free-ring device rptr" },
	{ 0x10u, 0x00000000u, HIRD_KIND_DYNAMIC, "completion ring device wptr" },
	{ 0x14u, 0x00200020u, HIRD_KIND_DEVICE,  "device-owned RX meta, stock 0x00200020" },
	{ 0x18u, 0x00000000u, HIRD_KIND_UNKNOWN, "device-owned, stock 0" },
	{ 0x1cu, 0x00000000u, HIRD_KIND_UNKNOWN, "device-owned, stock 0" },
	{ 0x20u, 0x00000000u, HIRD_KIND_UNKNOWN, "device-owned, stock 0" },
	{ 0x24u, 0x00800000u, HIRD_KIND_DEVICE,  "device-owned queue meta 0" },
	{ 0x28u, 0x01800800u, HIRD_KIND_DEVICE,  "device-owned queue meta 1" },
	{ 0x2cu, 0x02801000u, HIRD_KIND_DEVICE,  "device-owned queue meta 2" },
	{ 0x30u, 0x03801800u, HIRD_KIND_DEVICE,  "device-owned queue meta 3" },
	{ 0x34u, 0x00000000u, HIRD_KIND_UNKNOWN, "device-owned, stock 0" },
	{ 0x38u, 0x00000000u, HIRD_KIND_UNKNOWN, "device-owned, stock 0" },
	{ 0x3cu, 0x00000000u, HIRD_KIND_UNKNOWN, "device-owned, stock 0" },
	{ 0x40u, 0x00000000u, HIRD_KIND_UNKNOWN, "device-owned, stock 0" },
};

const rx_u16 hi5622_rx_live_extra_offsets[HIRD_LIVE_EXTRA_COUNT] = {
	0x14u, 0x24u, 0x28u, 0x2cu, 0x30u,
};

const struct hi5622_rx_word *hi5622_rx_cfg_words(unsigned int *count)
{
	if (count)
		*count = (unsigned int)(sizeof(rx_cfg_words) / sizeof(rx_cfg_words[0]));
	return rx_cfg_words;
}

const struct hi5622_rx_word *hi5622_rx_live_words(unsigned int *count)
{
	if (count)
		*count = (unsigned int)(sizeof(rx_live_words) / sizeof(rx_live_words[0]));
	return rx_live_words;
}

int hi5622_rx_parity_classify(const struct hi5622_rx_word *w,
			      rx_u32 actual, int range_ok,
			      enum hi5622_rx_parity *out)
{
	if (!w || !out)
		return HIL_RX_EARG;
	if (actual == 0xffffffffu) {
		*out = HIRD_PAR_READ_FAIL;
		return HIL_RX_OK;
	}
	switch (w->kind) {
	case HIRD_KIND_MATCH:
		*out = (actual == w->v) ? HIRD_PAR_MATCH : HIRD_PAR_MISMATCH;
		break;
	case HIRD_KIND_RANGE:
		*out = range_ok ? HIRD_PAR_RANGE_OK : HIRD_PAR_RANGE_BAD;
		break;
	case HIRD_KIND_DYNAMIC:
		*out = HIRD_PAR_DYNAMIC;
		break;
	case HIRD_KIND_DEVICE:
		*out = (actual != 0u) ? HIRD_PAR_DEVICE_OK : HIRD_PAR_DEVICE_ZERO;
		break;
	case HIRD_KIND_UNKNOWN:
	default:
		*out = (actual == w->v) ? HIRD_PAR_MATCH : HIRD_PAR_MISMATCH;
		break;
	}
	return HIL_RX_OK;
}

const char *hi5622_rx_parity_name(enum hi5622_rx_parity p)
{
	switch (p) {
	case HIRD_PAR_MATCH:		return "MATCH";
	case HIRD_PAR_MISMATCH:		return "MISMATCH";
	case HIRD_PAR_RANGE_OK:		return "RANGE_OK";
	case HIRD_PAR_RANGE_BAD:	return "RANGE_BAD";
	case HIRD_PAR_DYNAMIC:		return "DYNAMIC";
	case HIRD_PAR_READ_FAIL:	return "READ_FAIL";
	case HIRD_PAR_DEVICE_ZERO:	return "DEVICE_ZERO";
	case HIRD_PAR_DEVICE_OK:	return "DEVICE_OK";
	default:			return "?";
	}
}

/* -------------------------------------------------------------------
 * 2. Host-MAC interrupt status bits (vendor ISR shuangta_irq_host_mac_isr)
 * ------------------------------------------------------------------- */
static const char *const rx_status_bit_names[HIRD_STATUS_BITS] = {
	"forward-core",		/* 0  vendor helper 0x39558                */
	"forward-core",		/* 1  vendor helper 0x39558                */
	"rx-completion",	/* 2  the RX service branch                */
	"norm-ring",		/* 3  norm free-ring condition helper      */
	"small-ring",		/* 4  small free-ring condition helper     */
	"error-forward",	/* 5  error log + firmware forward         */
	"service-6",		/* 6  vendor service path                  */
	"service-7",		/* 7  vendor service path                  */
	"forward-core",		/* 8  vendor helper 0x3920c (16 B msg)     */
	"forward-core",		/* 9  vendor forward path                  */
	"forward-core",		/* 10 vendor forward path                  */
	"masked-11", "masked-12", "masked-13", "masked-14", "masked-15",
};

const char *hi5622_rx_status_bit_name(unsigned int bit)
{
	if (bit >= HIRD_STATUS_BITS)
		return NULL;
	return rx_status_bit_names[bit];
}

int hi5622_rx_status_decode(rx_u32 status, rx_u32 mask,
			    struct hi5622_rx_status_dec *out)
{
	rx_u32 forward = 0u;
	unsigned int i;

	if (!out)
		return HIL_RX_EARG;
	out->status = status;
	out->mask = mask;
	out->active = status & ~mask;
	out->rx_active = (out->active & (1u << HIRD_STATUS_RX_BIT)) ? 1u : 0u;
	for (i = 0; i < HIRD_STATUS_BITS; i++) {
		if (!(out->active & (1u << i)))
			continue;
		if (i == 0u || i == 1u || i == 8u || i == 9u || i == 10u)
			forward |= 1u << i;
	}
	out->forward_bits = forward;
	return HIL_RX_OK;
}

int hi5622_rx_status_bits_str(const struct hi5622_rx_status_dec *d,
			      char *buf, unsigned int cap)
{
	unsigned int i, used = 0, first = 1;
	int n;

	if (!d || !buf || cap == 0u)
		return HIL_RX_EARG;
	n = snprintf(buf, cap, "bits=");
	if (n < 0 || (unsigned int)n >= cap)
		return HIL_RX_EARG;
	used = (unsigned int)n;
	for (i = 0; i < HIRD_STATUS_BITS; i++) {
		if (!(d->active & (1u << i)))
			continue;
		n = snprintf(buf + used, cap - used, "%s%u", first ? "" : ",", i);
		if (n < 0 || (unsigned int)n >= cap - used)
			return HIL_RX_EARG;
		used += (unsigned int)n;
		first = 0;
	}
	if (first) {
		n = snprintf(buf + used, cap - used, "none");
		if (n < 0 || (unsigned int)n >= cap - used)
			return HIL_RX_EARG;
		used += (unsigned int)n;
	}
	n = snprintf(buf + used, cap - used, " (rx=%u fwd=0x%x)",
		     d->rx_active, d->forward_bits);
	if (n < 0 || (unsigned int)n >= cap - used)
		return HIL_RX_EARG;
	return (int)(used + (unsigned int)n);
}

/* -------------------------------------------------------------------
 * 3. Read-only completion observation
 * ------------------------------------------------------------------- */
int hi5622_rx_obs_entry(rx_u32 entry,
			const rx_u32 *norm_dma, rx_u32 ndepth,
			const rx_u32 *small_dma, rx_u32 sdepth,
			rx_u8 *ring_kind, rx_u32 *slot,
			rx_u32 *buf_dma)
{
	rx_u32 i;

	if (!ring_kind || !slot || !buf_dma)
		return HIL_RX_EARG;
	/* The completion entry is the 4-byte device VA of buffer+4 [V]. */
	if ((entry & (HIL_RX_SKB_WORD - 1u)) != 0u)
		return HIRD_OBS_BAD_ENTRY;
	if (norm_dma) {
		for (i = 0; i < ndepth; i++) {
			if ((rx_u32)(norm_dma[i] + HIL_RX_SKB_WORD) != entry)
				continue;
			*ring_kind = 0u;
			*slot = i;
			*buf_dma = norm_dma[i];
			return HIRD_OBS_FRAME;
		}
	}
	if (small_dma) {
		for (i = 0; i < sdepth; i++) {
			if ((rx_u32)(small_dma[i] + HIL_RX_SKB_WORD) != entry)
				continue;
			*ring_kind = 1u;
			*slot = i;
			*buf_dma = small_dma[i];
			return HIRD_OBS_FRAME;
		}
	}
	return HIRD_OBS_BAD_ENTRY;
}

/* -------------------------------------------------------------------
 * 4. Vendor RX-init program parity
 * ------------------------------------------------------------------- */
const struct hi5622_rx_word hi5622_rx_init_fixed[HIRD_INIT_FIXED_COUNT] = {
	{ 0x00u, 0x00000100u, HIRD_KIND_MATCH, "tx_ba_info_buff_depth=0x100" },
	{ 0x1cu, 0x00b0063cu, HIRD_KIND_MATCH, "rx norm/small buffer lengths" },
	{ 0x2cu, 0x00000100u, HIRD_KIND_MATCH, "rx norm free ring size=256" },
	{ 0x30u, 0x00000200u, HIRD_KIND_MATCH, "rx small free ring size=512" },
	{ 0x34u, 0x00000fffu, HIRD_KIND_MATCH, "rx completion ring size=4095" },
	{ 0x48u, 0xfffffcc1u, HIRD_KIND_MATCH, "host MAC interrupt mask init" },
};

/* Address-bearing program words: presence required, value range-checked by the
 * caller (dp_mac_program already refuses out-of-window addresses). */
static const rx_u16 rx_init_addr_offsets[] = {
	0x04u, 0x10u, 0x20u, 0x24u, 0x28u,
};
#define HIRD_INIT_ADDR_COUNT	5u

int hi5622_rx_init_program_check(const struct dp_mac_reg *regs,
				 unsigned int count,
				 struct hi5622_rx_init_check *out)
{
	unsigned int i, j;

	if (out) {
		out->missing_mask = 0u;
		out->fail_name = NULL;
		out->bad_value = 0u;
	}
	if (!regs || count == 0u)
		return HIL_RX_EARG;
	for (i = 0; i < HIRD_INIT_FIXED_COUNT; i++) {
		const struct hi5622_rx_word *w = &hi5622_rx_init_fixed[i];
		int found = 0;

		for (j = 0; j < count; j++) {
			if (regs[j].off != w->off)
				continue;
			if (regs[j].value != w->v) {
				if (out) {
					out->missing_mask |= 1u << i;
					out->fail_name = w->name;
					out->bad_value = regs[j].value;
				}
				return HIL_RX_EFORMAT;
			}
			found = 1;
			break;
		}
		if (!found) {
			if (out) {
				out->missing_mask |= 1u << i;
				out->fail_name = w->name;
			}
			return HIL_RX_EFORMAT;
		}
	}
	for (i = 0; i < HIRD_INIT_ADDR_COUNT; i++) {
		int found = 0;

		for (j = 0; j < count; j++) {
			if (regs[j].off == rx_init_addr_offsets[i]) {
				found = 1;
				break;
			}
		}
		if (!found) {
			if (out) {
				out->missing_mask |= 1u << (HIRD_INIT_FIXED_COUNT + i);
				out->fail_name = "ring/table address word missing";
			}
			return HIL_RX_EFORMAT;
		}
	}
	return HIL_RX_OK;
}
