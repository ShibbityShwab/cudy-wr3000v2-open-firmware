/* SPDX-License-Identifier: GPL-2.0 */
/*
 * hi5622_host_replica.h - post-release host counterpart of the vendor
 * Hi5622V100 platform driver (hi5622v100_plat.ko), rebuilt as ORIGINAL code
 * from the decoded interface contract.  No vendor source/object code is
 * reproduced.
 *
 * Contract sources: ../REPORT.md (this lane), ../SPEC.md, and the transport
 * header hi5622_pciet.h (wifi-transport-port lane).  Evidence markers:
 * [V] re-verified from a named local artifact, [V*] device-measured by an
 * earlier attended lane, [I] inference, [U] unknown.
 *
 * Purpose: sit on top of the transport module (pciet_main.c / pciet_ete.c)
 * and service the device after the 0x5a5a CPU release:
 *
 *   - full D2H mailbox order: ack -> read -> drain -> re-arm -> dispatch [V]
 *   - event-driven exception-vector gate acknowledge: per observed out[1] bit2
 *     write 0xcece to CA 0x4000010c and capture the staged vector frame
 *     (CA 0x400008 mask + CA 0x40000c..0x400044, 17 words) [V]
 *   - HCC chip-group-4 boot table: id1 completes the 2 s plat-ready wait,
 *     id2 copies the device payload into read_msg[chip][core] (100 B) and
 *     completes the 20 s device-ready wait [V]
 *   - ETE DR drain -> HCC dispatch, and SR transmit framing + H2D id3 [V]
 *   - optional 10 s heartbeat (restarted by g4 id4) [V]
 *
 * SAFETY: never write CA 0x400392f0 (out[5]); never read the RC misc window
 * 0x10161000; never send H2D mailbox id 5 on a normal path.  All device
 * writes here go through hr_write()/the transport API and are limited to the
 * registers listed in REPORT.md section 2.1.
 */
#ifndef _HI5622_HOST_REPLICA_H
#define _HI5622_HOST_REPLICA_H

#include <linux/completion.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include "hi5622_pciet.h"

/* ---- HCC frame contract (REPORT.md section 3.3) [V] ------------------ */
#define HR_HCC_HDR_LEN		12
#define HR_HCC_MAX_PAYLOAD	0x674	/* vendor hcc_msg_alloc limit [V] */
#define HR_HCC_TAG_LO		0x5a
#define HR_HCC_TAG_HI		0x5a

/* ---- boot-handshake timings (REPORT.md section 7) [V] --------------- */
#define HR_PLAT_READY_TIMEOUT_MS	2000	/* vendor 200 jiffies @HZ=100 */
#define HR_DEVICE_READY_TIMEOUT_MS	20000	/* vendor 2000 jiffies @HZ=100 */
#define HR_HEARTBEAT_NS			10000000000ULL	/* vendor 10 s */
#define HR_H2D_DRAIN_SPINS		0xfffe	/* vendor loop bound [V] */
#define HR_POLL_MS			50

/* ---- D2H/H2D ids this replica needs -------------------------------- */
#define HR_MBOX_PM_SLEEP	1	/* H2D: PM sleep request [V] */
#define HR_MBOX_DEV_PLAT_READY	1	/* D2H: platform-up stub [V] */
#define HR_MBOX_EXCP_PANIC	2	/* D2H: exception panic announce [V] */
#define HR_MBOX_ETE_DONE	3	/* H2D announce + D2H transfer done [V] */
#define HR_MBOX_PM_SLP_DISALLOW	4	/* D2H [V] */
#define HR_MBOX_PM_WKUP_OK	5	/* D2H [V] */
#define HR_MBOX_WAKE6		6	/* D2H [V] */
#define HR_MBOX_WAKE7		7	/* D2H [V] */
#define HR_MBOX_FORCE_BUGON	5	/* H2D guard: never on a normal path */
#define HR_MBOX_ANNOUNCE	3	/* H2D: ETE SR announce [V] */

/* ---- read-only exception context (REPORT.md section 6.4) [V] -------- */
#define HR_CA_VECTOR_MASK	0x400008u
#define HR_CA_VECTOR_FRAME	0x40000cu
#define HR_VECTOR_FRAME_WORDS	17
#define HR_READ_MSG_LEN		100	/* sizeof g_dmac_to_hmac_read_msg [V] */
#define HR_READ_MSG_CORES	2
#define HR_EXCP_INFO_LEN	0x30

/* ---- replica state machine (SPEC.md section 1) --------------------- */
enum hr_state {
	HR_ST_IDLE = 0,
	HR_ST_RELEASED,
	HR_ST_GATE_SERVICE,
	HR_ST_WAIT_PLAT,
	HR_ST_WAIT_DEVICE,
	HR_ST_READY,
	HR_ST_FAILED,
};

#define HR_FLAG_PLAT_READY	BIT(0)
#define HR_FLAG_DEVICE_READY	BIT(1)
#define HR_FLAG_GATE_ACKED	BIT(2)
#define HR_FLAG_HEARTBEAT_LOST	BIT(3)
#define HR_FLAG_EXCP_SEEN	BIT(4)

struct hr_replica {
	struct hi5622_pci	*pci;
	struct device		*dev;

	enum hr_state		state;
	spinlock_t		lock;
	bool			stop;

	/* H2D pending/in-flight coalescing (vendor pcie_msg_send) [V] */
	u32			pending;
	u32			inflight;

	/* ready handshake (vendor g_multi_chip_init_res) [V] */
	struct completion	plat_ready;
	struct completion	device_ready;
	u32			flags;
	u32			ready_map;

	/* g4 id2 payload consumer (vendor g_dmac_to_hmac_read_msg) [V] */
	u8			read_msg[HR_READ_MSG_CORES][HR_READ_MSG_LEN];
	u8			excp_info[HR_EXCP_INFO_LEN];

	/* last captured vector context [V] */
	u32			vector_mask;
	u32			frame[HR_VECTOR_FRAME_WORDS];
	bool			frame_valid;

	/* heartbeat (vendor 10 s hrtimer, armed by debug tooling) [V] */
	struct hrtimer		heartbeat;
	bool			heartbeat_armed;

	struct task_struct	*wait_thread;

	/* telemetry */
	u32			stat_events;
	u32			stat_d2h_empty;
	u32			stat_gate_ack;
	u32			stat_excp;
	u32			stat_hcc;
	u32			stat_unknown;
	u32			stat_bad_frame;
	u32			group_count[16];
};

int  hr_attach(struct hi5622_pci *pci, struct hr_replica *hr);
void hr_detach(struct hr_replica *hr);

/* called by the transport after pciet_cpu_release()+gate announce */
int  hr_on_released(struct hr_replica *hr);

/* one polled service tick: D2H ack/drain/re-arm + dispatch + gate ack */
int  hr_service(struct hr_replica *hr);

/* blocking ready waits (2 s / 20 s); intended for hr's own kthread */
int  hr_wait_ready(struct hr_replica *hr);

/* HCC transmit on SR0 + H2D id3 announce */
int  hr_hcc_send(struct hr_replica *hr, u8 group, u8 bid, u16 id,
		 const void *payload, u32 len);

/* H2D primitives (vendor order; out[0] wait, then out[0]/out[2]) */
int  hr_h2d_announce(struct hr_replica *hr, u32 id);
int  hr_h2d_pm_sleep(struct hr_replica *hr);

/* read-only exception-context capture + status dump */
void hr_capture_vector_context(struct hr_replica *hr);
void hr_dump_status(struct hr_replica *hr);

#endif /* _HI5622_HOST_REPLICA_H */
