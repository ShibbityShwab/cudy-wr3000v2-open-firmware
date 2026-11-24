/* SPDX-License-Identifier: GPL-2.0 */
/*
 * beacon_selftest.c - host selftest for the wifi-beacon-static lane
 * (task-140): the module channel-config frame builder (g0 id1 key 0x0124 /
 * WAL cfg cmd 0x0124, 4-byte body), the pure AP-start ordering plan, and the
 * provider-contract validation of the new capability.  Pure C99; no device.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_ap_frames.h"
#include "hi5622_scan_logic.h"

static int failures;
static int checks;

#define CHECK(cond) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static unsigned int rd16(const u8 *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

static unsigned int rd32(const u8 *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
	       ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

/* g0 id1 channel frame: total 28 + 4 = 32; key 0x0124 at +20, body len 4 at
 * +22, 4-byte body at +24, tag 0x5a5a at +10, inherited 0xff pad at +28. */
static void test_chan_g0(void)
{
	struct haf_ap_params p;
	struct haf_frame f;

	memset(&p, 0, sizeof(p));
	memset(&f, 0, sizeof(f));
	p.carrier = HAF_CARRIER_G0_ID1;
	p.band = 0;
	p.channel = 6;
	p.vap_id = 0;
	p.module = 0;
	p.dst = 0;
	CHECK(haf_build_chan_frame(&f, &p) == 0);
	CHECK(f.len == 32u);
	CHECK(f.buf[0] == 0u);			/* HCC group 0 */
	CHECK(rd16(f.buf + 4) == 32u);		/* total */
	CHECK(rd16(f.buf + 6) == HAF_HCC_ID_CFG_EVENT);
	CHECK(rd16(f.buf + 10) == HAF_HCC_TAG);
	CHECK(rd16(f.buf + 20) == HAF_G0_KEY_CHAN);
	CHECK(rd16(f.buf + 22) == HAF_CHAN_BODY_LEN);
	CHECK(f.buf[24] == 0u);			/* band 2.4 GHz */
	CHECK(f.buf[25] == 0u);			/* vap_id */
	CHECK(f.buf[26] == 6u);			/* channel */
	CHECK(f.buf[27] == 0u);
	CHECK(f.buf[28] == 0xffu && f.buf[29] == 0xffu &&
	      f.buf[30] == 0xffu && f.buf[31] == 0xffu);	/* inherited pad */
}

/* WAL group-2 channel frame: total 32 + 4 = 36; cmd 0x0124 at +28. */
static void test_chan_wal(void)
{
	struct haf_ap_params p;
	struct haf_frame f;

	memset(&p, 0, sizeof(p));
	memset(&f, 0, sizeof(f));
	p.carrier = HAF_CARRIER_WAL_G2;
	p.band = 1;
	p.channel = 36;
	p.vap_id = 2;
	p.module = 1;
	p.dst = 0;
	CHECK(haf_build_chan_frame(&f, &p) == 0);
	CHECK(f.len == 36u);
	CHECK(f.buf[0] == HAF_HCC_GROUP_CORE);
	CHECK(rd16(f.buf + 4) == 36u);
	CHECK(rd32(f.buf + 16) == HAF_WAL_MSG_CFG);
	CHECK(rd16(f.buf + 22) == 4u + HAF_CHAN_BODY_LEN);
	CHECK(rd16(f.buf + 28) == HAF_WAL_CMD_CHAN);
	CHECK(rd16(f.buf + 30) == HAF_CHAN_BODY_LEN);
	CHECK(f.buf[32] == 1u);			/* band 5 GHz */
	CHECK(f.buf[33] == 2u);			/* vap_id */
	CHECK(f.buf[34] == 36u);		/* channel */
	CHECK(f.buf[35] == 0u);
}

static void test_chan_refusals(void)
{
	struct haf_ap_params p;
	struct haf_frame f;

	memset(&p, 0, sizeof(p));
	memset(&f, 0, sizeof(f));
	p.carrier = HAF_CARRIER_G0_ID1;
	p.channel = 0;
	CHECK(haf_build_chan_frame(&f, &p) == -EINVAL);
	p.channel = 6;
	p.band = 2;
	CHECK(haf_build_chan_frame(&f, &p) == -EINVAL);
	p.band = 0;
	p.module = 0x13;
	CHECK(haf_build_chan_frame(&f, &p) == -EINVAL);
	p.module = 0;
	p.vap_id = 0x10;
	CHECK(haf_build_chan_frame(&f, &p) == -EINVAL);
	p.vap_id = 0;
	p.carrier = 7;				/* unknown carrier */
	CHECK(haf_build_chan_frame(&f, &p) == -EINVAL);
	CHECK(haf_build_chan_frame(NULL, &p) == -EINVAL);
}

/* The legacy AP-start sequences must stay byte-identical: the channel frame is
 * NOT part of them (the kernel posts it explicitly when beacon_static=1). */
static void test_default_sequences_unchanged(void)
{
	enum haf_frame_id id;

	CHECK(haf_sequence_at(0, HAF_CARRIER_G0_ID1, &id) == 0 &&
	      id == HAF_FRAME_SSID);
	CHECK(haf_sequence_at(1, HAF_CARRIER_G0_ID1, &id) == 0 &&
	      id == HAF_FRAME_AP_CFG);
	CHECK(haf_sequence_at(2, HAF_CARRIER_G0_ID1, &id) == -ENODATA);
	CHECK(haf_sequence_at(0, HAF_CARRIER_WAL_G2, &id) == 0 &&
	      id == HAF_FRAME_SSID);
	CHECK(haf_sequence_at(1, HAF_CARRIER_WAL_G2, &id) == 0 &&
	      id == HAF_FRAME_AUTH);
	CHECK(haf_sequence_at(2, HAF_CARRIER_WAL_G2, &id) == 0 &&
	      id == HAF_FRAME_AP_CFG);
	CHECK(haf_sequence_at(3, HAF_CARRIER_WAL_G2, &id) == 0 &&
	      id == HAF_FRAME_VAP_START);
	CHECK(haf_sequence_at(4, HAF_CARRIER_WAL_G2, &id) == -ENODATA);
}

static void test_plan(void)
{
	u8 plan[HI5622_BEACON_STEP_MAX];
	unsigned int n;

	CHECK(hi5622_beacon_plan(0, 1, plan, sizeof(plan)) == 0u);
	n = hi5622_beacon_plan(1, 1, plan, sizeof(plan));
	CHECK(n == 5u);
	if (n == 5u) {
		CHECK(plan[0] == HI5622_BEACON_STEP_VAP_ADD);
		CHECK(plan[1] == HI5622_BEACON_STEP_CHAN);
		CHECK(plan[2] == HI5622_BEACON_STEP_SSID);
		CHECK(plan[3] == HI5622_BEACON_STEP_AP_CFG);
		CHECK(plan[4] == HI5622_BEACON_STEP_VAP_START);
		CHECK(!strcmp(hi5622_beacon_step_name(plan[1]), "ap_chan"));
		CHECK(!strcmp(hi5622_beacon_step_name(plan[4]),
			      "ap_vap_start"));
	}
	n = hi5622_beacon_plan(1, 0, plan, sizeof(plan));
	CHECK(n == 4u);
	if (n == 4u)
		CHECK(plan[3] == HI5622_BEACON_STEP_AP_CFG);
	/* max bound: never write past the caller buffer, truncate the tail. */
	memset(plan, 0xaa, sizeof(plan));
	n = hi5622_beacon_plan(1, 1, plan, 2u);
	CHECK(n == 2u);
	CHECK(plan[0] == HI5622_BEACON_STEP_VAP_ADD);
	CHECK(plan[1] == HI5622_BEACON_STEP_CHAN);
	CHECK(plan[2] == 0xaau);
	CHECK(hi5622_beacon_plan(1, 1, NULL, 4u) == 0u);
	CHECK(!strcmp(hi5622_beacon_step_name(0xee), "none"));
}

static int dummy_build(u8 *wire, size_t cap, size_t *out_len,
		       const struct hi5622_chan_cfg_params *p)
{
	(void)wire; (void)cap; (void)out_len; (void)p;
	return -EOPNOTSUPP;
}

static void test_ops_contract(void)
{
	struct hi5622_frame_ops ops;
	char buf[128];

	memset(&ops, 0, sizeof(ops));
	ops.name = "selftest";
	ops.caps = HI5622_FRAME_CAP_CHAN_CFG;
	CHECK(hi5622_frame_ops_validate(&ops) == -EINVAL);	/* no callback */
	ops.build_chan_cfg = dummy_build;
	CHECK(hi5622_frame_ops_validate(&ops) == 0);
	CHECK(!strcmp(hi5622_frame_cap_name(HI5622_FRAME_CAP_CHAN_CFG),
		      "chan_cfg"));
	CHECK(hi5622_frame_caps_format(HI5622_FRAME_CAP_CHAN_CFG, buf,
				       sizeof(buf)) > 0);
	CHECK(!strcmp(buf, "chan_cfg"));
	/* The new cap bit is inside the contract mask. */
	CHECK((HI5622_FRAME_CAPS_ALL & HI5622_FRAME_CAP_CHAN_CFG) != 0u);
}

static void test_ap_params_channel_bound(void)
{
	struct hi5622_ap_start_params p;

	memset(&p, 0, sizeof(p));
	p.ssid_len = 4;
	p.iftype = 3;
	p.center_freq = 2437;
	p.width = 0;
	p.beacon_interval = 100;
	p.dtim_period = 2;
	p.head_len = 36;
	p.channel = 6;
	CHECK(hi5622_ap_start_params_validate(&p) == 0);
	p.channel = 197;
	CHECK(hi5622_ap_start_params_validate(&p) == -EINVAL);
	p.channel = 0;
	CHECK(hi5622_ap_start_params_validate(&p) == 0);	/* legacy */
}

int main(void)
{
	test_chan_g0();
	test_chan_wal();
	test_chan_refusals();
	test_default_sequences_unchanged();
	test_plan();
	test_ops_contract();
	test_ap_params_channel_bound();
	printf("beacon_selftest: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
