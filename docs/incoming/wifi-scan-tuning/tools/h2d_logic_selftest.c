// SPDX-License-Identifier: GPL-2.0
/*
 * h2d_logic_selftest.c - device-free host selftest for the first REAL
 * post-ready H2D message path of the Hi5622V100 clean-room driver.
 *
 * Builds the portable code exactly as the kernel module does (same sources:
 * h2d_send_logic.c + h2d_frames.c + hcc_core.c) and asserts:
 *   - the byte-exact emission vectors (txq_sync 48 B wire / 72 B capture,
 *     txq_sync72 same wire with a 72 B node length, the ordered ctx_create
 *     g0 id1 key-0x64 precondition in its three create-helper type variants
 *     t1/t2/t3, stub, edca, del_user),
 *     including the SR node word (total<<16)|0x6D2B and the packed producer
 *     arithmetic via h2d_plan_post();
 *   - the gate matrix (phase, latched boot, release, arm, lane, liveness,
 *     per-load budget, announce in-flight, live out[0] pending, duplicate TXQ);
 *   - the strict sysfs grammar of h2d_send and the refusal of anything that is
 *     not one of the six EXACT builders (no synthetic payload path);
 *   - the ordered create -> TXQ gate exception (both frames are group 0/id 1,
 *     so the core phase reaches ACTIVE on the create; the driver still allows
 *     exactly one real TXQ frame after it).
 *
 * No kernel tree, no device access, no vendor artifact.  Run with the lane's
 * tools/build.sh (or: cc -std=c99 -Wall -Wextra -Werror -pedantic ...).
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "h2d_frames.h"
#include "h2d_send_logic.h"
#include "hcc_core.h"

static int checks_total;
static int checks_fail;

static void check(const char *name, int cond, const char *detail)
{
	checks_total++;
	if (!cond) {
		checks_fail++;
		printf("FAIL: %s%s%s\n", name,
		       detail && detail[0] ? " - " : "",
		       detail && detail[0] ? detail : "");
	}
}

static void hexdump(const char *tag, const u8 *p, size_t n)
{
	size_t i;

	printf("  %s = ", tag);
	for (i = 0; i < n; i++)
		printf("%02x", p[i]);
	printf("\n");
}

static void expect_bytes(const char *name, const u8 *got, size_t got_len,
			 const u8 *want, size_t want_len)
{
	int ok = got_len == want_len && !memcmp(got, want, want_len);

	check(name, ok, "");
	if (!ok) {
		printf("  got %zu bytes:", got_len);
		hexdump("got", got, got_len);
		hexdump("want", want, want_len);
	}
}

/* ------------------------------------------------------------------ */
/* expected byte-exact images (derived from the committed captures)    */
/* ------------------------------------------------------------------ */
static void expected_txq_sync(u8 out[48])
{
	static const u8 hdr[12] = {
		0x00, 0x01, 0x00, 0x04, 0x30, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x5a, 0x5a,
	};
	static const u8 payload[36] = {
		/* token[8] = {0,0,0,0,1,0,0,0} */
		0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		/* cmd 0x00d8, len 0x14 */
		0xd8, 0x00, 0x14, 0x00,
		/* data[20] = {01 00 00 00, 16 x 00} */
		0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		/* inherited tail 0xffffffff */
		0xff, 0xff, 0xff, 0xff,
	};

	memcpy(out, hdr, sizeof(hdr));
	memcpy(out + sizeof(hdr), payload, sizeof(payload));
}

/* The synthesized context-create frame (g0 id1, key 0x0064, len 0x14,
 * vap 4 / user 4, inherited 0xffffffff tail) for create-helper type
 * `type` (data[0]; 1/2/3 are the only allocating types) - from
 * ../wifi-ctx-create/recipes/ctx-create-g0id1-key64.json plus the type-dispatch
 * analysis in REPORT.md section 12. */
static void expected_ctx_create(u8 out[48], u8 type)
{
	static const u8 hdr[12] = {
		0x00, 0x01, 0x00, 0x04, 0x30, 0x00,
		0x01, 0x00, 0x00, 0x00, 0x5a, 0x5a,
	};
	u8 payload[36] = {
		/* token[8] = {0,0,0,0,1,0,0,0} */
		0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
		/* u16 key 0x0064, u16 len 0x14 */
		0x64, 0x00, 0x14, 0x00,
		/* data[20]: type, vap 4, user 4 at +4 */
		0x03, 0x04, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		/* inherited tail 0xffffffff */
		0xff, 0xff, 0xff, 0xff,
	};

	payload[12] = type;	/* payload+0xC = data[0] = frame byte 24 */
	memcpy(out, hdr, sizeof(hdr));
	memcpy(out + sizeof(hdr), payload, sizeof(payload));
}

static void expected_edca(u8 out[72])
{
	static const u8 hdr[12] = {
		0x00, 0x01, 0x00, 0x04, 0x48, 0x00,
		0x1d, 0x00, 0x00, 0x00, 0x5a, 0x5a,
	};

	memset(out, 0, 72);
	memcpy(out, hdr, sizeof(hdr));
}

static void expected_stub(u8 out[12], u8 dst)
{
	static const u8 hdr[12] = {
		0x00, 0x00, 0x00, 0x00, 0x0c, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x5a, 0x5a,
	};

	memcpy(out, hdr, sizeof(hdr));
	out[1] = (u8)((dst << 4) | 0x01u);
	out[9] = dst;
}

/* ------------------------------------------------------------------ */
/* emission tests                                                      */
/* ------------------------------------------------------------------ */
static void test_emission(void)
{
	struct h2d_frame f;
	u8 wire[H2D_MAX_TOTAL];
	u8 want[128];
	u8 pad72[72];
	u32 total = 0;
	enum h2d_err err = H2D_OK;
	size_t n = 0;
	int i;

	/* --- txq_sync: the vendor's first post-ready message, byte-exact --- */
	memset(&f, 0, sizeof(f));
	memset(wire, 0xaa, sizeof(wire));
	check("txq_sync builds", h2d_msg_build(H2D_MSG_TXQ_SYNC, 0, 0, NULL,
					       &f, wire, sizeof(wire), &total,
					       &err) == 0 &&
	      err == H2D_OK, h2d_strerror(err));
	check("txq_sync total = 0x30", total == 0x30u, "");
	expected_txq_sync(want);
	expect_bytes("txq_sync 48-byte wire image == phase38 capture",
		     wire, total, want, 48);
	check("txq_sync frame struct: group 0 id 1 dst 0 fb 4",
	      f.group == 0 && f.id == 1 && f.dst == 0 && f.fb == 4 &&
	      f.fa == 0 && f.fc == 0, "");
	check("txq_sync is an EXACT-policy frame",
	      (f.policy & H2D_F_EXACT) != 0, "");

	/* The 72-byte live vendor post buffer is the same frame padded with
	 * the inherited 0xff tail (capture fidelity, not the sent node len). */
	memset(pad72, 0, sizeof(pad72));
	check("txq_sync export pad_to 72 works",
	      h2d_frame_export(&f, pad72, sizeof(pad72), 72, 0xff, &n,
			       &err) == 0 && n == 72, h2d_strerror(err));
	memset(want, 0xff, 72);
	memcpy(want, wire, 48);
	expect_bytes("txq_sync 72-byte capture image == phase38 post buffer",
		     pad72, n, want, 72);

	/* --- SR node + packed producer plan (h2d_frames validator) -------- */
	{
		struct h2d_post_plan plan;

		check("plan_post txq_sync @producer 0",
		      h2d_plan_post(&f, 0, 32, 0x84543000u, 0, 0x8, &plan,
				    &err) == 0, h2d_strerror(err));
		check("plan node word0 == devva",
		      plan.sr.node_word0 == 0x84543000u, "");
		check("plan node word1 == (0x30<<16)|0x6d2b",
		      plan.sr.node_word1 == 0x00306d2bu, "");
		check("plan producer 0 -> 1", plan.sr.producer_before == 0 &&
		      plan.sr.producer_after == 1, "");
		check("plan node_index == 0", plan.sr.node_index == 0, "");
		check("plan mb id 3 pending 0x8",
		      plan.mb.id == 3 && plan.mb.pending_write == 0x8u, "");
		check("plan doorbell = out2 | 1", plan.mb.doorbell_write == 9u,
		      "");
		check("plan frame_total == 0x30", plan.frame_total == 0x30u, "");

		/* wrap: producer 31 -> 0x400 (phase toggle) */
		check("plan_post wraps producer 31 -> 0x400",
		      h2d_plan_post(&f, 31, 32, 0x84543000u, 0, 0, &plan,
				    &err) == 0 &&
		      plan.sr.node_index == 31 &&
		      plan.sr.producer_after == 0x400u, h2d_strerror(err));
		/* phase 1 slot 2 -> slot 3 */
		check("plan_post producer 0x402 -> 0x403",
		      h2d_plan_post(&f, 0x402, 32, 0x84543000u, 0, 0, &plan,
				    &err) == 0 &&
		      plan.sr.node_index == 2 &&
		      plan.sr.producer_after == 0x403u, h2d_strerror(err));
		/* coalescing precondition: out[0] != 0 is H2D_ESTATE */
		check("plan_post refuses unconsumed out[0] (H2D_ESTATE)",
		      h2d_plan_post(&f, 0, 32, 0x84543000u, 0x8, 0, &plan,
				    &err) != 0 && err == H2D_ESTATE,
		      h2d_strerror(err));
	}

	/* --- txq_sync72: same 48-byte frame, 72-byte LIVE SR node buffer --- */
	{
		struct h2d_post_plan plan;
		u32 node_len = 0;
		u8 pad_byte = 0;

		memset(&f, 0, sizeof(f));
		total = 0;
		check("txq_sync72 builds",
		      h2d_msg_build(H2D_MSG_TXQ_SYNC72, 0, 0, NULL, &f, wire,
				    sizeof(wire), &total, &err) == 0 &&
		      err == H2D_OK, h2d_strerror(err));
		check("txq_sync72 declared frame total stays 0x30",
		      total == H2D_TXQ_TOTAL, "");
		expected_txq_sync(want);
		expect_bytes("txq_sync72 48-byte wire image == txq_sync",
			     wire, total, want, 48);
		check("txq_sync72 frame struct identical (g0 id1 dst0 fb4)",
		      f.group == 0 && f.id == 1 && f.dst == 0 && f.fb == 4 &&
		      f.fa == 0 && f.fc == 0, "");
		check("txq_sync72 node_len plan = 72, pad = 0xff",
		      h2d_msg_emit_plan(H2D_MSG_TXQ_SYNC72, total, &node_len,
					&pad_byte) == 0 &&
		      node_len == 72u && pad_byte == 0xffu, "");
		check("txq_sync72 node word1 = (72<<16)|0x6d2b",
		      ((node_len << 16) | H2D_NODE_FLAGS) == 0x00486d2bu, "");
		check("txq_sync node_len plan = frame total, pad = 0x00",
		      h2d_msg_emit_plan(H2D_MSG_TXQ_SYNC, total, &node_len,
					&pad_byte) == 0 &&
		      node_len == total && pad_byte == 0u, "");
		check("txq_sync72 default plan_post still the 48-byte node word1",
		      h2d_plan_post(&f, 0, 32, 0x84543000u, 0, 0x8, &plan,
				    &err) == 0 &&
		      plan.sr.node_word1 == 0x00306d2bu &&
		      plan.frame_total == H2D_TXQ_TOTAL, h2d_strerror(err));
		check("txq_sync72 emit plan refuses a non-0x30 frame total",
		      h2d_msg_emit_plan(H2D_MSG_TXQ_SYNC72, 0x48, &node_len,
					&pad_byte) != 0, "");
		check("txq_sync72 emit plan refuses a bad selector",
		      h2d_msg_emit_plan(H2D_MSG_COUNT, total, &node_len,
					&pad_byte) != 0, "");
		check("txq_sync72 dst != 0 refused (byte-exact only)",
		      h2d_msg_build(H2D_MSG_TXQ_SYNC72, 1, 0, NULL, &f, wire,
				    sizeof(wire), &total, &err) != 0 &&
		      err == H2D_EPOLICY, h2d_strerror(err));
	}

	/* --- ctx_create: the ordered g0 id1 key-0x64 pre-TXQ message ------ */
	{
		struct h2d_post_plan plan;
		u32 node_len = 0;
		u8 pad_byte = 0;

		memset(&f, 0, sizeof(f));
		memset(wire, 0, sizeof(wire));
		total = 0;
		check("ctx_create builds",
		      h2d_msg_build(H2D_MSG_CTX_CREATE, 0, 0, NULL, &f, wire,
				    sizeof(wire), &total, &err) == 0 &&
		      err == H2D_OK, h2d_strerror(err));
		check("ctx_create total = 0x30", total == H2D_CTX_TOTAL, "");
		expected_ctx_create(want, 3);
		expect_bytes("ctx_create 48-byte wire image == the ctx-create recipe",
			     wire, total, want, 48);
		check("ctx_create frame struct: group 0 id 1 dst 0 fb 4 fa 0 fc 0",
		      f.group == 0 && f.id == 1 && f.dst == 0 && f.fb == 4 &&
		      f.fa == 0 && f.fc == 0, "");
		check("ctx_create is an EXACT-policy frame",
		      (f.policy & H2D_F_EXACT) != 0, "");
		check("ctx_create key at payload+8 == 0x0064",
		      wire[20] == 0x64 && wire[21] == 0x00, "");
		check("ctx_create len at payload+10 == 0x14",
		      wire[22] == 0x14 && wire[23] == 0x00, "");
		check("ctx_create data block type/vap/user == 3/4/4",
		      wire[24] == 3 && wire[25] == 4 && wire[28] == 4, "");
		check("ctx_create helper flags data[0x11]/[0x12] are 0",
		      wire[24 + 0x11] == 0 && wire[24 + 0x12] == 0, "");
		check("ctx_create inherited tail is 0xffffffff",
		      wire[44] == 0xff && wire[47] == 0xff, "");
		check("ctx_create node_len plan = declared total (48), pad 0",
		      h2d_msg_emit_plan(H2D_MSG_CTX_CREATE, total, &node_len,
					&pad_byte) == 0 &&
		      node_len == H2D_CTX_TOTAL && pad_byte == 0u, "");
		check("ctx_create plan_post node word1 = (0x30<<16)|0x6d2b",
		      h2d_plan_post(&f, 0, 32, 0x84543000u, 0, 0, &plan,
				    &err) == 0 &&
		      plan.sr.node_word1 == 0x00306d2bu &&
		      plan.frame_total == H2D_CTX_TOTAL, h2d_strerror(err));
		check("ctx_create dst != 0 refused (byte-exact only)",
		      h2d_msg_build(H2D_MSG_CTX_CREATE, 1, 0, NULL, &f, wire,
				    sizeof(wire), &total, &err) != 0 &&
		      err == H2D_EPOLICY, h2d_strerror(err));
		/* the parameterised builder + the firmware parser's constraints */
		check("ctx_create builder types 1/2/3 accepted",
		      h2d_build_ctx_create(&f, 0, 1, 4, 4, &err) == 0 &&
		      h2d_build_ctx_create(&f, 0, 2, 4, 4, &err) == 0 &&
		      h2d_build_ctx_create(&f, 0, 3, 4, 4, &err) == 0,
		      h2d_strerror(err));
		check("ctx_create builder types 0/4/5 refused (no +0x190 alloc)",
		      h2d_build_ctx_create(&f, 0, 0, 4, 4, &err) != 0 &&
		      h2d_build_ctx_create(&f, 0, 4, 4, 4, &err) != 0 &&
		      h2d_build_ctx_create(&f, 0, 5, 4, 4, &err) != 0, "");
		check("ctx_create builder module index > 0x12 refused",
		      h2d_build_ctx_create(&f, 0, 3, 0x13, 4, &err) != 0 &&
		      h2d_build_ctx_create(&f, 0, 3, 4, 0x13, &err) != 0, "");
	}

	/* --- ctx_create TYPE variants: only data[0] (wire[24]) differs ------ */
	{
		u8 wire_t1[H2D_MAX_TOTAL], wire_t2[H2D_MAX_TOTAL];
		u32 total_t1 = 0, total_t2 = 0;
		u32 node_len = 0;
		u8 pad_byte = 0;

		memset(&f, 0, sizeof(f));
		memset(wire_t1, 0xaa, sizeof(wire_t1));
		check("ctx_create_t1 builds",
		      h2d_msg_build(H2D_MSG_CTX_CREATE_T1, 0, 0, NULL, &f,
				    wire_t1, sizeof(wire_t1), &total_t1,
				    &err) == 0 && err == H2D_OK,
		      h2d_strerror(err));
		check("ctx_create_t1 total = 0x30",
		      total_t1 == H2D_CTX_TOTAL, "");
		expected_ctx_create(want, 1);
		expect_bytes("ctx_create_t1 48-byte wire image (data[0]=1)",
			     wire_t1, total_t1, want, 48);
		check("ctx_create_t1 frame struct identical (g0 id1 dst0 fb4)",
		      f.group == 0 && f.id == 1 && f.dst == 0 && f.fb == 4 &&
		      f.fa == 0 && f.fc == 0, "");
		check("ctx_create_t1 wire[24] = 1 (type field)",
		      wire_t1[24] == 1 && wire_t1[25] == 4 &&
		      wire_t1[28] == 4, "");

		memset(&f, 0, sizeof(f));
		memset(wire_t2, 0xaa, sizeof(wire_t2));
		check("ctx_create_t2 builds",
		      h2d_msg_build(H2D_MSG_CTX_CREATE_T2, 0, 0, NULL, &f,
				    wire_t2, sizeof(wire_t2), &total_t2,
				    &err) == 0 && err == H2D_OK,
		      h2d_strerror(err));
		expected_ctx_create(want, 2);
		expect_bytes("ctx_create_t2 48-byte wire image (data[0]=2)",
			     wire_t2, total_t2, want, 48);
		check("ctx_create_t2 wire[24] = 2 (type field)",
		      wire_t2[24] == 2 && wire_t2[25] == 4 &&
		      wire_t2[28] == 4, "");

		/* the three variants are byte-identical except wire[24] */
		check("ctx_create variants differ ONLY at data[0]",
		      wire_t1[24] == 1 && wire_t2[24] == 2 && wire[24] == 3 &&
		      total_t1 == total_t2 && total_t1 == total &&
		      !memcmp(wire_t1, wire, 24) &&
		      !memcmp(wire_t1 + 25, wire + 25, 23) &&
		      !memcmp(wire_t2 + 25, wire + 25, 23), "");
		check("ctx_create variants share the fb/type/user fields",
		      wire_t1[3] == 4 && wire_t2[3] == 4 && wire_t1[28] == 4 &&
		      wire_t2[28] == 4, "");
		check("h2d_msg_is_ctx covers all three type variants",
		      h2d_msg_is_ctx(H2D_MSG_CTX_CREATE) &&
		      h2d_msg_is_ctx(H2D_MSG_CTX_CREATE_T1) &&
		      h2d_msg_is_ctx(H2D_MSG_CTX_CREATE_T2) &&
		      !h2d_msg_is_ctx(H2D_MSG_TXQ_SYNC) &&
		      !h2d_msg_is_ctx(H2D_MSG_EDCA_OPT) &&
		      !h2d_msg_is_ctx(H2D_MSG_STUB), "");
		check("ctx_create_t1 dst != 0 refused (byte-exact only)",
		      h2d_msg_build(H2D_MSG_CTX_CREATE_T1, 1, 0, NULL, &f,
				    wire_t1, sizeof(wire_t1), &total_t1,
				    &err) != 0 && err == H2D_EPOLICY,
		      h2d_strerror(err));
		check("ctx_create_t1/t2 node_len plan = 0x30, pad 0",
		      h2d_msg_emit_plan(H2D_MSG_CTX_CREATE_T1, total_t1,
					&node_len, &pad_byte) == 0 &&
		      node_len == H2D_CTX_TOTAL && pad_byte == 0u &&
		      h2d_msg_emit_plan(H2D_MSG_CTX_CREATE_T2, total_t2,
					&node_len, &pad_byte) == 0 &&
		      node_len == H2D_CTX_TOTAL && pad_byte == 0u, "");
	}

	/* --- stub: the payload-free group-0 id-0 probe -------------------- */
	memset(&f, 0, sizeof(f));
	total = 0;
	check("stub builds",
	      h2d_msg_build(H2D_MSG_STUB, 0, 0, NULL, &f, wire, sizeof(wire),
			    &total, &err) == 0, h2d_strerror(err));
	check("stub total = 0x0c", total == 12u, "");
	expected_stub(want, 0);
	expect_bytes("stub 12-byte wire image", wire, total, want, 12);
	check("stub refuses dst > 0xf",
	      h2d_msg_build(H2D_MSG_STUB, 0x10, 0, NULL, &f, wire,
			    sizeof(wire), &total, &err) != 0, "");
	check("stub dst=7 wire", h2d_msg_build(H2D_MSG_STUB, 7, 0, NULL, &f,
					       wire, sizeof(wire), &total,
					       &err) == 0, "");
	expected_stub(want, 7);
	expect_bytes("stub dst=7 wire image", wire, total, want, 12);

	/* --- edca: the live steady-state g0 id 0x1d frame ----------------- */
	memset(&f, 0, sizeof(f));
	total = 0;
	check("edca builds",
	      h2d_msg_build(H2D_MSG_EDCA_OPT, 0, 0, NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      err == H2D_OK, h2d_strerror(err));
	check("edca total = 0x48", total == 0x48u, "");
	expected_edca(want);
	expect_bytes("edca 72-byte wire image", wire, total, want, 72);
	check("edca frame struct: group 0 id 0x1d fb 4",
	      f.group == 0 && f.id == 0x1d && f.fb == 4 && f.fa == 0 &&
	      f.fc == 0, "");

	/* --- del_user: exact 12-byte payload ------------------------------ */
	memset(&f, 0, sizeof(f));
	total = 0;
	check("del_user (plain) builds",
	      h2d_msg_build(H2D_MSG_DEL_USER, 0, 1, NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      err == H2D_OK, h2d_strerror(err));
	check("del_user total = 0x18", total == 24u, "");
	{
		static const u8 want_du[24] = {
			0x00, 0x01, 0x00, 0x00, 0x18, 0x00,
			0x09, 0x00, 0x00, 0x00, 0x5a, 0x5a,
			0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		};

		expect_bytes("del_user user=1 wire image", wire, total,
			     want_du, 24);
	}
	{
		static const u8 addr[4] = { 0x11, 0x22, 0x33, 0x44 };
		static const u8 want_du[24] = {
			0x00, 0x01, 0x00, 0x00, 0x18, 0x00,
			0x09, 0x00, 0x00, 0x00, 0x5a, 0x5a,
			0xff, 0x0f, 0x11, 0x22, 0x33, 0x44,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		};

		total = 0;
		check("del_user user=0xfff addr builds",
		      h2d_msg_build(H2D_MSG_DEL_USER, 0, 0x0fff, addr, &f,
				    wire, sizeof(wire), &total, &err) == 0,
		      h2d_strerror(err));
		expect_bytes("del_user user=0xfff addr wire image", wire,
			     total, want_du, 24);
	}
	check("del_user 0xfff without addr refused",
	      h2d_msg_build(H2D_MSG_DEL_USER, 0, 0x0fff, NULL, &f, wire,
			    sizeof(wire), &total, &err) != 0, "");
	check("del_user non-0xfff with addr refused",
	      h2d_msg_build(H2D_MSG_DEL_USER, 0, 1, (const u8 *)"\x11\x22\x33\x44",
			    &f, wire, sizeof(wire), &total, &err) != 0, "");

	/* --- every message: legal header contract ------------------------- */
	for (i = 0; i < (int)H2D_MSG_COUNT; i++) {
		struct h2d_frame g;
		u8 w[H2D_MAX_TOTAL];
		u32 t = 0;
		const u8 *addr = NULL;

		memset(&g, 0, sizeof(g));
		if (i == (int)H2D_MSG_DEL_USER)
			addr = (const u8 *)"\x0a\x0b\x0c\x0d";
		check("emission header contract msg",
		      h2d_msg_build((enum h2d_msg)i, 0,
				    i == (int)H2D_MSG_DEL_USER ? 0x0fffu : 0,
				    addr, &g, w, sizeof(w), &t, &err) == 0,
		      h2d_strerror(err));
		check("emission group 0 only", g.group == 0, "");
		check("emission id is an exact-builder id",
		      g.id == 0 || g.id == 1 || g.id == 0x1d || g.id == 9, "");
		check("emission tag 0x5a5a", w[10] == 0x5a && w[11] == 0x5a,
		      "");
		check("emission declared total == wire length",
		      (u32)(w[4] | (w[5] << 8)) == t, "");
		check("emission alloc nibble 1", (w[1] & 0x0f) == 0x01, "");
		check("emission dst nibbles agree",
		      (w[1] >> 4) == w[9] && w[9] == g.dst, "");
	}
	check("txq_sync dst != 0 refused (byte-exact only)",
	      h2d_msg_build(H2D_MSG_TXQ_SYNC, 1, 0, NULL, &f, wire,
			    sizeof(wire), &total, &err) != 0 && err == H2D_EPOLICY,
	      h2d_strerror(err));
}

/* ------------------------------------------------------------------ */
/* gate matrix                                                         */
/* ------------------------------------------------------------------ */
static void test_gates(void)
{
	struct h2d_live good;

	memset(&good, 0, sizeof(good));
	good.armed = true;
	good.released = true;
	good.boot_latched = true;
	good.phase = HCC_PHASE_READY_TXQ_SYNC;
	good.budget = 1;
	good.out0 = 0;
	good.mbox_inflight = 0;

	check("gate: READY + all preconditions -> ok (txq_sync)",
	      h2d_gate_check(&good, H2D_MSG_TXQ_SYNC) == H2D_GATE_OK, "");
	check("gate: READY + all preconditions -> ok (txq_sync72)",
	      h2d_gate_check(&good, H2D_MSG_TXQ_SYNC72) == H2D_GATE_OK, "");
	check("gate: READY + all preconditions -> ok (edca)",
	      h2d_gate_check(&good, H2D_MSG_EDCA_OPT) == H2D_GATE_OK, "");
	check("gate: READY + all preconditions -> ok (del_user)",
	      h2d_gate_check(&good, H2D_MSG_DEL_USER) == H2D_GATE_OK, "");
	check("gate: READY + all preconditions -> ok (stub)",
	      h2d_gate_check(&good, H2D_MSG_STUB) == H2D_GATE_OK, "");
	check("gate: READY + all preconditions -> ok (ctx_create)",
	      h2d_gate_check(&good, H2D_MSG_CTX_CREATE) == H2D_GATE_OK, "");
	check("gate: READY + all preconditions -> ok (ctx_create_t1)",
	      h2d_gate_check(&good, H2D_MSG_CTX_CREATE_T1) == H2D_GATE_OK, "");
	check("gate: READY + all preconditions -> ok (ctx_create_t2)",
	      h2d_gate_check(&good, H2D_MSG_CTX_CREATE_T2) == H2D_GATE_OK, "");

	/* The ORDERED path: ctx_create is also g0/id1, so the core phase is ACTIVE
	 * after it.  The driver allows exactly one real TXQ frame in ACTIVE when
	 * this load committed the create, and refuses a duplicate create. */
	{
		struct h2d_live l = good;

		l.ctx_created = true;
		l.phase = HCC_PHASE_ACTIVE;
		check("gate: ordered create -> ACTIVE + ctx_created -> txq ok",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) == H2D_GATE_OK, "");
		check("gate: ordered create -> ACTIVE + ctx_created -> txq_sync72 ok",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC72) == H2D_GATE_OK, "");
		check("gate: create is once per load (DUP_CTX)",
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE) ==
		      H2D_GATE_DUP_CTX, "");
		check("gate: any ctx type variant is once per load (shared)",
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE_T1) ==
		      H2D_GATE_DUP_CTX &&
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE_T2) ==
		      H2D_GATE_DUP_CTX, "");
		l.txq_submitted = true;
		check("gate: ordered path still allows only ONE txq frame",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC72) ==
		      H2D_GATE_DUP_TXQ, "");
		l.txq_submitted = false;
		l.ctx_created = false;
		check("gate: ACTIVE without a create is not a TXQ path (TXQ_PHASE)",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_TXQ_PHASE, "");
		check("gate: ACTIVE without a create refuses ctx_create (CTX_PHASE)",
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE) ==
		      H2D_GATE_CTX_PHASE &&
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE_T1) ==
		      H2D_GATE_CTX_PHASE, "");
	}

	/* READY_TXQ_SYNC or later: ACTIVE allows all but the once-only TXQ. */
	{
		struct h2d_live l = good;

		l.phase = HCC_PHASE_ACTIVE;
		l.txq_submitted = true;
		check("gate: ACTIVE -> txq_sync refused DUP_TXQ",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_DUP_TXQ, "");
		check("gate: ACTIVE -> txq_sync72 refused DUP_TXQ (same idea)",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC72) ==
		      H2D_GATE_DUP_TXQ, "");
		check("gate: ACTIVE -> edca ok",
		      h2d_gate_check(&l, H2D_MSG_EDCA_OPT) == H2D_GATE_OK,
		      "");
		l.txq_submitted = false;
		check("gate: ACTIVE without submission -> TXQ_PHASE",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_TXQ_PHASE, "");
		check("gate: txq_sync72 is READY-only too",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC72) ==
		      H2D_GATE_TXQ_PHASE, "");
	}

	/* boot-stage / release gates */
	{
		struct h2d_live l;

		l = good;
		l.armed = false;
		check("gate: not armed",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_NOT_ARMED, "");
		l = good;
		l.released = false;
		check("gate: not released",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_NOT_RELEASED, "");
		check("gate: ctx_create not released",
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE) ==
		      H2D_GATE_NOT_RELEASED, "");
		l = good;
		l.boot_latched = false;
		check("gate: boot not latched (never H2D during ROM boot)",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_NOT_LATCHED, "");
		l = good;
		l.phase = HCC_PHASE_BOOT;
		check("gate: phase BOOT refused",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_PHASE, "");
		check("gate: phase BOOT refuses ctx_create",
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE) ==
		      H2D_GATE_PHASE, "");
		l.phase = HCC_PHASE_WAIT_PLAT_READY;
		check("gate: phase WAIT_PLAT_READY refused",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_PHASE, "");
		l.phase = HCC_PHASE_WAIT_DEVICE_READY;
		check("gate: phase WAIT_DEVICE_READY refused",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_PHASE, "");
		l.phase = HCC_PHASE_IDLE;
		check("gate: phase IDLE refused",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_PHASE, "");
		l.phase = HCC_PHASE_FAILED;
		check("gate: phase FAILED refused (FAILED is not later)",
		      h2d_gate_check(&l, H2D_MSG_EDCA_OPT) ==
		      H2D_GATE_PHASE, "");
	}

	/* one-frame-per-idea / lane / coalescing gates */
	{
		struct h2d_live l;

		l = good;
		l.lane_closed = true;
		check("gate: H2D lane closed after timeout",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_LANE_CLOSED, "");
		l = good;
		l.liveness_sent = 1;
		check("gate: liveness stub already used the idea",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_LIVENESS_USED, "");
		l = good;
		l.frames_sent = 1;
		check("gate: budget 1/1 exhausted",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_BUDGET, "");
		l = good;
		l.budget = 0;
		check("gate: budget 0 is the negative control",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_BUDGET, "");
		l = good;
		l.budget = 8;
		l.frames_sent = 7;
		check("gate: supervised budget 8 allows frame 8",
		      h2d_gate_check(&l, H2D_MSG_EDCA_OPT) ==
		      H2D_GATE_OK, "");
		l = good;
		l.frames_sent = 1;
		check("gate: ctx_create obeys the per-load budget",
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE) ==
		      H2D_GATE_BUDGET, "");
		l = good;
		l.liveness_sent = 1;
		check("gate: ctx_create refuses after the liveness stub",
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE) ==
		      H2D_GATE_LIVENESS_USED, "");
		l = good;
		l.out0 = 0x8;
		check("gate: ctx_create refuses an unconsumed out[0]",
		      h2d_gate_check(&l, H2D_MSG_CTX_CREATE) ==
		      H2D_GATE_OUT0_PENDING, "");
		l = good;
		l.mbox_inflight = 0x8;
		check("gate: previous announce in flight",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_INFLIGHT, "");
		l = good;
		l.out0 = 0x8;
		check("gate: live out[0] bit3 pending",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_OUT0_PENDING, "");
		l.out0 = 0x100;
		check("gate: any live out[0] pending bit blocks",
		      h2d_gate_check(&l, H2D_MSG_TXQ_SYNC) ==
		      H2D_GATE_OUT0_PENDING, "");
	}

	check("gate: bad selector refused", h2d_gate_check(&good, H2D_MSG_COUNT) ==
	      H2D_GATE_BAD_MSG, "");
	check("gate: NULL state refused", h2d_gate_check(NULL, H2D_MSG_STUB) ==
	      H2D_GATE_BAD_MSG, "");
	check("gate: reason names are distinct", strcmp(
	      h2d_gate_name(H2D_GATE_OUT0_PENDING),
	      h2d_gate_name(H2D_GATE_BUDGET)) != 0, "");
	check("gate: ok name", strcmp(h2d_gate_name(H2D_GATE_OK), "ok") == 0, "");
}

/* ------------------------------------------------------------------ */
/* strict sysfs grammar                                                */
/* ------------------------------------------------------------------ */
static void test_parser(void)
{
	struct h2d_req req;
	u8 wire[H2D_MAX_TOTAL];
	struct h2d_frame f;
	u32 total = 0;
	enum h2d_err err = H2D_OK;

	memset(&req, 0, sizeof(req));
	check("parse 'txq_sync'", h2d_req_parse("txq_sync", &req) == 0 &&
	      req.msg == H2D_MSG_TXQ_SYNC && req.dst == 0, "");
	check("parse 'txq_sync\n'", h2d_req_parse("txq_sync\n", &req) == 0,
	      "");
	check("parse 'txq_sync dst=0'",
	      h2d_req_parse("txq_sync dst=0", &req) == 0, "");
	check("parse 'txq_sync dst=1' refused (byte-exact)",
	      h2d_req_parse("txq_sync dst=1", &req) != 0, "");
	check("parse 'txq_sync72'",
	      h2d_req_parse("txq_sync72", &req) == 0 &&
	      req.msg == H2D_MSG_TXQ_SYNC72 && req.dst == 0, "");
	check("parse 'txq_sync72\n'",
	      h2d_req_parse("txq_sync72\n", &req) == 0, "");
	check("parse 'txq_sync72 dst=1' refused (byte-exact)",
	      h2d_req_parse("txq_sync72 dst=1", &req) != 0, "");
	check("parse name lookup txq_sync72",
	      h2d_msg_name_parse("txq_sync72", &req.msg) == 0 &&
	      req.msg == H2D_MSG_TXQ_SYNC72, "");
	check("parse 'ctx_create'", h2d_req_parse("ctx_create", &req) == 0 &&
	      req.msg == H2D_MSG_CTX_CREATE && req.dst == 0, "");
	check("parse 'ctx_create\n'", h2d_req_parse("ctx_create\n", &req) == 0,
	      "");
	check("parse 'ctx_create dst=0'",
	      h2d_req_parse("ctx_create dst=0", &req) == 0, "");
	check("parse 'ctx_create dst=1' refused (byte-exact)",
	      h2d_req_parse("ctx_create dst=1", &req) != 0, "");
	check("parse ctx_create rejects an unknown key",
	      h2d_req_parse("ctx_create type=1", &req) != 0, "");
	check("parse name lookup ctx_create",
	      h2d_msg_name_parse("ctx_create", &req.msg) == 0 &&
	      req.msg == H2D_MSG_CTX_CREATE, "");
	check("parse 'ctx_create_t1'",
	      h2d_req_parse("ctx_create_t1", &req) == 0 &&
	      req.msg == H2D_MSG_CTX_CREATE_T1, "");
	check("parse 'ctx_create_t2\n'",
	      h2d_req_parse("ctx_create_t2\n", &req) == 0 &&
	      req.msg == H2D_MSG_CTX_CREATE_T2, "");
	check("parse 'ctx_create_t1 dst=1' refused (byte-exact)",
	      h2d_req_parse("ctx_create_t1 dst=1", &req) != 0, "");
	check("parse name lookup ctx_create_t1/t2",
	      h2d_msg_name_parse("ctx_create_t1", &req.msg) == 0 &&
	      req.msg == H2D_MSG_CTX_CREATE_T1 &&
	      h2d_msg_name_parse("ctx_create_t2", &req.msg) == 0 &&
	      req.msg == H2D_MSG_CTX_CREATE_T2, "");
	check("parse 'stub dst=15'", h2d_req_parse("stub dst=15", &req) == 0 &&
	      req.msg == H2D_MSG_STUB && req.dst == 15, "");
	check("parse 'stub dst=16' refused",
	      h2d_req_parse("stub dst=16", &req) != 0, "");
	check("parse 'stub, dst=3' separators",
	      h2d_req_parse("stub, dst=3", &req) == 0 && req.dst == 3, "");
	check("parse 'edca'", h2d_req_parse("edca", &req) == 0 &&
	      req.msg == H2D_MSG_EDCA_OPT, "");
	check("parse 'del_user user=1'",
	      h2d_req_parse("del_user user=1", &req) == 0 &&
	      req.has_user && req.user_id == 1 && !req.has_addr, "");
	check("parse 'del_user user=0xfff addr=11223344'",
	      h2d_req_parse("del_user user=0xfff addr=11223344", &req) == 0 &&
	      req.user_id == 0x0fff && req.has_addr &&
	      req.addr[0] == 0x11 && req.addr[3] == 0x44, "");
	check("parse 'del_user user=65535' decimal",
	      h2d_req_parse("del_user user=65535", &req) == 0 &&
	      req.user_id == 0xffff, "");
	check("parse 'del_user user=0x10000' overflow refused",
	      h2d_req_parse("del_user user=0x10000", &req) == -ERANGE, "");
	check("parse 'del_user' without user refused",
	      h2d_req_parse("del_user", &req) != 0, "");
	check("parse '1' refused (needs an explicit message)",
	      h2d_req_parse("1", &req) != 0, "");
	check("parse 'garbage' refused", h2d_req_parse("garbage", &req) != 0,
	      "");
	check("parse 'txq_sync id=1' unknown key refused",
	      h2d_req_parse("txq_sync id=1", &req) != 0, "");
	check("parse 'stub addr=11223344' key not valid for stub refused",
	      h2d_req_parse("stub addr=11223344", &req) != 0, "");
	check("parse 'del_user user=1 addr=11223344' parses (builder then refuses)",
	      h2d_req_parse("del_user user=1 addr=11223344", &req) == 0 &&
	      req.has_addr, "");
	check("parse '' refused", h2d_req_parse("", &req) != 0, "");
	check("parse NULL refused", h2d_req_parse(NULL, &req) != 0, "");
	check("parse name lookup exact", h2d_msg_name_parse("txq_sync",
	      &req.msg) == 0 && req.msg == H2D_MSG_TXQ_SYNC, "");
	check("parse name lookup rejects prefix",
	      h2d_msg_name_parse("txq_syn", &req.msg) != 0, "");
	check("parse name lookup rejects suffix",
	      h2d_msg_name_parse("stubx", &req.msg) != 0, "");

	/* end-to-end: parse -> build (the kernel store path is parse+build+post) */
	check("parse->build del_user user=0xfff addr=a1b2c3d4",
	      h2d_req_parse("del_user user=0xfff addr=a1b2c3d4", &req) == 0 &&
	      h2d_msg_build(req.msg, req.dst, req.user_id,
			    req.has_addr ? req.addr : NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      total == 24u, h2d_strerror(err));
	check("parse->build txq_sync",
	      h2d_req_parse("txq_sync", &req) == 0 &&
	      h2d_msg_build(req.msg, req.dst, req.user_id, NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      total == 0x30u, h2d_strerror(err));
	check("parse->build ctx_create (the ordered pre-TXQ frame)",
	      h2d_req_parse("ctx_create", &req) == 0 &&
	      h2d_msg_build(req.msg, req.dst, req.user_id, NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      total == H2D_CTX_TOTAL && wire[20] == 0x64 &&
	      wire[24] == 3 && wire[28] == 4, h2d_strerror(err));
	check("parse->build ctx_create_t1 (data[0]=1)",
	      h2d_req_parse("ctx_create_t1", &req) == 0 &&
	      h2d_msg_build(req.msg, req.dst, req.user_id, NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      total == H2D_CTX_TOTAL && wire[24] == 1,
	      h2d_strerror(err));
	check("parse->build ctx_create_t2 (data[0]=2)",
	      h2d_req_parse("ctx_create_t2", &req) == 0 &&
	      h2d_msg_build(req.msg, req.dst, req.user_id, NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      total == H2D_CTX_TOTAL && wire[24] == 2,
	      h2d_strerror(err));
	check("parse->build del_user user=1 addr=... -> builder refuses",
	      h2d_req_parse("del_user user=1 addr=a1b2c3d4", &req) == 0 &&
	      h2d_msg_build(req.msg, req.dst, req.user_id, req.addr, &f,
			    wire, sizeof(wire), &total, &err) != 0, "");
}

/* ------------------------------------------------------------------ */
/* no forbidden message can be selected                                */
/* ------------------------------------------------------------------ */
static void test_policy(void)
{
	struct h2d_frame f;
	u8 wire[H2D_MAX_TOTAL];
	u32 total = 0;
	enum h2d_err err = H2D_OK;
	int i;

	/* Only the four exact message ids are reachable; the group-4 (device->
	 * host) and mailbox id-5 trap are not in the selector at all. */
	for (i = 0; i < (int)H2D_MSG_COUNT; i++) {
		memset(&f, 0, sizeof(f));
		if (h2d_msg_build((enum h2d_msg)i, 0, 0, NULL, &f, wire,
				  sizeof(wire), &total, &err) != 0)
			continue;
		check("policy: emitted group is 0", f.group == 0, "");
		check("policy: emitted id != 5", f.id != 5, "");
	}
	check("policy: txq_sync wire cmd is 0x00d8",
	      h2d_msg_build(H2D_MSG_TXQ_SYNC, 0, 0, NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      wire[12 + 8] == 0xd8 && wire[12 + 9] == 0x00, "");
	check("policy: txq_sync72 wire is the same byte-exact frame",
	      h2d_msg_build(H2D_MSG_TXQ_SYNC72, 0, 0, NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      total == 0x30u && wire[12 + 8] == 0xd8 &&
	      wire[12 + 10] == 0x14, h2d_strerror(err));
	check("policy: txq_sync wire len field is 0x14",
	      wire[12 + 10] == 0x14 && wire[12 + 11] == 0x00, "");
	check("policy: txq_sync 72-byte capture tail is 0xff * 24",
	      h2d_frame_export(&f, wire, sizeof(wire), 72, 0xff, NULL,
			       &err) == 0 && wire[48] == 0xff &&
	      wire[71] == 0xff, h2d_strerror(err));
	check("policy: ctx_create is a g0 id1 key-0x64 config event",
	      h2d_msg_build(H2D_MSG_CTX_CREATE, 0, 0, NULL, &f, wire,
			    sizeof(wire), &total, &err) == 0 &&
	      f.group == 0 && f.id == 1 && wire[20] == 0x64 &&
	      wire[21] == 0x00 && wire[22] == 0x14 && wire[23] == 0x00,
	      h2d_strerror(err));
	check("policy: ctx_create is refused unless the gate allows it",
	      h2d_gate_check(NULL, H2D_MSG_CTX_CREATE) == H2D_GATE_BAD_MSG, "");
}

int main(void)
{
	printf("h2d_send_logic selftest (host, device-free)\n");
	test_emission();
	test_gates();
	test_parser();
	test_policy();
	printf("h2d_send_logic selftest: %d/%d checks PASS\n",
	       checks_total - checks_fail, checks_total);
	return checks_fail ? 1 : 0;
}