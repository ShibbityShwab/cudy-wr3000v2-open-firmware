/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sta_ops_selftest.c - host gate for the wifi-sta-ops lane (task-149).
 *
 * Compiles hi5622_sta_ops_logic.c for the host (-std=c99 -Wall -Wextra
 * -Werror) and pins every decoded wire fact with LITERAL offsets/values, so
 * a builder/parse regression cannot make the lane's claims lie.
 *
 * HOST-ONLY: no kernel build, no device, no vendor artifact needed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hi5622_sta_ops_logic.h"

static int failures;
static int checks;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

/* Build one 802.11 ToDS data frame (client -> AP) with an LLC/SNAP payload. */
static size_t mk_data_frame(u8 *f, const u8 *bssid, const u8 *client,
			    const u8 *da, u16 ethertype, const u8 *payload,
			    size_t payload_len, u16 fc_extra)
{
	size_t n = 0;
	u16 fc = (u16)(0x0108u | fc_extra);	/* data, ToDS, version 0 */

	f[n++] = (u8)(fc & 0xffu);
	f[n++] = (u8)((fc >> 8) & 0xffu);
	f[n++] = 0; f[n++] = 0;			/* duration */
	memcpy(f + n, bssid, 6); n += 6;	/* addr1 */
	memcpy(f + n, client, 6); n += 6;	/* addr2 */
	memcpy(f + n, da, 6); n += 6;		/* addr3 */
	f[n++] = 0; f[n++] = 0;			/* seq */
	f[n++] = 0xaa; f[n++] = 0xaa; f[n++] = 0x03;
	f[n++] = 0x00; f[n++] = 0x00; f[n++] = 0x00;
	f[n++] = (u8)(ethertype >> 8);
	f[n++] = (u8)(ethertype & 0xffu);
	memcpy(f + n, payload, payload_len);
	n += payload_len;
	return n;
}

/* EAPOL-Key body fixture (95 bytes after the 4-byte EAPOL header):
 * descriptor type @0, key-info BE @1..2, key length @3..4, and the key-data
 * length BE @93..94 (EAPOL offset 97..98). */
static void mk_eapol_key_body(u8 *b, u16 key_info, u16 key_datalen)
{
	memset(b, 0, 95);
	b[0] = 2;			/* RSN descriptor */
	b[1] = (u8)(key_info >> 8);
	b[2] = (u8)(key_info & 0xffu);
	b[3] = 0x10; b[4] = 0x00;	/* key length 16 */
	/* nonce/replay/IV/RSC/ID/MIC stay zeroed fixtures */
	b[93] = (u8)(key_datalen >> 8);
	b[94] = (u8)(key_datalen & 0xffu);
}

static void test_user_add(void)
{
	struct hsop_user_add_params p;
	u8 wire[HSOP_USER_ADD_WIRE_LEN];
	size_t len = 0;
	const u8 mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };
	int rc;

	memset(&p, 0, sizeof(p));
	p.band = 0;
	p.vap_id = 0;
	p.aid = 42;
	memcpy(p.mac, mac, 6);
	p.type = HSOP_USER_TYPE_AP;
	p.rssi = HSOP_DEFAULT_RSSI;
	rc = hsop_build_user_add(wire, sizeof(wire), &len, &p);
	CHECK(rc == 0);
	CHECK(len == 180u);
	/* HCC header (literal) */
	CHECK(wire[0] == 0x00);
	CHECK(wire[1] == 0x01);
	CHECK(wire[4] == 0xb4 && wire[5] == 0x00);	/* total 180 LE */
	CHECK(wire[6] == 0x07 && wire[7] == 0x00);	/* HCC id 7     */
	CHECK(wire[8] == 0x00);
	CHECK(wire[10] == 0x5a && wire[11] == 0x5a);	/* tag          */
	/* body (literal): uid at 12+0x00, mac at 12+0x02, type at 12+0x8e,
	 * rssi at 12+0xa4 (the vendor hmac_user_add write offsets). */
	CHECK(wire[12 + 0x00] == 42 && wire[12 + 0x01] == 0x00);
	CHECK(memcmp(wire + 12 + 0x02, mac, 6) == 0);
	CHECK(wire[12 + 0x8e] == 2);
	CHECK(wire[12 + 0xa4] == 100);
	CHECK(hsop_check_user_add(wire, len) == 0);
	/* refusals */
	p.aid = 0;
	CHECK(hsop_build_user_add(wire, sizeof(wire), &len, &p) != 0);
	p.aid = 42;
	p.type = 0;
	CHECK(hsop_build_user_add(wire, sizeof(wire), &len, &p) != 0);
	p.type = HSOP_USER_TYPE_AP;
	CHECK(hsop_build_user_add(wire, 10, &len, &p) != 0);
}

static void test_user_del(void)
{
	struct hsop_user_del_params p;
	u8 wire[HSOP_USER_DEL_WIRE_LEN];
	size_t len = 0;
	const u8 mac[6] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };

	memset(&p, 0, sizeof(p));
	p.uid = 0;	/* invalid */
	memcpy(p.mac, mac, 6);
	p.uid = 42;
	p.flag = HSOP_USER_DEL_FLAG_AP;
	CHECK(hsop_build_user_del(wire, sizeof(wire), &len, &p) == 0);
	CHECK(len == 24u);
	CHECK(wire[4] == 24 && wire[5] == 0);
	CHECK(wire[6] == 9);
	CHECK(wire[12 + 0x00] == 42);
	CHECK(memcmp(wire + 12 + 0x02, mac, 6) == 0);
	CHECK(wire[12 + 0x08] == 2);
	CHECK(hsop_check_user_del(wire, len) == 0);
}

static void test_asoc_syn(void)
{
	struct hsop_asoc_syn_params p;
	u8 wire[HSOP_ASOC_SYN_WIRE_LEN];
	size_t len = 0;

	memset(&p, 0, sizeof(p));
	p.aid = 42;
	p.state = HSOP_ASOC_STATE_ASSOC;
	CHECK(hsop_build_asoc_syn(wire, sizeof(wire), &len, &p) == 0);
	CHECK(len == 32u);
	CHECK(wire[4] == 32 && wire[5] == 0);
	CHECK(wire[6] == 1);			/* HCC id 1 (cfg event) */
	CHECK(wire[10] == 0x5a && wire[11] == 0x5a);
	CHECK(wire[16] == 0x01);		/* token[4] pattern     */
	CHECK(wire[20] == 0xd2 && wire[21] == 0x07);	/* cmd 0x07d2   */
	CHECK(wire[22] == 4 && wire[23] == 0);		/* body len 4   */
	CHECK(wire[24] == 42 && wire[25] == 0);		/* AID          */
	CHECK(wire[26] == 1);			/* asoc state 1 */
	CHECK(wire[27] == 0x00);
	CHECK(wire[28] == 0xff && wire[31] == 0xff);	/* inherited pad */
	CHECK(hsop_check_asoc_syn(wire, len) == 0);
	p.state = 0;
	CHECK(hsop_build_asoc_syn(wire, sizeof(wire), &len, &p) != 0);
}

static void test_kick(void)
{
	struct hsop_kick_params p;
	u8 body[HSOP_KICK_BODY_LEN];
	const u8 mac[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };

	memset(&p, 0, sizeof(p));
	memcpy(p.mac, mac, 6);
	p.reason = 3;
	p.subtype = 10;	/* disassociation */
	CHECK(hsop_build_kick_body(body, sizeof(body), &p) == 0);
	CHECK(body[8] == 0x00);			/* no deauth flag */
	CHECK(body[15] == 1);
	p.subtype = HSOP_KICK_SUBTYPE_DEAUTH;
	CHECK(hsop_build_kick_body(body, sizeof(body), &p) == 0);
	CHECK(body[8] == 0xc0);			/* vendor subtype flag */
	CHECK(memcmp(body, mac, 6) == 0);
	CHECK(body[6] == 3 && body[7] == 0);
}

static void test_sta_table(void)
{
	struct hsop_sta_table t;
	struct hsop_sta s;
	const u8 a[6] = { 0x02, 0, 0, 0, 0, 0x01 };
	const u8 b[6] = { 0x02, 0, 0, 0, 0, 0x02 };
	const u8 mcast[6] = { 0x01, 0, 0, 0, 0, 0x01 };
	u32 i;

	hsop_sta_table_init(&t);
	CHECK(hsop_sta_add(&t, mcast, 1, 0, 0) != 0);	/* multicast refused */
	CHECK(hsop_sta_add(&t, a, 0, 0, 0) != 0);	/* AID 0 refused */
	CHECK(hsop_sta_add(&t, a, 7, 0, 0) == 0);
	CHECK(hsop_sta_add(&t, a, 7, 0, 0) != 0);	/* duplicate */
	CHECK(hsop_sta_add(&t, b, 9, 0, 0) == 0);
	CHECK(hsop_sta_count(&t) == 2u);
	CHECK(hsop_sta_find(&t, a) != NULL);
	CHECK(hsop_sta_find(&t, b)->aid == 9);
	CHECK(hsop_sta_update(&t, a, 0x2u, 0x2u) == 0);	/* AUTHORIZED bit */
	CHECK((hsop_sta_find(&t, a)->flags_set & 0x2u) == 0x2u);
	CHECK(hsop_sta_update(&t, mcast, 0, 0) != 0);
	CHECK(hsop_sta_at(&t, 0u, &s) == 0 && memcmp(s.mac, a, 6) == 0);
	CHECK(hsop_sta_at(&t, 1u, &s) == 0 && memcmp(s.mac, b, 6) == 0);
	CHECK(hsop_sta_at(&t, 2u, &s) != 0);
	i = hsop_sta_count(&t);
	CHECK(hsop_sta_del(&t, a) == 0);
	CHECK(hsop_sta_count(&t) == i - 1u);
	CHECK(hsop_sta_del(&t, a) != 0);
	CHECK(t.stats.unknown_del == 1u);
}

static void test_rx_eapol(void)
{
	const u8 bssid[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0x01 };
	const u8 client[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
	const u8 da[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0x01 };
	u8 eapol[99];		/* 4 B EAPOL header + 95 B EAPOL-Key fixed */
	u8 frame[512];
	u8 eth[600];
	size_t flen, elen = 0;
	struct hsop_rx_info rx;

	memset(eapol, 0, sizeof(eapol));
	eapol[0] = 2;		/* EAPOL version */
	eapol[1] = 3;		/* EAPOL-Key     */
	eapol[2] = 0x00;
	eapol[3] = 95;		/* EAPOL body length (EAPOL-Key fixed) */
	mk_eapol_key_body(eapol + 4, 0x010a /* v2 + pairwise + MIC */, 0);
	/* msg2-like frame: key-data-length 0 and pairwise -> vendor 4/4-like */
	flen = mk_data_frame(frame, bssid, client, da, 0x888e, eapol,
			     sizeof(eapol), 0);
	CHECK(hsop_rx_parse(frame, (u32)flen, &rx) == 0);
	CHECK(rx.is_data == 1);
	CHECK(rx.tods == 1 && rx.fromds == 0);
	CHECK(rx.hdr_len == 24u);
	CHECK(rx.has_snap == 1);
	CHECK(rx.ethertype == 0x888e);
	CHECK(rx.is_eapol == 1);
	CHECK(rx.eapol_type == 3);
	CHECK(rx.eapol_key == 1);
	CHECK(rx.eapol_key_pairwise == 1);
	CHECK(rx.eapol_key_datalen == 0);
	CHECK(rx.eapol_ptk_done_like == 1);
	CHECK(memcmp(rx.src, client, 6) == 0);
	CHECK(memcmp(rx.dst, da, 6) == 0);
	CHECK(hsop_rx_deliverable(&rx, 0) == 1);	/* EAPOL pre-auth */
	/* Ethernet conversion (literal layout) */
	CHECK(hsop_rx_to_eth(frame, (u32)flen, eth, sizeof(eth), &elen,
			     &rx) == 0);
	CHECK(elen == 14u + sizeof(eapol));
	CHECK(memcmp(eth, da, 6) == 0);		/* dst = addr3  */
	CHECK(memcmp(eth + 6, client, 6) == 0);	/* src = addr2  */
	CHECK(eth[12] == 0x88 && eth[13] == 0x8e);
	CHECK(eth[14] == 2 && eth[15] == 3);	/* EAPOL hdr preserved */
	/* msg1-like (ACK, no MIC) still pairwise */
	{
		mk_eapol_key_body(eapol + 4, 0x008a, 0);
		flen = mk_data_frame(frame, bssid, client, da, 0x888e, eapol,
				     sizeof(eapol), 0);
		CHECK(hsop_rx_parse(frame, (u32)flen, &rx) == 0);
		CHECK(rx.eapol_key_pairwise == 1);
	}
	/* non-EAPOL data: delivered only when the station is authorized */
	{
		const u8 ip[20] = { 0x45, 0, 0, 20, 0, 0, 0, 0, 64, 1 };
		flen = mk_data_frame(frame, bssid, client, da, 0x0800, ip,
				     sizeof(ip), 0);
		CHECK(hsop_rx_parse(frame, (u32)flen, &rx) == 0);
		CHECK(rx.is_eapol == 0);
		CHECK(hsop_rx_deliverable(&rx, 0) == 0);
		CHECK(hsop_rx_deliverable(&rx, 1) == 1);
	}
	/* QoS data + HT control: 30-byte header (24 + 2 + 4) */
	{
		u8 qf[512];
		size_t qn = 0;

		/* reuse mk_data_frame then insert QoS-control + HT-control */
		flen = mk_data_frame(frame, bssid, client, da, 0x888e, eapol,
				     sizeof(eapol), 0);
		frame[0] = 0x88u;	/* data, QoS subtype 8          */
		frame[1] = 0x81u;	/* ToDS + order (HT control)    */
		memcpy(qf, frame, 24);
		qn = 24;
		qf[qn++] = 0; qf[qn++] = 0;	/* QoS control */
		qf[qn++] = 0; qf[qn++] = 0; qf[qn++] = 0; qf[qn++] = 0;
		memcpy(qf + qn, frame + 24, flen - 24);
		qn += flen - 24;
		CHECK(hsop_rx_parse(qf, (u32)qn, &rx) == 0);
		CHECK(rx.hdr_len == 30u);
		CHECK(rx.is_eapol == 1);
		CHECK(hsop_rx_to_eth(qf, (u32)qn, eth, sizeof(eth), &elen,
				     &rx) == 0);
		CHECK(elen == 14u + sizeof(eapol));
	}
	/* malformed refusals */
	CHECK(hsop_rx_parse(frame, 10, &rx) != 0);
	{
		u8 bad[64];

		memset(bad, 0, sizeof(bad));
		bad[0] = 0x08; bad[1] = 0x01;	/* data ToDS */
		bad[24] = 0x11;			/* no LLC/SNAP */
		CHECK(hsop_rx_parse(bad, sizeof(bad), &rx) == 0);
		CHECK(rx.is_data == 1 && rx.has_snap == 0);
		CHECK(hsop_rx_to_eth(bad, sizeof(bad), eth, sizeof(eth), &elen,
				     &rx) != 0);
	}
}

static void test_literals(void)
{
	/* Pin the decoded numbers so a header edit cannot silently drift. */
	CHECK(HSOP_USER_ADD_WIRE_LEN == 180u);
	CHECK(HSOP_USER_ADD_BODY_LEN == 168u);
	CHECK(HSOP_USER_DEL_WIRE_LEN == 24u);
	CHECK(HSOP_ASOC_SYN_WIRE_LEN == 32u);
	CHECK(HSOP_CFG_CMD_ASOC_SYN == 0x07d2u);
	CHECK(HSOP_CFG_OVERHEAD == 28u);
	CHECK(HSOP_ID_USER_ADD == 7u);
	CHECK(HSOP_ID_USER_DEL == 9u);
	CHECK(HSOP_ID_CFG_EVENT == 1u);
	CHECK(HSOP_WAL_KICK_CMD == 0x00d0u);
	CHECK(HSOP_ASOC_STATE_ASSOC == 1u);
	CHECK(HSOP_USER_TYPE_AP == 2u);
	CHECK(HSOP_DEFAULT_RSSI == 100);
}

int main(void)
{
	test_literals();
	test_user_add();
	test_user_del();
	test_asoc_syn();
	test_kick();
	test_sta_table();
	test_rx_eapol();
	if (failures) {
		fprintf(stderr, "sta_ops_selftest: %d/%d CHECKS FAILED\n",
			failures, checks);
		return 1;
	}
	printf("sta_ops_selftest: OK (%d checks)\n", checks);
	return 0;
}
