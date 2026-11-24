// SPDX-License-Identifier: GPL-2.0
/*
 * hcc_core_test.c - deterministic, device-free selftest for hcc_core.
 *
 * ORIGINAL code by the wifi-hcc-core lane (task-92, 2026-11-24).  No device
 * is contacted: the "device" is a fake BAR0 (IO region + firmware-RAM region)
 * with a write recorder, and time is passed in explicitly, so every ordering,
 * coalescing, timeout and ring-wrap property is reproducible.
 *
 * Coverage:
 *   - address maps (including the out[3]/out[4] 0x4b9438/0x4b9414 fix)
 *   - packed ring index arithmetic and the SR node encoding
 *   - frame build/validate (tag 0x5a5a, length, H2D group/id refusals)
 *   - boot handshake LATCH (page overwrite after latch must not un-latch)
 *   - CECE policy: post-latch + evidence-driven + modes + CEED repair
 *   - vendor-order D2H mailbox (ack first, read, drain, re-arm, dispatch)
 *   - D2H dispatch table ids 1..7, dropped 0/8/9/10, stop above 10
 *   - ready pair via ENGINE and DIRECT paths + duplicate handling + timeouts
 *   - READY -> TXQ sync (group-0 id-1) -> ACTIVE transition
 *   - H2D mailbox coalescing, doorbell RMW, timeout/lane close, refusals
 *   - SR ring wrap (40 commits on depth 32) and node word1 encoding
 *   - heartbeat monitor and PM state machine
 *   - write-allowlist census: out[5] and the CPU release word are never written
 */
#include "hcc_core.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define IO_SPAN		0x120000u
#define IO_WORDS	(IO_SPAN / 4u)
#define RAM_SPAN	0x220000u
#define RAM_WORDS	(RAM_SPAN / 4u)
#define IO_BAR0		0x003b8000u
#define FW_BAR0		0x006b8000u

#define EVN		((unsigned)HCC_EVENT_FRAME_UNKNOWN + 1u)

static int g_checks;
static int g_failures;

#define CHECK(cond) do {						\
	g_checks++;							\
	if (!(cond)) {							\
		g_failures++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

#define CHECK_EQ(a, b) do {						\
	long long va_ = (long long)(a);					\
	long long vb_ = (long long)(b);					\
	g_checks++;							\
	if (va_ != vb_) {						\
		g_failures++;						\
		printf("FAIL %s:%d: %s=%lld != %s=%lld\n",		\
		       __FILE__, __LINE__, #a, va_, #b, vb_);		\
	}								\
} while (0)

/* ------------------------------------------------------------------ */
/* fake BAR0 device                                                    */
/* ------------------------------------------------------------------ */
struct fk_write {
	hcc_u32 ca;
	hcc_u32 val;
};

struct fake {
	hcc_u32		io[IO_WORDS];	/* device IO window (region 3)	*/
	hcc_u32		ram[RAM_WORDS];	/* firmware RAM window (region 5) */
	struct fk_write	writes[4096];
	hcc_u32		nwrites;
	hcc_u32		io_reads;
	hcc_u32		ram_reads;
	hcc_u32		bad_region_reads;
	hcc_u32		bad_region_writes;
	hcc_u32		bitmap_reads;
	hcc_u32		bitmap_after_ack;
	int		coalesce_armed;
	struct hcc_io_ops ops;
};

static hcc_u32 fk_read32(void *priv, hcc_u32 off)
{
	struct fake *f = priv;

	if (off >= IO_BAR0 && off < IO_BAR0 + IO_SPAN) {
		hcc_u32 idx = (off - IO_BAR0) >> 2;
		hcc_u32 ca = 0x40000000u + (off - IO_BAR0);

		f->io_reads++;
		if (ca == HCC_CA_D2H_BITMAP) {
			f->bitmap_reads++;
			if (f->coalesce_armed && f->bitmap_reads >= 2u)
				return f->bitmap_after_ack;
		}
		return f->io[idx];
	}
	if (off >= FW_BAR0 && off < FW_BAR0 + RAM_SPAN) {
		f->ram_reads++;
		return f->ram[(off - FW_BAR0) >> 2];
	}
	f->bad_region_reads++;
	return 0xffffffffu;
}

static void fk_write32(void *priv, hcc_u32 off, hcc_u32 val)
{
	struct fake *f = priv;
	hcc_u32 ca;

	if (off >= IO_BAR0 && off < IO_BAR0 + IO_SPAN) {
		ca = 0x40000000u + (off - IO_BAR0);
		f->io[(off - IO_BAR0) >> 2] = val;
	} else if (off >= FW_BAR0 && off < FW_BAR0 + RAM_SPAN) {
		/* the core must never write firmware RAM; record it with a
		 * marker CA so the census test catches a regression */
		ca = 0x80000000u | (off - FW_BAR0);
		f->bad_region_writes++;
		f->ram[(off - FW_BAR0) >> 2] = val;
	} else {
		ca = 0xffffffffu;
		f->bad_region_writes++;
	}
	if (f->nwrites < (hcc_u32)(sizeof(f->writes) / sizeof(f->writes[0]))) {
		f->writes[f->nwrites].ca = ca;
		f->writes[f->nwrites].val = val;
		f->nwrites++;
	}
}

static void fk_reset_writes(struct fake *f)
{
	f->nwrites = 0;
}

static hcc_u32 fk_writes_to(const struct fake *f, hcc_u32 ca)
{
	hcc_u32 i, n = 0;

	for (i = 0; i < f->nwrites; i++)
		if (f->writes[i].ca == ca)
			n++;
	return n;
}

static hcc_u32 fk_writes_to_fw(const struct fake *f)
{
	hcc_u32 i, n = 0;

	for (i = 0; i < f->nwrites; i++)
		if (f->writes[i].ca & 0x80000000u)
			n++;
	return n;
}

static void fk_set_io(struct fake *f, hcc_u32 ca, hcc_u32 val)
{
	if (ca >= 0x40000000u && ca <= 0x4011ffffu)
		f->io[(ca - 0x40000000u) >> 2] = val;
}

static hcc_u32 fk_get_io(const struct fake *f, hcc_u32 ca)
{
	if (ca >= 0x40000000u && ca <= 0x4011ffffu)
		return f->io[(ca - 0x40000000u) >> 2];
	return 0;
}

static void fk_set_ram(struct fake *f, hcc_u32 cpu, hcc_u32 val)
{
	if (cpu < RAM_SPAN)
		f->ram[cpu >> 2] = val;
}

static hcc_u32 fk_get_ram(const struct fake *f, hcc_u32 cpu)
{
	if (cpu < RAM_SPAN)
		return f->ram[cpu >> 2];
	return 0;
}

/* count writes whose CA is not in the core's write allowlist */
static hcc_u32 fk_bad_ca_writes(const struct fake *f)
{
	hcc_u32 i, n = 0;

	for (i = 0; i < f->nwrites; i++)
		if (!hcc_ca_writable(f->writes[i].ca))
			n++;
	return n;
}

/* ------------------------------------------------------------------ */
/* event log + rig                                                     */
/* ------------------------------------------------------------------ */
struct evlog {
	hcc_u32 count[EVN];
	hcc_u32 last[EVN];
};

static void ev_cb(void *priv, enum hcc_event ev, hcc_u32 data)
{
	struct evlog *e = priv;
	unsigned idx = (unsigned)ev;

	if (idx < EVN) {
		e->count[idx]++;
		e->last[idx] = data;
	}
}

struct rig {
	struct fake		fk;
	struct hcc_core		c;
	struct evlog		ev;
};

#define PAGE_CPU	0x001bf000u
#define EXCP_MASK_CPU	0x00040008u
#define VEC_FRAME_CPU	0x0004000cu

static struct rig g_rig;

static void rig_init(struct rig *r)
{
	memset(r, 0, sizeof(*r));
	r->fk.ops.priv = &r->fk;
	r->fk.ops.read32 = fk_read32;
	r->fk.ops.write32 = fk_write32;
	{
		struct hcc_core_config cfg;

		hcc_core_config_default(&cfg);
		hcc_core_init(&r->c, &r->fk.ops, &cfg, ev_cb, &r->ev);
	}
}

static void rig_set_golden_page(struct rig *r)
{
	fk_set_ram(&r->fk, PAGE_CPU + 0x0u, HCC_HS_W0_GOLDEN);
	fk_set_ram(&r->fk, PAGE_CPU + 0x4u, HCC_HS_W1_GOLDEN);
	fk_set_ram(&r->fk, PAGE_CPU + 0x8u, HCC_HS_ACK_GOLDEN);
	fk_set_ram(&r->fk, PAGE_CPU + 0xcu, HCC_HS_W3_GOLDEN);
}

static void rig_boot_at(struct rig *r, hcc_u64 t)
{
	rig_set_golden_page(r);
	hcc_core_note_release(&r->c, t);
	(void)hcc_core_tick(&r->c, t + 1u);
	CHECK(r->c.released);
	CHECK(r->c.boot_latched);
}

static void rig_deliver(struct rig *r, enum hcc_src src,
			const hcc_u8 *frame, hcc_u32 len, hcc_u64 t)
{
	CHECK_EQ(hcc_core_deliver_frame(&r->c, src, frame, len, t), 0);
}

static void rig_ready(struct rig *r, hcc_u64 t)
{
	/* g4 id1 (12 B) then g4 id2 (16 B, payload 0x01060850) */
	static const hcc_u8 id1[12] = {
		0x04, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x5a, 0x5a
	};
	static const hcc_u8 id2[16] = {
		0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x02, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x50, 0x08, 0x06, 0x01
	};

	rig_deliver(r, HCC_SRC_ENGINE, id1, sizeof(id1), t);
	rig_deliver(r, HCC_SRC_ENGINE, id2, sizeof(id2), t + 10u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_READY_TXQ_SYNC);
}

static void rig_active(struct rig *r, hcc_u64 t)
{
	hcc_u8 buf[64];
	hcc_u32 total = 0;
	hcc_u32 payload[1] = { 0x11223344u };

	rig_ready(r, t);
	CHECK_EQ(hcc_core_txq_sync_build(&r->c, buf, sizeof(buf),
					 (const hcc_u8 *)payload, 4u,
					 &total), 0);
	CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x80001000u, 0u, 1u, total,
				    t + 20u), 0);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_ACTIVE);
}

/* ------------------------------------------------------------------ */
/* 1. address maps                                                     */
/* ------------------------------------------------------------------ */
static void test_address_maps(void)
{
	printf("- address maps\n");
	/* region 3: BAR0 = 0x3b8000 + (CA - 0x40000000) [V] */
	CHECK_EQ(hcc_bar0_io(0x40039010u), 0x3f1010u);
	CHECK_EQ(hcc_bar0_io(0x40039014u), 0x3f1014u);
	CHECK_EQ(hcc_bar0_io(0x400392d4u), 0x3f12d4u);
	CHECK_EQ(hcc_bar0_io(0x400392f0u), 0x3f12f0u);
	CHECK_EQ(hcc_bar0_io(0x4000010cu), 0x3b810cu);
	CHECK_EQ(hcc_bar0_io(0x40039508u), 0x3f1508u);
	CHECK_EQ(hcc_bar0_io(0x4003a874u), 0x3f2874u);
	/* the fix: out[3]/out[4] need the 21-bit region offset */
	CHECK_EQ(hcc_bar0_io(HCC_CA_D2H_ACK), 0x4b9438u);
	CHECK_EQ(hcc_bar0_io(HCC_CA_D2H_REARM), 0x4b9414u);
	CHECK_EQ(hcc_bar0_io(HCC_CA_D2H_DOORBELL), 0x4b9434u);
	/* outside region 3 */
	CHECK_EQ(hcc_bar0_io(0x3fffffffu), 0u);
	CHECK_EQ(hcc_bar0_io(0x40120000u), 0u);
	/* region 5: BAR0 = 0x6b8000 + CPU [V] */
	CHECK_EQ(hcc_bar0_fw(0x1bf000u), 0x877000u);
	CHECK_EQ(hcc_bar0_fw(0x118b60u), 0x7d0b60u);
	CHECK_EQ(hcc_bar0_fw(0x40008u), 0x6f8008u);
	CHECK_EQ(hcc_bar0_fw(0x4000cu), 0x6f800cu);
	CHECK_EQ(hcc_bar0_fw(0u), 0x6b8000u);
	CHECK_EQ(hcc_bar0_fw(0x220000u), 0u);
	/* write allowlist: out[5] and the transport release word are closed */
	CHECK(hcc_ca_writable(HCC_CA_D2H_ACK));
	CHECK(hcc_ca_writable(HCC_CA_D2H_BITMAP));
	CHECK(hcc_ca_writable(HCC_CA_D2H_REARM));
	CHECK(hcc_ca_writable(HCC_CA_H2D_PENDING));
	CHECK(hcc_ca_writable(HCC_CA_H2D_DOORBELL));
	CHECK(hcc_ca_writable(HCC_CA_GATE));
	CHECK(!hcc_ca_writable(HCC_CA_NEVER_WRITE));
	CHECK(!hcc_ca_writable(HCC_CA_CPU_RELEASE));
}

/* ------------------------------------------------------------------ */
/* 2. packed ring index arithmetic                                     */
/* ------------------------------------------------------------------ */
static void test_index_arithmetic(void)
{
	hcc_u32 i, idx;

	printf("- ring index arithmetic\n");
	CHECK_EQ(hcc_idx_plus(0u, 32u), 1u);
	CHECK_EQ(hcc_idx_plus(31u, 32u), HCC_IDX_PHASE_BIT);
	CHECK_EQ(hcc_idx_plus(HCC_IDX_PHASE_BIT, 32u),
		 HCC_IDX_PHASE_BIT + 1u);
	CHECK_EQ(hcc_idx_plus(HCC_IDX_PHASE_BIT + 31u, 32u), 0u);
	CHECK_EQ(hcc_idx_count(0u, 1u, 32u), 1u);
	CHECK_EQ(hcc_idx_count(31u, HCC_IDX_PHASE_BIT, 32u), 1u);
	CHECK_EQ(fk_get_ram(&g_rig.fk, PAGE_CPU + 0x8u), 0u);
	CHECK_EQ(hcc_idx_count(0u, HCC_IDX_PHASE_BIT, 32u), 32u); /* pre-post */
	CHECK_EQ(hcc_idx_count(HCC_IDX_PHASE_BIT, 0u, 32u), 32u);
	CHECK_EQ(hcc_idx_count(0u, 0u, 32u), 0u);
	CHECK_EQ(hcc_idx_count(HCC_IDX_PHASE_BIT, HCC_IDX_PHASE_BIT + 8u,
			       32u), 8u);
	/* forward distance 0x408 -> 0x400 is 56 counts (wrap through a full
	 * phase: 24 to the phase flip + 32 more); the vendor formula agrees */
	CHECK_EQ(hcc_idx_count(HCC_IDX_PHASE_BIT + 8u, HCC_IDX_PHASE_BIT,
			       32u), 56u);
	CHECK_EQ(hcc_idx_slot(0x00000123u), 0x123u);
	CHECK_EQ(hcc_idx_slot(HCC_IDX_PHASE_BIT + 5u), 5u);
	CHECK_EQ(hcc_sr_node_word1(12u), (12u << 16) | 0x6d2bu);

	/* a 40-node walk on a depth-32 ring: 32 slots, then wrap, phase 1 */
	idx = 0;
	for (i = 0; i < 40u; i++) {
		CHECK_EQ(hcc_idx_slot(idx), i % 32u);
		idx = hcc_idx_plus(idx, 32u);
	}
	CHECK_EQ(idx, HCC_IDX_PHASE_BIT + 8u);
	CHECK_EQ(hcc_idx_count(0u, idx, 32u), 40u);
}

/* ------------------------------------------------------------------ */
/* 3. frame contract                                                   */
/* ------------------------------------------------------------------ */
static void test_frame_contract(void)
{
	struct hcc_frame f;
	hcc_u8 buf[0x700];
	hcc_u8 payload[8] = { 0xde, 0xad, 0xbe, 0xef, 1, 2, 3, 4 };
	hcc_u32 total = 0;
	static const hcc_u8 stub[12] = {
		0x00, 0x01, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x5a, 0x5a
	};
	static const hcc_u8 id1[12] = {
		0x04, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x5a, 0x5a
	};
	static const hcc_u8 id2[16] = {
		0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x02, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x50, 0x08, 0x06, 0x01
	};

	printf("- frame contract\n");
	CHECK_EQ(hcc_core_frame_build(NULL, buf, sizeof(buf), 0u, 0u, 0u,
				      NULL, 0u, &total), 0);
	CHECK_EQ(total, 12u);
	CHECK_EQ(buf[0], 0u);
	CHECK_EQ(buf[1], 1u);		/* allocated nibble [V] */
	CHECK_EQ(buf[4], 0x0cu);
	CHECK_EQ(buf[5], 0u);
	CHECK_EQ(buf[10], 0x5au);
	CHECK_EQ(buf[11], 0x5au);
	CHECK_EQ(hcc_core_frame_decode(buf, total, &f), 0);
	CHECK_EQ(f.group, 0u);
	CHECK_EQ(f.core, 0u);
	CHECK_EQ(f.id, 0u);
	CHECK_EQ(f.tag, HCC_FRAME_TAG);

	/* group 2 id 0x2d dst 3, 4-byte payload */
	CHECK_EQ(hcc_core_frame_build(NULL, buf, sizeof(buf), 2u, 0x2du, 3u,
				      payload, 4u, &total), 0);
	CHECK_EQ(total, 16u);
	CHECK_EQ(buf[1], 0x31u);	/* dst << 4 | 1 */
	CHECK_EQ(buf[9], 3u);
	CHECK_EQ(memcmp(buf + 12, payload, 4), 0);
	CHECK_EQ(hcc_core_frame_decode(buf, total, &f), 0);
	CHECK_EQ(f.group, 2u);
	CHECK_EQ(f.id, 0x2du);
	CHECK_EQ(f.dst, 3u);
	CHECK_EQ(f.total, 16u);

	/* refusals */
	CHECK_EQ(hcc_core_frame_build(NULL, buf, sizeof(buf), 4u, 0u, 0u,
				      NULL, 0u, &total), -EACCES);
	CHECK_EQ(hcc_core_frame_build(NULL, buf, sizeof(buf), 0u, 5u, 0u,
				      NULL, 0u, &total), -EACCES);
	CHECK_EQ(hcc_core_frame_build(NULL, buf, sizeof(buf), 1u, 0u, 0u,
				      NULL, 0u, &total), -EINVAL);
	CHECK_EQ(hcc_core_frame_build(NULL, buf, sizeof(buf), 0u, 0u, 0u,
				      payload, HCC_FRAME_MAX_PAYLOAD + 1u,
				      &total), -ERANGE);
	CHECK_EQ(hcc_core_frame_build(NULL, buf, 8u, 0u, 0u, 0u,
				      NULL, 0u, &total), -ENOSPC);
	CHECK_EQ(hcc_core_frame_build(NULL, buf, sizeof(buf), 0u, 0u, 0x10u,
				      NULL, 0u, &total), -ERANGE);

	/* decode of the captured wire forms [V*] */
	CHECK_EQ(hcc_core_frame_decode(stub, sizeof(stub), &f), 0);
	CHECK_EQ(f.group, 0u);
	CHECK_EQ(f.alloc, 1u);
	CHECK_EQ(f.core, 0u);
	CHECK_EQ(f.total, 12u);
	CHECK_EQ(hcc_core_frame_decode(id1, sizeof(id1), &f), 0);
	CHECK_EQ(f.group, 4u);
	CHECK_EQ(f.id, 1u);
	CHECK_EQ(f.total, 12u);
	CHECK_EQ(hcc_core_frame_decode(id2, sizeof(id2), &f), 0);
	CHECK_EQ(f.group, 4u);
	CHECK_EQ(f.id, 2u);
	CHECK_EQ(f.total, 16u);

	/* decode refusals: tag, length, short header, oversize */
	{
		hcc_u8 bad[16];

		memcpy(bad, id2, sizeof(bad));
		bad[10] = 0x5b;		/* tag mismatch */
		CHECK_EQ(hcc_core_frame_decode(bad, sizeof(bad), &f), -EPROTO);
		memcpy(bad, id2, sizeof(bad));
		bad[4] = 0x0b;		/* total < 12 */
		CHECK_EQ(hcc_core_frame_decode(bad, sizeof(bad), &f), -EMSGSIZE);
		memcpy(bad, id2, sizeof(bad));
		bad[4] = 0x11;		/* total != wire length */
		CHECK_EQ(hcc_core_frame_decode(bad, sizeof(bad), &f), -EMSGSIZE);
		CHECK_EQ(hcc_core_frame_decode(id2, 8u, &f), -EMSGSIZE);
		memcpy(bad, id2, sizeof(bad));
		bad[4] = 0x81;		/* total 0x681 > max */
		bad[5] = 0x06;
		CHECK_EQ(hcc_core_frame_decode(bad, 0x681u, &f), -EMSGSIZE);
	}

	/* TXQ sync builder: group 0 id 1 [V] */
	CHECK_EQ(hcc_core_txq_sync_build(NULL, buf, sizeof(buf), payload, 2u,
					 &total), 0);
	CHECK_EQ(total, 14u);
	CHECK_EQ(hcc_core_frame_decode(buf, total, &f), 0);
	CHECK_EQ(f.group, 0u);
	CHECK_EQ(f.id, 1u);
}

/* ------------------------------------------------------------------ */
/* 4. boot latch (the page-overwrite policy fix)                       */
/* ------------------------------------------------------------------ */
static void test_boot_latch(void)
{
	struct rig *r = &g_rig;

	printf("- boot latch / page overwrite\n");
	rig_init(r);
	(void)hcc_core_tick(&r->c, 10u);	/* not released: no reads */
	CHECK_EQ(r->fk.io_reads, 0u);
	CHECK_EQ(r->fk.ram_reads, 0u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_IDLE);

	hcc_core_note_release(&r->c, 100u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_BOOT);
	CHECK_EQ(r->c.boot_deadline_ms, 100u + HCC_BOOT_TIMEOUT_MS);
	(void)hcc_core_tick(&r->c, 150u);	/* page zero: no latch */
	CHECK(!r->c.boot_latched);
	CHECK_EQ(r->fk.nwrites, 0u);	/* no writes before the latch */

	rig_set_golden_page(r);
	(void)hcc_core_tick(&r->c, 200u);
	CHECK(r->c.boot_latched);
	CHECK(r->c.handshake_golden);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_PLAT_READY);
	CHECK_EQ(r->c.deadline_ms, 200u + HCC_PLAT_READY_MS);
	CHECK_EQ(r->ev.count[HCC_EVENT_BOOT_LATCHED], 1u);
	CHECK_EQ(r->ev.last[HCC_EVENT_BOOT_LATCHED], 1u);
	CHECK_EQ(r->c.handshake[0], HCC_HS_W0_GOLDEN);
	CHECK_EQ(r->c.handshake[2], HCC_HS_ACK_GOLDEN);

	/* patched-boot case: the page is overwritten after the latch.  The
	 * latch must survive (the PD image clobbers it after S1). */
	fk_set_ram(&r->fk, PAGE_CPU + 0x0u, 0x0010c190u);
	fk_set_ram(&r->fk, PAGE_CPU + 0x4u, 0u);
	fk_set_ram(&r->fk, PAGE_CPU + 0x8u, 0u);
	fk_set_ram(&r->fk, PAGE_CPU + 0xcu, 0x001bee50u);
	(void)hcc_core_boot_check(&r->c, 900u);
	(void)hcc_core_boot_check(&r->c, 950u);
	CHECK(r->c.boot_latched);
	CHECK(r->c.page_lost_after_latch >= 1u);
	CHECK_EQ(r->c.ceed_repairs, 0u);

	/* PD-style page: +8 == 2 but not the golden words -> must still latch */
	rig_init(r);
	fk_set_ram(&r->fk, PAGE_CPU + 0x0u, 0x86803fefu);
	fk_set_ram(&r->fk, PAGE_CPU + 0x4u, 0x00000017u);
	fk_set_ram(&r->fk, PAGE_CPU + 0x8u, 2u);
	fk_set_ram(&r->fk, PAGE_CPU + 0xcu, 0x0000abcdu);
	hcc_core_note_release(&r->c, 0u);
	(void)hcc_core_tick(&r->c, 10u);
	CHECK(r->c.boot_latched);
	CHECK(!r->c.handshake_golden);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_PLAT_READY);

	/* boot timeout, then a late valid page latches and recovers the phase */
	rig_init(r);
	hcc_core_note_release(&r->c, 0u);
	(void)hcc_core_tick(&r->c, HCC_BOOT_TIMEOUT_MS - 1u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_BOOT);
	(void)hcc_core_tick(&r->c, HCC_BOOT_TIMEOUT_MS);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_FAILED);
	CHECK_EQ((int)r->c.fail_reason, (int)HCC_FAIL_BOOT_TIMEOUT);
	CHECK_EQ(r->ev.count[HCC_EVENT_TIMEOUT], 1u);
	rig_set_golden_page(r);
	(void)hcc_core_tick(&r->c, HCC_BOOT_TIMEOUT_MS + 10u);
	CHECK(r->c.boot_latched);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_PLAT_READY);
}

/* ------------------------------------------------------------------ */
/* 5. CECE policy                                                      */
/* ------------------------------------------------------------------ */
static void test_cece_policy(void)
{
	struct rig *r = &g_rig;

	printf("- CECE policy\n");
	rig_init(r);
	rig_boot_at(r, 0u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 0u); /* boot tick: no CECE */
	fk_reset_writes(&r->fk);

	/* evidence class 1: out[1] bit2 (startup/vector announce) */
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x4u);
	(void)hcc_core_tick(&r->c, 100u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 1u);
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_GATE), HCC_GATE_CECE);
	CHECK_EQ(r->c.cece_writes, 1u);
	CHECK_EQ(r->c.d2h_id_count[2], 1u);
	CHECK(r->c.excp_announce);

	/* no new evidence -> no new write */
	fk_reset_writes(&r->fk);
	(void)hcc_core_tick(&r->c, 200u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 0u);
	CHECK_EQ(r->c.cece_writes, 1u);

	/* a second observed announce -> exactly one more write */
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x4u);
	(void)hcc_core_tick(&r->c, 300u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 1u);
	CHECK_EQ(r->c.cece_writes, 2u);

	/* evidence class 2: exception-seen mask change */
	r->c.exception_mask_valid = true;	/* pretend the zero was seen */
	r->c.exception_mask = 0u;
	fk_set_ram(&r->fk, EXCP_MASK_CPU, 0x0au);
	fk_reset_writes(&r->fk);
	(void)hcc_core_tick(&r->c, 400u);
	CHECK_EQ(r->c.exception_mask, 0x0au);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 1u);
	CHECK_EQ(r->c.cece_writes, 3u);

	/* evidence class 3: a registered handler other than the pure id6 */
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x8u);	/* id3 ETE done */
	fk_reset_writes(&r->fk);
	(void)hcc_core_tick(&r->c, 500u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 1u);
	CHECK_EQ(r->c.cece_writes, 4u);

	/* id6 (deposit notify) alone is NOT evidence */
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x40u);
	fk_reset_writes(&r->fk);
	(void)hcc_core_tick(&r->c, 600u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 0u);
	CHECK_EQ(r->c.cece_writes, 4u);

	/* pre-latch evidence must be refused and counted */
	rig_init(r);
	hcc_core_note_release(&r->c, 0u);
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x4u);
	(void)hcc_core_tick(&r->c, 10u);
	CHECK(!r->c.boot_latched);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 0u);
	CHECK_EQ(r->c.cece_blocked_boot, 1u);

	/* mode OFF disables every CECE write */
	rig_init(r);
	rig_boot_at(r, 0u);
	r->c.cece_mode = HCC_CECE_MODE_OFF;
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x4u);
	fk_reset_writes(&r->fk);
	(void)hcc_core_tick(&r->c, 100u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 0u);
	CHECK(r->c.cece_blocked_mode >= 1u);

	/* bounded manual kick: refused pre-latch, <= 3 writes after */
	rig_init(r);
	r->c.cece_mode = HCC_CECE_MODE_KICK;
	hcc_core_note_release(&r->c, 0u);
	CHECK_EQ(hcc_core_cece_kick(&r->c), -EPERM);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 0u);
	rig_set_golden_page(r);
	(void)hcc_core_tick(&r->c, 10u);
	CHECK(r->c.boot_latched);
	CHECK_EQ(hcc_core_cece_kick(&r->c), 0);
	CHECK_EQ(hcc_core_cece_kick(&r->c), 0);
	CHECK_EQ(hcc_core_cece_kick(&r->c), 0);
	CHECK_EQ(hcc_core_cece_kick(&r->c), -EAGAIN);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 3u);
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_GATE), HCC_GATE_CECE);

	/* optional CEED repair (default OFF) */
	rig_init(r);	/* default config: allow_ceed_repair = false */
	hcc_core_note_release(&r->c, 0u);
	fk_set_io(&r->fk, HCC_CA_GATE, 0xbaadu);
	(void)hcc_core_tick(&r->c, 10u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 0u);

	rig_init(r);
	r->c.cfg.allow_ceed_repair = true;
	hcc_core_note_release(&r->c, 0u);
	fk_set_io(&r->fk, HCC_CA_GATE, 0xbaadu);
	(void)hcc_core_tick(&r->c, 10u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 1u);
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_GATE), HCC_GATE_CEED);
	CHECK_EQ(r->c.ceed_repairs, 1u);
	(void)hcc_core_tick(&r->c, 20u);	/* bounded: no second repair */
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_GATE), 1u);
	rig_set_golden_page(r);
	(void)hcc_core_tick(&r->c, 30u);
	CHECK(r->c.boot_latched);
}

/* ------------------------------------------------------------------ */
/* 6. D2H mailbox service                                              */
/* ------------------------------------------------------------------ */
static void test_d2h_service(void)
{
	struct rig *r = &g_rig;

	printf("- D2H mailbox service\n");
	rig_init(r);
	rig_boot_at(r, 0u);
	fk_reset_writes(&r->fk);

	/* bits 1..7 -> ids 1,2,3,4,5,6,7 all dispatched */
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0xfeu);
	(void)hcc_core_tick(&r->c, 100u);
	CHECK_EQ(r->c.d2h_words, 1u);
	CHECK_EQ(r->c.d2h_last_bitmap, 0xfeu);
	CHECK_EQ(r->c.d2h_id_count[1], 1u);
	CHECK_EQ(r->c.d2h_id_count[2], 1u);
	CHECK_EQ(r->c.d2h_id_count[3], 1u);
	CHECK_EQ(r->c.d2h_id_count[4], 1u);
	CHECK_EQ(r->c.d2h_id_count[5], 1u);
	CHECK_EQ(r->c.d2h_id_count[6], 1u);
	CHECK_EQ(r->c.d2h_id_count[7], 1u);
	CHECK_EQ(r->c.d2h_dropped, 0u);
	CHECK_EQ(r->c.d2h_stop_gt10, 0u);
	CHECK_EQ(r->c.d2h_need_dr_drain, 3u);	/* ids 3, 6, 7 */
	CHECK_EQ(r->c.d2h_wake_requests, 2u);	/* ids 6, 7 */
	CHECK(r->c.pm_sleep_disallow);
	CHECK(r->c.pm_wake_ok);
	/* vendor order: ack, drain, re-arm -- and the id2 evidence CECE write */
	CHECK_EQ(r->fk.nwrites, 4u);
	CHECK_EQ(r->fk.writes[0].ca, HCC_CA_D2H_ACK);
	CHECK_EQ(r->fk.writes[0].val, 1u);
	CHECK_EQ(r->fk.writes[1].ca, HCC_CA_D2H_BITMAP);
	CHECK_EQ(r->fk.writes[1].val, 0u);
	CHECK_EQ(r->fk.writes[2].ca, HCC_CA_D2H_REARM);
	CHECK_EQ(r->fk.writes[2].val, 1u);
	CHECK_EQ(r->fk.writes[3].ca, HCC_CA_GATE);
	CHECK_EQ(r->fk.writes[3].val, HCC_GATE_CECE);

	/* a zero bitmap performs no device writes at all */
	fk_reset_writes(&r->fk);
	(void)hcc_core_tick(&r->c, 200u);
	CHECK_EQ(r->fk.nwrites, 0u);

	/* coalescing: the device may publish a second word between the ack and
	 * the drain; the re-read must merge it */
	fk_reset_writes(&r->fk);
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x4u);
	r->fk.bitmap_after_ack = 0x40u;
	r->fk.bitmap_reads = 0u;
	r->fk.coalesce_armed = 1;
	(void)hcc_core_tick(&r->c, 300u);
	r->fk.coalesce_armed = 0;
	CHECK_EQ(r->c.d2h_last_bitmap, 0x44u);
	CHECK_EQ(r->c.d2h_coalesced_bits, 1u);
	CHECK_EQ(r->c.d2h_id_count[2], 2u);	/* after the 0xfe pass */
	CHECK_EQ(r->c.d2h_id_count[6], 2u);

	/* ids 0/8/9/10 have no handler: dropped */
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x00000701u);
	(void)hcc_core_tick(&r->c, 400u);
	CHECK_EQ(r->c.d2h_dropped, 4u);
	CHECK_EQ(r->c.d2h_id_count[0], 1u);
	CHECK_EQ(r->c.d2h_id_count[8], 1u);
	CHECK_EQ(r->c.d2h_id_count[9], 1u);
	CHECK_EQ(r->c.d2h_id_count[10], 1u);

	/* a word with a bit above 10 stops after the lower ids */
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x00000808u);
	(void)hcc_core_tick(&r->c, 500u);
	CHECK_EQ(r->c.d2h_stop_gt10, 1u);
	CHECK_EQ(r->c.d2h_id_count[3], 2u);

	/* exception mask/frame capture on the id2 handler */
	fk_set_ram(&r->fk, EXCP_MASK_CPU, 0x0au);
	fk_set_ram(&r->fk, VEC_FRAME_CPU + 0x0u, 0x11111111u);
	fk_set_ram(&r->fk, VEC_FRAME_CPU + 0x4u, 0x22222222u);
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x4u);
	(void)hcc_core_tick(&r->c, 600u);
	CHECK_EQ(r->c.vector_capture_count, 3u);	/* id2 in passes 1, 2 and this one */
	CHECK_EQ(r->c.vector_mask, 0x0au);
	CHECK_EQ(r->c.vector_frame[0], 0x11111111u);
	CHECK_EQ(r->c.vector_frame[1], 0x22222222u);
	CHECK_EQ(r->c.exception_mask, 0x0au);
}

/* ------------------------------------------------------------------ */
/* 7. ready pair (both receive paths) and timeouts                     */
/* ------------------------------------------------------------------ */
static void test_ready_pair(void)
{
	struct rig *r = &g_rig;
	static const hcc_u8 id1[12] = {
		0x04, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00,
		0x00, 0x00, 0x5a, 0x5a
	};
	static const hcc_u8 id2[16] = {
		0x04, 0x00, 0x00, 0x00, 0x10, 0x00, 0x02, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x50, 0x08, 0x06, 0x01
	};
	hcc_u8 bad[16];

	printf("- ready pair\n");
	rig_init(r);
	rig_boot_at(r, 0u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_PLAT_READY);
	CHECK_EQ(r->c.deadline_ms, 1u + HCC_PLAT_READY_MS);

	/* id1 over the ENGINE path, then a duplicate over the DIRECT path */
	rig_deliver(r, HCC_SRC_ENGINE, id1, sizeof(id1), 1000u);
	CHECK(r->c.plat_ready);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_DEVICE_READY);
	CHECK_EQ(r->c.deadline_ms, 1000u + HCC_DEV_READY_MS);
	CHECK_EQ(r->ev.last[HCC_EVENT_PLAT_READY], (hcc_u32)HCC_SRC_ENGINE);
	rig_deliver(r, HCC_SRC_DIRECT, id1, sizeof(id1), 1100u);
	CHECK_EQ(r->c.plat_ready_count, 2u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_DEVICE_READY);

	/* id2 over the DIRECT path (the working receive path) */
	rig_deliver(r, HCC_SRC_DIRECT, id2, sizeof(id2), 2000u);
	CHECK(r->c.dev_ready);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_READY_TXQ_SYNC);
	CHECK_EQ(r->c.ready_map, 1u);
	CHECK_EQ(r->ev.last[HCC_EVENT_DEVICE_READY], (hcc_u32)HCC_SRC_DIRECT);
	CHECK_EQ(r->c.id2_plen, 4u);
	CHECK_EQ(r->c.id2_payload[0], 0x50u);
	CHECK_EQ(r->c.id2_payload[1], 0x08u);
	CHECK_EQ(r->c.id2_payload[2], 0x06u);
	CHECK_EQ(r->c.id2_payload[3], 0x01u);
	CHECK_EQ((hcc_u32)r->c.read_msg[0][0] |
		 ((hcc_u32)r->c.read_msg[0][1] << 8) |
		 ((hcc_u32)r->c.read_msg[0][2] << 16) |
		 ((hcc_u32)r->c.read_msg[0][3] << 24), 0x01060850u);
	CHECK_EQ(r->c.deadline_ms, 0u);

	/* duplicate id2 (engine path) does not disturb the state */
	rig_deliver(r, HCC_SRC_ENGINE, id2, sizeof(id2), 2100u);
	CHECK_EQ(r->c.dev_ready_count, 2u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_READY_TXQ_SYNC);
	CHECK_EQ(r->c.txq_sync_count, 0u);

	/* wrong core in the header -> rejected, no transition */
	rig_init(r);
	rig_boot_at(r, 0u);
	memset(bad, 0, sizeof(bad));
	memcpy(bad, id1, sizeof(id1));
	bad[1] = 0x10u;			/* core 1 */
	(void)hcc_core_deliver_frame(&r->c, HCC_SRC_DIRECT, bad, 12u, 500u);
	CHECK_EQ(r->c.id1_bad_core, 1u);
	CHECK(!r->c.plat_ready);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_PLAT_READY);

	/* id1 with the wrong total -> rejected */
	memset(bad, 0, sizeof(bad));
	memcpy(bad, id1, sizeof(id1));
	bad[4] = 0x10u;			/* total 16, wire_len 16 */
	(void)hcc_core_deliver_frame(&r->c, HCC_SRC_DIRECT, bad, 16u, 600u);
	CHECK_EQ(r->c.id1_bad_total, 1u);
	CHECK(!r->c.plat_ready);

	/* valid id1 then an out-of-range id2 -> rejected, still waiting */
	rig_deliver(r, HCC_SRC_DIRECT, id1, sizeof(id1), 700u);
	{
		static hcc_u8 big[200];

		memset(big, 0, sizeof(big));
		memcpy(big, id2, sizeof(id2));
		big[4] = 0xc8u;		/* total 200 > the vendor 111 cap */
		big[5] = 0x00u;
		(void)hcc_core_deliver_frame(&r->c, HCC_SRC_DIRECT, big,
					     sizeof(big), 800u);
	}
	CHECK_EQ(r->c.id2_bad_len, 1u);
	CHECK(!r->c.dev_ready);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_DEVICE_READY);

	/* a frame delivered before the boot latch is refused */
	rig_init(r);
	CHECK_EQ(hcc_core_deliver_frame(&r->c, HCC_SRC_DIRECT, id1,
					sizeof(id1), 0u), -EPERM);

	/* timeout #1: no id1 within 2.0 s */
	rig_init(r);
	rig_boot_at(r, 0u);
	(void)hcc_core_tick(&r->c, r->c.deadline_ms - 1u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_PLAT_READY);
	(void)hcc_core_tick(&r->c, r->c.deadline_ms);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_FAILED);
	CHECK_EQ((int)r->c.fail_reason, (int)HCC_FAIL_PLAT_READY_TIMEOUT);
	CHECK_EQ(r->ev.last[HCC_EVENT_TIMEOUT],
		 (hcc_u32)HCC_FAIL_PLAT_READY_TIMEOUT);
	/* the mailbox service continues after failure (vendor S9) */
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x40u);
	fk_reset_writes(&r->fk);
	(void)hcc_core_tick(&r->c, r->c.deadline_ms + 10u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_D2H_ACK), 1u);

	/* timeout #2: id1 arrives but no id2 within 20.0 s */
	rig_init(r);
	rig_boot_at(r, 0u);
	rig_deliver(r, HCC_SRC_DIRECT, id1, sizeof(id1), 500u);
	(void)hcc_core_tick(&r->c, 500u + HCC_DEV_READY_MS - 1u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_WAIT_DEVICE_READY);
	(void)hcc_core_tick(&r->c, 500u + HCC_DEV_READY_MS);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_FAILED);
	CHECK_EQ((int)r->c.fail_reason, (int)HCC_FAIL_DEVICE_READY_TIMEOUT);
}

/* ------------------------------------------------------------------ */
/* 8. TXQ sync transition and H2D mailbox coalescing                   */
/* ------------------------------------------------------------------ */
static void test_txq_and_h2d(void)
{
	struct rig *r = &g_rig;
	hcc_u8 buf[64];
	hcc_u32 total = 0;

	printf("- TXQ sync + H2D mailbox\n");
	rig_init(r);

	/* H2D before the release/boot latch is refused */
	CHECK_EQ(hcc_core_mbox_send(&r->c, 3u, 10u), -EPERM);
	CHECK_EQ(r->fk.nwrites, 0u);
	rig_boot_at(r, 0u);

	rig_active(r, 100u);
	CHECK(r->c.txq_sync_submitted);
	CHECK_EQ(r->c.txq_sync_count, 1u);
	CHECK_EQ(r->ev.count[HCC_EVENT_TXQ_SYNC], 1u);
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_H2D_PENDING), 0x8u);
	CHECK_EQ(r->c.mbox_inflight, 0x8u);
	CHECK_EQ(r->c.mbox_publishes, 1u);
	CHECK_EQ(r->c.h2d_announces, 1u);
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_H2D_DOORBELL), 1u);

	/* while in flight, further announces coalesce in pending */
	CHECK_EQ(hcc_core_frame_build(&r->c, buf, sizeof(buf), 0u, 0u, 0u,
				      NULL, 0u, &total), 0);
	CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x80002000u, 0u, 0u, total,
				    200u), 0);
	CHECK_EQ(r->c.mbox_pending, 0x8u);
	CHECK_EQ(r->c.mbox_coalesced_batches, 1u);
	CHECK_EQ(r->c.mbox_publishes, 1u);

	/* device consumes the first mask -> tick publishes the coalesced one */
	fk_set_io(&r->fk, HCC_CA_H2D_PENDING, 0u);
	fk_reset_writes(&r->fk);
	(void)hcc_core_tick(&r->c, 300u);
	CHECK_EQ(r->c.mbox_completions, 1u);
	CHECK_EQ(r->c.mbox_publishes, 2u);
	CHECK_EQ(r->c.mbox_inflight, 0x8u);
	CHECK_EQ(r->c.mbox_pending, 0u);
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_H2D_PENDING), 0x8u);

	/* second completion */
	fk_set_io(&r->fk, HCC_CA_H2D_PENDING, 0u);
	(void)hcc_core_tick(&r->c, 400u);
	CHECK_EQ(r->c.mbox_completions, 2u);
	CHECK_EQ(r->c.mbox_inflight, 0u);

	/* refusals */
	CHECK_EQ(hcc_core_mbox_send(&r->c, 5u, 500u), -EACCES);	/* BUG() trap */
	CHECK_EQ(hcc_core_mbox_send(&r->c, 10u, 500u), -EINVAL);	/* id > 9 */
	CHECK_EQ(hcc_core_mbox_send(&r->c, 2u, 500u), -EPERM);	/* never sent */
	CHECK_EQ(hcc_core_mbox_send(&r->c, 1u, 500u), -EOPNOTSUPP); /* PM off */
	CHECK(r->c.h2d_refused >= 4u);
	CHECK_EQ(r->c.mbox_inflight, 0u);

	/* PM enabled: id1 is allowed and coalesces with a following id3 */
	r->c.cfg.pm_enabled = true;
	CHECK_EQ(hcc_core_mbox_send(&r->c, 1u, 600u), 0);
	CHECK_EQ(r->c.mbox_inflight, 0x2u);
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_H2D_PENDING), 0x2u);
	CHECK_EQ(hcc_core_mbox_send(&r->c, 3u, 610u), 0);
	CHECK_EQ(r->c.mbox_pending, 0x8u);
	fk_set_io(&r->fk, HCC_CA_H2D_PENDING, 0u);
	(void)hcc_core_tick(&r->c, 700u);
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_H2D_PENDING), 0x8u);
	fk_set_io(&r->fk, HCC_CA_H2D_PENDING, 0u);
	(void)hcc_core_tick(&r->c, 800u);
	CHECK_EQ(r->c.mbox_inflight, 0u);

	/* out[0] never clears -> bounded timeout, lane closed, no writes after */
	CHECK_EQ(hcc_core_mbox_send(&r->c, 3u, 1000u), 0);
	CHECK_EQ(r->c.mbox_inflight, 0x8u);
	fk_set_io(&r->fk, HCC_CA_H2D_PENDING, 0x8u);
	fk_reset_writes(&r->fk);
	(void)hcc_core_tick(&r->c, 1000u + r->c.cfg.mbox_clear_timeout_ms - 1u);
	CHECK_EQ(r->c.mbox_timeouts, 0u);
	(void)hcc_core_tick(&r->c, 1000u + r->c.cfg.mbox_clear_timeout_ms);
	CHECK_EQ(r->c.mbox_timeouts, 1u);
	CHECK(r->c.h2d_lane_closed);
	CHECK_EQ(r->c.mbox_inflight, 0u);
	CHECK_EQ(r->ev.last[HCC_EVENT_H2D_TIMEOUT], 0x8u);
	CHECK_EQ(hcc_core_mbox_send(&r->c, 3u, 3000u), -EBUSY);
}

/* ------------------------------------------------------------------ */
/* 9. SR ring wrap and commit validation                               */
/* ------------------------------------------------------------------ */
static void test_sr_ring(void)
{
	struct rig *r = &g_rig;
	hcc_u8 buf[32];
	hcc_u32 total = 0;
	hcc_u32 i;

	printf("- SR ring wrap\n");
	rig_init(r);
	rig_boot_at(r, 0u);
	CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x80001000u, 0u, 0u, 12u,
				   50u), 0);
	CHECK_EQ(r->c.sr[0].depth, 32u);
	CHECK_EQ(r->c.sr[0].last_slot, 0u);
	CHECK_EQ(r->c.sr[0].producer, 1u);
	CHECK_EQ(r->c.sr[0].last_word1, (12u << 16) | 0x6d2bu);
	CHECK_EQ(r->c.sr[0].nodes, 1u);

	/* 40 commits on a depth-32 ring: wrap at 32 and at 64 (phase bit) */
	rig_init(r);
	rig_boot_at(r, 0u);
	for (i = 0; i < 40u; i++) {
		CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x80000000u + 0x100u * i,
					    0u, 0u, 12u, 100u + i), 0);
		CHECK_EQ(r->c.sr[0].last_slot, i % 32u);
	}
	CHECK_EQ(r->c.sr[0].nodes, 40u);
	CHECK_EQ(r->c.sr[0].producer, HCC_IDX_PHASE_BIT + 8u);
	CHECK_EQ(r->c.sr[0].last_devva, 0x80000000u + 0x100u * 39u);
	/* all 40 announces coalesced into one publish + pending batch */
	CHECK_EQ(r->c.mbox_publishes, 1u);
	CHECK_EQ(r->c.mbox_inflight, 0x8u);
	CHECK_EQ(r->c.mbox_pending, 0x8u);
	CHECK_EQ(r->c.mbox_coalesced_batches, 39u);

	/* commit validation */
	CHECK_EQ(hcc_core_sr_commit(&r->c, 3, 0x80000000u, 0u, 0u, 12u,
				   200u), -EINVAL);
	CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x80000000u, 0u, 0u, 11u,
				   200u), -ERANGE);
	CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x80000000u, 0u, 0u,
				    HCC_FRAME_MAX_TOTAL + 1u, 200u), -ERANGE);
	CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x80000000u, 4u, 0u, 12u,
				   200u), -EACCES);
	CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x80000000u, 0u, 5u, 12u,
				   200u), -EACCES);
	CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x00001000u, 0u, 0u, 12u,
				   200u), -ERANGE);	/* outside DMA window */

	/* H2D after failure is refused */
	rig_init(r);
	rig_boot_at(r, 0u);
	(void)hcc_core_tick(&r->c, HCC_PLAT_READY_MS + 1u);
	CHECK_EQ((int)r->c.phase, (int)HCC_PHASE_FAILED);
	CHECK_EQ(hcc_core_sr_commit(&r->c, 0, 0x80000000u, 0u, 0u, 12u,
				    5000u), -EPERM);
	CHECK_EQ(hcc_core_frame_build(&r->c, buf, sizeof(buf), 4u, 0u, 0u,
				      NULL, 0u, &total), -EACCES);
}

/* ------------------------------------------------------------------ */
/* 10. heartbeat and PM                                                */
/* ------------------------------------------------------------------ */
static void test_heartbeat_pm(void)
{
	struct rig *r = &g_rig;
	static const hcc_u8 id4[12] = {
		0x04, 0x00, 0x00, 0x00, 0x0c, 0x00, 0x04, 0x00,
		0x00, 0x00, 0x5a, 0x5a
	};
	printf("- heartbeat / PM\n");
	rig_init(r);
	rig_boot_at(r, 0u);
	CHECK(!r->c.hb_armed);
	rig_deliver(r, HCC_SRC_DIRECT, id4, sizeof(id4), 100u);
	CHECK_EQ(r->c.hb_count, 1u);
	CHECK_EQ(r->ev.count[HCC_EVENT_HEARTBEAT], 1u);
	(void)hcc_core_tick(&r->c, 100000u);
	CHECK_EQ(r->c.hb_timeouts, 0u);		/* not armed: no watchdog */

	CHECK_EQ(hcc_core_heartbeat_arm(&r->c, 200u), 0);
	CHECK_EQ(r->c.hb_deadline_ms, 200u + HCC_HEARTBEAT_MS);
	rig_deliver(r, HCC_SRC_DIRECT, id4, sizeof(id4), 5000u);
	CHECK_EQ(r->c.hb_deadline_ms, 5000u + HCC_HEARTBEAT_MS);
	(void)hcc_core_tick(&r->c, 5000u + HCC_HEARTBEAT_MS - 1u);
	CHECK_EQ(r->c.hb_timeouts, 0u);
	(void)hcc_core_tick(&r->c, 5000u + HCC_HEARTBEAT_MS);
	CHECK_EQ(r->c.hb_timeouts, 1u);
	CHECK_EQ(r->ev.count[HCC_EVENT_HEARTBEAT_TIMEOUT], 1u);
	hcc_core_heartbeat_disarm(&r->c);

	/* PM: fresh rig; disabled by default */
	rig_init(r);
	CHECK_EQ(hcc_core_pm_request_sleep(&r->c, 20000u), -EOPNOTSUPP);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_H2D_PENDING), 0u);
	CHECK_EQ(hcc_core_heartbeat_arm(&r->c, 0u), 0);
	CHECK_EQ((int)r->c.hb_deadline_ms, (int)HCC_HEARTBEAT_MS);
	hcc_core_heartbeat_disarm(&r->c);

	/* enabled, but only from ACTIVE */
	rig_init(r);
	r->c.cfg.pm_enabled = true;
	CHECK_EQ(hcc_core_pm_request_sleep(&r->c, 20000u), -EPERM);
	rig_boot_at(r, 100u);
	CHECK_EQ(hcc_core_pm_request_sleep(&r->c, 20000u), -EPERM);
	rig_active(r, 30000u);		/* leaves the id3 mask 0x8 in flight */
	CHECK_EQ(hcc_core_pm_request_sleep(&r->c, 30100u), 0);
	CHECK(r->c.pm_sleep_pending);
	CHECK_EQ(r->c.h2d_pm_requests, 1u);
	CHECK_EQ(r->c.mbox_pending, 0x2u);	/* id1 coalesced behind id3 */
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_H2D_PENDING), 0x8u);
	fk_set_io(&r->fk, HCC_CA_H2D_PENDING, 0u);
	(void)hcc_core_tick(&r->c, 30200u);
	CHECK(r->c.pm_sleep_pending);	/* mbox done != sleep ack */
	CHECK_EQ(fk_get_io(&r->fk, HCC_CA_H2D_PENDING), 0x2u);
	/* the sleep-ack wait expires (vendor 50 jiffies) */
	(void)hcc_core_tick(&r->c, 31000u);
	CHECK(!r->c.pm_sleep_pending);
	CHECK_EQ(r->c.pm_sleep_timeouts, 1u);

	/* D2H id5 (device wake-up ok) is a mailbox bitmap id, not an HCC frame */
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x20u);
	(void)hcc_core_tick(&r->c, 30300u);
	CHECK(r->c.pm_wake_ok);
	CHECK_EQ(r->ev.last[HCC_EVENT_PM_STATE], 5u);

	/* D2H id4 aborts a pending sleep request (device refuses sleep) */
	CHECK_EQ(hcc_core_pm_request_sleep(&r->c, 40000u), 0);
	fk_set_io(&r->fk, HCC_CA_H2D_PENDING, 0u);
	fk_set_io(&r->fk, HCC_CA_D2H_BITMAP, 0x10u);
	(void)hcc_core_tick(&r->c, 40100u);
	CHECK(!r->c.pm_sleep_pending);
	CHECK_EQ(r->c.pm_sleep_aborts, 1u);
	fk_set_io(&r->fk, HCC_CA_H2D_PENDING, 0u);
	(void)hcc_core_tick(&r->c, 40200u);	/* settle the mailbox */

	/* timeout path */
	CHECK_EQ(hcc_core_pm_request_sleep(&r->c, 50000u), 0);
	(void)hcc_core_tick(&r->c, 50000u + HCC_PM_SLEEP_ACK_MS - 1u);
	CHECK(r->c.pm_sleep_pending);
	(void)hcc_core_tick(&r->c, 50000u + HCC_PM_SLEEP_ACK_MS);
	CHECK(!r->c.pm_sleep_pending);
	CHECK_EQ(r->c.pm_sleep_timeouts, 2u);
}

/* ------------------------------------------------------------------ */
/* 11. status line + write-allowlist census                            */
/* ------------------------------------------------------------------ */
static void test_status_and_safety(void)
{
	struct rig *r = &g_rig;
	char status[512];

	printf("- status line / write safety\n");
	rig_init(r);
	rig_boot_at(r, 0u);
	rig_active(r, 1000u);
	CHECK(hcc_core_status(&r->c, status, sizeof(status)) > 0);
	CHECK(strstr(status, "phase=ACTIVE") != NULL);
	CHECK(strstr(status, "latched") != NULL);
	CHECK(strstr(status, "bad=0") != NULL);
	CHECK_EQ(hcc_core_status(&r->c, NULL, sizeof(status)), -EINVAL);
	CHECK_EQ(hcc_core_status(&r->c, status, 0u), -EINVAL);

	/* every write this core performed must be in the allowlist */
	CHECK_EQ(fk_bad_ca_writes(&r->fk), 0u);
	CHECK_EQ(fk_writes_to_fw(&r->fk), 0u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_CPU_RELEASE), 0u);
	CHECK_EQ(fk_writes_to(&r->fk, HCC_CA_NEVER_WRITE), 0u);
	CHECK_EQ(r->c.bad_writes, 0u);
	CHECK_EQ(r->c.bad_reads, 0u);
	CHECK_EQ(r->fk.bad_region_reads, 0u);
	CHECK_EQ(r->fk.bad_region_writes, 0u);

	/* the write allowlist itself must keep out[5] and release closed */
	CHECK(!hcc_ca_writable(HCC_CA_NEVER_WRITE));
	CHECK(!hcc_ca_writable(HCC_CA_CPU_RELEASE));
}

/* ------------------------------------------------------------------ */
int main(void)
{
	printf("wifi-hcc-core selftest: hcc_core %s\n", "1.0");

	test_address_maps();
	test_index_arithmetic();
	test_frame_contract();
	test_boot_latch();
	test_cece_policy();
	test_d2h_service();
	test_ready_pair();
	test_txq_and_h2d();
	test_sr_ring();
	test_heartbeat_pm();
	test_status_and_safety();

	printf("\n%d checks, %d failure(s)\n", g_checks, g_failures);
	if (g_failures)
		printf("RESULT: FAIL\n");
	else
		printf("RESULT: PASS\n");
	return g_failures ? 1 : 0;
}
