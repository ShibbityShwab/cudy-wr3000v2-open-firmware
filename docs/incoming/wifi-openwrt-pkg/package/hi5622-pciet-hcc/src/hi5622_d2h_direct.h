/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_d2h_direct.h - direct-read D2H receive service for the Hi5622V100
 * clean-room PCIe/ETE transport (device group-A DR rings read through the
 * host BAR0 windows; independent of the ETE engine).
 *
 * ORIGINAL code by the wifi-d2h-direct lane (task-89, 2026-11-24), written
 * from the project's decoded contract.  No vendor source, object code,
 * firmware byte, calibration value or secret is reproduced; every constant
 * below is a derived address/value with an evidence marker.
 *
 * Contract sources (committed lane reports under opensource/docs/incoming/):
 *   wifi-ete-frames/    12-byte HCC header (group low nibble at +0, u16 total
 *                       at +4, u16 id at +6, dst at +9, transport tag 0x5A5A
 *                       at +0xA), node word1 = (len << 16) | 0x6D2B
 *   wifi-ready-emit/    D2H deposit op CPU 0xC61E0 -> ring commit CPU 0xC192A;
 *                       the two rings per channel (group A device source ring
 *                       in PKTRAM, group B host destination ring), +0x38
 *                       published free index (host-owned), +0x3C fill
 *   wifi-hcc-re/        D2H handler catalogue per chip group/id; mailbox
 *                       ack/drain/re-arm order
 *   wifi-fw-postboot/   corrected post-handshake state (scheduler/GIC armed;
 *                       the missing transition is downstream), the boot task's
 *                       pass-1 id1/id3 send continuation and id2 phase gates
 *   wifi-hcc-service/   the existing host service this file extends; its DR
 *                       receive path assumed engine-copied host-DRAM rings
 *   ledger 2026-11-24 12:20 (hcc bench) and 13:10 (D2H frames decoded)
 *
 * NEW evidence taken for this lane directly from the local vendor firmware
 * image (read in place, derived annotations only):
 *   [V] pcie_msg_init CPU 0x49334 programs each DR channel (CA
 *       0x4003A590/0x5E0/0x630/0x680): +0x10 = node-array base (= the
 *       per-channel record word0: 0x01060440/0x01060330/0x01060220/
 *       0x01060110), +0x14 = depth-1 (0x1F), +0x18 = producer copy (0),
 *       +0x08 ctrl, +0x00 |= 1; it NEVER writes the +0x1C consumer.
 *   [V] the D2H send path CPU ~0xC67A4 tags the frame, resolves the buffer
 *       through CPU 0xC1D88 (PKTRAM CA + 0x1000000 -> ACP CA) and calls the
 *       deposit op CPU 0xC61E0 via ops+0x10; the deposit op commits the node
 *       through CPU 0xC192A, which stores {word0, word1} at
 *       node[producer & 0x3FF] in the device ring and advances the PACKED
 *       producer shadow at record+0xC; then d2h_notify(6) and record+0x340++.
 *   [V] the device ring node array is therefore record+0 = the +0x10 register
 *       value; the per-channel control/handshake word is at base + 0x100
 *       (record+0x14 = node_size*depth + base; the sending trigger CPU 0xC625A
 *       writes the u16 target there and re-reads it). The host must NEVER
 *       write base + 0x100.
 *   [V] the firmware DR records are static image data: base CPU 0x10F2B8 +
 *       0x354*ch; word0 = node-array CA; +0x08 = cfg pointer (runtime);
 *       +0xC = packed producer shadow; +0x31C = channel CA; +0x32C = send
 *       register CA; +0x340 = tx counter; +0x34C = released counter.
 *   [V*] the 2026-11-24 13:10 bench read the live device rings: DR3
 *       (0x01060110) held 12 nodes, DR0 (0x01060440) held 4 nodes of len 16;
 *       frame bodies were readable through the region-4 ACP window at the
 *       node word0 (CA 0x0204xxxx = ACP alias of the PKTRAM message pool).
 *
 * Address conventions (project-wide, unchanged):
 *   firmware CPU address -> host BAR0 = 0x6B8000 + CPU (region 5 ACP alias of
 *   the firmware image RAM); device IO CA -> 0x3B8000 + (CA - 0x40000000)
 *   (region 3, only CA 0x40000000..0x4011FFFF); PKTRAM CA -> 0x1D8000 +
 *   (CA - 0x01000000) (region 2); ACP CA -> 0x4D8000 + (CA - 0x02000000)
 *   (region 4).
 *
 * SAFETY CONTRACT (project hard rules, encoded here):
 *   - CA 0x400392F0 (out[5]) is never written;
 *   - no firmware RAM / device RAM writes at all in the read path; the only
 *     device writes this service can perform are the vendor-owned DR +0x38
 *     host free index republish (explicit sysfs store) and, behind a default
 *     OFF module parameter, the experimental device-ring consumer +0x1C;
 *   - every write requires the golden handshake page +8 == 2;
 *   - never read the RC misc window 0x10161000; no UART I/O.
 */
#ifndef _HI5622_D2H_DIRECT_H
#define _HI5622_D2H_DIRECT_H

#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/types.h>

#include "hi5622_pciet.h"
#include "hi5622_hcc_service.h"

/* ---------------------------------------------------------------------
 * 1. Host BAR0 windows used by the direct-read path
 *
 * All six inbound regions are programmed by the base transport ('prepare');
 * the direct path only *reads* them.  Ranges are [lo, hi] inclusive.
 * ------------------------------------------------------------------- */
/* region 2: PKTRAM_NOACP - the device group-A DR ring node arrays [V*] */
#define HI5622_D2H_PKTRAM_CA_LO		0x01000000u
#define HI5622_D2H_PKTRAM_CA_HI		0x011dffffu
#define HI5622_D2H_PKTRAM_BAR0		0x001d8000u	/* bar_base [V] */

/* region 4: ACP SRAM - the device frame bodies (node word0 is the ACP alias
 * of a PKTRAM address: CPU 0xC1D88 adds 0x1000000) [V] */
#define HI5622_D2H_ACP_CA_LO		0x02000000u
#define HI5622_D2H_ACP_CA_HI		0x021dffffu
#define HI5622_D2H_ACP_BAR0		0x004d8000u	/* bar_base [V] */
#define HI5622_D2H_ACP_ALIAS_DELTA	0x01000000u	/* PKTRAM -> ACP [V] */

/* host DRAM window the ETE engine addresses (DTS dma-ranges identity) */
#define HI5622_D2H_HOST_DMA_LO		0x80000000u
#define HI5622_D2H_HOST_DMA_HI		0x8fffffffu

/* region 5: firmware image RAM - host BAR0 = 0x6B8000 + CPU [V] */
#define HI5622_D2H_FW_RAM_BAR0		0x006b8000u
#define HI5622_D2H_FW_RAM_CPU_LO	0x00100000u
#define HI5622_D2H_FW_RAM_CPU_HI	0x00120000u

/* ---------------------------------------------------------------------
 * 2. DR channel registers and firmware records
 *
 * Channel register block CA 0x4003A000 + {0x590,0x5E0,0x630,0x680}; field
 * offsets (project-wide contract, phase42 A.3 + pcie_msg_init): [V]
 *   +0x00 enable (firmware-owned; +0x00 |= 1 in pcie_msg_init)
 *   +0x08 ctrl   (firmware writes cfg[5]&7)
 *   +0x10 group A base = node-array CA      +0x14 group A depth-1
 *   +0x18 group A producer (firmware copy)  +0x1C group A consumer
 *   +0x30 group B base (host DRAM)          +0x34 group B depth-1
 *   +0x38 group B published free index      +0x3C group B fill (device)
 * The device writes +0x1C only through its ETE copy engine; the firmware
 * never writes it and the vendor host never writes it [V].  The vendor host
 * DOES write +0x38 (pcie_ete_dr_reg_init at probe, ctc[+0x1C]; the sending
 * trigger re-publishes it under lock) - it is the only host-owned DR index.
 * ------------------------------------------------------------------- */
#define HI5622_D2H_DR_COUNT		4
#define HI5622_D2H_DR_CHAN_CA0		0x4003a590u
#define HI5622_D2H_DR_CHAN_STRIDE	0x50u
#define HI5622_D2H_DR_SEND_CA0		0x4003ac00u	/* +4/ch, device-owned [V] */
#define HI5622_D2H_DR_SEND_STRIDE	4u

/* firmware per-channel D2H record (static image data) [V] */
#define HI5622_D2H_REC_CPU0		0x0010f2b8u
#define HI5622_D2H_REC_STRIDE		0x354u
#define HI5622_D2H_REC_OFF_W0		0x000u	/* node-array CA (static) */
#define HI5622_D2H_REC_OFF_CFG		0x008u	/* cfg entry CPU ptr (runtime) */
#define HI5622_D2H_REC_OFF_PROD		0x00cu	/* packed producer shadow [V] */
#define HI5622_D2H_REC_OFF_PROGRESS	0x010u	/* send-reg lo16 copy */
#define HI5622_D2H_REC_OFF_CTLWORD	0x014u	/* base + 0x100: PKTRAM control
						 * word (device handshake; NEVER
						 * host-written) */
#define HI5622_D2H_REC_OFF_CHAN_CA	0x31cu	/* channel CA (static) */
#define HI5622_D2H_REC_OFF_SEND_CA	0x32cu	/* send reg CA (static) */
#define HI5622_D2H_REC_OFF_TX_CNT	0x340u	/* tx counter (runtime) */
#define HI5622_D2H_REC_OFF_REL_CNT	0x34cu	/* released counter (runtime) */

/* ---------------------------------------------------------------------
 * 3. Node + frame contract
 * ------------------------------------------------------------------- */
#define HI5622_D2H_NODE_FLAGS		0x6d2bu	/* low 16 of word1 [V] */
#define HI5622_D2H_NODE_LEN_SHIFT	16
#define HI5622_D2H_NODE_FLAGS_MASK	0x1fffu	/* word1[12:0] flags [+13/14
						 * present bits are set in 0x6d2b] */
#define HI5622_D2H_MAX_DEPTH		32	/* cfg depth, +0x14 = 0x1f [V] */
#define HI5622_D2H_MAX_TOTAL		HI5622_HCC_FRAME_MAX_TOTAL	/* 0x680 */
#define HI5622_D2H_HDR_LEN		HI5622_HCC_HDR_LEN		/* 12 */
#define HI5622_D2H_SNAP_MAX		96	/* header + up to 84 payload B */
#define HI5622_D2H_PAYLOAD_SNAP		48	/* per-frame payload bytes kept */
#define HI5622_D2H_LOG_ENTRIES		16	/* decoded-frame ring log */
#define HI5622_D2H_SEEN_ENTRIES		24	/* distinct (group,id) slots */
#define HI5622_D2H_STALL_MAX		200	/* polls stuck on one slot */
#define HI5622_D2H_READY_PAYLOAD_CAP	HI5622_HCC_DR_PAYLOAD_CAP	/* 0x63 */

/* chip-group-4 boot/exception catalogue (plat.ko table, hcc-re sec 5.5) [V] */
#define HI5622_D2H_G4_ID_OAM_LOG	0u	/* oam_rx_post_action_function */
#define HI5622_D2H_G4_ID_PLAT_READY	1u	/* device_plat_ready_msg_process */
#define HI5622_D2H_G4_ID_DEV_READY	2u	/* host_ready_msg_process */
#define HI5622_D2H_G4_ID_EXCP_INFO	3u	/* exception_info_msg_process */
#define HI5622_D2H_G4_ID_HEARTBEAT	4u	/* heartbeat_msg_process */
/* chip-group-3 ids seen/catalogued (wifi.ko hmac/hdpp tables) [V] */
#define HI5622_D2H_G3_ID_SMPS_CAP	9u	/* hmac_smps_update_device_capbility */
#define HI5622_D2H_G3_ID_PFM_MSG	10u	/* hmac_pfm_hiex_rx_local_msg */
#define HI5622_D2H_G3_ID_BSD_UPDATE	15u	/* hmac_bsd_update_msg */
#define HI5622_D2H_G3_ID_MULTIAP_11V	16u	/* hmac_multiap_report_11v_event */
#define HI5622_D2H_G3_ID_D2H_PM		17u	/* hmac_d2h_pm_event */
#define HI5622_D2H_G3_ID_TEMP_STATE	18u	/* hmac_d2h_temp_state_update */
#define HI5622_D2H_G3_ID_BEACON_REPROT	19u	/* hmac_receive_beacon_probrsp_to_reprot */
#define HI5622_D2H_G3_ID_BEACON_FRAME	20u	/* hmac_report_beacon_frame */

/* ---------------------------------------------------------------------
 * 4. Service state (embedded by value in struct hi5622_hcc_service)
 * ------------------------------------------------------------------- */
struct hi5622_d2h_chan {
	u32	base_ca;	/* group A node-array base (+0x10)	*/
	u32	depth;		/* depth (+0x14 + 1)			*/
	u32	prod_reg;	/* +0x18 as read (may stay 0: RAM shadow)	*/
	u32	cons_reg;	/* +0x1C as read (device/engine-owned)	*/
	u32	enable;		/* +0x00				*/
	u32	pub_reg;	/* +0x38 host free index (host-owned)	*/
	u32	fill_reg;	/* +0x3C device fill			*/
	u32	rec_cpu;	/* firmware record CPU address		*/
	u32	prod_shadow;	/* record+0xC packed producer (authority)	*/
	u32	prod;		/* producer used by this poll		*/
	u32	last;		/* host consumed baseline (packed)	*/
	u32	consumed;	/* nodes consumed since last republish	*/
	u32	overruns;	/* wrap-with-loss events		*/
	u32	bad_nodes;	/* invalid/skipped nodes			*/
	u32	stall;		/* consecutive polls stuck on one slot	*/
	bool	layout_ok;	/* record layout self-check passed	*/
	bool	prod_from_shadow;	/* producer source this poll	*/
	bool	ab_written;	/* experimental +0x1C write done	*/
};

struct hi5622_d2h_frame {
	u64	jiffies64;
	u8	ch;
	u8	group;
	u8	rsrc;
	u8	dst;
	u8	hdr[HI5622_D2H_HDR_LEN];
	u16	id;
	u16	total;
	u16	node_len;
	u32	node_ptr;
	u16	payload_len;
	u8	payload[HI5622_D2H_PAYLOAD_SNAP];
	bool	tag_ok;
	bool	len_ok;
	bool	ptr_ok;
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
	u8		id2_payload_raw[HI5622_D2H_READY_PAYLOAD_CAP];
	u16		id2_plen;
	u32		last_skip_boot;
	struct hi5622_d2h_chan	ch[HI5622_D2H_DR_COUNT];
	struct hi5622_d2h_frame	log[HI5622_D2H_LOG_ENTRIES];
	u32		log_pos;
	u32		log_count;
	struct hi5622_d2h_seen	seen[HI5622_D2H_SEEN_ENTRIES];
	u32		seen_count;
	u32		seen_other;
};

/* ---------------------------------------------------------------------
 * 5. Entry points (implemented in pciet_d2h_direct.c; wired into the
 *    wifi-hcc-service module by src/hcc-d2h-direct.patch)
 * ------------------------------------------------------------------- */
/* accessor implemented by the patched hcc service (the private hcc struct
 * lives in pciet_hcc_service.c; this keeps the direct path decoupled). */
struct hi5622_d2h_direct *pciet_hcc_rd(struct hi5622_pci *ctx);

void pciet_d2h_init(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd);
u32  pciet_d2h_poll(struct hi5622_pci *ctx, struct hi5622_d2h_direct *rd,
		    bool manual);
int  pciet_d2h_republish(struct hi5622_pci *ctx,
			 struct hi5622_d2h_direct *rd);
int  pciet_d2h_add_attrs(struct device *dev);
void pciet_d2h_del_attrs(struct device *dev);
int  pciet_d2h_ready_line(struct hi5622_pci *ctx,
			  struct hi5622_d2h_direct *rd, char *buf, size_t len);

#endif /* _HI5622_D2H_DIRECT_H */