// SPDX-License-Identifier: GPL-2.0
/*
 * scan_d2h_selftest.c - host-side unit tests for the scan D2H decoder.
 *
 * ORIGINAL clean-room code (lane wifi-scan-d2h, shared task task-118).
 * All frames used here are SYNTHETIC: they are built byte-by-byte from the
 * derived layout in ../REPORT.md, not copied from any vendor artifact.  The
 * vectors mirror the testvectors/ JSON files (same construction, same expected fields).
 *
 * Build: make -C .. test        (or) cc -std=c99 -Wall -Wextra -Werror \
 *        -pedantic -I../src scan_d2h_selftest.c ../src/hi5622_scan_d2h.c
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_scan_d2h.h"

static int checks;
static int failures;

#define CK(cond, msg) do {						\
	checks++;							\
	if (!(cond)) {							\
		failures++;						\
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, (msg));	\
	}								\
} while (0)

#define CK_EQ(a, b, msg) do {						\
	long long _a = (long long)(a), _b = (long long)(b);		\
	checks++;							\
	if (_a != _b) {							\
		failures++;						\
		printf("FAIL %s:%d: %s (got %lld want %lld)\n",		\
		       __FILE__, __LINE__, (msg), _a, _b);		\
	}								\
} while (0)

/* ---------------------------------------------------------------- */
/* synthetic frame builder                                          */
/* ---------------------------------------------------------------- */
#define MAXP 640

static size_t put16(u8 *p, size_t off, u16 v)
{
	p[off] = (u8)(v & 0xff);
	p[off + 1] = (u8)(v >> 8);
	return off + 2;
}

static size_t put64(u8 *p, size_t off, u64 v)
{
	int i;

	for (i = 0; i < 8; i++)
		p[off + i] = (u8)(v >> (8 * i));
	return off + 8;
}

static size_t put_ie(u8 *p, size_t off, u8 id, const u8 *body, u8 blen)
{
	p[off] = id;
	p[off + 1] = blen;
	if (blen)
		memcpy(p + off + 2, body, blen);
	return off + 2 + blen;
}

/*
 * Build a beacon/probe-response + 20-byte trailer event payload.
 *   subtype 8 = beacon, 5 = probe response
 *   chan    channel number (DS IE for 2.4 GHz, HT operation IE if ht5g)
 *   ssid    SSID string (NULL or "" = hidden zero-length SSID IE)
 *   trailer the 20 trailer bytes (NULL = none / frame-only layout)
 */
static size_t build_bss_event(u8 *p, u8 subtype, const u8 *bssid, u16 cap,
			      u16 interval, u8 chan, bool ht5g,
			      const char *ssid, const u8 *trailer)
{
	size_t off = 0;
	u8 i;

	memset(p, 0, MAXP);
	/* frame control: version 0, type 0 (mgmt), subtype */
	off = put16(p, off, ((u16)subtype << 4) | 0x0000u);
	off = put16(p, off, 0);			/* duration */
	for (i = 0; i < 6; i++) p[off++] = 0xff;	/* addr1 broadcast */
	for (i = 0; i < 6; i++) p[off++] = (u8)(0x20 + i); /* addr2 */
	for (i = 0; i < 6; i++) p[off++] = bssid[i];	/* addr3 = BSSID */
	off = put16(p, off, 0);			/* seq ctl */
	off = put64(p, off, 0x0102030405060708ull);	/* timestamp */
	off = put16(p, off, interval);
	off = put16(p, off, cap);
	/* SSID IE */
	if (ssid && ssid[0]) {
		off = put_ie(p, off, 0, (const u8 *)ssid,
			     (u8)strlen(ssid));
	} else {
		off = put_ie(p, off, 0, NULL, 0);
	}
	if (ht5g) {
		u8 ht[22];

		memset(ht, 0, sizeof(ht));
		ht[0] = chan;			/* primary channel */
		off = put_ie(p, off, 61, ht, (u8)sizeof(ht));
	} else {
		u8 ds = chan;

		off = put_ie(p, off, 3, &ds, 1);
	}
	if (trailer) {
		memcpy(p + off, trailer, HI5622_SCAN_D2H_TRAILER_LEN);
		off += HI5622_SCAN_D2H_TRAILER_LEN;
	}
	return off;
}

static void fill_trailer(u8 *t, s8 rssi_candidate, u8 phy)
{
	memset(t, 0, HI5622_SCAN_D2H_TRAILER_LEN);
	t[0x04] = 0x11;
	t[0x05] = (u8)rssi_candidate;
	t[0x06] = (u8)(rssi_candidate - 5);
	t[0x08] = 0x34;
	t[0x0c] = 0x56;
	t[0x10] = 0x78;
	t[0x11] = phy;
}

/* ---------------------------------------------------------------- */
static void test_chan_freq(void)
{
	CK_EQ(hi5622_scan_d2h_chan_to_freq(1, false), 2412, "ch1 2412");
	CK_EQ(hi5622_scan_d2h_chan_to_freq(6, false), 2437, "ch6 2437");
	CK_EQ(hi5622_scan_d2h_chan_to_freq(13, false), 2472, "ch13 2472");
	CK_EQ(hi5622_scan_d2h_chan_to_freq(14, false), 2484, "ch14 2484");
	CK_EQ(hi5622_scan_d2h_chan_to_freq(36, true), 5180, "ch36 5180");
	CK_EQ(hi5622_scan_d2h_chan_to_freq(0, false), 0, "ch0 invalid");
	CK_EQ(hi5622_scan_d2h_chan_to_freq(200, false), 0, "ch200 invalid");
}

static void test_classify(void)
{
	CK_EQ(hi5622_scan_d2h_event_kind(0x01, 0x0f), HI5622_SCAN_D2H_EV_BSS,
	      "g1 id15 = bss");
	CK_EQ(hi5622_scan_d2h_event_kind(0x01, 0x10),
	      HI5622_SCAN_D2H_EV_COMPLETE, "g1 id16 = complete");
	CK_EQ(hi5622_scan_d2h_event_kind(0x01, 0x11),
	      HI5622_SCAN_D2H_EV_CHANNEL, "g1 id17 = channel");
	CK_EQ(hi5622_scan_d2h_event_kind(0x02, 0x02),
	      HI5622_SCAN_D2H_EV_STA_COMP, "g2 id2 = sta comp");
	CK_EQ(hi5622_scan_d2h_event_kind(0x01, 0x03), HI5622_SCAN_D2H_EV_NONE,
	      "g1 id3 not a scan event");
	CK_EQ(hi5622_scan_d2h_event_kind(0x04, 0x02), HI5622_SCAN_D2H_EV_NONE,
	      "g4 id2 not a scan event");
}

static void test_beacon_2g_trailer(void)
{
	u8 p[MAXP];
	u8 bssid[6] = { 0x02, 0x11, 0x22, 0x33, 0x44, 0x55 };
	u8 tr[HI5622_SCAN_D2H_TRAILER_LEN];
	struct hi5622_scan_d2h_bss b;
	struct hi5622_scan_d2h_opts o = { false, true };
	size_t len;

	fill_trailer(tr, -42, HI5622_SCAN_D2H_PHY_RAW4);
	len = build_bss_event(p, 8, bssid, 0x0411, 100, 6, false,
			      "bench-ap-2g", tr);
	CK_EQ(hi5622_scan_d2h_parse_bss(p, (u32)len, &o, &b), 0,
	      "2.4 GHz beacon decodes");
	CK_EQ(b.layout, HI5622_SCAN_D2H_LAYOUT_FRAME_TRAILER, "layout frame+tail");
	CK_EQ(b.frame_type, HI5622_SCAN_D2H_FTYPE_BEACON, "type beacon");
	CK(memcmp(b.bssid, bssid, 6) == 0, "bssid addr3");
	CK_EQ(b.chan_num, 6, "chan 6");
	CK(!b.is_5g, "2.4 GHz");
	CK_EQ(b.freq, 2437, "freq 2437");
	CK_EQ(b.beacon_interval, 100, "beacon interval");
	CK_EQ(b.capability, 0x0411, "capability");
	CK_EQ(b.tsf, 0x0102030405060708ull, "tsf");
	CK_EQ(b.ssid_len, (u8)strlen("bench-ap-2g"), "ssid len");
	CK(memcmp(b.ssid, "bench-ap-2g", strlen("bench-ap-2g")) == 0, "ssid bytes");
	CK(!b.ssid_hidden, "ssid visible");
	CK_EQ(b.trailer_present, 1, "trailer present");
	CK_EQ(b.trailer_len, HI5622_SCAN_D2H_TRAILER_LEN, "trailer len 20");
	CK_EQ(b.tr_s8_5, -42, "trailer+5 raw s8");
	CK_EQ(b.tr_s8_6, -47, "trailer+6 raw s8");
	CK_EQ(b.tr_u8_4, 0x11, "trailer+4 u8");
	CK_EQ(b.tr_u32_8, 0x34, "trailer+8 u32");
	CK_EQ(b.tr_u32_12, 0x56, "trailer+12 u32");
	CK_EQ(b.tr_u8_16, 0x78, "trailer+16 u8");
	CK_EQ(b.phy_mode, HI5622_SCAN_D2H_PHY_RAW4, "phy raw4");
	CK_EQ(b.signal_mbm, 0, "signal is 0 under fail-closed default");
	CK(b.ie_len > 0, "IEs reported");
	/* the IE buffer must start with the SSID IE id 0 */
	CK_EQ(b.ie[0], 0, "first IE is SSID");
}

static void test_signal_optin(void)
{
	u8 p[MAXP];
	u8 bssid[6] = { 2, 3, 4, 5, 6, 7 };
	u8 tr[HI5622_SCAN_D2H_TRAILER_LEN];
	struct hi5622_scan_d2h_bss b;
	struct hi5622_scan_d2h_opts o = { true, true };
	size_t len;

	fill_trailer(tr, -42, HI5622_SCAN_D2H_PHY_RAW8);
	len = build_bss_event(p, 5, bssid, 0x0001, 50, 36, true, "5g",
			      tr);
	CK_EQ(hi5622_scan_d2h_parse_bss(p, (u32)len, &o, &b), 0,
	      "5 GHz probe response decodes");
	CK_EQ(b.frame_type, HI5622_SCAN_D2H_FTYPE_PRESP, "type probe-resp");
	CK(b.is_5g, "5 GHz detected");
	CK_EQ(b.freq, 5180, "freq 5180");
	CK_EQ(b.chan_num, 36, "chan 36");
	CK_EQ(b.phy_mode, HI5622_SCAN_D2H_PHY_RAW8, "phy raw8");
	CK_EQ(b.signal_mbm, -4200, "opt-in signal from trailer+5");
	CK(b.signal_from_trailer, "signal flagged as trailer-derived");
}

static void test_frame_only(void)
{
	u8 p[MAXP];
	u8 bssid[6] = { 0x0a, 0, 0, 0, 0, 0x0b };
	struct hi5622_scan_d2h_bss b;
	size_t len;

	len = build_bss_event(p, 8, bssid, 0x0401, 200, 11, false, "bare",
			      NULL);
	CK_EQ(hi5622_scan_d2h_parse_bss(p, (u32)len, NULL, &b), 0,
	      "bare beacon decodes");
	CK_EQ(b.layout, HI5622_SCAN_D2H_LAYOUT_FRAME_ONLY, "layout frame-only");
	CK_EQ(b.trailer_present, 0, "no trailer");
	CK_EQ(b.chan_num, 11, "chan 11");
	CK_EQ(b.freq, 2462, "freq 2462");
}

static void test_hidden_ssid(void)
{
	u8 p[MAXP];
	u8 bssid[6] = { 2, 2, 2, 2, 2, 2 };
	u8 tr[HI5622_SCAN_D2H_TRAILER_LEN];
	struct hi5622_scan_d2h_bss b;
	size_t len;

	fill_trailer(tr, -70, 0);
	len = build_bss_event(p, 8, bssid, 0x0401, 100, 1, false, "",
			      tr);
	CK_EQ(hi5622_scan_d2h_parse_bss(p, (u32)len, NULL, &b), 0,
	      "hidden ssid beacon decodes");
	CK_EQ(b.ssid_len, 0, "hidden ssid length 0");
	CK(b.ssid_hidden, "hidden ssid flagged");
}

static void test_rejects(void)
{
	u8 p[MAXP];
	u8 bssid[6] = { 1, 1, 1, 1, 1, 1 };
	struct hi5622_scan_d2h_bss b;
	size_t len;

	/* NULL / too short */
	CK_EQ(hi5622_scan_d2h_parse_bss(NULL, 100, NULL, &b), -EINVAL,
	      "NULL payload rejected");
	CK_EQ(hi5622_scan_d2h_parse_bss(p, 0, NULL, &b), -EINVAL,
	      "zero length rejected");
	CK_EQ(hi5622_scan_d2h_parse_bss(p, 10, NULL, &b), -EOPNOTSUPP,
	      "short payload unsupported");

	/* management-only: data frame subtype rejected */
	len = build_bss_event(p, 8, bssid, 0, 100, 6, false, "x", NULL);
	p[0] = (u8)(p[0] | 0x08u);	/* subtype 8 -> type 2 (data) */
	CK_EQ(hi5622_scan_d2h_parse_bss(p, (u32)len, NULL, &b),
	      -EOPNOTSUPP, "data frame rejected");

	/* beacon with no channel IE at all */
	len = build_bss_event(p, 8, bssid, 0, 100, 6, false, "x", NULL);
	{
		/* strip the trailing DS IE: 36 fixed + 3 ssid IE + 3 DS IE */
		CK_EQ(hi5622_scan_d2h_parse_bss(p, (u32)(len - 3), NULL, &b),
		      -EOPNOTSUPP, "no channel IE rejected");
	}

	/* truncated IE chain (exactness required) */
	len = build_bss_event(p, 8, bssid, 0, 100, 6, false, "abcdef",
			      NULL);
	CK_EQ(hi5622_scan_d2h_parse_bss(p, (u32)(len - 2), NULL, &b),
	      -EOPNOTSUPP, "truncated IE chain rejected");
}

static void test_complete_and_chan(void)
{
	struct hi5622_scan_d2h_complete c;
	struct hi5622_scan_d2h_chan ch;
	struct hi5622_scan_d2h_sta_comp sc;
	u8 p[HI5622_SCAN_D2H_CHAN_PAYLOAD];
	u8 raw[4] = { 1, 0x10, 0x00, 0x00 };

	CK_EQ(hi5622_scan_d2h_parse_complete(raw, sizeof(raw), &c), 0,
	      "complete decodes");
	CK_EQ(c.status, 1, "complete status payload[0]");
	CK_EQ(c.raw_len, 4, "complete raw len");
	CK_EQ(c.payload_len, 4, "complete payload len");

	memset(p, 0, sizeof(p));
	p[0] = 7;			/* slot */
	p[4] = 36;			/* channel number */
	CK_EQ(hi5622_scan_d2h_parse_chan(p, sizeof(p), &ch), 0,
	      "chan result decodes");
	CK_EQ(ch.slot, 7, "chan slot");
	CK_EQ(ch.chan_num, 36, "chan number");
	CK(ch.is_5g, "chan 5 GHz flag");
	CK_EQ(ch.freq, 5180, "chan freq");
	CK_EQ(ch.block_len, HI5622_SCAN_D2H_CHAN_BLOCK, "chan block len");

	CK_EQ(hi5622_scan_d2h_parse_complete(NULL, 4, &c), -EINVAL,
	      "complete NULL rejected");
	CK_EQ(hi5622_scan_d2h_parse_complete(raw, 0, &c), -EINVAL,
	      "complete zero len rejected");
	CK_EQ(hi5622_scan_d2h_parse_chan(p, 3, &ch), -EOPNOTSUPP,
	      "chan short rejected");

	memset(p, 0, sizeof(p));
	p[0] = 0x12;
	p[1] = 0x01;
	p[3] = 0x00;
	p[0x0d] = 1;
	CK_EQ(hi5622_scan_d2h_parse_sta_comp(p, 16, &sc), 0,
	      "sta comp decodes");
	CK_EQ(sc.status, 1, "sta comp status payload[0xd]");
}

static void test_fuzz(void)
{
	u8 p[64];
	struct hi5622_scan_d2h_bss b;
	u32 i, j;
	int rc;
	u32 ok = 0;

	/* Deterministic LCG; no libc rand() so the test is reproducible. */
	u32 seed = 0x12345678u;

	for (i = 0; i < 4000; i++) {
		u32 len;

		for (j = 0; j < sizeof(p); j++) {
			seed = seed * 1664525u + 1013904223u;
			p[j] = (u8)(seed >> 24);
		}
		seed = seed * 1664525u + 1013904223u;
		len = 1 + (seed % sizeof(p));
		rc = hi5622_scan_d2h_parse_bss(p, len, NULL, &b);
		CK(rc == 0 || rc == -EOPNOTSUPP || rc == -EINVAL,
		   "fuzz: defined return code");
		if (rc == 0) {
			ok++;
			CK(b.ie_len <= HI5622_SCAN_D2H_IE_MAX,
			   "fuzz: ie_len bounded");
			CK(b.ssid_len <= HI5622_SCAN_D2H_SSID_MAX,
			   "fuzz: ssid_len bounded");
			CK(b.frame_len <= len, "fuzz: frame_len bounded");
		}
	}
	/* Sanity: random bytes should almost never validate. */
	CK(ok <= 8, "fuzz: accept rate is tiny");
}

int main(void)
{
	test_chan_freq();
	test_classify();
	test_beacon_2g_trailer();
	test_signal_optin();
	test_frame_only();
	test_hidden_ssid();
	test_rejects();
	test_complete_and_chan();
	test_fuzz();

	printf("scan_d2h_selftest: %d/%d checks %s\n", checks - failures,
	       checks, failures ? "FAIL" : "PASS");
	return failures ? 1 : 0;
}
