/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_hcc.h - host-side HCC service state and protocol-core glue for the
 * Hi5622V100 clean-room transport driver.
 *
 * ORIGINAL clean-room code.  The post-release protocol state machine is the
 * portable hcc_core library (wifi-hcc-core lane, baked into this tree as
 * hcc_core.c/h + hcc_core_kernel.c/h): latched boot gate, ready pair from
 * both receive paths, vendor-order mailbox, coalesced H2D, evidence-driven
 * CECE and the F1-F9 fix list are implemented and self-tested there.
 *
 * This header only owns the kernel-side service state: the ETE ring mirrors,
 * the ETE arm sequence, the engine-DR secondary path and the sysfs surface.
 */
#ifndef _HI5622_HCC_H
#define _HI5622_HCC_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "hi5622_driver.h"
#include "hi5622_d2h.h"
#include "hcc_core_kernel.h"

/* ETE channel sets (vendor arms all 7 at probe, before the release) */
#define HI5622_HCC_SR_CH_COUNT		3
#define HI5622_HCC_DR_CH_COUNT		4
#define HI5622_HCC_SR_BUF_LEN		2048	/* >= max HCC frame */
#define HI5622_HCC_DR_BUF_LEN		2048
#define HI5622_HCC_MAX_HIST		16

/* Maximum H2D liveness frames per module load (bench rule: one) */
#define HI5622_HCC_SR_ANNOUNCE_MAX	1u

struct hcc_ring {
	struct hi5622_ete_ring	ete;
	u32			fill_last;	/* +0x3C baseline (packed) */
	u32			pub;		/* +0x38 host free index (packed) */
};

struct hi5622_hcc_service {
	struct hi5622_pci	*ctx;
	struct hcc_kernel	hcc;		/* portable protocol core + adapter */
	struct hcc_ring		sr[HI5622_HCC_SR_CH_COUNT];
	struct hcc_ring		dr[HI5622_HCC_DR_CH_COUNT];
	struct mutex		lock;
	bool			armed;
	u32			prepost;
	struct task_struct	*loop;
	unsigned int		loop_ms;
	u32			sr_announce_count;	/* liveness frames this load */
	u64			sr_announce_ok;
	u64			sr_announce_timeout;
	u64			sr_announce_refused;

	/* h2d_send.c: the REAL post-ready H2D message path (g0 id1 TXQ config
	 * sync and the strict exact-frame grammar).  One frame per idea: the
	 * liveness stub above and these frames are mutually exclusive in a
	 * module load, and hcc_clear never resets the budget counters. */
	u32			h2d_frames_sent;	/* real frames committed */
	u32			h2d_txq_sync_sent;
	u32			h2d_ctx_create_sent;	/* ordered txq precondition */
	u32			h2d_txq_addr_sent;	/* g0 id1 key 0x0320 data-plane hand-off */
	u64			h2d_send_ok;		/* announce consumed */
	u64			h2d_send_timeout;	/* announce not consumed */
	u64			h2d_send_refused;
	u32			h2d_desync;		/* core refused after wire write */
	u32			h2d_send_last_out0;
	u32			h2d_send_max_polls;
	u32			h2d_last_msg;		/* enum h2d_msg */
	u32			h2d_last_gate;		/* enum h2d_gate */
	u32			h2d_last_total;		/* wire bytes of last frame */
	u32			h2d_last_node_len;	/* SR node buffer bytes of last frame */
	struct hi5622_d2h_direct rd;	/* primary (direct) receive path */
	bool			boot_logged;
};

/* ---------------------------------------------------------------------
 * hcc.c - protocol-core glue (init/tick/release/deliver, SR announce,
 * CECE kick, DR republish) and its write attributes
 * ------------------------------------------------------------------- */
int  hi5622_hcc_init(struct hi5622_pci *ctx);
void hi5622_hcc_note_release(struct hi5622_pci *ctx);
void hi5622_hcc_tick(struct hi5622_pci *ctx);
int  hi5622_hcc_deliver(struct hi5622_pci *ctx, enum hcc_src src,
			const u8 *hdr, const u8 *payload, u32 payload_avail,
			u32 wire_len);
int  hi5622_hcc_sr_announce(struct hi5622_pci *ctx, const char *buf);
int  hi5622_hcc_cece_kick(struct hi5622_pci *ctx);
int  hi5622_hcc_dr_republish(struct hi5622_pci *ctx);
int  hi5622_hcc_add_attrs(struct device *dev);
void hi5622_hcc_del_attrs(struct device *dev);

/* ---------------------------------------------------------------------
 * service.c - ETE rings/arm, engine-DR secondary path, service loop,
 * probe/remove and the read-only status surface
 * ------------------------------------------------------------------- */
int  hi5622_hcc_probe(struct hi5622_pci *ctx);
void hi5622_hcc_remove(struct hi5622_pci *ctx);
int  hi5622_service_add_attrs(struct device *dev);
void hi5622_service_del_attrs(struct device *dev);

#endif /* _HI5622_HCC_H */