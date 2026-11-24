// SPDX-License-Identifier: GPL-2.0
/*
 * ap_frames_selftest.c - device-free known-answer + refusal tests for the
 * wifi-ap-start frame builders (task-121).  Every vector is SYNTHETIC: it is
 * built from the derived field tables in ../REPORT.md, never copied from a
 * vendor capture.  Run with --emit to print the bench frames in the stable
 * format consumed by ../tools/gen_frames_md.py.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "hi5622_ap_frames.h"

static unsigned int g_pass;
static unsigned int g_fail;

#define CHECK(cond, what) do {						\
	if (cond) {							\
		g_pass++;						\
	} else {							\
		g_fail++;						\
		printf("FAIL: %s (%s:%d)\n", what, __FILE__, __LINE__);	\
	}								\
} while (0)

#define CHECK_EQ(a, b, what) do {					\
	unsigned long _va = (unsigned long)(a);				\
	unsigned long _vb = (unsigned long)(b);				\
	if (_va == _vb) {						\
		g_pass++;						\
	} else {							\
		g_fail++;						\
		printf("FAIL: %s: got %lu expected %lu (%s:%d)\n",	\
		       what, _va, _vb, __FILE__, __LINE__);		\
	}								\
} while (0)

static u16 rd16(const u8 *p)
{
	return (u16)(p[0] | ((u16)p[1] << 8));
}

static u32 rd32(const u8 *p)
{
	return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) |
	       ((u32)p[3] << 24);
}

/* --- a synthetic hostapd-shaped 2.4 GHz AP ---------------------------- */
static const u8 test_ssid[] = "cudy-ap-test";
#define TEST_SSID_LEN 12u

static void make_params(struct haf_ap_params *p, u8 carrier, u8 band)
{
	const u8 rates[8] = { 0x82, 0x84, 0x8b, 0x96, 0x0c, 0x12, 0x18, 0x24 };
	const u8 rsn[22] = { 0x30, 20,		/* RSN element */
			     0x01, 0x00,		/* version */
			     0x00, 0x0f, 0xac, 0x04,	/* group CCMP */
			     0x01, 0x00, 0x00, 0x0f, 0xac, 0x04, /* pairwise */
			     0x01, 0x00, 0x00, 0x0f, 0xac, 0x02, /* AKM PSK */
			     0x00, 0x00 };
	const u8 htcap[28] = { 0x2d, 26, 0x1c, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	const u8 wmm[16] = { 0xdd, 14, 0x00, 0x50, 0xf2, 0x02, 0x01, 0x01,
			     0x00, 0x00, 0x03, 0xa4, 0x00, 0x00, 0x27, 0x27 };
	const u8 md[5] = { 54, 3, 0x34, 0x12, 0x03 };
	u8 *h, *t;

	memset(p, 0, sizeof(*p));
	p->carrier = carrier;
	p->dst = 0;
	p->module = 0;
	p->vap_id = 0;
	p->iftype = 2;			/* AP */
	p->band = band;
	p->hidden_ssid = 0;
	p->auth_algs = HAF_AUTH_OPEN;	/* hostapd WPA2-PSK default */
	p->start = 1;
	p->ssid_len = TEST_SSID_LEN;
	memcpy(p->ssid, test_ssid, TEST_SSID_LEN);
	p->beacon_interval = 100;
	p->dtim_period = 2;
	p->center_freq1 = 2437;
	p->width = 4;			/* opaque nl80211_chan_width */
	p->head_len = 36u + 2u + TEST_SSID_LEN + 2u + 8u + 2u + 1u;
	h = p->head;
	memset(h, 0, p->head_len);
	h[0] = 0x80;			/* beacon frame control */
	h[0x22] = 0x11;			/* capability: ESS | Privacy */
	/* SSID IE at head+36 */
	h[36] = HAF_IE_SSID;
	h[37] = TEST_SSID_LEN;
	memcpy(h + 38, test_ssid, TEST_SSID_LEN);
	/* supported rates IE */
	h[38 + TEST_SSID_LEN] = HAF_IE_RATES;
	h[39 + TEST_SSID_LEN] = 8;
	memcpy(h + 40 + TEST_SSID_LEN, rates, 8);
	/* DS parameter IE */
	h[48 + TEST_SSID_LEN] = 3;
	h[49 + TEST_SSID_LEN] = 1;
	h[50 + TEST_SSID_LEN] = 6;
	p->tail_len = (u16)(sizeof(rsn) + sizeof(htcap) + sizeof(wmm) +
			    sizeof(md));
	t = p->tail;
	memcpy(t, rsn, sizeof(rsn));
	t += sizeof(rsn);
	memcpy(t, htcap, sizeof(htcap));
	t += sizeof(htcap);
	memcpy(t, wmm, sizeof(wmm));
	t += sizeof(wmm);
	memcpy(t, md, sizeof(md));
}

static void test_params_and_body(void)
{
	struct haf_ap_params p;
	u8 body[HAF_AP_CFG_BODY_LEN];
	int rc;

	make_params(&p, HAF_CARRIER_G0_ID1, 0);
	CHECK_EQ(haf_ap_params_validate(&p), 0, "params validate (2.4 g0)");
	rc = haf_build_body_ap_cfg(body, sizeof(body), &p);
	CHECK_EQ(rc, 0, "ap cfg body build");
	CHECK_EQ(rd32(body + 0x00), 100, "body +0x00 beacon_interval");
	CHECK_EQ(rd32(body + 0x04), 2, "body +0x04 dtim_period");
	CHECK_EQ(body[0x08], 1, "body +0x08 rsn/wmm present (privacy cap)");
	CHECK_EQ(body[0x09], 0x30, "body +0x09 RSN element id");
	CHECK_EQ(body[0x0a], 20, "body +0x0a RSN element len");
	CHECK_EQ(body[0x49], 0xdd, "body +0x49 vendor element id");
	CHECK_EQ(body[0x4c], 0x50, "body +0x4c vendor OUI byte");
	CHECK_EQ(body[0x89], 0, "body +0x89 hidden_ssid off");
	CHECK_EQ(body[0x8a], (0x1c >> 5) & 1u, "body +0x8a HT GF bit");
	CHECK_EQ(body[0x8b], 0, "body +0x8b SGI20 cleared on band 0");
	CHECK_EQ(body[0x8e], 5, "body +0x8e mode 5 (HT)");
	CHECK_EQ(body[0x93], 1, "body +0x93 start flag");
	CHECK_EQ(rd16(body + 0x94), 0x1234, "body +0x94 MDID");
	CHECK_EQ(body[0x96], 1, "body +0x96 MD present");
	CHECK_EQ(body[0x97], 1, "body +0x97 MD bit0");
	CHECK_EQ(body[0x98], 1, "body +0x98 MD bit1");
	CHECK_EQ(body[0x8f] | body[0x90] | body[0x91] | body[0x92] |
		 body[0x99] | body[0x9a] | body[0x9b], 0,
		 "reserved body bytes zero");

	/* hidden SSID + no HT/VHT: legacy mode from rates, no IE copies */
	make_params(&p, HAF_CARRIER_WAL_G2, 0);
	p.hidden_ssid = 1;
	p.head[0x22] = 0x01;		/* privacy clear */
	p.tail_len = 0;
	p.start = 0;
	rc = haf_build_body_ap_cfg(body, sizeof(body), &p);
	CHECK_EQ(rc, 0, "ap cfg body build (open hidden)");
	CHECK_EQ(body[0x08], 0, "body +0x08 clear without privacy");
	CHECK_EQ(body[0x89], 1, "body +0x89 hidden flag");
	CHECK_EQ(body[0x8e], 2, "body +0x8e legacy 11g from 8 rates");
	CHECK_EQ(body[0x93], 0, "body +0x93 change_beacon flag");
	CHECK_EQ(body[0x09] | body[0x49], 0, "IE slots zero without privacy");

	/* 5 GHz hint copies the short-GI capability bits */
	make_params(&p, HAF_CARRIER_WAL_G2, 1);
	rc = haf_build_body_ap_cfg(body, sizeof(body), &p);
	CHECK_EQ(rc, 0, "ap cfg body build (5G hint)");
	CHECK_EQ(body[0x8b], (0x1c >> 6) & 1u, "body +0x8b SGI20 on 5G hint");

	/* HE/ext element switches the mode to 10 */
	make_params(&p, HAF_CARRIER_WAL_G2, 0);
	{
		u8 ext_ie[3] = { 255, 1, 0x24 };

		memcpy(p.tail + p.tail_len, ext_ie, sizeof(ext_ie));
		p.tail_len = (u16)(p.tail_len + sizeof(ext_ie));
	}
	rc = haf_build_body_ap_cfg(body, sizeof(body), &p);
	CHECK_EQ(rc, 0, "ap cfg body build (ext IE)");
	CHECK_EQ(body[0x8e], 10, "body +0x8e mode 10 with ext element");

	/* VHT capability switches the mode to 6 (band hint 1) */
	make_params(&p, HAF_CARRIER_WAL_G2, 1);
	{
		u8 vht[14] = { 191, 12, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

		memcpy(p.tail + p.tail_len, vht, sizeof(vht));
		p.tail_len = (u16)(p.tail_len + sizeof(vht));
	}
	rc = haf_build_body_ap_cfg(body, sizeof(body), &p);
	CHECK_EQ(rc, 0, "ap cfg body build (VHT)");
	CHECK_EQ(body[0x8e], 6, "body +0x8e mode 6 with VHT cap");
}

static void test_bodies(void)
{
	struct haf_ap_params p;
	u8 body[HAF_AP_CFG_BODY_LEN];
	int rc;

	make_params(&p, HAF_CARRIER_WAL_G2, 0);
	rc = haf_build_body_ssid(body, sizeof(body), &p);
	CHECK_EQ(rc, 0, "ssid body build");
	CHECK_EQ(body[0], TEST_SSID_LEN, "ssid body len byte");
	CHECK_EQ(body[1] | body[2], 0, "ssid body pad bytes zero");
	CHECK_EQ(memcmp(body + 3, test_ssid, TEST_SSID_LEN), 0,
		 "ssid body payload");
	rc = haf_build_body_auth(body, sizeof(body), &p);
	CHECK_EQ(rc, 0, "auth body build");
	CHECK_EQ(rd32(body), HAF_AUTH_OPEN, "auth body u32");
	p.auth_algs = HAF_AUTH_SAE;
	CHECK_EQ(haf_build_body_auth(body, sizeof(body), &p), 0,
		 "auth body SAE allowed");
	p.auth_algs = 2;	/* FT: not in the vendor allowlist */
	CHECK_EQ(haf_build_body_auth(body, sizeof(body), &p), -EOPNOTSUPP,
		 "auth body FT refused");
	rc = haf_build_body_vap_start(body, sizeof(body), &p);
	CHECK_EQ(rc, 0, "vap start body build");
	CHECK_EQ(body[0], 1, "vap start flag");
	CHECK_EQ(rd32(body + 8), 0, "vap start cookie default");
}

static void test_wire_g0(void)
{
	struct haf_ap_params p;
	struct haf_frame f;
	int rc;

	make_params(&p, HAF_CARRIER_G0_ID1, 0);
	rc = haf_build_frame(&f, HAF_FRAME_SSID, &p);
	CHECK_EQ(rc, 0, "g0 ssid frame build");
	CHECK_EQ(f.len, HAF_G0_OVERHEAD + HAF_SSID_BODY_LEN,
		 "g0 ssid frame total 64");
	CHECK_EQ(f.buf[0] & 0x0f, 0, "g0 ssid group 0");
	CHECK_EQ(f.buf[1] & 0x0f, HAF_ALLOC_NIBBLE, "g0 ssid alloc nibble");
	CHECK_EQ(rd16(f.buf + 4), f.len, "g0 ssid total field");
	CHECK_EQ(rd16(f.buf + 6), HAF_HCC_ID_CFG_EVENT, "g0 ssid id 1");
	CHECK_EQ(rd16(f.buf + 10), 0x5a5a, "g0 ssid tag");
	CHECK_EQ(f.buf[3], p.module, "g0 ssid hdr[3] = module");
	CHECK_EQ(rd16(f.buf + 20), HAF_G0_KEY_SSID, "g0 ssid key");
	CHECK_EQ(rd16(f.buf + 22), HAF_SSID_BODY_LEN, "g0 ssid body len");
	CHECK_EQ(f.buf[24], TEST_SSID_LEN, "g0 ssid body len byte at +24");
	CHECK_EQ(memcmp(f.buf + 27, test_ssid, TEST_SSID_LEN), 0,
		 "g0 ssid body at +27");
	CHECK_EQ(f.buf[f.len - 1], 0xff, "g0 ssid inherited pad");

	rc = haf_build_frame(&f, HAF_FRAME_AP_CFG, &p);
	CHECK_EQ(rc, 0, "g0 ap_cfg frame build");
	CHECK_EQ(f.len, HAF_G0_OVERHEAD + HAF_AP_CFG_BODY_LEN,
		 "g0 ap_cfg frame total 184");
	CHECK_EQ(rd16(f.buf + 4), f.len, "g0 ap_cfg total field");
	CHECK_EQ(rd16(f.buf + 20), HAF_G0_KEY_AP_CFG, "g0 ap_cfg key 0x127");
	CHECK_EQ(rd16(f.buf + 22), HAF_AP_CFG_BODY_LEN, "g0 ap_cfg body len");
	CHECK_EQ(rd32(f.buf + 24), 100, "g0 ap_cfg body beacon interval");
	CHECK_EQ(f.buf[24 + 0x89], 0, "g0 ap_cfg body hidden");
	CHECK_EQ(f.buf[24 + 0x93], 1, "g0 ap_cfg body start flag");
	CHECK_EQ(haf_validate_wire(f.buf, f.len), 0, "g0 frames validate");
}

static void test_wire_wal(void)
{
	struct haf_ap_params p;
	struct haf_frame f;
	int rc;

	make_params(&p, HAF_CARRIER_WAL_G2, 0);
	rc = haf_build_frame(&f, HAF_FRAME_SSID, &p);
	CHECK_EQ(rc, 0, "wal ssid frame build");
	CHECK_EQ(f.len, HAF_WAL_OVERHEAD + HAF_SSID_BODY_LEN,
		 "wal ssid frame total 68");
	CHECK_EQ(f.buf[0] & 0x0f, HAF_HCC_GROUP_CORE, "wal ssid group 2");
	CHECK_EQ(rd16(f.buf + 6), 0, "wal ssid hcc id 0");
	CHECK_EQ(rd16(f.buf + 22), HAF_SSID_BODY_LEN + 4, "wal ssid data_len");
	CHECK_EQ(rd32(f.buf + 16), HAF_WAL_MSG_CFG, "wal ssid wal id 1");
	CHECK_EQ(rd16(f.buf + 28), HAF_WAL_CMD_SSID, "wal ssid cmd 0x0005");
	CHECK_EQ(rd16(f.buf + 30), HAF_SSID_BODY_LEN, "wal ssid body_len");
	CHECK_EQ(haf_validate_wire(f.buf, f.len), 0, "wal ssid validates");

	rc = haf_build_frame(&f, HAF_FRAME_AUTH, &p);
	CHECK_EQ(rc, 0, "wal auth frame build");
	CHECK_EQ(f.len, HAF_WAL_OVERHEAD + HAF_AUTH_BODY_LEN,
		 "wal auth frame total 36");
	CHECK_EQ(rd16(f.buf + 28), HAF_WAL_CMD_AUTH, "wal auth cmd 0x006d");
	CHECK_EQ(rd32(f.buf + 32), HAF_AUTH_OPEN, "wal auth body");
	CHECK_EQ(haf_validate_wire(f.buf, f.len), 0, "wal auth validates");

	rc = haf_build_frame(&f, HAF_FRAME_AP_CFG, &p);
	CHECK_EQ(rc, 0, "wal ap_cfg frame build");
	CHECK_EQ(f.len, HAF_WAL_OVERHEAD + HAF_AP_CFG_BODY_LEN,
		 "wal ap_cfg frame total 188");
	CHECK_EQ(rd16(f.buf + 28), HAF_WAL_CMD_AP_CFG, "wal ap_cfg cmd 0x127");
	CHECK_EQ(rd16(f.buf + 30), HAF_AP_CFG_BODY_LEN, "wal ap_cfg body_len");
	CHECK_EQ(rd32(f.buf + 32), 100, "wal ap_cfg body beacon interval");
	CHECK_EQ(haf_validate_wire(f.buf, f.len), 0, "wal ap_cfg validates");

	rc = haf_build_frame(&f, HAF_FRAME_VAP_START, &p);
	CHECK_EQ(rc, 0, "wal vap_start frame build");
	CHECK_EQ(f.len, HAF_WAL_OVERHEAD + HAF_VAP_START_BODY_LEN,
		 "wal vap_start frame total 44");
	CHECK_EQ(rd16(f.buf + 28), HAF_WAL_CMD_VAP_START,
		 "wal vap_start cmd 0x0065");
	CHECK_EQ(f.buf[32], 1, "wal vap_start body flag");
	CHECK_EQ(haf_validate_wire(f.buf, f.len), 0, "wal vap_start validates");
}

static void test_sequence(void)
{
	struct haf_ap_params p;
	struct haf_frame frames[HAF_MAX_SEQ_FRAMES];
	unsigned int count = 0;
	enum haf_frame_id id;
	int rc;

	make_params(&p, HAF_CARRIER_G0_ID1, 0);
	rc = haf_build_sequence(frames, HAF_MAX_SEQ_FRAMES, &p, &count);
	CHECK_EQ(rc, 0, "g0 sequence build");
	CHECK_EQ(count, 2, "g0 sequence length 2 (no static auth key)");
	CHECK_EQ(frames[0].id, HAF_FRAME_SSID, "g0 seq[0] ssid");
	CHECK_EQ(frames[1].id, HAF_FRAME_AP_CFG, "g0 seq[1] ap_cfg");
	CHECK_EQ(haf_sequence_at(2, HAF_CARRIER_G0_ID1, &id), -ENODATA,
		 "g0 sequence end");
	CHECK_EQ(haf_validate_wire(frames[0].buf, frames[0].len), 0,
		 "g0 seq[0] wire");
	CHECK_EQ(haf_validate_wire(frames[1].buf, frames[1].len), 0,
		 "g0 seq[1] wire");

	make_params(&p, HAF_CARRIER_WAL_G2, 0);
	count = 0;
	rc = haf_build_sequence(frames, HAF_MAX_SEQ_FRAMES, &p, &count);
	CHECK_EQ(rc, 0, "wal sequence build");
	CHECK_EQ(count, 4, "wal sequence length 4");
	CHECK_EQ(frames[0].id, HAF_FRAME_SSID, "wal seq[0] ssid");
	CHECK_EQ(frames[1].id, HAF_FRAME_AUTH, "wal seq[1] auth");
	CHECK_EQ(frames[2].id, HAF_FRAME_AP_CFG, "wal seq[2] ap_cfg");
	CHECK_EQ(frames[3].id, HAF_FRAME_VAP_START, "wal seq[3] vap_start");
	CHECK_EQ(haf_sequence_at(4, HAF_CARRIER_WAL_G2, &id), -ENODATA,
		 "wal sequence end");
	{
		unsigned int i;
		int ok = 1;

		for (i = 0; i < count; i++)
			if (haf_validate_wire(frames[i].buf, frames[i].len))
				ok = 0;
		CHECK_EQ(ok, 1, "wal seq all wire-valid");
	}
}

static void test_refusals(void)
{
	struct haf_ap_params p, q;
	struct haf_frame f;
	u8 body[HAF_AP_CFG_BODY_LEN];
	int rc;

	make_params(&p, HAF_CARRIER_G0_ID1, 0);

	q = p;
	q.ssid_len = 0;
	CHECK_EQ(haf_ap_params_validate(&q), -EINVAL, "refuse empty SSID");
	q = p;
	q.ssid_len = 33;
	CHECK_EQ(haf_ap_params_validate(&q), -EINVAL, "refuse SSID 33");
	q = p;
	q.head_len = 35;
	CHECK_EQ(haf_ap_params_validate(&q), -EINVAL, "refuse head < 36");
	q = p;
	q.tail_len = HAF_MAX_TAIL + 1u;
	CHECK_EQ(haf_ap_params_validate(&q), -EINVAL, "refuse tail > max");
	q = p;
	q.auth_algs = 2;
	CHECK_EQ(haf_ap_params_validate(&q), -EINVAL, "refuse auth 2");
	q = p;
	q.carrier = 9;
	CHECK_EQ(haf_ap_params_validate(&q), -EINVAL, "refuse carrier");
	q = p;
	q.module = 0x13;
	CHECK_EQ(haf_ap_params_validate(&q), -EINVAL, "refuse module 0x13");
	q = p;
	q.beacon_interval = 0;
	CHECK_EQ(haf_ap_params_validate(&q), -EINVAL, "refuse beacon 0");

	q = p;
	q.tail_len = 0;
	CHECK_EQ(haf_build_body_ap_cfg(body, 4, &q), -ENOSPC,
		 "refuse short body buffer");
	CHECK_EQ(haf_build_body_ap_cfg(body, sizeof(body), &q), 0,
		 "accept head-only beacon");
	CHECK_EQ(haf_build_body_auth(body, 3, &p), -ENOSPC,
		 "refuse short auth buffer");
	CHECK_EQ(haf_build_body_ssid(body, 35, &p), -ENOSPC,
		 "refuse short ssid buffer");
	CHECK_EQ(haf_build_body_vap_start(body, 11, &p), -ENOSPC,
		 "refuse short vap-start buffer");
	CHECK_EQ(haf_build_frame(&f, HAF_FRAME_AUTH, &p), -EOPNOTSUPP,
		 "g0 carrier has no auth frame");
	CHECK_EQ(haf_build_frame(&f, HAF_FRAME_VAP_START, &p), -EOPNOTSUPP,
		 "g0 carrier has no vap-start frame");
	CHECK_EQ(haf_build_frame(NULL, HAF_FRAME_SSID, &p), -EINVAL,
		 "refuse NULL frame");

	/* RSN copy slot overflow: IE payload 63 (total 65) must be refused */
	q = p;
	memset(q.tail, 0, HAF_MAX_TAIL);
	q.tail[0] = HAF_IE_RSN;
	q.tail[1] = 63;
	q.tail_len = 65;
	CHECK_EQ(haf_build_body_ap_cfg(body, sizeof(body), &q),
		 -EOPNOTSUPP, "refuse RSN copy over 64-byte slot");

	/* MD IE with a non-3 length is refused, not guessed */
	q = p;
	memset(q.tail, 0, HAF_MAX_TAIL);
	q.tail[0] = HAF_IE_MD;
	q.tail[1] = 2;
	q.tail_len = 4;
	CHECK_EQ(haf_build_body_ap_cfg(body, sizeof(body), &q),
		 -EOPNOTSUPP, "refuse malformed MD IE");

	/* wire mutations: each must fail structural validation */
	rc = haf_build_frame(&f, HAF_FRAME_AP_CFG, &p);
	CHECK_EQ(rc, 0, "build frame for mutation tests");
	{
		u8 tmp[HAF_MAX_FRAME];

		memcpy(tmp, f.buf, f.len);
		tmp[10] = 0x00;
		CHECK(haf_validate_wire(tmp, f.len) != 0, "refuse bad tag");
		memcpy(tmp, f.buf, f.len);
		tmp[4]++;
		CHECK(haf_validate_wire(tmp, f.len) != 0, "refuse bad total");
		memcpy(tmp, f.buf, f.len);
		tmp[20] = 0x66;
		tmp[21] = 0x00;
		CHECK(haf_validate_wire(tmp, f.len) != 0, "refuse unknown key");
		memcpy(tmp, f.buf, f.len);
		tmp[0] = 0x02;
		CHECK(haf_validate_wire(tmp, f.len) != 0, "refuse wrong group");
		memcpy(tmp, f.buf, f.len);
		tmp[f.len - 1] = 0x00;
		CHECK(haf_validate_wire(tmp, f.len) != 0, "refuse bad pad");
	}
}

static void emit_frames(void)
{
	static const u8 carriers[2] = { HAF_CARRIER_G0_ID1,
					HAF_CARRIER_WAL_G2 };
	unsigned int c;

	printf("# wifi-ap-start synthetic bench frames (generated by"
	       " ap_frames_selftest --emit; do not edit)\n");
	for (c = 0; c < 2; c++) {
		struct haf_ap_params p;
		struct haf_frame frames[HAF_MAX_SEQ_FRAMES];
		unsigned int count = 0, i;
		int rc;

		make_params(&p, carriers[c], 0);
		p.ssid_len = TEST_SSID_LEN;
		rc = haf_build_sequence(frames, HAF_MAX_SEQ_FRAMES, &p,
					&count);
		if (rc) {
			printf("error: carrier %u: %d\n", carriers[c], rc);
			exit(1);
		}
		printf("\n## carrier %s\n", haf_carrier_name(carriers[c]));
		for (i = 0; i < count; i++) {
			size_t j;

			printf("\n### %s (%s) total %zu B\n",
			       haf_frame_name((enum haf_frame_id)frames[i].id),
			       haf_carrier_name(carriers[c]), frames[i].len);
			for (j = 0; j < frames[i].len; j++) {
				printf("%02x", frames[i].buf[j]);
				if ((j & 1u) == 1u)
					printf(j + 1u == frames[i].len
						       ? "\n" : " ");
			}
		}
	}
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "--emit") == 0) {
		emit_frames();
		return 0;
	}
	test_params_and_body();
	test_bodies();
	test_wire_g0();
	test_wire_wal();
	test_sequence();
	test_refusals();
	printf("ap_frames_selftest: %u passed, %u failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
