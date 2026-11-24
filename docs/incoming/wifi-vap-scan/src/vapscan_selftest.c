/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vapscan_selftest.c - device-free known-answer + refusal suite for the
 * HCC user/VAP-add + scan-request frame builders, plus a frame emitter.
 *
 * ORIGINAL clean-room code (lane wifi-vap-scan, task-110, 2026-11-25).
 * Exits 0 only when every check passes.  Run "vapscan_selftest --emit" for the
 * bench-ready frames documented in ../recipes/frames.md.
 */
#include <stdio.h>
#include <string.h>

#include "vapscan_frames.h"

static int g_checks;
static int g_fail;

#define CHECK(cond) do {						\
	g_checks++;							\
	if (!(cond)) {							\
		g_fail++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);	\
	}								\
} while (0)

static const u8 k_mac[6] = { 0x02, 0x00, 0x68, 0x69, 0x35, 0x01 };
static const u8 k_bcast[6] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

static void hexdump(const char *tag, const u8 *p, size_t n)
{
	size_t i;

	printf("%s len=%zu\n  ", tag, n);
	for (i = 0; i < n; i++) {
		printf("%02x", p[i]);
		if ((i & 0x1fu) == 0x1fu && i + 1 != n)
			printf("\n  ");
	}
	printf("\n");
}

/* ---------------- known-answer: g0 id7 user add ------------------ */
static void test_user_add(void)
{
	struct vs_user_add p;
	struct vs_frame f;
	const u8 *pl;
	int rc;

	vs_user_add_defaults(&p);
	CHECK(p.type == 2);
	CHECK(p.rssi == 100);
	p.user_id = 0;
	memcpy(p.mac, k_mac, 6);
	rc = vs_build_user_add(&f, &p);
	CHECK(rc == VS_OK);
	CHECK(f.len == VS_HDR_LEN + VS_LEN_USER_ADD);
	CHECK(f.len == 180);
	/* header */
	CHECK(f.buf[0] == 0x00);				/* group 0 */
	CHECK(f.buf[1] == 0x01);				/* alloc, dst 0 */
	CHECK(f.buf[2] == 0x00);				/* vap id */
	CHECK(f.buf[3] == 0x00);				/* chip id */
	CHECK(f.buf[4] == 0xb4 && f.buf[5] == 0x00);		/* 180 */
	CHECK(f.buf[6] == 0x07 && f.buf[7] == 0x00);		/* id 7 */
	CHECK(f.buf[8] == 0x00);
	CHECK(f.buf[9] == 0x00);
	CHECK(f.buf[10] == 0x5a && f.buf[11] == 0x5a);
	/* payload */
	pl = f.buf + VS_HDR_LEN;
	CHECK(pl[VS_UA_OFF_UID] == 0x00 && pl[VS_UA_OFF_UID + 1] == 0x00);
	CHECK(memcmp(pl + VS_UA_OFF_MAC, k_mac, 6) == 0);
	CHECK(pl[VS_UA_OFF_TYPE] == 2);
	CHECK((u8)pl[VS_UA_OFF_RSSI] == 100);
	/* vendor writes nothing else; builder zero-fills [I] */
	{
		int i, nonzero = 0;

		for (i = 2 + 6; i < (int)VS_LEN_USER_ADD; i++) {
			if (i == VS_UA_OFF_TYPE || i == VS_UA_OFF_RSSI)
				continue;
			if (pl[i] != 0)
				nonzero++;
		}
		CHECK(nonzero == 0);
	}
	CHECK(vs_frame_validate(f.buf, f.len) == VS_OK);

	/* refusal cases */
	CHECK(vs_build_user_add(&f, NULL) == VS_EARG);
	p.user_id = 0x0fffu;
	CHECK(vs_build_user_add(&f, &p) == VS_OK);	/* builder allows del-only
							 * sentinel; validator
							 * refuses the wire form */
	CHECK(vs_frame_validate(f.buf, f.len) == VS_EPOLICY);
	p.user_id = 0x8000u;
	CHECK(vs_build_user_add(&f, &p) == VS_EARG);
	p.user_id = 0;
	p.dst = 0x10;
	CHECK(vs_build_user_add(&f, &p) == VS_EDST);
	p.dst = 0x03;
	CHECK(vs_build_user_add(&f, &p) == VS_OK);
	CHECK(f.buf[1] == 0x31 && f.buf[9] == 0x03);
	CHECK(vs_frame_validate(f.buf, f.len) == VS_OK);
	p.chip_id = 1;
	p.vap_id = 2;
	CHECK(vs_build_user_add(&f, &p) == VS_OK);
	CHECK(f.buf[3] == 1 && f.buf[2] == 2);
	CHECK(vs_frame_validate(f.buf, f.len) == VS_OK);
}

/* ---------------- known-answer: g0 id8 user alg ------------------ */
static void test_user_alg(void)
{
	struct vs_user_alg p;
	struct vs_frame f;
	u8 vht[VS_ALG_LEN_VHT], ht[VS_ALG_LEN_HT], he[VS_ALG_LEN_HE];
	const u8 *pl;
	int i, rc;

	for (i = 0; i < (int)sizeof(vht); i++)
		vht[i] = (u8)(0x10 + i);
	for (i = 0; i < (int)sizeof(ht); i++)
		ht[i] = (u8)(0x40 + i);
	for (i = 0; i < (int)sizeof(he); i++)
		he[i] = (u8)(0x80 + i);

	vs_user_alg_defaults(&p);
	CHECK(p.vht == NULL && p.ht == NULL && p.he == NULL);
	p.user_id = 1;
	p.alg = 0x1234;
	p.nss = 2;
	p.vht = vht;
	p.ht = ht;
	p.he = he;
	rc = vs_build_user_alg(&f, &p);
	CHECK(rc == VS_OK);
	CHECK(f.len == 180);
	CHECK(f.buf[6] == 0x08 && f.buf[7] == 0x00);		/* id 8 */
	pl = f.buf + VS_HDR_LEN;
	CHECK(pl[VS_ALG_OFF_UID] == 1 && pl[VS_ALG_OFF_UID + 1] == 0);
	CHECK(pl[VS_ALG_OFF_ALG] == 0x34 && pl[VS_ALG_OFF_ALG + 1] == 0x12);
	CHECK(memcmp(pl + VS_ALG_OFF_VHT, vht, sizeof(vht)) == 0);
	CHECK(memcmp(pl + VS_ALG_OFF_HT, ht, sizeof(ht)) == 0);
	CHECK(memcmp(pl + VS_ALG_OFF_HE, he, sizeof(he)) == 0);
	CHECK(pl[VS_ALG_OFF_NSS] == 2);
	CHECK(vs_frame_validate(f.buf, f.len) == VS_OK);
	/* zero-caps default form */
	vs_user_alg_defaults(&p);
	p.user_id = 0;
	CHECK(vs_build_user_alg(&f, &p) == VS_OK);
	CHECK(vs_frame_validate(f.buf, f.len) == VS_OK);
	CHECK(vs_build_user_alg(&f, NULL) == VS_EARG);
	p.user_id = 0x0fffu;
	CHECK(vs_build_user_alg(&f, &p) == VS_OK);
	CHECK(vs_frame_validate(f.buf, f.len) == VS_EPOLICY);
}

/* ---------------- known-answer: g0 id9 user del ------------------ */
static void test_user_del(void)
{
	struct vs_frame f;
	const u8 *pl;

	CHECK(vs_build_user_del(&f, 0x0fffu, k_mac, 0, 0, 0) == VS_OK);
	CHECK(f.len == 24);
	CHECK(f.buf[4] == 0x18 && f.buf[5] == 0x00);
	CHECK(f.buf[6] == 0x09 && f.buf[7] == 0x00);
	pl = f.buf + VS_HDR_LEN;
	CHECK(pl[0] == 0xff && pl[1] == 0x0f);	/* sentinel -> by MAC [V] */
	CHECK(memcmp(pl + 2, k_mac, 6) == 0);
	CHECK(pl[8] == 0x00);
	CHECK(vs_frame_validate(f.buf, f.len) == VS_OK);
	/* exact known-answer wire bytes for the by-MAC delete */
	{
		static const u8 want[24] = {
			0x00, 0x01, 0x00, 0x00, 0x18, 0x00, 0x09, 0x00,
			0x00, 0x00, 0x5a, 0x5a,
			0xff, 0x0f, 0x02, 0x00, 0x68, 0x69, 0x35, 0x01,
			0x00, 0x00, 0x00, 0x00
		};

		CHECK(memcmp(f.buf, want, sizeof(want)) == 0);
	}
	CHECK(vs_build_user_del(&f, 3, k_mac, 1, 2, 0x7f) == VS_OK);
	CHECK(f.buf[3] == 1 && f.buf[2] == 2);
	CHECK(f.buf[VS_HDR_LEN] == 3);
	CHECK(f.buf[VS_HDR_LEN + 8] == 0x7f);
	CHECK(vs_frame_validate(f.buf, f.len) == VS_OK);
	CHECK(vs_build_user_del(&f, 0x8000u, k_mac, 0, 0, 0) == VS_EARG);
	CHECK(vs_build_user_del(&f, 0, NULL, 0, 0, 0) == VS_EARG);
}

/* ---------------- known-answer: g0 id0xC scan req ---------------- */
static void test_scan(void)
{
	struct vs_scan_params p;
	struct vs_frame f;
	const u8 *pl;
	int rc;

	vs_scan_defaults(&p);
	CHECK(p.flags0 == 1);
	CHECK(p.has_bssid == 1);
	CHECK(p.dwell_ms == 60);
	CHECK(p.scan_type == 0);
	CHECK(memcmp(p.bssid, k_bcast, 6) == 0);
	rc = vs_build_scan_req(&f, &p);
	CHECK(rc == VS_OK);
	CHECK(f.len == VS_HDR_LEN + VS_LEN_SCAN_REQ);
	CHECK(f.len == 520);
	CHECK(f.buf[6] == 0x0c && f.buf[7] == 0x00);		/* id 12 */
	CHECK(f.buf[4] == 0x08 && f.buf[5] == 0x02);		/* 0x0208 = 520 */
	pl = f.buf + VS_HDR_LEN;
	CHECK(pl[VS_SC_OFF_FLAGS0] == 1);
	CHECK(pl[VS_SC_OFF_FLAGS1] == 0);
	CHECK(pl[VS_SC_OFF_HAS_BSSID] == 1);
	CHECK(pl[VS_SC_OFF_N_SSIDS] == 0);
	CHECK(pl[VS_SC_OFF_TYPE_B] == 2);
	CHECK(memcmp(pl + VS_SC_OFF_BSSID, k_bcast, 6) == 0);
	CHECK(pl[VS_SC_OFF_COUNT_B] == 0);
	CHECK(pl[VS_SC_OFF_CHAN_COUNT] == 0);
	CHECK(pl[VS_SC_OFF_DWELL] == 60 && pl[VS_SC_OFF_DWELL + 1] == 0);
	CHECK(pl[VS_SC_OFF_SCAN_TYPE] == 0);
	CHECK(pl[VS_SC_OFF_ONE] == 1);
	CHECK(pl[VS_SC_OFF_TYPE_1F2] == 11);
	CHECK(pl[VS_SC_OFF_TAIL32] == 0);
	CHECK(vs_frame_validate(f.buf, f.len) == VS_OK);

	/* with one SSID and 13 channels (2.4 GHz) */
	memset(&p, 0, sizeof(p));
	vs_scan_defaults(&p);
	p.n_ssids = 1;
	p.ssids[0].len = 5;
	memcpy(p.ssids[0].str, "CUDY!", 5);
	p.n_chans = 13;
	{
		int i;

		for (i = 0; i < 13; i++) {
			p.chans[i].num = (u8)(i + 1);
			p.chans[i].band = 0;
			p.chans[i].idx = (u8)i;	/* [I] provisional mapping */
		}
	}
	CHECK(vs_build_scan_req(&f, &p) == VS_OK);
	pl = f.buf + VS_HDR_LEN;
	CHECK(pl[VS_SC_OFF_N_SSIDS] == 1);
	CHECK(pl[VS_SC_OFF_COUNT_B] == 1);
	CHECK(memcmp(pl + VS_SC_OFF_SSID_AREA, "CUDY!", 5) == 0);
	CHECK(pl[VS_SC_OFF_SSID_AREA + 5] == 0);	/* vendor NUL write [V] */
	CHECK(pl[VS_SC_OFF_SSID_AREA + 0x24] == 0);	/* next slot unused */
	CHECK(pl[VS_SC_OFF_CHAN_COUNT] == 13);
	CHECK(pl[VS_SC_OFF_CHAN_AREA + 0] == 1);
	CHECK(pl[VS_SC_OFF_CHAN_AREA + 1] == 0);
	CHECK(pl[VS_SC_OFF_CHAN_AREA + 2] == 0);
	CHECK(pl[VS_SC_OFF_CHAN_AREA + 3] == 0);
	CHECK(pl[VS_SC_OFF_CHAN_AREA + 4 * 12 + 0] == 13);
	CHECK(pl[VS_SC_OFF_CHAN_AREA + 4 * 12 + 3] == 12);
	CHECK(vs_frame_validate(f.buf, f.len) == VS_OK);

	/* refusals */
	CHECK(vs_build_scan_req(&f, NULL) == VS_EARG);
	p.n_ssids = VS_SC_SSID_MAX + 1;
	CHECK(vs_build_scan_req(&f, &p) == VS_EBOUNDS);
	p.n_ssids = 0;
	p.n_chans = VS_SC_CHAN_MAX + 1;
	CHECK(vs_build_scan_req(&f, &p) == VS_EBOUNDS);
	p.n_chans = 0;
	p.scan_type = 4;
	CHECK(vs_build_scan_req(&f, &p) == VS_EPOLICY);
	p.scan_type = 8;
	CHECK(vs_build_scan_req(&f, &p) == VS_EPOLICY);
	p.scan_type = 0;
	p.ssids[0].len = VS_SC_SSID_LEN_MAX + 1;
	p.n_ssids = 1;
	CHECK(vs_build_scan_req(&f, &p) == VS_EBOUNDS);
	p.n_ssids = 0;
	p.n_chans = 1;
	p.chans[0].band = 2;
	CHECK(vs_build_scan_req(&f, &p) == VS_EBOUNDS);
}

/* ---------------- header validator / decode ---------------------- */
static void test_validator(void)
{
	struct vs_frame f;
	struct vs_user_add p;
	u8 fam = 0, chip = 0xaa, vap = 0xaa;
	u16 plen = 0;
	u8 tmp[VS_MAX_FRAME];

	vs_user_add_defaults(&p);
	memcpy(p.mac, k_mac, 6);
	CHECK(vs_build_user_add(&f, &p) == VS_OK);
	CHECK(vs_frame_decode(f.buf, f.len, &fam, &chip, &vap, &plen) == VS_OK);
	CHECK(fam == VS_ID_USER_ADD);
	CHECK(chip == 0 && vap == 0);
	CHECK(plen == VS_LEN_USER_ADD);
	CHECK(strcmp(vs_family_name(fam), "user_add") == 0);

	/* tag */
	memcpy(tmp, f.buf, f.len);
	tmp[10] = 0x00;
	CHECK(vs_frame_validate(tmp, f.len) == VS_ETAG);
	/* declared total mismatch */
	memcpy(tmp, f.buf, f.len);
	tmp[4] = 0x00;
	tmp[5] = 0x00;
	CHECK(vs_frame_validate(tmp, f.len) == VS_ELEN);
	/* shortened frame */
	CHECK(vs_frame_validate(f.buf, 11) == VS_ESHORT);
	CHECK(vs_frame_validate(NULL, 0) == VS_ESHORT);
	/* alloc nibble */
	memcpy(tmp, f.buf, f.len);
	tmp[1] = 0x00;
	CHECK(vs_frame_validate(tmp, f.len) == VS_EALLOC);
	/* dst field mismatch */
	memcpy(tmp, f.buf, f.len);
	tmp[9] = 0x01;
	CHECK(vs_frame_validate(tmp, f.len) == VS_EDST);
	/* wrong group for an H2D table (group 2 exists, 1/4 do not) */
	memcpy(tmp, f.buf, f.len);
	tmp[0] = 0x04;
	CHECK(vs_frame_validate(tmp, f.len) == VS_EGROUP);
	/* unknown id */
	memcpy(tmp, f.buf, f.len);
	tmp[6] = 0x33;
	CHECK(vs_frame_validate(tmp, f.len) == VS_EID);
	/* id/payload-size disagreement: claim id 9 on the 0xa8 payload */
	memcpy(tmp, f.buf, f.len);
	tmp[6] = 0x09;
	CHECK(vs_frame_validate(tmp, f.len) == VS_ELEN);
	/* decode refusals mirror the validator */
	CHECK(vs_frame_decode(NULL, 0, NULL, NULL, NULL, NULL) == VS_ESHORT);
	/* export */
	{
		size_t n = 0;

		CHECK(vs_frame_export(&f, tmp, sizeof(tmp), &n) == VS_OK);
		CHECK(n == f.len);
		CHECK(memcmp(tmp, f.buf, n) == 0);
		CHECK(vs_frame_export(&f, tmp, 4, &n) == VS_EBUFFER);
	}
}

/* ---------------- SR carrier arithmetic -------------------------- */
static void test_carrier(void)
{
	u8 node[VS_SR_NODE_LEN];
	u32 prod;

	CHECK(vs_sr_node_encode(node, 0x836a1000u, 180) == VS_OK);
	CHECK(node[0] == 0x00 && node[1] == 0x10 && node[2] == 0x6a &&
	      node[3] == 0x83);
	CHECK(node[4] == 180 && node[5] == 0x00);
	CHECK(node[6] == 0x2b && node[7] == 0x6d);
	CHECK(vs_sr_node_encode(node, 0, 0) == VS_EARG);
	prod = 0;
	CHECK(vs_sr_producer_next(prod, 32) == 1);
	prod = 31;
	CHECK(vs_sr_producer_next(prod, 32) == (VS_SR_IDX_PHASE | 0u));
	CHECK(vs_sr_producer_next(VS_SR_IDX_PHASE | 31u, 32) == 0);
	CHECK(vs_sr_producer_next(5, 0) == 5);
}

/* ---------------- bench frame emitter ---------------------------- */
static int g_raw;

static void emit_frame(const char *tag, const struct vs_frame *f)
{
	size_t i;

	if (g_raw) {
		printf("%s|%u|", tag, f->len);
		for (i = 0; i < f->len; i++)
			printf("%02x", f->buf[i]);
		printf("|%u|0x%08x|%s\n", f->len,
		       ((u32)f->len << 16) | VS_SR_NODE_FLAGS,
		       vs_strerror(vs_frame_validate(f->buf, f->len)));
		return;
	}
	hexdump(tag, f->buf, f->len);
	printf("  sr_node word1=((%u<<16)|0x6d2b)=0x%08x\n", f->len,
	       ((u32)f->len << 16) | VS_SR_NODE_FLAGS);
	printf("  validate=%s\n", vs_strerror(vs_frame_validate(f->buf, f->len)));
}

static void emit_all(void)
{
	struct vs_user_add ua;
	struct vs_user_alg alg;
	struct vs_scan_params sc;
	struct vs_frame f;

	vs_user_add_defaults(&ua);
	memcpy(ua.mac, k_mac, 6);
	ua.user_id = 0;
	ua.chip_id = 0;
	ua.vap_id = 0;
	if (vs_build_user_add(&f, &ua) == VS_OK)
		emit_frame("user_add (g0 id7, uid=0, wlan0 mac)", &f);

	vs_user_alg_defaults(&alg);
	alg.user_id = 0;
	alg.chip_id = 0;
	alg.vap_id = 0;
	if (vs_build_user_alg(&f, &alg) == VS_OK)
		emit_frame("user_add_notify_alg (g0 id8, uid=0, zero caps [I])",
			   &f);

	if (vs_build_user_del(&f, VS_UID_BY_MAC, k_mac, 0, 0, 0) == VS_OK)
		emit_frame("del_user (g0 id9, by-MAC)", &f);

	vs_scan_defaults(&sc);
	sc.chip_id = 0;
	sc.vap_id = 0;
	if (vs_build_scan_req(&f, &sc) == VS_OK)
		emit_frame("scan_req (g0 id0xC, whole-band, no SSID)", &f);

	vs_scan_defaults(&sc);
	sc.chip_id = 0;
	sc.vap_id = 0;
	sc.n_chans = 13;
	{
		int i;

		for (i = 0; i < 13; i++) {
			sc.chans[i].num = (u8)(i + 1);
			sc.chans[i].band = 0;
			sc.chans[i].idx = (u8)i;	/* [I] provisional */
		}
	}
	if (vs_build_scan_req(&f, &sc) == VS_OK)
		emit_frame("scan_req (g0 id0xC, 2.4GHz ch1-13, idx [I])", &f);
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "--emit") == 0) {
		emit_all();
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "--emit-raw") == 0) {
		g_raw = 1;
		emit_all();
		return 0;
	}
	test_user_add();
	test_user_alg();
	test_user_del();
	test_scan();
	test_validator();
	test_carrier();
	printf("%d/%d checks passed\n", g_checks - g_fail, g_checks);
	return g_fail == 0 ? 0 : 1;
}
