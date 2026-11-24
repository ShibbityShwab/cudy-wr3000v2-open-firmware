// SPDX-License-Identifier: GPL-2.0
/*
 * hcc_core.c - host-side HCC protocol core for the Hi5622V100 clean-room
 * PCIe/ETE transport (corrected GP8 boot protocol).
 *
 * ORIGINAL code by the wifi-hcc-core lane (task-92, 2026-11-24), written from
 * the project's decoded contract (see hcc_core.h for the source list).  Every
 * address/value below is a derived annotation with an evidence marker; no
 * vendor byte, object code, calibration value or secret is reproduced.
 *
 * Design rules encoded here (see REPORT.md / INTEGRATION.md):
 *   - the boot handshake gate (page CA 0x1bf000 +8 == 2) is LATCHED once seen,
 *     so a patched boot that overwrites the page later does not lose the gate;
 *   - the ready pair (chip-group-4 id1/id2) is accepted from BOTH receive
 *     paths - the direct device group-A reader (the working one) and the
 *     legacy engine-copied host-DR path;
 *   - CECE (CA 0x4000010c = 0x0000cece) is written only AFTER the boot latch
 *     and only on observed handler-spin evidence, at most once per event;
 *   - every device write goes through hcc_write(), which enforces the
 *     allowlist: out[5] CA 0x400392f0 is never writable, and neither is the
 *     transport-owned CPU release word CA 0x40000108;
 *   - the H2D mailbox follows pcie_msg_send exactly (pending/in-flight
 *     coalescing, out[0] publish, out[2] |= 1 doorbell) and the polled wait
 *     for out[0] == 0 is bounded (vendor bound 0xfffe polls);
 *   - no firmware/device RAM store exists in this file; no clock or delay
 *     primitive is used - time is passed in by the caller (deterministic).
 */
#include "hcc_core.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/kernel.h>
#else
#include <errno.h>
#include <stdio.h>
#endif

/* =====================================================================
 * 1. Address helpers and pure contract helpers
 * ===================================================================== */

static bool hcc_io_ca_ok(hcc_u32 ca)
{
	return ca >= HCC_REGION_IO_CA_LO && ca <= HCC_REGION_IO_CA_HI;
}

hcc_u32 hcc_bar0_io(hcc_u32 ca)
{
	if (!hcc_io_ca_ok(ca))
		return 0;
	return HCC_REGION_IO_BAR0 + (ca - HCC_REGION_IO_CA_LO);
}

hcc_u32 hcc_bar0_fw(hcc_u32 cpu)
{
	if (cpu > HCC_FW_CPU_HI)
		return 0;
	return HCC_FW_RAM_BAR0 + cpu;
}

hcc_u32 hcc_idx_plus(hcc_u32 idx, hcc_u32 depth)
{
	hcc_u32 next = idx + 1u;

	if ((next & HCC_IDX_LOW_MASK) == depth) {
		next &= ~HCC_IDX_LOW_MASK;
		next ^= HCC_IDX_PHASE_BIT;
	}
	return next;
}

hcc_u32 hcc_idx_count(hcc_u32 from, hcc_u32 to, hcc_u32 depth)
{
	hcc_s32 phase = (hcc_s32)((to >> 10) & 1u) - (hcc_s32)((from >> 10) & 1u);
	hcc_s32 delta = phase * (hcc_s32)depth +
			(hcc_s32)(to & HCC_IDX_LOW_MASK) -
			(hcc_s32)(from & HCC_IDX_LOW_MASK);

	if (delta < 0)
		delta += 2 * (hcc_s32)depth;
	return (hcc_u32)delta;
}

hcc_u32 hcc_idx_slot(hcc_u32 idx)
{
	return idx & HCC_IDX_LOW_MASK;
}

hcc_u32 hcc_sr_node_word1(hcc_u32 total)
{
	return (total << 16) | HCC_SR_NODE_FLAGS;
}

/* The only device CAs this core may ever write.  out[5] is deliberately
 * absent (project hard rule); CA 0x40000108 is transport-owned. */
bool hcc_ca_writable(hcc_u32 ca)
{
	switch (ca) {
	case HCC_CA_D2H_ACK:
	case HCC_CA_D2H_BITMAP:
	case HCC_CA_D2H_REARM:
	case HCC_CA_H2D_PENDING:
	case HCC_CA_H2D_DOORBELL:
	case HCC_CA_GATE:
		return true;
	default:
		return false;
	}
}

/* =====================================================================
 * 2. Internal IO wrappers (read census + write allowlist)
 * ===================================================================== */

static hcc_u32 hcc_read(struct hcc_core *c, hcc_u32 ca)
{
	if (!hcc_io_ca_ok(ca) || !c->io.read32) {
		c->bad_reads++;
		return 0xffffffffu;
	}
	c->poll_reads++;
	return c->io.read32(c->io.priv, hcc_bar0_io(ca));
}

static hcc_u32 hcc_read_fw(struct hcc_core *c, hcc_u32 cpu)
{
	if (cpu > HCC_FW_CPU_HI || !c->io.read32) {
		c->bad_reads++;
		return 0u;
	}
	c->poll_reads++;
	return c->io.read32(c->io.priv, HCC_FW_RAM_BAR0 + cpu);
}

static int hcc_write(struct hcc_core *c, hcc_u32 ca, hcc_u32 val)
{
	if (!hcc_io_ca_ok(ca) || !hcc_ca_writable(ca) || !c->io.write32) {
		c->bad_writes++;
		c->last_write_ca = ca;
		return -EPERM;
	}
	c->io.write32(c->io.priv, hcc_bar0_io(ca), val);
	c->writes++;
	c->last_write_ca = ca;
	c->last_write_val = val;
	return 0;
}

static void hcc_emit(struct hcc_core *c, enum hcc_event ev, hcc_u32 data)
{
	if (c->on_event)
		c->on_event(c->on_event_priv, ev, data);
}

/* =====================================================================
 * 3. Frame contract (12-byte header, tag 0x5a5a, little-endian)
 * ===================================================================== */

static hcc_u16 hcc_rd16(const hcc_u8 *p)
{
	return (hcc_u16)((hcc_u16)p[0] | ((hcc_u16)p[1] << 8));
}

static void hcc_wr16(hcc_u8 *p, hcc_u16 v)
{
	p[0] = (hcc_u8)(v & 0xffu);
	p[1] = (hcc_u8)((v >> 8) & 0xffu);
}

/*
 * Parse the 12-byte header of a frame whose full wire length is known.
 * "wire_len" is the node length from the device/DMA descriptor; the header's
 * u16 total must equal it (the firmware's own receive check compares tag and
 * length, so a frame that fails this would have been dropped there too).
 */
static int hcc_frame_parse(const hcc_u8 *buf, hcc_u32 wire_len,
			   struct hcc_frame *f)
{
	hcc_u16 total;

	if (!buf || !f)
		return -EINVAL;
	if (wire_len < HCC_FRAME_HDR)
		return -EMSGSIZE;
	memset(f, 0, sizeof(*f));
	f->group = (hcc_u8)(buf[0] & 0x0fu);
	f->alloc = (hcc_u8)(buf[1] & 0x0fu);
	f->core = (hcc_u8)((buf[1] >> 4) & 0x0fu);
	f->field_a = buf[2];
	f->field_b = buf[3];
	f->total = hcc_rd16(buf + HCC_FRAME_LEN_OFF);
	f->id = hcc_rd16(buf + HCC_FRAME_ID_OFF);
	f->field_c = buf[8];
	f->dst = buf[HCC_FRAME_DST_OFF];
	f->tag = hcc_rd16(buf + HCC_FRAME_TAG_OFF);

	total = f->total;
	if (f->tag != HCC_FRAME_TAG)
		return -EPROTO;
	if (total < HCC_FRAME_HDR || total > HCC_FRAME_MAX_TOTAL)
		return -EMSGSIZE;
	if ((hcc_u32)total != wire_len)
		return -EMSGSIZE;
	return 0;
}

int hcc_core_frame_decode(const hcc_u8 *buf, hcc_u32 len, struct hcc_frame *f)
{
	return hcc_frame_parse(buf, len, f);
}

int hcc_core_frame_build(struct hcc_core *c, hcc_u8 *buf, hcc_u32 cap,
			 hcc_u8 group, hcc_u16 id, hcc_u8 dst,
			 const hcc_u8 *payload, hcc_u32 plen, hcc_u32 *total)
{
	hcc_u32 tlen;

	(void)c;
	if (!buf || !total)
		return -EINVAL;
	if (group == HCC_GROUP_PLAT)
		return -EACCES;		/* chip-group-4 is device->host only */
	if (group != HCC_GROUP_H2D_CFG && group != HCC_GROUP_H2D_ALT)
		return -EINVAL;		/* firmware H2D tables are group 0 and 2 */
	if (id == HCC_H2D_ID_BUGON)
		return -EACCES;		/* firmware BUG() trap: never post */
	if (group > 0x0fu || dst > 0x0fu)
		return -ERANGE;
	if (plen > HCC_FRAME_MAX_PAYLOAD)
		return -ERANGE;
	if (plen && !payload)
		return -EINVAL;
	tlen = plen + HCC_FRAME_HDR;
	if (cap < tlen)
		return -ENOSPC;

	memset(buf, 0, HCC_FRAME_HDR);
	buf[0] = (hcc_u8)(group & 0x0fu);
	buf[1] = (hcc_u8)(((dst & 0x0fu) << 4) | 0x01u);	/* allocated nibble [V] */
	hcc_wr16(buf + HCC_FRAME_LEN_OFF, (hcc_u16)tlen);
	hcc_wr16(buf + HCC_FRAME_ID_OFF, id);
	buf[HCC_FRAME_DST_OFF] = (hcc_u8)(dst & 0x0fu);
	hcc_wr16(buf + HCC_FRAME_TAG_OFF, HCC_FRAME_TAG);
	if (plen)
		memcpy(buf + HCC_FRAME_HDR, payload, plen);
	*total = tlen;
	return 0;
}

int hcc_core_txq_sync_build(struct hcc_core *c, hcc_u8 *buf, hcc_u32 cap,
			    const hcc_u8 *payload, hcc_u32 plen,
			    hcc_u32 *total)
{
	return hcc_core_frame_build(c, buf, cap, HCC_H2D_TXQ_SYNC_GROUP,
				    HCC_H2D_TXQ_SYNC_ID, 0, payload, plen,
				    total);
}

/* =====================================================================
 * 4. Boot protocol: handshake latch + optional bounded CEED repair
 * ===================================================================== */

void hcc_core_note_release(struct hcc_core *c, hcc_u64 now_ms)
{
	if (!c || c->released)
		return;
	c->released = true;
	c->phase = HCC_PHASE_BOOT;
	c->boot_deadline_ms = now_ms + c->cfg.boot_timeout_ms;
	c->now_ms = now_ms;
}

int hcc_core_boot_check(struct hcc_core *c, hcc_u64 now_ms)
{
	hcc_u32 w[4];
	hcc_u32 i;
	int changed = 0;

	if (!c->released)
		return 0;

	for (i = 0; i < 4u; i++)
		w[i] = hcc_read_fw(c, HCC_CPU_HANDSHAKE_PAGE + 4u * i);
	for (i = 0; i < 4u; i++)
		c->handshake[i] = w[i];
	c->boot_checks++;

	if (!c->boot_latched) {
		if (w[2] == HCC_HS_ACK_GOLDEN) {
			c->boot_latched = true;
			c->boot_latched_ms = now_ms;
			c->handshake_seen_valid = true;
			c->handshake_golden =
				(w[0] == HCC_HS_W0_GOLDEN &&
				 w[1] == HCC_HS_W1_GOLDEN &&
				 w[3] == HCC_HS_W3_GOLDEN);
			c->fail_reason = HCC_FAIL_NONE;
			c->phase = HCC_PHASE_WAIT_PLAT_READY;
			c->deadline_ms = now_ms + c->cfg.plat_ready_ms;
			hcc_emit(c, HCC_EVENT_BOOT_LATCHED,
				 c->handshake_golden ? 1u : 0u);
			changed++;
		} else if (c->cfg.allow_ceed_repair &&
			   c->ceed_repairs == 0u) {
			hcc_u32 gate = hcc_read(c, HCC_CA_GATE) & 0xffffu;

			if (gate != HCC_GATE_CEED) {
				if (hcc_write(c, HCC_CA_GATE,
					      HCC_GATE_CEED) == 0) {
					c->ceed_repairs++;
					hcc_emit(c, HCC_EVENT_BOOT_REPAIRED, 1u);
					changed++;
				}
			}
		}
	} else {
		/* The handshake page can be overwritten by the firmware after
		 * the boot stage (observed on the PD patched image).  Latch is
		 * authoritative; count the observations, never un-latch. */
		if (w[0] != HCC_HS_W0_GOLDEN || w[1] != HCC_HS_W1_GOLDEN ||
		    w[2] != HCC_HS_ACK_GOLDEN || w[3] != HCC_HS_W3_GOLDEN)
			c->page_lost_after_latch++;
	}
	return changed;
}

int hcc_core_exception_watch(struct hcc_core *c)
{
	hcc_u32 mask;

	if (!c->boot_latched)
		return 0;

	mask = hcc_read_fw(c, HCC_CPU_EXCP_MASK) & 0xffffu;
	if (!c->exception_mask_valid) {
		c->exception_mask_valid = true;
		c->exception_mask = mask;
		if (mask) {
			c->ev_mask = true;
			return 1;
		}
		return 0;
	}
	if (mask != c->exception_mask) {
		c->exception_mask = mask;
		c->exception_mask_changes++;
		c->ev_mask = true;
		return 1;
	}
	return 0;
}

/* =====================================================================
 * 5. D2H mailbox service (vendor pcie_msg_handle order)
 * ===================================================================== */

static void hcc_d2h_capture_vector(struct hcc_core *c)
{
	hcc_u32 i;

	c->vector_mask = hcc_read_fw(c, HCC_CPU_EXCP_MASK) & 0xffffu;
	for (i = 0; i < HCC_VECTOR_WORDS; i++)
		c->vector_frame[i] = hcc_read_fw(c, HCC_CPU_VECTOR_FRAME + 4u * i);
	c->vector_capture_count++;
	hcc_emit(c, HCC_EVENT_VECTOR_CAPTURE, c->vector_mask);
}

/* id 1: pcie_dev_ready_msg_handle - empty vendor stub [V] */
static void hcc_d2h_id1(struct hcc_core *c)
{
	(void)c;
}

/* id 2: exception_panic_msg_process - startup/vector announce (out[1] 0x4).
 * The vendor logs the panic and captures the staged context; we capture it
 * read-only and never send H2D id 5 (the vendor's force-bugon reply) [V]. */
static void hcc_d2h_id2(struct hcc_core *c)
{
	c->excp_announce = true;
	c->ev_bit2 = true;
	hcc_d2h_capture_vector(c);
}

/* id 3: pcie_ete_transfer_done_handle - drain DR channels mask 0x1f [V] */
static void hcc_d2h_id3(struct hcc_core *c)
{
	c->d2h_need_dr_drain++;
}

/* id 4: wlan_pm_slp_disallow_callback - the device refuses sleep [V] */
static void hcc_d2h_id4(struct hcc_core *c)
{
	c->pm_sleep_disallow = true;
	c->pm_state_changes++;
	if (c->pm_sleep_pending) {
		c->pm_sleep_pending = false;
		c->pm_sleep_aborts++;
	}
	hcc_emit(c, HCC_EVENT_PM_STATE, 4u);
}

/* id 5: wlan_pm_wkup_dev_ok_callback - device wake-up is OK [V] */
static void hcc_d2h_id5(struct hcc_core *c)
{
	c->pm_wake_ok = true;
	c->pm_state_changes++;
	hcc_emit(c, HCC_EVENT_PM_STATE, 5u);
}

/* ids 6/7: pcie_trigger_ete_sending_handle - wake the TX/service thread [V].
 * id 6 is the deposit notify (no CECE evidence); id 7 is the sending variant. */
static void hcc_d2h_id6(struct hcc_core *c)
{
	c->d2h_need_dr_drain++;
	c->d2h_wake_requests++;
}

static void hcc_d2h_id7(struct hcc_core *c)
{
	c->d2h_need_dr_drain++;
	c->d2h_wake_requests++;
}

struct hcc_d2h_entry {
	const char *name;
	void (*fn)(struct hcc_core *c);
};

/* The fully-loaded vendor driver registers exactly ids {1,2,3,4,5,6,7};
 * ids 0/8/9/10 have no handler and are dropped; the loop stops above 10 [V]. */
static const struct hcc_d2h_entry hcc_d2h_table[HCC_D2H_MAX_ID + 1] = {
	{ "id0-none", 0 },					/* dropped */
	{ "pcie_dev_ready_msg_handle", hcc_d2h_id1 },
	{ "exception_panic_msg_process", hcc_d2h_id2 },
	{ "pcie_ete_transfer_done_handle", hcc_d2h_id3 },
	{ "wlan_pm_slp_disallow_callback", hcc_d2h_id4 },
	{ "wlan_pm_wkup_dev_ok_callback", hcc_d2h_id5 },
	{ "pcie_trigger_ete_sending_handle", hcc_d2h_id6 },
	{ "pcie_trigger_ete_sending_handle", hcc_d2h_id7 },
	{ "id8-none", 0 },
	{ "id9-none", 0 },
	{ "id10-none", 0 },
};

static hcc_u32 hcc_popcount32(hcc_u32 v)
{
	hcc_u32 n = 0;

	while (v) {
		v &= v - 1u;
		n++;
	}
	return n;
}

static hcc_u32 hcc_ctz32(hcc_u32 v)
{
	hcc_u32 n = 0;

	if (!v)
		return 0;
	while (!(v & 1u)) {
		v >>= 1;
		n++;
	}
	return n;
}

static int hcc_d2h_dispatch(struct hcc_core *c, hcc_u32 bitmap)
{
	hcc_u32 b = bitmap;
	int serviced = 0;

	while (b) {
		hcc_u32 id = hcc_ctz32(b);
		hcc_u32 bit = 1u << id;

		if (id > HCC_D2H_MAX_ID) {
			c->d2h_stop_gt10++;
			break;
		}
		b &= ~bit;
		c->d2h_id_count[id]++;
		if (hcc_d2h_table[id].fn) {
			hcc_d2h_table[id].fn(c);
			if (id != 6u)
				c->ev_handler = true;
			hcc_emit(c, HCC_EVENT_D2H_DISPATCH, id);
			serviced++;
		} else {
			c->d2h_dropped++;
			hcc_emit(c, HCC_EVENT_D2H_DROPPED, id);
		}
	}
	return serviced;
}

int hcc_core_d2h_poll(struct hcc_core *c, hcc_u64 now_ms)
{
	hcc_u32 first, more, bitmap;
	int serviced = 0;

	(void)now_ms;
	if (!c->released)
		return 0;

	/* Probe read first: a zero word performs no device writes at all. */
	first = hcc_read(c, HCC_CA_D2H_BITMAP);
	if (first == 0)
		return 0;

	/* Vendor order [V]: ack out[3]=1 FIRST, re-read out[1], drain out[1]=0,
	 * re-arm out[4]=1; only then dispatch (lowest set bit first). */
	(void)hcc_write(c, HCC_CA_D2H_ACK, 1u);
	more = hcc_read(c, HCC_CA_D2H_BITMAP);
	bitmap = first | more;
	c->d2h_coalesced_bits += hcc_popcount32(more & ~first);
	(void)hcc_write(c, HCC_CA_D2H_BITMAP, 0u);
	(void)hcc_write(c, HCC_CA_D2H_REARM, 1u);

	c->d2h_words++;
	c->d2h_last_bitmap = bitmap;
	c->d2h_hist[c->d2h_hist_pos % 16u] = bitmap;
	c->d2h_hist_pos++;

	if (bitmap & (1u << 2)) {	/* out[1] bit2 = startup/vector announce */
		c->excp_announce = true;
		c->ev_bit2 = true;
	}
	hcc_emit(c, HCC_EVENT_D2H_WORD, bitmap);

	serviced += hcc_d2h_dispatch(c, bitmap);
	serviced += hcc_core_cece_service(c) > 0 ? 1 : 0;
	return serviced + 1;
}

/* =====================================================================
 * 6. CECE gate policy (corrected: post-latch, evidence-driven, bounded)
 * ===================================================================== */

int hcc_core_cece_service(struct hcc_core *c)
{
	bool evidence = c->ev_bit2 || c->ev_handler || c->ev_mask;
	int changed = 0;

	if (c->cece_mode == HCC_CECE_MODE_OFF) {
		if (evidence || c->cece_pending) {
			c->cece_blocked_mode++;
			hcc_emit(c, HCC_EVENT_CECE_REFUSED, 2u);
		}
		c->ev_bit2 = false;
		c->ev_handler = false;
		c->ev_mask = false;
		c->cece_pending = false;
		return 0;
	}

	if (!c->boot_latched) {
		if (evidence) {
			c->cece_blocked_boot++;
			hcc_emit(c, HCC_EVENT_CECE_REFUSED, 1u);
		}
		c->ev_bit2 = false;
		c->ev_handler = false;
		c->ev_mask = false;
		c->cece_pending = false;
		return 0;
	}

	if (evidence) {
		c->cece_pending = true;
		c->cece_evidence_events++;
		c->ev_bit2 = false;
		c->ev_handler = false;
		c->ev_mask = false;
	}

	if (c->cece_pending) {
		if (hcc_write(c, HCC_CA_GATE, HCC_GATE_CECE) == 0) {
			c->cece_writes++;
			c->cece_pending = false;
			hcc_emit(c, HCC_EVENT_CECE_WRITE, HCC_GATE_CECE);
			changed++;
		}
	}
	return changed;
}

int hcc_core_cece_kick(struct hcc_core *c)
{
	if (c->cece_mode != HCC_CECE_MODE_KICK) {
		c->cece_blocked_mode++;
		hcc_emit(c, HCC_EVENT_CECE_REFUSED, 2u);
		return -EPERM;
	}
	if (!c->boot_latched) {
		c->cece_blocked_boot++;
		hcc_emit(c, HCC_EVENT_CECE_REFUSED, 1u);
		return -EPERM;
	}
	if (c->cece_kicks >= 3u) {
		hcc_emit(c, HCC_EVENT_CECE_REFUSED, 3u);
		return -EAGAIN;
	}
	if (hcc_write(c, HCC_CA_GATE, HCC_GATE_CECE) != 0)
		return -EIO;
	c->cece_kicks++;
	c->cece_writes++;
	hcc_emit(c, HCC_EVENT_CECE_WRITE, HCC_GATE_CECE);
	return 0;
}

/* =====================================================================
 * 7. Ready state machine (chip-group-4 id1 -> id2), fed by both paths
 * ===================================================================== */

static void hcc_core_fail(struct hcc_core *c, enum hcc_fail_reason reason)
{
	c->phase = HCC_PHASE_FAILED;
	c->fail_reason = reason;
	hcc_emit(c, HCC_EVENT_TIMEOUT, (hcc_u32)reason);
}

static int hcc_core_ready_tick(struct hcc_core *c, hcc_u64 now_ms)
{
	if (c->phase == HCC_PHASE_BOOT) {
		if (c->boot_deadline_ms && now_ms >= c->boot_deadline_ms) {
			hcc_core_fail(c, HCC_FAIL_BOOT_TIMEOUT);
			return 1;
		}
	} else if (c->phase == HCC_PHASE_WAIT_PLAT_READY) {
		if (now_ms >= c->deadline_ms) {
			hcc_core_fail(c, HCC_FAIL_PLAT_READY_TIMEOUT);
			return 1;
		}
	} else if (c->phase == HCC_PHASE_WAIT_DEVICE_READY) {
		if (now_ms >= c->deadline_ms) {
			hcc_core_fail(c, HCC_FAIL_DEVICE_READY_TIMEOUT);
			return 1;
		}
	}
	return 0;
}

static void hcc_g4_plat_ready(struct hcc_core *c, const struct hcc_frame *f,
			      enum hcc_src src, hcc_u64 now_ms)
{
	c->plat_ready_count++;
	if (f->total != HCC_G4_ID1_TOTAL) {
		c->id1_bad_total++;
		return;
	}
	if (f->core != 0u) {		/* vendor: header chip id must be 0 */
		c->id1_bad_core++;
		return;
	}
	if (c->plat_ready)
		return;			/* duplicate: transition already done */
	if (c->phase != HCC_PHASE_WAIT_PLAT_READY)
		return;
	c->plat_ready = true;
	c->phase = HCC_PHASE_WAIT_DEVICE_READY;
	c->deadline_ms = now_ms + c->cfg.dev_ready_ms;
	hcc_emit(c, HCC_EVENT_PLAT_READY, (hcc_u32)src);
}

static void hcc_g4_device_ready(struct hcc_core *c, const struct hcc_frame *f,
				const hcc_u8 *payload, hcc_u32 avail,
				enum hcc_src src, hcc_u64 now_ms)
{
	hcc_u32 plen;
	hcc_u32 copy;

	c->dev_ready_count++;
	if (f->core != 0u) {
		c->id2_bad_core++;
		return;
	}
	if (f->total < HCC_G4_ID2_TOTAL_MIN || f->total > HCC_G4_ID2_TOTAL_MAX) {
		c->id2_bad_len++;
		return;
	}
	if (c->dev_ready)
		return;
	if (c->phase != HCC_PHASE_WAIT_DEVICE_READY)
		return;

	plen = (hcc_u32)f->total - HCC_FRAME_HDR;
	copy = plen < avail ? plen : avail;
	if (copy > READ_PAYLOAD_MAX)
		copy = READ_PAYLOAD_MAX;
	c->dev_ready = true;
	c->id2_plen = (hcc_u16)plen;
	memset(c->id2_payload, 0, sizeof(c->id2_payload));
	if (copy)
		memcpy(c->id2_payload, payload, copy);
	/* vendor host_ready_msg_process copies the payload into the 100-byte
	 * per-core read_msg record; only dword0 is used by the consumer [V]. */
	memset(c->read_msg[0], 0, HCC_READ_MSG_LEN);
	if (copy)
		memcpy(c->read_msg[0], payload,
		       copy < HCC_READ_MSG_LEN ? copy : HCC_READ_MSG_LEN);
	c->ready_map |= 1u << f->core;
	c->phase = HCC_PHASE_READY_TXQ_SYNC;
	c->deadline_ms = 0;
	(void)now_ms;
	hcc_emit(c, HCC_EVENT_DEVICE_READY, (hcc_u32)src);
}

int hcc_core_deliver_hdr(struct hcc_core *c, enum hcc_src src,
			 const hcc_u8 *hdr, const hcc_u8 *payload,
			 hcc_u32 payload_avail, hcc_u32 wire_len,
			 hcc_u64 now_ms)
{
	struct hcc_frame f;
	hcc_u32 plen;
	hcc_u32 copy;
	int rc;

	if (!c->boot_latched)
		return -EPERM;
	rc = hcc_frame_parse(hdr, wire_len, &f);
	if (rc < 0) {
		c->frames_invalid++;
		hcc_emit(c, HCC_EVENT_FRAME_INVALID, (hcc_u32)(-rc));
		return rc;
	}
	c->frames_seen++;
	c->last_frame = f;
	plen = (hcc_u32)f.total - HCC_FRAME_HDR;
	copy = plen < payload_avail ? plen : payload_avail;

	if (f.group == HCC_GROUP_PLAT) {
		switch (f.id) {
		case HCC_G4_ID_PLAT_READY:
			hcc_g4_plat_ready(c, &f, src, now_ms);
			break;
		case HCC_G4_ID_DEV_READY:
			hcc_g4_device_ready(c, &f, payload, copy, src, now_ms);
			break;
		case HCC_G4_ID_EXCP_INFO:
			c->g4_excp_info++;
			break;
		case HCC_G4_ID_HEARTBEAT:
			c->hb_count++;
			if (c->hb_armed)
				c->hb_deadline_ms = now_ms + c->cfg.heartbeat_ms;
			hcc_emit(c, HCC_EVENT_HEARTBEAT, c->hb_count);
			break;
		case HCC_G4_ID_OAM_LOG:
			c->g4_oam_log++;
			break;
		default:
			c->frames_unknown++;
			hcc_emit(c, HCC_EVENT_FRAME_UNKNOWN,
				 ((hcc_u32)f.group << 8) | f.id);
			break;
		}
	} else if (f.group == 3u) {
		c->g3_frames++;
		if (f.id == 0x12u)	/* hmac_d2h_temp_state_update [V] */
			c->g3_temp_state++;
	} else {
		c->frames_unknown++;
		hcc_emit(c, HCC_EVENT_FRAME_UNKNOWN,
			 ((hcc_u32)f.group << 8) | f.id);
	}
	return 0;
}

int hcc_core_deliver_frame(struct hcc_core *c, enum hcc_src src,
			   const hcc_u8 *frame, hcc_u32 len, hcc_u64 now_ms)
{
	if (!frame || len < HCC_FRAME_HDR)
		return -EINVAL;
	return hcc_core_deliver_hdr(c, src, frame, frame + HCC_FRAME_HDR,
				    len - HCC_FRAME_HDR, len, now_ms);
}

/* =====================================================================
 * 8. H2D mailbox: vendor pcie_msg_send coalescing state machine
 * ===================================================================== */

static int hcc_mbox_publish(struct hcc_core *c, hcc_u64 now_ms)
{
	hcc_u32 doorbell;

	if (hcc_write(c, HCC_CA_H2D_PENDING, c->mbox_inflight) != 0)
		return -EIO;
	doorbell = hcc_read(c, HCC_CA_H2D_DOORBELL);
	if (hcc_write(c, HCC_CA_H2D_DOORBELL, doorbell | 1u) != 0)
		return -EIO;
	c->mbox_publish_ms = now_ms;
	c->mbox_publishes++;
	hcc_emit(c, HCC_EVENT_H2D_SENT, c->mbox_inflight);
	return 0;
}

int hcc_core_mbox_send(struct hcc_core *c, hcc_u8 id, hcc_u64 now_ms)
{
	hcc_u32 bit;

	if (!c->released || !c->boot_latched || c->phase == HCC_PHASE_FAILED) {
		c->h2d_refused++;
		hcc_emit(c, HCC_EVENT_H2D_REFUSED, id);
		return -EPERM;
	}
	if (id > 9u) {
		c->h2d_refused++;
		hcc_emit(c, HCC_EVENT_H2D_REFUSED, id);
		return -EINVAL;
	}
	if (id == HCC_H2D_ID_BUGON) {
		c->h2d_refused++;
		hcc_emit(c, HCC_EVENT_H2D_REFUSED, id);
		return -EACCES;
	}
	if (id == HCC_H2D_ID_PM_SLEEP && !c->cfg.pm_enabled) {
		c->h2d_refused++;
		hcc_emit(c, HCC_EVENT_H2D_REFUSED, id);
		return -EOPNOTSUPP;
	}
	if (id != HCC_H2D_ID_ETE_ANNOUNCE && id != HCC_H2D_ID_PM_SLEEP) {
		c->h2d_refused++;
		hcc_emit(c, HCC_EVENT_H2D_REFUSED, id);
		return -EPERM;
	}
	if (c->h2d_lane_closed) {
		c->h2d_refused++;
		hcc_emit(c, HCC_EVENT_H2D_REFUSED, id);
		return -EBUSY;
	}

	bit = 1u << id;
	if (id == HCC_H2D_ID_ETE_ANNOUNCE)
		c->h2d_announces++;

	c->mbox_pending |= bit;
	if (c->mbox_inflight != 0) {
		/* Coalesced: merged into the next batch [V] pcie_msg_send. */
		c->mbox_coalesced_batches++;
		return 0;
	}
	c->mbox_inflight = c->mbox_pending;
	c->mbox_pending = 0;
	return hcc_mbox_publish(c, now_ms);
}

int hcc_core_mbox_tick(struct hcc_core *c, hcc_u64 now_ms)
{
	hcc_u32 out0;

	if (!c->released || c->mbox_inflight == 0)
		return 0;

	/* Polled substitute for pcie_msg_wait_for_clr(): the device clears
	 * out[0] (CA 0x40039010) once it consumes the pending mask [V]. */
	out0 = hcc_read(c, HCC_CA_H2D_PENDING);
	c->last_out0 = out0;
	if (out0 == 0u) {
		hcc_u32 done = c->mbox_inflight;

		c->mbox_completions++;
		c->mbox_inflight = 0;
		hcc_emit(c, HCC_EVENT_H2D_COMPLETE, done);
		if (c->mbox_pending) {
			c->mbox_inflight = c->mbox_pending;
			c->mbox_pending = 0;
			(void)hcc_mbox_publish(c, now_ms);
		}
		return 1;
	}
	if (c->cfg.mbox_clear_timeout_ms &&
	    now_ms >= c->mbox_publish_ms + c->cfg.mbox_clear_timeout_ms) {
		hcc_u32 stuck = c->mbox_inflight;

		c->mbox_timeouts++;
		c->h2d_lane_closed = true;
		c->mbox_inflight = 0;
		c->mbox_pending = 0;
		hcc_emit(c, HCC_EVENT_H2D_TIMEOUT, stuck);
		return 1;
	}
	return 0;
}

int hcc_core_sr_commit(struct hcc_core *c, hcc_u8 chan, hcc_u32 devva,
		       hcc_u8 group, hcc_u16 id, hcc_u32 total, hcc_u64 now_ms)
{
	struct hcc_sr_ring *r;

	if (!c->released || !c->boot_latched || c->phase == HCC_PHASE_FAILED)
		return -EPERM;
	if (chan >= HCC_SR_CHANNELS)
		return -EINVAL;
	if (total < HCC_FRAME_HDR || total > HCC_FRAME_MAX_TOTAL)
		return -ERANGE;
	if (group != HCC_GROUP_H2D_CFG && group != HCC_GROUP_H2D_ALT)
		return -EACCES;
	if (id == HCC_H2D_ID_BUGON)
		return -EACCES;
	if (devva < HCC_DMA_WIN_LO || devva > HCC_DMA_WIN_HI)
		return -ERANGE;		/* host-DRAM identity window [V*] */

	r = &c->sr[chan];
	if (r->depth == 0u)
		r->depth = HCC_RING_DEPTH_DEFAULT;
	r->last_slot = hcc_idx_slot(r->producer);
	r->last_devva = devva;
	r->last_word1 = hcc_sr_node_word1(total);
	r->last_group = group;
	r->last_id = id;
	r->last_total = total;
	r->producer = hcc_idx_plus(r->producer, r->depth);
	r->nodes++;

	if (c->phase == HCC_PHASE_READY_TXQ_SYNC &&
	    group == HCC_H2D_TXQ_SYNC_GROUP && id == HCC_H2D_TXQ_SYNC_ID) {
		c->phase = HCC_PHASE_ACTIVE;
		c->txq_sync_submitted = true;
		c->txq_sync_count++;
		c->deadline_ms = 0;
		hcc_emit(c, HCC_EVENT_TXQ_SYNC, 1u);
	}
	return hcc_core_mbox_send(c, HCC_H2D_ID_ETE_ANNOUNCE, now_ms);
}

/* =====================================================================
 * 9. Heartbeat monitor, PM state machine, periodic tick
 * ===================================================================== */

int hcc_core_heartbeat_arm(struct hcc_core *c, hcc_u64 now_ms)
{
	c->hb_armed = true;
	c->hb_deadline_ms = now_ms + c->cfg.heartbeat_ms;
	return 0;
}

void hcc_core_heartbeat_disarm(struct hcc_core *c)
{
	c->hb_armed = false;
	c->hb_deadline_ms = 0;
}

static int hcc_core_heartbeat_tick(struct hcc_core *c, hcc_u64 now_ms)
{
	if (!c->hb_armed || now_ms < c->hb_deadline_ms)
		return 0;
	c->hb_timeouts++;
	hcc_emit(c, HCC_EVENT_HEARTBEAT_TIMEOUT, c->hb_timeouts);
	/* Re-arm so each missed period is counted separately; the vendor
	 * submits EXCP_HEART on expiry [V], the counter is our equivalent. */
	c->hb_deadline_ms = now_ms + c->cfg.heartbeat_ms;
	return 1;
}

int hcc_core_pm_request_sleep(struct hcc_core *c, hcc_u64 now_ms)
{
	int rc;

	if (!c->cfg.pm_enabled) {
		c->h2d_refused++;
		hcc_emit(c, HCC_EVENT_H2D_REFUSED, HCC_H2D_ID_PM_SLEEP);
		return -EOPNOTSUPP;
	}
	if (!c->boot_latched || c->phase != HCC_PHASE_ACTIVE) {
		c->h2d_refused++;
		hcc_emit(c, HCC_EVENT_H2D_REFUSED, HCC_H2D_ID_PM_SLEEP);
		return -EPERM;
	}
	if (c->pm_sleep_pending)
		return -EBUSY;
	rc = hcc_core_mbox_send(c, HCC_H2D_ID_PM_SLEEP, now_ms);
	if (rc < 0)
		return rc;
	c->pm_sleep_pending = true;
	c->pm_sleep_deadline_ms = now_ms + c->cfg.pm_sleep_ack_ms;
	c->h2d_pm_requests++;
	return 0;
}

static int hcc_core_pm_tick(struct hcc_core *c, hcc_u64 now_ms)
{
	if (!c->pm_sleep_pending)
		return 0;
	if (now_ms < c->pm_sleep_deadline_ms)
		return 0;
	c->pm_sleep_pending = false;
	c->pm_sleep_timeouts++;
	hcc_emit(c, HCC_EVENT_PM_SLEEP_TIMEOUT, c->pm_sleep_timeouts);
	return 1;
}

int hcc_core_tick(struct hcc_core *c, hcc_u64 now_ms)
{
	int serviced = 0;

	c->now_ms = now_ms;
	c->ticks++;
	if (!c->released)
		return 0;

	serviced += hcc_core_boot_check(c, now_ms);
	if (c->boot_latched)
		serviced += hcc_core_exception_watch(c);
	serviced += hcc_core_d2h_poll(c, now_ms);
	serviced += hcc_core_ready_tick(c, now_ms);
	serviced += hcc_core_mbox_tick(c, now_ms);
	serviced += hcc_core_heartbeat_tick(c, now_ms);
	serviced += hcc_core_pm_tick(c, now_ms);
	serviced += hcc_core_cece_service(c);
	return serviced;
}

/* =====================================================================
 * 10. Names, defaults and the status line
 * ===================================================================== */

const char *hcc_phase_name(enum hcc_phase p)
{
	switch (p) {
	case HCC_PHASE_IDLE:			return "IDLE";
	case HCC_PHASE_BOOT:			return "BOOT";
	case HCC_PHASE_WAIT_PLAT_READY:		return "WAIT_PLAT_READY";
	case HCC_PHASE_WAIT_DEVICE_READY:	return "WAIT_DEVICE_READY";
	case HCC_PHASE_READY_TXQ_SYNC:		return "READY_TXQ_SYNC";
	case HCC_PHASE_ACTIVE:			return "ACTIVE";
	case HCC_PHASE_FAILED:			return "FAILED";
	default:				return "?";
	}
}

static const char *hcc_fail_name(enum hcc_fail_reason r)
{
	switch (r) {
	case HCC_FAIL_NONE:			return "none";
	case HCC_FAIL_BOOT_TIMEOUT:		return "boot_timeout";
	case HCC_FAIL_PLAT_READY_TIMEOUT:	return "plat_ready_timeout";
	case HCC_FAIL_DEVICE_READY_TIMEOUT:	return "device_ready_timeout";
	default:				return "?";
	}
}

const char *hcc_event_name(enum hcc_event e)
{
	switch (e) {
	case HCC_EVENT_NONE:			return "none";
	case HCC_EVENT_BOOT_LATCHED:		return "boot_latched";
	case HCC_EVENT_BOOT_REPAIRED:		return "boot_repaired";
	case HCC_EVENT_TIMEOUT:			return "timeout";
	case HCC_EVENT_D2H_WORD:		return "d2h_word";
	case HCC_EVENT_D2H_DISPATCH:		return "d2h_dispatch";
	case HCC_EVENT_D2H_DROPPED:		return "d2h_dropped";
	case HCC_EVENT_VECTOR_CAPTURE:		return "vector_capture";
	case HCC_EVENT_CECE_WRITE:		return "cece_write";
	case HCC_EVENT_CECE_REFUSED:		return "cece_refused";
	case HCC_EVENT_PLAT_READY:		return "plat_ready";
	case HCC_EVENT_DEVICE_READY:		return "device_ready";
	case HCC_EVENT_TXQ_SYNC:		return "txq_sync";
	case HCC_EVENT_HEARTBEAT:		return "heartbeat";
	case HCC_EVENT_HEARTBEAT_TIMEOUT:	return "heartbeat_timeout";
	case HCC_EVENT_PM_STATE:		return "pm_state";
	case HCC_EVENT_PM_SLEEP_TIMEOUT:	return "pm_sleep_timeout";
	case HCC_EVENT_H2D_SENT:		return "h2d_sent";
	case HCC_EVENT_H2D_COMPLETE:		return "h2d_complete";
	case HCC_EVENT_H2D_TIMEOUT:		return "h2d_timeout";
	case HCC_EVENT_H2D_REFUSED:		return "h2d_refused";
	case HCC_EVENT_FRAME_INVALID:		return "frame_invalid";
	case HCC_EVENT_FRAME_UNKNOWN:		return "frame_unknown";
	default:				return "?";
	}
}

void hcc_core_config_default(struct hcc_core_config *cfg)
{
	if (!cfg)
		return;
	memset(cfg, 0, sizeof(*cfg));
	cfg->plat_ready_ms = HCC_PLAT_READY_MS;
	cfg->dev_ready_ms = HCC_DEV_READY_MS;
	cfg->boot_timeout_ms = HCC_BOOT_TIMEOUT_MS;
	cfg->pm_sleep_ack_ms = HCC_PM_SLEEP_ACK_MS;
	cfg->heartbeat_ms = HCC_HEARTBEAT_MS;
	cfg->mbox_clear_timeout_ms = 1000u;	/* polled substitute, 5 ms cadence */
	cfg->mbox_clear_poll_max = HCC_MBOX_CLEAR_POLL_MAX;
	cfg->pm_enabled = false;		/* vendor PM is not on the boot path */
	cfg->heartbeat_armed = false;		/* vendor boot path leaves it off */
	cfg->allow_ceed_repair = false;		/* GP8 needs no repair; opt-in only */
	cfg->txq_sync_required = true;
}

void hcc_core_init(struct hcc_core *c, const struct hcc_io_ops *io,
		   const struct hcc_core_config *cfg,
		   hcc_event_cb on_event, void *on_event_priv)
{
	memset(c, 0, sizeof(*c));
	if (io)
		c->io = *io;
	if (cfg)
		c->cfg = *cfg;
	else
		hcc_core_config_default(&c->cfg);
	c->on_event = on_event;
	c->on_event_priv = on_event_priv;
	c->phase = HCC_PHASE_IDLE;
	c->cece_mode = HCC_CECE_MODE_EVIDENCE;
}

int hcc_core_status(const struct hcc_core *c, char *buf, hcc_u32 len)
{
	if (!c || !buf || !len)
		return -EINVAL;
	return snprintf(buf, (size_t)len,
		"phase=%s fail=%s boot=%s hs=%08x/%08x/%08x/%08x golden=%u "
		"page_lost=%u d2h[words=%llu last=0x%x drop=%u stop=%u "
		"need_dr=%u] ready[id1=%u id2=%u map=0x%x id2plen=%u] "
		"txq=%u cece[w=%u boot_block=%u ev=%u] "
		"h2d[pub=%u done=%u coal=%u to=%u closed=%u] "
		"hb[armed=%u n=%u to=%u] pm[dis=%u wok=%u pend=%u to=%u] "
		"writes=%llu bad=%u",
		hcc_phase_name(c->phase), hcc_fail_name(c->fail_reason),
		c->boot_latched ? "latched" :
			(c->released ? "boot" : "idle"),
		c->handshake[0], c->handshake[1],
		c->handshake[2], c->handshake[3],
		c->handshake_golden ? 1u : 0u, c->page_lost_after_latch,
		(unsigned long long)c->d2h_words, c->d2h_last_bitmap,
		c->d2h_dropped, c->d2h_stop_gt10, c->d2h_need_dr_drain,
		c->plat_ready_count, c->dev_ready_count, c->ready_map,
		c->id2_plen, c->txq_sync_count,
		c->cece_writes, c->cece_blocked_boot, c->cece_evidence_events,
		c->mbox_publishes, c->mbox_completions,
		c->mbox_coalesced_batches, c->mbox_timeouts,
		c->h2d_lane_closed ? 1u : 0u,
		c->hb_armed ? 1u : 0u, c->hb_count, c->hb_timeouts,
		c->pm_sleep_disallow ? 1u : 0u, c->pm_wake_ok ? 1u : 0u,
		c->pm_sleep_pending ? 1u : 0u, c->pm_sleep_timeouts,
		(unsigned long long)c->writes, c->bad_writes);
}
