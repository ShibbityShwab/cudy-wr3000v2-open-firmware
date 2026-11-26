// SPDX-License-Identifier: GPL-2.0
/*
 * app_ie_selftest.c - host selftest for the portable H2D 0x20 app/vendor IE
 * model (lane wifi-app-ie, task-234).  No kernel, no device, no vendor bytes:
 * every frame quoted below is a decoded protocol frame (the stock capture
 * record, quoted as hex text) or host-synthesized bytes.
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_app_ie.h"

static unsigned int checks, fails;

static void ok(int cond, const char *what)
{
	checks++;
	if (!cond) {
		fails++;
		printf("FAIL: %s\n", what);
	}
}

static void hexdump(const char *tag, const u8 *b, size_t n)
{
	size_t i;

	printf("%s (%zu B):", tag, n);
	for (i = 0; i < n; i++)
		printf(" %02x", b[i]);
	printf("\n");
}

/* The vendor's captured stock frame (wifi-stock-h2d-census
 * evidence/NEW-FAMILIES.txt, g0 id32, 23 B, len == total, t=1.6 s):
 *   00 01 00 04 17 00 20 00 00 00 5a 5a | 00 00 00 00 | 04 00 00 00 | 00 00 00
 * Decoded: group 0 id 0x20, total 23, VAP/ctx index 4, ie_len 0, type 4,
 * mode 0, 3 zero tail bytes.  The builder must reproduce it byte for byte. */
static const u8 stock_frame[23] = {
	0x00, 0x01, 0x00, 0x04, 0x17, 0x00, 0x20, 0x00,
	0x00, 0x00, 0x5a, 0x5a, 0x00, 0x00, 0x00, 0x00,
	0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* A well-formed vendor IE: id 0xDD, len 7, OUI 00:50:F2, type 04, body 010203. */
static const u8 vendor_ie[9] = {
	0xdd, 0x07, 0x00, 0x50, 0xf2, 0x04, 0x01, 0x02, 0x03,
};

static void test_stock_frame(void)
{
	struct hi5622_app_ie_plan plan;
	u8 wire[HI5622_APP_IE_WIRE_MAX];
	size_t len = 0;
	int rc;

	rc = hi5622_app_ie_plan(1u, 1u, 1u, 0u, 4u, 4u, NULL, 0, &plan);
	ok(rc == 0, "stock: plan accepts the captured request");
	ok(plan.post, "stock: plan posts");
	rc = hi5622_app_ie_build(wire, sizeof(wire), &len, &plan);
	ok(rc == 0, "stock: build ok");
	ok(len == sizeof(stock_frame), "stock: 23-byte wire length");
	ok(len == 0 && rc == 0 ? 0 : memcmp(wire, stock_frame, sizeof(stock_frame)) == 0,
	   "stock: builder == captured stock frame (byte for byte)");
	hexdump("stock-rebuilt", wire, len);
	ok(memcmp(wire, stock_frame, sizeof(stock_frame)) == 0,
	   "stock: byte comparison (direct)");
}

static void test_vendor_ie_frame(void)
{
	struct hi5622_app_ie_plan plan;
	u8 wire[HI5622_APP_IE_WIRE_MAX];
	size_t len = 0;
	unsigned int vendor = 0;
	int rc;

	rc = hi5622_app_ie_plan(1u, 1u, 1u, 0u, 9u, 0u, vendor_ie,
			       sizeof(vendor_ie), &plan);
	ok(rc == 0, "vendor: plan accepts a 9-byte vendor IE");
	ok(plan.vendor_ies == 1, "vendor: one 0xDD IE counted");
	rc = hi5622_app_ie_build(wire, sizeof(wire), &len, &plan);
	ok(rc == 0, "vendor: build ok");
	ok(len == 23u + sizeof(vendor_ie), "vendor: wire = 23 + ie_len");
	/* header */
	ok(wire[0] == 0x00 && wire[1] == 0x01 && wire[2] == 0x00 && wire[3] == 0x00,
	   "vendor: header group/id/length/VAP bytes");
	ok(wire[4] == (u8)(len & 0xff) && wire[5] == (u8)(len >> 8),
	   "vendor: total field == wire length");
	ok(wire[6] == 0x20 && wire[7] == 0x00, "vendor: message id 0x20 LE");
	ok(wire[10] == 0x5a && wire[11] == 0x5a, "vendor: 0x5a5a tag");
	/* payload */
	ok(wire[12] == 9 && wire[13] == 0 && wire[14] == 0 && wire[15] == 0,
	   "vendor: u32 ie_len == 9");
	ok(wire[16] == 9, "vendor: type byte");
	ok(wire[17] == 0, "vendor: mode byte (path A)");
	ok(memcmp(wire + 20, vendor_ie, sizeof(vendor_ie)) == 0,
	   "vendor: IE bytes copied at payload+8");
	ok(wire[20 + sizeof(vendor_ie)] == 0 &&
	   wire[20 + sizeof(vendor_ie) + 1] == 0 &&
	   wire[20 + sizeof(vendor_ie) + 2] == 0,
	   "vendor: 3-byte zero tail");
	{
		size_t c_len = 0;

		ok(hi5622_app_ie_collect(wire + 20, sizeof(wire) - 20, &c_len,
					 &vendor, wire + 20,
					 sizeof(vendor_ie)) == 0,
		   "vendor: collect over the built payload");
		ok(vendor == 1 && c_len == sizeof(vendor_ie),
		   "vendor: collect round-trip");
	}
}

static void test_collect_sources(void)
{
	/* cfg80211 beacon tail / proberesp_ies / assocresp_ies carry whole IE
	 * lists; only the 0xDD vendor IEs are ours to inject. */
	static const u8 source[6 + 9 + 4 + 7] = {
		0x00, 0x04, 't', 'e', 's', 't',		/* SSID, skipped */
		0xdd, 0x07, 0x00, 0x50, 0xf2, 0x04, 0x01, 0x02, 0x03,
		0x2d, 0x02, 0x01, 0x0c,			/* HT op, skipped */
		0xdd, 0x05, 0x00, 0x11, 0x22, 0x33, 0x44,	/* 2nd vendor IE */
	};
	u8 out[HI5622_APP_IE_MAX_BYTES * 2];
	size_t out_len = 0;
	unsigned int vendor = 0;
	int rc;

	rc = hi5622_app_ie_collect(out, sizeof(out), &out_len, &vendor,
				   source, sizeof(source));
	ok(rc == 0, "collect: mixed list ok");
	ok(vendor == 2, "collect: exactly the two 0xDD IEs");
	ok(out_len == 9 + 7, "collect: 16 bytes copied");
	ok(memcmp(out, source + 6, 9) == 0, "collect: first vendor IE");
	ok(memcmp(out + 9, source + 6 + 9 + 4, 7) == 0, "collect: second vendor IE");
	ok(hi5622_app_ie_collect(out, sizeof(out), &out_len, &vendor, NULL, 0) == 0,
	   "collect: empty source no-op");
	{	/* malformed list: len field runs past the buffer */
		static const u8 bad[4] = { 0xdd, 0x09, 0x00, 0x00 };

		ok(hi5622_app_ie_collect(out, sizeof(out), &out_len, &vendor,
					 bad, sizeof(bad)) == -EBADMSG,
		   "collect: malformed TLV refused (-EBADMSG)");
	}
	{	/* destination too small: 9-byte IE, cap 8 */
		u8 tiny[8];
		size_t tl = 0;
		unsigned int tv = 0;

		ok(hi5622_app_ie_collect(tiny, sizeof(tiny), &tl, &tv,
					 vendor_ie, sizeof(vendor_ie)) == -ENOSPC,
		   "collect: short destination refused (-ENOSPC)");
	}
}

static void test_fail_closed(void)
{
	struct hi5622_app_ie_plan plan;
	u8 wire[HI5622_APP_IE_WIRE_MAX];
	u8 big[HI5622_APP_IE_MAX_BYTES + 8];
	size_t len = 0;
	int rc;

	ok(hi5622_app_ie_plan(2u, 1u, 1u, 0u, 4u, 0u, vendor_ie,
			      sizeof(vendor_ie), &plan) == -EINVAL,
	   "gate: app_ie=2 refused");
	ok(hi5622_app_ie_plan(1u, 0u, 1u, 0u, 4u, 0u, vendor_ie,
			      sizeof(vendor_ie), &plan) == -EINVAL,
	   "gate: app_ie=1 without beacon_static refused");
	ok(hi5622_app_ie_plan(1u, 1u, 0u, 0u, 4u, 0u, vendor_ie,
			      sizeof(vendor_ie), &plan) == -EINVAL,
	   "gate: app_ie=1 without vap_start_g0 refused");
	ok(hi5622_app_ie_plan(1u, 1u, 1u, 0u, 20u, 0u, vendor_ie,
			      sizeof(vendor_ie), &plan) == -EINVAL,
	   "gate: type 20 refused");
	ok(hi5622_app_ie_plan(1u, 1u, 1u, 0u, 4u, 256u, vendor_ie,
			      sizeof(vendor_ie), &plan) == -EINVAL,
	   "gate: VAP 256 refused");
	ok(hi5622_app_ie_plan(1u, 1u, 1u, 2u, 4u, 0u, vendor_ie,
			      sizeof(vendor_ie), &plan) == -EINVAL,
	   "gate: band 2 refused");
	memset(big, 0, sizeof(big));
	ok(hi5622_app_ie_plan(1u, 1u, 1u, 0u, 4u, 0u, big,
			      HI5622_APP_IE_MAX_BYTES + 1, &plan) == -EINVAL,
	   "gate: ie_len > 608 refused");
	{	/* well-formed TLV whose len field runs past the buffer */
		u8 bad[10];

		memset(bad, 0, sizeof(bad));
		bad[0] = 0xdd;
		bad[1] = 0x09;		/* needs 11 bytes, only 10 present */
		ok(hi5622_app_ie_plan(1u, 1u, 1u, 0u, 4u, 0u, bad,
				      sizeof(bad), &plan) == -EINVAL,
		   "gate: malformed IE list refused");
	}
	{	/* app_ie=0: plan ok, post false, build refuses */
		rc = hi5622_app_ie_plan(0u, 0u, 0u, 0u, 4u, 0u, NULL, 0, &plan);
		ok(rc == 0 && !plan.post, "gate: app_ie=0 off plan");
		ok(hi5622_app_ie_build(wire, sizeof(wire), &len, &plan) < 0,
		   "gate: builder never builds an unrequested frame");
	}
	{	/* clear rung: ie_len 0 is a valid "clear the slot" message */
		rc = hi5622_app_ie_plan(1u, 1u, 1u, 1u, 4u, 0u, NULL, 0, &plan);
		ok(rc == 0 && plan.post && plan.ie_len == 0,
		   "gate: ie_len 0 (clear) allowed");
	}
	ok(hi5622_app_ie_build(wire, 5, &len, &plan) < 0,
	   "builder: short buffer refused (-ENOSPC)");
}

static void test_cfg80211_composition(void)
{
	/* Model the driver's start_ap collection: hostapd appends its
	 * vendor_elements option to the beacon buffer and to the probe-response
	 * IE buffer (hostapd src/ap/ap_drv_ops.c add_buf(&beacon,...) /
	 * add_buf(&proberesp,...)); cfg80211 delivers them as
	 * beacon.tail/beacon_ies/proberesp_ies/assocresp_ies. */
	static const u8 tail[9] = { 0xdd, 0x07, 0x00, 0x50, 0xf2, 0x04, 0x01,
				    0x02, 0x03 };
	u8 blob[HI5622_APP_IE_MAX_BYTES];
	size_t blob_len = 0;
	unsigned int vendor = 0, i;
	struct hi5622_app_ie_plan plan;
	u8 wire[HI5622_APP_IE_WIRE_MAX];
	size_t len = 0;

	ok(hi5622_app_ie_collect(blob, sizeof(blob), &blob_len, &vendor,
				 tail, sizeof(tail)) == 0,
	   "compose: tail collected");
	ok(vendor == 1 && blob_len == 9, "compose: one vendor IE from the tail");
	ok(hi5622_app_ie_plan(1u, 1u, 1u, 0u, 4u, 0u, blob, blob_len, &plan) == 0,
	   "compose: plan ok");
	ok(hi5622_app_ie_build(wire, sizeof(wire), &len, &plan) == 0,
	   "compose: build ok");
	ok(len == 32, "compose: 32-byte wire (23 + 9)");
	ok(wire[16] == 4 && wire[20] == 0xdd, "compose: type/VAP/IE placement");
	/* decode the frame back: header field map */
	ok((wire[4] | (wire[5] << 8)) == 32, "compose: total decodes");
	ok((wire[6] | (wire[7] << 8)) == 0x20, "compose: id decodes");
	for (i = 0; i < 3; i++)
		ok(wire[len - 3 + i] == 0, "compose: tail pad zero");
}

static void test_status_line(void)
{
	char buf[160];
	int n;

	n = hi5622_app_ie_status_line(1u, 4u, 1u, 1u, 0, 3u, 0u, 9u, 1u,
				      buf, sizeof(buf));
	ok(n > 0, "status: line written");
	ok(strstr(buf, "param=1") && strstr(buf, "type=4") &&
	   strstr(buf, "posted=3") && strstr(buf, "vendor_ies=1"),
	   "status: fields present");
	n = hi5622_app_ie_status_line(0u, 4u, 0u, 0u, -ENODATA, 0u, 0u, 0u, 0u,
				      buf, sizeof(buf));
	ok(n > 0 && strstr(buf, "off"), "status: off mode");
}

int main(void)
{
	test_stock_frame();
	test_vendor_ie_frame();
	test_collect_sources();
	test_fail_closed();
	test_cfg80211_composition();
	test_status_line();
	printf("app_ie selftest: %u checks, %u failures\n", checks, fails);
	return fails ? 1 : 0;
}
