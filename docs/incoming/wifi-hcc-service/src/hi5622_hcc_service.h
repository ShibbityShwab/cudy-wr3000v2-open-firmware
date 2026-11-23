/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_hcc_service.h - host-side HCC service counterpart for the Hi5622V100
 * clean-room PCIe/ETE transport, on the CORRECTED boot protocol.
 *
 * ORIGINAL code by the wifi-hcc-service lane (task-87, 2026-11-24), written
 * from the project's decoded contract.  No vendor source, object code, firmware
 * byte, calibration value or secret is reproduced; every constant below is a
 * derived address/value with an evidence marker.
 *
 * Contract sources (committed lane reports under opensource/docs/incoming/):
 *   wifi-host-replica/  vendor post-release sequence, pcie_msg_handle algorithm
 *                       (ack out[3]=1 -> read out[1] -> drain -> re-arm out[4]=1
 *                       -> dispatch ids 0..10), D2H handler table {1,2,3,4,5,6,7},
 *                       ready waits 200/2000 jiffies, H2D id3 announce order
 *   wifi-ete-frames/    HCC 12-byte header, tag 0x5A5A at +0xA, length at +4,
 *                       device->host g4 id1/id2 wire forms, no H2D in the
 *                       release->ready window
 *   wifi-ready-emit/    d2h_notify / g+0xB4 in-flight / g+0xB8 shadow / flush
 *                       0xC6108 contract; ETE status 0x4003A874 -> sending
 *                       trigger; no host register feeds the A->B copy
 *   wifi-ete-init/      vendor ETE init: intr RMW &= 0xFFE0F8F8 first, then
 *                       group-A SR and group-B DR writes (bfi depth/ctrl)
 *   wifi-ete-engine/    ETE start condition = device status path, no host wake
 *   wifi-event-router/  event controller map, IRQ-unmask analysis
 *   wifi-rom-dump/      corrected boot protocol: release 0x5A5A only; the
 *                       0xCECE gate must stay OUT of the ROM boot stage
 *                       (CA 0x4000010C carries ROM 0xCEED vs image 0xCECE)
 *   ledgers 2026-11-24 10:40 (GP8 golden boot), 11:10/11:40 (GP9/GP10 state)
 *
 * Address conventions (project-wide, unchanged):
 *   firmware-internal CPU address -> host BAR0 = CPU + 0x6B8000
 *   device IO CA                  -> host BAR0 = 0x3B8000 + (CA - 0x40000000)
 */
#ifndef _HI5622_HCC_SERVICE_H
#define _HI5622_HCC_SERVICE_H

#include <linux/types.h>

#include "hi5622_pciet.h"

/* ---------------------------------------------------------------------
 * 1. D2H mailbox (vendor pcie_msg_handle, host-replica REPORT sec 4.1-4.2)
 *    Write order per observed word: ack out[3]=1 FIRST, read out[1],
 *    drain out[1]=0, re-arm out[4]=1, then dispatch ids 0..10 lowest-first.
 * ------------------------------------------------------------------- */
#define HI5622_HCC_CA_D2H_ACK		0x40101438u	/* out[3]: write 1 [V] */
#define HI5622_HCC_CA_D2H_REARM		0x40101414u	/* out[4]: write 1 [V] */
#define HI5622_HCC_BAR0_D2H_ACK		HI5622_BAR0_IO(HI5622_HCC_CA_D2H_ACK)
#define HI5622_HCC_BAR0_D2H_REARM	HI5622_BAR0_IO(HI5622_HCC_CA_D2H_REARM)

/* read-only checkpoints (never written / only read) */
#define HI5622_HCC_BAR0_NEVER_WRITE	HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE)
#define HI5622_HCC_BAR0_D2H_DOORBELL	HI5622_BAR0_IO(HI5622_CA_D2H_DOORBELL)

/* D2H bitmap bits seen in the boot/service path [V]/[V*] */
#define HI5622_HCC_D2H_BIT_DEV_READY_STUB	1	/* pcie_dev_ready_msg_handle stub */
#define HI5622_HCC_D2H_BIT_EXCP_PANIC		2	/* out[1] bit2 = 0x4: startup/vector announce (0x86E14 stores it) */
#define HI5622_HCC_D2H_BIT_ETE_DONE		3	/* pcie_ete_transfer_done -> drain DR 0x1f */
#define HI5622_HCC_D2H_BIT_PM_SLEEP_DISALLOW	4	/* wlan_pm_slp_disallow */
#define HI5622_HCC_D2H_BIT_PM_WAKE_OK		5	/* wlan_pm_wkup_dev_ok */
#define HI5622_HCC_D2H_BIT_ETE_NOTIFY		6	/* d2h_notify(6) from the deposit op */
#define HI5622_HCC_D2H_BIT_ETE_SENDING		7	/* trigger-ete-sending variant */
#define HI5622_HCC_D2H_DR_CHANNELS		0x1fu	/* vendor drains DR bitmask 0x1f */

/* ---------------------------------------------------------------------
 * 2. Corrected boot protocol (2026-11-24 GP8; wifi-rom-dump REPORT sec 5-7)
 *
 * The ROM boot stage owns 16-bit CA 0x4000010C == 0xCEED until CPU1's S1
 * completes; the image's service/exception handler (CPU 0xC6E14) instead
 * zeroes that word and spins for 32-bit 0x0000CECE.  One register, two
 * mutually exclusive handshakes.  Hard policy encoded here:
 *   - NEVER write CECE before the handshake page +0x8 reads 2;
 *   - after boot, write CECE only on observed handler-spin evidence.
 * ------------------------------------------------------------------- */
#define HI5622_HCC_CA_GATE_CEED		0x4000010cu	/* dual-use 16-bit gate [V] */
#define HI5622_HCC_BAR0_GATE		HI5622_BAR0_IO(HI5622_HCC_CA_GATE_CEED)
#define HI5622_HCC_GATE_CECE		0x0000ceceu	/* image handler spin value [V] */
#define HI5622_HCC_GATE_CEED		0x0000ceedu	/* ROM S1 gate value [V] */

/* ROM<->image handshake page CA 0x1BF000, CPU -> BAR0 0x6B8000 + CPU = 0x877000
 * words {prog, cpu1_status, ack, magic}; ack(+0x8)==2 is the S1 completion [V] */
#define HI5622_HCC_CPU_HANDSHAKE_PAGE	0x1bf000u
#define HI5622_HCC_BAR0_HANDSHAKE_PAGE	(0x6b8000u + HI5622_HCC_CPU_HANDSHAKE_PAGE)
#define HI5622_HCC_HANDSHAKE_ACK	2u		/* +0x8 == 1<<core [V] */

/* ---------------------------------------------------------------------
 * 3. Firmware state objects the service reads (firmware RAM, CPU->BAR0 rule)
 * ------------------------------------------------------------------- */
#define HI5622_HCC_CPU_BOOT_STATE	0x118b60u	/* boot-task state; 1 = ran [V*] */
#define HI5622_HCC_BAR0_BOOT_STATE	(0x6b8000u + HI5622_HCC_CPU_BOOT_STATE)
#define HI5622_HCC_CPU_BOOT_SLOT	0x17d42cu	/* ROM-armed reset slot [V] */
#define HI5622_HCC_BAR0_BOOT_SLOT	(0x6b8000u + HI5622_HCC_CPU_BOOT_SLOT)
#define HI5622_HCC_CPU_G_OBJECT		0x10c0f4u	/* g = *(CA 0x172130) message object [V] */
#define HI5622_HCC_BAR0_G_OBJECT	(0x6b8000u + HI5622_HCC_CPU_G_OBJECT)
#define HI5622_HCC_CPU_G_ARMED		0x10c1a8u	/* g+0xB4: D2H word in flight [V] */
#define HI5622_HCC_CPU_G_PENDING	0x10c1acu	/* g+0xB8: deferred notify shadow [V] */
#define HI5622_HCC_BAR0_G_ARMED		(0x6b8000u + HI5622_HCC_CPU_G_ARMED)
#define HI5622_HCC_BAR0_G_PENDING	(0x6b8000u + HI5622_HCC_CPU_G_PENDING)
#define HI5622_HCC_CPU_EXCP_MASK	0x40008u	/* exception-seen mask [V] */
#define HI5622_HCC_BAR0_EXCP_MASK	(0x6b8000u + HI5622_HCC_CPU_EXCP_MASK)

/* ---------------------------------------------------------------------
 * 4. Image IRQ/event layer (wifi-event-router REPORT sec 2) - READ ONLY.
 *    The only image unmask sites are inside the IRQ dispatcher, the
 *    deferred-dispatch path and the scheduler idle; the scheduler is entered
 *    by entry A after the +8==2 handshake.  These reads decide whether the
 *    layer started and whether any event was ever delivered.
 * ------------------------------------------------------------------- */
#define HI5622_HCC_CA_EV_CTRL		0x40160100u	/* global enable [V] */
#define HI5622_HCC_CA_EV_MAXPRI		0x40160104u	/* 0xFF [V] */
#define HI5622_HCC_CA_EV_CFG		0x40160108u	/* 3 [V] */
#define HI5622_HCC_CA_EV_VECTOR		0x4016010cu	/* current vector, low 10 bits [V] */
#define HI5622_HCC_CA_EV_EOI		0x40160110u	/* EOI word written by dispatcher [V] */
#define HI5622_HCC_CA_EV_EN0		0x40161100u	/* per-event enables, W1S bank [V] */
#define HI5622_HCC_CA_EV_EN1		0x40161104u
#define HI5622_HCC_CA_EV_EN2		0x40161108u
#define HI5622_HCC_CA_EV_PEND0		0x40161188u	/* software raise bank A (0x4C|0x4E = 0x5000) [V] */
#define HI5622_HCC_CA_EV_PEND1		0x40161288u	/* mirror bank B [V] */
#define HI5622_HCC_CA_EV_PRIO		0x40161400u	/* priority bank base [V] */
#define HI5622_HCC_CA_TIMER_RELOAD	0x40160600u	/* scheduler-armed timer [V] */
#define HI5622_HCC_CA_TIMER_CTRL	0x40160608u
#define HI5622_HCC_CA_TIMER_START	0x4016060cu
#define HI5622_HCC_CA_IRQ_EDGE		0x40161f00u	/* inter-core/ROM wake word (read only) [V] */

/* ---------------------------------------------------------------------
 * 5. ETE engine (vendor init sequence, wifi-ete-init REPORT sec 2)
 * ------------------------------------------------------------------- */
#define HI5622_HCC_CA_ETE_INTR		0x40039508u	/* host-facing intr word [V] */
#define HI5622_HCC_BAR0_ETE_INTR	HI5622_BAR0_IO(HI5622_HCC_CA_ETE_INTR)
#define HI5622_HCC_ETE_INTR_KEEP	0xffe0f8f8u	/* vendor RMW mask [V] */
#define HI5622_HCC_CA_ETE_STATUS	0x4003a874u	/* D2H status path [V] */
#define HI5622_HCC_BAR0_ETE_STATUS	HI5622_BAR0_IO(HI5622_HCC_CA_ETE_STATUS)
#define HI5622_HCC_ETE_SEND_DR0		0xc00		/* device-authored send words, +4/ch [V] */
#define HI5622_HCC_ETE_SEND_SR0		0xc18		/* device-authored send words, +4/ch [V] */
#define HI5622_HCC_ETE_STATUS_CH_SHIFT	16		/* bits 16-20 -> sending trigger [V] */
#define HI5622_HCC_ETE_CH_SR_OFF	(HI5622_ETE_SR0_OFF)	/* 0x400/0x450/0x4a0 */
#define HI5622_HCC_ETE_CH_DR_OFF	(HI5622_ETE_DR0_OFF)	/* 0x590/0x5e0/0x630/0x680 */

/* ---------------------------------------------------------------------
 * 6. HCC frame (wifi-ete-frames REPORT sec 1.3 / 3.2; h2d-inject contract)
 * ------------------------------------------------------------------- */
#define HI5622_HCC_HDR_LEN		12
#define HI5622_HCC_TAG			0x5a5au		/* transport tag at +0xA [V] */
#define HI5622_HCC_TAG_OFF		0x0a
#define HI5622_HCC_LEN_OFF		0x04		/* u16 total = payload + 12, must be > 0 [V] */
#define HI5622_HCC_ID_OFF		0x06
#define HI5622_HCC_GROUP_PLAT		4u		/* device->host ready group [V] */
#define HI5622_HCC_GROUP_PLAT_ID1	1u		/* device_plat_ready (total 0x0C) [V] */
#define HI5622_HCC_GROUP_PLAT_ID2	2u		/* host_ready (total 0x10, 4 B payload) [V] */
#define HI5622_HCC_MSG_H2D_ETE_ANNOUNCE	3u		/* mailbox id posted after an SR commit [V] */
#define HI5622_HCC_MSG_H2D_POST_GLOBAL	1u		/* payload-free PM/global message (not on boot path) [V] */
#define HI5622_HCC_MSG_H2D_BUGON		5u		/* firmware BUG() trap: NEVER post [V] */
#define HI5622_HCC_SR_GROUP_SAFE		0u		/* group-0 H2D table exists (40 entries) [V] */
#define HI5622_HCC_SR_ID_SAFE			0u		/* group-0 id-0 handler = payload-free stub [V] */
#define HI5622_HCC_FRAME_MAX_PAYLOAD	0x674u		/* hcc_msg_alloc bound [V] */
#define HI5622_HCC_FRAME_MAX_TOTAL	(HI5622_HCC_FRAME_MAX_PAYLOAD + HI5622_HCC_HDR_LEN)
#define HI5622_HCC_DR_PAYLOAD_CAP	0x63u		/* host_ready_msg_process cap [V] */

/* vendor ready waits (HZ=100 in both trees; expressed in ms) [V] */
#define HI5622_HCC_PLAT_READY_MS	2000u		/* 200 jiffies */
#define HI5622_HCC_DEV_READY_MS		20000u		/* 2000 jiffies */
#define HI5622_HCC_DR_CH_COUNT		4
#define HI5622_HCC_SR_CH_COUNT		3

/* ---------------------------------------------------------------------
 * 7. Service entry points (called by pciet_main.c only with
 *    -DHI5622_HCC_BUILD; the plain transport never references them)
 * ------------------------------------------------------------------- */
struct hi5622_hcc_service;

int  pciet_hcc_probe(struct hi5622_pci *ctx);
void pciet_hcc_remove(struct hi5622_pci *ctx);

#endif /* _HI5622_HCC_SERVICE_H */
