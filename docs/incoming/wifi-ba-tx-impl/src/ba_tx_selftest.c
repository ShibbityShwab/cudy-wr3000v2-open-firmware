// SPDX-License-Identifier: GPL-2.0
/*
 * ba_tx_selftest.c - host selftest for the Hi5622V100 fail-closed
 * TX-direction BA core (lane wifi-ba-tx-impl, shared task-217).
 *
 * Ported from ../wifi-ba-agg/src/ba_selftest.c (the committed 183-check
 * model) and extended for this lane: the complete HCC wires (44/20/32 B),
 * the in-kernel ADDBA negotiation sequence, the TX BA notify on the state
 * transitions, the in-kernel TX-path originator step, and the fail-closed
 * matrix.  Compiles src/hi5622_ba.c standalone; no kernel, no vendor
 * artifact, no device.
 *
 * The build script additionally compiles this file with -DHAVE_H2D_FRAMES
 * and the patched tree's h2d_frames.c so every wire is re-validated by the
 * driver's own H2D decode gate (this is the exact gate hi5622_frame_post()
 * applies before any device write).
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_ba.h"

#ifdef HAVE_H2D_FRAMES
#include "h2d_frames.h"
#endif

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

static void ck_mem(const u8 *got, const u8 *want, size_t n, const char *name)
{
	checks++;
	if (memcmp(got, want, n) != 0) {
		size_t i;

		fails++;
		printf("FAIL %s (", name);
		for (i = 0; i < n; i++)
			printf("%02x", got[i]);
		printf(" != ");
		for (i = 0; i < n; i++)
			printf("%02x", want[i]);
		printf(")\n");
	}
}

/* ---- capture callbacks ------------------------------------------------- */

#define CAP_MAX 32
static u8 cap_wire[CAP_MAX][64];
static size_t cap_wire_len[CAP_MAX];
static int cap_wires;
static u8 cap_frame[CAP_MAX][64];
static size_t cap_frame_len[CAP_MAX];
static int cap_frames;
static int cap_logs;
static int cap_fail_after;	/* fail the Nth (1-based) callback, 0 = never */

static int cb_post_h2d(void *ctx, const u8 *wire, size_t len)
{
	(void)ctx;
	cap_wires++;
	if (cap_fail_after && cap_wires >= cap_fail_after)
		return -5;
	if (cap_wires > CAP_MAX || len > sizeof(cap_wire[0]))
		return -1;
	memcpy(cap_wire[cap_wires - 1], wire, len);
	cap_wire_len[cap_wires - 1] = len;
	return 0;
}

static int cb_tx_action(void *ctx, const u8 *frame, size_t len)
{
	(void)ctx;
	cap_frames++;
	if (cap_fail_after && cap_frames >= cap_fail_after)
		return -5;
	if (cap_frames > CAP_MAX || len > sizeof(cap_frame[0]))
		return -1;
	memcpy(cap_frame[cap_frames - 1], frame, len);
	cap_frame_len[cap_frames - 1] = len;
	return 0;
}

static void cb_log(void *ctx, const char *msg)
{
	(void)ctx;
	(void)msg;
	cap_logs++;
}

static const struct hi5622_ba_ops ops = {
	.ctx = NULL,
	.post_h2d = cb_post_h2d,
	.tx_action = cb_tx_action,
	.log = cb_log,
};

static void cap_reset(void)
{
	cap_wires = 0;
	cap_frames = 0;
	cap_logs = 0;
	cap_fail_after = 0;
	memset(cap_wire, 0, sizeof(cap_wire));
	memset(cap_wire_len, 0, sizeof(cap_wire_len));
	memset(cap_frame, 0, sizeof(cap_frame));
	memset(cap_frame_len, 0, sizeof(cap_frame_len));
}

static const u8 PEER[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
static const u8 OWN[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };
static const u8 BSSID[6] = { 0x02, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };
#define PEER_UID 0x1234u

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

static void core_up(struct hi5622_ba_core *core)
{
	hi5622_ba_core_init(core);
	hi5622_ba_core_set_addrs(core, OWN, BSSID);
}

/* ---- 1. fail-closed gate ---------------------------------------------- */

static void test_gate(void)
{
	struct hi5622_ba_core core;
	struct hi5622_ba_params p;
	u8 frame[HI5622_BA_FRAME_ADDBA_REQ_LEN];
	u8 wire[HI5622_BA_WIRE_MAX];
	u8 hdr[HI5622_BA_HDR_LEN + 8];
	char line[256];

	hi5622_ba_enable = 0;
	ck_eq(hi5622_ba_check_enabled(), HI5622_BA_EOPNOTSUPP, "gate: off");
	hi5622_ba_enable = 2;
	ck_eq(hi5622_ba_check_enabled(), HI5622_BA_EINVAL, "gate: >1 invalid");
	hi5622_ba_enable = 1;
	ck_eq(hi5622_ba_check_enabled(), HI5622_BA_OK, "gate: armed");

	/* disabled: no builder output, no callback, no state change */
	hi5622_ba_enable = 0;
	core_up(&core);
	base_params(&p, 0);
	cap_reset();
	ck_eq((int)hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER,
					     OWN, BSSID), 0,
	      "gate: req builder inert when off");
	ck_eq((int)hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, PEER,
					     OWN, BSSID), 0,
	      "gate: rsp builder inert when off");
	ck_eq((int)hi5622_ba_build_delba(frame, sizeof(frame), 0, 1, 1, PEER,
					 OWN, BSSID), 0,
	      "gate: delba builder inert when off");
	ck_eq((int)hi5622_ba_wire_relay_addba_rsp(wire, sizeof(wire), 7, &p),
	      0, "gate: relay wire inert when off");
	ck_eq((int)hi5622_ba_wire_tx_notify(wire, sizeof(wire), 7, 0, 0), 0,
	      "gate: notify wire inert when off");
	ck_eq((int)hi5622_ba_wire_ampdu_start(wire, sizeof(wire), 0, 1, 7, 0),
	      0, "gate: ampdu wire inert when off");
	ck_eq(hi5622_ba_tx_start(&core, &ops, PEER, &p), HI5622_BA_EOPNOTSUPP,
	      "gate: tx_start inert");
	ck_eq(hi5622_ba_tx_data(&core, &ops, PEER, 0, 1), HI5622_BA_EOPNOTSUPP,
	      "gate: tx_data inert");
	ck_eq(hi5622_ba_tx_mpdu(&core, &ops, PEER, 0, 1),
	      HI5622_BA_EOPNOTSUPP, "gate: tx_mpdu inert");
	ck_eq(hi5622_ba_tx_burst_end(&core, &ops, PEER, 0),
	      HI5622_BA_EOPNOTSUPP, "gate: tx_burst_end inert");
	ck_eq(hi5622_ba_mgmt_rx(&core, &ops, 7, PEER, frame, sizeof(frame)),
	      HI5622_BA_EOPNOTSUPP, "gate: mgmt_rx inert");
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_CREATE_BA;
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)),
	      HI5622_BA_EOPNOTSUPP, "gate: d2h inert");
	ck_eq(cap_wires, 0, "gate: zero H2D wires when off");
	ck_eq(cap_frames, 0, "gate: zero action frames when off");
	ck_eq(core.peers[0].used, 0, "gate: no peer created when off");
	ck_eq((int)hi5622_ba_status_line(&core, 0, line, sizeof(line)) > 0, 1,
	      "gate: status line renders");
	ck(strstr(line, "(off)") != NULL, "gate: status says off");

	hi5622_ba_enable = 2;
	ck_eq(hi5622_ba_tx_start(&core, &ops, PEER, &p), HI5622_BA_EINVAL,
	      "gate: >1 refused at use time");
	ck_eq((int)hi5622_ba_status_line(&core, 2, line, sizeof(line)) > 0, 1,
	      "gate: status line renders invalid");
	ck(strstr(line, "(invalid)") != NULL, "gate: status says invalid");
	hi5622_ba_enable = 1;
}

/* ---- 2. the three over-the-air action frames -------------------------- */

static void test_frames(void)
{
	struct hi5622_ba_params p;
	u8 frame[64];
	u8 bad[64];
	size_t n;
	u16 param;

	hi5622_ba_enable = 1;
	base_params(&p, 3);
	n = hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER, OWN,
				      BSSID);
	ck_eq((int)n, (int)HI5622_BA_FRAME_ADDBA_REQ_LEN, "req: len 33");
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
	ck_eq((int)n, (int)HI5622_BA_FRAME_ADDBA_RSP_LEN, "rsp: len 33");
	ck(frame[25] == 1, "rsp: action 1");
	ck_eq((int)(frame[27] | (frame[28] << 8)), 0, "rsp: status 0");
	param = (u16)((64 << 6) | (1 << 1) | 1 | (5 << 2));
	ck_eq(frame[29], param & 0xff, "rsp: BA param low");
	ck_eq(frame[30], param >> 8, "rsp: BA param high");
	ck_eq((int)(frame[31] | (frame[32] << 8)), 0,
	      "rsp: BA timeout 0 (vendor parity)");

	p.status = 7;
	n = hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, PEER, OWN,
				      BSSID);
	ck_eq((int)n, (int)HI5622_BA_FRAME_ADDBA_RSP_LEN, "rsp: fail len 33");
	ck_eq((int)(frame[27] | (frame[28] << 8)), 7, "rsp: status 7");

	/* DELBA: 30 bytes, parameter byte (tid<<4)|(initiator<<3) */
	n = hi5622_ba_build_delba(frame, sizeof(frame), 6, 1, 0x27, PEER, OWN,
				  BSSID);
	ck_eq((int)n, (int)HI5622_BA_FRAME_DELBA_LEN, "delba: len 30");
	ck(frame[24] == 3 && frame[25] == 2, "delba: category 3 action 2");
	ck_eq(frame[27], (6 << 4) | (1 << 3), "delba: param = tid<<4|init");
	ck_eq((int)(frame[28] | (frame[29] << 8)), 0x27, "delba: reason");

	/* rejects */
	ck_eq((int)hi5622_ba_build_addba_req(bad, 10, &p, PEER, OWN, BSSID), 0,
	      "req: short cap refused");
	ck_eq((int)hi5622_ba_build_addba_req(NULL, sizeof(bad), &p, PEER, OWN,
					     BSSID), 0,
	      "req: NULL frame refused");
	ck_eq((int)hi5622_ba_build_addba_req(bad, sizeof(bad), NULL, PEER, OWN,
					     BSSID), 0,
	      "req: NULL params refused");
	base_params(&p, 0);
	p.tid = 8;
	ck_eq((int)hi5622_ba_build_addba_req(bad, sizeof(bad), &p, PEER, OWN,
					     BSSID), 0,
	      "req: tid 8 refused");
	base_params(&p, 0);
	p.baw = 0;
	ck_eq((int)hi5622_ba_build_addba_req(bad, sizeof(bad), &p, PEER, OWN,
					     BSSID), 0,
	      "req: baw 0 refused");
	p.baw = 512;
	ck_eq((int)hi5622_ba_build_addba_req(bad, sizeof(bad), &p, PEER, OWN,
					     BSSID), 0,
	      "req: baw 512 refused");
	ck_eq((int)hi5622_ba_build_delba(bad, sizeof(bad), 8, 0, 0, PEER, OWN,
					 BSSID), 0,
	      "delba: tid 8 refused");
	ck_eq((int)hi5622_ba_build_delba(bad, 5, 0, 0, 0, PEER, OWN, BSSID),
	      0, "delba: short cap refused");
}

/* ---- 3. the complete HCC wires (byte-exact KATs) ---------------------- */

static void test_wires(void)
{
	struct hi5622_ba_params p;
	u8 wire[HI5622_BA_WIRE_MAX];
	const u8 relay_kat[HI5622_BA_RELAY_WIRE_LEN] = {
		0x00, 0x01, 0x00, 0x00, 0x2c, 0x00, 0x0a, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x03, 0x01, 0x34, 0x12,
		0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
		0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00,
		0x42, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	const u8 delba_relay_kat[HI5622_BA_RELAY_WIRE_LEN] = {
		0x00, 0x01, 0x00, 0x00, 0x2c, 0x00, 0x0a, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x03, 0x02, 0x34, 0x12,
		0x00, 0x00, 0x00, 0x05, 0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	const u8 ampdu_start_kat[HI5622_BA_AMPDU_WIRE_LEN] = {
		0x00, 0x01, 0x00, 0x00, 0x14, 0x00, 0x0b, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x00, 0x02, 0x34, 0x05,
		0x34, 0x12, 0x00, 0x00,
	};
	const u8 ampdu_end_kat[HI5622_BA_AMPDU_WIRE_LEN] = {
		0x00, 0x01, 0x00, 0x00, 0x14, 0x00, 0x0b, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x01, 0x02, 0x00, 0x00,
		0x34, 0x12, 0x00, 0x00,
	};
	const u8 notify_kat[HI5622_BA_NOTIFY_WIRE_LEN] = {
		0x00, 0x01, 0x00, 0x00, 0x20, 0x00, 0x1c, 0x00,
		0x00, 0x00, 0x5a, 0x5a, 0x34, 0x12, 0x02, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	size_t n;

	hi5622_ba_enable = 1;
	base_params(&p, 2);
	p.tid = 2;
	p.baw = 64;
	p.amsdu = 1;
	p.dialog = 0x42;
	p.policy = 1;
	n = hi5622_ba_wire_relay_addba_rsp(wire, sizeof(wire), PEER_UID, &p);
	ck_eq((int)n, (int)HI5622_BA_RELAY_WIRE_LEN, "wire relay: len 44");
	ck_mem(wire, relay_kat, HI5622_BA_RELAY_WIRE_LEN,
	       "wire relay: byte-exact KAT (uid/tid/status/baw/amsdu/dialog/policy)");
	n = hi5622_ba_wire_relay_delba(wire, sizeof(wire), PEER_UID, 5, 1);
	ck_eq((int)n, (int)HI5622_BA_RELAY_WIRE_LEN, "wire delba: len 44");
	ck_mem(wire, delba_relay_kat, HI5622_BA_RELAY_WIRE_LEN,
	       "wire delba: byte-exact KAT (initiator/clear flag)");
	n = hi5622_ba_wire_ampdu_start(wire, sizeof(wire), 2, 0x0534, PEER_UID,
				       0);
	ck_eq((int)n, (int)HI5622_BA_AMPDU_WIRE_LEN, "wire ampdu start: len 20");
	ck_mem(wire, ampdu_start_kat, HI5622_BA_AMPDU_WIRE_LEN,
	       "wire ampdu start: byte-exact KAT");
	n = hi5622_ba_wire_ampdu_end(wire, sizeof(wire), 2, PEER_UID);
	ck_eq((int)n, (int)HI5622_BA_AMPDU_WIRE_LEN, "wire ampdu end: len 20");
	ck_mem(wire, ampdu_end_kat, HI5622_BA_AMPDU_WIRE_LEN,
	       "wire ampdu end: byte-exact KAT");
	n = hi5622_ba_wire_tx_notify(wire, sizeof(wire), PEER_UID, 2, 0);
	ck_eq((int)n, (int)HI5622_BA_NOTIFY_WIRE_LEN, "wire notify: len 32");
	ck_mem(wire, notify_kat, HI5622_BA_NOTIFY_WIRE_LEN,
	       "wire notify: byte-exact KAT (active value 0)");
	hi5622_ba_wire_tx_notify(wire, sizeof(wire), PEER_UID, 2, 2);
	ck_eq(wire[12 + 3], 2, "wire notify: teardown value 2");
	ck_eq((int)hi5622_ba_wire_tx_notify(wire, 10, PEER_UID, 2, 0), 0,
	      "wire notify: short cap refused");
	ck_eq((int)hi5622_ba_wire_relay_delba(wire, 10, PEER_UID, 2, 0), 0,
	      "wire delba: short cap refused");

	/* DELBA relay: TID at payload+7, initiator at payload+8 (T3) */
	ck_eq(delba_relay_kat[12 + 7], 5, "wire delba: tid at payload+7");
	ck_eq(delba_relay_kat[12 + 8], 1,
	      "wire delba: initiator at payload+8");
	ck_eq(delba_relay_kat[12 + 0x14], 0, "wire delba: clear flag zero");

#ifdef HAVE_H2D_FRAMES
	{
		struct h2d_frame f;
		enum h2d_err err = H2D_OK;
		u8 tmp[64];
		size_t tl = 0;

		/* The driver's post gate decodes every wire with TRANSPORT +
		 * FIRMWARE + HANDLER; our three messages must pass it. */
		base_params(&p, 2);
		n = hi5622_ba_wire_relay_addba_rsp(wire, sizeof(wire),
						  PEER_UID, &p);
		ck_eq(h2d_frame_decode(wire, n, &f,
				       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				       H2D_VF_HANDLER, &err), 0,
		      "h2d gate: relay wire accepted (handler present)");
		ck_eq((int)f.group, 0, "h2d gate: relay group 0");
		ck_eq((int)f.id, 0x0a, "h2d gate: relay id 0x0a");
		ck_eq((int)f.payload_len, 32, "h2d gate: relay payload 32");
		n = hi5622_ba_wire_ampdu_start(wire, sizeof(wire), 2, 0x0534,
					       PEER_UID, 0);
		ck_eq(h2d_frame_decode(wire, n, &f,
				       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				       H2D_VF_HANDLER, &err), 0,
		      "h2d gate: ampdu wire accepted (handler present)");
		ck_eq((int)f.id, 0x0b, "h2d gate: ampdu id 0x0b");
		ck_eq((int)f.payload_len, 8, "h2d gate: ampdu payload 8");
		n = hi5622_ba_wire_tx_notify(wire, sizeof(wire), PEER_UID, 2, 0);
		ck_eq(h2d_frame_decode(wire, n, &f,
				       H2D_VF_TRANSPORT | H2D_VF_FIRMWARE |
				       H2D_VF_HANDLER, &err), 0,
		      "h2d gate: notify wire accepted (g0 id 0x1c handler)");
		ck_eq((int)f.id, 0x1c, "h2d gate: notify id 0x1c");
		ck_eq((int)f.payload_len, 20, "h2d gate: notify payload 20");
		/* round-trip: decode -> import -> export keeps the wire */
		ck_eq(h2d_frame_import(wire, n, &f, &err), 0,
		      "h2d gate: notify import");
		ck_eq(h2d_frame_export(&f, tmp, sizeof(tmp), 0, 0, &tl, &err),
		      0, "h2d gate: notify export");
		ck_eq((int)tl, (int)HI5622_BA_NOTIFY_WIRE_LEN,
		      "h2d gate: notify export length");
		ck_mem(tmp, wire, HI5622_BA_NOTIFY_WIRE_LEN,
		       "h2d gate: notify round-trip byte-exact");
	}
#endif
}

/* ---- 4. action-frame parser ------------------------------------------- */

static void test_parser(void)
{
	struct hi5622_ba_action a;
	u8 frame[HI5622_BA_FRAME_ADDBA_REQ_LEN];
	struct hi5622_ba_params p;

	hi5622_ba_enable = 1;
	base_params(&p, 3);
	hi5622_ba_build_addba_req(frame, sizeof(frame), &p, PEER, OWN, BSSID);
	ck_eq(hi5622_ba_parse_action(frame, sizeof(frame), &a), 0,
	      "parse: full ADDBA req");
	ck_eq(a.action, 0, "parse: action 0");
	ck_eq(a.tid, 3, "parse: req tid");
	ck_eq(a.dialog, 0x42, "parse: req dialog");
	ck_eq(a.baw, 64, "parse: req baw");
	ck_eq(a.policy, 1, "parse: req policy");
	ck_eq(a.amsdu, 1, "parse: req amsdu");

	/* embedded body form (the D2H 0x1D action-1 relay shape) */
	ck_eq(hi5622_ba_parse_action(frame + 24, sizeof(frame) - 24, &a), 0,
	      "parse: embedded body");
	ck_eq(a.tid, 3, "parse: embedded tid");

	base_params(&p, 5);
	hi5622_ba_build_addba_rsp(frame, sizeof(frame), &p, PEER, OWN, BSSID);
	ck_eq(hi5622_ba_parse_action(frame, sizeof(frame), &a), 0,
	      "parse: ADDBA rsp");
	ck_eq(a.action, 1, "parse: action 1");
	ck_eq(a.status, 0, "parse: rsp status");
	ck_eq(a.tid, 5, "parse: rsp tid");

	/* rejects */
	ck_eq(hi5622_ba_parse_action(NULL, 33, &a), HI5622_BA_EINVAL,
	      "parse: NULL frame");
	ck_eq(hi5622_ba_parse_action(frame, 2, &a), HI5622_BA_EINVAL,
	      "parse: short frame");
	frame[24] = 4;
	ck_eq(hi5622_ba_parse_action(frame, sizeof(frame), &a),
	      HI5622_BA_EINVAL, "parse: wrong category");
	frame[24] = 3;
	frame[25] = 9;
	ck_eq(hi5622_ba_parse_action(frame, sizeof(frame), &a),
	      HI5622_BA_EINVAL, "parse: unknown action");
}

/* ---- 5. the in-kernel ADDBA negotiation (the TX-direction cut) -------- */

static void test_negotiation(void)
{
	struct hi5622_ba_core core;
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_params p;
	u8 rsp[HI5622_BA_FRAME_ADDBA_RSP_LEN];
	int i;

	hi5622_ba_enable = 1;
	core_up(&core);
	peer = hi5622_ba_peer_bind(&core, PEER, PEER_UID);
	ck(peer != NULL, "neg: peer bound");
	ck_eq(peer ? (int)peer->uid : -1, PEER_UID, "neg: uid bound");
	cap_reset();

	/* 1. the TX path starts the negotiation (idle -> pending, dialog 1) */
	ck_eq(hi5622_ba_tx_data(&core, &ops, PEER, 0, 0x100), 0,
	      "neg: tx_data starts ADDBA");
	ck_eq(cap_frames, 1, "neg: one ADDBA request frame");
	ck_eq((int)cap_frame_len[0], (int)HI5622_BA_FRAME_ADDBA_REQ_LEN,
	      "neg: request len 33");
	ck_eq(cap_frame[0][25], 0, "neg: request action 0");
	ck_eq(cap_frame[0][26], 1, "neg: request dialog 1");
	ck_eq(peer->tx[0].state, 1, "neg: session pending");
	ck_eq(core.stats.addba_req, 1, "neg: addba_req accounted");
	ck(!memcmp(cap_frame[0] + 4, PEER, 6), "neg: request DA = peer");
	ck(!memcmp(cap_frame[0] + 10, OWN, 6), "neg: request SA = own");
	ck_eq(cap_frames, 1, "neg: no relay before the response");

	/* 2. a second MPDU while pending never aggregates and never re-asks */
	ck_eq(hi5622_ba_tx_data(&core, &ops, PEER, 0, 0x101),
	      HI5622_BA_EAGAIN, "neg: pending -> EAGAIN");
	ck_eq(cap_frames, 1, "neg: no second request while pending");
	ck_eq(cap_wires, 0, "neg: no H2D while pending");

	/* 3. a stale response (wrong dialog) is dropped */
	base_params(&p, 0);
	p.dialog = 2;
	p.baw = 64;
	p.status = 0;
	hi5622_ba_build_addba_rsp(rsp, sizeof(rsp), &p, PEER, OWN, BSSID);
	ck_eq(hi5622_ba_mgmt_rx(&core, &ops, PEER_UID, PEER, rsp,
				sizeof(rsp)), HI5622_BA_EINVAL,
	      "neg: stale dialog dropped");
	ck_eq(cap_wires, 0, "neg: no relay for stale dialog");

	/* 4. the peer's ADDBA response -> relay + notify + active */
	base_params(&p, 0);
	p.dialog = 1;
	p.baw = 64;
	p.policy = 1;
	p.amsdu = 0;
	p.status = 0;
	hi5622_ba_build_addba_rsp(rsp, sizeof(rsp), &p, PEER, OWN, BSSID);
	ck_eq(hi5622_ba_mgmt_rx(&core, &ops, PEER_UID, PEER, rsp,
				sizeof(rsp)), 0, "neg: ADDBA rsp accepted");
	ck_eq(peer->tx[0].state, 2, "neg: session active");
	ck_eq(cap_wires, 2, "neg: relay + notify posted");
	ck_eq((int)cap_wire_len[0], (int)HI5622_BA_RELAY_WIRE_LEN,
	      "neg: relay wire 44 B");
	ck_eq(cap_wire[0][0], 0, "neg: relay group 0");
	ck_eq(cap_wire[0][6], 0x0a, "neg: relay id 0x0a");
	ck_eq(cap_wire[0][12], 0x03, "neg: relay type 3");
	ck_eq(cap_wire[0][13], 0x01, "neg: relay subtype 1");
	ck_eq((int)(cap_wire[0][14] | (cap_wire[0][15] << 8)), PEER_UID,
	      "neg: relay user index");
	ck_eq(cap_wire[0][19], 0, "neg: relay TID");
	ck_eq(cap_wire[0][21], 0, "neg: relay status 0");
	ck_eq((int)cap_wire_len[1], (int)HI5622_BA_NOTIFY_WIRE_LEN,
	      "neg: notify wire 32 B");
	ck_eq(cap_wire[1][6], 0x1c, "neg: notify id 0x1c");
	ck_eq(cap_wire[1][12 + 3], 0, "neg: notify value active 0");
	ck_eq(core.stats.sessions, 1, "neg: session counted");
	ck_eq(core.stats.relay_rsp, 1, "neg: relay counted");
	ck_eq(core.stats.notify, 1, "neg: notify counted");

	/* 5. active session: the TX path emits the A-MPDU boundaries */
	cap_reset();
	ck_eq(hi5622_ba_tx_data(&core, &ops, PEER, 0, 0x200), 0,
	      "neg: first active MPDU");
	ck_eq(cap_wires, 1, "neg: ampdu start posted");
	ck_eq((int)cap_wire_len[0], (int)HI5622_BA_AMPDU_WIRE_LEN,
	      "neg: ampdu start wire 20 B");
	ck_eq(cap_wire[0][6], 0x0b, "neg: ampdu id 0x0b");
	ck_eq(cap_wire[0][12], 0, "neg: ampdu start subtype 0");
	ck_eq((int)(cap_wire[0][14] | (cap_wire[0][15] << 8)), 0x200,
	      "neg: ampdu start seq");
	ck_eq((int)(cap_wire[0][16] | (cap_wire[0][17] << 8)), PEER_UID,
	      "neg: ampdu start user key");
	ck_eq(peer->tx[0].ampdu_started, 1, "neg: burst open");
	for (i = 0; i < 8; i++)
		hi5622_ba_tx_data(&core, &ops, PEER, 0, (u16)(0x201 + i));
	ck_eq(cap_wires, 1, "neg: mid-burst MPDUs post nothing");
	ck_eq(hi5622_ba_tx_burst_end(&core, &ops, PEER, 0), 0,
	      "neg: burst end");
	ck_eq(cap_wires, 2, "neg: ampdu end posted");
	ck_eq(cap_wire[1][12], 1, "neg: ampdu end subtype 1");
	ck_eq(peer->tx[0].ampdu_started, 0, "neg: burst closed");
	ck_eq(core.stats.ampdu_start, 1, "neg: ampdu_start counted");
	ck_eq(core.stats.ampdu_end, 1, "neg: ampdu_end counted");
	/* no active MPDU may aggregate without an open session */
	ck_eq(hi5622_ba_tx_mpdu(&core, &ops, PEER, 1, 1),
	      HI5622_BA_EOPNOTSUPP, "neg: no aggregation on an un-agreed TID");

	/* 6. burst bound: HI5622_BA_BURST_MAX closes and reopens a window */
	cap_reset();
	for (i = 0; i < HI5622_BA_BURST_MAX + 2; i++)
		hi5622_ba_tx_data(&core, &ops, PEER, 0, (u16)(0x300 + i));
	ck_eq(cap_wires, 3, "neg: burst bound = start + end + reopen start");
	ck_eq(cap_wire[0][12], 0, "neg: bound: first window start");
	ck_eq(cap_wire[1][12], 1, "neg: bound: end at HI5622_BA_BURST_MAX");
	ck_eq(cap_wire[2][12], 0, "neg: bound: window reopened");
	hi5622_ba_tx_burst_end(&core, &ops, PEER, 0);

	/* 7. peer DELBA -> relay subtype 2 + teardown notify + clear */
	{
		u8 delba[HI5622_BA_FRAME_DELBA_LEN];

		base_params(&p, 0);
		hi5622_ba_build_delba(delba, sizeof(delba), 0, 1, 1, PEER,
				      OWN, BSSID);
		cap_reset();
		ck_eq(hi5622_ba_mgmt_rx(&core, &ops, PEER_UID, PEER, delba,
					sizeof(delba)), 0,
		      "neg: peer DELBA accepted");
		ck_eq(cap_wires, 2, "neg: delba relay + teardown notify");
		ck_eq(cap_wire[0][13], 0x02, "neg: relay subtype 2");
		ck_eq(cap_wire[0][19], 0, "neg: relay TID");
		ck_eq(cap_wire[0][20], 1, "neg: relay initiator 1");
		ck_eq(cap_wire[1][12 + 3], 2, "neg: notify value teardown 2");
		ck_eq(peer->tx[0].used, 0, "neg: session cleared");
		ck_eq(core.stats.relay_delba, 1, "neg: delba relay counted");
	}
	/* after the clear a new MPDU re-negotiates */
	cap_reset();
	ck_eq(hi5622_ba_tx_data(&core, &ops, PEER, 0, 0x400), 0,
	      "neg: re-negotiation after teardown");
	ck_eq(cap_frames, 1, "neg: new ADDBA request");
	ck_eq(cap_frame[0][26], 1, "neg: fresh dialog 1");

	/* 8. an ADDBA response with a nonzero status fails the session */
	{
		base_params(&p, 0);
		p.dialog = 1;
		p.status = 7;
		hi5622_ba_build_addba_rsp(rsp, sizeof(rsp), &p, PEER, OWN,
					  BSSID);
		cap_reset();
		ck_eq(hi5622_ba_mgmt_rx(&core, &ops, PEER_UID, PEER, rsp,
					sizeof(rsp)), 0,
		      "neg: failing rsp consumed");
		ck_eq(peer->tx[0].used, 0, "neg: failing rsp clears session");
		ck_eq(cap_wires, 1, "neg: failing rsp still relays (no notify)");
		ck_eq(cap_wire[0][21], 7, "neg: relay carries the status");
	}
}

/* ---- 6. the peer-as-originator (RX-recipient) direction --------------- */

static void test_rx_direction(void)
{
	struct hi5622_ba_core core;
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_params p;
	u8 req[HI5622_BA_FRAME_ADDBA_REQ_LEN];

	hi5622_ba_enable = 1;
	core_up(&core);
	peer = hi5622_ba_peer_bind(&core, PEER, PEER_UID);
	base_params(&p, 4);
	hi5622_ba_build_addba_req(req, sizeof(req), &p, OWN, PEER, BSSID);
	cap_reset();
	ck_eq(hi5622_ba_mgmt_rx(&core, &ops, PEER_UID, PEER, req,
				sizeof(req)), 0,
	      "rx-dir: ADDBA request answered");
	ck_eq(cap_frames, 1, "rx-dir: one ADDBA response frame");
	ck_eq(cap_frame[0][25], 1, "rx-dir: action 1");
	ck_eq(peer->tx[4].state, 2, "rx-dir: RX session mirrored active");
	ck_eq(cap_wires, 0,
	      "rx-dir: no H2D relay (vendor uses the user-sync writer [U])");
}

/* ---- 7. firmware D2H BA events ---------------------------------------- */

static void test_d2h(void)
{
	struct hi5622_ba_core core;
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_params p;
	u8 hdr[HI5622_BA_HDR_LEN + 8 + HI5622_BA_FRAME_ADDBA_RSP_LEN];
	u8 rsp[HI5622_BA_FRAME_ADDBA_RSP_LEN];

	hi5622_ba_enable = 1;
	core_up(&core);
	peer = hi5622_ba_peer_bind(&core, PEER, PEER_UID);
	ck(peer != NULL, "d2h: peer bound");

	/* unknown uid: no peer is guessed -> inert (fail closed) */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_CREATE_BA;
	hdr[12] = 0x99;
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)),
	      HI5622_BA_EOPNOTSUPP, "d2h: unknown uid refused");
	ck_eq(cap_frames, 0, "d2h: no frame for an unknown uid");

	/* D2H 2 (create BA): starts the ADDBA negotiation for that TID */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_CREATE_BA;
	hdr[12] = (u8)(PEER_UID & 0xff);
	hdr[13] = (u8)(PEER_UID >> 8);
	hdr[14] = 3;
	hdr[15] = 5;	/* peer index: not used by the host side */
	hdr[16] = 0;
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)), 0,
	      "d2h: create BA consumed");
	ck_eq(cap_frames, 1, "d2h: ADDBA request tx");
	ck_eq(peer->tx[3].state, 1, "d2h: TID 3 pending");
	ck_eq(core.stats.d2h_events, 1, "d2h: event counted");

	/* D2H 0x1D action 1: the firmware relays a received ADDBA response
	 * (frame bytes at hdr+20) -> relay + notify + active */
	base_params(&p, 3);
	p.dialog = 1;
	p.baw = 64;
	p.status = 0;
	hi5622_ba_build_addba_rsp(rsp, sizeof(rsp), &p, PEER, OWN, BSSID);
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_BA_ACTION;
	hdr[12] = (u8)(PEER_UID & 0xff);
	hdr[13] = (u8)(PEER_UID >> 8);
	hdr[14] = 3;
	hdr[15] = 1;	/* action 1 */
	memcpy(hdr + HI5622_BA_HDR_LEN + 8, rsp, sizeof(rsp));
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr,
			    HI5622_BA_HDR_LEN + 8 + sizeof(rsp)), 0,
	      "d2h: relayed ADDBA rsp consumed");
	ck_eq(peer->tx[3].state, 2, "d2h: relayed rsp -> active");
	ck_eq(cap_wires, 2, "d2h: relay + notify");
	ck_eq(cap_wire[0][13], 0x01, "d2h: relay subtype 1");

	/* D2H 3 (delete BA): DELBA action frame + teardown notify + clear */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_DEL_BA;
	hdr[12] = (u8)(PEER_UID & 0xff);
	hdr[13] = (u8)(PEER_UID >> 8);
	hdr[14] = 3;
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)), 0,
	      "d2h: delete BA consumed");
	ck_eq(cap_frames, 1, "d2h: DELBA action tx");
	ck_eq(cap_frame[0][25], 2, "d2h: DELBA action 2");
	ck_eq(cap_wires, 1, "d2h: teardown notify");
	ck_eq(peer->tx[3].used, 0, "d2h: session cleared");

	/* D2H 0x1D action 2: firmware asks for a DELBA (initiator 1) */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_BA_ACTION;
	hdr[12] = (u8)(PEER_UID & 0xff);
	hdr[13] = (u8)(PEER_UID >> 8);
	hdr[14] = 2;
	hdr[15] = 2;
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)), 0,
	      "d2h: action 2 consumed");
	ck_eq(cap_frames, 1, "d2h: action 2 DELBA tx");
	ck_eq(core.stats.delba, 2, "d2h: delba counted");

	/* D2H 0x0F (peer DELBA report): TID at hdr[19] */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = HI5622_BA_D2H_RX_DELBA_EVENT;
	hdr[12] = (u8)(PEER_UID & 0xff);
	hdr[13] = (u8)(PEER_UID >> 8);
	hdr[19] = 0;
	hdr[20] = 1;
	hdr[21] = 1;
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)), 0,
	      "d2h: 0x0F consumed");

	/* an unrelated D2H id is ignored, never misread */
	memset(hdr, 0, sizeof(hdr));
	hdr[6] = 0x77;
	cap_reset();
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, sizeof(hdr)), 0,
	      "d2h: unrelated id ignored");
	ck_eq(cap_frames + cap_wires, 0, "d2h: unrelated id posts nothing");
	ck_eq(hi5622_ba_d2h(&core, &ops, hdr, 8), HI5622_BA_EINVAL,
	      "d2h: truncated header refused");
}

/* ---- 8. error propagation and the rollback rules ---------------------- */

static void test_errors(void)
{
	struct hi5622_ba_core core;
	struct hi5622_ba_peer *peer;
	struct hi5622_ba_params p;
	u8 rsp[HI5622_BA_FRAME_ADDBA_RSP_LEN];

	hi5622_ba_enable = 1;
	core_up(&core);
	peer = hi5622_ba_peer_bind(&core, PEER, PEER_UID);

	/* a failed ADDBA request rolls the pending session back */
	base_params(&p, 0);
	cap_reset();
	cap_fail_after = 1;
	ck_eq(hi5622_ba_tx_start(&core, &ops, PEER, &p) != 0, 1,
	      "err: failed request tx");
	ck_eq(peer->tx[0].used, 0, "err: pending rolled back");
	ck_eq(peer->tx[0].state, 0, "err: state rolled back");

	/* a failed relay leaves the session pending (no silent active) */
	cap_reset();
	hi5622_ba_tx_start(&core, &ops, PEER, &p);
	base_params(&p, 0);
	p.dialog = 1;
	p.status = 0;
	hi5622_ba_build_addba_rsp(rsp, sizeof(rsp), &p, PEER, OWN, BSSID);
	cap_fail_after = 0;
	cap_reset();
	cap_fail_after = 1;
	ck_eq(hi5622_ba_mgmt_rx(&core, &ops, PEER_UID, PEER, rsp,
				sizeof(rsp)) != 0, 1,
	      "err: failed relay");
	ck_eq(peer->tx[0].state, 1, "err: session stays pending on relay fail");

	/* a failed notify is informational: the session still activates */
	cap_reset();
	cap_fail_after = 2;	/* 1 = relay ok, 2 = notify fails */
	ck_eq(hi5622_ba_mgmt_rx(&core, &ops, PEER_UID, PEER, rsp,
				sizeof(rsp)), 0,
	      "err: notify failure does not fail the session");
	ck_eq(peer->tx[0].state, 2, "err: session active after notify failure");
	cap_reset();

	/* bad arguments never post */
	ck_eq(hi5622_ba_tx_start(&core, &ops, NULL, &p), HI5622_BA_EINVAL,
	      "err: NULL mac refused");
	ck_eq(hi5622_ba_tx_start(&core, &ops, PEER, NULL), HI5622_BA_EINVAL,
	      "err: NULL params refused");
	ck_eq(hi5622_ba_mgmt_rx(&core, &ops, 0, PEER, NULL, 33),
	      HI5622_BA_EINVAL, "err: NULL frame refused");
	ck_eq(hi5622_ba_tx_mpdu(&core, &ops, PEER, 8, 0), HI5622_BA_EINVAL,
	      "err: tid 8 refused");
	ck_eq(cap_wires + cap_frames, 0, "err: no post on bad arguments");

	/* a peer table overflow refuses, never overwrites */
	{
		u8 mac[6];
		int i;

		core_up(&core);
		for (i = 0; i < HI5622_BA_MAX_PEERS; i++) {
			mac[0] = 0x02;
			mac[1] = (u8)i;
			mac[2] = 0x33;
			mac[3] = 0x44;
			mac[4] = 0x55;
			mac[5] = (u8)(0x60 + i);
			ck(hi5622_ba_peer_bind(&core, mac, (u16)(1 + i)) != NULL,
			   "err: peer table slot");
		}
		mac[5] = 0xff;
		ck(hi5622_ba_peer_bind(&core, mac, 0x77) == NULL,
		   "err: peer table full refuses");
	}
}

/* ---- 9. status line ---------------------------------------------------- */

static void test_status(void)
{
	struct hi5622_ba_core core;
	char line[512];

	core_up(&core);
	hi5622_ba_enable = 1;
	ck(hi5622_ba_status_line(&core, 1, line, sizeof(line)) > 0,
	   "status: renders");
	ck(strstr(line, "ba_enable=1 (tx-addba)") != NULL, "status: mode armed");
	ck(strstr(line, "active=0") != NULL, "status: no active session");
	ck_eq(hi5622_ba_status_line(NULL, 1, line, sizeof(line)) > 0, 1,
	      "status: NULL core renders");
	ck_eq(hi5622_ba_status_line(&core, 1, NULL, 10), HI5622_BA_EINVAL,
	      "status: NULL buffer refused");
	ck_eq(hi5622_ba_status_line(&core, 1, line, 4), 3,
	      "status: clamped to len-1");
}

int main(void)
{
	printf("ba_tx_selftest: Hi5622V100 TX-direction BA core (lane wifi-ba-tx-impl)\n");

	test_gate();
	test_frames();
	test_wires();
	test_parser();
	test_negotiation();
	test_rx_direction();
	test_d2h();
	test_errors();
	test_status();

	printf("ba_tx_selftest: %d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
