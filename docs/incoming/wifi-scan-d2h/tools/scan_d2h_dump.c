// SPDX-License-Identifier: GPL-2.0
/*
 * scan_d2h_dump.c - deterministic field dump for test-vector validation.
 *
 * ORIGINAL clean-room code (lane wifi-scan-d2h, task-118).  Reads a hex string
 * from argv and prints the decoded fields as key=value lines so that
 * tools/gen_vectors.py can cross-check the testvectors JSON against the C
 * decoder that the kernel module builds.
 *
 *   scan_d2h_dump bss       HEX
 *   scan_d2h_dump complete  HEX
 *   scan_d2h_dump chan      HEX
 *   scan_d2h_dump sta       HEX
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hi5622_scan_d2h.h"

static int from_hex(const char *hex, u8 *buf, size_t cap, size_t *out_len)
{
	size_t n = strlen(hex), i;
	int hi = 0;

	if (n & 1u)
		return -1;
	n /= 2;
	if (n > cap)
		return -1;
	for (i = 0; i < n; i++) {
		int a, b;
		char ca = hex[2 * i], cb = hex[2 * i + 1];

		a = (ca >= '0' && ca <= '9') ? ca - '0' :
		    (ca >= 'a' && ca <= 'f') ? ca - 'a' + 10 : -1;
		b = (cb >= '0' && cb <= '9') ? cb - '0' :
		    (cb >= 'a' && cb <= 'f') ? cb - 'a' + 10 : -1;
		if (a < 0 || b < 0)
			return -1;
		buf[i] = (u8)((a << 4) | b);
		hi += b;
	}
	(void)hi;
	*out_len = n;
	return 0;
}

int main(int argc, char **argv)
{
	static u8 buf[2048];
	size_t len = 0;
	const char *what;

	if (argc != 3) {
		fprintf(stderr, "usage: %s bss|complete|chan|sta HEX\n", argv[0]);
		return 2;
	}
	what = argv[1];
	if (from_hex(argv[2], buf, sizeof(buf), &len)) {
		fprintf(stderr, "bad hex\n");
		return 2;
	}
	if (!strcmp(what, "bss")) {
		struct hi5622_scan_d2h_bss b;
		struct hi5622_scan_d2h_opts o = { true, true };
		int rc = hi5622_scan_d2h_parse_bss(buf, (u32)len, &o, &b);
		int i;

		printf("rc=%d\n", rc);
		if (rc)
			return 0;
		printf("layout=%u\n", b.layout);
		printf("frame_type=%u\n", b.frame_type);
		printf("bssid=");
		for (i = 0; i < 6; i++)
			printf("%02x", b.bssid[i]);
		printf("\n");
		printf("ssid_len=%u\n", b.ssid_len);
		printf("ssid=");
		for (i = 0; i < b.ssid_len; i++)
			printf("%02x", b.ssid[i]);
		printf("\n");
		printf("chan=%u\nis_5g=%u\nfreq=%u\n", b.chan_num,
		       b.is_5g ? 1u : 0u, b.freq);
		printf("interval=%u\ncapability=%u\ntsf=%llu\n",
		       b.beacon_interval, b.capability,
		       (unsigned long long)b.tsf);
		printf("phy=%u\ntrailer=%u\nsignal_mbm=%d\nie_len=%u\n",
		       b.phy_mode, b.trailer_present, (int)b.signal_mbm,
		       b.ie_len);
	} else if (!strcmp(what, "complete")) {
		struct hi5622_scan_d2h_complete c;

		printf("rc=%d\n", hi5622_scan_d2h_parse_complete(buf, (u32)len,
								 &c));
		printf("status=%u\n", c.status);
	} else if (!strcmp(what, "chan")) {
		struct hi5622_scan_d2h_chan ch;

		printf("rc=%d\n", hi5622_scan_d2h_parse_chan(buf, (u32)len, &ch));
		printf("slot=%u\nchan=%u\nis_5g=%u\nfreq=%u\nblock_len=%u\n",
		       ch.slot, ch.chan_num, ch.is_5g ? 1u : 0u, ch.freq,
		       ch.block_len);
	} else if (!strcmp(what, "sta")) {
		struct hi5622_scan_d2h_sta_comp s;

		printf("rc=%d\n", hi5622_scan_d2h_parse_sta_comp(buf,
								 (u32)len, &s));
		printf("status=%u\ncore_band=%u\nchip_id=%u\n",
		       s.status, s.core_band, s.chip_id);
	} else {
		fprintf(stderr, "unknown kind %s\n", what);
		return 2;
	}
	return 0;
}
