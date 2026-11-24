/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_driver.h - shared driver state and transport/ETE API for the
 * Hi5622V100 clean-room Wi-Fi transport driver (Linux 6.18).
 *
 * ORIGINAL clean-room code; see hi5622_contract.h for provenance and the
 * hard safety rules.  This header only declares state/APIs; the protocol
 * contract lives in hi5622_contract.h.
 */
#ifndef _HI5622_DRIVER_H
#define _HI5622_DRIVER_H

#include <linux/bitops.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "hi5622_contract.h"

#define HI5622_DRV_NAME		"hi5622_wifi"
#define HI5622_FW_REC_LEN	0x64	/* g_dmac_to_hmac_read_msg stride [V] */

struct hi5622_hcc_service;

/* ---------------------------------------------------------------------
 * Outbound (SR) / inbound (DR) ETE ring, host-DRAM mirror.
 *
 * The node array and every message buffer are dma_alloc_coherent() so the
 * device-visible address comes from the DMA API (the vendor's host-VA ==
 * device-address identity is a 2 GiB userspace-split accident and is NOT
 * copied; this kernel is VMSPLIT_3G).
 * ------------------------------------------------------------------- */
struct hi5622_ete_ring {
	void			*node_arr;	/* depth * 8 B, host DRAM */
	dma_addr_t		node_dma;	/* device-visible node array address */
	void			**bufs;
	dma_addr_t		*buf_dma;
	u32			depth;
	u32			producer;
	u32			consumer;
	u32			chan_off;
	u32			buf_len;
	bool			enabled;
};

struct hi5622_ete_node {
	u32	devva;		/* device-visible address of the buffer */
	u32	word1;		/* (len << 16) | 0x6d2b */
};

/* ---------------------------------------------------------------------
 * Driver state.  The HCC service state (ready machine, rings, telemetry)
 * is owned by hi5622_hcc_service (hi5622_hcc.h); this is the PCI transport
 * core plus the boot-stage flags every subsystem gates on.
 * ------------------------------------------------------------------- */
struct hi5622_pci {
	struct pci_dev		*pdev;
	void __iomem		*bar0;	/* 16 MiB: six inbound regions */
	void __iomem		*bar2;	/* 16 KiB: endpoint iATU */
	struct hi5622_hcc_service *hcc;

	/* boot-stage state (each gate documented in hi5622_transport.c) */
	bool			windows_programmed;
	bool			fw_loaded;
	bool			fw_verified;
	bool			platform_seeded;
	bool			wifi_seeded;
	bool			blocks_seeded;
	bool			cpu_released;
	bool			gate_repair_done;
	size_t			fw_size;

	/* vendor host record g_dmac_to_hmac_read_msg[2] (0x64 B stride) */
	u8			read_msg[2][HI5622_FW_REC_LEN];

	/* doorbell send path serialization (H2D) */
	spinlock_t		doorbell_lock;
};

/* ---------------------------------------------------------------------
 * Address translation: device CA -> host BAR0 offset, range-checked.
 *
 * Only the six vendor inbound regions are host-visible.  Callers must test
 * the result (0 means "not mapped"; BAR0 offset 0 is the ROM_WRAM window
 * which this driver never reads).  The historic IRQ-probe bug - reading CA
 * 0x4016xxxx through the region-3 mask and silently wrapping into
 * CA 0x4006xxxx - is prevented by hi5622_reg_read_ca(), which refuses CAs
 * outside the region table.
 * ------------------------------------------------------------------- */
static inline u32 hi5622_ca_to_bar0(u32 ca)
{
	int i;

	for (i = 0; i < HI5622_NUM_REGIONS; i++) {
		const struct hi5622_inbound_region *r = &hi5622_regions[i];
		u32 size = r->bar_limit - r->bar_base + 1;

		if (ca >= r->ca_target && ca < r->ca_target + size)
			return r->bar_base + (ca - r->ca_target);
	}
	return 0;
}

/* IO-region subclass (region 3) used by the mailbox/ETE registers. */
static inline bool hi5622_io_ca_ok(u32 ca)
{
	return ca >= HI5622_IO_CA_LO && ca <= HI5622_IO_CA_HI;
}

static inline u32 hi5622_io_bar0(u32 ca)
{
	if (!hi5622_io_ca_ok(ca))
		return 0;
	return HI5622_IO_BAR_BASE + (ca - HI5622_IO_CA_LO);
}

/* Firmware-RAM CPU address -> BAR0 (region 5), range-checked. */
static inline u32 hi5622_fw_ram_bar0(u32 cpu)
{
	if (cpu > (HI5622_FW_RAM_CPU_HI - HI5622_FW_RAM_CPU_LO))
		return 0;
	return HI5622_FW_RAM_BAR_BASE + cpu;
}

/* ---------------------------------------------------------------------
 * Packed ring-index arithmetic [V] (pcie_ete_ring_ptr_plus/get_rls_num):
 *   idx low 10 bits = slot, bit 10 = wrap phase; count = depth*phase_delta +
 *   low delta.  Used by the ETE engine rings and by the direct reader.
 * ------------------------------------------------------------------- */
static inline u32 hi5622_idx_plus(u32 idx, u32 depth)
{
	u32 next = idx + 1;

	if ((next & HI5622_ETE_IDX_LOW_MASK) == depth) {
		next &= ~HI5622_ETE_IDX_LOW_MASK;
		next ^= HI5622_ETE_IDX_PHASE_BIT;
	}
	return next;
}

static inline u32 hi5622_idx_prev(u32 idx, u32 depth)
{
	u32 low = idx & HI5622_ETE_IDX_LOW_MASK;
	u32 phase = (idx & HI5622_ETE_IDX_PHASE_BIT) ? 1u : 0u;
	u32 hi = idx & ~(HI5622_ETE_IDX_LOW_MASK | HI5622_ETE_IDX_PHASE_BIT);

	if (low == 0) {
		low = depth - 1;
		phase ^= 1u;
	} else {
		low--;
	}
	return hi | (phase << 10) | low;
}

static inline u32 hi5622_idx_count(u32 from, u32 to, u32 depth)
{
	s32 phase = (s32)((to >> 10) & 1u) - (s32)((from >> 10) & 1u);
	s32 delta = phase * (s32)depth +
		    (s32)(to & HI5622_ETE_IDX_LOW_MASK) -
		    (s32)(from & HI5622_ETE_IDX_LOW_MASK);

	if (delta < 0)
		delta += 2 * (s32)depth;
	return (u32)delta;
}

static inline bool hi5622_idx_sane(u32 idx, u32 depth)
{
	return (idx & ~(HI5622_ETE_IDX_LOW_MASK | HI5622_ETE_IDX_PHASE_BIT)) == 0 &&
	       (idx & HI5622_ETE_IDX_LOW_MASK) < depth;
}

/* ---------------------------------------------------------------------
 * MODULE-PARAMS (defined in the C files that use them)
 * ------------------------------------------------------------------- */
extern char *hi5622_fw_path;			/* transport.c, 0444 */
extern char *hi5622_wifi_block_path;		/* transport.c, 0444 */
extern char *hi5622_plat_block_path;		/* transport.c, 0444 (override, empty = built-in) */
extern unsigned int hi5622_allow_unseeded_release; /* main.c, 0644 */
extern unsigned int hi5622_hcc_poll_ms;		/* service.c, 0644 */
extern unsigned int hi5622_cece_mode;		/* service.c, 0644 */
extern unsigned int hi5622_rd_ab_write;		/* d2h.c, 0644 */

/* ---------------------------------------------------------------------
 * transport.c - PCI transport core (iATU windows, firmware, boot)
 * ------------------------------------------------------------------- */
int  hi5622_transport_prepare_windows(struct hi5622_pci *ctx);
int  hi5622_transport_fw_upload(struct hi5622_pci *ctx);
int  hi5622_transport_fw_verify(struct hi5622_pci *ctx);
int  hi5622_transport_seed_blocks(struct hi5622_pci *ctx);
int  hi5622_transport_release(struct hi5622_pci *ctx);
int  hi5622_transport_gate_repair(struct hi5622_pci *ctx);
int  hi5622_transport_dev_status_show(struct hi5622_pci *ctx, char *buf,
				      size_t len);
u32  hi5622_transport_page(struct hi5622_pci *ctx, u32 off);
u32  hi5622_reg_read_ca(struct hi5622_pci *ctx, u32 ca);
u32  hi5622_fw_ram_read(struct hi5622_pci *ctx, u32 cpu);
void hi5622_hcc_note_release(struct hi5622_pci *ctx);	/* hcc.c: arm the core boot latch */
bool hi5622_boot_complete(struct hi5622_pci *ctx);
bool hi5622_golden_boot(struct hi5622_pci *ctx);
int  hi5622_transport_add_attrs(struct device *dev);
void hi5622_transport_del_attrs(struct device *dev);

/* ---------------------------------------------------------------------
 * ete.c - ETE ring helpers
 * ------------------------------------------------------------------- */
int  hi5622_ete_ring_alloc(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
			   u32 chan_off, u32 buf_len);
void hi5622_ete_ring_free(struct hi5622_pci *ctx, struct hi5622_ete_ring *r);
int  hi5622_ete_sr_post(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
		       u32 producer, const void *data, u32 len,
		       u32 *new_producer);

#endif /* _HI5622_DRIVER_H */
