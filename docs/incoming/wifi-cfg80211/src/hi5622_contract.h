/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_contract.h - single-source hardware/protocol contract for the
 * Hi5622V100 clean-room PCIe/ETE transport and host-side HCC service.
 *
 * ORIGINAL clean-room code for the Cudy WR3000 v2.0 / HiSilicon luofu board.
 * The contract below is transcribed from the project's own decoded evidence
 * (the incoming lane reports and phase docs under opensource/docs).  No
 * vendor source text, object code, firmware byte, calibration value or
 * secret is included.
 *
 * Evidence markers: [V] re-verified from a named artifact, [V*] measured by an
 * attended bench lane, [I] inference, [U] unknown/not pinned.
 *
 * HARD SAFETY RULES ENCODED HERE (project-wide):
 *   - device CA 0x400392F0 (out[5]) is NEVER written;
 *   - the boot-stage handshake word CA 0x4000010C must keep the mask-ROM 0xCEED
 *     value until CPU1's S1 stage completes (handshake page +0x8 == 2); the
 *     image's exception handler wants 0xCECE there only after boot;
 *   - the RC "misc" window 0x10161000 is never read or written;
 *   - only the six vendor inbound regions are host-visible; every CA -> BAR0
 *     translation below is range-checked and unmapped CAs are refused (the
 *     event controller at CA 0x40160000 is NOT host-visible - the historic
 *     "IRQ probe" read wrapped into CA 0x4006xxxx; fixed in this driver);
 *   - UART to the router is read-only; this driver performs no UART I/O.
 */
#ifndef _HI5622_CONTRACT_H
#define _HI5622_CONTRACT_H

#include <linux/types.h>

/* =====================================================================
 * 1. PCI identity and BARs ([V*] stock enumeration)
 * ===================================================================*/
#define HI5622_VENDOR_ID		0x59e7
#define HI5622_DEVICE_ID		0x0005
#define HI5622_CLASS_WIFI		0x028000

#define HI5622_BAR_MEM_WIN		0	/* 16 MiB: six inbound regions [V*] */
#define HI5622_BAR_IATU			2	/* 16 KiB: endpoint iATU [V*] */
#define HI5622_BAR_MISC_WIN		4	/* 8 MiB: role [U], never mapped */

/* =====================================================================
 * 2. The six inbound regions (g_shuangta_region_types, [V]/[V*])
 *
 * Each region is one endpoint-iATU viewport: BAR0-relative base/limit ->
 * device CA target.  Programming order is the vendor order (idx 0..5).
 * ===================================================================*/
struct hi5622_inbound_region {
	u32	bar_base;	/* BAR0-relative window base */
	u32	bar_limit;	/* BAR0-relative window limit (base + size - 1) */
	u32	ca_target;	/* device CA the window decodes to */
};

#define HI5622_NUM_REGIONS	6

/* Region indices used by the code. */
#define HI5622_REGION_ROM_WRAM		0	/* CA 0x00000000 */
#define HI5622_REGION_TCM_NOACP		1	/* CA 0x00400000 */
#define HI5622_REGION_PKTRAM_NOACP	2	/* CA 0x01000000 */
#define HI5622_REGION_IO		3	/* CA 0x40000000 */
#define HI5622_REGION_ACP		4	/* CA 0x02000000 */
#define HI5622_REGION_FW_RAM		5	/* CA 0x01200000 */

extern const struct hi5622_inbound_region hi5622_regions[HI5622_NUM_REGIONS];

/* BAR0 offset of the IO region (region 3, CA 0x40000000) */
#define HI5622_IO_BAR_BASE		0x3b8000u
#define HI5622_IO_CA_LO			0x40000000u
#define HI5622_IO_CA_HI			0x4011ffffu	/* region 3 size 0x120000 */

/* Correct region-3 device-CA -> BAR0 translation (constant-foldable):
 * BAR0 = 0x3b8000 + (CA - 0x40000000), valid for CA in the region-3 range.
 * The old transport macro 0x3b8000 + (CA & 0xfffff) silently mis-mapped
 * out[3]/out[4] (CA 0x40101438/0x40101414 -> BAR0 0x4b9438/0x4b9414, NOT
 * 0x3b9438/0x3b9414; ete/hcc-core lane finding F4).  Every mailbox / D2H /
 * out[N] / ETE CA below is range-checked against region 3 at compile time in
 * hi5622_transport_assert_contract(). */
#define HI5622_BAR0_CA(ca) 	(HI5622_IO_BAR_BASE + ((ca) - HI5622_IO_CA_LO))

/* Host BAR0 offset of the firmware image RAM: BAR0 = 0x6b8000 + CPU address
 * (region 5 window base 0x6b8000 targets CA 0x01200000; the firmware's CPU
 * view of that RAM starts at 0).  Valid for CPU 0x0..0x217fff. */
#define HI5622_FW_RAM_BAR_BASE		0x6b8000u
#define HI5622_FW_RAM_CPU_LO		0x00100000u	/* image/g records [V] */
#define HI5622_FW_RAM_CPU_HI		0x0217ffffu	/* region 5 size 0x218000 */

/* Translation helpers (pure; unusable CAs return 0 and must be checked by
 * the caller).  Implemented in hi5622_driver.h as static inlines. */

/* =====================================================================
 * 3. Endpoint iATU viewports (BAR2 path, phase18 A.4-A.6, [V]/[V*])
 * ===================================================================*/
#define HI5622_IATU_VP0_OFF		0x104u
#define HI5622_IATU_VP_STRIDE		0x200u
#define HI5622_IATU_MAX_VP		16

#define IATU_VP_CTRL2			0x00
#define IATU_VP_BASE_LO			0x04
#define IATU_VP_BASE_HI			0x08
#define IATU_VP_LIMIT_LO		0x0c
/* This endpoint has NO limit_hi: target_lo sits at +0x10 (compact layout). */
#define IATU_VP_TARGET_LO		0x10
#define IATU_VP_TARGET_HI		0x14

#define IATU_VP_CTRL2_ENA		0x80000000u
#define IATU_VP_CTRL2_BAR(bar)		(((bar) & 7) << 8)

/* =====================================================================
 * 4. Mailbox, doorbells and boot control words (region 3, [V*]/[V])
 * ===================================================================*/
/* out[0] H2D pending/message mask; pcie_msg_send_irq writes BIT(id) */
#define HI5622_CA_H2D_PENDING		0x40039010u
/* out[1] D2H message bitmap; one bit per message id, host drains to 0 */
#define HI5622_CA_D2H_BITMAP		0x40039014u
/* out[2] H2D doorbell (bit 0); the host's last write of the send path */
#define HI5622_CA_H2D_DOORBELL		0x400392d4u
/* out[3] D2H ack: host writes 1 after taking the bitmap [V] */
#define HI5622_CA_D2H_ACK		0x40101438u
/* out[4] D2H re-arm: host writes 1 to accept the next bitmap [V] */
#define HI5622_CA_D2H_REARM		0x40101414u
/* out[5]: NEVER WRITE.  Read-only status word for the bench. */
#define HI5622_CA_NEVER_WRITE		0x400392f0u
/* twin/ack word (copy B = copy A + 0x800); semantics [U], unused here */
#define HI5622_CA_TWIN_WORD		0x40039800u
/* device->host doorbell (bit 0, read-only for the host) */
#define HI5622_CA_D2H_DOORBELL		0x40101434u

/* CPU release word (host writes 0x5A5A once) and the dual-use gate word:
 * 16-bit ROM boot handshake 0xCEED vs 32-bit image handler spin 0xCECE. */
#define HI5622_CA_CPU_RELEASE		0x40000108u
#define HI5622_CA_GATE			0x4000010cu
#define HI5622_CPU_RELEASE_MAGIC	0x00005a5au	/* release-only: ALWAYS */
#define HI5622_GATE_CEED		0x0000ceedu	/* ROM boot stage value [V] */
#define HI5622_GATE_CECE		0x0000ceceu	/* image handler value, post-boot only [V] */

/* =====================================================================
 * 5. Firmware upload contract (firmware-re sec 2, [V]/[V*])
 * ===================================================================*/
#define HI5622_FW_PATH_DEFAULT		"hi_wifi/FIRMWARE.bin"
#define HI5622_FW_TARGET_CA		0x01240000u	/* [V] */
#define HI5622_FW_TARGET_BAR0		0x6f8000u	/* BAR0+0x6f8000 [V*] */
#define HI5622_FW_ALIAS_BAR0		0x40000u	/* read-back alias once running */
#define HI5622_FW_CHUNK			0x80000u	/* 512 KiB bounce [V] */
#define HI5622_FW_SIZE_STOCK		928920u		/* 0xE2C98 [V] */

/* =====================================================================
 * 6. Custom-block pre-seed (GP3..GP8; kprobe-verified pre-release order)
 *
 * The vendor host writes two configuration blocks into firmware RAM before
 * the release word: the platform descriptor (24 words / 0x60 B, TCM bounds +
 * boot config; entry A reads it) and the Wi-Fi PHY/radio custom block
 * (618 words / 0x9A8 B; vendor builds it from device INI data).  Layout:
 *   platform: CPU 0x1b2800 -> BAR0 0x86a800, 0x060 B (24 words)
 *   wifi:     CPU 0x1b2860 -> BAR0 0x86a860, 0x9a8 B (618 words)
 * The Wi-Fi block is vendor PHY/radio configuration: it is loaded from a
 * LOCAL firmware directory at bench time and is never committed (project
 * publication rule).  The platform descriptor is INI-derived and built in.
 * ===================================================================*/
#define HI5622_BLOCK_PLAT_CPU		0x001b2800u
#define HI5622_BLOCK_PLAT_BAR0		(HI5622_FW_RAM_BAR_BASE + HI5622_BLOCK_PLAT_CPU)
#define HI5622_BLOCK_PLAT_BYTES		0x60u
#define HI5622_BLOCK_PLAT_WORDS		(HI5622_BLOCK_PLAT_BYTES / 4)

#define HI5622_BLOCK_WIFI_CPU		0x001b2860u
#define HI5622_BLOCK_WIFI_BAR0		(HI5622_FW_RAM_BAR_BASE + HI5622_BLOCK_WIFI_CPU)
#define HI5622_BLOCK_WIFI_BYTES		0x9a8u
#define HI5622_BLOCK_WIFI_WORDS		(HI5622_BLOCK_WIFI_BYTES / 4)

#define HI5622_WIFI_BLOCK_PATH_DEFAULT	"hi_wifi/wifi_block.bin"

/* =====================================================================
 * 7. ROM/image handshake page and post-boot state objects
 *
 * Handshake page CA 0x001bf000 -> BAR0 0x877000.  Golden signature (GP8):
 *   +0x00 = 0x86807fef, +0x04 = 0x00000007, +0x08 = 0x00000002,
 *   +0x0c = 0x0000abcd, +0x10 = 0xe2f16f38 (contract)
 * +8 == 2 is CPU1's S1 ack; page +0 bit14 marks entry A's completion.
 * ===================================================================*/
#define HI5622_HANDSHAKE_CPU		0x001bf000u
#define HI5622_HANDSHAKE_BAR0		(HI5622_FW_RAM_BAR_BASE + HI5622_HANDSHAKE_CPU)
#define HI5622_HANDSHAKE_ACK		2u
#define HI5622_HANDSHAKE_MAGIC		0x0000abcdu
#define HI5622_HANDSHAKE_GOLDEN0	0x86807fefu

/* Post-boot firmware state objects (CPU -> BAR0 = 0x6b8000 + CPU) */
#define HI5622_CPU_BOOT_STATE		0x00118b60u	/* 1 = boot task ran [V*] */
#define HI5622_BAR0_BOOT_STATE		(HI5622_FW_RAM_BAR_BASE + HI5622_CPU_BOOT_STATE)
#define HI5622_CPU_BOOT_SLOT		0x0017d42cu	/* FIQ one-shot slot [V] */
#define HI5622_BAR0_BOOT_SLOT		(HI5622_FW_RAM_BAR_BASE + HI5622_CPU_BOOT_SLOT)
#define HI5622_CPU_G_OBJECT		0x0010c0f4u	/* message object pointer [V] */
#define HI5622_CPU_G_ARMED		0x0010c1a8u	/* g+0xB4 in-flight word [V] */
#define HI5622_CPU_G_PENDING		0x0010c1acu	/* g+0xB8 deferred shadow [V] */
#define HI5622_CPU_EXCP_MASK		0x00040008u	/* exception-seen mask [V*] */
#define HI5622_BAR0_EXCP_MASK		(HI5622_FW_RAM_BAR_BASE + HI5622_CPU_EXCP_MASK)

/* =====================================================================
 * 8. ETE ring engine (block CA 0x4003a000 -> BAR0 0x3f2000, [V*])
 *
 * 7 channels x 0x50 B: SR0..SR2 host->device group A, DR0..DR3
 * device->host group B.  Node = 8 B: {device VA of buffer, (len<<16)|0x6d2b}.
 * ===================================================================*/
#define HI5622_CA_ETE_BASE		0x4003a000u
#define HI5622_ETE_BAR0			(HI5622_IO_BAR_BASE + (HI5622_CA_ETE_BASE - HI5622_IO_CA_LO))

#define HI5622_ETE_SR0_OFF		0x400u
#define HI5622_ETE_SR1_OFF		0x450u
#define HI5622_ETE_SR2_OFF		0x4a0u
#define HI5622_ETE_DR0_OFF		0x590u
#define HI5622_ETE_DR1_OFF		0x5e0u
#define HI5622_ETE_DR2_OFF		0x630u
#define HI5622_ETE_DR3_OFF		0x680u
#define HI5622_ETE_CH_STRIDE		0x50u

#define HI5622_ETE_CH_ENABLE		0x00	/* bit 0 = enable (firmware-owned) */
#define HI5622_ETE_CH_CTRL		0x08	/* control nibble (vendor RMW) */
#define HI5622_ETE_CH_A_BASE		0x10	/* group A: base/depth-1/prod/cons */
#define HI5622_ETE_CH_A_DEPTH		0x14
#define HI5622_ETE_CH_A_PRD		0x18
#define HI5622_ETE_CH_A_CNS		0x1c
#define HI5622_ETE_CH_RESOURCE		0x28
#define HI5622_ETE_CH_B_BASE		0x30	/* group B: base/depth-1/pub/fill */
#define HI5622_ETE_CH_B_DEPTH		0x34
#define HI5622_ETE_CH_B_PRD		0x38
#define HI5622_ETE_CH_B_CNS		0x3c

#define HI5622_ETE_DEPTH		32
#define HI5622_ETE_DEPTH_MASK		0x1fu
#define HI5622_ETE_IDX_LOW_MASK		0x3ffu
#define HI5622_ETE_IDX_PHASE_BIT	0x400u
#define HI5622_ETE_NODE_FLAGS		0x6d2bu
#define HI5622_ETE_NODE_LEN_SHIFT	16

/* host-facing ETE interrupt word (RMW &= 0xffe0f8f8 at arm, [V]) */
#define HI5622_CA_ETE_INTR		0x40039508u
#define HI5622_ETE_INTR_KEEP		0xffe0f8f8u
/* D2H status path (device-authored) and device send words (read-only) */
#define HI5622_CA_ETE_STATUS		0x4003a874u
#define HI5622_ETE_SEND_DR0		0xc00u
#define HI5622_ETE_SEND_SR0		0xc18u

/* =====================================================================
 * 9. dev_status_check read-outs (firmware-re sec 2.5, [V*])
 * ===================================================================*/
#define HI5622_NUM_DEV_STATUS	11
struct hi5622_dev_status_entry {
	const char	*name;
	u32		bar0_off;
};
extern const struct hi5622_dev_status_entry hi5622_dev_status[HI5622_NUM_DEV_STATUS];

/* =====================================================================
 * 10. HCC message layer ([V], wifi-driver-re sec 3, wifi-ete-frames sec 1)
 *
 * 12-byte header: +0 group (low nibble), +1 rsrc/dst bands, +4 u16 total
 * length (payload + 12, must be > 0), +6 u16 message id, +9 dst, +0xa tag
 * 0x5a5a.  The D2H bitmap dispatches ids 0..10 lowest-first.
 * ===================================================================*/
#define HI5622_HCC_HDR_LEN		12
#define HI5622_HCC_TAG			0x5a5au
#define HI5622_HCC_TAG_OFF		0x0au
#define HI5622_HCC_LEN_OFF		0x04
#define HI5622_HCC_ID_OFF		0x06
#define HI5622_D2H_MAX_ID		10

/* chip-group-4 boot/exception catalogue (device->host ready pair + logs) */
#define HI5622_G4_ID_OAM_LOG		0u
#define HI5622_G4_ID_PLAT_READY		1u	/* "Device plat ready!" */
#define HI5622_G4_ID_DEV_READY		2u	/* "DEVICE READY" (4 B payload) */
#define HI5622_G4_ID_EXCP_INFO		3u	/* "DEVICE EXCP_INFO" */
#define HI5622_G4_ID_HEARTBEAT		4u

/* D2H bitmap bits (pcie_msg_handle table) */
#define HI5622_D2H_BIT_DEV_READY_STUB	1
#define HI5622_D2H_BIT_EXCP_PANIC	2	/* 0x86E14 startup/vector announce */
#define HI5622_D2H_BIT_ETE_DONE		3
#define HI5622_D2H_BIT_PM_SLEEP_DISALLOW 4
#define HI5622_D2H_BIT_PM_WAKE_OK	5
#define HI5622_D2H_BIT_ETE_NOTIFY	6	/* deposit op d2h_notify */
#define HI5622_D2H_BIT_ETE_SENDING	7

/* H2D mailbox ids (never post id 5 = firmware BUG() trap) */
#define HI5622_H2D_MSG_ETE_ANNOUNCE	3
#define HI5622_H2D_MSG_POST_GLOBAL	1
#define HI5622_H2D_MSG_BUGON		5

/* H2D frame safety bounds (firmware tables: group 0 and 2 only) */
#define HI5622_HCC_GROUP_PLAT		4u
#define HI5622_HCC_SR_GROUP_SAFE	0u
#define HI5622_HCC_SR_ID_SAFE		0u
#define HI5622_HCC_FRAME_MAX_PAYLOAD	0x674u
#define HI5622_HCC_FRAME_MAX_TOTAL	(HI5622_HCC_FRAME_MAX_PAYLOAD + HI5622_HCC_HDR_LEN)
#define HI5622_HCC_DR_PAYLOAD_CAP	0x63u

/* vendor ready waits (HZ=100 both trees): id1 <= 2.0 s, id2 <= 20.0 s */
#define HI5622_READY_PLAT_MS		2000u
#define HI5622_READY_DEV_MS		20000u

/* =====================================================================
 * 11. Direct device group-A D2H receive path (wifi-d2h-direct, [V]/[V*])
 *
 * The firmware stages completed HCC frames as 8-byte nodes in the DEVICE
 * group-A DR rings inside PKTRAM and the frame bodies in device ACP SRAM.
 * The ETE copy engine that would mirror them into host DRAM stays idle in
 * our reachable boot, so the host reads both structures directly:
 *   node array : group A base (+0x10) in PKTRAM (region 2)
 *   frame body : node.word0, ACP alias 0x02xxxxxx (region 4)
 *   producer   : firmware record+0xc packed shadow (region 5)
 * ===================================================================*/
#define HI5622_D2H_DR_COUNT		4
#define HI5622_D2H_DR_CHAN_CA0		0x4003a590u
#define HI5622_D2H_DR_SEND_CA0		0x4003ac00u

/* firmware per-channel D2H record (static image data) [V] */
#define HI5622_D2H_REC_CPU0		0x0010f2b8u
#define HI5622_D2H_REC_STRIDE		0x354u
#define HI5622_D2H_REC_OFF_CFG		0x008u
#define HI5622_D2H_REC_OFF_PROD		0x00cu
#define HI5622_D2H_REC_OFF_PROGRESS	0x010u
#define HI5622_D2H_REC_OFF_CHAN_CA	0x31cu
#define HI5622_D2H_REC_OFF_SEND_CA	0x32cu
#define HI5622_D2H_REC_OFF_TX_CNT	0x340u
#define HI5622_D2H_REC_OFF_REL_CNT	0x34cu

/* PKTRAM window (region 2): BAR0 = 0x1d8000 + (CA - 0x01000000) */
#define HI5622_PKTRAM_BAR_BASE		0x001d8000u
#define HI5622_PKTRAM_CA_LO		0x01000000u
#define HI5622_PKTRAM_CA_HI		0x011dffffu
/* ACP window (region 4): BAR0 = 0x4d8000 + (CA - 0x02000000) */
#define HI5622_ACP_BAR_BASE		0x004d8000u
#define HI5622_ACP_CA_LO		0x02000000u
#define HI5622_ACP_CA_HI		0x021dffffu

/* host-DRAM window the ETE engine addresses (DTS dma-ranges identity) */
#define HI5622_HOST_DMA_LO		0x80000000u
#define HI5622_HOST_DMA_HI		0x8fffffffu

/* node/depth bounds for the direct reader */
#define HI5622_NODE_FLAGS_MASK		0x1fffu
#define HI5622_D2H_MAX_DEPTH		32
#define HI5622_D2H_MAX_TOTAL		HI5622_HCC_FRAME_MAX_TOTAL
#define HI5622_D2H_SNAP_MAX		96
#define HI5622_D2H_PAYLOAD_SNAP		48
#define HI5622_D2H_LOG_ENTRIES		16
#define HI5622_D2H_SEEN_ENTRIES		24
#define HI5622_D2H_STALL_MAX		200

#endif /* _HI5622_CONTRACT_H */
