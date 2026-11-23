/* SPDX-License-Identifier: GPL-2.0 */
/*
 * pciet_hcc_link.h - Hi5622V100 host-side HCC link/event observability and
 * the vendor-attested H2D consumer-liveness ladder (wifi-hcc-link lane).
 *
 * ORIGINAL code written by the wifi-hcc-link lane (host-only, 2026-11-23)
 * from the project's decoded contract; no vendor source text or object code
 * is copied.  Evidence markers: [V] re-verified from a named local artifact,
 * [V*] device-measured by an earlier attended lane, [I] inference, [U] unknown.
 *
 * WHAT THIS IS (and is not)
 *
 *   The vendor host modules do NOT send any "HCC link create / link register /
 *   event register" H2D message: the host-side HCC framework (hcc_init,
 *   hcc_start, bal_init, hcc_msg_register_tab_*) is host-RAM-local, and the
 *   only host->device traffic in the vendor init/release flow is the mailbox
 *   announce for data frames (id 3), the PM/wake mailbox ids (1/6), the
 *   firmware-upload path (bal_write) and post-DEVICE-READY config frames
 *   (wifi-hcc-link REPORT.md sec 3).  The firmware's per-link/event contexts
 *   are created by firmware-internal boot code, not by an H2D message
 *   (REPORT.md sec 4).  Therefore this file does NOT fabricate any
 *   "link bring-up" frame (a fabricated device-RAM write is explicitly not
 *   justified - external-abort risk, wifi-abort-rca sec 10.4).
 *
 *   What the host counterpart CAN do - and what this file implements - is:
 *     1. read-only observability of exactly the firmware objects the boot
 *        fault involves (g / HCC instance / destination registry / per-link
 *        +0x4B0 words / ETE fill state / mailbox words), so the bench can
 *        classify "link/event context SET vs UNSET" without touching RAM;
 *     2. one hard-bounded, vendor-attested H2D consumer-liveness probe: post
 *        mailbox id 1 (no payload - the firmware id-1 handler just posts its
 *        global message object; hcc-re sec 2.4 / h2d-catalog sec 3) and
 *        measure whether the firmware consumes it (out[0] bit clears).
 *        "Consumer DEAD" is the decisive negative that no H2D frame can be
 *        the missing input; "consumer ALIVE but per-link contexts UNSET"
 *        points the next lane at the firmware-internal bring-up.
 *
 * SAFETY CONTRACT (project hard rules, encoded here)
 *   - CA 0x400392f0 (out[5], H2D ack) is NEVER written: read-only checkpoint
 *     only; build-time asserts below plus the transport's gated writer.
 *   - firmware mailbox id 5 is NEVER posted (it is the firmware BUG() trap);
 *     the only mailbox bit this file ever posts is id 1 (BIT(1)).
 *   - the RC misc window 0x10161000 is never touched.
 *   - no device RAM/IO write other than out[0]/out[2] in the wake probe; the
 *     ladder refuses to run before the CPU release and refuses when out[0]
 *     already holds another bitmap (nothing else in flight).
 *   - observability reads are bounded to the region-5 CPU window
 *     (BAR0 = CPU + 0x6b8000, CPU < 0x198000) and the region-3 IO window
 *     (BAR0 = 0x3b8000 + (CA & 0xfffff)); nothing outside is read.
 */
#ifndef _PCIET_HCC_LINK_H
#define _PCIET_HCC_LINK_H

#include <linux/types.h>

/* ---------------------------------------------------------------------
 * 1. Region-5 CPU-RAM window (abort-rca sec 0 / bench-proven BAR0 rule)
 *
 *   CPU address A (firmware image/objects, image loaded at CPU 0x40000)
 *     -> BAR0 offset = 0x6b8000 + A.
 *   The programmed viewport 5 covers BAR0 0x6b8000..0x8cffff, i.e. CPU
 *   0x000000..0x197fff; every object below is inside that bound.  [V*]
 * ------------------------------------------------------------------- */
#define HCC_LINK_BAR0_RAM_BASE	0x6b8000u
#define HCC_LINK_CPU_WIN_END	0x198000u	/* exclusive [V*] */

/* ---------------------------------------------------------------------
 * 2. Firmware boot/message observability words ([V*] bench reads)
 * ------------------------------------------------------------------- */
#define HCC_LINK_CA_EXC_MASK	0x00040008u	/* exception-seen bitmap, halfword [V*] */
#define HCC_LINK_CA_STATE	0x00118b60u	/* boot-task state byte [V*] */
#define HCC_LINK_CA_BOOT_SLOT	0x0017d42cu	/* reset-dispatch slot (0x000c19d1=BUG) [V*] */
#define HCC_LINK_CA_G_PTR	0x00172130u	/* *(u32) = g message object CA [V*] */
#define HCC_LINK_CA_INSTANCE	0x0010c190u	/* HCC instance pointer slot (g+0x9c) [V] */
#define HCC_LINK_CA_REG_REC0	0x00171fb4u	/* destination-registry core-0 record [V*] */

/* g message-object fields (base g, CA = *(0x172130)) [V* / V] */
#define HCC_LINK_G_OFF_OUT1_PTR	0x9cu	/* 0x40039014 [V*] */
#define HCC_LINK_G_OFF_ARMED	0xb4u	/* D2H armed shadow [V*] */
#define HCC_LINK_G_OFF_PENDING	0xb8u	/* D2H pending shadow [V*] */
#define HCC_LINK_G_OFF_2CC	0x2ccu	/* descriptor-block candidate [V] */

/* destination-registry record fields (record base 0x171fb4) [V*] */
#define HCC_LINK_REG_NWORDS	8

/* the four per-link +0x4B0 words the lane brief names [V*]:
 * registry target base +0x4b0 for the four targets 0x171b70/0x171e1c/
 * 0x171f54/0x171fa8.  All four read ZERO on the parked cold boot.
 * The CA table itself lives in pciet_hcc_link.c (hcc_link_perlink_ca[]). */
#define HCC_LINK_PERLINK_COUNT	4

/* ---------------------------------------------------------------------
 * 3. Mailbox / IO-region words (region-3 rule; mirrors hi5622_pciet.h) [V]
 * ------------------------------------------------------------------- */
#define HCC_LINK_CA_H2D_PENDING	0x40039010u	/* out[0] host writes, device clears */
#define HCC_LINK_CA_H2D_DOORBELL 0x400392d4u	/* out[2], OR bit0 */
#define HCC_LINK_CA_OUT5		0x400392f0u	/* out[5]: READ ONLY, never write */
#define HCC_LINK_CA_ACK		0x40101438u	/* out[3] D2H ack (write 1) */
#define HCC_LINK_CA_REARM	0x40101414u	/* out[4] D2H re-arm (write 1) */
#define HCC_LINK_CA_D2H_DOORBELL 0x40101434u	/* device->host doorbell (RO) */
#define HCC_LINK_WAKE_ID	1u		/* the only mailbox id this file posts */
#define HCC_LINK_WAKE_BIT	(1u << HCC_LINK_WAKE_ID)	/* BIT(1) */

/* wake-probe bounds */
#define HCC_LINK_WAKE_POLLS	2000u		/* 2000 x 50 us = 100 ms */
#define HCC_LINK_WAKE_POLL_US	50u
#define HCC_LINK_WAKE_MAX	3u		/* bench rule: at most 3 attempts */

/* ---------------------------------------------------------------------
 * 4. Result codes (mapped to -errno by the caller)
 * ------------------------------------------------------------------- */
#define HCC_LINK_OK		0
#define HCC_LINK_EWINDOW	(-1)	/* object outside the readable window */
#define HCC_LINK_ESTATE		(-2)	/* bad state (not released / out[0] busy) */
#define HCC_LINK_ETIMEOUT	(-3)	/* device did not consume the wake bit */
#define HCC_LINK_EIO		(-4)	/* windows not programmed / no mapping */

/* ---------------------------------------------------------------------
 * 5. Entry points (called by pciet_main.c probe/remove; wifi-hcc-link lane)
 * ------------------------------------------------------------------- */
struct hi5622_pci;

int  pciet_hcc_link_probe(struct hi5622_pci *ctx);
void pciet_hcc_link_remove(struct hi5622_pci *ctx);

#endif /* _PCIET_HCC_LINK_H */
