/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mgmt_ctrl_selftest.c - host-only strict-C99 test for the CORRECTED
 * management-TX carrier (raw HCC group 0 / id 6 control message) and the
 * unchanged WAL/RX helpers in src/hi5622_mgmt_logic.*.
 *
 * ORIGINAL clean-room code (lane wifi-sibling-gpl, task-151).  The wire facts
 * tested here are derived from our own vendor module (local evidence) and are
 * documented in ../REPORT.md and ../MGMT-CARRIER-CORRECTION.md.  No vendor
 * source/object code, firmware byte or calibration value is reproduced.
 *
 * Build/run:
 *   gcc -std=c99 -Wall -Wextra -O2 -I src -o /tmp/mgmt_ctrl_selftest \
 *       tools/mgmt_ctrl_selftest.c src/hi5622_mgmt_logic.c
 *   /tmp/mgmt_ctrl_selftest
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_mgmt_logic.h"

static unsigned int g_checks;
static unsigned int g_fails;

#define CHECK(cond, msg)                                                       \
	do {                                                                   \
		g_checks++;                                                    \
		if (!(cond)) {                                                 \
			g_fails++;                                             \
			printf("FAIL %u: %s\n", g_checks, (msg));              \
		}                                                              \
	} while (0)

#define CHECK_EQ(actual, expected, msg)                                        \
	do {                                                                   \
		unsigned long _a = (unsigned long)(actual);                    \
		unsigned long _e = (unsigned long)(expected);                  \
		g_checks++;                                                    \
		if (_a != _e) {                                                \
			g_fails++;                                             \
			printf("FAIL %u: %s (got %lu want %lu)\n",            \
			       g_checks, (msg), _a, _e);                       \
		}                                                              \
	} while (0)

static himg_u16 get16(const himg_u8 *p)
{
	return (himg_u16)((himg_u16)p[0] | ((himg_u16)p[1] << 8));
}

/* Build a minimal but structurally valid 802.11 management frame: assoc
 * response style (fc0 = 0x10), header 24 B + body. */
static size_t make_mgmt_frame(himg_u8 *f, size_t body_len)
{
	memset(f, 0, 24u + body_len);
	f[0] = 0x10u;	/* protocol 0, type mgmt, subtype assoc response */
	f[1] = 0x00u;
	f[4] = 0x02u; f[5] = 0x00u; f[6] = 0x00u; f[7] = 0x00u; f[8] = 0x00u;
	f[9] = 0x01u;	/* addr1 = 02:00:00:00:00:01 (the client) */
	f[10] = 0x02u; f[11] = 0x00u; f[12] = 0x00u; f[13] = 0x00u;
	f[14] = 0x00u; f[15] = 0x01u;
	return 24u + body_len;
}

static void test_hcc_mgmt_tx_builder(void)
{
	himg_u8 frame[200];
	himg_u8 wire[HIMG_MGMT_TX_MAX_TOTAL];
	struct himg_tx_params p;
	size_t flen, wlen = 0;
	himg_u8 ctl_off = HIMG_HCC_HDR_LEN;

	flen = make_mgmt_frame(frame, 56u); /* 80-byte assoc response */
	memset(&p, 0, sizeof(p));
	p.frame = frame;
	p.frame_len = (himg_u16)flen;
	p.cookie = 0x0au;
	p.dst = 0u;
	p.vap_id = 0u;
	p.band = 0u;
	p.hdr2 = 0u;
	CHECK_EQ(himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p), 0,
		 "g0 id6 builder accepts an 80-byte mgmt frame");
	CHECK_EQ(wlen, 12u + 48u + flen, "total = 12 hdr + 48 ctl + frame");

	/* 12-byte HCC header (same convention as the established g0 builders) */
	CHECK_EQ(wire[0], HIMG_HCC_GROUP_CHIP, "hdr group 0 (g0 carrier)");
	CHECK_EQ(wire[0], 0u, "hdr[0] group nibble is zero");
	CHECK_EQ(wire[1], (himg_u8)HIMG_ALLOC_NIBBLE, "hdr[1] alloc nibble = 1");
	CHECK_EQ(wire[2], 0u, "hdr[2] hdr2 = VAP byte 1");
	CHECK_EQ(wire[3], 0u, "hdr[3] VAP id");
	CHECK_EQ(get16(wire + 4), (himg_u16)wlen, "hdr[4..5] total length");
	CHECK_EQ(get16(wire + 6), 6u, "hdr[6..7] message id 6 (MGMT)");
	CHECK_EQ(wire[8], 0u, "hdr[8] fc = 0");
	CHECK_EQ(wire[9], 0u, "hdr[9] dst = 0 (local chip)");
	CHECK_EQ(get16(wire + 10), HIMG_HCC_TAG, "hdr[10..11] tag 0x5a5a");

	/* 48-byte device TX control block (vendor-filled fields only) */
	CHECK_EQ(get16(wire + ctl_off + HIMG_CTL_MPDU_LEN),
		 (himg_u16)(flen - 24u), "ctl[0..1] mpdu_len = frame_len - 24");
	CHECK_EQ(wire[ctl_off + HIMG_CTL_HDR_LEN], 24u,
		 "ctl[2] frame_header_length = 24");
	CHECK_EQ(wire[ctl_off + HIMG_CTL_FLAGS], HIMG_CTL_FLAGS_VAL,
		 "ctl[6] flags = vendor value 3");
	CHECK_EQ(wire[ctl_off + HIMG_CTL_COOKIE], (himg_u8)(0x0au << 4),
		 "ctl[8] cookie in high nibble");
	CHECK_EQ(get16(wire + ctl_off + HIMG_CTL_USER), HIMG_CTL_INVALID_USER,
		 "ctl[9..10] no-user value 0x0fff");
	CHECK_EQ(wire[HIMG_MGMT_TX_FRAME_OFF], frame[0],
		 "frame starts at wire offset 60");
	CHECK(memcmp(wire + HIMG_MGMT_TX_FRAME_OFF, frame, flen) == 0,
	      "frame bytes copied verbatim");

	/* known station: assoc id goes into the user field */
	p.user_id = 100u;
	CHECK_EQ(himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p), 0,
		 "builder accepts a known station");
	CHECK_EQ(get16(wire + ctl_off + HIMG_CTL_USER), 100u,
		 "ctl[9..10] carries the station assoc id");

	/* refusals */
	p.frame = NULL;
	CHECK(himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p) != 0,
	      "refuses NULL frame");
	p.frame = frame;
	p.frame_len = 25u;
	CHECK(himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p) != 0,
	      "refuses frame_len <= 25 (vendor check)");
	p.frame_len = (himg_u16)(HIMG_MGMT_TX_MAX_FRAME + 1u);
	CHECK(himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p) != 0,
	      "refuses frame_len above the vendor host bound");
	p.frame_len = (himg_u16)flen;
	p.dst = 0x10u;
	CHECK(himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p) != 0,
	      "refuses dst above the 4-bit nibble");
	p.dst = 0u;
	CHECK(himg_build_hcc_mgmt_tx(wire, 12u + 48u + 4u, &wlen, &p) != 0,
	      "refuses a buffer that cannot hold the message");
	/* minimum accepted frame is 26 bytes */
	flen = make_mgmt_frame(frame, 2u);
	p.frame_len = (himg_u16)flen;
	CHECK_EQ(himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p), 0,
		 "accepts the 26-byte minimum frame");
	/* 802.11 header check: a data frame must be refused */
	frame[0] = 0x08u; /* data */
	CHECK(himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p) != 0,
	      "refuses a non-management 802.11 frame");
	frame[0] = 0x10u;
	frame[0] = 0x11u; /* protocol version 1 */
	CHECK(himg_build_hcc_mgmt_tx(wire, sizeof(wire), &wlen, &p) != 0,
	      "refuses a bad protocol version");
}

static void test_deprecated_wal_builder_unchanged(void)
{
	himg_u8 wire[64];
	struct himg_tx_params p;
	size_t wlen = 0;

	memset(&p, 0, sizeof(p));
	p.channel = 6u;
	p.cookie_idx = 0u;
	p.len = 80u;
	p.frame_addr = 0x81000000u;
	p.seq = 7u;
	p.dst = 0u;
	CHECK_EQ(himg_build_wal_mgmt_tx(wire, sizeof(wire), &wlen, &p), 0,
		 "deprecated WAL builder still builds (record only)");
	CHECK_EQ(wlen, 44u, "WAL envelope total 32 + body 12");
	CHECK_EQ(wire[0], HIMG_HCC_GROUP_WAL, "WAL builder group 2");
	CHECK_EQ(get16(wire + 6), HIMG_HCC_ID_WAL, "WAL builder header id 0");
	CHECK_EQ(get16(wire + 28), HIMG_WAL_CMD_MGMT_TX,
		 "WAL builder cmd 0x01F2");
	CHECK_EQ(get16(wire + 30), HIMG_WAL_CMD_MGMT_TX_BODY,
		 "WAL builder body 12");
}

static void test_rx_mgmt_decode(void)
{
	himg_u8 wire[12u + 48u + 80u];
	himg_u8 *info = wire + 12;
	himg_u8 *frame = wire + 60;
	struct himg_rx_frame out;
	himg_u16 total = (himg_u16)sizeof(wire);

	memset(wire, 0, sizeof(wire));
	wire[0] = 1u;	/* group 1 (mgmt RX carrier) */
	wire[1] = HIMG_ALLOC_NIBBLE;
	wire[4] = (himg_u8)(total & 0xffu);
	wire[5] = (himg_u8)(total >> 8);
	wire[6] = HIMG_RX_ID;	/* 13 */
	wire[10] = 0x5au;
	wire[11] = 0x5au;
	info[HIMG_RX_INFO_CHANNEL] = 6u;
	info[HIMG_RX_INFO_FRAME_LEN] = 80u;
	info[HIMG_RX_INFO_FRAME_LEN + 1u] = 0u;
	info[HIMG_RX_INFO_RSSI] = (himg_u8)(signed char)-64;
	(void)make_mgmt_frame(frame, 56u);
	CHECK_EQ(himg_parse_rx_mgmt(wire, info, sizeof(wire) - 12u, total, &out),
		 0, "decodes the D2H g1 id13 mgmt RX event");
	CHECK_EQ(out.frame_len, 80u, "RX frame length from the 48-B info block");
	CHECK_EQ(out.channel, 6u, "RX channel at info+7");
	CHECK_EQ(out.rssi, -64, "RX RSSI at info+39");
	CHECK_EQ(out.freq, 2437u, "RX channel 6 -> 2437 MHz");
	CHECK(memcmp(out.frame, frame, 80u) == 0, "RX frame bytes are intact");
}

int main(void)
{
	test_hcc_mgmt_tx_builder();
	test_deprecated_wal_builder_unchanged();
	test_rx_mgmt_decode();
	printf("%s: %u/%u checks passed%s\n",
	       g_fails == 0u ? "PASS" : "FAIL", g_checks - g_fails, g_checks,
	       g_fails == 0u ? "" : " - see FAIL lines above");
	return g_fails == 0u ? 0 : 1;
}
