// SPDX-License-Identifier: GPL-2.0
/*
 * rx_parity_selftest.c - host-only checks for the wifi-rx-vendor-diff lane
 * (shared task task-162): the portable vendor-vs-ours RX state parity tables,
 * the host-MAC status-bit decode, the read-only completion observation rules
 * and the vendor RX-init program parity check.
 *
 * Strict C99, no kernel, no device, no vendor data.  Build:
 *   gcc -std=c99 -Wall -Wextra -Werror -pedantic -I src \
 *       -o /tmp/rx_parity_selftest src/rx_parity_selftest.c \
 *       src/hi5622_rx_diff.c src/hi5622_rx_live.c src/dp_rings.c
 */
#include <stdio.h>
#include <string.h>

#include "hi5622_rx_diff.h"
#include "dp_rings.h"

static int g_checks;
static int g_fail;

#define CHECK(cond, what) do { \
	g_checks++; \
	if (!(cond)) { \
		g_fail++; \
		printf("FAIL %3d: %s\n", g_checks, (what)); \
	} \
} while (0)

#define CHECK_EQ(a, b, what) do { \
	long long _a = (long long)(a), _b = (long long)(b); \
	g_checks++; \
	if (_a != _b) { \
		g_fail++; \
		printf("FAIL %3d: %s (got %lld want %lld)\n", \
		       g_checks, (what), _a, _b); \
	} \
} while (0)

static int is_one_of(rx_u16 v, const rx_u16 *set, unsigned int n)
{
	unsigned int i;

	for (i = 0; i < n; i++)
		if (set[i] == v)
			return 1;
	return 0;
}

/* --- 1. vendor stock tables ------------------------------------------- */
static void test_tables(void)
{
	const struct hi5622_rx_word *cfg, *live;
	unsigned int ncfg = 0, nlive = 0, i, j, matches = 0;
	static const rx_u16 fixed[] = { 0x00, 0x1c, 0x2c, 0x30, 0x34, 0x48 };
	static const rx_u16 dev_owned[] = { 0x14, 0x24, 0x28, 0x2c, 0x30 };

	cfg = hi5622_rx_cfg_words(&ncfg);
	live = hi5622_rx_live_words(&nlive);
	CHECK(cfg != NULL && ncfg == 21u, "config table count 21");
	CHECK(live != NULL && nlive == 17u, "live table count 17");

	/* literal vendor stock anchors (reg_all.txt) */
	for (i = 0; i < ncfg; i++) {
		if (cfg[i].off == 0x1cu)
			CHECK_EQ(cfg[i].v, 0x00b0063cu, "stock cfg +0x1c = 0x00b0063c");
		if (cfg[i].off == 0x48u)
			CHECK_EQ(cfg[i].v, 0xfffffcc1u, "stock cfg +0x48 = 0xfffffcc1");
		if (cfg[i].off == 0x2cu)
			CHECK_EQ(cfg[i].v, 0x100u, "stock cfg +0x2c = 256");
		if (cfg[i].off == 0x30u)
			CHECK_EQ(cfg[i].v, 0x200u, "stock cfg +0x30 = 512");
		if (cfg[i].off == 0x34u)
			CHECK_EQ(cfg[i].v, 0xfffu, "stock cfg +0x34 = 4095");
	}
	/* no duplicate offsets */
	for (i = 0; i < ncfg; i++)
		for (j = i + 1; j < ncfg; j++)
			CHECK(cfg[i].off != cfg[j].off, "config offsets unique");
	for (i = 0; i < nlive; i++)
		for (j = i + 1; j < nlive; j++)
			CHECK(live[i].off != live[j].off, "live offsets unique");

	/* the six fixed host-written words are present and classified MATCH */
	for (i = 0; i < ncfg; i++) {
		if (!is_one_of(cfg[i].off, fixed,
			       (unsigned int)(sizeof(fixed) / sizeof(fixed[0]))))
			continue;
		CHECK(cfg[i].kind == HIRD_KIND_MATCH,
		      "fixed cfg word classified MATCH");
		matches++;
	}
	CHECK_EQ(matches, 6, "six fixed config words");

	/* device-owned live-bank offsets exactly the vendor-nonzero set */
	matches = 0;
	for (i = 0; i < nlive; i++) {
		if (live[i].kind != HIRD_KIND_DEVICE)
			continue;
		matches++;
		CHECK(is_one_of(live[i].off, dev_owned,
				(unsigned int)(sizeof(dev_owned) /
					       sizeof(dev_owned[0]))),
		      "device-owned live word in the required set");
		CHECK(live[i].v != 0u, "device-owned live word vendor value nonzero");
	}
	CHECK_EQ(matches, HIRD_LIVE_EXTRA_COUNT, "five device-owned live words");
	CHECK_EQ(hi5622_rx_live_extra_offsets[0], 0x14u, "extra[0] = +0x14");
	CHECK_EQ(hi5622_rx_live_extra_offsets[4], 0x30u, "extra[4] = +0x30");
	/* literal vendor idle values */
	for (i = 0; i < nlive; i++) {
		if (live[i].off == 0x14u)
			CHECK_EQ(live[i].v, 0x00200020u, "live +0x14 stock 0x00200020");
		if (live[i].off == 0x24u)
			CHECK_EQ(live[i].v, 0x00800000u, "live +0x24 stock 0x00800000");
		if (live[i].off == 0x28u)
			CHECK_EQ(live[i].v, 0x01800800u, "live +0x28 stock 0x01800800");
		if (live[i].off == 0x2cu)
			CHECK_EQ(live[i].v, 0x02801000u, "live +0x2c stock 0x02801000");
		if (live[i].off == 0x30u)
			CHECK_EQ(live[i].v, 0x03801800u, "live +0x30 stock 0x03801800");
	}
	CHECK_EQ(HIRD_CFG_CA, 0x40040000u, "config block CA");
	CHECK_EQ(HIRD_LIVE_CA, 0x40050000u, "live block CA");
}

/* --- 2. parity classification ----------------------------------------- */
static void test_classify(void)
{
	struct hi5622_rx_word w;
	enum hi5622_rx_parity p;
	int rc;

	memset(&w, 0, sizeof(w));
	w.kind = HIRD_KIND_MATCH;
	w.v = 0xfffffcc1u;
	rc = hi5622_rx_parity_classify(&w, 0xfffffcc1u, 0, &p);
	CHECK_EQ(rc, HIL_RX_OK, "classify MATCH rc");
	CHECK_EQ(p, HIRD_PAR_MATCH, "classify MATCH");
	rc = hi5622_rx_parity_classify(&w, 0x00000001u, 0, &p);
	CHECK_EQ(p, HIRD_PAR_MISMATCH, "classify MISMATCH");
	rc = hi5622_rx_parity_classify(&w, 0xffffffffu, 0, &p);
	CHECK_EQ(p, HIRD_PAR_READ_FAIL, "classify transport miss");

	w.kind = HIRD_KIND_RANGE;
	rc = hi5622_rx_parity_classify(&w, 0x844c7000u, 1, &p);
	CHECK_EQ(p, HIRD_PAR_RANGE_OK, "classify RANGE_OK");
	rc = hi5622_rx_parity_classify(&w, 0x844c7000u, 0, &p);
	CHECK_EQ(p, HIRD_PAR_RANGE_BAD, "classify RANGE_BAD");

	w.kind = HIRD_KIND_DYNAMIC;
	rc = hi5622_rx_parity_classify(&w, 0x00008000u, 0, &p);
	CHECK_EQ(p, HIRD_PAR_DYNAMIC, "classify DYNAMIC");

	w.kind = HIRD_KIND_DEVICE;
	w.v = 0x00200020u;
	rc = hi5622_rx_parity_classify(&w, 0x00200020u, 0, &p);
	CHECK_EQ(p, HIRD_PAR_DEVICE_OK, "classify DEVICE_OK");
	rc = hi5622_rx_parity_classify(&w, 0x00000000u, 0, &p);
	CHECK_EQ(p, HIRD_PAR_DEVICE_ZERO, "classify DEVICE_ZERO");
	rc = hi5622_rx_parity_classify(&w, 0x12345678u, 0, &p);
	CHECK_EQ(p, HIRD_PAR_DEVICE_OK, "classify DEVICE non-zero variant");

	w.kind = HIRD_KIND_UNKNOWN;
	w.v = 0x00000004u;
	rc = hi5622_rx_parity_classify(&w, 0x00000004u, 0, &p);
	CHECK_EQ(p, HIRD_PAR_MATCH, "classify UNKNOWN match");
	rc = hi5622_rx_parity_classify(&w, 0x00000000u, 0, &p);
	CHECK_EQ(p, HIRD_PAR_MISMATCH, "classify UNKNOWN mismatch");
	CHECK_EQ(hi5622_rx_parity_classify(NULL, 0, 0, &p), HIL_RX_EARG,
		 "classify NULL word refused");
	CHECK_EQ(hi5622_rx_parity_classify(&w, 0, 0, NULL), HIL_RX_EARG,
		 "classify NULL out refused");
	CHECK(strcmp(hi5622_rx_parity_name(HIRD_PAR_DEVICE_ZERO),
		     "DEVICE_ZERO") == 0, "parity name DEVICE_ZERO");
	CHECK(strcmp(hi5622_rx_parity_name(HIRD_PAR_RANGE_BAD),
		     "RANGE_BAD") == 0, "parity name RANGE_BAD");
}

/* --- 3. host-MAC interrupt status decode ------------------------------ */
static void test_status(void)
{
	struct hi5622_rx_status_dec d;
	char bits[112];
	int rc, n;

	/* vendor stock idle: 0x400, mask 0xfffffcc1 -> all masked away */
	rc = hi5622_rx_status_decode(0x00000400u, 0xfffffcc1u, &d);
	CHECK_EQ(rc, HIL_RX_OK, "decode vendor idle rc");
	CHECK_EQ(d.active, 0u, "vendor idle observable bits = 0");
	CHECK_EQ(d.rx_active, 0u, "vendor idle rx bit inactive");

	/* our bench arm status: 0x182 -> observable bits 1 and 8, both the
	 * vendor's firmware-forward class; RX bit 2 inactive */
	rc = hi5622_rx_status_decode(0x00000182u, 0xfffffcc1u, &d);
	CHECK_EQ(d.active, 0x00000102u, "our status active = 0x102");
	CHECK_EQ(d.rx_active, 0u, "our status rx bit inactive");
	CHECK_EQ(d.forward_bits, 0x00000102u, "our status forward bits = 0x102");
	n = hi5622_rx_status_bits_str(&d, bits, sizeof(bits));
	CHECK(n > 0, "bits str rendered");
	CHECK(strstr(bits, "bits=1,8") != NULL, "bits str lists 1,8");
	CHECK(strstr(bits, "fwd=0x102") != NULL, "bits str fwd mask");

	/* synthetic RX-pending status: bit 2 observable */
	rc = hi5622_rx_status_decode(0x00000002u | 0x00000004u, 0xfffffcc1u,
				     &d);
	CHECK_EQ(d.rx_active, 1u, "synthetic rx bit observable");
	CHECK_EQ(d.active, 0x00000006u, "synthetic active 0x6");

	/* everything masked: active must be 0 */
	rc = hi5622_rx_status_decode(0x0000ffffu, 0x0000ffffu, &d);
	CHECK_EQ(d.active, 0u, "all-masked status");
	CHECK_EQ(hi5622_rx_status_decode(0, 0, NULL), HIL_RX_EARG,
		 "decode NULL refused");
	CHECK(hi5622_rx_status_bit_name(2u) != NULL &&
	      strcmp(hi5622_rx_status_bit_name(2u), "rx-completion") == 0,
	      "bit 2 named rx-completion");
	CHECK(hi5622_rx_status_bit_name(9u) != NULL &&
	      strcmp(hi5622_rx_status_bit_name(9u), "forward-core") == 0,
	      "bit 9 named forward-core");
	CHECK(hi5622_rx_status_bit_name(16u) == NULL, "bit 16 name refused");
}

/* --- 4. read-only observation rules ----------------------------------- */
static void test_obs(void)
{
	rx_u32 norm[4] = { 0x81000000u, 0x81001000u, 0x81002000u, 0x81003000u };
	rx_u32 small[2] = { 0x82000000u, 0x82001000u };
	rx_u8 k = 0xffu;
	rx_u32 slot = 0xdeadbeefu, dma = 0u;
	int rc;

	rc = hi5622_rx_obs_entry(norm[2] + 4u, norm, 4u, small, 2u,
				 &k, &slot, &dma);
	CHECK_EQ(rc, HIRD_OBS_FRAME, "obs norm entry found");
	CHECK_EQ(k, 0u, "obs norm kind");
	CHECK_EQ(slot, 2u, "obs norm slot");
	CHECK_EQ(dma, norm[2], "obs norm dma");

	rc = hi5622_rx_obs_entry(small[1] + 4u, norm, 4u, small, 2u,
				 &k, &slot, &dma);
	CHECK_EQ(rc, HIRD_OBS_FRAME, "obs small entry found");
	CHECK_EQ(k, 1u, "obs small kind");
	CHECK_EQ(slot, 1u, "obs small slot");
	CHECK_EQ(dma, small[1], "obs small dma");

	rc = hi5622_rx_obs_entry(norm[0] + 2u, norm, 4u, small, 2u,
				 &k, &slot, &dma);
	CHECK_EQ(rc, HIRD_OBS_BAD_ENTRY, "obs misaligned entry refused");
	rc = hi5622_rx_obs_entry(0x99999999u, norm, 4u, small, 2u,
				 &k, &slot, &dma);
	CHECK_EQ(rc, HIRD_OBS_BAD_ENTRY, "obs unknown entry refused");
	rc = hi5622_rx_obs_entry(norm[3] + 4u, norm, 3u, small, 2u,
				 &k, &slot, &dma);
	CHECK_EQ(rc, HIRD_OBS_BAD_ENTRY, "obs slot at/above depth refused");
	/* a NULL norm pool cannot validate norm entries; the small pool is used */
	CHECK_EQ(hi5622_rx_obs_entry(norm[0] + 4u, NULL, 4u, small, 2u,
				     &k, &slot, &dma), HIRD_OBS_BAD_ENTRY,
		 "obs norm NULL pool: norm entry not accepted");
	CHECK_EQ(hi5622_rx_obs_entry(small[0] + 4u, NULL, 4u, small, 2u,
				     &k, &slot, &dma), HIRD_OBS_FRAME,
		 "obs norm NULL pool: small entry still found");
	rc = hi5622_rx_obs_entry(norm[0] + 4u, NULL, 0u, small, 2u,
				 &k, &slot, &dma);
	CHECK_EQ(rc, HIRD_OBS_BAD_ENTRY, "obs norm NULL pool refused");
	CHECK_EQ(hi5622_rx_obs_entry(0, norm, 4u, small, 2u, NULL, &slot,
				     &dma), HIL_RX_EARG,
		 "obs NULL ring_kind refused");
}

/* --- 5. vendor RX-init program parity --------------------------------- */
static struct dp_mac_params mk_params(void)
{
	struct dp_mac_params p;

	memset(&p, 0, sizeof(p));
	p.msdu_table_acp = 0x02060850u;
	p.rx_norm_addr = 0x844c7000u;
	p.rx_small_addr = 0x844c8000u;
	p.rx_cmp_addr = 0x83734000u;
	p.txba_addr = 0x83fd7000u;
	p.txba_depth = 0x100u;
	p.rx_norm_len = 0x63cu;
	p.rx_small_len = 0xb0u;
	p.rx_norm_depth = 0x100u;
	p.rx_small_depth = 0x200u;
	p.rx_cmp_depth = 0xfffu;
	p.int_mask = 0xfffffcc1u;
	return p;
}

static void test_init_program(void)
{
	struct dp_mac_params p = mk_params();
	struct dp_mac_reg regs[16];
	struct hi5622_rx_init_check ck;
	unsigned int count = 0;
	int rc;

	rc = dp_mac_program(&p, regs, 16u, &count);
	CHECK_EQ(rc, DP_EOK, "dp_mac_program rc");
	CHECK_EQ(count, DP_MAC_REG_COUNT, "14-word vendor order program");
	rc = hi5622_rx_init_program_check(regs, count, &ck);
	CHECK_EQ(rc, HIL_RX_OK, "vendor init program parity OK");
	CHECK_EQ(ck.missing_mask, 0u, "no missing init words");
	CHECK(ck.fail_name == NULL, "no failing init word");

	/* corrupt one fixed value: +0x34 completion depth */
	for (unsigned int i = 0; i < count; i++) {
		if (regs[i].off == 0x34u) {
			regs[i].value = 0x100u;
			break;
		}
	}
	rc = hi5622_rx_init_program_check(regs, count, &ck);
	CHECK_EQ(rc, HIL_RX_EFORMAT, "corrupted depth refused");
	CHECK(ck.fail_name != NULL, "corrupted depth names the word");
	CHECK_EQ(ck.bad_value, 0x100u, "corrupted depth value recorded");
	CHECK((ck.missing_mask & (1u << 4)) != 0u,
	      "corrupted depth missing bit 4 (+0x34)");

	/* restore and drop an address word: +0x20 norm ring addr */
	regs[4].value = 0x00000fffu;
	rc = dp_mac_program(&p, regs, 16u, &count);
	CHECK_EQ(rc, DP_EOK, "program re-built");
	{
		struct dp_mac_reg tmp[16];
		unsigned int n = 0, i;

		for (i = 0; i < count; i++)
			if (regs[i].off != 0x20u)
				tmp[n++] = regs[i];
		rc = hi5622_rx_init_program_check(tmp, n, &ck);
		CHECK_EQ(rc, HIL_RX_EFORMAT, "missing +0x20 refused");
		CHECK_EQ(ck.missing_mask & (1u << (HIRD_INIT_FIXED_COUNT + 2u)),
			 1u << (HIRD_INIT_FIXED_COUNT + 2u),
			 "missing +0x20 flagged");
	}
	CHECK_EQ(hi5622_rx_init_program_check(NULL, 0, &ck), HIL_RX_EARG,
		 "NULL program refused");
}

int main(void)
{
	test_tables();
	test_classify();
	test_status();
	test_obs();
	test_init_program();
	printf("%d checks, %d failures\n", g_checks, g_fail);
	if (g_fail == 0)
		printf("RESULT: PASS\n");
	return g_fail == 0 ? 0 : 1;
}
