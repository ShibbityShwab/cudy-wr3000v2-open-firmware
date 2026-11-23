// SPDX-License-Identifier: GPL-2.0
/*
 * hi5622_host_replica.c - ORIGINAL clean-room host counterpart of the vendor
 * Hi5622V100 platform driver's post-release service (see REPORT.md/SPEC.md).
 * Builds as an extra object of the existing hi5622_pciet transport module:
 *
 *    hi5622_pciet-y := pciet_main.o pciet_ete.o pciet_host_replica.o
 *
 * Integration (one-line hooks in pciet_main.c, see BUILD-NOTES.md):
 *   - after the release+0xcece announce step: hr_on_released(&hr);
 *   - in the poll thread loop:                hr_service(&hr);
 *   - drop the raw pciet_d2h_scan() call when hr_service() is used, because
 *     hr_service implements the vendor ack/drain/re-arm order and dispatches
 *     the same d2h[] table.
 *
 * Contract evidence: REPORT.md (this lane); transport: hi5622_pciet.h.
 * Safety: never write 0x400392f0; never read the RC misc window; no H2D id 5.
 *
 * Note: log strings intentionally carry no trailing newline escape so this
 * file contains no backslash escapes at all; printk joins continuation lines.
 */
#include <linux/build_bug.h>
#include <linux/completion.h>
#include <linux/container_of.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/hrtimer.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/kthread.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/unaligned.h>

#include "hi5622_host_replica.h"

#define hr_dev(hr)	(&(hr)->pci->pdev->dev)
#define HR_MAX_INSTANCES 2

static struct hr_replica *hr_instances[HR_MAX_INSTANCES];

static struct hr_replica *hr_of(struct hi5622_pci *pci)
{
	unsigned int i;

	for (i = 0; i < HR_MAX_INSTANCES; i++)
		if (hr_instances[i] && hr_instances[i]->pci == pci)
			return hr_instances[i];
	return NULL;
}

/* only the registers listed in REPORT.md section 2.1 may be written here */
static void hr_write(struct hr_replica *hr, u32 bar0_off, u32 val)
{
	BUILD_BUG_ON(HI5622_BAR0_IO(HI5622_CA_MAC_MSG_A) ==
		     HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(HI5622_BAR0_IO(HI5622_CA_MAC_MSG_B) ==
		     HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	BUILD_BUG_ON(HI5622_BAR0_D2H_GATE == HI5622_BAR0_IO(HI5622_CA_NEVER_WRITE));
	writel(val, hr->pci->bar0 + bar0_off);
}

static u32 hr_read(struct hr_replica *hr, u32 bar0_off)
{
	return readl(hr->pci->bar0 + bar0_off);
}

static void hr_rx_frame(struct hi5622_pci *pci, void *va, u32 len);
static void hr_drain_dr(struct hi5622_pci *pci);

/*
 * Event-driven exception-vector gate acknowledge.
 * The firmware's non-reset vector handler stages the context, posts
 * out[1]=4 + D2H doorbell, zeroes CA 0x4000010C/0x40000108 and spins until
 * the host writes 0xcece.  One ack per observed announce [V]/[U].
 */
static void hr_gate_ack(struct hr_replica *hr)
{
	hr_write(hr, HI5622_BAR0_D2H_GATE, HI5622_D2H_GATE_MAGIC);
	hr->stat_gate_ack++;
	hr->flags |= HR_FLAG_GATE_ACKED;
	dev_info(hr_dev(hr),
		 "replica: gate ack 0x%04x -> CA 0x4000010c (event %u)",
		 HI5622_D2H_GATE_MAGIC, hr->stat_gate_ack);
}

/* read-only capture of the vector-seen mask and the 17-word context frame */
void hr_capture_vector_context(struct hr_replica *hr)
{
	unsigned int i;

	if (!hr || !hr->pci || !hr->pci->bar0)
		return;
	hr->vector_mask = hr_read(hr, HI5622_BAR0_IO(HR_CA_VECTOR_MASK));
	for (i = 0; i < HR_VECTOR_FRAME_WORDS; i++)
		hr->frame[i] = hr_read(hr, HI5622_BAR0_IO(HR_CA_VECTOR_FRAME + 4 * i));
	hr->frame_valid = true;
	hr->stat_excp++;
	dev_warn(hr_dev(hr),
		 "replica: vector frame mask 0x%08x sp 0x%08x lr 0x%08x lr_exc 0x%08x spsr 0x%08x",
		 hr->vector_mask, hr->frame[13], hr->frame[14],
		 hr->frame[15], hr->frame[16]);
}

/* ---------------- D2H handlers (vendor table emulation) [V] ---------- */

static void hr_on_dev_plat_ready(struct hi5622_pci *pci, u32 id)
{
	struct hr_replica *hr = hr_of(pci);

	if (!hr)
		return;
	dev_info(hr_dev(hr), "replica: D2H id %u platform-up nibble (stub)", id);
}

static void hr_on_excp_panic(struct hi5622_pci *pci, u32 id)
{
	struct hr_replica *hr = hr_of(pci);

	if (!hr)
		return;
	hr->flags |= HR_FLAG_EXCP_SEEN;
	hr_gate_ack(hr);
	hr_capture_vector_context(hr);
	/* deliberately no H2D id 5 here: we want the device alive (SPEC.md) */
}

static void hr_on_ete_done(struct hi5622_pci *pci, u32 id)
{
	struct hr_replica *hr = hr_of(pci);

	if (!hr)
		return;
	dev_dbg(hr_dev(hr), "replica: D2H id %u ETE transfer done, draining DR0", id);
	hr_drain_dr(pci);
}

static void hr_on_pm_slp_disallow(struct hi5622_pci *pci, u32 id)
{
	struct hr_replica *hr = hr_of(pci);

	if (hr)
		dev_info(hr_dev(hr), "replica: D2H id %u device disallows sleep", id);
}

static void hr_on_pm_wkup_ok(struct hi5622_pci *pci, u32 id)
{
	struct hr_replica *hr = hr_of(pci);

	if (hr)
		dev_info(hr_dev(hr), "replica: D2H id %u device wake-up ok", id);
}

static void hr_on_wake(struct hi5622_pci *pci, u32 id)
{
	struct hr_replica *hr = hr_of(pci);

	if (!hr)
		return;
	dev_dbg(hr_dev(hr), "replica: D2H id %u wake service thread", id);
	hr_drain_dr(pci);
}

/* ---------------- H2D primitive (vendor order) [V] ------------------ */

/*
 * pcie_msg_send_irq()/pcie_msg_send() decode:
 *   1. wait until out[0] (pending mask) reads 0  [vendor pcie_msg_wait_for_clr]
 *   2. publish the coalesced pending mask to out[0]
 *   3. OR bit0 into out[2] (H2D doorbell)
 * NOTE: our pciet_msg_send() currently drains out[2] instead of out[0]; this
 * function implements the decoded order and is the one the replica uses.
 */
int hr_h2d_announce(struct hr_replica *hr, u32 id)
{
	unsigned long flags;
	u32 pending;
	unsigned int i;

	if (!hr || !hr->pci || id > 9)
		return -EINVAL;
	if (id == HR_MBOX_FORCE_BUGON)
		dev_warn(hr_dev(hr), "replica: H2D id 5 (force bugon) requested");

	spin_lock_irqsave(&hr->lock, flags);
	hr->pending |= BIT(id);
	if (hr->inflight) {
		spin_unlock_irqrestore(&hr->lock, flags);
		return 0;	/* coalesced behind the in-flight mask [V] */
	}
	spin_unlock_irqrestore(&hr->lock, flags);

	for (i = 0; i < HR_H2D_DRAIN_SPINS; i++) {
		if (!hr_read(hr, HI5622_BAR0_H2D_PENDING))
			break;
		cpu_relax();
	}
	if (i == HR_H2D_DRAIN_SPINS)
		return -EBUSY;

	spin_lock_irqsave(&hr->lock, flags);
	pending = hr->pending;
	hr->pending = 0;
	hr->inflight = pending;
	if (pending) {
		hr_write(hr, HI5622_BAR0_H2D_PENDING, pending);
		hr_write(hr, HI5622_BAR0_H2D_DOORBELL,
			 hr_read(hr, HI5622_BAR0_H2D_DOORBELL) | 1);
	}
	spin_unlock_irqrestore(&hr->lock, flags);

	dev_dbg(hr_dev(hr), "replica: H2D announce mask 0x%x", pending);
	return pending ? 0 : -EAGAIN;
}

int hr_h2d_pm_sleep(struct hr_replica *hr)
{
	if (!hr)
		return -EINVAL;
	dev_info(hr_dev(hr), "replica: PM sleep request (H2D id 1)");
	return hr_h2d_announce(hr, HR_MBOX_PM_SLEEP);
}

/* ---------------- HCC transmit (SR0) [V] ---------------------------- */

int hr_hcc_send(struct hr_replica *hr, u8 group, u8 bid, u16 id,
		const void *payload, u32 len)
{
	struct hi5622_pci *pci;
	u8 *buf;
	u32 total;
	int ret;

	if (!hr || !hr->pci || len > HR_HCC_MAX_PAYLOAD)
		return -EINVAL;
	pci = hr->pci;
	total = len + HR_HCC_HDR_LEN;
	buf = kzalloc(total, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	buf[0] = group & 0x0f;		/* type/group [V] */
	buf[1] = bid;			/* [7:4] chip/core selector [V] */
	buf[2] = 0;			/* field A */
	buf[3] = 0;			/* field B (chip id) */
	put_unaligned_le16((u16)total, buf + 4);	/* total length [V] */
	put_unaligned_le16(id, buf + 6);		/* message id [V] */
	buf[8] = 0;			/* field C */
	buf[9] = (bid >> 4) & 0x0f;	/* dst core */
	buf[10] = HR_HCC_TAG_LO;	/* 0x5a5a transport tag [V] */
	buf[11] = HR_HCC_TAG_HI;
	if (len)
		memcpy(buf + HR_HCC_HDR_LEN, payload, len);

	ret = pciet_ete_sr_send(pci, &pci->sr0, buf, total);
	kfree(buf);
	if (ret)
		return ret;
	return hr_h2d_announce(hr, HR_MBOX_ANNOUNCE);
}

/* ---------------- HCC receive / dispatch [V] ------------------------ */

static void hr_hcc_dispatch(struct hr_replica *hr, const u8 *b, u32 total,
			    u8 group, u8 chip, u8 core, u16 id)
{
	u32 plen = (total >= HR_HCC_HDR_LEN) ? total - HR_HCC_HDR_LEN : 0;
	u32 n;

	hr->stat_hcc++;
	hr->group_count[group & 0x0f]++;

	if (group != 4) {
		hr->stat_unknown++;
		if (hr->stat_unknown <= 8)
			dev_info(hr_dev(hr),
				 "replica: HCC group %u id %u chip %u core %u (%u B) not handled",
				 group, id, chip, core, plen);
		return;
	}

	switch (id) {
	case 0:
		dev_info(hr_dev(hr), "replica: g4 id0 device log blob (%u B)", plen);
		break;
	case 1:	/* device_plat_ready_msg_process [V] */
		if (chip != 0) {
			dev_warn(hr_dev(hr), "replica: g4 id1 bad chip id %u", chip);
			break;
		}
		dev_info(hr_dev(hr), "replica: Device plat ready! chip id : %u", chip);
		hr->flags |= HR_FLAG_PLAT_READY;
		hr->state = HR_ST_WAIT_DEVICE;
		complete(&hr->plat_ready);
		break;
	case 2:	/* host_ready_msg_process: 100-byte payload into read_msg [V] */
		if (chip != 0) {
			dev_warn(hr_dev(hr), "replica: g4 id2 bad chip id %u", chip);
			break;
		}
		if (total > HR_READ_MSG_LEN + 11) {	/* vendor bound: total <= 111 [V] */
			dev_warn(hr_dev(hr),
				 "replica: g4 id2 invalid length %u (max %u)",
				 total, HR_READ_MSG_LEN + 11);
			break;
		}
		n = min_t(u32, plen, HR_READ_MSG_LEN);
		if (core >= HR_READ_MSG_CORES)
			core = 0;
		memcpy(hr->read_msg[core], b + HR_HCC_HDR_LEN, n);
		hr->ready_map |= BIT(core);
		dev_info(hr_dev(hr),
			 "replica: DEVICE READY total %u payload %u core %u ready_map 0x%x",
			 total, n, core, hr->ready_map);
		if (hr->ready_map == 1) {
			hr->flags |= HR_FLAG_DEVICE_READY;
			hr->state = HR_ST_READY;
			complete(&hr->device_ready);
		}
		break;
	case 3:	/* exception_info_msg_process: 0x30-byte body [V] */
		n = min_t(u32, plen, HR_EXCP_INFO_LEN);
		memcpy(hr->excp_info, b + HR_HCC_HDR_LEN, n);
		dev_info(hr_dev(hr), "replica: exception info message (%u B)", n);
		break;
	case 4:	/* heartbeat_msg_process: restart 10 s timer [V] */
		if (hr->heartbeat_armed) {
			hrtimer_start(&hr->heartbeat,
				      ns_to_ktime(HR_HEARTBEAT_NS),
				      HRTIMER_MODE_REL);
			dev_dbg(hr_dev(hr), "replica: heartbeat restarted");
		}
		break;
	default:
		hr->stat_unknown++;
		if (hr->stat_unknown <= 8)
			dev_info(hr_dev(hr),
				 "replica: g4 id %u not implemented (%u B)", id, plen);
		break;
	}
}

static void hr_rx_frame(struct hi5622_pci *pci, void *va, u32 len)
{
	struct hr_replica *hr = hr_of(pci);
	const u8 *b = va;
	u8 group, chip, core;
	u16 id, total;

	if (!hr)
		return;
	if (len < HR_HCC_HDR_LEN) {
		hr->stat_bad_frame++;
		dev_warn(hr_dev(hr), "replica: short HCC frame (%u B)", len);
		return;
	}
	group = b[0] & 0x0f;
	chip = (b[1] >> 4) & 0x0f;
	core = b[9] & 0x0f;
	total = get_unaligned_le16(b + 4);
	id = get_unaligned_le16(b + 6);
	if (total < HR_HCC_HDR_LEN || total > len)
		total = len;
	hr_hcc_dispatch(hr, b, total, group, chip, core, id);
}

/* ETE DR drain: D2H id3 (vendor mask 0x1f), ids 6/7 wake, and READY polling */
static void hr_drain_dr(struct hi5622_pci *pci)
{
	struct hr_replica *hr = hr_of(pci);

	if (!hr)
		return;
	pciet_ete_dr_consume(hr->pci, &hr->pci->dr0, hr_rx_frame);
}

/* ---------------- service tick [V] ---------------------------------- */

int hr_service(struct hr_replica *hr)
{
	struct hi5622_pci *pci;
	u32 bitmap, id;

	if (!hr || !hr->pci || !hr->pci->bar0)
		return -EINVAL;
	pci = hr->pci;

	bitmap = hr_read(hr, HI5622_BAR0_D2H_BITMAP);
	if (!bitmap) {
		hr->stat_d2h_empty++;
		return 0;
	}
	hr->stat_events++;

	/*
	 * Vendor pcie_msg_handle order [V]: ack, read, drain, re-arm, dispatch.
	 * Polled adaptation: the word was read above; the device-visible order
	 * of the three writes is unchanged.
	 */
	hr_write(hr, HI5622_BAR0_IO(HI5622_CA_MAC_MSG_A), 1);	/* out[3] ack */
	hr_write(hr, HI5622_BAR0_D2H_BITMAP, 0);		/* out[1] drain (P2) */
	hr_write(hr, HI5622_BAR0_IO(HI5622_CA_MAC_MSG_B), 1);	/* out[4] re-arm */

	for (id = 0; id <= HI5622_D2H_MAX_ID; id++) {
		if (!(bitmap & BIT(id)))
			continue;
		if (pci->d2h[id].func) {
			pci->d2h[id].func(pci, id);
		} else if (id != 0) {
			hr->stat_unknown++;
			if (hr->stat_unknown <= 8)
				dev_info(hr_dev(hr),
					 "replica: D2H id %u has no handler", id);
		}
	}
	if (bitmap & BIT(HR_MBOX_ETE_DONE))
		hr_drain_dr(pci);
	return 1;
}

/* ---------------- wait thread / state machine [V] ------------------- */

int hr_wait_ready(struct hr_replica *hr)
{
	long ret;

	if (!hr)
		return -EINVAL;
	if (hr->flags & HR_FLAG_DEVICE_READY)
		return 0;

	hr->state = HR_ST_WAIT_PLAT;
	dev_info(hr_dev(hr), "replica: waiting plat-ready (%u ms)",
		 HR_PLAT_READY_TIMEOUT_MS);
	ret = wait_for_completion_timeout(&hr->plat_ready,
					  msecs_to_jiffies(HR_PLAT_READY_TIMEOUT_MS));
	if (!ret) {
		dev_err(hr_dev(hr), "replica: plat-ready timeout after %u ms",
			HR_PLAT_READY_TIMEOUT_MS);
		hr_dump_status(hr);
		hr->state = HR_ST_FAILED;
		return -ETIMEDOUT;
	}

	hr->state = HR_ST_WAIT_DEVICE;
	dev_info(hr_dev(hr), "replica: waiting device-ready (%u ms)",
		 HR_DEVICE_READY_TIMEOUT_MS);
	ret = wait_for_completion_timeout(&hr->device_ready,
					  msecs_to_jiffies(HR_DEVICE_READY_TIMEOUT_MS));
	if (!ret) {
		dev_err(hr_dev(hr), "replica: device-ready timeout after %u ms",
			HR_DEVICE_READY_TIMEOUT_MS);
		hr_dump_status(hr);
		hr->state = HR_ST_FAILED;
		return -ETIMEDOUT;
	}

	hr->state = HR_ST_READY;
	dev_info(hr_dev(hr), "replica: READY (plat+device ready, %u D2H words)",
		 hr->stat_events);
	return 0;
}

static enum hrtimer_restart hr_heartbeat_fn(struct hrtimer *timer)
{
	struct hr_replica *hr = container_of(timer, struct hr_replica, heartbeat);

	hr->flags |= HR_FLAG_HEARTBEAT_LOST;
	dev_err(hr_dev(hr), "replica: device heartbeat timeout; capturing context");
	hr_capture_vector_context(hr);
	return HRTIMER_NORESTART;
}

static int hr_wait_thread_fn(void *data)
{
	struct hr_replica *hr = data;
	int ret = hr_wait_ready(hr);

	if (ret == 0 && !hr->heartbeat_armed) {
		hrtimer_start(&hr->heartbeat, ns_to_ktime(HR_HEARTBEAT_NS),
			      HRTIMER_MODE_REL);
		hr->heartbeat_armed = true;
	}
	return ret;
}

int hr_on_released(struct hr_replica *hr)
{
	if (!hr || !hr->pci)
		return -EINVAL;
	hr->state = HR_ST_GATE_SERVICE;
	dev_info(hr_dev(hr),
		 "replica: release accepted; D2H service armed (poll %u ms)",
		 HR_POLL_MS);
	/* NOTE: out[1] is deliberately NOT pre-cleared: the device may already
	 * have staged the first announce; hr_service() will consume it. */
	if (!hr->wait_thread) {
		hr->wait_thread = kthread_run(hr_wait_thread_fn, hr, "hi5622_hr");
		if (IS_ERR(hr->wait_thread)) {
			hr->wait_thread = NULL;
			hr->state = HR_ST_FAILED;
			return -ENOMEM;
		}
	}
	return 0;
}

/* ---------------- status -------------------------------------------------- */

void hr_dump_status(struct hr_replica *hr)
{
	if (!hr || !hr->pci)
		return;
	dev_info(hr_dev(hr),
		 "replica: state %d flags 0x%x ready_map 0x%x events %u gate_acks %u excp %u hcc %u unknown %u bad %u mask 0x%08x",
		 hr->state, hr->flags, hr->ready_map, hr->stat_events,
		 hr->stat_gate_ack, hr->stat_excp, hr->stat_hcc,
		 hr->stat_unknown, hr->stat_bad_frame, hr->vector_mask);
}

/* ---------------- attach / detach ----------------------------------------- */

int hr_attach(struct hi5622_pci *pci, struct hr_replica *hr)
{
	unsigned int i, slot = HR_MAX_INSTANCES;

	if (!pci || !hr)
		return -EINVAL;
	BUILD_BUG_ON(sizeof(struct hcc_msg_header) != 10);
	BUILD_BUG_ON(sizeof(struct hi5622_ete_node) != 8);
	BUILD_BUG_ON(HR_HCC_HDR_LEN != 12);

	for (i = 0; i < HR_MAX_INSTANCES; i++) {
		if (!hr_instances[i]) {
			slot = i;
			break;
		}
	}
	if (slot == HR_MAX_INSTANCES)
		return -ENOSPC;

	memset(hr, 0, sizeof(*hr));
	hr->pci = pci;
	hr->dev = &pci->pdev->dev;
	hr->state = HR_ST_IDLE;
	spin_lock_init(&hr->lock);
	init_completion(&hr->plat_ready);
	init_completion(&hr->device_ready);
	hrtimer_setup(&hr->heartbeat, hr_heartbeat_fn, CLOCK_MONOTONIC,
		      HRTIMER_MODE_REL);
	hr_instances[slot] = hr;

	pciet_d2h_register(pci, HR_MBOX_DEV_PLAT_READY, hr_on_dev_plat_ready);
	pciet_d2h_register(pci, HR_MBOX_EXCP_PANIC, hr_on_excp_panic);
	pciet_d2h_register(pci, HR_MBOX_ETE_DONE, hr_on_ete_done);
	pciet_d2h_register(pci, HR_MBOX_PM_SLP_DISALLOW, hr_on_pm_slp_disallow);
	pciet_d2h_register(pci, HR_MBOX_PM_WKUP_OK, hr_on_pm_wkup_ok);
	pciet_d2h_register(pci, HR_MBOX_WAKE6, hr_on_wake);
	pciet_d2h_register(pci, HR_MBOX_WAKE7, hr_on_wake);

	dev_info(hr->dev, "host replica attached (D2H ids 1-7, gate service armed)");
	return 0;
}

void hr_detach(struct hr_replica *hr)
{
	unsigned int i;

	if (!hr)
		return;
	hrtimer_cancel(&hr->heartbeat);
	if (hr->wait_thread) {
		complete_all(&hr->plat_ready);
		complete_all(&hr->device_ready);
		kthread_stop(hr->wait_thread);
		hr->wait_thread = NULL;
	}
	for (i = 0; i < HR_MAX_INSTANCES; i++)
		if (hr_instances[i] == hr)
			hr_instances[i] = NULL;
}
