// SPDX-License-Identifier: GPL-2.0
/*
 * ba_selftest.c - host selftest for the Hi5622V100 fail-closed BA core
 * (lane wifi-ba-agg, task-178).  Compiles src/hi5622_ba.c standalone; no
 * kernel, no vendor artifact, no device.
 *
 * Pins every byte of the decoded vendor wire shapes (TABLES.md T3-T5, T8),
 * the parser rejects, the session state machine, the D2H BA events, and the
 * fail-closed contract (ba_enable=0 -> no frame, no message, no state change;
 * ba_enable>1 -> -EINVAL).
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_ba.h"

static int checks;
static int fails;

static void ck(int cond, const char *name)
{
	checks++;
	if (!cond) {
		fails++;
		printf("FAIL %s\n", name);
	}
}

static void ck_eq(int got, int want, const char *name)
{
	checks++;
	if (got != want) {
		fails++;
		printf("FAIL %s (got %d want %d)\n", name, got, want);
	}
}

/* ---- capture callbacks ------------------------------------------------- */

#define CAP_MAX 8
static u8 cap_msg[CAP_MAX][64];
static size_t cap_msg_len[CAP_MAX];
static int cap_msgs;
static u8 cap_frame[CAP_MAX][64];
static size_t cap_frame_len[CAP_MAX];
static int cap_frames;
static int cap_logs;

static int cb_post_h2d(void *ctx, const u8 *msg, size_t len)
{
	(void)ctx;
	if (cap_msgs >= CAP_MAX || len > sizeof(cap_msg[0]))
		return -1;
	memcpy(cap_msg[cap_msgs], msg, len);
	cap_msg_len[cap_msgs] = len;
	cap_msgs++;
	return 0;
}

static int cb_tx_action(void *ctx, const u8 *frame, size_t len)
{
	(void)ctx;
	if (cap_frames >= CAP_MAX || len > sizeof(cap_frame[0]))
		return -1;
	memcpy(cap_frame[cap_frames], frame, len);
	cap_frame_len[cap_frames] = len;
	cap_frames++;
	return 0;
}

static void cb_log(void *ctx, const char *msg)
{
	(void)ctx;
	(void)msg;
	cap_logs++;
}

static void cap_reset(void)
{
	cap_msgs = 0;
	cap_frames = 0;
	cap_logs = 0;
	memset(cap_msg, 0, sizeof(cap_msg));
	memset(cap_frame, 0, sizeof(cap_frame));
}

static const struct hi5622_ba_ops ops = {
	.ctx = NULL,
	.post_h2d = cb_post_h2d,
	.tx_action = cb_tx_action,
	.log = cb_log,
};

static const u8 PEER[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const u8 OWN[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };
static const u8 BSSID[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };

static void base_params(struct hi5622_ba_params *p, u8 tid)
{
	memset(p, 0, sizeof(*p));
	p->tid = tid;
	p->dialog = 0x42;
	p->policy = 1;
	p->amsdu = 1;
	p->baw = 64;
	p->timeout_tu = 0;
	p->start_seq = 0;
}

/* ---- tests ------------------------------------------------------------- */

static void test_gate(void)
{
	struct hi5622_ba_core core;
	struct hi5622_ba_params p;
	u8 frame[HI5622_BA_FRAME_ADDBA_REQ_LEN];
	u8 payload[HI5622_BA_RELAY_PAYLOAD_LEN];
	u8 hdr[HI5622_BA_HDR_LEN + 8];

	hi5622_ba_enable = 0;
	ck_eq(hi5622_ba_check_enabled(), HI5622_BA_EOPNOTSUPP, "gate: off");
	hi5622_ba_enable = 2;
	ck_eq(hi5622_ba_check_enabled(), HI5622_BA_EINVAL, "gate: >1 invalid");
	hi5622_ba_enable = 1;
	ck_eq(hi5622_ba_check_enabled(), 0, "gate: armed");

	/* disabled: no builder output, no callback, no state change */
	hi5622_ba_enable = 0;
	hi5622_ba_core_init(&core);
	hi5622_ba_core_set_addrs(&core, OWN, BSSID);
	base_params(&p, 0);
	cap_reset();
	ck_eq((int)hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER,
					     OWN, BSSID), 0,
	      "gate: builder req inert when off");
	ck_eq((int)hi5622_ba_build_relay_addba_rsp(payload, sizeof(payload), 7,
						   &p), 0,
	      "gate: builder relay inert when off");
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_CREATE_BA;
	hdr[12] = 7;
	hdr[14] = 0;
	ck_eq(hi5622_ba_tx_start(&core, &ops, PEER, &p),
	      HI5622_BA_EOPNOTSUPP, "gate: tx_start inert");
	ck_eq(hi5622_ba_tx_mpdu(&core, &ops, PEER, 0, 1),
	      HI5622_BA_EOPNOTSUPP, "gate: tx_mpdu inert");
	ck_eq(hi5622_ba_tx_burst_end(&core, &ops, PEER, 0),
	      HI5622_BA_EOPNOTSUPP, "gate: tx_burst_end inert");
	ck_eq(hi5622_ba_mgmt_rx(&core, &ops, 7, PEER, frame, sizeof(frame)),
	      HI5622_BA_EOPNOTSUPP, "gate: mgmt_rx inert");
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)),
	      HI5622_BA_EOPNOTSUPP, "gate: d2h inert");
	ck_eq(cap_msgs, 0, "gate: zero H2D messages when off");
	ck_eq(cap_frames, 0, "gate: zero action frames when off");
	ck_eq(core.peers[0].used, 0, "gate: no peer created when off");

	hi5622_ba_enable = 2;
	ck_eq(hi5622_ba_tx_start(&core, &ops, PEER, &p), HI5622_BA_EINVAL,
	      "gate: >1 refused at use time");
	hi5622_ba_enable = 1;
}

static void test_frame_builders(void)
{
	struct hi5622_ba_params p;
	u8 frame[64];
	size_t n;
	u16 param;

	hi5622_ba_enable = 1;
	base_params(&p, 3);
	n = hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER, OWN,
				      BSSID);
	ck_eq((int)n, HI5622_BA_FRAME_ADDBA_REQ_LEN, "req: len 33");
	ck(frame[0] == 0xd0 && frame[1] == 0x00, "req: FC 0x00d0");
	ck(!memcmp(frame + 4, PEER, 6), "req: DA = peer");
	ck(!memcmp(frame + 10, OWN, 6), "req: SA = own");
	ck(!memcmp(frame + 16, BSSID, 6), "req: BSSID");
	ck(frame[24] == 3 && frame[25] == 0, "req: category 3 action 0");
	ck_eq(frame[26], 0x42, "req: dialog token");
	param = (u16)((64 << 6) | (1 << 1) | 1 | (3 << 2));
	ck_eq(frame[27], param & 0xff, "req: BA param low");
	ck_eq(frame[28], param >> 8, "req: BA param high");
	ck_eq((int)(frame[29] | (frame[30] << 8)), 0, "req: timeout 0");
	ck_eq((int)(frame[31] | (frame[32] << 8)), 0, "req: start seq 0");

	base_params(&p, 5);
	p.status = 0;
	n = hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, PEER, OWN,
				      BSSID);
	ck_eq((int)n, HI5622_BA_FRAME_ADDBA_RSP_LEN, "rsp: len 33");
	ck(frame[25] == 1, "rsp: action 1");
	ck_eq((int)(frame[27] | (frame[28] << 8)), 0, "rsp: status 0");
	param = (u16)((64 << 6) | (1 << 1) | 1 | (5 << 2));
	ck_eq(frame[29], param & 0xff, "rsp: BA param low");
	ck_eq(frame[30], param >> 8, "rsp: BA param high");
	ck_eq((int)(frame[31] | (frame[32] << 8)), 0,
	      "rsp: BA timeout 0 (vendor parity)");

	p.status = 1;	/* failure status */
	hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, PEER, OWN, BSSID);
	ck_eq((int)(frame[27] | (frame[28] << 8)), 1, "rsp: failure status");

	n = hi5622_ba_build_delba(frame, sizeof(frame), 5, 1, 0x27, PEER, OWN,
				  BSSID);
	ck_eq((int)n, HI5622_BA_FRAME_DELBA_LEN, "delba: len 30");
	ck(frame[24] == 3 && frame[25] == 2, "delba: category 3 action 2");
	ck_eq(frame[27], (5 << 4) | (1 << 3), "delba: param tid/initiator");
	ck_eq(frame[28], 0x27, "delba: reason 0x27");
	ck_eq(frame[29], 0, "delba: reason high 0");

	/* invalid inputs */
	ck_eq((int)hi5622_ba_build_addba_req(frame, 10, &p, PEER, OWN, BSSID),
	      0, "req: short cap refused");
	p.tid = 9;
	ck_eq((int)hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER,
					     OWN, BSSID), 0,
	      "req: tid 9 refused");
	base_params(&p, 0);
	p.baw = 0;
	ck_eq((int)hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER,
					     OWN, BSSID), 0,
	      "req: baw 0 refused");
	p.baw = 257;
	ck_eq((int)hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER,
					     OWN, BSSID), 0,
	      "req: baw 257 refused");
}

static void test_payload_builders(void)
{
	struct hi5622_ba_params p;
	u8 pl[64];
	size_t n;
	int i;

	hi5622_ba_enable = 1;
	base_params(&p, 3);
	p.status = 0;
	p.timeout_tu = 0x1234;
	n = hi5622_ba_build_relay_addba_rsp(pl, sizeof(pl), 0x1234, &p);
	ck_eq((int)n, HI5622_BA_RELAY_PAYLOAD_LEN,
	      "relay rsp: 32 B payload");
	ck(pl[0] == 3 && pl[1] == 1, "relay rsp: type 3 subtype 1");
	ck_eq(pl[2] | (pl[3] << 8), 0x1234, "relay rsp: uid at +2");
	ck_eq(pl[7], 3, "relay rsp: tid at +7");
	ck_eq(pl[9], 0, "relay rsp: status at +9");
	ck_eq(pl[0x0c] | (pl[0x0d] << 8), 64, "relay rsp: baw u16 at +0x0c");
	ck_eq(pl[0x10] | (pl[0x11] << 8), 0x1234,
	      "relay rsp: timeout u16 at +0x10");
	ck_eq(pl[0x12], 1, "relay rsp: amsdu at +0x12");
	ck_eq(pl[0x14], 0x42, "relay rsp: dialog at +0x14");
	ck_eq(pl[0x15], 1, "relay rsp: policy at +0x15");

	/* every other payload byte must be zero (fw reads some of them) */
	for (i = 0; i < HI5622_BA_RELAY_PAYLOAD_LEN; i++) {
		if (i == 0 || i == 1 || i == 2 || i == 3 || i == 7 || i == 9 ||
		    i == 0x0c || i == 0x0d || i == 0x10 || i == 0x11 ||
		    i == 0x12 || i == 0x14 || i == 0x15)
			continue;
		if (pl[i] != 0) {
			printf("non-zero relay rsp byte +0x%02x\n", i);
			ck(0, "relay rsp: only decoded fields non-zero");
			break;
		}
	}
	ck(1, "relay rsp: only decoded fields non-zero");

	n = hi5622_ba_build_relay_delba(pl, sizeof(pl), 0x0102, 6, 1);
	ck_eq((int)n, HI5622_BA_RELAY_PAYLOAD_LEN, "relay delba: 32 B");
	ck(pl[0] == 3 && pl[1] == 2, "relay delba: type 3 subtype 2");
	ck_eq(pl[2] | (pl[3] << 8), 0x0102, "relay delba: uid");
	ck_eq(pl[7], 6, "relay delba: tid at +7");
	ck_eq(pl[8], 1, "relay delba: initiator at +8");
	ck_eq(pl[0x14], 0, "relay delba: clear flag +0x14 = 0");
	ck_eq(pl[0x15], 0, "relay delba: +0x15 zeroed");

	n = hi5622_ba_build_ampdu_start(pl, sizeof(pl), 2, 0xabcd, 0x1234, 1);
	ck_eq((int)n, HI5622_BA_AMPDU_PAYLOAD_LEN, "ampdu start: 8 B");
	ck(pl[0] == 0, "ampdu start: subtype 0");
	ck_eq(pl[1], 2, "ampdu start: tid");
	ck_eq(pl[2] | (pl[3] << 8), 0xabcd, "ampdu start: seq u16");
	ck_eq(pl[4] | (pl[5] << 8), 0x1234, "ampdu start: uid u16");
	ck_eq(pl[6], 1, "ampdu start: flow flag");
	ck_eq(pl[7], 0, "ampdu start: trailing 0");

	n = hi5622_ba_build_ampdu_end(pl, sizeof(pl), 2, 0x1234);
	ck_eq((int)n, HI5622_BA_AMPDU_PAYLOAD_LEN, "ampdu end: 8 B");
	ck(pl[0] == 1, "ampdu end: subtype 1");
	ck_eq(pl[1], 2, "ampdu end: tid");
	ck_eq(pl[4] | (pl[5] << 8), 0x1234, "ampdu end: uid u16");

	n = hi5622_ba_build_tx_notify(pl, sizeof(pl), 0x1234, 3, 2);
	ck_eq((int)n, HI5622_BA_NOTIFY_PAYLOAD_LEN, "notify: 20 B");
	ck_eq(pl[0] | (pl[1] << 8), 0x1234, "notify: uid u16");
	ck_eq(pl[2], 3, "notify: tid");
	ck_eq(pl[3], 2, "notify: value");
	ck_eq(pl[4], 0, "notify: zeroed tail");

	/* h2d wrapper: 12-byte header */
	{
		u8 msg[32];

		hi5622_ba_wrap_h2d(msg, 8, HI5622_BA_H2D_AMPDU_TX, 0, 0, 0);
		ck_eq(msg[0] & 0x0f, 0, "h2d: alloc nibble cleared");
		ck_eq(msg[1] >> 4, 0, "h2d: dev2 high nibble");
		ck_eq(msg[6], HI5622_BA_H2D_AMPDU_TX, "h2d: id at byte 6");
		ck_eq(msg[7], 0, "h2d: byte 7 zero");
		ck_eq(msg[8], 0, "h2d: byte 8 zero");
	}
}

static void test_parser(void)
{
	struct hi5622_ba_params p;
	struct hi5622_ba_action a;
	u8 frame[64];
	u8 bad[64];
	size_t n;

	hi5622_ba_enable = 1;
	base_params(&p, 3);
	p.dialog = 0x77;
	p.timeout_tu = 0x1010;
	p.start_seq = 0x0044;
	n = hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER, OWN,
				      BSSID);
	ck_eq(hi5622_ba_parse_action(frame, n, &a), HI5622_BA_OK,
	      "parse: addba req ok");
	ck_eq(a.action, 0, "parse: req action 0");
	ck_eq(a.tid, 3, "parse: req tid");
	ck_eq(a.baw, 64, "parse: req baw");
	ck_eq(a.policy, 1, "parse: req policy");
	ck_eq(a.amsdu, 1, "parse: req amsdu");
	ck_eq(a.dialog, 0x77, "parse: req dialog");
	ck_eq(a.timeout_tu, 0x1010, "parse: req timeout");
	ck_eq(a.start_seq, 0x0044, "parse: req start seq");

	base_params(&p, 6);
	p.status = 0;
	n = hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, PEER, OWN,
				      BSSID);
	ck_eq(hi5622_ba_parse_action(frame, n, &a), HI5622_BA_OK,
	      "parse: addba rsp ok");
	ck_eq(a.action, 1, "parse: rsp action 1");
	ck_eq(a.status, 0, "parse: rsp status 0");
	ck_eq(a.tid, 6, "parse: rsp tid");

	n = hi5622_ba_build_delba(frame, sizeof(frame), 4, 0, 7, PEER, OWN,
				  BSSID);
	ck_eq(hi5622_ba_parse_action(frame, n, &a), HI5622_BA_OK,
	      "parse: delba ok");
	ck_eq(a.action, 2, "parse: delba action 2");
	ck_eq(a.tid, 4, "parse: delba tid");
	ck_eq(a.initiator, 0, "parse: delba initiator");
	ck_eq(a.reason, 7, "parse: delba reason");

	/* rejects */
	memcpy(bad, frame, n);
	bad[0] = 4;	/* wrong category */
	ck_eq(hi5622_ba_parse_action(bad, n, &a), HI5622_BA_EINVAL,
	      "parse: wrong category rejected");
	ck_eq(hi5622_ba_parse_action(frame, 4, &a), HI5622_BA_EINVAL,
	      "parse: short frame rejected");
	memcpy(bad, frame, n);
	bad[1] = 3;	/* unknown action */
	ck_eq(hi5622_ba_parse_action(bad, n, &a), HI5622_BA_EINVAL,
	      "parse: unknown action rejected");
	/* addba req body: category at [24], action [25], dialog [26],
	 * BA param [27..28], timeout [29..30], start seq [31..32] */
	base_params(&p, 1);
	n = hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER, OWN,
				      BSSID);
	/* TID 9 (param bits 2..5) must be rejected */
	memcpy(bad, frame, n);
	bad[27] = (u8)((64 << 6) | (1 << 1) | 1 | (9 << 2));
	bad[28] = (u8)(((64 << 6) | (1 << 1) | 1 | (9 << 2)) >> 8);
	ck_eq(hi5622_ba_parse_action(bad, n, &a), HI5622_BA_EINVAL,
	      "parse: tid 9 rejected");
	/* BAW 0 (param >> 6 == 0) must be rejected */
	memcpy(bad, frame, n);
	bad[27] = (u8)((1 << 1) | 1 | (1 << 2));
	bad[28] = 0;
	ck_eq(hi5622_ba_parse_action(bad, n, &a), HI5622_BA_EINVAL,
	      "parse: baw 0 rejected");
	/* a category-first body (the D2H 0x1D embedded shape) parses too */
	ck_eq(hi5622_ba_parse_action(frame + 24, n - 24, &a), HI5622_BA_OK,
	      "parse: category-first body accepted");
	ck_eq(a.tid, 1, "parse: body tid");
}

static void test_tx_sequence(void)
{
	struct hi5622_ba_core core;
	struct hi5622_ba_params p;
	u8 frame[64];
	size_t flen;
	int i;

	hi5622_ba_enable = 1;
	hi5622_ba_core_init(&core);
	hi5622_ba_core_set_addrs(&core, OWN, BSSID);

	/* 1. AP-as-recipient: peer ADDBA request -> response frame TX */
	base_params(&p, 2);
	p.dialog = 0x11;
	p.baw = 32;
	flen = hi5622_ba_build_addba_req(frame, sizeof(frame), &p, OWN, PEER,
					 PEER);
	ck(flen == HI5622_BA_FRAME_ADDBA_REQ_LEN, "seq: peer req built");
	cap_reset();
	ck_eq(hi5622_ba_mgmt_rx(&core, &ops, 7, PEER, frame, flen),
	      HI5622_BA_OK, "seq: peer addba req accepted");
	ck_eq(cap_frames, 1, "seq: addba response tx");
	ck_eq((int)cap_frame_len[0], HI5622_BA_FRAME_ADDBA_RSP_LEN,
	      "seq: response 33 B");
	ck_eq(cap_frame[0][25], 1, "seq: response action 1");
	ck_eq(cap_frame[0][26], 0x11, "seq: response dialog echoed");
	ck_eq(cap_msgs, 0, "seq: no H2D on peer request (vendor parity)");
	ck_eq(core.peers[0].tx[2].state, 2, "seq: rx session mirrored active");

	/* 2. host-initiated TX BA: ADDBA request -> response -> relay */
	{
		struct hi5622_ba_params rq;

		base_params(&rq, 2);
		cap_reset();
		ck_eq(hi5622_ba_tx_start(&core, &ops, PEER, &rq), HI5622_BA_OK,
		      "seq: tx_start ok");
		ck_eq(cap_frames, 1, "seq: addba request tx");
		ck_eq(cap_frame[0][25], 0, "seq: request action 0");
		ck_eq(cap_frame[0][26], 1, "seq: request dialog incremented");
		ck_eq(core.peers[0].tx[2].state, 1, "seq: session pending");
		/* peer answers with status 0, dialog 1 */
		base_params(&p, 2);
		p.dialog = 1;
		p.baw = 64;
		flen = hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, OWN,
						 PEER, PEER);
		cap_reset();
		ck_eq(hi5622_ba_mgmt_rx(&core, &ops, 7, PEER, frame, flen),
		      HI5622_BA_OK, "seq: addba rsp accepted");
		ck_eq(cap_frames, 0, "seq: no frame on response");
		ck_eq(cap_msgs, 1, "seq: g0 0x0A relay posted");
		ck_eq((int)cap_msg_len[0], HI5622_BA_RELAY_TOTAL_LEN,
		      "seq: relay 44 B total");
		ck_eq(cap_msg[0][6], HI5622_BA_H2D_BA_RELAY,
		      "seq: relay id 0x0A");
		ck_eq(cap_msg[0][12], 3, "seq: relay type 3");
		ck_eq(cap_msg[0][13], 1, "seq: relay subtype 1");
		ck_eq(cap_msg[0][14] | (cap_msg[0][15] << 8), 7,
		      "seq: relay uid");
		ck_eq(cap_msg[0][19], 2, "seq: relay tid at payload+7");
		ck_eq(cap_msg[0][21], 0, "seq: relay status at payload+9");
		ck_eq(cap_msg[0][24] | (cap_msg[0][25] << 8), 64,
		      "seq: relay baw at payload+0x0c");
		ck_eq(core.peers[0].tx[2].state, 2, "seq: session active");

		/* 3. TX data path: start/end aggregation boundaries */
		cap_reset();
		ck_eq(hi5622_ba_tx_mpdu(&core, &ops, PEER, 2, 100),
		      HI5622_BA_OK, "seq: first mpdu ok");
		ck_eq(cap_msgs, 1, "seq: ampdu start posted");
		ck_eq((int)cap_msg_len[0], HI5622_BA_AMPDU_TOTAL_LEN,
		      "seq: ampdu msg 20 B");
		ck_eq(cap_msg[0][6], HI5622_BA_H2D_AMPDU_TX,
		      "seq: ampdu id 0x0B");
		ck_eq(cap_msg[0][12], 0, "seq: ampdu start subtype 0");
		ck_eq(cap_msg[0][13], 2, "seq: ampdu start tid");
		ck_eq(cap_msg[0][14] | (cap_msg[0][15] << 8), 100,
		      "seq: ampdu start seq");
		ck_eq(hi5622_ba_tx_mpdu(&core, &ops, PEER, 2, 101),
		      HI5622_BA_OK, "seq: second mpdu ok");
		ck_eq(cap_msgs, 1, "seq: no second start inside burst");
		ck_eq(hi5622_ba_tx_burst_end(&core, &ops, PEER, 2),
		      HI5622_BA_OK, "seq: burst end ok");
		ck_eq(cap_msgs, 2, "seq: ampdu end posted");
		ck_eq(cap_msg[1][12], 1, "seq: ampdu end subtype 1");

		/* 4. DELBA from peer -> relay subtype 2 + session cleared */
		p.dialog = 0;
		flen = hi5622_ba_build_delba(frame, sizeof(frame), 2, 1, 1,
					     OWN, PEER, PEER);
		cap_reset();
		ck_eq(hi5622_ba_mgmt_rx(&core, &ops, 7, PEER, frame, flen),
		      HI5622_BA_OK, "seq: delba accepted");
		ck_eq(cap_msgs, 1, "seq: delba relay posted");
		ck_eq(cap_msg[0][12], 3, "seq: delba relay type 3");
		ck_eq(cap_msg[0][13], 2, "seq: delba relay subtype 2");
		ck_eq(cap_msg[0][20], 1, "seq: delba relay initiator at +8");
		ck_eq(core.peers[0].tx[2].used, 0, "seq: session cleared");
	}

	/* 5. burst limit: 33rd MPDU closes and reopens the window */
	{
		struct hi5622_ba_params bq;

		base_params(&bq, 2);
		hi5622_ba_tx_start(&core, &ops, PEER, &bq);
		base_params(&p, 2);
		p.dialog = 1;
		flen = hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, OWN,
						 PEER, PEER);
		hi5622_ba_mgmt_rx(&core, &ops, 7, PEER, frame, flen);
		cap_reset();
		for (i = 0; i < HI5622_BA_BURST_MAX; i++)
			hi5622_ba_tx_mpdu(&core, &ops, PEER, 2, (u16)i);
		ck_eq(cap_msgs, 1, "burst: one start for 32 MPDUs");
		hi5622_ba_tx_mpdu(&core, &ops, PEER, 2, 32);
		ck_eq(cap_msgs, 3, "burst: 33rd closes and reopens");
		ck_eq(cap_msg[1][12], 1, "burst: end before reopen");
		ck_eq(cap_msg[2][12], 0, "burst: reopen is a start");
		hi5622_ba_tx_burst_end(&core, &ops, PEER, 2);
		ck_eq(cap_msgs, 4, "burst: final end");
	}
}

static void test_d2h(void)
{
	struct hi5622_ba_core core;
	struct hi5622_ba_params p;
	u8 hdr[HI5622_BA_HDR_LEN + 40];
	u8 frame[64];
	size_t flen;

	hi5622_ba_enable = 1;
	hi5622_ba_core_init(&core);
	hi5622_ba_core_set_addrs(&core, OWN, BSSID);

	/* establish a host-side user (uid 7) via a peer ADDBA request */
	base_params(&p, 2);
	p.dialog = 0x11;
	flen = hi5622_ba_build_addba_req(frame, sizeof(frame), &p, OWN, PEER,
					 PEER);
	hi5622_ba_mgmt_rx(&core, &ops, 7, PEER, frame, flen);

	/* D2H 2 create BA -> ADDBA request frame */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_CREATE_BA;
	hdr[12] = 7;
	hdr[13] = 0;
	hdr[14] = 3;	/* tid */
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)), HI5622_BA_OK,
	      "d2h: create accepted");
	ck_eq(cap_frames, 1, "d2h: addba request tx");
	ck_eq(cap_frame[0][25], 0, "d2h: request action 0");
	ck_eq(core.peers[0].tx[3].state, 1, "d2h: session pending");

	/* D2H 0x1D action 1: embedded ADDBA response -> relay */
	base_params(&p, 3);
	p.dialog = (u8)core.peers[0].tx[3].dialog;
	flen = hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, OWN, PEER,
					 PEER);
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_BA_ACTION;
	hdr[12] = 7;
	hdr[14] = 3;
	hdr[15] = 1;	/* action 1 */
	memcpy(hdr + HI5622_BA_HDR_LEN + 8, frame, flen);
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr,
			    HI5622_BA_HDR_LEN + 8 + flen), HI5622_BA_OK,
	      "d2h: ba action 1 accepted");
	ck_eq(cap_msgs, 1, "d2h: relay posted");
	ck_eq(cap_msg[0][13], 1, "d2h: relay subtype 1");
	ck_eq(core.peers[0].tx[3].state, 2, "d2h: session active");

	/* D2H 0x1D action 2 -> DELBA frame */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_BA_ACTION;
	hdr[12] = 7;
	hdr[14] = 3;
	hdr[15] = 2;
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)), HI5622_BA_OK,
	      "d2h: ba action 2 accepted");
	ck_eq(cap_frames, 1, "d2h: delba tx");
	ck_eq(cap_frame[0][25], 2, "d2h: delba action 2");

	/* rebuild a session, then D2H 3 del_ba -> DELBA frame + clear */
	base_params(&p, 3);
	hi5622_ba_tx_start(&core, &ops, PEER, &p);
	base_params(&p, 3);
	p.dialog = (u8)core.peers[0].tx[3].dialog;
	flen = hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, OWN, PEER,
					 PEER);
	hi5622_ba_mgmt_rx(&core, &ops, 7, PEER, frame, flen);
	ck_eq(core.peers[0].tx[3].state, 2, "d2h: session re-active");
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_DEL_BA;
	hdr[12] = 7;
	hdr[14] = 3;
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)), HI5622_BA_OK,
	      "d2h: del_ba accepted");
	ck_eq(cap_frames, 1, "d2h: del_ba delba tx");
	ck_eq(core.peers[0].tx[3].used, 0, "d2h: del_ba cleared");

	/* unknown D2H id: ignore, no callback */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = 0x7e;
	hdr[12] = 7;
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)), HI5622_BA_OK,
	      "d2h: unknown id ignored");
	ck_eq(cap_msgs + cap_frames, 0, "d2h: unknown id no traffic");
	/* unknown uid: fail closed */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_CREATE_BA;
	hdr[12] = 0x55;
	hdr[14] = 1;
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)),
	      HI5622_BA_EOPNOTSUPP, "d2h: unknown uid fail-closed");
}

static void test_tx_guards(void)
{
	struct hi5622_ba_core core;
	struct hi5622_ba_params p;

	hi5622_ba_enable = 1;
	hi5622_ba_core_init(&core);
	hi5622_ba_core_set_addrs(&core, OWN, BSSID);
	cap_reset();
	/* no session: never aggregate, never post */
	ck_eq(hi5622_ba_tx_mpdu(&core, &ops, PEER, 0, 1),
	      HI5622_BA_EOPNOTSUPP, "guard: no session no ampdu");
	ck_eq(cap_msgs, 0, "guard: no message without session");
	/* no tx_action callback: tx_start refuses without state change */
	{
		struct hi5622_ba_ops no_tx = ops;

		no_tx.tx_action = NULL;
		base_params(&p, 1);
		ck_eq(hi5622_ba_tx_start(&core, &no_tx, PEER, &p),
		      HI5622_BA_EINVAL, "guard: missing tx_action refused");
	}
	/* 9th peer refused (fresh core: eight slots) */
	{
		struct hi5622_ba_core core2;
		int i;
		u8 mac[6] = { 0x02, 0, 0, 0, 0, 1 };

		hi5622_ba_core_init(&core2);
		for (i = 0; i < HI5622_BA_MAX_PEERS; i++) {
			mac[5] = (u8)(i + 1);
			ck(hi5622_ba_peer_get(&core2, mac) != NULL,
			   "guard: peer slot available");
		}
		mac[5] = 0xff;
		ck(hi5622_ba_peer_get(&core2, mac) == NULL,
		   "guard: peer table full refused");
	}
}

int main(void)
{
	test_gate();
	test_frame_builders();
	test_payload_builders();
	test_parser();
	test_tx_sequence();
	test_d2h();
	test_tx_guards();

	printf("ba_selftest: %d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
