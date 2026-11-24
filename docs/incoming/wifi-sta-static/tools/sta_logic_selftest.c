/* SPDX-License-Identifier: GPL-2.0 */
/*
 * sta_logic_selftest.c - host-side assertions for the portable STA/band/HT
 * logic (wifi-sta-static lane, host-only).
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "../src/hi5622_sta_logic.h"

static int fails;

#define CHECK(cond, msg) do {						\
	if (!(cond)) {							\
		printf("FAIL: %s\n", msg);				\
		fails++;						\
	}								\
} while (0)

static void test_channels(void)
{
	const struct hi5622_sta_chan *c;
	u16 n = 0;

	c = hi5622_sta_band_channels(0, &n);
	CHECK(c != NULL && n == 14, "2.4 GHz band = 14 channels");
	CHECK(c[0].num == 1 && c[0].freq == 2412, "ch1 = 2412");
	CHECK(c[13].num == 14 && c[13].freq == 2484, "ch14 = 2484");

	c = hi5622_sta_band_channels(1, &n);
	CHECK(c != NULL && n == 29, "5 GHz band = 29 channels");
	CHECK(c[0].num == 36 && c[0].freq == 5180, "first 5G = ch36 5180");
	CHECK(c[7].num == 64 && c[7].freq == 5320, "ch64 = 5320");
	CHECK(c[19].num == 144 && c[19].freq == 5720, "ch144 present");
	CHECK(c[24].num == 165 && c[24].freq == 5825, "ch165 present");
	CHECK(c[28].num == 196 && c[28].freq == 4980, "4.9 GHz ch196 present");
	CHECK((c[4].flags & HI5622_STA_CHAN_RADAR) != 0, "ch52 radar flag");

	CHECK(hi5622_sta_band_channels(2, &n) == NULL && n == 0,
	      "band 2 rejected");
}

static void test_ht(void)
{
	CHECK(hi5622_sta_ht_cap_word(HI5622_STA_HT_MODE_BASELINE) == 0x0820u,
	      "baseline HT cap = SGI20|MAX_AMSDU");
	CHECK(hi5622_sta_ht_cap_word(HI5622_STA_HT_MODE_VENDOR20) == 0x0020u,
	      "vendor HT20 cap = SGI20");
	CHECK(hi5622_sta_ht_cap_word(HI5622_STA_HT_MODE_VENDOR40) == 0x0062u,
	      "vendor HT20/40 cap = 0x62 (SUP_WIDTH_20_40|SGI20|SGI40)");
	CHECK(hi5622_sta_ht_cap_word(9) == 0xffffu, "invalid HT mode refused");
	CHECK(hi5622_sta_ht_mode_is_40(HI5622_STA_HT_MODE_VENDOR40) == 1,
	      "mode 2 advertises 40 MHz");
	CHECK(hi5622_sta_ht_mode_is_40(HI5622_STA_HT_MODE_VENDOR20) == 0,
	      "mode 1 does not advertise 40 MHz");
	CHECK(hi5622_sta_vht_cap_word(0) == 0x00400000u,
	      "2.4 GHz vendor VHT cap = HTC_VHT (no width bits)");
	CHECK(hi5622_sta_vht_cap_word(1) == 0x00400064u,
	      "5 GHz vendor VHT cap = HTC_VHT|160 MHz|SGI80|SGI160");
	CHECK((hi5622_sta_vht_cap_word(1) & 0x00000004u) != 0,
	      "5 GHz VHT advertises 160 MHz");
	CHECK(hi5622_sta_vht_cap_word(2) == 0xffffffffu, "invalid band refused");
	CHECK(HI5622_STA_VHT_MCS_MAP == 0xfffau, "vendor VHT MCS map 0xfffa");
}

static void test_connect_check(void)
{
	u8 ssid[8] = { 't', 'e', 's', 't' };
	struct hi5622_sta_connect_check p;

	memset(&p, 0, sizeof(p));
	p.ssid = ssid;
	p.ssid_len = 4;
	p.auth_type = 0;
	CHECK(hi5622_sta_connect_check(&p) == 0, "minimal connect accepted");

	CHECK(hi5622_sta_auth_type_valid(0) && hi5622_sta_auth_type_valid(8),
	      "auth 0..8 valid");
	CHECK(!hi5622_sta_auth_type_valid(9), "auth 9 invalid");

	p.ssid_len = 0;
	CHECK(hi5622_sta_connect_check(&p) == -EINVAL, "ssid_len 0 refused");
	p.ssid_len = 33;
	CHECK(hi5622_sta_connect_check(&p) == -EINVAL, "ssid_len 33 refused");
	p.ssid_len = 4;
	p.auth_type = 9;
	CHECK(hi5622_sta_connect_check(&p) == -EINVAL, "auth 9 refused");
	p.auth_type = 0;
	p.ie_len = 513;
	CHECK(hi5622_sta_connect_check(&p) == -E2BIG, "ie_len 513 refused");
	p.ie_len = 512;
	p.key_len = 33;
	CHECK(hi5622_sta_connect_check(&p) == -E2BIG, "key_len 33 refused");
	p.key_len = 32;
	CHECK(hi5622_sta_connect_check(&p) == 0, "max sizes accepted");
	CHECK(hi5622_sta_connect_check(NULL) == -EINVAL, "NULL refused");
}

int main(void)
{
	test_channels();
	test_ht();
	test_connect_check();
	if (fails) {
		printf("sta_logic_selftest: FAIL %d check(s)\n", fails);
		return 1;
	}
	printf("sta_logic_selftest: PASS (2.4G 14ch / 5G 29ch tables, HT 20/40 words, VHT map, connect bounds)\n");
	return 0;
}
