/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_d2h.h - direct device group-A D2H receive service (engine-independent).
 *
 * ORIGINAL clean-room code.  The firmware stages completed HCC frames as
 * 8-byte nodes in the DEVICE group-A DR rings (PKTRAM) and the frame bodies in
 * device ACP SRAM; the ETE copy engine that would mirror them into host DRAM
 * stays idle in our reachable boot, so this service reads both structures
 * directly through the mapped devices windows.  See hi5622_contract.h section
 * 11 for the addresses and hi5622_d2h.c for the delivery/logic.
 *
 * Safety contract:
 *   - no firmware RAM or device RAM writes in the read path; all reads go
 *     through the mapped inbound windows;
 *   - the only device writes are the host-owned DR published-free index
 *     (+0x38, explicit sysfs store) and the default-OFF experimental device
 *     consumer (+0x1C, module param hi5622_rd_ab_write=1);
 *   - every write requires the golden boot handshake page +8 == 2;
 *   - the PKTRAM control word node-base+0x100 is never touched;
 *   - probe() performs no device writes.
 */
#ifndef _HI5622_D2H_H
#define _HI5622_D2H_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "hi5622_driver.h"

struct hi5622_pci;

/* ---------------------------------------------------------------------
 * Per-channel state of the direct reader
 * ------------------------------------------------------------------- */
struct hi5622_d2h_chan {
	u32	base_ca;	/* group A node-array base (+0x10) */
	u32	depth;		/* depth (+0x14 + 1) */
	u32	prod_reg;	/* +0x18 as read (copy, not authority) */
	u32	cons_reg;	/* +0x1C as read (device/engine-owned) */
	u32	enable;		/* +0x00 */
	u32	pub_reg;	/* +0x38 host free index (host-owned) */
	u32	fill_reg;	/* +0x3C device fill */
	u32	rec_cpu;	/* firmware record CPU address */
	u32	prod_shadow;	/* record+0xC packed producer (authority) */
	u32	prod;		/* producer used by this poll */
	u32	last;		/* host consumed baseline (packed) */
	u32	consumed;	/* nodes consumed since last republish */
	u32	overruns;	/* wrap-with-loss events */
	u32	bad_nodes;	/* invalid/skipped nodes */
	u32	stall;		/* consecutive polls stuck on one slot */
	bool	layout_ok;	/* record layout self-check passed */
	bool	prod_from_shadow;
	bool	ab_written;	/* experimental +0x1C write done */
};

struct hi5622_d2h_frame {
	u64	jiffies64;
	u8	ch;
	u8	group;
	u8	rsrc;
	u8	dst;
	u8	hdr[HI5622_HCC_HDR_LEN];
	u16	id;
	u16	total;
	u16	node_len;
	u32	node_ptr;
	u16	payload_len;
	u8	payload[HI5622_D2H_PAYLOAD_SNAP];
	bool	tag_ok;
	bool	len_ok;
	bool	known;
};

struct hi5622_d2h_seen {
	u8	group;
	u16	id;
	u32	count;
	u16	last_total;
	u16	last_len;
};

struct hi5622_d2h_direct {
	struct mutex	lock;		/* serializes poll/advance/attr reads */
	bool		init;
	u64		polls;
	u64		manual_polls;
	u64		nodes;
	u64		frames;
	u64		bad_tag;
	u64		bad_len;
	u64		bad_ptr;
	u64		bad_node;
	u64		overruns;
	u64		repub_count;
	u64		ab_writes;
	u64		ready_id1;
	u64		ready_id2;
	u64		excp_info;
	bool		id1_seen;
	bool		id2_seen;
	u32		id2_payload;		/* g4 id2 payload dword0 [V] */
	u8		id2_payload_raw[HI5622_HCC_DR_PAYLOAD_CAP];
	u16		id2_plen;
	u32		last_skip_boot;
	struct hi5622_d2h_chan	ch[HI5622_D2H_DR_COUNT];
	/* Full-frame staging for one node (serialized by ->lock): the scan
	 * decoder must see whole IEs, not the 48-byte diagnostic copy. */
	u8			snap[HI5622_D2H_SNAP_MAX];
	struct hi5622_d2h_frame	log[HI5622_D2H_LOG_ENTRIES];
	u32		log_pos;
	u32		log_count;
	struct hi5622_d2h_seen	seen[HI5622_D2H_SEEN_ENTRIES];
	u32		seen_count;
	u32		seen_other;
};

/* ---------------------------------------------------------------------
 * API (implemented in hi5622_d2h.c)
 * ------------------------------------------------------------------- */
void hi5622_d2h_init(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd);
u32  hi5622_d2h_poll(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd,
		     bool manual);
int  hi5622_d2h_republish(struct hi5622_pci *ctx,
			  struct hi5622_d2h_direct *rd);
int  hi5622_d2h_advance_ab(struct hi5622_pci *ctx,
			   struct hi5622_d2h_direct *rd);
int  hi5622_d2h_ready_line(struct hi5622_pci *ctx,
			   struct hi5622_d2h_direct *rd, char *buf, size_t len);
int  hi5622_d2h_add_attrs(struct device *dev);
void hi5622_d2h_del_attrs(struct device *dev);

#endif /* _HI5622_D2H_H */
