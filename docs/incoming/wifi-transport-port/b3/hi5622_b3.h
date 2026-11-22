/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_b3.h - B3 bench-instrument additions to the Hi5622V100 clean-room
 * PCIe/ETE transport skeleton.
 *
 * ORIGINAL code by the b3-instrument lane (task-66, 2026-11-22).  This file
 * and pciet_b3.c extend the wifi-transport-port skeleton in ../build/ with the
 * post-DEVICE-READY bench probes P0-P5 specified by the hcc-re contract:
 *
 *   opensource/docs/incoming/wifi-hcc-re/REPORT.md  sec 4 (ETE engine),
 *       sec 5.1/5.2/5.4 (mailbox + required host write sequence),
 *       sec 8 (driver-facing contract), sec 9 (B3 probes P0-P5)
 *   opensource/docs/incoming/wifi-hcc-re/SUMMARY.md (the 10 contract points)
 *
 * No vendor source text, object code or blob is reproduced.  Evidence markers
 * follow hi5622_pciet.h: [V] re-verified from a named local artifact or bench
 * log, [V*] device-measured by an earlier attended lane, [I] inference,
 * [U] unknown / not yet pinned.
 *
 * SAFETY CONTRACT (project hard rules, encoded here):
 *   - CA 0x400392f0 (out[5], the H2D ack / "copy A" word) is NEVER written by
 *     this code: build-time asserts plus the runtime gate in b3_writel();
 *     probe P4 only READS it, which the rule permits.
 *   - ETE channel registers the firmware owns are never written: +0x00
 *     enable, +0x08 ctrl of DR channels, group-A consumer +0x1c, group-B fill
 *     +0x3c.  P4 reads them back; P0 writes only the host-owned fields.
 *   - The boot words 0x40000108 (0x5a5a release) and 0x4000010c (0xcece gate)
 *     are written only by the base module's own attended steps; B3 adds none.
 *   - The RC misc window 0x10161000 is never touched.
 *   - B3 posts no H2D message: the contract requires no host reply and H2D id 5
 *     is a device BUG() trap.
 */
#ifndef _HI5622_B3_H
#define _HI5622_B3_H

#include <linux/types.h>

#include "hi5622_pciet.h"

/* ---------------------------------------------------------------------
 * 1. Mailbox registers used by the contract-correct D2H service [V]
 *
 * hcc-re REPORT sec 5.1/5.2/5.4 pins these as write-1 registers.  The base
 * header carries 0x40101438 / 0x40101414 under the earlier "MAC message
 * A/B (hdpp-slice hypothesis)" names; the contract re-assigns them to the
 * D2H ack / re-arm sequence.  B3 uses its own names and asserts the values.
 * ------------------------------------------------------------------- */
#define HI5622_CA_B3_D2H_ACK		0x40101438u	/* out[3]: host writes 1 [V] */
#define HI5622_CA_B3_D2H_REARM		0x40101414u	/* out[4]: host writes 1 [V] */
#define HI5622_BAR0_B3_D2H_ACK		HI5622_BAR0_IO(HI5622_CA_B3_D2H_ACK)
#define HI5622_BAR0_B3_D2H_REARM	HI5622_BAR0_IO(HI5622_CA_B3_D2H_REARM)
/* read-only checkpoints (out[1], out[2], D2H doorbell, out[5]) */
#define HI5622_BAR0_B3_NEVER_WRITE	HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE)
#define HI5622_BAR0_B3_D2H_DOORBELL	HI5622_BAR0_IO(HI5622_CA_D2H_DOORBELL)

/* ---------------------------------------------------------------------
 * 2. HCC frame header (12 bytes) as pinned by hcc-re REPORT sec 3.1 [V]
 *
 *   +0x00 u8   group/type: low nibble = group index (&0xf at dispatch)
 *   +0x01 u8   low nibble = alloc state (1 = allocated);
 *              high nibble = resource-group / thread index (queue routing)
 *   +0x02 u8   per-message field A
 *   +0x03 u8   per-message field B (chip id / vap id)
 *   +0x04 u16  total length = payload + 12   (little-endian)
 *   +0x06 u16  message id - the dispatch key (little-endian)
 *   +0x08 u8   per-message field C
 *   +0x09 u8   dst = hdr[1] >> 4 (resource/thread selector)
 *   +0x0a u16  transport tag 0x5a5a (little-endian)
 *   +0x0c ...  payload
 *
 * The H2D SR node carries this frame: node word1 = (len << 16) | 0x6d2b with
 * len = total frame length (payload + 12), node word0 = buffer device VA [V].
 * The D2H DR node uses the same 8-byte node format [V].
 * ------------------------------------------------------------------- */
#define HI5622_B3_HCC_HDR_LEN	12
#define HI5622_B3_HCC_TAG	0x5a5au		/* transport tag [V] */
#define HI5622_B3_HCC_GROUP_PLAT 4u		/* boot/exception group [V] */
#define HI5622_B3_HCC_ID_PLAT_READY 1u		/* device_plat_ready_msg_process [V] */
#define HI5622_B3_HCC_ID_HOST_READY 2u		/* host_ready_msg_process, payload <= 0x63 B [V] */

struct hi5622_b3_hcc_hdr {
	u8	group;		/* +0x00 low nibble = group index [V] */
	u8	rsrc;		/* +0x01 [7:4] = resource-group / thread [V] */
	u8	field_a;	/* +0x02 [V] */
	u8	field_b;	/* +0x03 [V] */
	__le16	total_len;	/* +0x04 payload + 12 [V] */
	__le16	id;		/* +0x06 dispatch key [V] */
	u8	field_c;	/* +0x08 [V] */
	u8	dst;		/* +0x09 [V] */
	__le16	tag;		/* +0x0a 0x5a5a [V] */
} __packed;

/* ---------------------------------------------------------------------
 * 3. ETE channel sets armed by probe P0 [V] (hcc-re REPORT sec 4.1)
 *    SR0-2 = +0x400/+0x450/+0x4a0 (group A host ring)
 *    DR0-3 = +0x590/+0x5e0/+0x630/+0x680 (group B host ring)
 *    depth 32, 8-byte nodes, packed index (idx & 0x3ff) | phase << 10.
 * ------------------------------------------------------------------- */
#define HI5622_B3_SR_COUNT	3
#define HI5622_B3_DR_COUNT	4
#define HI5622_B3_MAX_NODE_CAP	8	/* P2: first 12 B of up to 8 nodes */

/* ---------------------------------------------------------------------
 * 4. B3 instrument entry points (called by pciet_main.c when built with
 *    -DHI5622_B3_BUILD; the base module never references them)
 * ------------------------------------------------------------------- */
struct hi5622_b3;

int  pciet_b3_probe(struct hi5622_pci *ctx);
void pciet_b3_remove(struct hi5622_pci *ctx);

#endif /* _HI5622_B3_H */
