/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_pciet.h — register/window contract of the Hi5622V100 PCIe/ETE
 * transport, written as ORIGINAL code by the wifi-transport-port lane from
 * the project's decoded interface contract (host-side, read-only reversal).
 *
 * Contract sources (see ../REPORT.md for the full evidence ledger):
 *   wifi-driver-re/REPORT.md     — mailbox/doorbell write order, D2H dispatch,
 *                                  HCC header, ETE channel register map
 *   wifi-firmware-re/REPORT.md   — firmware upload, CPU release, 0xcece gate,
 *                                  boot handshake, dev_status_check table
 *   phase18/inbound-map.md       — endpoint iATU viewport layout (BAR2 path)
 *   phase42/ete-registers-reconciled.md — ETE channel register offsets,
 *                                  device-side ring base window 0x8xxxxxxx
 *
 * No vendor source or object code is reproduced.  Every constant carries an
 * evidence marker: [V] re-verified from a named local artifact, [V*]
 * device-measured in an earlier attended lane and recorded in the named doc,
 * [I] inference, [U] unknown / not yet pinned.  "CA" = device address space.
 *
 * SAFETY CONTRACT (project hard rules, encoded in code):
 *   - device CA 0x400392f0 must NEVER be written (out[5]);
 *   - the RC "misc" window 0x10161000 is write-only and must never be read —
 *     this module never touches it (it belongs to the luofu host driver);
 *   - UART to the router is READ-ONLY; this module performs no UART I/O.
 */
#ifndef _HI5622_PCIET_H
#define _HI5622_PCIET_H

#include <linux/types.h>

/* ---------------------------------------------------------------------
 * 1. PCI identity of the endpoint (stock boot log, [V*])
 * ------------------------------------------------------------------- */
#define HI5622_VENDOR_ID		0x59e7
#define HI5622_DEVICE_ID		0x0005
#define HI5622_CLASS_WIFI		0x028000	/* class code [V*] */

/* Endpoint BARs as the vendor root complex assigns them ([V*]): */
#define HI5622_BAR_MEM_WIN		0	/* 16 MiB, the six inbound regions */
#define HI5622_BAR_IATU		2	/* 16 KiB, endpoint iATU viewports */
#define HI5622_BAR_MISC_WIN		4	/*  8 MiB, role [U] — not mapped */

/* ---------------------------------------------------------------------
 * 2. The six inbound regions of BAR0 ([V]/[V*], g_shuangta_region_types)
 *
 * Each region is programmed into one endpoint-iATU viewport (BAR2 path,
 * phase18 A.4-A.6): base/limit are BAR0-relative offsets, target is the
 * device CA.  The table order is the vendor's programming order (idx 0..5).
 * ------------------------------------------------------------------- */
struct hi5622_inbound_region {
	u32	bar_base;	/* BAR0-relative window base [V*] */
	u32	bar_limit;	/* BAR0-relative window limit (= base+size-1) [V*] */
	u32	ca_target;	/* device CA the window decodes to [V*] */
	u32	bar_size;	/* derived: limit - base + 1 */
};

static const struct hi5622_inbound_region hi5622_regions[6] = {
	/* idx 0: ROM_WRAM   — firmware image is byte-aliased here at +0x40000 */
	{ 0x0000000, 0x001bfff, 0x00000000, 0x1c0000 },
	/* idx 1: TCM_NOACP  */
	{ 0x01c0000, 0x01d7fff, 0x00400000, 0x18000 },
	/* idx 2: PKTRAM_NOACP */
	{ 0x01d8000, 0x03b7fff, 0x01000000, 0x1e0000 },
	/* idx 3: IO — the register/ETE/mailbox region (see below) */
	{ 0x03b8000, 0x04d7fff, 0x40000000, 0x120000 },
	/* idx 4: ACP       */
	{ 0x04d8000, 0x06b7fff, 0x02000000, 0x1e0000 },
	/* idx 5: ACP — firmware target CA 0x01240000 lives here */
	{ 0x06b8000, 0x08cffff, 0x01200000, 0x218000 },
};

#define HI5622_REGION_IO_BASE	0x3b8000u	/* [V*] */
#define HI5622_REGION_IO_MASK	0x000fffffu

/* BAR0 offset of a device CA in the IO region (region-3 rule, [V]): */
#define HI5622_BAR0_IO(ca) 	(HI5622_REGION_IO_BASE + ((ca) & HI5622_REGION_IO_MASK))

/* ---------------------------------------------------------------------
 * 3. Endpoint iATU viewports (BAR2 path, phase18 A.4-A.6, [V]/[V*])
 *
 * Viewport i register block: BAR2 + 0x104 + 0x200*i.  Register offsets
 * inside a block (config-space twins 0x908..0x91c shifted by -0x804):
 * ------------------------------------------------------------------- */
#define HI5622_IATU_VP0_OFF	0x104	/* first viewport block [V] */
#define HI5622_IATU_VP_STRIDE	0x200	/* [V] */
#define HI5622_IATU_NUM_VP	16	/* possible viewports [V] */

#define IATU_VP_CTRL2		0x00	/* 0 = disable; (bar&7)<<8 | BIT(31) [V] */
#define IATU_VP_BASE_LO		0x04	/* BAR0-relative base [V] */
#define IATU_VP_BASE_HI		0x08	/* [V] (zero) */
#define IATU_VP_LIMIT_LO	0x0c	/* base + size - 1 [V] */
/* phase18 A.5: this endpoint has NO limit_hi register; target_lo sits at +0x10. */
#define IATU_VP_TARGET_LO	0x10	/* device CA [V] (compact layout) */
#define IATU_VP_TARGET_HI	0x14	/* [V] (zero, compact layout) */

#define IATU_VP_CTRL2_ENA	0x80000000u	/* enable bit [V] */
#define IATU_VP_CTRL2_BAR(bar)	(((bar) & 7) << 8)	/* [V] */

/* ---------------------------------------------------------------------
 * 4. Mailboxes, doorbells, boot control words (region 3, [V*]/[V])
 * ------------------------------------------------------------------- */
/* H2D pending/message mask (out[0]); pcie_msg_send_irq writes 8 = id 3 */
#define HI5622_CA_H2D_PENDING	0x40039010	/* [V*] */
#define HI5622_BAR0_H2D_PENDING	HI5622_BAR0_IO(HI5622_CA_H2D_PENDING)

/* D2H message bitmap (out[1]); one bit per message id; host must drain to 0
 * before the device deposits the next word (device precondition P2, [V]) */
#define HI5622_CA_D2H_BITMAP	0x40039014	/* [V*] */
#define HI5622_BAR0_D2H_BITMAP	HI5622_BAR0_IO(HI5622_CA_D2H_BITMAP)

/* H2D doorbell (out[2]); the write that consumes the pending mask */
#define HI5622_CA_H2D_DOORBELL	0x400392d4	/* [V*] */
#define HI5622_BAR0_H2D_DOORBELL HI5622_BAR0_IO(HI5622_CA_H2D_DOORBELL)

/* out[5]: NEVER WRITE — asserted at build time and at runtime. */
#define HI5622_CA_NEVER_WRITE	0x400392f0	/* [V*] hard rule */

/* twin/ack word (copy B = copy A + 0x800); pcie_msg_send_irq sets bit 0
 * after the doorbell write.  Exact semantics [U]: the skeleton leaves this
 * write DISABLED until the first bench session pins it. */
#define HI5622_CA_TWIN_WORD	0x40039800	/* [V*]/[U] */
#define HI5622_BAR0_TWIN_WORD	HI5622_BAR0_IO(HI5622_CA_TWIN_WORD)

/* MAC-side message registers (out[3]/out[4]) — hdpp-slice consumers */
#define HI5622_CA_MAC_MSG_A	0x40101438	/* [V] */
#define HI5622_CA_MAC_MSG_B	0x40101414	/* [V] */

/* Device->host doorbell (bit 0); read-only for the host [V] */
#define HI5622_CA_D2H_DOORBELL	0x40101434	/* [V] */

/* Boot control word pair (firmware-re §2.2/§2.4): */
#define HI5622_CA_CPU_RELEASE	0x40000108	/* [V]/[V*] */
#define HI5622_BAR0_CPU_RELEASE	HI5622_BAR0_IO(HI5622_CA_CPU_RELEASE)
#define HI5622_CA_D2H_GATE	0x4000010c	/* [V] firmware-side precondition */
#define HI5622_BAR0_D2H_GATE	HI5622_BAR0_IO(HI5622_CA_D2H_GATE)

#define HI5622_CPU_RELEASE_MAGIC	0x00005a5au	/* [V] */
#define HI5622_D2H_GATE_MAGIC		0x0000ceceu	/* [V]; host write site [U] */

/* ---------------------------------------------------------------------
 * 5. Firmware upload contract (firmware-re §2.1, [V]/[V*])
 * ------------------------------------------------------------------- */
#define HI5622_FW_PATH		"hi_wifi/FIRMWARE.bin"	/* [V] vendor path */
#define HI5622_FW_TARGET_CA	0x01240000u		/* [V] */
#define HI5622_BAR0_FW_TARGET	0x6f8000u		/* BAR0+0x6f8000 [V*] */
#define HI5622_BAR0_FW_ALIAS	0x40000u		/* read-back alias [V*] */
#define HI5622_FW_CHUNK		0x80000u		/* 524288 B bounce [V] */
#define HI5622_FW_SIZE_STOCK	928920u		/* 0xE2C98 [V] */

/* ---------------------------------------------------------------------
 * 6. ETE ring engine (block CA 0x4003a000 = BAR0+0x3f2000, [V*])
 *
 * 7 channels x 0x50 bytes: SR0..SR2 (host->device, group A) and DR0..DR3
 * (device->host, group B).  Ring node = 8 bytes:
 *   word0 = device-side address of the host-DRAM buffer
 *   word1 = (len << 16) | flags, observed flags 0x6d2b = 0xd2b|0x2000|0x4000
 * ------------------------------------------------------------------- */
#define HI5622_CA_ETE_BASE	0x4003a000u	/* [V*] */
#define HI5622_BAR0_ETE_BASE	HI5622_BAR0_IO(HI5622_CA_ETE_BASE)

#define HI5622_ETE_SR0_OFF	0x400	/* [V*] */
#define HI5622_ETE_SR1_OFF	0x450	/* [V*] */
#define HI5622_ETE_SR2_OFF	0x4a0	/* [V*] */
#define HI5622_ETE_DR0_OFF	0x590	/* [V*] */
#define HI5622_ETE_DR1_OFF	0x5e0	/* [V*] */
#define HI5622_ETE_DR2_OFF	0x630	/* [V*] */
#define HI5622_ETE_DR3_OFF	0x680	/* [V*] */

/* per-channel register offsets (both families, phase42 E.2, [V*]) */
#define HI5622_ETE_CH_ENABLE	0x00	/* bit 0 = channel enable [V*] */
#define HI5622_ETE_CH_CTRL	0x08	/* control nibble [V*]/[U] */
#define HI5622_ETE_CH_A_BASE	0x10	/* group A: base / depth-1 / prod / cons */
#define HI5622_ETE_CH_A_DEPTH	0x14
#define HI5622_ETE_CH_A_PRD	0x18
#define HI5622_ETE_CH_A_CNS	0x1c
#define HI5622_ETE_CH_RESOURCE	0x28	/* resource word [V*]/[U] */
#define HI5622_ETE_CH_B_BASE	0x30	/* group B */
#define HI5622_ETE_CH_B_DEPTH	0x34
#define HI5622_ETE_CH_B_PRD	0x38
#define HI5622_ETE_CH_B_CNS	0x3c
#define HI5622_ETE_CH_ENABLE2	0x48	/* second enable [V*]/[U] */

/* SR uses group A for the host ring, DR uses group B (driver-re §4.1 [V]). */
#define HI5622_ETE_SR_GRP_A	1
#define HI5622_ETE_DR_GRP_B	1

#define HI5622_ETE_NODE_FLAGS	0x6d2bu		/* [V*] */
#define HI5622_ETE_NODE_LEN_SHIFT 16		/* [V] */

struct hi5622_ete_node {
	u32	devva;		/* device-side address of the buffer [V] */
	u32	word1;		/* (len << 16) | flags [V] */
};

/* Ring shape measured live on the vendor driver (phase42 E.3, [V*]):
 * base in the 0x80000000..0xffffffff device window (host DRAM), depth-1 = 0x1f,
 * indices <= 0x7ff carrying a 0x400 wrap phase bit. */
#define HI5622_ETE_DEPTH		32	/* [V*] */
#define HI5622_ETE_DEPTH_MASK		0x1f	/* [V*] */
#define HI5622_ETE_IDX_PHASE_BIT	0x400	/* [V*] */

/* ETE interrupt enable/mask word (CA 0x40039508); host-side mask 0xffe0f8f8,
 * live value 0x3f201818 [V*].  Read-only checkpoint; writing it is [U]. */
#define HI5622_CA_ETE_INTR		0x40039508u	/* [V*] */

/* ---------------------------------------------------------------------
 * 7. dev_status_check: the 11 read-only status words the vendor logs after
 * release (firmware-re §2.5, [V*]).  All reads through the BAR0 window.
 * ------------------------------------------------------------------- */
struct hi5622_dev_status_entry {
	const char	*name;
	u32		bar0_off;	/* BAR0 offset of the register */
};

static const struct hi5622_dev_status_entry hi5622_dev_status[11] = {
	{ "efuse_chip_id",    0x3b82a8 },	/* CA 0x400002A8 */
	{ "dcoldo_efuse",     0x3b82d4 },	/* CA 0x400002D4 */
	{ "dcoldo_vset",      0x3bd00c },	/* CA 0x4000500C */
	{ "pbank_code",       0x3bd05c },	/* CA 0x4000505C */
	{ "abank_code",       0x3bd060 },	/* CA 0x40005060 */
	{ "temp",             0x3fc110 },	/* CA 0x4003C110 */
	{ "lock_status",      0x3fc208 },	/* CA 0x4003C208 */
	{ "pcie0_status",     0x3f1224 },	/* CA 0x40039224 */
	{ "pcie0_latch",      0x3f1220 },	/* CA 0x40039220 */
	{ "tcxo_pll_mux_sel", 0x4b9230 },	/* CA 0x40101230 */
	{ "tcxo_pll_status",  0x4b9234 },	/* CA 0x40101234 */
};

/* ---------------------------------------------------------------------
 * 8. HCC message layer (driver-re §3.1, [V])
 * ------------------------------------------------------------------- */
#define HI5622_HCC_HDR_OBJ_OFF	0x118	/* header ptr offset in the host obj [V] */

/* Host-object header decode (wifi-driver-re §3.1 [V]): */
struct hcc_msg_header {
	u8	type;		/* +0x00; low 4 bits = group (0 chip, 1/2 core) */
	u8	bid;		/* +0x01; [7:4]=dst band, [3:0]=src band */
	u8	vap_id;		/* +0x02 */
	u8	chip_id;	/* +0x03 */
	u16	flag;		/* +0x04; nonzero => count stats */
	u16	id;		/* +0x06; message id, the dispatch key */
	u8	rsvd;		/* +0x08 */
	u8	dst;		/* +0x09; destination core/band */
	u8	params[];	/* +0x0a */
} __packed;

/* Boot-message wire reading (wifi-firmware-re §2.3 [V]): byte[1]>>4 = source
 * core, u16@+4 = length, byte[9] = core index, payload at +0xc.  The two
 * readings disagree on +4..+9 semantics; the bench capture arbitrates. */
#define HI5622_D2H_WIRE_PAYLOAD_OFF	0xc
#define HI5622_D2H_WIRE_READ_MSG_LEN	0x64	/* g_dmac_to_hmac_read_msg stride [V] */

/* Boot D2H message ids (chip table, .data+0x2660, driver-re §3.6 [V]): */
#define HI5622_MSG_PLAT_READY	1	/* "Device plat ready!" */
#define HI5622_MSG_HOST_READY	2	/* "DEVICE READY" */
#define HI5622_MSG_EXCP_INFO	3	/* exception_info_msg_process */
#define HI5622_MSG_HEARTBEAT	4	/* heartbeat_msg_process */
/* PCIe-layer ids registered by pcie_msg_init (driver-re §3.5 [V]):
 * 1 dev-ready stub, 3 ETE transfer done, 6/7 trigger ETE sending. */
#define HI5622_MSG_ETE_DONE	3

/* The D2H dispatch loop iterates bitmap bits 0..10 (pcie_msg_handle [V]). */
#define HI5622_D2H_MAX_ID	10

struct hi5622_pci;

/* ==== B3 (b3-instrument lane, task-66) addition: instrument state pointer.
 * The definition lives in pciet_b3.c; NULL in a build without
 * -DHI5622_B3_BUILD.  See hi5622_b3.h for the probe contract. ==== */
struct hi5622_b3;

struct hi5622_d2h_handler {
	void	(*func)(struct hi5622_pci *ctx, u32 id);	/* slot func [V] */
	u32	arg;						/* slot arg [V] */
};

/* ---------------------------------------------------------------------
 * 9. Shared driver state (host-only structures)
 * ------------------------------------------------------------------- */
struct hi5622_ete_ring {
	void			*node_arr;	/* host DRAM, depth*8 B */
	dma_addr_t		node_dma;	/* device-side address */
	void			**bufs;		/* per-node message buffers */
	dma_addr_t		*buf_dma;
	u32			depth;
	u32			producer;	/* software copy */
	u32			consumer;
	u32			chan_off;	/* ETE block channel offset */
	u32			buf_len;
	bool			enabled;
};

#define HI5622_ETE_BUF_LEN	2048	/* bench default, adjustable */

struct hi5622_pci {
	struct pci_dev		*pdev;
	void __iomem		*bar0;	/* 16 MiB six-region window */
	void __iomem		*bar2;	/* 16 KiB endpoint iATU */
	struct hi5622_ete_ring	sr0;	/* SR0 (group A) */
	struct hi5622_ete_ring	dr0;	/* DR0 (group B) */
	struct hi5622_d2h_handler d2h[HI5622_D2H_MAX_ID + 1];
	spinlock_t		doorbell_lock;	/* pcie_msg_send_irq spin [V] */
	u32			pending;	/* accumulated doorbell bitmap */
	struct completion	plat_ready;	/* id 1 */
	struct completion	host_ready;	/* id 2 */
	struct task_struct	*poll_thread;
	bool			fw_loaded;
	bool			cpu_released;
	bool			gate_announced;
	bool			windows_programmed;
	bool			twin_enabled;	/* [U] — off by default */
	u8			read_msg[2][HI5622_D2H_WIRE_READ_MSG_LEN];
	u32			d2h_count;	/* bench telemetry */

	/* ==== B3 (b3-instrument lane, task-66) addition: bench-instrument
	 * state (rings SR0-2/DR0-3, D2H event capture, DR snapshots, HCC decode).
	 * Allocated in pciet_b3_probe(); NULL when the module is built without
	 * -DHI5622_B3_BUILD. ==== */
	struct hi5622_b3	*b3;
};

/* pciet_main.c */
int  pciet_prepare_windows(struct hi5622_pci *ctx);
int  pciet_fw_upload(struct hi5622_pci *ctx);
void pciet_cpu_release(struct hi5622_pci *ctx);
void pciet_d2h_gate_announce(struct hi5622_pci *ctx);
void pciet_d2h_scan(struct hi5622_pci *ctx);
void pciet_d2h_register(struct hi5622_pci *ctx, u32 id,
			void (*func)(struct hi5622_pci *ctx, u32 id));
void pciet_msg_send_irq(struct hi5622_pci *ctx, u32 pending_mask);
int  pciet_msg_send(struct hi5622_pci *ctx, u32 id);
ssize_t pciet_dev_status_show(struct hi5622_pci *ctx, char *buf, size_t len);

/* pciet_ete.c */
int  pciet_ete_ring_alloc(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
			  u32 chan_off, u32 buf_len);
void pciet_ete_ring_free(struct hi5622_pci *ctx, struct hi5622_ete_ring *r);
int  pciet_ete_ring_init(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
			 bool use_grp_a);
int  pciet_ete_sr_send(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
		       const void *data, u32 len);
int  pciet_ete_dr_consume(struct hi5622_pci *ctx, struct hi5622_ete_ring *r,
			  void (*cb)(struct hi5622_pci *ctx, void *va, u32 len));

#endif /* _HI5622_PCIET_H */
