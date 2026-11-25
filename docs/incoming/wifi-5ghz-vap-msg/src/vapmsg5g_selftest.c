/* SPDX-License-Identifier: GPL-2.0 */
/*
 * vapmsg5g_selftest.c - host selftest for the 5 GHz AP vendor-order planner
 * (lane wifi-5ghz-vap-msg, shared task task-174).
 *
 * Build+run: tools/run-host-tests.sh (gcc, no kernel, no device).
 */
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "hi5622_vapmsg5g.h"

static int checks;
static int failures;

#define CHECK(cond) do { \
	checks++; \
	if (!(cond)) { \
		failures++; \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
	} \
} while (0)

#define CHECK_EQ(a, b) CHECK((long)(a) == (long)(b))

static void test_names(void)
{
	CHECK(!strcmp(hi5622_vapmsg5g_mode_name(HI5622_VAPMSG5G_MODE_LEGACY),
		     "legacy"));
	CHECK(!strcmp(hi5622_vapmsg5g_mode_name(HI5622_VAPMSG5G_MODE_VENDOR_ORDER),
		     "vendor-order"));
	CHECK(!strcmp(hi5622_vapmsg5g_mode_name(2u), "invalid"));
	CHECK(!strcmp(hi5622_vapmsg5g_step_name(HI5622_VAPMSG5G_STEP_VAP_ADD),
		     "vap_add"));
	CHECK(!strcmp(hi5622_vapmsg5g_step_name(HI5622_VAPMSG5G_STEP_USER_ADD),
		     "user_add"));
	CHECK(!strcmp(hi5622_vapmsg5g_step_name(HI5622_VAPMSG5G_STEP_CHAN_CFG),
		     "ap_chan"));
	CHECK(!strcmp(hi5622_vapmsg5g_step_name(0xffu), "invalid"));
}

static void test_legacy_default(void)
{
	struct hi5622_vapmsg5g_plan plan = { true, true };
	u8 steps[HI5622_VAPMSG5G_STEP_MAX];
	unsigned int n = 0;

	/* vap5g=0 on the exact one-radio/two-band AP shape: no change. */
	CHECK_EQ(hi5622_vapmsg5g_iface_plan(HI5622_VAPMSG5G_MODE_LEGACY, 2u,
					   HI5622_VAPMSG5G_IFTYPE_AP, &plan), 0);
	CHECK(!plan.defer_user_add);
	CHECK(!plan.post_user_after_vap_add);
	CHECK_EQ(hi5622_vapmsg5g_order(HI5622_VAPMSG5G_MODE_LEGACY, 2u,
				      HI5622_VAPMSG5G_IFTYPE_AP, steps, 8u, &n), 0);
	CHECK_EQ(n, 8u);
	/* Legacy: the interface-creation user add precedes the VAP add. */
	CHECK_EQ(steps[0], HI5622_VAPMSG5G_STEP_USER_ADD);
	CHECK_EQ(steps[1], HI5622_VAPMSG5G_STEP_USER_ADD_NOTIFY);
	CHECK_EQ(steps[2], HI5622_VAPMSG5G_STEP_VAP_ADD);
	CHECK_EQ(steps[3], HI5622_VAPMSG5G_STEP_CHAN_CFG);
	CHECK_EQ(steps[4], HI5622_VAPMSG5G_STEP_SSID);
	CHECK_EQ(steps[5], HI5622_VAPMSG5G_STEP_AUTH);
	CHECK_EQ(steps[6], HI5622_VAPMSG5G_STEP_AP_CFG);
	CHECK_EQ(steps[7], HI5622_VAPMSG5G_STEP_VAP_START);
}

static void test_vendor_order(void)
{
	struct hi5622_vapmsg5g_plan plan = { false, false };
	u8 steps[HI5622_VAPMSG5G_STEP_MAX];
	unsigned int n = 0;

	CHECK_EQ(hi5622_vapmsg5g_iface_plan(HI5622_VAPMSG5G_MODE_VENDOR_ORDER, 2u,
					   HI5622_VAPMSG5G_IFTYPE_AP, &plan), 0);
	CHECK(plan.defer_user_add);
	CHECK(plan.post_user_after_vap_add);
	CHECK_EQ(hi5622_vapmsg5g_order(HI5622_VAPMSG5G_MODE_VENDOR_ORDER, 2u,
				      HI5622_VAPMSG5G_IFTYPE_AP, steps, 8u, &n), 0);
	CHECK_EQ(n, 8u);
	/* Vendor: VAP create first, then its user add, then the five frames. */
	CHECK_EQ(steps[0], HI5622_VAPMSG5G_STEP_VAP_ADD);
	CHECK_EQ(steps[1], HI5622_VAPMSG5G_STEP_USER_ADD);
	CHECK_EQ(steps[2], HI5622_VAPMSG5G_STEP_USER_ADD_NOTIFY);
	CHECK_EQ(steps[3], HI5622_VAPMSG5G_STEP_CHAN_CFG);
	CHECK_EQ(steps[4], HI5622_VAPMSG5G_STEP_SSID);
	CHECK_EQ(steps[5], HI5622_VAPMSG5G_STEP_AUTH);
	CHECK_EQ(steps[6], HI5622_VAPMSG5G_STEP_AP_CFG);
	CHECK_EQ(steps[7], HI5622_VAPMSG5G_STEP_VAP_START);

	/* And exactly one user add still appears (no duplicate). */
	CHECK_EQ((long)steps[1] == (long)HI5622_VAPMSG5G_STEP_USER_ADD &&
		 (long)steps[2] == (long)HI5622_VAPMSG5G_STEP_USER_ADD_NOTIFY, 1L);
}

static void test_fail_closed_matrix(void)
{
	struct hi5622_vapmsg5g_plan plan;
	u8 steps[HI5622_VAPMSG5G_STEP_MAX];
	unsigned int n = 0;
	unsigned int bands, iftype, mode;

	/* Single-band wiphy: legacy, whatever the mode and iftype. */
	for (iftype = 0; iftype <= 8u; iftype++) {
		CHECK_EQ(hi5622_vapmsg5g_iface_plan(
				 HI5622_VAPMSG5G_MODE_VENDOR_ORDER, 1u, iftype,
				 &plan), 0);
		CHECK(!plan.defer_user_add);
		CHECK(!plan.post_user_after_vap_add);
	}

	/* Multi-band wiphy, non-AP iftypes: legacy. */
	for (iftype = 0; iftype <= 8u; iftype++) {
		if (iftype == HI5622_VAPMSG5G_IFTYPE_AP)
			continue;
		CHECK_EQ(hi5622_vapmsg5g_iface_plan(
				 HI5622_VAPMSG5G_MODE_VENDOR_ORDER, 2u, iftype,
				 &plan), 0);
		CHECK(!plan.defer_user_add);
		CHECK(!plan.post_user_after_vap_add);
	}

	/* Wiphy band count 0 (not registered yet): legacy. */
	CHECK_EQ(hi5622_vapmsg5g_iface_plan(HI5622_VAPMSG5G_MODE_VENDOR_ORDER,
					   0u, HI5622_VAPMSG5G_IFTYPE_AP, &plan), 0);
	CHECK(!plan.defer_user_add);

	/* Out-of-range mode is refused (never guessed). */
	CHECK_EQ(hi5622_vapmsg5g_iface_plan(2u, 2u, HI5622_VAPMSG5G_IFTYPE_AP,
					   &plan), -EINVAL);
	CHECK_EQ(hi5622_vapmsg5g_iface_plan(0xffffffffu, 2u,
					   HI5622_VAPMSG5G_IFTYPE_AP, &plan), -EINVAL);
	CHECK_EQ(hi5622_vapmsg5g_order(2u, 2u, HI5622_VAPMSG5G_IFTYPE_AP,
				      steps, 8u, &n), -EINVAL);

	/* NULL / short buffer. */
	CHECK_EQ(hi5622_vapmsg5g_iface_plan(0u, 2u, HI5622_VAPMSG5G_IFTYPE_AP,
					   NULL), -EINVAL);
	CHECK_EQ(hi5622_vapmsg5g_order(0u, 2u, HI5622_VAPMSG5G_IFTYPE_AP,
				      NULL, 8u, &n), -EINVAL);
	CHECK_EQ(hi5622_vapmsg5g_order(0u, 2u, HI5622_VAPMSG5G_IFTYPE_AP,
				      steps, 8u, NULL), -EINVAL);
	CHECK_EQ(hi5622_vapmsg5g_order(0u, 2u, HI5622_VAPMSG5G_IFTYPE_AP,
				      steps, 7u, &n), -ENOSPC);

	/* Mode non-regression: for every (bands, iftype) mode 1 and mode 0
	 * agree EXCEPT the one-radio/two-band AP. */
	for (mode = 0u; mode <= 1u; mode++) {
		for (bands = 0u; bands <= 3u; bands++) {
			for (iftype = 0; iftype <= 8u; iftype++) {
				struct hi5622_vapmsg5g_plan p0, p1;

				CHECK_EQ(hi5622_vapmsg5g_iface_plan(0u, bands,
							iftype, &p0), 0);
				CHECK_EQ(hi5622_vapmsg5g_iface_plan(1u, bands,
							iftype, &p1), 0);
				if (bands > 1u &&
				    iftype == HI5622_VAPMSG5G_IFTYPE_AP) {
					CHECK(p1.defer_user_add);
					CHECK(p1.post_user_after_vap_add);
				} else {
					CHECK_EQ(p0.defer_user_add,
						 p1.defer_user_add);
					CHECK_EQ(p0.post_user_after_vap_add,
						 p1.post_user_after_vap_add);
					CHECK(!p1.defer_user_add);
				}
			}
		}
	}
}

int main(void)
{
	test_names();
	test_legacy_default();
	test_vendor_order();
	test_fail_closed_matrix();
	printf("%s: %d checks, %d failure(s)\n",
	       failures ? "FAIL" : "PASS", checks, failures);
	return failures ? 1 : 0;
}
