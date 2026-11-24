// SPDX-License-Identifier: GPL-2.0
/*
 * mgmt_selftest.c - host selftest for the portable management-frame TX/RX
 * wire logic (wifi-ap-mgmt lane, shared task task-143).
 *
 * Device-free: compiles hi5622_mgmt_logic.c with a C99 compiler and pins the
 * decoded vendor layouts, the refusals and the fail-closed defaults.  Every
 * KAT is derived from the static reversal recorded in ../TABLES.md, never
 * from a vendor binary at test time.
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hi5622_mgmt_logic.h"

static int checks, fails;

static void ck(int ok, const char *what)
{
	checks++;
	if (!ok) {
		fails++;
		printf("FAIL %s\n", what);
	}
}

static int eq(const unsigned char *got, const unsigned char *want, size_t n)
{
	return memcmp(got, want, n) == 0;
}

/* ------------------------------------------------------------------ */
/* 1. channel -> frequency (vendor hmac_rx_mgmt_send_to_host formula) */
/* ------------------------------------------------------------------ */
static void test_freq(void)
{
	himg_u16 f = 0;

	ck(himg_chan_to_freq(1, &f) == 0 && f == 2412, "freq ch1 = 2412");
	ck(himg_chan_to_freq(6, &f) == 0 && f == 2437, "freq ch6 = 2437");
	ck(himg_chan_to_freq(11, &f) == 0 && f == 2462, "freq ch11 = 2462");
	ck(himg_chan_to_freq(13, &f) == 0 && f == 2472, "freq ch13 = 2472");
	ck(himg_chan_to_freq(14, &f) == 0 && f == 2484, "freq ch14 = 2484");
	ck(himg_chan_to_freq(36, &f) == 0 && f == 5180, "freq ch36 = 5180");
	ck(himg_chan_to_freq(100, &f) == 0 && f == 5500, "freq ch100 = 5500");
	ck(himg_chan_to_freq(149, &f) == 0 && f == 5745, "freq ch149 = 5745");
	ck(himg_chan_to_freq(184, &f) == 0 && f == 4920, "freq 4.9GHz ch184 = 4920");
	ck(himg_chan_to_freq(0, &f) != 0, "freq ch0 refused");
	ck(himg_chan_to_freq(15, &f) != 0, "freq ch15 refused (no silent 5GHz map)");
	ck(himg_chan_to_freq(14 + 1, &f) != 0, "freq ch15b refused");
	ck(himg_chan_to_freq(178, &f) != 0, "freq ch178 refused");
	ck(himg_chan_to_freq(197, &f) != 0, "freq ch197 refused");
	ck(himg_chan_to_freq(255, &f) != 0, "freq ch255 refused");
	ck(himg_chan_to_freq(6, NULL) != 0, "freq NULL refused");
}

/* ------------------------------------------------------------------ */
/* 2. 12-byte descriptor + WAL cfg-event wire KAT                     */
/* ------------------------------------------------------------------ */
static void test_tx_wire(void)
{
	/* WAL cfg-event mgmt-tx, 12-byte descriptor, seq 7, dst 0, ch 6,
	 * frame len 30 at devva 0x81234567: the full 44-byte HCC wire frame. */
	static const unsigned char want[44] = {
		0x02, 0x01, 0x00, 0x00, 0x2c, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x5a, 0x5a,
		0x00, 0x00, 0x00, 0x00,		/* cookie */
		0x01, 0x00, 0x00, 0x00,		/* wal cfg event id 1 */
		0x00, 0x00,			/* pad */
		0x10, 0x00,			/* data_len = 4 + 12 */
		0x07, 0x00, 0x00, 0x00,		/* seq */
		0xf2, 0x01,			/* cmd 0x01F2 */
		0x0c, 0x00,			/* body len 12 */
		0x06, 0x00, 0x00, 0x00,		/* channel 6 */
		0x00,				/* cookie idx */
		0x00,				/* pad */
		0x1e, 0x00,			/* frame len 30 */
		0x67, 0x45, 0x23, 0x81,		/* frame devva */
	};
	unsigned char wire[64];
	unsigned char desc[12];
	struct himg_tx_desc d;
	struct himg_tx_params p;
	size_t n = 0;

	memset(&d, 0, sizeof(d));
	d.channel = 6;
	d.cookie_idx = 0;
	d.len = 30;
	d.frame_addr = 0x81234567u;
	ck(himg_build_tx_desc(desc, sizeof(desc), &d) == 0, "desc build ok");
	ck(himg_build_tx_desc(desc, 11, &d) != 0, "desc short cap refused");
	ck(himg_build_tx_desc(NULL, sizeof(desc), &d) != 0, "desc NULL refused");

	memset(&p, 0, sizeof(p));
	p.channel = 6;
	p.cookie_idx = 0;
	p.len = 30;
	p.frame_addr = 0x81234567u;
	p.seq = 7;
	p.dst = 0;
	ck(himg_build_wal_mgmt_tx(wire, sizeof(wire), &n, &p) == 0, "wal frame ok");
	ck(n == 44, "wal frame len 44");
	ck(eq(wire, want, 44), "wal frame KAT bytes");
	ck(himg_build_wal_mgmt_tx(wire, 43, &n, &p) == -ENOSPC, "wal frame short cap ENOSPC");

	/* refusal matrix */
	{
		struct himg_tx_params q = p;

		q.dst = 16;
		ck(himg_build_wal_mgmt_tx(wire, sizeof(wire), &n, &q) != 0,
		   "wal frame dst>15 refused");
		q = p;
		q.channel = 0;
		ck(himg_build_wal_mgmt_tx(wire, sizeof(wire), &n, &q) != 0,
		   "wal frame channel 0 refused");
		q = p;
		q.len = 23;
		ck(himg_build_wal_mgmt_tx(wire, sizeof(wire), &n, &q) != 0,
		   "wal frame len 23 refused");
		q = p;
		q.len = 589;
		ck(himg_build_wal_mgmt_tx(wire, sizeof(wire), &n, &q) != 0,
		   "wal frame len 589 refused");
		q = p;
		q.frame_addr = 0;
		ck(himg_build_wal_mgmt_tx(wire, sizeof(wire), &n, &q) != 0,
		   "wal frame addr 0 refused");
		q = p;
		q.channel = 300;
		ck(himg_build_wal_mgmt_tx(wire, sizeof(wire), &n, &q) != 0,
		   "wal frame channel 300 refused");
	}
	ck(himg_build_wal_mgmt_tx(NULL, sizeof(wire), &n, &p) != 0,
	   "wal frame NULL wire refused");
	ck(himg_build_wal_mgmt_tx(wire, sizeof(wire), NULL, &p) != 0,
	   "wal frame NULL out refused");
	ck(himg_build_wal_mgmt_tx(wire, sizeof(wire), &n, NULL) != 0,
	   "wal frame NULL params refused");
}

/* ------------------------------------------------------------------ */
/* 3. 802.11 management frame sanity                                  */
/* ------------------------------------------------------------------ */
static void test_frame_ok(void)
{
	unsigned char f[600];

	memset(f, 0, sizeof(f));
	f[0] = 0x50;	/* probe response, mgmt type, proto 0 */
	ck(himg_mgmt_frame_ok(f, 24) == 0, "probe rsp 24 B ok");
	f[0] = 0xb0;	/* authentication */
	ck(himg_mgmt_frame_ok(f, 30) == 0, "auth 30 B ok");
	f[0] = 0x10;	/* assoc response */
	ck(himg_mgmt_frame_ok(f, 588) == 0, "assoc rsp 588 B ok");
	ck(himg_mgmt_frame_ok(f, 23) != 0, "23 B refused");
	ck(himg_mgmt_frame_ok(f, 589) != 0, "589 B refused");
	f[0] = 0x08;	/* data frame: not a mgmt frame */
	ck(himg_mgmt_frame_ok(f, 30) != 0, "data frame refused");
	f[0] = 0x51;	/* protocol version 1 */
	ck(himg_mgmt_frame_ok(f, 30) != 0, "protocol 1 refused");
	ck(himg_mgmt_frame_ok(NULL, 30) != 0, "NULL frame refused");
}

/* ------------------------------------------------------------------ */
/* 4. D2H group-1 id-13 RX event KAT + refusals                       */
/* ------------------------------------------------------------------ */
static void build_rx(unsigned char *hdr, unsigned char *payload,
		     unsigned int frame_len, unsigned char channel,
		     int bad_info_len, int frame_fc)
{
	unsigned int total = 60u + frame_len;

	memset(hdr, 0, 12);
	hdr[0] = 0x01;			/* group 1 */
	hdr[4] = (unsigned char)(total & 0xff);
	hdr[5] = (unsigned char)((total >> 8) & 0xff);
	hdr[6] = 13;			/* id 13 */
	hdr[7] = 0;
	hdr[10] = 0x5a;
	hdr[11] = 0x5a;
	memset(payload, 0, 48u + frame_len);
	payload[7] = channel;		/* RX info channel */
	payload[17] = (unsigned char)(frame_len & 0xff);
	payload[18] = (unsigned char)((frame_len >> 8) & 0xff);
	if (bad_info_len)
		payload[17] ^= 0xffu;
	payload[39] = 0xc6;		/* s8 -58 */
	payload[48] = (unsigned char)frame_fc;
}

static void test_rx(void)
{
	unsigned char hdr[12], payload[160];
	struct himg_rx_frame rx;
	int rc;

	build_rx(hdr, payload, 30, 6, 0, 0xb0);
	rc = himg_parse_rx_mgmt(hdr, payload, 48 + 30, 90, &rx);
	ck(rc == 0, "rx auth event decoded");
	ck(rx.frame == payload + 48, "rx frame pointer at payload+48");
	ck(rx.frame_len == 30, "rx frame len 30");
	ck(rx.freq == 2437 && rx.channel == 6, "rx channel 6 -> 2437 MHz");
	ck(rx.rssi == -58, "rx rssi -58");
	ck(rx.subtype == 11, "rx subtype auth (11)");

	/* minimum frame (24 B): probe request */
	build_rx(hdr, payload, 24, 11, 0, 0x40);
	rc = himg_parse_rx_mgmt(hdr, payload, 48 + 24, 84, &rx);
	ck(rc == 0 && rx.frame_len == 24 && rx.freq == 2462,
	   "rx 24 B probe-request event decoded");
	/* 5 GHz channel */
	build_rx(hdr, payload, 30, 36, 0, 0xb0);
	ck(himg_parse_rx_mgmt(hdr, payload, 48 + 30, 90, &rx) == 0 &&
	   rx.freq == 5180 && rx.channel == 36, "rx 5 GHz ch36 decoded");

	/* refusals */
	build_rx(hdr, payload, 30, 6, 0, 0xb0);
	hdr[0] = 0x02;
	ck(himg_parse_rx_mgmt(hdr, payload, 78, 90, &rx) == -ENODATA,
	   "rx not group1 -> ENODATA");
	build_rx(hdr, payload, 30, 6, 0, 0xb0);
	hdr[6] = 12;
	ck(himg_parse_rx_mgmt(hdr, payload, 78, 90, &rx) == -ENODATA,
	   "rx id12 -> ENODATA");
	build_rx(hdr, payload, 30, 6, 0, 0xb0);
	hdr[10] = 0;
	ck(himg_parse_rx_mgmt(hdr, payload, 78, 90, &rx) == -EPROTO,
	   "rx bad tag -> EPROTO");
	build_rx(hdr, payload, 30, 6, 0, 0xb0);
	ck(himg_parse_rx_mgmt(hdr, payload, 78, 89, &rx) == -EPROTO,
	   "rx wire_len mismatch -> EPROTO");
	build_rx(hdr, payload, 30, 6, 1, 0xb0);
	ck(himg_parse_rx_mgmt(hdr, payload, 78, 90, &rx) == -EPROTO,
	   "rx info frame_len mismatch -> EPROTO");
	build_rx(hdr, payload, 30, 6, 0, 0xb0);
	ck(himg_parse_rx_mgmt(hdr, payload, 77, 90, &rx) == -EPROTO,
	   "rx truncated snapshot -> EPROTO");
	build_rx(hdr, payload, 30, 0, 0, 0xb0);
	ck(himg_parse_rx_mgmt(hdr, payload, 78, 90, &rx) == -EPROTO,
	   "rx channel 0 -> EPROTO");
	build_rx(hdr, payload, 30, 6, 0, 0x08);
	ck(himg_parse_rx_mgmt(hdr, payload, 78, 90, &rx) == -EPROTO,
	   "rx data frame -> EPROTO");
	build_rx(hdr, payload, 0, 6, 0, 0xb0);
	ck(himg_parse_rx_mgmt(hdr, payload, 48, 60, &rx) == -EPROTO,
	   "rx total == 60 (no frame) -> EPROTO");
	build_rx(hdr, payload, 30, 6, 0, 0xb0);
	ck(himg_parse_rx_mgmt(NULL, payload, 78, 90, &rx) != 0,
	   "rx NULL hdr refused");
	ck(himg_parse_rx_mgmt(hdr, NULL, 78, 90, &rx) != 0,
	   "rx NULL payload refused");
	ck(himg_parse_rx_mgmt(hdr, payload, 78, 90, NULL) != 0,
	   "rx NULL out refused");
}

int main(void)
{
	printf("hi5622 mgmt wire selftest (wifi-ap-mgmt lane, task-143)\n");
	test_freq();
	test_tx_wire();
	test_frame_ok();
	test_rx();
	printf("mgmt_selftest: %d checks, %d failures\n", checks, fails);
	return fails ? 1 : 0;
}
